#include "calendar_database.h"
#include "calendar_time.h"
#include "contact_validation.h"
#include "server_config.h"

#include <errno.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CALENDAR_DATABASE_ERROR_SIZE 512
#define CALENDAR_SCHEMA_VERSION 6

static sqlite3 *calendar_database = NULL;
static char calendar_database_error[CALENDAR_DATABASE_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            calendar_database_error,
            sizeof(calendar_database_error),
            "%s",
            message == NULL ? "Unbekannter Kalender-Datenbankfehler" : message);
}

static void set_sqlite_error(const char *context)
{
    snprintf(
            calendar_database_error,
            sizeof(calendar_database_error),
            "%s: %s",
            context == NULL ? "SQLite-Fehler" : context,
            calendar_database == NULL
                    ? "Kalender-Datenbank ist nicht geöffnet"
                    : sqlite3_errmsg(calendar_database));
}

const char *calendar_database_last_error(void)
{
    return calendar_database_error[0] == '\0'
            ? "Unbekannter Kalender-Datenbankfehler"
            : calendar_database_error;
}

static bool is_memory_database_path(const char *path)
{
    if (path == NULL) {
        return false;
    }

    return strcmp(path, ":memory:") == 0 ||
           strncmp(path, "file::memory:", strlen("file::memory:")) == 0 ||
           (strncmp(path, "file:", strlen("file:")) == 0 &&
            strstr(path, "mode=memory") != NULL);
}

static const char *database_open_path(void)
{
    /*
     * Zwei Module verwenden getrennte SQLite-Verbindungen. Ein nacktes
     * :memory: würde deshalb zwei unabhängige Datenbanken erzeugen. Die
     * benannte Shared-Cache-URI hält beide Verbindungen im selben Prozess
     * auf derselben flüchtigen Datenbank.
     */
    if (strcmp(server_config_database_file(), ":memory:") == 0) {
        return "file:styles4dogs-runtime?mode=memory&cache=shared";
    }

    return server_config_database_file();
}

static int execute_sql(const char *sql)
{
    char *error_message = NULL;
    int result;

    result = sqlite3_exec(
            calendar_database,
            sql,
            NULL,
            NULL,
            &error_message);

    if (result != SQLITE_OK) {
        snprintf(
                calendar_database_error,
                sizeof(calendar_database_error),
                "SQLite-Anweisung fehlgeschlagen: %s",
                error_message == NULL
                        ? sqlite3_errmsg(calendar_database)
                        : error_message);
        sqlite3_free(error_message);
        return -1;
    }

    return 0;
}

static int configure_database(void)
{
    if (sqlite3_busy_timeout(calendar_database, 5000) != SQLITE_OK) {
        set_sqlite_error("SQLite Busy-Timeout konnte nicht gesetzt werden");
        return -1;
    }

    if (execute_sql("PRAGMA foreign_keys = ON;") != 0 ||
        execute_sql("PRAGMA temp_store = MEMORY;") != 0 ||
        execute_sql("PRAGMA synchronous = FULL;") != 0) {
        return -1;
    }

    if (is_memory_database_path(server_config_database_file())) {
        return execute_sql("PRAGMA journal_mode = MEMORY;");
    }

    return execute_sql("PRAGMA journal_mode = DELETE;");
}

