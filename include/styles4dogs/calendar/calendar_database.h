#ifndef STYLES4DOGS_CALENDAR_CALENDAR_DATABASE_H
#define STYLES4DOGS_CALENDAR_CALENDAR_DATABASE_H

/**
 * @file calendar_database.h
 * @brief Kapselt Kalenderkonfiguration, Öffnungszeiten, Sperrzeiten und Terminentscheidungen.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Maximale Länge eines stabilen Leistungscodes einschließlich Nullterminator. */
#define CALENDAR_SERVICE_CODE_SIZE 64
/** @brief Maximale Länge eines Leistungsnamens einschließlich Nullterminator. */
#define CALENDAR_SERVICE_NAME_SIZE 128
/** @brief Maximale Länge eines IANA-Zeitzonennamens einschließlich Nullterminator. */
#define CALENDAR_TIMEZONE_SIZE 64
/** @brief Maximale Länge einer Sperrzeitbezeichnung einschließlich Nullterminator. */
#define CALENDAR_LABEL_SIZE 256
/** @brief Maximale Anzahl regulärer Öffnungszeiträume pro Tag. */
#define CALENDAR_MAX_DAY_PERIODS 32
/** @brief Maximale Anzahl berücksichtigter Sperrzeiten pro Tag. */
#define CALENDAR_MAX_DAY_CLOSURES 64
/** @brief Maximale Anzahl berücksichtigter Buchungen pro Tag. */
#define CALENDAR_MAX_DAY_BOOKINGS 256

/**
 * @brief Globale Einstellungen des Termin- und Benachrichtigungskalenders.
 */
typedef struct calendar_settings {
    char timezone[CALENDAR_TIMEZONE_SIZE]; /**< IANA-Zeitzone des Salons. */
    int min_notice_minutes; /**< Mindestvorlauf für neue Buchungen in Minuten. */
    int booking_horizon_days; /**< Maximaler Buchungshorizont in Tagen. */
    int slot_interval_minutes; /**< Raster der angebotenen Startzeiten in Minuten. */
    int pending_hold_minutes; /**< Haltezeit einer offenen Reservierung in Minuten. */
    int capacity; /**< Reservierte Nutzdatenkapazität ohne Nullterminator. */
    bool auto_confirm_bookings; /**< Aktiviert die automatische Bestätigung freier Termine. */
    bool email_notifications_enabled; /**< Aktiviert kundenbezogene E-Mail-Ereignisse. */
    bool reminder_enabled; /**< Aktiviert automatische Terminerinnerungen. */
    int reminder_lead_minutes; /**< Vorlauf der Erinnerung in Minuten. */
    int cancellation_notice_minutes; /**< Frist für kurzfristige Absagen in Minuten. */
} calendar_settings; /**< Typalias für ::calendar_settings. */

/**
 * @brief Konfigurierbare Salonleistung mit Dauer und Pufferzeit.
 */
typedef struct calendar_service {
    int64_t id; /**< Eindeutige ID der Leistung. */
    char code[CALENDAR_SERVICE_CODE_SIZE]; /**< Stabiler maschinenlesbarer Leistungscode. */
    char name[CALENDAR_SERVICE_NAME_SIZE]; /**< Vollständiger Kundenname. */
    int duration_minutes; /**< Behandlungsdauer in Minuten. */
    int buffer_minutes; /**< Pufferzeit nach der Behandlung in Minuten. */
    bool active; /**< Kennzeichnet eine buchbare Leistung. */
    int sort_order; /**< Anzeigereihenfolge der Leistung. */
} calendar_service; /**< Typalias für ::calendar_service. */

/**
 * @brief Zeitintervall in Minuten seit Mitternacht.
 */
typedef struct calendar_time_range {
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
} calendar_time_range; /**< Typalias für ::calendar_time_range. */

/**
 * @brief Einmalige oder mehrtägige Sperrzeit des Salons.
 */
