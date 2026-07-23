#include "styles4dogs/core/server_config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef SERVER_DEFAULT_BIND_ADDRESS
#define SERVER_DEFAULT_BIND_ADDRESS "127.0.0.1"
#endif

#ifndef SERVER_DEFAULT_PORT
#define SERVER_DEFAULT_PORT "31337"
#endif

#ifndef SERVER_DEFAULT_DOCUMENT_ROOT
#define SERVER_DEFAULT_DOCUMENT_ROOT "public"
#endif

#ifndef SERVER_DEFAULT_SECRETS_DIR
#define SERVER_DEFAULT_SECRETS_DIR ".secrets"
#endif

#ifndef SERVER_DEFAULT_AUTH_FILE
#define SERVER_DEFAULT_AUTH_FILE ".secrets/admin.auth"
#endif

#ifndef SERVER_DEFAULT_DATA_DIR
#define SERVER_DEFAULT_DATA_DIR "data"
#endif

#ifndef SERVER_DEFAULT_DATABASE_FILE
#define SERVER_DEFAULT_DATABASE_FILE "data/styles4dogs.db"
#endif

#ifndef SERVER_DEFAULT_LEGACY_BOOKING_FILE
#define SERVER_DEFAULT_LEGACY_BOOKING_FILE "data/bookings.txt"
#endif

#define CONFIG_ERROR_SIZE 512
#define CONFIG_ADDRESS_SIZE INET_ADDRSTRLEN
#define CONFIG_PROXY_TOKEN_SIZE 129
#define CONFIG_SALON_NAME_SIZE 128
#define CONFIG_SALON_ADDRESS_SIZE 256
#define CONFIG_SALON_PHONE_SIZE 64
#define CONFIG_PUBLIC_BASE_URL_SIZE 256
#define CONFIG_COUNTRY_CODE_SIZE 5
#define CONFIG_POSTAL_LOOKUP_BASE_URL_SIZE 512

typedef struct server_config_state {
    bool initialized;
    char bind_address[CONFIG_ADDRESS_SIZE];
    uint16_t port;
    char document_root[PATH_MAX];
    char secrets_dir[PATH_MAX];
    char auth_file[PATH_MAX];
    char data_dir[PATH_MAX];
    char database_file[PATH_MAX];
    char legacy_booking_file[PATH_MAX];
    char trusted_proxy_token[CONFIG_PROXY_TOKEN_SIZE];
    char salon_name[CONFIG_SALON_NAME_SIZE];
    char salon_address[CONFIG_SALON_ADDRESS_SIZE];
    char salon_phone[CONFIG_SALON_PHONE_SIZE];
    char public_base_url[CONFIG_PUBLIC_BASE_URL_SIZE];
    char default_phone_country_code[CONFIG_COUNTRY_CODE_SIZE];
    char postal_lookup_base_url[CONFIG_POSTAL_LOOKUP_BASE_URL_SIZE];
} server_config_state;

static server_config_state config;
static char config_error[CONFIG_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            config_error,
            sizeof(config_error),
            "%s",
            message == NULL ? "Unbekannter Konfigurationsfehler" : message);
}

static void set_error_with_value(
        const char *prefix,
        const char *value
)
{
    snprintf(
            config_error,
            sizeof(config_error),
            "%s: %s",
            prefix == NULL ? "Ungültiger Konfigurationswert" : prefix,
            value == NULL ? "(null)" : value);
}

static const char *environment_or_default(
        const char *environment_name,
        const char *default_value,
        bool *out_was_set
)
{
    const char *environment_value = getenv(environment_name);

    if (out_was_set != NULL) {
        *out_was_set = environment_value != NULL;
    }

    return environment_value == NULL ? default_value : environment_value;
}

static int copy_nonempty_value(
        char *destination,
        size_t destination_size,
        const char *value,
        const char *description
)
{
    size_t length;

    if (destination == NULL || destination_size == 0 || value == NULL) {
        set_error("Interner Fehler beim Laden der Serverkonfiguration");
        return -1;
    }

    length = strlen(value);

    if (length == 0) {
        set_error_with_value(description, "darf nicht leer sein");
        return -1;
    }

    if (length >= destination_size) {
        set_error_with_value(description, "ist zu lang");
        return -1;
    }

    memcpy(destination, value, length + 1);
    return 0;
}

