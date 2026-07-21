# E-Mail-Bestätigungen und Erinnerungen

Der HTTP-Server versendet keine E-Mail direkt. Er legt einen Job in
`notification_jobs` ab. `/opt/styles4dogs/bin/notification_worker` liest diese
Queue, versendet über SMTP und wird durch
`styles4dogs-notification.timer` alle fünf Minuten gestartet.

## SMTP konfigurieren

Datei:

```text
/etc/styles4dogs/notification.env
```

Beispiel mit STARTTLS auf Port 587:

```text
STYLES4DOGS_SMTP_URL=smtp://smtp.example.de:587
STYLES4DOGS_SMTP_USERNAME=termine@example.de
STYLES4DOGS_SMTP_PASSWORD=starkes-anwendungspasswort
STYLES4DOGS_SMTP_FROM_ADDRESS=termine@example.de
STYLES4DOGS_SMTP_FROM_NAME=Styles 4 Dogs
```

Beispiel mit implizitem TLS auf Port 465:

```text
STYLES4DOGS_SMTP_URL=smtps://smtp.example.de:465
```

Die Datei muss `root:styles4dogs` gehören und Modus `0640` besitzen. Sie darf
nicht in Git eingecheckt werden.

## Saloninformationen

In `/etc/styles4dogs/server.env` werden für E-Mails und ICS-Dateien gesetzt:

```text
STYLES4DOGS_SALON_NAME=Styles 4 Dogs
STYLES4DOGS_SALON_ADDRESS=Musterstraße 1, 48369 Saerbeck
STYLES4DOGS_SALON_PHONE=+49 2574 123456
STYLES4DOGS_PUBLIC_BASE_URL=https://example.de
STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE=49
```

## Aktivieren

1. SMTP-Zugangsdaten eintragen.
2. Im Adminbereich `/admin/calendar` E-Mail-Benachrichtigungen aktivieren.
3. Erinnerungen und Vorlauf festlegen.
4. Timer neu starten:

```bash
sudo systemctl restart styles4dogs-notification.timer
sudo systemctl start styles4dogs-notification.service
```

## Kontrolle

```bash
systemctl status styles4dogs-notification.timer
journalctl -u styles4dogs-notification.service --since today
```

Warteschlange prüfen, ohne Nachrichtentexte auszugeben:

```bash
sudo sqlite3 /var/lib/styles4dogs/styles4dogs.db \
  "SELECT status, event_type, COUNT(*) FROM notification_jobs GROUP BY status, event_type;"
```

## Dry-Run ohne SMTP

In einer Entwicklungsumgebung schreibt der Worker die erzeugten Nachrichten
ohne SMTP-Zugriff in ein lokales Verzeichnis:

```bash
rm -rf ./notification-out
STYLES4DOGS_DATA_DIR="$PWD/data" \
  ./cmake-build-debug/notification_worker --dry-run ./notification-out
```

Der vollständige Regressionstest erzeugt Bestätigung, Erinnerung und ICS-Datei
in einer vollständig isolierten Testdatenbank:

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

## Wiederholungen

Ein Job wird höchstens fünfmal versucht. Die Wartezeiten steigen schrittweise.
Ein unterbrochener Worker gibt einen länger als 15 Minuten als `processing`
markierten Job automatisch wieder frei. Die Kombination aus Buchung und
Ereignistyp ist eindeutig, sodass Bestätigung und Erinnerung nicht doppelt in
die Warteschlange gelangen.
