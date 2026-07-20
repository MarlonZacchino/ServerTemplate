//
// Created by Marlon on 16.07.26.
//

#include "booking.h"

#include "booking_database.h"
#include "form_urlencoded.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PRIVACY_CONSENT_VALUE "accepted"

typedef struct admin_page_context {
    string *page;
    size_t booking_count;
} admin_page_context;

static void trim_ascii_whitespace(char *text)
{
    size_t length;
    size_t start = 0;

    if (text == NULL) {
        return;
    }

    length = strlen(text);

    while (start < length &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        start++;
    }

    while (length > start &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' ||
            text[length - 1] == '\r' || text[length - 1] == '\n')) {
        length--;
    }

    if (start > 0) {
        memmove(text, text + start, length - start);
    }

    text[length - start] = '\0';
}

static bool is_nonempty_single_line(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        unsigned char character = (unsigned char)text[index];

        if (character == '\r' || character == '\n' || character == '\t' ||
            character < 0x20 || character == 0x7f) {
            return false;
        }
    }

    return true;
}

static bool is_optional_single_line(const char *text)
{
    return text != NULL &&
           (text[0] == '\0' || is_nonempty_single_line(text));
}

static bool get_required_field(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            field_name,
            out,
            out_size);

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_nonempty_single_line(out);
}

static bool get_optional_single_line_field(
        const string *request,
        const char *field_name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            field_name,
            out,
            out_size);

    if (result == FORM_VALUE_NOT_FOUND) {
        out[0] = '\0';
        return true;
    }

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_optional_single_line(out);
}

static bool get_optional_message(
        const string *request,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(
            request,
            "message",
            out,
            out_size);

    if (result == FORM_VALUE_NOT_FOUND) {
        out[0] = '\0';
        return true;
    }

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return true;
}

static bool equals_one_of(
        const char *value,
        const char *const allowed_values[],
        size_t allowed_count
)
{
    if (value == NULL || allowed_values == NULL) {
        return false;
    }

    for (size_t index = 0; index < allowed_count; index++) {
        if (strcmp(value, allowed_values[index]) == 0) {
            return true;
        }
    }

    return false;
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static bool is_valid_iso_date(const char *date)
{
    int year;
    int month;
    int day;
    int max_day;
    static const int days_per_month[] = {
            31, 28, 31, 30, 31, 30,
            31, 31, 30, 31, 30, 31
    };

    if (date == NULL || date[0] == '\0') {
        return true;
    }

    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }

    for (size_t index = 0; index < 10; index++) {
        if (index == 4 || index == 7) {
            continue;
        }

        if (date[index] < '0' || date[index] > '9') {
            return false;
        }
    }

    year = (date[0] - '0') * 1000 +
           (date[1] - '0') * 100 +
           (date[2] - '0') * 10 +
           (date[3] - '0');
    month = (date[5] - '0') * 10 + (date[6] - '0');
    day = (date[8] - '0') * 10 + (date[9] - '0');

    if (year < 2000 || year > 9999 || month < 1 || month > 12) {
        return false;
    }

    max_day = days_per_month[month - 1];

    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }

    return day >= 1 && day <= max_day;
}

