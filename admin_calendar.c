#include "admin_calendar.h"

#include "calendar_database.h"
#include "calendar_time.h"
#include "form_urlencoded.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN_CALENDAR_ERROR_SIZE 512
#define ADMIN_OPENING_PERIODS_PER_DAY 4
#define ADMIN_MAX_SERVICES 128

static char admin_calendar_error[ADMIN_CALENDAR_ERROR_SIZE];

static void set_error(const char *message)
{
    snprintf(
            admin_calendar_error,
            sizeof(admin_calendar_error),
            "%s",
            message == NULL ? "Unbekannter Admin-Kalenderfehler" : message);
}

static void set_database_error(const char *context)
{
    snprintf(
            admin_calendar_error,
            sizeof(admin_calendar_error),
            "%s: %s",
            context == NULL ? "Kalender-Datenbankfehler" : context,
            calendar_database_last_error());
}

const char *admin_calendar_last_error(void)
{
    return admin_calendar_error[0] == '\0'
            ? "Unbekannter Admin-Kalenderfehler"
            : admin_calendar_error;
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

static void append_integer(string *destination, int value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%d", value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat_cstr(destination, buffer);
    }
}

static void append_int64(string *destination, int64_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%" PRId64, value);

    if (written > 0 && (size_t)written < sizeof(buffer)) {
        str_cat_cstr(destination, buffer);
    }
}

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

static bool is_single_line(const char *text, bool allow_empty)
{
    if (text == NULL || (!allow_empty && text[0] == '\0')) {
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

static bool get_required_field(
        const string *request,
        const char *name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(request, name, out, out_size);

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_single_line(out, false);
}

static bool get_optional_field(
        const string *request,
        const char *name,
        char *out,
        size_t out_size
)
{
    form_value_result result = form_urlencoded_get(request, name, out, out_size);

    if (result == FORM_VALUE_NOT_FOUND) {
        out[0] = '\0';
        return true;
    }

    if (result != FORM_VALUE_OK) {
        return false;
    }

    trim_ascii_whitespace(out);
    return is_single_line(out, true);
}

static bool checkbox_is_checked(const string *request, const char *name)
{
    char value[16];
    form_value_result result = form_urlencoded_get(request, name, value, sizeof(value));

    return result == FORM_VALUE_OK && strcmp(value, "1") == 0;
}

static bool parse_integer_text(
        const char *text,
        int minimum,
        int maximum,
        int *out_value
)
{
    long value;
    char *end = NULL;

    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }

    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < minimum || value > maximum) {
        return false;
    }

    *out_value = (int)value;
    return true;
}

static bool parse_positive_int64_text(const char *text, int64_t *out_value)
{
    uint64_t value = 0;

    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return false;
    }

    for (size_t index = 0; text[index] != '\0'; index++) {
        unsigned int digit;

        if (text[index] < '0' || text[index] > '9') {
            return false;
        }

        digit = (unsigned int)(text[index] - '0');
        if (value > ((uint64_t)INT64_MAX - digit) / 10U) {
            return false;
        }

        value = value * 10U + digit;
    }

    if (value == 0) {
        return false;
    }

    *out_value = (int64_t)value;
    return true;
}

static const char *weekday_name(int weekday)
{
    static const char *const names[] = {
            "", "Montag", "Dienstag", "Mittwoch", "Donnerstag",
            "Freitag", "Samstag", "Sonntag"
    };

    return weekday >= 1 && weekday <= 7 ? names[weekday] : "Unbekannt";
}

static const char *notice_text(const char *notice_code)
{
    if (notice_code == NULL) {
        return NULL;
    }
    if (strcmp(notice_code, "settings") == 0) {
        return "Die Buchungsregeln wurden gespeichert.";
    }
    if (strcmp(notice_code, "all") == 0) {
        return "Alle Kalendereinstellungen wurden gemeinsam gespeichert.";
    }
    if (strcmp(notice_code, "hours") == 0) {
        return "Die Öffnungszeiten wurden gespeichert.";
    }
    if (strcmp(notice_code, "service") == 0) {
        return "Die Leistung wurde gespeichert.";
    }
    if (strcmp(notice_code, "service-added") == 0) {
        return "Die neue Leistung wurde hinzugefügt.";
    }
    if (strcmp(notice_code, "service-deleted") == 0) {
        return "Die Leistung wurde gelöscht oder – falls sie bereits verwendet wurde – archiviert.";
    }
    if (strcmp(notice_code, "closure-added") == 0) {
        return "Die Sperrzeit wurde eingetragen.";
    }
    if (strcmp(notice_code, "closure-deleted") == 0) {
        return "Die Sperrzeit wurde gelöscht.";
    }

    return NULL;
}

