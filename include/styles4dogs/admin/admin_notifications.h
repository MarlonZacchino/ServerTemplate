#ifndef STYLES4DOGS_ADMIN_ADMIN_NOTIFICATIONS_H
#define STYLES4DOGS_ADMIN_ADMIN_NOTIFICATIONS_H

/**
 * @file admin_notifications.h
 * @brief Verarbeitet die Admin-Oberfläche für SMTP, Queue und Nachrichtenvorlagen.
 */

#include "styles4dogs/http/http_lib.h"

/**
 * @brief Ergebnis einer schreibenden E-Mail-Verwaltungsoperation.
 */
typedef enum admin_notifications_result {
    ADMIN_NOTIFICATIONS_OK = 0, /**< Operation erfolgreich. */
    ADMIN_NOTIFICATIONS_BAD_REQUEST = 1, /**< Ungültige oder unvollständige Eingabe. */
    ADMIN_NOTIFICATIONS_ERROR = -1 /**< Interner Fehler. */
} admin_notifications_result; /**< Typalias für ::admin_notifications_result. */

/**
 * @brief Baut die geschützte Verwaltungsseite für E-Mails und Vorlagen.
 * @param[in] csrf_token Gültiger CSRF-Token für schreibende Formulare.
 * @param[in] notice_code Optionaler Code für eine Erfolgsmeldung; darf NULL sein.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *admin_notifications_build_page(const char *csrf_token, const char *notice_code);
/**
 * @brief Speichert die SMTP-Verbindung und Admin-Benachrichtigungseinstellungen.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_update_smtp(const string *request);
/**
 * @brief Aktiviert oder pausiert den automatischen E-Mail-Versand.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_toggle_delivery(const string *request);
/**
 * @brief Trennt das gespeicherte SMTP-Konto.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_disconnect_smtp(const string *request);
/**
 * @brief Reiht eine Test-E-Mail in die Versandqueue ein.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_enqueue_test(const string *request);
/**
 * @brief Speichert eine bearbeitete Nachrichtenvorlage.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_update_template(const string *request);
/**
 * @brief Setzt eine Nachrichtenvorlage auf den Standard zurück.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_reset_template(const string *request);
/**
 * @brief Stellt fehlgeschlagene Nachrichten erneut zur Zustellung bereit.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_retry_failed(const string *request);
/**
 * @brief Löscht erfolgreich versendete Queue-Einträge.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_clear_sent(const string *request);
/**
 * @brief Löscht fehlgeschlagene Queue-Einträge.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_clear_failed(const string *request);
/**
 * @brief Löscht die abgeschlossene Versandhistorie.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::admin_notifications_result.
 */
admin_notifications_result admin_notifications_clear_completed(const string *request);
/**
 * @brief Liefert die letzte Fehlermeldung der E-Mail-Verwaltung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *admin_notifications_last_error(void);

#endif