static int join_path(
        char *destination,
        size_t destination_size,
        const char *directory,
        const char *filename,
        const char *description
)
{
    int written;
    bool needs_separator;

    if (directory == NULL || filename == NULL || directory[0] == '\0') {
        set_error("Interner Fehler beim Ableiten eines Konfigurationspfads");
        return -1;
    }

    needs_separator = directory[strlen(directory) - 1] != '/';

    written = snprintf(
            destination,
            destination_size,
            "%s%s%s",
            directory,
            needs_separator ? "/" : "",
            filename);

    if (written < 0 || (size_t)written >= destination_size) {
        set_error_with_value(description, "ist zu lang");
        return -1;
    }

    return 0;
}

static int parse_port(const char *value, uint16_t *out_port)
{
    unsigned long parsed_port = 0;

    if (value == NULL || out_port == NULL || value[0] == '\0') {
        set_error("STYLES4DOGS_PORT darf nicht leer sein");
        return -1;
    }

    for (size_t index = 0; value[index] != '\0'; index++) {
        unsigned int digit;

        if (value[index] < '0' || value[index] > '9') {
            set_error_with_value("Ungültiger STYLES4DOGS_PORT", value);
            return -1;
        }

        digit = (unsigned int)(value[index] - '0');

        if (parsed_port > (65535UL - digit) / 10UL) {
            set_error_with_value("STYLES4DOGS_PORT liegt außerhalb von 1 bis 65535", value);
            return -1;
        }

        parsed_port = parsed_port * 10UL + digit;
    }

    if (parsed_port == 0 || parsed_port > 65535UL) {
        set_error_with_value("STYLES4DOGS_PORT liegt außerhalb von 1 bis 65535", value);
        return -1;
    }

    *out_port = (uint16_t)parsed_port;
    return 0;
}

static int validate_bind_address(
        const char *value,
        char *destination,
        size_t destination_size
)
{
    struct in_addr parsed_address;

    if (value == NULL || destination == NULL) {
        set_error("Interner Fehler bei der Bind-Adressprüfung");
        return -1;
    }

    if (inet_pton(AF_INET, value, &parsed_address) != 1) {
        set_error_with_value("Ungültige IPv4-Adresse in STYLES4DOGS_BIND_ADDRESS", value);
        return -1;
    }

    if (inet_ntop(AF_INET, &parsed_address, destination, destination_size) == NULL) {
        set_error_with_value("Bind-Adresse konnte nicht normalisiert werden", value);
        return -1;
    }

    return 0;
}

static int validate_document_root(
        const char *value,
        char *destination,
        size_t destination_size
)
{
    char resolved_path[PATH_MAX];
    struct stat path_status;

    if (value == NULL || value[0] == '\0') {
        set_error("STYLES4DOGS_DOCUMENT_ROOT darf nicht leer sein");
        return -1;
    }

    if (realpath(value, resolved_path) == NULL) {
        snprintf(
                config_error,
                sizeof(config_error),
                "STYLES4DOGS_DOCUMENT_ROOT konnte nicht aufgelöst werden (%s): %s",
                value,
                strerror(errno));
        return -1;
    }

    if (stat(resolved_path, &path_status) != 0 || !S_ISDIR(path_status.st_mode)) {
        set_error_with_value(
                "STYLES4DOGS_DOCUMENT_ROOT ist kein Verzeichnis",
                resolved_path);
        return -1;
    }

    if (access(resolved_path, R_OK | X_OK) != 0) {
        snprintf(
                config_error,
                sizeof(config_error),
                "STYLES4DOGS_DOCUMENT_ROOT ist nicht lesbar: %s",
                strerror(errno));
        return -1;
    }

    return copy_nonempty_value(
            destination,
            destination_size,
            resolved_path,
            "STYLES4DOGS_DOCUMENT_ROOT");
}

