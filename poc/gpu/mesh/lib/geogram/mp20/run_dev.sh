#!/bin/bash
# MP20: score a runner on the three development corpora. usage: run_dev.sh <tag> <runner>   -> storage/mesh/mp20/<tag>_{touch,held,set3}.tsv
cd /Volumes/CrucialX9/Projects/mongo; M=storage/mesh/mp18
bash poc/gpu/mesh/lib/geogram/mp20/run_shards.sh $1_touch $2 $M/touch_corpus
bash poc/gpu/mesh/lib/geogram/mp20/run_shards.sh $1_held $2 $M/heldout_corpus
bash poc/gpu/mesh/lib/geogram/mp20/run_shards.sh $1_set3 $2 $M/set3_corpus
echo all > storage/mesh/mp20/done_dev_$1.txt
