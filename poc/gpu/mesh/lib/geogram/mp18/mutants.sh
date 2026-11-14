#!/bin/bash
# MP18 guard mutants: run the touch_* family with MP18_MUTANT=<name> and count FAIL / REFUSED rows. usage: mutants.sh <guard> <touch_corpus> <outdir>
cd /Volumes/CrucialX9/Projects/mongo; export TMPDIR=$PWD/storage/tmp GEOGRAM_RUNNER_THREADS=1
for m in skipaxis offbyone nocheck; do
  MP18_MUTANT=$m python3 poc/gpu/mesh/oracle/score_library.py mut_$m "$1" "$2" --outdir "$3/mut_$m" > "$3/mut_$m.log" 2>&1
  echo "mutant $m: $(tail -2 "$3/mut_$m.log" | head -1)"
done
