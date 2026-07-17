#ifndef SERVER_BOOKING_H
#define SERVER_BOOKING_H

#include <stdbool.h>

#include "http_lib.h"

#define BOOKING_NAME_SIZE 256
#define BOOKING_CONTACT_SIZE 256
#define BOOKING_DOG_NAME_SIZE 256
#define BOOKING_MESSAGE_SIZE 1024

typedef struct booking_request {
    char name[BOOKING_NAME_SIZE];
    char contact[BOOKING_CONTACT_SIZE];
    char dog_name[BOOKING_DOG_NAME_SIZE];
    char message[BOOKING_MESSAGE_SIZE];
} booking_request;

bool parse_booking_request(const string *request, booking_request *booking);

int save_booking_request(const booking_request *booking);

/*
 * Baut die HTML-Seite für /admin/bookings.
 *
 * Die Rückgabe muss später mit free_str() freigegeben werden.
 */
string *build_booking_admin_page(void);

#endif