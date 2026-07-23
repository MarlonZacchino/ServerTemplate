#include "styles4dogs/booking/booking_database.h"
#include "styles4dogs/calendar/calendar_database.h"
#include "styles4dogs/calendar/calendar_time.h"
#include "styles4dogs/services/contact_validation.h"
#include "styles4dogs/notifications/notification_queue.h"
#include "styles4dogs/notifications/notification_settings.h"
#include "styles4dogs/core/server_config.h"

#include <sodium.h>

#include <curl/curl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define SMTP_VALUE_SIZE 512
#define SMTP_ERROR_SIZE 512
#define WORKER_MAX_JOBS 20

typedef notification_smtp_settings smtp_config;

static int format_checked(char *destination, size_t destination_size, const char *format, ...)
{
    va_list arguments;
    int written;

    if (destination == NULL || destination_size == 0 || format == NULL) {
        return -1;
    }

    va_start(arguments, format);
    written = vsnprintf(destination, destination_size, format, arguments);
    va_end(arguments);

    return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

static int base64_encode(
        const unsigned char *source,
        size_t source_length,
        char *destination,
        size_t destination_size
)
{
    static const char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t output_length = ((source_length + 2) / 3) * 4;
    size_t input = 0;
    size_t output = 0;

    if (source == NULL || destination == NULL || output_length + 1 > destination_size) {
        return -1;
    }

    while (input < source_length) {
        unsigned int first = source[input++];
        unsigned int second = input < source_length ? source[input++] : 0;
        unsigned int third = input < source_length ? source[input++] : 0;
        unsigned int triple = (first << 16) | (second << 8) | third;

        destination[output++] = alphabet[(triple >> 18) & 0x3f];
        destination[output++] = alphabet[(triple >> 12) & 0x3f];
        destination[output++] = alphabet[(triple >> 6) & 0x3f];
        destination[output++] = alphabet[triple & 0x3f];
    }

    if (source_length % 3 == 1) {
        destination[output - 1] = '=';
        destination[output - 2] = '=';
    } else if (source_length % 3 == 2) {
        destination[output - 1] = '=';
    }

    destination[output] = '\0';
    return 0;
}

static int encode_header_value(
        const char *value,
        char *destination,
        size_t destination_size
)
{
    bool ascii_only = true;
    char encoded[SMTP_VALUE_SIZE * 2];
    int written;

    if (value == NULL || destination == NULL || destination_size == 0) {
        return -1;
    }

    for (size_t index = 0; value[index] != '\0'; index++) {
        unsigned char character = (unsigned char)value[index];
        if (character < 32 || character >= 127) {
            ascii_only = false;
            break;
        }
    }

    if (ascii_only) {
        written = snprintf(destination, destination_size, "%s", value);
        return written >= 0 && (size_t)written < destination_size ? 0 : -1;
    }

    if (base64_encode(
            (const unsigned char *)value,
            strlen(value),
            encoded,
            sizeof(encoded)) != 0) {
        return -1;
    }

    written = snprintf(destination, destination_size, "=?UTF-8?B?%s?=", encoded);
    return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

static int format_rfc2822_date(char output[64])
{
    time_t now = time(NULL);
    struct tm utc;

    if (now == (time_t)-1 || gmtime_r(&now, &utc) == NULL) {
        return -1;
    }

    return strftime(output, 64, "%a, %d %b %Y %H:%M:%S +0000", &utc) > 0 ? 0 : -1;
}

static int send_smtp(
        const smtp_config *config,
        const notification_job *job,
        char error[SMTP_ERROR_SIZE]
)
{
    CURL *curl = NULL;
    CURLcode curl_result;
    struct curl_slist *recipients = NULL;
    struct curl_slist *headers = NULL;
    curl_mime *mime = NULL;
    curl_mimepart *part;
    char curl_error[CURL_ERROR_SIZE] = {0};
    char mail_from[SMTP_VALUE_SIZE + 3];
    char recipient[NOTIFICATION_EMAIL_SIZE + 3];
    char from_header[SMTP_VALUE_SIZE * 3];
    char to_header[NOTIFICATION_EMAIL_SIZE + 8];
    char subject_header[NOTIFICATION_SUBJECT_SIZE * 3];
    char encoded_subject[NOTIFICATION_SUBJECT_SIZE * 2];
    char encoded_from_name[SMTP_VALUE_SIZE * 2];
    char date_header[96];
    char date_value[64];
    int result = -1;

    if (config == NULL || job == NULL ||
        encode_header_value(job->subject, encoded_subject, sizeof(encoded_subject)) != 0 ||
        encode_header_value(config->from_name, encoded_from_name, sizeof(encoded_from_name)) != 0 ||
        format_rfc2822_date(date_value) != 0 ||
        format_checked(mail_from, sizeof(mail_from), "<%s>", config->from_address) != 0 ||
        format_checked(recipient, sizeof(recipient), "<%s>", job->recipient_email) != 0 ||
        format_checked(from_header, sizeof(from_header), "From: %s <%s>",
                encoded_from_name, config->from_address) != 0 ||
        format_checked(to_header, sizeof(to_header), "To: <%s>", job->recipient_email) != 0 ||
        format_checked(subject_header, sizeof(subject_header), "Subject: %s", encoded_subject) != 0 ||
        format_checked(date_header, sizeof(date_header), "Date: %s", date_value) != 0) {
        snprintf(error, SMTP_ERROR_SIZE, "E-Mail-Header konnten nicht erstellt werden");
        return -1;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        snprintf(error, SMTP_ERROR_SIZE, "libcurl konnte nicht initialisiert werden");
        return -1;
    }

    recipients = curl_slist_append(recipients, recipient);
    headers = curl_slist_append(headers, from_header);
    headers = curl_slist_append(headers, to_header);
    headers = curl_slist_append(headers, subject_header);
    headers = curl_slist_append(headers, date_header);
    headers = curl_slist_append(headers, "Auto-Submitted: auto-generated");

    if (recipients == NULL || headers == NULL) {
        snprintf(error, SMTP_ERROR_SIZE, "Speicher für E-Mail-Header fehlt");
        goto cleanup;
    }

    mime = curl_mime_init(curl);
    if (mime == NULL) {
        snprintf(error, SMTP_ERROR_SIZE, "MIME-Nachricht konnte nicht erstellt werden");
        goto cleanup;
    }

    part = curl_mime_addpart(mime);
    if (part == NULL ||
        curl_mime_data(part, job->body_text, CURL_ZERO_TERMINATED) != CURLE_OK ||
        curl_mime_type(part, "text/plain; charset=utf-8") != CURLE_OK ||
        curl_mime_encoder(part, "quoted-printable") != CURLE_OK) {
        snprintf(error, SMTP_ERROR_SIZE, "Textteil der E-Mail konnte nicht erstellt werden");
        goto cleanup;
    }

    if (job->ics_content[0] != '\0') {
        part = curl_mime_addpart(mime);
        if (part == NULL ||
            curl_mime_data(part, job->ics_content, CURL_ZERO_TERMINATED) != CURLE_OK ||
            curl_mime_type(part, "text/calendar; charset=utf-8; method=PUBLISH") != CURLE_OK ||
            curl_mime_filename(part, "styles4dogs-termin.ics") != CURLE_OK ||
            curl_mime_encoder(part, "base64") != CURLE_OK) {
            snprintf(error, SMTP_ERROR_SIZE, "Kalenderdatei konnte nicht angehängt werden");
            goto cleanup;
        }
    }

    curl_easy_setopt(curl, CURLOPT_URL, config->url);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

    if (config->username[0] != '\0') {
        curl_easy_setopt(curl, CURLOPT_USERNAME, config->username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, config->password);
    }

    curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        snprintf(
                error,
                SMTP_ERROR_SIZE,
                "SMTP-Versand fehlgeschlagen: %s",
                curl_error[0] == '\0' ? curl_easy_strerror(curl_result) : curl_error);
        goto cleanup;
    }

    result = 0;

cleanup:
    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    return result;
}

static int ensure_directory(const char *path)
{
    struct stat status;

    if (stat(path, &status) == 0) {
        return S_ISDIR(status.st_mode) ? 0 : -1;
    }

    return errno == ENOENT && mkdir(path, 0700) == 0 ? 0 : -1;
}

static int write_dry_run(
        const char *directory,
        const notification_job *job,
        char error[SMTP_ERROR_SIZE]
)
{
    char path[1024];
    FILE *file;
    int written;

    if (directory == NULL || job == NULL || ensure_directory(directory) != 0) {
        snprintf(error, SMTP_ERROR_SIZE, "Dry-Run-Verzeichnis ist nicht verfügbar");
        return -1;
    }

    written = snprintf(
            path,
            sizeof(path),
            "%s/notification-%lld-%s.eml",
            directory,
            (long long)job->id,
            job->event_type);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        snprintf(error, SMTP_ERROR_SIZE, "Dry-Run-Dateipfad ist zu lang");
        return -1;
    }

    file = fopen(path, "wx");
    if (file == NULL) {
        snprintf(error, SMTP_ERROR_SIZE, "Dry-Run-Datei konnte nicht angelegt werden: %s", strerror(errno));
        return -1;
    }

    fprintf(file, "To: %s\nSubject: %s\nEvent: %s\n\n%s",
            job->recipient_email,
            job->subject,
            job->event_type,
            job->body_text);
    if (job->ics_content[0] != '\0') {
        fprintf(file, "\n--- styles4dogs-termin.ics ---\n%s", job->ics_content);
    }

    if (fclose(file) != 0) {
        snprintf(error, SMTP_ERROR_SIZE, "Dry-Run-Datei konnte nicht abgeschlossen werden");
        return -1;
    }

    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--dry-run DIRECTORY]\n", program);
}

