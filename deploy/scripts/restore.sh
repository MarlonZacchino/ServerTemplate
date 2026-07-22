#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DATABASE_FILE=${STYLES4DOGS_DATABASE_FILE:-/var/lib/styles4dogs/styles4dogs.db}
BACKUP_DIR=${STYLES4DOGS_BACKUP_DIR:-/var/backups/styles4dogs}
SERVICE_NAME=${STYLES4DOGS_SERVICE_NAME:-styles4dogs.service}
SERVICE_USER=${STYLES4DOGS_SERVICE_USER:-styles4dogs}
SERVICE_GROUP=${STYLES4DOGS_SERVICE_GROUP:-styles4dogs}
SKIP_SYSTEMD=${STYLES4DOGS_SKIP_SYSTEMD:-0}
BACKUP_FILE=""
CONFIRMED=0
CUSTOMER_PORTAL_KEY_FILE=${STYLES4DOGS_CUSTOMER_PORTAL_KEY_FILE:-/etc/styles4dogs/secrets/customer-portal.key}

usage() {
    cat <<'USAGE'
Usage: styles4dogs-restore --backup FILE [options]

Options:
  --backup FILE         Valid SQLite backup to restore (required)
  --database PATH       Target SQLite database
  --backup-dir PATH     Directory for the automatic pre-restore safety backup
  --service NAME        systemd service name (default: styles4dogs.service)
  --yes                 Confirm the destructive replacement
  --no-systemd          Do not stop or start a service (test/recovery mode)
  --help                Show this help

The script validates the backup, creates a safety backup of the current
database, stops the service, atomically replaces the database and rolls back if
the service cannot be started afterwards.
USAGE
}

