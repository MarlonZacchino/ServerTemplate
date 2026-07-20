//
// Created by Chatgpt & Marlon on 18.07.26.
//

#include "auth.h"
#include "server_config.h"

#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_AUTH_LINE 1024
#define MAX_USERNAME_LENGTH 128
#define MIN_PASSWORD_LENGTH 12
#define MAX_PASSWORD_LENGTH 512
#define MAX_BASIC_TOKEN_LENGTH 1024
#define MAX_DECODED_AUTH_LENGTH 1024

/*
 * create_admin_auth() gibt bei Erfolg 0 zurück.
 * Bei einem Fehler wird ein errno-kompatibler Fehlercode zurückgegeben:
 *
 * EINVAL  Ungültiger Benutzername oder ungültiges Passwort
 * EEXIST  Ein Admin-Zugang existiert bereits
 * EACCES  Fehlende Dateiberechtigungen
 * EIO     Kryptografie- oder Schreibfehler
 * ENOTDIR .secrets existiert, ist aber kein Verzeichnis
 */

static bool init_crypto(void)
{
    static bool initialized = false;

    if (initialized) {
        return true;
    }

    if (sodium_init() < 0) {
        return false;
    }

    initialized = true;
    return true;
}

static char ascii_lower(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return (char)(character - 'A' + 'a');
    }

    return character;
}

static bool equals_ignore_case_n(
        const char *text,
        size_t text_length,
        const char *expected
)
{
    size_t expected_length;

    if (text == NULL || expected == NULL) {
        return false;
    }

    expected_length = strlen(expected);

    if (text_length != expected_length) {
        return false;
    }

    for (size_t index = 0; index < text_length; index++) {
        if (ascii_lower(text[index]) != ascii_lower(expected[index])) {
            return false;
        }
    }

    return true;
}

static const char *find_basic_auth_token(
        const string *request,
        size_t *out_token_length
)
{
    const char *data;
    size_t length;
    size_t position;
    const char *found_token = NULL;
    size_t found_token_length = 0;

    if (out_token_length == NULL) {
        return NULL;
    }

    *out_token_length = 0;

    if (request == NULL) {
        return NULL;
    }

    data = get_const_char_str(request);
    length = get_length(request);

    if (data == NULL || length == 0) {
        return NULL;
    }

    /*
     * Zuerst die Request-Line überspringen.
     */
    position = 0;

    while (position < length && data[position] != '\n') {
        position++;
    }

    if (position >= length) {
        return NULL;
    }

    position++;

    /*
     * Nur die HTTP-Header durchsuchen.
     * Der Request-Body wird ausdrücklich nicht durchsucht.
     */
    while (position < length) {
        size_t raw_line_end = position;
        size_t content_line_end;
        size_t colon_position;
        size_t value_start;
        size_t scheme_end;
        size_t value_end;

        while (raw_line_end < length && data[raw_line_end] != '\n') {
            raw_line_end++;
        }

        content_line_end = raw_line_end;

        if (content_line_end > position &&
            data[content_line_end - 1] == '\r') {
            content_line_end--;
        }

        /*
         * Leere Zeile beendet den HTTP-Headerbereich.
         */
        if (content_line_end == position) {
            break;
        }

        colon_position = position;

        while (colon_position < content_line_end &&
               data[colon_position] != ':') {
            colon_position++;
        }

        if (colon_position < content_line_end &&
            equals_ignore_case_n(
                    data + position,
                    colon_position - position,
                    "Authorization")) {

            /*
             * Mehrere Authorization-Header werden abgelehnt.
             */
            if (found_token != NULL) {
                return NULL;
            }

            value_start = colon_position + 1;

            while (value_start < content_line_end &&
                   (data[value_start] == ' ' ||
                    data[value_start] == '\t')) {
                value_start++;
            }

            scheme_end = value_start;

            while (scheme_end < content_line_end &&
                   data[scheme_end] != ' ' &&
                   data[scheme_end] != '\t') {
                scheme_end++;
            }

            if (!equals_ignore_case_n(
                    data + value_start,
                    scheme_end - value_start,
                    "Basic")) {
                return NULL;
            }

            /*
             * Nach "Basic" muss mindestens ein Leerzeichen oder Tab folgen.
             */
            if (scheme_end >= content_line_end) {
                return NULL;
            }

            value_start = scheme_end;

            while (value_start < content_line_end &&
                   (data[value_start] == ' ' ||
                    data[value_start] == '\t')) {
                value_start++;
            }

            value_end = content_line_end;

            while (value_end > value_start &&
                   (data[value_end - 1] == ' ' ||
                    data[value_end - 1] == '\t')) {
                value_end--;
            }

            if (value_start == value_end) {
                return NULL;
            }

            found_token = data + value_start;
            found_token_length = value_end - value_start;
        }

        if (raw_line_end >= length) {
            break;
        }

        position = raw_line_end + 1;
    }

    if (found_token == NULL) {
        return NULL;
    }

    *out_token_length = found_token_length;
    return found_token;
}

