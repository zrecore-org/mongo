#!/bin/bash
# MP25: run every sweep once (frozen generator). usage: run_all.sh <mag> <out>
M=$1; O=$2; S=/Volumes/CrucialX9/Projects/mongo/storage/mesh; H=$(dirname "$0")
python3 -I $H/sweep.py unpatched $S/geogram/bin/geogram_runner $M $O/char
python3 -I $H/sweep.py mp20 $S/mp20/geogram_patched/bin/geogram_runner $M $O/char
python3 -I $H/sweep.py mp22 $S/mp22/gen/runner_mp22_m1 $M $O/char
python3 -I $H/sweep.py mp22_scaled $S/mp22/gen/runner_mp22_m1 $M $O/mit --scale
python3 -I $H/sweep.py fidelity $S/mp24/bin/geogram_fidelity $M $O/mit
python3 -I $H/sweep.py fidelity_scaled $S/mp24/bin/geogram_fidelity $M $O/mit --scale
echo ALLDONE