static int table_has_column(
        const char *table_name,
        const char *column_name,
        bool *out_exists
)
{
    sqlite3_stmt *statement = NULL;
    char sql[256];
    int written;
    int step_result;

    if (table_name == NULL || column_name == NULL || out_exists == NULL) {
        set_error("Ungültige Spaltenprüfung");
        return -1;
    }

    *out_exists = false;
    written = snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table_name);

    if (written < 0 || (size_t)written >= sizeof(sql)) {
        set_error("Tabellenname für Spaltenprüfung ist zu lang");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Spaltenprüfung konnte nicht vorbereitet werden");
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);

        if (name != NULL && strcmp((const char *)name, column_name) == 0) {
            *out_exists = true;
            break;
        }
    }

    if (step_result != SQLITE_ROW && step_result != SQLITE_DONE) {
        set_sqlite_error("Spaltenprüfung konnte nicht ausgeführt werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

static int ensure_booking_column(
        const char *column_name,
        const char *column_definition
)
{
    bool exists;
    char sql[512];
    int written;

    if (table_has_column("bookings", column_name, &exists) != 0) {
        return -1;
    }

    if (exists) {
        return 0;
    }

    written = snprintf(
            sql,
            sizeof(sql),
            "ALTER TABLE bookings ADD COLUMN %s;",
            column_definition);

    if (written < 0 || (size_t)written >= sizeof(sql)) {
        set_error("Kalender-Migrationsanweisung ist zu lang");
        return -1;
    }

    return execute_sql(sql);
}

static int ensure_table_column(
        const char *table_name,
        const char *column_name,
        const char *column_definition
)
{
    bool exists;
    char sql[768];
    int written;

    if (table_has_column(table_name, column_name, &exists) != 0) {
        return -1;
    }
    if (exists) {
        return 0;
    }

    written = snprintf(
            sql,
            sizeof(sql),
            "ALTER TABLE %s ADD COLUMN %s;",
            table_name,
            column_definition);
    if (written < 0 || (size_t)written >= sizeof(sql)) {
        set_error("Kalender-Migrationsanweisung ist zu lang");
        return -1;
    }

    return execute_sql(sql);
}

static int create_calendar_tables(void)
{
    static const char *sql =
            "CREATE TABLE IF NOT EXISTS services ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    code TEXT NOT NULL UNIQUE,"
            "    name TEXT NOT NULL,"
            "    duration_minutes INTEGER NOT NULL "
            "        CHECK(duration_minutes BETWEEN 15 AND 720),"
            "    buffer_minutes INTEGER NOT NULL DEFAULT 0 "
            "        CHECK(buffer_minutes BETWEEN 0 AND 240),"
            "    active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0, 1)),"
            "    sort_order INTEGER NOT NULL DEFAULT 0"
            ");"
            "CREATE TABLE IF NOT EXISTS calendar_settings ("
            "    id INTEGER PRIMARY KEY CHECK(id = 1),"
            "    timezone TEXT NOT NULL,"
            "    min_notice_minutes INTEGER NOT NULL "
            "        CHECK(min_notice_minutes BETWEEN 0 AND 525600),"
            "    booking_horizon_days INTEGER NOT NULL "
            "        CHECK(booking_horizon_days BETWEEN 1 AND 730),"
            "    slot_interval_minutes INTEGER NOT NULL "
            "        CHECK(slot_interval_minutes IN (5, 10, 15, 20, 30, 60)),"
            "    pending_hold_minutes INTEGER NOT NULL "
            "        CHECK(pending_hold_minutes BETWEEN 5 AND 10080),"
            "    capacity INTEGER NOT NULL DEFAULT 1 CHECK(capacity = 1),"
            "    auto_confirm_bookings INTEGER NOT NULL DEFAULT 0 "
            "        CHECK(auto_confirm_bookings IN (0, 1)),"
            "    email_notifications_enabled INTEGER NOT NULL DEFAULT 0 "
            "        CHECK(email_notifications_enabled IN (0, 1)),"
            "    reminder_enabled INTEGER NOT NULL DEFAULT 1 "
            "        CHECK(reminder_enabled IN (0, 1)),"
            "    reminder_lead_minutes INTEGER NOT NULL DEFAULT 1440 "
            "        CHECK(reminder_lead_minutes BETWEEN 60 AND 10080)"
            ");"
            "CREATE TABLE IF NOT EXISTS weekly_opening_hours ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    weekday INTEGER NOT NULL CHECK(weekday BETWEEN 1 AND 7),"
            "    start_minute INTEGER NOT NULL CHECK(start_minute BETWEEN 0 AND 1439),"
            "    end_minute INTEGER NOT NULL CHECK(end_minute BETWEEN 1 AND 1440),"
            "    CHECK(start_minute < end_minute),"
            "    UNIQUE(weekday, start_minute, end_minute)"
            ");"
            "CREATE TABLE IF NOT EXISTS calendar_closures ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    start_date TEXT NOT NULL,"
            "    end_date TEXT NOT NULL,"
            "    start_minute INTEGER NOT NULL DEFAULT 0 "
            "        CHECK(start_minute BETWEEN 0 AND 1439),"
            "    end_minute INTEGER NOT NULL DEFAULT 1440 "
            "        CHECK(end_minute BETWEEN 1 AND 1440),"
            "    label TEXT NOT NULL DEFAULT '',"
            "    CHECK(start_date <= end_date),"
            "    CHECK(start_date < end_date OR start_minute < end_minute)"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_opening_hours_weekday "
            "    ON weekly_opening_hours(weekday, start_minute);"
            "CREATE INDEX IF NOT EXISTS idx_calendar_closures_dates "
            "    ON calendar_closures(start_date, end_date);"
            "CREATE TABLE IF NOT EXISTS notification_jobs ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    booking_id INTEGER REFERENCES bookings(id) ON DELETE CASCADE,"
            "    event_type TEXT NOT NULL CHECK(event_type IN ("
            "        'booking_received', 'booking_confirmed', 'booking_rejected', "
            "        'appointment_reminder', 'admin_new_booking', 'smtp_test'"
            "    )),"
            "    recipient_email TEXT NOT NULL,"
            "    subject TEXT NOT NULL,"
            "    body_text TEXT NOT NULL,"
            "    ics_content TEXT NOT NULL DEFAULT '',"
            "    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ("
            "        'pending', 'processing', 'sent', 'failed'"
            "    )),"
            "    attempts INTEGER NOT NULL DEFAULT 0 CHECK(attempts BETWEEN 0 AND 20),"
            "    available_at TEXT NOT NULL,"
            "    created_at TEXT NOT NULL,"
            "    claimed_at TEXT,"
            "    sent_at TEXT,"
            "    last_error TEXT NOT NULL DEFAULT '',"
            "    UNIQUE(booking_id, event_type)"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_notification_jobs_delivery "
            "    ON notification_jobs(status, available_at, id);"
            "CREATE TABLE IF NOT EXISTS notification_templates ("
            "    event_type TEXT PRIMARY KEY CHECK(event_type IN ("
            "        'booking_received', 'booking_confirmed', 'booking_rejected', "
            "        'appointment_reminder', 'admin_new_booking'"
            "    )),"
            "    subject_template TEXT NOT NULL,"
            "    body_template TEXT NOT NULL,"
            "    updated_at_utc TEXT NOT NULL"
            ");";

    return execute_sql(sql);
}

static int seed_calendar_defaults(void)
{
    static const char *sql =
            "INSERT OR IGNORE INTO calendar_settings("
            "    id, timezone, min_notice_minutes, booking_horizon_days,"
            "    slot_interval_minutes, pending_hold_minutes, capacity, auto_confirm_bookings,"
            "    email_notifications_enabled, reminder_enabled, reminder_lead_minutes"
            ") VALUES(1, 'Europe/Berlin', 1440, 90, 15, 1440, 1, 0, 0, 1, 1440);"
            "INSERT OR IGNORE INTO services("
            "    code, name, duration_minutes, buffer_minutes, active, sort_order"
            ") VALUES"
            "    ('consultation', 'Beratung', 30, 0, 1, 10),"
            "    ('wash_dry', 'Waschen und Föhnen', 60, 15, 1, 20),"
            "    ('full_groom', 'Komplettpflege', 120, 15, 1, 30),"
            "    ('claw_care', 'Krallenpflege', 30, 0, 1, 40),"
            "    ('other', 'Sonstiges', 60, 15, 0, 50);"
            "INSERT OR IGNORE INTO notification_templates("
            "event_type, subject_template, body_template, updated_at_utc) VALUES"
            "('booking_received', 'Terminanfrage erhalten – {{salon_name}}', "
            "'Hallo {{customer_name}},\n\nwir haben deine Terminanfrage erhalten. Der Zeitraum ist vorläufig reserviert und noch nicht verbindlich bestätigt.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}', strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
            "('booking_confirmed', 'Termin bestätigt – {{salon_name}}', "
            "'Hallo {{customer_name}},\n\ndein Termin ist verbindlich bestätigt.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nDie Kalenderdatei für deinen Termin ist angehängt.\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}', strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
            "('booking_rejected', 'Terminanfrage nicht möglich – {{salon_name}}', "
            "'Hallo {{customer_name}},\n\nleider können wir deine Terminanfrage nicht bestätigen.\n{{rejection_reason}}\n\nAngefragter Termin: {{appointment_date}}, {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nMelde dich gerne bei uns, damit wir gemeinsam einen anderen Termin finden.\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}', strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
            "('appointment_reminder', 'Erinnerung an deinen Termin – {{salon_name}}', "
            "'Hallo {{customer_name}},\n\ndies ist eine Erinnerung an deinen bevorstehenden Termin.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nDie Kalenderdatei für deinen Termin ist angehängt.\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}', strftime('%Y-%m-%dT%H:%M:%SZ','now')),"
            "('admin_new_booking', 'Neue Terminanfrage #{{booking_id}} – {{customer_name}}', "
            "'Es ist eine neue Terminanfrage eingegangen.\n\nBuchungsnummer: {{booking_id}}\nKundin/Kunde: {{customer_name}}\nHund: {{dog_name}}\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\n\nDie Anfrage kann im Adminbereich geprüft werden:\n{{website_url}}/admin/bookings', strftime('%Y-%m-%dT%H:%M:%SZ','now'));";

    return execute_sql(sql);
}

static int migrate_booking_columns(void)
{
    if (ensure_booking_column(
            "service_id",
            "service_id INTEGER REFERENCES services(id)") != 0 ||
        ensure_booking_column(
            "appointment_date",
            "appointment_date TEXT") != 0 ||
        ensure_booking_column(
            "start_minute",
            "start_minute INTEGER") != 0 ||
        ensure_booking_column(
            "end_minute",
            "end_minute INTEGER") != 0 ||
        ensure_booking_column(
            "blocked_until_minute",
            "blocked_until_minute INTEGER") != 0 ||
        ensure_booking_column(
            "decision_status",
            "decision_status TEXT NOT NULL DEFAULT 'legacy'") != 0 ||
        ensure_booking_column(
            "hold_expires_at",
            "hold_expires_at TEXT") != 0 ||
        ensure_booking_column(
            "decision_at",
            "decision_at TEXT") != 0 ||
        ensure_booking_column(
            "rejection_reason",
            "rejection_reason TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "contact_channel",
            "contact_channel TEXT NOT NULL DEFAULT 'legacy'") != 0 ||
        ensure_booking_column(
            "email",
            "email TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "phone_number",
            "phone_number TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "phone_kind",
            "phone_kind TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "contact_preference",
            "contact_preference TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "service_name_snapshot",
            "service_name_snapshot TEXT NOT NULL DEFAULT ''") != 0 ||
        ensure_booking_column(
            "service_duration_minutes_snapshot",
            "service_duration_minutes_snapshot INTEGER") != 0 ||
        ensure_booking_column(
            "service_buffer_minutes_snapshot",
            "service_buffer_minutes_snapshot INTEGER") != 0 ||
        ensure_table_column(
            "calendar_settings",
            "auto_confirm_bookings",
            "auto_confirm_bookings INTEGER NOT NULL DEFAULT 0 CHECK(auto_confirm_bookings IN (0, 1))") != 0 ||
        ensure_table_column(
            "calendar_settings",
            "email_notifications_enabled",
            "email_notifications_enabled INTEGER NOT NULL DEFAULT 0 CHECK(email_notifications_enabled IN (0, 1))") != 0 ||
        ensure_table_column(
            "calendar_settings",
            "reminder_enabled",
            "reminder_enabled INTEGER NOT NULL DEFAULT 1 CHECK(reminder_enabled IN (0, 1))") != 0 ||
        ensure_table_column(
            "calendar_settings",
            "reminder_lead_minutes",
            "reminder_lead_minutes INTEGER NOT NULL DEFAULT 1440 CHECK(reminder_lead_minutes BETWEEN 60 AND 10080)") != 0) {
        return -1;
    }

    return 0;
}

static int create_booking_calendar_guards(void)
{
    static const char *status_and_appointment_sql =
            "DROP TRIGGER IF EXISTS trg_bookings_decision_status_insert;"
            "DROP TRIGGER IF EXISTS trg_bookings_decision_status_update;"
            "DROP TRIGGER IF EXISTS trg_bookings_appointment_insert;"
            "DROP TRIGGER IF EXISTS trg_bookings_appointment_update;"
            "DROP TRIGGER IF EXISTS trg_bookings_schedule_state_insert;"
            "DROP TRIGGER IF EXISTS trg_bookings_schedule_state_update;"
            "DROP TRIGGER IF EXISTS trg_bookings_overlap_insert;"
            "DROP TRIGGER IF EXISTS trg_bookings_overlap_update;"
            "CREATE TRIGGER trg_bookings_decision_status_insert "
            "BEFORE INSERT ON bookings "
            "WHEN NEW.decision_status NOT IN ("
            "    'pending', 'confirmed', 'rejected', 'cancelled', 'expired', 'legacy'"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking decision status'); END;"
            "CREATE TRIGGER trg_bookings_decision_status_update "
            "BEFORE UPDATE OF decision_status ON bookings "
            "WHEN NEW.decision_status NOT IN ("
            "    'pending', 'confirmed', 'rejected', 'cancelled', 'expired', 'legacy'"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking decision status'); END;"
            "CREATE TRIGGER trg_bookings_appointment_insert "
            "BEFORE INSERT ON bookings "
            "WHEN NOT ("
            "    (NEW.appointment_date IS NULL AND NEW.start_minute IS NULL "
            "     AND NEW.end_minute IS NULL AND NEW.blocked_until_minute IS NULL)"
            "    OR"
            "    (NEW.appointment_date IS NOT NULL "
            "     AND NEW.start_minute BETWEEN 0 AND 1439 "
            "     AND NEW.end_minute BETWEEN 1 AND 1440 "
            "     AND NEW.blocked_until_minute BETWEEN 1 AND 1440 "
            "     AND NEW.start_minute < NEW.end_minute "
            "     AND NEW.end_minute <= NEW.blocked_until_minute)"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking appointment'); END;"
            "CREATE TRIGGER trg_bookings_appointment_update "
            "BEFORE UPDATE OF appointment_date, start_minute, end_minute, blocked_until_minute "
            "ON bookings "
            "WHEN NOT ("
            "    (NEW.appointment_date IS NULL AND NEW.start_minute IS NULL "
            "     AND NEW.end_minute IS NULL AND NEW.blocked_until_minute IS NULL)"
            "    OR"
            "    (NEW.appointment_date IS NOT NULL "
            "     AND NEW.start_minute BETWEEN 0 AND 1439 "
            "     AND NEW.end_minute BETWEEN 1 AND 1440 "
            "     AND NEW.blocked_until_minute BETWEEN 1 AND 1440 "
            "     AND NEW.start_minute < NEW.end_minute "
            "     AND NEW.end_minute <= NEW.blocked_until_minute)"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking appointment'); END;";

    static const char *schedule_and_overlap_sql =
            "CREATE TRIGGER trg_bookings_schedule_state_insert "
            "BEFORE INSERT ON bookings "
            "WHEN NEW.decision_status IN ('pending', 'confirmed') AND ("
            "    NEW.service_id IS NULL OR NEW.appointment_date IS NULL "
            "    OR NEW.start_minute IS NULL OR NEW.end_minute IS NULL "
            "    OR NEW.blocked_until_minute IS NULL "
            "    OR (NEW.decision_status = 'pending' AND NEW.hold_expires_at IS NULL)"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'scheduled booking is incomplete'); END;"
            "CREATE TRIGGER trg_bookings_schedule_state_update "
            "BEFORE UPDATE OF decision_status, service_id, appointment_date, start_minute, "
            "                 end_minute, blocked_until_minute, hold_expires_at "
            "ON bookings "
            "WHEN NEW.decision_status IN ('pending', 'confirmed') AND ("
            "    NEW.service_id IS NULL OR NEW.appointment_date IS NULL "
            "    OR NEW.start_minute IS NULL OR NEW.end_minute IS NULL "
            "    OR NEW.blocked_until_minute IS NULL "
            "    OR (NEW.decision_status = 'pending' AND NEW.hold_expires_at IS NULL)"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'scheduled booking is incomplete'); END;"
            "CREATE TRIGGER trg_bookings_overlap_insert "
            "BEFORE INSERT ON bookings "
            "WHEN NEW.decision_status IN ('pending', 'confirmed') AND EXISTS ("
            "    SELECT 1 FROM bookings existing "
            "    WHERE existing.appointment_date = NEW.appointment_date "
            "      AND existing.decision_status IN ('pending', 'confirmed') "
            "      AND existing.start_minute < NEW.blocked_until_minute "
            "      AND existing.blocked_until_minute > NEW.start_minute"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'overlapping booking'); END;"
            "CREATE TRIGGER trg_bookings_overlap_update "
            "BEFORE UPDATE OF decision_status, appointment_date, start_minute, blocked_until_minute "
            "ON bookings "
            "WHEN NEW.decision_status IN ('pending', 'confirmed') AND EXISTS ("
            "    SELECT 1 FROM bookings existing "
            "    WHERE existing.id <> NEW.id "
            "      AND existing.appointment_date = NEW.appointment_date "
            "      AND existing.decision_status IN ('pending', 'confirmed') "
            "      AND existing.start_minute < NEW.blocked_until_minute "
            "      AND existing.blocked_until_minute > NEW.start_minute"
            ") "
            "BEGIN SELECT RAISE(ABORT, 'overlapping booking'); END;";

    static const char *index_and_backfill_sql =
            "CREATE INDEX IF NOT EXISTS idx_bookings_calendar "
            "    ON bookings(appointment_date, decision_status, start_minute);"
            "UPDATE bookings "
            "SET decision_status = 'legacy' "
            "WHERE appointment_date IS NULL;"
            "UPDATE bookings "
            "SET service_id = ("
            "    SELECT services.id FROM services WHERE services.code = bookings.service"
            ") "
            "WHERE service_id IS NULL AND service <> '';"
            "UPDATE bookings "
            "SET service_name_snapshot = COALESCE((SELECT name FROM services WHERE id = bookings.service_id), service) "
            "WHERE service_name_snapshot = '';"
            "UPDATE bookings "
            "SET service_duration_minutes_snapshot = (SELECT duration_minutes FROM services WHERE id = bookings.service_id), "
            "    service_buffer_minutes_snapshot = (SELECT buffer_minutes FROM services WHERE id = bookings.service_id) "
            "WHERE service_id IS NOT NULL AND service_duration_minutes_snapshot IS NULL;";

    if (execute_sql(status_and_appointment_sql) != 0 ||
        execute_sql(schedule_and_overlap_sql) != 0 ||
        execute_sql(index_and_backfill_sql) != 0) {
        return -1;
    }

    return 0;
}

