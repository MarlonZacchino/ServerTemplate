#ifndef STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_TEMPLATES_H
#define STYLES4DOGS_NOTIFICATIONS_NOTIFICATION_TEMPLATES_H

/**
 * @file notification_templates.h
 * @brief Verwaltet und rendert konfigurierbare E-Mail-Vorlagen.
 */

#include <stdbool.h>
#include <stddef.h>

#include "styles4dogs/notifications/notification_queue.h"

/** @brief Maximale Länge des Ereignistyps einer Nachrichtenvorlage einschließlich Nullterminator. */
#define NOTIFICATION_TEMPLATE_EVENT_SIZE 32

/**
 * @brief Betreff- und Textvorlage für einen Benachrichtigungstyp.
 */
typedef struct notification_template {
    char event_type[NOTIFICATION_TEMPLATE_EVENT_SIZE]; /**< Ereignistyp, dem die Vorlage zugeordnet ist. */
    char subject_template[NOTIFICATION_SUBJECT_SIZE]; /**< Vorlage für den E-Mail-Betreff. */
    char body_template[NOTIFICATION_BODY_SIZE]; /**< Vorlage für den E-Mail-Text. */
} notification_template; /**< Typalias für ::notification_template. */

/**
 * @brief Werte, die beim Rendern einer E-Mail-Vorlage eingesetzt werden.
 */
typedef struct notification_template_context {
    const char *customer_name; /**< Vollständiger Kundenname. */
    const char *customer_first_name; /**< Vorname des Kunden. */
    const char *customer_last_name; /**< Nachname des Kunden. */
    const char *booking_id; /**< Buchungs-ID als bereits formatierter Text. */
    const char *appointment_date; /**< Termindatum im Format YYYY-MM-DD. */
    const char *start_time; /**< Formatierte Startzeit. */
    const char *end_time; /**< Formatierte Endzeit. */
    const char *service_name; /**< Angezeigter Name der Leistung. */
    const char *dog_name; /**< Name des Hundes. */
    const char *rejection_reason; /**< Optionaler Ablehnungsgrund. */
    const char *cancellation_reason; /**< Optionaler Stornierungsgrund. */
    const char *late_cancellation; /**< Verständlicher Hinweis zur Stornierungsfrist. */
    const char *old_appointment_date; /**< Vorheriges Termindatum. */
    const char *old_start_time; /**< Vorherige Startzeit. */
    const char *old_end_time; /**< Vorherige Endzeit. */
    const char *salon_name; /**< Öffentlicher Salonname. */
    const char *salon_address; /**< Öffentliche Salonanschrift. */
    const char *salon_phone; /**< Öffentliche Salontelefonnummer. */
    const char *website_url; /**< Öffentliche Basisadresse der Website. */
    const char *booking_url; /**< Persönlicher tokenbasierter Buchungslink. */
} notification_template_context; /**< Typalias für ::notification_template_context. */

/**
 * @brief Callback für die Iteration über Nachrichtenvorlagen.
 * @param[in] template_value Aktuelle Nachrichtenvorlage.
 * @param[in,out] context Vom Aufrufer bereitgestellter Kontextzeiger.
 * @return 0 zum Fortsetzen, ein von null verschiedener Wert zum Abbrechen.
 */
typedef int (*notification_template_callback)(
        const notification_template *template_value,
        void *context
);

/**
 * @brief Prüft, ob ein Ereignistyp eine unterstützte Vorlage besitzt.
 * @param[in] event_type Stabiler Ereignistyp der Nachrichtenvorlage.
 * @retval true Wenn die Prüfung oder Operation erfolgreich ist.
 * @retval false Wenn die Prüfung fehlschlägt oder Eingaben ungültig sind.
 */
bool notification_template_event_is_valid(const char *event_type);
/**
 * @brief Liefert die deutschsprachige Bezeichnung eines Ereignistyps.
 * @param[in] event_type Stabiler Ereignistyp der Nachrichtenvorlage.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *notification_template_event_label(const char *event_type);
/**
 * @brief Lädt die wirksame Vorlage eines Ereignistyps.
 * @param[in] event_type Stabiler Ereignistyp der Nachrichtenvorlage.
 * @param[out] out_template Ausgabeparameter für die geladene Vorlage.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_template_get(const char *event_type, notification_template *out_template);
/**
 * @brief Speichert eine benutzerdefinierte Vorlage.
 * @param[in] template_value Zu speichernde oder zu rendernde Vorlage.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_template_update(const notification_template *template_value);
/**
 * @brief Entfernt die benutzerdefinierte Vorlage eines Ereignistyps.
 * @param[in] event_type Stabiler Ereignistyp der Nachrichtenvorlage.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_template_reset(const char *event_type);
/**
 * @brief Iteriert über alle unterstützten Nachrichtenvorlagen.
 * @param[in] callback Callback, das für jeden gefundenen Datensatz aufgerufen wird.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_template_for_each(notification_template_callback callback, void *context);
/**
 * @brief Ersetzt Platzhalter und rendert Betreff sowie Nachrichtentext.
 * @param[in] template_value Zu speichernde oder zu rendernde Vorlage.
 * @param[in] context Undurchsichtiger Kontextzeiger für das Callback.
 * @param[in] subject Ausgabepuffer für den gerenderten Betreff.
 * @param[in] body Ausgabepuffer für den gerenderten Nachrichtentext.
 * @retval 0 Bei Erfolg.
 * @retval -1 Bei einem internen Fehler, sofern nicht anders beschrieben.
 */
int notification_template_render(
        const notification_template *template_value,
        const notification_template_context *context,
        char subject[NOTIFICATION_SUBJECT_SIZE],
        char body[NOTIFICATION_BODY_SIZE]);
/**
 * @brief Liefert die letzte Fehlermeldung der Vorlagenverwaltung.
 * @return Zeiger auf den modulverwalteten Wert. Der Aufrufer darf ihn nicht freigeben.
 */
const char *notification_templates_last_error(void);

#endif
