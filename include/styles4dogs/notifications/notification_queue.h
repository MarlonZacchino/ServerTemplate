#ifndef STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_QUEUE_H
#define STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_QUEUE_H

/**
 * @file notification_queue.h
 * @brief Verwaltet die persistente Queue für automatische E-Mail-Nachrichten.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief Maximale Länge eines Benachrichtigungsereignisses einschließlich Nullterminator. */
#define NOTIFICATION_EVENT_SIZE 32
/** @brief Maximale Länge einer Empfängeradresse einschließlich Nullterminator. */
#define NOTIFICATION_EMAIL_SIZE 256
/** @brief Maximale Länge eines E-Mail-Betreffs einschließlich Nullterminator. */
#define NOTIFICATION_SUBJECT_SIZE 256
/** @brief Maximale Länge eines E-Mail-Textkörpers einschließlich Nullterminator. */
#define NOTIFICATION_BODY_SIZE 4096
/** @brief Maximale Länge eines iCalendar-Anhangs einschließlich Nullterminator. */
#define NOTIFICATION_ICS_SIZE 4096
/** @brief Maximale Länge einer Queue-Fehlermeldung einschließlich Nullterminator. */
#define NOTIFICATION_ERROR_SIZE 512

/**
 * @brief Aus der Versandqueue geladener E-Mail-Auftrag.
 */
typedef struct notification_job {
    int64_t id; /**< Eindeutige Datenbank-ID. */
    int64_t booking_id; /**< Zugehörige Buchungs-ID; 0 bei unabhängigen Testnachrichten. */
    char event_type[NOTIFICATION_EVENT_SIZE]; /**< Stabiler Typ des Benachrichtigungsereignisses. */
    char recipient_email[NOTIFICATION_EMAIL_SIZE]; /**< Empfängeradresse des Jobs. */
    char subject[NOTIFICATION_SUBJECT_SIZE]; /**< Gerenderter E-Mail-Betreff. */
    char body_text[NOTIFICATION_BODY_SIZE]; /**< Gerenderter E-Mail-Text. */
    char ics_content[NOTIFICATION_ICS_SIZE]; /**< Optionaler iCalendar-Anhang. */
    int attempts; /**< Anzahl bisheriger Versandversuche. */
} notification_job; /**< Typalias für ::notification_job. */

/**
 * @brief Aggregierte Anzahl der Queue-Einträge nach Zustand.
 */
typedef struct notification_queue_counts {
    size_t pending; /**< Anzahl ausstehender Jobs. */
    size_t failed; /**< Anzahl fehlgeschlagener Jobs. */
    size_t sent; /**< Anzahl erfolgreich übergebener Jobs. */
} notification_queue_counts; /**< Typalias für ::notification_queue_counts. */

/**
 * @brief Erzeugt die zu einem Buchungsereignis gehörenden E-Mail-Jobs.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] event_type Stabiler Ereignistyp der Nachrichtenvorlage.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_enqueue_booking_event(
        int64_t booking_id,
        const char *event_type
);

/**
 * @brief Reiht eine Terminverschiebung mit alten und neuen Termindaten ein.
 * @param[in] booking_id Eindeutige ID der Buchung.
 * @param[in] old_appointment_date Vorheriges Datum im Format YYYY-MM-DD.
 * @param[in] old_start_minute Vorherige Startzeit in Minuten seit Mitternacht.
 * @param[in] old_end_minute Vorherige Endzeit in Minuten seit Mitternacht.
 * @param[in] dedupe_nonce Eindeutige Kennung der konkreten Änderung.
 * @retval 0 Bei Erfolg oder wenn die Nachricht bereits eingereiht war.
 * @retval -1 Bei einem internen Fehler.
 */
int notification_queue_enqueue_rescheduled(
        int64_t booking_id,
        const char *old_appointment_date,
        int old_start_minute,
        int old_end_minute,
        const char *dedupe_nonce
);

/**
 * @brief Reiht fällige Terminerinnerungen dedupliziert ein.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_enqueue_due_reminders(void);

/**
 * @brief Beansprucht atomar den nächsten fälligen Versandjob.
 * @param[out] job Ausgabeparameter für den beanspruchten Versandjob.
 * @retval 0 Ein Job wurde beansprucht.
 * @retval 1 Es ist aktuell kein Job fällig.
 * @retval -1 Bei einem Datenbankfehler.
 */
int notification_queue_claim_next(notification_job *job);

/**
 * @brief Markiert einen Versandjob als erfolgreich und bereinigt personenbezogene Inhalte.
 * @param[in] job_id Eindeutige ID des Versandjobs.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_mark_sent(int64_t job_id);
/**
 * @brief Markiert einen Versandjob als fehlgeschlagen.
 * @param[in] job_id Eindeutige ID des Versandjobs.
 * @param[in] error_message Persistierbare Fehlermeldung des Versandversuchs.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_mark_failed(int64_t job_id, const char *error_message);
/**
 * @brief Reiht eine Test-E-Mail ein.
 * @param[in] recipient_email Empfängeradresse der Testnachricht.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_enqueue_test_email(const char *recipient_email);
/**
 * @brief Setzt fehlgeschlagene Jobs auf ausstehend zurück.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_retry_failed(void);
/**
 * @brief Löscht erfolgreich versendete Jobs.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_clear_sent(void);
/**
 * @brief Löscht fehlgeschlagene Jobs.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_clear_failed(void);
/**
 * @brief Löscht sämtliche abgeschlossenen Jobs.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_clear_completed(void);
/**
 * @brief Ermittelt die Anzahl ausstehender, fehlgeschlagener und versendeter Jobs.
 * @param[out] counts Ausgabe- oder Zielstruktur für aggregierte Zähler.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_queue_get_counts(notification_queue_counts *counts);

/**
 * @brief Liefert die letzte Fehlermeldung der Queue.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *notification_queue_last_error(void);

#endif