static void append_settings_section(
        string *page,
        const calendar_settings *settings
)
{
    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"buchungsregeln\">\n"
            "            <p class=\"eyebrow\">Kalender</p>\n"
            "            <h2>Buchungsregeln</h2>\n"
            "            <p>Alle Felder in den Bereichen Buchungsregeln, Öffnungszeiten und vorhandene Leistungen werden mit dem gemeinsamen Speicherknopf übernommen.</p>\n"
            "            <div class=\"admin-calendar-form\">\n"
            "                <div class=\"admin-calendar-fields\">\n"
            "                    <label>Frühester buchbarer Termin"
            "<input name=\"min_notice_hours\" type=\"number\" min=\"0\" max=\"8760\" required value=\"");
    append_integer(page, settings->min_notice_minutes / 60);
    str_cat_cstr(page,
            "\"><span class=\"admin-field-help\">Stunden zwischen der Buchung und dem Termin. Bei 24 kann frühestens ab morgen gebucht werden.</span></label>\n"
            "                    <label>Buchbar bis Tage im Voraus"
            "<input name=\"booking_horizon_days\" type=\"number\" min=\"1\" max=\"730\" required value=\"");
    append_integer(page, settings->booking_horizon_days);
    str_cat_cstr(page,
            "\"><span class=\"admin-field-help\">Wie weit der öffentliche Kalender in die Zukunft reicht.</span></label>\n"
            "                    <label>Zeitraster"
            "<select name=\"slot_interval_minutes\" required>\n");

    {
        static const int intervals[] = {5, 10, 15, 20, 30, 60};

        for (size_t index = 0; index < sizeof(intervals) / sizeof(intervals[0]); index++) {
            int interval = intervals[index];

            str_cat_cstr(page, "                        <option value=\"");
            append_integer(page, interval);
            str_cat_cstr(page, "\"");
            if (settings->slot_interval_minutes == interval) {
                str_cat_cstr(page, " selected");
            }
            str_cat_cstr(page, ">");
            append_integer(page, interval);
            str_cat_cstr(page, " Minuten</option>\n");
        }
    }

    str_cat_cstr(page,
            "                    </select><span class=\"admin-field-help\">Abstand zwischen möglichen Startzeiten.</span></label>\n"
            "                    <label>Freihaltezeit für offene Anfragen"
            "<input name=\"pending_hold_hours\" type=\"number\" min=\"1\" max=\"168\" required value=\"");
    append_integer(page, settings->pending_hold_minutes / 60);
    str_cat_cstr(page,
            "\"><span class=\"admin-field-help\">So viele Stunden bleibt ein angefragter Termin für andere Kunden blockiert, wenn er noch nicht angenommen wurde.</span></label>\n"
            "                    <label>Erinnerung vor dem Termin"
            "<input name=\"reminder_lead_hours\" type=\"number\" min=\"1\" max=\"168\" required value=\"");
    append_integer(page, settings->reminder_lead_minutes / 60);
    str_cat_cstr(page,
            "\"><span class=\"admin-field-help\">Wie viele Stunden vorher die Erinnerungsmail eingeplant wird.</span></label>\n"
            "                </div>\n"
            "                <label class=\"admin-checkbox admin-confirmation-setting\">"
            "<input type=\"checkbox\" name=\"auto_confirm_bookings\" value=\"1\"");
    if (settings->auto_confirm_bookings) {
        str_cat_cstr(page, " checked");
    }
    str_cat_cstr(page,
            "> Neue freie Termine automatisch verbindlich bestätigen</label>\n"
            "                <label class=\"admin-checkbox\"><input type=\"checkbox\" "
            "name=\"email_notifications_enabled\" value=\"1\"");
    if (settings->email_notifications_enabled) {
        str_cat_cstr(page, " checked");
    }
    str_cat_cstr(page,
            "> E-Mail-Bestätigungen und Absagen in die Versandwarteschlange stellen</label>\n"
            "                <label class=\"admin-checkbox\"><input type=\"checkbox\" "
            "name=\"reminder_enabled\" value=\"1\"");
    if (settings->reminder_enabled) {
        str_cat_cstr(page, " checked");
    }
    str_cat_cstr(page,
            "> Automatische E-Mail-Erinnerung vor bestätigten Terminen</label>\n"
            "                <p class=\"admin-calendar-hint\">Zeitzone: <strong>");
    append_html_text(page, settings->timezone);
    str_cat_cstr(page,
            "</strong>. Die Kapazität bleibt vorerst auf einen Hund gleichzeitig begrenzt.</p>\n"
            "            </div>\n"
            "        </section>\n");
}

static void append_time_input(
        string *page,
        const char *field_name,
        int minute,
        bool has_value
)
{
    char time_text[6] = "";

    if (has_value && calendar_time_format_hhmm(minute, time_text) != 0) {
        has_value = false;
    }

    str_cat_cstr(page, "<input type=\"time\" step=\"300\" name=\"");
    append_html_text(page, field_name);
    str_cat_cstr(page, "\" value=\"");
    if (has_value) {
        append_html_text(page, time_text);
    }
    str_cat_cstr(page, "\">");
}

static void append_opening_hours_fields(
        string *page,
        int weekday
)
{
    calendar_time_range ranges[CALENDAR_MAX_DAY_PERIODS];
    size_t count = 0;

    if (calendar_database_get_opening_periods(
            weekday,
            ranges,
            sizeof(ranges) / sizeof(ranges[0]),
            &count) != 0) {
        str_cat_cstr(page, "<p>Die Öffnungszeiten konnten nicht geladen werden.</p>");
        return;
    }

    str_cat_cstr(page,
            "                <div class=\"opening-day-form\">\n"
            "                    <h3>");
    append_html_text(page, weekday_name(weekday));
    str_cat_cstr(page, "</h3>\n                    <div class=\"opening-period-grid\">\n");

    for (int index = 0; index < ADMIN_OPENING_PERIODS_PER_DAY; index++) {
        char start_name[48];
        char end_name[48];
        bool has_value = (size_t)index < count;

        snprintf(start_name, sizeof(start_name), "day_%d_start_%d", weekday, index + 1);
        snprintf(end_name, sizeof(end_name), "day_%d_end_%d", weekday, index + 1);

        str_cat_cstr(page, "                        <div class=\"opening-period\"><span>Zeitraum ");
        append_integer(page, index + 1);
        str_cat_cstr(page, "</span><label>Von ");
        append_time_input(
                page,
                start_name,
                has_value ? ranges[index].start_minute : 0,
                has_value);
        str_cat_cstr(page, "</label><label>Bis ");
        append_time_input(
                page,
                end_name,
                has_value ? ranges[index].end_minute : 0,
                has_value);
        str_cat_cstr(page, "</label></div>\n");
    }

    if (count > ADMIN_OPENING_PERIODS_PER_DAY) {
        str_cat_cstr(page,
                "                        <p class=\"admin-warning\">Für diesen Tag sind mehr Zeiträume gespeichert, als diese Oberfläche bearbeiten kann. Bitte zuerst technisch bereinigen.</p>\n");
    }

    str_cat_cstr(page,
            "                    </div>\n"
            "                    <p class=\"admin-calendar-hint\">Leere Zeiträume werden ignoriert. Sind alle Felder leer, ist der Tag geschlossen.</p>\n"
            "                </div>\n");
}

