#include "admin_notifications.h"

#include "contact_validation.h"
#include "form_urlencoded.h"
#include "notification_queue.h"
#include "notification_settings.h"
#include "notification_templates.h"
#include "server_config.h"

#include <sodium.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ADMIN_NOTIFICATIONS_ERROR_SIZE 512

typedef struct template_page_context {
    string *page;
    const char *csrf_token;
} template_page_context;

static char module_error[ADMIN_NOTIFICATIONS_ERROR_SIZE];
static void set_error(const char *message) { snprintf(module_error, sizeof(module_error), "%s", message == NULL ? "Unbekannter E-Mail-Adminfehler" : message); }
const char *admin_notifications_last_error(void) { return module_error[0] == '\0' ? "Unbekannter E-Mail-Adminfehler" : module_error; }

static void html_char(string *destination, char c)
{
    switch (c) {
        case '&': str_cat_cstr(destination, "&amp;"); break;
        case '<': str_cat_cstr(destination, "&lt;"); break;
        case '>': str_cat_cstr(destination, "&gt;"); break;
        case '"': str_cat_cstr(destination, "&quot;"); break;
        case '\'': str_cat_cstr(destination, "&#39;"); break;
        default: str_cat(destination, &c, 1); break;
    }
}

static void html(string *destination, const char *source)
{
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0; source[i] != '\0'; i++) html_char(destination, source[i]);
}

static void append_size(string *destination, size_t value)
{
    char buffer[32];
    int written = snprintf(buffer, sizeof(buffer), "%zu", value);
    if (written > 0 && (size_t)written < sizeof(buffer)) str_cat_cstr(destination, buffer);
}

static void trim(char *text)
{
    size_t start = 0, length;
    if (text == NULL) return;
    length = strlen(text);
    while (start < length && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) start++;
    while (length > start && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r' || text[length - 1] == '\n')) length--;
    if (start > 0) memmove(text, text + start, length - start);
    text[length - start] = '\0';
}

static bool allowed_controls(const char *text, bool multiline)
{
    if (text == NULL) return false;
    for (size_t i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0x7f) return false;
        if (c < 0x20 && !(multiline && (c == '\r' || c == '\n' || c == '\t'))) return false;
    }
    return true;
}

static bool field(const string *request, const char *name, char *out, size_t size, bool required, bool multiline)
{
    form_value_result result = form_urlencoded_get(request, name, out, size);
    if (!required && result == FORM_VALUE_NOT_FOUND) { out[0] = '\0'; return true; }
    if (result != FORM_VALUE_OK) return false;
    trim(out);
    return (!required || out[0] != '\0') && allowed_controls(out, multiline);
}

static bool checked(const string *request, const char *name)
{
    char value[16];
    return form_urlencoded_get(request, name, value, sizeof(value)) == FORM_VALUE_OK && strcmp(value, "1") == 0;
}

static void csrf(string *page, const char *token)
{
    str_cat_cstr(page, "<input type=\"hidden\" name=\"csrf_token\" value=\"");
    html(page, token);
    str_cat_cstr(page, "\">");
}

static const char *notice(const char *code)
{
    if (code == NULL) return NULL;
    if (strcmp(code, "smtp") == 0) return "Die E-Mail-Verbindung wurde verschlüsselt gespeichert.";
    if (strcmp(code, "disconnected") == 0) return "Die E-Mail-Verbindung wurde deaktiviert.";
    if (strcmp(code, "test") == 0) return "Die Testmail wurde in die Warteschlange eingereiht.";
    if (strcmp(code, "template") == 0) return "Die Nachrichtenvorlage wurde gespeichert.";
    if (strcmp(code, "template-reset") == 0) return "Die Standardvorlage wurde wiederhergestellt.";
    if (strcmp(code, "retry") == 0) return "Fehlgeschlagene Nachrichten werden erneut versucht.";
    if (strcmp(code, "clear-sent") == 0) return "Der Zähler und die Historie gesendeter Nachrichten wurden zurückgesetzt.";
    if (strcmp(code, "clear-failed") == 0) return "Alle fehlgeschlagenen Nachrichten wurden aus der Warteschlange entfernt.";
    if (strcmp(code, "clear-completed") == 0) return "Gesendete und fehlgeschlagene Nachrichten wurden vollständig bereinigt.";
    return NULL;
}

