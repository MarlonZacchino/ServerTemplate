#include "gallery.h"

#include "form_urlencoded.h"
#include "server_config.h"

#include <errno.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define GALLERY_ERROR_SIZE 512
#define GALLERY_TITLE_SIZE 160
#define GALLERY_ALT_SIZE 255
#define GALLERY_FILENAME_SIZE 128
#define GALLERY_CONTENT_TYPE_SIZE 64
#define GALLERY_BOUNDARY_SIZE 160
#define GALLERY_UPLOAD_MAX_BYTES (8u * 1024u * 1024u)

typedef struct gallery_item {
    int64_t id;
    char title[GALLERY_TITLE_SIZE + 1];
    char alt_text[GALLERY_ALT_SIZE + 1];
    char file_name[GALLERY_FILENAME_SIZE + 1];
    char mime_type[GALLERY_CONTENT_TYPE_SIZE];
    int sort_order;
    int visible;
} gallery_item;

typedef int (*gallery_item_callback)(const gallery_item *item, void *opaque);

static char module_error[GALLERY_ERROR_SIZE];
static unsigned int unique_counter = 0;

static void set_error(const char *message)
{
    snprintf(
            module_error,
            sizeof(module_error),
            "%s",
            message == NULL ? "Unbekannter Galeriefehler" : message);
}

static void set_sqlite_error(sqlite3 *database, const char *context)
{
    snprintf(
            module_error,
            sizeof(module_error),
            "%s: %s",
            context == NULL ? "SQLite-Fehler" : context,
            database == NULL ? "Datenbank ist nicht geöffnet" : sqlite3_errmsg(database));
}

const char *gallery_last_error(void)
{
    return module_error[0] == '\0' ? "Unbekannter Galeriefehler" : module_error;
}

static const char *database_open_path(void)
{
    if (strcmp(server_config_database_file(), ":memory:") == 0) {
        return "file:styles4dogs-runtime?mode=memory&cache=shared";
    }

    return server_config_database_file();
}

static int execute_sql(sqlite3 *database, const char *sql)
{
    char *error_message = NULL;
    int result;

    result = sqlite3_exec(database, sql, NULL, NULL, &error_message);
    if (result != SQLITE_OK) {
        snprintf(
                module_error,
                sizeof(module_error),
                "SQLite-Anweisung fehlgeschlagen: %s",
                error_message == NULL ? sqlite3_errmsg(database) : error_message);
        sqlite3_free(error_message);
        return -1;
    }

    return 0;
}

static int gallery_open(sqlite3 **out_database)
{
    sqlite3 *database = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

    if (out_database == NULL) {
        set_error("Datenbank-Ausgabe fehlt");
        return -1;
    }

    *out_database = NULL;
    module_error[0] = '\0';

    if (strncmp(database_open_path(), "file:", strlen("file:")) == 0) {
        flags |= SQLITE_OPEN_URI;
    }

    if (sqlite3_open_v2(database_open_path(), &database, flags, NULL) != SQLITE_OK) {
        set_sqlite_error(database, "Galerie-Datenbank konnte nicht geöffnet werden");
        if (database != NULL) {
            sqlite3_close(database);
        }
        return -1;
    }

    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK ||
        execute_sql(database, "PRAGMA foreign_keys = ON;") != 0 ||
        execute_sql(
                database,
                "CREATE TABLE IF NOT EXISTS gallery_images ("
                "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "    created_at TEXT NOT NULL,"
                "    title TEXT NOT NULL DEFAULT '',"
                "    alt_text TEXT NOT NULL DEFAULT '',"
                "    file_name TEXT NOT NULL UNIQUE,"
                "    mime_type TEXT NOT NULL CHECK(mime_type IN ('image/jpeg','image/png','image/webp')),"
                "    image_data BLOB NOT NULL,"
                "    sort_order INTEGER NOT NULL DEFAULT 0,"
                "    visible INTEGER NOT NULL DEFAULT 1 CHECK(visible IN (0,1))"
                ");"
                "CREATE INDEX IF NOT EXISTS idx_gallery_images_listing "
                "ON gallery_images(visible DESC, sort_order ASC, id DESC);") != 0) {
        if (module_error[0] == '\0') {
            set_sqlite_error(database, "Galerie-Datenbank konnte nicht initialisiert werden");
        }
        sqlite3_close(database);
        return -1;
    }

    *out_database = database;
    return 0;
}

static void gallery_close(sqlite3 *database)
{
    if (database != NULL) {
        sqlite3_close(database);
    }
}

static void append_html_char_escaped(string *destination, char character)
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

static void append_html_escaped(string *destination, const char *source)
{
    size_t index;

    if (destination == NULL || source == NULL) {
        return;
    }

    for (index = 0; source[index] != '\0'; index++) {
        append_html_char_escaped(destination, source[index]);
    }
}

