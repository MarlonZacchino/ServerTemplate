#ifndef STYLES4DOGS_ADMIN_ADMIN_DASHBOARD_H
#define STYLES4DOGS_ADMIN_ADMIN_DASHBOARD_H

/**
 * @file admin_dashboard.h
 * @brief Erzeugt die zentrale Admin-Übersicht.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Baut die zentrale Admin-Übersicht.
 * @param[in] csrf_token Gültiger CSRF-Token für den Logout.
 * @param[in] username Benutzername des angemeldeten Admins.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *admin_dashboard_build_page(const char *csrf_token, const char *username);
/**
 * @brief Liefert die letzte Fehlermeldung des Admin-Dashboards.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *admin_dashboard_last_error(void);

#endif
