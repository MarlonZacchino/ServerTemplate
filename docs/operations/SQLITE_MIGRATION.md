# Migration der Buchungen auf SQLite

Standardmäßig speichert der Server neue Buchungsanfragen in
`data/styles4dogs.db`. Über `STYLES4DOGS_DATA_DIR` und
`STYLES4DOGS_DATABASE_FILE` kann der Pfad ohne neuen Build geändert werden.
Eine vorhandene, über `STYLES4DOGS_LEGACY_BOOKING_FILE` konfigurierte
`data/bookings.txt` wird beim ersten erfolgreichen Start
transaktional importiert. Unterstützt werden das frühere fünfspaltige Format
und das versionierte TSV-v2-Format.

## Vor der ersten Ausführung

Server beenden und die Laufzeitdaten sichern:

```bash
cp -a data "data.backup-$(date +%Y%m%d-%H%M%S)"
```

Unter Arch Linux wird SQLite über folgendes Paket bereitgestellt:

```bash
sudo pacman -S --needed sqlite
```

Danach das Projekt neu konfigurieren und bauen:

```bash
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

## Erster Start

```bash
./cmake-build-debug/Server
```

Beim Start werden Schema und Indizes angelegt. Existiert `data/bookings.txt`
und wurde sie noch nicht importiert, erfolgt der Import innerhalb einer
SQLite-Transaktion. Bei einer fehlerhaften TSV-Zeile startet der Server nicht
und es wird kein Teilimport übernommen.

Die alte TSV-Datei wird nicht gelöscht oder überschrieben. Der erfolgreiche
Import wird in `app_metadata` markiert und bei späteren Starts nicht wiederholt.

## Kontrolle

```bash
sqlite3 data/styles4dogs.db \
  "SELECT COUNT(*) AS bookings FROM bookings;"

sqlite3 data/styles4dogs.db \
  "SELECT key, value FROM app_metadata;"

stat -c '%a %n' data data/styles4dogs.db
```

Erwartete Berechtigungen:

```text
750 data
600 data/styles4dogs.db
```

Nach erfolgreicher Kontrolle kann `bookings.txt` außerhalb des aktiven
Datenordners archiviert werden. Sie sollte nicht gelöscht werden, bevor ein
Backup und eine Sichtprüfung des Adminbereichs erfolgt sind.

## Backup

Für den derzeit einzelnen Serverprozess reicht eine konsistente Dateikopie bei
gestopptem Server:

```bash
systemctl stop styles4dogs
cp -a data/styles4dogs.db "/sicherer/pfad/styles4dogs-$(date +%Y%m%d-%H%M%S).db"
systemctl start styles4dogs
```

Vor dem Produktivbetrieb wird daraus noch ein automatisierter Backup- und
Restore-Test erstellt.

## Kalenderschema Version 6

Phase 6 erweitert die Datenbank idempotent auf:

```text
PRAGMA user_version = 6
```

Neu angelegt wird:

```text
notification_templates
```

Die Tabelle enthält die individualisierbaren Betreff- und Textvorlagen für
Eingangsbestätigung, Terminbestätigung, Absage, Erinnerung und interne
Admin-Benachrichtigung.

`notification_jobs.booking_id` darf ab Version 6 für eine reine SMTP-Testmail
`NULL` sein. Zusätzlich werden die Ereignisse `admin_new_booking` und
`smtp_test` unterstützt. Eine bestehende Version-5-Tabelle wird innerhalb der
Schema-Transaktion umgebaut; vorhandene Jobs, Buchungen und Kalenderdaten
bleiben erhalten.

Vor jeder produktiven Aktualisierung muss ein geprüftes SQLite-Backup erstellt
werden. Nach der Installation:

```bash
sqlite3 /var/lib/styles4dogs/styles4dogs.db 'PRAGMA user_version;'
sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT name FROM sqlite_master WHERE type='table' AND name IN ('notification_jobs','notification_templates') ORDER BY name;"
sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT event_type FROM notification_templates ORDER BY event_type;"
```

Erwartet werden Version `6`, beide Tabellen und fünf Nachrichtenvorlagen.
SMTP-Zugangsdaten liegen nicht in SQLite. Details stehen in
[`CALENDAR_PHASE6.md`](../history/CALENDAR_PHASE6.md) und [`NOTIFICATIONS.md`](NOTIFICATIONS.md).


## Galerieschema Version 7

Phase 8 erweitert die Datenbank idempotent auf:

```text
PRAGMA user_version = 7
```

Neu angelegt wird:

```text
gallery_images
```

Die Tabelle enthält Titel, Alternativtext, Sichtbarkeit, Sortierreihenfolge,
MIME-Type und die Bilddaten als SQLite-BLOB. Damit werden Galeriebilder durch
die vorhandenen SQLite-Online-Backups automatisch mitgesichert und bei einer
Wiederherstellung zusammen mit den übrigen Salon-Daten zurückgespielt.

Prüfung nach der Installation:

```bash
sqlite3 /var/lib/styles4dogs/styles4dogs.db 'PRAGMA user_version;'
sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT name FROM sqlite_master WHERE type='table' AND name='gallery_images';"
```

## Adressschema Version 8

Phase 10 erweitert die Datenbank idempotent auf:

```text
PRAGMA user_version = 8
```

Die Tabelle `bookings` erhält die Felder `street_address`, `postal_code` und
`city`. Bestehende Datensätze und importierte Legacy-Buchungen erhalten leere
Werte; neue öffentliche Terminanfragen benötigen eine vollständige, serverseitig
validierte Adresse.

Prüfung nach der Installation:

```bash
sqlite3 /var/lib/styles4dogs/styles4dogs.db 'PRAGMA user_version;'
sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT name FROM pragma_table_info('bookings') WHERE name IN ('street_address','postal_code','city') ORDER BY name;"
```

Erwartet werden Version `8` sowie die drei Spalten `city`, `postal_code` und
`street_address`. Vor jedem produktiven Upgrade bleibt ein geprüftes Backup
verpflichtend.

## Buchungsworkflow Version 9

Phase 13 erweitert die Datenbank idempotent auf:

```text
PRAGMA user_version = 9
```

Die Tabelle `bookings` erhält `dog_breed`. Bestehende Datensätze bekommen
einen leeren Wert; neue öffentliche Buchungen speichern einen validierten
Rassecode. Die Statuswerte werden um `bestätigt`, `abgelehnt` und `abgesagt`
erweitert. Terminentscheidungen synchronisieren den sichtbaren Buchungsstatus,
und bestätigte Termine werden vier Stunden nach ihrem Terminende automatisch
auf `erledigt` gesetzt.

Prüfung nach der Installation:

```bash
sqlite3 /var/lib/styles4dogs/styles4dogs.db 'PRAGMA user_version;'
sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT name FROM pragma_table_info('bookings') WHERE name='dog_breed';"
```

Erwartet werden Version `9` und die Spalte `dog_breed`. Die Migration der
Rassespalte läuft bereits beim Öffnen der Buchungsdatenbank, damit auch ein
noch ausstehender Legacy-TSV-Import nicht auf einem älteren Schema scheitert.