static void append_json_escaped(string *destination, const char *source)
{
    size_t index;

    if (destination == NULL || source == NULL) {
        return;
    }

    for (index = 0; source[index] != '\0'; index++) {
        unsigned char character = (unsigned char)source[index];

        switch (character) {
            case '"': str_cat_cstr(destination, "\\\""); break;
            case '\\': str_cat_cstr(destination, "\\\\"); break;
            case '\b': str_cat_cstr(destination, "\\b"); break;
            case '\f': str_cat_cstr(destination, "\\f"); break;
            case '\n': str_cat_cstr(destination, "\\n"); break;
            case '\r': str_cat_cstr(destination, "\\r"); break;
            case '\t': str_cat_cstr(destination, "\\t"); break;
            default:
                if (character < 0x20) {
                    char buffer[8];
                    int written = snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                    if (written > 0 && (size_t)written < sizeof(buffer)) {
                        str_cat(destination, buffer, (size_t)written);
                    }
                } else {
                    str_cat(destination, (const char *)&character, 1);
                }
                break;
        }
    }
}

static void append_int64(string *destination, int64_t value)
{
    char buffer[48];
    int written = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat(destination, buffer, (size_t)written);
    }
}

static void trim_text(char *text)
{
    size_t start = 0;
    size_t end;

    if (text == NULL) {
        return;
    }

    end = strlen(text);
    while (start < end &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        start++;
    }
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r' || text[end - 1] == '\n')) {
        end--;
    }

    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static bool text_has_only_allowed_controls(const char *text)
{
    size_t index;

    if (text == NULL) {
        return false;
    }

    for (index = 0; text[index] != '\0'; index++) {
        unsigned char character = (unsigned char)text[index];
        if (character == 0x7f || (character < 0x20 && character != '\t')) {
            return false;
        }
    }

    return true;
}

static const char *find_request_body(const string *request, size_t *out_body_length)
{
    const char *data;
    size_t length;
    size_t index;

    if (request == NULL || out_body_length == NULL) {
        return NULL;
    }

    *out_body_length = 0;
    data = get_const_char_str(request);
    length = get_length(request);
    if (data == NULL) {
        return NULL;
    }

    for (index = 0; index + 3 < length; index++) {
        if (data[index] == '\r' && data[index + 1] == '\n' &&
            data[index + 2] == '\r' && data[index + 3] == '\n') {
            *out_body_length = length - (index + 4);
            return data + index + 4;
        }
    }

    return NULL;
}

static const char *find_header_line_value(
        const string *request,
        const char *header_name,
        size_t *out_length)
{
    const char *data;
    size_t data_length;
    size_t header_name_length;
    size_t position = 0;

    if (request == NULL || header_name == NULL || out_length == NULL) {
        return NULL;
    }

    *out_length = 0;
    data = get_const_char_str(request);
    data_length = get_length(request);
    header_name_length = strlen(header_name);
    if (data == NULL) {
        return NULL;
    }

    while (position < data_length) {
        size_t line_start = position;
        size_t line_end = line_start;

        while (line_end < data_length && data[line_end] != '\n') {
            line_end++;
        }

        if (line_end == line_start ||
            (line_end == line_start + 1 && data[line_start] == '\r')) {
            break;
        }

        if (line_end - line_start > header_name_length + 1 &&
            strncasecmp(data + line_start, header_name, header_name_length) == 0 &&
            data[line_start + header_name_length] == ':') {
            const char *value = data + line_start + header_name_length + 1;
            size_t value_length;

            while (value < data + line_end && (*value == ' ' || *value == '\t')) {
                value++;
            }
            value_length = (size_t)((data + line_end) - value);
            if (value_length > 0 && value[value_length - 1] == '\r') {
                value_length--;
            }
            *out_length = value_length;
            return value;
        }

        position = line_end + 1;
    }

    return NULL;
}

static const char *find_bytes(
        const char *haystack,
        size_t haystack_length,
        const char *needle,
        size_t needle_length)
{
    size_t index;

    if (haystack == NULL || needle == NULL || needle_length == 0 ||
        haystack_length < needle_length) {
        return NULL;
    }

    for (index = 0; index + needle_length <= haystack_length; index++) {
        if (memcmp(haystack + index, needle, needle_length) == 0) {
            return haystack + index;
        }
    }

    return NULL;
}

static bool extract_boundary(
        const string *request,
        char *out_boundary,
        size_t out_boundary_size)
{
    const char *content_type;
    size_t content_type_length;
    const char *boundary_start = NULL;
    const char *boundary_key = "boundary=";
    size_t index;
    size_t boundary_length = 0;

    if (out_boundary == NULL || out_boundary_size == 0) {
        return false;
    }

    out_boundary[0] = '\0';
    content_type = find_header_line_value(request, "Content-Type", &content_type_length);
    if (content_type == NULL || content_type_length == 0 ||
        content_type_length < strlen("multipart/form-data")) {
        return false;
    }

    if (strncasecmp(content_type, "multipart/form-data", strlen("multipart/form-data")) != 0) {
        return false;
    }

    for (index = 0; index + strlen(boundary_key) <= content_type_length; index++) {
        if (strncasecmp(content_type + index, boundary_key, strlen(boundary_key)) == 0) {
            boundary_start = content_type + index + strlen(boundary_key);
            break;
        }
    }

    if (boundary_start == NULL) {
        return false;
    }

    while (boundary_start + boundary_length < content_type + content_type_length &&
           boundary_start[boundary_length] != ';' &&
           boundary_start[boundary_length] != '\r' &&
           boundary_start[boundary_length] != '\n') {
        boundary_length++;
    }

    while (boundary_length > 0 &&
           (boundary_start[boundary_length - 1] == ' ' ||
            boundary_start[boundary_length - 1] == '\t')) {
        boundary_length--;
    }

    if (boundary_length >= 2 && boundary_start[0] == '"' &&
        boundary_start[boundary_length - 1] == '"') {
        boundary_start++;
        boundary_length -= 2;
    }

    if (boundary_length == 0 || boundary_length >= out_boundary_size) {
        return false;
    }

    memcpy(out_boundary, boundary_start, boundary_length);
    out_boundary[boundary_length] = '\0';
    return true;
}

