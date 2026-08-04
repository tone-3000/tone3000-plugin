#!/bin/bash
# Merge two single-arch macOS artefact trees into one universal (lipo'd) tree.
#
# Building each arch separately (instead of CMAKE_OSX_ARCHITECTURES="arm64;x86_64")
# lets sccache hash and reuse object files; a dual -arch clang invocation is not
# cacheable. CI builds arm64 + x86_64 into separate build dirs, then this script
# produces the universal artefacts that create-pkg.sh packages.
#
# Usage (from repo root):
#   ./script/lipo-macos-artefacts.sh \
#     build-arm64/plugin/TONE3000_artefacts/Release \
#     build-x86_64/plugin/TONE3000_artefacts/Release \
#     build/plugin/TONE3000_artefacts/Release

set -euo pipefail

if [[ "$(uname)" != "Darwin" ]]; then
  echo "This script must run on macOS (lipo / ditto)."
  exit 1
fi

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <arch-a-artefacts> <arch-b-artefacts> <out-artefacts>"
  exit 1
fi

ARCH_A="$1"
ARCH_B="$2"
OUT="$3"

for dir in "$ARCH_A" "$ARCH_B"; do
  if [[ ! -d "$dir" ]]; then
    echo "Artefacts dir not found: $dir"
    exit 1
  fi
done

echo "Staging universal tree from $ARCH_A → $OUT"
rm -rf "$OUT"
mkdir -p "$(dirname "$OUT")"
ditto "$ARCH_A" "$OUT"

# Replace every Mach-O in the staged tree with a fat binary of A + B.
# Non-binary files (plists, moduleinfo.json, resources) stay from ARCH_A.
lipo_count=0
while IFS= read -r -d '' rel; do
  # find prints ./path; strip the leading ./
  rel="${rel#./}"
  a_file="${ARCH_A}/${rel}"
  b_file="${ARCH_B}/${rel}"
  out_file="${OUT}/${rel}"

  if [[ ! -f "$b_file" ]]; then
    echo "WARNING: missing counterpart in $ARCH_B: ${rel} (keeping single-arch from A)"
    continue
  fi

  # Skip non-Mach-O (ditto already copied resources; only binaries need lipo).
  if ! file -b "$out_file" | grep -q 'Mach-O'; then
    continue
  fi

  # lipo refuses to overwrite in place when inputs share a path with output.
  tmp="${out_file}.lipo-tmp"
  lipo -create "$a_file" "$b_file" -output "$tmp"
  mv "$tmp" "$out_file"
  lipo_count=$((lipo_count + 1))
  echo "  lipo ${rel}"
  lipo -info "$out_file"
done < <(cd "$OUT" && find . -type f -print0)

if [[ "$lipo_count" -eq 0 ]]; then
  echo "ERROR: no Mach-O binaries were lipo'd; check artefact layouts."
  exit 1
fi

echo "Universal artefacts ready ($lipo_count binaries): $OUT"
