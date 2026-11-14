# The mesh workstream moved

The 3D/2D mesh-boolean research (MP01 to MP26) now lives in its own repository: https://github.com/zrecore-org/exactmesh (Apache-2.0).

It was removed from this branch after the standalone repository's CI went green on x86_64 and aarch64 (run 43645014901). Everything is still in this branch's git history: check out the commit before the removal (`git log -- poc/gpu/mesh`) to see the original files.

The results entries for MP01 to MP26 in `poc/gpu/RESULTS.md` are kept as the record of that work; paths in them refer to the old layout.
