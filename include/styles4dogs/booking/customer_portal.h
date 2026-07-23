#ifndef STYLES4DOGS_BOOKING_CUSTOMER_PORTAL_H
#define STYLES4DOGS_BOOKING_CUSTOMER_PORTAL_H

/**
 * @file customer_portal.h
 * @brief Stellt tokenbasierte persönliche Buchungslinks und Stornierungen bereit.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Puffergröße des hexadezimalen Kundenportal-Tokens einschließlich Nullterminator. */
#define CUSTOMER_PORTAL_TOKEN_HEX_SIZE 65
/** @brief Maximale Länge eines persönlichen Buchungslinks einschließlich Nullterminator. */
#define CUSTOMER_PORTAL_URL_SIZE 1024

/**
 * @brief Für das Kundenportal freigegebene Buchungsdaten.
 */
typedef struct customer_portal_booking {
    int64_t id; /**< Eindeutige ID der dargestellten Buchung. */
    char customer_name[256]; /**< Vollständiger Kundenname. */
    char dog_name[256]; /**< Name des Hundes. */
    char service_name[128]; /**< Angezeigter Name der Leistung. */
    char appointment_date[11]; /**< Termindatum im Format YYYY-MM-DD. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    char decision_status[32]; /**< Aktueller Entscheidungsstatus für die Kundenansicht. */
    char rejection_reason[512]; /**< Optionaler Ablehnungsgrund. */
    char cancellation_reason[1024]; /**< Optionaler vom Kunden angegebener Absagegrund. */
    int cancellation_notice_minutes; /**< Konfigurierte Frist für kurzfristige Absagen. */
    bool late_cancellation; /**< Kennzeichnet eine Absage innerhalb der Frist. */
    bool can_cancel; /**< Gibt an, ob die Onlineabsage aktuell möglich ist. */
} customer_portal_booking; /**< Typalias für ::customer_portal_booking. */

/**
 * @brief Ergebnis eines Zugriffs oder einer Aktion im Kundenportal.
 */
typedef enum customer_portal_result {
    CUSTOMER_PORTAL_ERROR = -1, /**< Interner Fehler. */
    CUSTOMER_PORTAL_OK = 0, /**< Operation erfolgreich. */
    CUSTOMER_PORTAL_NOT_FOUND = 1, /**< Angeforderter Datensatz wurde nicht gefunden. */
    CUSTOMER_PORTAL_NOT_CANCELLABLE = 2, /**< Buchung kann aufgrund ihres Status nicht storniert werden. */
    CUSTOMER_PORTAL_TOO_LATE = 3 /**< Der Termin ist bereits beendet. */
} customer_portal_result; /**< Typalias für ::customer_portal_result. */

/**
 * @brief Erzeugt den persönlichen tokenbasierten Buchungslink.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[out] out_url Ausgabepuffer für den persönlichen Link.
 * @param[out] out_url_size Größe von @p out_url in Bytes.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int customer_portal_build_url(
        int64_t booking_id,
        char *out_url,
        size_t out_url_size
);

/**
 * @brief Prüft den Token eines persönlichen Buchungslinks.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] token Hexadezimaler Zugriffstoken.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool customer_portal_token_is_valid(
        int64_t booking_id,
        const char *token
);

/**
 * @brief Lädt die für das Kundenportal freigegebenen Buchungsdaten.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] token Hexadezimaler Zugriffstoken.
 * @param[out] out_booking Ausgabeparameter für freigegebene Kundenportaldaten.
 * @return Ein Wert aus ::customer_portal_result.
 */
customer_portal_result customer_portal_load_booking(
        int64_t booking_id,
        const char *token,
        customer_portal_booking *out_booking
);

/**
 * @brief Zieht eine offene Anfrage zurück oder storniert einen bestätigten Termin.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] token Hexadezimaler Zugriffstoken.
 * @param[in] cancelled_at_utc Stornierungszeitpunkt als UTC-Zeitstempel.
 * @param[in] cancellation_reason Optionaler, vom Kunden angegebener Absagegrund.
 * @return Ein Wert aus ::customer_portal_result.
 */
customer_portal_result customer_portal_cancel_booking(
        int64_t booking_id,
        const char *token,
        const char *cancelled_at_utc,
        const char *cancellation_reason
);

/**
 * @brief Liefert die letzte Fehlermeldung des Kundenportals.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *customer_portal_last_error(void);

#endif
