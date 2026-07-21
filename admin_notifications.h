#ifndef STYLES4DOGS_ADMIN_NOTIFICATIONS_H
#define STYLES4DOGS_ADMIN_NOTIFICATIONS_H

#include "http_lib.h"

typedef enum admin_notifications_result {
    ADMIN_NOTIFICATIONS_OK = 0,
    ADMIN_NOTIFICATIONS_BAD_REQUEST = 1,
    ADMIN_NOTIFICATIONS_ERROR = -1
} admin_notifications_result;

string *admin_notifications_build_page(const char *csrf_token, const char *notice_code);
admin_notifications_result admin_notifications_update_smtp(const string *request);
admin_notifications_result admin_notifications_disconnect_smtp(const string *request);
admin_notifications_result admin_notifications_enqueue_test(const string *request);
admin_notifications_result admin_notifications_update_template(const string *request);
admin_notifications_result admin_notifications_reset_template(const string *request);
admin_notifications_result admin_notifications_retry_failed(const string *request);
admin_notifications_result admin_notifications_clear_sent(const string *request);
admin_notifications_result admin_notifications_clear_failed(const string *request);
admin_notifications_result admin_notifications_clear_completed(const string *request);
const char *admin_notifications_last_error(void);

#endif
