#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=${1:-"$PROJECT_ROOT/cmake-build-pewpew"}
RUNTIME_DIR="$BUILD_DIR/test-runtime"
SECRETS_DIR="$RUNTIME_DIR/secrets"
DATA_DIR="$RUNTIME_DIR/data"
AUTH_FILE="$SECRETS_DIR/admin.auth"
DATABASE_FILE="$DATA_DIR/styles4dogs-test.db"
LEGACY_BOOKING_FILE="$DATA_DIR/bookings.txt"
SERVER_LOG="$BUILD_DIR/server.log"
SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

rm -rf -- "$RUNTIME_DIR"
mkdir -p -- "$SECRETS_DIR" "$DATA_DIR"

cat > "$LEGACY_BOOKING_FILE" <<'LEGACY_EOF'
2026-07-01 12:00:00	Legacy Test	legacy@example.invalid	Waldi	Frühere Anfrage
v2	2026-07-02 13:30:00	neu	TSV V2 Test	v2@example.invalid	Flocke	small	wash_dry	2026-08-12	Importierte V2-Anfrage
LEGACY_EOF
chmod 600 "$LEGACY_BOOKING_FILE"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSTYLES4DOGS_SECRETS_DIR="$SECRETS_DIR" \
    -DSTYLES4DOGS_AUTH_FILE="$AUTH_FILE" \
    -DSTYLES4DOGS_DATA_DIR="$DATA_DIR" \
    -DSTYLES4DOGS_DATABASE_FILE="$DATABASE_FILE" \
    -DSTYLES4DOGS_BOOKING_FILE="$LEGACY_BOOKING_FILE"

cmake --build "$BUILD_DIR" --target Server

: > "$SERVER_LOG"
"$BUILD_DIR/Server" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

python3 - "$SERVER_PID" <<'PY'
import socket
import sys
import time

pid = int(sys.argv[1])
last_error = None

for _ in range(50):
    try:
        with socket.create_connection(("127.0.0.1", 31337), timeout=0.2):
            raise SystemExit(0)
    except OSError as error:
        last_error = error
        time.sleep(0.1)

print(f"Testserver wurde nicht erreichbar: {last_error}", file=sys.stderr)
raise SystemExit(1)
PY

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Der Testserver wurde unerwartet beendet:" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi

STYLES4DOGS_TEST_DATABASE_FILE="$DATABASE_FILE" \
    python3 "$SCRIPT_DIR/tests_styles4dogs.py" 127.0.0.1 31337

COUNT_BEFORE=$(python3 - "$DATABASE_FILE" <<'PY'
import sqlite3
import sys

with sqlite3.connect(sys.argv[1]) as connection:
    print(connection.execute("SELECT COUNT(*) FROM bookings").fetchone()[0])
PY
)

printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | \
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
