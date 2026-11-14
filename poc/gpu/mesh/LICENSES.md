# MP06 license audit

Audited 2026-11-13. For each library the license was read from the project's own license file (or, where there is none, the file header) at the pinned tag, via raw GitHub URLs
(or `gh api`), not from agent summaries or README badges alone. "Linkable / separate process" notes are **observations for legal review, not legal conclusions.**
The repo context: the MongoDB server is SSPL; `README.third_party.md` already lists vendored Apache-2.0, BSD-3-Clause and BSL-1.0 components (for example S2 Apache-2.0 and Boost 1.88.0 BSL-1.0).

## Summary

| Library | Pinned tag | Commit | SPDX (from its own file) | Observation |
|---|---|---|---|---|
| Manifold | v3.5.5 | 32271a1a999ce721ffaf5ab10e9746ceeb696547 | Apache-2.0 | Permissive; optional TBB and Clipper2 deps are Apache-2.0 / BSL-1.0 |
| Geogram | v1.10.1 | bbcacba146b357926b5524111f56cc70c42182a7 | BSD-3-Clause (core) | **Bundled TetGen (AGPL-3.0 or commercial), Triangle (no commercial distribution without author arrangement) and HLBFGS (non-commercial) are on by default; usable only with those three options off** |
| Embree | v4.4.1 | f590db83ef6559387df7f6d8725c34fb7acf851d | Apache-2.0 | Permissive; default tasking backend TBB (Apache-2.0) |
| Clipper2 | Clipper2_2.0.1 | 21ebba05db8894f0c7217ad35ea518080f324946 | BSL-1.0 | Permissive; header-only usable |
| Boost.Geometry | boost-1.92.0 | 90c27ef0bf9d1700f553b85f890f43461eced529 | BSL-1.0 | Permissive; not in-tree (in-tree Boost 1.88.0 has no `libs/geometry`) |
| earcut.hpp | v3.2.4 | 92acda9e2ed96ff1f910cda35de444727e9da7b0 | ISC | Permissive; single header |
| CDT | 2.0.2 | b4413ee44affb489099ed4d6ec65e94788cb6708 | MPL-2.0 (bundled predicates.h BSD-3-Clause) | **Weak (file-level) copyleft, not permissive**; flag for legal review |
| oneTBB | v2022.3.0 (Manifold's pin) | f1862f38f83568d96e814e469ab61f88336cc595 | Apache-2.0 | Its third-party file contains one entry that mentions GPL terms; flag |
| nanobind | v2.12.0 (Manifold's pin) | 2a61ad2494d09fecb2e13322c1383342c299900d | BSD-3-Clause | **Not required** by Manifold (only `MANIFOLD_PYBIND`, OFF by default) |
| S2 (in tree) | google/s2geometry a25c502 per SBOM | a25c502bda9d7e0274b9e2b7825fbddf13cc0306 | Apache-2.0 | Already shipped; **spherical, not planar** |
| SFCGAL | v2.3.0 | 137203dae01faab4a1287934f9457e925e3b4383 | LGPL-2.0-or-later (own files), combined with CGAL GPL-3.0-or-later | **Reference only, not used** |
| CGAL | v6.2.1 | 28811b671a12b5caa9e3688569dadbc6b3728fe6 | GPL-3.0-or-later or LGPL-3.0-or-later per file | **Reference only, not used** |

**Findings that look like blockers:** (1) Geogram: three restricted components compiled by default (details below). For legal review: reading Geogram as BSD-3 at all depends on a build with `GEOGRAM_WITH_TETGEN=OFF`,
`GEOGRAM_WITH_TRIANGLE=OFF`, `GEOGRAM_WITH_HLBFGS=OFF`, and the three source directories excluded from anything vendored. (2) CDT is MPL-2.0, not permissive; the parity matrix does not rely on it.
Neither blocks Manifold, Embree, Clipper2, Boost.Geometry or earcut.

---

## Manifold (elalish/manifold)

- Tag v3.5.5, commit `32271a1a999ce721ffaf5ab10e9746ceeb696547` (committer date 2026-11-13T02:01:03Z, the day of this audit; the previous tag is v3.5.4, `ce50d78021d64507f89e8c9fc2c2e51018117857`).
- License file: https://raw.githubusercontent.com/elalish/manifold/v3.5.5/LICENSE. First lines: "Apache License / Version 2.0, January 2004". SPDX: **Apache-2.0**.
  Source headers agree (`src/quickhull.cpp`: "Licensed under the Apache License, Version 2.0 (the "License")").
- Bundled or derived third-party code (found by reading the headers of the files in `src/`; there is no `third_party/` directory and no NOTICE file in the tree listing):
  - `src/quickhull.cpp`, `src/quickhull.h`: "Derived from the public domain work of Antti Kuukka at https://github.com/akuukka/quickhull".
  - `src/tri_dist.h`: "From NVIDIA-Omniverse PhysX - BSD 3-Clause "New" or "Revised" License".
  - `src/svd.h`: "MIT License, Copyright (c) 2019 wi-re ... Modified from https://github.com/wi-re/tbtSVD".
- Build/runtime dependencies (from `cmake/manifoldDeps.cmake` and `CMakeLists.txt` at the tag):
  - Clipper2: needed when `MANIFOLD_CROSS_SECTION` is ON (default ON). Fetched at commit `46f639177fe418f9689e8ddb74f08a870c71f5b4` (committer date 2026-03-05; its `LICENSE` first line "Boost Software License - Version 1.0 - August 17th, 2003") plus a local patch `cmake/patches/0001-clipper2-no-iostream.patch`. BSL-1.0.
  - oneTBB: only when `MANIFOLD_PAR` is ON (default OFF); fetched v2022.3.0. Apache-2.0.
  - nanobind v2.12.0: only when `MANIFOLD_PYBIND` is ON (default OFF). **Not required.** BSD-3-Clause (see below).
  - Test/optional only: googletest v1.17.0, fuzztest (commit f1e26613...), tracy v0.11.1, assimp (extras). Licenses of these were not read (test tooling, not shipped).
- Linkable observation: Apache-2.0 library with BSL-1.0 and optionally Apache-2.0 dependencies and small BSD/MIT/public-domain derived files; the same license families already appear in `README.third_party.md`. C++17 library (CMake), builds a shared or static library. For legal review.

## Geogram (BrunoLevy/geogram), including bundled third-party code

- Tag v1.10.1, commit `bbcacba146b357926b5524111f56cc70c42182a7` (2026-09-01).
- License file: https://raw.githubusercontent.com/BrunoLevy/geogram/v1.10.1/LICENSE. First lines: "BSD 3-Clause License / Copyright (c) 2000-2022 Inria". SPDX: **BSD-3-Clause** for Geogram's own code (`src/lib/geogram/**`, headers carry the same Inria text).
- CMake options (root `CMakeLists.txt` at the tag): `GEOGRAM_WITH_HLBFGS ON`, `GEOGRAM_WITH_TETGEN ON`, `GEOGRAM_WITH_TRIANGLE ON`, `GEOGRAM_WITH_LUA ON`, `GEOGRAM_WITH_LEGACY_NUMERICS ON`, `GEOGRAM_WITH_GRAPHICS ON`, `GEOGRAM_WITH_TBB OFF`. `src/lib/geogram/third_party/CMakeLists.txt` compiles tetgen, triangle and HLBFGS only when their option is ON, and always compiles libMeshb, rply, OpenNL, zlib, PoissonRecon, xatlas (and includes amgcl).

### Restricted bundled components (each is ON by default)

| Component | Path | What its own file says (verbatim quotes) | Raw URL (at v1.10.1) |
|---|---|---|---|
| **TetGen** | `src/lib/geogram/third_party/tetgen/` | README.txt: "license: GNU Affero General Public License: http://www.gnu.org/licenses/agpl-3.0.en.html // Free for academic use, contact copyright owners at tetgen@wias-berlin.de for other uses". A second copy `tetgen/Tetgen1.6/` (README: "WIP: including tetgen1.6 ... Seems that everything does not work") has "IMPORTANCE NOTICE ... READ the GENERAL LICENSE TERMS AND CONDITIONS" (terms text not read). | https://raw.githubusercontent.com/BrunoLevy/geogram/v1.10.1/src/lib/geogram/third_party/tetgen/README.txt |
| **Triangle** | `src/lib/geogram/third_party/triangle/` | README: "These programs may be freely redistributed under the condition that the copyright notices ... are not removed, and no compensation is received. Private, research, and institutional use is free. ... Distribution of this code as part of a commercial system is permissible ONLY BY DIRECT ARRANGEMENT WITH THE AUTHOR." | https://raw.githubusercontent.com/BrunoLevy/geogram/v1.10.1/src/lib/geogram/third_party/triangle/README |
| **HLBFGS** | `src/lib/geogram/third_party/HLBFGS/` | README.txt: "HLBFGS is freely available for non-commercial purposes." | https://raw.githubusercontent.com/BrunoLevy/geogram/v1.10.1/src/lib/geogram/third_party/HLBFGS/README.txt |

Consequences in the permissive build (all three OFF): `mesh_tetrahedralize()` is unavailable (its header: "needs a constrained Delaunay algorithm ... compiled with mg-tetra or tetgen"); `delaunay_triangle.h` wraps Triangle and is unavailable;
`CentroidalVoronoiTesselation::Newton_iterations` creates an `Optimizer` named "HLBFGS" and, if it is absent, warns and falls back to Lloyd iterations (`voronoi/CVT.cpp`), so `remesh_smooth` loses its Newton stage.
Geogram's own 3D Delaunay, `CDT_2d`, exact booleans and AABB trees are BSD-3-Clause and do not need these.
Observation for legal review: Geogram is plausible only with these three options off and the three directories excluded from any vendored copy.

### Other bundled code (read)

| Component | Path | License evidence read | SPDX |
|---|---|---|---|
| PoissonRecon | `third_party/PoissonRecon/` | `LICENSE.txt`: "Screened Poisson Surface Reconstruction ... Copyright (c) 2006, Michael Kazhdan and Matthew Bolitho ... Neither the name of the Johns Hopkins University ..." | BSD-3-Clause |
| zlib | `third_party/zlib/` | `LICENSE`: "(C) 1995-2022 Jean-loup Gailly and Mark Adler ... provided 'as-is'" | Zlib |
| Lua 5.3.3 | `third_party/lua/` (option `GEOGRAM_WITH_LUA`) | `lua.h` license block: "Copyright (C) 1994-2016 Lua.org, PUC-Rio. Permission is hereby granted, free of charge ..." | MIT |
| xatlas | `third_party/xatlas/` | `xatlas.h` header: "MIT License / Copyright (c) 2018-2019 Jonathan Young" | MIT |
| stb_image | `third_party/stb_image/` | header trailer: "This software is available under 2 licenses -- choose whichever you prefer. ALTERNATIVE A - MIT License" (B is public domain) | MIT OR Unlicense-style public domain |
| amgcl (submodule, commit 93827a00fc926d951c75f08fdb1d491912ff7065) | `third_party/amgcl/` | `LICENSE.md`: "The MIT License ... Copyright (c) 2012-2022 Denis Demidov" | MIT |
| libMeshb (submodule, 88095d5ed04bfaf6bee27b777e7190bebdc72230) | `third_party/libMeshb/` | `LICENSE.txt`: "MIT License / Copyright (c) 2001-2024 Loïc Maréchal / INRIA" | MIT |
| rply (submodule, 4296cc91b5c8c26d4e7d7aac0cee2b194ffc5800) | `third_party/rply/` | `LICENSE`: "MIT license / Copyright 2003-2015 Diego Nehab" | MIT |
| OpenNL (submodule, ae782db2db40cc40fcd7659629fc652e5eab1f9e) | `third_party/OpenNL/` | no LICENSE file in that repo at the pin; `nl.h` header: "Copyright (c) 2000-2022 Inria ... Neither the name of the ALICE Project-Team ..." | BSD-3-Clause (from header) |
| Legacy numerics: ARPACK, ARPACK_UTIL, CBLAS, CLAPACK, SUPERLU, LIBF2C | `src/lib/third_party/numerics/` (option `GEOGRAM_WITH_LEGACY_NUMERICS`, ON) | ARPACK `LICENSE`: "BSD Software License ... Copyright (c) 1996-2008 Rice University". SUPERLU `License.txt`: "Copyright (c) 2003, The Regents of the University of California, through Lawrence Berkeley National Laboratory" (BSD-style; remaining text not read). CBLAS/CLAPACK/INCLUDE `COPYING` files contain only the placeholder "Copyright (c) 1992-2008 The University of Tennessee. All rights reserved. $COPYRIGHT$" (no usable license text). LIBF2C not read. | **partly unverified**; whether these link into libgeogram by default was not established |
| stb (`stb_c_lexer.h`), glad, imgui and other `geogram_gfx` code | `third_party/stb/`, `src/lib/geogram_gfx/third_party/` (option `GEOGRAM_WITH_GRAPHICS`, ON) | not read | **unverified** (graphics is off for a server build) |

Required dependencies: none beyond a C++17 compiler and CMake; TBB optional (`GEOGRAM_WITH_TBB` OFF). The CMake file references a `shewchuk` directory under `third_party/` that is not present in the tree at this tag.

## Embree (embree/embree)

- Tag v4.4.1, commit `f590db83ef6559387df7f6d8725c34fb7acf851d` (2026-03-17).
- License file: https://raw.githubusercontent.com/embree/embree/v4.4.1/LICENSE.txt. First lines: "Apache License / Version 2.0, January 2004". README: "released as open source under the Apache 2.0 license"; source headers carry `SPDX-License-Identifier: Apache-2.0` (for example `include/embree4/rtcore_scene.h`). SPDX: **Apache-2.0**.
- Bundled: `common/simd/arm/sse2neon.h` (header text: "sse2neon is freely redistributable under the MIT License", MIT); `common/simd/arm/avx2neon.h` (license text not seen in its first 12 lines: **unverified**). `third-party-programs.txt` lists, under "1. Apache License 2.0": oneTBB and Open Image Denoise (OIDN). `third-party-programs-DPCPP.txt` and `-oneAPI-DPCPP.txt` (SYCL builds) were not read.
- Dependencies: TBB is the default tasking system (`EMBREE_TASKING_SYSTEM "TBB"`; `INTERNAL` is also selectable on Linux and macOS, `PPL` on Windows). ISPC support is `EMBREE_ISPC_SUPPORT OFF` by default. Tutorials (not needed) bundle imgui (`tutorials/common/imgui/LICENSE.txt`, not read).
- Linkable observation: Apache-2.0 C/C++ library, shared by default. Note Embree rays and queries are float32, relevant to the exactness contract, not to licensing.

## Clipper2 (AngusJohnson/Clipper2)

- Tag Clipper2_2.0.1, commit `21ebba05db8894f0c7217ad35ea518080f324946` (2025-12-19).
- License file: https://raw.githubusercontent.com/AngusJohnson/Clipper2/Clipper2_2.0.1/LICENSE. First line: "Boost Software License - Version 1.0 - August 17th, 2003". Source headers: "License : https://www.boost.org/LICENSE_1_0.txt". SPDX: **BSL-1.0**.
- Bundled third-party: none in `CPP/Clipper2Lib`; `.gitmodules` exists (not expanded; used for tests only). No NOTICE.
- Dependencies: none beyond the C++ standard library for the C++ library. `clipper.triangulation.h` is marked BETA in its header.
- Observation: header-implementable, BSL-1.0 (already the license of the Boost in tree).

## Boost.Geometry (boostorg/geometry)

- Tag boost-1.92.0, commit `90c27ef0bf9d1700f553b85f890f43461eced529` (2026-07-02).
- License file: https://raw.githubusercontent.com/boostorg/geometry/boost-1.92.0/LICENSE_1_0.txt. First line: "Boost Software License - Version 1.0 - August 17th, 2003". SPDX: **BSL-1.0**.
- Bundled third-party: none in the tree listing (headers under `include/`; `index/` is the spatial index). Depends on other Boost libraries only (header-only), all BSL-1.0.
- In-tree note: the repository vendors Boost 1.88.0 (`README.third_party.md`, `src/third_party/boost/boost/version.hpp`: `BOOST_VERSION 108800`) with 83 libs and **no `geometry`** (no `polygon` either). Using Boost.Geometry means vendoring a new Boost library and its closure, ideally at 1.88.0 rather than 1.92.0 (not checked whether the 1.88 sources differ for the algorithms audited).

## earcut.hpp (mapbox/earcut.hpp)

- Tag v3.2.4, commit `92acda9e2ed96ff1f910cda35de444727e9da7b0` (2026-09-28).
- License file: https://raw.githubusercontent.com/mapbox/earcut.hpp/v3.2.4/LICENSE. First lines: "ISC License / Copyright (c) 2015, Mapbox". SPDX: **ISC**.
- Bundled: `test/comparison/earcut.hpp` and `libtess2.hpp` are test-only comparison code (licenses not read). Runtime dependency: C++ standard library only.
- Observation: single header, permissive.

## CDT (artem-ogre/CDT)

- Tag 2.0.2, commit `b4413ee44affb489099ed4d6ec65e94788cb6708` (2026-11-04).
- License file: https://raw.githubusercontent.com/artem-ogre/CDT/2.0.2/LICENSE. First lines: "Mozilla Public License Version 2.0 / ==========". Source headers: "This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0." SPDX: **MPL-2.0**.
- Bundled: `CDT/include/predicates.h`, "Copyright (c) 2019, William C. Lenthe ... Redistribution and use in source and binary forms ... Neither the name of the copyright holder ..." (**BSD-3-Clause**; it implements Shewchuk-style robust predicates, referenced at https://www.cs.cmu.edu/~quake/robust.html). Test dependency Catch2 via `.gitmodules` (not read).
- Observation for legal review: MPL-2.0 is file-level copyleft (not permissive in the sense of the other libraries here). Permissive alternatives for the same function exist in the audit: Geogram `CDT_2d` (BSD-3-Clause) and Clipper2 `Triangulate` (BETA, rejects intersecting paths).

## oneTBB (oneapi-src/oneTBB) and nanobind (wjakob/nanobind)

- oneTBB: Manifold's pin v2022.3.0, commit `f1862f38f83568d96e814e469ab61f88336cc595` (the newest tag at the audit, v2023.2.0 `41158a44ab3fb7ac5df7f99ef82626c87607447f`, was not read). License file: https://raw.githubusercontent.com/oneapi-src/oneTBB/v2022.3.0/LICENSE.txt, first lines "Apache License / Version 2.0, January 2004". SPDX **Apache-2.0**.
  `third-party-programs.txt` (https://raw.githubusercontent.com/oneapi-src/oneTBB/v2022.3.0/third-party-programs.txt) lists: ITT Notify API (Intel, BSD-style conditions), hwloc (Indiana University etc., BSD-style), gperftools (BSD-3-Clause, Google), a libstdc++ bug-62258 workaround by Mateusz Kwiatkowski, ActiveState thread pool recipe (BSD-3-Clause), doctest (MIT).
  **Item 4 (Kwiatkowski workaround) says in its own text: "I hereby renounce all copyright ... It may be treated as public domain. However, as this file interfaces with GCC internal ABI, it may be subject to the terms and conditions of the GNU General Public License ... the GCC Runtime Library Exception ..."**: flag for legal review. Required runtime dependency of Embree by default; optional for Manifold.
- nanobind (only if Manifold's Python bindings are built, which this workstream does not do): v2.12.0, commit `2a61ad2494d09fecb2e13322c1383342c299900d`. https://raw.githubusercontent.com/wjakob/nanobind/v2.12.0/LICENSE: "Copyright (c) 2022 Wenzel Jakob ... All rights reserved. Redistribution and use in source and binary forms ... Neither the name of ..." (BSD-3-Clause). Bundles `ext/robin_map` (license not read). Not required.

## S2 geometry, already vendored at `src/third_party/s2`

- License file in tree: `src/third_party/s2/LICENSE-2.0.txt` ("Apache License / Version 2.0, January 2004"); `README.third_party.md` and `sbom.json` list "S2 Geometry Library", Apache-2.0, version a25c502bda9d7e0274b9e2b7825fbddf13cc0306, "Copyright 2005 Google Inc. All Rights Reserved." (source headers say the same). SPDX **Apache-2.0**.
- Version/provenance: the SBOM pin is google/s2geometry commit a25c502 (committer date 2015-04-10); `git log` shows the files were first added to this repo on 2012-10-24 (commit 325b42be26c "SERVER-2874 Import S2 ...") and last touched by 4994262c55b (SERVER-132874). There is no VERSION file or import script under `src/third_party/s2` (`src/third_party/scripts` has scripts for icu, pcre2, tomcrypt, yaml-cpp, zlib, zstandard, json-schema-test-suite, not s2).
- Functional observation: the old S2 generation (`S2Polygon`, `S2Loop`, `S2PolygonBuilder`) works on the unit sphere. `s2polygon.h` has `InitToUnion/Intersection/Difference` (+ `*Sloppy` snapping variants), `InitToSimplified`, `Contains/Intersects`, `GetArea`, `GetCentroid`. It is a geographic, 2D, spherical library: it does not provide planar Cartesian operations and nothing 3D.

## Reference only, not used

- **SFCGAL v2.3.0**, commit `137203dae01faab4a1287934f9457e925e3b4383`. `LICENSE` first line: "GNU LIBRARY GENERAL PUBLIC LICENSE Version 2, June 1991" (https://gitlab.com/sfcgal/SFCGAL/-/raw/v2.3.0/LICENSE); source headers: `SPDX-License-Identifier: LGPL-2.0-or-later`; `README.md`: "SFCGAL is provided under the following licence LGPL version 2 or later. ... SFCGAL uses CGAL modules which are licenced as GPLv3+. Whenever you compile and distribute SFCGAL with the GPL-licenced CGAL, the full packaged result is automatically considered as GPL version 3 or later".
  Used here only as the function inventory; no code read for reuse, none built.
- **CGAL v6.2.1**, commit `28811b671a12b5caa9e3688569dadbc6b3728fe6`. `Installation/LICENSE` (https://raw.githubusercontent.com/CGAL/cgal/v6.2.1/Installation/LICENSE): "each of which is licensed under an open source license. It is also possible to obtain commercial licenses from GeometryFactory ... This is either the GNU General Public License or the GNU Lesser General Public License (as published by the Free Software Foundation; either version 3 of the License or (at your option) any later version)", chosen per file; some Boost-derived files are BSL-1.0; CORE and ImageIO are LGPL; an ETH Zurich random forest is MIT-like. Not used; the workstream's out-of-scope list excludes CGAL and SFCGAL code.

## License checks for "beyond SFCGAL" candidates (license only; API unverified)

| Candidate | Use | Tag / commit | License file read | SPDX |
|---|---|---|---|---|
| meshoptimizer | mesh decimation | v1.3, 9e1f07b159d3cb777f1c67ed31fc11fd117986f4 | https://raw.githubusercontent.com/zeux/meshoptimizer/v1.3/LICENSE.md ("MIT License, Copyright (c) 2016-2026 Arseny Kapoulkine") | MIT |
| V-HACD | convex decomposition | v4.1.0, 22ec20a7f8ea221ab600df869369b9c6a258cb10 | https://raw.githubusercontent.com/kmammou/v-hacd/v4.1.0/LICENSE ("BSD 3-Clause License, Copyright (c) 2011, Khaled Mamou") | BSD-3-Clause |
| CoACD | convex decomposition | 1.0.14, 1401ce2a7ae1ed89c65ab958b48d489350c233c7 | https://raw.githubusercontent.com/SarahWeiii/CoACD/1.0.14/LICENSE ("MIT License, Copyright (c) 2022 Xinyue Wei, Minghua Liu"); its own dependencies not audited | MIT |
| OpenVDB | voxelization | v14.0.0, ecae2bda85744412b0cd47e1b8e1e4e76beb7b88 | https://raw.githubusercontent.com/AcademySoftwareFoundation/openvdb/v14.0.0/LICENSE (Apache License text); dependencies (Boost, TBB, optional others) not audited | Apache-2.0 |
| libigl (fast winding number) | winding number | v2.6.4, 7100764c2a2833284a8bdfa9b948d2b3bf124624 | files `LICENSE.MPL2` ("Mozilla Public License Version 2.0") and `LICENSE.GPL` ("GNU GENERAL PUBLIC LICENSE Version 3") at https://raw.githubusercontent.com/libigl/libigl/v2.6.4/ | MPL-2.0 or GPL-3.0 (**not permissive**; not recommended) |

## MP17 plane-based boolean sources (audited 2026-11-14 by a sub-agent; `lib/planebased/SURVEY.md`)
| Item | License as found | Use |
|---|---|---|
| Campen and Kobbelt 2010; Nehring-Wirxel et al. 2021; EMBER 2022 (papers) | papers; no source release stated for the first two | read for the mathematics only; implementation is clean-room |
| EMBER binary (project page) | "binary implementation, contact the authors"; no source, no license | not downloaded, not used |
| zalo/ember-csg (third-party port, d7f9674f) | MIT; but its geometry dependency has no license and its classification is not exact | inspected for its license only; not used |
| juliusnehring/mesh-kernel (9f6a63ec, `integer-plane-geometry`) | **no LICENSE file** | inspected for its license only; no code read or copied; not used |

## Cherchi et al. 2022 mesh booleans (audited 2026-11-14; `lib/cherchi/LICENSE_SURVEY.md`; header claim re-checked by me)
Repo gcherchi/InteractiveAndRobustMeshBooleans at 7bd6c269 is MIT with vendored Cinolib (MIT), Shewchuk predicates (public domain), oneTBB/abseil/parallel-hashmap (Apache-2.0). **Attene's `Indirect_Predicates` (required, header-only) is LGPL-3.0-or-later** (header text at `arrangements/external/Indirect_Predicates/include/indirect_predicates.h` lines 14-23; its LICENSE file is LGPL-2.1, inconsistent). It cannot be dropped. Not fully permissive: a no-GPL/LGPL rule fails; whether LGPL header-only code is acceptable is a legal question. Input must be manifold and watertight. Not built or scored. Observation for legal review, not a conclusion.
