#ifndef STYLES4DOGS_ADMIN_APPOINTMENTS_H
#define STYLES4DOGS_ADMIN_APPOINTMENTS_H

#include <stddef.h>

#include "http_lib.h"

/* Baut die geschützte Tages- oder Wochenansicht des Terminkalenders. */
string *admin_appointments_build_page(
        const char *csrf_token,
        const char *query,
        size_t query_length
);

const char *admin_appointments_last_error(void);

#endif
