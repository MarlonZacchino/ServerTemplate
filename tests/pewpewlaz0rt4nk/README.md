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
2. setzt isolierte `secrets/`- und `data/`-Ordner über die Runtime-Konfiguration,
3. führt den isolierten C-Test der Kalender- und Verfügbarkeitsengine aus,
4. startet den Server auf `127.0.0.1:31338`,
5. führt die HTTP-Regressions- und Zustandsprüfungen aus,
6. beendet den Testserver automatisch.

Vor dem Start darf kein anderer Prozess Port `31338` belegen.

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
- Statusänderungen nur mit Authentifizierung und gültigem CSRF-Token,
- Validierung von Buchungs-ID und den Statuswerten `neu`, `kontaktiert`, `erledigt`,
- SQLite-Speicherung in einer isolierten Testdatenbank,
- einmaliger Import von TSV-v1 und TSV-v2,
- Schutz gegen doppelte TSV-Importe bei einem Neustart,
- persistente Statusänderung in SQLite ohne Änderung anderer Buchungen,
- Statusfilter und Suche nach Kundenname, Kontakt oder Hund,
- Literalbehandlung von SQL-LIKE-Sonderzeichen und sichere HTML-Ausgabe von Suchwerten,
- Runtime-Overrides für Port und Pfade,
- kontrollierter Startabbruch bei ungültiger Adresse, ungültigem Port und fehlendem Document Root,
- Kalenderschema Version 3 und Migration bestehender Buchungen zu `legacy`,
- Leistungen mit Dauer und Puffer, Wochenöffnungszeiten und Sperrzeiten,
- Mindestvorlauf, Buchungshorizont und deaktivierbare Leistungen,
- ablaufende Pending-Reservierungen und transaktionssicherer Doppelbuchungsschutz.

Bei einem Fehler liefert das Skript einen Exit-Code ungleich null und eignet
sich damit später auch für CI.

## Rate-Limit-Regressionen

Der isolierte Lauf setzt ein eigenes Proxy-Token und prüft:

- fünf Buchungsversuche pro Client werden akzeptiert, der nächste erhält `429`,
- `Retry-After` wird gesetzt,
- gefälschte `X-Forwarded-For`-Werte ohne korrektes Token umgehen das Limit nicht,
- zehn fehlgeschlagene Admin-Anmeldungen führen zur Sperre,
- erfolgreiche Admin-Anmeldung löscht vorherige Fehlversuche,
- die globale Buchungs-Notbremse greift auch bei rotierenden IP-Adressen.
