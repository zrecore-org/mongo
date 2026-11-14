#!/bin/bash
# After the single acceptance run of the patched runner on set 4: context runs (unpatched, mutants m1 m2 m3) on the same corpus, not used for any decision.
cd /Volumes/CrucialX9/Projects/mongo; M=storage/mesh/mp20
bash poc/gpu/mesh/lib/geogram/mp20/run_shards.sh s4_u $PWD/$M/baseline/geogram_runner_unpatched $M/set4_corpus
for m in m1_revert_interior_choice m2_use_contact_vertex m3_edge_midpoint; do bash poc/gpu/mesh/lib/geogram/mp20/run_shards.sh s4_mut_$m $PWD/$M/mutants/runner_$m $M/set4_corpus; done
echo all > $M/done_s4_context.txt
