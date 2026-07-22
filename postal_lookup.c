#include "postal_lookup.h"

#include "server_config.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define POSTAL_LOOKUP_ERROR_SIZE 512
#define POSTAL_LOOKUP_RESPONSE_LIMIT (64U * 1024U)
#define POSTAL_LOOKUP_CACHE_CAPACITY 64U
#define POSTAL_LOOKUP_CACHE_TTL_SECONDS (24U * 60U * 60U)
#define POSTAL_LOOKUP_EMPTY_CACHE_TTL_SECONDS (60U * 60U)

typedef struct postal_response_buffer {
    char *data;
    size_t length;
    size_t capacity;
    bool overflow;
} postal_response_buffer;

typedef struct postal_cache_entry {
    bool used;
    char postal_code[6];
    char *json;
    size_t json_length;
    time_t stored_at;
    time_t last_used_at;
} postal_cache_entry;

static char lookup_error[POSTAL_LOOKUP_ERROR_SIZE];
static postal_cache_entry cache_entries[POSTAL_LOOKUP_CACHE_CAPACITY];
static bool curl_initialized;

static void set_error(const char *message)
{
    snprintf(
            lookup_error,
            sizeof(lookup_error),
            "%s",
            message == NULL ? "Unbekannter PLZ-Abfragefehler" : message);
}

static void set_curl_error(const char *context, CURLcode code)
{
    snprintf(
            lookup_error,
            sizeof(lookup_error),
            "%s: %s",
            context == NULL ? "PLZ-Abfrage fehlgeschlagen" : context,
            curl_easy_strerror(code));
}

const char *postal_lookup_last_error(void)
{
    return lookup_error[0] == '\0'
            ? "Unbekannter PLZ-Abfragefehler"
            : lookup_error;
}

static bool postal_code_is_valid(const char *postal_code)
{
    if (postal_code == NULL || strlen(postal_code) != 5) {
        return false;
    }

    for (size_t index = 0; index < 5; index++) {
        if (postal_code[index] < '0' || postal_code[index] > '9') {
            return false;
        }
    }

    return true;
}

static bool json_array_is_valid(const char *data, size_t length)
{
    size_t start = 0;
    size_t end = length;

    if (data == NULL) {
        return false;
    }

    while (start < end &&
           (data[start] == ' ' || data[start] == '\t' ||
            data[start] == '\r' || data[start] == '\n')) {
        start++;
    }

    while (end > start &&
           (data[end - 1] == ' ' || data[end - 1] == '\t' ||
            data[end - 1] == '\r' || data[end - 1] == '\n')) {
        end--;
    }

    return end > start && data[start] == '[' && data[end - 1] == ']';
}

static bool json_array_is_empty(const char *data, size_t length)
{
    size_t start = 0;
    size_t end = length;

    while (start < end &&
           (data[start] == ' ' || data[start] == '\t' ||
            data[start] == '\r' || data[start] == '\n')) {
        start++;
    }

    while (end > start &&
           (data[end - 1] == ' ' || data[end - 1] == '\t' ||
            data[end - 1] == '\r' || data[end - 1] == '\n')) {
        end--;
    }

    return end - start == 2 && data[start] == '[' && data[start + 1] == ']';
}

static size_t append_response(
        char *contents,
        size_t item_size,
        size_t item_count,
        void *user_data
)
{
    postal_response_buffer *buffer = user_data;
    size_t incoming;
    size_t required;
    size_t next_capacity;
    char *resized;

    if (buffer == NULL || item_size == 0 || item_count == 0) {
        return 0;
    }

    if (item_count > SIZE_MAX / item_size) {
        buffer->overflow = true;
        return 0;
    }

    incoming = item_size * item_count;

    if (incoming > POSTAL_LOOKUP_RESPONSE_LIMIT - buffer->length) {
        buffer->overflow = true;
        return 0;
    }

    required = buffer->length + incoming + 1;
    if (required > buffer->capacity) {
        next_capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;

        while (next_capacity < required) {
            if (next_capacity > POSTAL_LOOKUP_RESPONSE_LIMIT / 2) {
                next_capacity = POSTAL_LOOKUP_RESPONSE_LIMIT + 1;
                break;
            }
            next_capacity *= 2;
        }

        if (next_capacity > POSTAL_LOOKUP_RESPONSE_LIMIT + 1) {
            next_capacity = POSTAL_LOOKUP_RESPONSE_LIMIT + 1;
        }

        resized = realloc(buffer->data, next_capacity);
        if (resized == NULL) {
            return 0;
        }

        buffer->data = resized;
        buffer->capacity = next_capacity;
    }

    memcpy(buffer->data + buffer->length, contents, incoming);
    buffer->length += incoming;
    buffer->data[buffer->length] = '\0';
    return incoming;
}

static postal_cache_entry *find_cache_entry(
        const char *postal_code,
        time_t now
)
{
    for (size_t index = 0; index < POSTAL_LOOKUP_CACHE_CAPACITY; index++) {
        postal_cache_entry *entry = &cache_entries[index];
        unsigned int ttl;

        if (!entry->used || strcmp(entry->postal_code, postal_code) != 0) {
            continue;
        }

        ttl = json_array_is_empty(entry->json, entry->json_length)
                ? POSTAL_LOOKUP_EMPTY_CACHE_TTL_SECONDS
                : POSTAL_LOOKUP_CACHE_TTL_SECONDS;

        if (now < entry->stored_at ||
            (unsigned long)(now - entry->stored_at) >= ttl) {
            free(entry->json);
            memset(entry, 0, sizeof(*entry));
            return NULL;
        }

        entry->last_used_at = now;
        return entry;
    }

    return NULL;
}