static void append_opening_hours_section(string *page)
{
    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"oeffnungszeiten\">\n"
            "            <p class=\"eyebrow\">Wochenplan</p>\n"
            "            <h2>Regelmäßige Öffnungszeiten</h2>\n"
            "            <p>Pro Tag sind bis zu vier getrennte Zeiträume möglich, zum Beispiel vormittags und nachmittags.</p>\n"
            "            <div class=\"opening-days\">\n");

    for (int weekday = 1; weekday <= 7; weekday++) {
        append_opening_hours_fields(page, weekday);
    }

    str_cat_cstr(page, "            </div>\n        </section>\n");
}

typedef struct service_page_context {
    string *page;
    size_t count;
} service_page_context;

static int append_service_fields(
        const calendar_service *service,
        void *context_value
)
{
    service_page_context *context = context_value;
    string *page;
    char field_name[256];

    if (service == NULL || context == NULL || context->page == NULL) {
        return 1;
    }

    page = context->page;
    str_cat_cstr(page,
            "                <article class=\"service-admin-card\">\n"
            "                    <div class=\"service-admin-form\">\n"
            "                    <div class=\"service-admin-heading\"><div><h3>");
    append_html_text(page, service->name);
    str_cat_cstr(page, "</h3><code>");
    append_html_text(page, service->code);
    str_cat_cstr(page, "</code></div><label class=\"admin-checkbox\"><input type=\"checkbox\" name=\"");
    snprintf(field_name, sizeof(field_name), "service_%s_active", service->code);
    append_html_text(page, field_name);
    str_cat_cstr(page, "\" value=\"1\"");
    if (service->active) {
        str_cat_cstr(page, " checked");
    }
    str_cat_cstr(page,
            "> Online buchbar</label></div>\n"
            "                    <div class=\"admin-calendar-fields service-admin-fields\">\n"
            "                        <label>Anzeigename<input name=\"");
    snprintf(field_name, sizeof(field_name), "service_%s_name", service->code);
    append_html_text(page, field_name);
    str_cat_cstr(page, "\" maxlength=\"127\" required value=\"");
    append_html_text(page, service->name);
    str_cat_cstr(page, "\"></label>\n                        <label>Dauer in Minuten<input name=\"");
    snprintf(field_name, sizeof(field_name), "service_%s_duration", service->code);
    append_html_text(page, field_name);
    str_cat_cstr(page, "\" type=\"number\" min=\"15\" max=\"720\" step=\"5\" required value=\"");
    append_integer(page, service->duration_minutes);
    str_cat_cstr(page, "\"></label>\n                        <label>Puffer danach in Minuten<input name=\"");
    snprintf(field_name, sizeof(field_name), "service_%s_buffer", service->code);
    append_html_text(page, field_name);
    str_cat_cstr(page, "\" type=\"number\" min=\"0\" max=\"240\" step=\"5\" required value=\"");
    append_integer(page, service->buffer_minutes);
    str_cat_cstr(page,
            "\"></label>\n"
            "                    </div>\n"
            "                    </div>\n"
            "                    <div class=\"service-delete-form\">\n"
            "                    <button class=\"button button-small button-danger\" type=\"submit\" "
            "formaction=\"/admin/calendar/service/delete\" formmethod=\"post\" formnovalidate name=\"code\" value=\"");
    append_html_text(page, service->code);
    str_cat_cstr(page,
            "\" onclick=\"return confirm('Leistung wirklich löschen oder archivieren?')\">Leistung löschen</button>\n"
            "                    <p class=\"admin-calendar-hint\">Bereits verwendete Leistungen werden nur archiviert und bleiben in alten Buchungen erhalten.</p>\n"
            "                    </div>\n"
            "                </article>\n");

    context->count++;
    return 0;
}

static bool append_services_edit_section(string *page)
{
    service_page_context context = {.page = page, .count = 0};
    int result;

    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"leistungen\">\n"
            "            <p class=\"eyebrow\">Terminarten</p>\n"
            "            <h2>Vorhandene Leistungen und Dauer</h2>\n"
            "            <p>Änderungen an vorhandenen Leistungen werden zusammen mit den übrigen Kalendereinstellungen gespeichert.</p>\n"
            "            <div class=\"service-admin-list\">\n");

    result = calendar_database_for_each_service(append_service_fields, &context);
    if (result != 0) {
        str_cat_cstr(page, "                <p>Die Leistungen konnten nicht geladen werden.</p>\n");
    } else if (context.count == 0) {
        str_cat_cstr(page, "                <p>Es sind noch keine Leistungen eingerichtet.</p>\n");
    }

    str_cat_cstr(page, "            </div>\n        </section>\n");
    return result == 0;
}

static void append_service_add_section(string *page, const char *csrf_token)
{
    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"leistung-hinzufuegen\">\n"
            "            <p class=\"eyebrow\">Neue Terminart</p><h2>Leistung hinzufügen</h2>\n"
            "            <form class=\"admin-calendar-form service-add-form\" method=\"post\" action=\"/admin/calendar/service/add\">\n"
            "                <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page,
            "\">\n"
            "                <div class=\"admin-calendar-fields\">\n"
            "                    <label>Technischer Schlüssel<input name=\"code\" maxlength=\"63\" pattern=\"[a-z0-9_]+\" placeholder=\"zum_beispiel_welpenpflege\" required></label>\n"
            "                    <label>Anzeigename<input name=\"name\" maxlength=\"127\" required></label>\n"
            "                    <label>Dauer in Minuten<input name=\"duration_minutes\" type=\"number\" min=\"15\" max=\"720\" step=\"5\" value=\"60\" required></label>\n"
            "                    <label>Puffer danach<input name=\"buffer_minutes\" type=\"number\" min=\"0\" max=\"240\" step=\"5\" value=\"15\" required></label>\n"
            "                </div>\n"
            "                <label class=\"admin-checkbox\"><input type=\"checkbox\" name=\"active\" value=\"1\" checked> Sofort online buchbar</label>\n"
            "                <button class=\"button\" type=\"submit\">Leistung hinzufügen</button>\n"
            "            </form>\n"
            "        </section>\n");
}

