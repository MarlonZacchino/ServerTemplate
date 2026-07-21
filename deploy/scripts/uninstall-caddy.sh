#!/usr/bin/env bash
set -Eeuo pipefail
umask 027

DISABLE_SERVICE=0

usage() {
    cat <<'USAGE'
Usage: sudo ./deploy/scripts/uninstall-caddy.sh [--disable-service]

Removes only the Styling 4 Dogs Caddy snippet, environment and systemd drop-in.
The Caddy package and unrelated Caddy sites are preserved.
USAGE
}

while (($# > 0)); do
    case "$1" in
        --disable-service)
            DISABLE_SERVICE=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "Run this script as root." >&2
    exit 1
fi

CADDYFILE=/etc/caddy/Caddyfile
BACKUP_SUFFIX=$(date -u +%Y%m%dT%H%M%SZ)

if [[ -f "$CADDYFILE" ]]; then
    cp -a -- "$CADDYFILE" "$CADDYFILE.backup-$BACKUP_SUFFIX"
    awk '
        $0 == "# BEGIN Styling 4 Dogs managed include" { skip = 1; next }
        $0 == "# END Styling 4 Dogs managed include"   { skip = 0; next }
        !skip { print }
    ' "$CADDYFILE" > "$CADDYFILE.tmp"
    mv -- "$CADDYFILE.tmp" "$CADDYFILE"
    chmod 0644 "$CADDYFILE"
fi

rm -f -- \
    /etc/caddy/conf.d/styles4dogs.caddy \
    /etc/styles4dogs/caddy.env \
    /etc/systemd/system/caddy.service.d/styles4dogs.conf

systemctl daemon-reload

if [[ "$DISABLE_SERVICE" -eq 1 ]]; then
    systemctl disable --now caddy.service || true
else
    if systemctl is-active --quiet caddy.service; then
        if caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile; then
            systemctl restart caddy.service
        else
            echo "Remaining Caddy configuration is invalid; Caddy was not restarted." >&2
        fi
    fi
fi

echo "Styling 4 Dogs Caddy integration removed."