int main(int argc, char **argv)
{
    const char *dry_run_directory = NULL;
    smtp_config smtp;
    calendar_settings calendar;
    calendar_clock_snapshot snapshot;
    char error[SMTP_ERROR_SIZE] = {0};
    int processed = 0;
    int failed = 0;

    if (argc == 3 && strcmp(argv[1], "--dry-run") == 0) {
        dry_run_directory = argv[2];
    } else if (argc != 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (server_config_initialize() != 0) {
        fprintf(stderr, "ERROR loading server configuration: %s\n", server_config_last_error());
        return EXIT_FAILURE;
    }
    if (booking_database_initialize() != 0) {
        fprintf(stderr, "ERROR initializing booking database: %s\n", booking_database_last_error());
        return EXIT_FAILURE;
    }
    if (calendar_database_initialize() != 0) {
        fprintf(stderr, "ERROR initializing calendar database: %s\n", calendar_database_last_error());
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    if (calendar_database_get_settings(&calendar) != 0 ||
        calendar_clock_now(calendar.timezone, &snapshot) != 0 ||
        calendar_database_expire_pending(snapshot.now_utc) != 0 ||
        calendar_database_complete_due_bookings(calendar.timezone, snapshot.now_utc) != 0) {
        fprintf(stderr, "ERROR updating automatic booking statuses: %s\n",
                calendar_database_last_error());
        calendar_database_shutdown();
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    if (dry_run_directory == NULL) {
        if (notification_settings_load(&smtp) != 0) {
            fprintf(stderr, "ERROR loading SMTP configuration: %s\n",
                    notification_settings_last_error());
            calendar_database_shutdown();
            booking_database_shutdown();
            return EXIT_FAILURE;
        }
        if (!smtp.enabled) {
            fprintf(stderr, "Notification worker skipped: no active E-mail account\n");
            sodium_memzero(&smtp, sizeof(smtp));
            calendar_database_shutdown();
            booking_database_shutdown();
            return EXIT_SUCCESS;
        }
        if (!smtp.delivery_enabled) {
            fprintf(stderr, "Notification worker skipped: E-mail system is paused\n");
            sodium_memzero(&smtp, sizeof(smtp));
            calendar_database_shutdown();
            booking_database_shutdown();
            return EXIT_SUCCESS;
        }
        if (smtp.from_name[0] == '\0') {
            snprintf(smtp.from_name, sizeof(smtp.from_name), "%s",
                    server_config_salon_name());
        }
    }

    if (notification_queue_enqueue_due_reminders() != 0) {
        fprintf(stderr, "ERROR enqueueing reminders: %s\n", notification_queue_last_error());
        if (dry_run_directory == NULL) {
            sodium_memzero(&smtp, sizeof(smtp));
        }
        calendar_database_shutdown();
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    if (dry_run_directory == NULL && curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "ERROR initializing libcurl\n");
        calendar_database_shutdown();
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    while (processed < WORKER_MAX_JOBS) {
        notification_job job;
        int claim_result = notification_queue_claim_next(&job);
        int send_result;

        if (claim_result == 1) {
            break;
        }
        if (claim_result != 0) {
            fprintf(stderr, "ERROR claiming notification: %s\n", notification_queue_last_error());
            failed++;
            break;
        }

        error[0] = '\0';
        send_result = dry_run_directory == NULL
                ? send_smtp(&smtp, &job, error)
                : write_dry_run(dry_run_directory, &job, error);

        if (send_result == 0) {
            if (notification_queue_mark_sent(job.id) != 0) {
                fprintf(stderr, "ERROR completing notification %lld: %s\n",
                        (long long)job.id,
                        notification_queue_last_error());
                failed++;
                break;
            }
            fprintf(stderr, "Notification %lld sent (%s)\n",
                    (long long)job.id,
                    job.event_type);
        } else {
            if (notification_queue_mark_failed(job.id, error) != 0) {
                fprintf(stderr, "ERROR postponing notification %lld: %s\n",
                        (long long)job.id,
                        notification_queue_last_error());
                failed++;
                break;
            }
            fprintf(stderr, "Notification %lld failed: %s\n", (long long)job.id, error);
            failed++;
        }

        processed++;
    }

    if (dry_run_directory == NULL) {
        curl_global_cleanup();
        sodium_memzero(&smtp, sizeof(smtp));
    }
    calendar_database_shutdown();
    booking_database_shutdown();

    fprintf(stderr, "Notification worker finished: %d processed, %d failed\n", processed, failed);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