static int notification_jobs_need_v6_migration(bool *out_needed)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    bool found = false;

    if (out_needed == NULL) {
        set_error("Ausgabe für Benachrichtigungsmigration fehlt");
        return -1;
    }
    *out_needed = false;

    if (sqlite3_prepare_v2(
            calendar_database,
            "PRAGMA table_info(notification_jobs);",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Benachrichtigungsschema konnte nicht geprüft werden");
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);
        if (name != NULL && strcmp((const char *)name, "booking_id") == 0) {
            found = true;
            *out_needed = sqlite3_column_int(statement, 3) != 0;
            break;
        }
    }

    if (step_result != SQLITE_ROW && step_result != SQLITE_DONE) {
        set_sqlite_error("Benachrichtigungsschema konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    if (!found) {
        set_error("notification_jobs besitzt keine booking_id-Spalte");
        return -1;
    }
    return 0;
}

static int migrate_notification_jobs_v6(void)
{
    bool needed;
    static const char *sql =
            "ALTER TABLE notification_jobs RENAME TO notification_jobs_v5;"
            "DROP INDEX IF EXISTS idx_notification_jobs_delivery;"
            "CREATE TABLE notification_jobs ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    booking_id INTEGER REFERENCES bookings(id) ON DELETE CASCADE,"
            "    event_type TEXT NOT NULL CHECK(event_type IN ("
            "        'booking_received', 'booking_confirmed', 'booking_rejected', "
            "        'appointment_reminder', 'admin_new_booking', 'smtp_test'"
            "    )),"
            "    recipient_email TEXT NOT NULL,"
            "    subject TEXT NOT NULL,"
            "    body_text TEXT NOT NULL,"
            "    ics_content TEXT NOT NULL DEFAULT '',"
            "    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ("
            "        'pending', 'processing', 'sent', 'failed'"
            "    )),"
            "    attempts INTEGER NOT NULL DEFAULT 0 CHECK(attempts BETWEEN 0 AND 20),"
            "    available_at TEXT NOT NULL,"
            "    created_at TEXT NOT NULL,"
            "    claimed_at TEXT,"
            "    sent_at TEXT,"
            "    last_error TEXT NOT NULL DEFAULT '',"
            "    UNIQUE(booking_id, event_type)"
            ");"
            "INSERT INTO notification_jobs("
            "id, booking_id, event_type, recipient_email, subject, body_text, ics_content,"
            "status, attempts, available_at, created_at, claimed_at, sent_at, last_error"
            ") SELECT id, booking_id, event_type, recipient_email, subject, body_text, ics_content,"
            "status, attempts, available_at, created_at, claimed_at, sent_at, last_error "
            "FROM notification_jobs_v5;"
            "DROP TABLE notification_jobs_v5;"
            "CREATE INDEX idx_notification_jobs_delivery "
            "ON notification_jobs(status, available_at, id);";

    if (notification_jobs_need_v6_migration(&needed) != 0) return -1;
    return needed ? execute_sql(sql) : 0;
}

