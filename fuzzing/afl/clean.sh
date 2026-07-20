#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
rm -rf -- "$SCRIPT_DIR/out" "$SCRIPT_DIR/sync" "$SCRIPT_DIR/logs"
echo "AFL-Ausgaben wurden entfernt. Die Eingabekorpora bleiben erhalten."
