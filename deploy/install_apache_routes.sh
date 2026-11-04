#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE="$SCRIPT_DIR/apache-gomoku-paths.conf"
TARGET="/etc/apache2/conf-available/gomoku-paths-vhost.inc"
VHOST="/etc/apache2/sites-available/aeonthic-docs.conf"
INCLUDE_LINE="    IncludeOptional $TARGET"

if [ "${EUID:-$(id -u)}" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

install -m 0644 "$SOURCE" "$TARGET"
a2enmod proxy proxy_http rewrite >/dev/null
a2disconf gomoku-paths >/dev/null 2>&1 || true

backup=""
if ! grep -Fq "$INCLUDE_LINE" "$VHOST"; then
    backup="$VHOST.bak-gomoku-$(date +%Y%m%d-%H%M%S)"
    cp --preserve=all "$VHOST" "$backup"
    sed -i '\|^[[:space:]]*ErrorLog ${APACHE_LOG_DIR}/aeonthic-docs-ssl-error\.log|i\    IncludeOptional /etc/apache2/conf-available/gomoku-paths-vhost.inc' "$VHOST"
fi

if ! apache2ctl configtest; then
    if [ -n "$backup" ]; then cp --preserve=all "$backup" "$VHOST"; fi
    apache2ctl configtest
    exit 1
fi
systemctl reload apache2

echo "Apache routes installed and reloaded."
