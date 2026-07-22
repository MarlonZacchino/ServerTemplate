#include "booking_database.h"
#include "server_config.h"

#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATABASE_ERROR_SIZE 512
#define MAX_LEGACY_LINE 8192
#define LEGACY_FIELD_COUNT_V1 5
#define LEGACY_FIELD_COUNT_V2 10
#define BOOKING_STATUS_NEW "neu"
#define LEGACY_IMPORT_KEY "legacy_tsv_import_v1"

static sqlite3 *database = NULL;
static char database_error[DATABASE_ERROR_SIZE];

static void set_error_text(const char *message)
{
    if (message == NULL) {
        database_error[0] = '\0';
        return;
    }

    snprintf(database_error, sizeof(database_error), "%s", message);
}

static void set_sqlite_error(const char *context)
{
    const char *sqlite_message = database == NULL
            ? "SQLite-Datenbank ist nicht geöffnet"
            : sqlite3_errmsg(database);

    snprintf(
            database_error,
            sizeof(database_error),
            "%s: %s",
            context == NULL ? "SQLite-Fehler" : context,
            sqlite_message);
}

const char *booking_database_last_error(void)
{
    return database_error[0] == '\0' ? "Unbekannter Datenbankfehler" : database_error;
}

static bool is_memory_database_path(const char *path)
{
    if (path == NULL) {
        return false;
    }

    return strcmp(path, ":memory:") == 0 ||
           strncmp(path, "file::memory:", strlen("file::memory:")) == 0 ||
           (strncmp(path, "file:", strlen("file:")) == 0 &&
            strstr(path, "mode=memory") != NULL);
}

static const char *database_open_path(void)
{
    /*
     * Zwei Module verwenden getrennte SQLite-Verbindungen. Ein nacktes
     * :memory: würde deshalb zwei unabhängige Datenbanken erzeugen. Die
     * benannte Shared-Cache-URI hält beide Verbindungen im selben Prozess
     * auf derselben flüchtigen Datenbank.
     */
    if (strcmp(server_config_database_file(), ":memory:") == 0) {
        return "file:styles4dogs-runtime?mode=memory&cache=shared";
    }

    return server_config_database_file();
}

static int ensure_data_directory(void)
{
    struct stat directory_status;

    if (is_memory_database_path(server_config_database_file())) {
        return 0;
    }

    if (mkdir(server_config_data_dir(), 0750) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        snprintf(
                database_error,
                sizeof(database_error),
                "Datenverzeichnis konnte nicht erstellt werden: %s",
                strerror(errno));
        return -1;
    }

    if (lstat(server_config_data_dir(), &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode)) {
        set_error_text("Der konfigurierte Datenpfad ist kein reguläres Verzeichnis");
        return -1;
    }

    if (chmod(server_config_data_dir(), 0750) != 0) {
        snprintf(
                database_error,
                sizeof(database_error),
                "Berechtigungen des Datenverzeichnisses konnten nicht gesetzt werden: %s",
                strerror(errno));
        return -1;
    }

    return 0;
}

static int validate_database_path(void)
{
    struct stat file_status;

    if (is_memory_database_path(server_config_database_file())) {
        return 0;
    }

    if (lstat(server_config_database_file(), &file_status) != 0) {
        if (errno == ENOENT) {
            return 0;
        }

        snprintf(
                database_error,
                sizeof(database_error),
                "Datenbankpfad konnte nicht geprüft werden: %s",
                strerror(errno));
        return -1;
    }

    if (!S_ISREG(file_status.st_mode)) {
        set_error_text("Der konfigurierte Datenbankpfad ist keine reguläre Datei");
        return -1;
    }

    return 0;
}

static int execute_sql(const char *sql)
{
    char *error_message = NULL;
    int result;

    result = sqlite3_exec(database, sql, NULL, NULL, &error_message);

    if (result != SQLITE_OK) {
        snprintf(
                database_error,
                sizeof(database_error),
                "SQLite-Anweisung fehlgeschlagen: %s",
                error_message == NULL ? sqlite3_errmsg(database) : error_message);
        sqlite3_free(error_message);
        return -1;
    }

    return 0;
}

