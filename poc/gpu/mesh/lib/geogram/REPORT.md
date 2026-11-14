# MP04 Geogram: permissive build, build and API fit

Status: measurements only. No pass/fail is decided here; scoring happens later.
Date: 2026-11-13. Machine: Apple Silicon (arm64), macOS 27.0.1, 8 cores. Everything lives under `storage/mesh/geogram/` (external volume); only the files in this directory are in the repo tree.

## 1. What was built

- Source: `https://github.com/BrunoLevy/geogram`, checked out at tag v1.10.1; `git rev-parse HEAD` printed exactly `bbcacba146b357926b5524111f56cc70c42182a7` (committer date Tue Sep 1 15:59:49 2026 +0200), checkout at `storage/mesh/geogram/src`.
- Submodules (`.gitmodules` lists 9; only the 4 non-graphics ones were initialised, with `git submodule update --init <path>`; the 5 graphics ones under `geogram_gfx` / `third_party/glfw` are not needed with graphics OFF and were not fetched):

| submodule path | commit | fetched |
|---|---|---|
| src/lib/geogram/third_party/OpenNL | ae782db2db40cc40fcd7659629fc652e5eab1f9e | yes |
| src/lib/geogram/third_party/amgcl | 93827a00fc926d951c75f08fdb1d491912ff7065 | yes |
| src/lib/geogram/third_party/libMeshb | 88095d5ed04bfaf6bee27b777e7190bebdc72230 | yes |
| src/lib/geogram/third_party/rply | 4296cc91b5c8c26d4e7d7aac0cee2b194ffc5800 | yes |
| src/lib/geogram_gfx/third_party/imgui | 01a4cff8f9190b7f3731e4d1cef818ca675e9a42 | no (graphics OFF) |
| src/lib/geogram_gfx/third_party/imoguizmo | a05c47fb2c99e2d40858f7e0a4f6d48490747af9 | no |
| src/lib/geogram_gfx/third_party/imgui_lua_bindings_gomgen | 33cf2501a01ed51e8ad867eda227cb3917011f1c | no |
| src/lib/geogram_gfx/third_party/ImGuiColorTextEdit_geogram | 062408b1887534b9585e7cf3410ae57e3ebe7497 | no |
| src/lib/third_party/glfw | e7ea71be039836da3a98cea55ae5569cb5eb885c | no |

  Nothing else is fetched by the CMake configure step (no FetchContent; no network use during configure or build). The in-tree third-party directories (zlib, PoissonRecon, xatlas, stb_image, lua, TetGen, Triangle, HLBFGS) are plain files of the tag. Restricted sources `third_party/tetgen/tetgen.cpp`, `third_party/triangle/triangle.c` and `third_party/HLBFGS/*.cpp` are present in the checkout and were not compiled.
- Toolchain: Apple clang 21.0.0 (clang-2100.4.1.1), target arm64-apple-darwin27.0.0, libc++; CMake 4.4.4; Ninja 1.13.2.
- Configure command (exact):

```
export TMPDIR=/Volumes/CrucialX9/Projects/mongo/storage/tmp
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DVORPALINE_PLATFORM=Darwin-aarch64-clang \
  -DGEOGRAM_WITH_TETGEN=OFF -DGEOGRAM_WITH_TRIANGLE=OFF -DGEOGRAM_WITH_HLBFGS=OFF \
  -DGEOGRAM_WITH_GRAPHICS=OFF -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF -DGEOGRAM_WITH_LUA=OFF \
  -DGEOGRAM_WITH_TBB=OFF -DGEOGRAM_WITH_FPG=OFF -DGEOGRAM_WITH_GARGANTUA=OFF -DGEOGRAM_LIB_ONLY=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cd build && ninja -j 3 -v > ../ninja_build.log 2>&1        # 148 steps, about 60 s
```

- Option list as set and as read back from `CMakeCache.txt`: GEOGRAM_WITH_TETGEN=OFF, GEOGRAM_WITH_TRIANGLE=OFF, GEOGRAM_WITH_HLBFGS=OFF (the three restricted components); GEOGRAM_WITH_GRAPHICS=OFF (no viewers, no geogram_gfx, no GLFW/imgui); GEOGRAM_LIB_ONLY=ON (no example programs, no `src/bin` tools, no tests); GEOGRAM_WITH_LEGACY_NUMERICS=OFF (ARPACK/SuperLU/CBLAS/CLAPACK, whose license text MP06 could not fully verify); GEOGRAM_WITH_LUA=OFF; GEOGRAM_WITH_TBB=OFF (default); GEOGRAM_WITH_FPG=OFF (default); GEOGRAM_WITH_GARGANTUA=OFF (32-bit indices, default); CMAKE_BUILD_TYPE=Release; `VORPALINE_PLATFORM=Darwin-aarch64-clang`.
- Note on the first attempt: the platform default on Darwin is `Darwin-clang-dynamic`, which silently produced `libgeogram.1.10.1.dylib` and ignored my `-DVORPALINE_BUILD_DYNAMIC=OFF` (the platform file sets it). That build tree was deleted; the static platform `Darwin-aarch64-clang` was used for everything reported here. Compiler flags that Geogram's platform file applies (from the ninja log): `-frounding-math -ffp-contract=off -O3 -DNDEBUG -std=c++17`. `-DGEOGRAM_WITH_PDEL` and `-DNL_WITH_AMGCL` are the only `GEOGRAM_WITH_*`/`NL_*` defines on any compile line; there is no `-DGEOGRAM_WITH_TETGEN`, `-DGEOGRAM_WITH_TRIANGLE`, `-DGEOGRAM_WITH_HLBFGS`.
- Produced artifacts: `build/lib/libgeogram.a` (9.5 MB, 148 members) is the only library; executables are `bin/geogram_runner` and `bin/geogram_probe` (a small availability probe, `probe.cpp`). Build script for the runner: `build.sh` (links `libgeogram.a`, `-std=c++17 -O2 -frounding-math -ffp-contract=off`).

## 2. License proof (raw output of `check_license_proof.sh`, saved as `license_proof_output.txt`)

(a) Compiled sources: 147 translation units in `compile_commands.json` (list in `compiled_sources.txt`); none is under a `third_party/tetgen`, `third_party/triangle` or `third_party/HLBFGS` path; no mention of those directories anywhere in the ninja log.
(b) `nm -A` on every produced library and executable (3 files): zero restricted-library symbols. **Plain-text grep is not literally zero**, see the honest caveat after the raw output.

