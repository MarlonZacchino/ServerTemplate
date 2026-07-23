#ifndef STYLES4DOGS_SERVICES_POSTAL_LOOKUP_H
#define STYLES4DOGS_SERVICES_POSTAL_LOOKUP_H

/**
 * @file postal_lookup.h
 * @brief Ruft Ortsnamen für deutsche Postleitzahlen über den konfigurierten Dienst ab.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Ergebnis einer Postleitzahlabfrage.
 */
typedef enum postal_lookup_result {
    POSTAL_LOOKUP_ERROR = -1, /**< Interner Fehler. */
    POSTAL_LOOKUP_OK = 0, /**< Operation erfolgreich. */
    POSTAL_LOOKUP_INVALID_POSTAL_CODE = 1, /**< Postleitzahl ist syntaktisch ungültig. */
    POSTAL_LOOKUP_UNAVAILABLE = 2 /**< Gewählter Termin ist nicht verfügbar. */
} postal_lookup_result; /**< Typalias für ::postal_lookup_result. */

/**
 * @brief Lädt die Ortsliste für eine deutsche Postleitzahl.
 * @param[in] postal_code Deutsche fünfstellige Postleitzahl.
 * @param[out] out_json Ausgabeparameter für einen neu allozierten JSON-String.
 * @return Ein Wert aus ::postal_lookup_result.
 */
postal_lookup_result postal_lookup_fetch(
        const char *postal_code,
        string **out_json
);

/**
 * @brief Gibt den Arbeitsspeicher des PLZ-Caches frei.
 */
void postal_lookup_shutdown(void);
/**
 * @brief Liefert die letzte Fehlermeldung der PLZ-Abfrage.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *postal_lookup_last_error(void);

#endif
