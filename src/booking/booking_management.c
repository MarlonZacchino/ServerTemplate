#include "styles4dogs/booking/booking_management.h"

#include "styles4dogs/calendar/calendar_database.h"
#include "styles4dogs/calendar/calendar_time.h"
#include "styles4dogs/core/server_config.h"
#include "styles4dogs/notifications/notification_queue.h"
#include "styles4dogs/services/contact_validation.h"

#include <ctype.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MANAGEMENT_ERROR_SIZE 512
#define MANAGEMENT_MAX_TEXT 2048

static char management_error[MANAGEMENT_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(management_error, sizeof(management_error), "%s",
             message == NULL ? "Unbekannter Buchungsfehler" : message);
}

static void set_sqlite_error(sqlite3 *database, const char *context)
{
    snprintf(management_error, sizeof(management_error), "%s: %s",
             context == NULL ? "SQLite-Fehler" : context,
             database == NULL ? "Datenbank nicht geöffnet" : sqlite3_errmsg(database));
}

const char *booking_management_last_error(void)
{
    return management_error[0] == '\0' ? "Unbekannter Buchungsfehler" : management_error;
}

static const char *database_path(void)
{
    return strcmp(server_config_database_file(), ":memory:") == 0
           ? "file:styles4dogs-runtime?mode=memory&cache=shared"
           : server_config_database_file();
}

static int open_database(sqlite3 **out_database)
{
    sqlite3 *database = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;

    if (out_database == NULL) return -1;
    if (strncmp(database_path(), "file:", 5) == 0) flags |= SQLITE_OPEN_URI;
    if (sqlite3_open_v2(database_path(), &database, flags, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Buchungsdatenbank konnte nicht geöffnet werden");
        sqlite3_close_v2(database);
        return -1;
    }
    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK ||
        sqlite3_exec(database, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Buchungsdatenbank konnte nicht konfiguriert werden");
        sqlite3_close_v2(database);
        return -1;
    }
    *out_database = database;
    return 0;
}

static const char *column_text(sqlite3_stmt *statement, int column)
{
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == NULL ? "" : (const char *)value;
}

static int copy_column(sqlite3_stmt *statement, int column, char *output, size_t output_size)
{
    int written = snprintf(output, output_size, "%s", column_text(statement, column));
    return written >= 0 && (size_t)written < output_size ? 0 : -1;
}

static bool single_line(const char *value, bool allow_empty, size_t maximum)
{
    size_t length;
    if (value == NULL) return false;
    length = strlen(value);
    if ((!allow_empty && length == 0) || length >= maximum) return false;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];
        if (character < 0x20 || character == 0x7f) return false;
    }
    return true;
}

static bool multiline(const char *value, size_t maximum)
{
    size_t length;
    if (value == NULL) return false;
    length = strlen(value);
    if (length >= maximum) return false;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];
        if ((character < 0x20 && character != '\n' && character != '\r' && character != '\t') ||
            character == 0x7f) return false;
    }
    return true;
}

static void normalize_email(const char *source, char *output, size_t output_size)
{
    size_t position = 0;
    if (output_size == 0) return;
    for (size_t index = 0; source != NULL && source[index] != '\0' && position + 1 < output_size; index++) {
        unsigned char character = (unsigned char)source[index];
        if (!isspace(character)) output[position++] = (char)tolower(character);
    }
    output[position] = '\0';
}

static void normalize_phone(const char *source, char *output, size_t output_size)
{
    size_t position = 0;
    if (output_size == 0) return;
    for (size_t index = 0; source != NULL && source[index] != '\0' && position + 1 < output_size; index++)
        if (source[index] >= '0' && source[index] <= '9') output[position++] = source[index];
    output[position] = '\0';
}

static bool update_is_valid(const booking_management_update *update)
{
    if (update == NULL || update->booking_id <= 0 ||
        !single_line(update->first_name, false, 128) ||
        !single_line(update->last_name, false, 128) ||
        !single_line(update->email, true, 256) ||
        !single_line(update->phone_number, true, 64) ||
        !single_line(update->phone_kind, true, 32) ||
        !single_line(update->contact_channel, false, 32) ||
        !single_line(update->contact_preference, true, 32) ||
        !single_line(update->street_address, false, 256) ||
        !single_line(update->postal_code, false, 16) || strlen(update->postal_code) != 5 ||
        !single_line(update->city, false, 128) ||
        !single_line(update->dog_name, false, 128) ||
        !single_line(update->dog_breed, true, 128) ||
        !single_line(update->dog_size, false, 32) ||
        !single_line(update->service_code, false, 64) ||
        !calendar_date_is_valid(update->appointment_date) ||
        update->start_minute < 0 || update->start_minute > 1439 ||
        !multiline(update->message, MANAGEMENT_MAX_TEXT) ||
        !multiline(update->admin_note, BOOKING_INTERNAL_NOTE_SIZE) ||
        !single_line(update->actor_identifier, true, BOOKING_ACTOR_SIZE)) return false;

    return contact_fields_are_valid(update->contact_channel, update->email,
                                    update->phone_number, update->phone_kind,
                                    update->contact_preference);
}

