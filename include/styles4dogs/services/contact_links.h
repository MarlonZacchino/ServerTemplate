#ifndef STYLES4DOGS_SERVICES_CONTACT_LINKS_H
#define STYLES4DOGS_SERVICES_CONTACT_LINKS_H

/**
 * @file contact_links.h
 * @brief Normalisiert Telefonnummern für Telefon- und WhatsApp-Links.
 */

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Normalisiert eine Telefonnummer in das E.164-Format.
 * @param[in] phone_number Zu normalisierende oder zu prüfende Telefonnummer.
 * @param[in] default_country_code Ländercode ohne Pluszeichen für lokale Nummern.
 * @param[out] out_e164 Ausgabepuffer für die E.164-Nummer.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_phone_to_e164(
        const char *phone_number,
        const char *default_country_code,
        char *out_e164,
        size_t out_size
);

/**
 * @brief Entfernt das Pluszeichen einer E.164-Nummer für WhatsApp-Links.
 * @param[in] e164 Bereits normalisierte E.164-Nummer.
 * @param[out] out_digits Ausgabepuffer für die WhatsApp-Ziffernfolge.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_e164_to_whatsapp_digits(
        const char *e164,
        char *out_digits,
        size_t out_size
);

#endif
