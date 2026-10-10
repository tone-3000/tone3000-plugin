#!/bin/bash
# TONE3000 Linux installer.
#
# Installs from the extracted tarball (run from inside the extracted folder):
#   ./install.sh              install VST3 + LV2 + CLAP + standalone for the current user
#   ./install.sh --check      only check runtime dependencies, install nothing
#   ./install.sh --uninstall  remove a previous install
#
# Install locations (override with env vars):
#   VST3_DIR  VST3 plug-in dir   (default: ~/.vst3, the standard per-user location)
#   LV2_DIR   LV2 plug-in dir    (default: ~/.lv2)
#   CLAP_DIR  CLAP plug-in dir   (default: ~/.clap)
#   BIN_DIR   standalone app dir (default: ~/.local/bin)
#   DATA_DIR  XDG data dir for the desktop entry + icon
#             (default: $XDG_DATA_HOME, or ~/.local/share)
#   CONFIG_DIR  TONE3000 config root; factory presets install under
#               Presets/Factory (default: $XDG_CONFIG_HOME/TONE3000,
#               or ~/.config/TONE3000)
#
# Runtime dependencies: JUCE dlopens libcurl (tone downloads), ALSA and
# FreeType at runtime rather than linking them. This script checks for them
# and offers to install them with your package manager.

set -euo pipefail

VST3_DIR="${VST3_DIR:-$HOME/.vst3}"
LV2_DIR="${LV2_DIR:-$HOME/.lv2}"
CLAP_DIR="${CLAP_DIR:-$HOME/.clap}"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
DATA_DIR="${DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}}"
# Same root PresetManager uses for user presets on Linux (~/.config/TONE3000).
CONFIG_DIR="${CONFIG_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/TONE3000}"
FACTORY_DIR="$CONFIG_DIR/Presets/Factory"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Runtime dependency handling

# Captured once: `ldconfig -p | grep -q ...` (the previous approach) is racy
# under `set -o pipefail`: grep -q exits as soon as it finds a match, which
# can SIGPIPE a still-writing ldconfig, and pipefail then reports that SIGPIPE
# exit as the whole pipeline failing even though grep found the library. That
# made this script randomly claim present libraries were missing. Matching in-shell against a single captured snapshot avoids the
# pipe entirely.
LDCONFIG_CACHE="$(ldconfig -p 2>/dev/null || true)"

# Returns 0 if a shared library is resolvable by the dynamic loader.
have_lib() {
  case "$LDCONFIG_CACHE" in
    *"$1"*) return 0 ;;
    *) return 1 ;;
  esac
}

# JUCE dlopens libcurl at runtime for HTTPS (tone model downloads). It
# accepts the OpenSSL or GnuTLS flavour, any current SONAME.
have_curl() {
  have_lib "libcurl.so" || have_lib "libcurl-gnutls.so"
}

# Everything the binary needs but may not be on a minimal install.
# Prints the names of missing components (curl, alsa, freetype).
missing_deps() {
  local missing=()
  have_curl               || missing+=("curl")
  have_lib "libasound.so" || missing+=("alsa")
  have_lib "libfreetype.so" || missing+=("freetype")
  echo "${missing[@]:-}"
}

# Debian/Ubuntu renamed several runtime packages for the 64-bit time_t
# transition (24.04+: libcurl4t64, libasound2t64). Pick whichever name
# exists in this system's package index.
apt_pick() {
  for name in "$@"; do
    if apt-cache show "$name" >/dev/null 2>&1; then
      echo "$name"
      return
    fi
  done
  echo "$1"
}

# Maps missing components to this distro's package names and prints the
# install command. Empty output = unsupported/unknown package manager.
install_command() {
  local missing="$1" pkgs=()
  if command -v apt-get >/dev/null; then
    [[ "$missing" == *curl* ]]     && pkgs+=("$(apt_pick libcurl4t64 libcurl4)")
    [[ "$missing" == *alsa* ]]     && pkgs+=("$(apt_pick libasound2t64 libasound2)")
    [[ "$missing" == *freetype* ]] && pkgs+=("libfreetype6")
    echo "sudo apt-get install -y ${pkgs[*]}"
  elif command -v dnf >/dev/null; then
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa-lib")
    [[ "$missing" == *freetype* ]] && pkgs+=("freetype")
    echo "sudo dnf install -y ${pkgs[*]}"
  elif command -v pacman >/dev/null; then
    [[ "$missing" == *curl* ]]     && pkgs+=("curl")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa-lib")
    [[ "$missing" == *freetype* ]] && pkgs+=("freetype2")
    echo "sudo pacman -S --needed --noconfirm ${pkgs[*]}"
  elif command -v zypper >/dev/null; then
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl4")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa")
    [[ "$missing" == *freetype* ]] && pkgs+=("libfreetype6")
    echo "sudo zypper install -y ${pkgs[*]}"
  fi
}

