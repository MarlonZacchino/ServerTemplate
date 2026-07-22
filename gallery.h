#ifndef STYLES4DOGS_GALLERY_H
#define STYLES4DOGS_GALLERY_H

#include "http_lib.h"
#include <stdbool.h>

typedef enum gallery_result {
    GALLERY_OK = 0,
    GALLERY_BAD_REQUEST = 1,
    GALLERY_NOT_FOUND = 2,
    GALLERY_ERROR = -1
} gallery_result;

string *gallery_build_public_json(void);
string *gallery_build_admin_page(const char *csrf_token, const char *notice_code);
gallery_result gallery_handle_upload(const string *request);
gallery_result gallery_handle_delete(const string *request);
gallery_result gallery_handle_reorder(const string *request);
const char *gallery_last_error(void);
bool gallery_extract_multipart_text_field(const string *request, const char *field_name, char *out, size_t out_size);
int gallery_read_media(const char *file_name, bool include_hidden, char **out_data, size_t *out_length, char *out_content_type, size_t out_content_type_size);

#endif
