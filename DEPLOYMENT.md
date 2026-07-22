# Styling 4 Dogs – Produktionsinstallation

Dieses Deployment installiert den C-Server als eigenen, gehärteten systemd-
Dienst. Der Server lauscht ausschließlich auf `127.0.0.1:31337`. Öffentlicher
HTTPS-Verkehr wird über Caddy weitergeleitet.

## Zielstruktur

```text
/opt/styles4dogs/bin/Server             Webserver
/opt/styles4dogs/bin/notification_worker Benachrichtigungs-Worker
/opt/styles4dogs/bin/styles4dogs-*      Betriebswerkzeuge
/var/www/styles4dogs/                   schreibgeschützte Website
/etc/styles4dogs/server.env             root-verwaltete Serverkonfiguration
/etc/styles4dogs/notification.env       optionaler SMTP-Migrations-Fallback
/etc/styles4dogs/secrets/admin.auth     vom Server erzeugtes Admin-Secret
/etc/styles4dogs/secrets/notification.* verschlüsselte SMTP-Verbindung
/var/lib/styles4dogs/styles4dogs.db     SQLite-Datenbank inklusive Galerie und Kundenanschriften
/var/backups/styles4dogs/               geprüfte Online-Backups
```

`server.env` liegt absichtlich außerhalb des schreibbaren Secret-Verzeichnisses.
Der Server darf nur `secrets/` und `/var/lib/styles4dogs` ändern. Dort erzeugt
er `admin.auth` sowie die verschlüsselte, im Adminbereich verwaltete
SMTP-Konfiguration.

## Voraussetzungen auf Arch Linux

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf libsodium sqlite curl systemd
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
5. erzeugt bei Bedarf ein zufälliges internes Proxy-Token,
6. erstellt vor einem Upgrade ein SQLite-Backup,
7. installiert Webserver, Benachrichtigungs-Worker und systemd-Units,
8. startet Backup- und Benachrichtigungs-Timer,
9. prüft Rechte und lokale HTTP-Erreichbarkeit.

Eine vorhandene Konfiguration wird nur mit `--replace-env` ersetzt. Die alte
Datei bleibt dabei als zeitgestempeltes Backup erhalten.



## Fotogalerie

Galeriebilder werden nicht lose im Document Root gespeichert, sondern als BLOB
in der SQLite-Datenbank. Die bestehenden Backup- und Restore-Werkzeuge sichern
damit Buchungen, Kalender, Benachrichtigungen und Fotos gemeinsam. Der Upload
ist im Backend auf 8 MB und in Caddy auf 9 MiB inklusive Multipart-Overhead
begrenzt.

## Caddy und HTTPS

Nach erfolgreicher Backend-Installation kann der Reverse Proxy zunächst lokal
und später mit der echten Domain eingerichtet werden:

```bash
sudo pacman -S --needed caddy curl
sudo ./deploy/scripts/install-caddy.sh
```

Der lokale Test ist danach unter `http://127.0.0.1:8080` erreichbar. Für eine
öffentliche Domain wird der Installer erneut mit `--site-address DOMAIN`
ausgeführt. Vollständige Hinweise zu DNS, HTTPS, Sicherheitsheadern und Logs
stehen in `CADDY_DEPLOYMENT.md`. Caddy und Backend erhalten dabei automatisch
dasselbe Proxy-Token, damit nur die eigene Proxy-Instanz Client-IP-Angaben
weitergeben darf. Die internen Limits sind in `RATE_LIMITING.md` beschrieben. Caddy stellt
zusätzlich einen ausschließlich auf localhost gebundenen Upstream für die
PLZ-Ort-Abfrage bereit; dessen Konfiguration ist Voraussetzung für die
Adress-Autovervollständigung im gehärteten Produktivdienst.

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
systemctl list-timers styles4dogs-backup.timer styles4dogs-notification.timer
sudo systemctl start styles4dogs-backup.service
journalctl -u styles4dogs-backup.service
journalctl -u styles4dogs-notification.service
```

## Manuelles Backup

Der Server kann weiterlaufen. SQLite erstellt ein konsistentes Online-Backup:

```bash
sudo /opt/styles4dogs/bin/styles4dogs-backup
```

Das Skript führt `PRAGMA integrity_check` aus und legt zusätzlich eine
SHA-256-Datei ab. Sobald persönliche Buchungslinks verwendet werden, wird auch
`customer-portal.key` als geprüfte Sidecar-Datei gesichert. Datenbank und
Schlüssel müssen gemeinsam aufbewahrt werden, damit bereits versendete Links
nach einem Restore gültig bleiben. Standardmäßig werden automatische Backups
nach 30 Tagen entfernt.

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


## Terminübersicht und E-Mail-Versand

Der geschützte Tages-/Wochenkalender ist erreichbar unter:

```text
/admin/appointments
```

Das Salon-Postfach wird bevorzugt unter `/admin/notifications` verbunden. Dort
stehen außerdem Testmail, Queue-Status, Wiederholungen und individualisierbare
Bestätigungs-, Absage- und Erinnerungstexte zur Verfügung. Die Zugangsdaten
liegen verschlüsselt in `/etc/styles4dogs/secrets/notification.*`.

Der Worker läuft als eigener gehärteter Oneshoot-Dienst und erhält – anders als
der Webserver – ausgehenden Netzwerkzugriff. Der Webserver bleibt weiterhin
auf localhost beschränkt. `/etc/styles4dogs/notification.env` wird nur noch als
Migrations-Fallback unterstützt. Vollständige Hinweise stehen in
`NOTIFICATIONS.md` und `CALENDAR_PHASE6.md`.

Nach Änderungen:

```bash
sudo systemctl daemon-reload
sudo systemctl restart styles4dogs
sudo systemctl restart styles4dogs-notification.timer
sudo /opt/styles4dogs/bin/styles4dogs-verify
```
