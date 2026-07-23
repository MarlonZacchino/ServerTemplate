# Styling 4 Dogs Server

Produktionsnaher C11-Webserver für die Website, Buchungsverwaltung,
Kalendersteuerung, Galerie, Kundenportal und E-Mail-Benachrichtigungen von
Styling 4 Dogs.

## Projektstruktur

```text
include/styles4dogs/   Öffentliche Modul-Schnittstellen
src/app/               Startpunkte für Server und Notification-Worker
src/admin/             Geschützte Admin-Oberflächen
src/booking/           Buchungsworkflow, Persistenz und Kundenportal
src/calendar/          Kalender, Verfügbarkeit und Zeitberechnung
src/core/              Laufzeitkonfiguration
src/gallery/           Galerieverwaltung
src/http/              HTTP-Hilfen, Formulardecoding und Routing
src/notifications/     Queue, SMTP-Einstellungen und Vorlagen
src/security/          Authentifizierung und Rate Limiting
src/services/          Kontakt-, Validierungs- und externe Hilfsdienste
public/                 Statische Website-Dateien
deploy/                 Installation, systemd, Caddy, Backup und Restore
tests/                  Engine-, HTTP- und Deploymenttests
fuzzing/                AFL++-Ziele und Testkorpora
docs/operations/        Betriebs- und Deploymentdokumentation
docs/history/           Historische Entwicklungsphasen
tools/                  Eigenständige Administrationsprogramme
```

## Bauen

```bash
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target Server notification_worker
```

## Testen

```bash
./tests/pewpewlaz0rt4nk/run.sh
./deploy/tests/test_deployment.sh
```

## API-Dokumentation

Alle Funktionen in `include/styles4dogs/` sind mit Doxygen-Kommentaren
versehen. Wenn Doxygen installiert ist, erzeugt CMake automatisch das Ziel:

```bash
cmake --build cmake-build-debug --target documentation
```

Die HTML-Dokumentation liegt danach unter
`cmake-build-debug/docs/html/index.html`.

Unter Linux kann sie direkt im Standardbrowser geöffnet werden:
```bash
xdg-open cmake-build-debug/docs/html/index.html
```
