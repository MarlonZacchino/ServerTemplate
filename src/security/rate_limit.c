#include "styles4dogs/security/rate_limit.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RATE_LIMIT_CLIENT_CAPACITY 1024
#define RATE_LIMIT_IP_SIZE 46

#define BOOKING_CLIENT_LIMIT 5U
#define BOOKING_CLIENT_WINDOW_SECONDS 600U
#define BOOKING_GLOBAL_LIMIT 120U
#define BOOKING_GLOBAL_WINDOW_SECONDS 60U

#define POSTAL_CLIENT_LIMIT 30U
#define POSTAL_CLIENT_WINDOW_SECONDS 60U
#define POSTAL_GLOBAL_LIMIT 300U
#define POSTAL_GLOBAL_WINDOW_SECONDS 60U

#define ADMIN_FAILURE_LIMIT 10U
#define ADMIN_FAILURE_WINDOW_SECONDS 600U


typedef struct rate_limit_entry {
    bool used;
    char client_ip[RATE_LIMIT_IP_SIZE];
    uint64_t window_started;
    uint64_t last_seen;
    unsigned int attempts;
} rate_limit_entry;

static rate_limit_entry booking_clients[RATE_LIMIT_CLIENT_CAPACITY];
static rate_limit_entry postal_clients[RATE_LIMIT_CLIENT_CAPACITY];
static rate_limit_entry admin_clients[RATE_LIMIT_CLIENT_CAPACITY];
static uint64_t booking_global_window_started;
static unsigned int booking_global_attempts;
static uint64_t postal_global_window_started;
static unsigned int postal_global_attempts;

static uint64_t monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        return (uint64_t)now.tv_sec;
    }

    time_t wall_clock = time(NULL);
    return wall_clock < 0 ? 0 : (uint64_t)wall_clock;
}

static unsigned int retry_after(
        uint64_t now,
        uint64_t window_started,
        unsigned int window_seconds
)
{
    uint64_t elapsed;
    uint64_t remaining;

    if (now <= window_started) {
        return window_seconds;
    }

    elapsed = now - window_started;

    if (elapsed >= window_seconds) {
        return 1;
    }

    remaining = (uint64_t)window_seconds - elapsed;

    if (remaining == 0) {
        return 1;
    }

    if (remaining > UINT32_MAX) {
        return UINT32_MAX;
    }

    return (unsigned int)remaining;
}

static bool valid_client_ip(const char *client_ip)
{
    size_t length;

    if (client_ip == NULL) {
        return false;
    }

    length = strnlen(client_ip, RATE_LIMIT_IP_SIZE);
    return length > 0 && length < RATE_LIMIT_IP_SIZE;
}

static rate_limit_entry *find_or_allocate(
        rate_limit_entry *entries,
        const char *client_ip,
        uint64_t now
)
{
    rate_limit_entry *free_entry = NULL;
    rate_limit_entry *oldest_entry = NULL;

    for (size_t index = 0; index < RATE_LIMIT_CLIENT_CAPACITY; index++) {
        rate_limit_entry *entry = &entries[index];

        if (entry->used && strcmp(entry->client_ip, client_ip) == 0) {
            entry->last_seen = now;
            return entry;
        }

        if (!entry->used && free_entry == NULL) {
            free_entry = entry;
        }

        if (entry->used &&
            (oldest_entry == NULL || entry->last_seen < oldest_entry->last_seen)) {
            oldest_entry = entry;
        }
    }

    if (free_entry == NULL) {
        free_entry = oldest_entry;
    }

    if (free_entry == NULL) {
        return NULL;
    }

    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = true;
    free_entry->window_started = now;
    free_entry->last_seen = now;
    snprintf(free_entry->client_ip, sizeof(free_entry->client_ip), "%s", client_ip);

    return free_entry;
}

static void reset_expired_window(
        rate_limit_entry *entry,
        uint64_t now,
        unsigned int window_seconds
)
{
    if (entry == NULL) {
        return;
    }

    if (now < entry->window_started ||
        now - entry->window_started >= window_seconds) {
        entry->window_started = now;
        entry->attempts = 0;
    }
}

