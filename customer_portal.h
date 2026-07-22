#ifndef STYLING4DOGS_CUSTOMER_PORTAL_H
#define STYLING4DOGS_CUSTOMER_PORTAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CUSTOMER_PORTAL_TOKEN_HEX_SIZE 65
#define CUSTOMER_PORTAL_URL_SIZE 1024

typedef struct customer_portal_booking {
    int64_t id;
    char customer_name[256];
    char dog_name[256];
    char service_name[128];
    char appointment_date[11];
    int start_minute;
    int end_minute;
    char decision_status[32];
    char rejection_reason[512];
} customer_portal_booking;

typedef enum customer_portal_result {
    CUSTOMER_PORTAL_ERROR = -1,
    CUSTOMER_PORTAL_OK = 0,
    CUSTOMER_PORTAL_NOT_FOUND = 1,
    CUSTOMER_PORTAL_NOT_CANCELLABLE = 2
} customer_portal_result;

int customer_portal_build_url(
        int64_t booking_id,
        char *out_url,
        size_t out_url_size
);

bool customer_portal_token_is_valid(
        int64_t booking_id,
        const char *token
);

customer_portal_result customer_portal_load_booking(
        int64_t booking_id,
        const char *token,
        customer_portal_booking *out_booking
);

customer_portal_result customer_portal_cancel_booking(
        int64_t booking_id,
        const char *token,
        const char *cancelled_at_utc
);

const char *customer_portal_last_error(void);

#endif
