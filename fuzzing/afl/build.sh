#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
MODE=${1:-plain}

case "$MODE" in
    plain)
        BUILD_DIR="$PROJECT_ROOT/cmake-build-afl"
        BUILD_TYPE=RelWithDebInfo
        ;;
    asan)
        BUILD_DIR="$PROJECT_ROOT/cmake-build-afl-asan"
        BUILD_TYPE=Debug
        export AFL_USE_ASAN=1
        export AFL_USE_UBSAN=1
        ;;
    *)
        echo "Verwendung: $0 [plain|asan]" >&2
        exit 2
        ;;
esac

RUNTIME_DIR="$BUILD_DIR/fuzz-runtime"
mkdir -p "$RUNTIME_DIR/secrets" "$RUNTIME_DIR/data"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER=afl-clang-fast \
    -DSTYLES4DOGS_SECRETS_DIR="$RUNTIME_DIR/secrets" \
    -DSTYLES4DOGS_AUTH_FILE="$RUNTIME_DIR/secrets/admin.auth" \
    -DSTYLES4DOGS_DATA_DIR="$RUNTIME_DIR/data" \
    -DSTYLES4DOGS_BOOKING_FILE=/dev/null

cmake --build "$BUILD_DIR" --target Server

printf 'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n' | \
    afl-showmap -q -o "$BUILD_DIR/afl-showmap.txt" -- "$BUILD_DIR/Server" stdin >/dev/null

if [[ ! -s "$BUILD_DIR/afl-showmap.txt" ]]; then
    echo "AFL-Instrumentierung wurde nicht erkannt." >&2
    exit 1
fi

echo "AFL-Build erfolgreich: $BUILD_DIR/Server"
echo "Instrumentierte Kanten: $(wc -l < "$BUILD_DIR/afl-showmap.txt")"
