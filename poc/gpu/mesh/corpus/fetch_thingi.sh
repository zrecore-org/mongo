#!/bin/bash
# Fetches the Thingi10K raw archive (9.6 GB) into $STORAGE/thingi10k_raw/ (default: ./storage at the repository root, on the external volume, ignored by git;
# never the machine's internal disk) and verifies size and sha256 against THINGI.pin.
# Never committed. Each model has its own license (the dataset says "Each thing in the dataset has its own license"); see CORPUS.md before using any model outside local testing.
# Usage: fetch_thingi.sh --yes-download-9GB
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
[ "$1" = "--yes-download-9GB" ] || { echo "refusing: this downloads 9.6 GB. Re-run with --yes-download-9GB"; exit 2; }
. "$HERE/THINGI.pin"
STORAGE="${STORAGE:-$HERE/../../../../storage}"
DEST="$STORAGE/thingi10k_raw"; mkdir -p "$DEST"
[ -f "$DEST/Thingi10K.tar.gz" ] || curl -fL -o "$DEST/Thingi10K.tar.gz" "$url"
got=$(wc -c < "$DEST/Thingi10K.tar.gz" | tr -d ' ')
[ "$got" = "$size" ] || { echo "size mismatch: got $got, pinned $size"; exit 1; }
echo "$sha256  $DEST/Thingi10K.tar.gz" | shasum -a 256 -c -
tar -xzf "$DEST/Thingi10K.tar.gz" -C "$DEST"
echo "extracted into $DEST"