bool parse_booking_request(const string *request, booking_request *booking)
{
    static const char *const allowed_dog_sizes[] = {
            "small",
            "medium",
            "large",
            "very_large"
    };
    static const char *const allowed_services[] = {
            "consultation",
            "wash_dry",
            "full_groom",
            "claw_care",
            "other"
    };
    char privacy_consent[32];

    if (request == NULL || booking == NULL) {
        return false;
    }

    memset(booking, 0, sizeof(*booking));
    memset(privacy_consent, 0, sizeof(privacy_consent));

    if (!get_required_field(
            request,
            "name",
            booking->name,
            sizeof(booking->name)) ||
        !get_required_field(
            request,
            "contact",
            booking->contact,
            sizeof(booking->contact)) ||
        !get_optional_single_line_field(
            request,
            "dog_name",
            booking->dog_name,
            sizeof(booking->dog_name)) ||
        !get_required_field(
            request,
            "dog_size",
            booking->dog_size,
            sizeof(booking->dog_size)) ||
        !get_required_field(
            request,
            "service",
            booking->service,
            sizeof(booking->service)) ||
        !get_optional_single_line_field(
            request,
            "preferred_date",
            booking->preferred_date,
            sizeof(booking->preferred_date)) ||
        !get_optional_message(
            request,
            booking->message,
            sizeof(booking->message)) ||
        !get_required_field(
            request,
            "privacy_consent",
            privacy_consent,
            sizeof(privacy_consent))) {
        return false;
    }

    if (!equals_one_of(
            booking->dog_size,
            allowed_dog_sizes,
            sizeof(allowed_dog_sizes) / sizeof(allowed_dog_sizes[0])) ||
        !equals_one_of(
            booking->service,
            allowed_services,
            sizeof(allowed_services) / sizeof(allowed_services[0])) ||
        !is_valid_iso_date(booking->preferred_date) ||
        strcmp(privacy_consent, PRIVACY_CONSENT_VALUE) != 0) {
        return false;
    }

    return true;
}

int save_booking_request(const booking_request *booking)
{
    return booking_database_insert(booking);
}

static void append_html_char_escaped(string *destination, char character)
{
    switch (character) {
        case '&':
            str_cat_cstr(destination, "&amp;");
            break;
        case '<':
            str_cat_cstr(destination, "&lt;");
            break;
        case '>':
            str_cat_cstr(destination, "&gt;");
            break;
        case '"':
            str_cat_cstr(destination, "&quot;");
            break;
        case '\'':
            str_cat_cstr(destination, "&#39;");
            break;
        default:
            str_cat(destination, &character, 1);
            break;
    }
}

static void append_html_text(string *destination, const char *source)
{
    if (destination == NULL || source == NULL) {
        return;
    }

    for (size_t index = 0; source[index] != '\0'; index++) {
        append_html_char_escaped(destination, source[index]);
    }
}

static void append_html_text_or_fallback(
        string *destination,
        const char *source,
        const char *fallback
)
{
    if (source == NULL || source[0] == '\0') {
        str_cat_cstr(destination, fallback);
        return;
    }

    append_html_text(destination, source);
}

static const char *dog_size_label(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "Nicht angegeben";
    }

    if (strcmp(value, "small") == 0) {
        return "Klein";
    }

    if (strcmp(value, "medium") == 0) {
        return "Mittel";
    }

    if (strcmp(value, "large") == 0) {
        return "Groß";
    }

    if (strcmp(value, "very_large") == 0) {
        return "Sehr groß";
    }

    return value;
}

static const char *service_label(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "Nicht angegeben";
    }

    if (strcmp(value, "consultation") == 0) {
        return "Beratung";
    }

    if (strcmp(value, "wash_dry") == 0) {
        return "Waschen und Föhnen";
    }

    if (strcmp(value, "full_groom") == 0) {
        return "Komplettpflege";
    }

    if (strcmp(value, "claw_care") == 0) {
        return "Krallenpflege";
    }

    if (strcmp(value, "other") == 0) {
        return "Sonstiges";
    }

    return value;
}

static const char *status_label(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return "Unbekannt";
    }

    if (strcmp(value, "neu") == 0) {
        return "Neu";
    }

    if (strcmp(value, "kontaktiert") == 0) {
        return "Kontaktiert";
    }

    if (strcmp(value, "erledigt") == 0) {
        return "Erledigt";
    }

    if (strcmp(value, "altbestand") == 0) {
        return "Altbestand";
    }

    return value;
}

