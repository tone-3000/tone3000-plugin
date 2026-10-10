#!/usr/bin/env bash
# Copy a built plugin into the folder DAWs actually scan (system-wide on
# macOS, matching the .pkg installer; user-local on Linux).
#
#   ./script/install-plugin.sh VST3 [Debug|Release]
#   ./script/install-plugin.sh AU   [Debug|Release]
#   ./script/install-plugin.sh AAX  [Debug|Release]
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
      # System-wide folder, same as the .pkg: some hosts (LUNA) only scan
      # /Library for VST3 and ignore ~/Library entirely.
      Darwin) dest_dir="/Library/Audio/Plug-Ins/VST3" ;;
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
    # System-wide folder, same as the .pkg, so a dev install always replaces
    # the pkg copy instead of registering a same-version twin next to it.
    dest_dir="/Library/Audio/Plug-Ins/Components"
    ;;
  AAX)
    bundle="TONE3000.aaxplugin"
    if [ "$os" != "Darwin" ]; then
      echo "AAX is macOS only in this script (detected $os)" >&2
      exit 1
    fi
    # Pro Tools (incl. Developer) loads from the system Avid folder —
    # same path as the .pkg. Needs sudo.
    dest_dir="/Library/Application Support/Avid/Audio/Plug-Ins"
    need_sudo=1
    ;;
  *)
    echo "Usage: $0 <VST3|AU|AAX> [Debug|Release]" >&2
    exit 1
    ;;
esac

src="build/plugin/TONE3000_artefacts/$build_type/$format/$bundle"
if [ ! -d "$src" ]; then
  echo "Not found: $src" >&2
  echo "Build it first: cmake -B build -S . -DCMAKE_BUILD_TYPE=$build_type && cmake --build build" >&2
  exit 1
fi

run() {
  if [ "${need_sudo:-0}" -eq 1 ]; then
    sudo "$@"
  else
    "$@"
  fi
}

run mkdir -p "$dest_dir"
run rm -rf "${dest_dir:?}/$bundle"
run cp -R "$src" "$dest_dir/"
echo "Installed $bundle ($build_type) to $dest_dir"

# Also install the shipped factory presets, exactly where the official
# installers put them - same rationale as the bundle destinations above: a
# dev install must replace the installed copy, never leave a second set
# shadowing it. macOS: the .pkg's machine-wide /Library folder (root-owned,
# so the copy needs sudo). Linux: the tarball's per-user XDG config folder
# (there is no machine-wide Linux install).
factory_src="resources/factory-presets"  # cwd is the repo root (cd at top)

as_root() {
  if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
}

# Re-run a function as the user who invoked sudo (their $HOME, their uid) so
# nothing root-owned ever lands in a user home: macOS sudo keeps $HOME, and
# a sudo'd run of an older version of this script stamped the whole
# ~/Library/Application Support/TONE3000 tree root-owned that way - every
# settings save and drag-drop stash write in the plugin then failed while
# reads kept working (github issue #76). Without sudo (including a true root
# shell, where root's own home is the right target) just call the function.
run_in_user_home() {
  if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
    sudo -u "$SUDO_USER" -H bash -c "$(declare -f "$1")
cd $(printf '%q' "$PWD") && $1"
  else
    "$1"
  fi
}

if compgen -G "${factory_src}/*.t3kpreset" > /dev/null; then
  case "$os" in
    Darwin)
      # Replace, don't overlay: presets dropped from (or renamed in) the
      # repo set would otherwise linger from a previous install (the .pkg's
      # preinstall script does the same clear).
      preset_dest="/Library/Application Support/TONE3000/Presets/Factory"
      echo "Installing factory presets to $preset_dest (needs sudo)..."
      as_root mkdir -p "$preset_dest"
      as_root rm -f "$preset_dest"/*.t3kpreset
      as_root cp "${factory_src}"/*.t3kpreset "$preset_dest/"
      echo "Installed factory presets to $preset_dest"

      # Older versions of this script wrote the *user* Factory folder
      # instead. Those copies overlay the /Library set in
      # PresetManager::list() (a same-stem user file wins), so they would
      # shadow every later shipped update. Clear them - as the invoking
      # user, and only Factory/: user presets live in Presets/ proper and
      # are untouched.
      clear_legacy_user_factory() {
        rm -f "$HOME/Library/Application Support/TONE3000/Presets/Factory"/*.t3kpreset
      }
      run_in_user_home clear_legacy_user_factory
      ;;
    Linux)
      install_user_factory_presets() {
        local dest="${XDG_CONFIG_HOME:-$HOME/.config}/TONE3000/Presets/Factory"
        mkdir -p "$dest"
        # Replace, don't overlay (see the Darwin branch).
        rm -f "$dest"/*.t3kpreset
        cp resources/factory-presets/*.t3kpreset "$dest/"
        echo "Installed factory presets to $dest"
      }
      run_in_user_home install_user_factory_presets
      ;;
  esac
fi

echo "Rescan plugins in your DAW to pick up the change."
