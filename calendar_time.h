#ifndef STYLES4DOGS_CALENDAR_TIME_H
#define STYLES4DOGS_CALENDAR_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct calendar_clock_snapshot {
    char local_date[11];
    int local_minute;
    char now_utc[21];
} calendar_clock_snapshot;

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

/* Addiert eine begrenzte Anzahl Tage und schreibt YYYY-MM-DD. */
int calendar_date_add_days(
        const char *date,
        int days,
        char out_date[11]
);

/* ISO-Wochentag: Montag = 1, Sonntag = 7. */
int calendar_date_iso_weekday(const char *date, int *out_weekday);

/*
 * Formatiert YYYY-MM-DD für die Oberfläche als DD.MM.YYYY.
 * Mit include_weekday entsteht zum Beispiel "21.07.2026 - Dienstag".
 */
int calendar_date_format_de(
        const char *date,
        bool include_weekday,
        char *out_text,
        size_t out_size
);

/* Validiert UTC-Zeitstempel im festen Format YYYY-MM-DDTHH:MM:SSZ. */
bool calendar_utc_timestamp_is_valid(const char *timestamp);

/* Wandelt einen validen UTC-Zeitstempel in einen Unix-Zeitpunkt um. */
int calendar_utc_timestamp_to_epoch(
        const char *timestamp,
        time_t *out_epoch
);

/* Addiert Minuten zu einem validen UTC-Zeitstempel. */
int calendar_utc_add_minutes(
        const char *timestamp,
        int minutes,
        char out_timestamp[21]
);

/* Wandelt einen Unix-Zeitpunkt in lokales Datum und Minuten seit Mitternacht um. */
int calendar_epoch_to_local(
        const char *timezone,
        time_t epoch,
        char out_date[11],
        int *out_minute
);

/* Liest die aktuelle Salonzeit und die aktuelle UTC-Zeit. */
int calendar_clock_now(
        const char *timezone,
        calendar_clock_snapshot *snapshot
);

/* Konvertiert HH:MM in Minuten seit Mitternacht. */
int calendar_time_parse_hhmm(const char *text, int *out_minute);

/* Formatiert Minuten seit Mitternacht als HH:MM. */
int calendar_time_format_hhmm(int minute, char out_text[6]);

/* Wandelt ein lokales Salon-Datum samt Minute in einen Unix-Zeitpunkt um. */
int calendar_local_datetime_to_epoch(
        const char *timezone,
        const char *date,
        int minute,
        time_t *out_epoch
);

#endif
