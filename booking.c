//
// Created by Marlon on 16.07.26.
//

#include "booking.h"

#include "contact_validation.h"
#include "contact_links.h"
#include "server_config.h"

#include "booking_database.h"
#include "calendar_time.h"
#include "form_urlencoded.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PRIVACY_CONSENT_VALUE "accepted"

typedef struct admin_page_context {
    string *page;
    const char *csrf_token;
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

bool booking_request_hits_honeypot(const string *request)
{
    char value[256];
    form_value_result result;

    if (request == NULL) {
        return false;
    }

    result = form_urlencoded_get(
            request,
            "company_website",
            value,
            sizeof(value));

    if (result == FORM_VALUE_NOT_FOUND) {
        return false;
    }
    if (result != FORM_VALUE_OK) {
        return true;
    }

    trim_ascii_whitespace(value);
    return value[0] != '\0';
}


static bool parse_customer_name(const string *request, booking_request *booking)
{
    form_value_result first_name_result;
    form_value_result last_name_result;
    int written;

    if (request == NULL || booking == NULL) {
        return false;
    }

    first_name_result = form_urlencoded_get(
            request,
            "first_name",
            booking->first_name,
            sizeof(booking->first_name));
    last_name_result = form_urlencoded_get(
            request,
            "last_name",
            booking->last_name,
            sizeof(booking->last_name));

    /*
     * Übergangsweise bleibt das frühere Feld "name" gültig. Dadurch
     * funktionieren bereits geladene Formulare und ältere API-Clients nach
     * dem Deployment weiter. Neue Formulare senden Vor- und Nachname separat.
     */
    if (first_name_result == FORM_VALUE_NOT_FOUND &&
        last_name_result == FORM_VALUE_NOT_FOUND) {
        return get_required_field(
                request,
                "name",
                booking->name,
                sizeof(booking->name));
    }

    if (first_name_result != FORM_VALUE_OK ||
        last_name_result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(booking->first_name);
    trim_ascii_whitespace(booking->last_name);

    if (!is_nonempty_single_line(booking->first_name) ||
        !is_nonempty_single_line(booking->last_name)) {
        return false;
    }

    written = snprintf(
            booking->name,
            sizeof(booking->name),
            "%s %s",
            booking->first_name,
            booking->last_name);

    return written >= 0 && (size_t)written < sizeof(booking->name);
}


static bool parse_contact_fields(const string *request, booking_request *booking)
{
    if (!get_required_field(
            request,
            "contact_channel",
            booking->contact_channel,
            sizeof(booking->contact_channel)) ||
        !get_optional_single_line_field(
            request,
            "email",
            booking->email,
            sizeof(booking->email)) ||
        !get_optional_single_line_field(
            request,
            "phone_number",
            booking->phone_number,
            sizeof(booking->phone_number)) ||
        !get_optional_single_line_field(
            request,
            "phone_kind",
            booking->phone_kind,
            sizeof(booking->phone_kind)) ||
        !get_optional_single_line_field(
            request,
            "contact_preference",
            booking->contact_preference,
            sizeof(booking->contact_preference))) {
        return false;
    }

    if (!contact_fields_are_valid(
            booking->contact_channel,
            booking->email,
            booking->phone_number,
            booking->phone_kind,
            booking->contact_preference)) {
        return false;
    }

    snprintf(
            booking->contact,
            sizeof(booking->contact),
            "%s",
            strcmp(booking->contact_channel, "email") == 0
                    ? booking->email
                    : booking->phone_number);
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

bool parse_booking_request(const string *request, booking_request *booking)
{
    static const char *const allowed_dog_sizes[] = {
            "small",
            "medium",
            "large",
            "very_large"
    };
    char privacy_consent[32];
    int appointment_start_minute;

    if (request == NULL || booking == NULL) {
        return false;
    }

    memset(booking, 0, sizeof(*booking));
    memset(privacy_consent, 0, sizeof(privacy_consent));

    if (!parse_customer_name(request, booking) ||
        !parse_contact_fields(request, booking) ||
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
        !get_required_field(
            request,
            "appointment_date",
            booking->appointment_date,
            sizeof(booking->appointment_date)) ||
        !get_required_field(
            request,
            "appointment_start",
            booking->appointment_start,
            sizeof(booking->appointment_start)) ||
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
        !calendar_date_is_valid(booking->appointment_date) ||
        calendar_time_parse_hhmm(booking->appointment_start, &appointment_start_minute) != 0 ||
        strcmp(privacy_consent, PRIVACY_CONSENT_VALUE) != 0) {
        return false;
    }

    for (size_t index = 0; booking->service[index] != '\0'; index++) {
        char character = booking->service[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return false;
        }
    }

    return booking->service[0] != '\0';
}

static bool is_valid_admin_filter_status(const char *status)
{
    if (status == NULL || status[0] == '\0') {
        return true;
    }

    return strcmp(status, "neu") == 0 ||
           strcmp(status, "kontaktiert") == 0 ||
           strcmp(status, "erledigt") == 0 ||
           strcmp(status, "altbestand") == 0;
}

bool parse_booking_admin_filter(
        const char *query,
        size_t query_length,
        booking_admin_filter *filter
)
{
    form_value_result status_result;
    form_value_result search_result;

    if (filter == NULL) {
        return false;
    }

    memset(filter, 0, sizeof(*filter));

    if (query == NULL || query_length == 0) {
        return true;
    }

    status_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "status",
            filter->status,
            sizeof(filter->status));
    search_result = form_urlencoded_get_from_data(
            query,
            query_length,
            "q",
            filter->search,
            sizeof(filter->search));

    if (status_result == FORM_VALUE_NOT_FOUND) {
        filter->status[0] = '\0';
    } else if (status_result != FORM_VALUE_OK) {
        return false;
    }

    if (search_result == FORM_VALUE_NOT_FOUND) {
        filter->search[0] = '\0';
    } else if (search_result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(filter->status);
    trim_ascii_whitespace(filter->search);

    if (strcmp(filter->status, "all") == 0) {
        filter->status[0] = '\0';
    }

    return is_valid_admin_filter_status(filter->status) &&
           is_optional_single_line(filter->search);
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

static bool is_mutable_status(const char *status)
{
    if (status == NULL) {
        return false;
    }

    return strcmp(status, "neu") == 0 ||
           strcmp(status, "kontaktiert") == 0 ||
           strcmp(status, "erledigt") == 0;
}

static void append_size_value(string *destination, size_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%zu", value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat_cstr(destination, buffer);
    }
}

static void append_filter_option(
        string *page,
        const char *value,
        const char *label,
        const char *current_status
)
{
    str_cat_cstr(page, "                    <option value=\"");
    append_html_text(page, value);
    str_cat_cstr(page, "\"");

    if (current_status != NULL && strcmp(value, current_status) == 0) {
        str_cat_cstr(page, " selected");
    }

    str_cat_cstr(page, ">");
    append_html_text(page, label);
    str_cat_cstr(page, "</option>\n");
}

static void append_status_summary(
        string *page,
        const booking_status_counts *counts
)
{
    if (page == NULL || counts == NULL) {
        return;
    }

    str_cat_cstr(page,
            "            <div class=\"admin-summary\" aria-label=\"Buchungsübersicht\">\n"
            "                <div><span>Gesamt</span><strong>");
    append_size_value(page, counts->total);
    str_cat_cstr(page, "</strong></div>\n                <div><span>Neu</span><strong>");
    append_size_value(page, counts->new_count);
    str_cat_cstr(page, "</strong></div>\n                <div><span>Kontaktiert</span><strong>");
    append_size_value(page, counts->contacted_count);
    str_cat_cstr(page, "</strong></div>\n                <div><span>Erledigt</span><strong>");
    append_size_value(page, counts->completed_count);
    str_cat_cstr(page, "</strong></div>\n                <div><span>Altbestand</span><strong>");
    append_size_value(page, counts->legacy_count);
    str_cat_cstr(page, "</strong></div>\n            </div>\n");
}

static void append_admin_filter_form(
        string *page,
        const booking_admin_filter *filter
)
{
    if (page == NULL || filter == NULL) {
        return;
    }

    str_cat_cstr(page,
            "            <form class=\"admin-filter-form\" method=\"get\" action=\"/admin/bookings\">\n"
            "                <div class=\"admin-filter-field\">\n"
            "                    <label for=\"admin-status-filter\">Status</label>\n"
            "                    <select id=\"admin-status-filter\" name=\"status\">\n");

    append_filter_option(page, "", "Alle Status", filter->status);
    append_filter_option(page, "neu", "Neu", filter->status);
    append_filter_option(page, "kontaktiert", "Kontaktiert", filter->status);
    append_filter_option(page, "erledigt", "Erledigt", filter->status);
    append_filter_option(page, "altbestand", "Altbestand", filter->status);

    str_cat_cstr(page,
            "                    </select>\n"
            "                </div>\n"
            "                <div class=\"admin-filter-field admin-search-field\">\n"
            "                    <label for=\"admin-search\">Suche</label>\n"
            "                    <input id=\"admin-search\" name=\"q\" type=\"search\" "
            "maxlength=\"127\" placeholder=\"Name, Kontakt oder Hund\" value=\"");
    append_html_text(page, filter->search);
    str_cat_cstr(page,
            "\">\n"
            "                </div>\n"
            "                <button class=\"button button-small\" type=\"submit\">Filtern</button>\n");

    if (filter->status[0] != '\0' || filter->search[0] != '\0') {
        str_cat_cstr(page,
                "                <a class=\"button button-small button-secondary\" "
                "href=\"/admin/bookings\">Zurücksetzen</a>\n");
    }

    str_cat_cstr(page, "            </form>\n");
}

static void append_status_option(
        string *page,
        const char *value,
        const char *label,
        const char *current_status
)
{
    str_cat_cstr(page, "                        <option value=\"");
    append_html_text(page, value);
    str_cat_cstr(page, "\"");

    if (current_status != NULL && strcmp(value, current_status) == 0) {
        str_cat_cstr(page, " selected");
    }

    str_cat_cstr(page, ">");
    append_html_text(page, label);
    str_cat_cstr(page, "</option>\n");
}

static void append_status_form(
        string *page,
        const booking_record *record,
        const char *csrf_token
)
{
    char id_text[32];
    int written;

    if (page == NULL || record == NULL || csrf_token == NULL) {
        return;
    }

    written = snprintf(id_text, sizeof(id_text), "%" PRId64, record->id);

    if (written <= 0 || (size_t)written >= sizeof(id_text)) {
        return;
    }

    str_cat_cstr(page,
            "                <form class=\"booking-status-form\" "
            "method=\"post\" action=\"/admin/bookings/status\">\n"
            "                    <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page,
            "\">\n"
            "                    <input type=\"hidden\" name=\"booking_id\" value=\"");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\">\n"
            "                    <label for=\"booking-status-");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\">Status ändern</label>\n"
            "                    <div class=\"booking-status-controls\">\n"
            "                    <select id=\"booking-status-");
    append_html_text(page, id_text);
    str_cat_cstr(page, "\" name=\"status\" required>\n");

    if (!is_mutable_status(record->status)) {
        str_cat_cstr(page,
                "                        <option value=\"\" selected disabled>"
                "Neuen Status wählen</option>\n");
    }

    append_status_option(page, "neu", "Neu", record->status);
    append_status_option(page, "kontaktiert", "Kontaktiert", record->status);
    append_status_option(page, "erledigt", "Erledigt", record->status);

    str_cat_cstr(page,
            "                    </select>\n"
            "                    <button class=\"button button-small\" type=\"submit\">"
            "Speichern</button>\n"
            "                    </div>\n"
            "                </form>\n");
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

static const char *decision_status_label(const char *status)
{
    if (status == NULL || status[0] == '\0' || strcmp(status, "legacy") == 0) {
        return "Frühere Anfrage";
    }
    if (strcmp(status, "pending") == 0) {
        return "Angefragt – vorläufig reserviert";
    }
    if (strcmp(status, "confirmed") == 0) {
        return "Bestätigt";
    }
    if (strcmp(status, "rejected") == 0) {
        return "Abgelehnt";
    }
    if (strcmp(status, "cancelled") == 0) {
        return "Abgesagt";
    }
    if (strcmp(status, "expired") == 0) {
        return "Reservierung abgelaufen";
    }

    return status;
}

static const char *phone_kind_label(const char *value)
{
    if (value != NULL && strcmp(value, "mobile") == 0) {
        return "Mobilfunknummer";
    }
    if (value != NULL && strcmp(value, "landline") == 0) {
        return "Festnetznummer";
    }
    return "Telefonnummer";
}

static const char *contact_preference_label(const char *value)
{
    if (value != NULL && strcmp(value, "whatsapp") == 0) {
        return "WhatsApp-Nachricht";
    }
    if (value != NULL && strcmp(value, "call") == 0) {
        return "Anruf";
    }
    return "Nicht angegeben";
}

static void append_contact_details(string *page, const booking_record *record)
{
    if (record->contact_channel != NULL &&
        strcmp(record->contact_channel, "email") == 0) {
        append_booking_detail(page, "Kontaktweg", "E-Mail", "E-Mail");
        append_booking_detail(page, "E-Mail", record->email, "Nicht angegeben");
        return;
    }

    if (record->contact_channel != NULL &&
        strcmp(record->contact_channel, "phone") == 0) {
        append_booking_detail(page, "Kontaktweg", "Telefon", "Telefon");
        append_booking_detail(
                page,
                phone_kind_label(record->phone_kind),
                record->phone_number,
                "Nicht angegeben");
        append_booking_detail(
                page,
                "Gewünschte Rückmeldung",
                contact_preference_label(record->contact_preference),
                "Nicht angegeben");
        return;
    }

    append_booking_detail(page, "Kontakt", record->contact, "Nicht angegeben");
}


static void append_contact_quick_actions(string *page, const booking_record *record)
{
    char e164[32];
    char whatsapp_digits[32];

    if (page == NULL || record == NULL) {
        return;
    }

    str_cat_cstr(page, "<div class=\"contact-quick-actions\">");

    if (record->contact_channel != NULL &&
        strcmp(record->contact_channel, "email") == 0 &&
        record->email != NULL && record->email[0] != '\0') {
        str_cat_cstr(page, "<a class=\"button button-small button-secondary\" href=\"mailto:");
        append_html_text(page, record->email);
        str_cat_cstr(page, "\">E-Mail schreiben</a>");
    } else if (record->contact_channel != NULL &&
               strcmp(record->contact_channel, "phone") == 0 &&
               record->phone_number != NULL &&
               contact_phone_to_e164(
                       record->phone_number,
                       server_config_default_phone_country_code(),
                       e164,
                       sizeof(e164))) {
        str_cat_cstr(page, "<a class=\"button button-small button-secondary\" href=\"tel:");
        append_html_text(page, e164);
        str_cat_cstr(page, "\">Anrufen</a>");

        if (record->phone_kind != NULL && strcmp(record->phone_kind, "mobile") == 0 &&
            contact_e164_to_whatsapp_digits(e164, whatsapp_digits, sizeof(whatsapp_digits))) {
            str_cat_cstr(page,
                    "<a class=\"button button-small button-whatsapp\" target=\"_blank\" "
                    "rel=\"noopener noreferrer\" href=\"https://wa.me/");
            append_html_text(page, whatsapp_digits);
            str_cat_cstr(page, "\">WhatsApp öffnen</a>");
        }
    }

    str_cat_cstr(page, "</div>");
}

static void append_decision_form(
        string *page,
        const booking_record *record,
        const char *csrf_token
)
{
    char id_text[32];
    int written;

    if (record == NULL || csrf_token == NULL ||
        record->decision_status == NULL ||
        strcmp(record->decision_status, "pending") != 0) {
        return;
    }

    written = snprintf(id_text, sizeof(id_text), "%" PRId64, record->id);
    if (written <= 0 || (size_t)written >= sizeof(id_text)) {
        return;
    }

    str_cat_cstr(page,
            "                <section class=\"booking-decision-panel\" aria-label=\"Terminanfrage entscheiden\">\n"
            "                    <h3>Terminanfrage entscheiden</h3>\n"
            "                    <div class=\"booking-decision-actions\">\n"
            "                        <form method=\"post\" action=\"/admin/bookings/accept\">\n"
            "                            <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\"><button class=\"button button-small\" type=\"submit\">Termin annehmen</button>\n"
            "                        </form>\n"
            "                        <form class=\"booking-reject-form\" method=\"post\" action=\"/admin/bookings/reject\">\n"
            "                            <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\">\n"
            "                            <label>Ablehnungsgrund (optional)<input name=\"rejection_reason\" maxlength=\"500\" placeholder=\"Zum Beispiel Termin leider nicht möglich\"></label>\n"
            "                            <button class=\"button button-small button-danger\" type=\"submit\">Termin ablehnen</button>\n"
            "                        </form>\n"
            "                    </div>\n"
            "                </section>\n");
}

static void append_appointment_detail(
        string *page,
        const booking_record *record
)
{
    char time_text[32];
    char date_text[48];
    char start_text[6];
    char end_text[6];

    if (record->appointment_date == NULL || record->appointment_date[0] == '\0' ||
        record->start_minute < 0 || record->end_minute < 0 ||
        calendar_time_format_hhmm(record->start_minute, start_text) != 0 ||
        calendar_time_format_hhmm(record->end_minute, end_text) != 0) {
        const char *preferred = record->preferred_date;

        if (preferred != NULL && preferred[0] != '\0' &&
            calendar_date_format_de(preferred, true, date_text, sizeof(date_text)) == 0) {
            preferred = date_text;
        }
        append_booking_detail(page, "Wunschdatum", preferred, "Noch ohne festen Termin");
        return;
    }

    if (snprintf(time_text, sizeof(time_text), "%s–%s Uhr", start_text, end_text) <= 0) {
        append_booking_detail(page, "Termin", record->appointment_date, "Nicht angegeben");
        return;
    }

    if (calendar_date_format_de(
            record->appointment_date,
            true,
            date_text,
            sizeof(date_text)) != 0) {
        snprintf(date_text, sizeof(date_text), "%s", record->appointment_date);
    }

    append_booking_detail(page, "Termindatum", date_text, "Nicht angegeben");
    append_booking_detail(page, "Uhrzeit", time_text, "Nicht angegeben");
    append_booking_detail(
            page,
            "Terminstatus",
            decision_status_label(record->decision_status),
            "Nicht angegeben");
}

static void append_booking_card(
        const booking_record *record,
        void *context_value
)
{
    admin_page_context *context = context_value;
    string *page;
    char created_at_display[80];

    if (record == NULL || context == NULL || context->page == NULL) {
        return;
    }

    page = context->page;

    snprintf(
            created_at_display,
            sizeof(created_at_display),
            "%s",
            record->created_at == NULL ? "" : record->created_at);
    if (record->created_at != NULL && strlen(record->created_at) >= 10) {
        char iso_date[11];
        char date_text[24];

        memcpy(iso_date, record->created_at, 10);
        iso_date[10] = '\0';
        if (calendar_date_format_de(iso_date, false, date_text, sizeof(date_text)) == 0) {
            if (strlen(record->created_at) >= 16) {
                snprintf(
                        created_at_display,
                        sizeof(created_at_display),
                        "%s, %.5s Uhr",
                        date_text,
                        record->created_at + 11);
            } else {
                snprintf(created_at_display, sizeof(created_at_display), "%s", date_text);
            }
        }
    }

    str_cat_cstr(page,
            "            <article class=\"booking-card\">\n"
            "                <div class=\"booking-card-head\">\n"
            "                    <div><p class=\"booking-time\">");
    append_html_text(page, created_at_display);
    str_cat_cstr(page, "</p><p class=\"booking-id\">Anfrage #");
    {
        char id_text[32];
        int written = snprintf(id_text, sizeof(id_text), "%" PRId64, record->id);

        if (written > 0 && (size_t)written < sizeof(id_text)) {
            append_html_text(page, id_text);
        }
    }
    str_cat_cstr(page,
            "</p></div>\n"
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

    append_contact_details(page, record);
    append_booking_detail(page, "Hund", record->dog_name, "Nicht angegeben");
    append_booking_detail(page, "Größe", dog_size_label(record->dog_size), "Nicht angegeben");
    append_booking_detail(
            page,
            "Leistung",
            record->service_name_snapshot != NULL && record->service_name_snapshot[0] != '\0'
                    ? record->service_name_snapshot
                    : service_label(record->service),
            "Nicht angegeben");
    append_appointment_detail(page, record);

    if (record->legacy) {
        append_booking_detail(page, "Format", "Frühere Anfrage", "Frühere Anfrage");
    }

    str_cat_cstr(page, "                </div>\n");
    append_contact_quick_actions(page, record);
    str_cat_cstr(page, "                <div class=\"booking-message\">");
    append_html_text_or_fallback(
            page,
            record->message,
            "Keine Nachricht angegeben.");
    str_cat_cstr(page, "</div>\n");
    if (record->rejection_reason != NULL && record->rejection_reason[0] != '\0') {
        append_booking_detail(page, "Ablehnungsgrund", record->rejection_reason, "Nicht angegeben");
    }
    append_decision_form(page, record, context->csrf_token);
    append_status_form(page, record, context->csrf_token);
    str_cat_cstr(page, "            </article>\n");

    context->booking_count++;
}

string *build_booking_admin_page(
        const char *csrf_token,
        const booking_admin_filter *filter
)
{
    admin_page_context context;
    booking_status_counts counts;
    string *page = _new_string();
    int database_result;
    int counts_result;

    if (csrf_token == NULL || csrf_token[0] == '\0' || filter == NULL) {
        free_str(page);
        return NULL;
    }

    context.page = page;
    context.csrf_token = csrf_token;
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
            "            <nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/\">Website öffnen</a><a href=\"/admin/bookings\" aria-current=\"page\">Buchungsanfragen</a><a href=\"/admin/appointments\">Termine</a><a href=\"/admin/calendar\">Einstellungen</a><a href=\"/admin/notifications\">E-Mail</a></nav>\n"
            "        </div>\n"
            "    </header>\n"
            "    <main class=\"page admin-page\">\n"
            "        <section class=\"card admin-card\">\n"
            "            <p class=\"eyebrow\">Admin</p>\n"
            "            <h1>Buchungsanfragen</h1>\n"
            "            <p class=\"admin-intro\">Hier findest du alle gespeicherten Terminanfragen.</p>\n");

    counts_result = booking_database_get_status_counts(&counts);

    if (counts_result == 0) {
        append_status_summary(page, &counts);
    }

    append_admin_filter_form(page, filter);
    str_cat_cstr(page, "            <div class=\"booking-grid\">\n");

    database_result = booking_database_for_each_filtered(
            filter,
            append_booking_card,
            &context);

    if (database_result != 0) {
        str_cat_cstr(page,
                "                <p>Die Buchungsanfragen konnten momentan nicht geladen werden.</p>\n");
    } else if (context.booking_count == 0 &&
               (filter->status[0] != '\0' || filter->search[0] != '\0')) {
        str_cat_cstr(page,
                "                <p>Für diesen Filter wurden keine Buchungsanfragen gefunden.</p>\n");
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
