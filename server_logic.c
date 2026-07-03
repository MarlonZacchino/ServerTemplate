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
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "http_lib.h"

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
string *process(string *request);

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

/**
 * Prüft, ob der HTTP-Header vollständig gelesen wurde.
 *
 * Für euren PSE-Server reicht das, weil ihr hauptsächlich GET-Requests verarbeitet.
 * Falls später HTTP-Body/Post relevant wird, muss zusätzlich Content-Length
 * ausgewertet werden.
 */
static bool has_http_header_end(const char *buffer, size_t length)
{
    if (buffer == NULL || length < 2) {
        return false;
    }

    for (size_t i = 0; i + 3 < length; i++) {
        if (buffer[i] == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {
            return true;
        }
    }

    for (size_t i = 0; i + 1 < length; i++) {
        if (buffer[i] == '\n' && buffer[i + 1] == '\n') {
            return true;
        }
    }

    return false;
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

    while (total < capacity) {
        ssize_t length = read(fd, buffer + total, capacity - total);

        if (length > 0) {
            total += (size_t)length;

            if (has_http_header_end(buffer, total)) {
                break;
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

        /*
         * Bei Timeout: Wenn bereits Daten gelesen wurden, verarbeiten wir sie.
         */
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
    serv_addr.sin_addr.s_addr = INADDR_ANY;
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

/**
 * Übergangsweise Echo-Implementierung.
 *
 * In eurem eigentlichen Projekt sollte diese Funktion aus process.c kommen.
 * Dann diese Funktion hier löschen und stattdessen process.h einbinden.
 */
string *process(string *request)
{
    if (request == NULL) {
        return NULL;
    }

    /*
     * Wichtig:
     * Nicht einfach "return request;".
     * Sonst ist die Speicher-Verantwortung unsauber.
     */
    return cpy_str(get_char_str(request), get_length(request));
}

int main(int argc, char *argv[])
{
    register_signals();

    if (argc == 2 && strcmp(argv[1], "stdin") == 0) {
        main_loop_stdin();
    } else {
        main_loop();
    }

    return EXIT_SUCCESS;
}