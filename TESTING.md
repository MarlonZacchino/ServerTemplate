# Tests und Fuzzing

Die übernommenen PSE-Werkzeuge wurden auf den aktuellen Styling-4-Dogs-Server
umgestellt. Produktionsdaten und Tests sind voneinander getrennt.

## HTTP-Regressionsprüfungen

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

Der Testlauf baut und startet einen eigenen Server, verwendet isolierte
Admin-Dateien, SQLite-Datenbanken und TSV-Importdateien und beendet den Prozess anschließend automatisch.
Die Testpfade und der abweichende Port `31338` werden ausschließlich zur
Laufzeit über Umgebungsvariablen gesetzt. Zusätzlich werden ungültiger Port,
ungültige Bind-Adresse und fehlender Document Root als Startfehler geprüft.
Neben den öffentlichen Routen werden auch der authentifizierte Admin-Workflow,
CSRF-Schutz, persistente Statusänderungen, Admin-Suche und Statusfilter sowie
per-IP- und globale Rate-Limits geprüft. Der öffentliche Kalender wird über
JSON-Endpunkte, Slot-Anzeige, Pending-Reservierung und einen echten
Doppelbuchungsversuch geprüft. Gefälschte `X-Forwarded-For`-Header
ohne gültiges Proxy-Token sind ebenfalls Teil des Regressionstests. Details stehen in
`tests/pewpewlaz0rt4nk/README.md`.

## AFL++

```bash
./fuzzing/afl/build.sh
./fuzzing/afl/run.sh
```

Für mehrere Instanzen:

```bash
./fuzzing/afl/run_parallel.sh
```

Details, Crash-Replay und Sanitizer-Build stehen in
`fuzzing/afl/README.md`.

## Empfohlener Ablauf vor einem Release

1. normales Debug-Build erstellen,
2. PewPewLaz0rTank vollständig ausführen,
3. ASan/UBSan-Testlauf ausführen,
4. Valgrind für ausgewählte Requests verwenden,
5. AFL++ über einen längeren Zeitraum laufen lassen,
6. reproduzierbare AFL-Funde als feste Regressionstests ergänzen.

## Deployment-Skripte

Syntax, systemd-Units, Staging-Installation sowie SQLite-Backup und Restore:

```bash
./deploy/tests/test_deployment.sh
```

Der Test installiert ausschließlich in ein temporäres Verzeichnis und verändert
weder `/etc` noch `/opt`, `/var/lib` oder laufende systemd-Dienste.

## Caddy-Deployment

Der Staging-Test prüft zusätzlich die optionale Caddy-Installation, ohne
`/etc/caddy`, systemd oder einen laufenden Proxy zu verändern:

```bash
./deploy/tests/test_deployment.sh
```

Auf einem tatsächlich installierten System wird die Proxy-Konfiguration mit
folgendem Befehl geprüft:

```bash
sudo ./deploy/scripts/verify-caddy.sh
```

## Kalender-Engine

Der normale PewPew-Runner baut und startet vor den HTTP-Tests zusätzlich den
isolierten C-Test `calendar_engine_tests`:

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

Er prüft Schema-Version 7, Legacy-Migration, sichere geschlossene Defaults,
Leistungsdauer und Puffer, Hinzufügen und Löschen von Leistungen,
Wochenöffnungszeiten, Sperrzeiten, Buchungshorizont, Mindestvorlauf,
ablaufende Pending-Reservierungen, Annahme und Ablehnung, automatische
Bestätigung sowie den transaktionssicheren Doppelbuchungsschutz.

Die Verfügbarkeitsengine besitzt außerdem ein eigenes AFL-Ziel:

```bash
cd fuzzing/afl
make calendar-build
make calendar-fresh
```

Auswertung:

```bash
make calendar-crashes
make calendar-hangs
```


## Öffentlicher Kalender

Der HTTP-Testlauf prüft zusätzlich:

- aktive Leistungen über `/api/services`,
- validierte Monatsabfragen über `/api/availability`,
- freie und belegte Slotdarstellung ohne personenbezogene Daten,
- die Speicherung einer `pending`-Terminanfrage,
- die sofortige Blockierung überlappender Slots,
- `409 Conflict` bei einem erneuten Buchungsversuch,
- die statische Auslieferung von `calendar.js`.


## Kalender Phase 3

Der Stateful-Test `Admin verwaltet Öffnungszeiten, Leistungen und Sperrzeiten` prüft:

- geschützten Zugriff auf `/admin/calendar`
- CSRF-Ablehnung bei Kalenderänderungen
- Speichern von Mindestvorlauf, Horizont, Zeitraster und Pending-Frist
- transaktionales Ersetzen eines einzelnen Wochentags
- Ablehnung überlappender Öffnungszeiten ohne Datenverlust
- Änderung und Deaktivierung von Leistungen
- sofortige Auswirkung auf `/api/services` und `/api/availability`
- Eintragen einer ganztägigen Sperrzeit
- Blockierung aller Slots am gesperrten Tag
- Löschen der Sperrzeit und erneute Freigabe der Slots
- sichere Redirects mit `303 See Other`

## Kalender Phase 4

Zusätzlich werden geprüft:

