#!/usr/bin/env bash
# Build and run the DSP test suite (test/src/dsp_tests.cpp) locally.
#
#   ./script/test-dsp.sh                       # everything
#   ./script/test-dsp.sh 'ChainOversampler*'   # gtest filter
#
# Uses the existing build/ directory (configures a Release build if missing).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f build/CMakeCache.txt ]; then
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
fi

cmake --build build --target DspTests

BIN=$(find build/test -type f -name DspTests | head -1)
if [ -z "$BIN" ]; then
  echo "DspTests binary not found under build/test" >&2
  exit 1
fi

exec "$BIN" ${1:+--gtest_filter="$1"}
