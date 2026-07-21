#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
source "$SCRIPT_DIR/runtime_env.sh"
BUILD_DIR=${AFL_BUILD_DIR:-"$PROJECT_ROOT/cmake-build-afl"}
RUNTIME_DIR="$BUILD_DIR/calendar-fuzz-runtime"

styles4dogs_export_afl_runtime "$PROJECT_ROOT" "$RUNTIME_DIR"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=afl-clang-fast

cmake --build "$BUILD_DIR" --target calendar_fuzz_target

printf 'service=wash_dry&date=2026-08-03&current_date=2026-08-01&current_minute=0&now_utc=2026-08-01T00%%3A00%%3A00Z' | \
    afl-showmap -q -o "$BUILD_DIR/calendar-afl-showmap.txt" -- \
    "$BUILD_DIR/calendar_fuzz_target" >/dev/null

if [[ ! -s "$BUILD_DIR/calendar-afl-showmap.txt" ]]; then
    echo "AFL-Instrumentierung des Kalenderziels wurde nicht erkannt." >&2
    exit 1
fi

echo "Kalender-AFL-Build erfolgreich: $BUILD_DIR/calendar_fuzz_target"
echo "Instrumentierte Kanten: $(wc -l < "$BUILD_DIR/calendar-afl-showmap.txt")"
