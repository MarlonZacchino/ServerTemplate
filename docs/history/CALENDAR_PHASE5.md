# Kalender Phase 5 – Terminübersicht und Benachrichtigungen

Phase 5 ergänzt den Buchungsworkflow um eine betriebliche Tages- und
Wochenansicht, Kontakt-Schnellaktionen sowie eine persistente
E-Mail-Warteschlange mit Erinnerungen und ICS-Kalenderdateien.

## Admin-Terminkalender

Geschützte Route:

```text
/admin/appointments
```

Unterstützte Query-Parameter:

```text
view=day|week
date=YYYY-MM-DD
```

Die Ansicht zeigt:

- offene und bestätigte Termine,
- Urlaub und andere Sperrzeiten,
- Uhrzeit, Leistung, Kunde und Hund,
- Annehmen/Ablehnen bei offenen Anfragen,
- direkte Detailnavigation zur Buchung,
- Warteschlangenstatus der Benachrichtigungen.

## Kontakt-Schnellaktionen

E-Mail-Kontakte erhalten einen `mailto:`-Link. Telefonnummern werden für
`tel:` normalisiert. Bei einer Mobilfunknummer wird zusätzlich ein
`https://wa.me/`-Link angezeigt. Die lokale Landesvorwahl wird über
`STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE` festgelegt.

## Benachrichtigungen

In `/admin/calendar` sind jetzt getrennt konfigurierbar:

- E-Mail-Benachrichtigungen,
- automatische Erinnerungen,
- Erinnerungszeitpunkt in Stunden,
- automatische oder manuelle Terminbestätigung.

Unterstützte Ereignisse:

```text
booking_received
booking_confirmed
booking_rejected
appointment_reminder
```

Bestätigungen und Erinnerungen enthalten eine `.ics`-Datei. Start- und
Endzeit werden aus der Salon-Zeitzone korrekt in UTC übertragen, damit
Kalenderprogramme Sommer- und Winterzeit zuverlässig darstellen.

## Datenschutz und Ausfallsicherheit

Die E-Mail wird nicht im HTTP-Request versendet. Der Server schreibt einen Job
in SQLite und antwortet dem Kunden unabhängig von der Erreichbarkeit des
Mailservers. Ein separater Worker verarbeitet die Queue. Fehlgeschlagene Jobs
werden mit zunehmender Verzögerung erneut versucht. Nach erfolgreichem Versand
werden Empfänger, Nachrichtentext und ICS-Inhalt aus dem Job entfernt.

## Datenbankschema

Phase 5 verwendet:

```text
PRAGMA user_version = 5
```

Neu sind die Kalendereinstellungen:

```text
email_notifications_enabled
reminder_enabled
reminder_lead_minutes
```

und die Tabelle:

```text
notification_jobs
```
