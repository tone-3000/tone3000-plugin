#!/usr/bin/env bash
# Android build/install/run/log loop for the TONE3000 app and (from M1) the
# USB probe app. Everything runs over adb, which is expected to be a Wi-Fi
# connection during audio work (the tablet's USB-C port carries the interface);
# any adb transport works, wired included.
#
#   ./script/android-dev.sh doctor          # adb devices + audio dumpsys heads
#   ./script/android-dev.sh build           # assembleDebug, arm64-v8a only
#   ./script/android-dev.sh build all       # assembleDebug, every ABI
#   ./script/android-dev.sh install         # adb install -r the debug APK
#   ./script/android-dev.sh run             # launch the main activity
#   ./script/android-dev.sh log             # the app's own log (run-as; debug builds)
#   ./script/android-dev.sh csv             # the probe's measurement CSV (M1)
#   ./script/android-dev.sh probe build     # same verbs against the probe app
#
# Toolchain resolution: JAVA_HOME / ANDROID_HOME when set, otherwise the
# user-space installs under ~/Android (JDK tarball + sdkmanager's SDK). The
# versions are pinned in android/app/build.gradle.kts; nothing here installs
# or updates anything.
set -euo pipefail
cd "$(dirname "$0")/.."

# --- toolchain -------------------------------------------------------------

if [ -z "${JAVA_HOME:-}" ] && [ -d "$HOME/Android/jdk" ]; then
  _jdk=$(ls -d "$HOME/Android/jdk"/jdk-* 2>/dev/null | head -1 || true)
  [ -n "$_jdk" ] && export JAVA_HOME="$_jdk"
fi
export JAVA_HOME="${JAVA_HOME:-}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"

# --- targets ---------------------------------------------------------------

APP_PKG=com.TONE3000.TONE3000
APP_ACTIVITY=.MainActivity
APP_TASK=app
APP_APK=app
APP_LOG_PATH=TONE3000/TONE3000.log

PROBE_PKG=com.TONE3000.USBCHECK
PROBE_ACTIVITY=.MainActivity
PROBE_TASK=probe
PROBE_APK=probe
PROBE_LOG_PATH=files/rtl.csv

# The debug APK lands in either outputs/apk/debug or (this AGP) the
# intermediates tree, with a redirect file in outputs. Check both.
find_apk() {
  local name=$1 d
  for d in "android/$1/build/outputs/apk/debug" "android/$1/build/intermediates/apk/debug"; do
    if [ -f "$d/$name-debug.apk" ]; then
      echo "$d/$name-debug.apk"
      return 0
    fi
  done
  return 1
}

TARGET=app
case "${1:-}" in
  app|probe) TARGET=$1; shift ;;
esac
MODE=${1:-}
[ -n "$MODE" ] || { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
shift || true

if [ "$TARGET" = app ]; then
  PKG=$APP_PKG ACTIVITY=$APP_ACTIVITY TASK=$APP_TASK MODULE=$APP_APK
else
  PKG=$PROBE_PKG ACTIVITY=$PROBE_ACTIVITY TASK=$PROBE_TASK MODULE=$PROBE_APK
fi

case "$MODE" in
  doctor)
    adb devices -l
    echo "--- dumpsys media.audio_flinger (head -100) ---"
    adb shell dumpsys media.audio_flinger 2>&1 | head -100
    echo "--- dumpsys media.audio_policy (head -60) ---"
    adb shell dumpsys media.audio_policy 2>&1 | head -60
    ;;
  build)
    # One ABI by default: the edit-build loop is minutes faster and the
    # cold-libs fetch is less prone to the two-ABI configure race (see
    # docs/android.md). "build all" builds every ABI.
    ABI_ARGS=()
    if [ "${1:-}" != "all" ]; then
      ABI_ARGS+=(-Pandroid.injected.build.abi=arm64-v8a)
    fi
    (cd android && ./gradlew ":$TASK:assembleDebug" "${ABI_ARGS[@]}")
    echo "APK: $(find_apk "$MODULE" || echo 'NOT FOUND')"
    ;;
  install)
    APK=$(find_apk "$MODULE") || { echo "no $MODULE debug APK built yet" >&2; exit 1; }
    # -t: debug builds carry testOnly; the flag allows them.
    adb install -r -t "$APK"
    ;;
  run)
    adb shell am start -n "$PKG/$ACTIVITY"
    ;;
  log)
    if [ "$TARGET" = app ]; then
      adb shell run-as "$PKG" cat "$APP_LOG_PATH"
    else
      adb logcat -s T3KUSB
    fi
    ;;
  csv)
    adb shell run-as "$PKG" cat "$PROBE_LOG_PATH"
    ;;
  pair|connect)
    # adb pair <ip>:<pairport> <code>  |  adb connect <ip>:<connectport>
    adb "$MODE" "$@"
    ;;
  *)
    echo "unknown mode: $MODE" >&2
    exit 1
    ;;
esac
