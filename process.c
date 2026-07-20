//
// Created by Marlon on 11.07.26.
//

#include "auth.h"
#include "process.h"
#include "booking.h"
#include "form_urlencoded.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sodium.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_METHOD_LENGTH 15
#define MAX_PATH_LENGTH 512
#define SETUP_USERNAME_SIZE 128
#define SETUP_PASSWORD_SIZE 513
#define SETUP_CSRF_TOKEN_BYTES 32
#define SETUP_CSRF_TOKEN_HEX_SIZE (SETUP_CSRF_TOKEN_BYTES * 2 + 1)

#ifndef SERVER_DOCUMENT_ROOT
#define SERVER_DOCUMENT_ROOT "public"
#endif

#define DOCUMENT_ROOT SERVER_DOCUMENT_ROOT

/*
 * Content-Type anhand der Dateiendung bestimmen.
 * Das reicht erstmal für unsere eigenen statischen Dateien.
 */
static const char *guess_content_type(const char *path)
{
    const char *extension = strrchr(path, '.');

    if (extension == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(extension, ".html") == 0) {
        return "text/html; charset=utf-8";
    }

    if (strcmp(extension, ".css") == 0) {
        return "text/css; charset=utf-8";
    }

    if (strcmp(extension, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }

    if (strcmp(extension, ".png") == 0) {
        return "image/png";
    }

    if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
        return "image/jpeg";
    }

    if (strcmp(extension, ".svg") == 0) {
        return "image/svg+xml";
    }

    if (strcmp(extension, ".ico") == 0) {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

static void append_content_length(string *response, size_t body_length)
{
    char header[64];

    int written = snprintf(header, sizeof(header), "Content-Length: %zu\r\n", body_length);

    if (written > 0) {
        str_cat(response, header, (size_t)written);
    }
}

/*
 * Baut eine Response mit beliebigen Bytes.
 * Dadurch funktioniert das später auch mit Bildern.
 */
static string *build_response_bytes(
        const char *status,
        const char *content_type,
        const char *extra_headers,
        const char *body,
        size_t body_length,
        bool send_body
)
{
    string *response = _new_string();

    str_cat_cstr(response, "HTTP/1.1 ");
    str_cat_cstr(response, status);
    str_cat_cstr(response, "\r\n");

    str_cat_cstr(response, "Content-Type: ");
    str_cat_cstr(response, content_type);
    str_cat_cstr(response, "\r\n");

    append_content_length(response, body_length);

    if (extra_headers != NULL) {
        str_cat_cstr(response, extra_headers);
    }

    str_cat_cstr(response, "Connection: close\r\n");
    str_cat_cstr(response, "\r\n");

    if (send_body && body != NULL && body_length > 0) {
        str_cat(response, body, body_length);
    }

    return response;
}

static string *build_response_text(
        const char *status,
        const char *content_type,
        const char *extra_headers,
        const char *body,
        bool send_body
)
{
    size_t body_length = 0;

    if (body != NULL) {
        body_length = strlen(body);
    }

    return build_response_bytes(
            status,
            content_type,
            extra_headers,
            body,
            body_length,
            send_body
    );
}

/*
 * Holt Methode und Pfad aus der ersten Zeile.
 *
 * Beispiel:
 * GET /style.css HTTP/1.1
 */
static bool parse_request_line(
        const string *request,
        char *method,
        size_t method_size,
        char *path,
        size_t path_size
)
{
    const char *data;
    size_t length;
    size_t position = 0;
    size_t method_length = 0;
    size_t path_length = 0;
    size_t version_start;
    size_t version_length;

    if (request == NULL || method == NULL || path == NULL) {
        return false;
    }

    data = get_const_char_str(request);
    length = get_length(request);

    if (data == NULL || length == 0 || method_size == 0 || path_size == 0) {
        return false;
    }

    while (position < length && data[position] != ' ') {
        if (data[position] == '\r' || data[position] == '\n' ||
            method_length + 1 >= method_size) {
            return false;
        }

        method[method_length++] = data[position++];
    }

    if (method_length == 0 || position >= length || data[position] != ' ') {
        return false;
    }

    method[method_length] = '\0';

    while (position < length && data[position] == ' ') {
        position++;
    }

    while (position < length && data[position] != ' ') {
        if (data[position] == '\r' || data[position] == '\n' ||
            path_length + 1 >= path_size) {
            return false;
        }

        path[path_length++] = data[position++];
    }

    if (path_length == 0 || position >= length || data[position] != ' ') {
        return false;
    }

    path[path_length] = '\0';

    while (position < length && data[position] == ' ') {
        position++;
    }

    version_start = position;

    while (position < length && data[position] != '\r' && data[position] != '\n') {
        if (data[position] == ' ' || data[position] == '\t') {
            return false;
        }
        position++;
    }

    version_length = position - version_start;

    if (!((version_length == strlen("HTTP/1.0") &&
           memcmp(data + version_start, "HTTP/1.0", version_length) == 0) ||
          (version_length == strlen("HTTP/1.1") &&
           memcmp(data + version_start, "HTTP/1.1", version_length) == 0))) {
        return false;
    }

    if (position >= length) {
        return false;
    }

    if (data[position] == '\r') {
        return position + 1 < length && data[position + 1] == '\n';
    }

    return data[position] == '\n';
}

/*
 * Entfernt Query-Parameter aus dem Pfad.
 *
 * Aus:
 * /kontakt?foo=bar
 *
 * wird:
 * /kontakt
 */
static void remove_query_string(char *path)
{
    char *question_mark = strchr(path, '?');

    if (question_mark != NULL) {
        *question_mark = '\0';
    }
}

/*
 * Wandelt schöne Routen in echte Dateien um.
 */
static const char *map_route_to_file(const char *path)
{
    if (strcmp(path, "/") == 0) {
        return "/index.html";
    }

    if (strcmp(path, "/leistungen") == 0) {
        return "/leistungen.html";
    }

    if (strcmp(path, "/preise") == 0) {
        return "/preise.html";
    }

    if (strcmp(path, "/kontakt") == 0) {
        return "/kontakt.html";
    }

    if (strcmp(path, "/impressum") == 0) {
        return "/impressum.html";
    }

    if (strcmp(path, "/datenschutz") == 0) {
        return "/datenschutz.html";
    }

    return path;
}

static bool starts_with_path(const char *path, const char *prefix)
{
    size_t prefix_length = strlen(prefix);

    if (strncmp(path, prefix, prefix_length) != 0) {
        return false;
    }

    return path[prefix_length] == '\0' || path[prefix_length] == '/';
}

static bool contains_parent_path_segment(const char *path)
{
    size_t position = 0;

    if (path == NULL) {
        return false;
    }

    while (path[position] != '\0') {
        size_t segment_start;
        size_t segment_length;

        while (path[position] == '/') {
            position++;
        }

        segment_start = position;

        while (path[position] != '\0' && path[position] != '/') {
            position++;
        }

        segment_length = position - segment_start;

        if (segment_length == 2 &&
            path[segment_start] == '.' &&
            path[segment_start + 1] == '.') {
            return true;
        }
    }

    return false;
}

/*
 * Datei vollständig einlesen.
 *
 * Für den Anfang ist das okay.
 * Später könnte man große Dateien stückweise senden.
 */
static char *read_file(const char *path, size_t *out_length)
{
    FILE *file;
    long file_size;
    char *buffer;
    size_t read_bytes;

    if (out_length == NULL) {
        return NULL;
    }

    *out_length = 0;

    file = fopen(path, "rb");

    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    file_size = ftell(file);

    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    rewind(file);

    buffer = malloc((size_t)file_size + 1);

    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_bytes = fread(buffer, 1, (size_t)file_size, file);

    if (read_bytes != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[read_bytes] = '\0';
    *out_length = read_bytes;

    fclose(file);
    return buffer;
}

static string *handle_bad_request(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>400 Bad Request</title></head>\n"
            "<body><h1>400 Bad Request</h1><p>Der Request konnte nicht gelesen werden.</p></body>\n"
            "</html>\n";

    return build_response_text("400 Bad Request", "text/html; charset=utf-8", NULL, body, send_body);
}

static string *handle_method_not_allowed(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>405 Method Not Allowed</title></head>\n"
            "<body><h1>405 Method Not Allowed</h1><p>Erlaubt sind aktuell GET, HEAD und POST.</p></body>\n"
            "</html>\n";

    return build_response_text(
            "405 Method Not Allowed",
            "text/html; charset=utf-8",
            "Allow: GET, HEAD, POST\r\n",
            body,
            send_body
    );
}

static string *handle_not_found(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>404 Nicht gefunden</title></head>\n"
            "<body><h1>404 Nicht gefunden</h1><p>Diese Seite gibt es nicht.</p></body>\n"
            "</html>\n";

    return build_response_text("404 Not Found", "text/html; charset=utf-8", NULL, body, send_body);
}

static string *handle_forbidden(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>403 Forbidden</title></head>\n"
            "<body><h1>403 Forbidden</h1><p>Auf diese Datei darf nicht zugegriffen werden.</p></body>\n"
            "</html>\n";

    return build_response_text("403 Forbidden", "text/html; charset=utf-8", NULL, body, send_body);
}

static string *handle_internal_error(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>500 Internal Server Error</title></head>\n"
            "<body><h1>500 Internal Server Error</h1><p>Beim Server ist ein Fehler aufgetreten.</p></body>\n"
            "</html>\n";

    return build_response_text("500 Internal Server Error", "text/html; charset=utf-8", NULL, body, send_body);
}

static string *handle_unauthorized(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><title>401 Unauthorized</title></head>\n"
            "<body><h1>401 Unauthorized</h1><p>Für diesen Bereich musst du dich anmelden.</p></body>\n"
            "</html>\n";

    return build_response_text(
            "401 Unauthorized",
            "text/html; charset=utf-8",
            "WWW-Authenticate: Basic realm=\"Styles 4 Dogs Admin\"\r\n",
            body,
            send_body
    );
}

/*
 * Liefert eine Datei aus dem public-Ordner aus.
 *
 * Wichtig:
 * realpath() schützt uns davor, dass jemand mit ../ aus public ausbricht.
 */
static string *serve_static_file(const char *request_path, bool send_body)
{
    char document_root_real[PATH_MAX];
    char candidate_path[PATH_MAX];
    char file_real[PATH_MAX];
    const char *mapped_path;
    const char *content_type;
    struct stat file_stat;
    char *file_content;
    size_t file_length;
    string *response;

    if (realpath(DOCUMENT_ROOT, document_root_real) == NULL) {
        return handle_internal_error(send_body);
    }

    mapped_path = map_route_to_file(request_path);

    if (mapped_path[0] != '/') {
        return handle_bad_request(send_body);
    }

    if (contains_parent_path_segment(mapped_path)) {
        return handle_forbidden(send_body);
    }

    if (snprintf(candidate_path, sizeof(candidate_path), "%s%s", document_root_real, mapped_path)
        >= (int)sizeof(candidate_path)) {
        return handle_bad_request(send_body);
    }

    if (realpath(candidate_path, file_real) == NULL) {
        return handle_not_found(send_body);
    }

    if (!starts_with_path(file_real, document_root_real)) {
        return handle_forbidden(send_body);
    }

    if (stat(file_real, &file_stat) != 0) {
        return handle_not_found(send_body);
    }

    if (!S_ISREG(file_stat.st_mode)) {
        return handle_forbidden(send_body);
    }

    file_content = read_file(file_real, &file_length);

    if (file_content == NULL) {
        return handle_internal_error(send_body);
    }

    content_type = guess_content_type(file_real);

    response = build_response_bytes(
            "200 OK",
            content_type,
            NULL,
            file_content,
            file_length,
            send_body
    );

    free(file_content);

    return response;
}

static const char *setup_security_headers(void)
{
    return
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Security-Policy: default-src 'self'; "
            "style-src 'self'; form-action 'self'; base-uri 'none'; "
            "frame-ancestors 'none'\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n";
}

static bool get_setup_csrf_token(
        char *out_token,
        size_t out_token_size
)
{
    static bool initialized = false;
    static bool token_available = false;
    static char token[SETUP_CSRF_TOKEN_HEX_SIZE];

    if (out_token == NULL || out_token_size < sizeof(token)) {
        return false;
    }

    if (!initialized) {
        unsigned char random_token[SETUP_CSRF_TOKEN_BYTES];

        initialized = true;

        if (sodium_init() < 0) {
            sodium_memzero(random_token, sizeof(random_token));
            return false;
        }

        randombytes_buf(random_token, sizeof(random_token));

        if (sodium_bin2hex(
                token,
                sizeof(token),
                random_token,
                sizeof(random_token)) == NULL) {
            sodium_memzero(random_token, sizeof(random_token));
            sodium_memzero(token, sizeof(token));
            return false;
        }

        sodium_memzero(random_token, sizeof(random_token));
        token_available = true;
    }

    if (!token_available) {
        return false;
    }

    memcpy(out_token, token, sizeof(token));
    return true;
}

static bool setup_csrf_token_matches(const char *received_token)
{
    char expected_token[SETUP_CSRF_TOKEN_HEX_SIZE];
    size_t received_length;
    bool matches;

    if (received_token == NULL) {
        return false;
    }

    received_length = strnlen(
            received_token,
            SETUP_CSRF_TOKEN_HEX_SIZE + 1);

    if (received_length != SETUP_CSRF_TOKEN_HEX_SIZE - 1) {
        return false;
    }

    if (!get_setup_csrf_token(expected_token, sizeof(expected_token))) {
        return false;
    }

    matches = sodium_memcmp(
            received_token,
            expected_token,
            SETUP_CSRF_TOKEN_HEX_SIZE - 1) == 0;

    sodium_memzero(expected_token, sizeof(expected_token));
    return matches;
}

static bool passwords_match(
        const char *password,
        const char *password_repeat
)
{
    size_t password_length;
    size_t repeated_length;

    if (password == NULL || password_repeat == NULL) {
        return false;
    }

    password_length = strnlen(password, SETUP_PASSWORD_SIZE);
    repeated_length = strnlen(password_repeat, SETUP_PASSWORD_SIZE);

    if (password_length != repeated_length) {
        return false;
    }

    return sodium_memcmp(
            password,
            password_repeat,
            password_length) == 0;
}

static void clear_request_body(string *request)
{
    char *data;
    size_t length;

    if (request == NULL) {
        return;
    }

    data = get_char_str(request);
    length = get_length(request);

    if (data == NULL) {
        return;
    }

    for (size_t index = 0; index + 3 < length; index++) {
        if (data[index] == '\r' &&
            data[index + 1] == '\n' &&
            data[index + 2] == '\r' &&
            data[index + 3] == '\n') {
            sodium_memzero(
                    data + index + 4,
                    length - (index + 4));
            return;
        }
    }

    for (size_t index = 0; index + 1 < length; index++) {
        if (data[index] == '\n' && data[index + 1] == '\n') {
            sodium_memzero(
                    data + index + 2,
                    length - (index + 2));
            return;
        }
    }
}

static string *handle_admin_setup_page(bool send_body)
{
    char csrf_token[SETUP_CSRF_TOKEN_HEX_SIZE];
    string *body;
    string *response;

    if (admin_auth_exists()) {
        return handle_not_found(send_body);
    }

    if (!get_setup_csrf_token(csrf_token, sizeof(csrf_token))) {
        return handle_internal_error(send_body);
    }

    body = _new_string();

    str_cat_cstr(body,
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <meta name=\"robots\" content=\"noindex,nofollow\">\n"
            "    <title>Admin einrichten | Styles 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <main class=\"page\">\n"
            "        <section class=\"card form-card\">\n"
            "            <p class=\"eyebrow\">Ersteinrichtung</p>\n"
            "            <h1>Admin-Zugang erstellen</h1>\n"
            "            <p>Diese Seite funktioniert nur, solange noch kein Admin-Zugang existiert.</p>\n"
            "            <form class=\"form\" method=\"post\" action=\"/setup/admin\" autocomplete=\"off\">\n"
            "                <input type=\"hidden\" name=\"csrf_token\" value=\"");

    str_cat_cstr(body, csrf_token);

    str_cat_cstr(body,
            "\">\n"
            "                <label for=\"username\">Benutzername</label>\n"
            "                <input id=\"username\" name=\"username\" type=\"text\" "
            "maxlength=\"127\" pattern=\"[A-Za-z0-9._@-]+\" "
            "autocomplete=\"username\" required>\n"
            "                <label for=\"password\">Passwort</label>\n"
            "                <input id=\"password\" name=\"password\" type=\"password\" "
            "minlength=\"12\" maxlength=\"512\" "
            "autocomplete=\"new-password\" required>\n"
            "                <label for=\"password_repeat\">Passwort wiederholen</label>\n"
            "                <input id=\"password_repeat\" name=\"password_repeat\" "
            "type=\"password\" minlength=\"12\" maxlength=\"512\" "
            "autocomplete=\"new-password\" required>\n"
            "                <button class=\"button\" type=\"submit\">Admin-Zugang erstellen</button>\n"
            "            </form>\n"
            "        </section>\n"
            "    </main>\n"
            "</body>\n"
            "</html>\n");

    response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            setup_security_headers(),
            get_const_char_str(body),
            get_length(body),
            send_body);

    sodium_memzero(csrf_token, sizeof(csrf_token));
    free_str(body);

    return response;
}

static string *handle_admin_setup_error(
        const char *message,
        bool send_body
)
{
    string *body = _new_string();
    string *response;

    str_cat_cstr(body,
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>Einrichtung fehlgeschlagen | Styles 4 Dogs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head>"
            "<body><main class=\"page\"><section class=\"card form-card\">"
            "<p class=\"eyebrow\">Ersteinrichtung</p>"
            "<h1>Admin-Zugang konnte nicht erstellt werden</h1><p>");
    str_cat_cstr(body, message);
    str_cat_cstr(body,
            "</p><p><a class=\"button button-secondary\" href=\"/setup/admin\">"
            "Zurück zur Einrichtung</a></p></section></main></body></html>");

    response = build_response_bytes(
            "400 Bad Request",
            "text/html; charset=utf-8",
            setup_security_headers(),
            get_const_char_str(body),
            get_length(body),
            send_body);

    free_str(body);
    return response;
}

static string *handle_admin_setup_created(bool send_body)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>Admin erstellt | Styles 4 Dogs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head>"
            "<body><main class=\"page\"><section class=\"card form-card\">"
            "<p class=\"eyebrow\">Einrichtung abgeschlossen</p>"
            "<h1>Admin-Zugang wurde erstellt</h1>"
            "<p>Die Einrichtungsseite ist ab jetzt automatisch deaktiviert.</p>"
            "<p><a class=\"button\" href=\"/admin/bookings\">Zum Adminbereich</a></p>"
            "</section></main></body></html>";

    return build_response_text(
            "201 Created",
            "text/html; charset=utf-8",
            setup_security_headers(),
            body,
            send_body);
}

static string *handle_admin_setup_post(string *request)
{
    char csrf_token[SETUP_CSRF_TOKEN_HEX_SIZE] = {0};
    char username[SETUP_USERNAME_SIZE] = {0};
    char password[SETUP_PASSWORD_SIZE] = {0};
    char password_repeat[SETUP_PASSWORD_SIZE] = {0};
    form_value_result csrf_result;
    form_value_result username_result;
    form_value_result password_result;
    form_value_result repeat_result;
    int create_result;
    string *response;

    if (admin_auth_exists()) {
        clear_request_body(request);
        return handle_not_found(true);
    }

    csrf_result = form_urlencoded_get(
            request,
            "csrf_token",
            csrf_token,
            sizeof(csrf_token));

    username_result = form_urlencoded_get(
            request,
            "username",
            username,
            sizeof(username));

    password_result = form_urlencoded_get(
            request,
            "password",
            password,
            sizeof(password));

    repeat_result = form_urlencoded_get(
            request,
            "password_repeat",
            password_repeat,
            sizeof(password_repeat));

    if (csrf_result != FORM_VALUE_OK ||
        username_result != FORM_VALUE_OK ||
        password_result != FORM_VALUE_OK ||
        repeat_result != FORM_VALUE_OK) {
        response = handle_admin_setup_error(
                "Die Formulardaten sind unvollständig oder ungültig.",
                true);
        goto cleanup;
    }

    if (!setup_csrf_token_matches(csrf_token)) {
        response = handle_admin_setup_error(
                "Die Sicherheitsprüfung ist fehlgeschlagen. Bitte lade die Seite neu.",
                true);
        goto cleanup;
    }

    if (!passwords_match(password, password_repeat)) {
        response = handle_admin_setup_error(
                "Die beiden Passwörter stimmen nicht überein.",
                true);
        goto cleanup;
    }

    create_result = create_admin_auth(username, password);

    if (create_result == 0) {
        response = handle_admin_setup_created(true);
        goto cleanup;
    }

    if (create_result == EEXIST) {
        response = handle_not_found(true);
        goto cleanup;
    }

    if (create_result == EINVAL) {
        response = handle_admin_setup_error(
                "Der Benutzername oder das Passwort erfüllt die Vorgaben nicht.",
                true);
        goto cleanup;
    }

    response = handle_internal_error(true);

cleanup:
    clear_request_body(request);
    sodium_memzero(csrf_token, sizeof(csrf_token));
    sodium_memzero(username, sizeof(username));
    sodium_memzero(password, sizeof(password));
    sodium_memzero(password_repeat, sizeof(password_repeat));

    return response;
}

static string *handle_booking_created(void)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>Anfrage gesendet | Styles 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <header class=\"site-header\">\n"
            "        <div class=\"container nav-wrap\">\n"
            "            <a class=\"brand\" href=\"/\">"
            "<span class=\"brand-mark\">S4D</span>"
            "<span>Styles 4 Dogs</span></a>\n"
            "            <nav class=\"site-nav\" aria-label=\"Hauptnavigation\">"
            "<a href=\"/\">Start</a>"
            "<a href=\"/leistungen\">Leistungen</a>"
            "<a href=\"/preise\">Preise</a>"
            "<a href=\"/kontakt\">Kontakt</a>"
            "</nav>\n"
            "        </div>\n"
            "    </header>\n"
            "    <main class=\"page\">\n"
            "        <section class=\"card\">\n"
            "            <p class=\"eyebrow\">Anfrage eingegangen</p>\n"
            "            <h1>Vielen Dank!</h1>\n"
            "            <p>Deine Anfrage wurde gespeichert. "
            "Sie ist noch keine automatische Terminbestätigung. "
            "Wir melden uns persönlich bei dir.</p>\n"
            "            <p><a class=\"button\" href=\"/\">"
            "Zurück zur Startseite</a></p>\n"
            "        </section>\n"
            "    </main>\n"
            "    <footer class=\"site-footer\">"
            "<div class=\"container footer-bottom\">"
            "<small>&copy; 2026 Styles 4 Dogs.</small>"
            "</div></footer>\n"
            "</body>\n"
            "</html>\n";

    return build_response_text(
            "201 Created",
            "text/html; charset=utf-8",
            NULL,
            body,
            true
    );
}

