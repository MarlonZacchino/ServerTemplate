#ifndef STYLES4DOGS_BOOKING_BOOKING_DATABASE_H
#define STYLES4DOGS_BOOKING_BOOKING_DATABASE_H

/**
 * @file booking_database.h
 * @brief Kapselt die SQLite-Persistenz für Buchungsanfragen.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "styles4dogs/booking/booking.h"

/**
 * @brief Nicht besitzende Sicht auf einen aus SQLite gelesenen Buchungsdatensatz.
 */
typedef struct booking_record {
    int64_t id; /**< Eindeutige Datenbank-ID. */
    const char *created_at; /**< Erstellungszeitpunkt als UTC-Zeitstempel. */
    const char *status; /**< Sichtbarer, synchronisierter Buchungsstatus. */
    const char *name; /**< Vollständiger Kundenname. */
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
    const char *service; /**< Stabiler Code der gewählten Leistung. */
    const char *preferred_date; /**< Freitext für einen alternativen Terminwunsch. */
    const char *message; /**< Optionale Kundennachricht. */
    const char *appointment_date; /**< Termindatum im Format YYYY-MM-DD. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    const char *decision_status; /**< Technischer Entscheidungsstatus der Buchung. */
    const char *hold_expires_at; /**< Ablaufzeitpunkt einer vorläufigen Reservierung. */
    const char *decision_at; /**< Zeitpunkt der Adminentscheidung. */
    const char *rejection_reason; /**< Optionaler Ablehnungs- oder Stornierungsgrund. */
    const char *service_name_snapshot; /**< Zum Buchungszeitpunkt gespeicherter Leistungsname. */
    int service_duration_minutes_snapshot; /**< Zum Buchungszeitpunkt gespeicherte Leistungsdauer in Minuten. */
    int service_buffer_minutes_snapshot; /**< Zum Buchungszeitpunkt gespeicherte Pufferzeit in Minuten. */
    bool legacy; /**< Kennzeichnet einen importierten Altbestandseintrag. */
} booking_record; /**< Typalias für ::booking_record. */

/**
 * @brief Aggregierte Anzahl der Buchungen je sichtbarem Status.
 */
typedef struct booking_status_counts {
    size_t total; /**< Gesamtanzahl aller Buchungen. */
    size_t new_count; /**< Anzahl neuer Buchungsanfragen. */
    size_t confirmed_count; /**< Anzahl bestätigter Buchungen. */
    size_t rejected_count; /**< Anzahl abgelehnter Buchungen. */
    size_t cancelled_count; /**< Anzahl abgesagter Buchungen. */
    size_t completed_count; /**< Anzahl erledigter Buchungen. */
} booking_status_counts; /**< Typalias für ::booking_status_counts. */

/**
 * @brief Callback für die Iteration über Buchungsdatensätze.
 * @param[in] record Datensatz, dessen Stringzeiger nur während des Aufrufs gültig sind.
 * @param[in,out] context Vom Aufrufer bereitgestellter Kontextzeiger.
 */
typedef void (*booking_record_callback)(
        const booking_record *record,
        void *context
);

/**
 * @brief Ergebnis einer manuellen Buchungsstatusänderung.
 */
typedef enum booking_status_update_result {
    BOOKING_STATUS_UPDATE_ERROR = -1, /**< Interner Fehler. */
    BOOKING_STATUS_UPDATE_OK = 0, /**< Operation erfolgreich. */
    BOOKING_STATUS_UPDATE_NOT_FOUND = 1 /**< Buchung wurde nicht gefunden. */
} booking_status_update_result; /**< Typalias für ::booking_status_update_result. */

/**
 * @brief Öffnet die Datenbank, migriert das Schema und importiert optionale Altdaten.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int booking_database_initialize(void);

/**
 * @brief Schließt die globale Datenbankverbindung.
 */
void booking_database_shutdown(void);

/**
 * @brief Speichert eine validierte Buchungsanfrage.
 * @param[in] booking Validierte Buchungsdaten beziehungsweise Zielstruktur.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int booking_database_insert(const booking_request *booking);

/**
 * @brief Iteriert über gefilterte Buchungen in der Adminsortierung.
 * @param[in] filter Zielstruktur oder anzuwendender Adminfilter.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int booking_database_for_each_filtered(
        const booking_admin_filter *filter,
        booking_record_callback callback,
        void *context
);

/**
 * @brief Iteriert über Termine in einem inklusiven Datumsbereich.
 * @param[in] from_date Erstes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[in] to_date Letztes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int booking_database_for_each_appointment(
        const char *from_date,
        const char *to_date,
        booking_record_callback callback,
        void *context
);

/**
 * @brief Ermittelt die globalen Buchungsanzahlen je Status.
 * @param[out] counts Ausgabe- oder Zielstruktur für aggregierte Zähler.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int booking_database_get_status_counts(booking_status_counts *counts);

/**
 * @brief Ändert den sichtbaren Status einer Buchung.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] status Neuer sichtbarer Buchungsstatus.
 * @return Ein Wert aus ::booking_status_update_result.
 */
booking_status_update_result booking_database_update_status(
        int64_t booking_id,
        const char *status
);

/**
 * @brief Liefert die letzte Datenbank- oder Migrationsfehlermeldung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *booking_database_last_error(void);

#endif
