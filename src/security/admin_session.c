#include "styles4dogs/security/admin_session.h"

#include "styles4dogs/core/server_config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SESSION_ERROR_SIZE 512
#define SESSION_COOKIE_NAME "styles4dogs_admin"
#define SESSION_ABSOLUTE_SECONDS (12 * 60 * 60)
#define SESSION_IDLE_SECONDS (30 * 60)
#define LOGIN_WINDOW_SECONDS (15 * 60)
#define LOGIN_BLOCK_SECONDS (15 * 60)
#define LOGIN_MAX_FAILURES 5
#define AUTH_LINE_SIZE 1024
#define SESSION_HASH_BYTES 32

static char session_error[SESSION_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(session_error, sizeof(session_error), "%s",
             message == NULL ? "Unbekannter Sitzungsfehler" : message);
}

static void set_sqlite_error(sqlite3 *database, const char *context)
{
    snprintf(session_error, sizeof(session_error), "%s: %s",
             context == NULL ? "SQLite-Fehler" : context,
             database == NULL ? "Datenbank nicht geöffnet" : sqlite3_errmsg(database));
}

const char *admin_session_last_error(void)
{
    return session_error[0] == '\0' ? "Unbekannter Sitzungsfehler" : session_error;
}

static const char *database_path(void)
{
    return strcmp(server_config_database_file(), ":memory:") == 0
           ? "file:styles4dogs-runtime?mode=memory&cache=shared"
           : server_config_database_file();
}

static int open_database(sqlite3 **out_database)
{
    sqlite3 *database = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;

    if (out_database == NULL) {
        set_error("Datenbankausgabe fehlt");
        return -1;
    }
    if (strncmp(database_path(), "file:", 5) == 0) flags |= SQLITE_OPEN_URI;

    if (sqlite3_open_v2(database_path(), &database, flags, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Sitzungsdatenbank konnte nicht geöffnet werden");
        sqlite3_close_v2(database);
        return -1;
    }
    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK ||
        sqlite3_exec(database, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Sitzungsdatenbank konnte nicht konfiguriert werden");
        sqlite3_close_v2(database);
        return -1;
    }

    *out_database = database;
    return 0;
}

static char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value;
}

static bool equals_ignore_case_n(const char *text, size_t length, const char *expected)
{
    size_t expected_length = expected == NULL ? 0 : strlen(expected);
    if (text == NULL || expected == NULL || length != expected_length) return false;
    for (size_t index = 0; index < length; index++)
        if (ascii_lower(text[index]) != ascii_lower(expected[index])) return false;
    return true;
}

static bool request_header(
        const string *request,
        const char *name,
        char *out_value,
        size_t out_size
)
{
    const char *data;
    size_t length;
    size_t position = 0;
    bool found = false;

    if (request == NULL || name == NULL || out_value == NULL || out_size == 0) return false;
    out_value[0] = '\0';
    data = get_const_char_str(request);
    length = get_length(request);
    if (data == NULL) return false;

    while (position < length && data[position] != '\n') position++;
    if (position < length) position++;

    while (position < length) {
        size_t end = position;
        size_t content_end;
        size_t colon;
        size_t value_start;
        size_t value_end;
        size_t value_length;

        while (end < length && data[end] != '\n') end++;
        content_end = end;
        if (content_end > position && data[content_end - 1] == '\r') content_end--;
        if (content_end == position) break;

        colon = position;
        while (colon < content_end && data[colon] != ':') colon++;
        if (colon < content_end && equals_ignore_case_n(data + position, colon - position, name)) {
            if (found) return false;
            value_start = colon + 1;
            while (value_start < content_end &&
                   (data[value_start] == ' ' || data[value_start] == '\t')) value_start++;
            value_end = content_end;
            while (value_end > value_start &&
                   (data[value_end - 1] == ' ' || data[value_end - 1] == '\t')) value_end--;
            value_length = value_end - value_start;
            if (value_length >= out_size) return false;
            memcpy(out_value, data + value_start, value_length);
            out_value[value_length] = '\0';
            found = true;
        }
        position = end < length ? end + 1 : length;
    }
    return found;
}

static bool request_is_https(const string *request)
{
    const char *base = server_config_public_base_url();
    (void)request;
    return strncmp(base, "https://", 8) == 0;
}

