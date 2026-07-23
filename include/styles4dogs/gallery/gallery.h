#ifndef STYLES4DOGS_GALLERY_GALLERY_H
#define STYLES4DOGS_GALLERY_GALLERY_H

/**
 * @file gallery.h
 * @brief Stellt öffentliche und administrative Galerieoperationen bereit.
 */

#include "styles4dogs/http/http_lib.h"
#include <stdbool.h>

/**
 * @brief Ergebnis einer schreibenden Galerieoperation.
 */
typedef enum gallery_result {
    GALLERY_OK = 0, /**< Operation erfolgreich. */
    GALLERY_BAD_REQUEST = 1, /**< Ungültige oder unvollständige Eingabe. */
    GALLERY_NOT_FOUND = 2, /**< Angeforderter Datensatz wurde nicht gefunden. */
    GALLERY_ERROR = -1 /**< Interner Fehler. */
} gallery_result; /**< Typalias für ::gallery_result. */

/**
 * @brief Erzeugt die öffentliche JSON-Liste sichtbarer Galeriebilder.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *gallery_build_public_json(void);
/**
 * @brief Baut die geschützte Galerieverwaltung.
 * @param[in] csrf_token Gültiger CSRF-Token für schreibende Formulare.
 * @param[in] notice_code Optionaler Code für eine Erfolgsmeldung; darf NULL sein.
 * @return Neu allozierter Wert bei Erfolg, sonst NULL. Der Aufrufer gibt ihn mit der dokumentierten Freigabefunktion frei.
 */
string *gallery_build_admin_page(const char *csrf_token, const char *notice_code);
/**
 * @brief Validiert und speichert einen Galerie-Upload.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::gallery_result.
 */
gallery_result gallery_handle_upload(const string *request);
/**
 * @brief Löscht ein Galeriebild.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::gallery_result.
 */
gallery_result gallery_handle_delete(const string *request);
/**
 * @brief Speichert eine neue Reihenfolge der Galeriebilder.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @return Ein Wert aus ::gallery_result.
 */
gallery_result gallery_handle_reorder(const string *request);
/**
 * @brief Liefert die letzte Fehlermeldung des Galeriemoduls.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *gallery_last_error(void);
/**
 * @brief Liest ein Textfeld aus einem Multipart-Request.
 * @param[in] request Vollständiger HTTP-Request oder bereits validierte Requestdaten.
 * @param[in] field_name Name des gesuchten Formularfelds.
 * @param[in] out Ausgabepuffer für den dekodierten Wert.
 * @param[out] out_size Größe des Ausgabepuffers in Bytes.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool gallery_extract_multipart_text_field(const string *request, const char *field_name, char *out, size_t out_size);
/**
 * @brief Liest eine freigegebene Mediendatei aus dem Galerieverzeichnis.
 * @param[in] file_name Bereinigter Galerie-Dateiname.
 * @param[in] include_hidden Legt fest, ob ausgeblendete Medien gelesen werden dürfen.
 * @param[out] out_data Ausgabeparameter für den neu allozierten Dateipuffer.
 * @param[out] out_length Ausgabeparameter für die Dateigröße in Bytes.
 * @param[out] out_content_type Ausgabepuffer für den MIME-Typ.
 * @param[out] out_content_type_size Größe von @p out_content_type in Bytes.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int gallery_read_media(const char *file_name, bool include_hidden, char **out_data, size_t *out_length, char *out_content_type, size_t out_content_type_size);

#endif
