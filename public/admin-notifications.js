"use strict";

(() => {
    const absencePreset = {
        subject: "Terminanfrage erhalten – Rückmeldung kann länger dauern – {{salon_name}}",
        body: [
            "Hallo {{customer_name}},",
            "",
            "wir haben deine Terminanfrage erhalten. Der Salon ist derzeit vorübergehend nur eingeschränkt erreichbar, deshalb kann unsere persönliche Rückmeldung etwas länger dauern.",
            "",
            "Dein angefragter Zeitraum ist vorläufig reserviert und noch nicht verbindlich bestätigt.",
            "",
            "Datum: {{appointment_date}}",
            "Uhrzeit: {{start_time}}–{{end_time}} Uhr",
            "Leistung: {{service_name}}",
            "Hund: {{dog_name}}",
            "",
            "Wir melden uns so bald wie möglich bei dir.",
            "",
            "Viele Grüße",
            "{{salon_name}}",
            "{{salon_address}}",
            "{{salon_phone}}",
            "{{website_url}}",
        ].join("\n"),
    };

    document.querySelectorAll("[data-notification-preset]").forEach((button) => {
        button.addEventListener("click", () => {
            const card = button.closest("[data-notification-template]");
            const form = card?.querySelector(".notification-template-form");
            const subject = form?.querySelector('[name="subject_template"]');
            const body = form?.querySelector('[name="body_template"]');
            const status = card?.querySelector("[data-notification-preset-status]");

            if (!form || !subject || !body || button.dataset.notificationPreset !== "absence") {
                return;
            }

            const confirmed = window.confirm(
                "Die aktuell sichtbare Eingangsbestätigung wird durch die Abwesenheitsnotiz ersetzt. Fortfahren?"
            );
            if (!confirmed) {
                return;
            }

            subject.value = absencePreset.subject;
            body.value = absencePreset.body;
            subject.dispatchEvent(new Event("input", {bubbles: true}));
            body.dispatchEvent(new Event("input", {bubbles: true}));

            if (status) {
                status.textContent = "Abwesenheitsnotiz eingesetzt. Zum Aktivieren noch „Vorlage speichern“ wählen.";
            }
            subject.focus();
        });
    });
})();
