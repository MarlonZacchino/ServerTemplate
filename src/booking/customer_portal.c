#include "styles4dogs/booking/customer_portal.h"

#include "styles4dogs/calendar/calendar_time.h"
#include "styles4dogs/core/server_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sodium.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CUSTOMER_PORTAL_ERROR_SIZE 512
#define CUSTOMER_PORTAL_KEY_FILE "customer-portal.key"
#define CUSTOMER_PORTAL_KEY_BYTES crypto_generichash_KEYBYTES
#define CUSTOMER_PORTAL_TOKEN_BYTES 32

static char portal_error[CUSTOMER_PORTAL_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            portal_error,
            sizeof(portal_error),
            "%s",
            message == NULL ? "Unbekannter Kundenbereich-Fehler" : message);
}

static void set_errno_error(const char *context)
{
    snprintf(
            portal_error,
            sizeof(portal_error),
            "%s: %s",
            context == NULL ? "Dateifehler" : context,
            strerror(errno));
}

static void set_sqlite_error(sqlite3 *database, const char *context)
{
    snprintf(
            portal_error,
            sizeof(portal_error),
            "%s: %s",
            context == NULL ? "SQLite-Fehler" : context,
            database == NULL ? "Datenbank ist nicht geöffnet" : sqlite3_errmsg(database));
}

const char *customer_portal_last_error(void)
{
    return portal_error[0] == '\0'
            ? "Unbekannter Kundenbereich-Fehler"
            : portal_error;
}

static const char *database_open_path(void)
{
    if (strcmp(server_config_database_file(), ":memory:") == 0) {
        return "file:styles4dogs-runtime?mode=memory&cache=shared";
    }

    return server_config_database_file();
}

static int ensure_secrets_directory(void)
{
    const char *directory = server_config_secrets_dir();
    struct stat status;

    if (directory == NULL || directory[0] == '\0') {
        set_error("Secrets-Verzeichnis ist nicht konfiguriert");
        return -1;
    }

    if (lstat(directory, &status) == 0) {
        if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
            set_error("Secrets-Pfad ist kein reguläres Verzeichnis");
            return -1;
        }
        return 0;
    }

    if (errno != ENOENT || mkdir(directory, 0700) != 0) {
        set_errno_error("Secrets-Verzeichnis konnte nicht angelegt werden");
        return -1;
    }

    return 0;
}

static int key_path(char out_path[PATH_MAX])
{
    const char *directory = server_config_secrets_dir();
    int written;

    if (directory == NULL || directory[0] == '\0') {
        set_error("Secrets-Verzeichnis ist nicht konfiguriert");
        return -1;
    }

    written = snprintf(
            out_path,
            PATH_MAX,
            "%s%s%s",
            directory,
            directory[strlen(directory) - 1] == '/' ? "" : "/",
            CUSTOMER_PORTAL_KEY_FILE);

    if (written < 0 || written >= PATH_MAX) {
        set_error("Pfad des Kundenbereich-Schlüssels ist zu lang");
        return -1;
    }

    return 0;
}

static int write_all(int descriptor, const unsigned char *data, size_t length)
{
    size_t position = 0;

    while (position < length) {
        ssize_t written = write(descriptor, data + position, length - position);
        if (written > 0) {
            position += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }

    return 0;
}

static int read_exact_key(const char *path, unsigned char key[CUSTOMER_PORTAL_KEY_BYTES])
{
    int flags = O_RDONLY;
    int descriptor;
    struct stat status;
    size_t position = 0;

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    descriptor = open(path, flags);
    if (descriptor < 0) {
        return errno == ENOENT ? 1 : -1;
    }

    if (fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) ||
        (size_t)status.st_size != CUSTOMER_PORTAL_KEY_BYTES ||
        (status.st_mode & 0777) != 0600) {
        close(descriptor);
        errno = EINVAL;
        return -1;
    }

    while (position < CUSTOMER_PORTAL_KEY_BYTES) {
        ssize_t read_count = read(
                descriptor,
                key + position,
                CUSTOMER_PORTAL_KEY_BYTES - position);

        if (read_count > 0) {
            position += (size_t)read_count;
        } else if (read_count < 0 && errno == EINTR) {
            continue;
        } else {
            close(descriptor);
            errno = EIO;
            return -1;
        }
    }

    if (close(descriptor) != 0) {
        return -1;
    }

    return 0;
}