static int migrate_schema(void)
{
    if (execute_sql("BEGIN IMMEDIATE;") != 0) {
        return -1;
    }

    if (create_calendar_tables() != 0 ||
        migrate_booking_columns() != 0 ||
        migrate_notification_jobs_v6() != 0 ||
        seed_calendar_defaults() != 0 ||
        create_booking_calendar_guards() != 0 ||
        execute_sql("PRAGMA user_version = 6;") != 0 ||
        execute_sql("COMMIT;") != 0) {
        sqlite3_exec(calendar_database, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    return 0;
}

int calendar_database_initialize(void)
{
    int open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

    if (calendar_database != NULL) {
        return 0;
    }

    calendar_database_error[0] = '\0';

    if (strncmp(database_open_path(), "file:", strlen("file:")) == 0) {
        open_flags |= SQLITE_OPEN_URI;
    }

    if (sqlite3_open_v2(
            database_open_path(),
            &calendar_database,
            open_flags,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Kalender-Datenbank konnte nicht geöffnet werden");
        calendar_database_shutdown();
        return -1;
    }

    if (configure_database() != 0 || migrate_schema() != 0) {
        calendar_database_shutdown();
        return -1;
    }

    return 0;
}

void calendar_database_shutdown(void)
{
    if (calendar_database == NULL) {
        return;
    }

    if (sqlite3_close_v2(calendar_database) != SQLITE_OK) {
        set_sqlite_error("Kalender-Datenbank konnte nicht sauber geschlossen werden");
        return;
    }

    calendar_database = NULL;
}

int calendar_database_schema_version(int *out_version)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (calendar_database == NULL || out_version == NULL) {
        set_error("Kalender-Datenbank oder Versionsausgabe fehlt");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "PRAGMA user_version;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Schema-Version konnte nicht vorbereitet werden");
        return -1;
    }

    step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Schema-Version konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    *out_version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return 0;
}

static bool timezone_is_valid(const char *timezone)
{
    size_t length;

    if (timezone == NULL) {
        return false;
    }

    length = strlen(timezone);
    if (length == 0 || length >= CALENDAR_TIMEZONE_SIZE) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)timezone[index];
        bool valid =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '/' || character == '_' ||
                character == '-' || character == '+';

        if (!valid) {
            return false;
        }
    }

    return true;
}

static bool settings_are_valid(const calendar_settings *settings)
{
    if (settings == NULL || !timezone_is_valid(settings->timezone)) {
        return false;
    }

    return settings->min_notice_minutes >= 0 &&
           settings->min_notice_minutes <= 525600 &&
           settings->booking_horizon_days >= 1 &&
           settings->booking_horizon_days <= 730 &&
           (settings->slot_interval_minutes == 5 ||
            settings->slot_interval_minutes == 10 ||
            settings->slot_interval_minutes == 15 ||
            settings->slot_interval_minutes == 20 ||
            settings->slot_interval_minutes == 30 ||
            settings->slot_interval_minutes == 60) &&
           settings->pending_hold_minutes >= 5 &&
           settings->pending_hold_minutes <= 10080 &&
           settings->capacity == 1 &&
           settings->reminder_lead_minutes >= 60 &&
           settings->reminder_lead_minutes <= 10080;
}

