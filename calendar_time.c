#include "calendar_time.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void civil_from_days(
        int64_t epoch_days,
        int *out_year,
        unsigned int *out_month,
        unsigned int *out_day
)
{
    int64_t adjusted = epoch_days + 719468;
    int64_t era = (adjusted >= 0 ? adjusted : adjusted - 146096) / 146097;
    unsigned int day_of_era = (unsigned int)(adjusted - era * 146097);
    unsigned int year_of_era =
            (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
             day_of_era / 146096U) / 365U;
    int year = (int)year_of_era + (int)era * 400;
    unsigned int day_of_year =
            day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    unsigned int day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    int month = (int)month_prime + (month_prime < 10U ? 3 : -9);

    year += month <= 2 ? 1 : 0;

    if (out_year != NULL) {
        *out_year = year;
    }
    if (out_month != NULL) {
        *out_month = (unsigned int)month;
    }
    if (out_day != NULL) {
        *out_day = day;
    }
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

int calendar_date_add_days(
        const char *date,
        int days,
        char out_date[11]
)
{
    int year;
    int month;
    int day;
    int result_year;
    unsigned int result_month;
    unsigned int result_day;
    int64_t epoch_days;
    int written;

    if (out_date == NULL || days < -366000 || days > 366000 ||
        !parse_date(date, &year, &month, &day)) {
        return -1;
    }

    epoch_days = days_from_civil(year, (unsigned int)month, (unsigned int)day);
    civil_from_days(epoch_days + days, &result_year, &result_month, &result_day);

    if (result_year < 1970 || result_year > 9999) {
        return -1;
    }

    written = snprintf(
            out_date,
            11,
            "%04d-%02u-%02u",
            result_year,
            result_month,
            result_day);

    return written == 10 ? 0 : -1;
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

int calendar_date_format_de(
        const char *date,
        bool include_weekday,
        char *out_text,
        size_t out_size
)
{
    static const char *const weekday_names[] = {
            "", "Montag", "Dienstag", "Mittwoch", "Donnerstag",
            "Freitag", "Samstag", "Sonntag"
    };
    int year;
    int month;
    int day;
    int weekday = 0;
    int written;

    if (out_text == NULL || out_size == 0 ||
        !parse_date(date, &year, &month, &day) ||
        (include_weekday && calendar_date_iso_weekday(date, &weekday) != 0)) {
        return -1;
    }

    written = include_weekday
            ? snprintf(
                    out_text,
                    out_size,
                    "%02d.%02d.%04d - %s",
                    day,
                    month,
                    year,
                    weekday_names[weekday])
            : snprintf(
                    out_text,
                    out_size,
                    "%02d.%02d.%04d",
                    day,
                    month,
                    year);

    return written >= 0 && (size_t)written < out_size ? 0 : -1;
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

int calendar_utc_timestamp_to_epoch(
        const char *timestamp,
        time_t *out_epoch
)
{
    char date[11];
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int64_t epoch_seconds;
    time_t value;

    if (out_epoch == NULL || !calendar_utc_timestamp_is_valid(timestamp)) {
        return -1;
    }

    memcpy(date, timestamp, 10);
    date[10] = '\0';
    if (!parse_date(date, &year, &month, &day)) {
        return -1;
    }

    hour = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
    minute = (timestamp[14] - '0') * 10 + (timestamp[15] - '0');
    second = (timestamp[17] - '0') * 10 + (timestamp[18] - '0');
    epoch_seconds = days_from_civil(
            year,
            (unsigned int)month,
            (unsigned int)day) * 86400 +
            hour * 3600 + minute * 60 + second;
    value = (time_t)epoch_seconds;

    if ((int64_t)value != epoch_seconds) {
        return -1;
    }

    *out_epoch = value;
    return 0;
}

int calendar_utc_add_minutes(
        const char *timestamp,
        int minutes,
        char out_timestamp[21]
)
{
    char date[11];
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int64_t epoch_seconds;
    int64_t result_seconds;
    int64_t result_days;
    int64_t seconds_of_day;
    int result_year;
    unsigned int result_month;
    unsigned int result_day;
    int written;

    if (out_timestamp == NULL || !calendar_utc_timestamp_is_valid(timestamp) ||
        minutes < -5256000 || minutes > 5256000) {
        return -1;
    }

    memcpy(date, timestamp, 10);
    date[10] = '\0';

    if (!parse_date(date, &year, &month, &day)) {
        return -1;
    }

    hour = (timestamp[11] - '0') * 10 + (timestamp[12] - '0');
    minute = (timestamp[14] - '0') * 10 + (timestamp[15] - '0');
    second = (timestamp[17] - '0') * 10 + (timestamp[18] - '0');

    epoch_seconds = days_from_civil(year, (unsigned int)month, (unsigned int)day) * 86400 +
                    hour * 3600 + minute * 60 + second;
    result_seconds = epoch_seconds + (int64_t)minutes * 60;
    result_days = result_seconds / 86400;
    seconds_of_day = result_seconds % 86400;

    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        result_days--;
    }

    civil_from_days(result_days, &result_year, &result_month, &result_day);

    written = snprintf(
            out_timestamp,
            21,
            "%04d-%02u-%02uT%02lld:%02lld:%02lldZ",
            result_year,
            result_month,
            result_day,
            (long long)(seconds_of_day / 3600),
            (long long)((seconds_of_day % 3600) / 60),
            (long long)(seconds_of_day % 60));

    return written == 20 ? 0 : -1;
}

static bool timezone_is_valid(const char *timezone)
{
    size_t length;

    if (timezone == NULL) {
        return false;
    }

    length = strlen(timezone);
    if (length == 0 || length >= 64 || strstr(timezone, "..") != NULL) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        char character = timezone[index];

        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '/' || character == '_' ||
              character == '-' || character == '+')) {
            return false;
        }
    }

    return true;
}

