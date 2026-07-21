#ifndef STYLES4DOGS_ADMIN_CALENDAR_H
#define STYLES4DOGS_ADMIN_CALENDAR_H

#include "http_lib.h"

typedef enum admin_calendar_result {
    ADMIN_CALENDAR_ERROR = -1,
    ADMIN_CALENDAR_OK = 0,
    ADMIN_CALENDAR_BAD_REQUEST = 1,
    ADMIN_CALENDAR_NOT_FOUND = 2
} admin_calendar_result;

string *admin_calendar_build_page(
        const char *csrf_token,
        const char *notice_code
);

admin_calendar_result admin_calendar_update_settings(const string *request);
admin_calendar_result admin_calendar_save_all(const string *request);
admin_calendar_result admin_calendar_update_opening_hours(const string *request);
admin_calendar_result admin_calendar_update_service(const string *request);
admin_calendar_result admin_calendar_add_service(const string *request);
admin_calendar_result admin_calendar_delete_service(const string *request);
admin_calendar_result admin_calendar_add_closure(const string *request);
admin_calendar_result admin_calendar_delete_closure(const string *request);

const char *admin_calendar_last_error(void);

#endif
