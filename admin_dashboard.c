#include "admin_dashboard.h"

#include "booking_database.h"
#include "calendar_database.h"
#include "calendar_time.h"
#include "notification_queue.h"

#include <stdio.h>
#include <string.h>

#define ADMIN_DASHBOARD_ERROR_SIZE 512

static char dashboard_error[ADMIN_DASHBOARD_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            dashboard_error,
            sizeof(dashboard_error),
            "%s",
            message == NULL ? "Unbekannter Dashboardfehler" : message);
}

const char *admin_dashboard_last_error(void)
{
    return dashboard_error[0] == '\0'
            ? "Unbekannter Dashboardfehler"
            : dashboard_error;
}

static void append_html_char(string *destination, char character)
{
    switch (character) {
        case '&': str_cat_cstr(destination, "&amp;"); break;
        case '<': str_cat_cstr(destination, "&lt;"); break;
        case '>': str_cat_cstr(destination, "&gt;"); break;
        case '"': str_cat_cstr(destination, "&quot;"); break;
        case '\'': str_cat_cstr(destination, "&#39;"); break;
        default: str_cat(destination, &character, 1); break;
    }
}

static void append_html(string *destination, const char *source)
{
    size_t index;

    if (destination == NULL || source == NULL) {
        return;
    }

    for (index = 0; source[index] != '\0'; index++) {
        append_html_char(destination, source[index]);
    }
}

static void append_size(string *destination, size_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%zu", value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat(destination, buffer, (size_t)written);
    }
}

static const char *decision_label(const char *status)
{
    if (status == NULL) return "Unbekannt";
    if (strcmp(status, "pending") == 0) return "Angefragt";
    if (strcmp(status, "confirmed") == 0) return "Bestätigt";
    if (strcmp(status, "rejected") == 0) return "Abgelehnt";
    if (strcmp(status, "cancelled") == 0) return "Vom Kunden abgesagt";
    if (strcmp(status, "expired") == 0) return "Abgelaufen";
    return status;
}

typedef struct dashboard_appointment_context {
    string *page;
    size_t count;
    size_t pending;
    size_t confirmed;
} dashboard_appointment_context;

static void append_today_appointment(
        const booking_record *record,
        void *opaque
)
{
    dashboard_appointment_context *context = opaque;
    char start[6] = "--:--";
    char end[6] = "--:--";

    if (record == NULL || context == NULL || context->page == NULL) {
        return;
    }

    calendar_time_format_hhmm(record->start_minute, start);
    calendar_time_format_hhmm(record->end_minute, end);

    context->count++;
    if (strcmp(record->decision_status, "pending") == 0) {
        context->pending++;
    } else if (strcmp(record->decision_status, "confirmed") == 0) {
        context->confirmed++;
    }

    str_cat_cstr(
            context->page,
            "<article class=\"dashboard-appointment\"><div class=\"dashboard-appointment-time\"><strong>");
    append_html(context->page, start);
    str_cat_cstr(context->page, "</strong><span>bis ");
    append_html(context->page, end);
    str_cat_cstr(context->page, " Uhr</span></div><div><h3>");
    append_html(
            context->page,
            record->dog_name == NULL || record->dog_name[0] == '\0'
                    ? "Hund ohne Namensangabe"
                    : record->dog_name);
    str_cat_cstr(context->page, "</h3><p>");
    append_html(context->page, record->name == NULL ? "" : record->name);
    str_cat_cstr(context->page, " · ");
    append_html(
            context->page,
            record->service_name_snapshot == NULL ||
            record->service_name_snapshot[0] == '\0'
                    ? record->service
                    : record->service_name_snapshot);
    str_cat_cstr(context->page, "</p></div><span class=\"dashboard-status\">");
    append_html(context->page, decision_label(record->decision_status));
    str_cat_cstr(context->page, "</span></article>");
}