fail() {
    echo "Restore failed: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --backup)
            (($# >= 2)) || fail "--backup requires a file"
            BACKUP_FILE=$2
            shift 2
            ;;
        --database)
            (($# >= 2)) || fail "--database requires a path"
            DATABASE_FILE=$2
            shift 2
            ;;
        --backup-dir)
            (($# >= 2)) || fail "--backup-dir requires a path"
            BACKUP_DIR=$2
            shift 2
            ;;
        --service)
            (($# >= 2)) || fail "--service requires a name"
            SERVICE_NAME=$2
            shift 2
            ;;
        --yes)
            CONFIRMED=1
            shift
            ;;
        --no-systemd)
            SKIP_SYSTEMD=1
            shift
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

[[ -n "$BACKUP_FILE" ]] || fail "--backup is required"
[[ "$CONFIRMED" -eq 1 ]] || fail "pass --yes after verifying the selected backup"
[[ -f "$BACKUP_FILE" ]] || fail "backup does not exist: $BACKUP_FILE"
command -v sqlite3 >/dev/null 2>&1 || fail "sqlite3 is not installed"

if [[ -f "$BACKUP_FILE.sha256" ]]; then
    command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is not installed"
    (
        cd -- "$(dirname -- "$BACKUP_FILE")"
        sha256sum -c -- "$(basename -- "$BACKUP_FILE.sha256")"
    ) || fail "backup checksum verification failed"
fi

PORTAL_KEY_BACKUP="$BACKUP_FILE.customer-portal.key"
if [[ -f "$PORTAL_KEY_BACKUP" && "$(stat -c '%s' "$PORTAL_KEY_BACKUP")" != "32" ]]; then
    fail "customer portal key backup has an unexpected size"
fi

INTEGRITY_RESULT=$(sqlite3 "$BACKUP_FILE" 'PRAGMA integrity_check;')
[[ "$INTEGRITY_RESULT" == "ok" ]] || fail "backup integrity check returned: $INTEGRITY_RESULT"

if [[ "$SKIP_SYSTEMD" != 1 && $EUID -ne 0 ]]; then
    fail "run as root or use --no-systemd for an isolated recovery"
fi

TARGET_DIR=$(dirname -- "$DATABASE_FILE")
install -d -m 0750 -- "$TARGET_DIR" "$BACKUP_DIR"

BACKUP_COMMAND="$SCRIPT_DIR/styles4dogs-backup"
if [[ ! -x "$BACKUP_COMMAND" ]]; then
    BACKUP_COMMAND="$SCRIPT_DIR/backup.sh"
fi

SAFETY_BACKUP=""
if [[ -f "$DATABASE_FILE" ]]; then
    SAFETY_BACKUP=$(
        "$BACKUP_COMMAND" \
            --database "$DATABASE_FILE" \
            --output-dir "$BACKUP_DIR" \
            --retention-days 0
    )
fi

WAS_ACTIVE=0
if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        WAS_ACTIVE=1
    fi
    systemctl stop "$SERVICE_NAME"
fi

TEMP_FILE="$TARGET_DIR/.styles4dogs-restore.$$.tmp"
cleanup() {
    rm -f -- "$TEMP_FILE"
}
trap cleanup EXIT INT TERM

install -m 0600 -- "$BACKUP_FILE" "$TEMP_FILE"
if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    chown "$SERVICE_USER:$SERVICE_GROUP" "$TEMP_FILE"
fi

rm -f -- "$DATABASE_FILE-wal" "$DATABASE_FILE-shm"
mv -f -- "$TEMP_FILE" "$DATABASE_FILE"

if [[ -f "$PORTAL_KEY_BACKUP" ]]; then
    PORTAL_KEY_DIR=$(dirname -- "$CUSTOMER_PORTAL_KEY_FILE")
    install -d -m 0700 -- "$PORTAL_KEY_DIR"
    install -m 0600 -- "$PORTAL_KEY_BACKUP" "$CUSTOMER_PORTAL_KEY_FILE"
fi

if [[ "$SKIP_SYSTEMD" != 1 ]]; then
    chown "$SERVICE_USER:$SERVICE_GROUP" "$DATABASE_FILE"
    chmod 0600 "$DATABASE_FILE"
    if [[ -f "$PORTAL_KEY_BACKUP" ]]; then
        chown "$SERVICE_USER:$SERVICE_GROUP" "$CUSTOMER_PORTAL_KEY_FILE"
        chmod 0600 "$CUSTOMER_PORTAL_KEY_FILE"
    fi

    if ! systemctl start "$SERVICE_NAME" || ! systemctl is-active --quiet "$SERVICE_NAME"; then
        echo "The restored database did not start successfully; rolling back." >&2
        systemctl stop "$SERVICE_NAME" 2>/dev/null || true

        if [[ -n "$SAFETY_BACKUP" && -f "$SAFETY_BACKUP" ]]; then
            install -m 0600 -o "$SERVICE_USER" -g "$SERVICE_GROUP" -- \
                "$SAFETY_BACKUP" "$DATABASE_FILE"
            if [[ -f "$SAFETY_BACKUP.customer-portal.key" ]]; then
                install -d -m 0700 -o "$SERVICE_USER" -g "$SERVICE_GROUP" -- \
                    "$(dirname -- "$CUSTOMER_PORTAL_KEY_FILE")"
                install -m 0600 -o "$SERVICE_USER" -g "$SERVICE_GROUP" -- \
                    "$SAFETY_BACKUP.customer-portal.key" "$CUSTOMER_PORTAL_KEY_FILE"
            elif [[ -f "$PORTAL_KEY_BACKUP" ]]; then
                rm -f -- "$CUSTOMER_PORTAL_KEY_FILE"
            fi
            rm -f -- "$DATABASE_FILE-wal" "$DATABASE_FILE-shm"
            systemctl start "$SERVICE_NAME" || true
        fi

        fail "service startup failed after restore"
    fi
elif [[ "$WAS_ACTIVE" -eq 1 ]]; then
    :
fi

trap - EXIT INT TERM
printf 'Restore completed: %s\n' "$DATABASE_FILE"
[[ -z "$SAFETY_BACKUP" ]] || printf 'Pre-restore safety backup: %s\n' "$SAFETY_BACKUP"
