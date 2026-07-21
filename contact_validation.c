#include "contact_validation.h"

#include <stddef.h>
#include <string.h>

#define CONTACT_EMAIL_MAX_LENGTH 255
#define CONTACT_PHONE_MAX_LENGTH 63

static bool is_single_line_with_length(
        const char *text,
        size_t minimum_length,
        size_t maximum_length
)
{
    size_t length;

    if (text == NULL) {
        return false;
    }

    length = strlen(text);
    if (length < minimum_length || length > maximum_length) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)text[index];

        if (character == '\r' || character == '\n' || character == '\0') {
            return false;
        }
    }

    return true;
}


bool contact_aggregate_matches_fields(
        const char *contact,
        const char *channel,
        const char *email,
        const char *phone_number
)
{
    if (contact == NULL || channel == NULL || email == NULL || phone_number == NULL) {
        return false;
    }

    if (strcmp(channel, "email") == 0) {
        return strcmp(contact, email) == 0;
    }

    if (strcmp(channel, "phone") == 0) {
        return strcmp(contact, phone_number) == 0;
    }

    return false;
}

bool contact_email_is_valid(const char *email)
{
    const char *at;
    const char *dot;

    if (!is_single_line_with_length(email, 3, CONTACT_EMAIL_MAX_LENGTH)) {
        return false;
    }

    at = strchr(email, '@');
    if (at == NULL || at == email || strchr(at + 1, '@') != NULL || at[1] == '\0') {
        return false;
    }

    dot = strrchr(at + 1, '.');
    return dot != NULL && dot > at + 1 && dot[1] != '\0';
}

bool contact_phone_number_is_valid(const char *phone_number)
{
    size_t digits = 0;

    if (!is_single_line_with_length(phone_number, 6, CONTACT_PHONE_MAX_LENGTH)) {
        return false;
    }

    for (size_t index = 0; phone_number[index] != '\0'; index++) {
        unsigned char character = (unsigned char)phone_number[index];

        if (character >= '0' && character <= '9') {
            digits++;
            continue;
        }

        if (character == '+' || character == ' ' || character == '-' ||
            character == '(' || character == ')' || character == '/') {
            continue;
        }

        return false;
    }

    return digits >= 6 && digits <= 20;
}

bool contact_fields_are_valid(
        const char *channel,
        const char *email,
        const char *phone_number,
        const char *phone_kind,
        const char *contact_preference
)
{
    if (channel == NULL || email == NULL || phone_number == NULL ||
        phone_kind == NULL || contact_preference == NULL) {
        return false;
    }

    if (strcmp(channel, "email") == 0) {
        return contact_email_is_valid(email) &&
               phone_number[0] == '\0' &&
               phone_kind[0] == '\0' &&
               contact_preference[0] == '\0';
    }

    if (strcmp(channel, "phone") == 0) {
        bool is_landline = strcmp(phone_kind, "landline") == 0;
        bool is_mobile = strcmp(phone_kind, "mobile") == 0;
        bool wants_call = strcmp(contact_preference, "call") == 0;
        bool wants_whatsapp = strcmp(contact_preference, "whatsapp") == 0;

        return email[0] == '\0' &&
               contact_phone_number_is_valid(phone_number) &&
               (is_landline || is_mobile) &&
               (wants_call || wants_whatsapp) &&
               (!wants_whatsapp || is_mobile);
    }

    return false;
}