static int configure_database(void)
{
    if (sqlite3_busy_timeout(database, 5000) != SQLITE_OK) {
        set_sqlite_error("SQLite Busy-Timeout konnte nicht gesetzt werden");
        return -1;
    }

    if (execute_sql("PRAGMA foreign_keys = ON;") != 0 ||
        execute_sql("PRAGMA temp_store = MEMORY;") != 0 ||
        execute_sql("PRAGMA synchronous = FULL;") != 0) {
        return -1;
    }

    if (is_memory_database_path(server_config_database_file())) {
        return execute_sql("PRAGMA journal_mode = MEMORY;");
    }

    return execute_sql("PRAGMA journal_mode = DELETE;");
}

static int create_schema(void)
{
    static const char *schema_sql =
            "BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS bookings ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    created_at TEXT NOT NULL,"
            "    status TEXT NOT NULL DEFAULT 'neu',"
            "    customer_name TEXT NOT NULL,"
            "    contact TEXT NOT NULL,"
            "    street_address TEXT NOT NULL DEFAULT '',"
            "    postal_code TEXT NOT NULL DEFAULT '',"
            "    city TEXT NOT NULL DEFAULT '',"
            "    dog_name TEXT NOT NULL DEFAULT '',"
            "    dog_size TEXT NOT NULL DEFAULT '',"
            "    service TEXT NOT NULL DEFAULT '',"
            "    preferred_date TEXT NOT NULL DEFAULT '',"
            "    message TEXT NOT NULL DEFAULT '',"
            "    legacy INTEGER NOT NULL DEFAULT 0 CHECK (legacy IN (0, 1))"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_bookings_created_at "
            "    ON bookings(created_at DESC, id DESC);"
            "CREATE INDEX IF NOT EXISTS idx_bookings_status "
            "    ON bookings(status);"
            "CREATE TRIGGER IF NOT EXISTS trg_bookings_status_insert "
            "BEFORE INSERT ON bookings "
            "WHEN NEW.status NOT IN ('neu', 'kontaktiert', 'erledigt', 'altbestand') "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking status'); END;"
            "CREATE TRIGGER IF NOT EXISTS trg_bookings_status_update "
            "BEFORE UPDATE OF status ON bookings "
            "WHEN NEW.status NOT IN ('neu', 'kontaktiert', 'erledigt', 'altbestand') "
            "BEGIN SELECT RAISE(ABORT, 'invalid booking status'); END;"
            "CREATE TABLE IF NOT EXISTS app_metadata ("
            "    key TEXT PRIMARY KEY,"
            "    value TEXT NOT NULL"
            ");"
            "COMMIT;";

    return execute_sql(schema_sql);
}

