#ifndef STYLES4DOGS_ADMIN_ADMIN_APPOINTMENTS_H
#define STYLES4DOGS_ADMIN_ADMIN_APPOINTMENTS_H

/**
 * @file admin_appointments.h
 * @brief Erzeugt die geschützte Terminansicht für den Salonbetrieb.
 */

#include <stddef.h>

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Baut die geschützte Tages-, Wochen- oder Monatsansicht des Terminkalenders.
 * @param[in] csrf_token Gültiger CSRF-Token für schreibende Formulare.
 * @param[in] query Query-String ohne führendes Fragezeichen; darf NULL sein.
 * @param[in] query_length Länge von @p query in Bytes.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *admin_appointments_build_page(
        const char *csrf_token,
        const char *query,
        size_t query_length
);

/**
 * @brief Liefert die letzte Fehlermeldung der Admin-Terminansicht.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *admin_appointments_last_error(void);

#endif
