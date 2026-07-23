#ifndef STYLES4DOGS_HTTP_FORM_URLENCODED_H
#define STYLES4DOGS_HTTP_FORM_URLENCODED_H

/**
 * @file form_urlencoded.h
 * @brief Dekodiert application/x-www-form-urlencoded-Daten sicher.
 */

#include <stddef.h>

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Ergebnis beim Lesen eines URL-kodierten Formularfeldes.
 */
typedef enum form_value_result {
    FORM_VALUE_OK = 0, /**< Operation erfolgreich. */
    FORM_VALUE_NOT_FOUND, /**< Formularfeld ist nicht vorhanden. */
    FORM_VALUE_INVALID, /**< Formularfeld ist ungültig kodiert. */
    FORM_VALUE_TOO_LARGE, /**< Formularwert überschreitet den Zielpuffer. */
    FORM_VALUE_DUPLICATE /**< Formularfeld wurde mehrfach übermittelt. */
} form_value_result; /**< Typalias für ::form_value_result. */

/**
 * @brief Dekodiert ein einzelnes Feld aus URL-kodierten Daten.
 * @param[in] data URL-kodierte Eingabedaten.
 * @param[in] data_length Länge von @p data in Bytes.
 * @param[in] field_name Name des gesuchten Formularfelds.
 * @param[in] out Ausgabepuffer für den dekodierten Wert.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @return Ein Wert aus ::form_value_result.
 */
form_value_result form_urlencoded_get_from_data(
        const char *data,
        size_t data_length,
        const char *field_name,
        char *out,
        size_t out_size
);

/**
 * @brief Dekodiert ein einzelnes Feld aus dem Body eines HTTP-Requests.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @param[in] field_name Name des gesuchten Formularfelds.
 * @param[in] out Ausgabepuffer für den dekodierten Wert.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @return Ein Wert aus ::form_value_result.
 */
form_value_result form_urlencoded_get(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
);

#endif