string *admin_dashboard_build_page(void)
{
    booking_status_counts booking_counts;
    notification_queue_counts notification_counts;
    calendar_settings settings;
    calendar_clock_snapshot snapshot;
    dashboard_appointment_context appointment_context;
    char date_display[64];
    string *page;

    dashboard_error[0] = '\0';

    if (booking_database_get_status_counts(&booking_counts) != 0) {
        set_error(booking_database_last_error());
        return NULL;
    }
    if (notification_queue_get_counts(&notification_counts) != 0) {
        set_error(notification_queue_last_error());
        return NULL;
    }
    if (calendar_database_get_settings(&settings) != 0 ||
        calendar_clock_now(settings.timezone, &snapshot) != 0 ||
        calendar_date_format_de(
                snapshot.local_date,
                true,
                date_display,
                sizeof(date_display)) != 0) {
        set_error(calendar_database_last_error());
        return NULL;
    }

    page = _new_string();
    appointment_context.page = page;
    appointment_context.count = 0;
    appointment_context.pending = 0;
    appointment_context.confirmed = 0;

    str_cat_cstr(
            page,
            "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>Dashboard - Styling 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><header class=\"site-header\"><div class=\"container nav-wrap\"><a class=\"brand\" href=\"/admin\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Styling 4 Dogs Admin</span></a><nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/admin\" aria-current=\"page\">Übersicht</a><a href=\"/admin/bookings\">Buchungsanfragen</a><a href=\"/admin/appointments\">Termine</a><a href=\"/admin/calendar\">Einstellungen</a><a href=\"/admin/gallery\">Fotos</a><a href=\"/admin/notifications\">E-Mail</a></nav></div></header><main class=\"page admin-page admin-dashboard-page\"><section class=\"card admin-card dashboard-intro\"><p class=\"eyebrow\">Salonübersicht</p><h1>Guten Überblick behalten.</h1><p>Die wichtigsten Aufgaben, Termine und Hinweise auf einen Blick.</p></section><section class=\"dashboard-summary-grid\"><a class=\"dashboard-summary-card\" href=\"/admin/bookings\"><span>Buchungen insgesamt</span><strong>");
    append_size(page, booking_counts.total);
    str_cat_cstr(page, "</strong><small>Anfragen verwalten</small></a><a class=\"dashboard-summary-card\" href=\"/admin/bookings?status=neu\"><span>Neue Buchungen</span><strong>");
    append_size(page, booking_counts.new_count);
    str_cat_cstr(page, "</strong><small>Noch nicht bearbeitet</small></a><a class=\"dashboard-summary-card\" href=\"/admin/notifications\"><span>Fehlgeschlagene E-Mails</span><strong>");
    append_size(page, notification_counts.failed);
    str_cat_cstr(page, "</strong><small>Versand prüfen</small></a><a class=\"dashboard-summary-card\" href=\"/admin/notifications\"><span>Ausstehende E-Mails</span><strong>");
    append_size(page, notification_counts.pending);
    str_cat_cstr(page, "</strong><small>Warteschlange öffnen</small></a></section><section class=\"card admin-card dashboard-today\"><div class=\"dashboard-section-heading\"><div><p class=\"eyebrow\">Heute</p><h2>");
    append_html(page, date_display);
    str_cat_cstr(page, "</h2></div><a class=\"button button-small button-secondary\" href=\"/admin/appointments?view=day&amp;date=");
    append_html(page, snapshot.local_date);
    str_cat_cstr(page, "\">Kalender öffnen</a></div><div class=\"dashboard-appointment-list\">");

    if (booking_database_for_each_appointment(
            snapshot.local_date,
            snapshot.local_date,
            append_today_appointment,
            &appointment_context) != 0) {
        set_error(booking_database_last_error());
        free_str(page);
        return NULL;
    }

    if (appointment_context.count == 0) {
        str_cat_cstr(page, "<p class=\"dashboard-empty\">Heute stehen keine Termine oder offenen Terminanfragen an.</p>");
    }

    str_cat_cstr(page, "</div></section><section class=\"dashboard-quick-grid\"><a class=\"dashboard-quick-card\" href=\"/admin/bookings\"><strong>Anfragen bearbeiten</strong><span>Termine annehmen oder ablehnen</span></a><a class=\"dashboard-quick-card\" href=\"/admin/appointments\"><strong>Kalender öffnen</strong><span>Tages-, Wochen- und 30-Tage-Ansicht</span></a><a class=\"dashboard-quick-card\" href=\"/admin/gallery\"><strong>Fotos verwalten</strong><span>Galerie ergänzen und sortieren</span></a><a class=\"dashboard-quick-card\" href=\"/admin/notifications\"><strong>E-Mail prüfen</strong><span>Versand und Vorlagen verwalten</span></a></section></main><footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styling 4 Dogs Admin.</small></div></footer></body></html>");

    return page;
}
