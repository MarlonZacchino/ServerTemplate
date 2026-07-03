#include "http_lib.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Kleine Startgröße.
 *
 * Dadurch muss bei kurzen Strings nicht sofort wieder neuer Speicher
 * angefordert werden.
 */
#define STRING_INITIAL_CAPACITY 16

/*
 * Für diese kleine Library behandeln wir Speicherfehler bewusst hart.
 *
 * Wenn malloc/realloc fehlschlägt, kann der Server sowieso nicht sinnvoll
 * weiterarbeiten. Deshalb beenden wir das Programm mit einer klaren Meldung.
 */
static void httplib_fatal(const char *msg)
{
    int saved_errno = errno;

    fprintf(stderr, "HTTPLIB error: %s", msg);

    if (saved_errno != 0) {
        fprintf(stderr, " (%s)", strerror(saved_errno));
    }

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

/*
 * Prüft, ob a + b bei size_t überlaufen würde.
 *
 * Das ist vor allem für sehr große oder kaputte Requests wichtig.
 */
static int size_add_overflows(size_t a, size_t b)
{
    return a > SIZE_MAX - b;
}

/*
 * Sucht eine passende neue Größe.
 *
 * Wir verdoppeln den Speicher schrittweise. Das ist effizienter,
 * als bei jedem kleinen Anhängen exakt neu zu reservieren.
 */
static size_t next_capacity(size_t current, size_t required)
{
    size_t capacity = current;

    if (capacity < STRING_INITIAL_CAPACITY) {
        capacity = STRING_INITIAL_CAPACITY;
    }

    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }

        capacity *= 2;
    }

    return capacity;
}

string *_new_string(void)
{
    string *str = calloc(1, sizeof(*str));

    if (str == NULL) {
        httplib_fatal("could not allocate string struct");
    }

    str->len = 0;
    str->capacity = STRING_INITIAL_CAPACITY;

    /*
     * +1, weil wir zusätzlich immer einen Nullterminator setzen.
     * Die echte Länge steht trotzdem in str->len.
     */
    str->str = calloc(str->capacity + 1, sizeof(char));

    if (str->str == NULL) {
        free(str);
        httplib_fatal("could not allocate string buffer");
    }

    str->str[0] = '\0';

    return str;
}

string *str_reserve(string *str, size_t new_capacity)
{
    char *new_buffer;

    if (str == NULL) {
        return NULL;
    }

    if (new_capacity <= str->capacity) {
        return str;
    }

    /*
     * Wir brauchen intern immer ein Byte extra für '\0'.
     */
    if (new_capacity == SIZE_MAX) {
        httplib_fatal("string capacity overflow");
    }

    new_buffer = realloc(str->str, new_capacity + 1);

    if (new_buffer == NULL) {
        httplib_fatal("could not resize string buffer");
    }

    str->str = new_buffer;
    str->capacity = new_capacity;

    /*
     * Nach realloc bleibt der Inhalt erhalten.
     * Wir stellen nur nochmal sicher, dass der String am Ende sauber
     * abgeschlossen ist.
     */
    str->str[str->len] = '\0';

    return str;
}

string *cpy_str(const char *src, size_t len)
{
    string *dest = _new_string();

    /*
     * NULL mit Länge 0 ist okay: daraus wird einfach ein leerer String.
     * NULL mit Länge > 0 wäre aber ein echter Fehler.
     */
    if (src == NULL) {
        if (len == 0) {
            return dest;
        }

        free_str(dest);
        return NULL;
    }

    if (len == 0) {
        return dest;
    }

    str_reserve(dest, len);

    memcpy(dest->str, src, len);
    dest->len = len;
    dest->str[dest->len] = '\0';

    return dest;
}

string *str_cat(string *dest, const char *src, size_t len)
{
    size_t required_capacity;
    size_t new_capacity;

    if (dest == NULL) {
        return NULL;
    }

    /*
     * Nichts anhängen ist kein Fehler.
     */
    if (len == 0) {
        return dest;
    }

    if (src == NULL) {
        return NULL;
    }

    /*
     * Schutz gegen Überlauf bei extrem großen Längen.
     */
    if (size_add_overflows(dest->len, len)) {
        httplib_fatal("string length overflow");
    }

    required_capacity = dest->len + len;

    if (required_capacity > dest->capacity) {
        new_capacity = next_capacity(dest->capacity, required_capacity);
        str_reserve(dest, new_capacity);
    }

    memcpy(dest->str + dest->len, src, len);

    dest->len += len;
    dest->str[dest->len] = '\0';

    return dest;
}

string *str_cat_cstr(string *dest, const char *src)
{
    if (src == NULL) {
        return NULL;
    }

    return str_cat(dest, src, strlen(src));
}

void str_clear(string *str)
{
    if (str == NULL) {
        return;
    }

    str->len = 0;

    if (str->str != NULL) {
        str->str[0] = '\0';
    }
}

void print_string(const string *str)
{
    if (str == NULL || str->str == NULL || str->len == 0) {
        return;
    }

    /*
     * fwrite() nimmt die Länge explizit.
     * Dadurch funktioniert das auch, wenn im Inhalt irgendwo ein '\0' steht.
     */
    fwrite(str->str, 1, str->len, stdout);
}

void free_str(string *str)
{
    if (str == NULL) {
        return;
    }

    free(str->str);

    /*
     * Die folgenden Zuweisungen sind nicht zwingend nötig,
     * helfen aber beim Debuggen, falls man versehentlich später noch
     * auf den String schaut.
     */
    str->str = NULL;
    str->len = 0;
    str->capacity = 0;

    free(str);
}

size_t get_length(const string *str)
{
    if (str == NULL) {
        return 0;
    }

    return str->len;
}

char *get_char_str(string *str)
{
    if (str == NULL) {
        return NULL;
    }

    return str->str;
}

const char *get_const_char_str(const string *str)
{
    if (str == NULL) {
        return NULL;
    }

    return str->str;
}