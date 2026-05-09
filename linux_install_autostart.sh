#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER="$SCRIPT_DIR/linux_run_fullscreen.sh"
AUTOSTART_DIR="$HOME/.config/autostart"
DESKTOP_FILE="$AUTOSTART_DIR/gomoku-gui.desktop"

chmod +x "$RUNNER"
mkdir -p "$AUTOSTART_DIR"

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Gomoku GUI Fullscreen
Comment=Start Gomoku GUI in fullscreen mode
Exec=$RUNNER
Terminal=false
X-GNOME-Autostart-enabled=true
EOF

echo "Installed desktop autostart:"
echo "$DESKTOP_FILE"
echo
echo "Edit $RUNNER to change ASPECT_RATIO or BOARD_SIZE."
echo "The Linux account still needs to auto-login into an X11 desktop session."
