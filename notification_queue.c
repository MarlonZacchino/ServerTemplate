#include "notification_queue.h"

#include "calendar_time.h"
#include "contact_validation.h"
#include "customer_portal.h"
#include "notification_settings.h"
#include "notification_templates.h"
#include "server_config.h"

#include <sodium.h>

#include <stdarg.h>
#include <stdbool.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NOTIFICATION_DB_ERROR_SIZE 512
#define NOTIFICATION_MAX_REMINDER_CANDIDATES 1024

typedef struct notification_booking_data {
    int64_t id;
    char customer_name[256];
    char email[NOTIFICATION_EMAIL_SIZE];
    char dog_name[256];
    char appointment_date[11];
    int start_minute;
    int end_minute;
    char decision_status[32];
    char rejection_reason[512];
    char service_name[128];
    char timezone[64];
    bool email_notifications_enabled;
} notification_booking_data;

static char queue_error[NOTIFICATION_DB_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            queue_error,
            sizeof(queue_error),
            "%s",
            message == NULL ? "Unbekannter Benachrichtigungsfehler" : message);
}

static void set_sqlite_error(sqlite3 *database, const char *context)
{
    snprintf(
            queue_error,
            sizeof(queue_error),
            "%s: %s",
            context == NULL ? "SQLite-Fehler" : context,
            database == NULL ? "Datenbank ist nicht geöffnet" : sqlite3_errmsg(database));
}

const char *notification_queue_last_error(void)
{
    return queue_error[0] == '\0' ? "Unbekannter Benachrichtigungsfehler" : queue_error;
}

static bool is_memory_database_path(const char *path)
{
    return path != NULL &&
           (strcmp(path, ":memory:") == 0 ||
            strncmp(path, "file::memory:", strlen("file::memory:")) == 0 ||
            (strncmp(path, "file:", strlen("file:")) == 0 && strstr(path, "mode=memory") != NULL));
}

static const char *database_open_path(void)
{
    if (strcmp(server_config_database_file(), ":memory:") == 0) {
        return "file:styles4dogs-runtime?mode=memory&cache=shared";
    }

    return server_config_database_file();
}

