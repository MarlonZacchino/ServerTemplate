#ifndef SERVER_AUTH_H
#define SERVER_AUTH_H

#include <stdbool.h>

#include "http_lib.h"

bool request_has_valid_admin_auth(const string *request);

bool admin_auth_exists(void);

int create_admin_auth(
        const char *username,
        const char *password
);

#endif