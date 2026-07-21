//
// Created by Marlon on 11.07.26.
//

#include "admin_calendar.h"
#include "admin_appointments.h"
#include "auth.h"
#include "process.h"
#include "booking.h"
#include "booking_database.h"
#include "calendar_public.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "form_urlencoded.h"
#include "server_config.h"
#include "notification_queue.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sodium.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_METHOD_LENGTH 15
#define MAX_PATH_LENGTH 512
#define SETUP_USERNAME_SIZE 128
#define SETUP_PASSWORD_SIZE 513
#define FORM_CSRF_TOKEN_BYTES 32
#define FORM_CSRF_TOKEN_HEX_SIZE (FORM_CSRF_TOKEN_BYTES * 2 + 1)

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
 * Trennt den Query-String vom Pfad. Der Query-Zeiger zeigt anschließend
 * in denselben Pfadpuffer und ist nur so lange wie dieser gültig.
 */
static void split_query_string(
        char *path,
        const char **out_query,
        size_t *out_query_length
)
{
    char *question_mark;

    if (out_query != NULL) {
        *out_query = NULL;
    }

    if (out_query_length != NULL) {
        *out_query_length = 0;
    }

    if (path == NULL) {
        return;
    }

    question_mark = strchr(path, '?');

    if (question_mark == NULL) {
        return;
    }

    *question_mark = '\0';

    if (out_query != NULL) {
        *out_query = question_mark + 1;
    }

    if (out_query_length != NULL) {
        *out_query_length = strlen(question_mark + 1);
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
            "WWW-Authenticate: Basic realm=\"Styles 4 Dogs Admin\"\r\n"
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n",
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

    if (realpath(server_config_document_root(), document_root_real) == NULL) {
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

static bool get_form_csrf_token(
        char *out_token,
        size_t out_token_size
)
{
    static bool initialized = false;
    static bool token_available = false;
    static char token[FORM_CSRF_TOKEN_HEX_SIZE];

    if (out_token == NULL || out_token_size < sizeof(token)) {
        return false;
    }

    if (!initialized) {
        unsigned char random_token[FORM_CSRF_TOKEN_BYTES];

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

static bool form_csrf_token_matches(const char *received_token)
{
    char expected_token[FORM_CSRF_TOKEN_HEX_SIZE];
    size_t received_length;
    bool matches;

    if (received_token == NULL) {
        return false;
    }

    received_length = strnlen(
            received_token,
            FORM_CSRF_TOKEN_HEX_SIZE + 1);

    if (received_length != FORM_CSRF_TOKEN_HEX_SIZE - 1) {
        return false;
    }

    if (!get_form_csrf_token(expected_token, sizeof(expected_token))) {
        return false;
    }

    matches = sodium_memcmp(
            received_token,
            expected_token,
            FORM_CSRF_TOKEN_HEX_SIZE - 1) == 0;

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
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE];
    string *body;
    string *response;

    if (admin_auth_exists()) {
        return handle_not_found(send_body);
    }

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token))) {
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
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE] = {0};
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

    if (!form_csrf_token_matches(csrf_token)) {
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

static const char *calendar_api_headers(void)
{
    return
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Referrer-Policy: no-referrer\r\n";
}

static string *handle_calendar_api_error(
        const char *status,
        const char *code,
        bool send_body
)
{
    string *body = _new_string();
    string *response;

    if (body == NULL) {
        return handle_internal_error(send_body);
    }

    str_cat_cstr(body, "{\"error\":\"");
    str_cat_cstr(body, code == NULL ? "calendar_error" : code);
    str_cat_cstr(body, "\"}");

    response = build_response_bytes(
            status,
            "application/json; charset=utf-8",
            calendar_api_headers(),
            get_const_char_str(body),
            get_length(body),
            send_body);
    free_str(body);
    return response;
}

static string *handle_calendar_services(bool send_body)
{
    string *json = NULL;
    calendar_public_result result = calendar_public_build_services_json(&json);
    string *response;

    if (result != CALENDAR_PUBLIC_OK || json == NULL) {
        fprintf(stderr, "Leistungs-API fehlgeschlagen: %s\n", calendar_public_last_error());
        return handle_calendar_api_error(
                "500 Internal Server Error",
                "calendar_unavailable",
                send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "application/json; charset=utf-8",
            calendar_api_headers(),
            get_const_char_str(json),
            get_length(json),
            send_body);
    free_str(json);
    return response;
}

static string *handle_calendar_availability(
        const char *query,
        size_t query_length,
        bool send_body
)
{
    string *json = NULL;
    calendar_public_result result = calendar_public_build_availability_json(
            query,
            query_length,
            &json);
    string *response;

    if (result == CALENDAR_PUBLIC_BAD_REQUEST) {
        return handle_calendar_api_error("400 Bad Request", "invalid_query", send_body);
    }
    if (result == CALENDAR_PUBLIC_NOT_FOUND) {
        return handle_calendar_api_error("404 Not Found", "service_not_found", send_body);
    }
    if (result != CALENDAR_PUBLIC_OK || json == NULL) {
        fprintf(stderr, "Verfügbarkeits-API fehlgeschlagen: %s\n", calendar_public_last_error());
        return handle_calendar_api_error(
                "500 Internal Server Error",
                "calendar_unavailable",
                send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "application/json; charset=utf-8",
            calendar_api_headers(),
            get_const_char_str(json),
            get_length(json),
            send_body);
    free_str(json);
    return response;
}

static string *handle_booking_created(bool confirmed)
{
    const char *body_pending =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Terminanfrage gesendet | Styles 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"></head>\n"
            "<body><main class=\"page\"><section class=\"card\">"
            "<p class=\"eyebrow\">Termin vorläufig reserviert</p><h1>Vielen Dank!</h1>"
            "<p>Deine Terminanfrage wurde gespeichert und der gewählte Zeitraum vorläufig reserviert. "
            "Der Termin wird verbindlich, sobald der Salon ihn annimmt.</p>"
            "<p><a class=\"button\" href=\"/\">Zurück zur Startseite</a></p>"
            "</section></main></body></html>";
    const char *body_confirmed =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Termin bestätigt | Styles 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"></head>\n"
            "<body><main class=\"page\"><section class=\"card\">"
            "<p class=\"eyebrow\">Termin automatisch bestätigt</p><h1>Dein Termin ist eingetragen.</h1>"
            "<p>Der ausgewählte Zeitraum wurde verbindlich reserviert. Der Salon meldet sich bei Bedarf über deinen gewählten Kontaktweg.</p>"
            "<p><a class=\"button\" href=\"/\">Zurück zur Startseite</a></p>"
            "</section></main></body></html>";

    return build_response_text(
            "201 Created",
            "text/html; charset=utf-8",
            "Cache-Control: no-store\r\n",
            confirmed ? body_confirmed : body_pending,
            true);
}

static string *handle_booking_unavailable(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Termin nicht mehr verfügbar | Styles 4 Dogs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
            "<main class=\"page\"><section class=\"card\">"
            "<p class=\"eyebrow\">Termin nicht verfügbar</p>"
            "<h1>Dieser Zeitraum wurde inzwischen vergeben.</h1>"
            "<p>Bitte kehre zum Kalender zurück und wähle einen anderen freien Termin.</p>"
            "<p><a class=\"button\" href=\"/kontakt\">Anderen Termin wählen</a></p>"
            "</section></main></body></html>";

    return build_response_text(
            "409 Conflict",
            "text/html; charset=utf-8",
            "Cache-Control: no-store\r\n",
            body,
            true);
}

static string *handle_booking(string *request)
{
    booking_request booking;
    int64_t booking_id = 0;
    calendar_public_result result;

    if (!parse_booking_request(request, &booking)) {
        clear_request_body(request);
        return handle_bad_request(true);
    }

    result = calendar_public_reserve_booking(&booking, &booking_id);
    clear_request_body(request);

    if (result == CALENDAR_PUBLIC_OK) {
        if (notification_queue_enqueue_booking_event(
                booking_id,
                "booking_received") != 0) {
            fprintf(stderr, "Buchungsbestätigung konnte nicht eingereiht werden: %s\n",
                    notification_queue_last_error());
        }
        return handle_booking_created(false);
    }
    if (result == CALENDAR_PUBLIC_CONFIRMED) {
        if (notification_queue_enqueue_booking_event(
                booking_id,
                "booking_confirmed") != 0) {
            fprintf(stderr, "Terminbestätigung konnte nicht eingereiht werden: %s\n",
                    notification_queue_last_error());
        }
        return handle_booking_created(true);
    }
    if (result == CALENDAR_PUBLIC_UNAVAILABLE) {
        return handle_booking_unavailable();
    }
    if (result == CALENDAR_PUBLIC_BAD_REQUEST || result == CALENDAR_PUBLIC_NOT_FOUND) {
        return handle_bad_request(true);
    }

    fprintf(stderr, "Terminanfrage konnte nicht reserviert werden: %s\n", calendar_public_last_error());
    return handle_internal_error(true);
}

static const char *admin_security_headers(void)
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

static string *handle_admin_bookings(
        bool send_body,
        const booking_admin_filter *filter
)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE];
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    string *body;
    string *response;

    if (calendar_database_get_settings(&settings) != 0 ||
        calendar_clock_now(settings.timezone, &snapshot) != 0 ||
        calendar_database_expire_pending(snapshot.now_utc) != 0) {
        fprintf(stderr, "Abgelaufene Terminanfragen konnten nicht aktualisiert werden: %s\n",
                calendar_database_last_error());
        return handle_internal_error(send_body);
    }

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token))) {
        return handle_internal_error(send_body);
    }

    body = build_booking_admin_page(csrf_token, filter);
    sodium_memzero(csrf_token, sizeof(csrf_token));

    if (body == NULL) {
        return handle_internal_error(send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            admin_security_headers(),
            get_char_str(body),
            get_length(body),
            send_body
    );

    free_str(body);
    return response;
}

static string *handle_admin_action_forbidden(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>403 Forbidden</title></head>"
            "<body><h1>403 Forbidden</h1><p>Die Sicherheitsprüfung ist fehlgeschlagen.</p>"
            "</body></html>";

    return build_response_text(
            "403 Forbidden",
            "text/html; charset=utf-8",
            admin_security_headers(),
            body,
            true);
}

static string *handle_admin_status_bad_request(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>400 Bad Request</title></head>"
            "<body><h1>400 Bad Request</h1><p>Die Statusänderung ist ungültig.</p>"
            "</body></html>";

    return build_response_text(
            "400 Bad Request",
            "text/html; charset=utf-8",
            admin_security_headers(),
            body,
            true);
}

static string *handle_admin_status_not_found(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>404 Nicht gefunden</title></head>"
            "<body><h1>404 Nicht gefunden</h1><p>Die Buchungsanfrage existiert nicht.</p>"
            "</body></html>";

    return build_response_text(
            "404 Not Found",
            "text/html; charset=utf-8",
            admin_security_headers(),
            body,
            true);
}

static string *handle_admin_status_redirect(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<title>Status gespeichert</title></head>"
            "<body><p>Der Status wurde gespeichert.</p>"
            "<p><a href=\"/admin/bookings\">Zurück zu den Buchungsanfragen</a></p>"
            "</body></html>";

    return build_response_text(
            "303 See Other",
            "text/html; charset=utf-8",
            "Location: /admin/bookings\r\n"
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Security-Policy: default-src 'self'; "
            "style-src 'self'; form-action 'self'; base-uri 'none'; "
            "frame-ancestors 'none'\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n",
            body,
            true);
}

static bool parse_positive_booking_id(
        const char *text,
        int64_t *out_booking_id
)
{
    uint64_t value = 0;

    if (text == NULL || text[0] == '\0' || out_booking_id == NULL) {
        return false;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        unsigned int digit;

        if (text[index] < '0' || text[index] > '9') {
            return false;
        }

        digit = (unsigned int)(text[index] - '0');

        if (value > ((uint64_t)INT64_MAX - digit) / 10U) {
            return false;
        }

        value = value * 10U + digit;
    }

    if (value == 0) {
        return false;
    }

    *out_booking_id = (int64_t)value;
    return true;
}

static bool is_valid_admin_booking_status(const char *status)
{
    if (status == NULL) {
        return false;
    }

    return strcmp(status, "neu") == 0 ||
           strcmp(status, "kontaktiert") == 0 ||
           strcmp(status, "erledigt") == 0;
}

static string *handle_admin_booking_status_post(string *request)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE] = {0};
    char booking_id_text[32] = {0};
    char status[32] = {0};
    form_value_result csrf_result;
    form_value_result booking_id_result;
    form_value_result status_result;
    int64_t booking_id;
    booking_status_update_result update_result;

    csrf_result = form_urlencoded_get(
            request,
            "csrf_token",
            csrf_token,
            sizeof(csrf_token));

    if (csrf_result != FORM_VALUE_OK ||
        !form_csrf_token_matches(csrf_token)) {
        sodium_memzero(csrf_token, sizeof(csrf_token));
        return handle_admin_action_forbidden();
    }

    booking_id_result = form_urlencoded_get(
            request,
            "booking_id",
            booking_id_text,
            sizeof(booking_id_text));
    status_result = form_urlencoded_get(
            request,
            "status",
            status,
            sizeof(status));

    sodium_memzero(csrf_token, sizeof(csrf_token));

    if (booking_id_result != FORM_VALUE_OK ||
        status_result != FORM_VALUE_OK ||
        !parse_positive_booking_id(booking_id_text, &booking_id) ||
        !is_valid_admin_booking_status(status)) {
        return handle_admin_status_bad_request();
    }

    update_result = booking_database_update_status(booking_id, status);

    if (update_result == BOOKING_STATUS_UPDATE_OK) {
        return handle_admin_status_redirect();
    }

    if (update_result == BOOKING_STATUS_UPDATE_NOT_FOUND) {
        return handle_admin_status_not_found();
    }

    fprintf(
            stderr,
            "Buchungsstatus konnte nicht geändert werden: %s\n",
            booking_database_last_error());
    return handle_internal_error(true);
}



