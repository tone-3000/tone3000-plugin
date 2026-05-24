#!/bin/bash
# Build a macOS .pkg installer wizard for TONE3000 (Standalone, VST3, AU, AAX).
#
# Two modes:
#
# 1. Ad-hoc (default, no env vars):
#    Bundles inside are ad-hoc codesigned, the .pkg envelope is unsigned.
#    Recipients right-click the .pkg → Open the first time. Installed bundles
#    sidestep App Translocation, so the WebView UI loads on first launch.
#
# 2. Developer ID + notarization (when env vars are set):
#    Bundles are signed with `Developer ID Application` + Hardened Runtime
#    + secure timestamp; the .pkg is signed with `Developer ID Installer`;
#    the .pkg is then submitted to `xcrun notarytool` and stapled.
#    Recipients can double-click the .pkg with no Gatekeeper warning.
#
# Usage:
#   # Ad-hoc:
#   ./script/create-pkg.sh
#
#   # Fully signed + notarized:
#   SIGN_ID_APP='Developer ID Application: Your Name (TEAMID)' \
#   SIGN_ID_PKG='Developer ID Installer:   Your Name (TEAMID)' \
#   NOTARY_PROFILE='tone3000-notary' \
#     ./script/create-pkg.sh
#
# Environment overrides:
#   RELEASE=path           override the Release artefacts dir
#   STAGE=path             override the temp staging dir
#   SIGN_ID_APP=identity   Developer ID Application identity (enables real signing)
#   SIGN_ID_PKG=identity   Developer ID Installer identity (signs the .pkg envelope)
#   NOTARY_PROFILE=name    `notarytool store-credentials` profile name to use
#   ENTITLEMENTS=path      Standalone app entitlements (defaults to plugin/TONE3000-Standalone.entitlements)
#
# Prereqs for Developer ID mode:
#   - Both certs in your login keychain (`security find-identity -v -p basic`).
#   - notarytool credentials saved once:
#       xcrun notarytool store-credentials "tone3000-notary" \
#         --apple-id "you@example.com" --team-id "TEAMID" \
#         --password "app-specific-password"

set -euo pipefail

VERSION="0.0.1"
PKG_NAME="TONE3000-v${VERSION}"
RELEASE="${RELEASE:-./build/plugin/TONE3000_artefacts/Release}"
STAGE="${STAGE:-./build/pkg-stage}"
COMPONENTS_DIR="${COMPONENTS_DIR:-./build/pkg-components}"
OUTPUT_PKG="./build/${PKG_NAME}.pkg"
INSTALLER_DIR="./script/installer"
ENTITLEMENTS="${ENTITLEMENTS:-./plugin/TONE3000-Standalone.entitlements}"

SIGN_ID_APP="${SIGN_ID_APP:-}"
SIGN_ID_PKG="${SIGN_ID_PKG:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

if [[ "$(uname)" != "Darwin" ]]; then
  echo "This script must run on macOS (pkgbuild / productbuild)."
  exit 1
fi

if [[ ! -d "$RELEASE" ]]; then
  echo "Release build dir not found: $RELEASE"
  echo "Build Release first (see README)."
  exit 1
fi

if [[ ! -d "$RELEASE/Standalone/TONE3000.app" ]]; then
  echo "Standalone build not found at $RELEASE/Standalone/TONE3000.app"
  exit 1
fi

if [[ -n "$SIGN_ID_APP" && ! -f "$ENTITLEMENTS" ]]; then
  echo "Entitlements file not found: $ENTITLEMENTS"
  echo "Set ENTITLEMENTS=path or ensure plugin/TONE3000-Standalone.entitlements exists."
  exit 1
fi

if [[ -n "$SIGN_ID_APP" ]]; then
  echo "Signing mode: Developer ID"
  echo "  App:       $SIGN_ID_APP"
  echo "  Installer: ${SIGN_ID_PKG:-<unset — .pkg envelope will be unsigned!>}"
  echo "  Notary:    ${NOTARY_PROFILE:-<unset — skipping notarization>}"
else
  echo "Signing mode: ad-hoc"
fi

# ─── 0. Helper: sign one bundle ──────────────────────────────────────────────

sign_bundle() {
  local bundle="$1"
  local extra_args=()

  if [[ "$bundle" == *.app ]]; then
    extra_args=( --entitlements "$ENTITLEMENTS" )
  fi

  if [[ -n "$SIGN_ID_APP" ]]; then
    codesign --force --deep --options runtime --timestamp \
      ${extra_args[@]+"${extra_args[@]}"} \
      --sign "$SIGN_ID_APP" \
      "$bundle"
  else
    codesign --force --deep --sign - "$bundle"
  fi
}

