//
// Created by Marlon on 03.07.26.
//

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "booking_database.h"
#include "calendar_database.h"
#include "http_lib.h"
#include "process.h"
#include "rate_limit.h"
#include "server_config.h"

/**
 * Maximale Request-Größe.
 * Galerie-Uploads dürfen bis zu 8 MiB groß sein. Der zusätzliche Spielraum
 * deckt Multipart-Header und Textfelder ab, ohne unbeschränkte Requests zu erlauben.
 */
#define BUFFER_SIZE (10 * 1024 * 1024)

/**
 * Timeout für Socket-Lese- und Schreiboperationen.
 */
#define SOCKET_TIMEOUT_SECONDS 5

#define CLIENT_IP_SIZE INET6_ADDRSTRLEN
#define REQUEST_METHOD_SIZE 16
#define REQUEST_PATH_SIZE 512

typedef enum request_kind {
    REQUEST_KIND_OTHER = 0,
    REQUEST_KIND_BOOKING,
    REQUEST_KIND_ADMIN
} request_kind;

/**
 * Verarbeitet einen Request und erzeugt eine Response.
 *
 * Wichtig:
 * - request gehört weiterhin dem Aufrufer.
 * - process() soll request nicht freigeben.
 * - process() soll idealerweise eine neue string* Response zurückgeben.
 */

/**
 * Globale Laufvariable für die Main-Loop.
 * Muss für Signal-Handler sig_atomic_t sein, nicht bool.
 */
static volatile sig_atomic_t run = 1;

/**
 * Gibt eine Fehlermeldung aus und beendet das Programm.
 */
static void fatal(const char *msg)
{
    int saved_errno = errno;

    fprintf(stderr, "%s", msg);

    if (saved_errno != 0) {
        fprintf(stderr, ", errno: %s", strerror(saved_errno));
    }

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

/**
 * Gibt eine Warnung aus, beendet das Programm aber nicht.
 */
static void warn_errno(const char *msg)
{
    int saved_errno = errno;

    fprintf(stderr, "%s", msg);

    if (saved_errno != 0) {
        fprintf(stderr, ", errno: %s", strerror(saved_errno));
    }

    fprintf(stderr, "\n");
}

/**
 * Signal-Handler für SIGINT/SIGTERM.
 */
static void handle_signal(int sig)
{
    (void)sig;
    run = 0;
}

/**
 * Registriert Signale für sauberes Beenden.
 */
static void register_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    /*
     * Kein SA_RESTART:
     * accept(), read(), write() sollen bei Strg+C mit EINTR abbrechen.
     */
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        fatal("ERROR registering SIGTERM");
    }

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        fatal("ERROR registering SIGINT");
    }

#ifdef SIGPIPE
    /*
     * Verhindert, dass der Server beendet wird, wenn der Client
     * die Verbindung schließt, während wir noch schreiben.
     */
    signal(SIGPIPE, SIG_IGN);
#endif
}

static size_t find_http_header_end(const char *buffer, size_t length)
{
    if (buffer == NULL || length < 2) {
        return 0;
    }

    for (size_t i = 0; i + 3 < length; i++) {
        if (buffer[i] == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {
            return i + 4;
            }
    }

    for (size_t i = 0; i + 1 < length; i++) {
        if (buffer[i] == '\n' && buffer[i + 1] == '\n') {
            return i + 2;
        }
    }

    return 0;
}

static int header_name_matches(const char *buffer, size_t pos, size_t length, const char *name)
{
    size_t name_pos = 0;

    while (name[name_pos] != '\0') {
        if (pos + name_pos >= length) {
            return 0;
        }

        char a = buffer[pos + name_pos];
        char b = name[name_pos];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }

        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }

        if (a != b) {
            return 0;
        }

        name_pos++;
    }

    return 1;
}

static size_t get_content_length_from_header(const char *buffer, size_t header_length)
{
    const char *header_name = "Content-Length:";
    size_t name_length = strlen(header_name);

    for (size_t pos = 0; pos + name_length < header_length; pos++) {
        if (!header_name_matches(buffer, pos, header_length, header_name)) {
            continue;
        }

        pos += name_length;

        while (pos < header_length && (buffer[pos] == ' ' || buffer[pos] == '\t')) {
            pos++;
        }

        size_t value = 0;

        while (pos < header_length && buffer[pos] >= '0' && buffer[pos] <= '9') {
            size_t digit = (size_t)(buffer[pos] - '0');

            if (value > (SIZE_MAX - digit) / 10) {
                return 0;
            }

            value = value * 10 + digit;
            pos++;
        }

        return value;
    }

    return 0;
}


