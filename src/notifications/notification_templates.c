#include "styles4dogs/notifications/notification_templates.h"

#include "styles4dogs/core/server_config.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#define TEMPLATE_ERROR_SIZE 512
#define PLACEHOLDER_SIZE 64

typedef struct default_template {
    const char *event_type;
    const char *subject;
    const char *body;
    const char *label;
} default_template;

static const default_template defaults[] = {
    {"booking_received", "Terminanfrage erhalten – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\nwir haben deine Terminanfrage erhalten. Der Zeitraum ist vorläufig reserviert und noch nicht verbindlich bestätigt.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nAnfrage ansehen oder zurückziehen:\n{{booking_url}}\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Eingangsbestätigung"},
    {"booking_confirmed", "Termin bestätigt – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\ndein Termin ist verbindlich bestätigt.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nDie Kalenderdatei für deinen Termin ist angehängt.\n\nTermin ansehen oder absagen:\n{{booking_url}}\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Terminbestätigung"},
    {"booking_rejected", "Terminanfrage nicht möglich – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\nleider können wir deine Terminanfrage nicht bestätigen.\n{{rejection_reason}}\n\nAngefragter Termin: {{appointment_date}}, {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nMelde dich gerne bei uns, damit wir gemeinsam einen anderen Termin finden.\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Terminabsage"},
    {"appointment_reminder", "Erinnerung an deinen Termin – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\ndies ist eine Erinnerung an deinen bevorstehenden Termin.\n\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nDie Kalenderdatei für deinen Termin ist angehängt.\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Terminerinnerung"},
    {"admin_new_booking", "Neue Terminanfrage #{{booking_id}} – {{customer_first_name}} {{customer_last_name}}",
     "Es ist eine neue Terminanfrage eingegangen.\n\nBuchungsnummer: {{booking_id}}\nKundin/Kunde: {{customer_first_name}} {{customer_last_name}}\nHund: {{dog_name}}\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\n\nDie Anfrage kann im Adminbereich geprüft werden:\n{{website_url}}/admin/bookings",
     "Neue Terminanfrage für den Admin"},
    {"admin_booking_cancelled", "Kundenabsage #{{booking_id}} – {{customer_first_name}} {{customer_last_name}}",
     "Eine Kundin oder ein Kunde hat eine Buchung über den persönlichen Buchungslink abgesagt.\n\nBuchungsnummer: {{booking_id}}\nKundin/Kunde: {{customer_first_name}} {{customer_last_name}}\nHund: {{dog_name}}\nDatum: {{appointment_date}}\nUhrzeit: {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nGrund: {{cancellation_reason}}\n{{late_cancellation}}\n\nDie Buchung kann im Adminbereich geprüft werden:\n{{website_url}}/admin/bookings?search={{booking_id}}",
     "Kundenabsage für den Admin"},
    {"booking_rescheduled", "Dein Termin wurde geändert – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\ndein Termin wurde geändert.\n\nBisher: {{old_appointment_date}}, {{old_start_time}}–{{old_end_time}} Uhr\nNeu: {{appointment_date}}, {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\n\nDie aktualisierte Kalenderdatei ist angehängt.\n\nTermin ansehen oder absagen:\n{{booking_url}}\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Terminverschiebung"},
    {"booking_cancelled", "Deine Terminabsage – {{salon_name}}",
     "Hallo {{customer_first_name}},\n\nwir bestätigen die Absage deines Termins.\n\nTermin: {{appointment_date}}, {{start_time}}–{{end_time}} Uhr\nLeistung: {{service_name}}\nHund: {{dog_name}}\nGrund: {{cancellation_reason}}\n{{late_cancellation}}\n\nViele Grüße\n{{salon_name}}\n{{salon_address}}\n{{salon_phone}}\n{{website_url}}",
     "Absagebestätigung für den Kunden"}
};

static char template_error[TEMPLATE_ERROR_SIZE];
static void set_error(const char *message) { snprintf(template_error, sizeof(template_error), "%s", message == NULL ? "Unbekannter Vorlagenfehler" : message); }
static void set_sqlite(sqlite3 *db, const char *context) { snprintf(template_error, sizeof(template_error), "%s: %s", context, db == NULL ? "Datenbank nicht geöffnet" : sqlite3_errmsg(db)); }
const char *notification_templates_last_error(void) { return template_error[0] == '\0' ? "Unbekannter Vorlagenfehler" : template_error; }

static const default_template *find_default(const char *event_type)
{
    if (event_type == NULL) return NULL;
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        if (strcmp(defaults[i].event_type, event_type) == 0) return &defaults[i];
    return NULL;
}

