# Internes Rate-Limiting

Der C-Server begrenzt besonders missbrauchsgefährdete Routen unabhängig vom
Reverse Proxy. Die Zustände liegen ausschließlich im Arbeitsspeicher und werden
bei einem Neustart verworfen. Client-IP-Adressen werden dafür nicht dauerhaft
gespeichert oder in SQLite geschrieben.

## Aktuelle Limits

| Bereich | Limit | Fenster |
|---|---:|---:|
| `POST /booking` pro Client-IP | 5 Versuche | 10 Minuten |
| `POST /booking` global | 120 Versuche | 1 Minute |
| fehlgeschlagene Admin-Authentifizierung pro Client-IP | 10 Fehler | 10 Minuten |

Ein abgelehnter Request erhält:

```text
HTTP/1.1 429 Too Many Requests
Retry-After: <Sekunden>
Cache-Control: no-store
```

Das Buchungslimit zählt absichtlich auch ungültige Formulare. Dadurch kann ein
Angreifer die teurere Formularverarbeitung nicht unbegrenzt auslösen. Beim
Adminbereich werden ausschließlich Antworten mit `401 Unauthorized` als
fehlgeschlagene Anmeldung gezählt. Eine erfolgreiche Adminseite (`200`) oder
eine erfolgreiche Statusänderung (`303`) löscht die bisherigen Fehler dieses
Clients.

## Vertrauenswürdige Client-IP hinter Caddy

Der Backend-Socket ist nur über `127.0.0.1` erreichbar. Trotzdem wird
`X-Forwarded-For` nicht allein aufgrund der Loopback-Verbindung akzeptiert.
Caddy überschreibt zusätzlich den Header
`X-Styles4Dogs-Proxy-Token` mit einem zufälligen gemeinsamen Secret.

Der Server verwendet die weitergeleitete IP nur, wenn gleichzeitig gilt:

1. die TCP-Verbindung kommt von `127.0.0.1`,
2. genau ein Proxy-Token-Header ist vorhanden,
3. das Token stimmt in konstanter Zeit mit der Serverkonfiguration überein,
4. der erste `X-Forwarded-For`-Wert ist eine gültige IPv4- oder IPv6-Adresse.

Andernfalls wird ausschließlich die tatsächliche Socket-IP verwendet. Ein
Client kann das Limit daher nicht durch selbst gesetzte Forwarding-Header
umgehen.

Das Secret wird bei der Installation automatisch erzeugt und getrennt in
folgenden root-verwalteten Dateien abgelegt:

```text
/etc/styles4dogs/server.env
/etc/styles4dogs/caddy.env
```

Es ist kein Benutzerpasswort, muss aber vertraulich bleiben. In Git,
Zugriffslogs oder HTTP-Antworten darf es nicht erscheinen.

## Technische Grenzen

Die aktuelle Implementierung passt zum bewusst single-threaded arbeitenden
Server. Bei einem späteren Wechsel auf Threads oder mehrere Prozesse muss der
Limiter synchronisiert oder durch eine gemeinsame externe Instanz ersetzt
werden. Das feste In-Memory-Array begrenzt außerdem den Speicherverbrauch; bei
voller Tabelle wird der am längsten nicht verwendete Eintrag ersetzt.

## Zusätzlicher Buchungsschutz ab Kalender Phase 7

Neben den IP-basierten Limits gelten zwei formularbezogene Schutzschichten:

| Bereich | Limit | Fenster |
|---|---:|---:|
| gleiche E-Mail-Adresse oder Telefonnummer | 3 gespeicherte Anfragen | 24 Stunden |
| ausgefülltes Honeypot-Feld | keine Speicherung | sofort |

Das Kontaktlimit wird innerhalb derselben SQLite-Transaktion wie die
Slotreservierung geprüft. Dadurch können parallele Requests die Begrenzung
nicht durch eine Prüfung vor dem Speichern umgehen. Telefonnummern werden für
den Vergleich auf Ziffern normalisiert; E-Mail-Adressen werden ohne Beachtung
der Groß-/Kleinschreibung verglichen.

Der Honeypot befindet sich außerhalb des sichtbaren Layouts, ist nicht per
Tabulator erreichbar und hat deaktiviertes Autocomplete. Wird er ausgefüllt,
erhält der Absender eine neutrale `201 Created`-Antwort, während keine Buchung
angelegt wird. Dadurch bekommen einfache Bots kein verwertbares Signal über die
Erkennung.

Diese Maßnahmen ersetzen keinen optionalen Challenge-Dienst bei dauerhaftem
öffentlichem Missbrauch. Sie reduzieren aber automatisierte Formularfüllung,
rotierende IP-Adressen und wiederholte Anfragen mit derselben Kontaktadresse,
ohne jeden legitimen Kunden mit einem CAPTCHA zu belasten.