static bool constant_time_equals(
        const char *a,
        size_t a_length,
        const char *b,
        size_t b_length
)
{
    unsigned char difference = 0;

    if (a == NULL || b == NULL || a_length != b_length) {
        return false;
    }

    for (size_t index = 0; index < a_length; index++) {
        difference |= (unsigned char)a[index] ^ (unsigned char)b[index];
    }

    return difference == 0;
}

/*
 * Finds exactly one HTTP header in the bounded header section.
 * Duplicate headers are rejected to avoid ambiguous proxy identity data.
 */
static bool find_unique_header_value(
        const char *buffer,
        size_t request_length,
        const char *header_name,
        const char **out_value,
        size_t *out_value_length
)
{
    size_t header_end;
    size_t position = 0;
    size_t found_count = 0;
    const char *found_value = NULL;
    size_t found_length = 0;
    size_t header_name_length;

    if (buffer == NULL || header_name == NULL ||
        out_value == NULL || out_value_length == NULL) {
        return false;
    }

    *out_value = NULL;
    *out_value_length = 0;
    header_end = find_http_header_end(buffer, request_length);

    if (header_end == 0) {
        return false;
    }

    header_name_length = strlen(header_name);

    while (position < header_end && buffer[position] != '\n') {
        position++;
    }

    if (position >= header_end) {
        return false;
    }

    position++;

    while (position < header_end) {
        size_t raw_line_end = position;
        size_t line_end;
        size_t colon;
        size_t value_start;
        size_t value_end;

        while (raw_line_end < header_end && buffer[raw_line_end] != '\n') {
            raw_line_end++;
        }

        line_end = raw_line_end;
        if (line_end > position && buffer[line_end - 1] == '\r') {
            line_end--;
        }

        if (line_end == position) {
            break;
        }

        colon = position;
        while (colon < line_end && buffer[colon] != ':') {
            colon++;
        }

        if (colon < line_end &&
            colon - position == header_name_length &&
            header_name_matches(buffer, position, line_end, header_name)) {
            found_count++;
            if (found_count > 1) {
                return false;
            }

            value_start = colon + 1;
            while (value_start < line_end &&
                   (buffer[value_start] == ' ' || buffer[value_start] == '\t')) {
                value_start++;
            }

            value_end = line_end;
            while (value_end > value_start &&
                   (buffer[value_end - 1] == ' ' || buffer[value_end - 1] == '\t')) {
                value_end--;
            }

            found_value = buffer + value_start;
            found_length = value_end - value_start;
        }

        if (raw_line_end >= header_end) {
            break;
        }
        position = raw_line_end + 1;
    }

    if (found_count != 1 || found_length == 0) {
        return false;
    }

    *out_value = found_value;
    *out_value_length = found_length;
    return true;
}

static bool normalize_ip_slice(
        const char *value,
        size_t value_length,
        char *destination,
        size_t destination_size
)
{
    char temporary[CLIENT_IP_SIZE];
    struct in_addr address_v4;
    struct in6_addr address_v6;

    while (value_length > 0 && (*value == ' ' || *value == '\t')) {
        value++;
        value_length--;
    }

    while (value_length > 0 &&
           (value[value_length - 1] == ' ' || value[value_length - 1] == '\t')) {
        value_length--;
    }

    if (value_length == 0 || value_length >= sizeof(temporary)) {
        return false;
    }

    memcpy(temporary, value, value_length);
    temporary[value_length] = '\0';

    if (inet_pton(AF_INET, temporary, &address_v4) == 1) {
        return inet_ntop(AF_INET, &address_v4, destination, destination_size) != NULL;
    }

    if (inet_pton(AF_INET6, temporary, &address_v6) == 1) {
        return inet_ntop(AF_INET6, &address_v6, destination, destination_size) != NULL;
    }

    return false;
}

