#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${GOMOKU_BUILD_DIR:-$PROJECT_ROOT/build}"

if [ "${EUID:-$(id -u)}" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

SERVICE_USER="${GOMOKU_SERVICE_USER:-${SUDO_USER:-$(stat -c '%U' "$PROJECT_ROOT")}}"
SERVICE_GROUP="$(id -gn "$SERVICE_USER")"
ENV_FILE=/etc/default/aeonthic-gomoku

if ! id "$SERVICE_USER" >/dev/null 2>&1; then
    echo "Service account does not exist: $SERVICE_USER" >&2
    exit 1
fi

run_as_service_user() {
    if [ "$SERVICE_USER" = root ]; then
        "$@"
    else
        runuser -u "$SERVICE_USER" -- "$@"
    fi
}

echo "Building WebUI programs..."
run_as_service_user cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
run_as_service_user cmake --build "$BUILD_DIR" --target gomoku_webui gomoku_match_webui --parallel

# Stop processes launched by the legacy helper before systemd claims the ports.
run_as_service_user "$PROJECT_ROOT/start_webuis.sh" stop >/dev/null 2>&1 || true

if [ ! -e "$ENV_FILE" ]; then
    install -m 0644 "$SCRIPT_DIR/aeonthic-gomoku.env" "$ENV_FILE"
fi

tmp_main="$(mktemp)"
tmp_match="$(mktemp)"
cleanup() { rm -f "$tmp_main" "$tmp_match"; }
trap cleanup EXIT

sed -e "s|@USER@|$SERVICE_USER|g" \
    -e "s|@GROUP@|$SERVICE_GROUP|g" \
    -e "s|@ROOT@|$PROJECT_ROOT|g" \
    -e "s|@BUILD@|$BUILD_DIR|g" >"$tmp_main" <<'EOF'
[Unit]
Description=Aeonthic Gomoku and Sangin WebUI
After=network.target

[Service]
Type=simple
User=@USER@
Group=@GROUP@
WorkingDirectory=@ROOT@
EnvironmentFile=/etc/default/aeonthic-gomoku
ExecStart=@BUILD@/gomoku_webui --host ${GOMOKU_HOST} --port ${GOMOKU_PORT} --threads ${GOMOKU_THREADS} --public-url ${GOMOKU_PUBLIC_ORIGIN}/gomoku --public-mode
Restart=on-failure
RestartSec=2
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

sed -e "s|@USER@|$SERVICE_USER|g" \
    -e "s|@GROUP@|$SERVICE_GROUP|g" \
    -e "s|@ROOT@|$PROJECT_ROOT|g" \
    -e "s|@BUILD@|$BUILD_DIR|g" >"$tmp_match" <<'EOF'
[Unit]
Description=Aeonthic Gomoku engine-match WebUI
After=network.target

[Service]
Type=simple
User=@USER@
Group=@GROUP@
WorkingDirectory=@ROOT@
EnvironmentFile=/etc/default/aeonthic-gomoku
ExecStart=@BUILD@/gomoku_match_webui --host ${GOMOKU_HOST} --port ${GOMOKU_MATCH_PORT} --threads ${GOMOKU_THREADS} --public-url ${GOMOKU_PUBLIC_ORIGIN}/gomoku_match --public-mode
Restart=on-failure
RestartSec=2
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

install -m 0644 "$tmp_main" /etc/systemd/system/aeonthic-gomoku.service
install -m 0644 "$tmp_match" /etc/systemd/system/aeonthic-gomoku-match.service
systemctl daemon-reload
systemctl enable --now aeonthic-gomoku.service aeonthic-gomoku-match.service

# shellcheck disable=SC1090
source "$ENV_FILE"
for _ in $(seq 1 50); do
    if curl --silent --fail --max-time 1 "http://$GOMOKU_HOST:$GOMOKU_PORT/sangin/" >/dev/null &&
       curl --silent --fail --max-time 1 "http://$GOMOKU_HOST:$GOMOKU_MATCH_PORT/" >/dev/null; then
        echo "Autostart installed and both WebUIs are running."
        echo "Sangin is internally routed at ${GOMOKU_PUBLIC_ORIGIN}/gomoku/sangin/"
        exit 0
    fi
    sleep 0.1
done

systemctl --no-pager --full status aeonthic-gomoku.service aeonthic-gomoku-match.service || true
echo "Services were installed but did not become healthy." >&2
exit 1
