#include "form_urlencoded.h"

#include <stdbool.h>
#include <string.h>

#define MAX_FORM_KEY_LENGTH 128

static const char *find_request_body(
        const string *request,
        size_t *out_body_length
)
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

    for (size_t index = 0; index + 3 < length; index++) {
        if (data[index] == '\r' &&
            data[index + 1] == '\n' &&
            data[index + 2] == '\r' &&
            data[index + 3] == '\n') {
            *out_body_length = length - (index + 4);
            return data + index + 4;
        }
    }

    for (size_t index = 0; index + 1 < length; index++) {
        if (data[index] == '\n' && data[index + 1] == '\n') {
            *out_body_length = length - (index + 2);
            return data + index + 2;
        }
    }

    return NULL;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }

    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }

    return -1;
}

static form_value_result decode_component(
        const char *source,
        size_t source_length,
        char *destination,
        size_t destination_size
)
{
    size_t source_position = 0;
    size_t destination_position = 0;

    if (source == NULL || destination == NULL || destination_size == 0) {
        return FORM_VALUE_INVALID;
    }

    while (source_position < source_length) {
        unsigned char decoded_character;

        if (destination_position + 1 >= destination_size) {
            destination[0] = '\0';
            return FORM_VALUE_TOO_LARGE;
        }

        if (source[source_position] == '+') {
            decoded_character = (unsigned char)' ';
            source_position++;
        } else if (source[source_position] == '%') {
            int high;
            int low;

            if (source_position + 2 >= source_length) {
                destination[0] = '\0';
                return FORM_VALUE_INVALID;
            }

            high = hex_value(source[source_position + 1]);
            low = hex_value(source[source_position + 2]);

            if (high < 0 || low < 0) {
                destination[0] = '\0';
                return FORM_VALUE_INVALID;
            }

            decoded_character = (unsigned char)((high << 4) | low);
            source_position += 3;
        } else {
            decoded_character = (unsigned char)source[source_position];
            source_position++;
        }

        /* C-Strings können keine eingebetteten Nullbytes sicher abbilden. */
        if (decoded_character == '\0') {
            destination[0] = '\0';
            return FORM_VALUE_INVALID;
        }

        destination[destination_position] = (char)decoded_character;
        destination_position++;
    }

    destination[destination_position] = '\0';
    return FORM_VALUE_OK;
}

form_value_result form_urlencoded_get(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    const char *body;
    size_t body_length;
    size_t position = 0;
    bool found = false;
    char decoded_key[MAX_FORM_KEY_LENGTH];

    if (out == NULL || out_size == 0) {
        return FORM_VALUE_INVALID;
    }

    out[0] = '\0';

    if (request == NULL || field_name == NULL || field_name[0] == '\0') {
        return FORM_VALUE_INVALID;
    }

    body = find_request_body(request, &body_length);

    if (body == NULL) {
        return FORM_VALUE_INVALID;
    }

    while (position < body_length) {
        size_t pair_start = position;
        size_t pair_end;
        size_t equals_position;
        form_value_result key_result;

        while (position < body_length && body[position] != '&') {
            position++;
        }

        pair_end = position;
        equals_position = pair_start;

        while (equals_position < pair_end && body[equals_position] != '=') {
            equals_position++;
        }

        if (equals_position == pair_end) {
            return FORM_VALUE_INVALID;
        }

        key_result = decode_component(
                body + pair_start,
                equals_position - pair_start,
                decoded_key,
                sizeof(decoded_key));

        if (key_result != FORM_VALUE_OK) {
            return key_result;
        }

        if (strcmp(decoded_key, field_name) == 0) {
            form_value_result value_result;

            if (found) {
                out[0] = '\0';
                return FORM_VALUE_DUPLICATE;
            }

            value_result = decode_component(
                    body + equals_position + 1,
                    pair_end - equals_position - 1,
                    out,
                    out_size);

            if (value_result != FORM_VALUE_OK) {
                return value_result;
            }

            found = true;
        }

        if (position < body_length && body[position] == '&') {
            position++;
        }
    }

    return found ? FORM_VALUE_OK : FORM_VALUE_NOT_FOUND;
}
