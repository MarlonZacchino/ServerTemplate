#ifndef STYLES4DOGS_ADMIN_ADMIN_DASHBOARD_H
#define STYLES4DOGS_ADMIN_ADMIN_DASHBOARD_H

/**
 * @file admin_dashboard.h
 * @brief Erzeugt die zentrale Admin-Übersicht.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Baut die zentrale Admin-Übersicht.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *admin_dashboard_build_page(void);
/**
 * @brief Liefert die letzte Fehlermeldung des Admin-Dashboards.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *admin_dashboard_last_error(void);

#endif
