#!/bin/bash
# usage: run_heldout.sh <name> <runner>   scores storage/mesh/mp18/heldout_corpus (5000 sample points, same rules) in 6 parallel shards
cd /Volumes/CrucialX9/Projects/mongo; export TMPDIR=$PWD/storage/tmp GEOGRAM_RUNNER_THREADS=1
M=storage/mesh/mp18; C=$M/heldout_corpus; NAME=$1; RUN=$2
tail -n +2 $C/manifest.tsv | cut -f1 | python3 -c "
import sys
ids=sys.stdin.read().split()
for k in range(6): open('$M/held_ids_%d.txt'%k,'w').write(','.join(ids[k::6]))"
for k in 0 1 2 3 4 5; do
  python3 poc/gpu/mesh/oracle/score_library.py held_${NAME}_$k $RUN $C --cases $(cat $M/held_ids_$k.txt) --outdir $M/score_held_${NAME}_$k > $M/held_${NAME}_$k.log 2>&1 &
done
wait; echo done > $M/held_${NAME}_done.txt
