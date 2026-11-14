#!/bin/bash
# Builds nothing; runs the built generator and checker (cmake --build build --target gen_corpus corpus_check from poc/gpu).
#   gen_corpus.sh              generate the canonical corpus (--many 200) twice, require identical bytes, run corpus_check, print the sha256 list
#   gen_corpus.sh --check      same, then compare the sha256 list with EXPECTED_SHA256.txt (what CI does on x86_64 and aarch64)
#   gen_corpus.sh --update     rewrite EXPECTED_SHA256.txt from this machine's output
#   gen_corpus.sh --selftest   corrupt copies of the corpus three ways and require corpus_check to FAIL each time
set -e
# macOS has shasum, most Linux images have sha256sum; both print "hash  path"
SHA256="shasum -a 256"; command -v sha256sum >/dev/null && SHA256=sha256sum
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
GEN="$ROOT/build/mesh/gen_corpus"; CHK="$ROOT/build/mesh/corpus_check"
A="$ROOT/build/corpus"; B="$ROOT/build/corpus_again"
hashes() { (cd "$1" && find . -type f | LC_ALL=C sort | xargs $SHA256); }
rm -rf "$A" "$B"; "$GEN" "$A" --many 200 >/dev/null; "$GEN" "$B" --many 200 >/dev/null
if [ "$(hashes "$A")" != "$(hashes "$B")" ]; then echo "NOT reproducible: two runs differ"; exit 1; fi
echo "reproducible: two runs give identical bytes ($(hashes "$A" | wc -l | tr -d ' ') files)"
"$CHK" "$A"
case "$1" in
  --update) hashes "$A" > "$HERE/EXPECTED_SHA256.txt"; echo "wrote EXPECTED_SHA256.txt" ;;
  --check) if diff <(hashes "$A") "$HERE/EXPECTED_SHA256.txt" >/dev/null; then echo "matches EXPECTED_SHA256.txt"; else echo "DIFFERS from EXPECTED_SHA256.txt:"; diff <(hashes "$A") "$HERE/EXPECTED_SHA256.txt" | head -5; exit 1; fi ;;
  --selftest)
    fail=0
    M="$ROOT/build/corpus_mut"
    rm -rf "$M"; cp -R "$A" "$M"; python3 - "$M" <<'PY'
import sys,re
d=sys.argv[1]; p=d+'/manifest.tsv'; s=open(p).read().split('\n')
# mutation 1: change one expected volume digit
for i,l in enumerate(s):
    if l.startswith('box_overlap_generic'):
        c=l.split('\t'); c[10]=c[10].replace('*2^','1*2^',1); s[i]='\t'.join(c); break
open(p,'w').write('\n'.join(s))
PY
    if "$CHK" "$M" >/dev/null; then echo "mutation 1 (expected volume altered): NOT caught"; fail=1; else echo "mutation 1 (expected volume altered): caught"; fi
    rm -rf "$M"; cp -R "$A" "$M"; sed -i.bak 's/^box_identical\tidentical\tvalid,identical/box_identical\tidentical\topen,identical/' "$M/manifest.tsv"
    if "$CHK" "$M" >/dev/null; then echo "mutation 2 (false tag 'open'): NOT caught"; fail=1; else echo "mutation 2 (false tag 'open'): caught"; fi
    rm -rf "$M"; cp -R "$A" "$M"; python3 - "$M" <<'PY'
import sys,struct
f=sys.argv[1]+'/adversarial/box_face_touch_full_A.msh'; b=bytearray(open(f,'rb').read())
# mutation 3: move one vertex x coordinate (first vertex, first double) by overwriting it with 7.0
b[12:20]=struct.pack('<d',7.0); open(f,'wb').write(b)
PY
    if "$CHK" "$M" >/dev/null; then echo "mutation 3 (a vertex moved): NOT caught"; fail=1; else echo "mutation 3 (a vertex moved): caught"; fi
    rm -rf "$M" "$B"; exit $fail ;;
esac
rm -rf "$B"
