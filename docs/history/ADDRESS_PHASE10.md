# Pflichtadresse und PLZ-Ort-Autovervollständigung – Phase 10

Phase 10 erweitert jede neue Terminanfrage um eine verpflichtende
Kundenanschrift. Die Buchung bleibt weiterhin ohne Kundenkonto möglich.

## Pflichtfelder

Das öffentliche Formular verlangt:

```text
Straße und Hausnummer
Postleitzahl
Wohnort
```

Serverseitig gelten zusätzlich zu den HTML-Regeln:

- alle drei Felder müssen vorhanden und einzeilig sein,
- die Postleitzahl muss aus genau fünf Ziffern bestehen,
- Straße und Hausnummer müssen mindestens eine Ziffer enthalten,
- überlange Werte werden abgelehnt.

Die Prüfung im Browser ist nur eine Eingabehilfe. Verbindlich ist immer die
Validierung im C-Backend.

## Automatische Ortsergänzung

Nach Eingabe einer vollständigen deutschen Postleitzahl ruft das Frontend
same-origin auf:

```text
GET /api/postal-code?postal_code=26121
```

Der Backendserver fragt ausschließlich die Postleitzahl bei einem fest
konfigurierten OpenPLZ-Endpunkt ab. Name, Straße, Kontaktdaten oder Angaben zum
Hund werden nicht an den PLZ-Dienst übertragen.

- genau ein Ort: das bearbeitbare Wohnortfeld wird vorausgefüllt,
- mehrere Orte: passende Werte werden als Auswahlvorschläge angeboten,
- kein Treffer oder Ausfall: der Wohnort kann weiterhin manuell eingetragen
  werden; die Buchung wird dadurch nicht blockiert.

Erfolgreiche Antworten werden begrenzt im Arbeitsspeicher zwischengespeichert.
Leere Treffer haben eine kürzere Cache-Dauer. Ein eigenes IP- und Global-Limit
schützt die öffentliche API vor unnötiger Last.

## Produktionsnetzwerk

Der gehärtete Webserver darf per systemd nur localhost erreichen. Deshalb setzt
`server.env` in Produktion:

```text
STYLES4DOGS_POSTAL_LOOKUP_BASE_URL=http://127.0.0.1:31339/de/Localities
```

Caddy stellt diesen Port ausschließlich auf `127.0.0.1` bereit und schreibt
für den internen Upstream keine Access-Logs. Im öffentlichen Website-Log wird
der Querywert `postal_code` maskiert. Caddy vermittelt außerdem die
HTTPS-Verbindung zum festen OpenPLZ-Host.
Der Port darf nicht nach außen freigegeben werden.

Für eine lokale Entwicklungsumgebung ohne Caddy ist der Standard:

```text
https://openplzapi.org/de/Localities
```

## Datenbankmigration

Das SQLite-Schema wird idempotent auf Version 8 erweitert:

```text
PRAGMA user_version = 8
```

Neue Spalten in `bookings`:

```text
street_address TEXT NOT NULL DEFAULT ''
postal_code    TEXT NOT NULL DEFAULT ''
city           TEXT NOT NULL DEFAULT ''
```

Die leeren Defaults sind ausschließlich für bestehende Buchungen und den
Legacy-TSV-Import notwendig. Neue öffentliche Buchungen werden ohne vollständige
Adresse abgelehnt.

## Adminbereich

Straße und Hausnummer sowie Postleitzahl und Wohnort werden in jeder
Buchungskarte angezeigt. Die Adminsuche berücksichtigt alle drei Felder.

## Datenschutz

Die Anschrift erweitert den Umfang personenbezogener Buchungsdaten. Vor dem
Produktivstart müssen Zweck, Rechtsgrundlage, Löschfristen und die technische
PLZ-Abfrage in der final rechtlich geprüften Datenschutzerklärung korrekt
beschrieben sein.

## Tests

Der isolierte Regressionstest startet einen lokalen PLZ-Mockdienst. Dadurch
benötigt der Test weder Internetzugang noch echte Kundendaten. Geprüft werden
unter anderem:

- Pflichtfelder und Formularreihenfolge,
- fünfstellige PLZ-Validierung,
- erfolgreicher und ungültiger API-Aufruf,
- persistierte Adresse in SQLite,
- Anzeige und Suche im Adminbereich,
- Schema-Migration auf Version 8,
- Produktionskonfiguration und Caddy-Proxy.
