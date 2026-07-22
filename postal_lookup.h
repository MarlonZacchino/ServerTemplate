#ifndef STYLES4DOGS_POSTAL_LOOKUP_H
#define STYLES4DOGS_POSTAL_LOOKUP_H

#include "http_lib.h"

typedef enum postal_lookup_result {
    POSTAL_LOOKUP_ERROR = -1,
    POSTAL_LOOKUP_OK = 0,
    POSTAL_LOOKUP_INVALID_POSTAL_CODE = 1,
    POSTAL_LOOKUP_UNAVAILABLE = 2
} postal_lookup_result;

/*
 * Lädt die Ortsliste für eine deutsche fünfstellige Postleitzahl.
 * Die Antwort ist ein JSON-Array des fest konfigurierten OpenPLZ-Endpunkts.
 * Erfolgreiche Antworten werden begrenzt im Arbeitsspeicher zwischengespeichert.
 */
postal_lookup_result postal_lookup_fetch(
        const char *postal_code,
        string **out_json
);

void postal_lookup_shutdown(void);
const char *postal_lookup_last_error(void);

#endif