static bool extract_disposition_attribute(
        const char *headers,
        size_t headers_length,
        const char *attribute,
        char *out,
        size_t out_size)
{
    size_t attribute_length;
    size_t index;

    if (headers == NULL || attribute == NULL || out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    attribute_length = strlen(attribute);

    for (index = 0; index + attribute_length + 2 < headers_length; index++) {
        if (strncasecmp(headers + index, attribute, attribute_length) == 0 &&
            headers[index + attribute_length] == '=' &&
            headers[index + attribute_length + 1] == '"') {
            const char *value_start = headers + index + attribute_length + 2;
            const char *value_end = value_start;
            size_t value_length;

            while (value_end < headers + headers_length && *value_end != '"') {
                value_end++;
            }
            if (value_end >= headers + headers_length) {
                return false;
            }

            value_length = (size_t)(value_end - value_start);
            if (value_length >= out_size) {
                return false;
            }

            memcpy(out, value_start, value_length);
            out[value_length] = '\0';
            return true;
        }
    }

    return false;
}

static bool extract_part_content_type(
        const char *headers,
        size_t headers_length,
        char *out,
        size_t out_size)
{
    const char *line = headers;
    const char *headers_end = headers + headers_length;

    if (headers == NULL || out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';

    while (line < headers_end) {
        const char *line_end = memchr(line, '\n', (size_t)(headers_end - line));
        size_t line_length;

        if (line_end == NULL) {
            line_end = headers_end;
        }
        line_length = (size_t)(line_end - line);
        if (line_length > 0 && line[line_length - 1] == '\r') {
            line_length--;
        }

        if (line_length > strlen("Content-Type:") &&
            strncasecmp(line, "Content-Type:", strlen("Content-Type:")) == 0) {
            const char *value = line + strlen("Content-Type:");
            size_t value_length;

            while (value < line + line_length && (*value == ' ' || *value == '\t')) {
                value++;
            }
            value_length = (size_t)((line + line_length) - value);
            if (value_length >= out_size) {
                return false;
            }
            memcpy(out, value, value_length);
            out[value_length] = '\0';
            return true;
        }

        line = line_end < headers_end ? line_end + 1 : headers_end;
    }

    return false;
}

static bool find_multipart_part(
        const string *request,
        const char *field_name,
        const char **out_data,
        size_t *out_length,
        char *out_filename,
        size_t out_filename_size,
        char *out_content_type,
        size_t out_content_type_size)
{
    char boundary[GALLERY_BOUNDARY_SIZE];
    char delimiter[GALLERY_BOUNDARY_SIZE + 4];
    const char *body;
    size_t body_length;
    size_t delimiter_length;
    const char *position;

    if (request == NULL || field_name == NULL ||
        out_data == NULL || out_length == NULL) {
        return false;
    }

    *out_data = NULL;
    *out_length = 0;
    if (out_filename != NULL && out_filename_size > 0) {
        out_filename[0] = '\0';
    }
    if (out_content_type != NULL && out_content_type_size > 0) {
        out_content_type[0] = '\0';
    }

    if (!extract_boundary(request, boundary, sizeof(boundary))) {
        return false;
    }

    body = find_request_body(request, &body_length);
    if (body == NULL || body_length == 0) {
        return false;
    }

    delimiter_length = (size_t)snprintf(delimiter, sizeof(delimiter), "--%s", boundary);
    if (delimiter_length == 0 || delimiter_length >= sizeof(delimiter)) {
        return false;
    }

    position = body;
    while (position < body + body_length) {
        const char *boundary_position;
        const char *headers_start;
        const char *headers_end;
        const char *data_start;
        const char *part_end;
        size_t headers_length;
        char name[128] = {0};

        boundary_position = find_bytes(
                position,
                (size_t)(body + body_length - position),
                delimiter,
                delimiter_length);
        if (boundary_position == NULL) {
            break;
        }

        position = boundary_position + delimiter_length;
        if (position + 1 < body + body_length &&
            position[0] == '-' && position[1] == '-') {
            break;
        }

        if (position + 2 <= body + body_length &&
            position[0] == '\r' && position[1] == '\n') {
            position += 2;
        } else if (position < body + body_length && position[0] == '\n') {
            position++;
        } else {
            return false;
        }

        headers_start = position;
        headers_end = find_bytes(
                headers_start,
                (size_t)(body + body_length - headers_start),
                "\r\n\r\n",
                4);
        if (headers_end == NULL) {
            return false;
        }
        data_start = headers_end + 4;
        headers_length = (size_t)(headers_end - headers_start);

        {
            char end_marker[GALLERY_BOUNDARY_SIZE + 8];
            size_t end_marker_length = (size_t)snprintf(
                    end_marker,
                    sizeof(end_marker),
                    "\r\n--%s",
                    boundary);
            if (end_marker_length == 0 || end_marker_length >= sizeof(end_marker)) {
                return false;
            }
            part_end = find_bytes(
                    data_start,
                    (size_t)(body + body_length - data_start),
                    end_marker,
                    end_marker_length);
        }
        if (part_end == NULL) {
            return false;
        }

        if (!extract_disposition_attribute(
                headers_start,
                headers_length,
                "name",
                name,
                sizeof(name))) {
            position = part_end;
            continue;
        }

        if (strcmp(name, field_name) == 0) {
            if (out_filename != NULL && out_filename_size > 0) {
                extract_disposition_attribute(
                        headers_start,
                        headers_length,
                        "filename",
                        out_filename,
                        out_filename_size);
            }
            if (out_content_type != NULL && out_content_type_size > 0) {
                extract_part_content_type(
                        headers_start,
                        headers_length,
                        out_content_type,
                        out_content_type_size);
            }

            *out_data = data_start;
            *out_length = (size_t)(part_end - data_start);
            return true;
        }

        position = part_end;
    }

    return false;
}

bool gallery_extract_multipart_text_field(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size)
{
    const char *data;
    size_t data_length;

    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    if (!find_multipart_part(
            request,
            field_name,
            &data,
            &data_length,
            NULL,
            0,
            NULL,
            0)) {
        return false;
    }

    if (data_length >= out_size) {
        return false;
    }

    memcpy(out, data, data_length);
    out[data_length] = '\0';
    trim_text(out);
    return text_has_only_allowed_controls(out);
}

static bool detect_image_type(
        const unsigned char *data,
        size_t data_length,
        const char **out_mime_type,
        const char **out_extension)
{
    if (data == NULL || out_mime_type == NULL || out_extension == NULL) {
        return false;
    }

    if (data_length >= 3 &&
        data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
        *out_mime_type = "image/jpeg";
        *out_extension = ".jpg";
        return true;
    }

    if (data_length >= 8 &&
        memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        *out_mime_type = "image/png";
        *out_extension = ".png";
        return true;
    }

    if (data_length >= 12 &&
        memcmp(data, "RIFF", 4) == 0 &&
        memcmp(data + 8, "WEBP", 4) == 0) {
        *out_mime_type = "image/webp";
        *out_extension = ".webp";
        return true;
    }

    return false;
}

static bool build_unique_file_name(
        const char *extension,
        char *out_name,
        size_t out_name_size)
{
    time_t now;
    struct tm time_parts;
    struct tm *utc;
    char timestamp[32];
    int written;

    if (extension == NULL || out_name == NULL || out_name_size == 0) {
        return false;
    }

    now = time(NULL);
    utc = gmtime(&now);
    if (utc == NULL) {
        return false;
    }
    time_parts = *utc;
    if (strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", &time_parts) == 0) {
        return false;
    }

    unique_counter++;
    written = snprintf(
            out_name,
            out_name_size,
            "gallery-%s-%ld-%u%s",
            timestamp,
            (long)getpid(),
            unique_counter,
            extension);

    return written > 0 && (size_t)written < out_name_size;
}

static int next_gallery_sort_order(int *out_sort_order)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    if (out_sort_order == NULL) { set_error("Ausgabe für Galerie-Reihenfolge fehlt"); return -1; }
    if (gallery_open(&database) != 0) return -1;
    result = sqlite3_prepare_v2(database, "SELECT COALESCE(MAX(sort_order), -1) + 1 FROM gallery_images;", -1, &statement, NULL);
    if (result != SQLITE_OK) { set_sqlite_error(database, "Galerie-Reihenfolge konnte nicht vorbereitet werden"); gallery_close(database); return -1; }
    result = sqlite3_step(statement);
    if (result != SQLITE_ROW) { set_sqlite_error(database, "Galerie-Reihenfolge konnte nicht gelesen werden"); sqlite3_finalize(statement); gallery_close(database); return -1; }
    *out_sort_order = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement); gallery_close(database); return 0;
}

