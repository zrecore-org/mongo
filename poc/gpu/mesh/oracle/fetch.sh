#!/bin/bash
# Fetches the pinned third-party files the mesh POCs need into poc/gpu/build/third_party and verifies them against oracle/SHA256SUMS.
# Nothing fetched is ever committed (poc/gpu/.gitignore ignores build/).
set -e
# macOS has shasum, most Linux images have sha256sum; both print "hash  path"
SHA256="shasum -a 256"; command -v sha256sum >/dev/null && SHA256=sha256sum
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HERE/../../build/third_party"
mkdir -p "$DEST/shewchuk"
if [ ! -f "$DEST/shewchuk/predicates.c" ]; then
  curl -fsSL -o "$DEST/shewchuk/predicates.c" "https://www.cs.cmu.edu/afs/cs/project/quake/public/code/predicates.c"
fi
cd "$DEST"
grep -v '^#' "$HERE/SHA256SUMS" | $SHA256 -c -
