#include "admin_appointments.h"

#include "booking_database.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "contact_links.h"
#include "form_urlencoded.h"
#include "notification_queue.h"
#include "server_config.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ADMIN_APPOINTMENTS_ERROR_SIZE 512

typedef struct appointment_day_context {
    string *page;
    const char *csrf_token;
    const char *date;
    size_t booking_count;
    size_t closure_count;
} appointment_day_context;

static char appointments_error[ADMIN_APPOINTMENTS_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            appointments_error,
            sizeof(appointments_error),
            "%s",
            message == NULL ? "Unbekannter Fehler der Terminansicht" : message);
}

const char *admin_appointments_last_error(void)
{
    return appointments_error[0] == '\0'
            ? "Unbekannter Fehler der Terminansicht"
            : appointments_error;
}

static void append_html_text(string *output, const char *text)
{
    if (output == NULL || text == NULL) {
        return;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        switch (text[index]) {
            case '&':
                str_cat_cstr(output, "&amp;");
                break;
            case '<':
                str_cat_cstr(output, "&lt;");
                break;
            case '>':
                str_cat_cstr(output, "&gt;");
                break;
            case '"':
                str_cat_cstr(output, "&quot;");
                break;
            case '\'':
                str_cat_cstr(output, "&#39;");
                break;
            default:
                str_cat(output, text + index, 1);
                break;
        }
    }
}

static void append_display_date(string *page, const char *date)
{
    char display[48];

    if (calendar_date_format_de(date, true, display, sizeof(display)) != 0) {
        append_html_text(page, date);
        return;
    }
    append_html_text(page, display);
}

static void append_size_value(string *page, size_t value)
{
    char text[32];

    snprintf(text, sizeof(text), "%zu", value);
    append_html_text(page, text);
}

static bool month_start_for_date(const char *date, char out_start[11])
{
    int year;
    int month;
    int day;

    if (date == NULL || out_start == NULL ||
        sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        !calendar_date_is_valid(date)) {
        return false;
    }

    return snprintf(out_start, 11, "%04d-%02d-01", year, month) == 10;
}

static bool shift_month(const char *date, int offset, char out_date[11])
{
    int year;
    int month;
    int day;
    int shifted;

    if (date == NULL || out_date == NULL || offset < -1200 || offset > 1200 ||
        sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        !calendar_date_is_valid(date)) {
        return false;
    }

    shifted = year * 12 + (month - 1) + offset;
    if (shifted < 1970 * 12 || shifted > 9999 * 12 + 11) {
        return false;
    }

    year = shifted / 12;
    month = shifted % 12 + 1;
    return snprintf(out_date, 11, "%04d-%02d-01", year, month) == 10;
}

static const char *month_name(int month)
{
    static const char *names[] = {
        "Januar", "Februar", "März", "April", "Mai", "Juni",
        "Juli", "August", "September", "Oktober", "November", "Dezember"
    };

    return month >= 1 && month <= 12 ? names[month - 1] : "Monat";
}

