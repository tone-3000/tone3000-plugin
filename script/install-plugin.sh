#!/usr/bin/env bash
# Copy a built plugin into the user's plugin folder.
#
#   ./script/install-plugin.sh VST3 [Debug|Release]
#   ./script/install-plugin.sh AU   [Debug|Release]
#
# Defaults to Release.
set -euo pipefail
cd "$(dirname "$0")/.."

format="${1:-}"
build_type="${2:-Release}"

case "$build_type" in
  Debug|Release) ;;
  *) echo "Invalid build type: $build_type (use Debug or Release)" >&2; exit 1 ;;
esac

os="$(uname -s)"
case "$format" in
  VST3)
    bundle="TONE3000.vst3"
    case "$os" in
      Darwin) dest_dir="$HOME/Library/Audio/Plug-Ins/VST3" ;;
      Linux)  dest_dir="$HOME/.vst3" ;;
      *) echo "Unsupported OS for this script: $os" >&2; exit 1 ;;
    esac
    ;;
  AU)
    bundle="TONE3000.component"
    if [ "$os" != "Darwin" ]; then
      echo "AU is macOS only (detected $os)" >&2
      exit 1
    fi
    dest_dir="$HOME/Library/Audio/Plug-Ins/Components"
    ;;
  *)
    echo "Usage: $0 <VST3|AU> [Debug|Release]" >&2
    exit 1
    ;;
esac

src="build/plugin/TONE3000_artefacts/$build_type/$format/$bundle"
if [ ! -d "$src" ]; then
  echo "Not found: $src" >&2
  echo "Build it first: cmake -B build -S . -DCMAKE_BUILD_TYPE=$build_type && cmake --build build" >&2
  exit 1
fi

mkdir -p "$dest_dir"
rm -rf "${dest_dir:?}/$bundle"
cp -R "$src" "$dest_dir/"
echo "Installed $bundle ($build_type) to $dest_dir"
echo "Rescan plugins in your DAW to pick up the change."
