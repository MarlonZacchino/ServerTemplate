#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

DATABASE_FILE=${STYLES4DOGS_DATABASE_FILE:-/var/lib/styles4dogs/styles4dogs.db}
BACKUP_DIR=${STYLES4DOGS_BACKUP_DIR:-/var/backups/styles4dogs}
RETENTION_DAYS=${STYLES4DOGS_BACKUP_RETENTION_DAYS:-30}
LOCK_FILE=${STYLES4DOGS_BACKUP_LOCK:-/run/lock/styles4dogs-backup.lock}
CUSTOMER_PORTAL_KEY_FILE=${STYLES4DOGS_CUSTOMER_PORTAL_KEY_FILE:-/etc/styles4dogs/secrets/customer-portal.key}

usage() {
    cat <<'USAGE'
Usage: styles4dogs-backup [options]

Options:
  --database PATH       SQLite database to back up
  --output-dir PATH     Backup destination directory
  --retention-days N    Delete automatic backups older than N days (default: 30)
  --help                Show this help

The script uses SQLite's online backup command. The web server does not need to
be stopped. The resulting database is checked and receives a SHA-256 file.
USAGE
}

fail() {
    echo "Backup failed: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --database)
            (($# >= 2)) || fail "--database requires a path"
            DATABASE_FILE=$2
            shift 2
            ;;
        --output-dir)
            (($# >= 2)) || fail "--output-dir requires a path"
            BACKUP_DIR=$2
            shift 2
            ;;
        --retention-days)
            (($# >= 2)) || fail "--retention-days requires a number"
            RETENTION_DAYS=$2
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

command -v sqlite3 >/dev/null 2>&1 || fail "sqlite3 is not installed"
command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is not installed"
command -v flock >/dev/null 2>&1 || fail "flock is not installed"

[[ -f "$DATABASE_FILE" ]] || fail "database does not exist: $DATABASE_FILE"
[[ "$RETENTION_DAYS" =~ ^[0-9]+$ ]] || fail "retention days must be a non-negative integer"
[[ "$BACKUP_DIR" != *$'\n'* && "$BACKUP_DIR" != *'"'* ]] || fail "unsupported character in backup path"

install -d -m 0750 -- "$BACKUP_DIR"
LOCK_DIR=$(dirname -- "$LOCK_FILE")
if [[ ! -d "$LOCK_DIR" ]]; then
    install -d -m 0700 -- "$LOCK_DIR"
fi

exec 9>"$LOCK_FILE"
flock -n 9 || fail "another backup is already running"

TIMESTAMP=$(date -u +%Y%m%dT%H%M%S-%NZ)
FINAL_FILE="$BACKUP_DIR/styles4dogs-$TIMESTAMP.db"
TEMP_FILE="$BACKUP_DIR/.styles4dogs-$TIMESTAMP.$$.tmp"
CHECKSUM_FILE="$FINAL_FILE.sha256"
PORTAL_KEY_BACKUP="$FINAL_FILE.customer-portal.key"

cleanup() {
    rm -f -- "$TEMP_FILE"
}
trap cleanup EXIT INT TERM

sqlite3 "$DATABASE_FILE" <<SQL
.timeout 10000
.backup "$TEMP_FILE"
SQL

chmod 0600 -- "$TEMP_FILE"

INTEGRITY_RESULT=$(sqlite3 "$TEMP_FILE" 'PRAGMA integrity_check;')
[[ "$INTEGRITY_RESULT" == "ok" ]] || fail "backup integrity check returned: $INTEGRITY_RESULT"

mv -- "$TEMP_FILE" "$FINAL_FILE"

if [[ -f "$CUSTOMER_PORTAL_KEY_FILE" ]]; then
    [[ "$(stat -c '%s' "$CUSTOMER_PORTAL_KEY_FILE")" == "32" ]] || \
        fail "customer portal key has an unexpected size"
    install -m 0600 -- "$CUSTOMER_PORTAL_KEY_FILE" "$PORTAL_KEY_BACKUP"
fi

(
    cd -- "$BACKUP_DIR"
    sha256sum -- "$(basename -- "$FINAL_FILE")" > "$(basename -- "$CHECKSUM_FILE")"
    if [[ -f "$PORTAL_KEY_BACKUP" ]]; then
        sha256sum -- "$(basename -- "$PORTAL_KEY_BACKUP")" >> "$(basename -- "$CHECKSUM_FILE")"
    fi
)
chmod 0600 -- "$FINAL_FILE" "$CHECKSUM_FILE"
[[ ! -f "$PORTAL_KEY_BACKUP" ]] || chmod 0600 -- "$PORTAL_KEY_BACKUP"

if ((RETENTION_DAYS > 0)); then
    find "$BACKUP_DIR" -maxdepth 1 -type f \
        \( -name 'styles4dogs-*.db' -o -name 'styles4dogs-*.db.sha256' -o -name 'styles4dogs-*.db.customer-portal.key' \) \
        -mtime "+$RETENTION_DAYS" -delete
fi

trap - EXIT INT TERM
printf '%s\n' "$FINAL_FILE"
