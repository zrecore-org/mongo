#!/bin/bash
# MP22: score one runner on touch_*, held-out, set 3 and the first 200 pairs of set 4 (600 rows), in shards, single-threaded runner.
# usage: run_corpora.sh <tag> <runner> <out_dir> <touch_dir> <held_dir> <set3_dir> <set4_dir> <score_library.py> [jobs]
# Output: <out_dir>/<tag>_{touch,held,set3,set4}.tsv (merged results.tsv rows).
set -u
TAG=$1; RUN=$2; OUT=$3; TD=$4; HD=$5; S3=$6; S4=$7; SCORE=$8; JOBS=${9:-8}
export GEOGRAM_RUNNER_THREADS=1
mkdir -p "$OUT"
one() {  # name corpus nids
  local N=$1 C=$2 NIDS=${3:-0} S="$OUT/$TAG.$1"; mkdir -p "$S"
  if [ "$NIDS" = 0 ]; then tail -n +2 "$C/manifest.tsv" | cut -f1 > "$S/ids.all"; else tail -n +2 "$C/manifest.tsv" | cut -f1 | head -n "$NIDS" > "$S/ids.all"; fi
  python3 -I - "$S" "$JOBS" <<'PY'
import sys
s,k=sys.argv[1],int(sys.argv[2]); ids=open(s+"/ids.all").read().split()
for i in range(k): open(f"{s}/ids_{i}.txt","w").write(",".join(ids[i::k]))
PY
  local pids=() k
  for ((k=0;k<JOBS;k++)); do [ -s "$S/ids_$k.txt" ] || continue
    ( python3 "$SCORE" "${TAG}_${N}_$k" "$RUN" "$C" --points 5000 --cases "$(cat "$S/ids_$k.txt")" --outdir "$S/s$k" > "$S/log_$k.log" 2>&1 ) & pids+=($!); done
  for p in "${pids[@]}"; do wait "$p"; done
  head -1 "$S/s0/results.tsv" > "$OUT/${TAG}_$N.tsv"
  for ((k=0;k<JOBS;k++)); do [ -f "$S/s$k/results.tsv" ] && tail -n +2 "$S/s$k/results.tsv"; done | LC_ALL=C sort -t$'\t' -k1,1 -k2,2 >> "$OUT/${TAG}_$N.tsv"
  echo "$TAG $N: $(tail -n +2 "$OUT/${TAG}_$N.tsv" | awk -F'\t' '{c[$4]++} END{for(v in c) printf "%s=%d ",v,c[v]}') rows=$(tail -n +2 "$OUT/${TAG}_$N.tsv" | wc -l)"
  rm -rf "$S/s"*/*.msh
}
one touch "$TD" 0; one held "$HD" 0; one set3 "$S3" 0; one set4 "$S4" 200
