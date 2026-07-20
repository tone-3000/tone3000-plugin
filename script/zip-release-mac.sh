#!/bin/bash
# Build a ZIP of Release macOS plugin artefacts suitable for upload (Drive, etc.).
#
# Google Drive downloads do NOT remove code signatures. What usually bites you:
#   - com.apple.quarantine added on download — recipients should run after unzip:
#       xattr -cr ~/path/to/TONE3000.app
#       xattr -cr ~/path/to/TONE3000.vst3
#     …or Right-click → Open the first time.
#   - A plain `zip -r` can mishandle symlinks inside .app / .vst3 bundles — this
#     script stages with ditto then archives with ditto -c -k --sequesterRsrc --keepParent.
#
# Usage: from repo root, after Release build:
#   ./script/zip-release-mac.sh
# Already signed artefacts in artefacts dir? Still run this script; it copies with
# ditto and re-signs the staged bundles so what's inside the ZIP is consistent.

set -euo pipefail

# Version comes from the repo-root VERSION file (single source of truth).
VERSION="$(tr -d '[:space:]' < "$(dirname "$0")/../VERSION")"
ZIP_NAME="TONE3000-v${VERSION}-mac"
BUILD_DIR="${BUILD_DIR:-./build/plugin/TONE3000_artefacts/Release}"
ZIP_STAGE="${ZIP_STAGE:-./build/zip-mac-temp}"
OUTPUT_ZIP="./build/${ZIP_NAME}.zip"

if [[ "$(uname)" != "Darwin" ]]; then
  echo "This script must run on macOS (codesign / ditto)."
  exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Release build dir not found: $BUILD_DIR"
  echo "Build Release first (see README)."
  exit 1
fi

echo "Staging plugin bundles with ditto (preserves symlinks / bundle layout)..."
rm -rf "$ZIP_STAGE"
mkdir -p "$ZIP_STAGE/TONE3000"

ditto "${BUILD_DIR}/Standalone/TONE3000.app" "${ZIP_STAGE}/TONE3000/TONE3000.app"

if [[ -d "${BUILD_DIR}/VST3/TONE3000.vst3" ]]; then
  ditto "${BUILD_DIR}/VST3/TONE3000.vst3" "${ZIP_STAGE}/TONE3000/TONE3000.vst3"
fi
if [[ -d "${BUILD_DIR}/AU/TONE3000.component" ]]; then
  ditto "${BUILD_DIR}/AU/TONE3000.component" "${ZIP_STAGE}/TONE3000/TONE3000.component"
fi
if [[ -d "${BUILD_DIR}/AAX/TONE3000.aaxplugin" ]]; then
  ditto "${BUILD_DIR}/AAX/TONE3000.aaxplugin" "${ZIP_STAGE}/TONE3000/TONE3000.aaxplugin"
fi

echo "Ad-hoc signing staged bundles..."
codesign --force --deep --sign - "${ZIP_STAGE}/TONE3000/TONE3000.app"

if [[ -d "${ZIP_STAGE}/TONE3000/TONE3000.vst3" ]]; then
  codesign --force --deep --sign - "${ZIP_STAGE}/TONE3000/TONE3000.vst3"
fi
if [[ -d "${ZIP_STAGE}/TONE3000/TONE3000.component" ]]; then
  codesign --force --deep --sign - "${ZIP_STAGE}/TONE3000/TONE3000.component"
fi
if [[ -d "${ZIP_STAGE}/TONE3000/TONE3000.aaxplugin" ]]; then
  codesign --force --deep --sign - "${ZIP_STAGE}/TONE3000/TONE3000.aaxplugin"
fi

cat > "${ZIP_STAGE}/TONE3000/README.txt" << EOF
TONE3000 macOS (${VERSION})

Install:
  Standalone — copy TONE3000.app wherever you like.
  VST3     — ~/Library/Audio/Plug-Ins/VST3/
  AU       — ~/Library/Audio/Plug-Ins/Components/
  AAX      — /Library/Application Support/Avid/Audio/Plug-Ins/

Downloaded from the web — if macOS blocks the app or plugin after unzip:

  Terminal (replace paths):
    xattr -cr /path/to/TONE3000.app
    xattr -cr /path/to/TONE3000.vst3

  Or Right-click → Open → Open once.

Bundles are ad-hoc signed — not Developer ID / notarized.
EOF

mkdir -p ./build
rm -f "$OUTPUT_ZIP"

echo "Creating ZIP (ditto)..."
ditto -c -k \
  --sequesterRsrc \
  --zlibCompressionLevel 9 \
  --keepParent "${ZIP_STAGE}/TONE3000" \
  "$OUTPUT_ZIP"

xattr -cr "$OUTPUT_ZIP" || true

rm -rf "$ZIP_STAGE"

echo ""
echo "ZIP ready: $OUTPUT_ZIP"
echo ""
echo "After unpacking, signatures should validate. Example:"
echo "  unzip -q '$OUTPUT_ZIP' -d /tmp/tone3000-zip-check"
echo "  codesign --verify --verbose=2 /tmp/tone3000-zip-check/TONE3000/TONE3000.app"
echo ""