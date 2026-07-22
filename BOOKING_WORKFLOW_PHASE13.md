# Buchungsworkflow – Phase 13

Phase 13 erweitert den öffentlichen Buchungsprozess, die Adminverwaltung und
das E-Mail-System um einen durchgängigen Statusworkflow.

## Öffentliche Buchung

Das Kontaktformular verlangt zusätzlich eine Hunderasse. Die Auswahl wird als
stabiler Code in `bookings.dog_breed` gespeichert und im Adminbereich mit einer
deutschen Bezeichnung dargestellt. Bei `Sonstiges` blendet JavaScript einen
Hinweis ein, die genaue Rasse im Feld „Worum geht es?“ zu ergänzen. Die
serverseitige Prüfung akzeptiert ausschließlich die vorgegebenen Codes.

## Statusmodell

| Status | Bedeutung |
|---|---|
| `neu` | Neue, noch nicht bearbeitete Anfrage |
| `kontaktiert` | Salon hat Kontakt aufgenommen |
| `bestätigt` | Termin wurde automatisch oder manuell bestätigt |
| `abgelehnt` | Anfrage wurde nicht angenommen oder ist abgelaufen |
| `abgesagt` | Zuvor reservierter oder bestätigter Termin wurde storniert |
| `erledigt` | Termin ist abgeschlossen |
| `altbestand` | Importierter Datensatz ohne vollständigen Kalenderworkflow |

Kalenderentscheidung und sichtbarer Buchungsstatus werden innerhalb derselben
SQLite-Transaktion geändert. Dadurch kann eine bestätigte Kalenderreservierung
nicht mehr als `neu` in der Buchungsverwaltung stehen bleiben.

Bestätigte Termine werden ab vier Stunden nach ihrem Terminende automatisch
auf `erledigt` gesetzt. Die Pflege läuft beim Aufruf der Buchungsverwaltung und
im minütlichen Notification-Worker. Sie ist nicht vom E-Mail-Hauptschalter
abhängig.

## Admin-Buchungsverwaltung

`/admin/bookings` zeigt pro Status einen eigenen `<details>`-Bereich mit Zähler.
`neu`, `kontaktiert` und `bestätigt` sind standardmäßig geöffnet;
`abgelehnt`, `abgesagt`, `erledigt` und `altbestand` bleiben zunächst
geschlossen. Suche und Filter gelten weiterhin serverseitig für alle Bereiche.

## E-Mail-Hauptschalter

`/admin/notifications` besitzt einen globalen Hauptschalter. Beim Pausieren:

- bleiben SMTP-Zugangsdaten und Vorlagen gespeichert,
- werden vorhandene Queue-Einträge nicht gelöscht,
- versendet der Worker keine Nachrichten,
- läuft die automatische Statuspflege weiter.

Die verwaltete, verschlüsselte SMTP-Konfiguration verwendet Payload-Version 2.
Payload-Version 1 wird weiterhin gelesen und beim nächsten Speichern automatisch
aktualisiert. Für Environment-basierte Installationen steht zusätzlich
`STYLES4DOGS_NOTIFICATIONS_ENABLED=0|1` zur Verfügung.

`{{customer_name}}` enthält den in der Buchung gespeicherten vollständigen
Kundennamen aus Vor- und Nachname.

## Datenbankmigration

Das Schema wird idempotent auf Version 9 erweitert. `dog_breed` wird bereits
beim Initialisieren der Buchungsdatenbank ergänzt, bevor ein eventuell noch
ausstehender Legacy-TSV-Import beginnt. Dadurch ist das Upgrade auch für ältere
Installationen sicher.

Vor dem produktiven Upgrade ist weiterhin ein geprüftes SQLite-Backup Pflicht.