check_and_install_deps() {
  local missing
  missing="$(missing_deps)"

  if [[ -z "$missing" ]]; then
    echo "Runtime dependencies: OK (curl, ALSA, FreeType found)"
    return 0
  fi

  echo ""
  echo "Missing runtime dependencies: $missing"
  if [[ "$missing" == *curl* ]]; then
    echo "  Note: without libcurl, tone model downloads from TONE3000 will fail."
  fi

  # Atomic/immutable distros (Fedora Silverblue/Kinoite, Bazzite, ...): no dnf;
  # packages are layered with rpm-ostree and only appear after a reboot, so
  # print instructions instead of auto-running anything.
  if [[ -f /run/ostree-booted ]]; then
    local pkgs=()
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa-lib")
    [[ "$missing" == *freetype* ]] && pkgs+=("freetype")
    echo ""
    echo "This is an atomic (ostree-based) system. Layer the packages and reboot:"
    echo "  sudo rpm-ostree install ${pkgs[*]}"
    echo "  systemctl reboot"
    echo "Then re-run './install.sh --check' to verify."
    return 1
  fi

  local cmd
  cmd="$(install_command "$missing")"
  # Already root (containers, some minimal systems): no sudo needed/available.
  if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    cmd="${cmd#sudo }"
  fi
  if [[ -z "$cmd" ]]; then
    echo ""
    echo "Could not detect your package manager. Install the curl, ALSA and FreeType"
    echo "runtime libraries with your distro's package manager, then re-run this script."
    return 1
  fi

  echo ""
  echo "The following command will install them:"
  echo "  $cmd"
  if [[ "${AUTO_INSTALL_DEPS:-}" == "1" ]]; then
    REPLY=y
  else
    # Non-interactive runs (piped stdin) hit EOF here; treat as "no".
    read -r -p "Run it now? [y/N] " REPLY || REPLY=n
  fi
  if [[ "$REPLY" =~ ^[Yy]$ ]]; then
    # apt needs an up-to-date index or fresh installs may 404.
    if [[ "$cmd" == sudo\ apt-get* ]]; then sudo apt-get update; fi
    eval "$cmd"
    missing="$(missing_deps)"
    if [[ -n "$missing" ]]; then
      echo "Error: still missing after install: $missing" >&2
      return 1
    fi
    echo "Runtime dependencies: OK"
  else
    echo "Skipped. Tone downloads and/or audio will not work until these are installed."
    return 1
  fi
}

# Sanity check: report any directly-linked libraries the loader can't resolve.
check_linked_libs() {
  local unresolved
  unresolved="$(ldd "$HERE/TONE3000" 2>/dev/null | grep "not found" || true)"
  if [[ -n "$unresolved" ]]; then
    echo ""
    echo "Warning: the loader cannot resolve these libraries:" >&2
    echo "$unresolved" >&2
    echo "The standalone app may not start until they are installed." >&2
    return 1
  fi
  return 0
}

# Main