```
== (a1) compiled translation units: compile_commands.json entries
147
== (a2) grep restricted directories in compile_commands.json (third_party/(tetgen|triangle|HLBFGS)/), expect empty:
[matches: 0]
== (a3) same grep over the ninja -v log (any mention of those directories), expect 0:
0
== (a4) every .c/.cpp whose path contains tetgen, triangle or hlbfgs (case-insensitive, whole path):
src/lib/geogram/mesh/triangle_intersection.cpp
src/lib/geogram/delaunay/delaunay_tetgen.cpp
src/lib/geogram/delaunay/delaunay_triangle.cpp
== (a5) restricted source files that exist in the checkout but are not in the build:
tetgen/tetgen.cpp
triangle/triangle.c
HLBFGS/HLBFGS_BLAS.cpp
HLBFGS/HLBFGS.cpp
HLBFGS/ICFS.cpp
HLBFGS/LineSearch.cpp
HLBFGS/Lite_Sparse_Matrix.cpp
== (a6) archive members of libgeogram.a whose name matches tetgen|triangle|hlbfgs:
triangle_intersection.cpp.o
delaunay_tetgen.cpp.o
delaunay_triangle.cpp.o
== (b) nm on /Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/build/lib/libgeogram.a
   26703
-- (b1) lines matching 'tetgen|hlbfgs' (case-insens), mangled and demangled:
/Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/build/lib/libgeogram.a:delaunay_tetgen.cpp.o: 0000000000000000 D _dummy_delaunay_tetgen_compiled
/Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/build/lib/libgeogram.a:delaunay_tetgen.cpp.o: 0000000000000000 t ltmp0
/Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/build/lib/libgeogram.a:delaunay_tetgen.cpp.o: 0000000000000000 d ltmp1
3
-- (b2) lines matching 'triangle' (case-insens): count
353
-- (b3) symbols that belong to the restricted libraries' own APIs (TetGen: tetgenio tetgenbehavior tetgenmesh tetrahedralize; Triangle: triangulateio, global triangulate/trifree/triunsuitable/exactinit; HLBFGS: HLBFGS INIT_HLBFGS HLBFGS_UPDATE_Hessian ICFS Lite_Sparse_Matrix), count:
       0
== (b) nm on /Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/bin/geogram_runner
    9979
-- (b1) lines matching 'tetgen|hlbfgs' (case-insens), mangled and demangled:
0
-- (b2) lines matching 'triangle' (case-insens): count
200
-- (b3) symbols that belong to the restricted libraries' own APIs (TetGen: tetgenio tetgenbehavior tetgenmesh tetrahedralize; Triangle: triangulateio, global triangulate/trifree/triunsuitable/exactinit; HLBFGS: HLBFGS INIT_HLBFGS HLBFGS_UPDATE_Hessian ICFS Lite_Sparse_Matrix), count:
       0
== (b) nm on /Volumes/CrucialX9/Projects/mongo/storage/mesh/geogram/bin/geogram_probe
   10117
-- (b1) lines matching 'tetgen|hlbfgs' (case-insens), mangled and demangled:
0
-- (b2) lines matching 'triangle' (case-insens): count
200
-- (b3) symbols that belong to the restricted libraries' own APIs (TetGen: tetgenio tetgenbehavior tetgenmesh tetrahedralize; Triangle: triangulateio, global triangulate/trifree/triunsuitable/exactinit; HLBFGS: HLBFGS INIT_HLBFGS HLBFGS_UPDATE_Hessian ICFS Lite_Sparse_Matrix), count:
       0
-- (b4) the 'triangle' matches that are NOT in a GEO:: or Geogram-internal name (demangled; head):
               U ___cxa_allocate_exception
               U ___cxa_free_exception
               U ___cxa_throw
               U ___gxx_personality_v0
               U ___stack_chk_fail
               U ___stack_chk_guard
               U __Unwind_Resume
               U _memmove
               U _strlen
               U operator delete(void*)
               U operator new(unsigned long)
               U std::length_error::~length_error()
               U std::logic_error::logic_error(char const*)
               U typeinfo for std::length_error
               U VBW::ConvexCell::connect_triangles()
               U vtable for std::length_error
(anonymous namespace)::Co3NeManifoldExtraction::flip_triangle(unsigned int)
(anonymous namespace)::RVD_Nd_Impl<100u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<100u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<100u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<20u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<20u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<20u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<2u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<2u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<2u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<3u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<3u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<3u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<4u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<4u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<4u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<6u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<6u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<6u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::RVD_Nd_Impl<8u>::GetConnectedComponentsPrimalTriangles::~GetConnectedComponentsPrimalTriangles()
(anonymous namespace)::RVD_Nd_Impl<8u>::GetConnectedComponentsPrimalTriangles::end_connected_component()
(anonymous namespace)::RVD_Nd_Impl<8u>::GetConnectedComponentsPrimalTriangles::UNINITIALIZED
(anonymous namespace)::TriangleTriangleIntersection::compute()
bool std::__1::__insertion_sort_incomplete[abi:nqe220106]<std::__1::_ClassicAlgPolicy, (anonymous namespace)::CompareTriangles&, unsigned int*>(unsigned int*, unsigned int*, (anonymous namespace)::CompareTriangles&)

```

