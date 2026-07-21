# Kalender Phase 4: Terminentscheidungen und flexible Kontaktwege

Phase 4 schließt den ersten vollständigen Buchungsworkflow ab. Kundinnen und
Kunden wählen einen freien Slot und einen Kontaktweg. Je nach
Kalenderkonfiguration wird der Termin sofort bestätigt oder als vorläufige
Anfrage gespeichert, die der Admin annimmt oder ablehnt.

## Terminentscheidung

Offene Kalenderbuchungen besitzen den Status `pending` und blockieren den
gewählten Zeitraum bis zum konfigurierten Ablaufzeitpunkt. Im Adminbereich
stehen dafür zwei geschützte Aktionen bereit:

```text
POST /admin/bookings/accept
POST /admin/bookings/reject
```

Eine Annahme setzt den Status transaktional auf `confirmed`, entfernt die
Ablauffrist und speichert den Entscheidungszeitpunkt. Eine Ablehnung setzt den
Status auf `rejected`, speichert optional einen Ablehnungsgrund und gibt den
Slot sofort wieder frei. Bereits entschiedene oder abgelaufene Anfragen liefern
`409 Conflict`.

Vor dem Rendern der Adminübersicht werden abgelaufene Pending-Anfragen
aktualisiert. Dadurch werden für nicht mehr offene Anfragen keine
Entscheidungsschaltflächen angezeigt.

## Automatische Bestätigung

Unter `/admin/calendar` kann der Admin die Option
`auto_confirm_bookings` aktivieren. Bei aktivierter Option wird ein zum
Absendezeitpunkt weiterhin freier Slot innerhalb derselben
`BEGIN IMMEDIATE`-Transaktion direkt als `confirmed` gespeichert.

Die öffentliche Leistungs-API meldet den aktuellen Modus:

```json
{"automatic_confirmation": true}
```

Das Frontend passt den Hinweis und den Text der Absende-Schaltfläche daran an.
Diese Funktion bestätigt den Termin im Buchungssystem und auf der
Antwortseite. Ein automatischer E-Mail-, SMS- oder WhatsApp-Versand ist noch
nicht enthalten und benötigt später einen gesonderten Versanddienst.

## Kontaktwege

Das Buchungsformular unterstützt zwei eindeutige Varianten:

- E-Mail-Adresse
- Telefon- oder Mobilfunknummer

Bei einer Telefonnummer wird zusätzlich gespeichert:

- Festnetz oder Mobilfunk
- gewünschte Rückmeldung per Anruf oder WhatsApp

WhatsApp kann nur zusammen mit einer Mobilfunknummer gewählt werden. Nicht zur
gewählten Kontaktart gehörende Felder werden sowohl im Browser deaktiviert als
auch serverseitig abgelehnt. Für bestehende Altanfragen bleibt das bisherige
Feld `contact` erhalten.

## Leistungsverwaltung

Der Admin kann Leistungen hinzufügen, bearbeiten, deaktivieren und löschen:

```text
POST /admin/calendar/service/add
POST /admin/calendar/service
POST /admin/calendar/service/delete
```

Eine noch nie verwendete Leistung wird vollständig gelöscht. Wurde sie bereits
in einer Buchung verwendet, wird sie stattdessen archiviert (`active = 0`).
Damit bleiben historische Buchungen nachvollziehbar.

Neue Kalenderbuchungen speichern zusätzlich einen Snapshot aus Name, Dauer und
Pufferzeit der Leistung. Spätere Änderungen an der Leistung verändern daher
keinen bestehenden Termin.

## Datenbankschema Version 4

Die Migration auf `PRAGMA user_version = 4` ergänzt:

- `calendar_settings.auto_confirm_bookings`
- strukturierte Kontaktfelder in `bookings`
- Leistungssnapshots in `bookings`

Alle neuen Spalten werden idempotent ergänzt. Bestehende Buchungen behalten den
Kontaktkanal `legacy` und bleiben weiterhin sichtbar.

## Uhrzeitauswahl

Die freien Uhrzeiten erscheinen als einspaltige, vertikal scrollbare Liste mit
großen Schaltflächen. Nur tatsächlich freie Slots werden angezeigt. Beim
Wechsel von Tag oder Leistung wird eine frühere Auswahl verworfen und der Slot
beim Absenden nochmals transaktional geprüft.

## Sicherheitsmerkmale

- Basic Auth für alle Adminaktionen
- CSRF-Schutz für jede schreibende Adminroute
- Admin-Rate-Limit
- vorbereitete SQLite-Anweisungen
- transaktionale Terminentscheidungen
- erneute serverseitige Slotprüfung beim Buchen
- HTML-Escaping im Adminbereich
- keine personenbezogenen Daten in der öffentlichen Verfügbarkeits-API
- keine automatische Weitergabe an WhatsApp oder andere Drittanbieter durch den Server
