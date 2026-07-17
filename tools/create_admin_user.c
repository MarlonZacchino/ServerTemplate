//
// Created by Marlon on 17.07.26.
//

#include <sodium.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define MAX_USERNAME_LENGTH 128
#define MAX_PASSWORD_LENGTH 512

static void strip_newline(char *text)
{
    char *newline;

    if (text == NULL) {
        return;
    }

    newline = strpbrk(text, "\r\n");

    if (newline != NULL) {
        *newline = '\0';
    }
}

static bool contains_invalid_username_char(const char *username)
{
    if (username == NULL || username[0] == '\0') {
        return true;
    }

    for (size_t i = 0; username[i] != '\0'; i++) {
        if (username[i] == ':' || username[i] == '\n' || username[i] == '\r') {
            return true;
        }
    }

    return false;
}

static int read_password_hidden(const char *prompt, char *buffer, size_t buffer_size)
{
    struct termios old_terminal;
    struct termios new_terminal;

    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    fprintf(stderr, "%s", prompt);
    fflush(stderr);

    if (tcgetattr(STDIN_FILENO, &old_terminal) != 0) {
        return -1;
    }

    new_terminal = old_terminal;
    new_terminal.c_lflag &= (tcflag_t)~ECHO;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_terminal) != 0) {
        return -1;
    }

    if (fgets(buffer, (int)buffer_size, stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_terminal);
        return -1;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_terminal);
    fprintf(stderr, "\n");

    strip_newline(buffer);

    return 0;
}

int main(int argc, char *argv[])
{
    char password[MAX_PASSWORD_LENGTH];
    char password_repeat[MAX_PASSWORD_LENGTH];
    char hash[crypto_pwhash_STRBYTES];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <username>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (contains_invalid_username_char(argv[1])) {
        fprintf(stderr, "Invalid username.\n");
        return EXIT_FAILURE;
    }

    if (strlen(argv[1]) >= MAX_USERNAME_LENGTH) {
        fprintf(stderr, "Username is too long.\n");
        return EXIT_FAILURE;
    }

    if (sodium_init() < 0) {
        fprintf(stderr, "Could not initialize libsodium.\n");
        return EXIT_FAILURE;
    }

    if (read_password_hidden("Password: ", password, sizeof(password)) != 0 ||
        read_password_hidden("Repeat password: ", password_repeat, sizeof(password_repeat)) != 0) {
        fprintf(stderr, "Could not read password.\n");
        return EXIT_FAILURE;
    }

    if (password[0] == '\0') {
        fprintf(stderr, "Password must not be empty.\n");
        return EXIT_FAILURE;
    }

    if (strcmp(password, password_repeat) != 0) {
        fprintf(stderr, "Passwords do not match.\n");
        return EXIT_FAILURE;
    }

    if (crypto_pwhash_str(
            hash,
            password,
            strlen(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        fprintf(stderr, "Could not hash password.\n");
        return EXIT_FAILURE;
    }

    printf("%s:%s\n", argv[1], hash);

    sodium_memzero(password, sizeof(password));
    sodium_memzero(password_repeat, sizeof(password_repeat));

    return EXIT_SUCCESS;
}
