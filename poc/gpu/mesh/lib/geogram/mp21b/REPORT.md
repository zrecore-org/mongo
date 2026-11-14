# MP21b: exact pre-validator in front of Geogram (2026-11-13 run; nothing committed)
Files: validator.h (checks), geogram_validated.cpp (separate binary, execs the runner, also `validate` mode), geogram_validated_inproc.cpp (runner.cpp #included unchanged, validator before it), build.sh, timing.py.
Binaries, scoring outputs, logs: storage/mesh/mp21b/ (bin/, s_*/results.tsv, s_*.log, timing_*.txt, validate_sweep.txt, extra_twin_pillow.msh).
Checks: finite coordinates; closed 2-manifold as returned (index) and after exact welding (each edge once per direction); no duplicate/inverted-twin triangle; exact zero-area test (double filter, exact fallback); exact volume > 0. Refusal: exit 3, `REFUSED: invalid input A|B: <reason>`.
Scored with oracle/score_library.py, corpus_large, GEOGRAM_RUNNER_THREADS=1, pinned v1.10.1 runner and MP20 patched runner (in-process variant).

## Bars
| bar | result |
|---|---|
| 0 FAIL on 21 malformed rows | MET: 18 REFUSED, 3 PASS (bad_self_intersecting), 0 FAIL, both runners. Unwrapped: 14 ACCEPTED-UNVERIFIED, 6 PASS, 1 FAIL (bad_open_box intersection) |
| 63 passing valid byte-identical, unrefused | MET: 63/63 identical, 0 refused (pinned); 66/66 with the patched runner. No valid verdict changed |
| never refuses a valid corpus input | MET: 0 refusals over 48 + 160 touch + 758 held-out + 1068 set 3 + 2100 set 4 non-bad input files (4134). Only corpus_large/primitives/grid_16.msh is refused (an open grid, not a solid, never a boolean input) |
| median overhead <= 5% on valid pairs | MET in-process: +1.77% (pinned), +1.43% (patched), 72 rows, 9 reps. NOT MET as a separate exec'd binary: +33.97% (a second process start on 6 ms runs) |
| planted mutants caught | NOT MET by the registered corpus. skip-duplicate: 0 verdict changes (bad_duplicate_triangle is already refused by the edge check, so the check is redundant there). tolerate-one-boundary-edge: 0 changes (bad_open_box has 3 boundary edges). skip-orientation: bad_inverted rows go REFUSED to ACCEPTED-UNVERIFIED (3 rows; caught by verdict change, not by FAIL). Supplementary extra_twin_pillow.msh (box plus a triangle with its inverted twin, mine, not in the registered corpus): the full validator refuses it, the skip-duplicate mutant accepts it |

## Ambiguities, not silently decided
1. "Refused or closed result" for unwelded vertices: I refuse (bad_unwelded is open as returned, 36 boundary edges). Welding instead was not tried.
2. bad_self_intersecting is not refused (self-intersection is not in the registered check list). It scores PASS only because the scorer treats its inputs as valid closed manifolds. The wrapper does not protect against self-intersection in general.
3. "Caught" for a mutant is undefined for a refuse-or-FAIL bar. I used: a malformed row changes verdict versus the clean validator, or FAILs.
4. Degenerate-triangle input is refused by the open-surface check before the zero-area test, so that test is never exercised alone by the corpus.
5. Not run: many_small_10.mshc container; timing on non-corpus pairs.
