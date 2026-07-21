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

## Kalenderschema Version 4

Die Kalenderphasen erweitern die Datenbank inzwischen auf
`PRAGMA user_version = 4`. Bestehende Buchungszeilen bleiben sichtbar und
werden ohne konkreten Kalendertermin als `decision_status = 'legacy'`
behandelt. Zusätzlich werden strukturierte Kontaktfelder,
Leistungssnapshots und die Einstellung für automatische Bestätigung idempotent
ergänzt. Neue Tabellen für Leistungen, Einstellungen, Wochenöffnungszeiten
und Sperrzeiten werden weiterhin ohne Datenverlust angelegt. Details stehen in
`CALENDAR_PHASE1.md` bis `CALENDAR_PHASE4.md`.

Vor der Aktualisierung einer produktiven Installation muss ein geprüftes
SQLite-Backup erstellt werden. Nach der Installation lässt sich die Version so
kontrollieren:

```bash
sqlite3 /var/lib/styles4dogs/styles4dogs.db 'PRAGMA user_version;'
```

Erwartet wird `4`.