static string *handle_admin_appointments_page(
        bool send_body,
        const char *query,
        size_t query_length
)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE];
    string *body;
    string *response;

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token))) {
        return handle_internal_error(send_body);
    }

    body = admin_appointments_build_page(csrf_token, query, query_length);
    sodium_memzero(csrf_token, sizeof(csrf_token));

    if (body == NULL) {
        fprintf(stderr, "Admin-Terminansicht konnte nicht erzeugt werden: %s\n",
                admin_appointments_last_error());
        return handle_bad_request(send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            admin_security_headers(),
            get_char_str(body),
            get_length(body),
            send_body);
    free_str(body);
    return response;
}

static string *handle_admin_calendar_page(
        bool send_body,
        const char *query,
        size_t query_length
)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE];
    char notice_code[32] = {0};
    string *body;
    string *response;

    if (query != NULL && query_length > 0) {
        form_value_result notice_result = form_urlencoded_get_from_data(
                query,
                query_length,
                "saved",
                notice_code,
                sizeof(notice_code));

        if (notice_result != FORM_VALUE_OK &&
            notice_result != FORM_VALUE_NOT_FOUND) {
            return handle_bad_request(send_body);
        }
    }

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token))) {
        return handle_internal_error(send_body);
    }

    body = admin_calendar_build_page(
            csrf_token,
            notice_code[0] == '\0' ? NULL : notice_code);
    sodium_memzero(csrf_token, sizeof(csrf_token));

    if (body == NULL) {
        fprintf(stderr, "Admin-Kalender konnte nicht erzeugt werden: %s\n",
                admin_calendar_last_error());
        return handle_internal_error(send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            admin_security_headers(),
            get_char_str(body),
            get_length(body),
            send_body);
    free_str(body);
    return response;
}

