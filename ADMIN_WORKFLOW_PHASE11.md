# Phase 11: Admin-Kalender, Monatsansicht und Abwesenheitsvorlage

## Ziel

Diese Phase verbessert die tägliche Bedienung des Adminbereichs, ohne das bestehende Buchungs- oder Benachrichtigungsmodell zu verändern.

## Öffnungszeiten

- Tageskarten nutzen die volle Breite der Kalendersektion.
- Bei bis zu drei Zeiträumen werden drei Karten nebeneinander angezeigt.
- Ein optionaler vierter Zeitraum kann über „Vierten Zeitraum hinzufügen“ eingeblendet werden.
- Sobald vier Zeiträume sichtbar sind, werden sie als Raster mit zwei Spalten dargestellt.
- Auf schmaleren Bildschirmen wechselt die Darstellung automatisch auf zwei beziehungsweise eine Spalte.

## Gemeinsames Speichern

- Der Abschnitt „Änderungen übernehmen“ steht nach Leistungen und Sperrzeiten.
- Der untere Speicherknopf ist deaktiviert, solange keine Änderung vorgenommen wurde.
- Der schwebende Speicherknopf erscheint nur bei ungespeicherten Änderungen und nur, solange der untere Speicherbereich nicht sichtbar ist.
- Beim Verlassen der Seite mit ungespeicherten Änderungen warnt der Browser.
- Der Informationsabschnitt zum Buchungsschutz steht unterhalb des Speicherbereichs.

Neue Leistungen und Sperrzeiten behalten eigene, unmittelbar zugehörige Speicheraktionen. Der gemeinsame Speicherknopf gilt für Buchungsregeln, Öffnungszeiten und Änderungen an vorhandenen Leistungen.

## Monatsansicht

- Die bisherige feste 30-Tage-Ansicht wurde durch einen echten Kalendermonat ersetzt.
- Vorheriger und nächster Zeitraum wechseln um genau einen Monat.
- Der Monat wird als Sieben-Spalten-Raster von Montag bis Sonntag angezeigt.
- Ein Tag zeigt zunächst nur die Anzahl vorhandener Termine und gegebenenfalls Sperrzeiten.
- Über „Details anzeigen“ werden die bestehenden vollständigen Terminkarten eingeblendet.
- Tages- und Wochenansicht bleiben unverändert verfügbar.

## E-Mail-Einstellung

Die bisherige Formulierung zur Versandwarteschlange wurde durch eine kundenorientierte Beschreibung ersetzt:

> Kunden bei Buchung, Bestätigung und Absage automatisch per E-Mail informieren

Die ergänzende Erklärung stellt klar, dass zunächst Versandaufträge gespeichert und anschließend durch den Notification-Worker verschickt werden.

## Abwesenheitsvorlage

Bei der Vorlage `booking_received` steht ein vorbereiteter Textbaustein für Urlaub, Krankheit oder andere Abwesenheiten bereit.

- Der Text wird erst nach Bestätigung in die sichtbaren Eingabefelder eingesetzt.
- Die vorhandene Vorlage wird dadurch noch nicht gespeichert.
- Zur Aktivierung muss weiterhin bewusst „Vorlage speichern“ gewählt werden.
- Die Vorlage erklärt ausdrücklich, dass eine Anfrage nur vorläufig reserviert und noch nicht verbindlich bestätigt ist.
- Für Abwesenheitszeiten sollte die automatische Terminbestätigung im Kalender deaktiviert werden.

## Tests

Die HTTP-Regressionstests prüfen unter anderem:

- Reihenfolge der Kalenderabschnitte,
- deaktivierten Speicherknopf im Ausgangszustand,
- Drei- und Vier-Zeitraum-Darstellung,
- echte Monatslänge,
- aufklappbare Tageszusammenfassungen,
- Einbindung der Abwesenheitsvorlage.