static int template_editor(const notification_template *value, void *opaque)
{
    template_page_context *context = opaque;
    string *page;
    if (value == NULL || context == NULL || context->page == NULL) return -1;
    page = context->page;
    str_cat_cstr(page, "<article class=\"notification-template-card\"><div class=\"notification-template-heading\"><div><p class=\"eyebrow\">Vorlage</p><h3>");
    html(page, notification_template_event_label(value->event_type));
    str_cat_cstr(page, "</h3></div><code>"); html(page, value->event_type);
    str_cat_cstr(page, "</code></div><form class=\"notification-template-form\" method=\"post\" action=\"/admin/notifications/template\">");
    csrf(page, context->csrf_token);
    str_cat_cstr(page, "<input type=\"hidden\" name=\"event_type\" value=\""); html(page, value->event_type);
    str_cat_cstr(page, "\"><label>Betreff<input name=\"subject_template\" maxlength=\"255\" required value=\""); html(page, value->subject_template);
    str_cat_cstr(page, "\"></label><label>Nachricht<textarea name=\"body_template\" rows=\"12\" maxlength=\"4095\" required>"); html(page, value->body_template);
    str_cat_cstr(page, "</textarea></label><button class=\"button button-small\" type=\"submit\">Vorlage speichern</button></form>");
    str_cat_cstr(page, "<form class=\"notification-template-reset\" method=\"post\" action=\"/admin/notifications/template/reset\">");
    csrf(page, context->csrf_token);
    str_cat_cstr(page, "<input type=\"hidden\" name=\"event_type\" value=\""); html(page, value->event_type);
    str_cat_cstr(page, "\"><button class=\"button button-small button-secondary\" type=\"submit\">Standard wiederherstellen</button></form></article>");
    return 0;
}

