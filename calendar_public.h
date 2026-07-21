#ifndef STYLES4DOGS_CALENDAR_PUBLIC_H
#define STYLES4DOGS_CALENDAR_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

#include "booking.h"
#include "http_lib.h"

typedef enum calendar_public_result {
    CALENDAR_PUBLIC_ERROR = -1,
    CALENDAR_PUBLIC_OK = 0,
    CALENDAR_PUBLIC_BAD_REQUEST = 1,
    CALENDAR_PUBLIC_NOT_FOUND = 2,
    CALENDAR_PUBLIC_UNAVAILABLE = 3,
    CALENDAR_PUBLIC_CONFIRMED = 4,
    CALENDAR_PUBLIC_CONTACT_LIMIT = 5
} calendar_public_result;

calendar_public_result calendar_public_build_services_json(string **out_json);

calendar_public_result calendar_public_build_availability_json(
        const char *query,
        size_t query_length,
        string **out_json
);

calendar_public_result calendar_public_reserve_booking(
        const booking_request *booking,
        int64_t *out_booking_id
);

const char *calendar_public_last_error(void);

#endif