bool notification_template_event_is_valid(const char *event_type) { return find_default(event_type) != NULL; }
const char *notification_template_event_label(const char *event_type) { const default_template *d = find_default(event_type); return d == NULL ? "Unbekannte Nachricht" : d->label; }

static const char *db_path(void)
{
    return strcmp(server_config_database_file(), ":memory:") == 0
           ? "file:styles4dogs-runtime?mode=memory&cache=shared" : server_config_database_file();
}

static int open_db(sqlite3 **out)
{
    sqlite3 *db = NULL;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
    if (strncmp(db_path(), "file:", 5) == 0) flags |= SQLITE_OPEN_URI;
    if (sqlite3_open_v2(db_path(), &db, flags, NULL) != SQLITE_OK) { set_sqlite(db, "Vorlagendatenbank konnte nicht geöffnet werden"); sqlite3_close_v2(db); return -1; }
    sqlite3_busy_timeout(db, 5000);
    *out = db; return 0;
}

static int copy_column(sqlite3_stmt *stmt, int column, char *destination, size_t size)
{
    const unsigned char *value = sqlite3_column_text(stmt, column);
    int written = snprintf(destination, size, "%s", value == NULL ? "" : (const char *)value);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static const char *nonnull(const char *value)
{
    return value == NULL ? "" : value;
}

static const char *placeholder(const notification_template_context *context, const char *name)
{
    if (context == NULL || name == NULL) return NULL;
    if (strcmp(name, "customer_name") == 0) return nonnull(context->customer_name);
    if (strcmp(name, "customer_first_name") == 0) return nonnull(context->customer_first_name);
    if (strcmp(name, "customer_last_name") == 0) return nonnull(context->customer_last_name);
    if (strcmp(name, "booking_id") == 0) return nonnull(context->booking_id);
    if (strcmp(name, "appointment_date") == 0) return nonnull(context->appointment_date);
    if (strcmp(name, "start_time") == 0) return nonnull(context->start_time);
    if (strcmp(name, "end_time") == 0) return nonnull(context->end_time);
    if (strcmp(name, "service_name") == 0) return nonnull(context->service_name);
    if (strcmp(name, "dog_name") == 0) return nonnull(context->dog_name);
    if (strcmp(name, "rejection_reason") == 0) return nonnull(context->rejection_reason);
    if (strcmp(name, "cancellation_reason") == 0) return nonnull(context->cancellation_reason);
    if (strcmp(name, "late_cancellation") == 0) return nonnull(context->late_cancellation);
    if (strcmp(name, "old_appointment_date") == 0) return nonnull(context->old_appointment_date);
    if (strcmp(name, "old_start_time") == 0) return nonnull(context->old_start_time);
    if (strcmp(name, "old_end_time") == 0) return nonnull(context->old_end_time);
    if (strcmp(name, "salon_name") == 0) return nonnull(context->salon_name);
    if (strcmp(name, "salon_address") == 0) return nonnull(context->salon_address);
    if (strcmp(name, "salon_phone") == 0) return nonnull(context->salon_phone);
    if (strcmp(name, "website_url") == 0) return nonnull(context->website_url);
    if (strcmp(name, "booking_url") == 0) return nonnull(context->booking_url);
    return NULL;
}

static int append(char *destination, size_t size, size_t *position, const char *value)
{
    size_t length = value == NULL ? 0 : strlen(value);
    if (*position >= size || length >= size - *position) return -1;
    memcpy(destination + *position, value == NULL ? "" : value, length);
    *position += length; destination[*position] = '\0'; return 0;
}

static int render_text(const char *source, const notification_template_context *context, char *destination, size_t size)
{
    size_t in = 0, out = 0;
    destination[0] = '\0';
    while (source[in] != '\0') {
        if (source[in] == '{' && source[in + 1] == '{') {
            size_t start = in + 2, end = start;
            char name[PLACEHOLDER_SIZE];
            const char *value;
            while (source[end] != '\0' && !(source[end] == '}' && source[end + 1] == '}')) end++;
            if (source[end] == '\0' || end == start || end - start >= sizeof(name)) { set_error("Vorlage enthält einen ungültigen Platzhalter"); return -1; }
            memcpy(name, source + start, end - start); name[end - start] = '\0';
            value = placeholder(context, name);
            if (value == NULL) { snprintf(template_error, sizeof(template_error), "Unbekannter Platzhalter: {{%s}}", name); return -1; }
            if (append(destination, size, &out, value) != 0) { set_error("Gerenderte Nachricht ist zu lang"); return -1; }
            in = end + 2;
        } else {
            char one[2] = {source[in++], '\0'};
            if (append(destination, size, &out, one) != 0) { set_error("Gerenderte Nachricht ist zu lang"); return -1; }
        }
    }
    return 0;
}

static bool subject_valid(const char *subject)
{
    if (subject == NULL || subject[0] == '\0') return false;
    for (size_t i = 0; subject[i] != '\0'; i++) {
        unsigned char c = (unsigned char)subject[i];
        if (c == '\r' || c == '\n' || c == 0x7f || (c < 0x20 && c != '\t')) return false;
    }
    return true;
}

static bool valid_template(const notification_template *value)
{
    notification_template_context empty = {0};
    char subject[NOTIFICATION_SUBJECT_SIZE], body[NOTIFICATION_BODY_SIZE];
    return value != NULL && notification_template_event_is_valid(value->event_type) &&
           subject_valid(value->subject_template) && value->body_template[0] != '\0' &&
           render_text(value->subject_template, &empty, subject, sizeof(subject)) == 0 &&
           subject_valid(subject) && render_text(value->body_template, &empty, body, sizeof(body)) == 0;
}

int notification_template_get(const char *event_type, notification_template *out)
{
    sqlite3 *db = NULL; sqlite3_stmt *stmt = NULL; int result = -1;
    template_error[0] = '\0';
    if (!notification_template_event_is_valid(event_type) || out == NULL) { set_error("Ungültige Nachrichtenvorlage"); return -1; }
    if (open_db(&db) != 0) return -1;
    if (sqlite3_prepare_v2(db, "SELECT event_type, subject_template, body_template FROM notification_templates WHERE event_type=?1;", -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, event_type, -1, SQLITE_TRANSIENT) != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW ||
        copy_column(stmt, 0, out->event_type, sizeof(out->event_type)) != 0 ||
        copy_column(stmt, 1, out->subject_template, sizeof(out->subject_template)) != 0 ||
        copy_column(stmt, 2, out->body_template, sizeof(out->body_template)) != 0) {
        set_sqlite(db, "Nachrichtenvorlage konnte nicht gelesen werden");
    } else result = 0;
    sqlite3_finalize(stmt); sqlite3_close_v2(db); return result;
}

int notification_template_update(const notification_template *value)
{
    sqlite3 *db = NULL; sqlite3_stmt *stmt = NULL; int result = -1;
    template_error[0] = '\0';
    if (!valid_template(value)) { if (template_error[0] == '\0') set_error("Nachrichtenvorlage ist ungültig"); return -1; }
    if (open_db(&db) != 0) return -1;
    if (sqlite3_prepare_v2(db, "UPDATE notification_templates SET subject_template=?1, body_template=?2, updated_at_utc=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE event_type=?3;", -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, value->subject_template, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, value->body_template, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, value->event_type, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_DONE || sqlite3_changes(db) != 1) set_sqlite(db, "Nachrichtenvorlage konnte nicht gespeichert werden");
    else result = 0;
    sqlite3_finalize(stmt); sqlite3_close_v2(db); return result;
}

int notification_template_reset(const char *event_type)
{
    const default_template *d = find_default(event_type);
    notification_template value;
    if (d == NULL) { set_error("Unbekannte Nachrichtenvorlage"); return -1; }
    memset(&value, 0, sizeof(value));
    snprintf(value.event_type, sizeof(value.event_type), "%s", d->event_type);
    snprintf(value.subject_template, sizeof(value.subject_template), "%s", d->subject);
    snprintf(value.body_template, sizeof(value.body_template), "%s", d->body);
    return notification_template_update(&value);
}

int notification_template_for_each(notification_template_callback callback, void *context)
{
    if (callback == NULL) { set_error("Vorlagen-Callback fehlt"); return -1; }
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        notification_template value;
        if (notification_template_get(defaults[i].event_type, &value) != 0 || callback(&value, context) != 0) return -1;
    }
    return 0;
}

int notification_template_render(const notification_template *value, const notification_template_context *context,
                                 char subject[NOTIFICATION_SUBJECT_SIZE], char body[NOTIFICATION_BODY_SIZE])
{
    template_error[0] = '\0';
    if (!valid_template(value) || context == NULL ||
        render_text(value->subject_template, context, subject, NOTIFICATION_SUBJECT_SIZE) != 0 ||
        render_text(value->body_template, context, body, NOTIFICATION_BODY_SIZE) != 0 || !subject_valid(subject)) {
        if (template_error[0] == '\0') {
            set_error("Nachrichtenvorlage konnte nicht gerendert werden");
        }
        return -1;
    }
    return 0;
}
