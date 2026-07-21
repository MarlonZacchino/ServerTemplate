#include "availability.h"
#include "booking_database.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "server_config.h"

#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    if (calendar_database_initialize() != 0) {
        fprintf(stderr, "Kalenderdatenbankfehler: %s\n", calendar_database_last_error());
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    expect_int(calendar_database_schema_version(&schema_version), 0, "Schema-Version ist lesbar");
    expect_int(schema_version, 3, "Kalenderschema verwendet Version 3");

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
    expect_int(settings.capacity, 1, "Phase 1 unterstützt genau einen parallelen Termin");

    expect_int(calendar_database_get_service("wash_dry", &service), 0, "Standardleistung wash_dry existiert");
    expect_int(service.duration_minutes, 60, "Waschen und Föhnen dauert standardmäßig 60 Minuten");
    expect_int(service.buffer_minutes, 15, "Waschen und Föhnen hat 15 Minuten Puffer");
    expect_true(service.active, "Waschen und Föhnen ist standardmäßig aktiv");

    settings.min_notice_minutes = 0;
    settings.booking_horizon_days = 120;
    settings.pending_hold_minutes = 60;
    expect_int(calendar_database_update_settings(&settings), 0, "Kalendereinstellungen sind änderbar");

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
    reservation.dog_name = "Flocke";
    reservation.dog_size = "small";
    reservation.message = "Transaktionssichere Reservierung";

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
