//
// Created by Marlon on 16.07.26.
//

#include "booking.h"

#include "form_urlencoded.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef SERVER_DATA_DIR
#define SERVER_DATA_DIR "data"
#endif

#ifndef SERVER_BOOKING_FILE
#define SERVER_BOOKING_FILE "data/bookings.txt"
#endif

#define MAX_BOOKING_LINE 8192
#define BOOKING_FIELD_COUNT_V1 5
#define BOOKING_FIELD_COUNT_V2 10
#define BOOKING_STATUS_NEW "neu"
#define PRIVACY_CONSENT_VALUE "accepted"

typedef struct booking_record_view {
    const char *time;
    const char *status;
    const char *name;
    const char *contact;
    const char *dog_name;
    const char *dog_size;
    const char *service;
    const char *preferred_date;
    const char *message;
    bool legacy;
} booking_record_view;

static void trim_ascii_whitespace(char *text)
{
    size_t length;
    size_t start = 0;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (start < length &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        start++;
    }

    while (length > start &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' ||
            text[length - 1] == '\r' || text[length - 1] == '\n')) {
        length--;
    }

    if (start > 0) {
        memmove(text, text + start, length - start);
    }

    text[length - start] = '\0';
}

static bool is_nonempty_single_line(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        unsigned char character = (unsigned char)text[index];

        if (character == '\r' || character == '\n' || character == '\t' ||
            character < 0x20 || character == 0x7f) {
            return false;
        }
    }

    return true;
}

static bool is_optional_single_line(const char *text)
{
    return text != NULL &&
           (text[0] == '\0' || is_nonempty_single_line(text));
}

static bool get_required_field(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            field_name,
            out,
            out_size);

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_nonempty_single_line(out);
}

static bool get_optional_single_line_field(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            field_name,
            out,
            out_size);

    if (result == FORM_VALUE_NOT_FOUND) {
        out[0] = '\0';
        return true;
    }

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_optional_single_line(out);
}

static bool get_optional_message(
        const string *request,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            "message",
            out,
            out_size);

    if (result == FORM_VALUE_NOT_FOUND) {
        out[0] = '\0';
        return true;
    }

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return true;
}

static bool equals_one_of(
        const char *value,
        const char *const allowed_values[],
        size_t allowed_count
)
{
    if (value == NULL || allowed_values == NULL) {
        return false;
    }

    for (size_t index = 0; index < allowed_count; index++) {
        if (strcmp(value, allowed_values[index]) == 0) {
            return true;
        }
    }

    return false;
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool is_valid_iso_date(const char *date)
{
    int year;
    int month;
    int day;
    int max_day;
    static const int days_per_month[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31
    };

    if (date == NULL || date[0] == '\0') {
        return true;
    }

    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (size_t index = 0; index < 10; index++) {
        if (index == 4 || index == 7) {
            continue;
        }

        if (date[index] < '0' || date[index] > '9') {
            return false;
        }
    }

    year = (date[0] - '0') * 1000 +
           (date[1] - '0') * 100 +
           (date[2] - '0') * 10 +
           (date[3] - '0');
    month = (date[5] - '0') * 10 + (date[6] - '0');
    day = (date[8] - '0') * 10 + (date[9] - '0');

    if (year < 2000 || year > 9999 || month < 1 || month > 12) {
        return false;
    }

    max_day = days_per_month[month - 1];

    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }

    return day >= 1 && day <= max_day;
}

