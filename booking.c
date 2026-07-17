//
// Created by Marlon on 16.07.26.
//

#include "booking.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef SERVER_DATA_DIR
#define SERVER_DATA_DIR "data"
#endif

#ifndef SERVER_BOOKING_FILE
#define SERVER_BOOKING_FILE "data/bookings.txt"
#endif

#define MAX_BOOKING_LINE 4096

static const char *find_request_body(const string *request, size_t *out_body_length)
{
    const char *data;
    size_t length;

    if (out_body_length == NULL) {
        return NULL;
    }

    *out_body_length = 0;

    if (request == NULL) {
        return NULL;
    }

    data = get_const_char_str(request);
    length = get_length(request);

    if (data == NULL) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < length; i++) {
        if (data[i] == '\r' &&
            data[i + 1] == '\n' &&
            data[i + 2] == '\r' &&
            data[i + 3] == '\n') {
            *out_body_length = length - (i + 4);
            return data + i + 4;
        }
    }

    for (size_t i = 0; i + 1 < length; i++) {
        if (data[i] == '\n' && data[i + 1] == '\n') {
            *out_body_length = length - (i + 2);
            return data + i + 2;
        }
    }

    return NULL;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static void url_decode_to_buffer(const char *src, size_t src_length, char *dest, size_t dest_size)
{
    size_t src_pos = 0;
    size_t dest_pos = 0;

    if (dest == NULL || dest_size == 0) {
        return;
    }

    while (src_pos < src_length && dest_pos + 1 < dest_size) {
        if (src[src_pos] == '+') {
            dest[dest_pos] = ' ';
            dest_pos++;
            src_pos++;
            continue;
        }

        if (src[src_pos] == '%' && src_pos + 2 < src_length) {
            int high = hex_value(src[src_pos + 1]);
            int low = hex_value(src[src_pos + 2]);

            if (high >= 0 && low >= 0) {
                dest[dest_pos] = (char)((high * 16) + low);
                dest_pos++;
                src_pos += 3;
                continue;
            }
        }

        dest[dest_pos] = src[src_pos];
        dest_pos++;
        src_pos++;
    }

    dest[dest_pos] = '\0';
}

static void get_form_value(
        const char *body,
        size_t body_length,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    size_t field_length;
    size_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (body == NULL || field_name == NULL) {
        return;
    }

    field_length = strlen(field_name);

    while (pos < body_length) {
        size_t key_start = pos;
        size_t key_end;
        size_t value_start;
        size_t value_end;

        while (pos < body_length && body[pos] != '=' && body[pos] != '&') {
            pos++;
        }

        key_end = pos;

        if (pos >= body_length || body[pos] != '=') {
            while (pos < body_length && body[pos] != '&') {
                pos++;
            }

            if (pos < body_length && body[pos] == '&') {
                pos++;
            }

            continue;
        }

        pos++;
        value_start = pos;

        while (pos < body_length && body[pos] != '&') {
            pos++;
        }

        value_end = pos;

        if (key_end - key_start == field_length &&
            strncmp(body + key_start, field_name, field_length) == 0) {
            url_decode_to_buffer(body + value_start, value_end - value_start, out, out_size);
            return;
        }

        if (pos < body_length && body[pos] == '&') {
            pos++;
        }
    }
}

bool parse_booking_request(const string *request, booking_request *booking)
{
    const char *body;
    size_t body_length;

    if (booking == NULL) {
        return false;
    }

    memset(booking, 0, sizeof(*booking));

    body = find_request_body(request, &body_length);

    if (body == NULL) {
        return false;
    }

    get_form_value(body, body_length, "name", booking->name, sizeof(booking->name));
    get_form_value(body, body_length, "contact", booking->contact, sizeof(booking->contact));
    get_form_value(body, body_length, "dog_name", booking->dog_name, sizeof(booking->dog_name));
    get_form_value(body, body_length, "message", booking->message, sizeof(booking->message));

    /*
     * Name und Kontakt sind Pflichtfelder.
     */
    if (booking->name[0] == '\0' || booking->contact[0] == '\0') {
        return false;
    }

    return true;
}

static void write_storage_escaped(FILE *file, const char *text)
{
    if (file == NULL || text == NULL) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        switch (text[i]) {
            case '\\':
                fputs("\\\\", file);
                break;
            case '\n':
                fputs("\\n", file);
                break;
            case '\r':
                fputs("\\r", file);
                break;
            case '\t':
                fputs("\\t", file);
                break;
            default:
                fputc(text[i], file);
                break;
        }
    }
}