static int load_or_create_key(unsigned char key[CUSTOMER_PORTAL_KEY_BYTES])
{
    char path[PATH_MAX];
    int result;
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    int descriptor;

    if (sodium_init() < 0) {
        set_error("Kryptografie konnte nicht initialisiert werden");
        return -1;
    }

    if (key_path(path) != 0 || ensure_secrets_directory() != 0) {
        return -1;
    }

    result = read_exact_key(path, key);
    if (result == 0) {
        return 0;
    }
    if (result < 0 && errno != ENOENT) {
        set_errno_error("Kundenbereich-Schlüssel konnte nicht gelesen werden");
        return -1;
    }

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    descriptor = open(path, flags, 0600);
    if (descriptor < 0) {
        if (errno == EEXIST && read_exact_key(path, key) == 0) {
            return 0;
        }
        set_errno_error("Kundenbereich-Schlüssel konnte nicht angelegt werden");
        return -1;
    }

    randombytes_buf(key, CUSTOMER_PORTAL_KEY_BYTES);

    if (write_all(descriptor, key, CUSTOMER_PORTAL_KEY_BYTES) != 0 ||
        fsync(descriptor) != 0) {
        int saved_errno = errno;
        close(descriptor);
        unlink(path);
        sodium_memzero(key, CUSTOMER_PORTAL_KEY_BYTES);
        errno = saved_errno;
        set_errno_error("Kundenbereich-Schlüssel konnte nicht gespeichert werden");
        return -1;
    }

    if (close(descriptor) != 0) {
        int saved_errno = errno;
        unlink(path);
        sodium_memzero(key, CUSTOMER_PORTAL_KEY_BYTES);
        errno = saved_errno;
        set_errno_error("Kundenbereich-Schlüssel konnte nicht geschlossen werden");
        return -1;
    }

    return 0;
}

static int build_token(
        int64_t booking_id,
        char out_token[CUSTOMER_PORTAL_TOKEN_HEX_SIZE]
)
{
    unsigned char key[CUSTOMER_PORTAL_KEY_BYTES];
    unsigned char digest[CUSTOMER_PORTAL_TOKEN_BYTES];
    unsigned char message[32];
    int written;

    if (booking_id <= 0 || out_token == NULL) {
        set_error("Ungültige Buchungsnummer für Kundenlink");
        return -1;
    }

    if (load_or_create_key(key) != 0) {
        return -1;
    }

    written = snprintf(
            (char *)message,
            sizeof(message),
            "%lld",
            (long long)booking_id);

    if (written <= 0 || (size_t)written >= sizeof(message) ||
        crypto_generichash(
                digest,
                sizeof(digest),
                message,
                (unsigned long long)written,
                key,
                sizeof(key)) != 0 ||
        sodium_bin2hex(
                out_token,
                CUSTOMER_PORTAL_TOKEN_HEX_SIZE,
                digest,
                sizeof(digest)) == NULL) {
        sodium_memzero(key, sizeof(key));
        sodium_memzero(digest, sizeof(digest));
        sodium_memzero(message, sizeof(message));
        set_error("Kundenlink konnte nicht erzeugt werden");
        return -1;
    }

    sodium_memzero(key, sizeof(key));
    sodium_memzero(digest, sizeof(digest));
    sodium_memzero(message, sizeof(message));
    return 0;
}

int customer_portal_build_url(
        int64_t booking_id,
        char *out_url,
        size_t out_url_size
)
{
    char token[CUSTOMER_PORTAL_TOKEN_HEX_SIZE];
    const char *base_url;
    size_t base_length;
    int written;

    if (out_url == NULL || out_url_size == 0 || build_token(booking_id, token) != 0) {
        return -1;
    }

    base_url = server_config_public_base_url();
    base_length = strlen(base_url);

    written = snprintf(
            out_url,
            out_url_size,
            "%s%sbuchung/%lld/%s",
            base_url,
            base_length > 0 && base_url[base_length - 1] == '/' ? "" : "/",
            (long long)booking_id,
            token);

    sodium_memzero(token, sizeof(token));

    if (written < 0 || (size_t)written >= out_url_size) {
        set_error("Kundenlink ist zu lang");
        return -1;
    }

    return 0;
}

bool customer_portal_token_is_valid(
        int64_t booking_id,
        const char *token
)
{
    char expected[CUSTOMER_PORTAL_TOKEN_HEX_SIZE];
    bool valid;

    if (token == NULL ||
        strnlen(token, CUSTOMER_PORTAL_TOKEN_HEX_SIZE + 1) !=
                CUSTOMER_PORTAL_TOKEN_HEX_SIZE - 1 ||
        build_token(booking_id, expected) != 0) {
        return false;
    }

    valid = sodium_memcmp(
            token,
            expected,
            CUSTOMER_PORTAL_TOKEN_HEX_SIZE - 1) == 0;

    sodium_memzero(expected, sizeof(expected));
    return valid;
}

