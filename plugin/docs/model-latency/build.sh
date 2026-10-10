#!/usr/bin/env bash
# Builds nam_latency_tool against NeuralAmpModelerCore.
#
#   ./plugin/docs/model-latency/build.sh            # from anywhere in the repo
#   build/nam_latency_tool test/files/*.nam
#
# Defaults to the NAM core vendored in this repo and writes the binary to
# build/ (gitignored). Outside the repo, point NAM_CORE at a checkout and OUT
# wherever you like. The NAM sources are compiled straight in (no cmake;
# ~20 s, Eigen templates). Set NAM_LIB to a prebuilt libNAM.a to skip that;
# it is whole-archive linked so the self-registering model parsers survive.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

NAM_CORE="${NAM_CORE:-$REPO/plugin/NeuralAmpModelerCore}"
OUT="${OUT:-$REPO/build/nam_latency_tool}"
CXX="${CXX:-c++}"
FLAGS=(-std=c++20 -O3 -DNDEBUG -DNAM_ENABLE_A2_FAST
       -I"$HERE" -I"$NAM_CORE" -I"$NAM_CORE/Dependencies/eigen" -I"$NAM_CORE/Dependencies/nlohmann")

mkdir -p "$(dirname "$OUT")"
if [ -n "${NAM_LIB:-}" ]; then
  case "$(uname)" in
    Darwin) LINK=(-Wl,-force_load,"$NAM_LIB") ;;
    *)      LINK=(-Wl,--whole-archive "$NAM_LIB" -Wl,--no-whole-archive) ;;
  esac
  "$CXX" "${FLAGS[@]}" "$HERE/nam_latency_tool.cpp" "${LINK[@]}" -o "$OUT"
else
  "$CXX" "${FLAGS[@]}" "$HERE/nam_latency_tool.cpp" "$NAM_CORE"/NAM/*.cpp "$NAM_CORE"/NAM/*/*.cpp -o "$OUT"
fi
echo "built $OUT"
