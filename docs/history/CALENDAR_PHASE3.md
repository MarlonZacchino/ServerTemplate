# Kalender Phase 3: Admin-Verwaltung

Phase 3 ergänzt den geschützten Adminbereich um eine vollständige Verwaltung der öffentlich angebotenen Termine.

## Adminroute

```text
GET /admin/calendar
```

Die Seite ist mit derselben Basic-Auth-Absicherung geschützt wie die Buchungsübersicht. Alle schreibenden Aktionen benötigen zusätzlich das vorhandene CSRF-Token und unterliegen dem Admin-Rate-Limit.

## Buchungsregeln

Der Admin kann einstellen:

- Mindestvorlauf in Stunden
- Buchungshorizont in Tagen
- Zeitraster in Minuten
- Dauer einer vorläufigen Reservierung in Stunden

Die Zeitzone bleibt `Europe/Berlin` und die Kapazität bleibt vorerst auf einen Hund gleichzeitig beschränkt.

## Öffnungszeiten

Jeder Wochentag wird separat gespeichert. Pro Tag können über die Oberfläche bis zu vier getrennte Zeiträume gepflegt werden. Leere Zeiträume werden ignoriert; ein Tag ohne Zeitraum ist geschlossen.

Beim Speichern wird ausschließlich der gewählte Wochentag innerhalb einer `BEGIN IMMEDIATE`-Transaktion ersetzt. Ungültige oder überlappende Zeiträume werden vor der Transaktion abgelehnt, sodass die bisherige Konfiguration erhalten bleibt.

## Leistungen

Für jede vorhandene Leistung können geändert werden:

- sichtbarer Name
- Dauer
- Pufferzeit
- öffentlich buchbar oder deaktiviert

Der technische Leistungsschlüssel bleibt unveränderlich. Bereits gespeicherte Termine behalten ihre vorhandenen Start- und Endzeiten.

## Urlaub und Sperrzeiten

Der Admin kann:

- einzelne Tage vollständig sperren
- mehrtägigen Urlaub eintragen
- einen Teil eines einzelnen Tages sperren
- Sperrzeiten wieder löschen

Mehrere Tage dürfen über die Oberfläche nur ganztägig gesperrt werden. Teilweise Sperrzeiten gelten immer für genau einen Tag.

## Routen

```text
POST /admin/calendar/settings
POST /admin/calendar/hours
POST /admin/calendar/service
POST /admin/calendar/closure/add
POST /admin/calendar/closure/delete
```

Erfolgreiche Aktionen antworten mit `303 See Other` und leiten zurück auf den Admin-Kalender. Ungültige Angaben liefern `400`, fehlende Einträge `404` und eine fehlerhafte CSRF-Prüfung `403`.

## Sicherheitsmerkmale

- Basic Auth auf jeder Adminroute
- CSRF-Schutz auf jeder Änderung
- Admin-Rate-Limit für alle Pfade unter `/admin/`
- vorbereitete SQLite-Anweisungen
- HTML-Escaping für Namen und Bezeichnungen
- keine Kundendaten in der öffentlichen Verfügbarkeits-API
- `Cache-Control: no-store` im Adminbereich

## Weiterführung in Phase 4

Das Annehmen und Ablehnen vorläufiger Terminanfragen, automatische Bestätigung, strukturierte Kontaktwege sowie das Hinzufügen und Löschen von Leistungen sind inzwischen in `CALENDAR_PHASE4.md` dokumentiert.
