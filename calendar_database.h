#ifndef STYLES4DOGS_CALENDAR_DATABASE_H
#define STYLES4DOGS_CALENDAR_DATABASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CALENDAR_SERVICE_CODE_SIZE 64
#define CALENDAR_SERVICE_NAME_SIZE 128
#define CALENDAR_TIMEZONE_SIZE 64
#define CALENDAR_LABEL_SIZE 256
#define CALENDAR_MAX_DAY_PERIODS 32
#define CALENDAR_MAX_DAY_CLOSURES 64
#define CALENDAR_MAX_DAY_BOOKINGS 256

typedef struct calendar_settings {
    char timezone[CALENDAR_TIMEZONE_SIZE];
    int min_notice_minutes;
    int booking_horizon_days;
    int slot_interval_minutes;
    int pending_hold_minutes;
    int capacity;
    bool auto_confirm_bookings;
    bool email_notifications_enabled;
    bool reminder_enabled;
    int reminder_lead_minutes;
} calendar_settings;

typedef struct calendar_service {
    int64_t id;
    char code[CALENDAR_SERVICE_CODE_SIZE];
    char name[CALENDAR_SERVICE_NAME_SIZE];
    int duration_minutes;
    int buffer_minutes;
    bool active;
    int sort_order;
} calendar_service;

typedef struct calendar_time_range {
    int start_minute;
    int end_minute;
} calendar_time_range;

typedef struct calendar_closure {
    int64_t id;
    char start_date[11];
    char end_date[11];
    int start_minute;
    int end_minute;
    char label[CALENDAR_LABEL_SIZE];
} calendar_closure;

typedef int (*calendar_service_callback)(
        const calendar_service *service,
        void *context
);

typedef int (*calendar_closure_callback)(
        const calendar_closure *closure,
        void *context
);

typedef struct calendar_pending_booking {
    const char *created_at_utc;
    const char *hold_expires_at_utc;
    const char *customer_name;
    const char *contact;
    const char *contact_channel;
    const char *email;
    const char *phone_number;
    const char *phone_kind;
    const char *contact_preference;
    const char *street_address;
    const char *postal_code;
    const char *city;
    const char *dog_name;
    const char *dog_size;
    const char *service_code;
    const char *appointment_date;
    int start_minute;
    int end_minute;
    int blocked_until_minute;
    const char *message;
    bool auto_confirm;
} calendar_pending_booking;

int calendar_database_initialize(void);
void calendar_database_shutdown(void);
const char *calendar_database_last_error(void);
int calendar_database_schema_version(int *out_version);

int calendar_database_get_settings(calendar_settings *settings);
int calendar_database_update_settings(const calendar_settings *settings);

int calendar_database_get_service(
        const char *code,
        calendar_service *service
);
int calendar_database_update_service(const calendar_service *service);
int calendar_database_add_service(const calendar_service *service);

typedef enum calendar_service_delete_result {
    CALENDAR_SERVICE_DELETE_ERROR = -1,
    CALENDAR_SERVICE_DELETE_OK = 0,
    CALENDAR_SERVICE_DELETE_ARCHIVED = 1,
    CALENDAR_SERVICE_DELETE_NOT_FOUND = 2
} calendar_service_delete_result;

calendar_service_delete_result calendar_database_delete_service(const char *code);
int calendar_database_for_each_service(
        calendar_service_callback callback,
        void *context
);
int calendar_database_for_each_active_service(
        calendar_service_callback callback,
        void *context
);

int calendar_database_clear_opening_hours(void);
int calendar_database_clear_opening_hours_for_weekday(int weekday);
int calendar_database_add_opening_period(
        int weekday,
        int start_minute,
        int end_minute
);
int calendar_database_get_opening_periods(
        int weekday,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

int calendar_database_clear_closures(void);
int calendar_database_add_closure(
        const calendar_closure *closure,
        int64_t *out_id
);
int calendar_database_delete_closure(int64_t closure_id);
int calendar_database_for_each_closure(
        calendar_closure_callback callback,
        void *context
);
int calendar_database_for_each_closure_in_range(
        const char *from_date,
        const char *to_date,
        calendar_closure_callback callback,
        void *context
);
int calendar_database_get_closures_for_date(
        const char *date,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

int calendar_database_get_blocking_bookings(
        const char *date,
        const char *now_utc,
        calendar_time_range *ranges,
        size_t ranges_capacity,
        size_t *out_count
);

int calendar_database_begin_immediate(void);
int calendar_database_commit(void);
void calendar_database_rollback(void);
int calendar_database_expire_pending(const char *now_utc);
int calendar_database_count_recent_contact_bookings(
        const char *contact_channel,
        const char *email,
        const char *phone_digits,
        const char *since_utc,
        int *out_count
);
int calendar_database_insert_pending(
        const calendar_pending_booking *booking,
        int64_t *out_booking_id
);

typedef enum calendar_booking_decision_result {
    CALENDAR_BOOKING_DECISION_ERROR = -1,
    CALENDAR_BOOKING_DECISION_OK = 0,
    CALENDAR_BOOKING_DECISION_NOT_FOUND = 1,
    CALENDAR_BOOKING_DECISION_NOT_PENDING = 2,
    CALENDAR_BOOKING_DECISION_EXPIRED = 3
} calendar_booking_decision_result;

calendar_booking_decision_result calendar_database_decide_booking(
        int64_t booking_id,
        bool accept,
        const char *decision_at_utc,
        const char *rejection_reason
);

#endif
