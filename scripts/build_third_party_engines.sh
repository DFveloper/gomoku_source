#!/usr/bin/env bash
# Builds native Linux ports of the four external engines into
# third_party/runtime/ so gomoku_match_webui can launch them.
#
# Engines and sources:
#   Carbon 2.3      third_party/carbon-src        (local INFO max_depth patch)
#   XL / Niren 2016 third_party/xl-src            (clock() port fix via compat shim)
#   Rapfi           third_party/rapfi-src         (vendored master, AVX2)
#   AlphaGomoku     third_party/alphagomoku-src   (needs MinML, cloned on demand)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="$ROOT/third_party/runtime"
BUILD="${GOMOKU_ENGINE_BUILD_DIR:-${TMPDIR:-/tmp}/gomoku-engine-build}"
CXXFLAGS="-std=c++17 -O3 -march=native -w -pthread"
COMPAT="$ROOT/third_party/compat"

mkdir -p "$RUNTIME" "$BUILD"

echo "== Carbon Classic =="
mkdir -p "$RUNTIME/carbon"
g++ $CXXFLAGS -I "$COMPAT" -include windows.h \
    -o "$RUNTIME/carbon/pbrain-carbon" \
    "$ROOT"/third_party/carbon-src/AICarbon.cpp \
    "$ROOT"/third_party/carbon-src/AICarbonMove.cpp \
    "$ROOT"/third_party/carbon-src/AIwrapper.cpp \
    "$ROOT"/third_party/carbon-src/Random.cpp \
    "$ROOT"/third_party/carbon-src/pisqpipe.cpp

echo "== XL / Niren =="
mkdir -p "$RUNTIME/xl"
g++ $CXXFLAGS -I "$COMPAT" -include windows.h -DXL_ENGINE_BUILD \
    -o "$RUNTIME/xl/pbrain-xl" \
    "$ROOT"/third_party/xl-src/xl.cpp \
    "$ROOT"/third_party/xl-src/xl_ai.cpp \
    "$ROOT"/third_party/xl-src/pisqpipe.cpp

echo "== Rapfi (AVX2) =="
cmake -S "$ROOT/third_party/rapfi-src/Rapfi" -B "$BUILD/rapfi" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=ON -DUSE_BMI2=ON
cmake --build "$BUILD/rapfi"
cp "$BUILD/rapfi/pbrain-rapfi" "$RUNTIME/rapfi/pbrain-rapfi"

echo "== AlphaGomoku (CPU) =="
if [ ! -d "$BUILD/MinML" ]; then
    git clone --depth 1 https://github.com/MaciejKozarzewski/MinML.git "$BUILD/MinML"
fi
cmake -S "$BUILD/MinML" -B "$BUILD/minml" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$BUILD/minml" -j "$(nproc)"
mkdir -p "$BUILD/MinML/build/cpu/Release/lib"
cp "$BUILD/minml/lib/libMinML_cpu.a" "$BUILD/MinML/build/cpu/Release/lib/"
cmake -S "$ROOT/third_party/alphagomoku-src" -B "$BUILD/alphagomoku" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DMINML_BACKEND_PATH="$BUILD/MinML"
cmake --build "$BUILD/alphagomoku" -j "$(nproc)" --target ag_player_cpu
cp "$BUILD/alphagomoku/bin/ag_player_cpu" "$RUNTIME/alphagomoku/pbrain-agomoku"

echo "All engines built:"
ls -la "$RUNTIME"/*/pbrain-* | grep -v '\.exe'
