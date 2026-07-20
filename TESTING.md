# Tests und Fuzzing

Die übernommenen PSE-Werkzeuge wurden auf den aktuellen Styles-4-Dogs-Server
umgestellt. Produktionsdaten und Tests sind voneinander getrennt.

## HTTP-Regressionsprüfungen

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

Der Testlauf baut und startet einen eigenen Server, verwendet isolierte
Admin-Dateien, SQLite-Datenbanken und TSV-Importdateien und beendet den Prozess anschließend automatisch.
Neben den öffentlichen Routen werden auch der authentifizierte Admin-Workflow,
CSRF-Schutz, persistente Statusänderungen sowie Admin-Suche und Statusfilter geprüft. Details stehen in
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