typedef struct closure_page_context {
    string *page;
    const char *csrf_token;
    size_t count;
} closure_page_context;

static int append_closure_card(
        const calendar_closure *closure,
        void *context_value
)
{
    closure_page_context *context = context_value;
    string *page;
    bool all_day;
    char start_text[6] = "";
    char end_text[6] = "";
    char start_date_text[48] = "";
    char end_date_text[48] = "";

    if (closure == NULL || context == NULL || context->page == NULL ||
        context->csrf_token == NULL) {
        return 1;
    }

    page = context->page;
    all_day = closure->start_minute == 0 && closure->end_minute == 1440;

    if (calendar_date_format_de(
            closure->start_date,
            true,
            start_date_text,
            sizeof(start_date_text)) != 0 ||
        calendar_date_format_de(
            closure->end_date,
            true,
            end_date_text,
            sizeof(end_date_text)) != 0) {
        return 1;
    }

    if (!all_day &&
        (calendar_time_format_hhmm(closure->start_minute, start_text) != 0 ||
         calendar_time_format_hhmm(closure->end_minute, end_text) != 0)) {
        return 1;
    }

    str_cat_cstr(page,
            "                <article class=\"closure-card\"><div><h3>");
    if (closure->label[0] == '\0') {
        str_cat_cstr(page, "Gesperrter Zeitraum");
    } else {
        append_html_text(page, closure->label);
    }
    str_cat_cstr(page, "</h3><p>");
    append_html_text(page, start_date_text);
    if (strcmp(closure->start_date, closure->end_date) != 0) {
        str_cat_cstr(page, " bis ");
        append_html_text(page, end_date_text);
    }
    str_cat_cstr(page, " · ");
    if (all_day) {
        str_cat_cstr(page, "ganztägig");
    } else {
        append_html_text(page, start_text);
        str_cat_cstr(page, "–");
        append_html_text(page, end_text);
        str_cat_cstr(page, " Uhr");
    }
    str_cat_cstr(page,
            "</p></div><form method=\"post\" action=\"/admin/calendar/closure/delete\">"
            "<input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, context->csrf_token);
    str_cat_cstr(page, "\"><input type=\"hidden\" name=\"closure_id\" value=\"");
    append_int64(page, closure->id);
    str_cat_cstr(page,
            "\"><button class=\"button button-small button-danger\" type=\"submit\">Löschen</button>"
            "</form></article>\n");

    context->count++;
    return 0;
}

static bool append_closures_section(string *page, const char *csrf_token)
{
    closure_page_context context;
    int result;

    context.page = page;
    context.csrf_token = csrf_token;
    context.count = 0;

    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"sperrzeiten\">\n"
            "            <p class=\"eyebrow\">Ausnahmen</p>\n"
            "            <h2>Urlaub und Sperrzeiten</h2>\n"
            "            <p>Ganztägige Zeiträume eignen sich für Urlaub. Einzelne Stunden können für private Termine oder andere Pausen gesperrt werden.</p>\n"
            "            <form class=\"admin-calendar-form closure-add-form\" method=\"post\" action=\"/admin/calendar/closure/add\">\n"
            "                <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page,
            "\">\n"
            "                <div class=\"admin-calendar-fields\">\n"
            "                    <label>Von Datum<input type=\"date\" name=\"start_date\" required></label>\n"
            "                    <label>Bis Datum<input type=\"date\" name=\"end_date\" required></label>\n"
            "                    <label>Von Uhrzeit<input type=\"time\" step=\"300\" name=\"start_time\"></label>\n"
            "                    <label>Bis Uhrzeit<input type=\"time\" step=\"300\" name=\"end_time\"></label>\n"
            "                    <label class=\"admin-calendar-wide\">Bezeichnung<input name=\"label\" maxlength=\"255\" placeholder=\"Zum Beispiel Urlaub\"></label>\n"
            "                </div>\n"
            "                <label class=\"admin-checkbox\"><input type=\"checkbox\" name=\"all_day\" value=\"1\" checked> Ganztägig sperren</label>\n"
            "                <p class=\"admin-calendar-hint\">Bei mehrtägigen Zeiträumen muss „ganztägig“ aktiviert sein. Uhrzeiten werden dann ignoriert.</p>\n"
            "                <button class=\"button\" type=\"submit\">Sperrzeit eintragen</button>\n"
            "            </form>\n"
            "            <div class=\"closure-list\">\n");

    result = calendar_database_for_each_closure(append_closure_card, &context);
    if (result != 0) {
        str_cat_cstr(page, "                <p>Die Sperrzeiten konnten nicht geladen werden.</p>\n");
    } else if (context.count == 0) {
        str_cat_cstr(page, "                <p>Es sind noch keine Urlaubs- oder Sperrzeiten eingetragen.</p>\n");
    }

    str_cat_cstr(page, "            </div>\n        </section>\n");
    return result == 0;
}

