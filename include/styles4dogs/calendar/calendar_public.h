#ifndef STYLES4DOGS_CALENDAR_CALENDAR_PUBLIC_H
#define STYLES4DOGS_CALENDAR_CALENDAR_PUBLIC_H

/**
 * @file calendar_public.h
 * @brief Stellt die öffentlichen Kalender-Endpunkte für Leistungen und Verfügbarkeit bereit.
 */

#include <stddef.h>
#include <stdint.h>

#include "styles4dogs/booking/booking.h"
#include "styles4dogs/http/http_lib.h"

/**
 * @brief Ergebnis einer öffentlichen Kalender- oder Reservierungsanfrage.
 */
typedef enum calendar_public_result {
    CALENDAR_PUBLIC_ERROR = -1, /**< Interner Fehler. */
    CALENDAR_PUBLIC_OK = 0, /**< Operation erfolgreich. */
    CALENDAR_PUBLIC_BAD_REQUEST = 1, /**< Ungültige oder unvollständige Eingabe. */
    CALENDAR_PUBLIC_NOT_FOUND = 2, /**< Angeforderter Datensatz wurde nicht gefunden. */
    CALENDAR_PUBLIC_UNAVAILABLE = 3, /**< Gewählter Termin ist nicht verfügbar. */
    CALENDAR_PUBLIC_CONFIRMED = 4, /**< Buchung wurde unmittelbar bestätigt. */
    CALENDAR_PUBLIC_CONTACT_LIMIT = 5 /**< Kontaktbezogenes Buchungslimit wurde erreicht. */
} calendar_public_result; /**< Typalias für ::calendar_public_result. */

/**
 * @brief Erzeugt die öffentliche JSON-Liste aktiver Leistungen.
 * @param[out] out_json Ausgabeparameter für einen neu allozierten JSON-String.
 * @return Ein Wert aus ::calendar_public_result.
 */
calendar_public_result calendar_public_build_services_json(string **out_json);

/**
 * @brief Erzeugt die öffentliche Verfügbarkeitsantwort für eine Anfrage.
 * @param[in] query Query-String ohne führendes Fragezeichen; darf NULL sein.
 * @param[in] query_length Länge von @p query in Bytes.
 * @param[out] out_json Ausgabeparameter für einen neu allozierten JSON-String.
 * @return Ein Wert aus ::calendar_public_result.
 */
calendar_public_result calendar_public_build_availability_json(
        const char *query,
        size_t query_length,
        string **out_json
);

/**
 * @brief Reserviert den vom Kunden gewählten Termin.
 * @param[in] booking Validierte Buchungsdaten beziehungsweise Zielstruktur.
 * @param[out] out_booking_id Ausgabeparameter für die erzeugte Buchungs-ID.
 * @return Ein Wert aus ::calendar_public_result.
 */
calendar_public_result calendar_public_reserve_booking(
        const booking_request *booking,
        int64_t *out_booking_id
);

/**
 * @brief Liefert die letzte Fehlermeldung der öffentlichen Kalenderlogik.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *calendar_public_last_error(void);

#endif