static int validate_directory_if_present(
        const char *path,
        const char *description
)
{
    struct stat path_status;

    if (lstat(path, &path_status) == 0) {
        if (!S_ISDIR(path_status.st_mode)) {
            set_error_with_value(description, "ist kein reguläres Verzeichnis");
            return -1;
        }

        return 0;
    }

    if (errno == ENOENT) {
        return 0;
    }

    snprintf(
            config_error,
            sizeof(config_error),
            "%s konnte nicht geprüft werden: %s",
            description,
            strerror(errno));
    return -1;
}


static int load_trusted_proxy_token(void)
{
    const char *value = getenv("STYLES4DOGS_TRUSTED_PROXY_TOKEN");
    size_t length;

    config.trusted_proxy_token[0] = '\0';

    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    length = strlen(value);

    if (length < 32 || length >= sizeof(config.trusted_proxy_token)) {
        set_error(
                "STYLES4DOGS_TRUSTED_PROXY_TOKEN muss zwischen 32 und 128 Zeichen lang sein");
        return -1;
    }

    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)value[index];
        bool allowed =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_';

        if (!allowed) {
            set_error(
                    "STYLES4DOGS_TRUSTED_PROXY_TOKEN enthält ungültige Zeichen");
            return -1;
        }
    }

    memcpy(config.trusted_proxy_token, value, length + 1);
    return 0;
}


static int copy_optional_value(
        char *destination,
        size_t destination_size,
        const char *value,
        const char *description
)
{
    size_t length;

    if (destination == NULL || destination_size == 0 || value == NULL) {
        set_error("Interner Fehler beim Laden optionaler Serverkonfiguration");
        return -1;
    }

    length = strlen(value);
    if (length >= destination_size) {
        set_error_with_value(description, "ist zu lang");
        return -1;
    }

    memcpy(destination, value, length + 1);
    return 0;
}

static bool public_value_is_single_line(const char *value)
{
    if (value == NULL) {
        return false;
    }

    for (size_t index = 0; value[index] != '\0'; index++) {
        unsigned char character = (unsigned char)value[index];

        if (character < 32 || character == 127) {
            return false;
        }
    }

    return true;
}

static bool public_url_is_valid(const char *url)
{
    if (!public_value_is_single_line(url)) {
        return false;
    }

    return strncmp(url, "https://", strlen("https://")) == 0 ||
           strncmp(url, "http://", strlen("http://")) == 0;
}

static bool postal_lookup_url_is_valid(const char *url)
{
    return public_url_is_valid(url) &&
           strchr(url, '?') == NULL &&
           strchr(url, '#') == NULL;
}

static int load_public_identity(void)
{
    const char *country_code = environment_or_default(
            "STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE", "49", NULL);
    size_t country_length = strlen(country_code);

    if (copy_nonempty_value(
            config.salon_name,
            sizeof(config.salon_name),
            environment_or_default("STYLES4DOGS_SALON_NAME", "Styling 4 Dogs", NULL),
            "STYLES4DOGS_SALON_NAME") != 0 ||
        copy_optional_value(
            config.salon_address,
            sizeof(config.salon_address),
            environment_or_default("STYLES4DOGS_SALON_ADDRESS", "", NULL),
            "STYLES4DOGS_SALON_ADDRESS") != 0 ||
        copy_optional_value(
            config.salon_phone,
            sizeof(config.salon_phone),
            environment_or_default("STYLES4DOGS_SALON_PHONE", "", NULL),
            "STYLES4DOGS_SALON_PHONE") != 0 ||
        copy_nonempty_value(
            config.public_base_url,
            sizeof(config.public_base_url),
            environment_or_default(
                    "STYLES4DOGS_PUBLIC_BASE_URL",
                    "http://127.0.0.1:8080",
                    NULL),
            "STYLES4DOGS_PUBLIC_BASE_URL") != 0) {
        return -1;
    }

    if (!public_value_is_single_line(config.salon_name) ||
        !public_value_is_single_line(config.salon_address) ||
        !public_value_is_single_line(config.salon_phone) ||
        !public_url_is_valid(config.public_base_url)) {
        set_error("Salonangaben müssen einzeilig sein; die öffentliche URL muss mit http:// oder https:// beginnen");
        return -1;
    }

    if (country_length < 1 || country_length > 4) {
        set_error("STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE muss 1 bis 4 Ziffern enthalten");
        return -1;
    }

    for (size_t index = 0; index < country_length; index++) {
        if (country_code[index] < '0' || country_code[index] > '9') {
            set_error("STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE darf nur Ziffern enthalten");
            return -1;
        }
    }

    if (country_code[0] == '0') {
        set_error("STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE darf nicht mit 0 beginnen");
        return -1;
    }

    memcpy(config.default_phone_country_code, country_code, country_length + 1);
    return 0;
}

