#ifndef CONTACT_VALIDATION_H
#define CONTACT_VALIDATION_H

#include <stdbool.h>

/**
 * Validates the structured customer contact fields used by bookings.
 *
 * Supported combinations:
 * - email + a valid email address, all phone fields empty
 * - phone + landline/mobile + call/whatsapp, with WhatsApp only for mobile
 */
bool contact_fields_are_valid(
        const char *channel,
        const char *email,
        const char *phone_number,
        const char *phone_kind,
        const char *contact_preference
);

bool contact_aggregate_matches_fields(
        const char *contact,
        const char *channel,
        const char *email,
        const char *phone_number
);

bool contact_email_is_valid(const char *email);
bool contact_phone_number_is_valid(const char *phone_number);

#endif
