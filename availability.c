#include "availability.h"

#include "calendar_database.h"
#include "calendar_time.h"
#include "contact_validation.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AVAILABILITY_ERROR_SIZE 512

static char availability_error[AVAILABILITY_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            availability_error,
            sizeof(availability_error),
            "%s",
            message == NULL ? "Unbekannter Verfügbarkeitsfehler" : message);
}

static void set_database_error(const char *context)
{
    snprintf(
            availability_error,
            sizeof(availability_error),
            "%s: %s",
            context == NULL ? "Kalender-Datenbankfehler" : context,
            calendar_database_last_error());
}

const char *availability_last_error(void)
{
    return availability_error[0] == '\0'
            ? "Unbekannter Verfügbarkeitsfehler"
            : availability_error;
}

static bool ranges_overlap(
        int first_start,
        int first_end,
        int second_start,
        int second_end
)
{
    return first_start < second_end && first_end > second_start;
}

static bool overlaps_any(
        int start_minute,
        int end_minute,
        const calendar_time_range *ranges,
        size_t range_count
)
{
    for (size_t index = 0; index < range_count; index++) {
        if (ranges_overlap(
                start_minute,
                end_minute,
                ranges[index].start_minute,
                ranges[index].end_minute)) {
            return true;
        }
    }

    return false;
}

static bool query_is_valid(const availability_query *query)
{
    if (query == NULL || query->service_code == NULL ||
        query->service_code[0] == '\0' ||
        !calendar_date_is_valid(query->date) ||
        !calendar_date_is_valid(query->current_date) ||
        query->current_minute < 0 || query->current_minute > 1439 ||
        !calendar_utc_timestamp_is_valid(query->now_utc)) {
        return false;
    }

    return true;
}

int availability_collect(
        const availability_query *query,
        availability_slot *slots,
        size_t slots_capacity,
        size_t *out_count
)
{
    calendar_settings settings;
    calendar_service service;
    calendar_time_range opening_periods[CALENDAR_MAX_DAY_PERIODS];
    calendar_time_range closures[CALENDAR_MAX_DAY_CLOSURES];
    calendar_time_range bookings[CALENDAR_MAX_DAY_BOOKINGS];
    size_t opening_count = 0;
    size_t closure_count = 0;
    size_t booking_count = 0;
    size_t slot_count = 0;
    int days_until_target;
    int weekday;
    int service_result;
    int total_block_minutes;

    availability_error[0] = '\0';

    if (!query_is_valid(query) || slots == NULL ||
        slots_capacity == 0 || out_count == NULL) {
        set_error("Ungültige Verfügbarkeitsabfrage");
        return -1;
    }

    *out_count = 0;

    if (calendar_database_get_settings(&settings) != 0) {
        set_database_error("Kalendereinstellungen konnten nicht geladen werden");
        return -1;
    }

    service_result = calendar_database_get_service(query->service_code, &service);
    if (service_result < 0) {
        set_database_error("Leistung konnte nicht geladen werden");
        return -1;
    }
    if (service_result > 0 || !service.active) {
        return 0;
    }

    if (calendar_date_days_between(
            query->current_date,
            query->date,
            &days_until_target) != 0) {
        set_error("Datumsabstand konnte nicht berechnet werden");
        return -1;
    }

    if (days_until_target < 0 || days_until_target > settings.booking_horizon_days) {
        return 0;
    }

    if (calendar_date_iso_weekday(query->date, &weekday) != 0) {
        set_error("Wochentag konnte nicht berechnet werden");
        return -1;
    }

    if (calendar_database_get_opening_periods(
            weekday,
            opening_periods,
            sizeof(opening_periods) / sizeof(opening_periods[0]),
            &opening_count) != 0 ||
        calendar_database_get_closures_for_date(
            query->date,
            closures,
            sizeof(closures) / sizeof(closures[0]),
            &closure_count) != 0 ||
        calendar_database_get_blocking_bookings(
            query->date,
            query->now_utc,
            bookings,
            sizeof(bookings) / sizeof(bookings[0]),
            &booking_count) != 0) {
        set_database_error("Kalenderzeiträume konnten nicht geladen werden");
        return -1;
    }

    total_block_minutes = service.duration_minutes + service.buffer_minutes;

    for (size_t period_index = 0; period_index < opening_count; period_index++) {
        int period_start = opening_periods[period_index].start_minute;
        int period_end = opening_periods[period_index].end_minute;

        for (int start_minute = period_start;
             start_minute + total_block_minutes <= period_end;
             start_minute += settings.slot_interval_minutes) {
            int minutes_until_slot =
                    days_until_target * 1440 + start_minute - query->current_minute;
            int end_minute = start_minute + service.duration_minutes;
            int blocked_until_minute = end_minute + service.buffer_minutes;

            if (minutes_until_slot < settings.min_notice_minutes ||
                overlaps_any(start_minute, blocked_until_minute, closures, closure_count) ||
                overlaps_any(start_minute, blocked_until_minute, bookings, booking_count)) {
                continue;
            }

            if (slot_count >= slots_capacity) {
                set_error("Ausgabepuffer für freie Termine ist zu klein");
                return -1;
            }

            slots[slot_count].start_minute = start_minute;
            slots[slot_count].end_minute = end_minute;
            slots[slot_count].blocked_until_minute = blocked_until_minute;
            slot_count++;
        }
    }

    *out_count = slot_count;
    return 0;
}


