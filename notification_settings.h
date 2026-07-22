#ifndef STYLES4DOGS_NOTIFICATION_SETTINGS_H
#define STYLES4DOGS_NOTIFICATION_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#define NOTIFICATION_SMTP_URL_SIZE 512
#define NOTIFICATION_SMTP_USERNAME_SIZE 256
#define NOTIFICATION_SMTP_PASSWORD_SIZE 512
#define NOTIFICATION_SMTP_ADDRESS_SIZE 256
#define NOTIFICATION_SMTP_NAME_SIZE 256
#define NOTIFICATION_SETTINGS_ERROR_SIZE 512

typedef struct notification_smtp_settings {
    bool enabled;
    bool delivery_enabled;
    bool managed_by_admin;
    bool notify_admin_on_new_booking;
    char url[NOTIFICATION_SMTP_URL_SIZE];
    char username[NOTIFICATION_SMTP_USERNAME_SIZE];
    char password[NOTIFICATION_SMTP_PASSWORD_SIZE];
    char from_address[NOTIFICATION_SMTP_ADDRESS_SIZE];
    char from_name[NOTIFICATION_SMTP_NAME_SIZE];
    char admin_email[NOTIFICATION_SMTP_ADDRESS_SIZE];
} notification_smtp_settings;

int notification_settings_load(notification_smtp_settings *settings);
int notification_settings_save(const notification_smtp_settings *settings);
int notification_settings_disconnect(void);
int notification_settings_set_delivery_enabled(bool enabled);
bool notification_settings_are_valid(const notification_smtp_settings *settings, bool require_enabled);
const char *notification_settings_last_error(void);

#endif
