#!/usr/bin/env bash
# Validate the built plugin formats with each format's official validator.
#
#   ./script/validate-plugin.sh                # all formats, Release
#   ./script/validate-plugin.sh VST3           # one format
#   ./script/validate-plugin.sh AU Debug       # one format, Debug build
#
# What runs per format:
#   VST3, AU  pluginval at strictness 10 (brew install --cask pluginval).
#             AU is macOS only and is copied to ~/Library/Audio/Plug-Ins/
#             Components first: pluginval shells out to auval, which looks
#             plugins up by registered component ID, not file path. auval also
#             takes ~1 min on this plugin, longer than pluginval's default
#             30 s per-test timeout, hence --timeout-ms. Windows needs far
#             more than that anyway: Parameter thread safety alone runs past
#             two minutes there, so the ceiling is generous (see timeout_ms).
#   CLAP      clap-validator (github.com/free-audio/clap-validator; drop the
#             release binary on PATH or in build/tools/).
#   LV2       lv2lint if installed (Linux mainly); skipped otherwise. There is
#             no LV2 behavioral validator on macOS.
#   AAX       always skipped: needs Avid's DSH test harness + PACE signing.
#   Standalone always skipped: it's an app, there is no host spec to validate.
#
# Strictness can be overridden with STRICTNESS=n, the per-test timeout with
# TIMEOUT_MS=n.
set -uo pipefail
cd "$(dirname "$0")/.."

requested_format="${1:-}"
build_type="${2:-Release}"
strictness="${STRICTNESS:-10}"
# Per-test ceiling, not a delay: a healthy run never waits on it, so it costs
# nothing to be generous and a hang is still caught, just later. Two minutes
# was enough for auval on macOS but not for pluginval's Parameter thread
# safety test on Windows, where the stock value fails every build — including
# an unmodified main — with "Timeout after 2 mins".
timeout_ms="${TIMEOUT_MS:-600000}"

case "$build_type" in
  Debug|Release) ;;
  *) echo "Invalid build type: $build_type (use Debug or Release)" >&2; exit 1 ;;
esac

os="$(uname -s)"
artefacts="build/plugin/TONE3000_artefacts/$build_type"

find_pluginval() {
  if command -v pluginval > /dev/null; then
    echo "pluginval"
  elif [ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]; then
    echo "/Applications/pluginval.app/Contents/MacOS/pluginval"
  fi
}

find_clap_validator() {
  if command -v clap-validator > /dev/null; then
    echo "clap-validator"
  elif [ -x "build/tools/clap-validator" ]; then
    echo "build/tools/clap-validator"
  fi
}

all_formats=(VST3 CLAP LV2 AAX Standalone)
[ "$os" = "Darwin" ] && all_formats=(VST3 AU CLAP LV2 AAX Standalone)

formats=()
if [ -z "$requested_format" ]; then
  formats=("${all_formats[@]}")
else
  case " ${all_formats[*]} " in
    *" $requested_format "*) formats=("$requested_format") ;;
    *) echo "Usage: $0 [VST3|AU|CLAP|LV2|AAX|Standalone] [Debug|Release]" >&2; exit 1 ;;
  esac
fi

failures=()
skips=()

banner() {
  echo "=================================================================="
  echo "$1"
  echo "=================================================================="
}

skip() { # format, reason
  skips+=("$1")
  echo "SKIPPED $1: $2"
}

for format in "${formats[@]}"; do
  case "$format" in

    VST3|AU)
      pluginval="$(find_pluginval)"
      if [ -z "$pluginval" ]; then
        echo "pluginval not found (brew install --cask pluginval)" >&2
        failures+=("$format")
        continue
      fi
      if [ "$format" = "AU" ]; then
        src="$artefacts/AU/TONE3000.component"
        if [ ! -d "$src" ]; then
          echo "Not found: $src (build it first: cmake --build build)" >&2
          failures+=(AU)
          continue
        fi
        dest_dir="$HOME/Library/Audio/Plug-Ins/Components"
        mkdir -p "$dest_dir"
        rm -rf "$dest_dir/TONE3000.component"
        cp -R "$src" "$dest_dir/"
        # Restart the registrar so auval sees the fresh copy immediately.
        killall -9 AudioComponentRegistrar 2> /dev/null || true
        target="$dest_dir/TONE3000.component"
      else
        target="$artefacts/VST3/TONE3000.vst3"
        if [ ! -d "$target" ]; then
          echo "Not found: $target (build it first: cmake --build build)" >&2
          failures+=(VST3)
          continue
        fi
      fi
      banner "pluginval: $format ($build_type), strictness $strictness"
      if ! "$pluginval" --strictness-level "$strictness" \
           --timeout-ms "$timeout_ms" --validate "$target"; then
        failures+=("$format")
      fi
      ;;

    CLAP)
      clap_validator="$(find_clap_validator)"
      if [ -z "$clap_validator" ]; then
        skip CLAP "clap-validator not found; get a release from github.com/free-audio/clap-validator and put it on PATH or in build/tools/"
        continue
      fi
      target="$artefacts/CLAP/TONE3000.clap"
      if [ ! -d "$target" ]; then
        echo "Not found: $target (build it first: cmake --build build)" >&2
        failures+=(CLAP)
        continue
      fi
      banner "clap-validator: CLAP ($build_type)"
      if ! "$clap_validator" validate "$target"; then
        failures+=(CLAP)
      fi
      ;;

    LV2)
      if ! command -v lv2lint > /dev/null; then
        skip LV2 "lv2lint not installed (Linux: install lv2lint; no macOS validator exists)"
        continue
      fi
      target="$artefacts/LV2/TONE3000.lv2"
      if [ ! -d "$target" ]; then
        echo "Not found: $target (build it first: cmake --build build)" >&2
        failures+=(LV2)
        continue
      fi
      banner "lv2lint: LV2 ($build_type)"
      if ! LV2_PATH="$artefacts/LV2" lv2lint "https://www.tone3000.com/plugin"; then
        failures+=(LV2)
      fi
      ;;

    AAX)
      skip AAX "requires Avid's DSH test harness and PACE signing (manual, Pro Tools Developer)"
      ;;

    Standalone)
      skip Standalone "it's an app, not a hosted plugin; nothing to validate"
      ;;
  esac
done

banner "Summary"
[ "${#skips[@]}" -gt 0 ] && echo "Skipped: ${skips[*]}"
if [ "${#failures[@]}" -gt 0 ]; then
  echo "FAILED: ${failures[*]}"
  exit 1
fi
echo "All testable formats passed."
