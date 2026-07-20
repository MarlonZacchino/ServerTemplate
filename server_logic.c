//
// Created by Marlon on 03.07.26.
//

#define _POSIX_C_SOURCE 200809L

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
#include "http_lib.h"
#include "process.h"

/**
 * TCP-Port, auf welchem der Server hört.
 */
#define PORT 31337

/**
 * Maximale Request-Größe.
 * Für den PSE-Server reicht 1 MiB locker aus und schützt vor zu großen Requests.
 */
#define BUFFER_SIZE (1024 * 1024)

/**
 * Timeout für Socket-Lese- und Schreiboperationen.
 */
#define SOCKET_TIMEOUT_SECONDS 5

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

/**
 * Verarbeitet einen bereits gelesenen Request-Buffer.
 */
static int handle_raw_request(int output_fd, const char *buffer, size_t request_length)
{
    string *request = cpy_str((void *)buffer, request_length);

    if (request == NULL) {
        return -1;
    }

    string *response = process(request);

    if (response == NULL) {
        response = make_internal_error_response();
    }

    if (response == NULL) {
        free_str(request);
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

    free_str(request);

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

    if (handle_raw_request(STDOUT_FILENO, buffer, (size_t)request_length) < 0) {
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
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(PORT);

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
static void handle_client(int client_fd)
{
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

    if (handle_raw_request(client_fd, buffer, (size_t)request_length) < 0) {
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

        handle_client(client_fd);

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

    if (booking_database_initialize() != 0) {
        fprintf(
                stderr,
                "ERROR initializing booking database: %s\n",
                booking_database_last_error());
        return EXIT_FAILURE;
    }

    if (argc == 2 && strcmp(argv[1], "stdin") == 0) {
        main_loop_stdin();
    } else {
        main_loop();
    }

    booking_database_shutdown();
    return EXIT_SUCCESS;
}
