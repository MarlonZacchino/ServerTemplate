#ifndef STYLES4DOGS_SECURITY_ADMIN_SESSION_H
#define STYLES4DOGS_SECURITY_ADMIN_SESSION_H

/**
 * @file admin_session.h
 * @brief Verwaltet sitzungsbasierte Admin-Anmeldungen und CSRF-Schutz.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "styles4dogs/http/http_lib.h"

/** @brief Länge eines hexadezimalen Sitzungstokens einschließlich Nullterminator. */
#define ADMIN_SESSION_TOKEN_HEX_SIZE 65
/** @brief Länge eines hexadezimalen CSRF-Tokens einschließlich Nullterminator. */
#define ADMIN_SESSION_CSRF_HEX_SIZE 65
/** @brief Maximale Länge eines Admin-Benutzernamens einschließlich Nullterminator. */
#define ADMIN_SESSION_USERNAME_SIZE 128
/** @brief Maximale Länge eines Set-Cookie-Headers einschließlich Nullterminator. */
#define ADMIN_SESSION_COOKIE_HEADER_SIZE 512

/**
 * @brief Ergebnis eines Loginversuchs.
 */
typedef enum admin_session_login_result {
    ADMIN_SESSION_LOGIN_ERROR = -1, /**< Interner Fehler. */
    ADMIN_SESSION_LOGIN_OK = 0, /**< Anmeldung erfolgreich. */
    ADMIN_SESSION_LOGIN_INVALID = 1, /**< Zugangsdaten sind ungültig. */
    ADMIN_SESSION_LOGIN_RATE_LIMITED = 2 /**< Zu viele fehlgeschlagene Versuche. */
} admin_session_login_result; /**< Typalias für ::admin_session_login_result. */

/**
 * @brief Authentifizierte, serverseitig gespeicherte Admin-Sitzung.
 */
typedef struct admin_session {
    int64_t admin_id; /**< Eindeutige Admin-ID. */
    char username[ADMIN_SESSION_USERNAME_SIZE]; /**< Benutzername des angemeldeten Admins. */
    char token[ADMIN_SESSION_TOKEN_HEX_SIZE]; /**< Rohes Token nur für die aktuelle Request-Verarbeitung. */
    char csrf_token[ADMIN_SESSION_CSRF_HEX_SIZE]; /**< Aus dem Sitzungstoken abgeleiteter CSRF-Token. */
} admin_session; /**< Typalias für ::admin_session. */

/**
 * @brief Importiert vorhandene Basic-Auth-Zugangsdaten in die Admin-Benutzertabelle.
 * @retval 0 Bei Erfolg oder wenn kein Altzugang vorhanden ist.
 * @retval -1 Bei einem internen Fehler.
 */
int admin_session_initialize(void);

/**
 * @brief Prüft Zugangsdaten, rotiert die Sitzung und erzeugt ein Cookie.
 * @param[in] request Vollständiger HTTP-Request für Metadaten und HTTPS-Erkennung.
 * @param[in] client_ip Vom Server bereits vertrauenswürdig normalisierte Client-IP-Adresse.
 * @param[in] username Eingegebener Benutzername.
 * @param[in] password Eingegebenes Passwort.
 * @param[out] out_cookie_header Vollständiger Set-Cookie-Header.
 * @param[in] out_cookie_header_size Größe des Headerpuffers.
 * @return Ein Wert aus ::admin_session_login_result.
 */
admin_session_login_result admin_session_login(
        const string *request,
        const char *client_ip,
        const char *username,
        const char *password,
        char *out_cookie_header,
        size_t out_cookie_header_size
);

/**
 * @brief Authentifiziert einen Request anhand des Sitzungscookies.
 * @param[in] request Vollständiger HTTP-Request.
 * @param[out] out_session Geladene Sitzungsdaten.
 * @retval true Bei gültiger Sitzung.
 * @retval false Bei fehlender, ungültiger oder abgelaufener Sitzung.
 */
bool admin_session_authenticate(
        const string *request,
        admin_session *out_session
);

/**
 * @brief Invalidiert die Sitzung aus dem übergebenen Request.
 * @param[in] request Vollständiger HTTP-Request.
 * @retval 0 Bei Erfolg oder wenn keine Sitzung vorhanden war.
 * @retval -1 Bei einem Datenbankfehler.
 */
int admin_session_logout(const string *request);

/**
 * @brief Validiert einen Formular-CSRF-Token gegen die Sitzung.
 * @param[in] session Authentifizierte Sitzung.
 * @param[in] received_token Vom Formular empfangener Token.
 * @retval true Bei konstanter Übereinstimmung.
 * @retval false Bei ungültigen Eingaben oder Abweichung.
 */
bool admin_session_validate_csrf(
        const admin_session *session,
        const char *received_token
);

/**
 * @brief Erzeugt einen Cookie-Header, der das Sitzungscookie löscht.
 * @param[in] request Vollständiger HTTP-Request zur HTTPS-Erkennung.
 * @param[out] out_cookie_header Vollständiger Set-Cookie-Header.
 * @param[in] out_cookie_header_size Größe des Ausgabepuffers.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei ungültigem Ausgabepuffer.
 */
int admin_session_build_clear_cookie(
        const string *request,
        char *out_cookie_header,
        size_t out_cookie_header_size
);

/**
 * @brief Entfernt abgelaufene Sitzungen und veraltete Loginversuche.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem Datenbankfehler.
 */
int admin_session_cleanup(void);

/**
 * @brief Liefert die letzte modulinterne Fehlermeldung.
 * @return Modulverwalteter Fehlertext.
 */
const char *admin_session_last_error(void);

#endif
