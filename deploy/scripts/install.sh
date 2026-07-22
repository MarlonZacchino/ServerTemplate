#!/usr/bin/env bash
set -Eeuo pipefail
umask 027

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR=${STYLES4DOGS_BUILD_DIR:-$PROJECT_ROOT/cmake-build-release}
ROOT_PREFIX=""
START_SERVICE=1
ENABLE_BACKUP_TIMER=1
ENABLE_NOTIFICATION_TIMER=1
SKIP_BUILD=0
REPLACE_ENV=0
SKIP_PREINSTALL_BACKUP=0

SERVICE_USER=styles4dogs
SERVICE_GROUP=styles4dogs
APP_DIR=/opt/styles4dogs
WEB_ROOT=/var/www/styles4dogs
CONFIG_DIR=/etc/styles4dogs
SECRETS_DIR=/etc/styles4dogs/secrets
STATE_DIR=/var/lib/styles4dogs
BACKUP_DIR=/var/backups/styles4dogs
SYSTEMD_DIR=/etc/systemd/system

usage() {
    cat <<'USAGE'
Usage: sudo ./deploy/scripts/install.sh [options]

Options:
  --no-start             Install files but do not enable/start the web service
  --no-backup-timer      Do not enable the daily backup timer
  --no-notification-timer
                         Do not enable the notification queue timer
  --skip-build           Reuse an existing cmake-build-release/Server binary
  --replace-env          Replace an existing server.env (a backup is kept)
  --skip-preinstall-backup
                         Do not create a database backup before an upgrade
  --staging-root DIR     Install below DIR without users or systemd (test mode)
  --help                 Show this help
USAGE
}

fail() {
    echo "Installation failed: $*" >&2
    exit 1
}

rooted() {
    printf '%s%s' "$ROOT_PREFIX" "$1"
}

install_dir() {
    local mode=$1
    local owner=$2
    local group=$3
    local path=$4

    if [[ -n "$ROOT_PREFIX" ]]; then
        install -d -m "$mode" -- "$path"
    else
        install -d -m "$mode" -o "$owner" -g "$group" -- "$path"
    fi
}

install_file() {
    local mode=$1
    local owner=$2
    local group=$3
    local source=$4
    local destination=$5

    if [[ -n "$ROOT_PREFIX" ]]; then
        install -m "$mode" -- "$source" "$destination"
    else
        install -m "$mode" -o "$owner" -g "$group" -- "$source" "$destination"
    fi
}