static bool request_has_valid_admin_csrf(string *request)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE] = {0};
    form_value_result result = form_urlencoded_get(
            request,
            "csrf_token",
            csrf_token,
            sizeof(csrf_token));
    bool valid = result == FORM_VALUE_OK && form_csrf_token_matches(csrf_token);

    sodium_memzero(csrf_token, sizeof(csrf_token));
    return valid;
}

static string *handle_admin_booking_decision_redirect(void)
{
    return build_response_text(
            "303 See Other",
            "text/html; charset=utf-8",
            "Location: /admin/bookings\r\n"
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Security-Policy: default-src 'self'; "
            "style-src 'self'; form-action 'self'; base-uri 'none'; "
            "frame-ancestors 'none'\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n",
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<title>Terminentscheidung gespeichert</title></head><body>"
            "<p>Die Terminentscheidung wurde gespeichert.</p>"
            "<p><a href=\"/admin/bookings\">Zurück zu den Buchungen</a></p>"
            "</body></html>",
            true);
}

static string *handle_admin_booking_decision_conflict(void)
{
    return build_response_text(
            "409 Conflict",
            "text/html; charset=utf-8",
            admin_security_headers(),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<title>Terminanfrage nicht mehr offen</title></head><body>"
            "<h1>Diese Terminanfrage ist nicht mehr offen.</h1>"
            "<p>Sie wurde bereits entschieden oder die vorläufige Reservierung ist abgelaufen.</p>"
            "<p><a href=\"/admin/bookings\">Zurück zu den Buchungen</a></p>"
            "</body></html>",
            true);
}

