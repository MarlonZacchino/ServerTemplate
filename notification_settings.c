#include "notification_settings.h"

#include "contact_validation.h"
#include "server_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sodium.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SETTINGS_MAGIC "S4DSMTP1"
#define SETTINGS_MAGIC_SIZE 8
#define SETTINGS_PLAINTEXT_MAX 4096
#define SETTINGS_BLOB_MAX (SETTINGS_MAGIC_SIZE + crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + SETTINGS_PLAINTEXT_MAX)

static char settings_error[NOTIFICATION_SETTINGS_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(settings_error, sizeof(settings_error), "%s",
             message == NULL ? "Unbekannter E-Mail-Konfigurationsfehler" : message);
}

static void set_errno_error(const char *context)
{
    snprintf(settings_error, sizeof(settings_error), "%s: %s",
             context == NULL ? "Dateifehler" : context, strerror(errno));
}

const char *notification_settings_last_error(void)
{
    return settings_error[0] == '\0' ? "Unbekannter E-Mail-Konfigurationsfehler" : settings_error;
}

static bool single_line(const char *value, bool allow_empty)
{
    if (value == NULL || (!allow_empty && value[0] == '\0')) return false;
    for (size_t index = 0; value[index] != '\0'; index++) {
        unsigned char character = (unsigned char)value[index];
        if (character < 0x20 || character == 0x7f) return false;
    }
    return true;
}

static bool smtp_url_valid(const char *url)
{
    return url != NULL && single_line(url, false) &&
           ((strncmp(url, "smtp://", 7) == 0 && url[7] != '\0') ||
            (strncmp(url, "smtps://", 8) == 0 && url[8] != '\0'));
}

bool notification_settings_are_valid(const notification_smtp_settings *settings, bool require_enabled)
{
    bool credentials_match;
    if (settings == NULL) return false;
    if (!settings->enabled) return !require_enabled;

    credentials_match =
            (settings->username[0] == '\0' && settings->password[0] == '\0') ||
            (settings->username[0] != '\0' && settings->password[0] != '\0');

    return smtp_url_valid(settings->url) &&
           contact_email_is_valid(settings->from_address) &&
           (settings->admin_email[0] == '\0' || contact_email_is_valid(settings->admin_email)) &&
           (!settings->notify_admin_on_new_booking || settings->admin_email[0] != '\0') &&
           single_line(settings->username, true) &&
           single_line(settings->password, true) &&
           single_line(settings->from_name, true) && credentials_match;
}

static int secret_path(const char *filename, char destination[PATH_MAX])
{
    const char *directory = server_config_secrets_dir();
    int written;
    if (directory == NULL || directory[0] == '\0' || filename == NULL) {
        set_error("Secrets-Verzeichnis ist nicht konfiguriert");
        return -1;
    }
    written = snprintf(destination, PATH_MAX, "%s%s%s", directory,
                       directory[strlen(directory) - 1] == '/' ? "" : "/", filename);
    if (written < 0 || written >= PATH_MAX) {
        set_error("Pfad der E-Mail-Konfiguration ist zu lang");
        return -1;
    }
    return 0;
}