bool rate_limit_allow_booking(
        const char *client_ip,
        unsigned int *retry_after_seconds
)
{
    uint64_t now;
    rate_limit_entry *entry;

    if (retry_after_seconds != NULL) {
        *retry_after_seconds = 1;
    }

    if (!valid_client_ip(client_ip)) {
        return false;
    }

    now = monotonic_seconds();

    if (booking_global_window_started == 0 ||
        now < booking_global_window_started ||
        now - booking_global_window_started >= BOOKING_GLOBAL_WINDOW_SECONDS) {
        booking_global_window_started = now;
        booking_global_attempts = 0;
    }

    if (booking_global_attempts >= BOOKING_GLOBAL_LIMIT) {
        if (retry_after_seconds != NULL) {
            *retry_after_seconds = retry_after(
                    now,
                    booking_global_window_started,
                    BOOKING_GLOBAL_WINDOW_SECONDS);
        }
        return false;
    }

    entry = find_or_allocate(booking_clients, client_ip, now);

    if (entry == NULL) {
        return false;
    }

    reset_expired_window(entry, now, BOOKING_CLIENT_WINDOW_SECONDS);

    if (entry->attempts >= BOOKING_CLIENT_LIMIT) {
        if (retry_after_seconds != NULL) {
            *retry_after_seconds = retry_after(
                    now,
                    entry->window_started,
                    BOOKING_CLIENT_WINDOW_SECONDS);
        }
        return false;
    }

    entry->attempts++;
    booking_global_attempts++;
    return true;
}

bool rate_limit_allow_postal_lookup(
        const char *client_ip,
        unsigned int *retry_after_seconds
)
{
    uint64_t now;
    rate_limit_entry *entry;

    if (retry_after_seconds != NULL) {
        *retry_after_seconds = 1;
    }

    if (!valid_client_ip(client_ip)) {
        return false;
    }

    now = monotonic_seconds();

    if (postal_global_window_started == 0 ||
        now < postal_global_window_started ||
        now - postal_global_window_started >= POSTAL_GLOBAL_WINDOW_SECONDS) {
        postal_global_window_started = now;
        postal_global_attempts = 0;
    }

    if (postal_global_attempts >= POSTAL_GLOBAL_LIMIT) {
        if (retry_after_seconds != NULL) {
            *retry_after_seconds = retry_after(
                    now,
                    postal_global_window_started,
                    POSTAL_GLOBAL_WINDOW_SECONDS);
        }
        return false;
    }

    entry = find_or_allocate(postal_clients, client_ip, now);
    if (entry == NULL) {
        return false;
    }

    reset_expired_window(entry, now, POSTAL_CLIENT_WINDOW_SECONDS);
    if (entry->attempts >= POSTAL_CLIENT_LIMIT) {
        if (retry_after_seconds != NULL) {
            *retry_after_seconds = retry_after(
                    now,
                    entry->window_started,
                    POSTAL_CLIENT_WINDOW_SECONDS);
        }
        return false;
    }

    entry->attempts++;
    postal_global_attempts++;
    return true;
}

bool rate_limit_admin_is_blocked(
        const char *client_ip,
        unsigned int *retry_after_seconds
)
{
    uint64_t now;
    rate_limit_entry *entry;

    if (retry_after_seconds != NULL) {
        *retry_after_seconds = 1;
    }

    if (!valid_client_ip(client_ip)) {
        return true;
    }

    now = monotonic_seconds();
    entry = find_or_allocate(admin_clients, client_ip, now);

    if (entry == NULL) {
        return true;
    }

    reset_expired_window(entry, now, ADMIN_FAILURE_WINDOW_SECONDS);

    if (entry->attempts < ADMIN_FAILURE_LIMIT) {
        return false;
    }

    if (retry_after_seconds != NULL) {
        *retry_after_seconds = retry_after(
                now,
                entry->window_started,
                ADMIN_FAILURE_WINDOW_SECONDS);
    }

    return true;
}

void rate_limit_record_admin_failure(const char *client_ip)
{
    uint64_t now;
    rate_limit_entry *entry;

    if (!valid_client_ip(client_ip)) {
        return;
    }

    now = monotonic_seconds();
    entry = find_or_allocate(admin_clients, client_ip, now);

    if (entry == NULL) {
        return;
    }

    reset_expired_window(entry, now, ADMIN_FAILURE_WINDOW_SECONDS);

    if (entry->attempts < UINT32_MAX) {
        entry->attempts++;
    }
}

void rate_limit_clear_admin_failures(const char *client_ip)
{
    if (!valid_client_ip(client_ip)) {
        return;
    }

    for (size_t index = 0; index < RATE_LIMIT_CLIENT_CAPACITY; index++) {
        rate_limit_entry *entry = &admin_clients[index];

        if (entry->used && strcmp(entry->client_ip, client_ip) == 0) {
            memset(entry, 0, sizeof(*entry));
            return;
        }
    }
}

void rate_limit_reset(void)
{
    memset(booking_clients, 0, sizeof(booking_clients));
    memset(postal_clients, 0, sizeof(postal_clients));
    memset(admin_clients, 0, sizeof(admin_clients));
    booking_global_window_started = 0;
    booking_global_attempts = 0;
    postal_global_window_started = 0;
    postal_global_attempts = 0;
}
