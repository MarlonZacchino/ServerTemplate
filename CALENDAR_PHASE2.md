# Öffentlicher Terminkalender – Phase 2

Phase 2 verbindet die in Phase 1 getestete Kalender-Engine mit der öffentlichen
Website. Kunden wählen jetzt zuerst eine aktive Leistung, anschließend einen
Tag und eine freie Uhrzeit. Das Frontend zeigt belegte Zeiten nur als
`belegt`; Namen, Kontaktdaten oder andere Buchungsinformationen werden niemals
öffentlich ausgegeben.

## Öffentliche Endpunkte

```text
GET  /api/services
GET  /api/availability?service=CODE&from=YYYY-MM-DD&to=YYYY-MM-DD
POST /booking
```

`/api/services` liefert nur aktive und automatisch buchbare Leistungen.
`/api/availability` akzeptiert maximal 42 Kalendertage pro Anfrage und liefert
für jeden regulären Slot Start, Ende und den booleschen Wert `available`.

Beispiel:

```json
{
  "timezone": "Europe/Berlin",
  "service": {
    "code": "full_groom",
    "name": "Komplettpflege",
    "duration_minutes": 120,
    "buffer_minutes": 15
  },
  "from": "2026-08-01",
  "to": "2026-08-31",
  "days": [
    {
      "date": "2026-08-04",
      "slots": [
        {"start": "09:00", "end": "11:00", "available": true},
        {"start": "09:15", "end": "11:15", "available": false}
      ]
    }
  ]
}
```

Alle API-Antworten werden mit `Cache-Control: no-store` ausgeliefert.

## Buchungsablauf

Der Browser sendet zusätzlich zu den Kontaktdaten:

```text
service
appointment_date
appointment_start
```

Der Server vertraut diesen Werten nicht. Vor dem Speichern wird der Slot
innerhalb einer `BEGIN IMMEDIATE`-Transaktion erneut berechnet. Ist er weiterhin
frei, entsteht eine Buchung mit:

```text
decision_status = pending
hold_expires_at = aktuelle UTC-Zeit + konfigurierte Pending-Frist
```

Der Zeitraum ist damit vorläufig blockiert. Eine gleichzeitig oder später
abgesendete Anfrage für denselben Zeitraum erhält `409 Conflict`.

## Frontend

`public/calendar.js` lädt Leistungen und Monatsverfügbarkeit vom eigenen
Server. Der Kalender:

- markiert Tage mit mindestens einem freien Slot,
- zeigt freie Uhrzeiten als auswählbare Schaltflächen,
- zeigt belegte Uhrzeiten deaktiviert,
- schreibt Datum und Startzeit erst nach einer bewussten Auswahl in versteckte
  Formularfelder,
- verhindert das Absenden ohne ausgewählten Slot,
- bleibt mobil und mit Tastatur bedienbar.

Die endgültige Bestätigung erfolgt weiterhin nicht automatisch. In Phase 4
nimmt der Admin Pending-Anfragen an oder lehnt sie ab.

## Sichere Ausgangslage

Nach Phase 1 enthält eine neue Datenbank keine Öffnungszeiten. Der öffentliche
Kalender zeigt deshalb zunächst keine freien Termine. Phase 3 ergänzt die
Admin-Oberfläche zum Konfigurieren von Öffnungszeiten, Urlaub, Leistungen und
Buchungsregeln.
