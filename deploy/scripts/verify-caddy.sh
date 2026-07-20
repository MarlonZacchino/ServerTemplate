#!/usr/bin/env bash
set -Eeuo pipefail

CADDYFILE=${STYLES4DOGS_CADDYFILE:-/etc/caddy/Caddyfile}
ENV_FILE=${STYLES4DOGS_CADDY_ENV_FILE:-/etc/styles4dogs/caddy.env}
SERVICE_NAME=${STYLES4DOGS_CADDY_SERVICE_NAME:-caddy.service}
SKIP_SYSTEMD=${STYLES4DOGS_CADDY_SKIP_SYSTEMD:-0}

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

[[ -r "$CADDYFILE" ]] && ok "Caddyfile exists" || bad "missing Caddyfile"
[[ -r "$ENV_FILE" ]] && ok "Caddy environment exists" || bad "missing Caddy environment"
[[ -r /etc/caddy/conf.d/styles4dogs.caddy ]] \
    && ok "Styles 4 Dogs Caddy snippet exists" \
    || bad "missing Styles 4 Dogs Caddy snippet"

if [[ -r "$ENV_FILE" ]]; then
    PROXY_TOKEN=$(read_env_value STYLES4DOGS_TRUSTED_PROXY_TOKEN || true)
    if [[ "$PROXY_TOKEN" =~ ^[A-Za-z0-9_-]{32,128}$ ]]; then
        ok "trusted proxy token is configured"
    else
        bad "trusted proxy token is missing or invalid"
    fi

    set -a
    # shellcheck disable=SC1090
    source "$ENV_FILE"
    set +a

    caddy validate --config "$CADDYFILE" --adapter caddyfile >/dev/null \
        && ok "Caddy configuration is valid" \
        || bad "Caddy configuration is invalid"
fi

if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    systemctl is-enabled --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is enabled" \
        || bad "$SERVICE_NAME is not enabled"
    systemctl is-active --quiet "$SERVICE_NAME" \
        && ok "$SERVICE_NAME is active" \
        || bad "$SERVICE_NAME is not active"
fi

SITE_ADDRESS=$(read_env_value STYLES4DOGS_CADDY_SITE_ADDRESS || true)
SITE_ADDRESS=${SITE_ADDRESS:-http://127.0.0.1:8080}

if [[ "$SITE_ADDRESS" == http://127.0.0.1:* || "$SITE_ADDRESS" == http://localhost:* ]]; then
    ROOT_HEADERS=$(mktemp)
    SETUP_HEADERS=$(mktemp)
    ADMIN_HEADERS=$(mktemp)
    BODY_FILE=$(mktemp)
    trap 'rm -f "$ROOT_HEADERS" "$SETUP_HEADERS" "$ADMIN_HEADERS" "$BODY_FILE"' EXIT

    if curl -fsS -D "$ROOT_HEADERS" -o /dev/null "$SITE_ADDRESS/"; then
        ok "Caddy proxy health check returned 200"
    else
        bad "Caddy proxy health check failed"
    fi

    grep -qi '^X-Content-Type-Options: nosniff' "$ROOT_HEADERS" \
        && ok "nosniff header is present" \
        || bad "nosniff header is missing"
    grep -qi '^Content-Security-Policy:' "$ROOT_HEADERS" \
        && ok "Content-Security-Policy is present" \
        || bad "Content-Security-Policy is missing"

    SETUP_STATUS=$(curl -sS -D "$SETUP_HEADERS" -o /dev/null -w '%{http_code}' \
        "$SITE_ADDRESS/setup/admin" || true)
    [[ "$SETUP_STATUS" == 404 ]] \
        && ok "public admin setup is blocked" \
        || bad "admin setup returned $SETUP_STATUS instead of 404"

    ADMIN_STATUS=$(curl -sS -D "$ADMIN_HEADERS" -o /dev/null -w '%{http_code}' \
        "$SITE_ADDRESS/admin/bookings" || true)
    [[ "$ADMIN_STATUS" == 401 ]] \
        && ok "admin area still requires authentication" \
        || bad "admin area returned $ADMIN_STATUS instead of 401"

    head -c 20000 /dev/zero | tr '\0' 'A' > "$BODY_FILE"
    LARGE_STATUS=$(curl -sS -o /dev/null -w '%{http_code}' \
        -X POST --data-binary "@$BODY_FILE" "$SITE_ADDRESS/booking" || true)
    [[ "$LARGE_STATUS" == 413 ]] \
        && ok "oversized booking body is rejected" \
        || bad "oversized booking body returned $LARGE_STATUS instead of 413"
else
    ok "public site address configured: $SITE_ADDRESS"
    echo "INFO: external DNS/TLS reachability was not tested automatically."
fi

if ((failures > 0)); then
    exit 1
fi
