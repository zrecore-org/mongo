#!/bin/bash
# usage: run_corpus_shards.sh <name> <runner> <corpus_dir> [points]   score a corpus in 6 parallel shards; results in storage/mesh/mp18/score_<name>_<k>/
cd /Volumes/CrucialX9/Projects/mongo; export TMPDIR=$PWD/storage/tmp GEOGRAM_RUNNER_THREADS=1
M=storage/mesh/mp18; NAME=$1; RUN=$2; C=$3; PTS=${4:-5000}
tail -n +2 $C/manifest.tsv | cut -f1 | python3 -c "
import sys
ids=sys.stdin.read().split()
for k in range(6): open('$M/ids_${NAME}_%d.txt'%k,'w').write(','.join(ids[k::6]))"
for k in 0 1 2 3 4 5; do
  python3 poc/gpu/mesh/oracle/score_library.py ${NAME}_$k $RUN $C --points $PTS --cases $(cat $M/ids_${NAME}_$k.txt) --outdir $M/score_${NAME}_$k > $M/log_${NAME}_$k.log 2>&1 &
done
wait; echo done > $M/done_${NAME}.txt