booking_management_result booking_management_load(
        int64_t booking_id,
        booking_management_record *out_record
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int step_result;

    management_error[0] = '\0';
    if (booking_id <= 0 || out_record == NULL || open_database(&database) != 0)
        return BOOKING_MANAGEMENT_ERROR;
    memset(out_record, 0, sizeof(*out_record));

    if (sqlite3_prepare_v2(database,
            "SELECT b.id,COALESCE(b.customer_id,0),COALESCE(b.dog_id,0),"
            "COALESCE(b.customer_first_name,''),COALESCE(b.customer_last_name,''),b.customer_name,"
            "b.email,b.phone_number,b.phone_kind,b.contact_channel,b.contact_preference,"
            "b.street_address,b.postal_code,b.city,b.dog_name,b.dog_breed,b.dog_size,b.service,"
            "CASE WHEN b.service_name_snapshot<>'' THEN b.service_name_snapshot ELSE b.service END,"
            "COALESCE(b.appointment_date,''),COALESCE(b.start_minute,-1),COALESCE(b.end_minute,-1),"
            "COALESCE(b.blocked_until_minute,-1),b.message,COALESCE(b.admin_note,''),b.status,"
            "b.decision_status,COALESCE(b.cancelled_at,''),COALESCE(b.cancellation_reason,''),"
            "COALESCE(b.cancellation_actor,''),COALESCE(b.late_cancellation,0),"
            "COALESCE(d.internal_note,'') FROM bookings b LEFT JOIN dogs d ON d.id=b.dog_id "
            "WHERE b.id=?1 AND b.legacy=0;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK) {
        set_sqlite_error(database, "Buchung konnte nicht vorbereitet werden");
        goto error;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement); sqlite3_close_v2(database);
        return BOOKING_MANAGEMENT_NOT_FOUND;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error(database, "Buchung konnte nicht gelesen werden");
        goto error;
    }

    out_record->id = sqlite3_column_int64(statement, 0);
    out_record->customer_id = sqlite3_column_int64(statement, 1);
    out_record->dog_id = sqlite3_column_int64(statement, 2);
    out_record->start_minute = sqlite3_column_int(statement, 20);
    out_record->end_minute = sqlite3_column_int(statement, 21);
    out_record->blocked_until_minute = sqlite3_column_int(statement, 22);
    out_record->late_cancellation = sqlite3_column_int(statement, 30) != 0;

#define COPY(COL, FIELD) copy_column(statement, COL, out_record->FIELD, sizeof(out_record->FIELD))
    if (COPY(3, first_name) != 0 || COPY(4, last_name) != 0 || COPY(5, customer_name) != 0 ||
        COPY(6, email) != 0 || COPY(7, phone_number) != 0 || COPY(8, phone_kind) != 0 ||
        COPY(9, contact_channel) != 0 || COPY(10, contact_preference) != 0 ||
        COPY(11, street_address) != 0 || COPY(12, postal_code) != 0 || COPY(13, city) != 0 ||
        COPY(14, dog_name) != 0 || COPY(15, dog_breed) != 0 || COPY(16, dog_size) != 0 ||
        COPY(17, service_code) != 0 || COPY(18, service_name) != 0 ||
        COPY(19, appointment_date) != 0 || COPY(23, message) != 0 || COPY(24, admin_note) != 0 ||
        COPY(25, status) != 0 || COPY(26, decision_status) != 0 || COPY(27, cancelled_at) != 0 ||
        COPY(28, cancellation_reason) != 0 || COPY(29, cancellation_actor) != 0 ||
        COPY(31, dog_internal_note) != 0) {
        set_error("Buchungsdaten sind zu lang");
        goto error;
    }
#undef COPY

    sqlite3_finalize(statement); sqlite3_close_v2(database);
    return BOOKING_MANAGEMENT_OK;

error:
    sqlite3_finalize(statement); sqlite3_close_v2(database);
    return BOOKING_MANAGEMENT_ERROR;
}

static int load_service(sqlite3 *database, const char *code, calendar_service *service)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;
    memset(service, 0, sizeof(*service));
    if (sqlite3_prepare_v2(database,
            "SELECT id,code,name,duration_minutes,buffer_minutes,active,sort_order "
            "FROM services WHERE code=?1;", -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, code, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) goto cleanup;
    service->id = sqlite3_column_int64(statement, 0);
    service->duration_minutes = sqlite3_column_int(statement, 3);
    service->buffer_minutes = sqlite3_column_int(statement, 4);
    service->active = sqlite3_column_int(statement, 5) != 0;
    service->sort_order = sqlite3_column_int(statement, 6);
    if (copy_column(statement, 1, service->code, sizeof(service->code)) != 0 ||
        copy_column(statement, 2, service->name, sizeof(service->name)) != 0) goto cleanup;
    result = 0;
cleanup:
    sqlite3_finalize(statement);
    return result;
}

