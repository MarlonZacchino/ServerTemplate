#include "calendar_public.h"

#include "availability.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "form_urlencoded.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CALENDAR_PUBLIC_ERROR_SIZE 512
#define CALENDAR_PUBLIC_MAX_RANGE_DAYS 42

static char calendar_public_error[CALENDAR_PUBLIC_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            calendar_public_error,
            sizeof(calendar_public_error),
            "%s",
            message == NULL ? "Unbekannter öffentlicher Kalenderfehler" : message);
}

static void set_calendar_database_error(const char *context)
{
    snprintf(
            calendar_public_error,
            sizeof(calendar_public_error),
            "%s: %s",
            context,
            calendar_database_last_error());
}

static void set_availability_error(const char *context)
{
    snprintf(
            calendar_public_error,
            sizeof(calendar_public_error),
            "%s: %s",
            context,
            availability_last_error());
}

const char *calendar_public_last_error(void)
{
    return calendar_public_error[0] == '\0'
            ? "Unbekannter öffentlicher Kalenderfehler"
            : calendar_public_error;
}

static void append_json_escaped(string *destination, const char *source)
{
    if (destination == NULL || source == NULL) {
        return;
    }

    for (size_t index = 0; source[index] != '\0'; index++) {
        unsigned char character = (unsigned char)source[index];
        char encoded[7];

        switch (character) {
            case '"':
                str_cat_cstr(destination, "\\\"");
                break;
            case '\\':
                str_cat_cstr(destination, "\\\\");
                break;
            case '\b':
                str_cat_cstr(destination, "\\b");
                break;
            case '\f':
                str_cat_cstr(destination, "\\f");
                break;
            case '\n':
                str_cat_cstr(destination, "\\n");
                break;
            case '\r':
                str_cat_cstr(destination, "\\r");
                break;
            case '\t':
                str_cat_cstr(destination, "\\t");
                break;
            default:
                if (character < 0x20) {
                    int written = snprintf(encoded, sizeof(encoded), "\\u%04x", character);
                    if (written == 6) {
                        str_cat(destination, encoded, 6);
                    }
                } else {
                    str_cat(destination, (const char *)&source[index], 1);
                }
                break;
        }
    }
}

static void append_json_string(string *destination, const char *source)
{
    str_cat_cstr(destination, "\"");
    append_json_escaped(destination, source == NULL ? "" : source);
    str_cat_cstr(destination, "\"");
}

static void append_json_int(string *destination, int value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%d", value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat(destination, buffer, (size_t)written);
    }
}

typedef struct services_json_context {
    string *json;
    bool first;
} services_json_context;

static int append_service_json(
        const calendar_service *service,
        void *context_value
)
{
    services_json_context *context = context_value;

    if (service == NULL || context == NULL || context->json == NULL) {
        return -1;
    }

    if (!context->first) {
        str_cat_cstr(context->json, ",");
    }
    context->first = false;

    str_cat_cstr(context->json, "{\"code\":");
    append_json_string(context->json, service->code);
    str_cat_cstr(context->json, ",\"name\":");
    append_json_string(context->json, service->name);
    str_cat_cstr(context->json, ",\"duration_minutes\":");
    append_json_int(context->json, service->duration_minutes);
    str_cat_cstr(context->json, ",\"buffer_minutes\":");
    append_json_int(context->json, service->buffer_minutes);
    str_cat_cstr(context->json, "}");

    return 0;
}

