#include "styles4dogs/admin/admin_booking_management.h"

#include "styles4dogs/booking/booking_management.h"
#include "styles4dogs/calendar/calendar_database.h"
#include "styles4dogs/calendar/calendar_time.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ADMIN_BOOKING_ERROR_SIZE 512
#define ADMIN_BOOKING_MAX_SERVICES 128

static char module_error[ADMIN_BOOKING_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(module_error, sizeof(module_error), "%s",
             message == NULL ? "Unbekannter Buchungsverwaltungsfehler" : message);
}

const char *admin_booking_management_last_error(void)
{
    return module_error[0] == '\0' ? "Unbekannter Buchungsverwaltungsfehler" : module_error;
}

static void append_html(string *output, const char *text)
{
    for (size_t index = 0; text != NULL && text[index] != '\0'; index++) {
        switch (text[index]) {
            case '&': str_cat_cstr(output, "&amp;"); break;
            case '<': str_cat_cstr(output, "&lt;"); break;
            case '>': str_cat_cstr(output, "&gt;"); break;
            case '"': str_cat_cstr(output, "&quot;"); break;
            case '\'': str_cat_cstr(output, "&#39;"); break;
            default: str_cat(output, text + index, 1); break;
        }
    }
}

static void append_int64(string *output, int64_t value)
{
    char text[32];
    int written = snprintf(text, sizeof(text), "%" PRId64, value);
    if (written > 0 && (size_t)written < sizeof(text)) str_cat(output, text, (size_t)written);
}



static void append_selected(string *output, const char *value, const char *current)
{
    if (value != NULL && current != NULL && strcmp(value, current) == 0) {
        str_cat_cstr(output, " selected");
    }
}

static void append_option(string *output, const char *value, const char *label, const char *current)
{
    str_cat_cstr(output, "<option value=\"");
    append_html(output, value);
    str_cat_cstr(output, "\"");
    append_selected(output, value, current);
    str_cat_cstr(output, ">");
    append_html(output, label);
    str_cat_cstr(output, "</option>");
}

typedef struct service_collection {
    calendar_service values[ADMIN_BOOKING_MAX_SERVICES];
    size_t count;
} service_collection;

static int collect_service(const calendar_service *service, void *opaque)
{
    service_collection *collection = opaque;
    if (service == NULL || collection == NULL || collection->count >= ADMIN_BOOKING_MAX_SERVICES) return 1;
    collection->values[collection->count++] = *service;
    return 0;
}

static const char *notice_text(const char *code)
{
    if (code == NULL || code[0] == '\0') return NULL;
    if (strcmp(code, "updated") == 0) return "Die Buchungsdaten wurden gespeichert.";
    if (strcmp(code, "rescheduled") == 0) return "Der Termin wurde verschoben und die Kundenmail eingereiht.";
    if (strcmp(code, "no-show") == 0) return "Der Termin wurde als nicht erschienen markiert.";
    if (strcmp(code, "dog-note") == 0) return "Die interne Hundenotiz wurde gespeichert.";
    if (strcmp(code, "conflict") == 0) return "Der gewünschte Zeitraum ist nicht verfügbar.";
    if (strcmp(code, "invalid") == 0) return "Die eingegebenen Daten sind unvollständig oder ungültig.";
    if (strcmp(code, "not-allowed") == 0) return "Diese Änderung ist für den aktuellen Status nicht möglich.";
    return NULL;
}

static bool notice_is_error(const char *code)
{
    return code != NULL &&
           (strcmp(code, "conflict") == 0 ||
            strcmp(code, "invalid") == 0 ||
            strcmp(code, "not-allowed") == 0);
}

