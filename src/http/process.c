//
// Created by Marlon on 11.07.26.
//

#include "styles4dogs/admin/admin_calendar.h"
#include "styles4dogs/admin/admin_appointments.h"
#include "styles4dogs/admin/admin_notifications.h"
#include "styles4dogs/security/auth.h"
#include "styles4dogs/http/process.h"
#include "styles4dogs/booking/booking.h"
#include "styles4dogs/booking/booking_database.h"
#include "styles4dogs/calendar/calendar_public.h"
#include "styles4dogs/calendar/calendar_database.h"
#include "styles4dogs/calendar/calendar_time.h"
#include "styles4dogs/http/form_urlencoded.h"
#include "styles4dogs/core/server_config.h"
#include "styles4dogs/notifications/notification_queue.h"
#include "styles4dogs/gallery/gallery.h"
#include "styles4dogs/booking/customer_portal.h"
#include "styles4dogs/admin/admin_dashboard.h"
#include "styles4dogs/services/postal_lookup.h"

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

static void customer_portal_append_html(
        string *destination,
        const char *source
);

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

    if (strcmp(extension, ".webp") == 0) {
        return "image/webp";
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

    if (strcmp(path, "/galerie") == 0) {
        return "/galerie.html";
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
            "WWW-Authenticate: Basic realm=\"Styling 4 Dogs Admin\"\r\n"
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
            "    <title>Admin einrichten | Styling 4 Dogs</title>\n"
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
            "<title>Einrichtung fehlgeschlagen | Styling 4 Dogs</title>"
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
            "<title>Admin erstellt | Styling 4 Dogs</title>"
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

static string *handle_postal_lookup(
        const char *query,
        size_t query_length,
        bool send_body
)
{
    char postal_code[6] = {0};
    string *json = NULL;
    string *response;
    form_value_result query_result;
    postal_lookup_result lookup_result;

    if (query == NULL || query_length == 0) {
        return handle_calendar_api_error(
                "400 Bad Request",
                "invalid_postal_code",
                send_body);
    }

    query_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "postal_code",
            postal_code,
            sizeof(postal_code));

    if (query_result != FORM_VALUE_OK) {
        return handle_calendar_api_error(
                "400 Bad Request",
                "invalid_postal_code",
                send_body);
    }

    lookup_result = postal_lookup_fetch(postal_code, &json);
    if (lookup_result == POSTAL_LOOKUP_INVALID_POSTAL_CODE) {
        return handle_calendar_api_error(
                "400 Bad Request",
                "invalid_postal_code",
                send_body);
    }
    if (lookup_result == POSTAL_LOOKUP_UNAVAILABLE) {
        fprintf(stderr, "PLZ-Abfrage nicht verfügbar: %s\n", postal_lookup_last_error());
        return handle_calendar_api_error(
                "503 Service Unavailable",
                "postal_lookup_unavailable",
                send_body);
    }
    if (lookup_result != POSTAL_LOOKUP_OK || json == NULL) {
        fprintf(stderr, "PLZ-Abfrage fehlgeschlagen: %s\n", postal_lookup_last_error());
        return handle_calendar_api_error(
                "500 Internal Server Error",
                "postal_lookup_error",
                send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "application/json; charset=utf-8",
            "Cache-Control: public, max-age=3600\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "Referrer-Policy: no-referrer\r\n",
            get_const_char_str(json),
            get_length(json),
            send_body);
    free_str(json);
    return response;
}

static string *handle_booking_created(bool confirmed, int64_t booking_id)
{
    char booking_url[CUSTOMER_PORTAL_URL_SIZE] = {0};
    string *body = _new_string();
    string *response;

    if (booking_id > 0 &&
        customer_portal_build_url(booking_id, booking_url, sizeof(booking_url)) != 0) {
        fprintf(stderr, "Kundenlink konnte nicht erzeugt werden: %s\n",
                customer_portal_last_error());
        free_str(body);
        return handle_internal_error(true);
    }

    str_cat_cstr(body,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>");
    str_cat_cstr(body, confirmed
            ? "Termin bestätigt | Styling 4 Dogs"
            : "Terminanfrage gesendet | Styling 4 Dogs");
    str_cat_cstr(body,
            "</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
            "<main class=\"page customer-portal-page\"><section class=\"card customer-portal-card\">"
            "<p class=\"eyebrow\">");
    str_cat_cstr(body, confirmed
            ? "Termin automatisch bestätigt"
            : "Termin vorläufig reserviert");
    str_cat_cstr(body, "</p><h1>");
    str_cat_cstr(body, confirmed
            ? "Dein Termin ist eingetragen."
            : "Vielen Dank!");
    str_cat_cstr(body, "</h1><p>");
    str_cat_cstr(body, confirmed
            ? "Der ausgewählte Zeitraum wurde verbindlich reserviert. Der Salon meldet sich bei Bedarf über deinen gewählten Kontaktweg."
            : "Deine Terminanfrage wurde gespeichert und der gewählte Zeitraum vorläufig reserviert. Der Termin wird verbindlich, sobald der Salon ihn annimmt.");
    str_cat_cstr(body, "</p>");

    if (booking_id > 0) {
        str_cat_cstr(body,
                "<div class=\"customer-portal-help\"><h2>Buchung später wieder öffnen</h2>"
                "<p>Über deinen persönlichen Link kannst du den Status prüfen und die Buchung bei Bedarf absagen.</p>"
                "<div class=\"customer-portal-actions\">"
                "<a class=\"button\" href=\"");
        customer_portal_append_html(body, booking_url);
        str_cat_cstr(body,
                "\">Meine Buchung öffnen</a>"
                "<a class=\"button button-secondary\" href=\"/\">Zurück zur Startseite</a>"
                "</div></div>");
    } else {
        str_cat_cstr(body,
                "<div class=\"customer-portal-actions\">"
                "<a class=\"button button-secondary\" href=\"/\">Zurück zur Startseite</a>"
                "</div>");
    }

    str_cat_cstr(body, "</section></main></body></html>");

    response = build_response_bytes(
            "201 Created",
            "text/html; charset=utf-8",
            "Cache-Control: no-store\r\nReferrer-Policy: no-referrer\r\n",
            get_char_str(body),
            get_length(body),
            true);
    free_str(body);
    return response;
}

static string *handle_booking_unavailable(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Termin nicht mehr verfügbar | Styling 4 Dogs</title>"
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

static string *handle_booking_contact_limit(void)
{
    const char *body =
            "<!doctype html><html lang=\"de\"><head>"
            "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Zu viele Terminanfragen | Styling 4 Dogs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
            "<main class=\"page\"><section class=\"card\">"
            "<p class=\"eyebrow\">Buchungsschutz</p>"
            "<h1>Bitte nicht noch eine weitere Anfrage senden.</h1>"
            "<p>Über diesen Kontakt wurden innerhalb der letzten 24 Stunden bereits mehrere Terminanfragen erstellt. "
            "Bitte warte etwas oder melde dich direkt beim Salon.</p>"
            "<p><a class=\"button\" href=\"/kontakt\">Zur Kontaktseite</a></p>"
            "</section></main></body></html>";

    return build_response_text(
            "429 Too Many Requests",
            "text/html; charset=utf-8",
            "Cache-Control: no-store\r\nRetry-After: 86400\r\n",
            body,
            true);
}

static string *handle_booking(string *request)
{
    booking_request booking;
    int64_t booking_id = 0;
    calendar_public_result result;

    if (booking_request_hits_honeypot(request)) {
        clear_request_body(request);
        /* Bots erhalten absichtlich dieselbe neutrale Antwort wie echte Anfragen. */
        return handle_booking_created(false, 0);
    }

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
        return handle_booking_created(false, booking_id);
    }
    if (result == CALENDAR_PUBLIC_CONFIRMED) {
        if (notification_queue_enqueue_booking_event(
                booking_id,
                "booking_confirmed") != 0) {
            fprintf(stderr, "Terminbestätigung konnte nicht eingereiht werden: %s\n",
                    notification_queue_last_error());
        }
        return handle_booking_created(true, booking_id);
    }
    if (result == CALENDAR_PUBLIC_UNAVAILABLE) {
        return handle_booking_unavailable();
    }
    if (result == CALENDAR_PUBLIC_CONTACT_LIMIT) {
        return handle_booking_contact_limit();
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
        calendar_database_expire_pending(snapshot.now_utc) != 0 ||
        calendar_database_complete_due_bookings(settings.timezone, snapshot.now_utc) != 0) {
        fprintf(stderr, "Automatische Buchungsstatus konnten nicht aktualisiert werden: %s\n",
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
           strcmp(status, "bestätigt") == 0 ||
           strcmp(status, "abgelehnt") == 0 ||
           strcmp(status, "abgesagt") == 0 ||
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

static string *handle_admin_notifications_page(
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
        form_value_result result = form_urlencoded_get_from_data(
                query, query_length, "saved", notice_code, sizeof(notice_code));
        if (result != FORM_VALUE_OK && result != FORM_VALUE_NOT_FOUND)
            return handle_bad_request(send_body);
    }

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token)))
        return handle_internal_error(send_body);

    body = admin_notifications_build_page(
            csrf_token, notice_code[0] == '\0' ? NULL : notice_code);
    sodium_memzero(csrf_token, sizeof(csrf_token));
    if (body == NULL) {
        fprintf(stderr, "E-Mail-Adminseite konnte nicht erzeugt werden: %s\n",
                admin_notifications_last_error());
        return handle_internal_error(send_body);
    }

    response = build_response_bytes(
            "200 OK", "text/html; charset=utf-8", admin_security_headers(),
            get_char_str(body), get_length(body), send_body);
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

static bool request_has_valid_admin_csrf(const string *request)
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

static string *handle_admin_notifications_bad_request(void)
{
    return build_response_text(
            "400 Bad Request",
            "text/html; charset=utf-8",
            admin_security_headers(),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>400 Bad Request</title></head><body>"
            "<h1>Ungültige E-Mail-Einstellung</h1>"
            "<p>Die Änderung wurde nicht gespeichert. Bitte prüfe Eingaben und Platzhalter.</p>"
            "<p><a href=\"/admin/notifications\">Zurück zu E-Mail und Nachrichten</a></p>"
            "</body></html>",
            true);
}

static string *handle_admin_notifications_redirect(const char *code)
{
    char headers[512];
    char body[512];
    int h, b;

    if (code == NULL || code[0] == '\0') return handle_internal_error(true);
    h = snprintf(headers, sizeof(headers),
                 "Location: /admin/notifications?saved=%s\r\n%s",
                 code, admin_security_headers());
    b = snprintf(body, sizeof(body),
                 "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
                 "<title>E-Mail-Einstellung gespeichert</title></head><body>"
                 "<p>Die E-Mail-Einstellung wurde gespeichert.</p>"
                 "<p><a href=\"/admin/notifications\">Zurück</a></p></body></html>");
    if (h < 0 || (size_t)h >= sizeof(headers) || b < 0 || (size_t)b >= sizeof(body))
        return handle_internal_error(true);
    return build_response_text("303 See Other", "text/html; charset=utf-8",
                               headers, body, true);
}

typedef admin_notifications_result (*admin_notifications_action)(const string *);

static string *handle_admin_notifications_post(
        string *request,
        admin_notifications_action action,
        const char *saved_code
)
{
    admin_notifications_result result;
    if (!request_has_valid_admin_csrf(request))
        return handle_admin_action_forbidden();

    result = action(request);
    if (result == ADMIN_NOTIFICATIONS_OK)
        return handle_admin_notifications_redirect(saved_code);
    if (result == ADMIN_NOTIFICATIONS_BAD_REQUEST) {
        fprintf(stderr, "Ungültige E-Mail-Einstellung: %s\n",
                admin_notifications_last_error());
        return handle_admin_notifications_bad_request();
    }

    fprintf(stderr, "E-Mail-Einstellung fehlgeschlagen: %s\n",
            admin_notifications_last_error());
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


static bool request_has_valid_admin_multipart_csrf(const string *request)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE] = {0};
    bool ok = gallery_extract_multipart_text_field(request, "csrf_token", csrf_token, sizeof(csrf_token)) &&
              form_csrf_token_matches(csrf_token);
    sodium_memzero(csrf_token, sizeof(csrf_token));
    return ok;
}

static string *handle_gallery_api(bool send_body)
{
    string *body = gallery_build_public_json();
    string *response;

    if (body == NULL) {
        fprintf(stderr, "Galerie-JSON konnte nicht erzeugt werden: %s\n", gallery_last_error());
        return handle_internal_error(send_body);
    }

    response = build_response_bytes(
            "200 OK",
            "application/json; charset=utf-8",
            "Cache-Control: no-store\r\n"
            "X-Content-Type-Options: nosniff\r\n",
            get_char_str(body),
            get_length(body),
            send_body);
    free_str(body);
    return response;
}

static string *handle_gallery_media(const char *request_path, const char *prefix, bool include_hidden, bool send_body)
{
    char *body = NULL;
    size_t body_length = 0;
    char content_type[64] = {0};
    int result = gallery_read_media(
            request_path + strlen(prefix),
            include_hidden,
            &body,
            &body_length,
            content_type,
            sizeof(content_type));
    string *response;

    if (result == 1) {
        return handle_not_found(send_body);
    }
    if (result != 0) {
        fprintf(stderr, "Galeriebild konnte nicht geladen werden: %s\n", gallery_last_error());
        return handle_internal_error(send_body);
    }

    response = build_response_bytes(
            "200 OK",
            content_type,
            include_hidden
                    ? "Cache-Control: no-store\r\n"
                      "Pragma: no-cache\r\n"
                      "X-Content-Type-Options: nosniff\r\n"
                    : "Cache-Control: public, max-age=300\r\n"
                      "X-Content-Type-Options: nosniff\r\n",
            body,
            body_length,
            send_body);
    free(body);
    return response;
}

static string *handle_admin_gallery_page(bool send_body, const char *query, size_t query_length)
{
    char csrf_token[FORM_CSRF_TOKEN_HEX_SIZE];
    char notice_code[32] = {0};
    string *body;
    string *response;

    if (query != NULL && query_length > 0) {
        form_value_result result = form_urlencoded_get_from_data(query, query_length, "saved", notice_code, sizeof(notice_code));
        if (result != FORM_VALUE_OK && result != FORM_VALUE_NOT_FOUND) {
            return handle_bad_request(send_body);
        }
    }

    if (!get_form_csrf_token(csrf_token, sizeof(csrf_token))) {
        return handle_internal_error(send_body);
    }

    body = gallery_build_admin_page(csrf_token, notice_code[0] == '\0' ? NULL : notice_code);
    sodium_memzero(csrf_token, sizeof(csrf_token));
    if (body == NULL) {
        fprintf(stderr, "Admin-Galerieseite konnte nicht erzeugt werden: %s\n", gallery_last_error());
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

static string *handle_admin_gallery_bad_request(void)
{
    return build_response_text(
            "400 Bad Request",
            "text/html; charset=utf-8",
            admin_security_headers(),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>400 Bad Request</title></head><body><h1>Ungültige Galerieangabe</h1><p>Die Änderung konnte nicht gespeichert werden. Bitte prüfe die Eingaben und die Bilddatei.</p><p><a href=\"/admin/gallery\">Zurück zur Galerie</a></p></body></html>",
            true);
}

static string *handle_admin_gallery_not_found(void)
{
    return build_response_text(
            "404 Not Found",
            "text/html; charset=utf-8",
            admin_security_headers(),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>404 Nicht gefunden</title></head><body><h1>Bild nicht gefunden</h1><p>Das gewünschte Foto existiert nicht mehr.</p><p><a href=\"/admin/gallery\">Zurück zur Galerie</a></p></body></html>",
            true);
}

static string *handle_admin_gallery_redirect(const char *saved_code)
{
    char headers[512];
    char body[512];
    int headers_written;
    int body_written;

    headers_written = snprintf(headers, sizeof(headers),
                               "Location: /admin/gallery?saved=%s\r\n%s",
                               saved_code, admin_security_headers());
    body_written = snprintf(body, sizeof(body),
                            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><title>Galerie gespeichert</title></head><body><p>Die Galerie wurde aktualisiert.</p><p><a href=\"/admin/gallery\">Zurück zur Galerie</a></p></body></html>");
    if (headers_written < 0 || (size_t)headers_written >= sizeof(headers) ||
        body_written < 0 || (size_t)body_written >= sizeof(body)) {
        return handle_internal_error(true);
    }

    return build_response_text("303 See Other", "text/html; charset=utf-8", headers, body, true);
}

static string *handle_admin_gallery_upload_post(const string *request)
{
    gallery_result result;

    if (!request_has_valid_admin_multipart_csrf(request)) {
        return handle_admin_action_forbidden();
    }

    result = gallery_handle_upload(request);
    if (result == GALLERY_OK) {
        return handle_admin_gallery_redirect("uploaded");
    }
    if (result == GALLERY_BAD_REQUEST) {
        fprintf(stderr, "Ungültiger Galerie-Upload: %s\n", gallery_last_error());
        return handle_admin_gallery_bad_request();
    }

    fprintf(stderr, "Galerie-Upload fehlgeschlagen: %s\n", gallery_last_error());
    return handle_internal_error(true);
}

static string *handle_admin_gallery_delete_post(const string *request)
{
    gallery_result result;

    if (!request_has_valid_admin_csrf(request)) {
        return handle_admin_action_forbidden();
    }

    result = gallery_handle_delete(request);
    if (result == GALLERY_OK) {
        return handle_admin_gallery_redirect("deleted");
    }
    if (result == GALLERY_BAD_REQUEST) {
        return handle_admin_gallery_bad_request();
    }
    if (result == GALLERY_NOT_FOUND) {
        return handle_admin_gallery_not_found();
    }

    fprintf(stderr, "Galerie-Löschen fehlgeschlagen: %s\n", gallery_last_error());
    return handle_internal_error(true);
}


static void customer_portal_append_html_char(string *destination, char character)
{
    switch (character) {
        case '&': str_cat_cstr(destination, "&amp;"); break;
        case '<': str_cat_cstr(destination, "&lt;"); break;
        case '>': str_cat_cstr(destination, "&gt;"); break;
        case '"': str_cat_cstr(destination, "&quot;"); break;
        case '\'': str_cat_cstr(destination, "&#39;"); break;
        default: str_cat(destination, &character, 1); break;
    }
}

static void customer_portal_append_html(string *destination, const char *source)
{
    size_t index;

    if (destination == NULL || source == NULL) {
        return;
    }

    for (index = 0; source[index] != '\0'; index++) {
        customer_portal_append_html_char(destination, source[index]);
    }
}

static bool parse_customer_portal_path(
        const char *path,
        int64_t *out_booking_id,
        char out_token[CUSTOMER_PORTAL_TOKEN_HEX_SIZE],
        bool *out_cancel_action
)
{
    const char *position;
    const char *id_start;
    char id_text[32];
    size_t id_length;
    char *end = NULL;
    long long booking_id;
    size_t token_length = CUSTOMER_PORTAL_TOKEN_HEX_SIZE - 1;
    size_t index;

    if (path == NULL || out_booking_id == NULL || out_token == NULL ||
        out_cancel_action == NULL || strncmp(path, "/buchung/", 9) != 0) {
        return false;
    }

    position = path + 9;
    id_start = position;
    while (*position >= '0' && *position <= '9') {
        position++;
    }

    id_length = (size_t)(position - id_start);
    if (id_length == 0 || id_length >= sizeof(id_text) || *position != '/') {
        return false;
    }

    memcpy(id_text, id_start, id_length);
    id_text[id_length] = '\0';
    errno = 0;
    booking_id = strtoll(id_text, &end, 10);
    if (errno != 0 || end == id_text || *end != '\0' || booking_id <= 0) {
        return false;
    }

    position++;
    if (strlen(position) < token_length) {
        return false;
    }
    for (index = 0; index < token_length; index++) {
        char character = position[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
        out_token[index] = character;
    }
    out_token[token_length] = '\0';
    position += token_length;

    if (*position == '\0') {
        *out_cancel_action = false;
    } else if (strcmp(position, "/cancel") == 0) {
        *out_cancel_action = true;
    } else {
        return false;
    }

    *out_booking_id = (int64_t)booking_id;
    return true;
}

static const char *customer_portal_status_label(const char *status)
{
    if (status == NULL) return "Unbekannt";
    if (strcmp(status, "pending") == 0) return "Wird vom Salon geprüft";
    if (strcmp(status, "confirmed") == 0) return "Termin bestätigt";
    if (strcmp(status, "rejected") == 0) return "Anfrage abgelehnt";
    if (strcmp(status, "cancelled") == 0) return "Von dir abgesagt";
    if (strcmp(status, "expired") == 0) return "Reservierung abgelaufen";
    return "Status unbekannt";
}

static const char *customer_portal_status_class(const char *status)
{
    if (status != NULL && strcmp(status, "confirmed") == 0) return "confirmed";
    if (status != NULL && strcmp(status, "pending") == 0) return "pending";
    if (status != NULL && strcmp(status, "cancelled") == 0) return "cancelled";
    return "closed";
}

static const char *customer_portal_security_headers(void)
{
    return
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Security-Policy: default-src 'self'; style-src 'self'; "
            "form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n"
            "Referrer-Policy: no-referrer\r\n"
            "X-Content-Type-Options: nosniff\r\n"
            "X-Frame-Options: DENY\r\n";
}

static string *handle_customer_portal_not_found(bool send_body)
{
    return build_response_text(
            "404 Not Found",
            "text/html; charset=utf-8",
            customer_portal_security_headers(),
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>Buchung nicht gefunden</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><main class=\"page customer-portal-page\"><section class=\"card customer-portal-card\"><p class=\"eyebrow\">Styling 4 Dogs</p><h1>Buchung nicht gefunden</h1><p>Der Link ist ungültig oder die Buchung ist nicht mehr verfügbar.</p><a class=\"button\" href=\"/kontakt\">Neue Terminanfrage</a></section></main></body></html>",
            send_body);
}

static string *handle_customer_portal_page(
        int64_t booking_id,
        const char *token,
        bool send_body,
        bool cancelled_notice
)
{
    customer_portal_booking booking;
    customer_portal_result result;
    char date_display[64] = "Nicht angegeben";
    char start[6] = "--:--";
    char end[6] = "--:--";
    string *body;
    string *response;
    bool cancellable;

    result = customer_portal_load_booking(booking_id, token, &booking);
    if (result == CUSTOMER_PORTAL_NOT_FOUND) {
        return handle_customer_portal_not_found(send_body);
    }
    if (result != CUSTOMER_PORTAL_OK) {
        fprintf(stderr, "Kundenbereich konnte nicht geladen werden: %s\n", customer_portal_last_error());
        return handle_internal_error(send_body);
    }

    if (booking.appointment_date[0] != '\0') {
        calendar_date_format_de(
                booking.appointment_date,
                true,
                date_display,
                sizeof(date_display));
    }
    if (booking.start_minute >= 0) calendar_time_format_hhmm(booking.start_minute, start);
    if (booking.end_minute >= 0) calendar_time_format_hhmm(booking.end_minute, end);

    cancellable = strcmp(booking.decision_status, "pending") == 0 ||
                  strcmp(booking.decision_status, "confirmed") == 0;

    body = _new_string();
    str_cat_cstr(body, "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>Deine Buchung - Styling 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><header class=\"site-header\"><div class=\"container nav-wrap\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Styling 4 Dogs</span></a><a class=\"button button-small button-secondary\" href=\"/kontakt\">Neue Anfrage</a></div></header><main class=\"page customer-portal-page\"><section class=\"card customer-portal-card\"><p class=\"eyebrow\">Deine Buchung</p><h1>Hallo ");
    customer_portal_append_html(body, booking.customer_name);
    str_cat_cstr(body, "</h1><p class=\"customer-portal-security-note\">Dieser persönliche Link zeigt deine Buchungsdaten. Bitte teile ihn nicht öffentlich.</p>");

    if (cancelled_notice) {
        str_cat_cstr(body, "<p class=\"admin-success\" role=\"status\">Deine Buchung wurde abgesagt und der Zeitraum wieder freigegeben.</p>");
    }

    str_cat_cstr(body, "<div class=\"customer-portal-status customer-portal-status-");
    str_cat_cstr(body, customer_portal_status_class(booking.decision_status));
    str_cat_cstr(body, "\"><span>Status</span><strong>");
    customer_portal_append_html(body, customer_portal_status_label(booking.decision_status));
    str_cat_cstr(body, "</strong></div><dl class=\"customer-portal-details\"><div><dt>Buchungsnummer</dt><dd>#");
    {
        char id_text[32];
        int written = snprintf(id_text, sizeof(id_text), "%lld", (long long)booking.id);
        if (written > 0 && (size_t)written < sizeof(id_text)) str_cat(body, id_text, (size_t)written);
    }
    str_cat_cstr(body, "</dd></div><div><dt>Hund</dt><dd>");
    customer_portal_append_html(body, booking.dog_name[0] == '\0' ? "Nicht angegeben" : booking.dog_name);
    str_cat_cstr(body, "</dd></div><div><dt>Leistung</dt><dd>");
    customer_portal_append_html(body, booking.service_name[0] == '\0' ? "Nicht angegeben" : booking.service_name);
    str_cat_cstr(body, "</dd></div><div><dt>Datum</dt><dd>");
    customer_portal_append_html(body, date_display);
    str_cat_cstr(body, "</dd></div><div><dt>Uhrzeit</dt><dd>");
    customer_portal_append_html(body, start);
    str_cat_cstr(body, "–");
    customer_portal_append_html(body, end);
    str_cat_cstr(body, " Uhr</dd></div></dl>");

    if (strcmp(booking.decision_status, "rejected") == 0 &&
        booking.rejection_reason[0] != '\0') {
        str_cat_cstr(body, "<div class=\"customer-portal-message\"><h2>Hinweis des Salons</h2><p>");
        customer_portal_append_html(body, booking.rejection_reason);
        str_cat_cstr(body, "</p></div>");
    }

    if (cancellable) {
        bool confirmed = strcmp(booking.decision_status, "confirmed") == 0;

        str_cat_cstr(body, "<div class=\"customer-portal-cancel\"><h2>");
        str_cat_cstr(body, confirmed ? "Termin absagen" : "Anfrage zurückziehen");
        str_cat_cstr(body, "</h2><p>Der Zeitraum wird anschließend wieder für andere Kundinnen und Kunden freigegeben.</p>");

        if (confirmed) {
            str_cat_cstr(
                    body,
                    "<p class=\"customer-portal-cancellation-fee\">"
                    "<strong>Hinweis zur Stornierung:</strong> "
                    "Bei einer Stornierung innerhalb von 72 Stunden vor dem Termin "
                    "fällt eine Ausfallgebühr an."
                    "</p>");
        }

        str_cat_cstr(body, "<form method=\"post\" action=\"/buchung/");
        {
            char id_text[32];
            int written = snprintf(id_text, sizeof(id_text), "%lld", (long long)booking.id);
            if (written > 0 && (size_t)written < sizeof(id_text)) str_cat(body, id_text, (size_t)written);
        }
        str_cat_cstr(body, "/");
        customer_portal_append_html(body, token);
        str_cat_cstr(body, "/cancel\"><label class=\"consent-field\"><input type=\"checkbox\" name=\"confirm_cancel\" value=\"1\" required><span>Ja, ich möchte diese Buchung wirklich absagen.</span></label><button class=\"button button-danger\" type=\"submit\">Buchung absagen</button></form></div>");
    }

    str_cat_cstr(body, "<div class=\"customer-portal-help\"><h2>Fragen oder Änderungswünsche?</h2><p>Die Kontaktdaten des Salons findest du im Impressum.</p><a class=\"button button-secondary\" href=\"/impressum\">Zum Impressum</a></div></section></main><footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styling 4 Dogs.</small></div></footer></body></html>");

    response = build_response_bytes(
            "200 OK",
            "text/html; charset=utf-8",
            customer_portal_security_headers(),
            get_char_str(body),
            get_length(body),
            send_body);
    free_str(body);
    return response;
}

static string *handle_customer_portal_cancel_post(
        const string *request,
        int64_t booking_id,
        const char *token
)
{
    char confirmation[8] = {0};
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    customer_portal_result result;
    char location[1200];
    int written;

    if (form_urlencoded_get(
            request,
            "confirm_cancel",
            confirmation,
            sizeof(confirmation)) != FORM_VALUE_OK ||
        strcmp(confirmation, "1") != 0) {
        return handle_bad_request(true);
    }

    if (calendar_database_get_settings(&settings) != 0 ||
        calendar_clock_now(settings.timezone, &snapshot) != 0) {
        return handle_internal_error(true);
    }

    result = customer_portal_cancel_booking(
            booking_id,
            token,
            snapshot.now_utc);

    if (result == CUSTOMER_PORTAL_NOT_FOUND) {
        return handle_customer_portal_not_found(true);
    }
    if (result == CUSTOMER_PORTAL_NOT_CANCELLABLE) {
        return handle_customer_portal_page(booking_id, token, true, false);
    }
    if (result != CUSTOMER_PORTAL_OK) {
        fprintf(stderr, "Kundenabsage konnte nicht gespeichert werden: %s\n", customer_portal_last_error());
        return handle_internal_error(true);
    }

    written = snprintf(
            location,
            sizeof(location),
            "Location: /buchung/%lld/%s?cancelled=1\r\n%s",
            (long long)booking_id,
            token,
            customer_portal_security_headers());
    if (written < 0 || (size_t)written >= sizeof(location)) {
        return handle_internal_error(true);
    }

    return build_response_text(
            "303 See Other",
            "text/html; charset=utf-8",
            location,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><title>Buchung abgesagt</title></head><body><p>Die Buchung wurde abgesagt.</p></body></html>",
            true);
}

static string *handle_admin_dashboard_page(bool send_body)
{
    string *body = admin_dashboard_build_page();
    string *response;

    if (body == NULL) {
        fprintf(stderr, "Admin-Dashboard konnte nicht erzeugt werden: %s\n", admin_dashboard_last_error());
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

static string *handle_admin_gallery_reorder_post(const string *request)
{
    gallery_result result;

    if (!request_has_valid_admin_csrf(request)) {
        return handle_admin_action_forbidden();
    }

    result = gallery_handle_reorder(request);
    if (result == GALLERY_OK) {
        return build_response_bytes(
                "204 No Content",
                "text/plain; charset=utf-8",
                admin_security_headers(),
                NULL,
                0,
                false);
    }
    if (result == GALLERY_BAD_REQUEST) {
        return handle_admin_gallery_bad_request();
    }

    fprintf(stderr, "Galerie-Sortierung fehlgeschlagen: %s\n", gallery_last_error());
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
        int64_t customer_booking_id;
        char customer_token[CUSTOMER_PORTAL_TOKEN_HEX_SIZE];
        bool customer_cancel_action;

        if (parse_customer_portal_path(
                path,
                &customer_booking_id,
                customer_token,
                &customer_cancel_action)) {
            if (!customer_cancel_action) {
                return handle_method_not_allowed(true);
            }
            return handle_customer_portal_cancel_post(
                    request,
                    customer_booking_id,
                    customer_token);
        }

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

        if (strncmp(path, "/admin/notifications/", strlen("/admin/notifications/")) == 0) {
            if (!request_has_valid_admin_auth(request))
                return handle_unauthorized(true);

            if (strcmp(path, "/admin/notifications/toggle") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_toggle_delivery, "system");
            if (strcmp(path, "/admin/notifications/smtp") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_update_smtp, "smtp");
            if (strcmp(path, "/admin/notifications/disconnect") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_disconnect_smtp, "disconnected");
            if (strcmp(path, "/admin/notifications/test") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_enqueue_test, "test");
            if (strcmp(path, "/admin/notifications/template") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_update_template, "template");
            if (strcmp(path, "/admin/notifications/template/reset") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_reset_template, "template-reset");
            if (strcmp(path, "/admin/notifications/retry") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_retry_failed, "retry");
            if (strcmp(path, "/admin/notifications/clear-sent") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_clear_sent, "clear-sent");
            if (strcmp(path, "/admin/notifications/clear-failed") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_clear_failed, "clear-failed");
            if (strcmp(path, "/admin/notifications/clear-completed") == 0)
                return handle_admin_notifications_post(
                        request, admin_notifications_clear_completed, "clear-completed");
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
            if (strcmp(path, "/admin/calendar/save-all") == 0) {
                return handle_admin_calendar_post(
                        request,
                        admin_calendar_save_all,
                        "all");
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

        if (strcmp(path, "/admin/gallery/upload") == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }
            return handle_admin_gallery_upload_post(request);
        }

        if (strcmp(path, "/admin/gallery/delete") == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }
            return handle_admin_gallery_delete_post(request);
        }

        if (strcmp(path, "/admin/gallery/reorder") == 0) {
            if (!request_has_valid_admin_auth(request)) {
                return handle_unauthorized(true);
            }
            return handle_admin_gallery_reorder_post(request);
        }

        return handle_not_found(true);
    }

    if (strcmp(path, "/api/services") == 0) {
        return handle_calendar_services(send_body);
    }

    if (strcmp(path, "/api/availability") == 0) {
        return handle_calendar_availability(query, query_length, send_body);
    }

    if (strcmp(path, "/api/postal-code") == 0) {
        return handle_postal_lookup(query, query_length, send_body);
    }

    if (strcmp(path, "/api/gallery") == 0) {
        return handle_gallery_api(send_body);
    }

    if (strncmp(path, "/media/", strlen("/media/")) == 0) {
        return handle_gallery_media(path, "/media/", false, send_body);
    }

    {
        int64_t customer_booking_id;
        char customer_token[CUSTOMER_PORTAL_TOKEN_HEX_SIZE];
        bool customer_cancel_action;

        if (parse_customer_portal_path(
                path,
                &customer_booking_id,
                customer_token,
                &customer_cancel_action)) {
            char cancelled_text[8] = {0};
            bool cancelled_notice = false;

            if (customer_cancel_action) {
                return handle_method_not_allowed(send_body);
            }

            if (query != NULL && query_length > 0) {
                form_value_result result = form_urlencoded_get_from_data(
                        query,
                        query_length,
                        "cancelled",
                        cancelled_text,
                        sizeof(cancelled_text));
                if (result != FORM_VALUE_OK && result != FORM_VALUE_NOT_FOUND) {
                    return handle_bad_request(send_body);
                }
                cancelled_notice = result == FORM_VALUE_OK &&
                                   strcmp(cancelled_text, "1") == 0;
            }

            return handle_customer_portal_page(
                    customer_booking_id,
                    customer_token,
                    send_body,
                    cancelled_notice);
        }
    }

    /*
     * Ab hier bleiben nur noch GET und HEAD übrig.
     * Die Admin-Seite schützen wir mit Basic Auth.
     */
    if (strcmp(path, "/setup/admin") == 0) {
        return handle_admin_setup_page(send_body);
    }

    if (strcmp(path, "/admin") == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }
        return handle_admin_dashboard_page(send_body);
    }

    if (strcmp(path, "/admin/notifications") == 0) {
        if (!request_has_valid_admin_auth(request))
            return handle_unauthorized(send_body);
        return handle_admin_notifications_page(send_body, query, query_length);
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

    if (strncmp(path, "/admin/gallery/media/", strlen("/admin/gallery/media/")) == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        return handle_gallery_media(
                path,
                "/admin/gallery/media/",
                true,
                send_body);
    }

    if (strcmp(path, "/admin/gallery") == 0) {
        if (!request_has_valid_admin_auth(request)) {
            return handle_unauthorized(send_body);
        }

        return handle_admin_gallery_page(send_body, query, query_length);
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