static void append_month_label(string *page, const char *month_start)
{
    int year;
    int month;
    int day;

    if (sscanf(month_start, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        append_html_text(page, month_start);
        return;
    }

    append_html_text(page, month_name(month));
    str_cat_cstr(page, " ");
    {
        char year_text[8];
        snprintf(year_text, sizeof(year_text), "%d", year);
        append_html_text(page, year_text);
    }
}

static const char *decision_label(const char *status)
{
    if (status != NULL && strcmp(status, "confirmed") == 0) {
        return "Bestätigt";
    }
    return "Angefragt";
}

static const char *decision_class(const char *status)
{
    if (status != NULL && strcmp(status, "confirmed") == 0) {
        return "appointment-status-confirmed";
    }
    return "appointment-status-pending";
}

static void append_contact_actions(string *page, const booking_record *record)
{
    char e164[32];
    char whatsapp_digits[32];

    if (record->contact_channel != NULL &&
        strcmp(record->contact_channel, "email") == 0 &&
        record->email != NULL && record->email[0] != '\0') {
        str_cat_cstr(page, "<a class=\"button button-small button-secondary\" href=\"mailto:");
        append_html_text(page, record->email);
        str_cat_cstr(page, "\">E-Mail schreiben</a>");
        return;
    }

    if (record->contact_channel == NULL || strcmp(record->contact_channel, "phone") != 0 ||
        record->phone_number == NULL ||
        !contact_phone_to_e164(
                record->phone_number,
                server_config_default_phone_country_code(),
                e164,
                sizeof(e164))) {
        return;
    }

    str_cat_cstr(page, "<a class=\"button button-small button-secondary\" href=\"tel:");
    append_html_text(page, e164);
    str_cat_cstr(page, "\">Anrufen</a>");

    if (record->phone_kind != NULL && strcmp(record->phone_kind, "mobile") == 0 &&
        contact_e164_to_whatsapp_digits(e164, whatsapp_digits, sizeof(whatsapp_digits))) {
        str_cat_cstr(page,
                "<a class=\"button button-small button-whatsapp\" target=\"_blank\" rel=\"noopener noreferrer\" href=\"https://wa.me/");
        append_html_text(page, whatsapp_digits);
        str_cat_cstr(page, "\">WhatsApp öffnen</a>");
    }
}

static void append_decision_forms(
        string *page,
        const booking_record *record,
        const char *csrf_token
)
{
    char id_text[32];

    if (record->decision_status == NULL || strcmp(record->decision_status, "pending") != 0) {
        return;
    }

    snprintf(id_text, sizeof(id_text), "%" PRId64, record->id);
    str_cat_cstr(page,
            "<div class=\"appointment-decision-actions\">"
            "<form method=\"post\" action=\"/admin/bookings/accept\">"
            "<input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\"><button class=\"button button-small\" type=\"submit\">Annehmen</button></form>"
            "<form class=\"appointment-reject-form\" method=\"post\" action=\"/admin/bookings/reject\">"
            "<input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"booking_id\" value=\"");
    append_html_text(page, id_text);
    str_cat_cstr(page,
            "\"><input name=\"rejection_reason\" maxlength=\"500\" placeholder=\"Ablehnungsgrund (optional)\">"
            "<button class=\"button button-small button-danger\" type=\"submit\">Ablehnen</button>"
            "</form></div>");
}

static void append_appointment_record(
        const booking_record *record,
        void *context_value
)
{
    appointment_day_context *context = context_value;
    char start[6] = "";
    char end[6] = "";
    char id_text[32];

    if (context == NULL || record == NULL ||
        calendar_time_format_hhmm(record->start_minute, start) != 0 ||
        calendar_time_format_hhmm(record->end_minute, end) != 0) {
        return;
    }

    context->booking_count++;
    snprintf(id_text, sizeof(id_text), "%" PRId64, record->id);

    str_cat_cstr(context->page, "<article class=\"appointment-entry appointment-booking ");
    str_cat_cstr(context->page, decision_class(record->decision_status));
    str_cat_cstr(context->page, "\"><div class=\"appointment-entry-head\"><div><strong>");
    append_html_text(context->page, start);
    str_cat_cstr(context->page, "–");
    append_html_text(context->page, end);
    str_cat_cstr(context->page, " Uhr</strong><span>");
    append_html_text(
            context->page,
            record->service_name_snapshot != NULL && record->service_name_snapshot[0] != '\0'
                    ? record->service_name_snapshot
                    : record->service);
    str_cat_cstr(context->page, "</span></div><span class=\"appointment-status\">Termin · ");
    append_html_text(context->page, decision_label(record->decision_status));
    str_cat_cstr(context->page, "</span></div><div class=\"appointment-entry-details\"><p><span>Hund</span><strong>");
    append_html_text(context->page, record->dog_name == NULL || record->dog_name[0] == '\0'
            ? "Nicht angegeben" : record->dog_name);
    str_cat_cstr(context->page, "</strong></p><p><span>Kunde</span><strong>");
    append_html_text(context->page, record->name);
    str_cat_cstr(context->page, "</strong></p><p><span>Kontakt</span><strong>");
    append_html_text(context->page, record->contact);
    str_cat_cstr(context->page, "</strong></p><p><span>Buchung</span><strong>#");
    append_html_text(context->page, id_text);
    str_cat_cstr(context->page, "</strong></p></div><div class=\"contact-quick-actions\">");
    append_contact_actions(context->page, record);
    str_cat_cstr(context->page, "<a class=\"text-link\" href=\"/admin/bookings?search=");
    append_html_text(context->page, id_text);
    str_cat_cstr(context->page, "\">Details öffnen</a></div>");
    append_decision_forms(context->page, record, context->csrf_token);
    str_cat_cstr(context->page, "</article>");
}

static int append_closure_record(
        const calendar_closure *closure,
        void *context_value
)
{
    appointment_day_context *context = context_value;
    int start_minute;
    int end_minute;
    char start[6];
    char end[6];

    if (context == NULL || closure == NULL ||
        strcmp(closure->start_date, context->date) > 0 ||
        strcmp(closure->end_date, context->date) < 0) {
        return 0;
    }

    start_minute = strcmp(closure->start_date, context->date) < 0 ? 0 : closure->start_minute;
    end_minute = strcmp(closure->end_date, context->date) > 0 ? 1440 : closure->end_minute;
    context->closure_count++;

    str_cat_cstr(context->page, "<article class=\"appointment-entry appointment-closure\"><div class=\"appointment-entry-head\"><div><strong>");
    if (start_minute == 0 && end_minute == 1440) {
        str_cat_cstr(context->page, "Ganztägig");
    } else if (calendar_time_format_hhmm(start_minute, start) == 0 &&
               calendar_time_format_hhmm(end_minute, end) == 0) {
        append_html_text(context->page, start);
        str_cat_cstr(context->page, "–");
        append_html_text(context->page, end);
        str_cat_cstr(context->page, " Uhr");
    }
    str_cat_cstr(context->page, "</strong><span>Kalendersperre</span></div><span class=\"appointment-status\">Sperrzeit · Nicht buchbar</span></div><p>");
    append_html_text(context->page, closure->label[0] == '\0' ? "Geschlossen" : closure->label);
    str_cat_cstr(context->page, "</p></article>");
    return 0;
}

static bool parse_view_query(
        const char *query,
        size_t query_length,
        char view[16],
        char date[11],
        const char *default_date
)
{
    char parsed_view[16] = "";
    char parsed_date[11] = "";

    snprintf(view, 16, "week");
    snprintf(date, 11, "%s", default_date);

    if (query == NULL || query_length == 0) {
        return true;
    }

    form_value_result view_result = form_urlencoded_get_from_data(
            query, query_length, "view", parsed_view, sizeof(parsed_view));
    form_value_result date_result = form_urlencoded_get_from_data(
            query, query_length, "date", parsed_date, sizeof(parsed_date));

    if ((view_result != FORM_VALUE_OK && view_result != FORM_VALUE_NOT_FOUND) ||
        (date_result != FORM_VALUE_OK && date_result != FORM_VALUE_NOT_FOUND)) {
        return false;
    }

    if (view_result == FORM_VALUE_OK) {
        if (strcmp(parsed_view, "day") != 0 &&
            strcmp(parsed_view, "week") != 0 &&
            strcmp(parsed_view, "month") != 0) {
            return false;
        }
        snprintf(view, 16, "%s", parsed_view);
    }

    if (date_result == FORM_VALUE_OK) {
        if (!calendar_date_is_valid(parsed_date)) {
            return false;
        }
        snprintf(date, 11, "%s", parsed_date);
    }

    return true;
}

static void append_navigation(
        string *page,
        const char *view,
        const char *range_start,
        const char *today
)
{
    char previous[11];
    char next[11];
    int step;

    if (strcmp(view, "month") == 0) {
        if (!shift_month(range_start, -1, previous) ||
            !shift_month(range_start, 1, next)) {
            return;
        }
    } else {
        step = strcmp(view, "day") == 0 ? 1 : 7;
        if (calendar_date_add_days(range_start, -step, previous) != 0 ||
            calendar_date_add_days(range_start, step, next) != 0) {
            return;
        }
    }

    str_cat_cstr(page, "<nav class=\"appointment-navigation\" aria-label=\"Kalendernavigation\"><a class=\"button button-small button-secondary\" href=\"/admin/appointments?view=");
    append_html_text(page, view);
    str_cat_cstr(page, "&amp;date=");
    append_html_text(page, previous);
    str_cat_cstr(page, "\">Zurück</a><a class=\"button button-small button-secondary\" href=\"/admin/appointments?view=");
    append_html_text(page, view);
    str_cat_cstr(page, "&amp;date=");
    append_html_text(page, today);
    str_cat_cstr(page, "\">Heute</a><a class=\"button button-small button-secondary\" href=\"/admin/appointments?view=");
    append_html_text(page, view);
    str_cat_cstr(page, "&amp;date=");
    append_html_text(page, next);
    str_cat_cstr(page, "\">Weiter</a></nav>");
}

string *admin_appointments_build_page(
        const char *csrf_token,
        const char *query,
        size_t query_length
)
{
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    notification_queue_counts queue_counts;
    char view[16];
    char selected_date[11];
    char range_start[11];
    char next_month[11];
    int weekday;
    int day_count;
    string *page;

    appointments_error[0] = '\0';
    if (csrf_token == NULL ||
        calendar_database_get_settings(&settings) != 0 ||
        calendar_clock_now(settings.timezone, &snapshot) != 0 ||
        !parse_view_query(query, query_length, view, selected_date, snapshot.local_date)) {
        set_error("Terminansicht konnte nicht vorbereitet werden");
        return NULL;
    }

    snprintf(range_start, sizeof(range_start), "%s", selected_date);
    if (strcmp(view, "month") == 0) {
        if (!month_start_for_date(selected_date, range_start) ||
            !shift_month(range_start, 1, next_month) ||
            calendar_date_days_between(range_start, next_month, &day_count) != 0) {
            set_error("Monatsansicht konnte nicht berechnet werden");
            return NULL;
        }
    } else {
        day_count = strcmp(view, "day") == 0 ? 1 : 7;
        if (day_count == 7 &&
            (calendar_date_iso_weekday(selected_date, &weekday) != 0 ||
             calendar_date_add_days(selected_date, -(weekday - 1), range_start) != 0)) {
            set_error("Wochenbeginn konnte nicht berechnet werden");
            return NULL;
        }
    }

    if (notification_queue_get_counts(&queue_counts) != 0) {
        memset(&queue_counts, 0, sizeof(queue_counts));
    }

    page = _new_string();
    if (page == NULL) {
        set_error("Speicher für Terminansicht fehlt");
        return NULL;
    }

    str_cat_cstr(page,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<meta name=\"robots\" content=\"noindex,nofollow\">"
            "<title>Terminkalender | Styling 4 Dogs Admin</title>"
            "<link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
            "<header class=\"site-header\"><div class=\"container nav-wrap\">"
            "<a class=\"brand\" href=\"/admin/appointments\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Admin</span></a>"
            "<nav class=\"site-nav\" aria-label=\"Admin-Navigation\">"
            "<a href=\"/admin\">Übersicht</a>"
            "<a href=\"/\">Website öffnen</a>"
            "<a href=\"/admin/bookings\">Buchungsanfragen</a>"
            "<a href=\"/admin/gallery\">Fotos</a>"
            "<a href=\"/admin/appointments\" aria-current=\"page\">Termine</a>"
            "<a href=\"/admin/notifications\">E-Mail</a>"
            "<a href=\"/admin/calendar\">Einstellungen</a>"
            "</nav></div></header>"
            "<main class=\"page admin-appointments-page\"><section class=\"card admin-appointments-intro\">"
            "<p class=\"eyebrow\">Salonbetrieb</p><h1>Terminkalender</h1>"
            "<p>Offene Anfragen, bestätigte Termine und Sperrzeiten in einer gemeinsamen Ansicht.</p>"
            "<div class=\"appointment-view-switch\"><a class=\"button button-small ");
    str_cat_cstr(page, strcmp(view, "day") == 0 ? "" : "button-secondary");
    str_cat_cstr(page, "\" href=\"/admin/appointments?view=day&amp;date=");
    append_html_text(page, selected_date);
    str_cat_cstr(page, "\">Tag</a><a class=\"button button-small ");
    str_cat_cstr(page, strcmp(view, "week") == 0 ? "" : "button-secondary");
    str_cat_cstr(page, "\" href=\"/admin/appointments?view=week&amp;date=");
    append_html_text(page, selected_date);
    str_cat_cstr(page, "\">Woche</a><a class=\"button button-small ");
    str_cat_cstr(page, strcmp(view, "month") == 0 ? "" : "button-secondary");
    str_cat_cstr(page, "\" href=\"/admin/appointments?view=month&amp;date=");
    append_html_text(page, selected_date);
    str_cat_cstr(page, "\">Monat</a></div>");
    append_navigation(page, view, range_start, snapshot.local_date);
    if (strcmp(view, "month") == 0) {
        str_cat_cstr(page, "<p class=\"appointment-range-title\">");
        append_month_label(page, range_start);
        str_cat_cstr(page, "</p>");
    }

    str_cat_cstr(page,
            "<div class=\"appointment-legend\" aria-label=\"Legende\">"
            "<span class=\"appointment-legend-item appointment-legend-confirmed\">Bestätigter Termin</span>"
            "<span class=\"appointment-legend-item appointment-legend-pending\">Offene Terminanfrage</span>"
            "<span class=\"appointment-legend-item appointment-legend-closure\">Sperrzeit oder Urlaub</span>"
            "</div>");

    str_cat_cstr(page, "<div class=\"notification-summary\"><span>Versandbereit <strong>");
    {
        char count[32];
        snprintf(count, sizeof(count), "%zu", queue_counts.pending);
        append_html_text(page, count);
    }
    str_cat_cstr(page, "</strong></span><span>Fehlgeschlagen <strong>");
    {
        char count[32];
        snprintf(count, sizeof(count), "%zu", queue_counts.failed);
        append_html_text(page, count);
    }
    str_cat_cstr(page, "</strong></span></div></section>");

    if (strcmp(view, "month") == 0) {
        static const char *weekday_labels[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
        int first_weekday;

        if (calendar_date_iso_weekday(range_start, &first_weekday) != 0) {
            free_str(page);
            set_error("Monatsraster konnte nicht berechnet werden");
            return NULL;
        }

        str_cat_cstr(page, "<section class=\"appointment-month-scroll\"><div class=\"appointment-month\">");
        for (size_t index = 0; index < sizeof(weekday_labels) / sizeof(weekday_labels[0]); index++) {
            str_cat_cstr(page, "<div class=\"appointment-month-weekday\">");
            append_html_text(page, weekday_labels[index]);
            str_cat_cstr(page, "</div>");
        }
        for (int spacer = 1; spacer < first_weekday; spacer++) {
            str_cat_cstr(page, "<div class=\"appointment-month-spacer\" aria-hidden=\"true\"></div>");
        }

        for (int index = 0; index < day_count; index++) {
            char date[11];
            int day_number = 0;
            string *entries = _new_string();
            appointment_day_context context = {
                    .page = entries,
                    .csrf_token = csrf_token,
                    .date = date,
                    .booking_count = 0,
                    .closure_count = 0
            };

            if (entries == NULL || calendar_date_add_days(range_start, index, date) != 0 ||
                sscanf(date + 8, "%2d", &day_number) != 1) {
                free_str(entries);
                free_str(page);
                set_error("Kalendertag konnte nicht vorbereitet werden");
                return NULL;
            }

            if (calendar_database_for_each_closure_in_range(
                    date, date, append_closure_record, &context) < 0 ||
                booking_database_for_each_appointment(
                    date, date, append_appointment_record, &context) != 0) {
                free_str(entries);
                free_str(page);
                set_error("Termine oder Sperrzeiten konnten nicht geladen werden");
                return NULL;
            }

            if (context.booking_count == 0 && context.closure_count == 0) {
                str_cat_cstr(page, "<article class=\"appointment-month-day appointment-month-day-empty\"><div class=\"appointment-month-day-number\">");
                append_size_value(page, (size_t)day_number);
                str_cat_cstr(page, "</div><span>Keine Termine</span></article>");
                free_str(entries);
                continue;
            }

            str_cat_cstr(page, "<details class=\"appointment-month-day\"><summary><span class=\"appointment-month-day-number\">");
            append_size_value(page, (size_t)day_number);
            str_cat_cstr(page, "</span><strong>");
            if (context.booking_count == 0) {
                str_cat_cstr(page, "Keine Termine vorhanden");
            } else {
                append_size_value(page, context.booking_count);
                str_cat_cstr(page, context.booking_count == 1 ? " Termin vorhanden!" : " Termine vorhanden!");
            }
            str_cat_cstr(page, "</strong>");
            if (context.closure_count > 0) {
                str_cat_cstr(page, "<small>");
                append_size_value(page, context.closure_count);
                str_cat_cstr(page, context.closure_count == 1 ? " Sperrzeit" : " Sperrzeiten");
                str_cat_cstr(page, "</small>");
            }
            str_cat_cstr(page, "<span class=\"appointment-month-toggle\">Details anzeigen</span></summary><div class=\"appointment-month-expanded\"><h2>");
            append_display_date(page, date);
            str_cat_cstr(page, "</h2><div class=\"appointment-day-list\">");
            str_cat(page, entries->str, entries->len);
            str_cat_cstr(page, "</div></div></details>");
            free_str(entries);
        }

        str_cat_cstr(page, "</div></section>");
    } else {
        const bool collapse_multiple_week_appointments = strcmp(view, "week") == 0;

        str_cat_cstr(page, "<section class=\"appointment-days\">");
        for (int index = 0; index < day_count; index++) {
            char date[11];
            string *entries = _new_string();
            appointment_day_context context = {
                    .page = entries,
                    .csrf_token = csrf_token,
                    .date = date,
                    .booking_count = 0,
                    .closure_count = 0
            };

            if (entries == NULL || calendar_date_add_days(range_start, index, date) != 0) {
                free_str(entries);
                free_str(page);
                set_error("Kalendertag konnte nicht berechnet werden");
                return NULL;
            }

            if (calendar_database_for_each_closure_in_range(
                    date, date, append_closure_record, &context) < 0 ||
                booking_database_for_each_appointment(
                    date, date, append_appointment_record, &context) != 0) {
                free_str(entries);
                free_str(page);
                set_error("Termine oder Sperrzeiten konnten nicht geladen werden");
                return NULL;
            }

            if (collapse_multiple_week_appointments && context.booking_count > 1) {
                str_cat_cstr(page,
                        "<details class=\"card appointment-day appointment-week-day-collapsible\">"
                        "<summary class=\"appointment-week-day-summary\"><div><h2>");
                append_display_date(page, date);
                str_cat_cstr(page, "</h2>");
                if (context.closure_count > 0) {
                    str_cat_cstr(page, "<small class=\"appointment-week-day-closures\">");
                    append_size_value(page, context.closure_count);
                    str_cat_cstr(page, context.closure_count == 1 ? " Sperrzeit" : " Sperrzeiten");
                    str_cat_cstr(page, "</small>");
                }
                str_cat_cstr(page, "</div><strong class=\"appointment-week-day-count\">");
                append_size_value(page, context.booking_count);
                str_cat_cstr(page,
                        context.booking_count == 1
                                ? " Termin vorhanden!"
                                : " Termine vorhanden!");
                str_cat_cstr(page,
                        "</strong><span class=\"appointment-week-day-toggle\">Details anzeigen</span>"
                        "</summary><div class=\"appointment-week-day-expanded\">"
                        "<div class=\"appointment-day-list\">");
                str_cat(page, entries->str, entries->len);
                str_cat_cstr(page, "</div></div></details>");
            } else {
                str_cat_cstr(page, "<section class=\"card appointment-day\"><h2>");
                append_display_date(page, date);
                str_cat_cstr(page, "</h2><div class=\"appointment-day-list\">");
                if (context.booking_count == 0 && context.closure_count == 0) {
                    str_cat_cstr(page, "<p class=\"appointment-empty\">Keine Termine.</p>");
                } else {
                    str_cat(page, entries->str, entries->len);
                }
                str_cat_cstr(page, "</div></section>");
            }

            free_str(entries);
        }
        str_cat_cstr(page, "</section>");
    }

    str_cat_cstr(page,
            "</main><footer class=\"site-footer\"><div class=\"container footer-bottom\">"
            "<small>&copy; 2026 Styling 4 Dogs Admin.</small></div></footer></body></html>");
    return page;
}
