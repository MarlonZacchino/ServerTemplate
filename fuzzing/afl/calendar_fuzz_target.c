#include "styles4dogs/calendar/availability.h"
#include "styles4dogs/booking/booking_database.h"
#include "styles4dogs/calendar/calendar_database.h"
#include "styles4dogs/http/form_urlencoded.h"
#include "styles4dogs/core/server_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FUZZ_INPUT_SIZE 4096

static int parse_minute(const char *value, int fallback)
{
    unsigned long result = 0;

    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    for (size_t index = 0; value[index] != '\0'; index++) {
        if (value[index] < '0' || value[index] > '9') {
            return fallback;
        }

        result = result * 10UL + (unsigned long)(value[index] - '0');
        if (result > 1439UL) {
            return fallback;
        }
    }

    return (int)result;
}

static ssize_t read_all(char *buffer, size_t capacity)
{
    size_t total = 0;

    while (total < capacity) {
        ssize_t read_bytes = read(STDIN_FILENO, buffer + total, capacity - total);

        if (read_bytes > 0) {
            total += (size_t)read_bytes;
            continue;
        }
        if (read_bytes == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }

    return (ssize_t)total;
}

static void get_value_or_default(
        const char *data,
        size_t data_length,
        const char *name,
        char *output,
        size_t output_size,
        const char *fallback
)
{
    form_value_result result = form_urlencoded_get_from_data(
            data,
            data_length,
            name,
            output,
            output_size);

    if (result != FORM_VALUE_OK) {
        snprintf(output, output_size, "%s", fallback);
    }
}

int main(void)
{
    char input[FUZZ_INPUT_SIZE];
    char service[CALENDAR_SERVICE_CODE_SIZE];
    char date[32];
    char current_date[32];
    char current_minute_text[32];
    char now_utc[64];
    availability_slot slots[AVAILABILITY_MAX_SLOTS];
    availability_query query;
    calendar_settings settings;
    size_t slot_count = 0;
    ssize_t input_length;

    if (server_config_initialize() != 0 ||
        booking_database_initialize() != 0 ||
        calendar_database_initialize() != 0) {
        return EXIT_SUCCESS;
    }

    if (calendar_database_get_settings(&settings) == 0) {
        settings.min_notice_minutes = 0;
        settings.booking_horizon_days = 730;
        (void)calendar_database_update_settings(&settings);
    }

    (void)calendar_database_clear_opening_hours();
    for (int weekday = 1; weekday <= 7; weekday++) {
        (void)calendar_database_add_opening_period(weekday, 0, 1440);
    }

    input_length = read_all(input, sizeof(input));
    if (input_length <= 0) {
        calendar_database_shutdown();
        booking_database_shutdown();
        return EXIT_SUCCESS;
    }

    get_value_or_default(
            input,
            (size_t)input_length,
            "service",
            service,
            sizeof(service),
            "wash_dry");
    get_value_or_default(
            input,
            (size_t)input_length,
            "date",
            date,
            sizeof(date),
            "2026-08-03");
    get_value_or_default(
            input,
            (size_t)input_length,
            "current_date",
            current_date,
            sizeof(current_date),
            "2026-08-01");
    get_value_or_default(
            input,
            (size_t)input_length,
            "current_minute",
            current_minute_text,
            sizeof(current_minute_text),
            "0");
    get_value_or_default(
            input,
            (size_t)input_length,
            "now_utc",
            now_utc,
            sizeof(now_utc),
            "2026-08-01T00:00:00Z");

    query = (availability_query){
            .service_code = service,
            .date = date,
            .current_date = current_date,
            .current_minute = parse_minute(current_minute_text, 0),
            .now_utc = now_utc
    };

    (void)availability_collect(
            &query,
            slots,
            sizeof(slots) / sizeof(slots[0]),
            &slot_count);

    calendar_database_shutdown();
    booking_database_shutdown();
    return EXIT_SUCCESS;
}