bool parse_booking_request(const string *request, booking_request *booking)
{
    static const char *const allowed_dog_sizes[] = {
            "small",
            "medium",
            "large",
            "very_large"
    };
    static const char *const allowed_services[] = {
            "consultation",
            "wash_dry",
            "full_groom",
            "claw_care",
            "other"
    };
    char privacy_consent[32];

    if (request == NULL || booking == NULL) {
        return false;
    }

    memset(booking, 0, sizeof(*booking));
    memset(privacy_consent, 0, sizeof(privacy_consent));

    if (!get_required_field(
            request,
            "name",
            booking->name,
            sizeof(booking->name)) ||
        !get_required_field(
            request,
            "contact",
            booking->contact,
            sizeof(booking->contact)) ||
        !get_optional_single_line_field(
            request,
            "dog_name",
            booking->dog_name,
            sizeof(booking->dog_name)) ||
        !get_required_field(
            request,
            "dog_size",
            booking->dog_size,
            sizeof(booking->dog_size)) ||
        !get_required_field(
            request,
            "service",
            booking->service,
            sizeof(booking->service)) ||
        !get_optional_single_line_field(
            request,
            "preferred_date",
            booking->preferred_date,
            sizeof(booking->preferred_date)) ||
        !get_optional_message(
            request,
            booking->message,
            sizeof(booking->message)) ||
        !get_required_field(
            request,
            "privacy_consent",
            privacy_consent,
            sizeof(privacy_consent))) {
        return false;
    }

    if (!equals_one_of(
            booking->dog_size,
            allowed_dog_sizes,
            sizeof(allowed_dog_sizes) / sizeof(allowed_dog_sizes[0])) ||
        !equals_one_of(
            booking->service,
            allowed_services,
            sizeof(allowed_services) / sizeof(allowed_services[0])) ||
        !is_valid_iso_date(booking->preferred_date) ||
        strcmp(privacy_consent, PRIVACY_CONSENT_VALUE) != 0) {
        return false;
    }

    return true;
}

static void append_storage_escaped(string *destination, const char *text)
{
    if (destination == NULL || text == NULL) {
        return;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        switch (text[index]) {
            case '\\':
                str_cat_cstr(destination, "\\\\");
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
                str_cat(destination, text + index, 1);
                break;
        }
    }
}

static int ensure_data_directory(void)
{
    struct stat directory_status;

    if (mkdir(SERVER_DATA_DIR, 0750) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        return -1;
    }

    if (lstat(SERVER_DATA_DIR, &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode)) {
        return -1;
    }

    return chmod(SERVER_DATA_DIR, 0750) == 0 ? 0 : -1;
}

static int write_all_to_file(
        int file_descriptor,
        const char *data,
        size_t length
)
{
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(
                file_descriptor,
                data + written_total,
                length - written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (written == 0) {
            return -1;
        }

        written_total += (size_t)written;
    }

    return 0;
}

int save_booking_request(const booking_request *booking)
{
    time_t now;
    struct tm local_time;
    char time_buffer[64];
    string *line;
    int file_descriptor;
    int open_flags = O_WRONLY | O_CREAT | O_APPEND;
    int result = -1;

    if (booking == NULL || ensure_data_directory() != 0) {
        return -1;
    }

    now = time(NULL);

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(
                time_buffer,
                sizeof(time_buffer),
                "%Y-%m-%d %H:%M:%S",
                &local_time);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "%ld", (long)now);
    }

    line = _new_string();

    str_cat_cstr(line, "v2\t");
    append_storage_escaped(line, time_buffer);
    str_cat_cstr(line, "\t" BOOKING_STATUS_NEW "\t");
    append_storage_escaped(line, booking->name);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->contact);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->dog_name);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->dog_size);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->service);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->preferred_date);
    str_cat_cstr(line, "\t");
    append_storage_escaped(line, booking->message);
    str_cat_cstr(line, "\n");

#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif

#ifdef O_NOFOLLOW
    open_flags |= O_NOFOLLOW;
#endif

    file_descriptor = open(SERVER_BOOKING_FILE, open_flags, 0600);

    if (file_descriptor < 0) {
        free_str(line);
        return -1;
    }

    if (fchmod(file_descriptor, 0600) == 0 &&
        write_all_to_file(
                file_descriptor,
                get_const_char_str(line),
                get_length(line)) == 0 &&
        fsync(file_descriptor) == 0) {
        result = 0;
    }

    if (close(file_descriptor) != 0) {
        result = -1;
    }

    free_str(line);
    return result;
}

static void append_html_char_escaped(string *destination, char character)
{
    switch (character) {
        case '&':
            str_cat_cstr(destination, "&amp;");
            break;
        case '<':
            str_cat_cstr(destination, "&lt;");
            break;
        case '>':
            str_cat_cstr(destination, "&gt;");
            break;
        case '"':
            str_cat_cstr(destination, "&quot;");
            break;
        case '\'':
            str_cat_cstr(destination, "&#39;");
            break;
        default:
            str_cat(destination, &character, 1);
            break;
    }
}

