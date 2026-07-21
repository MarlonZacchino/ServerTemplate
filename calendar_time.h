#ifndef STYLES4DOGS_CALENDAR_TIME_H
#define STYLES4DOGS_CALENDAR_TIME_H

#include <stdbool.h>

/* Validiert ein kalendarisch korrektes Datum im Format YYYY-MM-DD. */
bool calendar_date_is_valid(const char *date);

/*
 * Liefert die Anzahl Tage von from_date bis to_date.
 * Beispiel: 2026-08-01 -> 2026-08-03 ergibt 2.
 */
int calendar_date_days_between(
        const char *from_date,
        const char *to_date,
        int *out_days
);

/* ISO-Wochentag: Montag = 1, Sonntag = 7. */
int calendar_date_iso_weekday(const char *date, int *out_weekday);

/* Validiert UTC-Zeitstempel im festen Format YYYY-MM-DDTHH:MM:SSZ. */
bool calendar_utc_timestamp_is_valid(const char *timestamp);

#endif