string *admin_notifications_build_page(const char *csrf_token, const char *notice_code)
{
    notification_smtp_settings smtp;
    notification_queue_counts counts;
    string *page;
    template_page_context context;
    const char *message;

    module_error[0] = '\0';
    if (csrf_token == NULL || csrf_token[0] == '\0') { set_error("CSRF-Token fehlt"); return NULL; }
    if (notification_settings_load(&smtp) != 0) { set_error(notification_settings_last_error()); return NULL; }
    if (notification_queue_get_counts(&counts) != 0) { sodium_memzero(&smtp, sizeof(smtp)); set_error(notification_queue_last_error()); return NULL; }

    page = _new_string();
    if (page == NULL) { sodium_memzero(&smtp, sizeof(smtp)); set_error("Speicher für Adminseite fehlt"); return NULL; }
    message = notice(notice_code);

    str_cat_cstr(page, "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"robots\" content=\"noindex,nofollow\"><title>E-Mail und Nachrichten - Styles 4 Dogs</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><header class=\"site-header\"><div class=\"container nav-wrap\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark\">S4D</span><span>Styles 4 Dogs</span></a><nav class=\"site-nav\" aria-label=\"Admin-Navigation\"><a href=\"/\">Website öffnen</a><a href=\"/admin/bookings\">Buchungsanfragen</a><a href=\"/admin/appointments\">Termine</a><a href=\"/admin/calendar\">Einstellungen</a><a href=\"/admin/notifications\" aria-current=\"page\">E-Mail</a></nav></div></header><main class=\"page admin-page admin-notifications-page\"><section class=\"card admin-card\"><p class=\"eyebrow\">Admin</p><h1>E-Mail und Nachrichten</h1><p>Verbinde das Salon-Postfach, teste den Versand und passe automatische Nachrichten an.</p>");
    if (message != NULL) { str_cat_cstr(page, "<p class=\"admin-success\" role=\"status\">"); html(page, message); str_cat_cstr(page, "</p>"); }
    str_cat_cstr(page, "<div class=\"notification-status-grid\"><div><span>Verbindung</span><strong>"); str_cat_cstr(page, smtp.enabled ? "Aktiv" : "Nicht verbunden");
    str_cat_cstr(page, "</strong></div><div><span>Ausstehend</span><strong>"); append_size(page, counts.pending);
    str_cat_cstr(page, "</strong></div><div><span>Fehlgeschlagen</span><strong>"); append_size(page, counts.failed);
    str_cat_cstr(page, "</strong></div><div><span>Gesendet</span><strong>"); append_size(page, counts.sent);
    str_cat_cstr(page, "</strong></div></div></section>");

    str_cat_cstr(page, "<section class=\"card admin-card\"><p class=\"eyebrow\">Postfach</p><h2>E-Mail-Konto verbinden</h2><p>Die Zugangsdaten werden verschlüsselt im Secrets-Verzeichnis gespeichert. Ein leeres Passwortfeld behält beim Bearbeiten das vorhandene Passwort.</p><form class=\"admin-calendar-form\" method=\"post\" action=\"/admin/notifications/smtp\">"); csrf(page, csrf_token);
    str_cat_cstr(page, "<div class=\"admin-calendar-fields notification-smtp-fields\"><label class=\"admin-calendar-wide\">SMTP-Adresse<input name=\"smtp_url\" maxlength=\"511\" required placeholder=\"smtps://smtp.anbieter.de:465\" value=\""); html(page, smtp.url);
    str_cat_cstr(page, "\"></label><label>Benutzername<input name=\"smtp_username\" maxlength=\"255\" autocomplete=\"username\" value=\""); html(page, smtp.username);
    str_cat_cstr(page, "\"></label><label>Neues Passwort oder App-Passwort<input name=\"smtp_password\" type=\"password\" maxlength=\"511\" autocomplete=\"new-password\" placeholder=\"Beim Bearbeiten leer lassen\"></label><label>Absenderadresse<input name=\"from_address\" type=\"email\" maxlength=\"255\" required value=\""); html(page, smtp.from_address);
    str_cat_cstr(page, "\"></label><label>Absendername<input name=\"from_name\" maxlength=\"255\" value=\""); html(page, smtp.from_name[0] == '\0' ? server_config_salon_name() : smtp.from_name);
    str_cat_cstr(page, "\"></label><label>Admin-Benachrichtigungsadresse<input name=\"admin_email\" type=\"email\" maxlength=\"255\" value=\""); html(page, smtp.admin_email);
    str_cat_cstr(page, "\"></label></div><label class=\"admin-checkbox\"><input type=\"checkbox\" name=\"notify_admin_on_new_booking\" value=\"1\""); if (smtp.notify_admin_on_new_booking) str_cat_cstr(page, " checked");
    str_cat_cstr(page, "><span>Bei neuen Terminanfragen eine E-Mail an den Admin senden</span></label><p class=\"admin-calendar-hint\">Für manche Anbieter wird ein separates App-Passwort benötigt. Zertifikate werden beim SMTP-Versand geprüft.</p><p class=\"admin-calendar-hint\">Kunden-E-Mails und Erinnerungen werden getrennt unter <a href=\"/admin/calendar#buchungsregeln\">Kalender-Einstellungen</a> aktiviert.</p><button class=\"button\" type=\"submit\">E-Mail-Verbindung speichern</button></form>");

    if (smtp.enabled) {
        str_cat_cstr(page, "<div class=\"notification-account-actions\"><form method=\"post\" action=\"/admin/notifications/test\">"); csrf(page, csrf_token);
        str_cat_cstr(page, "<label>Testmail an<input name=\"recipient_email\" type=\"email\" required value=\""); html(page, smtp.admin_email[0] == '\0' ? smtp.from_address : smtp.admin_email);
        str_cat_cstr(page, "\"></label><button class=\"button button-small\" type=\"submit\">Testmail einreihen</button></form><form method=\"post\" action=\"/admin/notifications/disconnect\">"); csrf(page, csrf_token);
        str_cat_cstr(page, "<button class=\"button button-small button-danger\" type=\"submit\">Verbindung deaktivieren</button></form></div>");
    }
    str_cat_cstr(page, "</section><section class=\"card admin-card\"><p class=\"eyebrow\">Versand</p><h2>Warteschlange und Zähler</h2><p>Ausstehende Nachrichten bleiben unangetastet. Gesendete oder endgültig fehlgeschlagene Einträge können separat bereinigt werden.</p><div class=\"notification-queue-actions\"><form method=\"post\" action=\"/admin/notifications/retry\">"); csrf(page, csrf_token);
    str_cat_cstr(page, "<button class=\"button button-secondary\" type=\"submit\">Fehlgeschlagene erneut versuchen</button></form><form method=\"post\" action=\"/admin/notifications/clear-sent\">"); csrf(page, csrf_token);
    str_cat_cstr(page, "<button class=\"button button-secondary\" type=\"submit\" onclick=\"return confirm('Zähler und Historie gesendeter E-Mails zurücksetzen?')\">Gesendet-Zähler zurücksetzen</button></form><form method=\"post\" action=\"/admin/notifications/clear-failed\">"); csrf(page, csrf_token);
    str_cat_cstr(page, "<button class=\"button button-danger\" type=\"submit\" onclick=\"return confirm('Alle fehlgeschlagenen Nachrichten endgültig entfernen?')\">Fehlgeschlagene löschen</button></form><form method=\"post\" action=\"/admin/notifications/clear-completed\">"); csrf(page, csrf_token);
    str_cat_cstr(page, "<button class=\"button button-danger\" type=\"submit\" onclick=\"return confirm('Gesendete und fehlgeschlagene Nachrichten vollständig bereinigen?')\">Abgeschlossene Historie leeren</button></form></div><p class=\"admin-calendar-hint\">Ausstehende und gerade verarbeitete E-Mails werden durch diese Bereinigung nicht gelöscht.</p></section>");

    str_cat_cstr(page, "<section class=\"card admin-card notification-template-section\"><p class=\"eyebrow\">Texte</p><h2>Automatische Nachrichten individualisieren</h2><p>Bereits eingereihte E-Mails behalten ihren bisherigen Text.</p><details class=\"notification-placeholder-help\"><summary>Verfügbare Platzhalter</summary><code>{{customer_name}}</code> <code>{{booking_id}}</code> <code>{{appointment_date}}</code> <code>{{start_time}}</code> <code>{{end_time}}</code> <code>{{service_name}}</code> <code>{{dog_name}}</code> <code>{{rejection_reason}}</code> <code>{{salon_name}}</code> <code>{{salon_address}}</code> <code>{{salon_phone}}</code> <code>{{website_url}}</code></details><div class=\"notification-template-list\">");
    context.page = page; context.csrf_token = csrf_token;
    if (notification_template_for_each(template_editor, &context) != 0) { free_str(page); sodium_memzero(&smtp, sizeof(smtp)); set_error(notification_templates_last_error()); return NULL; }
    str_cat_cstr(page, "</div></section></main><footer class=\"site-footer\"><div class=\"container footer-bottom\"><small>&copy; 2026 Styles 4 Dogs Admin.</small></div></footer></body></html>");
    sodium_memzero(&smtp, sizeof(smtp)); return page;
}