int availability_collect_public(
        const availability_query *query,
        availability_public_slot *slots,
        size_t slots_capacity,
        size_t *out_count
)
{
    calendar_settings settings;
    calendar_service service;
    calendar_time_range opening_periods[CALENDAR_MAX_DAY_PERIODS];
    calendar_time_range closures[CALENDAR_MAX_DAY_CLOSURES];
    calendar_time_range bookings[CALENDAR_MAX_DAY_BOOKINGS];
    size_t opening_count = 0;
    size_t closure_count = 0;
    size_t booking_count = 0;
    size_t slot_count = 0;
    int days_until_target;
    int weekday;
    int service_result;
    int total_block_minutes;

    availability_error[0] = '\0';

    if (!query_is_valid(query) || slots == NULL ||
        slots_capacity == 0 || out_count == NULL) {
        set_error("Ungültige öffentliche Verfügbarkeitsabfrage");
        return -1;
    }

    *out_count = 0;

    if (calendar_database_get_settings(&settings) != 0) {
        set_database_error("Kalendereinstellungen konnten nicht geladen werden");
        return -1;
    }

    service_result = calendar_database_get_service(query->service_code, &service);
    if (service_result < 0) {
        set_database_error("Leistung konnte nicht geladen werden");
        return -1;
    }
    if (service_result > 0 || !service.active) {
        return 0;
    }

    if (calendar_date_days_between(
            query->current_date,
            query->date,
            &days_until_target) != 0) {
        set_error("Datumsabstand konnte nicht berechnet werden");
        return -1;
    }

    if (days_until_target < 0 || days_until_target > settings.booking_horizon_days) {
        return 0;
    }

    if (calendar_date_iso_weekday(query->date, &weekday) != 0) {
        set_error("Wochentag konnte nicht berechnet werden");
        return -1;
    }

    if (calendar_database_get_opening_periods(
            weekday,
            opening_periods,
            sizeof(opening_periods) / sizeof(opening_periods[0]),
            &opening_count) != 0 ||
        calendar_database_get_closures_for_date(
            query->date,
            closures,
            sizeof(closures) / sizeof(closures[0]),
            &closure_count) != 0 ||
        calendar_database_get_blocking_bookings(
            query->date,
            query->now_utc,
            bookings,
            sizeof(bookings) / sizeof(bookings[0]),
            &booking_count) != 0) {
        set_database_error("Kalenderzeiträume konnten nicht geladen werden");
        return -1;
    }

    total_block_minutes = service.duration_minutes + service.buffer_minutes;

    for (size_t period_index = 0; period_index < opening_count; period_index++) {
        int period_start = opening_periods[period_index].start_minute;
        int period_end = opening_periods[period_index].end_minute;

        for (int start_minute = period_start;
             start_minute + total_block_minutes <= period_end;
             start_minute += settings.slot_interval_minutes) {
            int minutes_until_slot =
                    days_until_target * 1440 + start_minute - query->current_minute;
            int end_minute = start_minute + service.duration_minutes;
            int blocked_until_minute = end_minute + service.buffer_minutes;
            bool available =
                    minutes_until_slot >= settings.min_notice_minutes &&
                    !overlaps_any(start_minute, blocked_until_minute, closures, closure_count) &&
                    !overlaps_any(start_minute, blocked_until_minute, bookings, booking_count);

            if (slot_count >= slots_capacity) {
                set_error("Ausgabepuffer für öffentliche Termine ist zu klein");
                return -1;
            }

            slots[slot_count].start_minute = start_minute;
            slots[slot_count].end_minute = end_minute;
            slots[slot_count].available = available;
            slot_count++;
        }
    }

    *out_count = slot_count;
    return 0;
}

static const availability_slot *find_requested_slot(
        const availability_slot *slots,
        size_t slot_count,
        int start_minute
)
{
    for (size_t index = 0; index < slot_count; index++) {
        if (slots[index].start_minute == start_minute) {
            return &slots[index];
        }
    }

    return NULL;
}

