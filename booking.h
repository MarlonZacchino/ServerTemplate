#ifndef SERVER_BOOKING_H
#define SERVER_BOOKING_H

#include <stdbool.h>
#include <stddef.h>

#include "http_lib.h"

#define BOOKING_NAME_SIZE 256
#define BOOKING_FIRST_NAME_SIZE 128
#define BOOKING_LAST_NAME_SIZE 128
#define BOOKING_CONTACT_SIZE 256
#define BOOKING_CONTACT_CHANNEL_SIZE 16
#define BOOKING_EMAIL_SIZE 256
#define BOOKING_PHONE_SIZE 64
#define BOOKING_PHONE_KIND_SIZE 16
#define BOOKING_CONTACT_PREFERENCE_SIZE 16
#define BOOKING_STREET_ADDRESS_SIZE 256
#define BOOKING_POSTAL_CODE_SIZE 6
#define BOOKING_CITY_SIZE 128
#define BOOKING_DOG_NAME_SIZE 256
#define BOOKING_DOG_BREED_SIZE 64
#define BOOKING_DOG_SIZE_SIZE 64
#define BOOKING_SERVICE_SIZE 128
#define BOOKING_APPOINTMENT_DATE_SIZE 11
#define BOOKING_APPOINTMENT_START_SIZE 6
#define BOOKING_PREFERRED_DATE_SIZE 32
#define BOOKING_MESSAGE_SIZE 1024
#define BOOKING_ADMIN_STATUS_SIZE 32
#define BOOKING_ADMIN_SEARCH_SIZE 128

typedef struct booking_request {
    char name[BOOKING_NAME_SIZE];
    char first_name[BOOKING_FIRST_NAME_SIZE];
    char last_name[BOOKING_LAST_NAME_SIZE];
    char contact[BOOKING_CONTACT_SIZE];
    char contact_channel[BOOKING_CONTACT_CHANNEL_SIZE];
    char email[BOOKING_EMAIL_SIZE];
    char phone_number[BOOKING_PHONE_SIZE];
    char phone_kind[BOOKING_PHONE_KIND_SIZE];
    char contact_preference[BOOKING_CONTACT_PREFERENCE_SIZE];
    char street_address[BOOKING_STREET_ADDRESS_SIZE];
    char postal_code[BOOKING_POSTAL_CODE_SIZE];
    char city[BOOKING_CITY_SIZE];
    char dog_name[BOOKING_DOG_NAME_SIZE];
    char dog_breed[BOOKING_DOG_BREED_SIZE];
    char dog_size[BOOKING_DOG_SIZE_SIZE];
    char service[BOOKING_SERVICE_SIZE];
    char appointment_date[BOOKING_APPOINTMENT_DATE_SIZE];
    char appointment_start[BOOKING_APPOINTMENT_START_SIZE];
    char preferred_date[BOOKING_PREFERRED_DATE_SIZE];
    char message[BOOKING_MESSAGE_SIZE];
} booking_request;

typedef struct booking_admin_filter {
    char status[BOOKING_ADMIN_STATUS_SIZE];
    char search[BOOKING_ADMIN_SEARCH_SIZE];
} booking_admin_filter;

/*
 * Liest und validiert eine Buchungsanfrage aus einem
 * application/x-www-form-urlencoded HTTP-Request.
 */
bool parse_booking_request(const string *request, booking_request *booking);

/* Unsichtbares Bot-Feld. Echte Nutzer lassen es leer. */
bool booking_request_hits_honeypot(const string *request);

/* Liest und validiert Status- und Suchfilter aus dem Query-String. */
bool parse_booking_admin_filter(
        const char *query,
        size_t query_length,
        booking_admin_filter *filter
);

/*
 * Baut die HTML-Seite für /admin/bookings.
 *
 * Die Rückgabe muss später mit free_str() freigegeben werden.
 */
string *build_booking_admin_page(
        const char *csrf_token,
        const booking_admin_filter *filter
);

#endif