Honest reading of the raw output:
- `tetgen`: the only matches in any artifact are 3 lines in `libgeogram.a` member `delaunay_tetgen.cpp.o` (symbols `_dummy_delaunay_tetgen_compiled`, `ltmp0`, `ltmp1`). That file is Geogram's own BSD-3 wrapper `src/lib/geogram/delaunay/delaunay_tetgen.cpp`; its body is inside `#ifdef GEOGRAM_WITH_TETGEN` and with the option OFF it compiles to one dummy variable (present so the translation unit is not empty). It contains none of TetGen's code. The linked executables contain 0 `tetgen` lines. Zero is therefore reached only for the executables, and for the library only if that wrapper's marker variable is not counted. I did not patch Geogram to remove it.
- `hlbfgs`: 0 matches in all three files.
- `triangle`: a bare case-insensitive grep cannot be zero for any geometry library (353 lines in the library: `GEO::MeshFacets::create_triangle`, `TriangleTriangleIntersection`, `ray_triangle_intersection`, PoissonRecon's `MarchingCubes::AddTriangles`, xatlas, ...). `delaunay_triangle.cpp.o` (the Triangle wrapper, `#ifdef GEOGRAM_WITH_TRIANGLE` around its whole body) defines no symbol at all (only the local `ltmp0`). The check that targets the restricted code itself (b3: TetGen's `tetgenio/tetgenbehavior/tetgenmesh/tetrahedralize`, Triangle's `triangulateio` and the C-level `triangulate/trifree/triunsuitable/exactinit`, HLBFGS's `INIT_HLBFGS/HLBFGS_UPDATE_Hessian/Lite_Sparse_Matrix/Sparse_Entry/ICFS`, excluding anything inside `GEO::`) counts 0 in all three files. `GEO::MeshFacets::triangulate` and `GEO::CSGBuilder::triangulate` are Geogram's own functions.
- `nm` does not see string constants: `voronoi/CVT.cpp` still contains the literal name "HLBFGS" (it asks the optimizer factory for it and falls back to Lloyd when absent), and `mesh_tetrahedralize.cpp.o` (compiled, Geogram's own code) asks the Delaunay factory for "tetgen". These are names, not TetGen/HLBFGS code.
- Runtime confirmation (`probe_output.txt`): `Optimizer::create("HLBFGS")` returns null; `mesh_tetrahedralize()` returns false with 0 cells.

## 3. The runner

Files: `runner.cpp`, `build.sh` (output `storage/mesh/geogram/bin/geogram_runner`). Reads/writes MSH1 with `../../harness/mesh_io.h`. Uses `GEO::mesh_boolean_operation(result, A, B, "A+B"|"A*B"|"A-B", MESH_BOOL_OPS_DEFAULT)` (what `mesh_union/intersection/difference` forward to; default flags = coplanar simplification ON, neighbour intersection check ON), after `GEO::initialize(GEOGRAM_INSTALL_ALL)` and `set_assert_mode(ASSERT_THROW)`. The wiki page https://github.com/BrunoLevy/geogram/wiki/BooleanOps was not read (no web access used); the API was taken from `mesh_surface_intersection.h` and `src/examples/geogram/boolean_operations/main.cpp` at the tag, which calls `GEO::initialize(GEO::GEOGRAM_INSTALL_ALL)` and `mesh_union/intersection/difference(result, A, B, flags)`.

CLI as specified. Choices where the contract left room:
- Exit 3 (`REFUSED: geogram threw: <what()>`) is used for any `std::exception` thrown inside the boolean call; exit 4 for read/write/usage errors, exceptions outside the boolean call, and unknown exceptions. A crash (signal) is not caught and would appear as 128+signal. None of these paths was exercised by the corpus: **Geogram never threw, so exit 3 never occurred and the REFUSED path is untested**.
- No validation of my own is done on the inputs. Conversion into `GEO::Mesh` is only vertex and triangle copy (no `mesh_repair`).
- `convert_ms=` (stderr) is the sum of A+B conversion in and result conversion out; a second stderr line gives `convert_in_ms convert_out_ms boolean_ms out_nv out_nt`. Output coordinates are copied exactly from `GEO::Mesh` vertices (doubles); non-triangular facets would be fan-triangulated (stderr `note:`; none occurred).
- stdout: Geogram's logger writes to stdout, so the runner `dup2`s stderr onto fd 1 at start and keeps the real stdout only for the `measure` line; Geogram logging is set quiet unless `GEOGRAM_RUNNER_LOG` is set. `measure` prints `volume= area= closed=` only. Run on every corpus case, stdout of `boolean` was 0 bytes.
- Extra knob outside the contract: `GEOGRAM_RUNNER_THREADS=<n>` calls `GEO::Process::set_max_threads(n)` (used for the determinism study below).
- `measure`: `volume` = `Geom::mesh_enclosed_volume` (plain double sum of dot(p1,cross(p2,p3))/6 then `fabs`, so the sign is lost and it is not exact: a unit cube gives 0.99999999999999989); `area` = `Geom::mesh_area(M,3)`; `closed` = `mesh_nb_borders(M)==0` after `facets.connect()` (it returns -1, counted as not closed, for a non-manifold border configuration; an edge shared by 3 facets is not what it tests).

## 4. Corpus runs

Corpus: `poc/gpu/build/corpus/adversarial` (31 cases: 24 box/sphere/torus cases with valid operands plus 7 malformed-operand cases `bad_*`, as generated by MP01; 93 case x op runs per configuration). Driver: `run_corpus.py` (60 s timeout, each run twice, sha256 of each output). Two configurations were run in full: default threading (Geogram's own default worker count) and `GEOGRAM_RUNNER_THREADS=1`.

Totals (each configuration, 93 case-op pairs):

| | default threads | threads=1 |
|---|---|---|
| ran, exit 0 | 93 | 93 |
| exit 3 REFUSED | 0 | 0 |
| exit 4 error | 0 | 0 |
| timeouts | 0 | 0 |
| run1 == run2 byte-identical | 13 of 93 | 93 of 93 |
| run1 == run2 same triangle multiset by coordinates (informational, rotation-normalised) | 88 of 93 | (identical) |
| default-run sha256 == threads=1 sha256 | 11 of 93 | |

Wall time per run (process start to exit, includes read/write): median 6.6 ms, max 168.2 ms. `convert_ms` max 0.107 ms.

Per-case table (default threads: exit code of run1/run2, wall ms of run1/run2, output triangle count, convert_ms, first 16 hex of the sha256 of run1, whether run1 and run2 are byte-identical; last column the threads=1 configuration). Full hashes in `results_default_threads.tsv` and `results_threads1.tsv`.

| case | op | exit | wall ms (run1/run2) | out tris | convert_ms | sha256 (first 16) | run1==run2 bytes | threads=1: wall ms, tris, run1==run2 |
|---|---|---|---|---|---|---|---|---|
| bad_degenerate_triangle | union | 0/0 | 168.2/6.3 | 36 | 0.047 | d730b64d0e3fd12d | NO | 5.2, 36, yes |
| bad_degenerate_triangle | intersection | 0/0 | 6.0/5.7 | 12 | 0.014 | 9918223c5d8e469e | NO | 4.9, 12, yes |
| bad_degenerate_triangle | difference | 0/0 | 6.0/6.2 | 24 | 0.016 | ecaed21468cb6620 | NO | 4.9, 24, yes |
| bad_duplicate_triangle | union | 0/0 | 6.4/6.3 | 36 | 0.016 | d18bf7852383cceb | NO | 4.9, 36, yes |
| bad_duplicate_triangle | intersection | 0/0 | 5.7/6.0 | 12 | 0.017 | c71609afadb4d1b4 | NO | 4.7, 12, yes |
| bad_duplicate_triangle | difference | 0/0 | 6.1/5.9 | 24 | 0.015 | d790c6957d762bb3 | NO | 5.1, 24, yes |
| bad_inverted | union | 0/0 | 6.7/6.5 | 36 | 0.018 | ff978a8f3108efdc | NO | 4.8, 36, yes |
| bad_inverted | intersection | 0/0 | 6.0/5.8 | 12 | 0.015 | 7da00a26ed26ffc6 | NO | 4.8, 12, yes |
| bad_inverted | difference | 0/0 | 6.0/6.1 | 24 | 0.012 | 03ddec457436ccc5 | NO | 4.9, 24, yes |
| bad_nonmanifold_edge | union | 0/0 | 8.4/7.6 | 58 | 0.014 | bde50e3d284f6c41 | NO | 6.4, 58, yes |
| bad_nonmanifold_edge | intersection | 0/0 | 7.1/7.1 | 24 | 0.013 | 0172273c0dee0d69 | NO | 5.7, 24, yes |
| bad_nonmanifold_edge | difference | 0/0 | 7.5/8.4 | 48 | 0.017 | 3f369f652293d99d | NO | 5.7, 48, yes |
| bad_open_box | union | 0/0 | 6.2/5.8 | 12 | 0.014 | b4f726281d8d6cfd | NO | 4.5, 12, yes |
| bad_open_box | intersection | 0/0 | 5.8/6.3 | 22 | 0.021 | 41f51c35fd3e44e9 | NO | 4.3, 2, yes |
| bad_open_box | difference | 0/0 | 5.1/5.0 | 0 | 0.016 | cce2e3a55c7e2217 | yes | 4.6, 0, yes |
| bad_self_intersecting | union | 0/0 | 7.9/8.7 | 60 | 0.018 | 5c1535f428160a63 | NO | 5.9, 60, yes |
| bad_self_intersecting | intersection | 0/0 | 6.9/7.6 | 24 | 0.017 | 2b8600ea62ebb149 | NO | 5.1, 24, yes |
| bad_self_intersecting | difference | 0/0 | 8.4/8.1 | 72 | 0.014 | 4c1f77dce5559607 | NO | 6.2, 72, yes |
| bad_unwelded | union | 0/0 | 5.5/6.3 | 36 | 0.013 | c1ca674ae2a5eb74 | NO | 5.0, 36, yes |
| bad_unwelded | intersection | 0/0 | 5.8/5.8 | 12 | 0.018 | 54cb412445b77a2a | NO | 4.6, 12, yes |
| bad_unwelded | difference | 0/0 | 5.6/6.8 | 24 | 0.014 | 8e83ee08a7b7f695 | NO | 4.8, 24, yes |
| box_coplanar_chain | union | 0/0 | 7.4/7.3 | 12 | 0.019 | d1e7f5712f8ed30c | NO | 5.7, 12, yes |
| box_coplanar_chain | intersection | 0/0 | 7.2/6.7 | 12 | 0.015 | bc40e6c3c32c13f5 | NO | 6.2, 12, yes |
| box_coplanar_chain | difference | 0/0 | 7.2/7.3 | 12 | 0.016 | a55d18340c4cb230 | NO | 6.1, 12, yes |
| box_coplanar_overlap | union | 0/0 | 6.9/6.9 | 28 | 0.012 | 3015bd59de540d6b | NO | 5.3, 28, yes |
| box_coplanar_overlap | intersection | 0/0 | 6.3/6.3 | 12 | 0.012 | cacd36f79c014c19 | NO | 4.9, 12, yes |
| box_coplanar_overlap | difference | 0/0 | 6.7/6.8 | 20 | 0.016 | 04e4110382a0015b | NO | 5.0, 20, yes |
| box_edge_touch | union | 0/0 | 5.4/5.6 | 24 | 0.012 | a2279c8061db9293 | NO | 5.4, 24, yes |
| box_edge_touch | intersection | 0/0 | 4.3/4.2 | 0 | 0.012 | cce2e3a55c7e2217 | yes | 4.3, 0, yes |
| box_edge_touch | difference | 0/0 | 5.3/5.3 | 12 | 0.016 | aeafe449972ddea6 | NO | 4.7, 12, yes |
| box_face_touch_full | union | 0/0 | 5.1/5.0 | 12 | 0.013 | f5ce7d0e10288d2f | NO | 4.6, 12, yes |
| box_face_touch_full | intersection | 0/0 | 4.3/4.5 | 0 | 0.014 | cce2e3a55c7e2217 | yes | 4.2, 0, yes |
| box_face_touch_full | difference | 0/0 | 4.7/5.5 | 12 | 0.015 | 2f52d3ae375758fe | yes | 4.4, 12, yes |
| box_face_touch_partial | union | 0/0 | 6.9/6.3 | 32 | 0.014 | d50ff93a318cfc87 | NO | 5.4, 32, yes |
| box_face_touch_partial | intersection | 0/0 | 5.4/5.3 | 0 | 0.011 | cce2e3a55c7e2217 | yes | 4.8, 0, yes |
| box_face_touch_partial | difference | 0/0 | 5.9/5.8 | 12 | 0.013 | ecbd4ffeb88bbd71 | NO | 5.1, 12, yes |
| box_gap_one_ulp | union | 0/0 | 5.4/5.4 | 24 | 0.013 | afdd17fc9ba82a2c | NO | 4.9, 24, yes |
| box_gap_one_ulp | intersection | 0/0 | 4.4/4.1 | 0 | 0.012 | cce2e3a55c7e2217 | yes | 4.3, 0, yes |
| box_gap_one_ulp | difference | 0/0 | 5.2/5.4 | 12 | 0.016 | 55ada83010438b8d | NO | 4.4, 12, yes |
| box_huge_offset_one_ulp | union | 0/0 | 7.3/7.7 | 12 | 0.016 | c6c5b90be8cf05f6 | NO | 5.7, 12, yes |
| box_huge_offset_one_ulp | intersection | 0/0 | 7.5/7.6 | 12 | 0.012 | 151eb9edbadbd984 | NO | 6.0, 12, yes |
| box_huge_offset_one_ulp | difference | 0/0 | 7.9/7.4 | 12 | 0.012 | e6095fef2e7a7c55 | NO | 5.5, 12, yes |
| box_huge_offset_overlap | union | 0/0 | 7.7/7.5 | 24 | 0.012 | 4c2f6b4d4f93d866 | NO | 5.0, 24, yes |
| box_huge_offset_overlap | intersection | 0/0 | 7.7/7.1 | 24 | 0.013 | 7962f04c3ee7bed2 | NO | 5.3, 24, yes |
| box_huge_offset_overlap | difference | 0/0 | 8.1/8.3 | 36 | 0.013 | 2a4b934fa6d5029c | NO | 4.8, 12, yes |
| box_identical | union | 0/0 | 5.2/5.5 | 12 | 0.017 | a9cfa66a62be7cda | NO | 4.0, 12, yes |
| box_identical | intersection | 0/0 | 5.0/4.9 | 12 | 0.012 | fac4af2197c17b02 | yes | 4.2, 12, yes |
| box_identical | difference | 0/0 | 4.0/4.4 | 0 | 0.013 | cce2e3a55c7e2217 | yes | 3.9, 0, yes |
| box_nested_inside | union | 0/0 | 5.1/5.2 | 12 | 0.013 | e5303d0ec2cc0b2d | NO | 4.6, 12, yes |
| box_nested_inside | intersection | 0/0 | 4.8/5.1 | 12 | 0.013 | d8acd5fe89b4c1de | NO | 4.5, 12, yes |
| box_nested_inside | difference | 0/0 | 5.3/5.5 | 24 | 0.013 | 8e22b9160c7536d9 | NO | 4.8, 24, yes |
| box_nested_touching_face | union | 0/0 | 6.2/5.3 | 12 | 0.013 | 06101147d1215f33 | NO | 4.4, 12, yes |
| box_nested_touching_face | intersection | 0/0 | 6.0/5.5 | 12 | 0.019 | 8669fb490ed5e0d4 | NO | 4.4, 12, yes |
| box_nested_touching_face | difference | 0/0 | 5.9/6.2 | 28 | 0.012 | 868e94aa7744e2f9 | NO | 5.4, 28, yes |
| box_on_top_of_face | union | 0/0 | 5.8/5.9 | 28 | 0.013 | 10f38ff34ff437df | NO | 4.9, 28, yes |
| box_on_top_of_face | intersection | 0/0 | 5.2/4.8 | 0 | 0.012 | cce2e3a55c7e2217 | yes | 4.1, 0, yes |
| box_on_top_of_face | difference | 0/0 | 5.4/6.0 | 12 | 0.015 | f77b10e407821a5e | NO | 4.5, 12, yes |
| box_overlap_generic | union | 0/0 | 6.6/6.1 | 36 | 0.012 | 414c3d658c4c514c | NO | 5.2, 36, yes |
| box_overlap_generic | intersection | 0/0 | 5.9/5.9 | 12 | 0.013 | d36cd493afc9bb85 | NO | 5.0, 12, yes |
| box_overlap_generic | difference | 0/0 | 5.9/5.8 | 24 | 0.015 | 0b769ebe37efb383 | NO | 4.9, 24, yes |
| box_shift_2^-20 | union | 0/0 | 6.2/6.8 | 12 | 0.012 | 16b03ad2e053cff9 | NO | 5.3, 12, yes |
| box_shift_2^-20 | intersection | 0/0 | 8.4/7.1 | 12 | 0.012 | a54c394f981c56dd | NO | 6.0, 12, yes |
| box_shift_2^-20 | difference | 0/0 | 6.5/6.7 | 12 | 0.011 | 510af7ba34c3350a | NO | 5.3, 12, yes |
| box_shift_2^-40 | union | 0/0 | 7.1/6.7 | 12 | 0.015 | 192f63366ea77ae3 | NO | 5.3, 12, yes |
| box_shift_2^-40 | intersection | 0/0 | 6.7/7.1 | 12 | 0.015 | dd7507294bffe329 | NO | 5.6, 12, yes |
| box_shift_2^-40 | difference | 0/0 | 6.6/7.3 | 12 | 0.016 | 35a7b7f0c97851a1 | NO | 5.7, 12, yes |
| box_shift_2^-52 | union | 0/0 | 8.1/7.2 | 12 | 0.013 | e736322f2babec67 | NO | 5.8, 12, yes |
| box_shift_2^-52 | intersection | 0/0 | 7.2/6.7 | 12 | 0.015 | d49f8818d5922937 | NO | 5.5, 12, yes |
| box_shift_2^-52 | difference | 0/0 | 7.7/8.3 | 12 | 0.017 | 6f7a2511fcab6030 | NO | 5.7, 12, yes |
| box_sliver_crossing | union | 0/0 | 8.1/7.6 | 44 | 0.016 | 62cf4a7d0dfa818e | NO | 5.9, 44, yes |
| box_sliver_crossing | intersection | 0/0 | 6.5/6.5 | 12 | 0.013 | 49d7702d30acc6f2 | NO | 5.4, 12, yes |
| box_sliver_crossing | difference | 0/0 | 6.9/7.1 | 32 | 0.014 | c66e7cb1af1092ba | NO | 6.1, 32, yes |
| box_sliver_inside | union | 0/0 | 7.3/7.4 | 12 | 0.012 | e913bdbb45a43546 | NO | 6.0, 12, yes |
| box_sliver_inside | intersection | 0/0 | 7.3/8.3 | 12 | 0.018 | 1adfd7a2d1c9ec1d | NO | 5.8, 12, yes |
| box_sliver_inside | difference | 0/0 | 7.8/8.2 | 24 | 0.013 | e0a4881b1f067bc8 | NO | 6.0, 24, yes |
| box_vertex_touch | union | 0/0 | 5.1/5.4 | 12 | 0.012 | 633648634d76c75a | NO | 4.3, 12, yes |
| box_vertex_touch | intersection | 0/0 | 5.4/5.2 | 12 | 0.012 | 507afdf98de719a3 | NO | 4.6, 12, yes |
| box_vertex_touch | difference | 0/0 | 5.7/5.3 | 24 | 0.013 | f435bec32c0d6f4b | NO | 4.9, 24, yes |
| sphere_identical | union | 0/0 | 16.5/17.2 | 1280 | 0.048 | 082ed82ee4980277 | yes | 17.6, 1280, yes |
| sphere_identical | intersection | 0/0 | 17.2/17.4 | 1280 | 0.054 | 082ed82ee4980277 | yes | 18.4, 1280, yes |
| sphere_identical | difference | 0/0 | 7.8/8.3 | 0 | 0.039 | cce2e3a55c7e2217 | yes | 10.1, 0, yes |
| sphere_in_box_tangent | union | 0/0 | 9.3/9.0 | 12 | 0.030 | 487b4397b912702c | NO | 10.3, 12, yes |
| sphere_in_box_tangent | intersection | 0/0 | 18.0/18.0 | 1280 | 0.043 | 65c365b1b7e2260a | NO | 17.9, 1280, yes |
| sphere_in_box_tangent | difference | 0/0 | 8.8/8.2 | 0 | 0.027 | cce2e3a55c7e2217 | yes | 10.4, 0, yes |
| sphere_shift_2^-30 | union | 0/0 | 32.0/31.0 | 1500 | 0.060 | aa1b2a7bcb09e0f4 | NO | 44.1, 1500, yes |
| sphere_shift_2^-30 | intersection | 0/0 | 30.6/30.9 | 1372 | 0.053 | 4f9e13333121dc33 | NO | 87.4, 1372, yes |
| sphere_shift_2^-30 | difference | 0/0 | 30.5/30.3 | 1436 | 0.062 | 56752d34f63d0e17 | NO | 44.9, 1436, yes |
| sphere_vs_star | union | 0/0 | 35.2/37.8 | 1334 | 0.045 | 0d2d553000b0c797 | NO | 54.7, 1334, yes |
| sphere_vs_star | intersection | 0/0 | 42.4/39.6 | 1466 | 0.047 | da5d136a37cee4b4 | NO | 56.7, 1466, yes |
| sphere_vs_star | difference | 0/0 | 39.8/39.0 | 1190 | 0.046 | 4e201448d2d78067 | NO | 52.3, 1190, yes |
| torus_vs_sphere | union | 0/0 | 59.9/59.9 | 5028 | 0.107 | 4fb2c51a7267e556 | NO | 91.3, 5028, yes |
| torus_vs_sphere | intersection | 0/0 | 88.3/46.2 | 1212 | 0.083 | 87855fe13ae87849 | NO | 72.3, 1212, yes |
| torus_vs_sphere | difference | 0/0 | 59.4/58.3 | 4084 | 0.094 | f0e2de1520327d7b | NO | 86.0, 4084, yes |


### Informational comparison with the manifest's exact volumes (not scoring)
`volcheck.py` recomputes the exact volume (Fraction arithmetic on the float64 output) and compares it with the manifest's exact expectation for the 60 case-op pairs that have one. 51 of 60 match in both configurations. The 9 that do not (columns: case, op, expected, output exact volume):

default threads:
```
box_huge_offset_one_ulp	union	1.000244140625	0.000244140625	NO
box_huge_offset_one_ulp	intersection	0.999755859375	-0.000244140625	NO
box_huge_offset_one_ulp	difference	0.000244140625	1.000244140625	NO
box_huge_offset_overlap	union	1.875	0.875	NO
box_huge_offset_overlap	intersection	0.125	-0.875	NO
box_huge_offset_overlap	difference	0.875	1.875	NO
box_vertex_touch	union	2.0	1.0	NO
box_vertex_touch	intersection	0.0	1.0	NO
box_vertex_touch	difference	1.0	0.0	NO
```
threads=1:
```
box_huge_offset_one_ulp	union	1.000244140625	0.000244140625	NO
box_huge_offset_one_ulp	intersection	0.999755859375	-0.000244140625	NO
box_huge_offset_one_ulp	difference	0.000244140625	1.000244140625	NO
box_huge_offset_overlap	union	1.875	-0.875	NO
box_huge_offset_overlap	intersection	0.125	0.875	NO
box_huge_offset_overlap	difference	0.875	0.125	NO
box_vertex_touch	union	2.0	1.0	NO
box_vertex_touch	intersection	0.0	1.0	NO
box_vertex_touch	difference	1.0	0.0	NO
```
The other 33 case-op pairs have no manifest expectation (curved pairs and the malformed operands) and were not checked. A negative output volume means inverted orientation (or a wrong shape).

## 5. Operations available in the permissive build (headers at v1.10.1, doc comment first sentence)

Runtime check in `probe_output.txt`:
```
Delaunay::create(3,"tetgen") -> ok, dynamic type N3GEO25Delaunay_NearestNeighborsE
Delaunay::create(2,"triangle") -> ok, dynamic type N3GEO25Delaunay_NearestNeighborsE
Delaunay::create(3,"BDEL") -> ok, dynamic type N3GEO10Delaunay3dE
Delaunay::create(3,"PDEL") -> ok, dynamic type N3GEO18ParallelDelaunay3dE
Delaunay::create(2,"BDEL2d") -> ok, dynamic type N3GEO10Delaunay2dE
Optimizer::create("HLBFGS") -> null (unavailable)
AABB contains(0.5,0.5,0.5)=1 contains(2,2,2)=0 squared_distance(2,0.5,0.5)=1
compute_convex_hull_3d ok: facets=4 enclosed_volume=0.166667
mesh_tetrahedralize returned 0 cells=0
remesh_smooth ok: out facets=96
mesh_decimate_vertex_clustering ok: facets=12
```

| Area | Function / class (header) | First sentence of its doc comment | SFCGAL counterpart |
|---|---|---|---|
| mesh boolean | `mesh_union`, `mesh_intersection`, `mesh_difference`, `mesh_boolean_operation(.., "A+B"/"A*B"/"A-B"/"B-A")` (`mesh/mesh_surface_intersection.h`) | "Computes the union of two surface meshes." (likewise intersection, difference); details: "A and B need to be two closed surface mesh without intersections." | union3D, intersection3D, difference3D (solid operands only) |
| exact intersection engine | `MeshSurfaceIntersection` (same header): `intersect()`, `classify(expr)`, `remove_external_shell()`, `remove_internal_shells()` | "Computes surface intersections" (details: "New vertices are stored with exact coordinates"); `classify`: "Classifies the facets and keep only the ones on the boundary of a combination of regions defined by a boolean expression." | n-ary booleans: no direct counterpart (SFCGAL is binary) |
| coplanar handling | `MeshSurfaceIntersection::simplify_coplanar_facets()`; `set_delaunay()` | "Merge coplanar facets and retriangulate them using a Constrained Delaunay triangulation"; "If set, compute constrained Delaunay triangulation in the intersected triangles." | no counterpart as a public function (internal to SFCGAL's booleans) |
| self-intersection handling | `mesh_remove_intersections(M, max_iter, verbose)`; `mesh_facets_have_intersection(M, f1, f2)` | "Attempts to make a surface mesh conformal by removing intersecting facets and re-triangulating the holes."; "Tests whether two mesh facets have a non-degenerate intersection." | no counterpart (SFCGAL has `intersects`-style predicates on geometries, not a repair) |
| repair | `mesh_repair(M, mode, eps)`, `mesh_reorient`, `mesh_detect_colocated_vertices`, `mesh_detect_degenerate_facets`, `mesh_remove_bad_facets_no_check`, `fill_holes` (`mesh/mesh_repair.h`, `mesh_fill_holes.h`) | "Fixes some defaults in a mesh."; "Reorients the facets of a mesh coherently."; "Fills holes in mesh by generating new triangles" | no counterpart |
| expression/CSG | `CSGBuilder` (`mesh/mesh_CSG_builder.h`): `union_instr`, `intersection`, `difference`, `hull`, `minkowski`, `linear_extrude`, `rotate_extrude`, `projection`; `CSGCompiler` reads OpenSCAD `.csg` | "Computes the union of two or more meshes."; "Computes the convex hull of several meshes."; "Computes the Minkowski sum of meshes."; "Computes a 3D extrusion from a 2D shape"; `CSGCompiler`: "Creates meshes from OpenSCAD .csg files." | union3D/intersection3D/difference3D (n-ary), convexHull3D, minkowskiSum3D, extrude; projection/hull in 2D: no direct match |
| AABB | `MeshFacetsAABB`, `MeshCellsAABB`, `MeshFacetsAABB2d` (`mesh/mesh_AABB.h`) | "Axis Aligned Bounding Box tree of mesh facets in 3D."; its details: "Used to quickly compute facet intersection and to locate the nearest facet from 3d query points." | no counterpart (SFCGAL's AABB is CGAL's) |
| point-in-mesh | `MeshFacetsAABB::contains(p)` | "Tests whether a closed surface contains a point" (precondition: surface closed) | closest to SFCGAL `covers`/`intersects` of a point with a solid; no exact-predicate guarantee stated |
| distance | `MeshFacetsAABB::squared_distance(p)`, `nearest_facet`; `mesh_one_sided_Hausdorff_distance`, `mesh_symmetric_Hausdorff_distance` (`mesh/mesh_distance.h`) | "Computes the distance between an arbitrary 3d query point and the surface."; "Computes an approximation of the single sided Hausdorff distance dH(m1->m2) between two surfacic meshes." | distance3D: point-to-surface only; no exact geometry-to-geometry distance (the Hausdorff functions are sampled approximations) |
| signed distance | none found (`grep -i signed.?dist` over `mesh/*.h` is empty); `contains()` plus unsigned distance would give the sign | n/a | no counterpart |
| measures | `Geom::mesh_enclosed_volume`, `Geom::mesh_area` (`mesh/mesh_geometry.h`) | "Computes the volume enclosed by a surfacic mesh."; "Computes the total surface area of a mesh in arbitrary dimension." | volume, area, but in plain double, not exact; `mesh_enclosed_volume` returns `fabs` |
| convex hull | `compute_convex_hull_3d(Mesh&)`, `compute_convex_hull_2d` (`mesh/mesh_convex_hull.h`) | "Computes the 3d convex hull of a set of points" | convexHull3D (works on the mesh's vertices); probe: 4 facets, volume 1/6 for a tetrahedron of 5 points |
| Minkowski | `compute_minkowski_sum_3d`, `compute_minkowski_sum_2d` (`mesh/mesh_minkowski.h`) | "Computes the Minkowski sum of two 3D meshes" | minkowskiSum3D (not exercised here) |
| extrusion | `CSGBuilder::linear_extrude`, `rotate_extrude` | "Computes a 3D extrusion from a 2D shape" | extrude (from a 2D mesh; not exercised here) |
| triangulation (2D) | `CDT2d`, `ExactCDT2d` (`delaunay/CDT_2d.h`); `Delaunay2d` ("BDEL2d") | "Constrained Delaunay triangulation"; "Constrained Delaunay Triangulation with vertices that are exact points."; "Implementation of Delaunay in 2d." | tessellate/triangulate: partial (a 2D constrained triangulation, not SFCGAL's polygon-with-holes API); `MeshFacets::triangulate()` "Triangulates the facets"; `tessellate_facets` "Subdivides the facets with more than nb_vertices." |
| Delaunay 3D | `Delaunay3d` ("BDEL"), `ParallelDelaunay3d` ("PDEL"), `RegularWeightedDelaunay3d` (`delaunay/delaunay_3d.h`, `parallel_delaunay_3d.h`) | "Implementation of Delaunay in 3d."; "Multithreaded implementation of Delaunay in 3d." | no counterpart |
| Delaunay needing restricted code | `DelaunayTetgen` ("tetgen"), `DelaunayTriangle` ("triangle"), `mesh_tetrahedralize` (`delaunay/delaunay_tetgen.h`, `delaunay_triangle.h`, `mesh/mesh_tetrahedralize.h`) | "Implementation of Delaunay using Hang Si's tetgen library."; "Implementation of Delaunay in 2D using the triangle library by Jonathan Shewchuk."; "Fills a closed surface mesh with tetrahedra." | **unavailable in this build** (TetGen/Triangle OFF); `mesh_tetrahedralize` returned false; see anomaly 6 |
| remeshing | `remesh_smooth(M_in, M_out, nb_points, ...)`, `CentroidalVoronoiTesselation` (`mesh/mesh_remesh.h`, `voronoi/CVT.h`) | "Remeshes a 'smooth' shape (that is, without management of sharp features)."; "CentroidalVoronoiTesselation is the main component of the remeshing algorithm." | no counterpart. Needs HLBFGS for its Newton stage: with HLBFGS OFF `Newton_iterations` logs "This geogram was not compiled with HLBFGS (falling back to Lloyd iterations)" (source quote, `voronoi/CVT.cpp`); `remesh_smooth` on the probe cube ran and returned 96 facets |
| decimation | `mesh_decimate_vertex_clustering` (`mesh/mesh_decimate.h`); `remove_small_facets`, `remove_degree2/3_vertices` | "Generates a simplified representation of a mesh." (vertex clustering only; no edge-collapse/QEM decimation found in `mesh_decimate.h`) | no counterpart |
| other | `mesh_smooth`, `simple_Laplacian_smooth`, `orient_normals` ("Orients the normals in such a way that each connected component has a positive signed volume."), subdivision, parameterization, Poisson reconstruction (PoissonRecon, BSD-3), Co3Ne point-cloud reconstruction | | no counterpart |

SFCGAL functions with no Geogram counterpart found in these headers: straight skeleton, approximate medial axis, alpha shapes, visibility, offset, buffer, WKT/WKB geometry types, 2D polygon booleans on polygons-with-holes (only via the CSG 2D path), exact geometry-to-geometry distance, signed distance.

## 6. Anomalies and surprises (raw facts, no judgement)

1. **No input refusal.** All 21 runs on the 7 malformed classes (open box, non-manifold edge, self-intersecting, unwelded vertices, zero-area triangle, inverted, duplicate triangle) exited 0 and produced a mesh; Geogram threw nothing and the runner added no validation. Example outputs: `bad_open_box difference` returned 0 triangles; `bad_open_box intersection` returned 33 triangles in the first sweep, 22 in the final sweep run1 and a different count again in run2 (see 3).
2. **Wrong volumes on 9 pairs with a manifest expectation** (listed in section 4). `box_vertex_touch` (two boxes touching at one vertex): union volume 1 instead of 2, intersection 1 instead of 0, difference 0 instead of 1; the same results in both thread configurations, so the engine produced a result of one box only. `box_huge_offset_overlap` and `box_huge_offset_one_ulp` (coordinates near 2^40, operands A=[2^40,2^40+1]^3 and B=[2^40+0.5,2^40+1.5]^3 for the overlap case): the 24-triangle union output has 14 vertices spanning only 2^40 to 2^40+1 (default threads, run1 and run2) or only 2^40+0.5 to 2^40+1.5 (threads=1), i.e. neither is the 20-vertex union; exact output volumes 0.875 / -0.875 against expected 1.875 / 0.125 (default) and -0.875 / 0.875 (threads=1). Geogram scales coordinates internally in the prologue (`intersect_prologue` documents "optionally scales the coordinates"); I did not investigate the cause.
3. **Non-deterministic output order with the default thread count.** 80 of 93 outputs differ byte-wise between two identical invocations. In 88 of 93 the triangle set (by vertex coordinates, cyclic order normalised) is identical, so it is ordering of vertices/triangles; in 5 pairs (`bad_open_box intersection`, `sphere_shift_2^-30` union/difference, `torus_vs_sphere` union/difference) even the triangle set differs run to run (for `bad_open_box intersection`: volume 0.9375 vs 0.91666666666666663 in run1 vs run2; for the others the volume and area printed by `measure` were equal, so a different triangulation of the same surface or different rounding of intersection vertices; not investigated). With `GEOGRAM_RUNNER_THREADS=1` all 93 are byte-identical between two runs, but 8 of 93 triangle sets differ from the default-thread run and only 11 hashes match, so the single-thread result is not the same as the multi-thread result. Cross-platform determinism (x86_64 vs aarch64) was not measured here.
4. **Output precision of measures.** `mesh_enclosed_volume` takes `fabs` and sums in plain double (unit cube: 0.99999999999999989, area 5.9999999999999991).
5. **Orientation.** For the 51 case-op pairs whose exact output volume matched the manifest, the sign was positive (CCW outward). For the 9 mismatches, signs are in the table above (negative for intersection of the two huge-offset pairs). The 33 pairs without expectation were not checked for orientation. (An early quick orientation check using `math.fsum` of per-triangle double terms at 2^40 coordinates reported negative volumes for `box_huge_offset_one_ulp`; that check is unreliable at that magnitude and the Fraction-based `volcheck.py` is the one to use.)
6. **Silent fallback in the Delaunay factory.** With TetGen and Triangle OFF, `Delaunay::create(3,"tetgen")` and `Delaunay::create(2,"triangle")` return a valid object of dynamic type `GEO::Delaunay_NearestNeighbors` (a nearest-neighbour structure, not a triangulation) instead of null or an error. Anyone calling them gets no diagnostic. `mesh_tetrahedralize` returns false without throwing.
7. **Build surprises.** The platform default on macOS builds a dylib (see section 1). `GEO::initialize` plus `CmdLine::get_arg` asserts if `CmdLine::import_arg_group("algo")` was not called before `Delaunay::create` (the probe first aborted with `Assertion failed: variable_exists` and appeared to hang in the assertion stack-trace printer; fixed in the probe; the boolean path does not need it).
8. One-run speed: 4 to 8 ms for the box cases (mostly process start); the largest outputs (5,028 triangles) took 59 ms in-process end to end. This is not a benchmark.

## 7. Not done

- No Thingi10K or large inputs, no many-small or few-large workloads, no speed measurement beyond the wall time above (no repeats for timing, no warm/cold study).
- No x86_64 or Linux build, so no cross-platform determinism; no 4/8-thread sweep, no `-ffp-contract` variants beyond Geogram's own flags.
- The wiki page BooleanOps was not read; the API fit was derived from the headers and the shipped example at the tag.
- The exit-3 REFUSED path was never triggered, so its message format and behaviour on a real Geogram exception is untested. Crash/signal behaviour was not tested.
- Orientation was checked only where the manifest gives an expectation; no check of manifoldness, watertightness or self-intersection of the outputs; no components/Euler/point-in-solid comparisons (that is the later scorer's job).
- The causes of anomalies 2 and 3 were not investigated; no Geogram source was modified; no GPU, CI or lock wrapper was used; nothing was added to git.
- SFCGAL counterparts for minkowski and extrude were read from headers but not run; AABB `contains`/`squared_distance` and `compute_convex_hull_3d` were run only on a unit cube / a 5-point set.

## Files in this directory
`runner.cpp`, `build.sh`, `run_corpus.py`, `volcheck.py`, `probe.cpp`, `check_license_proof.sh`; outputs: `results_default_threads.tsv`, `results_threads1.tsv`, `volcheck_default_threads.txt`, `volcheck_threads1.txt`, `license_proof_output.txt`, `probe_output.txt`, `compiled_sources.txt`. Large artifacts stay in `storage/mesh/geogram/` (`build/`, `ninja_build.log`, `nm_*.txt`, `runs/`, `runs_t1/` with every output mesh and stderr of failed runs).
