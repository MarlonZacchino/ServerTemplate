#ifndef STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_SETTINGS_H
#define STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_SETTINGS_H

/**
 * @file notification_settings.h
 * @brief Lädt, validiert und speichert verschlüsselte SMTP-Einstellungen.
 */

#include <stdbool.h>
#include <stddef.h>

/** @brief Maximale Länge der SMTP-URL einschließlich Nullterminator. */
#define NOTIFICATION_SMTP_URL_SIZE 512
/** @brief Maximale Länge des SMTP-Benutzernamens einschließlich Nullterminator. */
#define NOTIFICATION_SMTP_USERNAME_SIZE 256
/** @brief Maximale Länge des SMTP-Passworts einschließlich Nullterminator. */
#define NOTIFICATION_SMTP_PASSWORD_SIZE 512
/** @brief Maximale Länge einer SMTP-E-Mail-Adresse einschließlich Nullterminator. */
#define NOTIFICATION_SMTP_ADDRESS_SIZE 256
/** @brief Maximale Länge des sichtbaren Absendernamens einschließlich Nullterminator. */
#define NOTIFICATION_SMTP_NAME_SIZE 256
/** @brief Maximale Länge einer SMTP-Konfigurationsfehlermeldung einschließlich Nullterminator. */
#define NOTIFICATION_SETTINGS_ERROR_SIZE 512

/**
 * @brief Wirksame SMTP- und Admin-Benachrichtigungseinstellungen.
 */
typedef struct notification_smtp_settings {
    bool enabled; /**< Kennzeichnet eine vollständig konfigurierte SMTP-Verbindung. */
    bool delivery_enabled; /**< Aktiviert oder pausiert die automatische Queue-Verarbeitung. */
    bool managed_by_admin; /**< Kennzeichnet eine über die Adminoberfläche gespeicherte Konfiguration. */
    bool notify_admin_on_new_booking; /**< Aktiviert Adminmails bei neuen Buchungen. */
    char url[NOTIFICATION_SMTP_URL_SIZE]; /**< SMTP-URL einschließlich Protokoll und Port. */
    char username[NOTIFICATION_SMTP_USERNAME_SIZE]; /**< SMTP-Benutzername. */
    char password[NOTIFICATION_SMTP_PASSWORD_SIZE]; /**< SMTP-Passwort; im Speicher sensibel behandeln. */
    char from_address[NOTIFICATION_SMTP_ADDRESS_SIZE]; /**< Technische Absenderadresse. */
    char from_name[NOTIFICATION_SMTP_NAME_SIZE]; /**< Sichtbarer Absendername. */
    char admin_email[NOTIFICATION_SMTP_ADDRESS_SIZE]; /**< Empfängeradresse für Admin-Benachrichtigungen. */
} notification_smtp_settings; /**< Typalias für ::notification_smtp_settings. */

/**
 * @brief Lädt die wirksamen SMTP-Einstellungen.
 * @param[out] settings Zu ladende, zu validierende oder zu speichernde Einstellungen.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_settings_load(notification_smtp_settings *settings);
/**
 * @brief Verschlüsselt und speichert administrativ verwaltete SMTP-Einstellungen.
 * @param[in] settings Zu ladende, zu validierende oder zu speichernde Einstellungen.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_settings_save(const notification_smtp_settings *settings);
/**
 * @brief Entfernt die aktive SMTP-Verbindung.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_settings_disconnect(void);
/**
 * @brief Aktiviert oder pausiert den automatischen Versand.
 * @param[in] enabled Gewünschter Aktivierungszustand.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_settings_set_delivery_enabled(bool enabled);
/**
 * @brief Prüft SMTP-Einstellungen auf Vollständigkeit und Konsistenz.
 * @param[in] settings Zu ladende, zu validierende oder zu speichernde Einstellungen.
 * @param[in] require_enabled Erfordert bei true eine vollständig aktive SMTP-Verbindung.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool notification_settings_are_valid(const notification_smtp_settings *settings, bool require_enabled);
/**
 * @brief Liefert die letzte Fehlermeldung der SMTP-Einstellungen.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *notification_settings_last_error(void);

#endif