string *admin_calendar_build_page(
        const char *csrf_token,
        const char *notice_code
)
{
    calendar_settings settings;
    const char *notice;
    string *page;

    if (csrf_token == NULL || csrf_token[0] == '\0' ||
        calendar_database_get_settings(&settings) != 0) {
        set_database_error("Admin-Kalender konnte nicht geladen werden");
        return NULL;
    }

    page = _new_string();
    if (page == NULL) {
        set_error("Speicher für Admin-Kalender konnte nicht reserviert werden");
        return NULL;
    }

    notice = notice_text(notice_code);
    str_cat_cstr(page,
            "<!doctype html>\n<html lang=\"de\">\n<head>\n"
            "    <meta charset=\"utf-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <meta name=\"robots\" content=\"noindex,nofollow\">\n"
            "    <title>Kalender verwalten - Styling 4 Dogs</title>\n"
            "    <link rel=\"stylesheet\" href=\"/style.css\">\n"
            "    <script src=\"/admin-calendar.js\" defer></script>\n"
            "</head>\n<body>\n"
            "    <header class=\"site-header\"><div class=\"container nav-wrap\">\n"
            "        <a class=\"brand\" href=\"/\"><span class=\"brand-mark brand-mark-logo\"><img src=\"/logo.jpg\" alt=\"\"></span><span>Styling 4 Dogs</span></a>\n"
            "        <nav class=\"site-nav\" aria-label=\"Admin-Navigation\">"
            "<a href=\"/admin\">Übersicht</a><a href=\"/\">Website öffnen</a>"
            "<a href=\"/admin/bookings\">Buchungsanfragen</a>"
            "<a href=\"/admin/appointments\">Termine</a>"
            "<a href=\"/admin/calendar\" aria-current=\"page\">Einstellungen</a><a href=\"/admin/gallery\">Fotos</a><a href=\"/admin/notifications\">E-Mail</a></nav>\n"
            "    </div></header>\n"
            "    <main class=\"page admin-page admin-calendar-page\">\n"
            "        <section class=\"card admin-card admin-calendar-intro\">\n"
            "            <p class=\"eyebrow\">Admin</p><h1>Kalender verwalten</h1>\n"
            "            <p>Hier steuerst du die öffentlich angebotenen Termine. Änderungen wirken sofort auf den Kundenkalender.</p>\n");

    if (notice != NULL) {
        str_cat_cstr(page, "            <p class=\"admin-success\" role=\"status\">");
        append_html_text(page, notice);
        str_cat_cstr(page, "</p>\n");
    }

    str_cat_cstr(page,
            "            <nav class=\"admin-calendar-jumpnav\" aria-label=\"Kalenderbereiche\">"
            "<a href=\"#buchungsregeln\">Buchungsregeln</a>"
            "<a href=\"#oeffnungszeiten\">Öffnungszeiten</a>"
            "<a href=\"#leistungen\">Leistungen</a>"
            "<a href=\"#buchungsschutz\">Buchungsschutz</a>"
            "<a href=\"#sperrzeiten\">Urlaub und Sperrzeiten</a></nav>\n"
            "        </section>\n");

    str_cat_cstr(page,
            "        <form id=\"calendar-settings-form\" class=\"admin-calendar-master-form\" "
            "method=\"post\" action=\"/admin/calendar/save-all\">\n"
            "            <input type=\"hidden\" name=\"csrf_token\" value=\"");
    append_html_text(page, csrf_token);
    str_cat_cstr(page, "\">\n");

    append_settings_section(page, &settings);
    append_opening_hours_section(page);
    (void)append_services_edit_section(page);
    str_cat_cstr(page,
            "            <section class=\"card admin-calendar-section admin-save-section\" id=\"speichern\">"
            "<div><p class=\"eyebrow\">Abschluss</p><h2>Änderungen übernehmen</h2>"
            "<p>Dieser Knopf speichert Buchungsregeln, Öffnungszeiten und alle Änderungen an vorhandenen Leistungen in einem Schritt.</p></div>"
            "<button id=\"calendar-save-bottom\" class=\"button\" type=\"submit\">Alle Einstellungen speichern</button>"
            "</section>\n"
            "        </form>\n"
            "        <div id=\"calendar-save-floating\" class=\"admin-save-floating\">"
            "<span id=\"calendar-unsaved-label\">Noch keine ungespeicherten Änderungen</span>"
            "<button class=\"button\" type=\"submit\" form=\"calendar-settings-form\">Alle speichern</button>"
            "</div>\n");

    append_service_add_section(page, csrf_token);
    str_cat_cstr(page,
            "        <section class=\"card admin-calendar-section\" id=\"buchungsschutz\">"
            "<p class=\"eyebrow\">Sicherheit</p><h2>Schutz vor Spam und Quatschbuchungen</h2>"
            "<div class=\"booking-protection-grid\">"
            "<div><strong>IP-Limit</strong><span>Maximal fünf Buchungsversuche pro Client-IP innerhalb von zehn Minuten.</span></div>"
            "<div><strong>Kontakt-Limit</strong><span>Maximal drei Anfragen pro E-Mail-Adresse oder Telefonnummer innerhalb von 24 Stunden.</span></div>"
            "<div><strong>Unsichtbare Bot-Falle</strong><span>Automatisierte Formulare, die versteckte Felder ausfüllen, werden nicht gespeichert.</span></div>"
            "<div><strong>Doppelbuchungsschutz</strong><span>Der Termin wird unmittelbar vor dem Speichern noch einmal transaktionssicher geprüft.</span></div>"
            "</div><p class=\"admin-calendar-hint\">Ein externes CAPTCHA ist zunächst nicht nötig und würde zusätzliche Datenschutz- und Abhängigkeitsfragen verursachen. Bei echtem Missbrauch kann später eine datenschutzfreundliche Challenge ergänzt werden.</p>"
            "</section>\n");
    (void)append_closures_section(page, csrf_token);

    str_cat_cstr(page,
            "    </main>\n"
            "    <footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styling 4 Dogs Admin.</small></div></footer>\n"
            "</body>\n</html>\n");

    return page;
}

