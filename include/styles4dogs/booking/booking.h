#ifndef STYLES4DOGS_BOOKING_BOOKING_H
#define STYLES4DOGS_BOOKING_BOOKING_H

/**
 * @file booking.h
 * @brief Definiert öffentliche Buchungsdaten und die Admin-Darstellung von Buchungsanfragen.
 */

#include <stdbool.h>
#include <stddef.h>

#include "styles4dogs/http/http_lib.h"

/** @brief Maximale Länge des zusammengesetzten Kundennamens einschließlich Nullterminator. */
#define BOOKING_NAME_SIZE 256
/** @brief Maximale Länge des Kundenvornamens einschließlich Nullterminator. */
#define BOOKING_FIRST_NAME_SIZE 128
/** @brief Maximale Länge des Kundennachnamens einschließlich Nullterminator. */
#define BOOKING_LAST_NAME_SIZE 128
/** @brief Maximale Länge der zusammengefassten Kontaktangabe einschließlich Nullterminator. */
#define BOOKING_CONTACT_SIZE 256
/** @brief Maximale Länge des stabilen Kontaktkanal-Codes einschließlich Nullterminator. */
#define BOOKING_CONTACT_CHANNEL_SIZE 16
/** @brief Maximale Länge einer E-Mail-Adresse einschließlich Nullterminator. */
#define BOOKING_EMAIL_SIZE 256
/** @brief Maximale Länge einer Telefonnummer einschließlich Nullterminator. */
#define BOOKING_PHONE_SIZE 64
/** @brief Maximale Länge des Telefonart-Codes einschließlich Nullterminator. */
#define BOOKING_PHONE_KIND_SIZE 16
/** @brief Maximale Länge der Kontaktpräferenz einschließlich Nullterminator. */
#define BOOKING_CONTACT_PREFERENCE_SIZE 16
/** @brief Maximale Länge von Straße und Hausnummer einschließlich Nullterminator. */
#define BOOKING_STREET_ADDRESS_SIZE 256
/** @brief Puffergröße für eine deutsche Postleitzahl einschließlich Nullterminator. */
#define BOOKING_POSTAL_CODE_SIZE 6
/** @brief Maximale Länge des Wohnorts einschließlich Nullterminator. */
#define BOOKING_CITY_SIZE 128
/** @brief Maximale Länge des Hundenamens einschließlich Nullterminator. */
#define BOOKING_DOG_NAME_SIZE 256
/** @brief Maximale Länge der Hunderasse einschließlich Nullterminator. */
#define BOOKING_DOG_BREED_SIZE 64
/** @brief Maximale Länge der Größenangabe des Hundes einschließlich Nullterminator. */
#define BOOKING_DOG_SIZE_SIZE 64
/** @brief Maximale Länge des Leistungscodes einschließlich Nullterminator. */
#define BOOKING_SERVICE_SIZE 128
/** @brief Puffergröße für ein Datum im Format YYYY-MM-DD einschließlich Nullterminator. */
#define BOOKING_APPOINTMENT_DATE_SIZE 11
/** @brief Puffergröße für eine Uhrzeit im Format HH:MM einschließlich Nullterminator. */
#define BOOKING_APPOINTMENT_START_SIZE 6
/** @brief Maximale Länge einer freien Terminwunschangabe einschließlich Nullterminator. */
#define BOOKING_PREFERRED_DATE_SIZE 32
/** @brief Maximale Länge der Kundennachricht einschließlich Nullterminator. */
#define BOOKING_MESSAGE_SIZE 1024
/** @brief Maximale Länge eines Admin-Statusfilters einschließlich Nullterminator. */
#define BOOKING_ADMIN_STATUS_SIZE 32
/** @brief Maximale Länge des Admin-Suchbegriffs einschließlich Nullterminator. */
#define BOOKING_ADMIN_SEARCH_SIZE 128

/**
 * @brief Validierte Daten einer öffentlichen Buchungsanfrage.
 */
