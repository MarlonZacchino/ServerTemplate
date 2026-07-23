#ifndef STYLES4DOGS_SERVICES_CONTACT_VALIDATION_H
#define STYLES4DOGS_SERVICES_CONTACT_VALIDATION_H

/**
 * @file contact_validation.h
 * @brief Validiert strukturierte E-Mail- und Telefondaten.
 */

#include <stdbool.h>

/**
 * @brief Prüft die erlaubten Kombinationen strukturierter Kontaktdaten.
 * @param[in] channel Kontaktkanal email oder phone.
 * @param[in] email E-Mail-Adresse; abhängig vom Kontaktkanal darf sie leer sein.
 * @param[in] phone_number Zu normalisierende oder zu prüfende Telefonnummer.
 * @param[in] phone_kind Telefonart landline oder mobile.
 * @param[in] contact_preference Kontaktpräferenz call oder whatsapp.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_fields_are_valid(
        const char *channel,
        const char *email,
        const char *phone_number,
        const char *phone_kind,
        const char *contact_preference
);

/**
 * @brief Prüft, ob das zusammengefasste Kontaktfeld zu den strukturierten Feldern passt.
 * @param[in] contact Zusammengefasster Kontaktwert.
 * @param[in] channel Kontaktkanal email oder phone.
 * @param[in] email E-Mail-Adresse; abhängig vom Kontaktkanal darf sie leer sein.
 * @param[in] phone_number Zu normalisierende oder zu prüfende Telefonnummer.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_aggregate_matches_fields(
        const char *contact,
        const char *channel,
        const char *email,
        const char *phone_number
);

/**
 * @brief Prüft eine E-Mail-Adresse auf das unterstützte Format.
 * @param[in] email E-Mail-Adresse; abhängig vom Kontaktkanal darf sie leer sein.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_email_is_valid(const char *email);
/**
 * @brief Prüft eine Telefonnummer auf das unterstützte Format.
 * @param[in] phone_number Zu normalisierende oder zu prüfende Telefonnummer.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool contact_phone_number_is_valid(const char *phone_number);

#endif