static bool decode_basic_auth_token(
        const char *token,
        size_t token_length,
        unsigned char *decoded,
        size_t decoded_size,
        size_t *out_decoded_length
)
{
    size_t decoded_length = 0;

    if (token == NULL ||
        decoded == NULL ||
        decoded_size < 2 ||
        out_decoded_length == NULL) {
        return false;
    }

    *out_decoded_length = 0;

    if (token_length == 0 ||
        token_length > MAX_BASIC_TOKEN_LENGTH) {
        return false;
    }

    if (sodium_base642bin(
            decoded,
            decoded_size - 1,
            token,
            token_length,
            NULL,
            &decoded_length,
            NULL,
            sodium_base64_VARIANT_ORIGINAL) != 0) {

        decoded_length = 0;

        if (sodium_base642bin(
                decoded,
                decoded_size - 1,
                token,
                token_length,
                NULL,
                &decoded_length,
                NULL,
                sodium_base64_VARIANT_ORIGINAL_NO_PADDING) != 0) {
            return false;
        }
    }

    decoded[decoded_length] = '\0';
    *out_decoded_length = decoded_length;

    return true;
}

static bool load_admin_auth_file(
        char *username,
        size_t username_size,
        char *password_hash,
        size_t password_hash_size
)
{
    int file_descriptor;
    int open_flags = O_RDONLY;
    FILE *file;
    struct stat file_status;

    char line[MAX_AUTH_LINE];
    char *line_end;
    char *colon;

    size_t username_length;
    size_t hash_length;

#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif

#ifdef O_NOFOLLOW
    open_flags |= O_NOFOLLOW;
#endif

    if (username == NULL ||
        username_size == 0 ||
        password_hash == NULL ||
        password_hash_size == 0) {
        return false;
    }

    file_descriptor = open(server_config_auth_file(), open_flags);

    if (file_descriptor < 0) {
        return false;
    }

    if (fstat(file_descriptor, &file_status) != 0 ||
        !S_ISREG(file_status.st_mode)) {
        close(file_descriptor);
        return false;
    }

    file = fdopen(file_descriptor, "r");

    if (file == NULL) {
        close(file_descriptor);
        return false;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return false;
    }

    /*
     * Die Datei darf nur genau eine Authentifizierungszeile enthalten.
     */
    line_end = strpbrk(line, "\r\n");

    if (line_end != NULL) {
        *line_end = '\0';
    } else {
        int extra_character = fgetc(file);

        if (extra_character != EOF) {
            fclose(file);
            sodium_memzero(line, sizeof(line));
            return false;
        }
    }

    fclose(file);

    colon = strchr(line, ':');

    if (colon == NULL) {
        sodium_memzero(line, sizeof(line));
        return false;
    }

    username_length = (size_t)(colon - line);
    hash_length = strlen(colon + 1);

    if (username_length == 0 ||
        username_length >= username_size ||
        hash_length == 0 ||
        hash_length >= password_hash_size) {
        sodium_memzero(line, sizeof(line));
        return false;
    }

    memcpy(username, line, username_length);
    username[username_length] = '\0';

    memcpy(password_hash, colon + 1, hash_length);
    password_hash[hash_length] = '\0';

    sodium_memzero(line, sizeof(line));

    return true;
}

