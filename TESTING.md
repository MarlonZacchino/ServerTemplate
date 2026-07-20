# Tests und Fuzzing

Die übernommenen PSE-Werkzeuge wurden auf den aktuellen Styles-4-Dogs-Server
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
per-IP- und globale Rate-Limits geprüft. Gefälschte `X-Forwarded-For`-Header
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