static postal_cache_entry *allocate_cache_entry(time_t now)
{
    postal_cache_entry *candidate = NULL;

    for (size_t index = 0; index < POSTAL_LOOKUP_CACHE_CAPACITY; index++) {
        postal_cache_entry *entry = &cache_entries[index];

        if (!entry->used) {
            return entry;
        }

        if (candidate == NULL || entry->last_used_at < candidate->last_used_at) {
            candidate = entry;
        }
    }

    if (candidate != NULL) {
        free(candidate->json);
        memset(candidate, 0, sizeof(*candidate));
        candidate->last_used_at = now;
    }

    return candidate;
}

static void store_cache_entry(
        const char *postal_code,
        const char *json,
        size_t json_length,
        time_t now
)
{
    postal_cache_entry *entry = allocate_cache_entry(now);
    char *copy;

    if (entry == NULL) {
        return;
    }

    copy = malloc(json_length + 1);
    if (copy == NULL) {
        return;
    }

    memcpy(copy, json, json_length);
    copy[json_length] = '\0';

    entry->used = true;
    memcpy(entry->postal_code, postal_code, 6);
    entry->json = copy;
    entry->json_length = json_length;
    entry->stored_at = now;
    entry->last_used_at = now;
}

postal_lookup_result postal_lookup_fetch(
        const char *postal_code,
        string **out_json
)
{
    CURL *curl = NULL;
    CURLcode curl_result;
    postal_response_buffer response = {0};
    postal_cache_entry *cached;
    char url[1024];
    long status_code = 0;
    char *content_type = NULL;
    const char *base_url;
    time_t now;
    int written;
    postal_lookup_result result = POSTAL_LOOKUP_ERROR;

    lookup_error[0] = '\0';

    if (out_json == NULL) {
        set_error("Ausgabepuffer für PLZ-Abfrage fehlt");
        return POSTAL_LOOKUP_ERROR;
    }
    *out_json = NULL;

    if (!postal_code_is_valid(postal_code)) {
        set_error("Postleitzahl muss aus genau fünf Ziffern bestehen");
        return POSTAL_LOOKUP_INVALID_POSTAL_CODE;
    }

    now = time(NULL);
    cached = find_cache_entry(postal_code, now);
    if (cached != NULL) {
        *out_json = cpy_str(cached->json, cached->json_length);
        if (*out_json == NULL) {
            set_error("Zwischengespeicherte PLZ-Antwort konnte nicht kopiert werden");
            return POSTAL_LOOKUP_ERROR;
        }
        return POSTAL_LOOKUP_OK;
    }

    if (!curl_initialized) {
        curl_result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curl_result != CURLE_OK) {
            set_curl_error("HTTP-Bibliothek konnte nicht initialisiert werden", curl_result);
            return POSTAL_LOOKUP_UNAVAILABLE;
        }
        curl_initialized = true;
    }

    base_url = server_config_postal_lookup_base_url();
    written = snprintf(url, sizeof(url), "%s?postalCode=%s", base_url, postal_code);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        set_error("Konfigurierte PLZ-Abfrage-URL ist zu lang");
        return POSTAL_LOOKUP_ERROR;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        set_error("HTTP-Anfrage für PLZ-Abfrage konnte nicht vorbereitet werden");
        return POSTAL_LOOKUP_UNAVAILABLE;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Styling4Dogs/1.0 postal-code-lookup");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        set_curl_error("PLZ-Dienst ist nicht erreichbar", curl_result);
        result = POSTAL_LOOKUP_UNAVAILABLE;
        goto cleanup;
    }

    if (response.overflow) {
        set_error("Antwort des PLZ-Dienstes überschreitet das Größenlimit");
        result = POSTAL_LOOKUP_UNAVAILABLE;
        goto cleanup;
    }

    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK ||
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) != CURLE_OK) {
        set_error("Antwortstatus des PLZ-Dienstes konnte nicht geprüft werden");
        result = POSTAL_LOOKUP_UNAVAILABLE;
        goto cleanup;
    }

    if (status_code != 200 || response.data == NULL ||
        (content_type == NULL || strstr(content_type, "json") == NULL) ||
        !json_array_is_valid(response.data, response.length)) {
        set_error("PLZ-Dienst hat keine gültige JSON-Antwort geliefert");
        result = POSTAL_LOOKUP_UNAVAILABLE;
        goto cleanup;
    }

    *out_json = cpy_str(response.data, response.length);
    if (*out_json == NULL) {
        set_error("PLZ-Antwort konnte nicht gespeichert werden");
        result = POSTAL_LOOKUP_ERROR;
        goto cleanup;
    }

    store_cache_entry(postal_code, response.data, response.length, now);
    result = POSTAL_LOOKUP_OK;

cleanup:
    curl_easy_cleanup(curl);
    free(response.data);
    return result;
}

void postal_lookup_shutdown(void)
{
    for (size_t index = 0; index < POSTAL_LOOKUP_CACHE_CAPACITY; index++) {
        free(cache_entries[index].json);
        memset(&cache_entries[index], 0, sizeof(cache_entries[index]));
    }

    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = false;
    }
}
