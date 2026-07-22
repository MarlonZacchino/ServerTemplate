#!/usr/bin/env bash
set -Eeuo pipefail

APP_DIR=${STYLES4DOGS_APP_DIR:-/opt/styles4dogs}
WEB_ROOT=${STYLES4DOGS_WEB_ROOT:-/var/www/styles4dogs}
CONFIG_DIR=${STYLES4DOGS_CONFIG_DIR:-/etc/styles4dogs}
STATE_DIR=${STYLES4DOGS_STATE_DIR:-/var/lib/styles4dogs}
SERVICE_NAME=${STYLES4DOGS_SERVICE_NAME:-styles4dogs.service}
NOTIFICATION_TIMER=${STYLES4DOGS_NOTIFICATION_TIMER:-styles4dogs-notification.timer}
SKIP_SYSTEMD=${STYLES4DOGS_SKIP_SYSTEMD:-0}
ENV_FILE=${STYLES4DOGS_ENV_FILE:-$CONFIG_DIR/server.env}
NOTIFICATION_ENV_FILE=${STYLES4DOGS_NOTIFICATION_ENV_FILE:-$CONFIG_DIR/notification.env}
NOTIFICATION_SECRET_FILE=${STYLES4DOGS_NOTIFICATION_SECRET_FILE:-$CONFIG_DIR/secrets/notification.smtp}
NOTIFICATION_KEY_FILE=${STYLES4DOGS_NOTIFICATION_KEY_FILE:-$CONFIG_DIR/secrets/notification.key}
CUSTOMER_PORTAL_KEY_FILE=${STYLES4DOGS_CUSTOMER_PORTAL_KEY_FILE:-$CONFIG_DIR/secrets/customer-portal.key}

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
[[ -x "$APP_DIR/bin/notification_worker" ]] \
    && ok "notification worker exists" \
    || bad "missing notification worker"
[[ -r "$WEB_ROOT/index.html" ]] && ok "website files exist" || bad "missing website files"
[[ -r "$WEB_ROOT/galerie.html" && -r "$WEB_ROOT/gallery.js" && -r "$WEB_ROOT/logo.jpg" ]] \
    && ok "gallery and logo files exist" \
    || bad "gallery or logo files are missing"
[[ -r "$ENV_FILE" ]] && ok "server.env exists" || bad "missing server.env"
[[ -r "$NOTIFICATION_ENV_FILE" ]] \
    && ok "notification.env exists" \
    || bad "missing notification.env"
[[ -d "$CONFIG_DIR/secrets" ]] && ok "secret directory exists" || bad "missing secret directory"
[[ -d "$STATE_DIR" ]] && ok "state directory exists" || bad "missing state directory"

check_mode "$CONFIG_DIR" 750
check_mode "$CONFIG_DIR/secrets" 700
check_mode "$ENV_FILE" 640
check_mode "$NOTIFICATION_ENV_FILE" 640
check_mode "$STATE_DIR" 750
check_mode "$STATE_DIR/styles4dogs.db" 600
check_mode "$CONFIG_DIR/secrets/admin.auth" 600
check_mode "$NOTIFICATION_SECRET_FILE" 600
check_mode "$NOTIFICATION_KEY_FILE" 600
check_mode "$CUSTOMER_PORTAL_KEY_FILE" 600

if [[ -e "$CUSTOMER_PORTAL_KEY_FILE" ]]; then
    [[ -f "$CUSTOMER_PORTAL_KEY_FILE" ]] \
        && ok "customer portal key exists" \
        || bad "customer portal key is not a regular file"
    [[ "$(stat -c '%s' "$CUSTOMER_PORTAL_KEY_FILE")" == "32" ]] \
        && ok "customer portal key has the expected size" \
        || bad "customer portal key has an unexpected size"
else
    ok "customer portal key will be created with the first booking link"
fi


if [[ -f "$STATE_DIR/styles4dogs.db" ]] && command -v sqlite3 >/dev/null 2>&1; then
    SCHEMA_VERSION=$(sqlite3 "$STATE_DIR/styles4dogs.db" 'PRAGMA user_version;' || true)
    [[ "$SCHEMA_VERSION" == "7" ]] \
        && ok "database schema version is 7" \
        || bad "database schema version is ${SCHEMA_VERSION:-unknown}, expected 7"

    GALLERY_TABLE=$(sqlite3 "$STATE_DIR/styles4dogs.db" \
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='gallery_images';" || true)
    [[ "$GALLERY_TABLE" == "1" ]] \
        && ok "gallery table exists" \
        || bad "gallery table is missing"
fi

if [[ -e "$NOTIFICATION_SECRET_FILE" || -e "$NOTIFICATION_KEY_FILE" ]]; then
    if [[ -f "$NOTIFICATION_SECRET_FILE" && -f "$NOTIFICATION_KEY_FILE" ]]; then
        ok "admin-managed SMTP configuration files exist"
    else
        bad "admin-managed SMTP configuration is incomplete"
    fi
fi

if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    systemctl is-enabled --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is enabled" \
        || bad "$SERVICE_NAME is not enabled"
    systemctl is-active --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is active" \
        || bad "$SERVICE_NAME is not active"
    systemctl is-enabled --quiet "$NOTIFICATION_TIMER" \
        && ok "$NOTIFICATION_TIMER is enabled" \
        || bad "$NOTIFICATION_TIMER is not enabled"
    systemctl is-active --quiet "$NOTIFICATION_TIMER" \
        && ok "$NOTIFICATION_TIMER is active" \
        || bad "$NOTIFICATION_TIMER is not active"
fi

if [[ -r "$ENV_FILE" ]]; then
    PROXY_TOKEN=$(read_env_value STYLES4DOGS_TRUSTED_PROXY_TOKEN || true)
    if [[ "$PROXY_TOKEN" =~ ^[A-Za-z0-9_-]{32,128}$ ]]; then
        ok "trusted proxy token is configured"
    else
        bad "trusted proxy token is missing or invalid"
    fi

    COUNTRY_CODE=$(read_env_value STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE || true)
    if [[ "$COUNTRY_CODE" =~ ^[1-9][0-9]{0,3}$ ]]; then
        ok "default phone country code is configured"
    else
        bad "default phone country code is missing or invalid"
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

if [[ -f "$NOTIFICATION_SECRET_FILE" && -f "$NOTIFICATION_KEY_FILE" ]]; then
    ok "SMTP delivery is managed through the admin area"
elif [[ -r "$NOTIFICATION_ENV_FILE" ]]; then
    SMTP_URL=$(awk -F= '$1 == "STYLES4DOGS_SMTP_URL" {sub(/^[^=]*=/, ""); print; exit}' \
        "$NOTIFICATION_ENV_FILE")
    SMTP_FROM=$(awk -F= '$1 == "STYLES4DOGS_SMTP_FROM_ADDRESS" {sub(/^[^=]*=/, ""); print; exit}' \
        "$NOTIFICATION_ENV_FILE")
    if [[ -n "$SMTP_URL" && -n "$SMTP_FROM" ]]; then
        ok "SMTP delivery uses the legacy environment fallback"
    else
        ok "SMTP delivery is not configured yet; queued mail remains unsent"
    fi
fi

if ((failures > 0)); then
    exit 1
fi