echo "Cleaning stage..."
rm -rf "$STAGE" "$COMPONENTS_DIR"
mkdir -p "$STAGE/standalone/Applications"
mkdir -p "$STAGE/vst3"
mkdir -p "$STAGE/au"
mkdir -p "$STAGE/aax"
mkdir -p "$COMPONENTS_DIR"

# ─── 1. Stage artefacts at their final layout ────────────────────────────────

echo "Staging Standalone..."
ditto "$RELEASE/Standalone/TONE3000.app" "$STAGE/standalone/Applications/TONE3000.app"

HAVE_VST3=0
HAVE_AU=0
HAVE_AAX=0

if [[ -d "$RELEASE/VST3/TONE3000.vst3" ]]; then
  echo "Staging VST3..."
  ditto "$RELEASE/VST3/TONE3000.vst3" "$STAGE/vst3/TONE3000.vst3"
  HAVE_VST3=1
fi

if [[ -d "$RELEASE/AU/TONE3000.component" ]]; then
  echo "Staging AU..."
  ditto "$RELEASE/AU/TONE3000.component" "$STAGE/au/TONE3000.component"
  HAVE_AU=1
fi

if [[ -d "$RELEASE/AAX/TONE3000.aaxplugin" ]]; then
  echo "Staging AAX..."
  ditto "$RELEASE/AAX/TONE3000.aaxplugin" "$STAGE/aax/TONE3000.aaxplugin"
  HAVE_AAX=1
fi

xattr -cr "$STAGE" 2>/dev/null || true

# ─── 2. Sign each staged bundle ──────────────────────────────────────────────

echo "Signing staged bundles..."
sign_bundle "$STAGE/standalone/Applications/TONE3000.app"
[[ $HAVE_VST3 -eq 1 ]] && sign_bundle "$STAGE/vst3/TONE3000.vst3"
[[ $HAVE_AU   -eq 1 ]] && sign_bundle "$STAGE/au/TONE3000.component"
[[ $HAVE_AAX  -eq 1 ]] && sign_bundle "$STAGE/aax/TONE3000.aaxplugin"

# ─── 3. Build component .pkg files (one per install location) ────────────────

echo "Building component packages..."

pkgbuild \
  --root "$STAGE/standalone" \
  --identifier "com.tone3000.standalone" \
  --version "$VERSION" \
  --install-location "/" \
  "$COMPONENTS_DIR/_standalone.pkg"

if [[ $HAVE_VST3 -eq 1 ]]; then
  pkgbuild \
    --root "$STAGE/vst3" \
    --identifier "com.tone3000.vst3" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$COMPONENTS_DIR/_vst3.pkg"
fi

if [[ $HAVE_AU -eq 1 ]]; then
  pkgbuild \
    --root "$STAGE/au" \
    --identifier "com.tone3000.au" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "$COMPONENTS_DIR/_au.pkg"
fi

if [[ $HAVE_AAX -eq 1 ]]; then
  pkgbuild \
    --root "$STAGE/aax" \
    --identifier "com.tone3000.aax" \
    --version "$VERSION" \
    --install-location "/Library/Application Support/Avid/Audio/Plug-Ins" \
    "$COMPONENTS_DIR/_aax.pkg"
fi

# ─── 4. Generate distribution.xml for the components we actually built ──────

DIST_XML="$COMPONENTS_DIR/distribution.xml"
HAS_WELCOME=0
HAS_CONCLUSION=0
[[ -f "$INSTALLER_DIR/Resources/welcome.html"    ]] && HAS_WELCOME=1
[[ -f "$INSTALLER_DIR/Resources/conclusion.html" ]] && HAS_CONCLUSION=1

