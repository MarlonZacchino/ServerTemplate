#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
STAGING_ROOT=$(mktemp -d)
TEST_DATABASE="$STAGING_ROOT/test-source.db"
RESTORED_DATABASE="$STAGING_ROOT/restored/styles4dogs.db"
SOURCE_PORTAL_KEY="$STAGING_ROOT/source-customer-portal.key"
RESTORED_PORTAL_KEY="$STAGING_ROOT/restored-secrets/customer-portal.key"

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
[[ -x "$STAGING_ROOT/opt/styles4dogs/bin/notification_worker" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/index.html" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/galerie.html" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/gallery.js" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/postal-code.js" ]]
[[ -f "$STAGING_ROOT/var/www/styles4dogs/logo.jpg" ]]
[[ -f "$STAGING_ROOT/etc/styles4dogs/server.env" ]]
[[ -f "$STAGING_ROOT/etc/styles4dogs/notification.env" ]]
[[ "$(stat -c '%a' "$STAGING_ROOT/etc/styles4dogs/notification.env")" == "640" ]]
[[ -f "$STAGING_ROOT/etc/systemd/system/styles4dogs.service" ]]
[[ -f "$STAGING_ROOT/etc/systemd/system/styles4dogs-notification.service" ]]
[[ -f "$STAGING_ROOT/etc/systemd/system/styles4dogs-notification.timer" ]]
[[ -x "$STAGING_ROOT/opt/styles4dogs/bin/styles4dogs-install-caddy" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/CADDY_DEPLOYMENT.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/RATE_LIMITING.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/CALENDAR_PHASE5.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/CALENDAR_PHASE6.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/GALLERY_PHASE8.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/CUSTOMER_PORTAL_PHASE9.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/ADDRESS_PHASE10.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/BOOKING_WORKFLOW_PHASE13.md" ]]
[[ -f "$STAGING_ROOT/opt/styles4dogs/share/NOTIFICATIONS.md" ]]

grep -Fq "STYLES4DOGS_DOCUMENT_ROOT=$STAGING_ROOT/var/www/styles4dogs" \
    "$STAGING_ROOT/etc/styles4dogs/server.env"
grep -Eq '^STYLES4DOGS_TRUSTED_PROXY_TOKEN=[A-Za-z0-9_-]{32,128}$' \
    "$STAGING_ROOT/etc/styles4dogs/server.env"
grep -Fq 'STYLES4DOGS_SALON_NAME=Styling 4 Dogs' \
    "$STAGING_ROOT/etc/styles4dogs/server.env"
grep -Fq 'STYLES4DOGS_DEFAULT_PHONE_COUNTRY_CODE=49' \
    "$STAGING_ROOT/etc/styles4dogs/server.env"
grep -Fq 'STYLES4DOGS_POSTAL_LOOKUP_BASE_URL=http://127.0.0.1:31339/de/Localities' \
    "$STAGING_ROOT/etc/styles4dogs/server.env"
grep -Fq 'STYLES4DOGS_SMTP_FROM_NAME=Styling 4 Dogs' \
    "$STAGING_ROOT/etc/styles4dogs/notification.env"
grep -Fq 'STYLES4DOGS_NOTIFY_ADMIN_NEW_BOOKING=0' \
    "$STAGING_ROOT/etc/styles4dogs/notification.env"
grep -Fq 'STYLES4DOGS_NOTIFICATIONS_ENABLED=1' \
    "$STAGING_ROOT/etc/styles4dogs/notification.env"

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
grep -Eq '^STYLES4DOGS_TRUSTED_PROXY_TOKEN=[A-Za-z0-9_-]{32,128}$' \
    "$STAGING_ROOT/etc/styles4dogs/caddy.env"
SERVER_TOKEN=$(awk -F= '$1 == "STYLES4DOGS_TRUSTED_PROXY_TOKEN" {print $2}' \
    "$STAGING_ROOT/etc/styles4dogs/server.env")
CADDY_TOKEN=$(awk -F= '$1 == "STYLES4DOGS_TRUSTED_PROXY_TOKEN" {print $2}' \
    "$STAGING_ROOT/etc/styles4dogs/caddy.env")
[[ "$SERVER_TOKEN" == "$CADDY_TOKEN" ]]
grep -Fq 'header_up X-Styles4Dogs-Proxy-Token {$STYLES4DOGS_TRUSTED_PROXY_TOKEN}' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq '@gallery_upload_body path /admin/gallery/upload' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq 'log_skip /buchung/*' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq 'replace postal_code REDACTED' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq '>Referrer-Policy "no-referrer"' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq 'max_size 9MiB' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq 'http://127.0.0.1:31339' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"
grep -Fq 'reverse_proxy https://openplzapi.org' \
    "$STAGING_ROOT/etc/caddy/conf.d/styles4dogs.caddy"

if command -v systemd-analyze >/dev/null 2>&1; then
    systemd-analyze verify --recursive-errors=no --root="$STAGING_ROOT" \
        styles4dogs.service \
        styles4dogs-backup.service \
        styles4dogs-backup.timer \
        styles4dogs-notification.service \
        styles4dogs-notification.timer
fi

head -c 32 /dev/urandom > "$SOURCE_PORTAL_KEY"
chmod 0600 "$SOURCE_PORTAL_KEY"

sqlite3 "$TEST_DATABASE" <<'SQL'
CREATE TABLE smoke_test (id INTEGER PRIMARY KEY, value TEXT NOT NULL);
INSERT INTO smoke_test(value) VALUES ('backup works');
SQL

BACKUP_FILE=$(
    STYLES4DOGS_BACKUP_LOCK="$STAGING_ROOT/backup.lock" \
    STYLES4DOGS_CUSTOMER_PORTAL_KEY_FILE="$SOURCE_PORTAL_KEY" \
    "$PROJECT_ROOT/deploy/scripts/backup.sh" \
        --database "$TEST_DATABASE" \
        --output-dir "$STAGING_ROOT/backups" \
        --retention-days 0
)

[[ -f "$BACKUP_FILE" ]]
[[ -f "$BACKUP_FILE.sha256" ]]
[[ -f "$BACKUP_FILE.customer-portal.key" ]]
cmp -s "$SOURCE_PORTAL_KEY" "$BACKUP_FILE.customer-portal.key"

mkdir -p "$(dirname -- "$RESTORED_DATABASE")"
STYLES4DOGS_SKIP_SYSTEMD=1 \
STYLES4DOGS_DATABASE_FILE="$RESTORED_DATABASE" \
STYLES4DOGS_BACKUP_DIR="$STAGING_ROOT/restore-safety" \
STYLES4DOGS_CUSTOMER_PORTAL_KEY_FILE="$RESTORED_PORTAL_KEY" \
    "$PROJECT_ROOT/deploy/scripts/restore.sh" \
    --backup "$BACKUP_FILE" \
    --database "$RESTORED_DATABASE" \
    --backup-dir "$STAGING_ROOT/restore-safety" \
    --no-systemd \
    --yes

RESTORED_VALUE=$(sqlite3 "$RESTORED_DATABASE" 'SELECT value FROM smoke_test;')
[[ "$RESTORED_VALUE" == "backup works" ]]
[[ -f "$RESTORED_PORTAL_KEY" ]]
[[ "$(stat -c '%a' "$RESTORED_PORTAL_KEY")" == "600" ]]
cmp -s "$SOURCE_PORTAL_KEY" "$RESTORED_PORTAL_KEY"

echo "Deployment, Caddy staging, backup and restore tests: OK"