static string *handle_admin_booking_decision_post(
        string *request,
        bool accept
)
{
    char booking_id_text[32] = {0};
    char rejection_reason[512] = {0};
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    calendar_booking_decision_result result;
    int64_t booking_id;

    if (!request_has_valid_admin_csrf(request)) {
        return handle_admin_action_forbidden();
    }

    if (form_urlencoded_get(
            request,
            "booking_id",
            booking_id_text,
            sizeof(booking_id_text)) != FORM_VALUE_OK ||
        !parse_positive_booking_id(booking_id_text, &booking_id)) {
        return handle_admin_status_bad_request();
    }

    if (!accept) {
        form_value_result reason_result = form_urlencoded_get(
                request,
                "rejection_reason",
                rejection_reason,
                sizeof(rejection_reason));

        if (reason_result != FORM_VALUE_OK &&
            reason_result != FORM_VALUE_NOT_FOUND) {
            return handle_admin_status_bad_request();
        }
    }

    if (calendar_database_get_settings(&settings) != 0 ||
        calendar_clock_now(settings.timezone, &snapshot) != 0) {
        fprintf(stderr, "Zeit für Terminentscheidung konnte nicht bestimmt werden: %s\n",
                calendar_database_last_error());
        return handle_internal_error(true);
    }

    result = calendar_database_decide_booking(
            booking_id,
            accept,
            snapshot.now_utc,
            rejection_reason);

    if (result == CALENDAR_BOOKING_DECISION_OK) {
        if (notification_queue_enqueue_booking_event(
                booking_id,
                accept ? "booking_confirmed" : "booking_rejected") != 0) {
            fprintf(stderr, "Terminentscheidung konnte nicht zur Benachrichtigung eingereiht werden: %s\n",
                    notification_queue_last_error());
        }
        return handle_admin_booking_decision_redirect();
    }
    if (result == CALENDAR_BOOKING_DECISION_NOT_FOUND) {
        return handle_admin_status_not_found();
    }
    if (result == CALENDAR_BOOKING_DECISION_NOT_PENDING ||
        result == CALENDAR_BOOKING_DECISION_EXPIRED) {
        return handle_admin_booking_decision_conflict();
    }

    fprintf(stderr, "Terminentscheidung konnte nicht gespeichert werden: %s\n",
            calendar_database_last_error());
    return handle_internal_error(true);
}

