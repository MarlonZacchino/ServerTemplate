#include "calendar_time.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool is_digit(char character)
{
    return character >= '0' && character <= '9';
}

static bool is_leap_year(int year)
{
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

static int days_in_month(int year, int month)
{
    static const int month_lengths[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }

    return month_lengths[month - 1];
}

static bool parse_date(
        const char *date,
        int *out_year,
        int *out_month,
        int *out_day
)
{
    int year;
    int month;
    int day;

    if (date == NULL || strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (size_t index = 0; index < 10; index++) {
        if (index == 4 || index == 7) {
            continue;
        }

        if (!is_digit(date[index])) {
            return false;
        }
    }

    year = (date[0] - '0') * 1000 +
           (date[1] - '0') * 100 +
           (date[2] - '0') * 10 +
           (date[3] - '0');
    month = (date[5] - '0') * 10 + (date[6] - '0');
    day = (date[8] - '0') * 10 + (date[9] - '0');

    if (year < 1970 || year > 9999 ||
        month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month)) {
        return false;
    }

    if (out_year != NULL) {
        *out_year = year;
    }
    if (out_month != NULL) {
        *out_month = month;
    }
    if (out_day != NULL) {
        *out_day = day;
    }

    return true;
}

/* Howard Hinnants days-from-civil algorithm, epoch 1970-01-01. */
static int64_t days_from_civil(int year, unsigned int month, unsigned int day)
{
    int adjusted_year = year - (month <= 2 ? 1 : 0);
    int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    unsigned int year_of_era = (unsigned int)(adjusted_year - era * 400);
    int adjusted_month = (int)month + (month > 2 ? -3 : 9);
    unsigned int day_of_year = (153U * (unsigned int)adjusted_month + 2U) / 5U + day - 1U;
    unsigned int day_of_era = year_of_era * 365U + year_of_era / 4U -
                              year_of_era / 100U + day_of_year;

    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

bool calendar_date_is_valid(const char *date)
{
    return parse_date(date, NULL, NULL, NULL);
}

int calendar_date_days_between(
        const char *from_date,
        const char *to_date,
        int *out_days
)
{
    int from_year;
    int from_month;
    int from_day;
    int to_year;
    int to_month;
    int to_day;
    int64_t difference;

    if (out_days == NULL ||
        !parse_date(from_date, &from_year, &from_month, &from_day) ||
        !parse_date(to_date, &to_year, &to_month, &to_day)) {
        return -1;
    }

    difference = days_from_civil(
            to_year,
            (unsigned int)to_month,
            (unsigned int)to_day) -
                 days_from_civil(
                         from_year,
                         (unsigned int)from_month,
                         (unsigned int)from_day);

    if (difference < INT_MIN || difference > INT_MAX) {
        return -1;
    }

    *out_days = (int)difference;
    return 0;
}

int calendar_date_iso_weekday(const char *date, int *out_weekday)
{
    int year;
    int month;
    int day;
    int64_t epoch_days;
    int weekday;

    if (out_weekday == NULL || !parse_date(date, &year, &month, &day)) {
        return -1;
    }

    epoch_days = days_from_civil(year, (unsigned int)month, (unsigned int)day);
    weekday = (int)((epoch_days + 3) % 7);

    if (weekday < 0) {
        weekday += 7;
    }

    *out_weekday = weekday + 1;
    return 0;
}

bool calendar_utc_timestamp_is_valid(const char *timestamp)
{
    char date[11];
    int hour;
    int minute;
    int second;

    if (timestamp == NULL || strlen(timestamp) != 20 ||
        timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != 'T' || timestamp[13] != ':' ||
        timestamp[16] != ':' || timestamp[19] != 'Z') {
        return false;
    }

    for (size_t index = 0; index < 20; index++) {
        if (index == 4 || index == 7 || index == 10 ||
            index == 13 || index == 16 || index == 19) {
            continue;
        }

        if (!is_digit(timestamp[index])) {
            return false;
        }
    }

    memcpy(date, timestamp, 10);
    date[10] = '\0';

    if (!calendar_date_is_valid(date)) {
        return false;
    }

    hour = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
    minute = (timestamp[14] - '0') * 10 + (timestamp[15] - '0');
    second = (timestamp[17] - '0') * 10 + (timestamp[18] - '0');

    return hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}