static int insert_gallery_item(
        const char *title,
        const char *alt_text,
        const char *file_name,
        const char *mime_type,
        const void *image_data,
        size_t image_length,
        int sort_order,
        bool visible)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;

    if (gallery_open(&database) != 0) {
        return -1;
    }

    result = sqlite3_prepare_v2(
            database,
            "INSERT INTO gallery_images("
            "created_at, title, alt_text, file_name, mime_type, image_data, sort_order, visible"
            ") VALUES(strftime('%Y-%m-%dT%H:%M:%SZ','now'), ?1, ?2, ?3, ?4, ?5, ?6, ?7);",
            -1,
            &statement,
            NULL);
    if (result != SQLITE_OK) {
        set_sqlite_error(database, "Galerieeintrag konnte nicht vorbereitet werden");
        gallery_close(database);
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, title, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, alt_text, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 3, file_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 4, mime_type, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_blob64(statement, 5, image_data, (sqlite3_uint64)image_length, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int(statement, 6, sort_order) != SQLITE_OK ||
        sqlite3_bind_int(statement, 7, visible ? 1 : 0) != SQLITE_OK) {
        set_sqlite_error(database, "Galeriedaten konnten nicht gebunden werden");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        set_sqlite_error(database, "Galerieeintrag konnte nicht gespeichert werden");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    sqlite3_finalize(statement);
    gallery_close(database);
    return 0;
}

static int delete_gallery_item(int64_t image_id)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    int changed_rows;

    if (gallery_open(&database) != 0) {
        return -1;
    }

    result = sqlite3_prepare_v2(
            database,
            "DELETE FROM gallery_images WHERE id = ?1;",
            -1,
            &statement,
            NULL);
    if (result != SQLITE_OK) {
        set_sqlite_error(database, "Galeriebild konnte nicht zum Löschen vorbereitet werden");
        gallery_close(database);
        return -1;
    }

    sqlite3_bind_int64(statement, 1, image_id);
    result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        set_sqlite_error(database, "Galeriebild konnte nicht gelöscht werden");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    changed_rows = sqlite3_changes(database);
    sqlite3_finalize(statement);
    gallery_close(database);
    return changed_rows == 0 ? 1 : 0;
}

