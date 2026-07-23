#ifndef STYLES4DOGS_CALENDAR_CALENDAR_TIME_H
#define STYLES4DOGS_CALENDAR_CALENDAR_TIME_H

/**
 * @file calendar_time.h
 * @brief Enthält Datums-, Uhrzeit- und Zeitzonenfunktionen des Kalenders.
 */

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * @brief Gemeinsam ermittelter lokaler und UTC-Zeitstand.
 */
typedef struct calendar_clock_snapshot {
    char local_date[11]; /**< Lokales Datum im Format YYYY-MM-DD. */
    int local_minute; /**< Lokale Minute seit Mitternacht. */
    char now_utc[21]; /**< Aktueller UTC-Zeitstempel. */
} calendar_clock_snapshot; /**< Typalias für ::calendar_clock_snapshot. */

/**
 * @brief Prüft ein Datum im Format YYYY-MM-DD auf kalendarische Gültigkeit.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool calendar_date_is_valid(const char *date);

/**
 * @brief Berechnet die Anzahl ganzer Tage zwischen zwei Daten.
 * @param[in] from_date Erstes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[in] to_date Letztes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[out] out_days Ausgabeparameter für die Tagesdifferenz.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_date_days_between(
        const char *from_date,
        const char *to_date,
        int *out_days
);

/**
 * @brief Addiert eine begrenzte Zahl von Tagen zu einem Datum.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[in] days Zu addierende Tageszahl.
 * @param[out] out_date Ausgabepuffer für ein Datum im Format YYYY-MM-DD.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_date_add_days(
        const char *date,
        int days,
        char out_date[11]
);

/**
 * @brief Ermittelt den ISO-Wochentag eines Datums.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[out] out_weekday Ausgabeparameter für den ISO-Wochentag.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_date_iso_weekday(const char *date, int *out_weekday);

/**
 * @brief Formatiert ein ISO-Datum für die deutschsprachige Oberfläche.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[in] include_weekday Ergänzt bei true den ausgeschriebenen Wochentag.
 * @param[out] out_text Ausgabepuffer für den formatierten Text.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_date_format_de(
        const char *date,
        bool include_weekday,
        char *out_text,
        size_t out_size
);

/**
 * @brief Prüft einen UTC-Zeitstempel im festen ISO-Format.
 * @param[in] timestamp UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool calendar_utc_timestamp_is_valid(const char *timestamp);

/**
 * @brief Konvertiert einen UTC-Zeitstempel in einen Unix-Zeitpunkt.
 * @param[in] timestamp UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @param[out] out_epoch Ausgabeparameter für den Unix-Zeitpunkt.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_utc_timestamp_to_epoch(
        const char *timestamp,
        time_t *out_epoch
);

/**
 * @brief Addiert Minuten zu einem UTC-Zeitstempel.
 * @param[in] timestamp UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @param[in] minutes Zu addierende Minuten.
 * @param[out] out_timestamp Ausgabepuffer für den UTC-Zeitstempel.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_utc_add_minutes(
        const char *timestamp,
        int minutes,
        char out_timestamp[21]
);

/**
 * @brief Konvertiert einen Unix-Zeitpunkt in lokale Salonzeit.
 * @param[in] timezone IANA-Zeitzonenname, beispielsweise Europe/Berlin.
 * @param[in] epoch Unix-Zeitpunkt.
 * @param[out] out_date Ausgabepuffer für ein Datum im Format YYYY-MM-DD.
 * @param[out] out_minute Ausgabeparameter für Minuten seit Mitternacht.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_epoch_to_local(
        const char *timezone,
        time_t epoch,
        char out_date[11],
        int *out_minute
);

/**
 * @brief Liest die aktuelle UTC- und Salonzeit.
 * @param[in] timezone IANA-Zeitzonenname, beispielsweise Europe/Berlin.
 * @param[out] snapshot Ausgabeparameter für die aktuelle Uhrzeitaufnahme.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_clock_now(
        const char *timezone,
        calendar_clock_snapshot *snapshot
);

/**
 * @brief Konvertiert HH:MM in Minuten seit Mitternacht.
 * @param[in] text Zu parsende Uhrzeit im Format HH:MM.
 * @param[out] out_minute Ausgabeparameter für Minuten seit Mitternacht.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_time_parse_hhmm(const char *text, int *out_minute);

/**
 * @brief Formatiert Minuten seit Mitternacht als HH:MM.
 * @param[in] minute Minute seit Mitternacht.
 * @param[out] out_text Ausgabepuffer für den formatierten Text.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_time_format_hhmm(int minute, char out_text[6]);

/**
 * @brief Konvertiert ein lokales Salondatum mit Uhrzeit in einen Unix-Zeitpunkt.
 * @param[in] timezone IANA-Zeitzonenname, beispielsweise Europe/Berlin.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[in] minute Minute seit Mitternacht.
 * @param[out] out_epoch Ausgabeparameter für den Unix-Zeitpunkt.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_local_datetime_to_epoch(
        const char *timezone,
        const char *date,
        int minute,
        time_t *out_epoch
);

#endif
