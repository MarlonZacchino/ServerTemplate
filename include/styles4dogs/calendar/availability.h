#ifndef STYLES4DOGS_CALENDAR_AVAILABILITY_H
#define STYLES4DOGS_CALENDAR_AVAILABILITY_H

/**
 * @file availability.h
 * @brief Berechnet freie Termine und reserviert Slots transaktional.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Maximale Anzahl berechneter Slots pro Verfügbarkeitsabfrage. */
#define AVAILABILITY_MAX_SLOTS 256

/**
 * @brief Eingaben für die Berechnung freier Termine eines Tages.
 */
typedef struct availability_query {
    const char *service_code; /**< Stabiler Code der gewählten Leistung. */
    const char *date; /**< Zu prüfendes Datum im Format YYYY-MM-DD. */
    const char *current_date; /**< Aktuelles lokales Datum im Format YYYY-MM-DD. */
    int current_minute; /**< Aktuelle lokale Minute seit Mitternacht. */
    const char *now_utc; /**< Aktueller UTC-Zeitstempel. */
} availability_query; /**< Typalias für ::availability_query. */

/**
 * @brief Intern berechneter verfügbarer Zeitraum einschließlich Blockiergrenze.
 */
typedef struct availability_slot {
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    int blocked_until_minute; /**< Ende der Belegung einschließlich Pufferzeit in Minuten seit Mitternacht. */
} availability_slot; /**< Typalias für ::availability_slot. */

/**
 * @brief Öffentlich auslieferbarer Startzeitpunkt mit Verfügbarkeitsstatus.
 */
typedef struct availability_public_slot {
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    bool available; /**< Gibt an, ob der Slot aktuell buchbar ist. */
} availability_public_slot; /**< Typalias für ::availability_public_slot. */

/**
 * @brief Vollständige Daten für eine transaktionale Terminreservierung.
 */
typedef struct availability_reservation_request {
    availability_query query; /**< Verfügbarkeitsparameter für Leistung und Datum. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    const char *created_at_utc; /**< Erstellungszeitpunkt im UTC-Format. */
    const char *hold_expires_at_utc; /**< Ablaufzeitpunkt der Reservierung im UTC-Format. */
    const char *customer_name; /**< Vollständiger Kundenname. */
    const char *contact; /**< Primäre Kontaktangabe. */
    const char *contact_channel; /**< Gewählter Kontaktkanal. */
    const char *email; /**< E-Mail-Adresse des Kunden. */
    const char *phone_number; /**< Telefonnummer des Kunden. */
    const char *phone_kind; /**< Art der Telefonnummer. */
    const char *contact_preference; /**< Bevorzugter Kontaktweg. */
    const char *street_address; /**< Straße und Hausnummer. */
    const char *postal_code; /**< Deutsche Postleitzahl. */
    const char *city; /**< Wohnort. */
    const char *dog_name; /**< Name des Hundes. */
    const char *dog_breed; /**< Rasse des Hundes. */
    const char *dog_size; /**< Größenklasse des Hundes. */
    const char *message; /**< Optionale Kundennachricht. */
    bool auto_confirm; /**< Gibt an, ob die Reservierung unmittelbar bestätigt wird. */
} availability_reservation_request; /**< Typalias für ::availability_reservation_request. */

/**
 * @brief Ergebnis einer transaktionalen Terminreservierung.
 */
typedef enum availability_reservation_result {
    AVAILABILITY_RESERVATION_ERROR = -1, /**< Interner Fehler. */
    AVAILABILITY_RESERVATION_OK = 0, /**< Operation erfolgreich. */
    AVAILABILITY_RESERVATION_UNAVAILABLE = 1, /**< Gewählter Termin ist nicht verfügbar. */
    AVAILABILITY_RESERVATION_INVALID = 2, /**< Eingabedaten sind ungültig. */
    AVAILABILITY_RESERVATION_CONTACT_LIMIT = 3 /**< Kontaktbezogenes Buchungslimit wurde erreicht. */
} availability_reservation_result; /**< Typalias für ::availability_reservation_result. */

/**
 * @brief Berechnet aktuell freie Startzeiten für einen Kalendertag.
 * @param[in] query Query-String ohne führendes Fragezeichen; darf NULL sein.
 * @param[in] slots Ausgabepuffer für berechnete Slots.
 * @param[in] slots_capacity Anzahl verfügbarer Elemente in @p slots.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int availability_collect(
        const availability_query *query,
        availability_slot *slots,
        size_t slots_capacity,
        size_t *out_count
);

/**
 * @brief Liefert alle regulären Startzeiten und deren öffentlichen Verfügbarkeitsstatus.
 * @param[in] query Query-String ohne führendes Fragezeichen; darf NULL sein.
 * @param[in] slots Ausgabepuffer für berechnete Slots.
 * @param[in] slots_capacity Anzahl verfügbarer Elemente in @p slots.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int availability_collect_public(
        const availability_query *query,
        availability_public_slot *slots,
        size_t slots_capacity,
        size_t *out_count
);

/**
 * @brief Reserviert einen freien Termin innerhalb einer exklusiven Datenbanktransaktion.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @param[out] out_booking_id Ausgabeparameter für die erzeugte Buchungs-ID.
 * @return Ein Wert aus ::availability_reservation_result.
 */
availability_reservation_result availability_reserve_pending(
        const availability_reservation_request *request,
        int64_t *out_booking_id
);

/**
 * @brief Liefert die letzte Fehlermeldung der Verfügbarkeitsberechnung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *availability_last_error(void);

#endif
