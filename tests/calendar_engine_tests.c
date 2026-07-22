#include "availability.h"
#include "booking_database.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "notification_settings.h"
#include "notification_templates.h"
#include "server_config.h"

#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures = 0;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void expect_int(int actual, int expected, const char *message)
{
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (erwartet %d, erhalten %d)\n", message, expected, actual);
        failures++;
    }
}

static int query_single_int(const char *sql, int *out_value)
{
    sqlite3 *connection = NULL;
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (sqlite3_open_v2(
            server_config_database_file(),
            &connection,
            SQLITE_OPEN_READWRITE,
            NULL) != SQLITE_OK) {
        goto cleanup;
    }

    if (sqlite3_prepare_v2(connection, sql, -1, &statement, NULL) != SQLITE_OK) {
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_ROW) {
        goto cleanup;
    }

    *out_value = sqlite3_column_int(statement, 0);
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    if (connection != NULL) {
        sqlite3_close(connection);
    }
    return result;
}

static int execute_direct_sql(const char *sql)
{
    sqlite3 *connection = NULL;
    char *error_message = NULL;
    int result = -1;

    if (sqlite3_open_v2(
            server_config_database_file(),
            &connection,
            SQLITE_OPEN_READWRITE,
            NULL) != SQLITE_OK) {
        goto cleanup;
    }

    if (sqlite3_exec(connection, sql, NULL, NULL, &error_message) == SQLITE_OK) {
        result = 0;
    }

cleanup:
    sqlite3_free(error_message);
    if (connection != NULL) {
        sqlite3_close(connection);
    }
    return result;
}

static availability_query make_query(
        const char *service,
        const char *date,
        const char *current_date,
        int current_minute,
        const char *now_utc
)
{
    availability_query query = {
            .service_code = service,
            .date = date,
            .current_date = current_date,
            .current_minute = current_minute,
            .now_utc = now_utc
    };

    return query;
}

static size_t collect_slots(const availability_query *query, availability_slot *slots)
{
    size_t count = 0;

    if (availability_collect(query, slots, AVAILABILITY_MAX_SLOTS, &count) != 0) {
        fprintf(stderr, "Verfügbarkeitsfehler: %s\n", availability_last_error());
        fail("Freie Termine konnten nicht berechnet werden");
        return 0;
    }

    return count;
}