calendar_public_result calendar_public_build_services_json(string **out_json)
{
    services_json_context context;
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    string *json;
    int database_result;

    calendar_public_error[0] = '\0';

    if (out_json == NULL) {
        set_error("Ausgabe für Leistungen fehlt");
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    *out_json = NULL;

    if (calendar_database_get_settings(&settings) != 0) {
        set_calendar_database_error("Kalendereinstellungen konnten nicht geladen werden");
        return CALENDAR_PUBLIC_ERROR;
    }
    if (calendar_clock_now(settings.timezone, &snapshot) != 0) {
        set_error("Aktuelle Kalenderzeit konnte nicht bestimmt werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    json = _new_string();
    if (json == NULL) {
        set_error("Speicher für Leistungsantwort konnte nicht reserviert werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    str_cat_cstr(json, "{\"timezone\":");
    append_json_string(json, settings.timezone);
    str_cat_cstr(json, ",\"current_date\":");
    append_json_string(json, snapshot.local_date);
    str_cat_cstr(json, ",\"booking_horizon_days\":");
    append_json_int(json, settings.booking_horizon_days);
    str_cat_cstr(json, ",\"automatic_confirmation\":");
    str_cat_cstr(json, settings.auto_confirm_bookings ? "true" : "false");
    str_cat_cstr(json, ",\"services\":[");
    context = (services_json_context){.json = json, .first = true};

    database_result = calendar_database_for_each_active_service(
            append_service_json,
            &context);
    if (database_result != 0) {
        set_calendar_database_error("Leistungen konnten nicht gelesen werden");
        free_str(json);
        return CALENDAR_PUBLIC_ERROR;
    }

    str_cat_cstr(json, "]}");
    *out_json = json;
    return CALENDAR_PUBLIC_OK;
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
        char character = code[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return false;
        }
    }

    return true;
}

static calendar_public_result parse_availability_query(
        const char *query,
        size_t query_length,
        char service_code[CALENDAR_SERVICE_CODE_SIZE],
        char from_date[11],
        char to_date[11]
)
{
    form_value_result service_result;
    form_value_result from_result;
    form_value_result to_result;
    int range_days;

    if (query == NULL || query_length == 0) {
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    service_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "service",
            service_code,
            CALENDAR_SERVICE_CODE_SIZE);
    from_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "from",
            from_date,
            11);
    to_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "to",
            to_date,
            11);

    if (service_result != FORM_VALUE_OK ||
        from_result != FORM_VALUE_OK ||
        to_result != FORM_VALUE_OK ||
        !service_code_is_valid(service_code) ||
        !calendar_date_is_valid(from_date) ||
        !calendar_date_is_valid(to_date) ||
        calendar_date_days_between(from_date, to_date, &range_days) != 0 ||
        range_days < 0 || range_days >= CALENDAR_PUBLIC_MAX_RANGE_DAYS) {
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    return CALENDAR_PUBLIC_OK;
}

calendar_public_result calendar_public_build_availability_json(
        const char *query,
        size_t query_length,
        string **out_json
)
{
    char service_code[CALENDAR_SERVICE_CODE_SIZE] = {0};
    char from_date[11] = {0};
    char to_date[11] = {0};
    char current_date[11];
    calendar_settings settings;
    calendar_service service;
    calendar_clock_snapshot snapshot;
    string *json;
    int range_days;
    int service_result;

    calendar_public_error[0] = '\0';

    if (out_json == NULL) {
        set_error("Ausgabe für Verfügbarkeit fehlt");
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }
    *out_json = NULL;

    if (parse_availability_query(
            query,
            query_length,
            service_code,
            from_date,
            to_date) != CALENDAR_PUBLIC_OK) {
        set_error("Ungültige Verfügbarkeitsparameter");
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    if (calendar_database_get_settings(&settings) != 0) {
        set_calendar_database_error("Kalendereinstellungen konnten nicht geladen werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    service_result = calendar_database_get_service(service_code, &service);
    if (service_result < 0) {
        set_calendar_database_error("Leistung konnte nicht geladen werden");
        return CALENDAR_PUBLIC_ERROR;
    }
    if (service_result > 0 || !service.active) {
        return CALENDAR_PUBLIC_NOT_FOUND;
    }

    if (calendar_clock_now(settings.timezone, &snapshot) != 0) {
        set_error("Aktuelle Kalenderzeit konnte nicht bestimmt werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    if (calendar_date_days_between(from_date, to_date, &range_days) != 0) {
        set_error("Verfügbarkeitszeitraum konnte nicht berechnet werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    json = _new_string();
    if (json == NULL) {
        set_error("Speicher für Verfügbarkeitsantwort konnte nicht reserviert werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    str_cat_cstr(json, "{\"timezone\":");
    append_json_string(json, settings.timezone);
    str_cat_cstr(json, ",\"service\":{\"code\":");
    append_json_string(json, service.code);
    str_cat_cstr(json, ",\"name\":");
    append_json_string(json, service.name);
    str_cat_cstr(json, ",\"duration_minutes\":");
    append_json_int(json, service.duration_minutes);
    str_cat_cstr(json, ",\"buffer_minutes\":");
    append_json_int(json, service.buffer_minutes);
    str_cat_cstr(json, "},\"from\":");
    append_json_string(json, from_date);
    str_cat_cstr(json, ",\"to\":");
    append_json_string(json, to_date);
    str_cat_cstr(json, ",\"days\":[");

    memcpy(current_date, from_date, sizeof(current_date));

    for (int day_index = 0; day_index <= range_days; day_index++) {
        availability_query availability_query_value = {
                .service_code = service.code,
                .date = current_date,
                .current_date = snapshot.local_date,
                .current_minute = snapshot.local_minute,
                .now_utc = snapshot.now_utc
        };
        availability_public_slot slots[AVAILABILITY_MAX_SLOTS];
        size_t slot_count = 0;

        if (availability_collect_public(
                &availability_query_value,
                slots,
                sizeof(slots) / sizeof(slots[0]),
                &slot_count) != 0) {
            set_availability_error("Verfügbarkeit konnte nicht berechnet werden");
            free_str(json);
            return CALENDAR_PUBLIC_ERROR;
        }

        if (day_index > 0) {
            str_cat_cstr(json, ",");
        }

        str_cat_cstr(json, "{\"date\":");
        append_json_string(json, current_date);
        str_cat_cstr(json, ",\"slots\":[");

        for (size_t slot_index = 0; slot_index < slot_count; slot_index++) {
            char start_text[6];
            char end_text[6];

            if (calendar_time_format_hhmm(slots[slot_index].start_minute, start_text) != 0 ||
                calendar_time_format_hhmm(slots[slot_index].end_minute, end_text) != 0) {
                set_error("Terminzeit konnte nicht formatiert werden");
                free_str(json);
                return CALENDAR_PUBLIC_ERROR;
            }

            if (slot_index > 0) {
                str_cat_cstr(json, ",");
            }

            str_cat_cstr(json, "{\"start\":");
            append_json_string(json, start_text);
            str_cat_cstr(json, ",\"end\":");
            append_json_string(json, end_text);
            str_cat_cstr(json, ",\"available\":");
            str_cat_cstr(json, slots[slot_index].available ? "true" : "false");
            str_cat_cstr(json, "}");
        }

        str_cat_cstr(json, "]}");

        if (day_index < range_days &&
            calendar_date_add_days(current_date, 1, current_date) != 0) {
            set_error("Folgedatum konnte nicht berechnet werden");
            free_str(json);
            return CALENDAR_PUBLIC_ERROR;
        }
    }

    str_cat_cstr(json, "]}");
    *out_json = json;
    return CALENDAR_PUBLIC_OK;
}

calendar_public_result calendar_public_reserve_booking(
        const booking_request *booking,
        int64_t *out_booking_id
)
{
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    availability_reservation_request reservation;
    availability_reservation_result result;
    char hold_expires_at[21];
    int start_minute;

    calendar_public_error[0] = '\0';

    if (booking == NULL ||
        calendar_time_parse_hhmm(booking->appointment_start, &start_minute) != 0) {
        set_error("Ungültige Terminangaben");
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    if (calendar_database_get_settings(&settings) != 0) {
        set_calendar_database_error("Kalendereinstellungen konnten nicht geladen werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    if (calendar_clock_now(settings.timezone, &snapshot) != 0) {
        set_error("Reservierungszeit konnte nicht bestimmt werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    hold_expires_at[0] = '\0';
    if (!settings.auto_confirm_bookings &&
        calendar_utc_add_minutes(
                snapshot.now_utc,
                settings.pending_hold_minutes,
                hold_expires_at) != 0) {
        set_error("Reservierungszeit konnte nicht berechnet werden");
        return CALENDAR_PUBLIC_ERROR;
    }

    reservation = (availability_reservation_request){
            .query = {
                    .service_code = booking->service,
                    .date = booking->appointment_date,
                    .current_date = snapshot.local_date,
                    .current_minute = snapshot.local_minute,
                    .now_utc = snapshot.now_utc
            },
            .start_minute = start_minute,
            .created_at_utc = snapshot.now_utc,
            .hold_expires_at_utc = hold_expires_at,
            .customer_name = booking->name,
            .contact = booking->contact,
            .contact_channel = booking->contact_channel,
            .email = booking->email,
            .phone_number = booking->phone_number,
            .phone_kind = booking->phone_kind,
            .contact_preference = booking->contact_preference,
            .dog_name = booking->dog_name,
            .dog_size = booking->dog_size,
            .message = booking->message,
            .auto_confirm = settings.auto_confirm_bookings
    };

    result = availability_reserve_pending(&reservation, out_booking_id);

    if (result == AVAILABILITY_RESERVATION_OK) {
        return settings.auto_confirm_bookings
                ? CALENDAR_PUBLIC_CONFIRMED
                : CALENDAR_PUBLIC_OK;
    }
    if (result == AVAILABILITY_RESERVATION_UNAVAILABLE) {
        return CALENDAR_PUBLIC_UNAVAILABLE;
    }
    if (result == AVAILABILITY_RESERVATION_INVALID) {
        return CALENDAR_PUBLIC_BAD_REQUEST;
    }

    set_availability_error("Termin konnte nicht reserviert werden");
    return CALENDAR_PUBLIC_ERROR;
}
