#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
    echo "Verwendung: $0 <AFL-Crashdatei>" >&2
    exit 2
fi

CRASH_FILE=$(realpath -- "$1")
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR="$PROJECT_ROOT/cmake-build-asan"
RUNTIME_DIR="$BUILD_DIR/test-runtime"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_FLAGS_DEBUG="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DSTYLES4DOGS_SECRETS_DIR="$RUNTIME_DIR/secrets" \
    -DSTYLES4DOGS_AUTH_FILE="$RUNTIME_DIR/secrets/admin.auth" \
    -DSTYLES4DOGS_DATA_DIR="$RUNTIME_DIR/data" \
    -DSTYLES4DOGS_DATABASE_FILE=:memory: \
    -DSTYLES4DOGS_BOOKING_FILE="$RUNTIME_DIR/data/legacy-bookings.txt"

cmake --build "$BUILD_DIR" --target Server

ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    "$BUILD_DIR/Server" stdin < "$CRASH_FILE"