static void append_booking_detail(
        string *page,
        const char *label,
        const char *value,
        const char *fallback
)
{
    str_cat_cstr(page, "                    <p><span>");
    str_cat_cstr(page, label);
    str_cat_cstr(page, "</span><strong>");
    append_html_text_or_fallback(page, value, fallback);
    str_cat_cstr(page, "</strong></p>\n");
}

static void append_booking_card(
        const booking_record *record,
        void *context_value
)
{
    admin_page_context *context = context_value;
    string *page;

    if (record == NULL || context == NULL || context->page == NULL) {
        return;
    }

    page = context->page;

    str_cat_cstr(page,
            "            <article class=\"booking-card\">\n"
            "                <div class=\"booking-card-head\">\n"
            "                    <p class=\"booking-time\">");
    append_html_text(page, record->created_at);
    str_cat_cstr(page,
            "</p>\n"
            "                    <span class=\"booking-status\">");
    append_html_text(page, status_label(record->status));
    str_cat_cstr(page,
            "</span>\n"
            "                </div>\n"
            "                <h2>");
    append_html_text(page, record->name);
    str_cat_cstr(page,
            "</h2>\n"
            "                <div class=\"booking-details\">\n");

    append_booking_detail(page, "Kontakt", record->contact, "Nicht angegeben");
    append_booking_detail(page, "Hund", record->dog_name, "Nicht angegeben");
    append_booking_detail(page, "Größe", dog_size_label(record->dog_size), "Nicht angegeben");
    append_booking_detail(page, "Leistung", service_label(record->service), "Nicht angegeben");
    append_booking_detail(page, "Wunschdatum", record->preferred_date, "Flexibel");

    if (record->legacy) {
        append_booking_detail(page, "Format", "Frühere Anfrage", "Frühere Anfrage");
    }

    str_cat_cstr(page,
            "                </div>\n"
            "                <div class=\"booking-message\">");
    append_html_text_or_fallback(
            page,
            record->message,
            "Keine Nachricht angegeben.");
    str_cat_cstr(page,
            "</div>\n"
            "            </article>\n");

    context->booking_count++;
}

string *build_booking_admin_page(void)
{
    admin_page_context context;
    string *page = _new_string();
    int database_result;

    context.page = page;
    context.booking_count = 0;

    str_cat_cstr(page,
            "<!doctype html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <meta name=\"robots\" content=\"noindex,nofollow\">\n"
            "    <title>Buchungsanfragen - Styles 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <header class=\"site-header\">\n"
            "        <div class=\"container nav-wrap\">\n"
            "            <a class=\"brand\" href=\"/\"><span class=\"brand-mark\">S4D</span><span>Styles 4 Dogs</span></a>\n"
            "            <nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/\">Website öffnen</a><a href=\"/admin/bookings\" aria-current=\"page\">Buchungsanfragen</a></nav>\n"
            "        </div>\n"
            "    </header>\n"
            "    <main class=\"page admin-page\">\n"
            "        <section class=\"card admin-card\">\n"
            "            <p class=\"eyebrow\">Admin</p>\n"
            "            <h1>Buchungsanfragen</h1>\n"
            "            <p class=\"admin-intro\">Hier findest du alle gespeicherten Terminanfragen.</p>\n"
            "            <div class=\"booking-grid\">\n");

    database_result = booking_database_for_each_newest(
            append_booking_card,
            &context);

    if (database_result != 0) {
        str_cat_cstr(page,
                "                <p>Die Buchungsanfragen konnten momentan nicht geladen werden.</p>\n");
    } else if (context.booking_count == 0) {
        str_cat_cstr(page,
                "                <p>Es wurden noch keine Buchungsanfragen gespeichert.</p>\n");
    }

    str_cat_cstr(page,
            "            </div>\n"
            "            <p><a href=\"/\">Zurück zur Startseite</a></p>\n"
            "        </section>\n"
            "    </main>\n"
            "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer>\n"
            "</body>\n"
            "</html>\n");

    return page;
}
