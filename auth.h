//
// Created by Marlon on 17.07.26.
//

#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

#include <stdbool.h>

#include "http_lib.h"

/*
 * Prüft, ob der Request gültige Admin-Zugangsdaten enthält.
 */
bool request_has_valid_admin_auth(const string *request);

#endif