- strukturierte E-Mail- und Telefonkontakte
- Mobilfunk mit gewünschter WhatsApp-Rückmeldung
- Ablehnung von WhatsApp zusammen mit einer Festnetznummer
- Annehmen und Ablehnen offener Terminanfragen
- sofortige Freigabe eines abgelehnten Slots
- Schutz gegen wiederholte Entscheidungen
- automatische Bestätigung eines freien Termins
- Hinzufügen und Löschen unbenutzter Leistungen
- Archivierung verwendeter Leistungen im C-Engine-Test
- Speicherung historischer Leistungssnapshots
- scrollbare einspaltige Uhrzeitauswahl im Frontend

Der vollständige Lauf enthält 35 Beams. Die Stateful-Prüfungen werden zusammen mit den Kalender-, SMTP- und Worker-Tests ausgeführt.


## Kalender Phase 5

Der HTTP- und Worker-Test prüft zusätzlich:

- geschützten Zugriff auf `/admin/appointments`,
- Tages- und Wochenansicht mit konkreten Terminen,
- direkte E-Mail-, Telefon- und WhatsApp-Schnellaktionen,
- persistente `notification_jobs`,
- Bestätigungs-E-Mail nach automatischer Bestätigung,
- automatische Erinnerung für einen bevorstehenden Termin,
- ICS-Inhalte mit `VCALENDAR`, `VEVENT`, `DTSTART` und `DTEND`,
- Worker-Dry-Run ohne externen SMTP-Zugriff,
- Schema-Migration auf Version 5.


## Kalender Phase 6

Der Regressionstest prüft zusätzlich:

- geschützten Zugriff auf `/admin/notifications`,
- CSRF-Schutz für alle E-Mail-Adminaktionen,
- verschlüsselte Speicherung der SMTP-Verbindung ohne Klartextpasswort,
- Dateimodi `0600` für `notification.smtp` und `notification.key`,
- Beibehaltung des Passworts bei leerem Bearbeitungsfeld,
- Deaktivieren der Verbindung ohne Fallback auf alte Zugangsdaten,
- Testmails mit `booking_id = NULL`,
- Admin-Benachrichtigungen bei neuen Terminanfragen,
- Speichern, Rendern und Zurücksetzen aller Nachrichtenvorlagen,
- Ablehnung unbekannter oder unvollständiger Platzhalter,
- erneute Freigabe fehlgeschlagener Jobs,
- Schema-Migration auf Version 6.

Die Testumgebung verwendet ein eigenes temporäres Secrets-Verzeichnis. Echte
SMTP-Zugangsdaten oder produktive Dateien werden dabei nicht gelesen.

## Kalender Phase 7

Der Regressionstest prüft zusätzlich:

- das gemeinsame Speichern von Buchungsregeln, sieben Wochentagen und allen
  vorhandenen Leistungen in einer Transaktion,
- den nachlaufenden Speicherknopf und die statische Auslieferung von
  `admin-calendar.js`,
- verständlich getrennte Bezeichnungen für Mindestabstand und Freihaltezeit,
- die Tages-, Wochen- und 30-Tage-Ansicht,
- deutsches Datumsformat mit ausgeschriebenem Wochentag,
- eindeutig markierte Termine und Sperrzeiten samt Legende,
- das Honeypot-Verhalten ohne Datenbankeintrag,
- höchstens drei Anfragen desselben Kontakts innerhalb von 24 Stunden,
- `429 Too Many Requests` und `Retry-After: 86400` beim Kontaktlimit,
- Zurücksetzen gesendeter sowie Löschen fehlgeschlagener Mailjobs.

Der vollständige HTTP-Lauf enthält jetzt 40 Beams. Das Datenbankschema wird
mit Phase 8 auf Version 7 erweitert.


## Galerie Phase 8

Der Regressionstest prüft zusätzlich:

- öffentliche Route `/galerie` und `gallery.js`,
- geschützten Zugriff auf `/admin/gallery`,
- Multipart-Upload mit Admin-Authentifizierung und CSRF-Schutz,
- serverseitige Erkennung eines echten PNG-Bildes,
- Speicherung der Binärdaten in SQLite,
- korrekte JSON-Ausgabe über `/api/gallery`,
- binär korrekte Bildauslieferung über `/media/...`,
- vollständiges Löschen des Galerieeintrags,
- Schema-Migration auf Version 7,
- Einbindung des Logos und des Namens „Styling 4 Dogs“.


## Kundenbereich und Dashboard – Phase 9

Der Regressionstest prüft zusätzlich:

- geschützte Route `/admin` mit zentralen Kennzahlen und Schnellzugriffen,
- persönlichen Kundenlink direkt nach einer Buchung,
- 64-stelligen, nicht erratbaren Zugriffstoken,
- `Cache-Control: no-store` und ungültige Token-Antworten,
- Schlüsseldatei `customer-portal.key` mit Modus `0600`,
- Statusanzeige im Kundenbereich,
- Absage offener oder bestätigter Buchungen,
- idempotente wiederholte Absagen,
- sofortige Freigabe des stornierten Zeitraums,
- Kundenlink in automatisch erzeugten E-Mails,
- Backup und Restore von Datenbank und Kundenbereich-Schlüssel.
