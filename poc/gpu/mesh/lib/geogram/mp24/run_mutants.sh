#!/bin/bash
# MP24 bar 7 (M1): score the three planted mutants of the re-centring on G1, offset and thin with the same script and scorer as the pipeline, then compare with the clean run.
# usage: WORK=<mp24 work dir, with geo_chain, corpora (stage corpus24 done)> MUT_BIN=<dir with geogram_fidelity_mut_*> run_mutants.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; CI="$HERE/../../../ci"
for m in WRONGC NOBACK ROUND; do
  FID_BIN="$MUT_BIN/geogram_fidelity_mut_$m" SCORE24_DIR="$WORK/mut_$m" FAMS24="g1:g1_corpus off:fid_corpus/offset thin:fid_corpus/thin" JOBS="${JOBS:-6}" "$CI/mp21_geogram_xplat.sh" score24 > "$WORK/mut_$m.log" 2>&1
done
python3 -I "$HERE/mutant_report.py" "$WORK" "$WORK/score24" WRONGC NOBACK ROUND | tee "$WORK/mutant_report.txt"