static void resolve_client_ip(
        const char *buffer,
        size_t request_length,
        const struct sockaddr_in *peer_address,
        bool allow_trusted_proxy,
        char *destination,
        size_t destination_size
)
{
    const char *configured_token = server_config_trusted_proxy_token();
    const char *received_token;
    const char *forwarded_for;
    size_t received_token_length;
    size_t forwarded_for_length;
    size_t first_address_length;

    if (peer_address == NULL ||
        inet_ntop(AF_INET, &peer_address->sin_addr, destination, destination_size) == NULL) {
        snprintf(destination, destination_size, "127.0.0.1");
    }

    if (!allow_trusted_proxy ||
        strcmp(destination, "127.0.0.1") != 0 ||
        configured_token == NULL || configured_token[0] == '\0') {
        return;
    }

    if (!find_unique_header_value(
            buffer,
            request_length,
            "X-Styles4Dogs-Proxy-Token",
            &received_token,
            &received_token_length) ||
        !constant_time_equals(
            received_token,
            received_token_length,
            configured_token,
            strlen(configured_token)) ||
        !find_unique_header_value(
            buffer,
            request_length,
            "X-Forwarded-For",
            &forwarded_for,
            &forwarded_for_length)) {
        return;
    }

    first_address_length = 0;
    while (first_address_length < forwarded_for_length &&
           forwarded_for[first_address_length] != ',') {
        first_address_length++;
    }

    (void)normalize_ip_slice(
            forwarded_for,
            first_address_length,
            destination,
            destination_size);
}

static request_kind classify_request(
        const char *buffer,
        size_t request_length
)
{
    char method[REQUEST_METHOD_SIZE];
    char path[REQUEST_PATH_SIZE];
    size_t position = 0;
    size_t method_length = 0;
    size_t path_length = 0;

    if (buffer == NULL || request_length == 0) {
        return REQUEST_KIND_OTHER;
    }

    while (position < request_length && buffer[position] != ' ') {
        if (buffer[position] == '\r' || buffer[position] == '\n' ||
            method_length + 1 >= sizeof(method)) {
            return REQUEST_KIND_OTHER;
        }
        method[method_length++] = buffer[position++];
    }

    if (method_length == 0 || position >= request_length) {
        return REQUEST_KIND_OTHER;
    }
    method[method_length] = '\0';

    while (position < request_length && buffer[position] == ' ') {
        position++;
    }

    while (position < request_length && buffer[position] != ' ') {
        if (buffer[position] == '\r' || buffer[position] == '\n' ||
            path_length + 1 >= sizeof(path)) {
            return REQUEST_KIND_OTHER;
        }
        path[path_length++] = buffer[position++];
    }

    if (path_length == 0) {
        return REQUEST_KIND_OTHER;
    }
    path[path_length] = '\0';

    char *query = strchr(path, '?');
    if (query != NULL) {
        *query = '\0';
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/booking") == 0) {
        return REQUEST_KIND_BOOKING;
    }

    if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
         strcmp(method, "POST") == 0) &&
        strncmp(path, "/admin/", strlen("/admin/")) == 0) {
        return REQUEST_KIND_ADMIN;
    }

    return REQUEST_KIND_OTHER;
}

/**
 * Schreibt alle Bytes in fd.
 *
 * write() kann weniger Bytes schreiben als angefordert.
 * Deshalb wird hier in einer Schleife geschrieben.
 */
static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written_total = 0;

    while (written_total < length) {
        ssize_t written = write(fd, buffer + written_total, length - written_total);

        if (written > 0) {
            written_total += (size_t)written;
            continue;
        }

        if (written < 0 && errno == EINTR) {
            if (!run) {
                return -1;
            }
            continue;
        }

        return -1;
    }

    return 0;
}

/**
 * Liest einen Request von stdin.
 *
 * Wichtig für Tests und AFL:
 * Hier wird bis EOF gelesen, nicht nur einmal.
 */
static ssize_t read_from_stdin(char *buffer, size_t capacity)
{
    size_t total = 0;

    while (total < capacity) {
        ssize_t length = read(STDIN_FILENO, buffer + total, capacity - total);

        if (length > 0) {
            total += (size_t)length;
            continue;
        }

        if (length == 0) {
            break;
        }

        if (errno == EINTR) {
            if (!run) {
                return -1;
            }
            continue;
        }

        return -1;
    }

    return (ssize_t)total;
}

/**
 * Liest einen HTTP-Request vom Socket.
 *
 * Es wird gelesen, bis:
 * - der Header vollständig ist,
 * - der Client schließt,
 * - der Buffer voll ist,
 * - ein Timeout/Fehler auftritt.
 */
