#ifndef STYLES4DOGS_ADMIN_ADMIN_BOOKING_MANAGEMENT_H
#define STYLES4DOGS_ADMIN_ADMIN_BOOKING_MANAGEMENT_H

/**
 * @file admin_booking_management.h
 * @brief Rendert die administrative Bearbeitungsansicht einzelner Buchungen.
 */

#include <stdint.h>

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Erzeugt die Detail-, Bearbeitungs- und Verlaufsansicht einer Buchung.
 * @param[in] booking_id Eindeutige Buchungs-ID.
 * @param[in] csrf_token Gültiger CSRF-Token der Admin-Sitzung.
 * @param[in] notice_code Optionaler Ergebniscode für eine Statusmeldung.
 * @param[in] admin_username Anzeigename des angemeldeten Admins.
 * @return Neu reservierte HTML-Seite oder NULL bei einem Fehler.
 */
string *admin_booking_management_build_page(
        int64_t booking_id,
        const char *csrf_token,
        const char *notice_code,
        const char *admin_username
);

/**
 * @brief Liefert die letzte Fehlermeldung des Moduls.
 * @return Modulverwalteter Fehlertext.
 */
const char *admin_booking_management_last_error(void);

#endif
