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
[[ -x "$STAGING_ROOT/opt/styles4dogs/bin/styles4dogs-install-caddy" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/CADDY_DEPLOYMENT.md" ]]

grep -Fq "STYLES4DOGS_DOCUMENT_ROOT=$STAGING_ROOT/var/www/styles4dogs" \
    "$STAGING_ROOT/etc/styles4dogs/server.env"

# Simulate the Arch Linux package Caddyfile, which already imports the whole
# conf.d directory. The installer must reuse this import instead of adding a
# second, overlapping *.caddy import.
mkdir -p "$STAGING_ROOT/etc/caddy/conf.d"
cat > "$STAGING_ROOT/etc/caddy/Caddyfile" <<'EOF_CADDYFILE'
{
    admin unix//run/caddy/admin.socket
}

import /etc/caddy/conf.d/*
EOF_CADDYFILE

"$PROJECT_ROOT/deploy/scripts/install-caddy.sh" \
    --staging-root "$STAGING_ROOT" \
    --site-address http://127.0.0.1:18080 \
    --upstream 127.0.0.1:31337 \
    --no-start

[[ -f "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy" ]]
[[ -f "$STAGING_ROOT/etc/styles4dogs/caddy.env" ]]
[[ -f "$STAGING_ROOT/etc/systemd/system/caddy.service.d/styles4dogs.conf" ]]
grep -Fq "import /etc/caddy/conf.d/*" \
    "$STAGING_ROOT/etc/caddy/Caddyfile"
! grep -Fq "import /etc/caddy/conf.d/*.caddy" \
    "$STAGING_ROOT/etc/caddy/Caddyfile"
[[ -f "$STAGING_ROOT/var/log/caddy/styles4dogs-access.log" ]]
[[ "$(stat -c '%a' "$STAGING_ROOT/var/log/caddy/styles4dogs-access.log")" == "640" ]]
grep -Fq "STYLES4DOGS_CADDY_SITE_ADDRESS=http://127.0.0.1:18080" \
    "$STAGING_ROOT/etc/styles4dogs/caddy.env"

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

echo "Deployment, Caddy staging, backup and restore tests: OK"