int calendar_database_get_settings(calendar_settings *settings)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    const unsigned char *timezone;

    if (calendar_database == NULL || settings == NULL) {
        set_error("Kalender-Datenbank oder Einstellungsausgabe fehlt");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT timezone, min_notice_minutes, booking_horizon_days, "
            "       slot_interval_minutes, pending_hold_minutes, capacity, auto_confirm_bookings, "
            "       email_notifications_enabled, reminder_enabled, reminder_lead_minutes "
            "FROM calendar_settings WHERE id = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Kalendereinstellungen konnten nicht vorbereitet werden");
        return -1;
    }

    step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Kalendereinstellungen konnten nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    timezone = sqlite3_column_text(statement, 0);
    snprintf(
            settings->timezone,
            sizeof(settings->timezone),
            "%s",
            timezone == NULL ? "" : (const char *)timezone);
    settings->min_notice_minutes = sqlite3_column_int(statement, 1);
    settings->booking_horizon_days = sqlite3_column_int(statement, 2);
    settings->slot_interval_minutes = sqlite3_column_int(statement, 3);
    settings->pending_hold_minutes = sqlite3_column_int(statement, 4);
    settings->capacity = sqlite3_column_int(statement, 5);
    settings->auto_confirm_bookings = sqlite3_column_int(statement, 6) != 0;
    settings->email_notifications_enabled = sqlite3_column_int(statement, 7) != 0;
    settings->reminder_enabled = sqlite3_column_int(statement, 8) != 0;
    settings->reminder_lead_minutes = sqlite3_column_int(statement, 9);

    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_update_settings(const calendar_settings *settings)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !settings_are_valid(settings)) {
        set_error("Ungültige Kalendereinstellungen");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "UPDATE calendar_settings "
            "SET timezone = ?1, min_notice_minutes = ?2, booking_horizon_days = ?3, "
            "    slot_interval_minutes = ?4, pending_hold_minutes = ?5, capacity = ?6, "
            "    auto_confirm_bookings = ?7, email_notifications_enabled = ?8, "
            "    reminder_enabled = ?9, reminder_lead_minutes = ?10 "
            "WHERE id = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Kalendereinstellungen konnten nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, settings->timezone, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, settings->min_notice_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, settings->booking_horizon_days) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, settings->slot_interval_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 5, settings->pending_hold_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 6, settings->capacity) != SQLITE_OK ||
        sqlite3_bind_int(statement, 7, settings->auto_confirm_bookings ? 1 : 0) != SQLITE_OK ||
        sqlite3_bind_int(statement, 8, settings->email_notifications_enabled ? 1 : 0) != SQLITE_OK ||
        sqlite3_bind_int(statement, 9, settings->reminder_enabled ? 1 : 0) != SQLITE_OK ||
        sqlite3_bind_int(statement, 10, settings->reminder_lead_minutes) != SQLITE_OK) {
        set_sqlite_error("Kalendereinstellungen konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Kalendereinstellungen konnten nicht gespeichert werden");
        goto cleanup;
    }

    result = sqlite3_changes(calendar_database) == 1 ? 0 : -1;
    if (result != 0) {
        set_error("Kalendereinstellungen wurden nicht aktualisiert");
    }

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static bool service_code_is_valid(const char *code)
{
    size_t length;

    if (code == NULL) {
        return false;
    }

    length = strlen(code);
    if (length == 0 || length >= CALENDAR_SERVICE_CODE_SIZE) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)code[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return false;
        }
    }

    return true;
}

static bool service_is_valid(const calendar_service *service)
{
    size_t name_length;

    if (service == NULL || !service_code_is_valid(service->code)) {
        return false;
    }

    name_length = strlen(service->name);
    if (name_length == 0 || name_length >= CALENDAR_SERVICE_NAME_SIZE) {
        return false;
    }

    for (size_t index = 0; index < name_length; index++) {
        if (service->name[index] == '\r' || service->name[index] == '\n') {
            return false;
        }
    }

    return service->duration_minutes >= 15 &&
           service->duration_minutes <= 720 &&
           service->buffer_minutes >= 0 &&
           service->buffer_minutes <= 240;
}

