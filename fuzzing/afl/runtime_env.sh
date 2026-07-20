#!/usr/bin/env bash

styles4dogs_export_afl_runtime() {
    if [[ $# -ne 2 ]]; then
        echo "Interner Fehler: styles4dogs_export_afl_runtime <project-root> <runtime-dir>" >&2
        return 2
    fi

    local project_root=$1
    local runtime_dir=$2

    mkdir -p -- "$runtime_dir/secrets" "$runtime_dir/data"

    export STYLES4DOGS_BIND_ADDRESS=127.0.0.1
    export STYLES4DOGS_PORT=31337
    export STYLES4DOGS_DOCUMENT_ROOT="$project_root/public"
    export STYLES4DOGS_SECRETS_DIR="$runtime_dir/secrets"
    export STYLES4DOGS_AUTH_FILE="$runtime_dir/secrets/admin.auth"
    export STYLES4DOGS_DATA_DIR="$runtime_dir/data"
    export STYLES4DOGS_DATABASE_FILE=:memory:
    export STYLES4DOGS_LEGACY_BOOKING_FILE="$runtime_dir/data/legacy-bookings.txt"
    export STYLES4DOGS_TRUSTED_PROXY_TOKEN=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
}