static string *handle_admin_calendar_bad_request(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>400 Bad Request</title></head><body>"
            "<h1>Ungültige Kalenderangabe</h1>"
            "<p>Die Änderung wurde nicht gespeichert. Bitte prüfe die Eingaben.</p>"
            "<p><a href=\"/admin/calendar\">Zurück zum Kalender</a></p>"
            "</body></html>";

    return build_response_text(
            "400 Bad Request",
            "text/html; charset=utf-8",
            admin_security_headers(),
            body,
            true);
}

static string *handle_admin_calendar_not_found(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>404 Nicht gefunden</title></head><body>"
            "<h1>Eintrag nicht gefunden</h1>"
            "<p>Der Kalender-Eintrag existiert nicht mehr.</p>"
            "<p><a href=\"/admin/calendar\">Zurück zum Kalender</a></p>"
            "</body></html>";

    return build_response_text(
            "404 Not Found",
            "text/html; charset=utf-8",
            admin_security_headers(),
            body,
            true);
}

static string *handle_admin_calendar_redirect(const char *saved_code)
{
    char headers[512];
    char body[512];
    int headers_written;
    int body_written;

    if (saved_code == NULL || saved_code[0] == '\0') {
        return handle_internal_error(true);
    }

    headers_written = snprintf(
            headers,
            sizeof(headers),
            "Location: /admin/calendar?saved=%s\r\n%s",
            saved_code,
            admin_security_headers());
    body_written = snprintf(
            body,
            sizeof(body),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<title>Kalender gespeichert</title></head><body>"
            "<p>Die Kalenderänderung wurde gespeichert.</p>"
            "<p><a href=\"/admin/calendar\">Zurück zum Kalender</a></p>"
            "</body></html>");

    if (headers_written < 0 || (size_t)headers_written >= sizeof(headers) ||
        body_written < 0 || (size_t)body_written >= sizeof(body)) {
        return handle_internal_error(true);
    }

    return build_response_text(
            "303 See Other",
            "text/html; charset=utf-8",
            headers,
            body,
            true);
}

