#!/bin/sh
# Proof that the restricted components (TetGen, Triangle, HLBFGS) are neither compiled nor linked.
set -u
G="${GEOGRAM_DIR:-/Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram}"
B="$G/build"
echo "== (a1) compiled translation units: compile_commands.json entries"
python3 -I -c "import json,sys; print(len(json.load(open('$B/compile_commands.json'))))"
echo "== (a2) grep restricted directories in compile_commands.json (third_party/(tetgen|triangle|HLBFGS)/), expect empty:"
grep -E '"file"' "$B/compile_commands.json" | grep -iE 'third_party/(tetgen|triangle|hlbfgs)/'; echo "[matches: $(grep -E '"file"' "$B/compile_commands.json" | grep -ciE 'third_party/(tetgen|triangle|hlbfgs)/')]"
echo "== (a3) same grep over the ninja -v log (any mention of those directories), expect 0:"
grep -ciE 'third_party/(tetgen|triangle|hlbfgs)' "$G/ninja_build.log"
echo "== (a4) every .c/.cpp whose path contains tetgen, triangle or hlbfgs (case-insensitive, whole path):"
grep -E '"file"' "$B/compile_commands.json" | grep -iE 'tetgen|triangle|hlbfgs' | sed 's#.*/src/src/#src/#; s#",*$##'
echo "== (a5) restricted source files that exist in the checkout but are not in the build:"
for d in tetgen triangle HLBFGS; do ls "$G/src/src/lib/geogram/third_party/$d" | grep -E '\.(c|cpp)$' | sed "s#^#$d/#"; done
echo "== (a6) archive members of libgeogram.a whose name matches tetgen|triangle|hlbfgs:"
ar t "$B/lib/libgeogram.a" | grep -iE 'tetgen|triangle|hlbfgs'
for f in "$B/lib/libgeogram.a" "$G/bin/geogram_runner" "$G/bin/geogram_probe"; do
  echo "== (b) nm on $f"
  nm -A "$f" 2>/dev/null > "$G/nm_$(basename $f).txt"; wc -l < "$G/nm_$(basename $f).txt"
  echo "-- (b1) lines matching 'tetgen|hlbfgs' (case-insens), mangled and demangled:"
  grep -iE 'tetgen|hlbfgs' "$G/nm_$(basename $f).txt"; c++filt < "$G/nm_$(basename $f).txt" | grep -icE 'tetgen|hlbfgs'
  echo "-- (b2) lines matching 'triangle' (case-insens): count"
  grep -ic triangle "$G/nm_$(basename $f).txt"
  echo "-- (b3) symbols that belong to the restricted libraries' own APIs (TetGen: tetgenio tetgenbehavior tetgenmesh tetrahedralize; Triangle: triangulateio, global triangulate/trifree/triunsuitable/exactinit; HLBFGS: HLBFGS INIT_HLBFGS HLBFGS_UPDATE_Hessian ICFS Lite_Sparse_Matrix), count:"
  c++filt < "$G/nm_$(basename $f).txt" | grep -E '(tetgenio|tetgenbehavior|tetgenmesh|triangulateio|INIT_HLBFGS|HLBFGS_UPDATE_Hessian|Lite_Sparse_Matrix|Sparse_Entry)|[ :]_?(triangulate|trifree|triunsuitable|exactinit|tetrahedralize|HLBFGS|ICFS)( |\(|$)' | grep -v 'GEO::' | wc -l
done
echo "-- (b4) the 'triangle' matches that are NOT in a GEO:: or Geogram-internal name (demangled; head):"
c++filt < "$G/nm_libgeogram.a.txt" | grep -i triangle | grep -v 'GEO::' | sed 's/^[^ ]* [^ ]* [^ ]* //' | sort -u | head -40
