#ifndef STYLES4DOGS_AVAILABILITY_H
#define STYLES4DOGS_AVAILABILITY_H

#include <stddef.h>
#include <stdint.h>

#define AVAILABILITY_MAX_SLOTS 256

typedef struct availability_query {
    const char *service_code;
    const char *date;
    const char *current_date;
    int current_minute;
    const char *now_utc;
} availability_query;

typedef struct availability_slot {
    int start_minute;
    int end_minute;
    int blocked_until_minute;
} availability_slot;

typedef struct availability_reservation_request {
    availability_query query;
    int start_minute;
    const char *created_at_utc;
    const char *hold_expires_at_utc;
    const char *customer_name;
    const char *contact;
    const char *dog_name;
    const char *dog_size;
    const char *message;
} availability_reservation_request;

typedef enum availability_reservation_result {
    AVAILABILITY_RESERVATION_ERROR = -1,
    AVAILABILITY_RESERVATION_OK = 0,
    AVAILABILITY_RESERVATION_UNAVAILABLE = 1,
    AVAILABILITY_RESERVATION_INVALID = 2
} availability_reservation_result;

/*
 * Berechnet freie Startzeiten für genau einen Tag. Die Uhrzeiten sind Minuten
 * seit Mitternacht. out_count wird auch bei einem geschlossenen Tag auf 0
 * gesetzt. Es werden nur aktive Leistungen berücksichtigt.
 */
int availability_collect(
        const availability_query *query,
        availability_slot *slots,
        size_t slots_capacity,
        size_t *out_count
);

/*
 * Reserviert einen freien Slot innerhalb einer BEGIN-IMMEDIATE-Transaktion.
 * Dadurch kann derselbe Termin nicht durch zwei gleichzeitige Requests doppelt
 * vorläufig reserviert werden.
 */
availability_reservation_result availability_reserve_pending(
        const availability_reservation_request *request,
        int64_t *out_booking_id
);

const char *availability_last_error(void);

#endif
