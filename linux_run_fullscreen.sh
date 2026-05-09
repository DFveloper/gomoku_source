#!/usr/bin/env bash
set -euo pipefail

# Edit these values for the target display.
# Use "4:3" when the monitor is detected as 16:9 but the real panel is 4:3.
ASPECT_RATIO="${ASPECT_RATIO:-4:3}"
BOARD_SIZE="${BOARD_SIZE:-15}"
DISPLAY="${DISPLAY:-:0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$SCRIPT_DIR/gomoku_gui"
SRC="$SCRIPT_DIR/AI_Gomoku_GUI.cpp"

export DISPLAY
export GOMOKU_ASPECT="$ASPECT_RATIO"

if [ ! -x "$APP" ] || [ "$SRC" -nt "$APP" ]; then
    g++ -std=c++17 -O2 "$SRC" -o "$APP" -lX11 -lXi
fi

# Give the desktop/X server a short moment during boot.
for _ in $(seq 1 30); do
    if xdpyinfo >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

exec "$APP" --fullscreen --aspect "$ASPECT_RATIO" --board "$BOARD_SIZE"