static string *handle_booking(string *request)
{
    booking_request booking;

    if (!parse_booking_request(request, &booking)) {
        return handle_bad_request(true);
    }

    if (save_booking_request(&booking) != 0) {
        return handle_internal_error(true);
    }

    return handle_booking_created();
}

static string *handle_admin_bookings(bool send_body)
{
    string *body = build_booking_admin_page();

    if (body == NULL) {
        return handle_internal_error(send_body);
    }

    string *response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            NULL,
            get_char_str(body),
            get_length(body),
            send_body
    );

    free_str(body);

    return response;
}

string *process(string *request)
{
    char method[MAX_METHOD_LENGTH + 1];
    char path[MAX_PATH_LENGTH + 1];
    bool send_body = true;

    if (!parse_request_line(request, method, sizeof(method), path, sizeof(path))) {
        return handle_bad_request(true);
    }

    remove_query_string(path);

    if (strcmp(method, "HEAD") == 0) {
        send_body = false;
    } else if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
        return handle_method_not_allowed(true);
    }

    if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/booking") == 0) {
            return handle_booking(request);
        }

        if (strcmp(path, "/setup/admin") == 0) {
            return handle_admin_setup_post(request);
        }

        return handle_not_found(true);
    }

    /*
     * Ab hier bleiben nur noch GET und HEAD übrig.
     * Die Admin-Seite schützen wir mit Basic Auth.
     */
    if (strcmp(path, "/setup/admin") == 0) {
        return handle_admin_setup_page(send_body);
    }

    if (strcmp(path, "/admin/bookings") == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        return handle_admin_bookings(send_body);
    }

    return serve_static_file(path, send_body);
}