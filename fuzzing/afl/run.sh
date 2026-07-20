#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
source "$SCRIPT_DIR/runtime_env.sh"
BUILD_DIR=${AFL_BUILD_DIR:-"$PROJECT_ROOT/cmake-build-afl"}
TARGET="$BUILD_DIR/Server"
INPUT_DIR="$SCRIPT_DIR/in"
OUTPUT_DIR="$SCRIPT_DIR/out"
DICTIONARY="$SCRIPT_DIR/http.dict"
RUNTIME_DIR="$BUILD_DIR/fuzz-runtime"

styles4dogs_export_afl_runtime "$PROJECT_ROOT" "$RUNTIME_DIR"

if [[ ! -x "$TARGET" ]]; then
    echo "AFL-Binary fehlt. Zuerst ausführen:" >&2
    echo "  $SCRIPT_DIR/build.sh" >&2
    exit 1
fi

INPUT_ARGUMENT="$INPUT_DIR"
if [[ -f "$OUTPUT_DIR/default/fuzzer_stats" ]]; then
    INPUT_ARGUMENT="-"
    echo "Vorhandene AFL-Sitzung wird fortgesetzt: $OUTPUT_DIR"
fi

exec afl-fuzz \
    -i "$INPUT_ARGUMENT" \
    -o "$OUTPUT_DIR" \
    -m none \
    -x "$DICTIONARY" \
    -- "$TARGET" stdin