static bool username_equals_n(
        const unsigned char *received_username,
        size_t received_length,
        const char *expected_username
)
{
    size_t expected_length;

    if (received_username == NULL || expected_username == NULL) {
        return false;
    }

    expected_length = strlen(expected_username);

    if (received_length != expected_length) {
        return false;
    }

    return sodium_memcmp(
            received_username,
            expected_username,
            received_length) == 0;
}

static bool is_valid_username(const char *username, size_t username_length)
{
    if (username == NULL ||
        username_length == 0 ||
        username_length >= MAX_USERNAME_LENGTH) {
        return false;
    }

    for (size_t index = 0; index < username_length; index++) {
        unsigned char character = (unsigned char)username[index];

        bool is_letter =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z');

        bool is_number =
                character >= '0' && character <= '9';

        bool is_allowed_symbol =
                character == '.' ||
                character == '_' ||
                character == '-' ||
                character == '@';

        if (!is_letter && !is_number && !is_allowed_symbol) {
            return false;
        }
    }

    return true;
}

static int ensure_secrets_directory(void)
{
    struct stat directory_status;

    if (mkdir(server_config_secrets_dir(), 0700) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        return errno;
    }

    /*
     * lstat() folgt keinen symbolischen Links.
     * Ein Symlink als Secrets-Verzeichnis wird damit abgelehnt.
     */
    if (lstat(server_config_secrets_dir(), &directory_status) != 0) {
        return errno;
    }

    if (!S_ISDIR(directory_status.st_mode)) {
        return ENOTDIR;
    }

    /*
     * Nur der Server-Benutzer soll Zugriff erhalten.
     */
    if (chmod(server_config_secrets_dir(), 0700) != 0) {
        return errno;
    }

    return 0;
}

static int write_all_to_file(
        int file_descriptor,
        const char *data,
        size_t length
)
{
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(
                file_descriptor,
                data + written_total,
                length - written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return errno;
        }

        if (written == 0) {
            return EIO;
        }

        written_total += (size_t)written;
    }

    return 0;
}

bool admin_auth_exists(void)
{
    struct stat file_status;

    if (lstat(server_config_auth_file(), &file_status) == 0) {
        return true;
    }

    /*
     * Bei Berechtigungs- oder sonstigen Dateisystemfehlern wird aus
     * Sicherheitsgründen ebenfalls angenommen, dass die Datei existiert.
     * Nur ENOENT bedeutet eindeutig: Es gibt noch keinen Admin-Zugang.
     */
    return errno != ENOENT;
}

