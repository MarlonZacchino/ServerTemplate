#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
source "$SCRIPT_DIR/runtime_env.sh"
BUILD_DIR=${AFL_BUILD_DIR:-"$PROJECT_ROOT/cmake-build-afl"}
TARGET="$BUILD_DIR/Server"
INPUT_DIR="$SCRIPT_DIR/in"
OUTPUT_DIR="$SCRIPT_DIR/sync"
LOG_DIR="$SCRIPT_DIR/logs"
DICTIONARY="$SCRIPT_DIR/http.dict"
RUNTIME_DIR="$BUILD_DIR/fuzz-runtime"
CPU_THREADS=$(nproc)
INSTANCES=${AFL_INSTANCES:-$((CPU_THREADS / 2))}
SECONDARY_PIDS=()

styles4dogs_export_afl_runtime "$PROJECT_ROOT" "$RUNTIME_DIR"

if (( INSTANCES < 1 )); then
    INSTANCES=1
fi

if [[ ! -x "$TARGET" ]]; then
    echo "AFL-Binary fehlt. Zuerst ausführen:" >&2
    echo "  $SCRIPT_DIR/build.sh" >&2
    exit 1
fi

mkdir -p "$LOG_DIR"

cleanup() {
    for pid in "${SECONDARY_PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

INPUT_ARGUMENT="$INPUT_DIR"
if [[ -f "$OUTPUT_DIR/main/fuzzer_stats" ]]; then
    INPUT_ARGUMENT="-"
    echo "Vorhandene parallele AFL-Sitzung wird fortgesetzt."
fi

for ((index = 2; index <= INSTANCES; index++)); do
    name=$(printf 'secondary%02d' "$index")
    afl-fuzz \
        -i "$INPUT_ARGUMENT" \
        -o "$OUTPUT_DIR" \
        -S "$name" \
        -m none \
        -x "$DICTIONARY" \
        -- "$TARGET" stdin \
        >"$LOG_DIR/$name.log" 2>&1 &
    SECONDARY_PIDS+=("$!")
done

echo "CPU-Threads:   $CPU_THREADS"
echo "AFL-Instanzen: $INSTANCES"
echo "Status in einem zweiten Terminal:"
echo "  $SCRIPT_DIR/status.sh"
echo "Mit Strg+C werden Master und Secondary-Instanzen beendet."

# Der Master bleibt im Vordergrund und zeigt die AFL-Oberfläche.
afl-fuzz \
    -i "$INPUT_ARGUMENT" \
    -o "$OUTPUT_DIR" \
    -M main \
    -m none \
    -x "$DICTIONARY" \
    -- "$TARGET" stdin