static bool reservation_request_is_valid(
        const availability_reservation_request *request
)
{
    if (request == NULL || !query_is_valid(&request->query) ||
        request->start_minute < 0 || request->start_minute > 1439 ||
        !calendar_utc_timestamp_is_valid(request->created_at_utc) ||
        (!request->auto_confirm &&
         (!calendar_utc_timestamp_is_valid(request->hold_expires_at_utc) ||
          strcmp(request->created_at_utc, request->hold_expires_at_utc) >= 0)) ||
        request->customer_name == NULL || request->customer_name[0] == '\0' ||
        request->contact == NULL || request->contact[0] == '\0' ||
        request->street_address == NULL || request->street_address[0] == '\0' ||
        request->postal_code == NULL || strlen(request->postal_code) != 5 ||
        request->city == NULL || request->city[0] == '\0' ||
        request->dog_breed == NULL || request->dog_breed[0] == '\0' ||
        !contact_fields_are_valid(
                request->contact_channel,
                request->email,
                request->phone_number,
                request->phone_kind,
                request->contact_preference) ||
        !contact_aggregate_matches_fields(
                request->contact,
                request->contact_channel,
                request->email,
                request->phone_number)) {
        return false;
    }

    return true;
}

static bool phone_digits_only(
        const char *phone_number,
        char *out_digits,
        size_t out_size
)
{
    size_t output = 0;

    if (phone_number == NULL || out_digits == NULL || out_size < 2) {
        return false;
    }

    for (size_t index = 0; phone_number[index] != '\0'; index++) {
        char character = phone_number[index];

        if (character >= '0' && character <= '9') {
            if (output + 1 >= out_size) {
                return false;
            }
            out_digits[output++] = character;
        }
    }

    out_digits[output] = '\0';
    return output >= 6;
}

availability_reservation_result availability_reserve_pending(
        const availability_reservation_request *request,
        int64_t *out_booking_id
)
{
    availability_slot slots[AVAILABILITY_MAX_SLOTS];
    size_t slot_count = 0;
    const availability_slot *selected_slot;
    calendar_pending_booking pending_booking;
    char since_utc[21];
    char phone_digits[64] = "";
    int recent_contact_bookings = 0;

    availability_error[0] = '\0';

    if (!reservation_request_is_valid(request)) {
        set_error("Ungültige Reservierungsanfrage");
        return AVAILABILITY_RESERVATION_INVALID;
    }

    if (calendar_database_begin_immediate() != 0) {
        set_database_error("Reservierungstransaktion konnte nicht gestartet werden");
        return AVAILABILITY_RESERVATION_ERROR;
    }

    if (calendar_database_expire_pending(request->query.now_utc) != 0) {
        set_database_error("Abgelaufene Reservierungen konnten nicht freigegeben werden");
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_ERROR;
    }

    if (calendar_utc_add_minutes(request->query.now_utc, -1440, since_utc) != 0 ||
        (strcmp(request->contact_channel, "phone") == 0 &&
         !phone_digits_only(request->phone_number, phone_digits, sizeof(phone_digits))) ||
        calendar_database_count_recent_contact_bookings(
                request->contact_channel,
                request->email,
                phone_digits,
                since_utc,
                &recent_contact_bookings) != 0) {
        set_database_error("Buchungsschutz konnte nicht geprüft werden");
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_ERROR;
    }

    if (recent_contact_bookings >= 3) {
        set_error("Für diesen Kontakt wurden innerhalb von 24 Stunden bereits drei Anfragen erstellt");
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_CONTACT_LIMIT;
    }

    if (availability_collect(
            &request->query,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            &slot_count) != 0) {
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_ERROR;
    }

    selected_slot = find_requested_slot(slots, slot_count, request->start_minute);
    if (selected_slot == NULL) {
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_UNAVAILABLE;
    }

    pending_booking = (calendar_pending_booking){
            .created_at_utc = request->created_at_utc,
            .hold_expires_at_utc = request->hold_expires_at_utc,
            .customer_name = request->customer_name,
            .contact = request->contact,
            .contact_channel = request->contact_channel,
            .email = request->email,
            .phone_number = request->phone_number,
            .phone_kind = request->phone_kind,
            .contact_preference = request->contact_preference,
            .street_address = request->street_address,
            .postal_code = request->postal_code,
            .city = request->city,
            .dog_name = request->dog_name,
            .dog_breed = request->dog_breed,
            .dog_size = request->dog_size,
            .service_code = request->query.service_code,
            .appointment_date = request->query.date,
            .start_minute = selected_slot->start_minute,
            .end_minute = selected_slot->end_minute,
            .blocked_until_minute = selected_slot->blocked_until_minute,
            .message = request->message,
            .auto_confirm = request->auto_confirm
    };

    if (calendar_database_insert_pending(&pending_booking, out_booking_id) != 0) {
        set_database_error("Vorläufige Reservierung konnte nicht gespeichert werden");
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_ERROR;
    }

    if (calendar_database_commit() != 0) {
        set_database_error("Reservierungstransaktion konnte nicht abgeschlossen werden");
        calendar_database_rollback();
        return AVAILABILITY_RESERVATION_ERROR;
    }

    return AVAILABILITY_RESERVATION_OK;
}
