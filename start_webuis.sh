#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${GOMOKU_BUILD_DIR:-$SCRIPT_DIR/build}"
STATE_DIR="${GOMOKU_STATE_DIR:-$SCRIPT_DIR/.run}"
LOG_DIR="${GOMOKU_LOG_DIR:-$STATE_DIR/logs}"
HOST="${GOMOKU_HOST:-127.0.0.1}"
GOMOKU_PORT="${GOMOKU_PORT:-18899}"
MATCH_PORT="${GOMOKU_MATCH_PORT:-18900}"
THREADS="${GOMOKU_THREADS:-4}"
PUBLIC_ORIGIN="${GOMOKU_PUBLIC_ORIGIN:-https://aeonthic.com}"
BUILD_JOBS="${GOMOKU_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
USE_USER_SYSTEMD=0
if command -v systemd-run >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    USE_USER_SYSTEMD=1
fi

usage() {
    echo "Usage: $0 {start|stop|restart|status|logs}"
}

pid_file() {
    echo "$STATE_DIR/$1.pid"
}

unit_name() {
    case "$1" in
        gomoku) echo gomoku-webui.service ;;
        gomoku_match) echo gomoku-match-webui.service ;;
    esac
}

is_running() {
    local name="$1" file pid
    if [ "$USE_USER_SYSTEMD" -eq 1 ]; then
        systemctl --user is-active --quiet "$(unit_name "$name")"
        return
    fi
    file="$(pid_file "$name")"
    [ -f "$file" ] || return 1
    pid="$(<"$file")"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

assert_port_free() {
    local port="$1" name="$2"
    if ss -ltn "sport = :$port" 2>/dev/null | tail -n +2 | grep -q .; then
        echo "$name cannot start: 127.0.0.1:$port is already in use." >&2
        return 1
    fi
}

build_apps() {
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$BUILD_DIR" --target gomoku_webui gomoku_match_webui --parallel "$BUILD_JOBS"
}

start_one() {
    local name="$1" binary="$2" port="$3" public_url="$4" health_path="$5"
    local file log pid
    file="$(pid_file "$name")"
    log="$LOG_DIR/$name.log"

    if is_running "$name"; then
        echo "$name is already running (PID $(<"$file"))."
        return 0
    fi
    assert_port_free "$port" "$name"

    if [ "$USE_USER_SYSTEMD" -eq 1 ]; then
        systemd-run --user --quiet --collect --unit "$(unit_name "$name")" \
            --property "WorkingDirectory=$SCRIPT_DIR" \
            --property Restart=on-failure --property RestartSec=2 \
            --property "StandardOutput=append:$log" \
            --property "StandardError=append:$log" \
            "$binary" --host "$HOST" --port "$port" --threads "$THREADS" \
            --public-url "$public_url" --public-mode
        pid=0
        for _ in $(seq 1 50); do
            pid="$(systemctl --user show --property MainPID --value "$(unit_name "$name")")"
            [ "$pid" != 0 ] && break
            sleep 0.1
        done
    else
        nohup "$binary" --host "$HOST" --port "$port" --threads "$THREADS" \
            --public-url "$public_url" --public-mode >>"$log" 2>&1 &
        pid=$!
    fi
    echo "$pid" >"$file"

    for _ in $(seq 1 50); do
        if ! is_running "$name"; then
            echo "$name exited during startup. See $log" >&2
            return 1
        fi
        if curl --silent --fail --max-time 1 "http://$HOST:$port$health_path" >/dev/null; then
            echo "$name started (PID $pid, port $port)."
            return 0
        fi
        sleep 0.1
    done
    echo "$name did not become healthy. See $log" >&2
    return 1
}

stop_one() {
    local name="$1" file pid
    file="$(pid_file "$name")"
    if ! is_running "$name"; then
        rm -f "$file"
        echo "$name is not running."
        return 0
    fi
    if [ "$USE_USER_SYSTEMD" -eq 1 ]; then
        pid="$(systemctl --user show --property MainPID --value "$(unit_name "$name")")"
    else
        pid="$(<"$file")"
    fi
    if [ "$USE_USER_SYSTEMD" -eq 1 ]; then
        systemctl --user stop "$(unit_name "$name")"
    else
        kill "$pid"
    fi
    for _ in $(seq 1 50); do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$file"
            echo "$name stopped."
            return 0
        fi
        sleep 0.1
    done
    if [ "$USE_USER_SYSTEMD" -eq 1 ]; then
        systemctl --user kill --signal=KILL "$(unit_name "$name")" 2>/dev/null || true
    else
        kill -KILL "$pid"
    fi
    rm -f "$file"
    echo "$name was force-stopped."
}

start_all() {
    mkdir -p "$STATE_DIR" "$LOG_DIR"
    build_apps
    start_one gomoku "$BUILD_DIR/gomoku_webui" "$GOMOKU_PORT" \
        "$PUBLIC_ORIGIN/gomoku" /
    if ! start_one gomoku_match "$BUILD_DIR/gomoku_match_webui" "$MATCH_PORT" \
        "$PUBLIC_ORIGIN/gomoku_match" /; then
        stop_one gomoku
        return 1
    fi
    echo "Public routes: $PUBLIC_ORIGIN/gomoku/, $PUBLIC_ORIGIN/gomoku/sangin/, and $PUBLIC_ORIGIN/gomoku_match/"
}

stop_all() {
    stop_one gomoku_match
    stop_one gomoku
}

status_one() {
    local name="$1" port="$2" file
    file="$(pid_file "$name")"
    if is_running "$name"; then
        echo "$name: running (PID $(<"$file"), port $port)"
    else
        echo "$name: stopped"
        return 1
    fi
}

case "${1:-}" in
    start) start_all ;;
    stop) stop_all ;;
    restart) stop_all; start_all ;;
    status)
        status=0
        status_one gomoku "$GOMOKU_PORT" || status=1
        status_one gomoku_match "$MATCH_PORT" || status=1
        exit "$status"
        ;;
    logs)
        mkdir -p "$LOG_DIR"
        tail -n 100 -F "$LOG_DIR/gomoku.log" "$LOG_DIR/gomoku_match.log"
        ;;
    *) usage; exit 2 ;;
esac
