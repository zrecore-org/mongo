#!/bin/bash
# POC 1 experiment: shader variant (precise / plain / precise+SignedZeroInfNanPreserve) x MoltenVK fast-math setting.
# Prints the worst in-range kernel error (units of 2^-44) per op.
cd "$(dirname "$0")"
for fm in 0 1 2; do
  for v in precise plain precise_szinp; do
    MVK_CONFIG_FAST_MATH_ENABLED=$fm ./run.sh poc1_df64 build/shaders $v >/dev/null 2>&1 || { echo "fm=$fm $v: run failed"; continue; }
    python3 verify_df64.py $v | awk -v fm=$fm -v v=$v '
      $1~/^(add|mul|div|sqrt)$/ && ($2=="mixed"||$2=="cancel"||$2=="wide") { if ($5>m[$1]) m[$1]=$5 }
      END { printf "fast_math=%s variant=%-14s worst kernel err (x2^-44): add=%-8s mul=%-8s div=%-8s sqrt=%-8s\n", fm, v, m["add"], m["mul"], m["div"], m["sqrt"] }'
  done
done