typedef admin_calendar_result (*admin_calendar_action)(const string *request);

static string *handle_admin_calendar_post(
        string *request,
        admin_calendar_action action,
        const char *saved_code
)
{
    admin_calendar_result result;

    if (!request_has_valid_admin_csrf(request)) {
        return handle_admin_action_forbidden();
    }

    result = action(request);
    if (result == ADMIN_CALENDAR_OK) {
        return handle_admin_calendar_redirect(saved_code);
    }
    if (result == ADMIN_CALENDAR_BAD_REQUEST) {
        return handle_admin_calendar_bad_request();
    }
    if (result == ADMIN_CALENDAR_NOT_FOUND) {
        return handle_admin_calendar_not_found();
    }

    fprintf(stderr, "Admin-Kalenderänderung fehlgeschlagen: %s\n",
            admin_calendar_last_error());
    return handle_internal_error(true);
}

string *process(string *request)
{
    char method[MAX_METHOD_LENGTH + 1];
    char path[MAX_PATH_LENGTH + 1];
    const char *query = NULL;
    size_t query_length = 0;
    bool send_body = true;

    if (!parse_request_line(request, method, sizeof(method), path, sizeof(path))) {
        return handle_bad_request(true);
    }

    split_query_string(path, &query, &query_length);

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

        if (strcmp(path, "/admin/bookings/status") == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }

            return handle_admin_booking_status_post(request);
        }

        if (strcmp(path, "/admin/bookings/accept") == 0 ||
            strcmp(path, "/admin/bookings/reject") == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }

            return handle_admin_booking_decision_post(
                    request,
                    strcmp(path, "/admin/bookings/accept") == 0);
        }

        if (strncmp(path, "/admin/calendar/", strlen("/admin/calendar/")) == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }

            if (strcmp(path, "/admin/calendar/settings") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_update_settings,
                        "settings");
            }
            if (strcmp(path, "/admin/calendar/hours") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_update_opening_hours,
                        "hours");
            }
            if (strcmp(path, "/admin/calendar/service") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_update_service,
                        "service");
            }
            if (strcmp(path, "/admin/calendar/service/add") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_add_service,
                        "service-added");
            }
            if (strcmp(path, "/admin/calendar/service/delete") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_delete_service,
                        "service-deleted");
            }
            if (strcmp(path, "/admin/calendar/closure/add") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_add_closure,
                        "closure-added");
            }
            if (strcmp(path, "/admin/calendar/closure/delete") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_delete_closure,
                        "closure-deleted");
            }
        }

        return handle_not_found(true);
    }

    if (strcmp(path, "/api/services") == 0) {
        return handle_calendar_services(send_body);
    }

    if (strcmp(path, "/api/availability") == 0) {
        return handle_calendar_availability(query, query_length, send_body);
    }

    /*
     * Ab hier bleiben nur noch GET und HEAD übrig.
     * Die Admin-Seite schützen wir mit Basic Auth.
     */
    if (strcmp(path, "/setup/admin") == 0) {
        return handle_admin_setup_page(send_body);
    }

    if (strcmp(path, "/admin/appointments") == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        return handle_admin_appointments_page(send_body, query, query_length);
    }

    if (strcmp(path, "/admin/calendar") == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        return handle_admin_calendar_page(send_body, query, query_length);
    }

    if (strcmp(path, "/admin/bookings") == 0) {
        booking_admin_filter filter;

        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        if (!parse_booking_admin_filter(query, query_length, &filter)) {
            return handle_bad_request(send_body);
        }

        return handle_admin_bookings(send_body, &filter);
    }

    return serve_static_file(path, send_body);
}