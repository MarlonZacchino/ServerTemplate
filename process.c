//
// Created by Marlon on 11.07.26.
//

#include "process.h"

#include <time.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_METHOD_LENGTH 15
#define MAX_PATH_LENGTH 512
#define DOCUMENT_ROOT "public"

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
    size_t pos = 0;
    size_t method_len = 0;
    size_t path_len = 0;

    if (request == NULL || method == NULL || path == NULL) {
        return false;
    }

    data = get_const_char_str(request);
    length = get_length(request);

    if (data == NULL || length == 0 || method_size == 0 || path_size == 0) {
        return false;
    }

    while (pos < length && data[pos] != ' ' && data[pos] != '\r' && data[pos] != '\n') {
        if (method_len + 1 >= method_size) {
            return false;
        }

        method[method_len] = data[pos];
        method_len++;
        pos++;
    }

    if (method_len == 0 || pos >= length || data[pos] != ' ') {
        return false;
    }

    method[method_len] = '\0';

    while (pos < length && data[pos] == ' ') {
        pos++;
    }

    while (pos < length && data[pos] != ' ' && data[pos] != '\r' && data[pos] != '\n') {
        if (path_len + 1 >= path_size) {
            return false;
        }

        path[path_len] = data[pos];
        path_len++;
        pos++;
    }

    if (path_len == 0) {
        return false;
    }

    path[path_len] = '\0';

    return true;
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

    if (strcmp(path, "/kontakt") == 0) {
        return "/kontakt.html";
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
            "<body><h1>405 Method Not Allowed</h1><p>Erlaubt sind aktuell nur GET und HEAD.</p></body>\n"
            "</html>\n";

    return build_response_text(
            "405 Method Not Allowed",
            "text/html; charset=utf-8",
            "Allow: GET, HEAD\r\n",
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

static const char *find_request_body(const string *request, size_t *out_body_length)
{
    const char *data;
    size_t length;

    if (out_body_length == NULL) {
        return NULL;
    }

    *out_body_length = 0;

    if (request == NULL) {
        return NULL;
    }

    data = get_const_char_str(request);
    length = get_length(request);

    if (data == NULL) {
        return NULL;
    }

    for (size_t i = 0; i + 3 < length; i++) {
        if (data[i] == '\r' &&
            data[i + 1] == '\n' &&
            data[i + 2] == '\r' &&
            data[i + 3] == '\n') {
            *out_body_length = length - (i + 4);
            return data + i + 4;
        }
    }

    for (size_t i = 0; i + 1 < length; i++) {
        if (data[i] == '\n' && data[i + 1] == '\n') {
            *out_body_length = length - (i + 2);
            return data + i + 2;
        }
    }

    return NULL;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static void url_decode_to_buffer(const char *src, size_t src_length, char *dest, size_t dest_size)
{
    size_t src_pos = 0;
    size_t dest_pos = 0;

    if (dest == NULL || dest_size == 0) {
        return;
    }

    while (src_pos < src_length && dest_pos + 1 < dest_size) {
        if (src[src_pos] == '+') {
            dest[dest_pos] = ' ';
            dest_pos++;
            src_pos++;
            continue;
        }

        if (src[src_pos] == '%' && src_pos + 2 < src_length) {
            int high = hex_value(src[src_pos + 1]);
            int low = hex_value(src[src_pos + 2]);

            if (high >= 0 && low >= 0) {
                dest[dest_pos] = (char)((high * 16) + low);
                dest_pos++;
                src_pos += 3;
                continue;
            }
        }

        dest[dest_pos] = src[src_pos];
        dest_pos++;
        src_pos++;
    }

    dest[dest_pos] = '\0';
}

static void get_form_value(
        const char *body,
        size_t body_length,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    size_t field_length;

    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';

    if (body == NULL || field_name == NULL) {
        return;
    }

    field_length = strlen(field_name);

    size_t pos = 0;

    while (pos < body_length) {
        size_t key_start = pos;
        size_t key_end;
        size_t value_start;
        size_t value_end;

        while (pos < body_length && body[pos] != '=' && body[pos] != '&') {
            pos++;
        }

        key_end = pos;

        if (pos >= body_length || body[pos] != '=') {
            while (pos < body_length && body[pos] != '&') {
                pos++;
            }

            if (pos < body_length && body[pos] == '&') {
                pos++;
            }

            continue;
        }

        pos++;
        value_start = pos;

        while (pos < body_length && body[pos] != '&') {
            pos++;
        }

        value_end = pos;

        if (key_end - key_start == field_length &&
            strncmp(body + key_start, field_name, field_length) == 0) {
            url_decode_to_buffer(body + value_start, value_end - value_start, out, out_size);
            return;
        }

        if (pos < body_length && body[pos] == '&') {
            pos++;
        }
    }
}

static int save_booking_request(
        const char *name,
        const char *contact,
        const char *dog_name,
        const char *message
)
{
    FILE *file;
    time_t now = time(NULL);

    /*
     * Falls der Ordner schon existiert, ist das kein Problem.
     */
    mkdir("data", 0755);

    file = fopen("data/bookings.txt", "a");

    if (file == NULL) {
        return -1;
    }

    fprintf(file, "----- Neue Anfrage -----\n");
    fprintf(file, "Zeit: %ld\n", (long)now);
    fprintf(file, "Name: %s\n", name);
    fprintf(file, "Kontakt: %s\n", contact);
    fprintf(file, "Hund: %s\n", dog_name);
    fprintf(file, "Nachricht: %s\n", message);
    fprintf(file, "\n");

    fclose(file);
    return 0;
}

static string *handle_booking_created(bool send_body)
{
    const char *body =
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>Anfrage gesendet - Styles 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <main class=\"page\">\n"
            "        <section class=\"card\">\n"
            "            <h1>Anfrage gesendet</h1>\n"
            "            <p>Danke. Deine Anfrage wurde gespeichert.</p>\n"
            "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
            "        </section>\n"
            "    </main>\n"
            "</body>\n"
            "</html>\n";

    return build_response_text("201 Created", "text/html; charset=utf-8", NULL, body, send_body);
}

static string *handle_booking(string *request)
{
    const char *body;
    size_t body_length;

    char name[256];
    char contact[256];
    char dog_name[256];
    char message[1024];

    body = find_request_body(request, &body_length);

    if (body == NULL) {
        return handle_bad_request(true);
    }

    get_form_value(body, body_length, "name", name, sizeof(name));
    get_form_value(body, body_length, "contact", contact, sizeof(contact));
    get_form_value(body, body_length, "dog_name", dog_name, sizeof(dog_name));
    get_form_value(body, body_length, "message", message, sizeof(message));

    if (name[0] == '\0' || contact[0] == '\0') {
        return handle_bad_request(true);
    }

    if (save_booking_request(name, contact, dog_name, message) != 0) {
        return handle_internal_error(true);
    }

    return handle_booking_created(true);
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

        return handle_not_found(true);
    }

    return serve_static_file(path, send_body);
}