#!/usr/bin/env bash
set -Eeuo pipefail

APP_DIR=${STYLES4DOGS_APP_DIR:-/opt/styles4dogs}
WEB_ROOT=${STYLES4DOGS_WEB_ROOT:-/var/www/styles4dogs}
CONFIG_DIR=${STYLES4DOGS_CONFIG_DIR:-/etc/styles4dogs}
STATE_DIR=${STYLES4DOGS_STATE_DIR:-/var/lib/styles4dogs}
SERVICE_NAME=${STYLES4DOGS_SERVICE_NAME:-styles4dogs.service}
SKIP_SYSTEMD=${STYLES4DOGS_SKIP_SYSTEMD:-0}
ENV_FILE=${STYLES4DOGS_ENV_FILE:-$CONFIG_DIR/server.env}

failures=0

ok() {
    printf 'OK: %s\n' "$*"
}

bad() {
    printf 'ERROR: %s\n' "$*" >&2
    failures=$((failures + 1))
}

read_env_value() {
    local key=$1
    awk -F= -v key="$key" '
        $0 !~ /^[[:space:]]*#/ && $1 == key {
            sub(/^[^=]*=/, "")
            print
            exit
        }
    ' "$ENV_FILE"
}

check_mode() {
    local path=$1
    local expected=$2
    local actual

    [[ -e "$path" ]] || return 0
    actual=$(stat -c '%a' -- "$path")
    [[ "$actual" == "$expected" ]] \
        && ok "$path has mode $expected" \
        || bad "$path has mode $actual, expected $expected"
}

[[ -x "$APP_DIR/bin/Server" ]] && ok "server binary exists" || bad "missing server binary"
[[ -r "$WEB_ROOT/index.html" ]] && ok "website files exist" || bad "missing website files"
[[ -r "$ENV_FILE" ]] && ok "server.env exists" || bad "missing server.env"
[[ -d "$CONFIG_DIR/secrets" ]] && ok "secret directory exists" || bad "missing secret directory"
[[ -d "$STATE_DIR" ]] && ok "state directory exists" || bad "missing state directory"

check_mode "$CONFIG_DIR" 750
check_mode "$CONFIG_DIR/secrets" 700
check_mode "$ENV_FILE" 640
check_mode "$STATE_DIR" 750
check_mode "$STATE_DIR/styles4dogs.db" 600
check_mode "$CONFIG_DIR/secrets/admin.auth" 600

if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    systemctl is-enabled --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is enabled" \
        || bad "$SERVICE_NAME is not enabled"
    systemctl is-active --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is active" \
        || bad "$SERVICE_NAME is not active"
fi

if [[ -r "$ENV_FILE" ]]; then
    PROXY_TOKEN=$(read_env_value STYLES4DOGS_TRUSTED_PROXY_TOKEN || true)
    if [[ "$PROXY_TOKEN" =~ ^[A-Za-z0-9_-]{32,128}$ ]]; then
        ok "trusted proxy token is configured"
    else
        bad "trusted proxy token is missing or invalid"
    fi

    HOST=$(read_env_value STYLES4DOGS_BIND_ADDRESS)
    PORT=$(read_env_value STYLES4DOGS_PORT)
    HOST=${HOST:-127.0.0.1}
    PORT=${PORT:-31337}

    if [[ "$HOST" != "127.0.0.1" ]]; then
        bad "production bind address is not loopback: $HOST"
    else
        ok "server is configured for loopback only"
    fi

    HTTP_READY=0
    status_line=""

    for _ in {1..30}; do
        if exec 3<>"/dev/tcp/$HOST/$PORT" 2>/dev/null; then
            printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' >&3
            IFS= read -r status_line <&3 || true
            exec 3>&-
            status_line=${status_line%$'\r'}

            if [[ "$status_line" == "HTTP/1.1 200 OK" ]]; then
                HTTP_READY=1
                break
            fi
        fi

        sleep 0.2
    done

    [[ "$HTTP_READY" -eq 1 ]] \
        && ok "HTTP health check returned 200" \
        || bad "unexpected HTTP status: ${status_line:-no response from $HOST:$PORT}"
fi

if ((failures > 0)); then
    exit 1
fi
