#!/usr/bin/env bash
# Remove macOS quarantine from CI-built artifacts.
# Run this after extracting a downloaded build (e.g. from GitHub Actions).
# Usage: ./script/fix-quarantine-macos.sh [path-to-extracted-artifacts]
# Example: ./script/fix-quarantine-macos.sh ~/Downloads/TONE3000-macOS-ARM64-Release

set -e
ROOT="${1:-.}"
if [[ ! -d "$ROOT" ]]; then
  echo "Usage: $0 <path-to-extracted-artifacts>"
  echo "Example: $0 ~/Downloads/TONE3000-macOS-ARM64-Release"
  exit 1
fi
echo "Removing quarantine from: $ROOT"
xattr -cr "$ROOT"
echo "Done. You can now open the Standalone app or install VST3/AU."
