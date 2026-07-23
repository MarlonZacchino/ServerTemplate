#ifndef STYLES4DOGS_SECURITY_RATE_LIMIT_H
#define STYLES4DOGS_SECURITY_RATE_LIMIT_H

/**
 * @file rate_limit.h
 * @brief Begrenzt öffentliche und administrative Anfragen im Arbeitsspeicher.
 */

#include <stdbool.h>

/*
 * In-memory rate limiting for the single-process HTTP server.
 *
 * No client addresses are persisted. State is intentionally reset when the
 * process restarts. All functions are safe for the current single-threaded
 * server loop; a future multi-threaded server must add synchronization.
 */

/**
 * @brief Prüft und zählt einen öffentlichen Buchungsversuch.
 * @param[in] client_ip Normalisierte Client-IP-Adresse.
 * @param[in] retry_after_seconds Ausgabeparameter für die empfohlene Wartezeit.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool rate_limit_allow_booking(
        const char *client_ip,
        unsigned int *retry_after_seconds
);

/**
 * @brief Prüft und zählt eine öffentliche PLZ-Abfrage.
 * @param[in] client_ip Normalisierte Client-IP-Adresse.
 * @param[in] retry_after_seconds Ausgabeparameter für die empfohlene Wartezeit.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool rate_limit_allow_postal_lookup(
        const char *client_ip,
        unsigned int *retry_after_seconds
);

/**
 * @brief Prüft, ob eine Clientadresse nach Fehlversuchen gesperrt ist.
 * @param[in] client_ip Normalisierte Client-IP-Adresse.
 * @param[in] retry_after_seconds Ausgabeparameter für die empfohlene Wartezeit.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool rate_limit_admin_is_blocked(
        const char *client_ip,
        unsigned int *retry_after_seconds
);

/**
 * @brief Registriert einen fehlgeschlagenen Admin-Login.
 * @param[in] client_ip Normalisierte Client-IP-Adresse.
 */
void rate_limit_record_admin_failure(const char *client_ip);

/**
 * @brief Löscht die Fehlversuchshistorie einer Clientadresse.
 * @param[in] client_ip Normalisierte Client-IP-Adresse.
 */
void rate_limit_clear_admin_failures(const char *client_ip);

/**
 * @brief Leert den gesamten flüchtigen Rate-Limit-Zustand.
 */
void rate_limit_reset(void);

#endif
