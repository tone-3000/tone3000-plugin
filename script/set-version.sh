#!/bin/bash
# Bump the plugin version everywhere in one shot.
#
# Usage:
#   ./script/set-version.sh 0.1.0
#
# The repo-root VERSION file is the single source of truth:
#   - CMake (root + plugin/) reads it at configure time → JucePlugin_VersionString
#     → the UI's getPluginVersion native function → the in-app update check.
#   - script/create-pkg.sh reads it for the installer name.
#   - CI (.github/workflows/build.yml) reads it for the Windows installer define,
#     the Linux tarball name and the uploaded artifact names.
#
# This script only has to update VERSION and mirror it into ui/package.json
# (+ lockfile), which npm requires to carry its own copy.
#
# Remember to also bump LATEST_VERSION in the tone3000 web app
# (app/(main)/api/v1/plugin/version/route.ts) when publishing the release.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <major.minor.patch>" >&2
  exit 1
fi

V="$1"
if [[ ! "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid version: '$V' (expected major.minor.patch, e.g. 0.1.0)" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "$V" > "$ROOT/VERSION"
(cd "$ROOT/ui" && npm version "$V" --no-git-tag-version --allow-same-version > /dev/null)

echo "Version set to $V:"
echo "  VERSION"
echo "  ui/package.json + ui/package-lock.json"
echo ""
echo "CMake, packaging scripts and CI read the VERSION file directly."
echo "Don't forget LATEST_VERSION in the tone3000 repo when releasing."
