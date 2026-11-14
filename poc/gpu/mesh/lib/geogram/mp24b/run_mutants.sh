#!/bin/bash
# MP24b mutants (M1): score the five planted mutants of the v2 pipeline (NOSCALE, SCALE19, C0, NOBACK, TLARGE) on all six families with the same script and scorer as the pipeline, then compare with the clean run.
# usage: WORK=<mp24b work dir (corpora generated, score24b done)> MUT_BIN=<dir with geogram_fidelity2_mut_*> run_mutants.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; CI="$HERE/../../../ci"
for m in NOSCALE SCALE19 C0 NOBACK TLARGE; do
  FID2_BIN="$MUT_BIN/geogram_fidelity2_mut_$m" SCORE24B_DIR="$WORK/mut_$m" JOBS="${JOBS:-6}" "$CI/mp21_geogram_xplat.sh" score24b > "$WORK/mut_$m.log" 2>&1
done
python3 -I "$HERE/mutant_report.py" "$WORK" "$WORK/score24b" NOSCALE SCALE19 C0 NOBACK TLARGE | tee "$WORK/mutant_report.txt"
