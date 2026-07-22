#ifndef SERVER_BOOKING_DATABASE_H
#define SERVER_BOOKING_DATABASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "booking.h"

typedef struct booking_record {
    int64_t id;
    const char *created_at;
    const char *status;
    const char *name;
    const char *contact;
    const char *contact_channel;
    const char *email;
    const char *phone_number;
    const char *phone_kind;
    const char *contact_preference;
    const char *street_address;
    const char *postal_code;
    const char *city;
    const char *dog_name;
    const char *dog_breed;
    const char *dog_size;
    const char *service;
    const char *preferred_date;
    const char *message;
    const char *appointment_date;
    int start_minute;
    int end_minute;
    const char *decision_status;
    const char *hold_expires_at;
    const char *decision_at;
    const char *rejection_reason;
    const char *service_name_snapshot;
    int service_duration_minutes_snapshot;
    int service_buffer_minutes_snapshot;
    bool legacy;
} booking_record;

typedef struct booking_status_counts {
    size_t total;
    size_t new_count;
    size_t confirmed_count;
    size_t rejected_count;
    size_t cancelled_count;
    size_t completed_count;
} booking_status_counts;

typedef void (*booking_record_callback)(
        const booking_record *record,
        void *context
);

typedef enum booking_status_update_result {
    BOOKING_STATUS_UPDATE_ERROR = -1,
    BOOKING_STATUS_UPDATE_OK = 0,
    BOOKING_STATUS_UPDATE_NOT_FOUND = 1
} booking_status_update_result;

/*
 * Öffnet die SQLite-Datenbank, legt das Schema an und importiert eine
 * vorhandene TSV-Datei einmalig. Muss vor der Request-Verarbeitung
 * aufgerufen werden.
 */
int booking_database_initialize(void);

/*
 * Schließt die globale Datenbankverbindung. Darf auch aufgerufen werden,
 * wenn die Initialisierung fehlgeschlagen ist.
 */
void booking_database_shutdown(void);

/* Speichert eine validierte Buchungsanfrage. */
int booking_database_insert(const booking_request *booking);

/*
 * Ruft callback für alle Buchungen auf, neueste zuerst.
 * Die String-Zeiger im Datensatz sind nur während des Callback-Aufrufs gültig.
 */
int booking_database_for_each_filtered(
        const booking_admin_filter *filter,
        booking_record_callback callback,
        void *context
);

/* Liefert offene und bestätigte Termine in einem inklusiven Datumsbereich. */
int booking_database_for_each_appointment(
        const char *from_date,
        const char *to_date,
        booking_record_callback callback,
        void *context
);

/* Liefert die globalen Anzahlen pro Status für die Adminübersicht. */
int booking_database_get_status_counts(booking_status_counts *counts);

/*
 * Ändert den sichtbaren Status einer vorhandenen Buchung. Erlaubt sind
 * "neu", "bestätigt", "abgelehnt", "abgesagt" und "erledigt".
 * Kalenderentscheidungen pflegen diese Werte weiterhin automatisch.
 */
booking_status_update_result booking_database_update_status(
        int64_t booking_id,
        const char *status
);

/* Letzte interne SQLite-/Migrationsfehlermeldung für das Server-Log. */
const char *booking_database_last_error(void);

#endif