if [[ "${1:-}" == "--uninstall" ]]; then
  rm -rf "$VST3_DIR/TONE3000.vst3"
  rm -rf "$LV2_DIR/TONE3000.lv2"
  rm -f "$CLAP_DIR/TONE3000.clap"
  rm -f "$BIN_DIR/TONE3000"
  rm -f "$DATA_DIR/applications/tone3000.desktop"
  rm -f "$DATA_DIR/icons/hicolor/512x512/apps/tone3000.png"
  # Remove only the factory presets this tarball shipped (matched by stem),
  # never the user's own saved presets.
  if compgen -G "${HERE}/factory-presets/*.t3kpreset" > /dev/null; then
    for f in "${HERE}/factory-presets"/*.t3kpreset; do
      rm -f "$FACTORY_DIR/$(basename "$f")"
    done
    rmdir "$FACTORY_DIR" 2>/dev/null || true
  fi
  command -v update-desktop-database >/dev/null &&
    update-desktop-database "$DATA_DIR/applications" 2>/dev/null || true
  echo "TONE3000 uninstalled."
  exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
  status=0
  check_and_install_deps || status=1
  [[ -f "$HERE/TONE3000" ]] && { check_linked_libs || status=1; }
  exit "$status"
fi

if [[ ! -d "$HERE/TONE3000.vst3" || ! -d "$HERE/TONE3000.lv2" ||
      ! -f "$HERE/TONE3000.clap" || ! -f "$HERE/TONE3000" ]]; then
  echo "Error: TONE3000.vst3, TONE3000.lv2, TONE3000.clap and/or TONE3000 not"
  echo "found next to this script. Run install.sh from inside the extracted"
  echo "release folder."
  exit 1
fi

# Dependencies first: a broken install is worse than no install.
deps_ok=1
check_and_install_deps || deps_ok=0
check_linked_libs || deps_ok=0

echo ""
echo "Installing VST3 to $VST3_DIR ..."
mkdir -p "$VST3_DIR"
rm -rf "$VST3_DIR/TONE3000.vst3"
cp -r "$HERE/TONE3000.vst3" "$VST3_DIR/"

echo "Installing LV2 to $LV2_DIR ..."
mkdir -p "$LV2_DIR"
rm -rf "$LV2_DIR/TONE3000.lv2"
cp -r "$HERE/TONE3000.lv2" "$LV2_DIR/"

echo "Installing CLAP to $CLAP_DIR ..."
mkdir -p "$CLAP_DIR"
install -m 644 "$HERE/TONE3000.clap" "$CLAP_DIR/TONE3000.clap"

echo "Installing standalone app to $BIN_DIR ..."
mkdir -p "$BIN_DIR"
install -m 755 "$HERE/TONE3000" "$BIN_DIR/TONE3000"

if compgen -G "${HERE}/factory-presets/*.t3kpreset" > /dev/null; then
  echo "Installing factory presets to $FACTORY_DIR ..."
  mkdir -p "$FACTORY_DIR"
  # Replace, don't overlay: presets dropped from (or renamed in) the shipped
  # set would otherwise linger from a previous install. User presets live in
  # the parent directory and are untouched.
  rm -f "$FACTORY_DIR"/*.t3kpreset
  cp "${HERE}/factory-presets"/*.t3kpreset "$FACTORY_DIR/"
fi

# Desktop entry + icon so the standalone shows up in app launchers with the
# proper name and artwork. Written here (not shipped as a file) because Exec
# must be the absolute install path: launchers don't resolve ~/.local/bin
# through the shell PATH.
echo "Installing desktop entry and icon to $DATA_DIR ..."
mkdir -p "$DATA_DIR/applications" "$DATA_DIR/icons/hicolor/512x512/apps"
install -m 644 "$HERE/tone3000.png" "$DATA_DIR/icons/hicolor/512x512/apps/tone3000.png"
cat > "$DATA_DIR/applications/tone3000.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=TONE3000
Comment=Play NAM captures and IRs straight from TONE3000
Exec=$BIN_DIR/TONE3000
Icon=tone3000
Terminal=false
Categories=AudioVideo;Audio;Music;
StartupWMClass=TONE3000
EOF
command -v update-desktop-database >/dev/null &&
  update-desktop-database "$DATA_DIR/applications" 2>/dev/null || true

echo ""
echo "Done."
echo "  VST3:       $VST3_DIR/TONE3000.vst3 (rescan plug-ins in your DAW)"
echo "  LV2:        $LV2_DIR/TONE3000.lv2"
echo "  CLAP:       $CLAP_DIR/TONE3000.clap"
echo "  Standalone: $BIN_DIR/TONE3000 (desktop entry: applications menu > TONE3000)"
if [[ -d "$FACTORY_DIR" ]] && compgen -G "${FACTORY_DIR}/*.t3kpreset" > /dev/null; then
  echo "  Presets:    $FACTORY_DIR"
fi
if ! echo ":$PATH:" | grep -q ":$BIN_DIR:"; then
  echo ""
  echo "Note: $BIN_DIR is not on your PATH; launch the standalone with its full path"
  echo "or add the directory to PATH."
fi
if [[ "$deps_ok" == "0" ]]; then
  echo ""
  echo "WARNING: runtime dependencies are still missing (see above)."
  echo "Re-run './install.sh --check' after installing them to verify."
fi