while (($# > 0)); do
    case "$1" in
        --no-start)
            START_SERVICE=0
            shift
            ;;
        --no-backup-timer)
            ENABLE_BACKUP_TIMER=0
            shift
            ;;
        --no-notification-timer)
            ENABLE_NOTIFICATION_TIMER=0
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --replace-env)
            REPLACE_ENV=1
            shift
            ;;
        --skip-preinstall-backup)
            SKIP_PREINSTALL_BACKUP=1
            shift
            ;;
        --staging-root)
            (($# >= 2)) || fail "--staging-root requires a directory"
            ROOT_PREFIX=$(realpath -m -- "$2")
            START_SERVICE=0
            ENABLE_BACKUP_TIMER=0
            ENABLE_NOTIFICATION_TIMER=0
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

if [[ -z "$ROOT_PREFIX" && $EUID -ne 0 ]]; then
    fail "run this installer as root"
fi

for command in cmake install pkg-config sqlite3 flock sha256sum realpath od tr grep awk; do
    command -v "$command" >/dev/null 2>&1 || fail "$command is not installed"
done

if [[ -z "$ROOT_PREFIX" ]]; then
    for command in systemctl getent groupadd useradd; do
        command -v "$command" >/dev/null 2>&1 || fail "$command is not installed"
    done
fi

pkg-config --exists libsodium || fail "libsodium development files are missing"
pkg-config --exists sqlite3 || fail "sqlite3 development files are missing"
pkg-config --exists libcurl || fail "libcurl development files are missing"

if [[ "$SKIP_BUILD" -ne 1 ]]; then
    command -v ninja >/dev/null 2>&1 || fail "ninja is not installed"
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" --target Server notification_worker
fi

[[ -x "$BUILD_DIR/Server" ]] || fail "missing built server: $BUILD_DIR/Server"
[[ -x "$BUILD_DIR/notification_worker" ]] || fail "missing notification worker: $BUILD_DIR/notification_worker"

APP_PATH=$(rooted "$APP_DIR")
WEB_PATH=$(rooted "$WEB_ROOT")
CONFIG_PATH=$(rooted "$CONFIG_DIR")
SECRETS_PATH=$(rooted "$SECRETS_DIR")
STATE_PATH=$(rooted "$STATE_DIR")
BACKUP_PATH=$(rooted "$BACKUP_DIR")
SYSTEMD_PATH=$(rooted "$SYSTEMD_DIR")

if [[ -z "$ROOT_PREFIX" ]]; then
    if ! getent group "$SERVICE_GROUP" >/dev/null; then
        groupadd --system "$SERVICE_GROUP"
    fi

    if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
        useradd \
            --system \
            --gid "$SERVICE_GROUP" \
            --home-dir "$STATE_DIR" \
            --no-create-home \
            --shell /usr/bin/nologin \
            "$SERVICE_USER"
    fi
fi

if [[ -z "$ROOT_PREFIX" && "$SKIP_PREINSTALL_BACKUP" -ne 1 && -f "$STATE_PATH/styles4dogs.db" ]]; then
    "$SCRIPT_DIR/backup.sh" \
        --database "$STATE_PATH/styles4dogs.db" \
        --output-dir "$BACKUP_PATH" \
        --retention-days 30 >/dev/null
fi

if [[ -z "$ROOT_PREFIX" ]] && systemctl list-unit-files styles4dogs.service >/dev/null 2>&1; then
    systemctl stop styles4dogs.service 2>/dev/null || true
    systemctl stop styles4dogs-notification.timer styles4dogs-notification.service 2>/dev/null || true
fi

install_dir 0755 root root "$APP_PATH"
install_dir 0755 root root "$APP_PATH/bin"
install_dir 0755 root root "$APP_PATH/share"
install_dir 0755 root root "$WEB_PATH"
install_dir 0750 root "$SERVICE_GROUP" "$CONFIG_PATH"
install_dir 0700 "$SERVICE_USER" "$SERVICE_GROUP" "$SECRETS_PATH"
install_dir 0750 "$SERVICE_USER" "$SERVICE_GROUP" "$STATE_PATH"
install_dir 0750 root "$SERVICE_GROUP" "$BACKUP_PATH"
install_dir 0755 root root "$SYSTEMD_PATH"

install_file 0755 root root "$BUILD_DIR/Server" "$APP_PATH/bin/Server"
install_file 0755 root root "$BUILD_DIR/notification_worker" "$APP_PATH/bin/notification_worker"
install_file 0755 root root "$SCRIPT_DIR/backup.sh" "$APP_PATH/bin/styles4dogs-backup"
install_file 0755 root root "$SCRIPT_DIR/restore.sh" "$APP_PATH/bin/styles4dogs-restore"
install_file 0755 root root "$SCRIPT_DIR/verify-installation.sh" "$APP_PATH/bin/styles4dogs-verify"
install_file 0755 root root "$SCRIPT_DIR/install-caddy.sh" "$APP_PATH/bin/styles4dogs-install-caddy"
install_file 0755 root root "$SCRIPT_DIR/verify-caddy.sh" "$APP_PATH/bin/styles4dogs-verify-caddy"
install_file 0755 root root "$SCRIPT_DIR/uninstall-caddy.sh" "$APP_PATH/bin/styles4dogs-uninstall-caddy"
install_file 0644 root root "$PROJECT_ROOT/DEPLOYMENT.md" "$APP_PATH/share/DEPLOYMENT.md"
install_file 0644 root root "$PROJECT_ROOT/CADDY_DEPLOYMENT.md" "$APP_PATH/share/CADDY_DEPLOYMENT.md"
install_file 0644 root root "$PROJECT_ROOT/RATE_LIMITING.md" "$APP_PATH/share/RATE_LIMITING.md"
install_file 0644 root root "$PROJECT_ROOT/CALENDAR_PHASE5.md" "$APP_PATH/share/CALENDAR_PHASE5.md"
install_file 0644 root root "$PROJECT_ROOT/CALENDAR_PHASE6.md" "$APP_PATH/share/CALENDAR_PHASE6.md"
install_file 0644 root root "$PROJECT_ROOT/GALLERY_PHASE8.md" "$APP_PATH/share/GALLERY_PHASE8.md"
install_file 0644 root root "$PROJECT_ROOT/CUSTOMER_PORTAL_PHASE9.md" "$APP_PATH/share/CUSTOMER_PORTAL_PHASE9.md"
install_file 0644 root root "$PROJECT_ROOT/ADDRESS_PHASE10.md" "$APP_PATH/share/ADDRESS_PHASE10.md"
install_file 0644 root root "$PROJECT_ROOT/BOOKING_WORKFLOW_PHASE13.md" "$APP_PATH/share/BOOKING_WORKFLOW_PHASE13.md"
install_file 0644 root root "$PROJECT_ROOT/NOTIFICATIONS.md" "$APP_PATH/share/NOTIFICATIONS.md"

case "$WEB_PATH" in
    */var/www/styles4dogs|*/staging/*|/var/www/styles4dogs) ;;
    *) [[ -n "$ROOT_PREFIX" ]] || fail "refusing to clean unexpected web root: $WEB_PATH" ;;
esac
find "$WEB_PATH" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
cp -a -- "$PROJECT_ROOT/public/." "$WEB_PATH/"
find "$WEB_PATH" -type d -exec chmod 0755 {} +
find "$WEB_PATH" -type f -exec chmod 0644 {} +
if [[ -z "$ROOT_PREFIX" ]]; then
    chown -R root:root "$WEB_PATH"
fi

ENV_PATH="$CONFIG_PATH/server.env"

generate_proxy_token() {
    od -An -N32 -tx1 /dev/urandom | tr -d ' \n'
}

TRUSTED_PROXY_TOKEN=""
if [[ -f "$ENV_PATH" ]]; then
    TRUSTED_PROXY_TOKEN=$(awk -F= '
        $1 == "STYLES4DOGS_TRUSTED_PROXY_TOKEN" {
            sub(/^[^=]*=/, "")
            print
            exit
        }
    ' "$ENV_PATH")
fi

if [[ -z "$TRUSTED_PROXY_TOKEN" ]]; then
    TRUSTED_PROXY_TOKEN=$(generate_proxy_token)
fi

if [[ ! "$TRUSTED_PROXY_TOKEN" =~ ^[A-Za-z0-9_-]{32,128}$ ]]; then
    fail "existing STYLES4DOGS_TRUSTED_PROXY_TOKEN is invalid"
fi

if [[ -f "$ENV_PATH" && "$REPLACE_ENV" -eq 1 ]]; then
    cp -a -- "$ENV_PATH" "$ENV_PATH.backup-$(date -u +%Y%m%dT%H%M%SZ)"
fi

if [[ ! -f "$ENV_PATH" || "$REPLACE_ENV" -eq 1 ]]; then
    cat > "$ENV_PATH" <<EOF_ENV
STYLES4DOGS_BIND_ADDRESS=127.0.0.1
STYLES4DOGS_PORT=31337
STYLES4DOGS_DOCUMENT_ROOT=$WEB_PATH
STYLES4DOGS_SECRETS_DIR=$SECRETS_PATH
STYLES4DOGS_AUTH_FILE=$SECRETS_PATH/admin.auth
STYLES4DOGS_DATA_DIR=$STATE_PATH
STYLES4DOGS_DATABASE_FILE=$STATE_PATH/styles4dogs.db
STYLES4DOGS_LEGACY_BOOKING_FILE=$STATE_PATH/bookings.txt
STYLES4DOGS_TRUSTED_PROXY_TOKEN=$TRUSTED_PROXY_TOKEN
STYLES4DOGS_SALON_NAME=Styling 4 Dogs
STYLES4DOGS_SALON_ADDRESS=
STYLES4DOGS_SALON_PHONE=
STYLES4DOGS_PUBLIC_BASE_URL=http://127.0.0.1:8080
STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE=49
STYLES4DOGS_POSTAL_LOOKUP_BASE_URL=http://127.0.0.1:31339/de/Localities
EOF_ENV
fi

if ! grep -q '^STYLES4DOGS_TRUSTED_PROXY_TOKEN=' "$ENV_PATH"; then
    printf '\nSTYLES4DOGS_TRUSTED_PROXY_TOKEN=%s\n' "$TRUSTED_PROXY_TOKEN" >> "$ENV_PATH"
fi

ensure_env_setting() {
    local key=$1
    local value=$2

    if ! grep -q "^${key}=" "$ENV_PATH"; then
        printf '%s=%s\n' "$key" "$value" >> "$ENV_PATH"
    fi
}

ensure_env_setting STYLES4DOGS_SALON_NAME 'Styling 4 Dogs'
ensure_env_setting STYLES4DOGS_SALON_ADDRESS ''
ensure_env_setting STYLES4DOGS_SALON_PHONE ''
ensure_env_setting STYLES4DOGS_PUBLIC_BASE_URL 'http://127.0.0.1:8080'
ensure_env_setting STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE '49'
ensure_env_setting STYLES4DOGS_POSTAL_LOOKUP_BASE_URL 'http://127.0.0.1:31339/de/Localities'

# Migrate only the former built-in brand value. Custom salon names remain untouched.
if grep -Fxq 'STYLES4DOGS_SALON_NAME=Styles 4 Dogs' "$ENV_PATH"; then
    sed -i 's/^STYLES4DOGS_SALON_NAME=Styles 4 Dogs$/STYLES4DOGS_SALON_NAME=Styling 4 Dogs/' "$ENV_PATH"
fi

chmod 0640 "$ENV_PATH"
if [[ -z "$ROOT_PREFIX" ]]; then
    chown root:"$SERVICE_GROUP" "$ENV_PATH"
fi

NOTIFICATION_ENV_PATH="$CONFIG_PATH/notification.env"
if [[ ! -f "$NOTIFICATION_ENV_PATH" ]]; then
    install_file 0640 root "$SERVICE_GROUP" \
        "$PROJECT_ROOT/deploy/notification.env.example" \
        "$NOTIFICATION_ENV_PATH"
else
    if grep -Fxq 'STYLES4DOGS_SMTP_FROM_NAME=Styles 4 Dogs' "$NOTIFICATION_ENV_PATH"; then
        sed -i 's/^STYLES4DOGS_SMTP_FROM_NAME=Styles 4 Dogs$/STYLES4DOGS_SMTP_FROM_NAME=Styling 4 Dogs/' "$NOTIFICATION_ENV_PATH"
    fi
    chmod 0640 "$NOTIFICATION_ENV_PATH"
    if [[ -z "$ROOT_PREFIX" ]]; then
        chown root:"$SERVICE_GROUP" "$NOTIFICATION_ENV_PATH"
    fi
fi

install_file 0644 root root "$PROJECT_ROOT/deploy/systemd/styles4dogs.service" \
    "$SYSTEMD_PATH/styles4dogs.service"
install_file 0644 root root "$PROJECT_ROOT/deploy/systemd/styles4dogs-backup.service" \
    "$SYSTEMD_PATH/styles4dogs-backup.service"
install_file 0644 root root "$PROJECT_ROOT/deploy/systemd/styles4dogs-backup.timer" \
    "$SYSTEMD_PATH/styles4dogs-backup.timer"
install_file 0644 root root "$PROJECT_ROOT/deploy/systemd/styles4dogs-notification.service" \
    "$SYSTEMD_PATH/styles4dogs-notification.service"
install_file 0644 root root "$PROJECT_ROOT/deploy/systemd/styles4dogs-notification.timer" \
    "$SYSTEMD_PATH/styles4dogs-notification.timer"

if [[ -z "$ROOT_PREFIX" ]]; then
    systemctl daemon-reload

    if [[ "$START_SERVICE" -eq 1 ]]; then
        systemctl enable --now styles4dogs.service
    else
        systemctl enable styles4dogs.service
    fi

    if [[ "$ENABLE_BACKUP_TIMER" -eq 1 ]]; then
        if [[ "$START_SERVICE" -eq 1 ]]; then
            systemctl enable --now styles4dogs-backup.timer
        else
            systemctl enable styles4dogs-backup.timer
        fi
    fi

    if [[ "$ENABLE_NOTIFICATION_TIMER" -eq 1 ]]; then
        if [[ "$START_SERVICE" -eq 1 ]]; then
            systemctl enable --now styles4dogs-notification.timer
        else
            systemctl enable styles4dogs-notification.timer
        fi
    fi
fi

cat <<EOF_SUMMARY
Styling 4 Dogs installed.
  Binary:        $APP_PATH/bin/Server
  Mail worker:   $APP_PATH/bin/notification_worker
  Website:       $WEB_PATH
  Configuration: $ENV_PATH
  SMTP config:   $NOTIFICATION_ENV_PATH
  Secrets:       $SECRETS_PATH
  Database:      $STATE_PATH/styles4dogs.db
  Backups:       $BACKUP_PATH
  Caddy setup:   $APP_PATH/bin/styles4dogs-install-caddy
EOF_SUMMARY

if [[ -z "$ROOT_PREFIX" && "$START_SERVICE" -eq 1 ]]; then
    "$APP_PATH/bin/styles4dogs-verify"
    echo "First-run admin setup: http://127.0.0.1:31337/setup/admin"
fi
