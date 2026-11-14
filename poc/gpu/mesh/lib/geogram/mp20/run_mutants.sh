#!/bin/bash
# MP20: run the three planted mutants of the frozen patch over the three development families. Mutant sources: storage/mesh/mp20/mutants/*.cpp
# (generated from the frozen patched file by text replacement, linked ahead of libgeogram.a). Output: storage/mesh/mp20/mut_<name>_{touch,held,set3}.tsv
cd /Volumes/CrucialX9/Projects/mongo
for m in m1_revert_interior_choice m2_use_contact_vertex m3_edge_midpoint; do bash poc/gpu/mesh/lib/geogram/mp20/run_dev.sh mut_$m $PWD/storage/mesh/mp20/mutants/runner_$m; done
echo all > storage/mesh/mp20/done_mutants.txt