static int load_settings(sqlite3 *database, calendar_settings *settings)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;
    memset(settings, 0, sizeof(*settings));
    if (sqlite3_prepare_v2(database,
            "SELECT timezone,min_notice_minutes,booking_horizon_days,slot_interval_minutes,"
            "pending_hold_minutes,capacity,auto_confirm_bookings,email_notifications_enabled,"
            "reminder_enabled,reminder_lead_minutes,cancellation_notice_minutes "
            "FROM calendar_settings WHERE id=1;", -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) goto cleanup;
    snprintf(settings->timezone, sizeof(settings->timezone), "%s", column_text(statement, 0));
    settings->min_notice_minutes = sqlite3_column_int(statement, 1);
    settings->booking_horizon_days = sqlite3_column_int(statement, 2);
    settings->slot_interval_minutes = sqlite3_column_int(statement, 3);
    settings->pending_hold_minutes = sqlite3_column_int(statement, 4);
    settings->capacity = sqlite3_column_int(statement, 5);
    settings->auto_confirm_bookings = sqlite3_column_int(statement, 6) != 0;
    settings->email_notifications_enabled = sqlite3_column_int(statement, 7) != 0;
    settings->reminder_enabled = sqlite3_column_int(statement, 8) != 0;
    settings->reminder_lead_minutes = sqlite3_column_int(statement, 9);
    settings->cancellation_notice_minutes = sqlite3_column_int(statement, 10);
    result = 0;
cleanup:
    sqlite3_finalize(statement);
    return result;
}