static int load_postal_lookup_configuration(void)
{
    const char *value = environment_or_default(
            "STYLES4DOGS_POSTAL_LOOKUP_BASE_URL",
            "https://openplzapi.org/de/Localities",
            NULL);

    if (copy_nonempty_value(
            config.postal_lookup_base_url,
            sizeof(config.postal_lookup_base_url),
            value,
            "STYLES4DOGS_POSTAL_LOOKUP_BASE_URL") != 0) {
        return -1;
    }

    if (!postal_lookup_url_is_valid(config.postal_lookup_base_url)) {
        set_error(
                "STYLES4DOGS_POSTAL_LOOKUP_BASE_URL muss mit http:// oder https:// beginnen und darf keine Query oder kein Fragment enthalten");
        return -1;
    }

    return 0;
}

static int load_file_path(
        char *destination,
        size_t destination_size,
        const char *environment_name,
        const char *default_value,
        bool parent_was_overridden,
        const char *parent_directory,
        const char *derived_filename,
        const char *description
)
{
    const char *environment_value = getenv(environment_name);

    if (environment_value != NULL) {
        return copy_nonempty_value(
                destination,
                destination_size,
                environment_value,
                description);
    }

    if (parent_was_overridden) {
        return join_path(
                destination,
                destination_size,
                parent_directory,
                derived_filename,
                description);
    }

    return copy_nonempty_value(
            destination,
            destination_size,
            default_value,
            description);
}

