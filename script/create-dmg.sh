#!/bin/bash
# Create DMG installer for TONE3000 plugin (ad-hoc signed)

set -e

VERSION="0.0.1"
DMG_NAME="TONE3000-v${VERSION}"
BUILD_DIR="./build/plugin/TONE3000_artefacts/Release"
TEMP_DIR="./build/dmg-temp"
OUTPUT_DIR="./build"

echo "Creating TONE3000 DMG installer..."

# Clean up any previous temp directory
rm -rf "$TEMP_DIR"
mkdir -p "$TEMP_DIR/TONE3000"

# Copy standalone app
echo "Copying Standalone app..."
cp -R "$BUILD_DIR/Standalone/TONE3000.app" "$TEMP_DIR/TONE3000/"

# Copy VST3 (if exists)
if [ -d "$BUILD_DIR/VST3/TONE3000.vst3" ]; then
  echo "Copying VST3..."
  cp -R "$BUILD_DIR/VST3/TONE3000.vst3" "$TEMP_DIR/TONE3000/"
fi

# Copy AU (if exists)
if [ -d "$BUILD_DIR/AU/TONE3000.component" ]; then
  echo "Copying AU..."
  cp -R "$BUILD_DIR/AU/TONE3000.component" "$TEMP_DIR/TONE3000/"
fi

# Copy AAX (if exists)
if [ -d "$BUILD_DIR/AAX/TONE3000.aaxplugin" ]; then
  echo "Copying AAX..."
  cp -R "$BUILD_DIR/AAX/TONE3000.aaxplugin" "$TEMP_DIR/TONE3000/"
fi

# Ad-hoc sign all binaries
echo "Ad-hoc signing binaries..."

# Sign standalone app
echo "  Signing TONE3000.app..."
codesign --force --deep --sign - "$TEMP_DIR/TONE3000/TONE3000.app"

# Sign VST3 (if exists)
if [ -d "$TEMP_DIR/TONE3000/TONE3000.vst3" ]; then
  echo "  Signing TONE3000.vst3..."
  codesign --force --deep --sign - "$TEMP_DIR/TONE3000/TONE3000.vst3"
fi

# Sign AU (if exists)
if [ -d "$TEMP_DIR/TONE3000/TONE3000.component" ]; then
  echo "  Signing TONE3000.component..."
  codesign --force --deep --sign - "$TEMP_DIR/TONE3000/TONE3000.component"
fi

# Sign AAX (if exists)
if [ -d "$TEMP_DIR/TONE3000/TONE3000.aaxplugin" ]; then
  echo "  Signing TONE3000.aaxplugin..."
  codesign --force --deep --sign - "$TEMP_DIR/TONE3000/TONE3000.aaxplugin"
fi

# Verify signatures
echo "Verifying signatures..."
codesign --verify --verbose "$TEMP_DIR/TONE3000/TONE3000.app" || echo "  ⚠️  App signature verification failed"

# Create README
cat > "$TEMP_DIR/TONE3000/README.txt" << 'EOF'
TONE3000 Plugin v0.0.1

Installation Instructions:

STANDALONE APP:
- Double-click TONE3000.app to run

VST3:
- Copy TONE3000.vst3 to: /Library/Audio/Plug-Ins/VST3/
  or ~/Library/Audio/Plug-Ins/VST3/

AU (Audio Unit):
- Copy TONE3000.component to: /Library/Audio/Plug-Ins/Components/
  or ~/Library/Audio/Plug-Ins/Components/

AAX (Pro Tools):
- Copy TONE3000.aaxplugin to: /Library/Application Support/Avid/Audio/Plug-Ins/

IMPORTANT - First Launch:
This app is ad-hoc signed. On first launch:
1. Right-click the app and select "Open"
2. Click "Open" in the security dialog
3. Or: Go to System Settings > Privacy & Security and click "Open Anyway"

If you still get errors, run in Terminal:
  xattr -cr /path/to/TONE3000.app

Enjoy!
EOF

# Create DMG
echo "Creating DMG..."
OUTPUT_DMG="$OUTPUT_DIR/${DMG_NAME}.dmg"

# Remove old DMG if exists
rm -f "$OUTPUT_DMG"

# Create DMG using hdiutil
hdiutil create -volname "TONE3000" \
  -srcfolder "$TEMP_DIR/TONE3000" \
  -ov -format UDZO \
  "$OUTPUT_DMG"

# Sign the DMG itself
echo "Signing DMG..."
codesign --force --sign - "$OUTPUT_DMG"

# Remove quarantine from DMG
echo "Removing quarantine from DMG..."
xattr -cr "$OUTPUT_DMG"

# Clean up temp directory
rm -rf "$TEMP_DIR"

echo ""
echo "✅ DMG created and signed successfully!"
echo "📦 Location: $OUTPUT_DMG"
echo ""
echo "Signature info:"
codesign -dvv "$OUTPUT_DMG" 2>&1 | grep "Signature"
echo ""
echo "Your friend should:"
echo "  1. Download and mount the DMG"
echo "  2. Right-click TONE3000.app and select 'Open' (first time only)"
echo "  3. Click 'Open' in the security dialog"
echo ""