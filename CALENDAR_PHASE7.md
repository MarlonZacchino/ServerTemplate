# Kalender Phase 7 – gemeinsame Einstellungen und Schutzmaßnahmen

Phase 7 bündelt die Bedienung des Admin-Kalenders und ergänzt die für den
öffentlichen Betrieb notwendigen Schutz- und Wartungsfunktionen. Das
Datenbankschema bleibt auf Version 6; es ist keine neue Migration erforderlich.

## Gemeinsames Speichern

Buchungsregeln, alle regelmäßigen Öffnungszeiten und Änderungen an vorhandenen
Leistungen liegen in einem gemeinsamen Formular. Der Server validiert zunächst
den vollständigen Stand und ersetzt ihn danach innerhalb einer
`BEGIN IMMEDIATE`-Transaktion. Bei einem ungültigen Feld bleibt die bisherige
Konfiguration vollständig erhalten.

Der schwebende Speicherbereich erscheint erst nach einer Änderung. Sobald der
feste Speicherknopf am Seitenende sichtbar ist, wird die schwebende Variante
ausgeblendet. Das Hinzufügen oder Löschen einer Leistung sowie Sperrzeiten
bleiben eigenständige Aktionen, weil sie einzelne Datensätze erzeugen oder
löschen und eine eigene Bestätigung benötigen.

## 30-Tage-Ansicht und Datumsformat

`/admin/appointments` unterstützt jetzt:

- Tagesansicht,
- Wochenansicht,
- fortlaufende 30-Tage-Ansicht.

Termine und Sperrzeiten besitzen unterschiedliche Kartenfarben, Statusbezeichnungen
und eine sichtbare Legende. Oberflächendaten werden als `DD.MM.YYYY` dargestellt.
Bei Terminen und Kalendertagen wird zusätzlich der Wochentag angezeigt, zum
Beispiel `21.07.2026 - Dienstag`. Intern und in HTML-Datumsfeldern bleibt das
maschinenlesbare ISO-Format `YYYY-MM-DD` erhalten.

## Verständlichere Buchungsregeln

Die bisherigen ähnlichen Bezeichnungen wurden ersetzt:

- **Frühester buchbarer Termin:** Mindestabstand zwischen Buchung und Termin.
- **Freihaltezeit für offene Anfragen:** Zeitraum, in dem eine noch nicht
  entschiedene Anfrage den Slot für andere Kunden blockiert.

Zusätzliche Hilfetexte erklären Buchungshorizont, Zeitraster und Erinnerung.

## Schutz vor Quatschbuchungen und Spam

Der öffentliche Buchungsweg kombiniert mehrere Schichten:

1. vorhandenes IP-Limit: fünf Versuche in zehn Minuten,
2. globale Notbremse: 120 Versuche pro Minute,
3. unsichtbares Honeypot-Feld für einfache Formularbots,
4. höchstens drei Buchungsanfragen pro E-Mail-Adresse oder Telefonnummer in
   24 Stunden,
5. erneute Slot- und Überschneidungsprüfung in einer SQLite-Transaktion.

Das Honeypot-Feld erzeugt absichtlich eine neutrale Erfolgsantwort, speichert
aber keine Buchung. Das Kontaktlimit antwortet mit `429 Too Many Requests` und
`Retry-After: 86400`. Personenbezogene Kontaktwerte werden dafür nicht in einer
zusätzlichen Tabelle gespeichert; die Prüfung nutzt die ohnehin vorhandenen
Buchungsdaten.

Für den späteren öffentlichen Betrieb kann optional ein datenschutzfreundlicher
Challenge-Dienst vor `/booking` ergänzt werden, falls die vorhandenen Schichten
unter realem Traffic nicht ausreichen. Er ist für den lokalen Betrieb zunächst
nicht erforderlich.

## E-Mail-Warteschlange bereinigen

Unter `/admin/notifications` kann der Admin nun:

- fehlgeschlagene Nachrichten erneut freigeben,
- die Historie und den Zähler gesendeter Nachrichten zurücksetzen,
- endgültig fehlgeschlagene Nachrichten löschen,
- gesendete und fehlgeschlagene Historie gemeinsam leeren.

Ausstehende und gerade verarbeitete Jobs werden durch diese Aktionen bewusst
nicht gelöscht.