static bool cookie_token(const string *request, char token[ADMIN_SESSION_TOKEN_HEX_SIZE])
{
    char cookies[4096];
    const char *position;
    size_t name_length = strlen(SESSION_COOKIE_NAME);

    token[0] = '\0';
    if (!request_header(request, "Cookie", cookies, sizeof(cookies))) return false;

    position = cookies;
    while (*position != '\0') {
        const char *name_start;
        const char *name_end;
        const char *value_start;
        const char *value_end;
        size_t value_length;

        while (*position == ' ' || *position == '\t' || *position == ';') position++;
        name_start = position;
        while (*position != '\0' && *position != '=' && *position != ';') position++;
        name_end = position;
        while (name_end > name_start && name_end[-1] == ' ') name_end--;
        if (*position != '=') {
            while (*position != '\0' && *position != ';') position++;
            continue;
        }
        value_start = ++position;
        while (*position != '\0' && *position != ';') position++;
        value_end = position;
        while (value_end > value_start && value_end[-1] == ' ') value_end--;

        if ((size_t)(name_end - name_start) == name_length &&
            memcmp(name_start, SESSION_COOKIE_NAME, name_length) == 0) {
            value_length = (size_t)(value_end - value_start);
            if (value_length != ADMIN_SESSION_TOKEN_HEX_SIZE - 1) return false;
            for (size_t index = 0; index < value_length; index++)
                if (!isxdigit((unsigned char)value_start[index])) return false;
            memcpy(token, value_start, value_length);
            token[value_length] = '\0';
            return true;
        }
    }
    return false;
}

static int hash_text(const char *text, unsigned char output[SESSION_HASH_BYTES])
{
    if (text == NULL || sodium_init() < 0) return -1;
    return crypto_generichash(output, SESSION_HASH_BYTES,
                              (const unsigned char *)text,
                              (unsigned long long)strlen(text), NULL, 0);
}

static int derive_csrf(const char *token, char output[ADMIN_SESSION_CSRF_HEX_SIZE])
{
    unsigned char digest[SESSION_HASH_BYTES];
    char input[128];
    int written;

    written = snprintf(input, sizeof(input), "styles4dogs-csrf:%s", token == NULL ? "" : token);
    if (written < 0 || (size_t)written >= sizeof(input) || hash_text(input, digest) != 0) return -1;
    sodium_bin2hex(output, ADMIN_SESSION_CSRF_HEX_SIZE, digest, sizeof(digest));
    sodium_memzero(digest, sizeof(digest));
    sodium_memzero(input, sizeof(input));
    return 0;
}

