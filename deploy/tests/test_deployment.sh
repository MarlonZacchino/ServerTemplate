#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
STAGING_ROOT=$(mktemp -d)
TEST_DATABASE="$STAGING_ROOT/test-source.db"
RESTORED_DATABASE="$STAGING_ROOT/restored/styles4dogs.db"

cleanup() {
    rm -rf -- "$STAGING_ROOT"
}
trap cleanup EXIT INT TERM

for script in "$PROJECT_ROOT"/deploy/scripts/*.sh "$PROJECT_ROOT"/deploy/tests/*.sh; do
    bash -n "$script"
done

STYLES4DOGS_BUILD_DIR="$PROJECT_ROOT/cmake-build-deploy-test" \
    "$PROJECT_ROOT/deploy/scripts/install.sh" \
    --staging-root "$STAGING_ROOT" \
    --no-start \
    --no-backup-timer

[[ -x "$STAGING_ROOT/opt/styles4dogs/bin/Server" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/index.html" ]]
[[ -f "$STAGING_ROOT/etc/styles4dogs/server.env" ]]
[[ -f "$STAGING_ROOT/etc/systemd/system/styles4dogs.service" ]]

grep -Fq "STYLES4DOGS_DOCUMENT_ROOT=$STAGING_ROOT/var/www/styles4dogs" \
    "$STAGING_ROOT/etc/styles4dogs/server.env"


if command -v systemd-analyze >/dev/null 2>&1; then
    systemd-analyze verify --recursive-errors=no --root="$STAGING_ROOT" \
        styles4dogs.service \
        styles4dogs-backup.service \
        styles4dogs-backup.timer
fi

sqlite3 "$TEST_DATABASE" <<'SQL'
CREATE TABLE smoke_test (id INTEGER PRIMARY KEY, value TEXT NOT NULL);
INSERT INTO smoke_test(value) VALUES ('backup works');
SQL

BACKUP_FILE=$(
    STYLES4DOGS_BACKUP_LOCK="$STAGING_ROOT/backup.lock" \
    "$PROJECT_ROOT/deploy/scripts/backup.sh" \
        --database "$TEST_DATABASE" \
        --output-dir "$STAGING_ROOT/backups" \
        --retention-days 0
)

[[ -f "$BACKUP_FILE" ]]
[[ -f "$BACKUP_FILE.sha256" ]]

mkdir -p "$(dirname -- "$RESTORED_DATABASE")"
STYLES4DOGS_SKIP_SYSTEMD=1 \
STYLES4DOGS_DATABASE_FILE="$RESTORED_DATABASE" \
STYLES4DOGS_BACKUP_DIR="$STAGING_ROOT/restore-safety" \
    "$PROJECT_ROOT/deploy/scripts/restore.sh" \
    --backup "$BACKUP_FILE" \
    --database "$RESTORED_DATABASE" \
    --backup-dir "$STAGING_ROOT/restore-safety" \
    --no-systemd \
    --yes

RESTORED_VALUE=$(sqlite3 "$RESTORED_DATABASE" 'SELECT value FROM smoke_test;')
[[ "$RESTORED_VALUE" == "backup works" ]]

echo "Deployment staging, backup and restore tests: OK"
