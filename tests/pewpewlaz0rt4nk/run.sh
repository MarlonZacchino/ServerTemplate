#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=${1:-"$PROJECT_ROOT/cmake-build-pewpew"}
RUNTIME_DIR="$BUILD_DIR/test-runtime"
SECRETS_DIR="$RUNTIME_DIR/secrets"
DATA_DIR="$RUNTIME_DIR/data"
DATABASE_FILE="$DATA_DIR/styles4dogs.db"
LEGACY_BOOKING_FILE="$DATA_DIR/bookings.txt"
SERVER_LOG="$BUILD_DIR/server.log"
NOTIFICATION_OUTPUT_DIR="$BUILD_DIR/notification-out"
SERVER_PID=""
TEST_HOST=127.0.0.1
TEST_PORT=31338
TEST_PROXY_TOKEN=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef

SERVER_ENV=(
    "STYLES4DOGS_BIND_ADDRESS=$TEST_HOST"
    "STYLES4DOGS_PORT=$TEST_PORT"
    "STYLES4DOGS_DOCUMENT_ROOT=$PROJECT_ROOT/public"
    "STYLES4DOGS_SECRETS_DIR=$SECRETS_DIR"
    "STYLES4DOGS_DATA_DIR=$DATA_DIR"
    "STYLES4DOGS_TRUSTED_PROXY_TOKEN=$TEST_PROXY_TOKEN"
)

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

assert_configuration_rejected() {
    local name=$1
    local expected_message=$2
    shift 2

    local log_file="$BUILD_DIR/config-error.log"
    : > "$log_file"

    if printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | \
        env "${SERVER_ENV[@]}" "$@" \
        "$BUILD_DIR/Server" stdin > /dev/null 2> "$log_file"; then
        echo "Fehler: Ungültige Konfiguration wurde akzeptiert: $name" >&2
        exit 1
    fi

    if ! grep -Fq -- "$expected_message" "$log_file"; then
        echo "Fehler: Unerwartete Fehlermeldung für $name" >&2
        cat "$log_file" >&2
        exit 1
    fi

    echo "    $name: Ok"
}

rm -rf -- "$RUNTIME_DIR"
mkdir -p -- "$SECRETS_DIR" "$DATA_DIR"

cat > "$LEGACY_BOOKING_FILE" <<'LEGACY_EOF'
2026-07-01 12:00:00	Legacy Test	legacy@example.invalid	Waldi	Frühere Anfrage
v2	2026-07-02 13:30:00	neu	TSV V2 Test	v2@example.invalid	Flocke	small	wash_dry	2026-08-12	Importierte V2-Anfrage
LEGACY_EOF
chmod 600 "$LEGACY_BOOKING_FILE"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug

cmake --build "$BUILD_DIR" --target Server notification_worker calendar_engine_tests

CALENDAR_TEST_RUNTIME="$BUILD_DIR/calendar-engine-runtime"
rm -rf -- "$CALENDAR_TEST_RUNTIME"
mkdir -p -- "$CALENDAR_TEST_RUNTIME/secrets" "$CALENDAR_TEST_RUNTIME/data"

env \
    "STYLES4DOGS_BIND_ADDRESS=$TEST_HOST" \
    "STYLES4DOGS_PORT=$TEST_PORT" \
    "STYLES4DOGS_DOCUMENT_ROOT=$PROJECT_ROOT/public" \
    "STYLES4DOGS_SECRETS_DIR=$CALENDAR_TEST_RUNTIME/secrets" \
    "STYLES4DOGS_DATA_DIR=$CALENDAR_TEST_RUNTIME/data" \
    "STYLES4DOGS_DATABASE_FILE=$CALENDAR_TEST_RUNTIME/data/calendar-engine.db" \
    "STYLES4DOGS_LEGACY_BOOKING_FILE=$CALENDAR_TEST_RUNTIME/data/no-legacy-file.txt" \
    "$BUILD_DIR/calendar_engine_tests"

: > "$SERVER_LOG"
env "${SERVER_ENV[@]}" \
    "$BUILD_DIR/Server" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

python3 - "$SERVER_PID" "$TEST_HOST" "$TEST_PORT" <<'PY'
import socket
import sys
import time

pid = int(sys.argv[1])
host = sys.argv[2]
port = int(sys.argv[3])
last_error = None

for _ in range(50):
    try:
        with socket.create_connection((host, port), timeout=0.2):
            raise SystemExit(0)
    except OSError as error:
        last_error = error
        time.sleep(0.1)

