# Kalender Phase 6 – E-Mail-Verbindung und Nachrichtenvorlagen

Phase 6 macht die E-Mail-Konfiguration im geschützten Adminbereich bedienbar
und löst die fest im Programm hinterlegten Kundentexte durch versionierte
SQLite-Vorlagen ab.

## Adminbereich

Neue Seite:

```text
/admin/notifications
```

Dort kann der Admin:

- ein SMTP-Postfach verbinden oder deaktivieren,
- eine eigene Absenderadresse und einen Absendernamen hinterlegen,
- eine separate Admin-Adresse für neue Terminanfragen festlegen,
- eine Testmail in die Warteschlange einreihen,
- fehlgeschlagene Nachrichten erneut freigeben,
- Betreff und Text aller automatischen Nachrichten bearbeiten,
- einzelne Vorlagen auf den sicheren Standard zurücksetzen.

Die Seite ist wie alle Admin-POST-Routen durch Basic Auth, CSRF-Prüfung und das
Admin-Rate-Limit geschützt. Das gespeicherte Passwort wird niemals wieder an
den Browser ausgegeben. Beim Bearbeiten behält ein leeres Passwortfeld das
vorhandene Passwort.

## Verwaltete Vorlagen

Folgende Ereignisse besitzen eigene Vorlagen:

```text
booking_received       Eingangsbestätigung
booking_confirmed      Terminbestätigung
booking_rejected       Terminabsage
appointment_reminder   Terminerinnerung
admin_new_booking      interne Admin-Benachrichtigung
```

Erlaubte Platzhalter:

```text
{{customer_name}}
{{booking_id}}
{{appointment_date}}
{{start_time}}
{{end_time}}
{{service_name}}
{{dog_name}}
{{rejection_reason}}
{{salon_name}}
{{salon_address}}
{{salon_phone}}
{{website_url}}
```

Unbekannte oder nicht geschlossene Platzhalter werden beim Speichern
abgelehnt. Betreffzeilen dürfen keine Zeilenumbrüche enthalten. Damit können
aus den Vorlagen keine zusätzlichen Mail-Header eingeschleust werden.

Bereits in `notification_jobs` eingereihte Nachrichten behalten bewusst ihren
bereits erzeugten Text. Eine spätere Vorlagenänderung verändert daher keine
schon geplanten oder fehlgeschlagenen E-Mails.

## Schutz der SMTP-Zugangsdaten

Bei einer Konfiguration über den Adminbereich werden die Daten mit libsodium
`crypto_secretbox` authentifiziert verschlüsselt gespeichert:

```text
/etc/styles4dogs/secrets/notification.smtp
/etc/styles4dogs/secrets/notification.key
```

Beide Dateien erhalten Modus `0600`; das Secrets-Verzeichnis bleibt `0700`.
Der Schlüssel liegt aus Betriebsgründen auf demselben Server. Die
Verschlüsselung schützt deshalb vor versehentlichem Klartextzugriff, Backups
oder Dateiansichten, nicht vor einem kompromittierten Root-Konto oder dem
laufenden Dienstbenutzer.

Für den Produktivbetrieb soll ein eigenes Salon-Postfach und – soweit der
Anbieter dies unterstützt – ein separates App-Passwort verwendet werden.
SMTP-Geheimnisse dürfen nicht in Git, Screenshots oder Support-Nachrichten
kopiert werden.

## Kompatibilität

Eine vorhandene `/etc/styles4dogs/notification.env` bleibt als
Migrations-Fallback unterstützt. Sobald der Admin eine Verbindung speichert
oder ausdrücklich deaktiviert, hat die verwaltete Konfiguration Vorrang. Eine
Deaktivierung fällt nicht unbemerkt auf alte Umgebungsvariablen zurück.

## Datenbankmigration

Das Schema wird idempotent auf Version 6 erweitert:

```text
PRAGMA user_version = 6
```

Neu ist die Tabelle `notification_templates`. Außerdem akzeptiert
`notification_jobs.booking_id` für Testmails `NULL`, und die Ereignistypen
`admin_new_booking` sowie `smtp_test` werden unterstützt. Bestehende Jobs,
Buchungen und Einstellungen bleiben erhalten.