static int ensure_secrets_directory(void)
{
    const char *directory = server_config_secrets_dir();
    struct stat status;
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

static int write_all(int descriptor, const unsigned char *data, size_t length)
{
    size_t total = 0;
    while (total < length) {
        ssize_t written = write(descriptor, data + total, length - total);
        if (written > 0) total += (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else return -1;
    }
    return 0;
}

static int read_exact_file(const char *path, unsigned char *buffer, size_t expected)
{
    int flags = O_RDONLY;
    int descriptor;
    struct stat status;
    size_t total = 0;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(path, flags);
    if (descriptor < 0) return errno == ENOENT ? 1 : -1;
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || (size_t)status.st_size != expected) {
        close(descriptor); errno = EINVAL; return -1;
    }
    while (total < expected) {
        ssize_t count = read(descriptor, buffer + total, expected - total);
        if (count > 0) total += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else { close(descriptor); errno = EIO; return -1; }
    }
    return close(descriptor) == 0 ? 0 : -1;
}

static int load_or_create_key(unsigned char key[crypto_secretbox_KEYBYTES])
{
    char path[PATH_MAX];
    int result;
    int descriptor;
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    if (secret_path("notification.key", path) != 0 || ensure_secrets_directory() != 0) return -1;
    result = read_exact_file(path, key, crypto_secretbox_KEYBYTES);
    if (result == 0) return 0;
    if (result < 0 && errno != ENOENT) { set_errno_error("E-Mail-Schlüssel konnte nicht gelesen werden"); return -1; }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(path, flags, 0600);
    if (descriptor < 0) {
        if (errno == EEXIST && read_exact_file(path, key, crypto_secretbox_KEYBYTES) == 0) return 0;
        set_errno_error("E-Mail-Schlüssel konnte nicht angelegt werden"); return -1;
    }
    randombytes_buf(key, crypto_secretbox_KEYBYTES);
    if (write_all(descriptor, key, crypto_secretbox_KEYBYTES) != 0 || fsync(descriptor) != 0) {
        int saved = errno; close(descriptor); unlink(path); errno = saved;
        sodium_memzero(key, crypto_secretbox_KEYBYTES);
        set_errno_error("E-Mail-Schlüssel konnte nicht gespeichert werden"); return -1;
    }
    if (close(descriptor) != 0) { unlink(path); set_errno_error("E-Mail-Schlüssel konnte nicht geschlossen werden"); return -1; }
    return 0;
}

static int serialize(const notification_smtp_settings *settings, unsigned char *output, size_t size, size_t *out_length)
{
    int written = snprintf((char *)output, size, "1\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s",
                           settings->enabled ? 1 : 0,
                           settings->notify_admin_on_new_booking ? 1 : 0,
                           settings->url, settings->username, settings->password,
                           settings->from_address, settings->from_name, settings->admin_email);
    if (written < 0 || (size_t)written + 1 > size) { set_error("E-Mail-Konfiguration ist zu groß"); return -1; }
    *out_length = (size_t)written + 1;
    return 0;
}

static int split_fields(char *text, char *fields[], size_t expected)
{
    size_t count = 0;
    char *start = text;
    for (char *cursor = text;; cursor++) {
        if (*cursor == '\t' || *cursor == '\0') {
            if (count >= expected) return -1;
            fields[count++] = start;
            if (*cursor == '\0') break;
            *cursor = '\0';
            start = cursor + 1;
        }
    }
    return count == expected ? 0 : -1;
}

static int copy_field(char *destination, size_t size, const char *source)
{
    int written = snprintf(destination, size, "%s", source);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int deserialize(unsigned char *plaintext, size_t length, notification_smtp_settings *settings)
{
    char *fields[9];
    if (length == 0 || plaintext[length - 1] != '\0' || split_fields((char *)plaintext, fields, 9) != 0 ||
        strcmp(fields[0], "1") != 0 || (strcmp(fields[1], "0") != 0 && strcmp(fields[1], "1") != 0) ||
        (strcmp(fields[2], "0") != 0 && strcmp(fields[2], "1") != 0)) return -1;

    memset(settings, 0, sizeof(*settings));
    settings->enabled = strcmp(fields[1], "1") == 0;
    settings->managed_by_admin = true;
    settings->notify_admin_on_new_booking = strcmp(fields[2], "1") == 0;
    if (copy_field(settings->url, sizeof(settings->url), fields[3]) != 0 ||
        copy_field(settings->username, sizeof(settings->username), fields[4]) != 0 ||
        copy_field(settings->password, sizeof(settings->password), fields[5]) != 0 ||
        copy_field(settings->from_address, sizeof(settings->from_address), fields[6]) != 0 ||
        copy_field(settings->from_name, sizeof(settings->from_name), fields[7]) != 0 ||
        copy_field(settings->admin_email, sizeof(settings->admin_email), fields[8]) != 0 ||
        !notification_settings_are_valid(settings, false)) {
        sodium_memzero(settings, sizeof(*settings)); return -1;
    }
    return 0;
}

static int read_managed(notification_smtp_settings *settings)
{
    char path[PATH_MAX];
    int flags = O_RDONLY;
    int descriptor;
    struct stat status;
    unsigned char blob[SETTINGS_BLOB_MAX];
    unsigned char plaintext[SETTINGS_PLAINTEXT_MAX];
    unsigned char key[crypto_secretbox_KEYBYTES];
    size_t total = 0;
    int result = -1;
    if (secret_path("notification.smtp", path) != 0) return -1;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(path, flags);
    if (descriptor < 0) {
        if (errno == ENOENT) return 1;
        set_errno_error("Verschlüsselte E-Mail-Konfiguration konnte nicht geöffnet werden"); return -1;
    }
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < (off_t)(SETTINGS_MAGIC_SIZE + crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + 1) ||
        status.st_size > (off_t)sizeof(blob)) {
        close(descriptor); set_error("Verschlüsselte E-Mail-Konfiguration hat ein ungültiges Format"); return -1;
    }
    while (total < (size_t)status.st_size) {
        ssize_t count = read(descriptor, blob + total, (size_t)status.st_size - total);
        if (count > 0) total += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else { close(descriptor); set_errno_error("E-Mail-Konfiguration konnte nicht gelesen werden"); return -1; }
    }
    close(descriptor);
    if (memcmp(blob, SETTINGS_MAGIC, SETTINGS_MAGIC_SIZE) != 0) { set_error("Unbekannte E-Mail-Konfigurationsversion"); goto cleanup; }
    if (load_or_create_key(key) != 0) goto cleanup;
    {
        size_t cipher_length = total - SETTINGS_MAGIC_SIZE - crypto_secretbox_NONCEBYTES;
        size_t plaintext_length = cipher_length - crypto_secretbox_MACBYTES;
        if (crypto_secretbox_open_easy(plaintext,
                blob + SETTINGS_MAGIC_SIZE + crypto_secretbox_NONCEBYTES,
                (unsigned long long)cipher_length,
                blob + SETTINGS_MAGIC_SIZE, key) != 0) {
            set_error("E-Mail-Konfiguration konnte nicht entschlüsselt werden"); goto cleanup;
        }
        if (deserialize(plaintext, plaintext_length, settings) != 0) {
            set_error("Entschlüsselte E-Mail-Konfiguration ist ungültig"); goto cleanup;
        }
    }
    result = 0;
cleanup:
    sodium_memzero(key, sizeof(key));
    sodium_memzero(plaintext, sizeof(plaintext));
    sodium_memzero(blob, sizeof(blob));
    return result;
}

static int copy_env(const char *name, char *destination, size_t size)
{
    const char *value = getenv(name);
    if (value == NULL) value = "";
    if (!single_line(value, true) || strlen(value) >= size) {
        snprintf(settings_error, sizeof(settings_error), "%s ist ungültig", name); return -1;
    }
    memcpy(destination, value, strlen(value) + 1); return 0;
}

static int load_environment(notification_smtp_settings *settings)
{
    memset(settings, 0, sizeof(*settings));
    if (copy_env("STYLES4DOGS_SMTP_URL", settings->url, sizeof(settings->url)) != 0 ||
        copy_env("STYLES4DOGS_SMTP_USERNAME", settings->username, sizeof(settings->username)) != 0 ||
        copy_env("STYLES4DOGS_SMTP_PASSWORD", settings->password, sizeof(settings->password)) != 0 ||
        copy_env("STYLES4DOGS_SMTP_FROM_ADDRESS", settings->from_address, sizeof(settings->from_address)) != 0 ||
        copy_env("STYLES4DOGS_SMTP_FROM_NAME", settings->from_name, sizeof(settings->from_name)) != 0 ||
        copy_env("STYLES4DOGS_ADMIN_NOTIFICATION_EMAIL", settings->admin_email, sizeof(settings->admin_email)) != 0) return -1;
    settings->notify_admin_on_new_booking = getenv("STYLES4DOGS_NOTIFY_ADMIN_NEW_BOOKING") != NULL &&
                                             strcmp(getenv("STYLES4DOGS_NOTIFY_ADMIN_NEW_BOOKING"), "1") == 0;
    settings->enabled = settings->url[0] != '\0' || settings->from_address[0] != '\0';
    settings->managed_by_admin = false;
    if (settings->enabled && settings->from_name[0] == '\0')
        snprintf(settings->from_name, sizeof(settings->from_name), "%s", server_config_salon_name());
    if (!notification_settings_are_valid(settings, false)) {
        set_error("SMTP-Umgebungsvariablen sind unvollständig oder ungültig");
        sodium_memzero(settings, sizeof(*settings)); return -1;
    }
    return 0;
}

static void migrate_default_brand_name(notification_smtp_settings *settings)
{
    if (settings != NULL && strcmp(settings->from_name, "Styles 4 Dogs") == 0) {
        snprintf(settings->from_name, sizeof(settings->from_name), "%s", "Styling 4 Dogs");
    }
}

int notification_settings_load(notification_smtp_settings *settings)
{
    int result;
    settings_error[0] = '\0';
    if (settings == NULL) { set_error("Ausgabe für E-Mail-Konfiguration fehlt"); return -1; }
    if (sodium_init() < 0) { set_error("Kryptografie konnte nicht initialisiert werden"); return -1; }
    result = read_managed(settings);
    if (result == 0) {
        migrate_default_brand_name(settings);
        return 0;
    }
    if (result < 0) return -1;
    result = load_environment(settings);
    if (result == 0) migrate_default_brand_name(settings);
    return result;
}

static int write_blob(const unsigned char *blob, size_t length)
{
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    int descriptor;
    int written;
    if (secret_path("notification.smtp", path) != 0 || ensure_secrets_directory() != 0) return -1;
    written = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary)) { set_error("Temporärer Konfigurationspfad ist zu lang"); return -1; }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    unlink(temporary);
    descriptor = open(temporary, flags, 0600);
    if (descriptor < 0) { set_errno_error("Temporäre E-Mail-Konfiguration konnte nicht angelegt werden"); return -1; }
    if (write_all(descriptor, blob, length) != 0 || fsync(descriptor) != 0) {
        int saved = errno; close(descriptor); unlink(temporary); errno = saved;
        set_errno_error("E-Mail-Konfiguration konnte nicht gespeichert werden"); return -1;
    }
    if (close(descriptor) != 0 || rename(temporary, path) != 0) {
        int saved = errno; unlink(temporary); errno = saved;
        set_errno_error("E-Mail-Konfiguration konnte nicht atomar gespeichert werden"); return -1;
    }
    return 0;
}

