# Kalender und Verfügbarkeit – Phase 1

Phase 1 legt ausschließlich das Datenmodell und die serverseitige
Verfügbarkeitsberechnung an. Der öffentliche Kalender und die Admin-Oberfläche
werden erst in den folgenden Phasen ergänzt. Dadurch bleibt die bisherige
Buchungsanfrage während der Migration unverändert nutzbar.

## Datenbankschema

Phase 1 führte ursprünglich `PRAGMA user_version = 3` ein. Der aktuelle
Gesamtstand migriert inzwischen auf Version 4; die folgenden Tabellen stammen
aus Phase 1:

- `services` für Leistung, Dauer, Puffer, Aktivierung und Sortierung,
- `calendar_settings` für Zeitzone, Vorlauf, Buchungshorizont, Raster und
  Pending-Frist,
- `weekly_opening_hours` mit mehreren Zeitfenstern pro Wochentag,
- `calendar_closures` für Urlaub, ganze Schließtage und teilweise Sperrzeiten.

Die bestehende Tabelle `bookings` erhält zusätzliche Terminspalten:

- `service_id`,
- `appointment_date`,
- `start_minute`,
- `end_minute`,
- `blocked_until_minute`,
- `decision_status`,
- `hold_expires_at`,
- `decision_at`,
- `rejection_reason`.

Bestehende Anfragen bleiben erhalten und erhalten den Entscheidungsstatus
`legacy`. Sie blockieren keine Kalenderzeit.

## Sichere Standardwerte

Der Kalender ist nach der Migration absichtlich geschlossen, bis der Admin
Öffnungszeiten konfiguriert. So werden nie versehentlich frei erfundene
Öffnungszeiten veröffentlicht.

Voreingestellte Regeln:

```text
Zeitzone:               Europe/Berlin
Mindestvorlauf:         24 Stunden
Buchungshorizont:       90 Tage
Zeitraster:             15 Minuten
Pending-Reservierung:   24 Stunden
Kapazität:              1 Hund gleichzeitig
```

Die bereits vorhandenen Leistungscodes werden als konfigurierbare Datensätze
angelegt. `other` ist zunächst nicht automatisch buchbar, weil dafür keine
verlässliche feste Dauer bekannt ist.

## Verfügbarkeitsberechnung

`availability.c` berechnet freie Slots ausschließlich aus serverseitigen Daten:

1. aktive Leistung und feste Dauer laden,
2. Mindestvorlauf und Buchungshorizont prüfen,
3. regelmäßige Öffnungszeiten laden,
4. Urlaub und Sperrzeiten abziehen,
5. bestätigte und noch gültige Pending-Termine abziehen,
6. Startzeiten im konfigurierten Raster erzeugen.

Der Puffer nach einer Leistung blockiert weitere Termine, wird aber getrennt
vom eigentlichen Leistungsende gespeichert.

## Schutz vor Doppelbuchungen

`availability_reserve_pending()` verwendet `BEGIN IMMEDIATE`, berechnet den
Slot innerhalb derselben SQLite-Transaktion erneut und speichert erst danach
die vorläufige Reservierung. Zusätzlich verhindern SQLite-Trigger direkte
überlappende `pending`- oder `confirmed`-Einträge.

Abgelaufene Pending-Reservierungen werden auf `expired` gesetzt und geben den
Slot wieder frei.

## Noch nicht enthalten

Phase 1 enthält bewusst noch nicht:

- keinen öffentlichen JSON-Endpunkt,
- keinen sichtbaren Monatskalender,
- keine Admin-Seite für Öffnungszeiten und Urlaub,
- keine Annehmen-/Ablehnen-Schaltflächen.

Diese Oberflächen werden auf der jetzt getesteten Engine aufgebaut.
