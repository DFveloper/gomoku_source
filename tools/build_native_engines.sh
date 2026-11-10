#!/usr/bin/env bash
# Builds native copies of the external engines for THIS host and drops them next
# to the bundled runtimes, where tools/duel picks them up automatically.
#
#   scripts/build_native_engines.sh rapfi
#   scripts/build_native_engines.sh xl
#
# Why: third_party/runtime/* binaries are built for a newer glibc than many
# hosts provide (they fail with "GLIBC_2.38 not found" on Debian 12), and Rapfi
# additionally wants -march=native to reach its AVX2/AVX-512 code paths. The
# bundled binaries are never modified; the native build is written beside them
# with a `.native-<glibc>` suffix and the referee prefers it.
#
# Notes per engine:
#   rapfi  builds from Rapfi/CMakeLists.txt with -DNO_COMMAND_MODULES=ON, which
#          drops the private command modules and their git submodule deps.
#          Requires cmake; the NNUE weights are read from the runtime directory,
#          which is also the process working directory, so no network copy is
#          needed.
#   xl     needs third_party/compat/windows.h (the repo ships it) and a couple
#          of std::min/max qualifications. It is Renju-only and, on Linux,
#          answers far weaker than the bundled Windows build.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="${root}/third_party/runtime"
work="${GOMOKU_NATIVE_WORK:-/tmp/gomoku-native}"
glibc="$(ldd --version | head -1 | awk '{print $NF}' | tr -d '.')"
mkdir -p "${work}"

fetch() {
    local url="$1" dest="$2"
    if [ ! -d "${dest}" ]; then
        echo "cloning ${url}"
        git clone --depth 1 "${url}" "${dest}"
    fi
}

build_rapfi() {
    fetch https://github.com/dhbloo/rapfi.git "${work}/rapfi"
    command -v cmake >/dev/null 2>&1 || { echo "cmake is required for rapfi" >&2; exit 1; }
    cmake -S "${work}/rapfi/Rapfi" -B "${work}/rapfi-build" \
        -DCMAKE_BUILD_TYPE=Release -DNO_COMMAND_MODULES=ON >/dev/null
    cmake --build "${work}/rapfi-build" --parallel "$(nproc)"
    install -m 755 "${work}/rapfi-build/pbrain-rapfi" \
        "${runtime}/rapfi/pbrain-rapfi.native-glibc${glibc}"
    echo "installed ${runtime}/rapfi/pbrain-rapfi.native-glibc${glibc}"
}

build_xl() {
    fetch https://github.com/accreator/xl-engine.git "${work}/xl"
    # The tree is a Windows-oriented port; the repo's own compat header and a
    # std::min/max qualification are enough to compile it on Linux.
    sed -i 's|#include <windows.h>|#include "third_party/compat/windows.h"|' \
        "${work}/xl/xl.cpp" "${work}/xl/pisqpipe.cpp"
    sed -i 's|#include "third_party/compat/windows.h"|#include "third_party/compat/windows.h"\n#include <algorithm>\nusing std::min;\nusing std::max;|' \
        "${work}/xl/pisqpipe.cpp"
    ( cd "${work}/xl" && g++ -std=c++11 -O2 -w -I"${root}" \
        xl.cpp xl_ai.cpp pisqpipe.cpp -o pbrain-xl -lpthread )
    install -m 755 "${work}/xl/pbrain-xl" "${runtime}/xl/pbrain-xl.native-glibc${glibc}"
    echo "installed ${runtime}/xl/pbrain-xl.native-glibc${glibc}"
}

for engine in "$@"; do
    case "${engine}" in
        rapfi) build_rapfi ;;
        xl) build_xl ;;
        *) echo "unknown engine: ${engine} (supported: rapfi, xl)" >&2; exit 2 ;;
    esac
done
