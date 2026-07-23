# Phase 9 – Kundenbereich und Admin-Dashboard

Phase 9 ergänzt zwei zusammenhängende Produktfunktionen: einen persönlichen
Buchungslink für Kundinnen und Kunden und eine zentrale Startseite für den
Adminbereich.

## Persönlicher Buchungslink

Nach einer erfolgreichen Terminanfrage zeigt die Bestätigungsseite einen
persönlichen Link. Derselbe Link wird automatisch an Kunden-E-Mails angehängt:

```text
/buchung/<buchungsnummer>/<64-stelliger-token>
```

Der Link zeigt ausschließlich die zugehörige Buchung:

- aktuellen Status,
- Buchungsnummer,
- Hund und Leistung,
- Datum und Uhrzeit,
- optionalen Ablehnungsgrund.

Offene und bestätigte Buchungen können über den Link abgesagt werden. Dabei
wird der Zeitraum sofort wieder freigegeben. Wiederholte Absagen sind
idempotent und verändern keine bereits abgeschlossene Buchung mehr.

## Sicherheit

Der Token wird mit `crypto_generichash` und einem zufälligen, 32 Byte langen
Server-Schlüssel erzeugt. Der Schlüssel liegt mit Modus `0600` unter:

```text
/etc/styles4dogs/secrets/customer-portal.key
```

Weitere Schutzmaßnahmen:

- 256 Bit Tokenmaterial, hexadezimal im Link,
- Vergleich in konstanter Zeit,
- keine Kunden-Logins oder erratbaren Buchungslinks,
- `Cache-Control: no-store`,
- `Referrer-Policy: no-referrer`,
- `robots=noindex,nofollow`,
- identische 404-Antwort für falsche Tokens und unbekannte Buchungen.

Der persönliche Link ist ein Zugangsschlüssel und darf nicht öffentlich geteilt
werden.

## Backup und Restore

Das Backupskript sichert den Kundenbereich-Schlüssel neben dem SQLite-Backup:

```text
styles4dogs-<zeitstempel>.db
styles4dogs-<zeitstempel>.db.customer-portal.key
styles4dogs-<zeitstempel>.db.sha256
```

Die Prüfsummendatei enthält Datenbank und Schlüssel. Beim Restore wird der
passende Schlüssel automatisch wiederhergestellt. Dadurch bleiben bereits
versendete Kundenlinks nach einem Serverwechsel oder Restore gültig.

## Admin-Dashboard

Neue geschützte Route:

```text
/admin
```

Die Übersicht zeigt:

- Anzahl aller und neuer Buchungen,
- ausstehende und fehlgeschlagene E-Mails,
- heutige offene und bestätigte Termine,
- Schnellzugriffe auf Buchungen, Kalender, Galerie und E-Mail-Verwaltung.

Alle Adminseiten enthalten einen Link zurück zur Übersicht.

## Noch folgende Ausbaustufen

Auf dieser Grundlage folgen als separate Phasen:

1. Verschieben von Terminen per Drag-and-drop,
2. automatische Nachricht bei einer Terminverschiebung,
3. Warteliste für ausgebuchte Zeiträume.

## Reverse-Proxy und Protokollierung

Die Caddy-Konfiguration überspringt Zugriffslogs für `/buchung/*`, damit der
persönliche Zugriffstoken nicht in der Access-Logdatei landet. Zusätzlich wird
siteweit `Referrer-Policy: no-referrer` gesetzt, sodass der Token nicht über den
HTTP-Referer an verlinkte Seiten weitergegeben wird.