typedef struct booking_request {
    char name[BOOKING_NAME_SIZE]; /**< Vollständiger Kundenname. */
    char first_name[BOOKING_FIRST_NAME_SIZE]; /**< Vorname des Kunden. */
    char last_name[BOOKING_LAST_NAME_SIZE]; /**< Nachname des Kunden. */
    char contact[BOOKING_CONTACT_SIZE]; /**< Primäre Kontaktangabe. */
    char contact_channel[BOOKING_CONTACT_CHANNEL_SIZE]; /**< Gewählter Kontaktkanal. */
    char email[BOOKING_EMAIL_SIZE]; /**< E-Mail-Adresse des Kunden. */
    char phone_number[BOOKING_PHONE_SIZE]; /**< Telefonnummer des Kunden. */
    char phone_kind[BOOKING_PHONE_KIND_SIZE]; /**< Art der Telefonnummer. */
    char contact_preference[BOOKING_CONTACT_PREFERENCE_SIZE]; /**< Bevorzugter Kontaktweg. */
    char street_address[BOOKING_STREET_ADDRESS_SIZE]; /**< Straße und Hausnummer. */
    char postal_code[BOOKING_POSTAL_CODE_SIZE]; /**< Deutsche Postleitzahl. */
    char city[BOOKING_CITY_SIZE]; /**< Wohnort. */
    char dog_name[BOOKING_DOG_NAME_SIZE]; /**< Name des Hundes. */
    char dog_breed[BOOKING_DOG_BREED_SIZE]; /**< Rasse des Hundes. */
    char dog_size[BOOKING_DOG_SIZE_SIZE]; /**< Größenklasse des Hundes. */
    char service[BOOKING_SERVICE_SIZE]; /**< Stabiler Code der gewählten Leistung. */
    char appointment_date[BOOKING_APPOINTMENT_DATE_SIZE]; /**< Termindatum im Format YYYY-MM-DD. */
    char appointment_start[BOOKING_APPOINTMENT_START_SIZE]; /**< Startzeit im Format HH:MM. */
    char preferred_date[BOOKING_PREFERRED_DATE_SIZE]; /**< Freitext für einen alternativen Terminwunsch. */
    char message[BOOKING_MESSAGE_SIZE]; /**< Optionale Kundennachricht. */
} booking_request; /**< Typalias für ::booking_request. */

/**
 * @brief Filterkriterien für die Adminansicht der Buchungsanfragen.
 */
typedef struct booking_admin_filter {
    char status[BOOKING_ADMIN_STATUS_SIZE]; /**< Optionaler Statusfilter. */
    char search[BOOKING_ADMIN_SEARCH_SIZE]; /**< Freier Suchbegriff für die Adminansicht. */
} booking_admin_filter; /**< Typalias für ::booking_admin_filter. */

/**
 * @brief Liest und validiert eine Buchungsanfrage aus einem HTTP-Request.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @param[out] booking Validierte Buchungsdaten beziehungsweise Zielstruktur.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool parse_booking_request(const string *request, booking_request *booking);

/**
 * @brief Prüft, ob das unsichtbare Bot-Feld ausgefüllt wurde.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool booking_request_hits_honeypot(const string *request);

/**
 * @brief Liest Status- und Suchfilter der Buchungsverwaltung.
 * @param[in] query Query-String ohne führendes Fragezeichen; darf NULL sein.
 * @param[in] query_length Länge von @p query in Bytes.
 * @param[out] filter Zielstruktur oder anzuwendender Adminfilter.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool parse_booking_admin_filter(
        const char *query,
        size_t query_length,
        booking_admin_filter *filter
);

/**
 * @brief Baut die gefilterte Adminseite für Buchungsanfragen.
 * @param[in] csrf_token Gültiger CSRF-Token für schreibende Formulare.
 * @param[in] filter Zielstruktur oder anzuwendender Adminfilter.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *build_booking_admin_page(
        const char *csrf_token,
        const booking_admin_filter *filter
);

#endif
