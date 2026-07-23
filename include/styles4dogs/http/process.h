
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

/**
 * @brief Verarbeitet einen HTTP-Request mit bereits vertrauenswürdig aufgelöster Client-IP.
 * @param[in] request Vollständiger HTTP-Request.
 * @param[in] client_ip Vom Socket-/Proxy-Layer normalisierte Client-IP-Adresse.
 * @return Neu allozierte HTTP-Response oder NULL bei einem internen Fehler.
 *
 * Diese Variante verhindert, dass sicherheitsrelevante Funktionen ungeprüfte
 * Forwarding-Header direkt auswerten müssen.
 */
string *process_from_client(string *request, const char *client_ip);

#endif