
#ifndef STYLES4DOGS_HTTP_PROCESS_H
#define STYLES4DOGS_HTTP_PROCESS_H

/**
 * @file process.h
 * @brief Deklariert den zentralen HTTP-Request-Router.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Verarbeitet einen vollständigen HTTP-Request und erzeugt die HTTP-Response.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *process(string *request);

#endif