typedef struct calendar_closure {
    int64_t id; /**< Eindeutige ID der Sperrzeit. */
    char start_date[11]; /**< Erster Tag der Sperrzeit im Format YYYY-MM-DD. */
    char end_date[11]; /**< Letzter Tag der Sperrzeit im Format YYYY-MM-DD. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    char label[CALENDAR_LABEL_SIZE]; /**< Sichtbare Bezeichnung der Sperrzeit. */
} calendar_closure; /**< Typalias für ::calendar_closure. */

/**
 * @brief Callback für die Iteration über Kalenderleistungen.
 * @param[in] service Aktuelle Leistung.
 * @param[in,out] context Vom Aufrufer bereitgestellter Kontextzeiger.
 * @return 0 zum Fortsetzen, ein von null verschiedener Wert zum Abbrechen.
 */
typedef int (*calendar_service_callback)(
        const calendar_service *service,
        void *context
);

/**
 * @brief Callback für die Iteration über Sperrzeiten.
 * @param[in] closure Aktuelle Sperrzeit.
 * @param[in,out] context Vom Aufrufer bereitgestellter Kontextzeiger.
 * @return 0 zum Fortsetzen, ein von null verschiedener Wert zum Abbrechen.
 */
typedef int (*calendar_closure_callback)(
        const calendar_closure *closure,
        void *context
);

/**
 * @brief Daten einer neu einzufügenden Reservierung oder Bestätigung.
 */
typedef struct calendar_pending_booking {
    const char *created_at_utc; /**< Erstellungszeitpunkt im UTC-Format. */
    const char *hold_expires_at_utc; /**< Ablaufzeitpunkt der Reservierung im UTC-Format. */
    const char *customer_name; /**< Vollständiger Kundenname. */
    const char *customer_first_name; /**< Vorname des Kunden. */
    const char *customer_last_name; /**< Nachname des Kunden. */
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
    const char *service_code; /**< Stabiler Code der gewählten Leistung. */
    const char *appointment_date; /**< Termindatum im Format YYYY-MM-DD. */
    int start_minute; /**< Startzeit in Minuten seit Mitternacht. */
    int end_minute; /**< Endzeit in Minuten seit Mitternacht. */
    int blocked_until_minute; /**< Ende der Belegung einschließlich Pufferzeit in Minuten seit Mitternacht. */
    const char *message; /**< Optionale Kundennachricht. */
    bool auto_confirm; /**< Gibt an, ob die Reservierung unmittelbar bestätigt wird. */
} calendar_pending_booking; /**< Typalias für ::calendar_pending_booking. */

/**
 * @brief Initialisiert Kalenderdaten, Schema und Standardwerte.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_initialize(void);
/**
 * @brief Schließt die vom Kalendermodul verwendete Datenbankverbindung.
 */
void calendar_database_shutdown(void);
/**
 * @brief Liefert die letzte Fehlermeldung des Kalendermoduls.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *calendar_database_last_error(void);
/**
 * @brief Liest die aktuelle SQLite-Schemaversion.
 * @param[out] out_version Ausgabeparameter für die Schemaversion.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_schema_version(int *out_version);

/**
 * @brief Lädt die allgemeinen Kalendereinstellungen.
 * @param[out] settings Zu ladende, zu validierende oder zu speichernde Einstellungen.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_get_settings(calendar_settings *settings);
/**
 * @brief Speichert die allgemeinen Kalendereinstellungen.
 * @param[in] settings Zu ladende, zu validierende oder zu speichernde Einstellungen.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_update_settings(const calendar_settings *settings);

/**
 * @brief Lädt eine Leistung anhand ihres stabilen Codes.
 * @param[in] code Stabiler Leistungscode.
 * @param[out] service Zu ladende oder zu speichernde Leistung.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_get_service(
        const char *code,
        calendar_service *service
);
/**
 * @brief Aktualisiert eine bestehende Leistung.
 * @param[in] service Zu ladende oder zu speichernde Leistung.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_update_service(const calendar_service *service);
/**
 * @brief Legt eine neue Kalenderleistung an.
 * @param[in] service Zu ladende oder zu speichernde Leistung.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_add_service(const calendar_service *service);

/**
 * @brief Ergebnis beim Löschen oder Archivieren einer Leistung.
 */