admin_notifications_result admin_notifications_update_smtp(const string *request)
{
    notification_smtp_settings current, updated;
    char password[NOTIFICATION_SMTP_PASSWORD_SIZE];
    if (request == NULL || notification_settings_load(&current) != 0) { set_error(notification_settings_last_error()); return ADMIN_NOTIFICATIONS_ERROR; }
    memset(&updated, 0, sizeof(updated)); memset(password, 0, sizeof(password));
    if (!field(request, "smtp_url", updated.url, sizeof(updated.url), true, false) ||
        !field(request, "smtp_username", updated.username, sizeof(updated.username), false, false) ||
        !field(request, "smtp_password", password, sizeof(password), false, false) ||
        !field(request, "from_address", updated.from_address, sizeof(updated.from_address), true, false) ||
        !field(request, "from_name", updated.from_name, sizeof(updated.from_name), false, false) ||
        !field(request, "admin_email", updated.admin_email, sizeof(updated.admin_email), false, false)) {
        sodium_memzero(&current, sizeof(current)); sodium_memzero(password, sizeof(password)); set_error("E-Mail-Verbindungsdaten sind unvollständig oder zu lang"); return ADMIN_NOTIFICATIONS_BAD_REQUEST;
    }
    if (password[0] == '\0') {
        if (updated.username[0] != '\0' && (current.password[0] == '\0' || strcmp(updated.username, current.username) != 0)) {
            sodium_memzero(&current, sizeof(current)); sodium_memzero(password, sizeof(password)); set_error("Für einen neuen Benutzernamen muss ein Passwort angegeben werden"); return ADMIN_NOTIFICATIONS_BAD_REQUEST;
        }
        snprintf(updated.password, sizeof(updated.password), "%s", current.password);
    } else snprintf(updated.password, sizeof(updated.password), "%s", password);
    updated.enabled = true; updated.managed_by_admin = true; updated.notify_admin_on_new_booking = checked(request, "notify_admin_on_new_booking");
    if (updated.from_name[0] == '\0') snprintf(updated.from_name, sizeof(updated.from_name), "%s", server_config_salon_name());
    if (!notification_settings_are_valid(&updated, true)) { set_error("SMTP-Adresse, Absender oder Zugangsdaten sind ungültig"); sodium_memzero(&current, sizeof(current)); sodium_memzero(&updated, sizeof(updated)); sodium_memzero(password, sizeof(password)); return ADMIN_NOTIFICATIONS_BAD_REQUEST; }
    if (notification_settings_save(&updated) != 0) { set_error(notification_settings_last_error()); sodium_memzero(&current, sizeof(current)); sodium_memzero(&updated, sizeof(updated)); sodium_memzero(password, sizeof(password)); return ADMIN_NOTIFICATIONS_ERROR; }
    sodium_memzero(&current, sizeof(current)); sodium_memzero(&updated, sizeof(updated)); sodium_memzero(password, sizeof(password)); return ADMIN_NOTIFICATIONS_OK;
}

