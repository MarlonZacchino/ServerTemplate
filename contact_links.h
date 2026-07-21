#ifndef STYLES4DOGS_CONTACT_LINKS_H
#define STYLES4DOGS_CONTACT_LINKS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Normalisiert eine validierte Telefonnummer für tel:- und WhatsApp-Links.
 * Lokale Nummern mit führender Null werden mit dem konfigurierten Ländercode
 * ergänzt. out_e164 enthält anschließend +<Ziffern>.
 */
bool contact_phone_to_e164(
        const char *phone_number,
        const char *default_country_code,
        char *out_e164,
        size_t out_size
);

/* Entfernt das führende Plus aus einer normalisierten E.164-Nummer. */
bool contact_e164_to_whatsapp_digits(
        const char *e164,
        char *out_digits,
        size_t out_size
);

#endif
