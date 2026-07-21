#ifndef STYLES4DOGS_NOTIFICATION_QUEUE_H
#define STYLES4DOGS_NOTIFICATION_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#define NOTIFICATION_EVENT_SIZE 32
#define NOTIFICATION_EMAIL_SIZE 256
#define NOTIFICATION_SUBJECT_SIZE 256
#define NOTIFICATION_BODY_SIZE 4096
#define NOTIFICATION_ICS_SIZE 4096
#define NOTIFICATION_ERROR_SIZE 512

typedef struct notification_job {
    int64_t id;
    int64_t booking_id;
    char event_type[NOTIFICATION_EVENT_SIZE];
    char recipient_email[NOTIFICATION_EMAIL_SIZE];
    char subject[NOTIFICATION_SUBJECT_SIZE];
    char body_text[NOTIFICATION_BODY_SIZE];
    char ics_content[NOTIFICATION_ICS_SIZE];
    int attempts;
} notification_job;

typedef struct notification_queue_counts {
    size_t pending;
    size_t failed;
    size_t sent;
} notification_queue_counts;

/*
 * Fügt eine Benachrichtigung für eine Buchung ein. Ist der E-Mail-Versand in
 * den Kalendereinstellungen deaktiviert oder besitzt die Buchung keine
 * E-Mail-Adresse, wird erfolgreich nichts eingereiht.
 */
int notification_queue_enqueue_booking_event(
        int64_t booking_id,
        const char *event_type
);

/* Fügt fällige Erinnerungen dedupliziert in die Queue ein. */
int notification_queue_enqueue_due_reminders(void);

/* Beansprucht atomar den nächsten fälligen Job. 0=Job, 1=keiner, -1=Fehler. */
int notification_queue_claim_next(notification_job *job);

int notification_queue_mark_sent(int64_t job_id);
int notification_queue_mark_failed(int64_t job_id, const char *error_message);
int notification_queue_enqueue_test_email(const char *recipient_email);
int notification_queue_retry_failed(void);
int notification_queue_clear_sent(void);
int notification_queue_clear_failed(void);
int notification_queue_clear_completed(void);
int notification_queue_get_counts(notification_queue_counts *counts);

const char *notification_queue_last_error(void);

#endif