static int open_database(sqlite3 **out_database)
{
    sqlite3 *database = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;

    if (out_database == NULL) {
        set_error("Datenbankausgabe fehlt");
        return -1;
    }

    if (strncmp(database_open_path(), "file:", strlen("file:")) == 0) {
        flags |= SQLITE_OPEN_URI;
    }

    if (sqlite3_open_v2(database_open_path(), &database, flags, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Benachrichtigungsdatenbank konnte nicht geöffnet werden");
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK ||
        sqlite3_exec(database, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Benachrichtigungsdatenbank konnte nicht konfiguriert werden");
        sqlite3_close_v2(database);
        return -1;
    }

    if (is_memory_database_path(server_config_database_file()) &&
        sqlite3_exec(database, "PRAGMA journal_mode = MEMORY;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Speicherjournal konnte nicht aktiviert werden");
        sqlite3_close_v2(database);
        return -1;
    }

    *out_database = database;
    return 0;
}

static const char *column_text_or_empty(sqlite3_stmt *statement, int column)
{
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == NULL ? "" : (const char *)value;
}

static int copy_column(
        sqlite3_stmt *statement,
        int column,
        char *destination,
        size_t destination_size
)
{
    const char *value = column_text_or_empty(statement, column);
    int written = snprintf(destination, destination_size, "%s", value);

    return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

static bool booking_event_type_is_valid(const char *event_type)
{
    return event_type != NULL &&
           (strcmp(event_type, "booking_received") == 0 ||
            strcmp(event_type, "booking_confirmed") == 0 ||
            strcmp(event_type, "booking_rejected") == 0 ||
            strcmp(event_type, "appointment_reminder") == 0);
}

static bool internal_event_type_is_valid(const char *event_type)
{
    return booking_event_type_is_valid(event_type) ||
           (event_type != NULL && strcmp(event_type, "admin_new_booking") == 0);
}

static bool event_matches_booking(const char *event_type, const char *decision_status)
{
    if (strcmp(event_type, "booking_received") == 0)
        return strcmp(decision_status, "pending") == 0;
    if (strcmp(event_type, "booking_confirmed") == 0 ||
        strcmp(event_type, "appointment_reminder") == 0)
        return strcmp(decision_status, "confirmed") == 0;
    if (strcmp(event_type, "booking_rejected") == 0)
        return strcmp(decision_status, "rejected") == 0;
    if (strcmp(event_type, "admin_new_booking") == 0)
        return strcmp(decision_status, "pending") == 0 ||
               strcmp(decision_status, "confirmed") == 0;
    return false;
}

static int load_booking(
        sqlite3 *database,
        int64_t booking_id,
        notification_booking_data *data
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (database == NULL || booking_id <= 0 || data == NULL) {
        set_error("Ungültige Buchungsabfrage für Benachrichtigung");
        return -1;
    }

    memset(data, 0, sizeof(*data));

    if (sqlite3_prepare_v2(
            database,
            "SELECT b.id, b.customer_name, b.email, b.dog_name, "
            "       COALESCE(b.appointment_date, ''), COALESCE(b.start_minute, -1), "
            "       COALESCE(b.end_minute, -1), b.decision_status, b.rejection_reason, "
            "       CASE WHEN b.service_name_snapshot <> '' THEN b.service_name_snapshot ELSE b.service END, "
            "       s.timezone, s.email_notifications_enabled "
            "FROM bookings b CROSS JOIN calendar_settings s "
            "WHERE b.id = ?1 AND s.id = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK) {
        set_sqlite_error(database, "Buchungsdaten für Nachricht konnten nicht vorbereitet werden");
        sqlite3_finalize(statement);
        return -1;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return 1;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error(database, "Buchungsdaten für Nachricht konnten nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    data->id = sqlite3_column_int64(statement, 0);
    data->start_minute = sqlite3_column_int(statement, 5);
    data->end_minute = sqlite3_column_int(statement, 6);
    data->email_notifications_enabled = sqlite3_column_int(statement, 11) != 0;

    if (copy_column(statement, 1, data->customer_name, sizeof(data->customer_name)) != 0 ||
        copy_column(statement, 2, data->email, sizeof(data->email)) != 0 ||
        copy_column(statement, 3, data->dog_name, sizeof(data->dog_name)) != 0 ||
        copy_column(statement, 4, data->appointment_date, sizeof(data->appointment_date)) != 0 ||
        copy_column(statement, 7, data->decision_status, sizeof(data->decision_status)) != 0 ||
        copy_column(statement, 8, data->rejection_reason, sizeof(data->rejection_reason)) != 0 ||
        copy_column(statement, 9, data->service_name, sizeof(data->service_name)) != 0 ||
        copy_column(statement, 10, data->timezone, sizeof(data->timezone)) != 0) {
        set_error("Buchungsdaten für Nachricht sind zu lang");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

static int append_text(
        char *buffer,
        size_t buffer_size,
        size_t *position,
        const char *text
)
{
    size_t length;

    if (buffer == NULL || position == NULL || text == NULL || *position >= buffer_size) {
        return -1;
    }

    length = strlen(text);
    if (length >= buffer_size - *position) {
        return -1;
    }

    memcpy(buffer + *position, text, length);
    *position += length;
    buffer[*position] = '\0';
    return 0;
}

static int append_format(
        char *buffer,
        size_t buffer_size,
        size_t *position,
        const char *format,
        ...
)
{
    va_list arguments;
    int written;

    if (buffer == NULL || position == NULL || format == NULL || *position >= buffer_size) {
        return -1;
    }

    va_start(arguments, format);
    written = vsnprintf(buffer + *position, buffer_size - *position, format, arguments);
    va_end(arguments);

    if (written < 0 || (size_t)written >= buffer_size - *position) {
        return -1;
    }

    *position += (size_t)written;
    return 0;
}

static int append_ics_escaped(
        char *buffer,
        size_t buffer_size,
        size_t *position,
        const char *text
)
{
    if (text == NULL) {
        return 0;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        char character = text[index];

        if (character == '\\' || character == ';' || character == ',') {
            if (append_text(buffer, buffer_size, position, "\\") != 0) {
                return -1;
            }
        }

        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            if (append_text(buffer, buffer_size, position, "\\n") != 0) {
                return -1;
            }
            continue;
        }

        char one[2] = {character, '\0'};
        if (append_text(buffer, buffer_size, position, one) != 0) {
            return -1;
        }
    }

    return 0;
}

static int compact_utc_timestamp(const char *timestamp, char out_timestamp[17])
{
    if (!calendar_utc_timestamp_is_valid(timestamp)) {
        return -1;
    }

    snprintf(
            out_timestamp,
            17,
            "%.4s%.2s%.2sT%.2s%.2s%.2sZ",
            timestamp,
            timestamp + 5,
            timestamp + 8,
            timestamp + 11,
            timestamp + 14,
            timestamp + 17);
    return 0;
}

static int appointment_epoch(
        const char *timezone,
        const char *date,
        int minute,
        time_t *out_epoch
)
{
    char effective_date[11];
    int effective_minute = minute;

    if (minute == 1440) {
        if (calendar_date_add_days(date, 1, effective_date) != 0) {
            return -1;
        }
        effective_minute = 0;
    } else {
        if (minute < 0 || minute > 1439) {
            return -1;
        }
        snprintf(effective_date, sizeof(effective_date), "%s", date);
    }

    return calendar_local_datetime_to_epoch(
            timezone,
            effective_date,
            effective_minute,
            out_epoch);
}

static int compact_epoch(time_t epoch, char out_timestamp[17])
{
    struct tm utc;

    if (gmtime_r(&epoch, &utc) == NULL) {
        return -1;
    }

    return strftime(out_timestamp, 17, "%Y%m%dT%H%M%SZ", &utc) == 16 ? 0 : -1;
}

static int build_ics(
        const notification_booking_data *data,
        const char *now_utc,
        char *out_ics,
        size_t out_size
)
{
    char stamp[17];
    char start_utc[17];
    char end_utc[17];
    time_t start_epoch;
    time_t end_epoch;
    size_t position = 0;

    if (data == NULL || out_ics == NULL ||
        compact_utc_timestamp(now_utc, stamp) != 0 ||
        appointment_epoch(
                data->timezone,
                data->appointment_date,
                data->start_minute,
                &start_epoch) != 0 ||
        appointment_epoch(
                data->timezone,
                data->appointment_date,
                data->end_minute,
                &end_epoch) != 0 ||
        end_epoch <= start_epoch ||
        compact_epoch(start_epoch, start_utc) != 0 ||
        compact_epoch(end_epoch, end_utc) != 0) {
        return -1;
    }

    out_ics[0] = '\0';
    if (append_text(out_ics, out_size, &position,
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Styling 4 Dogs//Terminbuchung//DE\r\n"
            "CALSCALE:GREGORIAN\r\nMETHOD:PUBLISH\r\nBEGIN:VEVENT\r\n") != 0 ||
        append_format(out_ics, out_size, &position,
            "UID:styles4dogs-booking-%lld@styles4dogs\r\nDTSTAMP:%s\r\n",
            (long long)data->id,
            stamp) != 0 ||
        append_format(out_ics, out_size, &position,
            "DTSTART:%s\r\nDTEND:%s\r\nSUMMARY:",
            start_utc,
            end_utc) != 0 ||
        append_ics_escaped(out_ics, out_size, &position, data->service_name) != 0 ||
        append_text(out_ics, out_size, &position, " bei ") != 0 ||
        append_ics_escaped(out_ics, out_size, &position, server_config_salon_name()) != 0 ||
        append_text(out_ics, out_size, &position, "\r\nDESCRIPTION:Termin für ") != 0 ||
        append_ics_escaped(out_ics, out_size, &position,
                data->dog_name[0] == '\0' ? "den Hund" : data->dog_name) != 0) {
        return -1;
    }

    if (server_config_salon_address()[0] != '\0') {
        if (append_text(out_ics, out_size, &position, "\r\nLOCATION:") != 0 ||
            append_ics_escaped(out_ics, out_size, &position, server_config_salon_address()) != 0) {
            return -1;
        }
    }

    return append_text(
            out_ics,
            out_size,
            &position,
            "\r\nSTATUS:CONFIRMED\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
}

static int build_payload(
        const notification_booking_data *data,
        const char *event_type,
        const char *now_utc,
        char subject[NOTIFICATION_SUBJECT_SIZE],
        char body[NOTIFICATION_BODY_SIZE],
        char ics[NOTIFICATION_ICS_SIZE]
)
{
    notification_template template_value;
    notification_template_context context;
    char start[6];
    char end[6];
    char appointment_date_display[48];
    char booking_id[32];
    char rejection_reason[640];
    char booking_url[CUSTOMER_PORTAL_URL_SIZE];

    if (data == NULL || !internal_event_type_is_valid(event_type) ||
        calendar_time_format_hhmm(data->start_minute, start) != 0 ||
        calendar_time_format_hhmm(data->end_minute, end) != 0 ||
        notification_template_get(event_type, &template_value) != 0 ||
        calendar_date_format_de(
                data->appointment_date,
                true,
                appointment_date_display,
                sizeof(appointment_date_display)) != 0 ||
        customer_portal_build_url(data->id, booking_url, sizeof(booking_url)) != 0) {
        return -1;
    }

    snprintf(booking_id, sizeof(booking_id), "%lld", (long long)data->id);
    if (data->rejection_reason[0] == '\0') {
        snprintf(rejection_reason, sizeof(rejection_reason),
                 "%s", "Es wurde kein weiterer Grund angegeben.");
    } else {
        snprintf(rejection_reason, sizeof(rejection_reason),
                 "Grund: %s", data->rejection_reason);
    }

    context.customer_name = data->customer_name;
    context.booking_id = booking_id;
    context.appointment_date = appointment_date_display;
    context.start_time = start;
    context.end_time = end;
    context.service_name = data->service_name[0] == '\0'
                           ? "Nicht angegeben" : data->service_name;
    context.dog_name = data->dog_name[0] == '\0'
                       ? "Nicht angegeben" : data->dog_name;
    context.rejection_reason = rejection_reason;
    context.salon_name = server_config_salon_name();
    context.salon_address = server_config_salon_address();
    context.salon_phone = server_config_salon_phone();
    context.website_url = server_config_public_base_url();
    context.booking_url = booking_url;

    if (notification_template_render(&template_value, &context, subject, body) != 0)
        return -1;

    if (booking_event_type_is_valid(event_type) && strstr(body, booking_url) == NULL) {
        size_t body_length = strlen(body);
        int written = snprintf(
                body + body_length,
                NOTIFICATION_BODY_SIZE - body_length,
                "\n\nBuchung ansehen oder absagen:\n%s",
                booking_url);
        if (written < 0 || (size_t)written >= NOTIFICATION_BODY_SIZE - body_length) {
            set_error("Kundenlink passt nicht in die Benachrichtigung");
            return -1;
        }
    }

    ics[0] = '\0';
    if ((strcmp(event_type, "booking_confirmed") == 0 ||
         strcmp(event_type, "appointment_reminder") == 0) &&
        build_ics(data, now_utc, ics, NOTIFICATION_ICS_SIZE) != 0) {
        return -1;
    }
    return 0;
}

static int insert_job(
        sqlite3 *database,
        const int64_t *booking_id,
        const char *event_type,
        const char *recipient,
        const char *subject,
        const char *body,
        const char *ics,
        const char *now_utc
)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (sqlite3_prepare_v2(
            database,
            "INSERT OR IGNORE INTO notification_jobs("
            "booking_id, event_type, recipient_email, subject, body_text, ics_content, "
            "status, attempts, available_at, created_at, last_error"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, 'pending', 0, ?7, ?7, '');",
            -1, &statement, NULL) != SQLITE_OK ||
        (booking_id == NULL
         ? sqlite3_bind_null(statement, 1)
         : sqlite3_bind_int64(statement, 1, *booking_id)) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, event_type, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 3, recipient, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 4, subject, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 5, body, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 6, ics, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 7, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error(database, "Benachrichtigung konnte nicht vorbereitet werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Benachrichtigung konnte nicht eingereiht werden");
        goto cleanup;
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static int enqueue_event_with_database(
        sqlite3 *database,
        int64_t booking_id,
        const char *event_type,
        const char *now_utc
)
{
    notification_booking_data data;
    char subject[NOTIFICATION_SUBJECT_SIZE];
    char body[NOTIFICATION_BODY_SIZE];
    char ics[NOTIFICATION_ICS_SIZE];
    int load_result;

    if (!internal_event_type_is_valid(event_type) ||
        !calendar_utc_timestamp_is_valid(now_utc)) {
        set_error("Ungültiger Benachrichtigungstyp oder Zeitpunkt");
        return -1;
    }

    load_result = load_booking(database, booking_id, &data);
    if (load_result != 0) return load_result < 0 ? -1 : 0;
    if (!event_matches_booking(event_type, data.decision_status)) return 0;

    if (strcmp(event_type, "admin_new_booking") == 0) {
        notification_smtp_settings smtp;
        int result;

        if (notification_settings_load(&smtp) != 0) {
            set_error(notification_settings_last_error());
            return -1;
        }
        if (!smtp.enabled || !smtp.notify_admin_on_new_booking ||
            smtp.admin_email[0] == '\0') {
            sodium_memzero(&smtp, sizeof(smtp));
            return 0;
        }
        if (build_payload(&data, event_type, now_utc, subject, body, ics) != 0) {
            sodium_memzero(&smtp, sizeof(smtp));
            set_error(notification_templates_last_error());
            return -1;
        }
        result = insert_job(database, &booking_id, event_type, smtp.admin_email,
                            subject, body, ics, now_utc);
        sodium_memzero(&smtp, sizeof(smtp));
        return result;
    }

    if (!data.email_notifications_enabled || data.email[0] == '\0') return 0;
    if (build_payload(&data, event_type, now_utc, subject, body, ics) != 0) {
        set_error(notification_templates_last_error());
        return -1;
    }
    return insert_job(database, &booking_id, event_type, data.email,
                      subject, body, ics, now_utc);
}

static int current_clock(sqlite3 *database, calendar_clock_snapshot *snapshot)
{
    sqlite3_stmt *statement = NULL;
    const char *timezone;
    int result = -1;

    if (sqlite3_prepare_v2(
            database,
            "SELECT timezone FROM calendar_settings WHERE id = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK || sqlite3_step(statement) != SQLITE_ROW) {
        set_sqlite_error(database, "Kalenderzeitzone konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    timezone = column_text_or_empty(statement, 0);
    if (calendar_clock_now(timezone, snapshot) != 0) {
        set_error("Aktuelle Salonzeit konnte nicht bestimmt werden");
    } else {
        result = 0;
    }

    sqlite3_finalize(statement);
    return result;
}

int notification_queue_enqueue_booking_event(
        int64_t booking_id,
        const char *event_type
)
{
    sqlite3 *database = NULL;
    calendar_clock_snapshot snapshot;
    int result;

    queue_error[0] = '\0';
    if (booking_id <= 0 || !booking_event_type_is_valid(event_type) ||
        open_database(&database) != 0) {
        return -1;
    }

    if (current_clock(database, &snapshot) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    result = enqueue_event_with_database(database, booking_id, event_type, snapshot.now_utc);
    if (result == 0 &&
        (strcmp(event_type, "booking_received") == 0 ||
         strcmp(event_type, "booking_confirmed") == 0)) {
        result = enqueue_event_with_database(
                database, booking_id, "admin_new_booking", snapshot.now_utc);
    }
    sqlite3_close_v2(database);
    return result;
}

int notification_queue_enqueue_due_reminders(void)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    calendar_clock_snapshot snapshot;
    int reminder_lead_minutes;
    bool enabled;
    int64_t booking_ids[NOTIFICATION_MAX_REMINDER_CANDIDATES];
    size_t count = 0;
    int step_result;
    time_t now = time(NULL);
    int result = -1;

    queue_error[0] = '\0';
    if (now == (time_t)-1 || open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT email_notifications_enabled, reminder_enabled, reminder_lead_minutes "
            "FROM calendar_settings WHERE id = 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK || sqlite3_step(statement) != SQLITE_ROW) {
        set_sqlite_error(database, "Erinnerungseinstellungen konnten nicht gelesen werden");
        goto cleanup;
    }

    enabled = sqlite3_column_int(statement, 0) != 0 && sqlite3_column_int(statement, 1) != 0;
    reminder_lead_minutes = sqlite3_column_int(statement, 2);
    sqlite3_finalize(statement);
    statement = NULL;

    if (!enabled) {
        result = 0;
        goto cleanup;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT b.id, b.appointment_date, b.start_minute, s.timezone "
            "FROM bookings b CROSS JOIN calendar_settings s "
            "WHERE b.decision_status = 'confirmed' "
            "  AND b.contact_channel = 'email' AND b.email <> '' "
            "  AND b.appointment_date >= ?1 "
            "ORDER BY b.appointment_date, b.start_minute;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, snapshot.local_date, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error(database, "Erinnerungskandidaten konnten nicht vorbereitet werden");
        goto cleanup;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const char *date = column_text_or_empty(statement, 1);
        const char *timezone = column_text_or_empty(statement, 3);
        int start_minute = sqlite3_column_int(statement, 2);
        time_t appointment_epoch;
        double seconds_until;

        if (calendar_local_datetime_to_epoch(timezone, date, start_minute, &appointment_epoch) != 0) {
            continue;
        }

        seconds_until = difftime(appointment_epoch, now);
        if (seconds_until > 0.0 && seconds_until <= (double)reminder_lead_minutes * 60.0) {
            if (count >= NOTIFICATION_MAX_REMINDER_CANDIDATES) {
                set_error("Zu viele gleichzeitig fällige Erinnerungen");
                goto cleanup;
            }
            booking_ids[count++] = sqlite3_column_int64(statement, 0);
        }
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error(database, "Erinnerungskandidaten konnten nicht vollständig gelesen werden");
        goto cleanup;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    for (size_t index = 0; index < count; index++) {
        if (enqueue_event_with_database(
                database,
                booking_ids[index],
                "appointment_reminder",
                snapshot.now_utc) != 0) {
            goto cleanup;
        }
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

static int execute_simple(sqlite3 *database, const char *sql)
{
    char *error_message = NULL;
    int sqlite_result = sqlite3_exec(database, sql, NULL, NULL, &error_message);

    if (sqlite_result != SQLITE_OK) {
        snprintf(
                queue_error,
                sizeof(queue_error),
                "SQLite-Anweisung fehlgeschlagen: %s",
                error_message == NULL ? sqlite3_errmsg(database) : error_message);
        sqlite3_free(error_message);
        return -1;
    }

    return 0;
}

int notification_queue_claim_next(notification_job *job)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    calendar_clock_snapshot snapshot;
    char stale_before[21];
    int step_result;
    int result = -1;

    queue_error[0] = '\0';
    if (job == NULL || open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0 ||
        calendar_utc_add_minutes(snapshot.now_utc, -15, stale_before) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    memset(job, 0, sizeof(*job));

    if (execute_simple(database, "BEGIN IMMEDIATE;") != 0) {
        goto cleanup;
    }

    if (sqlite3_prepare_v2(
            database,
            "UPDATE notification_jobs SET status = 'failed', claimed_at = NULL, "
            "last_error = 'Vorheriger Worker wurde unterbrochen' "
            "WHERE status = 'processing' AND claimed_at <= ?1;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, stale_before, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Unterbrochene Benachrichtigungen konnten nicht freigegeben werden");
        goto rollback;
    }
    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            "SELECT id, booking_id, event_type, recipient_email, subject, body_text, "
            "       ics_content, attempts "
            "FROM notification_jobs "
            "WHERE status IN ('pending', 'failed') AND attempts < 5 AND available_at <= ?1 "
            "ORDER BY available_at, id LIMIT 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, snapshot.now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error(database, "Nächste Benachrichtigung konnte nicht vorbereitet werden");
        goto rollback;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        statement = NULL;
        if (execute_simple(database, "COMMIT;") != 0) {
            goto cleanup;
        }
        result = 1;
        goto cleanup;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error(database, "Nächste Benachrichtigung konnte nicht gelesen werden");
        goto rollback;
    }

    job->id = sqlite3_column_int64(statement, 0);
    job->booking_id = sqlite3_column_int64(statement, 1);
    job->attempts = sqlite3_column_int(statement, 7) + 1;
    if (copy_column(statement, 2, job->event_type, sizeof(job->event_type)) != 0 ||
        copy_column(statement, 3, job->recipient_email, sizeof(job->recipient_email)) != 0 ||
        copy_column(statement, 4, job->subject, sizeof(job->subject)) != 0 ||
        copy_column(statement, 5, job->body_text, sizeof(job->body_text)) != 0 ||
        copy_column(statement, 6, job->ics_content, sizeof(job->ics_content)) != 0) {
        set_error("Benachrichtigungsjob ist zu groß");
        goto rollback;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (sqlite3_prepare_v2(
            database,
            "UPDATE notification_jobs SET status = 'processing', attempts = attempts + 1, "
            "claimed_at = ?1, last_error = '' WHERE id = ?2 AND status IN ('pending', 'failed');",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, snapshot.now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, job->id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {
        set_sqlite_error(database, "Benachrichtigungsjob konnte nicht beansprucht werden");
        goto rollback;
    }

    sqlite3_finalize(statement);
    statement = NULL;
    if (execute_simple(database, "COMMIT;") != 0) {
        goto cleanup;
    }

    result = 0;
    goto cleanup;

rollback:
    sqlite3_finalize(statement);
    statement = NULL;
    sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

int notification_queue_mark_sent(int64_t job_id)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    calendar_clock_snapshot snapshot;
    int result = -1;

    queue_error[0] = '\0';
    if (job_id <= 0 || open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "UPDATE notification_jobs SET status = 'sent', sent_at = ?1, claimed_at = NULL, "
            "recipient_email = '', body_text = '', ics_content = '', last_error = '' "
            "WHERE id = ?2 AND status = 'processing';",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, snapshot.now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, job_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {
        set_sqlite_error(database, "Gesendete Benachrichtigung konnte nicht abgeschlossen werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

static void sanitize_error_message(const char *source, char destination[NOTIFICATION_ERROR_SIZE])
{
    size_t output = 0;

    if (source == NULL) {
        source = "Unbekannter Versandfehler";
    }

    for (size_t index = 0; source[index] != '\0' && output + 1 < NOTIFICATION_ERROR_SIZE; index++) {
        char character = source[index];
        destination[output++] = character == '\r' || character == '\n' ? ' ' : character;
    }
    destination[output] = '\0';
}

int notification_queue_mark_failed(int64_t job_id, const char *error_message)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    calendar_clock_snapshot snapshot;
    char next_attempt[21];
    char sanitized[NOTIFICATION_ERROR_SIZE];
    int attempts;
    int delay_minutes;
    int result = -1;

    queue_error[0] = '\0';
    if (job_id <= 0 || open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT attempts FROM notification_jobs WHERE id = ?1 AND status = 'processing';",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, job_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) {
        set_sqlite_error(database, "Fehlgeschlagener Job konnte nicht geladen werden");
        goto cleanup;
    }

    attempts = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    statement = NULL;

    if (attempts <= 1) {
        delay_minutes = 5;
    } else if (attempts == 2) {
        delay_minutes = 15;
    } else if (attempts == 3) {
        delay_minutes = 60;
    } else if (attempts == 4) {
        delay_minutes = 240;
    } else {
        delay_minutes = 720;
    }

    if (calendar_utc_add_minutes(snapshot.now_utc, delay_minutes, next_attempt) != 0) {
        set_error("Nächster Versandzeitpunkt konnte nicht berechnet werden");
        goto cleanup;
    }

    sanitize_error_message(error_message, sanitized);
    if (sqlite3_prepare_v2(
            database,
            "UPDATE notification_jobs SET status = 'failed', available_at = ?1, "
            "claimed_at = NULL, last_error = ?2 WHERE id = ?3 AND status = 'processing';",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, next_attempt, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, sanitized, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, job_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {
        set_sqlite_error(database, "Fehlgeschlagener Job konnte nicht zurückgestellt werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

int notification_queue_enqueue_test_email(const char *recipient_email)
{
    sqlite3 *database = NULL;
    calendar_clock_snapshot snapshot;
    notification_smtp_settings smtp;
    char subject[NOTIFICATION_SUBJECT_SIZE];
    char body[NOTIFICATION_BODY_SIZE];
    const char *recipient;
    int result;

    queue_error[0] = '\0';
    if (notification_settings_load(&smtp) != 0) {
        set_error(notification_settings_last_error());
        return -1;
    }
    if (!smtp.enabled) {
        sodium_memzero(&smtp, sizeof(smtp));
        set_error("Es ist noch kein aktives E-Mail-Konto verbunden");
        return -1;
    }

    recipient = recipient_email != NULL && recipient_email[0] != '\0'
                ? recipient_email : smtp.admin_email;
    if (!contact_email_is_valid(recipient)) {
        sodium_memzero(&smtp, sizeof(smtp));
        set_error("Empfängeradresse der Testmail ist ungültig");
        return -1;
    }

    if (snprintf(subject, sizeof(subject), "Testmail – %s",
                 server_config_salon_name()) < 0 ||
        snprintf(body, sizeof(body),
                 "Diese Testmail bestätigt, dass die E-Mail-Verbindung für %s eingerichtet wurde.\n\n"
                 "Wenn diese Nachricht angekommen ist, können automatische Terminbestätigungen, "
                 "Absagen und Erinnerungen verschickt werden.\n",
                 server_config_salon_name()) < 0 ||
        open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0) {
        sodium_memzero(&smtp, sizeof(smtp));
        sqlite3_close_v2(database);
        return -1;
    }

    result = insert_job(database, NULL, "smtp_test", recipient,
                        subject, body, "", snapshot.now_utc);
    sqlite3_close_v2(database);
    sodium_memzero(&smtp, sizeof(smtp));
    return result;
}

int notification_queue_retry_failed(void)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    calendar_clock_snapshot snapshot;
    int result = -1;

    queue_error[0] = '\0';
    if (open_database(&database) != 0 ||
        current_clock(database, &snapshot) != 0) {
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "UPDATE notification_jobs SET status='pending', attempts=0, "
            "available_at=?1, claimed_at=NULL, last_error='' WHERE status='failed';",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, snapshot.now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Fehlgeschlagene Nachrichten konnten nicht zurückgestellt werden");
        goto cleanup;
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

static int delete_jobs_with_status_filter(const char *where_clause)
{
    sqlite3 *database = NULL;
    char sql[256];
    int written;
    int result;

    queue_error[0] = '\0';
    if (where_clause == NULL ||
        (strcmp(where_clause, "status='sent'") != 0 &&
         strcmp(where_clause, "status='failed'") != 0 &&
         strcmp(where_clause, "status IN ('sent','failed')") != 0) ||
        open_database(&database) != 0) {
        sqlite3_close_v2(database);
        if (where_clause == NULL) {
            set_error("Ungültige Bereinigung der E-Mail-Warteschlange");
        }
        return -1;
    }

    written = snprintf(
            sql,
            sizeof(sql),
            "DELETE FROM notification_jobs WHERE %s;",
            where_clause);
    if (written < 0 || (size_t)written >= sizeof(sql)) {
        sqlite3_close_v2(database);
        set_error("Bereinigungsanweisung ist zu lang");
        return -1;
    }

    result = execute_simple(database, sql);
    sqlite3_close_v2(database);
    return result;
}

int notification_queue_clear_sent(void)
{
    return delete_jobs_with_status_filter("status='sent'");
}

int notification_queue_clear_failed(void)
{
    return delete_jobs_with_status_filter("status='failed'");
}

int notification_queue_clear_completed(void)
{
    return delete_jobs_with_status_filter("status IN ('sent','failed')");
}

int notification_queue_get_counts(notification_queue_counts *counts)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result = -1;

    queue_error[0] = '\0';
    if (counts == NULL || open_database(&database) != 0) {
        return -1;
    }

    memset(counts, 0, sizeof(*counts));
    if (sqlite3_prepare_v2(
            database,
            "SELECT "
            "SUM(CASE WHEN status IN ('pending', 'processing') THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN status = 'failed' THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN status = 'sent' THEN 1 ELSE 0 END) "
            "FROM notification_jobs;",
            -1,
            &statement,
            NULL) != SQLITE_OK || sqlite3_step(statement) != SQLITE_ROW) {
        set_sqlite_error(database, "Benachrichtigungszähler konnten nicht gelesen werden");
        goto cleanup;
    }

    counts->pending = (size_t)sqlite3_column_int64(statement, 0);
    counts->failed = (size_t)sqlite3_column_int64(statement, 1);
    counts->sent = (size_t)sqlite3_column_int64(statement, 2);
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}