static int each_gallery_item(
        bool include_hidden,
        gallery_item_callback callback,
        void *opaque)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    const char *sql;
    int result;

    if (callback == NULL) {
        set_error("Galerie-Callback fehlt");
        return -1;
    }

    sql = include_hidden
            ? "SELECT id, title, alt_text, file_name, mime_type, sort_order, visible "
              "FROM gallery_images ORDER BY sort_order ASC, id DESC;"
            : "SELECT id, title, alt_text, file_name, mime_type, sort_order, visible "
              "FROM gallery_images WHERE visible = 1 ORDER BY sort_order ASC, id DESC;";

    if (gallery_open(&database) != 0) {
        return -1;
    }

    result = sqlite3_prepare_v2(database, sql, -1, &statement, NULL);
    if (result != SQLITE_OK) {
        set_sqlite_error(database, "Galerieliste konnte nicht vorbereitet werden");
        gallery_close(database);
        return -1;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        gallery_item item;
        const unsigned char *title;
        const unsigned char *alt_text;
        const unsigned char *file_name;
        const unsigned char *mime_type;

        memset(&item, 0, sizeof(item));
        item.id = sqlite3_column_int64(statement, 0);
        title = sqlite3_column_text(statement, 1);
        alt_text = sqlite3_column_text(statement, 2);
        file_name = sqlite3_column_text(statement, 3);
        mime_type = sqlite3_column_text(statement, 4);
        item.sort_order = sqlite3_column_int(statement, 5);
        item.visible = sqlite3_column_int(statement, 6);

        snprintf(item.title, sizeof(item.title), "%s", title == NULL ? "" : (const char *)title);
        snprintf(item.alt_text, sizeof(item.alt_text), "%s", alt_text == NULL ? "" : (const char *)alt_text);
        snprintf(item.file_name, sizeof(item.file_name), "%s", file_name == NULL ? "" : (const char *)file_name);
        snprintf(item.mime_type, sizeof(item.mime_type), "%s", mime_type == NULL ? "" : (const char *)mime_type);

        if (callback(&item, opaque) != 0) {
            sqlite3_finalize(statement);
            gallery_close(database);
            return -1;
        }
    }

    if (result != SQLITE_DONE) {
        set_sqlite_error(database, "Galerieliste konnte nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    sqlite3_finalize(statement);
    gallery_close(database);
    return 0;
}

typedef struct gallery_json_context {
    string *json;
    bool first;
} gallery_json_context;

static int append_gallery_json_item(const gallery_item *item, void *opaque)
{
    gallery_json_context *context = opaque;

    if (!context->first) {
        str_cat_cstr(context->json, ",");
    }
    context->first = false;

    str_cat_cstr(context->json, "{\"id\":");
    append_int64(context->json, item->id);
    str_cat_cstr(context->json, ",\"title\":\"");
    append_json_escaped(context->json, item->title);
    str_cat_cstr(context->json, "\",\"alt\":\"");
    append_json_escaped(
            context->json,
            item->alt_text[0] == '\0' ? item->title : item->alt_text);
    str_cat_cstr(context->json, "\",\"url\":\"/media/");
    append_json_escaped(context->json, item->file_name);
    str_cat_cstr(context->json, "\"}");
    return 0;
}

string *gallery_build_public_json(void)
{
    gallery_json_context context;
    string *json = _new_string();

    context.json = json;
    context.first = true;
    str_cat_cstr(json, "[");

    if (each_gallery_item(false, append_gallery_json_item, &context) != 0) {
        free_str(json);
        return NULL;
    }

    str_cat_cstr(json, "]");
    return json;
}

static const char *notice_message(const char *notice_code)
{
    if (notice_code == NULL) return NULL;
    if (strcmp(notice_code, "uploaded") == 0) return "Das Foto wurde in die Galerie übernommen.";
    if (strcmp(notice_code, "deleted") == 0) return "Das Foto wurde aus der Galerie entfernt.";
    return NULL;
}

typedef struct gallery_admin_context {
    string *page;
    const char *csrf_token;
    size_t count;
} gallery_admin_context;

static int append_admin_gallery_item(const gallery_item *item, void *opaque)
{
    gallery_admin_context *context = opaque;
    context->count++;
    str_cat_cstr(context->page, "<article class=\"gallery-admin-item\" draggable=\"true\" data-gallery-item data-image-id=\"");
    append_int64(context->page, item->id);
    str_cat_cstr(
    context->page,
    "\"><div class=\"gallery-sort-controls\" role=\"group\" aria-label=\"Foto sortieren\">"
    "<button class=\"gallery-sort-button\" type=\"button\" "
    "data-gallery-move=\"up\" aria-label=\"Foto nach oben verschieben\" "
    "title=\"Nach oben verschieben\">↑</button>"
    "<button class=\"gallery-sort-button\" type=\"button\" "
    "data-gallery-move=\"down\" aria-label=\"Foto nach unten verschieben\" "
    "title=\"Nach unten verschieben\">↓</button>"
    "</div><div class=\"gallery-admin-preview\"><img src=\"/admin/gallery/media/"
);
    append_html_escaped(context->page, item->file_name);
    str_cat_cstr(context->page, "\" alt=\"");
    append_html_escaped(context->page, item->alt_text[0] == '\0' ? item->title : item->alt_text);
    str_cat_cstr(context->page, "\"></div><div class=\"gallery-admin-content\"><div><h3>");
    append_html_escaped(context->page, item->title[0] == '\0' ? "Ohne Titel" : item->title);
    str_cat_cstr(context->page, "</h3><p>");
    append_html_escaped(context->page, item->alt_text[0] == '\0' ? "Kein Alternativtext hinterlegt." : item->alt_text);
    str_cat_cstr(context->page, "</p></div><form method=\"post\" action=\"/admin/gallery/delete\" class=\"gallery-delete-form\"><input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_escaped(context->page, context->csrf_token);
    str_cat_cstr(context->page, "\"><input type=\"hidden\" name=\"image_id\" value=\"");
    append_int64(context->page, item->id);
    str_cat_cstr(context->page, "\"><button class=\"button button-small button-danger\" type=\"submit\">Löschen</button></form></div></article>");
    return 0;
}

string *gallery_build_admin_page(
        const char *csrf_token,
        const char *notice_code)
{
    gallery_admin_context context;
    string *page;
    const char *notice;

    if (csrf_token == NULL) {
        set_error("CSRF-Token fehlt");
        return NULL;
    }

    page = _new_string();
    notice = notice_message(notice_code);

    str_cat_cstr(
            page,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>Galerie verwalten - Styling 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"><script src=\"/admin-gallery.js\" defer></script></head><body><header class=\"site-header\"><div class=\"container nav-wrap\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Styling 4 Dogs</span></a><nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/admin\">Übersicht</a><a href=\"/\">Website öffnen</a><a href=\"/admin/bookings\">Buchungsanfragen</a><a href=\"/admin/appointments\">Termine</a><a href=\"/admin/calendar\">Einstellungen</a><a href=\"/admin/gallery\" aria-current=\"page\">Fotos</a><a href=\"/admin/notifications\">E-Mail</a></nav></div></header><main class=\"page admin-page admin-gallery-page\"><section class=\"card admin-card admin-calendar-section\"><p class=\"eyebrow\">Admin</p><h1>Galerie verwalten</h1><p>Lade Bilder für Kundinnen und Kunden hoch und pflege die öffentliche Galerie.</p>");

    if (notice != NULL) {
        str_cat_cstr(page, "<p class=\"admin-success\" role=\"status\">");
        append_html_escaped(page, notice);
        str_cat_cstr(page, "</p>");
    }

    str_cat_cstr(
            page,
            "<div class=\"gallery-admin-layout\"><section class=\"gallery-upload-card\"><h2>Neues Foto hochladen</h2><form class=\"gallery-upload-form\" method=\"post\" action=\"/admin/gallery/upload\" enctype=\"multipart/form-data\"><input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_escaped(page, csrf_token);
    str_cat_cstr(
            page,
            "\"><label for=\"gallery-title\">Titel</label><input id=\"gallery-title\" type=\"text\" name=\"title\" maxlength=\"160\" placeholder=\"Zum Beispiel: Nach dem Komplettschnitt\"><label for=\"gallery-alt\">Alternativtext</label><input id=\"gallery-alt\" type=\"text\" name=\"alt_text\" maxlength=\"255\" placeholder=\"Kurze Bildbeschreibung für Barrierefreiheit\"><label for=\"gallery-image\">Bilddatei</label><input id=\"gallery-image\" type=\"file\" name=\"image\" accept=\"image/jpeg,image/png,image/webp\" required><p class=\"admin-calendar-hint\">Erlaubt sind echte JPG-, PNG- und WebP-Dateien bis 8 MB. Neue Fotos werden sofort veröffentlicht.</p><button class=\"button\" type=\"submit\">Foto hochladen</button></form></section><section class=\"gallery-list-card\"><div class=\"gallery-list-heading\"><div><h2>Vorhandene Fotos</h2><p>Ziehe die Fotos in die gewünschte Reihenfolge. Änderungen werden automatisch gespeichert.</p></div><span class=\"gallery-order-status\" data-gallery-order-status aria-live=\"polite\"></span></div><div class=\"gallery-admin-list\" data-gallery-sort-list data-csrf-token=\"");
    append_html_escaped(page, csrf_token);
    str_cat_cstr(page, "\">");

    context.page = page;
    context.csrf_token = csrf_token;
    context.count = 0;

    if (each_gallery_item(true, append_admin_gallery_item, &context) != 0) {
        free_str(page);
        return NULL;
    }

    if (context.count == 0) {
        str_cat_cstr(
                page,
                "<p class=\"gallery-empty\">Es wurden noch keine Fotos hochgeladen.</p>");
    }

    str_cat_cstr(
            page,
            "</div></section></div></section></main><footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styling 4 Dogs Admin.</small></div></footer></body></html>");
    return page;
}

gallery_result gallery_handle_upload(const string *request)
{
    char title[GALLERY_TITLE_SIZE + 1] = {0};
    char alt_text[GALLERY_ALT_SIZE + 1] = {0};
    char original_filename[GALLERY_FILENAME_SIZE + 1] = {0};
    char reported_content_type[GALLERY_CONTENT_TYPE_SIZE] = {0};
    char stored_name[GALLERY_FILENAME_SIZE + 1] = {0};
    const char *image_data = NULL;
    const char *detected_mime_type = NULL;
    const char *detected_extension = NULL;
    size_t image_length = 0;
    int sort_order = 0;

    gallery_extract_multipart_text_field(request, "title", title, sizeof(title));
    gallery_extract_multipart_text_field(request, "alt_text", alt_text, sizeof(alt_text));
    if (next_gallery_sort_order(&sort_order) != 0) return GALLERY_ERROR;

    if (!find_multipart_part(
            request,
            "image",
            &image_data,
            &image_length,
            original_filename,
            sizeof(original_filename),
            reported_content_type,
            sizeof(reported_content_type))) {
        set_error("Es wurde keine Bilddatei übertragen");
        return GALLERY_BAD_REQUEST;
    }

    trim_text(title);
    trim_text(alt_text);
    trim_text(original_filename);

    if (!text_has_only_allowed_controls(title) ||
        !text_has_only_allowed_controls(alt_text)) {
        set_error("Titel oder Alternativtext enthält ungültige Zeichen");
        return GALLERY_BAD_REQUEST;
    }

    if (original_filename[0] == '\0') {
        set_error("Die Bilddatei besitzt keinen Dateinamen");
        return GALLERY_BAD_REQUEST;
    }

    if (image_length == 0 || image_length > GALLERY_UPLOAD_MAX_BYTES) {
        set_error("Die Bilddatei ist leer oder größer als 8 MB");
        return GALLERY_BAD_REQUEST;
    }

    if (!detect_image_type(
            (const unsigned char *)image_data,
            image_length,
            &detected_mime_type,
            &detected_extension)) {
        set_error("Die Datei ist kein unterstütztes JPG-, PNG- oder WebP-Bild");
        return GALLERY_BAD_REQUEST;
    }

    if (!build_unique_file_name(
            detected_extension,
            stored_name,
            sizeof(stored_name))) {
        set_error("Ein sicherer Dateiname konnte nicht erzeugt werden");
        return GALLERY_ERROR;
    }

    if (insert_gallery_item(
            title,
            alt_text,
            stored_name,
            detected_mime_type,
            image_data,
            image_length,
            sort_order,
            true) != 0) {
        return GALLERY_ERROR;
    }

    return GALLERY_OK;
}

gallery_result gallery_handle_delete(const string *request)
{
    char image_id_text[32] = {0};
    char *end = NULL;
    long long image_id;
    int result;

    if (form_urlencoded_get(
            request,
            "image_id",
            image_id_text,
            sizeof(image_id_text)) != FORM_VALUE_OK) {
        set_error("Die Bild-ID fehlt");
        return GALLERY_BAD_REQUEST;
    }

    errno = 0;
    image_id = strtoll(image_id_text, &end, 10);
    if (errno != 0 || end == image_id_text || *end != '\0' || image_id <= 0) {
        set_error("Die Bild-ID ist ungültig");
        return GALLERY_BAD_REQUEST;
    }

    result = delete_gallery_item((int64_t)image_id);
    if (result == 1) {
        return GALLERY_NOT_FOUND;
    }
    if (result != 0) {
        return GALLERY_ERROR;
    }

    return GALLERY_OK;
}

gallery_result gallery_handle_reorder(const string *request)
{
    char order[8192] = {0};
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    char *cursor;
    int position = 0;
    int result = SQLITE_DONE;

    if (form_urlencoded_get(request, "order", order, sizeof(order)) != FORM_VALUE_OK || order[0] == '\0') {
        set_error("Die neue Galerie-Reihenfolge fehlt");
        return GALLERY_BAD_REQUEST;
    }
    if (gallery_open(&database) != 0) return GALLERY_ERROR;
    if (execute_sql(database, "BEGIN IMMEDIATE;") != 0) { gallery_close(database); return GALLERY_ERROR; }
    result = sqlite3_prepare_v2(database, "UPDATE gallery_images SET sort_order = ?1 WHERE id = ?2;", -1, &statement, NULL);
    if (result != SQLITE_OK) { set_sqlite_error(database, "Galerie-Reihenfolge konnte nicht vorbereitet werden"); execute_sql(database, "ROLLBACK;"); gallery_close(database); return GALLERY_ERROR; }

    cursor = order;
    while (*cursor != '\0') {
        char *separator = strchr(cursor, ',');
        char *end = NULL;
        long long image_id;
        if (separator != NULL) *separator = '\0';
        errno = 0;
        image_id = strtoll(cursor, &end, 10);
        if (errno != 0 || end == cursor || *end != '\0' || image_id <= 0) { result = SQLITE_MISMATCH; break; }
        sqlite3_reset(statement); sqlite3_clear_bindings(statement);
        sqlite3_bind_int(statement, 1, position++);
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)image_id);
        result = sqlite3_step(statement);
        if (result != SQLITE_DONE || sqlite3_changes(database) != 1) break;
        if (separator == NULL) break;
        cursor = separator + 1;
        if (*cursor == '\0') { result = SQLITE_MISMATCH; break; }
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) { execute_sql(database, "ROLLBACK;"); gallery_close(database); set_error("Die neue Galerie-Reihenfolge ist ungültig"); return GALLERY_BAD_REQUEST; }
    if (execute_sql(database, "COMMIT;") != 0) { execute_sql(database, "ROLLBACK;"); gallery_close(database); return GALLERY_ERROR; }
    gallery_close(database);
    return GALLERY_OK;
}

int gallery_read_media(
        const char *file_name,
        bool include_hidden,
        char **out_data,
        size_t *out_length,
        char *out_content_type,
        size_t out_content_type_size)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    int result;
    const void *blob;
    int blob_length;
    const unsigned char *mime_type;
    char *copy;

    if (file_name == NULL || file_name[0] == '\0' ||
        out_data == NULL || out_length == NULL ||
        out_content_type == NULL || out_content_type_size == 0) {
        set_error("Ungültige Medienanfrage");
        return -1;
    }

    *out_data = NULL;
    *out_length = 0;
    out_content_type[0] = '\0';

    if (strstr(file_name, "..") != NULL || strchr(file_name, '/') != NULL ||
        strchr(file_name, '\\') != NULL) {
        return 1;
    }

    if (gallery_open(&database) != 0) {
        return -1;
    }

    result = sqlite3_prepare_v2(
            database,
            include_hidden
                    ? "SELECT mime_type, image_data FROM gallery_images WHERE file_name = ?1 LIMIT 1;"
                    : "SELECT mime_type, image_data FROM gallery_images WHERE file_name = ?1 AND visible = 1 LIMIT 1;",
            -1,
            &statement,
            NULL);
    if (result != SQLITE_OK) {
        set_sqlite_error(database, "Galeriebild konnte nicht vorbereitet werden");
        gallery_close(database);
        return -1;
    }

    sqlite3_bind_text(statement, 1, file_name, -1, SQLITE_STATIC);
    result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        gallery_close(database);
        return 1;
    }
    if (result != SQLITE_ROW) {
        set_sqlite_error(database, "Galeriebild konnte nicht gelesen werden");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    mime_type = sqlite3_column_text(statement, 0);
    blob = sqlite3_column_blob(statement, 1);
    blob_length = sqlite3_column_bytes(statement, 1);
    if (mime_type == NULL || blob == NULL || blob_length <= 0) {
        set_error("Galeriebild ist unvollständig");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    copy = malloc((size_t)blob_length);
    if (copy == NULL) {
        set_error("Nicht genug Speicher für Galeriebild");
        sqlite3_finalize(statement);
        gallery_close(database);
        return -1;
    }

    memcpy(copy, blob, (size_t)blob_length);
    snprintf(
            out_content_type,
            out_content_type_size,
            "%s",
            (const char *)mime_type);

    *out_data = copy;
    *out_length = (size_t)blob_length;

    sqlite3_finalize(statement);
    gallery_close(database);
    return 0;
}