int main(void)
{
    booking_request old_request = {
            .name = "Bestehende Anfrage",
            .contact = "legacy@example.invalid",
            .dog_name = "Waldi",
            .dog_size = "medium",
            .service = "wash_dry",
            .preferred_date = "2026-08-03",
            .message = "Vor der Kalendermigration gespeichert"
    };
    calendar_settings settings;
    calendar_service service;
    calendar_closure closure;
    availability_slot slots[AVAILABILITY_MAX_SLOTS];
    availability_query query;
    availability_reservation_request reservation;
    int schema_version = 0;
    int value = 0;
    int64_t first_booking_id = 0;
    int64_t second_booking_id = 0;
    size_t slot_count;

    {
        char next_date[11];
        char next_timestamp[21];
        char time_text[6];
        char display_date[48];
        int parsed_minute = -1;

        expect_int(calendar_date_add_days("2026-02-28", 1, next_date), 0,
                   "Datumsaddition funktioniert");
        expect_true(strcmp(next_date, "2026-03-01") == 0,
                    "Datumsaddition überschreitet Monatsgrenzen korrekt");
        expect_int(calendar_utc_add_minutes(
                "2026-12-31T23:30:00Z", 90, next_timestamp), 0,
                "UTC-Minutenaddition funktioniert");
        expect_true(strcmp(next_timestamp, "2027-01-01T01:00:00Z") == 0,
                    "UTC-Minutenaddition überschreitet Jahresgrenzen korrekt");
        expect_int(calendar_time_parse_hhmm("09:45", &parsed_minute), 0,
                   "HH:MM wird geparst");
        expect_int(parsed_minute, 585, "09:45 entspricht 585 Minuten");
        expect_int(calendar_time_format_hhmm(parsed_minute, time_text), 0,
                   "Minuten werden als HH:MM formatiert");
        expect_true(strcmp(time_text, "09:45") == 0,
                    "Zeitformatierung ist stabil");
        expect_int(calendar_date_format_de(
                "2026-07-21", true, display_date, sizeof(display_date)), 0,
                "Deutsches Termindatum wird formatiert");
        expect_true(strcmp(display_date, "21.07.2026 - Dienstag") == 0,
                    "Termindatum enthält Datum und deutschen Wochentag");

        {
            time_t winter_epoch;
            time_t summer_epoch;

            expect_int(calendar_local_datetime_to_epoch(
                    "Europe/Berlin", "2026-01-15", 540, &winter_epoch), 0,
                    "Wintertermin wird in einen UTC-Zeitpunkt umgerechnet");
            expect_int(calendar_local_datetime_to_epoch(
                    "Europe/Berlin", "2026-07-15", 540, &summer_epoch), 0,
                    "Sommertermin wird in einen UTC-Zeitpunkt umgerechnet");
            expect_true(winter_epoch != summer_epoch,
                        "Unterschiedliche Termine erhalten unterschiedliche Zeitpunkte");
        }
    }

    if (server_config_initialize() != 0) {
        fprintf(stderr, "Konfigurationsfehler: %s\n", server_config_last_error());
        return EXIT_FAILURE;
    }

    if (booking_database_initialize() != 0) {
        fprintf(stderr, "Buchungsdatenbankfehler: %s\n", booking_database_last_error());
        return EXIT_FAILURE;
    }

    expect_int(booking_database_insert(&old_request), 0, "Bestehende Anfrage wird vor Migration gespeichert");

    /*
     * Simuliert eine echte Phase-3-Datenbank. Die Tabelle existiert bereits,
     * besitzt aber die in Phase 4 ergänzte Spalte auto_confirm_bookings noch
     * nicht. Die Migration muss die Spalte vor dem Phase-4-Seed hinzufügen.
     */
    expect_int(execute_direct_sql(
            "CREATE TABLE calendar_settings ("
            "    id INTEGER PRIMARY KEY CHECK(id = 1),"
            "    timezone TEXT NOT NULL,"
            "    min_notice_minutes INTEGER NOT NULL,"
            "    booking_horizon_days INTEGER NOT NULL,"
            "    slot_interval_minutes INTEGER NOT NULL,"
            "    pending_hold_minutes INTEGER NOT NULL,"
            "    capacity INTEGER NOT NULL DEFAULT 1"
            ");"
            "INSERT INTO calendar_settings("
            "    id, timezone, min_notice_minutes, booking_horizon_days,"
            "    slot_interval_minutes, pending_hold_minutes, capacity"
            ") VALUES(1, 'Europe/Berlin', 1440, 90, 15, 1440, 1);"
            "PRAGMA user_version = 3;"),
            0,
            "Phase-3-Kalendereinstellungen werden für Migration vorbereitet");

    if (calendar_database_initialize() != 0) {
        fprintf(stderr, "Kalenderdatenbankfehler: %s\n", calendar_database_last_error());
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    expect_int(calendar_database_schema_version(&schema_version), 0, "Schema-Version ist lesbar");
    expect_int(schema_version, 7, "Kalenderschema verwendet Version 7");

    expect_int(query_single_int(
            "SELECT COUNT(*) FROM bookings "
            "WHERE customer_name = 'Bestehende Anfrage' "
            "AND decision_status = 'legacy' "
            "AND appointment_date IS NULL;",
            &value), 0, "Migrierte Altanfrage ist abfragbar");
    expect_int(value, 1, "Bestehende Anfrage wird als legacy migriert");

    expect_int(calendar_database_get_settings(&settings), 0, "Standard-Kalendereinstellungen sind vorhanden");
    expect_true(strcmp(settings.timezone, "Europe/Berlin") == 0, "Standard-Zeitzone ist Europe/Berlin");
    expect_int(settings.min_notice_minutes, 1440, "Standard-Vorlauf beträgt 24 Stunden");
    expect_int(settings.booking_horizon_days, 90, "Standard-Buchungshorizont beträgt 90 Tage");
    expect_int(settings.slot_interval_minutes, 15, "Standard-Zeitraster beträgt 15 Minuten");
    expect_int(settings.capacity, 1, "Kalender unterstützt genau einen parallelen Termin");
    expect_true(!settings.auto_confirm_bookings, "Automatische Bestätigung ist standardmäßig deaktiviert");
    expect_true(!settings.email_notifications_enabled,
                "E-Mail-Versand ist standardmäßig deaktiviert");
    expect_true(settings.reminder_enabled,
                "Terminerinnerungen sind standardmäßig vorbereitet");
    expect_int(settings.reminder_lead_minutes, 1440,
               "Standard-Erinnerung erfolgt 24 Stunden vorher");
    expect_int(query_single_int(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
            "AND name = 'notification_jobs';",
            &value), 0, "Benachrichtigungswarteschlange ist abfragbar");
    expect_int(value, 1, "Benachrichtigungswarteschlange wurde migriert");
    expect_int(query_single_int(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' "
            "AND name = 'gallery_images';",
            &value), 0, "Galerietabelle ist abfragbar");
    expect_int(value, 1, "Galerietabelle wurde migriert");
    expect_int(query_single_int(
            "SELECT COUNT(*) FROM notification_templates;",
            &value), 0, "Nachrichtenvorlagen sind abfragbar");
    expect_int(value, 5, "Fünf Standard-Nachrichtenvorlagen wurden angelegt");

    {
        notification_template template_value;
        notification_template_context context = {
            .customer_name = "Marlon Test",
            .booking_id = "42",
            .appointment_date = "21.08.2026",
            .start_time = "09:00",
            .end_time = "10:00",
            .service_name = "Komplettpflege",
            .dog_name = "Flocke",
            .rejection_reason = "Grund: Test",
            .salon_name = "Styling 4 Dogs",
            .salon_address = "Teststraße 1",
            .salon_phone = "02571 12345",
            .website_url = "https://example.invalid",
            .booking_url = "https://example.invalid/buchung/test-token"
    };
        char subject[NOTIFICATION_SUBJECT_SIZE];
        char body[NOTIFICATION_BODY_SIZE];

        expect_int(notification_template_get("booking_confirmed", &template_value), 0,
                   "Standard-Terminbestätigung ist lesbar");
        snprintf(template_value.subject_template, sizeof(template_value.subject_template),
                 "%s", "Termin für {{dog_name}} bestätigt");
        snprintf(template_value.body_template, sizeof(template_value.body_template),
                 "%s", "Hallo {{customer_name}}, Buchung {{booking_id}} ist bestätigt.");
        expect_int(notification_template_update(&template_value), 0,
                   "Nachrichtenvorlage kann individualisiert werden");
        expect_int(notification_template_render(&template_value, &context, subject, body), 0,
                   "Individualisierte Vorlage wird gerendert");
        expect_true(strcmp(subject, "Termin für Flocke bestätigt") == 0,
                    "Platzhalter im Betreff wird ersetzt");
        expect_true(strstr(body, "Marlon Test") != NULL && strstr(body, "42") != NULL,
                    "Platzhalter im Text werden ersetzt");

        snprintf(template_value.body_template, sizeof(template_value.body_template),
                 "%s", "{{unbekannt}}");
        expect_true(notification_template_update(&template_value) != 0,
                    "Unbekannte Platzhalter werden abgelehnt");
        expect_int(notification_template_reset("booking_confirmed"), 0,
                   "Vorlage kann auf Standard zurückgesetzt werden");
    }

    {
        notification_smtp_settings saved;
        notification_smtp_settings loaded;
        struct stat status;
        char path[1024];

        memset(&saved, 0, sizeof(saved));
        saved.enabled = true;
        saved.managed_by_admin = true;
        saved.notify_admin_on_new_booking = true;
        snprintf(saved.url, sizeof(saved.url), "%s", "smtps://smtp.example.invalid:465");
        snprintf(saved.username, sizeof(saved.username), "%s", "salon@example.invalid");
        snprintf(saved.password, sizeof(saved.password), "%s", "app-password-test");
        snprintf(saved.from_address, sizeof(saved.from_address), "%s", "salon@example.invalid");
        snprintf(saved.from_name, sizeof(saved.from_name), "%s", "Styles 4 Dogs");
        snprintf(saved.admin_email, sizeof(saved.admin_email), "%s", "admin@example.invalid");

        expect_int(notification_settings_save(&saved), 0,
                   "SMTP-Verbindung kann verschlüsselt gespeichert werden");
        memset(&loaded, 0, sizeof(loaded));
        expect_int(notification_settings_load(&loaded), 0,
                   "Verschlüsselte SMTP-Verbindung ist lesbar");
        expect_true(loaded.enabled && loaded.managed_by_admin &&
                    loaded.notify_admin_on_new_booking,
                    "SMTP-Status bleibt gespeichert");
        expect_true(strcmp(loaded.password, saved.password) == 0,
                    "SMTP-Passwort wird korrekt entschlüsselt");
        expect_true(strcmp(loaded.from_name, "Styling 4 Dogs") == 0,
                    "Ehemaliger Standard-Absendername wird auf Styling 4 Dogs migriert");

        snprintf(path, sizeof(path), "%s/notification.smtp", server_config_secrets_dir());
        expect_true(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600,
                    "SMTP-Konfiguration besitzt Dateimodus 0600");
        snprintf(path, sizeof(path), "%s/notification.key", server_config_secrets_dir());
        expect_true(stat(path, &status) == 0 && (status.st_mode & 0777) == 0600,
                    "SMTP-Schlüssel besitzt Dateimodus 0600");

        expect_int(notification_settings_disconnect(), 0,
                   "SMTP-Verbindung kann deaktiviert werden");
        memset(&loaded, 0, sizeof(loaded));
        expect_int(notification_settings_load(&loaded), 0,
                   "Deaktivierter Zustand ist lesbar");
        expect_true(!loaded.enabled && loaded.managed_by_admin,
                    "Deaktivieren verhindert den Umgebungsvariablen-Fallback");
    }

    expect_int(calendar_database_get_service("wash_dry", &service), 0, "Standardleistung wash_dry existiert");
    expect_int(service.duration_minutes, 60, "Waschen und Föhnen dauert standardmäßig 60 Minuten");
    expect_int(service.buffer_minutes, 15, "Waschen und Föhnen hat 15 Minuten Puffer");
    expect_true(service.active, "Waschen und Föhnen ist standardmäßig aktiv");

    settings.min_notice_minutes = 0;
    settings.booking_horizon_days = 120;
    settings.pending_hold_minutes = 60;
    settings.email_notifications_enabled = true;
    settings.reminder_enabled = true;
    settings.reminder_lead_minutes = 2880;
    expect_int(calendar_database_update_settings(&settings), 0, "Kalendereinstellungen sind änderbar");
    expect_int(calendar_database_get_settings(&settings), 0,
               "Erweiterte Kalendereinstellungen sind lesbar");
    expect_true(settings.email_notifications_enabled,
                "E-Mail-Versand bleibt gespeichert");
    expect_int(settings.reminder_lead_minutes, 2880,
               "Erinnerungsvorlauf bleibt gespeichert");

    query = make_query(
            "wash_dry",
            "2026-08-03",
            "2026-08-03",
            480,
            "2026-08-03T06:00:00Z");
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 0, "Kalender ist ohne konfigurierte Öffnungszeiten geschlossen");

    expect_int(calendar_database_clear_opening_hours(), 0, "Öffnungszeiten können geleert werden");
    expect_int(calendar_database_add_opening_period(1, 540, 720), 0, "Montagsöffnung 09:00 bis 12:00 wird gespeichert");
    expect_int(calendar_database_add_opening_period(1, 600, 780), 1, "Überlappende Öffnungszeit wird abgelehnt");

    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 8, "Dreistündige Öffnung erzeugt acht 15-Minuten-Startzeiten");
    if (slot_count >= 1) {
        expect_int(slots[0].start_minute, 540, "Erster Slot startet um 09:00");
        expect_int(slots[0].end_minute, 600, "Leistung endet um 10:00");
        expect_int(slots[0].blocked_until_minute, 615, "Puffer blockiert bis 10:15");
    }

    memset(&closure, 0, sizeof(closure));
    snprintf(closure.start_date, sizeof(closure.start_date), "%s", "2026-08-03");
    snprintf(closure.end_date, sizeof(closure.end_date), "%s", "2026-08-03");
    closure.start_minute = 660;
    closure.end_minute = 720;
    snprintf(closure.label, sizeof(closure.label), "%s", "Privater Termin");
    expect_int(calendar_database_add_closure(&closure, NULL), 0, "Teilweise Sperrzeit wird gespeichert");

    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 4, "Sperrzeit entfernt alle in 11:00 bis 12:00 hineinragenden Slots");
    expect_int(calendar_database_clear_closures(), 0, "Sperrzeiten können geleert werden");

    query = make_query(
            "wash_dry",
            "2026-08-10",
            "2026-08-09",
            480,
            "2026-08-09T06:00:00Z");
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 8, "Freier Folgemontag enthält alle acht Slots");

    memset(&reservation, 0, sizeof(reservation));
    reservation.query = query;
    reservation.start_minute = 540;
    reservation.created_at_utc = "2026-08-09T06:00:00Z";
    reservation.hold_expires_at_utc = "2026-08-09T07:00:00Z";
    reservation.customer_name = "Kalender Test";
    reservation.contact = "calendar@example.invalid";
    reservation.contact_channel = "email";
    reservation.email = "calendar@example.invalid";
    reservation.phone_number = "";
    reservation.phone_kind = "";
    reservation.contact_preference = "";
    reservation.auto_confirm = false;
    reservation.dog_name = "Flocke";
    reservation.dog_size = "small";
    reservation.message = "Transaktionssichere Reservierung";

    {
        availability_reservation_request invalid_contact = reservation;

        invalid_contact.contact = "+49 170 1234567";
        invalid_contact.contact_channel = "phone";
        invalid_contact.email = "";
        invalid_contact.phone_number = "+49 170 1234567";
        invalid_contact.phone_kind = "landline";
        invalid_contact.contact_preference = "whatsapp";
        expect_int(
                availability_reserve_pending(&invalid_contact, NULL),
                AVAILABILITY_RESERVATION_INVALID,
                "WhatsApp ist nur mit einer Mobilfunknummer zulässig");

        invalid_contact = reservation;
        invalid_contact.contact = "abweichend@example.invalid";
        expect_int(
                availability_reserve_pending(&invalid_contact, NULL),
                AVAILABILITY_RESERVATION_INVALID,
                "Zusammengefasster Kontakt muss zu den strukturierten Feldern passen");
    }

    expect_int(
            availability_reserve_pending(&reservation, &first_booking_id),
            AVAILABILITY_RESERVATION_OK,
            "Freier Slot wird vorläufig reserviert");
    expect_true(first_booking_id > 0, "Reservierung erhält eine Datenbank-ID");

    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 3, "Pending-Anfrage blockiert alle überlappenden Startzeiten");
    if (slot_count >= 1) {
        expect_int(slots[0].start_minute, 615, "Nächster freier Slot beginnt nach reserviertem Block");
    }

    expect_int(
            availability_reserve_pending(&reservation, NULL),
            AVAILABILITY_RESERVATION_UNAVAILABLE,
            "Derselbe Slot kann nicht doppelt reserviert werden");

    query.now_utc = "2026-08-09T08:00:00Z";
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 8, "Abgelaufene Pending-Anfrage blockiert keine Termine mehr");

    reservation.query = query;
    reservation.created_at_utc = "2026-08-09T08:00:00Z";
    reservation.hold_expires_at_utc = "2026-08-09T09:00:00Z";
    expect_int(
            availability_reserve_pending(&reservation, &second_booking_id),
            AVAILABILITY_RESERVATION_OK,
            "Abgelaufener Slot kann erneut reserviert werden");
    expect_true(second_booking_id > first_booking_id, "Neue Reservierung erhält eine neue ID");

    expect_int(
            calendar_database_decide_booking(
                    second_booking_id,
                    true,
                    "2026-08-09T08:05:00Z",
                    ""),
            CALENDAR_BOOKING_DECISION_OK,
            "Offene Terminanfrage kann angenommen werden");
    expect_int(query_single_int(
            "SELECT COUNT(*) FROM bookings WHERE decision_status = 'confirmed' "
            "AND appointment_date = '2026-08-10' AND start_minute = 540 "
            "AND hold_expires_at IS NULL AND decision_at = '2026-08-09T08:05:00Z';",
            &value), 0, "Angenommener Termin ist abfragbar");
    expect_int(value, 1, "Annahme speichert einen bestätigten Termin");
    expect_int(
            calendar_database_decide_booking(
                    second_booking_id,
                    false,
                    "2026-08-09T08:06:00Z",
                    "zu spät"),
            CALENDAR_BOOKING_DECISION_NOT_PENDING,
            "Bereits entschiedener Termin kann nicht erneut entschieden werden");

    reservation.start_minute = 615;
    reservation.created_at_utc = "2026-08-09T08:10:00Z";
    reservation.hold_expires_at_utc = "2026-08-09T09:10:00Z";
    {
        int64_t rejected_booking_id = 0;
        expect_int(
                availability_reserve_pending(&reservation, &rejected_booking_id),
                AVAILABILITY_RESERVATION_OK,
                "Weiterer freier Slot kann angefragt werden");
        expect_int(
                calendar_database_decide_booking(
                        rejected_booking_id,
                        false,
                        "2026-08-09T08:15:00Z",
                        "Termin nicht möglich"),
                CALENDAR_BOOKING_DECISION_OK,
                "Offene Terminanfrage kann abgelehnt werden");
        expect_int(query_single_int(
                "SELECT COUNT(*) FROM bookings WHERE decision_status = 'rejected' "
                "AND rejection_reason = 'Termin nicht möglich';",
                &value), 0, "Ablehnungsgrund ist abfragbar");
        expect_true(value >= 1, "Ablehnung und Ablehnungsgrund werden gespeichert");
    }

    settings.auto_confirm_bookings = true;
    expect_int(calendar_database_update_settings(&settings), 0,
               "Automatische Terminbestätigung kann aktiviert werden");
    query = make_query(
            "wash_dry",
            "2026-08-17",
            "2026-08-16",
            0,
            "2026-08-16T00:00:00Z");
    reservation.query = query;
    reservation.start_minute = 540;
    reservation.created_at_utc = "2026-08-16T00:00:00Z";
    reservation.hold_expires_at_utc = "";
    reservation.auto_confirm = true;
    {
        int64_t confirmed_booking_id = 0;
        expect_int(
                availability_reserve_pending(&reservation, &confirmed_booking_id),
                AVAILABILITY_RESERVATION_OK,
                "Freier Slot kann automatisch bestätigt werden");
        expect_int(query_single_int(
                "SELECT COUNT(*) FROM bookings WHERE decision_status = 'confirmed' "
                "AND hold_expires_at IS NULL AND appointment_date = '2026-08-17';",
                &value), 0, "Automatisch bestätigter Termin ist abfragbar");
        expect_int(value, 1, "Automatische Bestätigung speichert direkt confirmed");
    }
    settings.auto_confirm_bookings = false;
    expect_int(calendar_database_update_settings(&settings), 0,
               "Manuelle Terminentscheidung kann wieder aktiviert werden");

    expect_true(execute_direct_sql(
            "INSERT INTO bookings("
            "created_at, status, customer_name, contact, dog_name, dog_size, service, "
            "preferred_date, message, legacy, service_id, appointment_date, start_minute, "
            "end_minute, blocked_until_minute, decision_status, hold_expires_at, rejection_reason"
            ") SELECT "
            "'2026-08-09T08:05:00Z', 'neu', 'Doppelbuchung', 'double@example.invalid', "
            "'', 'small', code, '2026-08-10', '', 0, id, '2026-08-10', 555, 615, 630, "
            "'pending', '2026-08-09T09:05:00Z', '' "
            "FROM services WHERE code = 'wash_dry';") != 0,
            "SQLite-Trigger verhindert auch direkte überlappende Inserts");

    expect_int(query_single_int(
            "SELECT COUNT(*) FROM bookings "
            "WHERE decision_status = 'expired' AND id > 0;",
            &value), 0, "Ablaufstatus ist abfragbar");
    expect_true(value >= 1, "Abgelaufene Pending-Anfrage wird als expired markiert");

    expect_true(execute_direct_sql(
            "UPDATE bookings SET decision_status = 'unbekannt' "
            "WHERE id = 1;") != 0,
            "Datenbanktrigger verhindert unbekannte Entscheidungsstatus");

    settings.min_notice_minutes = 2000;
    expect_int(calendar_database_update_settings(&settings), 0, "Hoher Mindestvorlauf wird gespeichert");
    query = make_query(
            "wash_dry",
            "2026-08-10",
            "2026-08-09",
            600,
            "2026-08-09T08:00:00Z");
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 0, "Mindestvorlauf kann einen ganzen Tag sperren");

    settings.min_notice_minutes = 0;
    settings.booking_horizon_days = 1;
    expect_int(calendar_database_update_settings(&settings), 0, "Kurzer Buchungshorizont wird gespeichert");
    query = make_query(
            "wash_dry",
            "2026-08-10",
            "2026-08-08",
            0,
            "2026-08-08T00:00:00Z");
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 0, "Termin außerhalb des Buchungshorizonts bleibt unsichtbar");

    expect_int(calendar_database_get_service("wash_dry", &service), 0, "Leistung kann erneut geladen werden");
    service.active = false;
    expect_int(calendar_database_update_service(&service), 0, "Leistung kann deaktiviert werden");
    query = make_query(
            "wash_dry",
            "2026-08-10",
            "2026-08-09",
            0,
            "2026-08-09T00:00:00Z");
    slot_count = collect_slots(&query, slots);
    expect_int((int)slot_count, 0, "Deaktivierte Leistung erzeugt keine Termine");

    service.active = true;
    expect_int(calendar_database_update_service(&service), 0, "Leistung kann wieder aktiviert werden");

    memset(&service, 0, sizeof(service));
    snprintf(service.code, sizeof(service.code), "%s", "puppy_intro");
    snprintf(service.name, sizeof(service.name), "%s", "Welpengewöhnung");
    service.duration_minutes = 45;
    service.buffer_minutes = 15;
    service.active = true;
    expect_int(calendar_database_add_service(&service), 0,
               "Admin kann eine neue Leistung anlegen");
    expect_int(calendar_database_add_service(&service), 1,
               "Doppelte Leistungsschlüssel werden abgelehnt");
    expect_int(calendar_database_delete_service("puppy_intro"),
               CALENDAR_SERVICE_DELETE_OK,
               "Unbenutzte Leistung wird vollständig gelöscht");
    expect_int(calendar_database_get_service("puppy_intro", &service), 1,
               "Gelöschte unbenutzte Leistung existiert nicht mehr");

    expect_int(calendar_database_delete_service("wash_dry"),
               CALENDAR_SERVICE_DELETE_ARCHIVED,
               "Verwendete Leistung wird beim Löschen archiviert");
    expect_int(calendar_database_get_service("wash_dry", &service), 0,
               "Archivierte Leistung bleibt für historische Termine erhalten");
    expect_true(!service.active, "Archivierte Leistung ist nicht mehr öffentlich aktiv");

    query.date = "2026-02-31";
    expect_true(
            availability_collect(&query, slots, AVAILABILITY_MAX_SLOTS, &slot_count) != 0,
            "Ungültiges Kalenderdatum wird abgelehnt");

    calendar_database_shutdown();
    booking_database_shutdown();

    if (failures != 0) {
        fprintf(stderr, "Calendar engine tests: %d Fehler\n", failures);
        return EXIT_FAILURE;
    }

    printf("Calendar schema, settings, closures and availability tests: OK\n");
    return EXIT_SUCCESS;
}
