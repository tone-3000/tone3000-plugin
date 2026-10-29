#!/usr/bin/env bash
# Build and run the USB driver core test suite (plugin/usb/test) locally.
#
#   ./script/test-usb.sh                       # everything
#   ./script/test-usb.sh 'UsbAudioStream.*'    # gtest filter
#
# Uses the existing build/ directory (configures a Release build if missing),
# same idiom as script/test-dsp.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f build/CMakeCache.txt ]; then
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
fi

cmake --build build --target UsbTests

BIN=$(find build/plugin/usb/test -type f -name UsbTests | head -1)
if [ -z "$BIN" ]; then
  echo "UsbTests binary not found under build/plugin/usb/test" >&2
  exit 1
fi

exec "$BIN" ${1:+--gtest_filter="$1"}