string *admin_booking_management_build_page(
        int64_t booking_id,
        const char *csrf_token,
        const char *notice_code,
        const char *admin_username
)
{
    booking_management_record record;
    booking_management_result load_result;
    service_collection services = {0};
    string *history = NULL;
    string *page = NULL;
    char start_time[6] = "";
    const char *notice;

    module_error[0] = '\0';
    if (booking_id <= 0 || csrf_token == NULL || csrf_token[0] == '\0') {
        set_error("Buchungs-ID oder CSRF-Token fehlt");
        return NULL;
    }

    load_result = booking_management_load(booking_id, &record);
    if (load_result != BOOKING_MANAGEMENT_OK) {
        set_error(load_result == BOOKING_MANAGEMENT_NOT_FOUND
                  ? "Buchung wurde nicht gefunden"
                  : booking_management_last_error());
        return NULL;
    }
    if (calendar_database_for_each_service(collect_service, &services) != 0 ||
        calendar_time_format_hhmm(record.start_minute, start_time) != 0) {
        set_error("Leistungen oder Terminzeit konnten nicht geladen werden");
        return NULL;
    }
    history = booking_management_build_history_html(booking_id);
    if (history == NULL) {
        set_error(booking_management_last_error());
        return NULL;
    }

    page = _new_string();
    if (page == NULL) {
        free_str(history);
        set_error("Speicher für die Buchungsansicht konnte nicht reserviert werden");
        return NULL;
    }

    str_cat_cstr(page,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>Buchung bearbeiten - Styling 4 Dogs</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
            "<header class=\"site-header admin-header\"><div class=\"container nav-wrap\">"
            "<a class=\"brand\" href=\"/admin\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Styling 4 Dogs Admin</span></a>"
            "<nav class=\"admin-nav\"><a href=\"/admin\">Übersicht</a><a href=\"/\">Website öffnen</a>"
            "<a class=\"active\" aria-current=\"page\" href=\"/admin/bookings\">Buchungsanfragen</a>"
            "<a href=\"/admin/gallery\">Fotos</a><a href=\"/admin/appointments\">Termine</a>"
            "<a href=\"/admin/notifications\">E-Mail</a><a href=\"/admin/calendar\">Einstellungen</a></nav>"
            "<div class=\"admin-session-bar\"><span>");
    append_html(page, admin_username == NULL ? "admin" : admin_username);
    str_cat_cstr(page, "</span><form method=\"post\" action=\"/admin/logout\"><input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html(page, csrf_token);
    str_cat_cstr(page, "\"><button class=\"button button-secondary button-small\" type=\"submit\">Abmelden</button></form></div>"
            "</div></header><main class=\"page admin-page\"><div class=\"container\">"
            "<section class=\"admin-page-head\"><div><p class=\"eyebrow\">Buchungsverwaltung</p><h1>Buchung #");
    append_int64(page, record.id);
    str_cat_cstr(page, " bearbeiten</h1><p>Änderungen an Datum, Uhrzeit oder Leistung werden als Terminverschiebung behandelt.</p></div>"
                         "<a class=\"button button-secondary\" href=\"/admin/bookings\">Zurück</a></section>");

    notice = notice_text(notice_code);
    if (notice != NULL) {
        str_cat_cstr(page, notice_is_error(notice_code)
                           ? "<p class=\"admin-error\" role=\"alert\">"
                           : "<p class=\"admin-success\" role=\"status\">");
        append_html(page, notice);
        str_cat_cstr(page, "</p>");
    }
    if (record.late_cancellation) {
        str_cat_cstr(page, "<p class=\"booking-late-cancellation\"><strong>Kurzfristig abgesagt</strong> – die Absage erfolgte innerhalb der konfigurierten Frist.</p>");
    }

    str_cat_cstr(page, "<section class=\"card admin-booking-editor\"><h2>Buchungsdaten</h2>"
                         "<form method=\"post\" action=\"/admin/bookings/update\">"
                         "<input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_int64(page, record.id);
    str_cat_cstr(page, "\"><div class=\"admin-booking-grid\">"
                         "<label>Vorname<input name=\"first_name\" maxlength=\"127\" value=\"");
    append_html(page, record.first_name);
    str_cat_cstr(page, "\" required></label><label>Nachname<input name=\"last_name\" maxlength=\"127\" value=\"");
    append_html(page, record.last_name);
    str_cat_cstr(page, "\" required></label><label>E-Mail<input type=\"email\" name=\"email\" maxlength=\"255\" value=\"");
    append_html(page, record.email);
    str_cat_cstr(page, "\"></label><label>Telefon<input name=\"phone_number\" maxlength=\"63\" value=\"");
    append_html(page, record.phone_number);
    str_cat_cstr(page, "\"></label><label>Kontaktkanal<select name=\"contact_channel\">");
    append_option(page, "email", "E-Mail", record.contact_channel);
    append_option(page, "phone", "Telefon", record.contact_channel);
    str_cat_cstr(page, "</select></label><label>Telefonart<select name=\"phone_kind\"><option value=\"\">Keine Angabe</option>");
    append_option(page, "mobile", "Mobil", record.phone_kind);
    append_option(page, "landline", "Festnetz", record.phone_kind);
    str_cat_cstr(page, "</select></label><label>Rückmeldung<select name=\"contact_preference\"><option value=\"\">Keine Angabe</option>");
    append_option(page, "whatsapp", "WhatsApp", record.contact_preference);
    append_option(page, "call", "Anruf", record.contact_preference);
    str_cat_cstr(page, "</select></label><label>Straße und Hausnummer<input name=\"street_address\" maxlength=\"255\" value=\"");
    append_html(page, record.street_address);
    str_cat_cstr(page, "\" required></label><label>Postleitzahl<input name=\"postal_code\" inputmode=\"numeric\" pattern=\"[0-9]{5}\" maxlength=\"5\" value=\"");
    append_html(page, record.postal_code);
    str_cat_cstr(page, "\" required></label><label>Ort<input name=\"city\" maxlength=\"127\" value=\"");
    append_html(page, record.city);
    str_cat_cstr(page, "\" required></label><label>Hundename<input name=\"dog_name\" maxlength=\"127\" value=\"");
    append_html(page, record.dog_name);
    str_cat_cstr(page, "\" required></label><label>Hunderasse <span class=\"field-optional\">(optional)</span><input name=\"dog_breed\" maxlength=\"127\" value=\"");
    append_html(page, record.dog_breed);
    str_cat_cstr(page, "\"></label><label>Hundegröße<select name=\"dog_size\">");
    append_option(page, "small", "Klein", record.dog_size);
    append_option(page, "medium", "Mittel", record.dog_size);
    append_option(page, "large", "Groß", record.dog_size);
    append_option(page, "very_large", "Sehr groß", record.dog_size);
    str_cat_cstr(page, "</select></label><label>Leistung<select name=\"service_code\">");
    for (size_t index = 0; index < services.count; index++) {
        calendar_service *service = &services.values[index];
        if (!service->active && strcmp(service->code, record.service_code) != 0) continue;
        append_option(page, service->code, service->name, record.service_code);
    }
    str_cat_cstr(page, "</select></label><label>Datum<input type=\"date\" name=\"appointment_date\" value=\"");
    append_html(page, record.appointment_date);
    str_cat_cstr(page, "\" required></label><label>Startzeit<input type=\"time\" name=\"appointment_start\" step=\"300\" value=\"");
    append_html(page, start_time);
    str_cat_cstr(page, "\" required></label></div><label>Kundennachricht<textarea name=\"message\" maxlength=\"2000\" rows=\"5\">");
    append_html(page, record.message);
    str_cat_cstr(page, "</textarea></label><label>Interne Adminnotiz<textarea name=\"admin_note\" maxlength=\"2000\" rows=\"5\">");
    append_html(page, record.admin_note);
    str_cat_cstr(page, "</textarea></label><button class=\"button\" type=\"submit\">Änderungen speichern</button></form></section>");

    if (record.dog_id > 0) {
        str_cat_cstr(page, "<section class=\"card admin-booking-editor\"><h2>Interne Hundenotiz</h2>"
                         "<p>Diese Notiz erscheint niemals im Kundenbereich oder in E-Mails.</p>"
                         "<form method=\"post\" action=\"/admin/bookings/dog-note\"><input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_int64(page, record.id);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"dog_id\" value=\"");
    append_int64(page, record.dog_id);
    str_cat_cstr(page, "\"><textarea name=\"dog_note\" maxlength=\"2000\" rows=\"5\">");
    append_html(page, record.dog_internal_note);
        str_cat_cstr(page, "</textarea><button class=\"button button-secondary\" type=\"submit\">Hundenotiz speichern</button></form></section>");
    }

    if (strcmp(record.decision_status, "confirmed") == 0) {
        str_cat_cstr(page, "<section class=\"card admin-booking-danger-zone\"><h2>Terminstatus</h2>"
                             "<form method=\"post\" action=\"/admin/bookings/no-show\"><input type=\"hidden\" name=\"csrf_token\" value=\"");
        append_html(page, csrf_token);
        str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
        append_int64(page, record.id);
        str_cat_cstr(page, "\"><label>Interne Notiz <span class=\"field-optional\">(optional)</span><textarea name=\"note\" maxlength=\"2000\" rows=\"3\"></textarea></label>"
                             "<button class=\"button button-danger\" type=\"submit\">Als nicht erschienen markieren</button></form></section>");
    }

    str_cat_cstr(page, get_const_char_str(history));
    str_cat_cstr(page, "</div></main><footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styling 4 Dogs.</small></div></footer></body></html>");
    free_str(history);
    return page;
}