static bool parse_settings(
        const string *request,
        calendar_settings *settings
)
{
    char min_notice_hours_text[32];
    char horizon_text[32];
    char interval_text[32];
    char hold_hours_text[32];
    char reminder_lead_hours_text[32];
    int min_notice_hours;
    int booking_horizon_days;
    int slot_interval_minutes;
    int hold_hours;
    int reminder_lead_hours;

    if (request == NULL || settings == NULL ||
        !get_required_field(request, "min_notice_hours", min_notice_hours_text, sizeof(min_notice_hours_text)) ||
        !get_required_field(request, "booking_horizon_days", horizon_text, sizeof(horizon_text)) ||
        !get_required_field(request, "slot_interval_minutes", interval_text, sizeof(interval_text)) ||
        !get_required_field(request, "pending_hold_hours", hold_hours_text, sizeof(hold_hours_text)) ||
        !get_required_field(request, "reminder_lead_hours", reminder_lead_hours_text, sizeof(reminder_lead_hours_text)) ||
        !parse_integer_text(min_notice_hours_text, 0, 8760, &min_notice_hours) ||
        !parse_integer_text(horizon_text, 1, 730, &booking_horizon_days) ||
        !parse_integer_text(interval_text, 5, 60, &slot_interval_minutes) ||
        !parse_integer_text(hold_hours_text, 1, 168, &hold_hours) ||
        !parse_integer_text(reminder_lead_hours_text, 1, 168, &reminder_lead_hours)) {
        return false;
    }

    if (!(slot_interval_minutes == 5 ||
          slot_interval_minutes == 10 ||
          slot_interval_minutes == 15 ||
          slot_interval_minutes == 20 ||
          slot_interval_minutes == 30 ||
          slot_interval_minutes == 60)) {
        return false;
    }

    if (calendar_database_get_settings(settings) != 0) {
        return false;
    }

    settings->min_notice_minutes = min_notice_hours * 60;
    settings->booking_horizon_days = booking_horizon_days;
    settings->slot_interval_minutes = slot_interval_minutes;
    settings->pending_hold_minutes = hold_hours * 60;
    settings->capacity = 1;
    settings->auto_confirm_bookings = checkbox_is_checked(request, "auto_confirm_bookings");
    settings->email_notifications_enabled = checkbox_is_checked(request, "email_notifications_enabled");
    settings->reminder_enabled = checkbox_is_checked(request, "reminder_enabled");
    settings->reminder_lead_minutes = reminder_lead_hours * 60;

    return true;
}

