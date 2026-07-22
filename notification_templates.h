#ifndef STYLES4DOGS_NOTIFICATION_TEMPLATES_H
#define STYLES4DOGS_NOTIFICATION_TEMPLATES_H

#include <stdbool.h>
#include <stddef.h>

#include "notification_queue.h"

#define NOTIFICATION_TEMPLATE_EVENT_SIZE 32

typedef struct notification_template {
    char event_type[NOTIFICATION_TEMPLATE_EVENT_SIZE];
    char subject_template[NOTIFICATION_SUBJECT_SIZE];
    char body_template[NOTIFICATION_BODY_SIZE];
} notification_template;

typedef struct notification_template_context {
    const char *customer_name;
    const char *booking_id;
    const char *appointment_date;
    const char *start_time;
    const char *end_time;
    const char *service_name;
    const char *dog_name;
    const char *rejection_reason;
    const char *salon_name;
    const char *salon_address;
    const char *salon_phone;
    const char *website_url;
    const char *booking_url;
} notification_template_context;

typedef int (*notification_template_callback)(const notification_template *, void *);

bool notification_template_event_is_valid(const char *event_type);
const char *notification_template_event_label(const char *event_type);
int notification_template_get(const char *event_type, notification_template *out_template);
int notification_template_update(const notification_template *template_value);
int notification_template_reset(const char *event_type);
int notification_template_for_each(notification_template_callback callback, void *context);
int notification_template_render(
        const notification_template *template_value,
        const notification_template_context *context,
        char subject[NOTIFICATION_SUBJECT_SIZE],
        char body[NOTIFICATION_BODY_SIZE]);
const char *notification_templates_last_error(void);

#endif
