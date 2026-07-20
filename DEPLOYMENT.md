# Styles 4 Dogs – Produktionsinstallation

Dieses Deployment installiert den C-Server als eigenen, gehärteten systemd-
Dienst. Der Server lauscht ausschließlich auf `127.0.0.1:31337`. Öffentlicher
HTTPS-Verkehr wird im nächsten Schritt über Caddy weitergeleitet.

## Zielstruktur

```text
/opt/styles4dogs/bin/Server             Programm
/opt/styles4dogs/bin/styles4dogs-*      Betriebswerkzeuge
/var/www/styles4dogs/                   schreibgeschützte Website
/etc/styles4dogs/server.env             root-verwaltete Konfiguration
/etc/styles4dogs/secrets/admin.auth     vom Server erzeugtes Admin-Secret
/var/lib/styles4dogs/styles4dogs.db     SQLite-Datenbank
/var/backups/styles4dogs/               geprüfte Online-Backups
```

`server.env` und `admin.auth` liegen absichtlich nicht im selben schreibbaren
Verzeichnis. Der Server darf nur `secrets/` und `/var/lib/styles4dogs` ändern.

## Voraussetzungen auf Arch Linux

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf libsodium sqlite systemd
```

## Installation oder Upgrade

Im sauberen Projekt-Checkout:

```bash
./tests/pewpewlaz0rt4nk/run.sh
sudo ./deploy/scripts/install.sh
```

Der Installer:

1. baut einen Release-Build,
2. legt den Systembenutzer `styles4dogs` an,
3. installiert Binary und Website,
4. erhält eine bestehende `server.env`,
5. erstellt vor einem Upgrade ein SQLite-Backup,
6. installiert und startet Service sowie täglichen Backup-Timer,
7. prüft Rechte und lokale HTTP-Erreichbarkeit.

Eine vorhandene Konfiguration wird nur mit `--replace-env` ersetzt. Die alte
Datei bleibt dabei als zeitgestempeltes Backup erhalten.

## Ersteinrichtung des Adminzugangs

Solange noch keine `admin.auth` existiert:

```text
http://127.0.0.1:31337/setup/admin
```

Bei einem entfernten Server erfolgt die Einrichtung vor Caddy sicher über ein
SSH-Tunnel vom eigenen Rechner:

```bash
ssh -L 31337:127.0.0.1:31337 user@server
```

Danach im lokalen Browser die oben genannte URL öffnen. Nach erfolgreicher
Einrichtung verschwindet die Setup-Route automatisch und liefert `404`.

## Betrieb

```bash
sudo systemctl status styles4dogs
sudo journalctl -u styles4dogs -f
sudo systemctl restart styles4dogs
sudo /opt/styles4dogs/bin/styles4dogs-verify
```

Timer prüfen:

```bash
systemctl list-timers styles4dogs-backup.timer
sudo systemctl start styles4dogs-backup.service
journalctl -u styles4dogs-backup.service
```

## Manuelles Backup

Der Server kann weiterlaufen. SQLite erstellt ein konsistentes Online-Backup:

```bash
sudo /opt/styles4dogs/bin/styles4dogs-backup
```

Das Skript führt `PRAGMA integrity_check` aus und legt zusätzlich eine
SHA-256-Datei ab. Standardmäßig werden automatische Backups nach 30 Tagen
entfernt.

## Restore

Vorher den gewählten Dateinamen genau prüfen:

```bash
sudo /opt/styles4dogs/bin/styles4dogs-restore \
  --backup /var/backups/styles4dogs/styles4dogs-YYYYMMDDTHHMMSS-NNNNNNNNNZ.db \
  --yes
```

Der Restore erstellt zuerst ein Sicherheitsbackup des aktuellen Zustands,
stoppt den Dienst, ersetzt die Datenbank atomar und startet den Dienst wieder.
Falls der Dienst nicht startet, wird automatisch auf das Sicherheitsbackup
zurückgerollt.

## Deinstallation

Programm und Service entfernen, Kundendaten aber behalten:

```bash
sudo ./deploy/scripts/uninstall.sh
```

Eine vollständige, irreversible Löschung verlangt zwei ausdrückliche Angaben:

```bash
sudo env STYLES4DOGS_CONFIRM_PURGE=YES \
  ./deploy/scripts/uninstall.sh --purge-data
```

## Härtung

Die Unit verwendet unter anderem:

- eigenen Benutzer ohne Login-Shell,
- leere Capability-Menge,
- `NoNewPrivileges`,
- schreibgeschütztes System und Home-Verzeichnis,
- getrennte Schreibfreigaben für Datenbank und Secrets,
- eingeschränkte Address-Families,
- ausschließlich localhost über `IPAddressAllow`,
- begrenzte Dateideskriptoren und Tasks,
- automatischen Neustart nur bei Fehlern.

Nach Änderungen an der Unit:

```bash
sudo systemd-analyze verify /etc/systemd/system/styles4dogs.service
sudo systemctl daemon-reload
sudo systemctl restart styles4dogs
systemd-analyze security styles4dogs.service
```
