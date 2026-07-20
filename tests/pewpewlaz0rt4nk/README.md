# PewPewLaz0rTank-Regressionstests

Dieser Ordner basiert auf dem bisherigen PSE-Testwerkzeug und ist auf den
Styles-4-Dogs-Server angepasst. Der Testlauf verwendet bewusst eigene
Laufzeitpfade im Build-Ordner. Bestehende Admin-Zugänge und echte Buchungen im
Projekt werden weder gelesen noch verändert.

## Start

Im Projektwurzelverzeichnis:

```bash
./tests/pewpewlaz0rt4nk/run.sh
```

Der Ablauf:

1. konfiguriert `cmake-build-pewpew/`,
2. verwendet isolierte `secrets/`- und `data/`-Ordner,
3. startet den Server auf `127.0.0.1:31337`,
4. führt die HTTP-Regressions- und Zustandsprüfungen aus,
5. beendet den Testserver automatisch.

Vor dem Start darf kein anderer Prozess Port `31337` belegen.

## Abgedeckte Bereiche

- alle öffentlichen Website-Routen,
- CSS-Auslieferung und Query-Strings,
- `400`, `401`, `403`, `404`, `405` und `201`,
- HTTP/1.0-/HTTP/1.1-Request-Line und Ablehnung von HTTP/2.0,
- Directory-Traversal-Schutz,
- gültige und ungültige Buchungsanfragen,
- korrekte `Content-Length`,
- leerer Response-Body bei `HEAD`,
- einmaliges Browser-Setup mit CSRF-Token,
- Sperrung der Setup-Route nach der Einrichtung,
- Basic Auth mit falschen und korrekten Zugangsdaten,
- TSV-Speicherung in einer isolierten Testdatei.

Bei einem Fehler liefert das Skript einen Exit-Code ungleich null und eignet
sich damit später auch für CI.