admin_notifications_result admin_notifications_disconnect_smtp(const string *request) { (void)request; if (notification_settings_disconnect() != 0) { set_error(notification_settings_last_error()); return ADMIN_NOTIFICATIONS_ERROR; } return ADMIN_NOTIFICATIONS_OK; }

admin_notifications_result admin_notifications_enqueue_test(const string *request)
{
    char recipient[NOTIFICATION_SMTP_ADDRESS_SIZE];
    if (!field(request, "recipient_email", recipient, sizeof(recipient), true, false) || !contact_email_is_valid(recipient)) { set_error("Empfängeradresse der Testmail ist ungültig"); return ADMIN_NOTIFICATIONS_BAD_REQUEST; }
    if (notification_queue_enqueue_test_email(recipient) != 0) { set_error(notification_queue_last_error()); return ADMIN_NOTIFICATIONS_ERROR; }
    return ADMIN_NOTIFICATIONS_OK;
}

admin_notifications_result admin_notifications_update_template(const string *request)
{
    notification_template value; memset(&value, 0, sizeof(value));
    if (!field(request, "event_type", value.event_type, sizeof(value.event_type), true, false) ||
        !field(request, "subject_template", value.subject_template, sizeof(value.subject_template), true, false) ||
        !field(request, "body_template", value.body_template, sizeof(value.body_template), true, true) ||
        !notification_template_event_is_valid(value.event_type)) { set_error("Nachrichtenvorlage ist ungültig oder zu lang"); return ADMIN_NOTIFICATIONS_BAD_REQUEST; }
    if (notification_template_update(&value) != 0) { set_error(notification_templates_last_error()); return ADMIN_NOTIFICATIONS_BAD_REQUEST; }
    return ADMIN_NOTIFICATIONS_OK;
}

admin_notifications_result admin_notifications_reset_template(const string *request)
{
    char event_type[NOTIFICATION_TEMPLATE_EVENT_SIZE];
    if (!field(request, "event_type", event_type, sizeof(event_type), true, false) || notification_template_reset(event_type) != 0) { set_error(notification_templates_last_error()); return ADMIN_NOTIFICATIONS_BAD_REQUEST; }
    return ADMIN_NOTIFICATIONS_OK;
}

admin_notifications_result admin_notifications_retry_failed(const string *request) { (void)request; if (notification_queue_retry_failed() != 0) { set_error(notification_queue_last_error()); return ADMIN_NOTIFICATIONS_ERROR; } return ADMIN_NOTIFICATIONS_OK; }
admin_notifications_result admin_notifications_clear_sent(const string *request) { (void)request; if (notification_queue_clear_sent() != 0) { set_error(notification_queue_last_error()); return ADMIN_NOTIFICATIONS_ERROR; } return ADMIN_NOTIFICATIONS_OK; }
admin_notifications_result admin_notifications_clear_failed(const string *request) { (void)request; if (notification_queue_clear_failed() != 0) { set_error(notification_queue_last_error()); return ADMIN_NOTIFICATIONS_ERROR; } return ADMIN_NOTIFICATIONS_OK; }
admin_notifications_result admin_notifications_clear_completed(const string *request) { (void)request; if (notification_queue_clear_completed() != 0) { set_error(notification_queue_last_error()); return ADMIN_NOTIFICATIONS_ERROR; } return ADMIN_NOTIFICATIONS_OK; }
