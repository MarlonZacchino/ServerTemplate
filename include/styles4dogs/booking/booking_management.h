#ifndef STYLES4DOGS_BOOKING_BOOKING_MANAGEMENT_H
#define STYLES4DOGS_BOOKING_BOOKING_MANAGEMENT_H

/**
 * @file booking_management.h
 * @brief Verwaltet administrative Buchungsänderungen, Historien und Kundenzuordnungen.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "styles4dogs/http/http_lib.h"

/** @brief Maximale Länge eines internen Akteurs einschließlich Nullterminator. */
#define BOOKING_ACTOR_SIZE 128
/** @brief Maximale Länge einer internen Notiz einschließlich Nullterminator. */
#define BOOKING_INTERNAL_NOTE_SIZE 2048
/** @brief Maximale Länge eines Stornierungsgrundes einschließlich Nullterminator. */
#define BOOKING_CANCELLATION_REASON_SIZE 1024
/** @brief Maximale Länge eines Audit-Wertes einschließlich Nullterminator. */
#define BOOKING_EVENT_VALUE_SIZE 1024

/**
 * @brief Vollständiger administrativer Snapshot einer Buchung.
 */
typedef struct booking_management_record {
    int64_t id; /**< Eindeutige Buchungs-ID. */
    int64_t customer_id; /**< Zugeordnete Kunden-ID oder 0. */
    int64_t dog_id; /**< Zugeordnete Hunde-ID oder 0. */
    char first_name[128]; /**< Vorname des Kunden. */
    char last_name[128]; /**< Nachname des Kunden. */
    char customer_name[256]; /**< Vollständiger Kundenname. */
    char email[256]; /**< E-Mail-Adresse. */
    char phone_number[64]; /**< Telefonnummer. */
    char phone_kind[32]; /**< Art der Telefonnummer. */
    char contact_channel[32]; /**< Primärer Kontaktkanal. */
    char contact_preference[32]; /**< Bevorzugter Kontaktweg. */
    char street_address[256]; /**< Straße und Hausnummer. */
    char postal_code[16]; /**< Postleitzahl. */
    char city[128]; /**< Wohnort. */
    char dog_name[128]; /**< Hundename. */
    char dog_breed[128]; /**< Optionale Hunderasse. */
    char dog_size[32]; /**< Hundegröße. */
    char service_code[64]; /**< Stabiler Leistungscode. */
    char service_name[128]; /**< Angezeigter Leistungsname. */
    char appointment_date[11]; /**< Termindatum im Format YYYY-MM-DD. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    int blocked_until_minute; /**< Blockierende Endzeit einschließlich Puffer. */
    char message[2048]; /**< Kundennachricht. */
    char admin_note[BOOKING_INTERNAL_NOTE_SIZE]; /**< Interne Adminnotiz. */
    char status[32]; /**< Sichtbarer Buchungsstatus. */
    char decision_status[32]; /**< Technischer Buchungsstatus. */
    char cancelled_at[21]; /**< Stornierungszeitpunkt in UTC. */
    char cancellation_reason[BOOKING_CANCELLATION_REASON_SIZE]; /**< Optionaler Stornierungsgrund. */
    char cancellation_actor[32]; /**< Auslöser der Stornierung. */
    bool late_cancellation; /**< Kennzeichnet eine kurzfristige Absage. */
    char dog_internal_note[BOOKING_INTERNAL_NOTE_SIZE]; /**< Interne Notiz zum Hund. */
} booking_management_record; /**< Typalias für ::booking_management_record. */

/**
 * @brief Eingabedaten für eine administrative Buchungsänderung.
 */
typedef struct booking_management_update {
    int64_t booking_id; /**< Zu ändernde Buchungs-ID. */
    const char *first_name; /**< Neuer Vorname. */
    const char *last_name; /**< Neuer Nachname. */
    const char *email; /**< Neue E-Mail-Adresse. */
    const char *phone_number; /**< Neue Telefonnummer. */
    const char *phone_kind; /**< Neue Art der Telefonnummer. */
    const char *contact_channel; /**< Neuer primärer Kontaktkanal. */
    const char *contact_preference; /**< Neuer bevorzugter Kontaktweg. */
    const char *street_address; /**< Neue Straße und Hausnummer. */
    const char *postal_code; /**< Neue Postleitzahl. */
    const char *city; /**< Neuer Wohnort. */
    const char *dog_name; /**< Neuer Hundename. */
    const char *dog_breed; /**< Neue optionale Hunderasse. */
    const char *dog_size; /**< Neue Hundegröße. */
    const char *service_code; /**< Neuer Leistungscode. */
    const char *appointment_date; /**< Neues Termindatum. */
    int start_minute; /**< Neue Startzeit. */
    const char *message; /**< Neue Kundennachricht. */
    const char *admin_note; /**< Neue interne Adminnotiz. */
    const char *actor_identifier; /**< Anzeigename oder Kennung des Admins. */
} booking_management_update; /**< Typalias für ::booking_management_update. */