typedef enum calendar_service_delete_result {
    CALENDAR_SERVICE_DELETE_ERROR = -1, /**< Interner Fehler. */
    CALENDAR_SERVICE_DELETE_OK = 0, /**< Operation erfolgreich. */
    CALENDAR_SERVICE_DELETE_ARCHIVED = 1, /**< Leistung wurde wegen vorhandener Referenzen archiviert. */
    CALENDAR_SERVICE_DELETE_NOT_FOUND = 2 /**< Angeforderter Datensatz wurde nicht gefunden. */
} calendar_service_delete_result; /**< Typalias für ::calendar_service_delete_result. */

/**
 * @brief Löscht eine unbenutzte Leistung oder archiviert eine bereits referenzierte Leistung.
 * @param[in] code Stabiler Leistungscode.
 * @return Ein Wert aus ::calendar_service_delete_result.
 */
calendar_service_delete_result calendar_database_delete_service(const char *code);
/**
 * @brief Iteriert über alle Leistungen.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_for_each_service(
        calendar_service_callback callback,
        void *context
);
/**
 * @brief Iteriert über alle aktiven Leistungen.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_for_each_active_service(
        calendar_service_callback callback,
        void *context
);

/**
 * @brief Entfernt sämtliche regulären Öffnungszeiten.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_clear_opening_hours(void);
/**
 * @brief Entfernt die Öffnungszeiten eines Wochentags.
 * @param[in] weekday ISO-Wochentag von 1 (Montag) bis 7 (Sonntag).
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_clear_opening_hours_for_weekday(int weekday);
/**
 * @brief Fügt einem Wochentag einen Öffnungszeitraum hinzu.
 * @param[in] weekday ISO-Wochentag von 1 (Montag) bis 7 (Sonntag).
 * @param[in] start_minute Startzeit in Minuten seit Mitternacht.
 * @param[in] end_minute Endzeit in Minuten seit Mitternacht.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_add_opening_period(
        int weekday,
        int start_minute,
        int end_minute
);
/**
 * @brief Lädt die Öffnungszeiten eines Wochentags.
 * @param[in] weekday ISO-Wochentag von 1 (Montag) bis 7 (Sonntag).
 * @param[in] ranges Ausgabepuffer für Zeiträume.
 * @param[in] ranges_capacity Anzahl verfügbarer Elemente in @p ranges.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_get_opening_periods(
        int weekday,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

/**
 * @brief Entfernt sämtliche Sperrzeiten.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_clear_closures(void);
/**
 * @brief Legt eine Sperrzeit an.
 * @param[in] closure Zu speichernde Sperrzeit.
 * @param[out] out_id Ausgabeparameter für die neu erzeugte ID.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_add_closure(
        const calendar_closure *closure,
        int64_t *out_id
);
/**
 * @brief Löscht eine Sperrzeit anhand ihrer ID.
 * @param[in] closure_id Eindeutige ID der Sperrzeit.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_delete_closure(int64_t closure_id);
/**
 * @brief Iteriert über alle Sperrzeiten.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_for_each_closure(
        calendar_closure_callback callback,
        void *context
);
/**
 * @brief Iteriert über Sperrzeiten, die einen Datumsbereich berühren.
 * @param[in] from_date Erstes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[in] to_date Letztes Datum des inklusiven Bereichs im Format YYYY-MM-DD.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_for_each_closure_in_range(
        const char *from_date,
        const char *to_date,
        calendar_closure_callback callback,
        void *context
);
/**
 * @brief Lädt die blockierten Zeiträume eines Datums.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[in] ranges Ausgabepuffer für Zeiträume.
 * @param[in] ranges_capacity Anzahl verfügbarer Elemente in @p ranges.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_get_closures_for_date(
        const char *date,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

/**
 * @brief Lädt alle Buchungen, die einen Tag zeitlich blockieren.
 * @param[in] date Datum im Format YYYY-MM-DD.
 * @param[in] now_utc Aktueller UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @param[in] ranges Ausgabepuffer für Zeiträume.
 * @param[in] ranges_capacity Anzahl verfügbarer Elemente in @p ranges.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_get_blocking_bookings(
        const char *date,
        const char *now_utc,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

/**
 * @brief Startet eine SQLite-Transaktion mit sofortiger Schreibsperre.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_begin_immediate(void);
/**
 * @brief Bestätigt die laufende Datenbanktransaktion.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_commit(void);
/**
 * @brief Verwirft die laufende Datenbanktransaktion.
 */