int calendar_database_get_service(
        const char *code,
        calendar_service *service
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    const unsigned char *db_code;
    const unsigned char *db_name;

    if (calendar_database == NULL || !service_code_is_valid(code) || service == NULL) {
        set_error("Ungültiger Leistungsschlüssel oder fehlende Ausgabe");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT id, code, name, duration_minutes, buffer_minutes, active, sort_order "
            "FROM services WHERE code = ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Leistungsabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, code, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Leistungsschlüssel konnte nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return 1;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Leistung konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    memset(service, 0, sizeof(*service));
    service->id = sqlite3_column_int64(statement, 0);
    db_code = sqlite3_column_text(statement, 1);
    db_name = sqlite3_column_text(statement, 2);
    snprintf(service->code, sizeof(service->code), "%s", db_code == NULL ? "" : (const char *)db_code);
    snprintf(service->name, sizeof(service->name), "%s", db_name == NULL ? "" : (const char *)db_name);
    service->duration_minutes = sqlite3_column_int(statement, 3);
    service->buffer_minutes = sqlite3_column_int(statement, 4);
    service->active = sqlite3_column_int(statement, 5) != 0;
    service->sort_order = sqlite3_column_int(statement, 6);

    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_update_service(const calendar_service *service)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !service_is_valid(service)) {
        set_error("Ungültige Leistungskonfiguration");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "UPDATE services "
            "SET name = ?1, duration_minutes = ?2, buffer_minutes = ?3, "
            "    active = ?4, sort_order = ?5 "
            "WHERE code = ?6;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Leistungsänderung konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, service->name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, service->duration_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, service->buffer_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, service->active ? 1 : 0) != SQLITE_OK ||
        sqlite3_bind_int(statement, 5, service->sort_order) != SQLITE_OK ||
        sqlite3_bind_text(statement, 6, service->code, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Leistungswerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Leistung konnte nicht aktualisiert werden");
        goto cleanup;
    }

    result = sqlite3_changes(calendar_database) == 1 ? 0 : 1;

cleanup:
    sqlite3_finalize(statement);
    return result;
}


int calendar_database_add_service(const calendar_service *service)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !service_is_valid(service)) {
        set_error("Ungültige neue Leistung");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "INSERT INTO services(code, name, duration_minutes, buffer_minutes, active, sort_order) "
            "VALUES(?1, ?2, ?3, ?4, ?5, "
            "       COALESCE((SELECT MAX(sort_order) + 10 FROM services), 10));",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Neue Leistung konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, service->code, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, service->name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, service->duration_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, service->buffer_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 5, service->active ? 1 : 0) != SQLITE_OK) {
        set_sqlite_error("Neue Leistungswerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        if (sqlite3_extended_errcode(calendar_database) == SQLITE_CONSTRAINT_UNIQUE) {
            set_error("Dieser Leistungsschlüssel ist bereits vergeben");
            result = 1;
        } else {
            set_sqlite_error("Neue Leistung konnte nicht gespeichert werden");
        }
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

calendar_service_delete_result calendar_database_delete_service(const char *code)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    int64_t service_id;
    int64_t usage_count;
    calendar_service_delete_result result = CALENDAR_SERVICE_DELETE_ERROR;

    if (calendar_database == NULL || !service_code_is_valid(code)) {
        set_error("Ungültiger Leistungsschlüssel");
        return CALENDAR_SERVICE_DELETE_ERROR;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT services.id, COUNT(bookings.id) "
            "FROM services LEFT JOIN bookings ON bookings.service_id = services.id "
            "WHERE services.code = ?1 GROUP BY services.id;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, code, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Leistungsnutzung konnte nicht geprüft werden");
        sqlite3_finalize(statement);
        return CALENDAR_SERVICE_DELETE_ERROR;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return CALENDAR_SERVICE_DELETE_NOT_FOUND;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Leistungsnutzung konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return CALENDAR_SERVICE_DELETE_ERROR;
    }

    service_id = sqlite3_column_int64(statement, 0);
    usage_count = sqlite3_column_int64(statement, 1);
    sqlite3_finalize(statement);
    statement = NULL;

    if (usage_count > 0) {
        if (sqlite3_prepare_v2(
                calendar_database,
                "UPDATE services SET active = 0 WHERE id = ?1;",
                -1,
                &statement,
                NULL) != SQLITE_OK ||
            sqlite3_bind_int64(statement, 1, service_id) != SQLITE_OK) {
            set_sqlite_error("Verwendete Leistung konnte nicht archiviert werden");
            goto cleanup;
        }
        if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(calendar_database) != 1) {
            set_sqlite_error("Verwendete Leistung konnte nicht archiviert werden");
            goto cleanup;
        }
        result = CALENDAR_SERVICE_DELETE_ARCHIVED;
    } else {
        if (sqlite3_prepare_v2(
                calendar_database,
                "DELETE FROM services WHERE id = ?1;",
                -1,
                &statement,
                NULL) != SQLITE_OK ||
            sqlite3_bind_int64(statement, 1, service_id) != SQLITE_OK) {
            set_sqlite_error("Leistung konnte nicht gelöscht werden");
            goto cleanup;
        }
        if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(calendar_database) != 1) {
            set_sqlite_error("Leistung konnte nicht gelöscht werden");
            goto cleanup;
        }
        result = CALENDAR_SERVICE_DELETE_OK;
    }

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static int for_each_service_query(
        const char *sql,
        const char *error_context,
        calendar_service_callback callback,
        void *context
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (calendar_database == NULL || callback == NULL || sql == NULL) {
        set_error("Kalender-Datenbank oder Service-Callback fehlt");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error(error_context);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        calendar_service service;
        const unsigned char *code = sqlite3_column_text(statement, 1);
        const unsigned char *name = sqlite3_column_text(statement, 2);

        memset(&service, 0, sizeof(service));
        service.id = sqlite3_column_int64(statement, 0);
        snprintf(service.code, sizeof(service.code), "%s", code == NULL ? "" : (const char *)code);
        snprintf(service.name, sizeof(service.name), "%s", name == NULL ? "" : (const char *)name);
        service.duration_minutes = sqlite3_column_int(statement, 3);
        service.buffer_minutes = sqlite3_column_int(statement, 4);
        service.active = sqlite3_column_int(statement, 5) != 0;
        service.sort_order = sqlite3_column_int(statement, 6);

        if (callback(&service, context) != 0) {
            sqlite3_finalize(statement);
            return 1;
        }
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error(error_context);
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_for_each_service(
        calendar_service_callback callback,
        void *context
)
{
    return for_each_service_query(
            "SELECT id, code, name, duration_minutes, buffer_minutes, active, sort_order "
            "FROM services ORDER BY sort_order, name, id;",
            "Leistungen konnten nicht gelesen werden",
            callback,
            context);
}

int calendar_database_for_each_active_service(
        calendar_service_callback callback,
        void *context
)
{
    return for_each_service_query(
            "SELECT id, code, name, duration_minutes, buffer_minutes, active, sort_order "
            "FROM services WHERE active = 1 ORDER BY sort_order, name, id;",
            "Aktive Leistungen konnten nicht gelesen werden",
            callback,
            context);
}

int calendar_database_clear_opening_hours(void)
{
    if (calendar_database == NULL) {
        set_error("Kalender-Datenbank ist nicht initialisiert");
        return -1;
    }

    return execute_sql("DELETE FROM weekly_opening_hours;");
}

int calendar_database_clear_opening_hours_for_weekday(int weekday)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || weekday < 1 || weekday > 7) {
        set_error("Ungültiger Wochentag für Öffnungszeiten");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "DELETE FROM weekly_opening_hours WHERE weekday = ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Öffnungszeiten konnten nicht zum Ersetzen vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_int(statement, 1, weekday) != SQLITE_OK) {
        set_sqlite_error("Wochentag konnte nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Öffnungszeiten konnten nicht ersetzt werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

int calendar_database_add_opening_period(
        int weekday,
        int start_minute,
        int end_minute
)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || weekday < 1 || weekday > 7 ||
        start_minute < 0 || start_minute > 1439 ||
        end_minute < 1 || end_minute > 1440 ||
        start_minute >= end_minute) {
        set_error("Ungültiger Öffnungszeitraum");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "INSERT INTO weekly_opening_hours(weekday, start_minute, end_minute) "
            "SELECT ?1, ?2, ?3 "
            "WHERE NOT EXISTS ("
            "    SELECT 1 FROM weekly_opening_hours "
            "    WHERE weekday = ?1 AND start_minute < ?3 AND end_minute > ?2"
            ");",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Öffnungszeit konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_int(statement, 1, weekday) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, end_minute) != SQLITE_OK) {
        set_sqlite_error("Öffnungszeit konnte nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Öffnungszeit konnte nicht gespeichert werden");
        goto cleanup;
    }

    result = sqlite3_changes(calendar_database) == 1 ? 0 : 1;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

int calendar_database_get_opening_periods(
        int weekday,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    size_t count = 0;

    if (calendar_database == NULL || weekday < 1 || weekday > 7 ||
        ranges == NULL || ranges_capacity == 0 || out_count == NULL) {
        set_error("Ungültige Öffnungszeitenabfrage");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT start_minute, end_minute "
            "FROM weekly_opening_hours WHERE weekday = ?1 "
            "ORDER BY start_minute, end_minute;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Öffnungszeitenabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_int(statement, 1, weekday) != SQLITE_OK) {
        set_sqlite_error("Wochentag konnte nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        if (count >= ranges_capacity) {
            set_error("Zu viele Öffnungszeiträume für einen Tag");
            sqlite3_finalize(statement);
            return -1;
        }

        ranges[count].start_minute = sqlite3_column_int(statement, 0);
        ranges[count].end_minute = sqlite3_column_int(statement, 1);
        count++;
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Öffnungszeiten konnten nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    *out_count = count;
    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_clear_closures(void)
{
    if (calendar_database == NULL) {
        set_error("Kalender-Datenbank ist nicht initialisiert");
        return -1;
    }

    return execute_sql("DELETE FROM calendar_closures;");
}

static bool closure_is_valid(const calendar_closure *closure)
{
    int day_difference;
    size_t label_length;

    if (closure == NULL ||
        !calendar_date_is_valid(closure->start_date) ||
        !calendar_date_is_valid(closure->end_date) ||
        calendar_date_days_between(
                closure->start_date,
                closure->end_date,
                &day_difference) != 0 ||
        day_difference < 0 ||
        closure->start_minute < 0 || closure->start_minute > 1439 ||
        closure->end_minute < 1 || closure->end_minute > 1440) {
        return false;
    }

    if (day_difference == 0 && closure->start_minute >= closure->end_minute) {
        return false;
    }

    label_length = strlen(closure->label);
    if (label_length >= CALENDAR_LABEL_SIZE) {
        return false;
    }

    for (size_t index = 0; index < label_length; index++) {
        if (closure->label[index] == '\r' || closure->label[index] == '\n') {
            return false;
        }
    }

    return true;
}

int calendar_database_add_closure(
        const calendar_closure *closure,
        int64_t *out_id
)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !closure_is_valid(closure)) {
        set_error("Ungültige Sperrzeit");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "INSERT INTO calendar_closures("
            "    start_date, end_date, start_minute, end_minute, label"
            ") VALUES(?1, ?2, ?3, ?4, ?5);",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Sperrzeit konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, closure->start_date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, closure->end_date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, closure->start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, closure->end_minute) != SQLITE_OK ||
        sqlite3_bind_text(statement, 5, closure->label, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Sperrzeitwerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Sperrzeit konnte nicht gespeichert werden");
        goto cleanup;
    }

    if (out_id != NULL) {
        *out_id = sqlite3_last_insert_rowid(calendar_database);
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

int calendar_database_delete_closure(int64_t closure_id)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || closure_id <= 0) {
        set_error("Ungültige Sperrzeit-ID");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "DELETE FROM calendar_closures WHERE id = ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Sperrzeit-Löschung konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_int64(statement, 1, closure_id) != SQLITE_OK) {
        set_sqlite_error("Sperrzeit-ID konnte nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Sperrzeit konnte nicht gelöscht werden");
        goto cleanup;
    }

    result = sqlite3_changes(calendar_database) == 1 ? 0 : 1;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

int calendar_database_for_each_closure(
        calendar_closure_callback callback,
        void *context
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (calendar_database == NULL || callback == NULL) {
        set_error("Kalender-Datenbank oder Sperrzeiten-Callback fehlt");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT id, start_date, end_date, start_minute, end_minute, label "
            "FROM calendar_closures "
            "ORDER BY start_date, start_minute, end_date, end_minute, id;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Sperrzeiten konnten nicht vorbereitet werden");
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        calendar_closure closure;
        const unsigned char *start_date = sqlite3_column_text(statement, 1);
        const unsigned char *end_date = sqlite3_column_text(statement, 2);
        const unsigned char *label = sqlite3_column_text(statement, 5);

        memset(&closure, 0, sizeof(closure));
        closure.id = sqlite3_column_int64(statement, 0);
        snprintf(
                closure.start_date,
                sizeof(closure.start_date),
                "%s",
                start_date == NULL ? "" : (const char *)start_date);
        snprintf(
                closure.end_date,
                sizeof(closure.end_date),
                "%s",
                end_date == NULL ? "" : (const char *)end_date);
        closure.start_minute = sqlite3_column_int(statement, 3);
        closure.end_minute = sqlite3_column_int(statement, 4);
        snprintf(
                closure.label,
                sizeof(closure.label),
                "%s",
                label == NULL ? "" : (const char *)label);

        if (callback(&closure, context) != 0) {
            sqlite3_finalize(statement);
            return 1;
        }
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Sperrzeiten konnten nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}


int calendar_database_for_each_closure_in_range(
        const char *from_date,
        const char *to_date,
        calendar_closure_callback callback,
        void *context
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (calendar_database == NULL || !calendar_date_is_valid(from_date) ||
        !calendar_date_is_valid(to_date) || strcmp(from_date, to_date) > 0 ||
        callback == NULL) {
        set_error("Ungültige Sperrzeiten-Bereichsabfrage");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT id, start_date, end_date, start_minute, end_minute, label "
            "FROM calendar_closures "
            "WHERE start_date <= ?2 AND end_date >= ?1 "
            "ORDER BY start_date, start_minute, end_date, end_minute, id;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, from_date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, to_date, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Sperrzeiten-Bereich konnte nicht vorbereitet werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        calendar_closure closure;
        const unsigned char *start_date = sqlite3_column_text(statement, 1);
        const unsigned char *end_date = sqlite3_column_text(statement, 2);
        const unsigned char *label = sqlite3_column_text(statement, 5);

        memset(&closure, 0, sizeof(closure));
        closure.id = sqlite3_column_int64(statement, 0);
        snprintf(closure.start_date, sizeof(closure.start_date), "%s",
                start_date == NULL ? "" : (const char *)start_date);
        snprintf(closure.end_date, sizeof(closure.end_date), "%s",
                end_date == NULL ? "" : (const char *)end_date);
        closure.start_minute = sqlite3_column_int(statement, 3);
        closure.end_minute = sqlite3_column_int(statement, 4);
        snprintf(closure.label, sizeof(closure.label), "%s",
                label == NULL ? "" : (const char *)label);

        if (callback(&closure, context) != 0) {
            sqlite3_finalize(statement);
            return 1;
        }
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Sperrzeiten-Bereich konnte nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_get_closures_for_date(
        const char *date,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    size_t count = 0;

    if (calendar_database == NULL || !calendar_date_is_valid(date) ||
        ranges == NULL || ranges_capacity == 0 || out_count == NULL) {
        set_error("Ungültige Sperrzeitenabfrage");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT "
            "    CASE WHEN start_date < ?1 THEN 0 ELSE start_minute END,"
            "    CASE WHEN end_date > ?1 THEN 1440 ELSE end_minute END "
            "FROM calendar_closures "
            "WHERE start_date <= ?1 AND end_date >= ?1 "
            "ORDER BY start_date, start_minute;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Sperrzeitenabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, date, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Sperrzeitdatum konnte nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        if (count >= ranges_capacity) {
            set_error("Zu viele Sperrzeiten für einen Tag");
            sqlite3_finalize(statement);
            return -1;
        }

        ranges[count].start_minute = sqlite3_column_int(statement, 0);
        ranges[count].end_minute = sqlite3_column_int(statement, 1);
        count++;
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Sperrzeiten konnten nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    *out_count = count;
    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_get_blocking_bookings(
        const char *date,
        const char *now_utc,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    size_t count = 0;

    if (calendar_database == NULL ||
        !calendar_date_is_valid(date) ||
        !calendar_utc_timestamp_is_valid(now_utc) ||
        ranges == NULL || ranges_capacity == 0 || out_count == NULL) {
        set_error("Ungültige Belegungsabfrage");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT start_minute, blocked_until_minute "
            "FROM bookings "
            "WHERE appointment_date = ?1 "
            "  AND start_minute IS NOT NULL "
            "  AND blocked_until_minute IS NOT NULL "
            "  AND ("
            "      decision_status = 'confirmed' "
            "      OR (decision_status = 'pending' AND hold_expires_at > ?2)"
            "  ) "
            "ORDER BY start_minute;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Belegungsabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Belegungswerte konnten nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        if (count >= ranges_capacity) {
            set_error("Zu viele blockierende Termine für einen Tag");
            sqlite3_finalize(statement);
            return -1;
        }

        ranges[count].start_minute = sqlite3_column_int(statement, 0);
        ranges[count].end_minute = sqlite3_column_int(statement, 1);
        count++;
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Belegungen konnten nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    *out_count = count;
    sqlite3_finalize(statement);
    return 0;
}

int calendar_database_begin_immediate(void)
{
    if (calendar_database == NULL) {
        set_error("Kalender-Datenbank ist nicht initialisiert");
        return -1;
    }

    return execute_sql("BEGIN IMMEDIATE;");
}

int calendar_database_commit(void)
{
    if (calendar_database == NULL) {
        set_error("Kalender-Datenbank ist nicht initialisiert");
        return -1;
    }

    return execute_sql("COMMIT;");
}

void calendar_database_rollback(void)
{
    if (calendar_database != NULL) {
        sqlite3_exec(calendar_database, "ROLLBACK;", NULL, NULL, NULL);
    }
}

int calendar_database_expire_pending(const char *now_utc)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !calendar_utc_timestamp_is_valid(now_utc)) {
        set_error("Ungültiger Zeitstempel für Pending-Ablauf");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "UPDATE bookings "
            "SET decision_status = 'expired', decision_at = ?1 "
            "WHERE decision_status = 'pending' "
            "  AND hold_expires_at IS NOT NULL "
            "  AND hold_expires_at <= ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Ablauf alter Anfragen konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Ablaufzeitpunkt konnte nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Alte Anfragen konnten nicht freigegeben werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

int calendar_database_count_recent_contact_bookings(
        const char *contact_channel,
        const char *email,
        const char *phone_digits,
        const char *since_utc,
        int *out_count
)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || contact_channel == NULL ||
        email == NULL || phone_digits == NULL ||
        !calendar_utc_timestamp_is_valid(since_utc) || out_count == NULL ||
        (strcmp(contact_channel, "email") != 0 &&
         strcmp(contact_channel, "phone") != 0)) {
        set_error("Ungültige Kontaktprüfung für Buchungsschutz");
        return -1;
    }

    *out_count = 0;
    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT COUNT(*) FROM bookings "
            "WHERE legacy = 0 AND created_at >= ?4 AND ("
            " (?1 = 'email' AND contact_channel = 'email' AND lower(email) = lower(?2)) OR "
            " (?1 = 'phone' AND contact_channel = 'phone' AND "
            "  replace(replace(replace(replace(replace(replace(replace(phone_number, ' ', ''), '-', ''), '(', ''), ')', ''), '+', ''), '/', ''), '.', '') = ?3)"
            ");",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, contact_channel, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 3, phone_digits, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 4, since_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {
        set_sqlite_error("Buchungsanzahl für Kontakt konnte nicht geprüft werden");
        goto cleanup;
    }

    *out_count = sqlite3_column_int(statement, 0);
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static bool pending_booking_is_valid(const calendar_pending_booking *booking)
{
    if (booking == NULL ||
        !calendar_utc_timestamp_is_valid(booking->created_at_utc) ||
        (!booking->auto_confirm &&
         (!calendar_utc_timestamp_is_valid(booking->hold_expires_at_utc) ||
          strcmp(booking->created_at_utc, booking->hold_expires_at_utc) >= 0)) ||
        booking->customer_name == NULL || booking->customer_name[0] == '\0' ||
        booking->contact == NULL || booking->contact[0] == '\0' ||
        !contact_fields_are_valid(
                booking->contact_channel,
                booking->email,
                booking->phone_number,
                booking->phone_kind,
                booking->contact_preference) ||
        !contact_aggregate_matches_fields(
                booking->contact,
                booking->contact_channel,
                booking->email,
                booking->phone_number) ||
        !service_code_is_valid(booking->service_code) ||
        !calendar_date_is_valid(booking->appointment_date) ||
        booking->start_minute < 0 || booking->start_minute > 1439 ||
        booking->end_minute < 1 || booking->end_minute > 1440 ||
        booking->blocked_until_minute < 1 || booking->blocked_until_minute > 1440 ||
        booking->start_minute >= booking->end_minute ||
        booking->end_minute > booking->blocked_until_minute) {
        return false;
    }

    return true;
}

int calendar_database_insert_pending(
        const calendar_pending_booking *booking,
        int64_t *out_booking_id
)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (calendar_database == NULL || !pending_booking_is_valid(booking)) {
        set_error("Ungültige Terminreservierung");
        return -1;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "INSERT INTO bookings("
            "    created_at, status, customer_name, contact, dog_name, dog_size,"
            "    service, preferred_date, message, legacy, service_id,"
            "    appointment_date, start_minute, end_minute, blocked_until_minute,"
            "    decision_status, hold_expires_at, decision_at, rejection_reason,"
            "    contact_channel, email, phone_number, phone_kind, contact_preference,"
            "    service_name_snapshot, service_duration_minutes_snapshot,"
            "    service_buffer_minutes_snapshot"
            ") "
            "SELECT ?1, 'neu', ?2, ?3, ?4, ?5, services.code, ?6, ?7, 0, services.id,"
            "       ?6, ?8, ?9, ?10, CASE WHEN ?11 = 1 THEN 'confirmed' ELSE 'pending' END,"
            "       CASE WHEN ?11 = 1 THEN NULL ELSE ?12 END,"
            "       CASE WHEN ?11 = 1 THEN ?1 ELSE NULL END, '',"
            "       ?13, ?14, ?15, ?16, ?17, services.name, services.duration_minutes,"
            "       services.buffer_minutes "
            "FROM services "
            "WHERE services.code = ?18 AND services.active = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Terminreservierung konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, booking->created_at_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, booking->customer_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 3, booking->contact, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 4, booking->dog_name == NULL ? "" : booking->dog_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 5, booking->dog_size == NULL ? "" : booking->dog_size, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 6, booking->appointment_date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 7, booking->message == NULL ? "" : booking->message, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 8, booking->start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 9, booking->end_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 10, booking->blocked_until_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 11, booking->auto_confirm ? 1 : 0) != SQLITE_OK ||
        (booking->auto_confirm
         ? sqlite3_bind_null(statement, 12)
         : sqlite3_bind_text(statement, 12, booking->hold_expires_at_utc, -1, SQLITE_TRANSIENT)) != SQLITE_OK ||
        sqlite3_bind_text(statement, 13, booking->contact_channel, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 14, booking->email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 15, booking->phone_number, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 16, booking->phone_kind, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 17, booking->contact_preference, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 18, booking->service_code, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Terminreservierungswerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Terminreservierung konnte nicht gespeichert werden");
        goto cleanup;
    }

    if (sqlite3_changes(calendar_database) != 1) {
        set_error("Leistung ist unbekannt oder nicht buchbar");
        goto cleanup;
    }

    if (out_booking_id != NULL) {
        *out_booking_id = sqlite3_last_insert_rowid(calendar_database);
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

calendar_booking_decision_result calendar_database_decide_booking(
        int64_t booking_id,
        bool accept,
        const char *decision_at_utc,
        const char *rejection_reason
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    const char *current_status;
    const char *hold_expires_at;
    calendar_booking_decision_result result = CALENDAR_BOOKING_DECISION_ERROR;

    if (calendar_database == NULL || booking_id <= 0 ||
        !calendar_utc_timestamp_is_valid(decision_at_utc) ||
        rejection_reason == NULL) {
        set_error("Ungültige Terminentscheidung");
        return CALENDAR_BOOKING_DECISION_ERROR;
    }

    if (calendar_database_begin_immediate() != 0) {
        return CALENDAR_BOOKING_DECISION_ERROR;
    }

    if (calendar_database_expire_pending(decision_at_utc) != 0) {
        calendar_database_rollback();
        return CALENDAR_BOOKING_DECISION_ERROR;
    }

    if (sqlite3_prepare_v2(
            calendar_database,
            "SELECT decision_status, COALESCE(hold_expires_at, '') "
            "FROM bookings WHERE id = ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK) {
        set_sqlite_error("Terminanfrage konnte nicht zur Entscheidung geladen werden");
        goto rollback;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        result = CALENDAR_BOOKING_DECISION_NOT_FOUND;
        goto rollback;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Terminanfrage konnte nicht gelesen werden");
        goto rollback;
    }

    current_status = (const char *)sqlite3_column_text(statement, 0);
    hold_expires_at = (const char *)sqlite3_column_text(statement, 1);

    if (current_status == NULL || strcmp(current_status, "expired") == 0 ||
        (hold_expires_at != NULL && hold_expires_at[0] != '\0' &&
         strcmp(hold_expires_at, decision_at_utc) <= 0)) {
        result = CALENDAR_BOOKING_DECISION_EXPIRED;
        goto rollback;
    }
    if (strcmp(current_status, "pending") != 0) {
        result = CALENDAR_BOOKING_DECISION_NOT_PENDING;
        goto rollback;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(
            calendar_database,
            accept
                ? "UPDATE bookings SET decision_status = 'confirmed', decision_at = ?1, "
                  "hold_expires_at = NULL, rejection_reason = '' WHERE id = ?2 AND decision_status = 'pending';"
                : "UPDATE bookings SET decision_status = 'rejected', decision_at = ?1, "
                  "hold_expires_at = NULL, rejection_reason = ?3 WHERE id = ?2 AND decision_status = 'pending';",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, decision_at_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, booking_id) != SQLITE_OK ||
        (!accept && sqlite3_bind_text(statement, 3, rejection_reason, -1, SQLITE_TRANSIENT) != SQLITE_OK)) {
        set_sqlite_error("Terminentscheidung konnte nicht vorbereitet werden");
        goto rollback;
    }

    if (sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(calendar_database) != 1) {
        set_sqlite_error("Terminentscheidung konnte nicht gespeichert werden");
        goto rollback;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (calendar_database_commit() != 0) {
        calendar_database_rollback();
        return CALENDAR_BOOKING_DECISION_ERROR;
    }

    return CALENDAR_BOOKING_DECISION_OK;

rollback:
    sqlite3_finalize(statement);
    calendar_database_rollback();
    return result;
}