int create_admin_auth(
        const char *username,
        const char *password
)
{
    size_t username_length;
    size_t password_length;

    char password_hash[crypto_pwhash_STRBYTES];
    char auth_line[MAX_AUTH_LINE];

    int directory_result;
    int file_descriptor = -1;
    int open_flags = O_WRONLY | O_CREAT | O_EXCL;
    int result = 0;
    int line_length;
    bool file_created = false;

    if (username == NULL || password == NULL) {
        return EINVAL;
    }

    username_length = strnlen(username, MAX_USERNAME_LENGTH);
    password_length = strnlen(password, MAX_PASSWORD_LENGTH + 1);

    if (!is_valid_username(username, username_length)) {
        return EINVAL;
    }

    if (password_length < MIN_PASSWORD_LENGTH ||
        password_length > MAX_PASSWORD_LENGTH) {
        return EINVAL;
    }

    if (!init_crypto()) {
        return EIO;
    }

    /*
     * Schnelle Vorprüfung.
     * O_EXCL schützt später zusätzlich gegen Race Conditions.
     */
    if (admin_auth_exists()) {
        return EEXIST;
    }

    directory_result = ensure_secrets_directory();

    if (directory_result != 0) {
        return directory_result;
    }

    if (crypto_pwhash_str_alg(
            password_hash,
            password,
            (unsigned long long)password_length,
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE,
            crypto_pwhash_ALG_ARGON2ID13) != 0) {
        sodium_memzero(password_hash, sizeof(password_hash));
        return EIO;
    }

    line_length = snprintf(
            auth_line,
            sizeof(auth_line),
            "%s:%s\n",
            username,
            password_hash);

    if (line_length < 0 ||
        (size_t)line_length >= sizeof(auth_line)) {
        sodium_memzero(password_hash, sizeof(password_hash));
        sodium_memzero(auth_line, sizeof(auth_line));
        return EOVERFLOW;
    }

#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif

#ifdef O_NOFOLLOW
    open_flags |= O_NOFOLLOW;
#endif

    /*
     * O_EXCL verhindert das Überschreiben eines bestehenden Admin-Zugangs.
     * 0600 erlaubt nur dem Server-Benutzer Lesen und Schreiben.
     */
    file_descriptor = open(
            server_config_auth_file(),
            open_flags,
            0600);

    if (file_descriptor < 0) {
        result = errno;
        goto cleanup;
    }

    file_created = true;

    if (fchmod(file_descriptor, 0600) != 0) {
        result = errno;
        goto cleanup;
    }

    result = write_all_to_file(
            file_descriptor,
            auth_line,
            (size_t)line_length);

    if (result != 0) {
        goto cleanup;
    }

    /*
     * Sicherstellen, dass die Datei tatsächlich auf den Datenträger
     * geschrieben wurde, bevor Erfolg gemeldet wird.
     */
    if (fsync(file_descriptor) != 0) {
        result = errno;
        goto cleanup;
    }

cleanup:
    if (file_descriptor >= 0) {
        if (close(file_descriptor) != 0 && result == 0) {
            result = errno;
        }
    }

    /*
     * Bei unvollständiger Erstellung keine beschädigte Datei zurücklassen.
     */
    if (result != 0 && file_created) {
        unlink(server_config_auth_file());
    }

    sodium_memzero(password_hash, sizeof(password_hash));
    sodium_memzero(auth_line, sizeof(auth_line));

    return result;
}

bool request_has_valid_admin_auth(const string *request)
{
    const char *token;
    size_t token_length = 0;
    size_t decoded_length = 0;

    unsigned char decoded[MAX_DECODED_AUTH_LENGTH] = {0};
    char expected_username[MAX_USERNAME_LENGTH] = {0};
    char stored_hash[crypto_pwhash_STRBYTES] = {0};

    unsigned char *colon;
    size_t username_length;
    const char *password;
    size_t password_length;

    bool username_valid;
    bool password_valid;
    bool result = false;

    if (!init_crypto()) {
        goto cleanup;
    }

    if (!load_admin_auth_file(
            expected_username,
            sizeof(expected_username),
            stored_hash,
            sizeof(stored_hash))) {
        goto cleanup;
    }

    token = find_basic_auth_token(request, &token_length);

    if (token == NULL ||
        token_length == 0 ||
        token_length > MAX_BASIC_TOKEN_LENGTH) {
        goto cleanup;
    }

    if (!decode_basic_auth_token(
            token,
            token_length,
            decoded,
            sizeof(decoded),
            &decoded_length)) {
        goto cleanup;
    }

    if (decoded_length == 0) {
        goto cleanup;
    }

    /*
     * Eingebettete Nullbytes werden abgelehnt.
     */
    if (memchr(decoded, '\0', decoded_length) != NULL) {
        goto cleanup;
    }

    colon = memchr(decoded, ':', decoded_length);

    if (colon == NULL) {
        goto cleanup;
    }

    username_length = (size_t)(colon - decoded);
    password = (const char *)(colon + 1);
    password_length =
            decoded_length - username_length - 1;

    if (username_length == 0 ||
        username_length >= MAX_USERNAME_LENGTH ||
        password_length > MAX_PASSWORD_LENGTH) {
        goto cleanup;
    }

    username_valid = username_equals_n(
            decoded,
            username_length,
            expected_username);

    /*
     * Die Passwortprüfung wird auch bei einem falschen Benutzernamen
     * durchgeführt. Dadurch unterscheiden sich die Antwortzeiten weniger.
     */
    password_valid =
            crypto_pwhash_str_verify(
                    stored_hash,
                    password,
                    (unsigned long long)password_length) == 0;

    result = username_valid && password_valid;

cleanup:
    sodium_memzero(decoded, sizeof(decoded));
    sodium_memzero(stored_hash, sizeof(stored_hash));

    return result;
}