static void append_storage_field_as_html(
        string *destination,
        const char *source
)
{
    if (destination == NULL || source == NULL) {
        return;
    }

    for (size_t index = 0; source[index] != '\0'; index++) {
        if (source[index] == '\\' && source[index + 1] != '\0') {
            index++;

            switch (source[index]) {
                case 'n':
                    append_html_char_escaped(destination, '\n');
                    break;
                case 'r':
                    append_html_char_escaped(destination, '\r');
                    break;
                case 't':
                    append_html_char_escaped(destination, '\t');
                    break;
                case '\\':
                    append_html_char_escaped(destination, '\\');
                    break;
                default:
                    append_html_char_escaped(destination, '\\');
                    append_html_char_escaped(destination, source[index]);
                    break;
            }

            continue;
        }

        append_html_char_escaped(destination, source[index]);
    }
}

static void append_storage_field_or_fallback(
        string *destination,
        const char *source,
        const char *fallback
)
{
    if (source == NULL || source[0] == '\0') {
        str_cat_cstr(destination, fallback);
        return;
    }

    append_storage_field_as_html(destination, source);
}

static size_t split_booking_line(
        char *line,
        char *fields[],
        size_t max_fields
)
{
    size_t count = 0;
    char *start;

    if (line == NULL || fields == NULL || max_fields == 0) {
        return 0;
    }

    start = line;

    for (char *position = line; *position != '\0'; position++) {
        if (*position == '\r' || *position == '\n') {
            *position = '\0';
            break;
        }

        if (*position == '\t') {
            if (count >= max_fields) {
                return max_fields + 1;
            }

            *position = '\0';
            fields[count] = start;
            count++;
            start = position + 1;
        }
    }

    if (count >= max_fields) {
        return max_fields + 1;
    }

    fields[count] = start;
    return count + 1;
}

static bool parse_booking_record(
        char *line,
        booking_record_view *record
)
{
    char *fields[BOOKING_FIELD_COUNT_V2];
    size_t field_count;

    if (line == NULL || record == NULL) {
        return false;
    }

    memset(record, 0, sizeof(*record));
    field_count = split_booking_line(
            line,
            fields,
            BOOKING_FIELD_COUNT_V2);

    if (field_count == BOOKING_FIELD_COUNT_V1) {
        record->time = fields[0];
        record->status = "Altbestand";
        record->name = fields[1];
        record->contact = fields[2];
        record->dog_name = fields[3];
        record->dog_size = "";
        record->service = "";
        record->preferred_date = "";
        record->message = fields[4];
        record->legacy = true;
        return true;
    }

    if (field_count == BOOKING_FIELD_COUNT_V2 &&
        strcmp(fields[0], "v2") == 0) {
        record->time = fields[1];
        record->status = fields[2];
        record->name = fields[3];
        record->contact = fields[4];
        record->dog_name = fields[5];
        record->dog_size = fields[6];
        record->service = fields[7];
        record->preferred_date = fields[8];
        record->message = fields[9];
        record->legacy = false;
        return true;
    }

    return false;
}

static const char *dog_size_label(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "Nicht angegeben";
    }

    if (strcmp(value, "small") == 0) {
        return "Klein";
    }

    if (strcmp(value, "medium") == 0) {
        return "Mittel";
    }

    if (strcmp(value, "large") == 0) {
        return "Groß";
    }

    if (strcmp(value, "very_large") == 0) {
        return "Sehr groß";
    }

    return value;
}

static const char *service_label(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "Nicht angegeben";
    }

    if (strcmp(value, "consultation") == 0) {
        return "Beratung";
    }

    if (strcmp(value, "wash_dry") == 0) {
        return "Waschen und Föhnen";
    }

    if (strcmp(value, "full_groom") == 0) {
        return "Komplettpflege";
    }

    if (strcmp(value, "claw_care") == 0) {
        return "Krallenpflege";
    }

    if (strcmp(value, "other") == 0) {
        return "Sonstiges";
    }

    return value;
}