static ssize_t read_from_socket(int fd, char *buffer, size_t capacity)
{
    size_t total = 0;
    size_t header_end = 0;
    size_t content_length = 0;

    while (total < capacity) {
        ssize_t length = read(fd, buffer + total, capacity - total);

        if (length > 0) {
            total += (size_t)length;

            if (header_end == 0) {
                header_end = find_http_header_end(buffer, total);

                if (header_end > 0) {
                    content_length = get_content_length_from_header(buffer, header_end);

                    if (content_length > capacity - header_end) {
                        break;
                    }
                }
            }

            if (header_end > 0) {
                size_t expected_total = header_end + content_length;

                if (total >= expected_total) {
                    break;
                }
            }

            continue;
        }

        if (length == 0) {
            break;
        }

        if (errno == EINTR) {
            if (!run) {
                return -1;
            }

            continue;
        }

        if ((errno == EAGAIN || errno == EWOULDBLOCK) && total > 0) {
            break;
        }

        return -1;
    }

    return (ssize_t)total;
}

/**
 * Erstellt eine einfache 500-Response, falls process() NULL zurückgibt.
 */
static string *make_internal_error_response(void)
{
    const char *response =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "Content-Length: 21\r\n"
        "\r\n"
        "Internal Server Error";

    return cpy_str((void *)response, strlen(response));
}


static string *make_too_many_requests_response(unsigned int retry_after_seconds)
{
    const char *body = "Zu viele Anfragen. Bitte versuche es später erneut.\n";
    char response[512];
    int written;

    if (retry_after_seconds == 0) {
        retry_after_seconds = 1;
    }

    written = snprintf(
            response,
            sizeof(response),
            "HTTP/1.1 429 Too Many Requests\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Retry-After: %u\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            strlen(body),
            retry_after_seconds,
            body);

    if (written < 0 || (size_t)written >= sizeof(response)) {
        return make_internal_error_response();
    }

    return cpy_str(response, (size_t)written);
}

static bool response_has_status(const string *response, const char *status)
{
    const char *data;
    size_t response_length;
    size_t status_length;

    if (response == NULL || status == NULL) {
        return false;
    }

    data = get_const_char_str(response);
    response_length = get_length(response);
    status_length = strlen(status);

    return data != NULL && response_length >= status_length &&
           memcmp(data, status, status_length) == 0;
}

/**
 * Verarbeitet einen bereits gelesenen Request-Buffer.
 */
static int handle_raw_request(
        int output_fd,
        const char *buffer,
        size_t request_length,
        const char *client_ip
)
{
    request_kind kind = classify_request(buffer, request_length);
    unsigned int retry_after_seconds = 1;
    string *request = NULL;
    string *response = NULL;

    if (kind == REQUEST_KIND_BOOKING &&
        !rate_limit_allow_booking(client_ip, &retry_after_seconds)) {
        response = make_too_many_requests_response(retry_after_seconds);
    } else if (kind == REQUEST_KIND_ADMIN &&
               rate_limit_admin_is_blocked(client_ip, &retry_after_seconds)) {
        response = make_too_many_requests_response(retry_after_seconds);
    } else {
        request = cpy_str((void *)buffer, request_length);

        if (request == NULL) {
            return -1;
        }

        response = process(request);

        if (kind == REQUEST_KIND_ADMIN) {
            if (response_has_status(response, "HTTP/1.1 401 ")) {
                rate_limit_record_admin_failure(client_ip);
            } else if (response_has_status(response, "HTTP/1.1 200 ") ||
                       response_has_status(response, "HTTP/1.1 303 ")) {
                rate_limit_clear_admin_failures(client_ip);
            }
        }
    }

    if (response == NULL) {
        response = make_internal_error_response();
    }

    if (response == NULL) {
        if (request != NULL) {
            free_str(request);
        }
        return -1;
    }

    const size_t response_length = get_length(response);
    const char *response_chars = get_char_str(response);

    int result = write_all(output_fd, response_chars, response_length);

    /*
     * Normalfall:
     * request und response sind verschiedene Objekte.
     *
     * Sicherheitscheck:
     * Falls process() aus Versehen noch wie der alte Echo-Server einfach
     * request zurückgibt, vermeiden wir einen double-free.
     */
    if (response != request) {
        free_str(response);
    }

    if (request != NULL) {
        free_str(request);
    }

    return result;
}

