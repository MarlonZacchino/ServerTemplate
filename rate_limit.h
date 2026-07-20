#ifndef STYLES4DOGS_RATE_LIMIT_H
#define STYLES4DOGS_RATE_LIMIT_H

#include <stdbool.h>

/*
 * In-memory rate limiting for the single-process HTTP server.
 *
 * No client addresses are persisted. State is intentionally reset when the
 * process restarts. All functions are safe for the current single-threaded
 * server loop; a future multi-threaded server must add synchronization.
 */

/*
 * Counts a public booking attempt for one client and globally.
 * Returns true when the request may continue. On false, retry_after_seconds
 * contains the minimum whole-second delay advertised via Retry-After.
 */
bool rate_limit_allow_booking(
        const char *client_ip,
        unsigned int *retry_after_seconds
);

/* Returns true while this client is blocked after repeated admin failures. */
bool rate_limit_admin_is_blocked(
        const char *client_ip,
        unsigned int *retry_after_seconds
);

/* Records one failed admin authentication attempt. */
void rate_limit_record_admin_failure(const char *client_ip);

/* Clears failure history after successful admin authentication. */
void rate_limit_clear_admin_failures(const char *client_ip);

/* Clears all in-memory state. Primarily useful for controlled tests. */
void rate_limit_reset(void);

#endif
