#!/bin/bash
# TONE3000 Linux installer.
#
# Installs from the extracted tarball (run from inside the extracted folder):
#   ./install.sh              install VST3 + standalone for the current user
#   ./install.sh --check      only check runtime dependencies, install nothing
#   ./install.sh --uninstall  remove a previous install
#
# Install locations (override with env vars):
#   VST3_DIR  VST3 plug-in dir   (default: ~/.vst3, the standard per-user location)
#   BIN_DIR   standalone app dir (default: ~/.local/bin)
#
# Runtime dependencies: the UI runs in a system WebKitGTK webview, which JUCE
# loads dynamically at runtime (it is NOT bundled, unlike WebView2 on Windows).
# Without it the plugin window renders black. This script checks for the
# required libraries and offers to install them with your package manager.

set -euo pipefail

VST3_DIR="${VST3_DIR:-$HOME/.vst3}"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
HERE="$(cd "$(dirname "$0")" && pwd)"

# ──────────────────────────────────────────────────────────────────────────────
# Runtime dependency handling
# ──────────────────────────────────────────────────────────────────────────────

# Returns 0 if a shared library is resolvable by the dynamic loader.
have_lib() {
  ldconfig -p 2>/dev/null | grep -q "$1"
}

# JUCE dlopens WebKitGTK at runtime, preferring the 4.1 ABI (libsoup3) and
# falling back to 4.0 (libsoup2). Either one works.
have_webkit() {
  { have_lib "libwebkit2gtk-4.1.so" && have_lib "libjavascriptcoregtk-4.1.so" && have_lib "libsoup-3.0.so"; } ||
  { have_lib "libwebkit2gtk-4.0.so" && have_lib "libjavascriptcoregtk-4.0.so" && have_lib "libsoup-2.4.so"; }
}

# JUCE also dlopens libcurl at runtime for native HTTPS (tone model
# downloads). It accepts the OpenSSL or GnuTLS flavour, any current SONAME.
have_curl() {
  have_lib "libcurl.so" || have_lib "libcurl-gnutls.so"
}

# Everything the binary needs but may not be on a minimal install.
# Prints the names of missing components (webkit, curl, gtk3, alsa, freetype).
missing_deps() {
  local missing=()
  have_webkit             || missing+=("webkit")
  have_curl               || missing+=("curl")
  have_lib "libgtk-3.so"  || missing+=("gtk3")
  have_lib "libasound.so" || missing+=("alsa")
  have_lib "libfreetype.so" || missing+=("freetype")
  echo "${missing[@]:-}"
}

# Debian/Ubuntu renamed several runtime packages for the 64-bit time_t
# transition (24.04+: libgtk-3-0t64, libasound2t64). Pick whichever name
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
    [[ "$missing" == *webkit* ]]   && pkgs+=("$(apt_pick libwebkit2gtk-4.1-0 libwebkit2gtk-4.0-37)")
    [[ "$missing" == *curl* ]]     && pkgs+=("$(apt_pick libcurl4t64 libcurl4)")
    [[ "$missing" == *gtk3* ]]     && pkgs+=("$(apt_pick libgtk-3-0t64 libgtk-3-0)")
    [[ "$missing" == *alsa* ]]     && pkgs+=("$(apt_pick libasound2t64 libasound2)")
    [[ "$missing" == *freetype* ]] && pkgs+=("libfreetype6")
    echo "sudo apt-get install -y ${pkgs[*]}"
  elif command -v dnf >/dev/null; then
    [[ "$missing" == *webkit* ]]   && pkgs+=("webkit2gtk4.1")
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl")
    [[ "$missing" == *gtk3* ]]     && pkgs+=("gtk3")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa-lib")
    [[ "$missing" == *freetype* ]] && pkgs+=("freetype")
    echo "sudo dnf install -y ${pkgs[*]}"
  elif command -v pacman >/dev/null; then
    [[ "$missing" == *webkit* ]]   && pkgs+=("webkit2gtk-4.1")
    [[ "$missing" == *curl* ]]     && pkgs+=("curl")
    [[ "$missing" == *gtk3* ]]     && pkgs+=("gtk3")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa-lib")
    [[ "$missing" == *freetype* ]] && pkgs+=("freetype2")
    echo "sudo pacman -S --needed --noconfirm ${pkgs[*]}"
  elif command -v zypper >/dev/null; then
    [[ "$missing" == *webkit* ]]   && pkgs+=("libwebkit2gtk-4_1-0")
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl4")
    [[ "$missing" == *gtk3* ]]     && pkgs+=("libgtk-3-0")
    [[ "$missing" == *alsa* ]]     && pkgs+=("alsa")
    [[ "$missing" == *freetype* ]] && pkgs+=("libfreetype6")
    echo "sudo zypper install -y ${pkgs[*]}"
  fi
}

