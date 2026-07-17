//
// Created by Marlon on 17.07.26.
//

#include "auth.h"

#include <sodium.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef SERVER_AUTH_FILE
#define SERVER_AUTH_FILE ".secrets/admin.auth"
#endif

#define MAX_AUTH_LINE 1024
#define MAX_USERNAME_LENGTH 128
#define MAX_BASIC_TOKEN_LENGTH 1024
#define MAX_DECODED_AUTH_LENGTH 1024

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

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }

    return c;
}

static bool starts_with_ignore_case(const char *text, const char *prefix)
{
    size_t i = 0;

    while (prefix[i] != '\0') {
        if (text[i] == '\0') {
            return false;
        }

        if (ascii_lower(text[i]) != ascii_lower(prefix[i])) {
            return false;
        }

        i++;
    }

    return true;
}

static bool is_line_start(const char *data, size_t pos)
{
    return pos == 0 || data[pos - 1] == '\n';
}

static bool header_name_matches_at(
        const char *data,
        size_t length,
        size_t pos,
        const char *header_name
)
{
    size_t i = 0;

    while (header_name[i] != '\0') {
        if (pos + i >= length) {
            return false;
        }

        if (ascii_lower(data[pos + i]) != ascii_lower(header_name[i])) {
            return false;
        }

        i++;
    }

    return pos + i < length && data[pos + i] == ':';
}

static const char *find_basic_auth_token(const string *request, size_t *out_token_length)
{
    const char *data;
    size_t length;
    const char *header_name = "Authorization";
    size_t header_name_length = strlen(header_name);

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

    for (size_t pos = 0; pos < length; pos++) {
        size_t value_start;
        size_t value_end;

        if (!is_line_start(data, pos)) {
            continue;
        }

        if (!header_name_matches_at(data, length, pos, header_name)) {
            continue;
        }

        value_start = pos + header_name_length + 1;

        while (value_start < length &&
               (data[value_start] == ' ' || data[value_start] == '\t')) {
            value_start++;
        }

        if (!starts_with_ignore_case(data + value_start, "Basic ")) {
            return NULL;
        }

        value_start += strlen("Basic ");

        while (value_start < length &&
               (data[value_start] == ' ' || data[value_start] == '\t')) {
            value_start++;
        }

        value_end = value_start;

        while (value_end < length &&
               data[value_end] != '\r' &&
               data[value_end] != '\n') {
            value_end++;
        }

        while (value_end > value_start &&
               (data[value_end - 1] == ' ' || data[value_end - 1] == '\t')) {
            value_end--;
        }

        *out_token_length = value_end - value_start;
        return data + value_start;
    }

    return NULL;
}

static bool decode_basic_auth_token(
        const char *token,
        size_t token_length,
        char *decoded,
        size_t decoded_size
)
{
    size_t decoded_length = 0;

    if (token == NULL || decoded == NULL || decoded_size == 0) {
        return false;
    }

    if (token_length == 0 || token_length > MAX_BASIC_TOKEN_LENGTH) {
        return false;
    }

    if (sodium_base642bin(
            (unsigned char *)decoded,
            decoded_size - 1,
            token,
            token_length,
            NULL,
            &decoded_length,
            NULL,
            sodium_base64_VARIANT_ORIGINAL) != 0) {

        if (sodium_base642bin(
                (unsigned char *)decoded,
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

    return true;
}

static bool load_admin_auth_file(
        char *username,
        size_t username_size,
        char *password_hash,
        size_t password_hash_size
)
{
    FILE *file;
    char line[MAX_AUTH_LINE];
    char *colon;
    char *line_end;

    if (username == NULL || password_hash == NULL) {
        return false;
    }

    file = fopen(SERVER_AUTH_FILE, "r");

    if (file == NULL) {
        return false;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return false;
    }

    fclose(file);

    line_end = strpbrk(line, "\r\n");

    if (line_end != NULL) {
        *line_end = '\0';
    }

    colon = strchr(line, ':');

    if (colon == NULL) {
        return false;
    }

    *colon = '\0';

    if (line[0] == '\0' || colon[1] == '\0') {
        return false;
    }

    if (snprintf(username, username_size, "%s", line) >= (int)username_size) {
        return false;
    }

    if (snprintf(password_hash, password_hash_size, "%s", colon + 1)
        >= (int)password_hash_size) {
        return false;
    }

    return true;
}

static bool username_equals(const char *a, const char *b)
{
    size_t a_length;
    size_t b_length;

    if (a == NULL || b == NULL) {
        return false;
    }

    a_length = strlen(a);
    b_length = strlen(b);

    if (a_length != b_length) {
        return false;
    }

    return sodium_memcmp(a, b, a_length) == 0;
}

bool request_has_valid_admin_auth(const string *request)
{
    const char *token;
    size_t token_length;

    char decoded[MAX_DECODED_AUTH_LENGTH];
    char expected_username[MAX_USERNAME_LENGTH];
    char stored_hash[crypto_pwhash_STRBYTES];

    char *colon;
    char *username;
    char *password;

    if (!init_crypto()) {
        return false;
    }

    if (!load_admin_auth_file(
            expected_username,
            sizeof(expected_username),
            stored_hash,
            sizeof(stored_hash))) {
        return false;
    }

    token = find_basic_auth_token(request, &token_length);

    if (token == NULL || token_length == 0) {
        return false;
    }

    if (!decode_basic_auth_token(token, token_length, decoded, sizeof(decoded))) {
        return false;
    }

    colon = strchr(decoded, ':');

    if (colon == NULL) {
        return false;
    }

    *colon = '\0';

    username = decoded;
    password = colon + 1;

    if (!username_equals(username, expected_username)) {
        return false;
    }

    return crypto_pwhash_str_verify(stored_hash, password, strlen(password)) == 0;
}