admin_calendar_result admin_calendar_update_settings(const string *request)
{
    calendar_settings settings;

    if (!parse_settings(request, &settings)) {
        set_error("Ungültige Buchungsregeln");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    if (calendar_database_update_settings(&settings) != 0) {
        set_database_error("Buchungsregeln konnten nicht gespeichert werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

static int compare_ranges(const void *left_value, const void *right_value)
{
    const calendar_time_range *left = left_value;
    const calendar_time_range *right = right_value;

    if (left->start_minute < right->start_minute) {
        return -1;
    }
    if (left->start_minute > right->start_minute) {
        return 1;
    }
    if (left->end_minute < right->end_minute) {
        return -1;
    }
    if (left->end_minute > right->end_minute) {
        return 1;
    }
    return 0;
}

typedef struct admin_opening_update {
    calendar_time_range ranges[ADMIN_OPENING_PERIODS_PER_DAY];
    size_t count;
} admin_opening_update;

typedef struct admin_service_update_collection {
    const string *request;
    calendar_service services[ADMIN_MAX_SERVICES];
    size_t count;
    bool invalid;
} admin_service_update_collection;

static bool parse_opening_day_for_save_all(
        const string *request,
        int weekday,
        admin_opening_update *update
)
{
    if (request == NULL || weekday < 1 || weekday > 7 || update == NULL) {
        return false;
    }

    memset(update, 0, sizeof(*update));

    for (int index = 0; index < ADMIN_OPENING_PERIODS_PER_DAY; index++) {
        char start_name[48];
        char end_name[48];
        char start_text[16];
        char end_text[16];
        int start_minute;
        int end_minute;

        snprintf(start_name, sizeof(start_name), "day_%d_start_%d", weekday, index + 1);
        snprintf(end_name, sizeof(end_name), "day_%d_end_%d", weekday, index + 1);

        if (!get_optional_field(request, start_name, start_text, sizeof(start_text)) ||
            !get_optional_field(request, end_name, end_text, sizeof(end_text))) {
            return false;
        }

        if (start_text[0] == '\0' && end_text[0] == '\0') {
            continue;
        }

        if (start_text[0] == '\0' || end_text[0] == '\0' ||
            calendar_time_parse_hhmm(start_text, &start_minute) != 0 ||
            calendar_time_parse_hhmm(end_text, &end_minute) != 0 ||
            start_minute >= end_minute) {
            return false;
        }

        update->ranges[update->count].start_minute = start_minute;
        update->ranges[update->count].end_minute = end_minute;
        update->count++;
    }

    qsort(update->ranges, update->count, sizeof(update->ranges[0]), compare_ranges);
    for (size_t index = 1; index < update->count; index++) {
        if (update->ranges[index - 1].end_minute >
            update->ranges[index].start_minute) {
            return false;
        }
    }

    return true;
}

static int collect_service_update(
        const calendar_service *current,
        void *context_value
)
{
    admin_service_update_collection *context = context_value;
    calendar_service *updated;
    char field_name[256];
    char duration_text[32];
    char buffer_text[32];

    if (current == NULL || context == NULL || context->request == NULL ||
        context->count >= ADMIN_MAX_SERVICES) {
        if (context != NULL) {
            context->invalid = true;
        }
        return 1;
    }

    updated = &context->services[context->count];
    *updated = *current;

    snprintf(field_name, sizeof(field_name), "service_%s_name", current->code);
    if (!get_required_field(
            context->request,
            field_name,
            updated->name,
            sizeof(updated->name))) {
        context->invalid = true;
        return 1;
    }

    snprintf(field_name, sizeof(field_name), "service_%s_duration", current->code);
    if (!get_required_field(
            context->request,
            field_name,
            duration_text,
            sizeof(duration_text)) ||
        !parse_integer_text(duration_text, 15, 720, &updated->duration_minutes)) {
        context->invalid = true;
        return 1;
    }

    snprintf(field_name, sizeof(field_name), "service_%s_buffer", current->code);
    if (!get_required_field(
            context->request,
            field_name,
            buffer_text,
            sizeof(buffer_text)) ||
        !parse_integer_text(buffer_text, 0, 240, &updated->buffer_minutes)) {
        context->invalid = true;
        return 1;
    }

    snprintf(field_name, sizeof(field_name), "service_%s_active", current->code);
    updated->active = checkbox_is_checked(context->request, field_name);
    context->count++;
    return 0;
}

admin_calendar_result admin_calendar_save_all(const string *request)
{
    calendar_settings settings;
    admin_opening_update opening_hours[7];
    admin_service_update_collection services;

    memset(&services, 0, sizeof(services));
    services.request = request;

    if (!parse_settings(request, &settings)) {
        set_error("Mindestens eine Buchungsregel ist ungültig");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    for (int weekday = 1; weekday <= 7; weekday++) {
        if (!parse_opening_day_for_save_all(
                request,
                weekday,
                &opening_hours[weekday - 1])) {
            set_error("Öffnungszeiten sind unvollständig oder überschneiden sich");
            return ADMIN_CALENDAR_BAD_REQUEST;
        }
    }

    if (calendar_database_for_each_service(collect_service_update, &services) != 0 ||
        services.invalid) {
        set_error("Mindestens eine Leistung ist ungültig oder zu lang");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    if (calendar_database_begin_immediate() != 0) {
        set_database_error("Kalendereinstellungen konnten nicht für die Aktualisierung gesperrt werden");
        return ADMIN_CALENDAR_ERROR;
    }

    if (calendar_database_update_settings(&settings) != 0 ||
        calendar_database_clear_opening_hours() != 0) {
        set_database_error("Kalendereinstellungen konnten nicht ersetzt werden");
        calendar_database_rollback();
        return ADMIN_CALENDAR_ERROR;
    }

    for (int weekday = 1; weekday <= 7; weekday++) {
        admin_opening_update *day = &opening_hours[weekday - 1];

        for (size_t index = 0; index < day->count; index++) {
            if (calendar_database_add_opening_period(
                    weekday,
                    day->ranges[index].start_minute,
                    day->ranges[index].end_minute) != 0) {
                set_database_error("Öffnungszeiten konnten nicht gespeichert werden");
                calendar_database_rollback();
                return ADMIN_CALENDAR_ERROR;
            }
        }
    }

    for (size_t index = 0; index < services.count; index++) {
        if (calendar_database_update_service(&services.services[index]) != 0) {
            set_database_error("Leistungen konnten nicht gemeinsam gespeichert werden");
            calendar_database_rollback();
            return ADMIN_CALENDAR_ERROR;
        }
    }

    if (calendar_database_commit() != 0) {
        set_database_error("Gemeinsame Kalenderaktualisierung konnte nicht abgeschlossen werden");
        calendar_database_rollback();
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_update_opening_hours(const string *request)
{
    calendar_time_range ranges[ADMIN_OPENING_PERIODS_PER_DAY];
    char weekday_text[16];
    size_t count = 0;
    int weekday;

    if (request == NULL ||
        !get_required_field(request, "weekday", weekday_text, sizeof(weekday_text)) ||
        !parse_integer_text(weekday_text, 1, 7, &weekday)) {
        set_error("Ungültiger Wochentag");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    for (int index = 0; index < ADMIN_OPENING_PERIODS_PER_DAY; index++) {
        char start_name[32];
        char end_name[32];
        char start_text[16];
        char end_text[16];
        int start_minute;
        int end_minute;

        snprintf(start_name, sizeof(start_name), "start_%d", index + 1);
        snprintf(end_name, sizeof(end_name), "end_%d", index + 1);

        if (!get_optional_field(request, start_name, start_text, sizeof(start_text)) ||
            !get_optional_field(request, end_name, end_text, sizeof(end_text))) {
            set_error("Ungültige Öffnungszeiten");
            return ADMIN_CALENDAR_BAD_REQUEST;
        }

        if (start_text[0] == '\0' && end_text[0] == '\0') {
            continue;
        }

        if (start_text[0] == '\0' || end_text[0] == '\0' ||
            calendar_time_parse_hhmm(start_text, &start_minute) != 0 ||
            calendar_time_parse_hhmm(end_text, &end_minute) != 0 ||
            start_minute >= end_minute) {
            set_error("Jeder Öffnungszeitraum benötigt eine gültige Von- und Bis-Uhrzeit");
            return ADMIN_CALENDAR_BAD_REQUEST;
        }

        ranges[count].start_minute = start_minute;
        ranges[count].end_minute = end_minute;
        count++;
    }

    qsort(ranges, count, sizeof(ranges[0]), compare_ranges);

    for (size_t index = 1; index < count; index++) {
        if (ranges[index - 1].end_minute > ranges[index].start_minute) {
            set_error("Öffnungszeiten eines Tages dürfen sich nicht überschneiden");
            return ADMIN_CALENDAR_BAD_REQUEST;
        }
    }

    if (calendar_database_begin_immediate() != 0) {
        set_database_error("Öffnungszeiten konnten nicht gesperrt werden");
        return ADMIN_CALENDAR_ERROR;
    }

    if (calendar_database_clear_opening_hours_for_weekday(weekday) != 0) {
        set_database_error("Alte Öffnungszeiten konnten nicht ersetzt werden");
        calendar_database_rollback();
        return ADMIN_CALENDAR_ERROR;
    }

    for (size_t index = 0; index < count; index++) {
        if (calendar_database_add_opening_period(
                weekday,
                ranges[index].start_minute,
                ranges[index].end_minute) != 0) {
            set_database_error("Öffnungszeiten konnten nicht gespeichert werden");
            calendar_database_rollback();
            return ADMIN_CALENDAR_ERROR;
        }
    }

    if (calendar_database_commit() != 0) {
        set_database_error("Öffnungszeiten konnten nicht abgeschlossen werden");
        calendar_database_rollback();
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_update_service(const string *request)
{
    calendar_service service;
    char code[CALENDAR_SERVICE_CODE_SIZE];
    char name[CALENDAR_SERVICE_NAME_SIZE];
    char duration_text[32];
    char buffer_text[32];
    int load_result;
    int update_result;

    if (request == NULL ||
        !get_required_field(request, "code", code, sizeof(code)) ||
        !get_required_field(request, "name", name, sizeof(name)) ||
        !get_required_field(request, "duration_minutes", duration_text, sizeof(duration_text)) ||
        !get_required_field(request, "buffer_minutes", buffer_text, sizeof(buffer_text))) {
        set_error("Ungültige Leistungskonfiguration");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    load_result = calendar_database_get_service(code, &service);
    if (load_result == 1) {
        set_error("Leistung wurde nicht gefunden");
        return ADMIN_CALENDAR_NOT_FOUND;
    }
    if (load_result != 0) {
        set_database_error("Leistung konnte nicht geladen werden");
        return ADMIN_CALENDAR_ERROR;
    }

    if (!parse_integer_text(duration_text, 15, 720, &service.duration_minutes) ||
        !parse_integer_text(buffer_text, 0, 240, &service.buffer_minutes)) {
        set_error("Dauer oder Puffer ist ungültig");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    snprintf(service.name, sizeof(service.name), "%s", name);
    service.active = checkbox_is_checked(request, "active");

    update_result = calendar_database_update_service(&service);
    if (update_result == 1) {
        set_error("Leistung wurde nicht gefunden");
        return ADMIN_CALENDAR_NOT_FOUND;
    }
    if (update_result != 0) {
        set_database_error("Leistung konnte nicht gespeichert werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_add_service(const string *request)
{
    calendar_service service;
    char duration_text[32];
    char buffer_text[32];
    int result;

    memset(&service, 0, sizeof(service));

    if (request == NULL ||
        !get_required_field(request, "code", service.code, sizeof(service.code)) ||
        !get_required_field(request, "name", service.name, sizeof(service.name)) ||
        !get_required_field(request, "duration_minutes", duration_text, sizeof(duration_text)) ||
        !get_required_field(request, "buffer_minutes", buffer_text, sizeof(buffer_text)) ||
        !parse_integer_text(duration_text, 15, 720, &service.duration_minutes) ||
        !parse_integer_text(buffer_text, 0, 240, &service.buffer_minutes)) {
        set_error("Ungültige neue Leistung");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    service.active = checkbox_is_checked(request, "active");
    result = calendar_database_add_service(&service);
    if (result == 1) {
        set_error("Der technische Leistungsschlüssel ist bereits vergeben");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }
    if (result != 0) {
        set_database_error("Neue Leistung konnte nicht gespeichert werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_delete_service(const string *request)
{
    char code[CALENDAR_SERVICE_CODE_SIZE];
    calendar_service_delete_result result;

    if (request == NULL ||
        !get_required_field(request, "code", code, sizeof(code))) {
        set_error("Ungültiger Leistungsschlüssel");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    result = calendar_database_delete_service(code);
    if (result == CALENDAR_SERVICE_DELETE_NOT_FOUND) {
        set_error("Leistung wurde nicht gefunden");
        return ADMIN_CALENDAR_NOT_FOUND;
    }
    if (result == CALENDAR_SERVICE_DELETE_ERROR) {
        set_database_error("Leistung konnte nicht gelöscht oder archiviert werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_add_closure(const string *request)
{
    calendar_closure closure;
    char start_time[16];
    char end_time[16];
    int day_difference;
    bool all_day;

    memset(&closure, 0, sizeof(closure));

    if (request == NULL ||
        !get_required_field(request, "start_date", closure.start_date, sizeof(closure.start_date)) ||
        !get_required_field(request, "end_date", closure.end_date, sizeof(closure.end_date)) ||
        !get_optional_field(request, "start_time", start_time, sizeof(start_time)) ||
        !get_optional_field(request, "end_time", end_time, sizeof(end_time)) ||
        !get_optional_field(request, "label", closure.label, sizeof(closure.label)) ||
        !calendar_date_is_valid(closure.start_date) ||
        !calendar_date_is_valid(closure.end_date) ||
        calendar_date_days_between(
                closure.start_date,
                closure.end_date,
                &day_difference) != 0 ||
        day_difference < 0) {
        set_error("Ungültige Sperrzeit");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    all_day = checkbox_is_checked(request, "all_day");
    if (all_day) {
        closure.start_minute = 0;
        closure.end_minute = 1440;
    } else {
        if (day_difference != 0 ||
            calendar_time_parse_hhmm(start_time, &closure.start_minute) != 0 ||
            calendar_time_parse_hhmm(end_time, &closure.end_minute) != 0 ||
            closure.start_minute >= closure.end_minute) {
            set_error("Teilweise Sperrzeiten müssen an einem einzelnen Tag gültige Uhrzeiten besitzen");
            return ADMIN_CALENDAR_BAD_REQUEST;
        }
    }

    if (calendar_database_add_closure(&closure, NULL) != 0) {
        set_database_error("Sperrzeit konnte nicht gespeichert werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}

admin_calendar_result admin_calendar_delete_closure(const string *request)
{
    char id_text[32];
    int64_t closure_id;
    int result;

    if (request == NULL ||
        !get_required_field(request, "closure_id", id_text, sizeof(id_text)) ||
        !parse_positive_int64_text(id_text, &closure_id)) {
        set_error("Ungültige Sperrzeit-ID");
        return ADMIN_CALENDAR_BAD_REQUEST;
    }

    result = calendar_database_delete_closure(closure_id);
    if (result == 1) {
        set_error("Sperrzeit wurde nicht gefunden");
        return ADMIN_CALENDAR_NOT_FOUND;
    }
    if (result != 0) {
        set_database_error("Sperrzeit konnte nicht gelöscht werden");
        return ADMIN_CALENDAR_ERROR;
    }

    return ADMIN_CALENDAR_OK;
}
