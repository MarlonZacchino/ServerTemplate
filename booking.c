//
// Created by Marlon on 16.07.26.
//

#include "booking.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

int save_booking_request(const booking_request *booking)
{
    FILE *file;
    time_t now;
    struct tm local_time;
    char time_buffer[64];

    if (booking == NULL) {
        return -1;
    }

    /*
     * Der Ordner enthält später potenziell echte Kundendaten.
     * Deshalb sollte data/ in .gitignore stehen.
     */
    if (mkdir("data", 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    file = fopen("data/bookings.txt", "a");

    if (file == NULL) {
        return -1;
    }

    now = time(NULL);

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "%ld", (long)now);
    }

    fprintf(file, "----- Neue Anfrage -----\n");
    fprintf(file, "Zeit: %s\n", time_buffer);
    fprintf(file, "Name: %s\n", booking->name);
    fprintf(file, "Kontakt: %s\n", booking->contact);
    fprintf(file, "Hund: %s\n", booking->dog_name);
    fprintf(file, "Nachricht: %s\n", booking->message);
    fprintf(file, "\n");

    fclose(file);
    return 0;
}