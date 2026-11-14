#!/usr/bin/env python3 -I
"""MP26 step 1: the selection rule, applied to the Thingi10K metadata (metadata/input_summary.csv and metadata/geometry_data.csv at the pinned HF commit
2d5d3b2f3cd3711028ad75b12788c13b25559ec6, copied to storage/thingi10k_raw/metadata/). Frozen before any tool is run.
RULE (eligible): the raw file is .stl AND input_summary Closed == TRUE AND Edge manifold == TRUE AND Vertex manifold == TRUE AND geometry_data num_self_intersections == 0
                 AND 4 <= num_faces <= 200000.   (no condition on orientation, degenerate faces, duplicates, components: those are reported, not filtered)
ORDER: eligible ids sorted by sha256("20261114:" + id) (hex); the first 560 are the selected models (platform independent).
ROLES: selected[0:150] -> pairs 'perm' (model + copy under an exact signed axis permutation with det +1, plus offset)
       selected[150:280] -> pairs 'rot' (model + copy under a random double-precision rotation, plus offset)
       selected[280:560] -> 140 pairs 'diff' (selected[280+2i], selected[281+2i] rescaled to a comparable size and offset to overlap)   => 150 + 130 + 140 = 420 pairs
usage: select_models.py <storage/thingi10k_raw> <outdir (poc/gpu/mesh/corpus/real)>"""
import csv, hashlib, os, sys
raw, out = sys.argv[1], sys.argv[2]
SEED = "20261114"
g = {r["file_id"]: r for r in csv.DictReader(open(os.path.join(raw, "metadata/geometry_data.csv")))}
s = {r["ID"]: r for r in csv.DictReader(open(os.path.join(raw, "metadata/input_summary.csv")))}
elig = []
for i in sorted(s, key=int):
    if i not in g or not os.path.exists(os.path.join(raw, "Thingi10K/raw_meshes", i + ".stl")): continue
    r, q = s[i], g[i]
    nf = int(q["num_faces"])
    if r["Closed"] == "TRUE" and r["Edge manifold"] == "TRUE" and r["Vertex manifold"] == "TRUE" and q["num_self_intersections"] == "0" and 4 <= nf <= 200000:
        elig.append(i)
order = sorted(elig, key=lambda i: hashlib.sha256((SEED + ":" + i).encode()).hexdigest())
sel = order[:560]
open(os.path.join(out, "eligible_ids.txt"), "w").write("\n".join(elig) + "\n")
with open(os.path.join(out, "selected_models.tsv"), "w") as f:
    f.write("id\trole\tnum_faces\tlicense\tflags(oriented,PWN,single_component,no_degenerate,no_duplicated)\n")
    for k, i in enumerate(sel):
        role = "perm" if k < 150 else "rot" if k < 280 else "diff"
        f.write("\t".join([i, role, g[i]["num_faces"], s[i]["License"], ",".join([g[i]["oriented"], s[i]["PWN"], s[i]["Single Component"], s[i]["No degenerate faces"], s[i]["No duplicated faces"]])]) + "\n")
print(len(elig), "eligible,", len(sel), "selected")
