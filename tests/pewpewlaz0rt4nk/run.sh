#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=${1:-"$PROJECT_ROOT/cmake-build-pewpew"}
RUNTIME_DIR="$BUILD_DIR/test-runtime"
SECRETS_DIR="$RUNTIME_DIR/secrets"
DATA_DIR="$RUNTIME_DIR/data"
AUTH_FILE="$SECRETS_DIR/admin.auth"
BOOKING_FILE="$DATA_DIR/bookings.txt"
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
mkdir -p -- "$RUNTIME_DIR"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSTYLES4DOGS_SECRETS_DIR="$SECRETS_DIR" \
    -DSTYLES4DOGS_AUTH_FILE="$AUTH_FILE" \
    -DSTYLES4DOGS_DATA_DIR="$DATA_DIR" \
    -DSTYLES4DOGS_BOOKING_FILE="$BOOKING_FILE"

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

STYLES4DOGS_TEST_BOOKING_FILE="$BOOKING_FILE" \
    python3 "$SCRIPT_DIR/tests_styles4dogs.py" 127.0.0.1 31337
