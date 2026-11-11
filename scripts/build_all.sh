#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${GOMOKU_BUILD_DIR:-$PROJECT_ROOT/build}"
BUILD_TYPE="${GOMOKU_BUILD_TYPE:-Release}"

usage() {
    cat <<'EOF'
Usage: scripts/build_all.sh [options]

Configure the project and build all default CMake targets, including the
engine-development tools (build/tools/ab_match, build/tools/id_soundness).

Options:
  --debug           Build with CMAKE_BUILD_TYPE=Debug
  --release         Build with CMAKE_BUILD_TYPE=Release (default)
  --build-dir DIR   Use a custom CMake build directory
  -h, --help        Show this help

Environment:
  GOMOKU_BUILD_DIR   Default build directory
  GOMOKU_BUILD_TYPE  Default CMake build type
  GOMOKU_BUILD_JOBS  Parallel build job count
EOF
}

while (($#)); do
    case "$1" in
        --debug)
            BUILD_TYPE=Debug
            shift
            ;;
        --release)
            BUILD_TYPE=Release
            shift
            ;;
        --build-dir)
            if (($# < 2)); then
                echo "Missing directory after --build-dir" >&2
                exit 2
            fi
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake was not found in PATH" >&2
    exit 1
fi

if [[ "${OSTYPE:-}" == linux* ]]; then
    if ! command -v c++ >/dev/null 2>&1; then
        echo "A C++ compiler was not found in PATH" >&2
        exit 1
    fi
    if ! printf '#include <X11/extensions/XInput2.h>\n' | c++ -E -x c++ - >/dev/null 2>&1; then
        echo "XInput2 development headers were not found." >&2
        echo "Ubuntu/Debian: sudo apt install build-essential libx11-dev libxi-dev" >&2
        exit 1
    fi
fi

echo "== Configure all targets ($BUILD_TYPE) =="
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== Build all default targets =="
if [[ -n "${GOMOKU_BUILD_JOBS:-}" ]]; then
    cmake --build "$BUILD_DIR" --parallel "$GOMOKU_BUILD_JOBS"
else
    cmake --build "$BUILD_DIR" --parallel
fi

echo "Build complete: $BUILD_DIR"