static int metadata_key_exists(const char *key, bool *out_exists)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (key == NULL || out_exists == NULL) {
        set_error_text("Metadatenschlüssel oder Ausgabewert fehlt");
        return -1;
    }

    *out_exists = false;

    if (sqlite3_prepare_v2(
            database,
            "SELECT 1 FROM app_metadata WHERE key = ?1 LIMIT 1;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Metadatenabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        set_sqlite_error("Metadatenschlüssel konnte nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    step_result = sqlite3_step(statement);

    if (step_result == SQLITE_ROW) {
        *out_exists = true;
    } else if (step_result != SQLITE_DONE) {
        set_sqlite_error("Metadatenabfrage konnte nicht ausgeführt werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

static int set_metadata_value(const char *key, const char *value)
{
    sqlite3_stmt *statement = NULL;
    int result = -1;

    if (sqlite3_prepare_v2(
            database,
            "INSERT INTO app_metadata(key, value) VALUES(?1, ?2) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Metadatenanweisung konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Metadaten konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Metadaten konnten nicht gespeichert werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static size_t split_legacy_line(
        char *line,
        char *fields[],
        size_t max_fields
)
{
    size_t count = 0;
    char *start;

    if (line == NULL || fields == NULL || max_fields == 0) {
        return 0;
    }

    start = line;

    for (char *position = line; *position != '\0'; position++) {
        if (*position == '\r' || *position == '\n') {
            *position = '\0';
            break;
        }

        if (*position == '\t') {
            if (count >= max_fields) {
                return max_fields + 1;
            }

            *position = '\0';
            fields[count++] = start;
            start = position + 1;
        }
    }

    if (count >= max_fields) {
        return max_fields + 1;
    }

    fields[count] = start;
    return count + 1;
}

static bool decode_legacy_field(
        const char *source,
        char *destination,
        size_t destination_size
)
{
    size_t output_index = 0;

    if (source == NULL || destination == NULL || destination_size == 0) {
        return false;
    }

    for (size_t index = 0; source[index] != '\0'; index++) {
        char character = source[index];

        if (character == '\\' && source[index + 1] != '\0') {
            index++;

            switch (source[index]) {
                case 'n':
                    character = '\n';
                    break;
                case 'r':
                    character = '\r';
                    break;
                case 't':
                    character = '\t';
                    break;
                case '\\':
                    character = '\\';
                    break;
                default:
                    if (output_index + 2 >= destination_size) {
                        return false;
                    }
                    destination[output_index++] = '\\';
                    character = source[index];
                    break;
            }
        }

        if (output_index + 1 >= destination_size) {
            return false;
        }

        destination[output_index++] = character;
    }

    destination[output_index] = '\0';
    return true;
}

typedef struct legacy_booking {
    char created_at[64];
    char status[32];
    char name[BOOKING_NAME_SIZE];
    char contact[BOOKING_CONTACT_SIZE];
    char dog_name[BOOKING_DOG_NAME_SIZE];
    char dog_size[BOOKING_DOG_SIZE_SIZE];
    char service[BOOKING_SERVICE_SIZE];
    char preferred_date[BOOKING_PREFERRED_DATE_SIZE];
    char message[BOOKING_MESSAGE_SIZE];
    bool legacy;
} legacy_booking;

static bool parse_legacy_booking_line(char *line, legacy_booking *booking)
{
    char *fields[LEGACY_FIELD_COUNT_V2];
    size_t field_count;

    if (line == NULL || booking == NULL) {
        return false;
    }

    memset(booking, 0, sizeof(*booking));
    field_count = split_legacy_line(line, fields, LEGACY_FIELD_COUNT_V2);

    if (field_count == LEGACY_FIELD_COUNT_V1) {
        booking->legacy = true;

        return decode_legacy_field(fields[0], booking->created_at, sizeof(booking->created_at)) &&
               decode_legacy_field("altbestand", booking->status, sizeof(booking->status)) &&
               decode_legacy_field(fields[1], booking->name, sizeof(booking->name)) &&
               decode_legacy_field(fields[2], booking->contact, sizeof(booking->contact)) &&
               decode_legacy_field(fields[3], booking->dog_name, sizeof(booking->dog_name)) &&
               decode_legacy_field("", booking->dog_size, sizeof(booking->dog_size)) &&
               decode_legacy_field("", booking->service, sizeof(booking->service)) &&
               decode_legacy_field("", booking->preferred_date, sizeof(booking->preferred_date)) &&
               decode_legacy_field(fields[4], booking->message, sizeof(booking->message));
    }

    if (field_count == LEGACY_FIELD_COUNT_V2 && strcmp(fields[0], "v2") == 0) {
        booking->legacy = false;

        return decode_legacy_field(fields[1], booking->created_at, sizeof(booking->created_at)) &&
               decode_legacy_field(fields[2], booking->status, sizeof(booking->status)) &&
               decode_legacy_field(fields[3], booking->name, sizeof(booking->name)) &&
               decode_legacy_field(fields[4], booking->contact, sizeof(booking->contact)) &&
               decode_legacy_field(fields[5], booking->dog_name, sizeof(booking->dog_name)) &&
               decode_legacy_field(fields[6], booking->dog_size, sizeof(booking->dog_size)) &&
               decode_legacy_field(fields[7], booking->service, sizeof(booking->service)) &&
               decode_legacy_field(fields[8], booking->preferred_date, sizeof(booking->preferred_date)) &&
               decode_legacy_field(fields[9], booking->message, sizeof(booking->message));
    }

    return false;
}

static int bind_text(sqlite3_stmt *statement, int index, const char *value)
{
    return sqlite3_bind_text(
            statement,
            index,
            value == NULL ? "" : value,
            -1,
            SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
}

static int insert_values(
        sqlite3_stmt *statement,
        const char *created_at,
        const char *status,
        const char *name,
        const char *contact,
        const char *dog_name,
        const char *dog_size,
        const char *service,
        const char *preferred_date,
        const char *message,
        bool legacy
)
{
    if (sqlite3_reset(statement) != SQLITE_OK ||
        sqlite3_clear_bindings(statement) != SQLITE_OK ||
        bind_text(statement, 1, created_at) != 0 ||
        bind_text(statement, 2, status) != 0 ||
        bind_text(statement, 3, name) != 0 ||
        bind_text(statement, 4, contact) != 0 ||
        bind_text(statement, 5, dog_name) != 0 ||
        bind_text(statement, 6, dog_size) != 0 ||
        bind_text(statement, 7, service) != 0 ||
        bind_text(statement, 8, preferred_date) != 0 ||
        bind_text(statement, 9, message) != 0 ||
        sqlite3_bind_int(statement, 10, legacy ? 1 : 0) != SQLITE_OK) {
        set_sqlite_error("Buchungswerte konnten nicht gebunden werden");
        return -1;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Buchung konnte nicht gespeichert werden");
        return -1;
    }

    return 0;
}

static int import_legacy_tsv(void)
{
    int open_flags = O_RDONLY;
    int file_descriptor;
    FILE *file = NULL;
    struct stat file_status;
    sqlite3_stmt *insert_statement = NULL;
    char line[MAX_LEGACY_LINE];
    size_t line_number = 0;
    size_t imported_count = 0;
    int result = -1;

    {
        bool import_completed = false;

        if (metadata_key_exists(LEGACY_IMPORT_KEY, &import_completed) != 0) {
            return -1;
        }

        if (import_completed) {
            return 0;
        }
    }

#ifdef O_CLOEXEC
    open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    open_flags |= O_NOFOLLOW;
#endif

    file_descriptor = open(server_config_legacy_booking_file(), open_flags);

    if (file_descriptor < 0) {
        if (errno == ENOENT) {
            return 0;
        }

        snprintf(
                database_error,
                sizeof(database_error),
                "Alte Buchungsdatei konnte nicht geöffnet werden: %s",
                strerror(errno));
        return -1;
    }

    if (fstat(file_descriptor, &file_status) != 0 ||
        !S_ISREG(file_status.st_mode)) {
        close(file_descriptor);
        set_error_text("Die alte Buchungsdatei ist keine reguläre Datei");
        return -1;
    }

    file = fdopen(file_descriptor, "r");

    if (file == NULL) {
        close(file_descriptor);
        snprintf(
                database_error,
                sizeof(database_error),
                "Alte Buchungsdatei konnte nicht gelesen werden: %s",
                strerror(errno));
        return -1;
    }

    if (execute_sql("BEGIN IMMEDIATE;") != 0) {
        goto cleanup;
    }

    if (sqlite3_prepare_v2(
            database,
            "INSERT INTO bookings("
            "created_at, status, customer_name, contact, dog_name, dog_size, "
            "service, preferred_date, message, legacy"
            ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);",
            -1,
            &insert_statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Importanweisung konnte nicht vorbereitet werden");
        goto rollback;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        legacy_booking booking;
        size_t line_length;

        line_number++;
        line_length = strlen(line);

        if (line_length == 0 || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        if (line[line_length - 1] != '\n' && !feof(file)) {
            snprintf(
                    database_error,
                    sizeof(database_error),
                    "Alte Buchungsdatei: Zeile %zu überschreitet das Größenlimit",
                    line_number);
            goto rollback;
        }

        if (!parse_legacy_booking_line(line, &booking)) {
            snprintf(
                    database_error,
                    sizeof(database_error),
                    "Alte Buchungsdatei: Zeile %zu hat ein ungültiges Format",
                    line_number);
            goto rollback;
        }

        if (insert_values(
                insert_statement,
                booking.created_at,
                booking.status,
                booking.name,
                booking.contact,
                booking.dog_name,
                booking.dog_size,
                booking.service,
                booking.preferred_date,
                booking.message,
                booking.legacy) != 0) {
            goto rollback;
        }

        imported_count++;
    }

    if (ferror(file)) {
        set_error_text("Alte Buchungsdatei konnte nicht vollständig gelesen werden");
        goto rollback;
    }

    {
        char metadata_value[128];
        snprintf(
                metadata_value,
                sizeof(metadata_value),
                "completed:%zu",
                imported_count);

        if (set_metadata_value(LEGACY_IMPORT_KEY, metadata_value) != 0) {
            goto rollback;
        }
    }

    if (execute_sql("COMMIT;") != 0) {
        goto rollback_without_statement;
    }

    result = 0;
    goto cleanup;

rollback:
    sqlite3_finalize(insert_statement);
    insert_statement = NULL;
rollback_without_statement:
    sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(insert_statement);
    fclose(file);
    return result;
}

int booking_database_initialize(void)
{
    int open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    mode_t previous_umask;

    if (database != NULL) {
        return 0;
    }

    set_error_text(NULL);

    if (ensure_data_directory() != 0 || validate_database_path() != 0) {
        return -1;
    }

    if (strncmp(database_open_path(), "file:", strlen("file:")) == 0) {
        open_flags |= SQLITE_OPEN_URI;
    }

    previous_umask = umask(0077);
    if (sqlite3_open_v2(database_open_path(), &database, open_flags, NULL) != SQLITE_OK) {
        umask(previous_umask);
        set_sqlite_error("SQLite-Datenbank konnte nicht geöffnet werden");
        booking_database_shutdown();
        return -1;
    }
    umask(previous_umask);

    if (!is_memory_database_path(server_config_database_file()) &&
        chmod(server_config_database_file(), 0600) != 0) {
        snprintf(
                database_error,
                sizeof(database_error),
                "Datenbankberechtigungen konnten nicht gesetzt werden: %s",
                strerror(errno));
        booking_database_shutdown();
        return -1;
    }

    if (configure_database() != 0 ||
        create_schema() != 0 ||
        import_legacy_tsv() != 0) {
        booking_database_shutdown();
        return -1;
    }

    return 0;
}

void booking_database_shutdown(void)
{
    if (database == NULL) {
        return;
    }

    if (sqlite3_close_v2(database) != SQLITE_OK) {
        set_sqlite_error("SQLite-Datenbank konnte nicht sauber geschlossen werden");
        return;
    }

    database = NULL;
}

static bool booking_calendar_columns_exist(void)
{
    sqlite3_stmt *statement = NULL;
    bool service_id_found = false;
    bool decision_status_found = false;
    int step_result;

    if (sqlite3_prepare_v2(
            database,
            "PRAGMA table_info(bookings);",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        return false;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(statement, 1);

        if (name == NULL) {
            continue;
        }

        if (strcmp((const char *)name, "service_id") == 0) {
            service_id_found = true;
        } else if (strcmp((const char *)name, "decision_status") == 0) {
            decision_status_found = true;
        }
    }

    sqlite3_finalize(statement);
    return step_result == SQLITE_DONE && service_id_found && decision_status_found;
}

int booking_database_insert(const booking_request *booking)
{
    sqlite3_stmt *statement = NULL;
    time_t now;
    struct tm local_time;
    char created_at[64];
    int result = -1;

    if (database == NULL || booking == NULL) {
        set_error_text("Datenbank ist nicht initialisiert oder Buchung fehlt");
        return -1;
    }

    now = time(NULL);

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(created_at, sizeof(created_at), "%Y-%m-%d %H:%M:%S", &local_time);
    } else {
        snprintf(created_at, sizeof(created_at), "%ld", (long)now);
    }

    const char *insert_sql = booking_calendar_columns_exist()
            ? "INSERT INTO bookings("
              "created_at, status, customer_name, contact, street_address, postal_code, city, "
              "dog_name, dog_size, service, preferred_date, message, legacy, service_id, decision_status"
              ") VALUES("
              "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, 0, "
              "(SELECT id FROM services WHERE code = ?10), 'legacy'"
              ");"
            : "INSERT INTO bookings("
              "created_at, status, customer_name, contact, street_address, postal_code, city, "
              "dog_name, dog_size, service, preferred_date, message, legacy"
              ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, 0);";

    if (sqlite3_prepare_v2(
            database,
            insert_sql,
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Buchungsanweisung konnte nicht vorbereitet werden");
        return -1;
    }

    if (bind_text(statement, 1, created_at) != 0 ||
        bind_text(statement, 2, BOOKING_STATUS_NEW) != 0 ||
        bind_text(statement, 3, booking->name) != 0 ||
        bind_text(statement, 4, booking->contact) != 0 ||
        bind_text(statement, 5, booking->street_address) != 0 ||
        bind_text(statement, 6, booking->postal_code) != 0 ||
        bind_text(statement, 7, booking->city) != 0 ||
        bind_text(statement, 8, booking->dog_name) != 0 ||
        bind_text(statement, 9, booking->dog_size) != 0 ||
        bind_text(statement, 10, booking->service) != 0 ||
        bind_text(statement, 11, booking->preferred_date) != 0 ||
        bind_text(statement, 12, booking->message) != 0) {
        set_sqlite_error("Buchungswerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Buchung konnte nicht gespeichert werden");
        goto cleanup;
    }

    result = 0;

cleanup:
    sqlite3_finalize(statement);
    return result;
}

static const char *column_text_or_empty(sqlite3_stmt *statement, int column)
{
    const unsigned char *value = sqlite3_column_text(statement, column);
    return value == NULL ? "" : (const char *)value;
}

static int build_literal_like_pattern(
        const char *search,
        char *out_pattern,
        size_t out_size
)
{
    size_t out_position = 0;

    if (search == NULL || out_pattern == NULL || out_size < 3) {
        set_error_text("Ungültiger Suchwert oder Ausgabepuffer");
        return -1;
    }

    out_pattern[out_position++] = '%';

    for (size_t index = 0; search[index] != '\0'; index++) {
        char character = search[index];

        if (character == '%' || character == '_' || character == '\\') {
            if (out_position + 2 >= out_size) {
                set_error_text("Suchwert ist zu lang");
                return -1;
            }

            out_pattern[out_position++] = '\\';
        } else if (out_position + 1 >= out_size) {
            set_error_text("Suchwert ist zu lang");
            return -1;
        }

        out_pattern[out_position++] = character;
    }

    if (out_position + 2 > out_size) {
        set_error_text("Suchwert ist zu lang");
        return -1;
    }

    out_pattern[out_position++] = '%';
    out_pattern[out_position] = '\0';
    return 0;
}

int booking_database_for_each_filtered(
        const booking_admin_filter *filter,
        booking_record_callback callback,
        void *context
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    char search_pattern[BOOKING_ADMIN_SEARCH_SIZE * 2 + 3];

    if (database == NULL || filter == NULL || callback == NULL) {
        set_error_text("Datenbank, Filter oder Callback fehlt");
        return -1;
    }

    if (build_literal_like_pattern(
            filter->search,
            search_pattern,
            sizeof(search_pattern)) != 0) {
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT id, created_at, status, customer_name, contact, street_address, postal_code, city, dog_name, "
            "       dog_size, service, preferred_date, message, legacy, "
            "       appointment_date, start_minute, end_minute, decision_status, hold_expires_at, "
            "       contact_channel, email, phone_number, phone_kind, contact_preference, "
            "       decision_at, rejection_reason, service_name_snapshot, "
            "       service_duration_minutes_snapshot, service_buffer_minutes_snapshot "
            "FROM bookings "
            "WHERE (?1 = '' OR status = ?1) "
            "  AND (?2 = '%%' OR customer_name LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR contact LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR email LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR phone_number LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR street_address LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR postal_code LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR city LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR dog_name LIKE ?2 ESCAPE '\\' COLLATE NOCASE "
            "       OR CAST(id AS TEXT) LIKE ?2 ESCAPE '\\') "
            "ORDER BY created_at DESC, id DESC;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Gefilterte Buchungsabfrage konnte nicht vorbereitet werden");
        return -1;
    }

    if (sqlite3_bind_text(
            statement,
            1,
            filter->status,
            -1,
            SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(
            statement,
            2,
            search_pattern,
            -1,
            SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Buchungsfilter konnten nicht gebunden werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        booking_record record = {
                .id = sqlite3_column_int64(statement, 0),
                .created_at = column_text_or_empty(statement, 1),
                .status = column_text_or_empty(statement, 2),
                .name = column_text_or_empty(statement, 3),
                .contact = column_text_or_empty(statement, 4),
                .street_address = column_text_or_empty(statement, 5),
                .postal_code = column_text_or_empty(statement, 6),
                .city = column_text_or_empty(statement, 7),
                .dog_name = column_text_or_empty(statement, 8),
                .dog_size = column_text_or_empty(statement, 9),
                .service = column_text_or_empty(statement, 10),
                .preferred_date = column_text_or_empty(statement, 11),
                .message = column_text_or_empty(statement, 12),
                .legacy = sqlite3_column_int(statement, 13) != 0,
                .appointment_date = column_text_or_empty(statement, 14),
                .start_minute = sqlite3_column_type(statement, 15) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 15),
                .end_minute = sqlite3_column_type(statement, 16) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 16),
                .decision_status = column_text_or_empty(statement, 17),
                .hold_expires_at = column_text_or_empty(statement, 18),
                .contact_channel = column_text_or_empty(statement, 19),
                .email = column_text_or_empty(statement, 20),
                .phone_number = column_text_or_empty(statement, 21),
                .phone_kind = column_text_or_empty(statement, 22),
                .contact_preference = column_text_or_empty(statement, 23),
                .decision_at = column_text_or_empty(statement, 24),
                .rejection_reason = column_text_or_empty(statement, 25),
                .service_name_snapshot = column_text_or_empty(statement, 26),
                .service_duration_minutes_snapshot = sqlite3_column_type(statement, 27) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 27),
                .service_buffer_minutes_snapshot = sqlite3_column_type(statement, 28) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 28)
        };

        callback(&record, context);
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Gefilterte Buchungsabfrage konnte nicht vollständig ausgeführt werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}


int booking_database_for_each_appointment(
        const char *from_date,
        const char *to_date,
        booking_record_callback callback,
        void *context
)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (database == NULL || from_date == NULL || to_date == NULL ||
        strlen(from_date) != 10 || strlen(to_date) != 10 ||
        strcmp(from_date, to_date) > 0 || callback == NULL) {
        set_error_text("Ungültige Terminbereichsabfrage");
        return -1;
    }

    if (sqlite3_prepare_v2(
            database,
            "SELECT id, created_at, status, customer_name, contact, street_address, postal_code, city, dog_name, "
            "       dog_size, service, preferred_date, message, legacy, "
            "       appointment_date, start_minute, end_minute, decision_status, hold_expires_at, "
            "       contact_channel, email, phone_number, phone_kind, contact_preference, "
            "       decision_at, rejection_reason, service_name_snapshot, "
            "       service_duration_minutes_snapshot, service_buffer_minutes_snapshot "
            "FROM bookings "
            "WHERE appointment_date BETWEEN ?1 AND ?2 "
            "  AND decision_status IN ('pending', 'confirmed') "
            "ORDER BY appointment_date, start_minute, id;",
            -1,
            &statement,
            NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, from_date, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, to_date, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        set_sqlite_error("Terminbereich konnte nicht vorbereitet werden");
        sqlite3_finalize(statement);
        return -1;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        booking_record record = {
                .id = sqlite3_column_int64(statement, 0),
                .created_at = column_text_or_empty(statement, 1),
                .status = column_text_or_empty(statement, 2),
                .name = column_text_or_empty(statement, 3),
                .contact = column_text_or_empty(statement, 4),
                .street_address = column_text_or_empty(statement, 5),
                .postal_code = column_text_or_empty(statement, 6),
                .city = column_text_or_empty(statement, 7),
                .dog_name = column_text_or_empty(statement, 8),
                .dog_size = column_text_or_empty(statement, 9),
                .service = column_text_or_empty(statement, 10),
                .preferred_date = column_text_or_empty(statement, 11),
                .message = column_text_or_empty(statement, 12),
                .legacy = sqlite3_column_int(statement, 13) != 0,
                .appointment_date = column_text_or_empty(statement, 14),
                .start_minute = sqlite3_column_type(statement, 15) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 15),
                .end_minute = sqlite3_column_type(statement, 16) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 16),
                .decision_status = column_text_or_empty(statement, 17),
                .hold_expires_at = column_text_or_empty(statement, 18),
                .contact_channel = column_text_or_empty(statement, 19),
                .email = column_text_or_empty(statement, 20),
                .phone_number = column_text_or_empty(statement, 21),
                .phone_kind = column_text_or_empty(statement, 22),
                .contact_preference = column_text_or_empty(statement, 23),
                .decision_at = column_text_or_empty(statement, 24),
                .rejection_reason = column_text_or_empty(statement, 25),
                .service_name_snapshot = column_text_or_empty(statement, 26),
                .service_duration_minutes_snapshot = sqlite3_column_type(statement, 27) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 27),
                .service_buffer_minutes_snapshot = sqlite3_column_type(statement, 28) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(statement, 28)
        };

        callback(&record, context);
    }

    if (step_result != SQLITE_DONE) {
        set_sqlite_error("Terminbereich konnte nicht vollständig gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);
    return 0;
}

int booking_database_get_status_counts(booking_status_counts *counts)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (database == NULL || counts == NULL) {
        set_error_text("Datenbank ist nicht initialisiert oder Zählerausgabe fehlt");
        return -1;
    }

    memset(counts, 0, sizeof(*counts));

    if (sqlite3_prepare_v2(
            database,
            "SELECT COUNT(*), "
            "       SUM(CASE WHEN status = 'neu' THEN 1 ELSE 0 END), "
            "       SUM(CASE WHEN status = 'kontaktiert' THEN 1 ELSE 0 END), "
            "       SUM(CASE WHEN status = 'erledigt' THEN 1 ELSE 0 END), "
            "       SUM(CASE WHEN status = 'altbestand' THEN 1 ELSE 0 END) "
            "FROM bookings;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Statuszähler konnten nicht vorbereitet werden");
        return -1;
    }

    step_result = sqlite3_step(statement);

    if (step_result != SQLITE_ROW) {
        set_sqlite_error("Statuszähler konnten nicht gelesen werden");
        sqlite3_finalize(statement);
        return -1;
    }

    counts->total = (size_t)sqlite3_column_int64(statement, 0);
    counts->new_count = (size_t)sqlite3_column_int64(statement, 1);
    counts->contacted_count = (size_t)sqlite3_column_int64(statement, 2);
    counts->completed_count = (size_t)sqlite3_column_int64(statement, 3);
    counts->legacy_count = (size_t)sqlite3_column_int64(statement, 4);

    sqlite3_finalize(statement);
    return 0;
}

static bool is_valid_mutable_status(const char *status)
{
    if (status == NULL) {
        return false;
    }

    return strcmp(status, "neu") == 0 ||
           strcmp(status, "kontaktiert") == 0 ||
           strcmp(status, "erledigt") == 0;
}

booking_status_update_result booking_database_update_status(
        int64_t booking_id,
        const char *status
)
{
    sqlite3_stmt *statement = NULL;
    booking_status_update_result result = BOOKING_STATUS_UPDATE_ERROR;

    if (database == NULL) {
        set_error_text("Datenbank ist nicht initialisiert");
        return BOOKING_STATUS_UPDATE_ERROR;
    }

    if (booking_id <= 0 || !is_valid_mutable_status(status)) {
        set_error_text("Ungültige Buchungs-ID oder ungültiger Status");
        return BOOKING_STATUS_UPDATE_ERROR;
    }

    if (sqlite3_prepare_v2(
            database,
            "UPDATE bookings SET status = ?1 WHERE id = ?2;",
            -1,
            &statement,
            NULL) != SQLITE_OK) {
        set_sqlite_error("Statusänderung konnte nicht vorbereitet werden");
        return BOOKING_STATUS_UPDATE_ERROR;
    }

    if (sqlite3_bind_text(statement, 1, status, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)booking_id) != SQLITE_OK) {
        set_sqlite_error("Statuswerte konnten nicht gebunden werden");
        goto cleanup;
    }

    if (sqlite3_step(statement) != SQLITE_DONE) {
        set_sqlite_error("Buchungsstatus konnte nicht geändert werden");
        goto cleanup;
    }

    result = sqlite3_changes(database) == 0
            ? BOOKING_STATUS_UPDATE_NOT_FOUND
            : BOOKING_STATUS_UPDATE_OK;

cleanup:
    sqlite3_finalize(statement);
    return result;
}
