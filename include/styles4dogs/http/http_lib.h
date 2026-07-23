#ifndef STYLES4DOGS_HTTP_HTTP_LIB_H
#define STYLES4DOGS_HTTP_HTTP_LIB_H

/**
 * @file http_lib.h
 * @brief Definiert den binärsicheren dynamischen String-Typ des HTTP-Servers.
 */

#include <stddef.h>
#include <stdio.h>

/**
 * @brief Binärsicherer dynamischer String des HTTP-Servers.
 *
 * Wichtig:
 * - len ist die echte Länge der Daten.
 * - str ist zusätzlich mit '\0' terminiert.
 * - Trotzdem darf man sich bei Binärdaten NICHT auf strlen() verlassen.
 * - capacity ist die intern reservierte Größe ohne Nullterminator.
 */
typedef struct string {
    size_t len; /**< Aktuelle Anzahl gespeicherter Nutzbytes. */
    size_t capacity; /**< Reservierte Nutzdatenkapazität ohne Nullterminator. */
    char *str; /**< Nullterminierter interner Datenpuffer. */
} string;

/**
 * @brief Erzeugt einen leeren dynamischen String.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *_new_string(void);

/**
 * @brief Erzeugt eine binärsichere Kopie einer Bytefolge.
 * @param[in] src Quellpuffer.
 * @param[in] len Anzahl zu kopierender oder anzuhängender Bytes.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *cpy_str(const char *src, size_t len);

/**
 * @brief Hängt eine Bytefolge an einen dynamischen String an.
 * @param[in] dest Zielstring.
 * @param[in] src Quellpuffer.
 * @param[in] len Anzahl zu kopierender oder anzuhängender Bytes.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
string *str_cat(string *dest, const char *src, size_t len);

/**
 * @brief Hängt einen nullterminierten C-String an.
 * @param[in] dest Zielstring.
 * @param[in] src Quellpuffer.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
string *str_cat_cstr(string *dest, const char *src);

/**
 * @brief Reserviert mindestens die angegebene Nutzdatenkapazität.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 * @param[in] new_capacity Gewünschte Mindestkapazität der Nutzdaten.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
string *str_reserve(string *str, size_t new_capacity);

/**
 * @brief Leert einen String, ohne seine Kapazität freizugeben.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 */
void str_clear(string *str);

/**
 * @brief Schreibt den String binärsicher auf die Standardausgabe.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 */
void print_string(const string *str);

/**
 * @brief Gibt einen dynamischen String frei.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 */
void free_str(string *str);

/**
 * @brief Liefert die gespeicherte Byteanzahl eines Strings.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 * @return Die ermittelte Byteanzahl.
 */
size_t get_length(const string *str);

/**
 * @brief Liefert einen veränderbaren Zeiger auf den internen Puffer.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
char *get_char_str(string *str);

/**
 * @brief Liefert einen schreibgeschützten Zeiger auf den internen Puffer.
 * @param[in] str Stringinstanz; NULL ist nur bei entsprechend dokumentierten Funktionen erlaubt.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *get_const_char_str(const string *str);

#endif
