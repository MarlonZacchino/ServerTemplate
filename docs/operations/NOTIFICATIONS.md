# E-Mail-Verbindung, Vorlagen und Versandwarteschlange

Der HTTP-Server sendet E-Mails nicht während einer Buchungsanfrage. Er erzeugt
einen Job in `notification_jobs`. Der separate
`/opt/styles4dogs/bin/notification_worker` verarbeitet die Queue über SMTP und
wird von `styles4dogs-notification.timer` jede Minute gestartet. Derselbe Lauf pflegt außerdem überfällige Buchungsstatus. Eine
Buchung bleibt dadurch auch erhalten, wenn der Mailanbieter vorübergehend nicht
erreichbar ist.

## Postfach im Adminbereich verbinden

Geschützte Seite:

```text
/admin/notifications
```

Benötigt werden gewöhnlich:

```text
SMTP-Adresse:       smtps://smtp.anbieter.de:465
oder STARTTLS:      smtp://smtp.anbieter.de:587
Benutzername:       häufig die vollständige E-Mail-Adresse
Passwort:           vorzugsweise ein separates App-Passwort
Absenderadresse:    sichtbare Salon-Adresse
Absendername:       Styling 4 Dogs
Admin-Adresse:      Ziel für neue Terminanfragen und Kundenabsagen
```

Nach dem Speichern:

1. eine Testmail einreihen,
2. den Worker einmal manuell starten,
3. das Journal und den Posteingang prüfen,
4. unter `/admin/calendar` Kunden-E-Mails und Erinnerungen aktivieren.

```bash
sudo systemctl start styles4dogs-notification.service
sudo journalctl -u styles4dogs-notification.service --since "10 minutes ago" --no-pager
```

Die Testmail wird wie alle Nachrichten über die persistente Warteschlange
verschickt. Der Browser zeigt das SMTP-Passwort nach dem Speichern niemals
wieder an. Ein leeres Passwortfeld behält beim Bearbeiten das vorhandene
Passwort.

## Verschlüsselte Speicherung

Admin-verwaltete SMTP-Daten liegen nicht in SQLite und nicht im Git-Checkout:

```text
/etc/styles4dogs/secrets/notification.smtp
/etc/styles4dogs/secrets/notification.key
```

Die Konfiguration wird mit libsodium `crypto_secretbox` authentifiziert
verschlüsselt. Beide Dateien müssen dem Dienstbenutzer gehören und Modus `0600`
besitzen; das Verzeichnis bleibt `0700`.

Der Schlüssel befindet sich auf demselben Server, weil Webserver und Worker die
Daten ohne manuelle Eingabe lesen müssen. Diese Verschlüsselung verhindert
Klartext in Dateiansichten und versehentlich kopierten Konfigurationen. Sie ist
kein Schutz gegen Root-Zugriff oder einen vollständig kompromittierten
Dienstbenutzer. Für den Salon ist deshalb ein separates Postfach mit
App-Passwort und möglichst eingeschränkten Rechten vorgesehen.

## Alte Environment-Konfiguration

`/etc/styles4dogs/notification.env` bleibt für bestehende Installationen als
Fallback unterstützt:

```text
STYLES4DOGS_SMTP_URL=smtp://smtp.example.de:587
STYLES4DOGS_SMTP_USERNAME=termine@example.de
STYLES4DOGS_SMTP_PASSWORD=anwendungspasswort
STYLES4DOGS_SMTP_FROM_ADDRESS=termine@example.de
STYLES4DOGS_SMTP_FROM_NAME=Styling 4 Dogs
STYLES4DOGS_ADMIN_NOTIFICATION_EMAIL=termine@example.de
STYLES4DOGS_NOTIFY_ADMIN_NEW_BOOKING=1
STYLES4DOGS_NOTIFICATIONS_ENABLED=1
```

Für neue Installationen ist die Eingabe über `/admin/notifications` der
bevorzugte Weg. Sobald dort gespeichert oder deaktiviert wurde, hat diese
verwaltete Einstellung Vorrang und der Worker fällt nicht mehr automatisch auf
alte Environment-Zugangsdaten zurück.

## Automatische Nachrichten bearbeiten

Unter `/admin/notifications` können Betreff und Nachrichtentext separat für
folgende Ereignisse angepasst werden:

```text
booking_received       Anfrage eingegangen
booking_confirmed      Termin bestätigt
booking_rejected       Terminanfrage abgelehnt
appointment_reminder   Erinnerung
admin_new_booking      neue Anfrage für den Admin
admin_booking_cancelled Kundenabsage für den Admin
```

Erlaubte Platzhalter:

