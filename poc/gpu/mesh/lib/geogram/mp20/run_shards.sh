#!/bin/bash
# MP20: score a corpus with a runner in 6 parallel shards, exactly as MP18 run_corpus_shards.sh did (score_library.py, 5000 points default,
# GEOGRAM_RUNNER_THREADS=1), but under storage/mesh/mp20. usage: run_shards.sh <name> <runner> <corpus_dir> [points]
# Output: storage/mesh/mp20/score_<name>_<k>/results.tsv (k=0..5) and storage/mesh/mp20/<name>.tsv (merged, header once). Never run alongside timing.
cd /Volumes/CrucialX9/Projects/mongo; export TMPDIR=$PWD/storage/tmp GEOGRAM_RUNNER_THREADS=1
M=storage/mesh/mp20; NAME=$1; RUN=$2; C=$3; PTS=${4:-5000}
tail -n +2 $C/manifest.tsv | cut -f1 | python3 -c "
import sys
ids=sys.stdin.read().split()
for k in range(6): open('$M/ids_${NAME}_%d.txt'%k,'w').write(','.join(ids[k::6]))"
for k in 0 1 2 3 4 5; do
  python3 poc/gpu/mesh/oracle/score_library.py ${NAME}_$k $RUN $C --points $PTS --cases $(cat $M/ids_${NAME}_$k.txt) --outdir $M/score_${NAME}_$k > $M/log_${NAME}_$k.log 2>&1 &
done
wait
head -1 $M/score_${NAME}_0/results.tsv > $M/${NAME}.tsv
for k in 0 1 2 3 4 5; do tail -n +2 $M/score_${NAME}_$k/results.tsv; done | sort -t$'\t' -k1,1 -k2,2 >> $M/${NAME}.tsv
echo done > $M/done_${NAME}.txt
