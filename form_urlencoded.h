#ifndef SERVER_FORM_URLENCODED_H
#define SERVER_FORM_URLENCODED_H

#include <stddef.h>

#include "http_lib.h"

typedef enum form_value_result {
    FORM_VALUE_OK = 0,
    FORM_VALUE_NOT_FOUND,
    FORM_VALUE_INVALID,
    FORM_VALUE_TOO_LARGE,
    FORM_VALUE_DUPLICATE
} form_value_result;

/*
 * Liest ein Feld aus beliebigen application/x-www-form-urlencoded-Daten,
 * zum Beispiel aus einem Query-String oder einem Request-Body.
 */
form_value_result form_urlencoded_get_from_data(
        const char *data,
        size_t data_length,
        const char *field_name,
        char *out,
        size_t out_size
);

/*
 * Liest ein Feld aus einem application/x-www-form-urlencoded-Request-Body.
 *
 * Die Ausgabe ist immer nullterminiert, sofern out_size > 0 ist.
 * Doppelte Felder, ungültige Prozent-Codierung und eingebettete Nullbytes
 * werden abgelehnt.
 */
form_value_result form_urlencoded_get(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
);

#endif