{
  echo '<?xml version="1.0" encoding="utf-8"?>'
  echo '<installer-gui-script minSpecVersion="2">'
  echo "  <title>TONE3000 ${VERSION}</title>"
  echo '  <options customize="always" allow-external-scripts="no" rootVolumeOnly="false" />'
  echo '  <domains enable_localSystem="true" />'

  [[ $HAS_WELCOME    -eq 1 ]] && echo '  <welcome file="welcome.html" mime-type="text/html" />'
  [[ $HAS_CONCLUSION -eq 1 ]] && echo '  <conclusion file="conclusion.html" mime-type="text/html" />'

  echo '  <choices-outline>'
  echo '    <line choice="standalone" />'
  [[ $HAVE_VST3 -eq 1 ]] && echo '    <line choice="vst3" />'
  [[ $HAVE_AU   -eq 1 ]] && echo '    <line choice="au" />'
  [[ $HAVE_AAX  -eq 1 ]] && echo '    <line choice="aax" />'
  echo '  </choices-outline>'

  cat <<XML
  <choice id="standalone" title="Standalone App" description="Installs TONE3000.app to /Applications.">
    <pkg-ref id="com.tone3000.standalone" />
  </choice>
  <pkg-ref id="com.tone3000.standalone" version="${VERSION}" auth="root">_standalone.pkg</pkg-ref>
XML

  if [[ $HAVE_VST3 -eq 1 ]]; then
    cat <<XML
  <choice id="vst3" title="VST3 Plug-In" description="Installs TONE3000.vst3 to /Library/Audio/Plug-Ins/VST3.">
    <pkg-ref id="com.tone3000.vst3" />
  </choice>
  <pkg-ref id="com.tone3000.vst3" version="${VERSION}" auth="root">_vst3.pkg</pkg-ref>
XML
  fi

  if [[ $HAVE_AU -eq 1 ]]; then
    cat <<XML
  <choice id="au" title="Audio Unit (AU)" description="Installs TONE3000.component to /Library/Audio/Plug-Ins/Components.">
    <pkg-ref id="com.tone3000.au" />
  </choice>
  <pkg-ref id="com.tone3000.au" version="${VERSION}" auth="root">_au.pkg</pkg-ref>
XML
  fi

  if [[ $HAVE_AAX -eq 1 ]]; then
    cat <<XML
  <choice id="aax" title="AAX (Pro Tools)" description="Installs TONE3000.aaxplugin to /Library/Application Support/Avid/Audio/Plug-Ins.">
    <pkg-ref id="com.tone3000.aax" />
  </choice>
  <pkg-ref id="com.tone3000.aax" version="${VERSION}" auth="root">_aax.pkg</pkg-ref>
XML
  fi

  echo '</installer-gui-script>'
} > "$DIST_XML"

# ─── 5. Build the final wizard pkg ───────────────────────────────────────────

RESOURCES_ARG=()
if [[ -d "$INSTALLER_DIR/Resources" ]]; then
  RESOURCES_ARG=( --resources "$INSTALLER_DIR/Resources" )
fi

PRODUCTBUILD_SIGN=()
if [[ -n "$SIGN_ID_PKG" ]]; then
  PRODUCTBUILD_SIGN=( --sign "$SIGN_ID_PKG" )
fi

mkdir -p ./build
rm -f "$OUTPUT_PKG"
echo "Building final installer..."
productbuild \
  --distribution "$DIST_XML" \
  --package-path "$COMPONENTS_DIR" \
  ${RESOURCES_ARG[@]+"${RESOURCES_ARG[@]}"} \
  ${PRODUCTBUILD_SIGN[@]+"${PRODUCTBUILD_SIGN[@]}"} \
  "$OUTPUT_PKG"

xattr -cr "$OUTPUT_PKG" 2>/dev/null || true

# ─── 6. Notarize + staple (optional) ─────────────────────────────────────────

if [[ -n "$SIGN_ID_APP" && -n "$NOTARY_PROFILE" ]]; then
  if [[ -z "$SIGN_ID_PKG" ]]; then
    echo "Skipping notarization: NOTARY_PROFILE set but SIGN_ID_PKG is not."
    echo "Apple will reject an unsigned .pkg envelope."
  else
    echo "Submitting to notarytool (this can take a few minutes)..."
    xcrun notarytool submit "$OUTPUT_PKG" \
      --keychain-profile "$NOTARY_PROFILE" \
      --wait

    echo "Stapling notarization ticket..."
    xcrun stapler staple "$OUTPUT_PKG"
    xcrun stapler validate "$OUTPUT_PKG"
  fi
fi

rm -rf "$STAGE" "$COMPONENTS_DIR"

echo ""
echo "Installer ready: $OUTPUT_PKG"
if [[ -n "$SIGN_ID_APP" && -n "$SIGN_ID_PKG" && -n "$NOTARY_PROFILE" ]]; then
  echo "Signed + notarized + stapled — recipients can double-click with no warning."
elif [[ -n "$SIGN_ID_APP" ]]; then
  echo "Bundles signed with Developer ID."
  if [[ -z "$SIGN_ID_PKG" ]]; then
    echo "  .pkg envelope is unsigned — set SIGN_ID_PKG to sign it."
  fi
  if [[ -z "$NOTARY_PROFILE" ]]; then
    echo "  Not notarized — set NOTARY_PROFILE to notarize and staple."
  fi
else
  echo "Ad-hoc only. Recipients: right-click the .pkg in Finder → Open → Open."
fi
echo "After install, TONE3000.app lives in /Applications and the UI loads"
echo "normally (no App Translocation)."