static bool load_legacy_auth(
        char username[ADMIN_SESSION_USERNAME_SIZE],
        char password_hash[crypto_pwhash_STRBYTES]
)
{
    int descriptor;
    char line[AUTH_LINE_SIZE];
    ssize_t count;
    char *colon;
    char *line_end;

    username[0] = '\0';
    password_hash[0] = '\0';
    descriptor = open(server_config_auth_file(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return false;
    count = read(descriptor, line, sizeof(line) - 1);
    close(descriptor);
    if (count <= 0 || (size_t)count >= sizeof(line)) return false;
    line[count] = '\0';
    line_end = strpbrk(line, "\r\n");
    if (line_end != NULL) *line_end = '\0';
    colon = strchr(line, ':');
    if (colon == NULL) return false;
    *colon = '\0';
    if (line[0] == '\0' || colon[1] == '\0' ||
        strlen(line) >= ADMIN_SESSION_USERNAME_SIZE ||
        strlen(colon + 1) >= crypto_pwhash_STRBYTES) return false;
    memcpy(username, line, strlen(line) + 1);
    memcpy(password_hash, colon + 1, strlen(colon + 1) + 1);
    sodium_memzero(line, sizeof(line));
    return true;
}

int admin_session_initialize(void)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char username[ADMIN_SESSION_USERNAME_SIZE];
    char password_hash[crypto_pwhash_STRBYTES];
    int result = -1;

    session_error[0] = '\0';
    if (!load_legacy_auth(username, password_hash)) return 0;
    if (open_database(&database) != 0) goto cleanup;

    if (sqlite3_prepare_v2(database,
            "INSERT INTO admin_users(username,password_hash,active,created_at,updated_at) "
            "VALUES(?1,?2,1,strftime('%Y-%m-%dT%H:%M:%SZ','now'),strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
            "ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash, "
            "active=1, updated_at=excluded.updated_at;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, password_hash, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Admin-Zugang konnte nicht importiert werden");
        goto cleanup;
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    sodium_memzero(password_hash, sizeof(password_hash));
    sodium_memzero(username, sizeof(username));
    return result;
}

static int build_cookie(
        const string *request,
        const char *token,
        int max_age,
        char *output,
        size_t output_size
)
{
    int written = snprintf(output, output_size,
            "Set-Cookie: %s=%s; Path=/admin; HttpOnly; SameSite=Lax; Max-Age=%d%s\r\n",
            SESSION_COOKIE_NAME, token == NULL ? "" : token, max_age,
            request_is_https(request) ? "; Secure" : "");
    return written >= 0 && (size_t)written < output_size ? 0 : -1;
}

static int login_identity_hash(
        const char *client_ip,
        const char *username,
        unsigned char output[SESSION_HASH_BYTES]
)
{
    char input[512];
    int written;

    written = snprintf(input, sizeof(input), "%s|%s",
                       username == NULL ? "" : username,
                       client_ip == NULL ? "" : client_ip);
    if (written < 0 || (size_t)written >= sizeof(input)) return -1;
    for (size_t index = 0; input[index] != '\0'; index++) input[index] = ascii_lower(input[index]);
    return hash_text(input, output);
}

static int login_rate_state(
        sqlite3 *database,
        const unsigned char identity[SESSION_HASH_BYTES],
        time_t now,
        bool *out_blocked
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    *out_blocked = false;

    if (sqlite3_prepare_v2(database,
            "SELECT blocked_until FROM admin_login_attempts WHERE identity_hash=?1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, identity, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error(database, "Loginlimit konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }
    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_ROW) *out_blocked = sqlite3_column_int64(statement, 0) > (sqlite3_int64)now;
    else if (step_result != SQLITE_DONE) {
        set_sqlite_error(database, "Loginlimit konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }
    sqlite3_finalize(statement);
    return 0;
}

static int record_login_failure(
        sqlite3 *database,
        const unsigned char identity[SESSION_HASH_BYTES],
        time_t now
)
{
    sqlite3_stmt *statement = NULL;
    sqlite3_int64 window_start = (sqlite3_int64)now - LOGIN_WINDOW_SECONDS;
    sqlite3_int64 blocked_until = (sqlite3_int64)now + LOGIN_BLOCK_SECONDS;

    if (sqlite3_prepare_v2(database,
            "INSERT INTO admin_login_attempts(identity_hash,attempt_count,window_started_at,blocked_until) "
            "VALUES(?1,1,?2,0) ON CONFLICT(identity_hash) DO UPDATE SET "
            "attempt_count=CASE WHEN window_started_at<?3 THEN 1 ELSE attempt_count+1 END, "
            "window_started_at=CASE WHEN window_started_at<?3 THEN ?2 ELSE window_started_at END, "
            "blocked_until=CASE WHEN (CASE WHEN window_started_at<?3 THEN 1 ELSE attempt_count+1 END)>=?4 "
            "THEN ?5 ELSE blocked_until END;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, identity, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)now) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, window_start) != SQLITE_OK ||
        sqlite3_bind_int(statement, 4, LOGIN_MAX_FAILURES) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 5, blocked_until) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Fehlgeschlagener Login konnte nicht protokolliert werden");
        sqlite3_finalize(statement);
        return -1;
    }
    sqlite3_finalize(statement);
    return 0;
}

admin_session_login_result admin_session_login(
        const string *request,
        const char *client_ip,
        const char *username,
        const char *password,
        char *out_cookie_header,
        size_t out_cookie_header_size
)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char identity[SESSION_HASH_BYTES];
    unsigned char token_bytes[32];
    unsigned char token_hash[SESSION_HASH_BYTES];
    char token[ADMIN_SESSION_TOKEN_HEX_SIZE];
    char stored_hash[crypto_pwhash_STRBYTES] = "";
    int64_t admin_id = 0;
    time_t now = time(NULL);
    bool blocked = false;
    bool password_valid = false;
    admin_session_login_result result = ADMIN_SESSION_LOGIN_ERROR;

    session_error[0] = '\0';
    if (request == NULL || username == NULL || password == NULL ||
        out_cookie_header == NULL || out_cookie_header_size == 0 || now == (time_t)-1 ||
        sodium_init() < 0 || login_identity_hash(client_ip, username, identity) != 0 ||
        open_database(&database) != 0) {
        set_error("Anmeldung konnte nicht verarbeitet werden");
        goto cleanup;
    }

    if (login_rate_state(database, identity, now, &blocked) != 0) goto cleanup;
    if (blocked) {
        result = ADMIN_SESSION_LOGIN_RATE_LIMITED;
        goto cleanup;
    }

    if (sqlite3_prepare_v2(database,
            "SELECT id,username,password_hash FROM admin_users WHERE username=?1 COLLATE NOCASE AND active=1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error(database, "Admin-Zugang konnte nicht geprüft werden");
        goto cleanup;
    }
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *db_hash = sqlite3_column_text(statement, 2);
        admin_id = sqlite3_column_int64(statement, 0);
        if (db_hash != NULL) {
            int hash_length = sqlite3_column_bytes(statement, 2);
            if (hash_length <= 0 || (size_t)hash_length >= sizeof(stored_hash)) {
                set_error("Gespeicherter Admin-Passworthash ist ungültig");
                goto cleanup;
            }
            memcpy(stored_hash, db_hash, (size_t)hash_length);
            stored_hash[hash_length] = '\0';
        }
    }
    sqlite3_finalize(statement);
    statement = NULL;

    /* Auch bei unbekanntem Benutzer einen echten Argon2-Hash prüfen. */
    if (stored_hash[0] == '\0') {
        char dummy[crypto_pwhash_STRBYTES];
        if (crypto_pwhash_str(dummy, "not-the-password", 16,
                              crypto_pwhash_OPSLIMIT_INTERACTIVE,
                              crypto_pwhash_MEMLIMIT_INTERACTIVE) == 0) {
            password_valid = crypto_pwhash_str_verify(dummy, password,
                    (unsigned long long)strlen(password)) == 0;
        }
        sodium_memzero(dummy, sizeof(dummy));
        password_valid = false;
    } else {
        password_valid = crypto_pwhash_str_verify(stored_hash, password,
                (unsigned long long)strlen(password)) == 0;
    }

    if (!password_valid || admin_id <= 0) {
        if (record_login_failure(database, identity, now) != 0) goto cleanup;
        result = ADMIN_SESSION_LOGIN_INVALID;
        goto cleanup;
    }

    randombytes_buf(token_bytes, sizeof(token_bytes));
    sodium_bin2hex(token, sizeof(token), token_bytes, sizeof(token_bytes));
    if (hash_text(token, token_hash) != 0 ||
        sqlite3_exec(database, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Sitzungstransaktion konnte nicht gestartet werden");
        goto cleanup;
    }

    if (sqlite3_prepare_v2(database,
            "DELETE FROM admin_sessions WHERE admin_id=?1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, admin_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "INSERT INTO admin_sessions(token_hash,admin_id,created_at,expires_at,last_activity_at) "
            "VALUES(?1,?2,?3,?4,?3);",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, token_hash, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, admin_id) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, (sqlite3_int64)now) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 4, (sqlite3_int64)now + SESSION_ABSOLUTE_SECONDS) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "UPDATE admin_users SET last_login_at=strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
            "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, admin_id) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "DELETE FROM admin_login_attempts WHERE identity_hash=?1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, identity, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_exec(database, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK ||
        build_cookie(request, token, SESSION_ABSOLUTE_SECONDS,
                     out_cookie_header, out_cookie_header_size) != 0) {
        set_error("Sitzungscookie konnte nicht erzeugt werden");
        goto cleanup;
    }
    result = ADMIN_SESSION_LOGIN_OK;
    goto cleanup;

rollback:
    set_sqlite_error(database, "Admin-Sitzung konnte nicht gespeichert werden");
    sqlite3_finalize(statement); statement = NULL;
    sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    sodium_memzero(identity, sizeof(identity));
    sodium_memzero(token_bytes, sizeof(token_bytes));
    sodium_memzero(token_hash, sizeof(token_hash));
    sodium_memzero(token, sizeof(token));
    sodium_memzero(stored_hash, sizeof(stored_hash));
    return result;
}

bool admin_session_authenticate(const string *request, admin_session *out_session)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char token_hash[SESSION_HASH_BYTES];
    char token[ADMIN_SESSION_TOKEN_HEX_SIZE];
    time_t now = time(NULL);
    int64_t created_at;
    int64_t expires_at;
    int64_t last_activity;
    bool valid = false;

    if (out_session == NULL || now == (time_t)-1 ||
        !cookie_token(request, token) || hash_text(token, token_hash) != 0 ||
        open_database(&database) != 0) goto cleanup;

    if (sqlite3_prepare_v2(database,
            "SELECT s.admin_id,u.username,s.created_at,s.expires_at,s.last_activity_at "
            "FROM admin_sessions s JOIN admin_users u ON u.id=s.admin_id "
            "WHERE s.token_hash=?1 AND u.active=1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, token_hash, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_ROW) goto cleanup;

    created_at = sqlite3_column_int64(statement, 2);
    expires_at = sqlite3_column_int64(statement, 3);
    last_activity = sqlite3_column_int64(statement, 4);
    if (created_at > (int64_t)now || expires_at <= (int64_t)now ||
        last_activity + SESSION_IDLE_SECONDS <= (int64_t)now) goto cleanup;

    memset(out_session, 0, sizeof(*out_session));
    out_session->admin_id = sqlite3_column_int64(statement, 0);
    {
        const unsigned char *database_username = sqlite3_column_text(statement, 1);
        int username_length = sqlite3_column_bytes(statement, 1);
        if (database_username == NULL || username_length <= 0 ||
            (size_t)username_length >= sizeof(out_session->username)) goto cleanup;
        memcpy(out_session->username, database_username, (size_t)username_length);
        out_session->username[username_length] = '\0';
    }
    snprintf(out_session->token, sizeof(out_session->token), "%s", token);
    if (derive_csrf(token, out_session->csrf_token) != 0) goto cleanup;
    sqlite3_finalize(statement); statement = NULL;

    if (sqlite3_prepare_v2(database,
            "UPDATE admin_sessions SET last_activity_at=?1 WHERE token_hash=?2;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 1, (sqlite3_int64)now) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 2, token_hash, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) goto cleanup;
    valid = true;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    sodium_memzero(token_hash, sizeof(token_hash));
    sodium_memzero(token, sizeof(token));
    if (!valid && out_session != NULL) sodium_memzero(out_session, sizeof(*out_session));
    return valid;
}

int admin_session_logout(const string *request)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char token_hash[SESSION_HASH_BYTES];
    char token[ADMIN_SESSION_TOKEN_HEX_SIZE];
    int result = -1;

    if (!cookie_token(request, token)) return 0;
    if (hash_text(token, token_hash) != 0 || open_database(&database) != 0) goto cleanup;
    if (sqlite3_prepare_v2(database,
            "DELETE FROM admin_sessions WHERE token_hash=?1;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, token_hash, SESSION_HASH_BYTES, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error(database, "Sitzung konnte nicht beendet werden");
        goto cleanup;
    }
    result = 0;

cleanup:
    sqlite3_finalize(statement);
    sqlite3_close_v2(database);
    sodium_memzero(token_hash, sizeof(token_hash));
    sodium_memzero(token, sizeof(token));
    return result;
}

bool admin_session_validate_csrf(
        const admin_session *session,
        const char *received_token
)
{
    if (session == NULL || received_token == NULL ||
        strnlen(received_token, ADMIN_SESSION_CSRF_HEX_SIZE + 1) != ADMIN_SESSION_CSRF_HEX_SIZE - 1)
        return false;
    return sodium_memcmp(session->csrf_token, received_token,
                         ADMIN_SESSION_CSRF_HEX_SIZE - 1) == 0;
}

int admin_session_build_clear_cookie(
        const string *request,
        char *out_cookie_header,
        size_t out_cookie_header_size
)
{
    return build_cookie(request, "", 0, out_cookie_header, out_cookie_header_size);
}

int admin_session_cleanup(void)
{
    sqlite3 *database = NULL;
    time_t now = time(NULL);
    char sql[512];
    int written;

    if (now == (time_t)-1 || open_database(&database) != 0) return -1;
    written = snprintf(sql, sizeof(sql),
            "DELETE FROM admin_sessions WHERE expires_at<=%lld OR last_activity_at<=%lld;"
            "DELETE FROM admin_login_attempts WHERE window_started_at<=%lld AND blocked_until<=%lld;",
            (long long)now, (long long)now - SESSION_IDLE_SECONDS,
            (long long)now - 86400, (long long)now);
    if (written < 0 || (size_t)written >= sizeof(sql) ||
        sqlite3_exec(database, sql, NULL, NULL, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Alte Admin-Sitzungen konnten nicht bereinigt werden");
        sqlite3_close_v2(database);
        return -1;
    }
    sqlite3_close_v2(database);
    return 0;
}
