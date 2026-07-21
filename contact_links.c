#include "contact_links.h"

#include <string.h>

static bool country_code_is_valid(const char *country_code)
{
    size_t length;

    if (country_code == NULL) {
        return false;
    }

    length = strlen(country_code);
    if (length < 1 || length > 4) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        if (country_code[index] < '0' || country_code[index] > '9') {
            return false;
        }
    }

    return country_code[0] != '0';
}

bool contact_phone_to_e164(
        const char *phone_number,
        const char *default_country_code,
        char *out_e164,
        size_t out_size
)
{
    char digits[32];
    size_t digit_count = 0;
    bool had_plus = false;
    bool had_double_zero = false;
    size_t start = 0;
    size_t output_position = 0;

    if (phone_number == NULL || out_e164 == NULL || out_size < 4 ||
        !country_code_is_valid(default_country_code)) {
        return false;
    }

    for (size_t index = 0; phone_number[index] != '\0'; index++) {
        char character = phone_number[index];

        if (character >= '0' && character <= '9') {
            if (digit_count + 1 >= sizeof(digits)) {
                return false;
            }
            digits[digit_count++] = character;
            continue;
        }

        if (character == '+' && index == 0) {
            had_plus = true;
            continue;
        }

        if (character == ' ' || character == '-' || character == '(' ||
            character == ')' || character == '/') {
            continue;
        }

        return false;
    }

    digits[digit_count] = '\0';
    if (digit_count < 6 || digit_count > 20) {
        return false;
    }

    if (!had_plus && digit_count >= 2 && digits[0] == '0' && digits[1] == '0') {
        had_double_zero = true;
        start = 2;
    }

    out_e164[output_position++] = '+';

    if (!had_plus && !had_double_zero) {
        size_t country_length = strlen(default_country_code);

        if (digits[0] == '0') {
            start = 1;
        }

        if (output_position + country_length + digit_count - start >= out_size) {
            return false;
        }

        memcpy(out_e164 + output_position, default_country_code, country_length);
        output_position += country_length;
    }

    if (start >= digit_count || output_position + digit_count - start >= out_size) {
        return false;
    }

    memcpy(out_e164 + output_position, digits + start, digit_count - start);
    output_position += digit_count - start;
    out_e164[output_position] = '\0';
    return true;
}

bool contact_e164_to_whatsapp_digits(
        const char *e164,
        char *out_digits,
        size_t out_size
)
{
    size_t length;

    if (e164 == NULL || e164[0] != '+' || out_digits == NULL || out_size == 0) {
        return false;
    }

    length = strlen(e164 + 1);
    if (length == 0 || length >= out_size) {
        return false;
    }

    for (size_t index = 1; e164[index] != '\0'; index++) {
        if (e164[index] < '0' || e164[index] > '9') {
            return false;
        }
    }

    memcpy(out_digits, e164 + 1, length + 1);
    return true;
}
