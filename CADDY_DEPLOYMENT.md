# Caddy-Reverse-Proxy für Styling 4 Dogs

Der C-Server bleibt ausschließlich unter `127.0.0.1:31337` erreichbar. Caddy
übernimmt die öffentliche Verbindung, automatische TLS-Zertifikate,
Komprimierung, Sicherheitsheader, Body-Limits und datensparsame Zugriffslogs.

## Warum der offizielle Caddy-Build verwendet wird

Die Arch-Linux-Paketversion von Caddy enthält kein HTTP-Rate-Limit-Modul. Die
verfügbaren Rate-Limit-Module sind nicht Bestandteil des offiziellen Builds.
Deshalb verwendet dieses Deployment bewusst keine fremde Caddy-Erweiterung.
Deshalb liegt das eigentliche Rate-Limit reproduzierbar im C-Server. Caddy
übernimmt weiterhin Body-Limits und sendet die Client-IP nur zusammen mit einem
automatisch erzeugten Proxy-Secret an den lokalen Backendprozess. Details zu
den Limits und zur Vertrauensprüfung stehen in `RATE_LIMITING.md`.

Zusammen schützen:

- der ausschließlich lokale Upstream,
- das interne per-IP- und globale Rate-Limit,
- ein Body-Limit von 16 KiB für `/booking`,
- ein Body-Limit von 64 KiB für normale Adminaktionen,
- ein separates Limit von 9 MiB für Galerie-Uploads,
- die öffentlich vollständig gesperrte Setup-Route.

## Installation auf Arch Linux

```bash
sudo pacman -S --needed caddy curl
sudo ./deploy/scripts/install-caddy.sh
```

Der Standard ist ein lokaler Smoke-Test unter:

```text
http://127.0.0.1:8080
```

Dabei bleibt der direkte C-Server weiterhin unter `127.0.0.1:31337` aktiv.

## Öffentliche Domain aktivieren

Voraussetzungen:

1. A- und gegebenenfalls AAAA-DNS-Einträge zeigen auf den Server.
2. TCP-Port 80 und TCP/UDP-Port 443 sind von außen erreichbar.
3. Kein anderer Dienst belegt diese Ports.

Danach zum Beispiel:

```bash
sudo ./deploy/scripts/install-caddy.sh \
  --site-address styles4dogs.example
```

Caddy aktiviert für einen öffentlichen DNS-Namen automatisch HTTPS und leitet
HTTP auf HTTPS um. Für den finalen Betrieb muss `styles4dogs.example` durch die
echte Domain ersetzt werden.

## Sicherheitsverhalten

Die installierte Konfiguration:

- gibt `/setup/admin` öffentlich immer als `404` zurück,
- lässt Basic Auth des Adminbereichs unverändert durch,
- setzt eine restriktive Content-Security-Policy,
- setzt HSTS, `nosniff`, Frame- und Referrer-Schutz,
- entfernt den `Server`-Antwortheader,
- komprimiert geeignete Antworten mit Zstandard oder gzip,
- begrenzt Formulargrößen vor dem C-Server,
- maskiert IP-Adressen in Zugriffslogs,
- ersetzt den Admin-Suchparameter `q` im Log durch `REDACTED`,
- protokolliert Zugangsdaten nicht im Klartext,
- überschreibt den internen Proxy-Token-Header vor der Weiterleitung.

Die HSTS-Antwort wird auch im lokalen HTTP-Test konfiguriert, von Browsern aber
nur über eine sichere HTTPS-Verbindung berücksichtigt.

## Dateien

```text
/etc/caddy/Caddyfile
/etc/caddy/conf.d/styles4dogs.caddy
/etc/styles4dogs/caddy.env
/etc/systemd/system/caddy.service.d/styles4dogs.conf
/var/log/caddy/styles4dogs-access.log
```

Der Installer ersetzt die globale Caddy-Konfiguration nicht. Existiert bereits
ein Import für `/etc/caddy/conf.d/*` oder `*.caddy`, wird dieser wiederverwendet.
Nur wenn kein passender Import existiert, ergänzt der Installer einen markierten
Block. Außerdem legt er die Access-Logdatei vor der Validierung mit Besitzer
`caddy:caddy` und Modus `0640` an. Vor Änderungen wird ein zeitgestempeltes
Backup des Caddyfile erstellt.

## Prüfung

```bash
sudo ./deploy/scripts/verify-caddy.sh
sudo systemctl status caddy
sudo journalctl -u caddy -f
```

Bei lokaler Standardkonfiguration prüft das Skript zusätzlich:

- Startseite über Caddy liefert `200`,
- `/setup/admin` liefert `404`,
- `/admin/bookings` liefert `401`,
- Sicherheitsheader sind vorhanden,
- ein übergroßer Buchungsbody liefert `413`,
- Galerie-Uploads sind auf 9 MiB inklusive Multipart-Overhead begrenzt.

## Admin-Ersteinrichtung

Die Setup-Route ist absichtlich nicht über Caddy erreichbar. Lokal auf dem
Server oder über einen SSH-Tunnel wird weiterhin direkt der Backend-Port
verwendet:

```bash
ssh -L 31337:127.0.0.1:31337 user@server
```

Dann im lokalen Browser:

```text
http://127.0.0.1:31337/setup/admin
```

## Entfernen der Integration

```bash
sudo ./deploy/scripts/uninstall-caddy.sh
```

Das entfernt nur die Styling-4-Dogs-Site, ihre Environment-Datei und den
systemd-Drop-in. Andere Caddy-Sites und das Paket bleiben erhalten.

Caddy zusätzlich deaktivieren:

```bash
sudo ./deploy/scripts/uninstall-caddy.sh --disable-service
```

## Schutz persönlicher Buchungslinks

Pfade unter `/buchung/*` enthalten einen geheimen Zugriffstoken. Die verwaltete
Caddy-Konfiguration schließt diese Requests mit `log_skip` aus dem Access-Log
aus und setzt `Referrer-Policy: no-referrer`. Nach einem Phase-9-Update muss die
Caddy-Konfiguration deshalb erneut installiert werden.