print(f"Testserver wurde nicht erreichbar (PID {pid}): {last_error}", file=sys.stderr)
raise SystemExit(1)
PY

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Der Testserver wurde unerwartet beendet:" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi

STYLES4DOGS_TEST_DATABASE_FILE="$DATABASE_FILE" \
STYLES4DOGS_TEST_PROXY_TOKEN="$TEST_PROXY_TOKEN" \
    python3 "$SCRIPT_DIR/tests_styles4dogs.py" "$TEST_HOST" "$TEST_PORT"

rm -rf -- "$NOTIFICATION_OUTPUT_DIR"
mkdir -p -- "$NOTIFICATION_OUTPUT_DIR"

env "${SERVER_ENV[@]}" \
    "$BUILD_DIR/notification_worker" --dry-run "$NOTIFICATION_OUTPUT_DIR"

CONFIRMATION_MAIL=$(find "$NOTIFICATION_OUTPUT_DIR" -maxdepth 1 -type f \
    -name '*-booking_confirmed.eml' -print -quit)
REMINDER_MAIL=$(find "$NOTIFICATION_OUTPUT_DIR" -maxdepth 1 -type f \
    -name '*-appointment_reminder.eml' -print -quit)

if [[ -z "$CONFIRMATION_MAIL" || -z "$REMINDER_MAIL" ]]; then
    echo "Fehler: Bestätigungs- oder Erinnerungs-E-Mail wurde im Dry-Run nicht erzeugt." >&2
    find "$NOTIFICATION_OUTPUT_DIR" -maxdepth 1 -type f -print >&2
    exit 1
fi

grep -Fq 'BEGIN:VCALENDAR' "$CONFIRMATION_MAIL"
grep -Fq 'BEGIN:VCALENDAR' "$REMINDER_MAIL"
grep -Fq 'DTSTART:' "$CONFIRMATION_MAIL"
grep -Fq 'DTEND:' "$REMINDER_MAIL"

echo "* Benachrichtigungs-Worker"
echo "    Bestätigung, Erinnerung und ICS-Daten im Dry-Run: Ok"

COUNT_BEFORE=$(python3 - "$DATABASE_FILE" <<'PY'
import sqlite3
import sys

with sqlite3.connect(sys.argv[1]) as connection:
    print(connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0])
PY
)

printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | \
    env "${SERVER_ENV[@]}" \
    "$BUILD_DIR/Server" stdin >/dev/null

COUNT_AFTER=$(python3 - "$DATABASE_FILE" <<'PY'
import sqlite3
import sys

with sqlite3.connect(sys.argv[1]) as connection:
    print(connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0])
PY
)

if [[ "$COUNT_BEFORE" != "$COUNT_AFTER" ]]; then
    echo "Fehler: Der einmalige TSV-Import wurde beim Neustart erneut ausgeführt." >&2
    echo "Vorher: $COUNT_BEFORE, nachher: $COUNT_AFTER" >&2
    exit 1
fi

echo "* SQLite-Migration"
echo "    Einmaliger TSV-Import bleibt bei erneutem Start idempotent: Ok"

echo "* Runtime-Konfiguration"
assert_configuration_rejected \
    "Port außerhalb des gültigen Bereichs" \
    "STYLES4DOGS_PORT liegt außerhalb" \
    "STYLES4DOGS_PORT=70000"
assert_configuration_rejected \
    "Ungültige Bind-Adresse" \
    "Ungültige IPv4-Adresse" \
    "STYLES4DOGS_BIND_ADDRESS=keine-ip-adresse"
assert_configuration_rejected \
    "Fehlender Document Root" \
    "STYLES4DOGS_DOCUMENT_ROOT konnte nicht aufgelöst werden" \
    "STYLES4DOGS_DOCUMENT_ROOT=$RUNTIME_DIR/nicht-vorhanden"

assert_configuration_rejected \
    "Zu kurzes Proxy-Token" \
    "STYLES4DOGS_TRUSTED_PROXY_TOKEN muss zwischen 32 und 128 Zeichen lang sein" \
    "STYLES4DOGS_TRUSTED_PROXY_TOKEN=zu-kurz"
assert_configuration_rejected \
    "Ungültige öffentliche URL" \
    "öffentliche URL muss mit http:// oder https:// beginnen" \
    "STYLES4DOGS_PUBLIC_BASE_URL=ftp://example.invalid"
assert_configuration_rejected \
    "Ungültige Telefon-Landesvorwahl" \
    "STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE darf nicht mit 0 beginnen" \
    "STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE=049"