check_and_install_deps() {
  local missing
  missing="$(missing_deps)"

  if [[ -z "$missing" ]]; then
    echo "Runtime dependencies: OK (WebKitGTK, GTK3, ALSA, FreeType found)"
    return 0
  fi

  echo ""
  echo "Missing runtime dependencies: $missing"
  if [[ "$missing" == *webkit* ]]; then
    echo "  Note: without WebKitGTK the plugin window will render as a black screen."
  fi
  if [[ "$missing" == *curl* ]]; then
    echo "  Note: without libcurl, tone model downloads from TONE3000 will fail."
  fi

  # Atomic/immutable distros (Fedora Silverblue/Kinoite, Bazzite, ...): no dnf;
  # packages are layered with rpm-ostree and only appear after a reboot, so
  # print instructions instead of auto-running anything.
  if [[ -f /run/ostree-booted ]]; then
    local pkgs=()
    [[ "$missing" == *webkit* ]]   && pkgs+=("webkit2gtk4.1")
    [[ "$missing" == *curl* ]]     && pkgs+=("libcurl")
    [[ "$missing" == *gtk3* ]]     && pkgs+=("gtk3")
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
    echo "Could not detect your package manager. Install the WebKitGTK 4.1 (or 4.0),"
    echo "curl, GTK3, ALSA and FreeType runtime libraries with your distro's package"
    echo "manager, then re-run this script."
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
    echo "Skipped. The plugin UI will not work until these are installed."
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

# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────

if [[ "${1:-}" == "--uninstall" ]]; then
  rm -rf "$VST3_DIR/TONE3000.vst3"
  rm -f "$BIN_DIR/TONE3000"
  echo "TONE3000 uninstalled."
  exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
  status=0
  check_and_install_deps || status=1
  [[ -f "$HERE/TONE3000" ]] && { check_linked_libs || status=1; }
  exit "$status"
fi

if [[ ! -d "$HERE/TONE3000.vst3" || ! -f "$HERE/TONE3000" ]]; then
  echo "Error: TONE3000.vst3 and/or TONE3000 not found next to this script."
  echo "Run install.sh from inside the extracted release folder."
  exit 1
fi

# Dependencies first: a black-screen install is worse than no install.
deps_ok=1
check_and_install_deps || deps_ok=0
check_linked_libs || deps_ok=0

echo ""
echo "Installing VST3 to $VST3_DIR ..."
mkdir -p "$VST3_DIR"
rm -rf "$VST3_DIR/TONE3000.vst3"
cp -r "$HERE/TONE3000.vst3" "$VST3_DIR/"

echo "Installing standalone app to $BIN_DIR ..."
mkdir -p "$BIN_DIR"
install -m 755 "$HERE/TONE3000" "$BIN_DIR/TONE3000"

echo ""
echo "Done."
echo "  VST3:       $VST3_DIR/TONE3000.vst3 (rescan plug-ins in your DAW)"
echo "  Standalone: $BIN_DIR/TONE3000"
if ! echo ":$PATH:" | grep -q ":$BIN_DIR:"; then
  echo ""
  echo "Note: $BIN_DIR is not on your PATH; launch the standalone with its full path"
  echo "or add the directory to PATH."
fi
if [[ "$deps_ok" == "0" ]]; then
  echo ""
  echo "WARNING: runtime dependencies are still missing (see above)."
  echo "The plugin UI will show a black screen until they are installed."
  echo "Re-run './install.sh --check' after installing them to verify."
fi
