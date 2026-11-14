#!/bin/bash
# Builds bvh_pairs_bench into storage/mesh/embree/ (external volume).
set -e
ROOT=/Volumes/CrucialX9/Projects/mongo; HERE=$ROOT/poc/gpu/mesh/cpu
c++ -std=c++17 -O2 -ffp-contract=off -pthread -DBVH_PAIRS_MAIN -I$ROOT/poc/gpu/mesh/harness -I$HERE $HERE/bvh_pairs.cpp -o $ROOT/storage/mesh/embree/bvh_pairs_bench