static bool interval_is_available(
        sqlite3 *database,
        int64_t booking_id,
        const char *date,
        int start_minute,
        int blocked_until_minute,
        const calendar_settings *settings,
        const calendar_clock_snapshot *clock
)
{
    sqlite3_stmt *statement = NULL;
    char horizon[11];
    time_t appointment;
    time_t now = time(NULL);
    int weekday;
    bool open = false;
    bool available = false;

    if (calendar_date_add_days(clock->local_date, settings->booking_horizon_days, horizon) != 0 ||
        strcmp(date, clock->local_date) < 0 || strcmp(date, horizon) > 0 ||
        calendar_local_datetime_to_epoch(settings->timezone, date, start_minute, &appointment) != 0 ||
        now == (time_t)-1 || difftime(appointment, now) < settings->min_notice_minutes * 60.0 ||
        calendar_date_iso_weekday(date, &weekday) != 0 ||
        start_minute % settings->slot_interval_minutes != 0 || blocked_until_minute > 1440)
        return false;

    if (sqlite3_prepare_v2(database,
            "SELECT 1 FROM weekly_opening_hours WHERE weekday=?1 "
            "AND start_minute<=?2 AND end_minute>=?3 LIMIT 1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int(statement, 1, weekday) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, blocked_until_minute) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) goto cleanup;
    open = true;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "SELECT 1 FROM calendar_closures WHERE start_date<=?1 AND end_date>=?1 "
            "AND start_minute<?3 AND end_minute>?2 LIMIT 1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, blocked_until_minute) != SQLITE_OK) goto cleanup;
    if (sqlite3_step(statement) == SQLITE_ROW) goto cleanup;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "SELECT 1 FROM bookings WHERE id<>?1 AND legacy=0 AND appointment_date=?2 "
            "AND decision_status IN ('confirmed','pending') "
            "AND (decision_status='confirmed' OR hold_expires_at>?5) "
            "AND start_minute<?4 AND blocked_until_minute>?3 LIMIT 1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 3, start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, blocked_until_minute) != SQLITE_OK ||
        sqlite3_bind_text(statement, 5, clock->now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto cleanup;
    available = sqlite3_step(statement) == SQLITE_DONE;

cleanup:
    sqlite3_finalize(statement);
    return open && available;
}

static int upsert_customer_and_dog(
        sqlite3 *database,
        const booking_management_update *update,
        const char *now_utc,
        int64_t *out_customer_id,
        int64_t *out_dog_id
)
{
    sqlite3_stmt *statement = NULL;
    char normalized_email[256];
    char normalized_phone[64];
    int64_t customer_id = 0;
    int64_t dog_id = 0;

    normalize_email(update->email, normalized_email, sizeof(normalized_email));
    normalize_phone(update->phone_number, normalized_phone, sizeof(normalized_phone));

    if (normalized_email[0] != '\0') {
        if (sqlite3_prepare_v2(database,
                "SELECT id FROM customers WHERE normalized_email=?1 LIMIT 1;",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_text(statement, 1, normalized_email, -1, SQLITE_TRANSIENT) != SQLITE_OK)
            goto error;
    } else if (normalized_phone[0] != '\0') {
        if (sqlite3_prepare_v2(database,
                "SELECT id FROM customers WHERE normalized_email='' AND normalized_phone=?1 LIMIT 1;",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_text(statement, 1, normalized_phone, -1, SQLITE_TRANSIENT) != SQLITE_OK)
            goto error;
    }
    if (statement != NULL && sqlite3_step(statement) == SQLITE_ROW)
        customer_id = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement); statement = NULL;

    if (customer_id == 0) {
        if (sqlite3_prepare_v2(database,
                "INSERT INTO customers(first_name,last_name,email,normalized_email,phone_number,normalized_phone,"
                "contact_preference,street_address,postal_code,city,created_at,updated_at) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?11);",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_text(statement, 1, update->first_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 2, update->last_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 3, update->email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 4, normalized_email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 5, update->phone_number, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 6, normalized_phone, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 7, update->contact_preference, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 8, update->street_address, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 9, update->postal_code, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 10, update->city, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 11, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_step(statement) != SQLITE_DONE) goto error;
        customer_id = sqlite3_last_insert_rowid(database);
    } else {
        sqlite3_finalize(statement); statement = NULL;
        if (sqlite3_prepare_v2(database,
                "UPDATE customers SET first_name=?1,last_name=?2,email=?3,normalized_email=?4,"
                "phone_number=?5,normalized_phone=?6,contact_preference=?7,street_address=?8,"
                "postal_code=?9,city=?10,updated_at=?11 WHERE id=?12;",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_text(statement, 1, update->first_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 2, update->last_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 3, update->email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 4, normalized_email, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 5, update->phone_number, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 6, normalized_phone, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 7, update->contact_preference, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 8, update->street_address, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 9, update->postal_code, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 10, update->city, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 11, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int64(statement, 12, customer_id) != SQLITE_OK ||
            sqlite3_step(statement) != SQLITE_DONE) goto error;
    }
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "SELECT id FROM dogs WHERE customer_id=?1 AND lower(trim(name))=lower(trim(?2)) LIMIT 1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, customer_id) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, update->dog_name, -1, SQLITE_TRANSIENT) != SQLITE_OK)
        goto error;
    if (sqlite3_step(statement) == SQLITE_ROW) dog_id = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement); statement = NULL;

    if (dog_id == 0) {
        if (sqlite3_prepare_v2(database,
                "INSERT INTO dogs(customer_id,name,breed,size,created_at,updated_at) VALUES(?1,?2,?3,?4,?5,?5);",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_int64(statement, 1, customer_id) != SQLITE_OK ||
            sqlite3_bind_text(statement, 2, update->dog_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 3, update->dog_breed, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 4, update->dog_size, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 5, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_step(statement) != SQLITE_DONE) goto error;
        dog_id = sqlite3_last_insert_rowid(database);
    } else {
        if (sqlite3_prepare_v2(database,
                "UPDATE dogs SET name=?1,breed=?2,size=?3,updated_at=?4 WHERE id=?5;",
                -1, &statement, NULL) != SQLITE_OK ||
            sqlite3_bind_text(statement, 1, update->dog_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 2, update->dog_breed, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 3, update->dog_size, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_text(statement, 4, now_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
            sqlite3_bind_int64(statement, 5, dog_id) != SQLITE_OK ||
            sqlite3_step(statement) != SQLITE_DONE) goto error;
    }
    sqlite3_finalize(statement);
    *out_customer_id = customer_id;
    *out_dog_id = dog_id;
    return 0;

error:
    sqlite3_finalize(statement);
    set_sqlite_error(database, "Kunden- und Hundedaten konnten nicht gespeichert werden");
    return -1;
}

booking_management_result booking_management_update_booking(
        const booking_management_update *update,
        bool *out_rescheduled
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    booking_management_record old;
    calendar_service service;
    calendar_settings settings;
    calendar_clock_snapshot clock;
    char customer_name[256];
    char contact[320];
    char reschedule_nonce[160];
    int end_minute;
    int blocked_until;
    int64_t customer_id;
    int64_t dog_id;
    bool rescheduled;
    booking_management_result result = BOOKING_MANAGEMENT_ERROR;

    management_error[0] = '\0';
    if (out_rescheduled != NULL) *out_rescheduled = false;
    if (!update_is_valid(update)) {
        set_error("Die Buchungsangaben sind ungültig");
        return BOOKING_MANAGEMENT_INVALID;
    }
    result = booking_management_load(update->booking_id, &old);
    if (result != BOOKING_MANAGEMENT_OK) return result;
    if (open_database(&database) != 0) return BOOKING_MANAGEMENT_ERROR;

    if (load_service(database, update->service_code, &service) != 0 || !service.active ||
        load_settings(database, &settings) != 0 ||
        calendar_clock_now(settings.timezone, &clock) != 0) {
        set_error("Leistung oder Kalendereinstellungen konnten nicht geladen werden");
        goto cleanup;
    }

    end_minute = update->start_minute + service.duration_minutes;
    blocked_until = end_minute + service.buffer_minutes;
    if (end_minute > 1440 || blocked_until > 1440) {
        set_error("Der Termin endet außerhalb des Kalendertages");
        result = BOOKING_MANAGEMENT_INVALID;
        goto cleanup;
    }
    rescheduled = strcmp(old.appointment_date, update->appointment_date) != 0 ||
                  old.start_minute != update->start_minute ||
                  strcmp(old.service_code, update->service_code) != 0;
    if (rescheduled && strcmp(old.decision_status, "pending") != 0 &&
        strcmp(old.decision_status, "confirmed") != 0) {
        set_error("Dieser Termin kann in seinem aktuellen Status nicht verschoben werden");
        result = BOOKING_MANAGEMENT_NOT_ALLOWED;
        goto cleanup;
    }

    if (sqlite3_exec(database, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Änderungstransaktion konnte nicht gestartet werden");
        goto cleanup;
    }
    if (rescheduled && !interval_is_available(database, update->booking_id,
            update->appointment_date, update->start_minute, blocked_until,
            &settings, &clock)) {
        set_error("Der gewünschte Termin ist nicht verfügbar");
        result = BOOKING_MANAGEMENT_CONFLICT;
        goto rollback;
    }

    if (snprintf(customer_name, sizeof(customer_name), "%s %s", update->first_name, update->last_name) < 0 ||
        snprintf(contact, sizeof(contact), "%s",
                 strcmp(update->contact_channel, "email") == 0 ? update->email : update->phone_number) < 0 ||
        upsert_customer_and_dog(database, update, clock.now_utc, &customer_id, &dog_id) != 0)
        goto rollback;

    if (sqlite3_prepare_v2(database,
            "UPDATE bookings SET customer_first_name=?1,customer_last_name=?2,customer_name=?3,"
            "email=?4,phone_number=?5,phone_kind=?6,contact_channel=?7,contact_preference=?8,contact=?9,"
            "street_address=?10,postal_code=?11,city=?12,dog_name=?13,dog_breed=?14,dog_size=?15,"
            "service_id=?16,service=?17,service_name_snapshot=?18,service_duration_minutes_snapshot=?19,"
            "service_buffer_minutes_snapshot=?20,appointment_date=?21,start_minute=?22,end_minute=?23,"
            "blocked_until_minute=?24,message=?25,admin_note=?26,customer_id=?27,dog_id=?28,"
            "last_actor_type='admin',last_actor_identifier=?29 WHERE id=?30 AND legacy=0;",
            -1, &statement, NULL) != SQLITE_OK) goto rollback;

#define BTEXT(I,V) sqlite3_bind_text(statement,I,V,-1,SQLITE_TRANSIENT)
    if (BTEXT(1, update->first_name) != SQLITE_OK || BTEXT(2, update->last_name) != SQLITE_OK ||
        BTEXT(3, customer_name) != SQLITE_OK || BTEXT(4, update->email) != SQLITE_OK ||
        BTEXT(5, update->phone_number) != SQLITE_OK || BTEXT(6, update->phone_kind) != SQLITE_OK ||
        BTEXT(7, update->contact_channel) != SQLITE_OK || BTEXT(8, update->contact_preference) != SQLITE_OK ||
        BTEXT(9, contact) != SQLITE_OK || BTEXT(10, update->street_address) != SQLITE_OK ||
        BTEXT(11, update->postal_code) != SQLITE_OK || BTEXT(12, update->city) != SQLITE_OK ||
        BTEXT(13, update->dog_name) != SQLITE_OK || BTEXT(14, update->dog_breed) != SQLITE_OK ||
        BTEXT(15, update->dog_size) != SQLITE_OK || sqlite3_bind_int64(statement, 16, service.id) != SQLITE_OK ||
        BTEXT(17, service.code) != SQLITE_OK || BTEXT(18, service.name) != SQLITE_OK ||
        sqlite3_bind_int(statement, 19, service.duration_minutes) != SQLITE_OK ||
        sqlite3_bind_int(statement, 20, service.buffer_minutes) != SQLITE_OK ||
        BTEXT(21, update->appointment_date) != SQLITE_OK || sqlite3_bind_int(statement, 22, update->start_minute) != SQLITE_OK ||
        sqlite3_bind_int(statement, 23, end_minute) != SQLITE_OK || sqlite3_bind_int(statement, 24, blocked_until) != SQLITE_OK ||
        BTEXT(25, update->message) != SQLITE_OK || BTEXT(26, update->admin_note) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 27, customer_id) != SQLITE_OK || sqlite3_bind_int64(statement, 28, dog_id) != SQLITE_OK ||
        BTEXT(29, update->actor_identifier) != SQLITE_OK || sqlite3_bind_int64(statement, 30, update->booking_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE || sqlite3_changes(database) != 1) {
        set_sqlite_error(database, "Buchungsänderung konnte nicht gespeichert werden");
        goto rollback;
    }
#undef BTEXT
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_exec(database, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Buchungsänderung konnte nicht abgeschlossen werden");
        goto cleanup;
    }

    if (rescheduled) {
        snprintf(reschedule_nonce, sizeof(reschedule_nonce), "%s:%d:%s:%d",
                 old.appointment_date, old.start_minute,
                 update->appointment_date, update->start_minute);
        if (notification_queue_enqueue_rescheduled(update->booking_id, old.appointment_date,
                old.start_minute, old.end_minute, reschedule_nonce) != 0) {
            set_error("Termin wurde gespeichert, die Änderungsmail konnte aber nicht eingereiht werden");
            result = BOOKING_MANAGEMENT_ERROR;
            goto cleanup;
        }
    }
    if (out_rescheduled != NULL) *out_rescheduled = rescheduled;
    result = BOOKING_MANAGEMENT_OK;
    goto cleanup;

rollback:
    sqlite3_finalize(statement); statement = NULL;
    sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return result;
}

booking_management_result booking_management_mark_no_show(
        int64_t booking_id,
        const char *note,
        const char *actor_identifier
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int changed;

    management_error[0] = '\0';
    if (booking_id <= 0 || !multiline(note == NULL ? "" : note, BOOKING_INTERNAL_NOTE_SIZE) ||
        !single_line(actor_identifier == NULL ? "" : actor_identifier, true, BOOKING_ACTOR_SIZE))
        return BOOKING_MANAGEMENT_INVALID;
    if (open_database(&database) != 0) return BOOKING_MANAGEMENT_ERROR;
    if (sqlite3_prepare_v2(database,
            "UPDATE bookings SET decision_status='no_show',status='nicht_erschienen',"
            "admin_note=CASE WHEN ?1='' THEN admin_note ELSE ?1 END,last_actor_type='admin',"
            "last_actor_identifier=?2,decision_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
            "WHERE id=?3 AND legacy=0 AND decision_status='confirmed' AND status='bestätigt';",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, note == NULL ? "" : note, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, actor_identifier == NULL ? "" : actor_identifier, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, booking_id) != SQLITE_OK || sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Nicht-erschienen-Status konnte nicht gespeichert werden");
        sqlite3_finalize(statement); sqlite3_close_v2(database);
        return BOOKING_MANAGEMENT_ERROR;
    }
    changed = sqlite3_changes(database);
    sqlite3_finalize(statement); sqlite3_close_v2(database);
    if (changed == 0) return BOOKING_MANAGEMENT_NOT_ALLOWED;
    return BOOKING_MANAGEMENT_OK;
}

booking_management_result booking_management_update_dog_note(
        int64_t dog_id,
        const char *note,
        const char *actor_identifier
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char previous_note[BOOKING_INTERNAL_NOTE_SIZE] = "";
    int changed;
    if (dog_id <= 0 || !multiline(note == NULL ? "" : note, BOOKING_INTERNAL_NOTE_SIZE) ||
        !single_line(actor_identifier == NULL ? "" : actor_identifier, true, BOOKING_ACTOR_SIZE))
        return BOOKING_MANAGEMENT_INVALID;
    if (open_database(&database) != 0) return BOOKING_MANAGEMENT_ERROR;
    if (sqlite3_exec(database, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Hundenotiz-Transaktion konnte nicht gestartet werden");
        sqlite3_close_v2(database);
        return BOOKING_MANAGEMENT_ERROR;
    }
    if (sqlite3_prepare_v2(database,
            "SELECT internal_note FROM dogs WHERE id=?1;", -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, dog_id) != SQLITE_OK) goto error;
    if (sqlite3_step(statement) != SQLITE_ROW ||
        copy_column(statement, 0, previous_note, sizeof(previous_note)) != 0) {
        sqlite3_finalize(statement); statement = NULL;
        sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_close_v2(database);
        return BOOKING_MANAGEMENT_NOT_FOUND;
    }
    sqlite3_finalize(statement); statement = NULL;
    if (sqlite3_prepare_v2(database,
            "UPDATE dogs SET internal_note=?1,updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?2;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, note == NULL ? "" : note, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, dog_id) != SQLITE_OK || sqlite3_step(statement) != SQLITE_DONE) {
        goto error;
    }
    changed = sqlite3_changes(database);
    sqlite3_finalize(statement); statement = NULL;
    if (changed != 1) goto error;
    if (sqlite3_prepare_v2(database,
            "INSERT INTO booking_events(booking_id,event_type,actor_type,actor_identifier,old_value,new_value,created_at) "
            "SELECT id,'dog_note_updated','admin',?1,?2,?3,strftime('%Y-%m-%dT%H:%M:%SZ','now') "
            "FROM bookings WHERE dog_id=?4 AND legacy=0;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, actor_identifier == NULL ? "" : actor_identifier,
                          -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, previous_note[0] == '\0' ? "leer" : "vorhanden",
                          -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(statement, 3, note == NULL || note[0] == '\0' ? "leer" : "vorhanden",
                          -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 4, dog_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto error;
    sqlite3_finalize(statement); statement = NULL;
    if (sqlite3_exec(database, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) goto error;
    sqlite3_close_v2(database);
    return BOOKING_MANAGEMENT_OK;

error:
    set_sqlite_error(database, "Hundenotiz konnte nicht gespeichert werden");
    sqlite3_finalize(statement);
    sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close_v2(database);
    return BOOKING_MANAGEMENT_ERROR;
}

int booking_management_for_each_event(
        int64_t booking_id,
        booking_event_callback callback,
        void *context
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int step_result;
    if (booking_id <= 0 || callback == NULL || open_database(&database) != 0) return -1;
    if (sqlite3_prepare_v2(database,
            "SELECT id,booking_id,event_type,actor_type,actor_identifier,old_value,new_value,reason,created_at "
            "FROM booking_events WHERE booking_id=?1 ORDER BY created_at,id;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK) goto error;
    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        booking_event_record event = {0};
        event.id = sqlite3_column_int64(statement, 0);
        event.booking_id = sqlite3_column_int64(statement, 1);
#define ECOPY(C,F) copy_column(statement,C,event.F,sizeof(event.F))
        if (ECOPY(2,event_type)!=0 || ECOPY(3,actor_type)!=0 || ECOPY(4,actor_identifier)!=0 ||
            ECOPY(5,old_value)!=0 || ECOPY(6,new_value)!=0 || ECOPY(7,reason)!=0 || ECOPY(8,created_at)!=0 ||
            callback(&event, context) != 0) goto error;
#undef ECOPY
    }
    if (step_result != SQLITE_DONE) goto error;
    sqlite3_finalize(statement); sqlite3_close_v2(database); return 0;
error:
    set_sqlite_error(database, "Buchungsverlauf konnte nicht gelesen werden");
    sqlite3_finalize(statement); sqlite3_close_v2(database); return -1;
}

static void append_html(string *output, const char *text)
{
    for (size_t index = 0; text != NULL && text[index] != '\0'; index++) {
        switch (text[index]) {
            case '&': str_cat_cstr(output, "&amp;"); break;
            case '<': str_cat_cstr(output, "&lt;"); break;
            case '>': str_cat_cstr(output, "&gt;"); break;
            case '"': str_cat_cstr(output, "&quot;"); break;
            case '\'': str_cat_cstr(output, "&#39;"); break;
            default: str_cat(output, text + index, 1); break;
        }
    }
}

static const char *event_label(const char *type)
{
    if (strcmp(type,"booking_created")==0) return "Buchung wurde angefragt";
    if (strcmp(type,"booking_confirmed")==0) return "Termin wurde bestätigt";
    if (strcmp(type,"booking_rejected")==0) return "Anfrage wurde abgelehnt";
    if (strcmp(type,"customer_cancelled")==0) return "Kunde hat den Termin abgesagt";
    if (strcmp(type,"admin_cancelled")==0) return "Admin hat den Termin abgesagt";
    if (strcmp(type,"booking_rescheduled")==0) return "Termin wurde verschoben";
    if (strcmp(type,"booking_updated")==0) return "Buchungsdaten wurden geändert";
    if (strcmp(type,"reminder_queued")==0) return "Terminerinnerung wurde eingereiht";
    if (strcmp(type,"booking_completed")==0) return "Termin wurde automatisch erledigt";
    if (strcmp(type,"booking_no_show")==0) return "Termin wurde als nicht erschienen markiert";
    if (strcmp(type,"dog_note_updated")==0) return "Interne Hundenotiz wurde geändert";
    return "Buchung wurde aktualisiert";
}

static const char *actor_label(const char *type)
{
    if (strcmp(type, "customer") == 0) return "Kunde";
    if (strcmp(type, "admin") == 0) return "Admin";
    if (strcmp(type, "system") == 0) return "System";
    return "Unbekannt";
}

static const char *status_label(const char *status)
{
    if (strcmp(status, "neu") == 0) return "Neu";
    if (strcmp(status, "bestätigt") == 0) return "Bestätigt";
    if (strcmp(status, "abgelehnt") == 0) return "Abgelehnt";
    if (strcmp(status, "abgesagt") == 0) return "Abgesagt";
    if (strcmp(status, "erledigt") == 0) return "Erledigt";
    if (strcmp(status, "nicht_erschienen") == 0) return "Nicht erschienen";
    return status;
}

static void format_event_timestamp(const char *source, char output[32])
{
    int year, month, day, hour, minute;
    if (source != NULL &&
        sscanf(source, "%4d-%2d-%2dT%2d:%2d", &year, &month, &day, &hour, &minute) == 5) {
        snprintf(output, 32, "%02d.%02d.%04d %02d:%02d UTC", day, month, year, hour, minute);
    } else {
        snprintf(output, 32, "%s", source == NULL ? "" : source);
    }
}

static bool format_schedule_value(const char *source, char output[160])
{
    char date[11] = "";
    char date_display[64] = "";
    char start[6] = "";
    char end[6] = "";
    char service[64] = "";
    int start_minute;
    int end_minute;

    if (source == NULL ||
        sscanf(source, "%10[^|]|%d|%d|%63[^\n]", date, &start_minute, &end_minute, service) != 4 ||
        calendar_date_format_de(date, false, date_display, sizeof(date_display)) != 0 ||
        calendar_time_format_hhmm(start_minute, start) != 0 ||
        calendar_time_format_hhmm(end_minute, end) != 0) return false;
    snprintf(output, 160, "%s, %s–%s Uhr", date_display, start, end);
    return true;
}

typedef struct history_html_context { string *page; } history_html_context;
static int append_event_html(const booking_event_record *event, void *opaque)
{
    history_html_context *context = opaque;
    char timestamp[32];
    char old_schedule[160];
    char new_schedule[160];
    format_event_timestamp(event->created_at, timestamp);
    str_cat_cstr(context->page, "<li><time>"); append_html(context->page, timestamp);
    str_cat_cstr(context->page, "</time><strong>"); append_html(context->page, event_label(event->event_type));
    str_cat_cstr(context->page, "</strong><span> durch "); append_html(context->page, actor_label(event->actor_type));
    if (event->actor_identifier[0] != '\0') { str_cat_cstr(context->page, " ("); append_html(context->page, event->actor_identifier); str_cat_cstr(context->page, ")"); }
    if (strcmp(event->event_type, "booking_rescheduled") == 0 &&
        format_schedule_value(event->old_value, old_schedule) &&
        format_schedule_value(event->new_value, new_schedule)) {
        str_cat_cstr(context->page, " – von "); append_html(context->page, old_schedule);
        str_cat_cstr(context->page, " auf "); append_html(context->page, new_schedule);
    } else if (event->old_value[0] != '\0' || event->new_value[0] != '\0') {
        str_cat_cstr(context->page, " – "); append_html(context->page, status_label(event->old_value));
        if (event->new_value[0] != '\0') { str_cat_cstr(context->page, " → "); append_html(context->page, status_label(event->new_value)); }
    }
    if (event->reason[0] != '\0') { str_cat_cstr(context->page, " – "); append_html(context->page, event->reason); }
    str_cat_cstr(context->page, "</span></li>");
    return 0;
}

string *booking_management_build_history_html(int64_t booking_id)
{
    booking_management_record record;
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    string *page;
    history_html_context context;
    int step_result;

    if (booking_management_load(booking_id, &record) != BOOKING_MANAGEMENT_OK) return NULL;
    page = _new_string();
    if (page == NULL) return NULL;
    str_cat_cstr(page, "<section class=\"booking-history\"><h3>Verlauf</h3><ol class=\"booking-timeline\">");
    context.page = page;
    if (booking_management_for_each_event(booking_id, append_event_html, &context) != 0) {
        free_str(page); return NULL;
    }
    str_cat_cstr(page, "</ol></section><section class=\"customer-history\"><h3>Kunden- und Hundehistorie</h3>");
    if (open_database(&database) != 0) { free_str(page); return NULL; }
    if (sqlite3_prepare_v2(database,
            "SELECT b.id,b.appointment_date,b.status,CASE WHEN b.service_name_snapshot<>'' THEN b.service_name_snapshot ELSE b.service END,"
            "b.late_cancellation FROM bookings b WHERE b.legacy=0 AND "
            "((?1>0 AND b.customer_id=?1) OR (?2>0 AND b.dog_id=?2)) ORDER BY b.appointment_date DESC,b.id DESC LIMIT 30;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, record.customer_id) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, record.dog_id) != SQLITE_OK) {
        sqlite3_finalize(statement); sqlite3_close_v2(database); free_str(page); return NULL;
    }
    str_cat_cstr(page, "<ul class=\"booking-history-list\">");
    while ((step_result=sqlite3_step(statement))==SQLITE_ROW) {
        char id[32]; snprintf(id,sizeof(id),"%lld",(long long)sqlite3_column_int64(statement,0));
        str_cat_cstr(page,"<li><a href=\"/admin/bookings/edit?id="); append_html(page,id); str_cat_cstr(page,"\">#"); append_html(page,id);
        str_cat_cstr(page,"</a> – "); append_html(page,column_text(statement,1)); str_cat_cstr(page," – ");
        append_html(page,column_text(statement,3)); str_cat_cstr(page," – "); append_html(page,column_text(statement,2));
        if (sqlite3_column_int(statement,4)!=0) str_cat_cstr(page," <strong>Kurzfristig abgesagt</strong>");
        str_cat_cstr(page,"</li>");
    }
    str_cat_cstr(page,"</ul></section>");
    sqlite3_finalize(statement); sqlite3_close_v2(database);
    return page;
}
