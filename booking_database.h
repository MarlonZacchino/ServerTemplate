#ifndef SERVER_BOOKING_DATABASE_H
#define SERVER_BOOKING_DATABASE_H

#include <stdbool.h>
#include <stdint.h>

#include "booking.h"

typedef struct booking_record {
    int64_t id;
    const char *created_at;
    const char *status;
    const char *name;
    const char *contact;
    const char *dog_name;
    const char *dog_size;
    const char *service;
    const char *preferred_date;
    const char *message;
    bool legacy;
} booking_record;

typedef void (*booking_record_callback)(
        const booking_record *record,
        void *context
);

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
int booking_database_for_each_newest(
        booking_record_callback callback,
        void *context
);

/* Letzte interne SQLite-/Migrationsfehlermeldung für das Server-Log. */
const char *booking_database_last_error(void);

#endif