static int open_database(sqlite3 **out_database)
{
    sqlite3 *database = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;

    if (out_database == NULL) {
        set_error("Datenbank-Ausgabe fehlt");
        return -1;
    }

    if (strncmp(database_open_path(), "file:", 5) == 0) {
        flags |= SQLITE_OPEN_URI;
    }

    if (sqlite3_open_v2(database_open_path(), &database, flags, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Kundenbereich-Datenbank konnte nicht geöffnet werden");
        sqlite3_close_v2(database);
        return -1;
    }

    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK ||
        sqlite3_exec(database, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Kundenbereich-Datenbank konnte nicht konfiguriert werden");
        sqlite3_close_v2(database);
        return -1;
    }

    *out_database = database;
    return 0;
}

static int copy_column(
        sqlite3_stmt *statement,
        int column,
        char *destination,
        size_t destination_size
)
{
    const unsigned char *value = sqlite3_column_text(statement, column);
    int written = snprintf(
            destination,
            destination_size,
            "%s",
            value == NULL ? "" : (const char *)value);

    return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

customer_portal_result customer_portal_load_booking(
        int64_t booking_id,
        const char *token,
        customer_portal_booking *out_booking
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (out_booking == NULL || !customer_portal_token_is_valid(booking_id, token)) {
        return CUSTOMER_PORTAL_NOT_FOUND;
    }

    memset(out_booking, 0, sizeof(*out_booking));

    if (open_database(&database) != 0) {
        return CUSTOMER_PORTAL_ERROR;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT id, customer_name, dog_name, "
            "       CASE WHEN service_name_snapshot <> '' THEN service_name_snapshot ELSE service END, "
            "       COALESCE(appointment_date, ''), COALESCE(start_minute, -1), "
            "       COALESCE(end_minute, -1), decision_status, rejection_reason "
            "FROM bookings WHERE id = ?1 AND legacy = 0;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, booking_id) != SQLITE_OK) {
        set_sqlite_error(database, "Buchung konnte für den Kundenbereich nicht vorbereitet werden");
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_ERROR;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_NOT_FOUND;
    }
    if (step_result != SQLITE_ROW) {
        set_sqlite_error(database, "Buchung konnte für den Kundenbereich nicht gelesen werden");
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_ERROR;
    }

    out_booking->id = sqlite3_column_int64(statement, 0);
    out_booking->start_minute = sqlite3_column_int(statement, 5);
    out_booking->end_minute = sqlite3_column_int(statement, 6);

    if (copy_column(statement, 1, out_booking->customer_name, sizeof(out_booking->customer_name)) != 0 ||
        copy_column(statement, 2, out_booking->dog_name, sizeof(out_booking->dog_name)) != 0 ||
        copy_column(statement, 3, out_booking->service_name, sizeof(out_booking->service_name)) != 0 ||
        copy_column(statement, 4, out_booking->appointment_date, sizeof(out_booking->appointment_date)) != 0 ||
        copy_column(statement, 7, out_booking->decision_status, sizeof(out_booking->decision_status)) != 0 ||
        copy_column(statement, 8, out_booking->rejection_reason, sizeof(out_booking->rejection_reason)) != 0) {
        set_error("Buchungsdaten sind zu lang");
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_ERROR;
    }

    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    return CUSTOMER_PORTAL_OK;
}

customer_portal_result customer_portal_cancel_booking(
        int64_t booking_id,
        const char *token,
        const char *cancelled_at_utc
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    int changed_rows;

    if (!customer_portal_token_is_valid(booking_id, token)) {
        return CUSTOMER_PORTAL_NOT_FOUND;
    }
    if (!calendar_utc_timestamp_is_valid(cancelled_at_utc)) {
        set_error("Ungültiger Zeitpunkt für Terminabsage");
        return CUSTOMER_PORTAL_ERROR;
    }

    if (open_database(&database) != 0) {
        return CUSTOMER_PORTAL_ERROR;
    }

    if (sqlite3_prepare_v2(
            database,
            "UPDATE bookings "
            "SET decision_status = 'cancelled', decision_at = ?1, hold_expires_at = NULL, status = 'abgesagt' "
            "WHERE id = ?2 AND legacy = 0 "
            "  AND decision_status IN ('pending', 'confirmed');",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, cancelled_at_utc, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, booking_id) != SQLITE_OK) {
        set_sqlite_error(database, "Terminabsage konnte nicht vorbereitet werden");
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_ERROR;
    }

    result = sqlite3_step(statement);
    changed_rows = sqlite3_changes(database);
    if (result != SQLITE_DONE) {
        set_sqlite_error(database, "Terminabsage konnte nicht gespeichert werden");
        sqlite3_finalize(statement);
        sqlite3_close_v2(database);
        return CUSTOMER_PORTAL_ERROR;
    }
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);

    if (changed_rows == 0) {
        customer_portal_booking booking;
        customer_portal_result load_result = customer_portal_load_booking(
                booking_id,
                token,
                &booking);
        if (load_result == CUSTOMER_PORTAL_NOT_FOUND) {
            return CUSTOMER_PORTAL_NOT_FOUND;
        }
        if (load_result != CUSTOMER_PORTAL_OK) {
            return CUSTOMER_PORTAL_ERROR;
        }
        return CUSTOMER_PORTAL_NOT_CANCELLABLE;
    }

    return CUSTOMER_PORTAL_OK;
}