int calendar_epoch_to_local(
        const char *timezone,
        time_t epoch,
        char out_date[11],
        int *out_minute
)
{
    struct tm local_time;

    if (out_date == NULL || out_minute == NULL || !timezone_is_valid(timezone)) {
        return -1;
    }

    if (setenv("TZ", timezone, 1) != 0) {
        return -1;
    }
    tzset();

    if (localtime_r(&epoch, &local_time) == NULL ||
        strftime(out_date, 11, "%Y-%m-%d", &local_time) != 10) {
        return -1;
    }

    *out_minute = local_time.tm_hour * 60 + local_time.tm_min;
    return 0;
}

int calendar_clock_now(
        const char *timezone,
        calendar_clock_snapshot *snapshot
)
{
    time_t now;
    struct tm local_time;
    struct tm utc_time;

    if (snapshot == NULL || !timezone_is_valid(timezone)) {
        return -1;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return -1;
    }

    if (setenv("TZ", timezone, 1) != 0) {
        return -1;
    }
    tzset();

    if (localtime_r(&now, &local_time) == NULL ||
        gmtime_r(&now, &utc_time) == NULL) {
        return -1;
    }

    if (strftime(snapshot->local_date, sizeof(snapshot->local_date), "%Y-%m-%d", &local_time) != 10 ||
        strftime(snapshot->now_utc, sizeof(snapshot->now_utc), "%Y-%m-%dT%H:%M:%SZ", &utc_time) != 20) {
        return -1;
    }

    snapshot->local_minute = local_time.tm_hour * 60 + local_time.tm_min;
    return 0;
}

int calendar_time_parse_hhmm(const char *text, int *out_minute)
{
    int hour;
    int minute;

    if (text == NULL || out_minute == NULL || strlen(text) != 5 ||
        text[2] != ':' || !is_digit(text[0]) || !is_digit(text[1]) ||
        !is_digit(text[3]) || !is_digit(text[4])) {
        return -1;
    }

    hour = (text[0] - '0') * 10 + (text[1] - '0');
    minute = (text[3] - '0') * 10 + (text[4] - '0');

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return -1;
    }

    *out_minute = hour * 60 + minute;
    return 0;
}

int calendar_time_format_hhmm(int minute, char out_text[6])
{
    int written;

    if (out_text == NULL || minute < 0 || minute > 1440) {
        return -1;
    }

    if (minute == 1440) {
        memcpy(out_text, "24:00", 6);
        return 0;
    }

    written = snprintf(out_text, 6, "%02d:%02d", minute / 60, minute % 60);
    return written == 5 ? 0 : -1;
}


int calendar_local_datetime_to_epoch(
        const char *timezone,
        const char *date,
        int minute,
        time_t *out_epoch
)
{
    int year;
    int month;
    int day;
    struct tm local_time;
    struct tm verification;
    time_t epoch;

    if (!timezone_is_valid(timezone) || !calendar_date_is_valid(date) ||
        minute < 0 || minute > 1439 || out_epoch == NULL ||
        !parse_date(date, &year, &month, &day)) {
        return -1;
    }

    memset(&local_time, 0, sizeof(local_time));
    local_time.tm_year = year - 1900;
    local_time.tm_mon = month - 1;
    local_time.tm_mday = day;
    local_time.tm_hour = minute / 60;
    local_time.tm_min = minute % 60;
    local_time.tm_isdst = -1;

    if (setenv("TZ", timezone, 1) != 0) {
        return -1;
    }
    tzset();

    epoch = mktime(&local_time);
    if (epoch == (time_t)-1 || localtime_r(&epoch, &verification) == NULL) {
        return -1;
    }

    if (verification.tm_year != year - 1900 ||
        verification.tm_mon != month - 1 ||
        verification.tm_mday != day ||
        verification.tm_hour != minute / 60 ||
        verification.tm_min != minute % 60) {
        return -1;
    }

    *out_epoch = epoch;
    return 0;
}