/**
 * Verarbeitet stdin-Modus.
 *
 * Nützlich für:
 * - AFL++
 * - automatische Tests
 * - Debugging ohne Netzwerk
 */
static void main_loop_stdin(void)
{
    char *buffer = malloc(BUFFER_SIZE);

    if (buffer == NULL) {
        fatal("ERROR allocating stdin buffer");
    }

    ssize_t request_length = read_from_stdin(buffer, BUFFER_SIZE);

    if (request_length < 0) {
        free(buffer);
        fatal("ERROR reading from stdin");
    }

    if (handle_raw_request(
            STDOUT_FILENO,
            buffer,
            (size_t)request_length,
            "127.0.0.1") < 0) {
        free(buffer);
        fatal("ERROR writing response to stdout");
    }

    free(buffer);
}

/**
 * Setzt Socket-Timeouts.
 */
static void set_socket_timeouts(int fd)
{
    struct timeval timeout;

    timeout.tv_sec = SOCKET_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        warn_errno("WARNING setting receive timeout failed");
    }

    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        warn_errno("WARNING setting send timeout failed");
    }
}

/**
 * Erstellt und konfiguriert den Server-Socket.
 */
static int setup_socket(void)
{
    int opt = 1;
    int sockfd;
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;

    if (inet_pton(
            AF_INET,
            server_config_bind_address(),
            &serv_addr.sin_addr) != 1) {
        errno = EINVAL;
        fatal("ERROR converting configured bind address");
    }

    serv_addr.sin_port = htons(server_config_port());

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        fatal("ERROR opening socket");
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(sockfd);
        fatal("ERROR setting SO_REUSEADDR");
    }

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        fatal("ERROR binding socket");
    }

    if (listen(sockfd, SOMAXCONN) < 0) {
        close(sockfd);
        fatal("ERROR listening on socket");
    }

    return sockfd;
}

/**
 * Verarbeitet eine einzelne Client-Verbindung.
 */
static void handle_client(int client_fd, const struct sockaddr_in *client_address)
{
    char client_ip[CLIENT_IP_SIZE];
    char *buffer = malloc(BUFFER_SIZE);

    if (buffer == NULL) {
        warn_errno("ERROR allocating request buffer");
        return;
    }

    ssize_t request_length = read_from_socket(client_fd, buffer, BUFFER_SIZE);

    if (request_length < 0) {
        warn_errno("ERROR reading from client socket");
        free(buffer);
        return;
    }

    resolve_client_ip(
            buffer,
            (size_t)request_length,
            client_address,
            true,
            client_ip,
            sizeof(client_ip));

    if (handle_raw_request(
            client_fd,
            buffer,
            (size_t)request_length,
            client_ip) < 0) {
        warn_errno("ERROR writing response to client socket");
    }

    free(buffer);
}

/**
 * Hauptschleife für normalen Socket-Betrieb.
 */
static void main_loop(void)
{
    int sockfd = setup_socket();

    while (run) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR && !run) {
                break;
            }

            warn_errno("ERROR accepting client connection");
            continue;
        }

        set_socket_timeouts(client_fd);

        handle_client(client_fd, &client_addr);

        if (close(client_fd) < 0) {
            warn_errno("ERROR closing client socket");
        }
    }

    if (close(sockfd) < 0) {
        warn_errno("ERROR closing server socket");
    }
}


int main(int argc, char *argv[])
{
    register_signals();

    if (server_config_initialize() != 0) {
        fprintf(
                stderr,
                "ERROR loading server configuration: %s\n",
                server_config_last_error());
        return EXIT_FAILURE;
    }

    if (booking_database_initialize() != 0) {
        fprintf(
                stderr,
                "ERROR initializing booking database: %s\n",
                booking_database_last_error());
        return EXIT_FAILURE;
    }

    if (calendar_database_initialize() != 0) {
        fprintf(
                stderr,
                "ERROR initializing calendar database: %s\n",
                calendar_database_last_error());
        booking_database_shutdown();
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "stdin") == 0) {
        main_loop_stdin();
    } else {
        fprintf(
                stderr,
                "Styling 4 Dogs server listening on %s:%u\n",
                server_config_bind_address(),
                (unsigned int)server_config_port());
        main_loop();
    }

    calendar_database_shutdown();
    booking_database_shutdown();
    return EXIT_SUCCESS;
}