```text
{{customer_name}}      vollständiger Kundenname
{{booking_id}}         Buchungsnummer
{{appointment_date}}   Termindatum
{{start_time}}         Beginn
{{end_time}}           Ende
{{service_name}}       gebuchte Leistung
{{dog_name}}           Name des Hundes
{{rejection_reason}}   optionaler Ablehnungsgrund
{{salon_name}}         Salonname aus server.env
{{salon_address}}      Salonadresse aus server.env
{{salon_phone}}        Salontelefon aus server.env
{{website_url}}        öffentliche Basis-URL
{{booking_url}}        persönlicher Link zur Buchungsübersicht
```

Unbekannte Platzhalter, unvollständige `{{...}}`-Ausdrücke und Zeilenumbrüche
im Betreff werden abgelehnt. Jede Vorlage kann einzeln auf den mitgelieferten
Standard zurückgesetzt werden.

Bereits eingereihte Nachrichten behalten den Text, der zum Zeitpunkt der
Einreihung erzeugt wurde. Dadurch ändern spätere Bearbeitungen keine bereits
geplanten Bestätigungen oder Erinnerungen.

Kunden-E-Mails erhalten immer einen persönlichen Buchungslink. Wird
`{{booking_url}}` nicht ausdrücklich in der Vorlage verwendet, hängt das System
den Link automatisch am Ende der Nachricht an.

## Aktivierung und Erinnerungen

Die technische SMTP-Verbindung und die fachliche Versandfreigabe sind bewusst
getrennt:

- `/admin/notifications`: globaler Hauptschalter, Postfach, Testmail, Admin-Mails bei neuen Anfragen und Kundenabsagen, Vorlagen und Queue
- `/admin/calendar`: Kunden-E-Mails, Erinnerungen und Erinnerungszeitpunkt

Der Hauptschalter pausiert den Versand, ohne Zugangsdaten, Vorlagen oder
wartende Jobs zu löschen. Automatische Statuspflege – insbesondere
`bestätigt` zu `erledigt` vier Stunden nach dem Terminende – läuft unabhängig
davon weiter. Die feineren Kalendereinstellungen greifen nur, wenn das globale
E-Mail-System aktiviert ist.

## Warteschlange und Wiederholungen

Ein Job wird höchstens fünfmal automatisch versucht. Die Wartezeiten steigen
schrittweise. Ein länger als 15 Minuten als `processing` markierter Job wird
beim nächsten Worker-Lauf wieder freigegeben. Fehlgeschlagene Jobs können im
Adminbereich erneut auf `pending` gesetzt werden.

Statusübersicht ohne Nachrichtentexte:

```bash
sudo sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT status, event_type, COUNT(*) FROM notification_jobs GROUP BY status, event_type;"
```

Timer und Journal:

```bash
systemctl status styles4dogs-notification.timer --no-pager
systemctl list-timers styles4dogs-notification.timer
sudo journalctl -u styles4dogs-notification.service --since today --no-pager
```

## Dry-Run ohne externen Versand

In einer isolierten Entwicklungsumgebung erzeugt der Worker `.eml`- und
`.ics`-Dateien lokal:

```bash
rm -rf ./notification-out
STYLES4DOGS_DATA_DIR="$PWD/data" \
STYLES4DOGS_SECRETS_DIR="$PWD/.secrets" \
  ./cmake-build-debug/notification_worker --dry-run ./notification-out
```

Der vollständige Regressionstest nutzt eigene temporäre Secrets und eine
isolierte SQLite-Datenbank:

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

## Backup und Wiederherstellung

Das normale SQLite-Backup enthält:

- Buchungen,
- Benachrichtigungsjobs,
- individualisierte Nachrichtenvorlagen.

Die SMTP-Dateien und ihr Schlüssel werden bewusst nicht in das
Datenbank-Backup kopiert. Nach einem vollständigen Serververlust wird das
Postfach deshalb neu im Adminbereich verbunden. Eine separate Sicherung der
Secrets ist nur verschlüsselt und mit besonders eingeschränktem Zugriff
zulässig; Schlüssel und Chiffretext müssen dabei gemeinsam wiederhergestellt
werden.

## Zähler und Historie zurücksetzen

Die Adminseite bietet getrennte Wartungsaktionen:

- **Fehlgeschlagene erneut versuchen:** setzt endgültig fehlgeschlagene Jobs
  wieder auf `pending`.
- **Gesendet-Zähler zurücksetzen:** löscht ausschließlich bereits gesendete
  Jobs und setzt dadurch den sichtbaren Zähler zurück.
- **Fehlgeschlagene löschen:** entfernt ausschließlich Jobs mit Status
  `failed`.
- **Abgeschlossene Historie leeren:** entfernt `sent` und `failed` gemeinsam.

Jobs mit Status `pending` oder `processing` werden nie durch eine
Bereinigungsaktion entfernt. Alle Aktionen sind mit Basic Auth und CSRF-Schutz
geschützt und erfordern in der Oberfläche eine Bestätigung.
