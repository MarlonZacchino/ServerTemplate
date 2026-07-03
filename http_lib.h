//
// Created by Marlon on 03.07.26.
//
#ifndef HTTP_LIB_H
#define HTTP_LIB_H

#include <stddef.h>
#include <stdio.h>

/**
 * Eigener String-Typ für den Server.
 *
 * Wichtig:
 * - len ist die echte Länge der Daten.
 * - str ist zusätzlich mit '\0' terminiert.
 * - Trotzdem darf man sich bei Binärdaten NICHT auf strlen() verlassen.
 * - capacity ist die intern reservierte Größe ohne Nullterminator.
 */
typedef struct string {
    size_t len;
    size_t capacity;
    char *str;
} string;

/**
 * Erstellt einen neuen leeren String.
 *
 * Rückgabe:
 * - Zeiger auf neuen String
 * - beendet das Programm bei Speicherfehler
 */
string *_new_string(void);

/**
 * Erstellt eine Kopie von src mit genau len Bytes.
 *
 * Geeignet für:
 * - HTTP-Requests
 * - HTTP-Responses
 * - Binärdaten
 *
 * src darf nur NULL sein, wenn len == 0 ist.
 */
string *cpy_str(const char *src, size_t len);

/**
 * Hängt len Bytes von src an dest an.
 *
 * Wichtig:
 * - binary-safe
 * - src muss nicht nullterminiert sein
 * - dest wird bei Bedarf vergrößert
 *
 * Rückgabe:
 * - dest bei Erfolg
 * - NULL bei ungültigen Parametern
 */
string *str_cat(string *dest, const char *src, size_t len);

/**
 * Hängt einen normalen nullterminierten C-String an dest an.
 *
 * Nur für Text verwenden, nicht für Binärdaten.
 */
string *str_cat_cstr(string *dest, const char *src);

/**
 * Reserviert mindestens new_capacity Bytes Nutzdaten.
 *
 * Der Nullterminator wird intern zusätzlich berücksichtigt.
 */
string *str_reserve(string *str, size_t new_capacity);

/**
 * Setzt den String auf Länge 0 zurück.
 *
 * Der Speicher bleibt erhalten und kann wiederverwendet werden.
 */
void str_clear(string *str);

/**
 * Gibt den String auf stdout aus.
 *
 * Verwendet fwrite(), daher auch für nicht-nullterminierte Daten geeignet.
 */
void print_string(const string *str);

/**
 * Gibt den String frei.
 *
 * NULL ist erlaubt.
 */
void free_str(string *str);

/**
 * Gibt die Länge des Strings zurück.
 *
 * Bei NULL wird 0 zurückgegeben.
 */
size_t get_length(const string *str);

/**
 * Gibt einen Zeiger auf das interne char-Array zurück.
 *
 * Achtung:
 * - Der Zeiger gehört weiterhin dem string.
 * - Nicht manuell freigeben.
 * - Bei Binärdaten get_length() verwenden, nicht strlen().
 */
char *get_char_str(string *str);

/**
 * Const-Variante von get_char_str().
 */
const char *get_const_char_str(const string *str);

#endif