int save_booking_request(const booking_request *booking)
{
    FILE *file;
    time_t now;
    struct tm local_time;
    char time_buffer[64];

    if (booking == NULL) {
        return -1;
    }

    if (mkdir(SERVER_DATA_DIR, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    file = fopen(SERVER_BOOKING_FILE, "a");

    if (file == NULL) {
        return -1;
    }

    now = time(NULL);

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "%ld", (long)now);
    }

    /*
     * Format:
     * zeit<TAB>name<TAB>kontakt<TAB>hund<TAB>nachricht
     *
     * Tabs, Zeilenumbrüche und Backslashes in Nutzereingaben werden escaped.
     */
    write_storage_escaped(file, time_buffer);
    fputc('\t', file);
    write_storage_escaped(file, booking->name);
    fputc('\t', file);
    write_storage_escaped(file, booking->contact);
    fputc('\t', file);
    write_storage_escaped(file, booking->dog_name);
    fputc('\t', file);
    write_storage_escaped(file, booking->message);
    fputc('\n', file);

    fclose(file);
    return 0;
}

static void append_html_char_escaped(string *dest, char c)
{
    switch (c) {
        case '&':
            str_cat_cstr(dest, "&amp;");
            break;
        case '<':
            str_cat_cstr(dest, "&lt;");
            break;
        case '>':
            str_cat_cstr(dest, "&gt;");
            break;
        case '"':
            str_cat_cstr(dest, "&quot;");
            break;
        case '\'':
            str_cat_cstr(dest, "&#39;");
            break;
        default:
            str_cat(dest, &c, 1);
            break;
    }
}

static void append_storage_field_as_html(string *dest, const char *src)
{
    if (dest == NULL || src == NULL) {
        return;
    }

    for (size_t i = 0; src[i] != '\0'; i++) {
        if (src[i] == '\\' && src[i + 1] != '\0') {
            i++;

            switch (src[i]) {
                case 'n':
                    append_html_char_escaped(dest, '\n');
                    break;
                case 'r':
                    append_html_char_escaped(dest, '\r');
                    break;
                case 't':
                    append_html_char_escaped(dest, '\t');
                    break;
                case '\\':
                    append_html_char_escaped(dest, '\\');
                    break;
                default:
                    append_html_char_escaped(dest, '\\');
                    append_html_char_escaped(dest, src[i]);
                    break;
            }

            continue;
        }

        append_html_char_escaped(dest, src[i]);
    }
}

static size_t split_booking_line(char *line, char *fields[], size_t max_fields)
{
    size_t count = 0;
    char *start = line;

    if (line == NULL || fields == NULL || max_fields == 0) {
        return 0;
    }

    for (char *pos = line; *pos != '\0'; pos++) {
        if (*pos == '\r' || *pos == '\n') {
            *pos = '\0';
            break;
        }

        if (*pos == '\t') {
            *pos = '\0';

            if (count < max_fields) {
                fields[count] = start;
                count++;
            }

            start = pos + 1;
        }
    }

    if (count < max_fields) {
        fields[count] = start;
        count++;
    }

    return count;
}

static void append_booking_card(string *page, char *fields[])
{
    str_cat_cstr(page, "            <article class=\"booking-card\">\n");

    str_cat_cstr(page, "                <p class=\"booking-time\">");
    append_storage_field_as_html(page, fields[0]);
    str_cat_cstr(page, "</p>\n");

    str_cat_cstr(page, "                <h2>");
    append_storage_field_as_html(page, fields[1]);
    str_cat_cstr(page, "</h2>\n");

    str_cat_cstr(page, "                <div class=\"booking-details\">\n");

    str_cat_cstr(page, "                    <p><span>Kontakt</span><strong>");
    append_storage_field_as_html(page, fields[2]);
    str_cat_cstr(page, "</strong></p>\n");

    str_cat_cstr(page, "                    <p><span>Hund</span><strong>");
    append_storage_field_as_html(page, fields[3]);
    str_cat_cstr(page, "</strong></p>\n");

    str_cat_cstr(page, "                </div>\n");

    str_cat_cstr(page, "                <div class=\"booking-message\">");
    append_storage_field_as_html(page, fields[4]);
    str_cat_cstr(page, "</div>\n");

    str_cat_cstr(page, "            </article>\n");
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
            "            <p class=\"admin-intro\">Hier findest du alle gespeicherten Terminanfragen.</p>\n"
    );

    file = fopen(SERVER_BOOKING_FILE, "rb");

    if (file == NULL) {
        str_cat_cstr(page,
                "            <p>Es wurden noch keine Buchungsanfragen gespeichert.</p>\n"
                "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
                "        </section>\n"
                "    </main>\n"
                "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer>\n"
                "</body>\n"
                "</html>\n"
        );

        return page;
    }

    str_cat_cstr(page, "            <div class=\"booking-grid\">\n");

    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[5];
        size_t field_count = split_booking_line(line, fields, 5);

        if (field_count != 5) {
            continue;
        }

        append_booking_card(page, fields);
        booking_count++;
    }

    fclose(file);

    if (booking_count == 0) {
        str_cat_cstr(page, "                <p>Noch keine lesbaren Buchungsanfragen vorhanden.</p>\n");
    }

    str_cat_cstr(page,
            "            </div>\n"
            "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
            "        </section>\n"
            "    </main>\n"
            "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer>\n"
            "</body>\n"
            "</html>\n"
    );

    return page;
}