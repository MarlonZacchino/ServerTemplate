#ifndef STYLES4DOGS_SECURITY_AUTH_H
#define STYLES4DOGS_SECURITY_AUTH_H

/**
 * @file auth.h
 * @brief Prüft und erzeugt die Zugangsdaten für den geschützten Adminbereich.
 */

#include <stdbool.h>

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Prüft die Admin-Authentifizierung eines HTTP-Requests.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool request_has_valid_admin_auth(const string *request);

/**
 * @brief Prüft, ob bereits Admin-Zugangsdaten eingerichtet wurden.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool admin_auth_exists(void);

/**
 * @brief Erzeugt und speichert neue Admin-Zugangsdaten.
 * @param[in] username Neuer Admin-Benutzername.
 * @param[in] password Neues Admin-Passwort im Klartext; wird nicht dauerhaft im Klartext gespeichert.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int create_admin_auth(
        const char *username,
        const char *password
);

#endif