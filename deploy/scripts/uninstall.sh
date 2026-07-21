#!/usr/bin/env bash
set -Eeuo pipefail

PURGE_DATA=0
CONFIRM_PURGE=${STYLES4DOGS_CONFIRM_PURGE:-}

usage() {
    cat <<'USAGE'
Usage: sudo ./deploy/scripts/uninstall.sh [options]

Options:
  --purge-data   Also delete configuration, secrets, database and backups.
                 Requires STYLES4DOGS_CONFIRM_PURGE=YES.
  --help         Show this help

Without --purge-data, customer data and configuration are deliberately kept.
USAGE
}

fail() {
    echo "Uninstall failed: $*" >&2
    exit 1
}

while (($# > 0)); do
    case "$1" in
        --purge-data)
            PURGE_DATA=1
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

[[ $EUID -eq 0 ]] || fail "run this script as root"

if [[ "$PURGE_DATA" -eq 1 && "$CONFIRM_PURGE" != "YES" ]]; then
    fail "set STYLES4DOGS_CONFIRM_PURGE=YES to confirm permanent deletion"
fi

systemctl disable --now styles4dogs-notification.timer 2>/dev/null || true
systemctl stop styles4dogs-notification.service 2>/dev/null || true
systemctl disable --now styles4dogs-backup.timer 2>/dev/null || true
systemctl stop styles4dogs-backup.service 2>/dev/null || true
systemctl disable --now styles4dogs.service 2>/dev/null || true

rm -f -- \
    /etc/systemd/system/styles4dogs.service \
    /etc/systemd/system/styles4dogs-backup.service \
    /etc/systemd/system/styles4dogs-backup.timer \
    /etc/systemd/system/styles4dogs-notification.service \
    /etc/systemd/system/styles4dogs-notification.timer
rm -rf -- /opt/styles4dogs /var/www/styles4dogs

systemctl daemon-reload
systemctl reset-failed styles4dogs.service styles4dogs-backup.service \
    styles4dogs-notification.service 2>/dev/null || true

if [[ "$PURGE_DATA" -eq 1 ]]; then
    rm -rf -- /etc/styles4dogs /var/lib/styles4dogs /var/backups/styles4dogs
    userdel styles4dogs 2>/dev/null || true
    groupdel styles4dogs 2>/dev/null || true
    echo "Styles 4 Dogs and all runtime data were removed."
else
    echo "Styles 4 Dogs was uninstalled. Configuration and customer data were preserved:"
    echo "  /etc/styles4dogs"
    echo "  /var/lib/styles4dogs"
    echo "  /var/backups/styles4dogs"
fi