/**
 * @brief Ergebnis einer administrativen Buchungsänderung.
 */
typedef enum booking_management_result {
    BOOKING_MANAGEMENT_ERROR = -1, /**< Interner Fehler. */
    BOOKING_MANAGEMENT_OK = 0, /**< Änderung erfolgreich. */
    BOOKING_MANAGEMENT_NOT_FOUND = 1, /**< Buchung wurde nicht gefunden. */
    BOOKING_MANAGEMENT_INVALID = 2, /**< Eingaben sind ungültig. */
    BOOKING_MANAGEMENT_CONFLICT = 3, /**< Der gewünschte Termin ist nicht verfügbar. */
    BOOKING_MANAGEMENT_NOT_ALLOWED = 4 /**< Änderung ist für den aktuellen Status nicht erlaubt. */
} booking_management_result; /**< Typalias für ::booking_management_result. */

/**
 * @brief Ein verständlich darstellbares Audit-Ereignis.
 */
typedef struct booking_event_record {
    int64_t id; /**< Eindeutige Ereignis-ID. */
    int64_t booking_id; /**< Zugehörige Buchungs-ID. */
    char event_type[64]; /**< Stabiler Ereignistyp. */
    char actor_type[16]; /**< customer, admin oder system. */
    char actor_identifier[BOOKING_ACTOR_SIZE]; /**< Optionale Akteurskennung. */
    char old_value[BOOKING_EVENT_VALUE_SIZE]; /**< Vorheriger fachlicher Wert. */
    char new_value[BOOKING_EVENT_VALUE_SIZE]; /**< Neuer fachlicher Wert. */
    char reason[BOOKING_CANCELLATION_REASON_SIZE]; /**< Optionaler Grund. */
    char created_at[21]; /**< UTC-Zeitstempel. */
} booking_event_record; /**< Typalias für ::booking_event_record. */

/**
 * @brief Callback für Audit-Ereignisse.
 * @param[in] event Aktuelles Audit-Ereignis.
 * @param[in,out] context Vom Aufrufer bereitgestellter Kontext.
 * @return 0 zum Fortsetzen, ungleich 0 zum Abbrechen.
 */
typedef int (*booking_event_callback)(const booking_event_record *event, void *context);

/**
 * @brief Lädt eine Buchung für die administrative Detailansicht.
 * @param[in] booking_id Eindeutige Buchungs-ID.
 * @param[out] out_record Ausgabeparameter für den Buchungssnapshot.
 * @return Ein Wert aus ::booking_management_result.
 */
booking_management_result booking_management_load(
        int64_t booking_id,
        booking_management_record *out_record
);

/**
 * @brief Speichert Buchungsdaten und verschiebt den Termin bei Bedarf transaktionssicher.
 * @param[in] update Validierte Änderungsdaten.
 * @param[out] out_rescheduled true, wenn Datum, Uhrzeit oder Leistung geändert wurden.
 * @return Ein Wert aus ::booking_management_result.
 */
booking_management_result booking_management_update_booking(
        const booking_management_update *update,
        bool *out_rescheduled
);

/**
 * @brief Markiert eine bestätigte Buchung als nicht erschienen.
 * @param[in] booking_id Eindeutige Buchungs-ID.
 * @param[in] note Optionale interne Notiz.
 * @param[in] actor_identifier Kennung des handelnden Admins.
 * @return Ein Wert aus ::booking_management_result.
 */
booking_management_result booking_management_mark_no_show(
        int64_t booking_id,
        const char *note,
        const char *actor_identifier
);

/**
 * @brief Speichert die interne Salonnotiz eines Hundes.
 * @param[in] dog_id Eindeutige Hunde-ID.
 * @param[in] note Neue interne Notiz.
 * @param[in] actor_identifier Kennung des handelnden Admins.
 * @return Ein Wert aus ::booking_management_result.
 */
booking_management_result booking_management_update_dog_note(
        int64_t dog_id,
        const char *note,
        const char *actor_identifier
);

/**
 * @brief Iteriert chronologisch über den Verlauf einer Buchung.
 * @param[in] booking_id Eindeutige Buchungs-ID.
 * @param[in] callback Callback für jedes Ereignis.
 * @param[in,out] context Benutzerdefinierter Kontext.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem Fehler.
 */
int booking_management_for_each_event(
        int64_t booking_id,
        booking_event_callback callback,
        void *context
);

/**
 * @brief Rendert die Kunden- und Hundehistorie einer Buchung als sicheres HTML-Fragment.
 * @param[in] booking_id Eindeutige Buchungs-ID.
 * @return Neu reservierter String oder NULL bei einem Fehler.
 */
string *booking_management_build_history_html(int64_t booking_id);

/**
 * @brief Liefert die letzte Fehlermeldung des Verwaltungsmoduls.
 * @return Modulverwalteter Fehlertext.
 */
const char *booking_management_last_error(void);

#endif
