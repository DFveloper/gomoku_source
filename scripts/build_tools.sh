#!/usr/bin/env bash
# Builds the engine-development tools into build/tools.
#
#   scripts/build_tools.sh            # build all of them
#   scripts/build_tools.sh ab_match   # build one target
#
# Tools:
#   ab_match       paired A/B match between the working tree and the frozen
#                  baseline revision in tools/fixtures/
#   id_soundness   checks the iterative-deepening root against an independent
#                  full-window argmax at the depth it reports
#   tactics_suite  positions with one known correct answer, plus a budget check
#   duel           equal-time match against an external Gomocup engine (rapfi,
#                  carbon, alphagomoku, xl) or an earlier revision, including the
#                  expert-mixture modes; see tools/build_native_engines.sh for
#                  hosts whose bundled runtimes need a newer glibc
#   expert_strength       scores each mixture member on its own
#   ensemble_invariants   asserts the mixture's degradation and proof-supremacy
#                         guarantees (also registered with ctest)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/build/tools"
mkdir -p "${out}"

CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -O2 -pthread}"

build() {
    local name="$1"
    echo "building ${name}"
    # shellcheck disable=SC2086
    ${CXX} ${CXXFLAGS} -I"${root}" -I"${root}/tools" \
        -DGOMOKU_SOURCE_ROOT="\"${root}\"" \
        "${root}/tools/${name}.cpp" -o "${out}/${name}"
}

if [ "$#" -eq 0 ]; then
    build ab_match
    build id_soundness
    build tactics_suite
    build duel
    build expert_strength
    build ensemble_invariants
else
    for name in "$@"; do build "${name}"; done
fi

echo "tools in ${out}"
