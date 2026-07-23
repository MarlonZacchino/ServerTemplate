#ifndef STYLES4DOGS_ADMIN_ADMIN_CALENDAR_H
#define STYLES4DOGS_ADMIN_ADMIN_CALENDAR_H

/**
 * @file admin_calendar.h
 * @brief Stellt die Admin-Schnittstelle für Kalender- und Öffnungszeiteneinstellungen bereit.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Ergebnis einer schreibenden Kalenderverwaltungsoperation.
 */
typedef enum admin_calendar_result {
    ADMIN_CALENDAR_ERROR = -1, /**< Interner Fehler. */
    ADMIN_CALENDAR_OK = 0, /**< Operation erfolgreich. */
    ADMIN_CALENDAR_BAD_REQUEST = 1, /**< Ungültige oder unvollständige Eingabe. */
    ADMIN_CALENDAR_NOT_FOUND = 2 /**< Angeforderter Datensatz wurde nicht gefunden. */
} admin_calendar_result; /**< Typalias für ::admin_calendar_result. */

/**
 * @brief Baut die geschützte Seite für Kalender- und Öffnungszeiteneinstellungen.
 * @param[in] csrf_token Gültiger CSRF-Token für schreibende Formulare.
 * @param[in] notice_code Optionaler Code für eine Erfolgsmeldung; darf NULL sein.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *admin_calendar_build_page(
        const char *csrf_token,
        const char *notice_code
);

/**
 * @brief Aktualisiert die allgemeinen Kalendereinstellungen aus einem HTTP-Request.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_update_settings(const string *request);
/**
 * @brief Speichert die zusammengefasste Kalenderkonfiguration aus einem HTTP-Request.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_save_all(const string *request);
/**
 * @brief Aktualisiert die Öffnungszeiten eines Wochentags.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_update_opening_hours(const string *request);
/**
 * @brief Aktualisiert eine vorhandene Leistung.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_update_service(const string *request);
/**
 * @brief Legt eine neue Leistung an.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_add_service(const string *request);
/**
 * @brief Löscht oder archiviert eine Leistung.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_delete_service(const string *request);
/**
 * @brief Legt eine neue Sperrzeit an.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_add_closure(const string *request);
/**
 * @brief Entfernt eine vorhandene Sperrzeit.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_calendar_result.
 */
admin_calendar_result admin_calendar_delete_closure(const string *request);

/**
 * @brief Liefert die letzte Fehlermeldung der Kalenderverwaltung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *admin_calendar_last_error(void);

#endif
