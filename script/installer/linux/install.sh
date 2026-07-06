#!/bin/bash
# TONE3000 Linux installer.
#
# Installs from the extracted tarball (run from inside the extracted folder):
#   ./install.sh              install VST3 + standalone for the current user
#   ./install.sh --uninstall  remove a previous install
#
# Install locations (override with env vars):
#   VST3_DIR  VST3 plug-in dir   (default: ~/.vst3, the standard per-user location)
#   BIN_DIR   standalone app dir (default: ~/.local/bin)

set -euo pipefail

VST3_DIR="${VST3_DIR:-$HOME/.vst3}"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [[ "${1:-}" == "--uninstall" ]]; then
  rm -rf "$VST3_DIR/TONE3000.vst3"
  rm -f "$BIN_DIR/TONE3000"
  echo "TONE3000 uninstalled."
  exit 0
fi

if [[ ! -d "$HERE/TONE3000.vst3" || ! -f "$HERE/TONE3000" ]]; then
  echo "Error: TONE3000.vst3 and/or TONE3000 not found next to this script."
  echo "Run install.sh from inside the extracted release folder."
  exit 1
fi

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
