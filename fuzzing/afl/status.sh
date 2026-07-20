#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ -d "$SCRIPT_DIR/sync" ]]; then
    exec afl-whatsup "$SCRIPT_DIR/sync"
fi

if [[ -d "$SCRIPT_DIR/out" ]]; then
    exec afl-whatsup "$SCRIPT_DIR/out"
fi

echo "Noch keine AFL-Ausgabe gefunden." >&2
exit 1