int server_config_initialize(void)
{
    const char *bind_address;
    const char *port;
    const char *document_root;
    const char *secrets_dir;
    const char *data_dir;
    const char *legacy_booking_environment;
    bool secrets_dir_was_set = false;
    bool data_dir_was_set = false;

    if (config.initialized) {
        return 0;
    }

    memset(&config, 0, sizeof(config));
    config_error[0] = '\0';

    bind_address = environment_or_default(
            "STYLES4DOGS_BIND_ADDRESS",
            SERVER_DEFAULT_BIND_ADDRESS,
            NULL);

    port = environment_or_default(
            "STYLES4DOGS_PORT",
            SERVER_DEFAULT_PORT,
            NULL);

    document_root = environment_or_default(
            "STYLES4DOGS_DOCUMENT_ROOT",
            SERVER_DEFAULT_DOCUMENT_ROOT,
            NULL);

    secrets_dir = environment_or_default(
            "STYLES4DOGS_SECRETS_DIR",
            SERVER_DEFAULT_SECRETS_DIR,
            &secrets_dir_was_set);

    data_dir = environment_or_default(
            "STYLES4DOGS_DATA_DIR",
            SERVER_DEFAULT_DATA_DIR,
            &data_dir_was_set);

    if (validate_bind_address(
            bind_address,
            config.bind_address,
            sizeof(config.bind_address)) != 0 ||
        parse_port(port, &config.port) != 0 ||
        validate_document_root(
            document_root,
            config.document_root,
            sizeof(config.document_root)) != 0 ||
        load_trusted_proxy_token() != 0 ||
        load_public_identity() != 0 ||
        load_postal_lookup_configuration() != 0 ||
        copy_nonempty_value(
            config.secrets_dir,
            sizeof(config.secrets_dir),
            secrets_dir,
            "STYLES4DOGS_SECRETS_DIR") != 0 ||
        copy_nonempty_value(
            config.data_dir,
            sizeof(config.data_dir),
            data_dir,
            "STYLES4DOGS_DATA_DIR") != 0) {
        return -1;
    }

    if (validate_directory_if_present(
            config.secrets_dir,
            "STYLES4DOGS_SECRETS_DIR") != 0 ||
        validate_directory_if_present(
            config.data_dir,
            "STYLES4DOGS_DATA_DIR") != 0) {
        return -1;
    }

    if (load_file_path(
            config.auth_file,
            sizeof(config.auth_file),
            "STYLES4DOGS_AUTH_FILE",
            SERVER_DEFAULT_AUTH_FILE,
            secrets_dir_was_set,
            config.secrets_dir,
            "admin.auth",
            "STYLES4DOGS_AUTH_FILE") != 0 ||
        load_file_path(
            config.database_file,
            sizeof(config.database_file),
            "STYLES4DOGS_DATABASE_FILE",
            SERVER_DEFAULT_DATABASE_FILE,
            data_dir_was_set,
            config.data_dir,
            "styles4dogs.db",
            "STYLES4DOGS_DATABASE_FILE") != 0) {
        return -1;
    }

    legacy_booking_environment = getenv("STYLES4DOGS_LEGACY_BOOKING_FILE");

    if (legacy_booking_environment == NULL) {
        /* Rückwärtskompatibler Alias für ältere Test-/Deployment-Skripte. */
        legacy_booking_environment = getenv("STYLES4DOGS_BOOKING_FILE");
    }

    if (legacy_booking_environment != NULL) {
        if (copy_nonempty_value(
                config.legacy_booking_file,
                sizeof(config.legacy_booking_file),
                legacy_booking_environment,
                "STYLES4DOGS_LEGACY_BOOKING_FILE") != 0) {
            return -1;
        }
    } else if (data_dir_was_set) {
        if (join_path(
                config.legacy_booking_file,
                sizeof(config.legacy_booking_file),
                config.data_dir,
                "bookings.txt",
                "STYLES4DOGS_LEGACY_BOOKING_FILE") != 0) {
            return -1;
        }
    } else if (copy_nonempty_value(
            config.legacy_booking_file,
            sizeof(config.legacy_booking_file),
            SERVER_DEFAULT_LEGACY_BOOKING_FILE,
            "STYLES4DOGS_LEGACY_BOOKING_FILE") != 0) {
        return -1;
    }

    config.initialized = true;
    return 0;
}

const char *server_config_last_error(void)
{
    return config_error[0] == '\0'
            ? "Serverkonfiguration wurde nicht initialisiert"
            : config_error;
}

const char *server_config_bind_address(void)
{
    return config.bind_address;
}

uint16_t server_config_port(void)
{
    return config.port;
}

const char *server_config_document_root(void)
{
    return config.document_root;
}

const char *server_config_secrets_dir(void)
{
    return config.secrets_dir;
}

const char *server_config_auth_file(void)
{
    return config.auth_file;
}

const char *server_config_data_dir(void)
{
    return config.data_dir;
}

const char *server_config_database_file(void)
{
    return config.database_file;
}

const char *server_config_legacy_booking_file(void)
{
    return config.legacy_booking_file;
}

const char *server_config_trusted_proxy_token(void)
{
    return config.trusted_proxy_token;
}

const char *server_config_salon_name(void)
{
    return config.salon_name;
}

const char *server_config_salon_address(void)
{
    return config.salon_address;
}

const char *server_config_salon_phone(void)
{
    return config.salon_phone;
}

const char *server_config_public_base_url(void)
{
    return config.public_base_url;
}

const char *server_config_default_phone_country_code(void)
{
    return config.default_phone_country_code;
}

const char *server_config_postal_lookup_base_url(void)
{
    return config.postal_lookup_base_url;
}
