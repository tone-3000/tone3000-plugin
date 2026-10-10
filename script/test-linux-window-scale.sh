#!/usr/bin/env bash
# Regression test for GitHub issue #43 (Linux standalone: GUI not rendering
# on scaled X11 desktops).
#
# On any X11 desktop that advertises a window scale of 2 - real HiDPI, or
# Cinnamon/GNOME fractional scaling at 125%/150%, which round up to 2 in the
# XSettings key Gdk/WindowScalingFactor - stock JUCE 9.0.1 publishes
# WM_NORMAL_HINTS maximums scaled to within a title-bar's height of INT_MAX
# (the standalone's DecoratorConstrainer reports the unset 0x3FFFFFFF
# defaults, times 2). Mutter-family window managers (Muffin on Mint, Mutter
# on GNOME) re-add the frame border to those hints, overflow int, and clamp
# the window to the 1px PMinSize floor instead: the app maps as a full-width,
# 1-pixel-high strip under a bare title bar and the editor never becomes
# visible. Fixed by the T3K_X11_HINT_OVERFLOW patch in CMakeLists.txt
# (plus the editor's maxStartScale fit clamp for restored window scales).
#
# This script emulates the failing desktop on a virtual X server - Xvfb +
# xsettingsd advertising Gdk/WindowScalingFactor 2 + mutter - launches the
# standalone, and asserts that the mapped TONE3000 window has a sane height
# and that the screen actually painted. Run it against a stock-JUCE build to
# reproduce the bug (window collapses to <WIDTH>x1, test fails); against a
# patched build it passes.
#
# Linux only. Requires: Xvfb, xsettingsd, mutter, dbus-x11 (dbus-launch),
# x11-utils (xwininfo), imagemagick (import/identify).
#   Ubuntu/Debian: sudo apt install xvfb xsettingsd mutter dbus-x11 \
#                    x11-utils imagemagick
#
# Usage:
#   script/test-linux-window-scale.sh [path/to/TONE3000-standalone]
#
# Default binary: build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000
# Env overrides:
#   T3K_TEST_DISPLAY  X display to use (default :93)
#   T3K_TEST_SCREEN   virtual screen size (default 2560x1440)
#   T3K_TEST_SCALE1   set to 1 to skip xsettingsd (control run at 1x scale)

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$REPO_ROOT/build/plugin/TONE3000_artefacts/Release/Standalone/TONE3000}"
DISPLAY_NUM="${T3K_TEST_DISPLAY:-:93}"
SCREEN="${T3K_TEST_SCREEN:-2560x1440}"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "SKIP: this test only runs on Linux (X11)." >&2
    exit 0
fi

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: standalone binary not found at $BIN" >&2
    echo "      build it first, or pass its path as the first argument." >&2
    exit 1
fi

for tool in Xvfb xsettingsd mutter dbus-launch xwininfo import identify; do
    if ! command -v "$tool" > /dev/null; then
        echo "FAIL: required tool '$tool' not installed (see header of this script)." >&2
        exit 1
    fi
done

WORK="$(mktemp -d /tmp/t3k-scale-test.XXXXXX)"
PIDS=()

cleanup() {
    # Kill the app first so X teardown doesn't produce spurious errors.
    [[ -n "${APP_PID:-}" ]] && kill "$APP_PID" 2> /dev/null
    sleep 1
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2> /dev/null
    done
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "==> Xvfb $DISPLAY_NUM (${SCREEN}) ..."
Xvfb "$DISPLAY_NUM" -screen 0 "${SCREEN}x24" -dpi 96 > "$WORK/xvfb.log" 2>&1 &
PIDS+=($!)
export DISPLAY="$DISPLAY_NUM"
sleep 2

if [[ "${T3K_TEST_SCALE1:-0}" != "1" ]]; then
    echo "==> xsettingsd advertising Gdk/WindowScalingFactor 2 (the failing desktop) ..."
    printf 'Gdk/WindowScalingFactor 2\nGdk/UnscaledDPI 98304\nXft/DPI 196608\n' \
        > "$WORK/xsettingsd.conf"
    xsettingsd -c "$WORK/xsettingsd.conf" > "$WORK/xsettingsd.log" 2>&1 &
    PIDS+=($!)
    sleep 1
fi

echo "==> mutter (the Muffin/Cinnamon window-manager family) ..."
# --exit-with-x11 ties the spawned session bus to the Xvfb connection, so
# killing Xvfb in cleanup doesn't leak a dbus-daemon per run.
dbus-launch --exit-with-x11 mutter --x11 > "$WORK/mutter.log" 2>&1 &
PIDS+=($!)
sleep 2

echo "==> launching standalone (isolated HOME) ..."
export HOME="$WORK/home"
mkdir -p "$HOME"
"$BIN" > "$WORK/app.log" 2>&1 &
APP_PID=$!
sleep 12

xwininfo -root -tree > "$WORK/tree.txt" 2>&1

# The app may own more than one X window (the JUCE top-level plus any GTK
# helper). The bug's signature is the JUCE top-level collapsing to a
# 1px-high, full-width strip, so assert on the widest TONE3000 window: its
# height must be plausible for the 1024x614 design box.
GEOM=$(grep '"TONE3000"' "$WORK/tree.txt" | grep -oE '[0-9]+x[0-9]+\+' \
        | sort -t x -k 1 -n | tail -1 | tr -d '+')
WIDTH=${GEOM%x*}
HEIGHT=${GEOM#*x}

import -window root "$WORK/screen.png" 2> /dev/null
COLORS=$(identify -format %k "$WORK/screen.png" 2> /dev/null || echo 0)

echo "    widest TONE3000 window: ${WIDTH}x${HEIGHT}, screen unique colors: $COLORS"

FAIL=0

if [[ -z "$WIDTH" || -z "$HEIGHT" ]]; then
    echo "FAIL: no TONE3000 window found on $DISPLAY_NUM" >&2
    FAIL=1
elif (( HEIGHT < 400 )); then
    # The broken build maps at <width>x1 (issue #43); anything under 400px
    # cannot contain the 614px-tall design box at any scale >= 1.
    echo "FAIL: TONE3000 window is ${WIDTH}x${HEIGHT} - height collapsed (issue #43 regression)" >&2
    FAIL=1
fi

# The broken build paints nothing but a title bar (~800 unique colors on a
# flat background); a rendered UI measures >2000. 1500 splits them cleanly.
if (( COLORS < 1500 )); then
    echo "FAIL: screen has only $COLORS unique colors - UI likely never painted" >&2
    FAIL=1
fi

if (( FAIL )); then
    echo "--- xwininfo tree (TONE3000 windows):" >&2
    grep -i tone "$WORK/tree.txt" >&2
    echo "--- app log tail:" >&2
    tail -20 "$WORK/app.log" >&2
    exit 1
fi

if [[ "${T3K_TEST_SCALE1:-0}" != "1" ]]; then
    MODE="under a 2x-scaled X11 desktop"
else
    MODE="at 1x scale (control)"
fi
echo "PASS: window ${WIDTH}x${HEIGHT} mapped and rendered $MODE."
