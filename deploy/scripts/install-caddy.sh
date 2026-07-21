#!/usr/bin/env bash
set -Eeuo pipefail
umask 027

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
ROOT_PREFIX=""
START_SERVICE=1

SITE_ADDRESS=${STYLES4DOGS_CADDY_SITE_ADDRESS:-http://127.0.0.1:8080}
UPSTREAM=${STYLES4DOGS_CADDY_UPSTREAM:-127.0.0.1:31337}

CADDY_CONFIG_DIR=/etc/caddy
CADDYFILE=/etc/caddy/Caddyfile
CADDY_SNIPPET_DIR=/etc/caddy/conf.d
CADDY_SNIPPET=/etc/caddy/conf.d/styles4dogs.caddy
CADDY_DROPIN_DIR=/etc/systemd/system/caddy.service.d
CADDY_DROPIN=/etc/systemd/system/caddy.service.d/styles4dogs.conf
STYLES_CONFIG_DIR=/etc/styles4dogs
CADDY_ENV=/etc/styles4dogs/caddy.env
SERVER_ENV=/etc/styles4dogs/server.env
CADDY_LOG_DIR=/var/log/caddy
CADDY_LOG_FILE=/var/log/caddy/styles4dogs-access.log

usage() {
    cat <<'USAGE'
Usage: sudo ./deploy/scripts/install-caddy.sh [options]

Options:
  --site-address ADDRESS  Caddy site address or domain
                          Default: http://127.0.0.1:8080
  --upstream HOST:PORT    Local Styling 4 Dogs backend
                          Default: 127.0.0.1:31337
  --no-start              Install and validate, but do not start Caddy
  --staging-root DIR      Install below DIR without systemd or Caddy validation
  --help                  Show this help

Examples:
  sudo ./deploy/scripts/install-caddy.sh
  sudo ./deploy/scripts/install-caddy.sh --site-address styles4dogs.example
USAGE
}

fail() {
    echo "Caddy installation failed: $*" >&2
    exit 1
}

rooted() {
    printf '%s%s' "$ROOT_PREFIX" "$1"
}

validate_single_line_value() {
    local name=$1
    local value=$2

    [[ -n "$value" ]] || fail "$name must not be empty"
    [[ "$value" != *$'\n'* && "$value" != *$'\r'* ]] \
        || fail "$name must be a single line"
    [[ "$value" != *'{'* && "$value" != *'}'* ]] \
        || fail "$name contains forbidden Caddyfile characters"
    [[ "$value" != *$'\t'* && "$value" != *' '* ]] \
        || fail "$name must not contain whitespace"
}

while (($# > 0)); do
    case "$1" in
        --site-address)
            (($# >= 2)) || fail "--site-address requires a value"
            SITE_ADDRESS=$2
            shift 2
            ;;
        --upstream)
            (($# >= 2)) || fail "--upstream requires a value"
            UPSTREAM=$2
            shift 2
            ;;
        --no-start)
            START_SERVICE=0
            shift
            ;;
        --staging-root)
            (($# >= 2)) || fail "--staging-root requires a directory"
            ROOT_PREFIX=$(realpath -m -- "$2")
            START_SERVICE=0
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

validate_single_line_value "site address" "$SITE_ADDRESS"
validate_single_line_value "upstream" "$UPSTREAM"

if [[ ! "$UPSTREAM" =~ ^(127\.0\.0\.1|localhost):([1-9][0-9]{0,4})$ ]]; then
    fail "upstream must be a loopback host and port"
fi

UPSTREAM_PORT=${BASH_REMATCH[2]}
if ((UPSTREAM_PORT > 65535)); then
    fail "upstream port must be between 1 and 65535"
fi

if [[ -z "$ROOT_PREFIX" && $EUID -ne 0 ]]; then
    fail "run this installer as root"
fi

for command in install awk grep cp date realpath; do
    command -v "$command" >/dev/null 2>&1 || fail "$command is not installed"
done

if [[ -z "$ROOT_PREFIX" ]]; then
    for command in caddy systemctl getent; do
        command -v "$command" >/dev/null 2>&1 || fail "$command is not installed"
    done

    getent passwd caddy >/dev/null || fail "the caddy system user is missing"
    getent group caddy >/dev/null || fail "the caddy system group is missing"
fi

CONFIG_DIR_PATH=$(rooted "$CADDY_CONFIG_DIR")
CADDYFILE_PATH=$(rooted "$CADDYFILE")
SNIPPET_DIR_PATH=$(rooted "$CADDY_SNIPPET_DIR")
SNIPPET_PATH=$(rooted "$CADDY_SNIPPET")
DROPIN_DIR_PATH=$(rooted "$CADDY_DROPIN_DIR")
DROPIN_PATH=$(rooted "$CADDY_DROPIN")
STYLES_CONFIG_PATH=$(rooted "$STYLES_CONFIG_DIR")
ENV_PATH=$(rooted "$CADDY_ENV")
SERVER_ENV_PATH=$(rooted "$SERVER_ENV")
LOG_PATH=$(rooted "$CADDY_LOG_DIR")
LOG_FILE_PATH=$(rooted "$CADDY_LOG_FILE")

install -d -m 0755 -- "$CONFIG_DIR_PATH" "$SNIPPET_DIR_PATH" "$DROPIN_DIR_PATH"
install -d -m 0750 -- "$STYLES_CONFIG_PATH"
install -d -m 0750 -- "$LOG_PATH"

if [[ -z "$ROOT_PREFIX" ]]; then
    chown root:root "$CONFIG_DIR_PATH" "$SNIPPET_DIR_PATH" "$DROPIN_DIR_PATH"
    chown root:styles4dogs "$STYLES_CONFIG_PATH"
    chown caddy:caddy "$LOG_PATH"
fi

if [[ ! -f "$CADDYFILE_PATH" ]]; then
    : > "$CADDYFILE_PATH"
    chmod 0644 "$CADDYFILE_PATH"
fi

BACKUP_SUFFIX=$(date -u +%Y%m%dT%H%M%SZ)
cp -a -- "$CADDYFILE_PATH" "$CADDYFILE_PATH.backup-$BACKUP_SUFFIX"

MANAGED_BEGIN="# BEGIN Styling 4 Dogs managed include"
MANAGED_END="# END Styling 4 Dogs managed include"

# Remove an older managed block first. This keeps upgrades idempotent and
# prevents a duplicate import when the distribution Caddyfile already imports
# /etc/caddy/conf.d/* itself.
awk -v begin="$MANAGED_BEGIN" -v end="$MANAGED_END" '
    $0 == begin { skip = 1; next }
    $0 == end   { skip = 0; next }
    !skip { print }
' "$CADDYFILE_PATH" > "$CADDYFILE_PATH.tmp"
mv -- "$CADDYFILE_PATH.tmp" "$CADDYFILE_PATH"
chmod 0644 "$CADDYFILE_PATH"

if ! awk '
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*import[[:space:]]+/ {
        path = $2
        gsub(/^"|"$/, "", path)
        if (path == "/etc/caddy/conf.d/*" ||
            path == "/etc/caddy/conf.d/*.caddy") {
            found = 1
        }
    }
    END { exit found ? 0 : 1 }
' "$CADDYFILE_PATH"; then
    cat >> "$CADDYFILE_PATH" <<EOF_IMPORT

$MANAGED_BEGIN
import /etc/caddy/conf.d/*.caddy
$MANAGED_END
EOF_IMPORT
fi

install -m 0644 -- "$PROJECT_ROOT/deploy/caddy/styles4dogs.caddy" "$SNIPPET_PATH"
install -m 0644 -- "$PROJECT_ROOT/deploy/systemd/caddy-styles4dogs.conf" "$DROPIN_PATH"

if [[ ! -r "$SERVER_ENV_PATH" ]]; then
    fail "missing server environment: $SERVER_ENV_PATH (run install.sh first)"
fi

TRUSTED_PROXY_TOKEN=$(awk -F= '
    $1 == "STYLES4DOGS_TRUSTED_PROXY_TOKEN" {
        sub(/^[^=]*=/, "")
        print
        exit
    }
' "$SERVER_ENV_PATH")

if [[ ! "$TRUSTED_PROXY_TOKEN" =~ ^[A-Za-z0-9_-]{32,128}$ ]]; then
    fail "server.env contains no valid STYLES4DOGS_TRUSTED_PROXY_TOKEN"
fi

if [[ -f "$ENV_PATH" ]]; then
    cp -a -- "$ENV_PATH" "$ENV_PATH.backup-$BACKUP_SUFFIX"
fi

cat > "$ENV_PATH" <<EOF_ENV
STYLES4DOGS_CADDY_SITE_ADDRESS=$SITE_ADDRESS
STYLES4DOGS_CADDY_UPSTREAM=$UPSTREAM
STYLES4DOGS_TRUSTED_PROXY_TOKEN=$TRUSTED_PROXY_TOKEN
EOF_ENV
chmod 0600 "$ENV_PATH"
if [[ -z "$ROOT_PREFIX" ]]; then
    chown root:root "$ENV_PATH"
fi

# Caddy validation opens configured log writers. Ensure the access log exists
# with the final service ownership before validation, otherwise a root-run
# validation can create a root-owned file that the caddy service cannot reopen.
if [[ ! -e "$LOG_FILE_PATH" ]]; then
    : > "$LOG_FILE_PATH"
fi
chmod 0640 "$LOG_FILE_PATH"
if [[ -z "$ROOT_PREFIX" ]]; then
    chown caddy:caddy "$LOG_FILE_PATH"
fi

if [[ -z "$ROOT_PREFIX" ]]; then
    set -a
    # shellcheck disable=SC1090
    source "$ENV_PATH"
    set +a

    caddy validate --config "$CADDYFILE_PATH" --adapter caddyfile
    chown caddy:caddy "$LOG_FILE_PATH"
    chmod 0640 "$LOG_FILE_PATH"
    systemctl daemon-reload

    if [[ "$START_SERVICE" -eq 1 ]]; then
        systemctl enable caddy.service
        systemctl restart caddy.service
        "$SCRIPT_DIR/verify-caddy.sh"
    fi
fi

cat <<EOF_SUMMARY
Styling 4 Dogs Caddy configuration installed.
  Site address: $SITE_ADDRESS
  Upstream:     $UPSTREAM
  Caddyfile:    $CADDYFILE_PATH
  Site snippet: $SNIPPET_PATH
  Environment:  $ENV_PATH
EOF_SUMMARY

if [[ "$SITE_ADDRESS" == http://127.0.0.1:* ]]; then
    echo "Local proxy URL: $SITE_ADDRESS"
else
    echo "Public HTTPS requires working DNS plus externally reachable ports 80 and 443."
fi