static void append_booking_detail(
        string *page,
        const char *label,
        const char *value,
        const char *fallback
)
{
    str_cat_cstr(page, "                    <p><span>");
    str_cat_cstr(page, label);
    str_cat_cstr(page, "</span><strong>");
    append_storage_field_or_fallback(page, value, fallback);
    str_cat_cstr(page, "</strong></p>\n");
}

static void append_booking_card(
        string *page,
        const booking_record_view *record
)
{
    const char *size_label;
    const char *selected_service_label;

    if (page == NULL || record == NULL) {
        return;
    }

    size_label = dog_size_label(record->dog_size);
    selected_service_label = service_label(record->service);

    str_cat_cstr(page,
            "            <article class=\"booking-card\">\n"
            "                <div class=\"booking-card-head\">\n"
            "                    <p class=\"booking-time\">");
    append_storage_field_as_html(page, record->time);
    str_cat_cstr(page,
            "</p>\n"
            "                    <span class=\"booking-status\">");
    append_storage_field_as_html(page, record->status);
    str_cat_cstr(page,
            "</span>\n"
            "                </div>\n"
            "                <h2>");
    append_storage_field_as_html(page, record->name);
    str_cat_cstr(page,
            "</h2>\n"
            "                <div class=\"booking-details\">\n");

    append_booking_detail(page, "Kontakt", record->contact, "Nicht angegeben");
    append_booking_detail(page, "Hund", record->dog_name, "Nicht angegeben");
    append_booking_detail(page, "Größe", size_label, "Nicht angegeben");
    append_booking_detail(page, "Leistung", selected_service_label, "Nicht angegeben");
    append_booking_detail(page, "Wunschdatum", record->preferred_date, "Flexibel");

    if (record->legacy) {
        append_booking_detail(page, "Format", "Frühere Anfrage", "Frühere Anfrage");
    }

    str_cat_cstr(page,
            "                </div>\n"
            "                <div class=\"booking-message\">");
    append_storage_field_or_fallback(
            page,
            record->message,
            "Keine Nachricht angegeben.");
    str_cat_cstr(page,
            "</div>\n"
            "            </article>\n");
}

string *build_booking_admin_page(void)
{
    FILE *file;
    char line[MAX_BOOKING_LINE];
    size_t booking_count = 0;
    string *page = _new_string();

    str_cat_cstr(page,
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <meta name=\"robots\" content=\"noindex,nofollow\">\n"
            "    <title>Buchungsanfragen - Styles 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <header class=\"site-header\">\n"
            "        <div class=\"container nav-wrap\">\n"
            "            <a class=\"brand\" href=\"/\"><span class=\"brand-mark\">S4D</span><span>Styles 4 Dogs</span></a>\n"
            "            <nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/\">Website öffnen</a><a href=\"/admin/bookings\" aria-current=\"page\">Buchungsanfragen</a></nav>\n"
            "        </div>\n"
            "    </header>\n"
            "    <main class=\"page admin-page\">\n"
            "        <section class=\"card admin-card\">\n"
            "            <p class=\"eyebrow\">Admin</p>\n"
            "            <h1>Buchungsanfragen</h1>\n"
            "            <p class=\"admin-intro\">Hier findest du alle gespeicherten Terminanfragen.</p>\n");

    file = fopen(SERVER_BOOKING_FILE, "rb");

    if (file == NULL) {
        str_cat_cstr(page,
                "            <p>Es wurden noch keine Buchungsanfragen gespeichert.</p>\n"
                "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
                "        </section>\n"
                "    </main>\n"
                "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer>\n"
                "</body>\n"
                "</html>\n");

        return page;
    }

    str_cat_cstr(page, "            <div class=\"booking-grid\">\n");

    while (fgets(line, sizeof(line), file) != NULL) {
        booking_record_view record;

        if (!parse_booking_record(line, &record)) {
            continue;
        }

        append_booking_card(page, &record);
        booking_count++;
    }

    fclose(file);

    if (booking_count == 0) {
        str_cat_cstr(page,
                "                <p>Noch keine lesbaren Buchungsanfragen vorhanden.</p>\n");
    }

    str_cat_cstr(page,
            "            </div>\n"
            "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
            "        </section>\n"
            "    </main>\n"
            "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer>\n"
            "</body>\n"
            "</html>\n");

    return page;
}
