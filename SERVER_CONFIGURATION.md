# Laufzeitkonfiguration

Der Server wird einmal gebaut und anschließend über Umgebungsvariablen an die
jeweilige Installation angepasst. Ein Wechsel von Daten-, Secret- oder
Webpfaden erfordert dadurch keinen neuen Build.

Beim Start wird die Konfiguration vollständig validiert. Ungültige Werte führen
zu einem kontrollierten Startabbruch, bevor die SQLite-Datenbank geöffnet oder
ein Netzwerk-Socket erstellt wird.

## Variablen

| Variable | Entwicklungsstandard | Bedeutung |
|---|---|---|
| `STYLES4DOGS_BIND_ADDRESS` | `127.0.0.1` | IPv4-Adresse, auf der der Server lauscht |
| `STYLES4DOGS_PORT` | `31337` | TCP-Port von `1` bis `65535` |
| `STYLES4DOGS_DOCUMENT_ROOT` | `public/` | Verzeichnis der öffentlichen Website-Dateien |
| `STYLES4DOGS_SECRETS_DIR` | `.secrets/` | Verzeichnis für Authentifizierungsdaten |
| `STYLES4DOGS_AUTH_FILE` | `.secrets/admin.auth` | Argon2id-Admin-Datei |
| `STYLES4DOGS_DATA_DIR` | `data/` | beschreibbares Laufzeitverzeichnis |
| `STYLES4DOGS_DATABASE_FILE` | `data/styles4dogs.db` | SQLite-Datenbank oder für Tests `:memory:` |
| `STYLES4DOGS_LEGACY_BOOKING_FILE` | `data/bookings.txt` | einmalig zu importierende TSV-Datei |
| `STYLES4DOGS_TRUSTED_PROXY_TOKEN` | leer | gemeinsames Secret zur abgesicherten Übernahme von `X-Forwarded-For` |
| `STYLES4DOGS_SALON_NAME` | `Styling 4 Dogs` | Anzeigename in E-Mails und ICS-Dateien |
| `STYLES4DOGS_SALON_ADDRESS` | leer | Salonadresse in E-Mails und ICS-Dateien |
| `STYLES4DOGS_SALON_PHONE` | leer | Salontelefonnummer in E-Mails |
| `STYLES4DOGS_PUBLIC_BASE_URL` | `http://127.0.0.1:8080` | öffentliche Basis-URL für spätere Kundenlinks |
| `STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE` | `49` | Landesvorwahl für lokale Telefon-/WhatsApp-Links |

`STYLES4DOGS_BOOKING_FILE` wird vorläufig als rückwärtskompatibler Alias für
`STYLES4DOGS_LEGACY_BOOKING_FILE` akzeptiert. Neue Konfigurationen sollen nur
den eindeutigen Namen mit `LEGACY` verwenden.

## Abgeleitete Pfade

Wird nur `STYLES4DOGS_SECRETS_DIR` gesetzt, aber nicht
`STYLES4DOGS_AUTH_FILE`, verwendet der Server automatisch:

```text
<STYLES4DOGS_SECRETS_DIR>/admin.auth
```

Wird nur `STYLES4DOGS_DATA_DIR` gesetzt, werden automatisch verwendet:

```text
<STYLES4DOGS_DATA_DIR>/styles4dogs.db
<STYLES4DOGS_DATA_DIR>/bookings.txt
```

Explizite Dateivariablen haben immer Vorrang.

## Entwicklungsstart

Ohne Variablen gelten die in CMake gesetzten Entwicklungsstandards:

```bash
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
./cmake-build-debug/Server
```

Beispiel mit vollständig isolierten Laufzeitdaten:

```bash
runtime_dir="$PWD/runtime-local"
mkdir -p "$runtime_dir/secrets" "$runtime_dir/data"

STYLES4DOGS_BIND_ADDRESS=127.0.0.1 \
STYLES4DOGS_PORT=31338 \
STYLES4DOGS_DOCUMENT_ROOT="$PWD/public" \
STYLES4DOGS_SECRETS_DIR="$runtime_dir/secrets" \
STYLES4DOGS_DATA_DIR="$runtime_dir/data" \
./cmake-build-debug/Server
```

## Vorgesehene Produktionspfade

```text
/opt/styles4dogs/bin/Server
/var/www/styles4dogs/
/etc/styles4dogs/secrets/admin.auth
/var/lib/styles4dogs/styles4dogs.db
```

Eine passende Vorlage liegt unter `deploy/server.env.example`. Sie ist für eine
spätere systemd-`EnvironmentFile` gedacht.

Der öffentliche Webverkehr soll nicht direkt auf den C-Server treffen. Der
Standard bleibt deshalb `127.0.0.1`; Caddy oder nginx übernimmt davor HTTPS und
Proxy-Schutz. Eine abweichende Bind-Adresse muss bewusst gesetzt werden.

## Validierung

Der Start wird unter anderem abgebrochen bei:

- ungültiger IPv4-Adresse,
- Port `0` oder größer als `65535`,
- fehlendem oder nicht lesbarem Document Root,
- leeren oder zu langen Pfaden,
- bereits vorhandenen Daten-/Secret-Pfaden, die keine Verzeichnisse sind.

Passwörter und CSRF-Token gehören nicht in Umgebungsvariablen. Die
Admin-Anmeldedaten bleiben ausschließlich in der geschützten `admin.auth`. Das
Proxy-Token ist dagegen ein automatisch erzeugtes Maschinen-Secret und wird
benötigt, damit der Backendserver weitergeleiteten Client-IP-Angaben nur von
der eigenen Caddy-Instanz vertraut. Details stehen in `RATE_LIMITING.md`.


## SMTP-Konfiguration

Für neue Installationen verbindet der Admin das Salon-Postfach unter:

```text
/admin/notifications
```

Der HTTP-Server speichert die SMTP-Daten authentifiziert verschlüsselt in:

```text
<STYLES4DOGS_SECRETS_DIR>/notification.smtp
<STYLES4DOGS_SECRETS_DIR>/notification.key
```

Der separate Benachrichtigungs-Worker liest diese Dateien und übernimmt den
eigentlichen Netzwerkversand. Der Webserver selbst behält seine systemd-
Netzwerkbeschränkung auf localhost.

Die bisherigen Variablen in `/etc/styles4dogs/notification.env` bleiben als
Migrations-Fallback unterstützt:

| Variable | Bedeutung |
|---|---|
| `STYLES4DOGS_SMTP_URL` | `smtp://...:587` oder `smtps://...:465` |
| `STYLES4DOGS_SMTP_USERNAME` | SMTP-Benutzername |
| `STYLES4DOGS_SMTP_PASSWORD` | SMTP- oder App-Passwort |
| `STYLES4DOGS_SMTP_FROM_ADDRESS` | sichtbare Absenderadresse |
| `STYLES4DOGS_SMTP_FROM_NAME` | sichtbarer Absendername |
| `STYLES4DOGS_ADMIN_NOTIFICATION_EMAIL` | Empfänger neuer Terminanfragen |
| `STYLES4DOGS_NOTIFY_ADMIN_NEW_BOOKING` | `1` aktiviert Admin-Mails |

Sobald der Admin eine Verbindung speichert oder deaktiviert, hat die
verschlüsselte Konfiguration Vorrang. Details zu Testmails, Vorlagen,
Warteschlange und Backups stehen in `NOTIFICATIONS.md`.