void calendar_database_rollback(void);
/**
 * @brief Lässt abgelaufene vorläufige Reservierungen verfallen.
 * @param[in] now_utc Aktueller UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_expire_pending(const char *now_utc);
/**
 * @brief Markiert Termine vier Stunden nach ihrem Ende automatisch als erledigt.
 * @param[in] timezone IANA-Zeitzonenname, beispielsweise Europe/Berlin.
 * @param[in] now_utc Aktueller UTC-Zeitstempel im Format YYYY-MM-DDTHH:MM:SSZ.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_complete_due_bookings(
        const char *timezone,
        const char *now_utc
);
/**
 * @brief Zählt kürzlich angelegte Buchungen derselben Kontaktidentität.
 * @param[in] contact_channel Kontaktkanal email oder phone.
 * @param[in] email E-Mail-Adresse; abhängig vom Kontaktkanal darf sie leer sein.
 * @param[in] phone_digits Normalisierte Telefonnummer nur aus Ziffern.
 * @param[in] since_utc Untergrenze des Zeitfensters als UTC-Zeitstempel.
 * @param[out] out_count Ausgabeparameter für die Anzahl geschriebener Elemente.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_count_recent_contact_bookings(
        const char *contact_channel,
        const char *email,
        const char *phone_digits,
        const char *since_utc,
        int *out_count
);
/**
 * @brief Speichert eine vorläufige oder automatisch bestätigte Reservierung.
 * @param[in] booking Validierte Buchungsdaten beziehungsweise Zielstruktur.
 * @param[out] out_booking_id Ausgabeparameter für die erzeugte Buchungs-ID.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int calendar_database_insert_pending(
        const calendar_pending_booking *booking,
        int64_t *out_booking_id
);

/**
 * @brief Ergebnis der Bestätigung oder Ablehnung einer Buchungsanfrage.
 */
typedef enum calendar_booking_decision_result {
    CALENDAR_BOOKING_DECISION_ERROR = -1, /**< Interner Fehler. */
    CALENDAR_BOOKING_DECISION_OK = 0, /**< Operation erfolgreich. */
    CALENDAR_BOOKING_DECISION_NOT_FOUND = 1, /**< Angeforderter Datensatz wurde nicht gefunden. */
    CALENDAR_BOOKING_DECISION_NOT_PENDING = 2, /**< Buchung ist nicht mehr offen. */
    CALENDAR_BOOKING_DECISION_EXPIRED = 3 /**< Vorläufige Reservierung ist abgelaufen. */
} calendar_booking_decision_result; /**< Typalias für ::calendar_booking_decision_result. */

/**
 * @brief Bestätigt oder lehnt eine noch offene Buchungsanfrage ab.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] accept true zum Bestätigen, false zum Ablehnen.
 * @param[in] decision_at_utc Zeitpunkt der Entscheidung als UTC-Zeitstempel.
 * @param[in] rejection_reason Optionaler Ablehnungsgrund.
 * @return Ein Wert aus ::calendar_booking_decision_result.
 */
calendar_booking_decision_result calendar_database_decide_booking(
        int64_t booking_id,
        bool accept,
        const char *decision_at_utc,
        const char *rejection_reason
);

#endif