int notification_settings_save(const notification_smtp_settings *settings)
{
    unsigned char plaintext[SETTINGS_PLAINTEXT_MAX];
    unsigned char blob[SETTINGS_BLOB_MAX];
    unsigned char key[crypto_secretbox_KEYBYTES];
    size_t plaintext_length;
    size_t blob_length;
    int result = -1;
    settings_error[0] = '\0';
    if (sodium_init() < 0) { set_error("Kryptografie konnte nicht initialisiert werden"); return -1; }
    if (!notification_settings_are_valid(settings, false)) { set_error("E-Mail-Konfiguration ist unvollständig oder ungültig"); return -1; }
    if (serialize(settings, plaintext, sizeof(plaintext), &plaintext_length) != 0 || load_or_create_key(key) != 0) goto cleanup;
    memcpy(blob, SETTINGS_MAGIC, SETTINGS_MAGIC_SIZE);
    randombytes_buf(blob + SETTINGS_MAGIC_SIZE, crypto_secretbox_NONCEBYTES);
    if (crypto_secretbox_easy(blob + SETTINGS_MAGIC_SIZE + crypto_secretbox_NONCEBYTES,
            plaintext, (unsigned long long)plaintext_length,
            blob + SETTINGS_MAGIC_SIZE, key) != 0) {
        set_error("E-Mail-Konfiguration konnte nicht verschlüsselt werden"); goto cleanup;
    }
    blob_length = SETTINGS_MAGIC_SIZE + crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + plaintext_length;
    result = write_blob(blob, blob_length);
cleanup:
    sodium_memzero(key, sizeof(key)); sodium_memzero(plaintext, sizeof(plaintext)); sodium_memzero(blob, sizeof(blob));
    return result;
}

int notification_settings_disconnect(void)
{
    notification_smtp_settings settings;
    memset(&settings, 0, sizeof(settings));
    settings.managed_by_admin = true;
    return notification_settings_save(&settings);
}
