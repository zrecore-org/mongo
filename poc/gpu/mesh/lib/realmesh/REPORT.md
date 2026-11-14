# MP26: validation on real meshes (Thingi10K subset) (sub-agent run; counts in `corpus/real/aggregate.txt`, which I re-read; selection and pair list hashed before any run in `corpus/real/HASHES.txt`)

Data: the pinned Thingi10K archive (sha256 matched `THINGI.pin`); no model files are committed or redistributed (results by model id only; each model has its own licence). The archive has no metadata, so `input_summary.csv` and `geometry_data.csv` were fetched from the same pinned Hugging Face commit. Selection rule: `.stl`, Closed, Edge and Vertex manifold, 0 self-intersections, 4 to 200k triangles: 4947 eligible, 560 selected by sha256("20261114:" + id). 420 pairs: 150 axis-permutation copies, 130 random-rotation copies, 140 different models rescaled and offset to overlap; 8 to 336,516 triangles (median 6.5k). Serial runs, 6 in parallel; no timeouts, no crashes anywhere.
**Checker amendment before the full run (disclosed):** a smoke test on 2 pairs showed nested same-orientation shells (winding 2) break parity, so the scorer uses exact winding: it drops samples where an operand's winding is outside {0,1} and requires the result's winding to be 0 or 1. 13 pairs have such samples and are reported separately.
## Results per operation (union / intersection / difference alike)
| pairs | tool | correct / wrong / refused |
|---|---|---|
| 407 without ambiguous operands | **pipeline v2** | 405 / **0** / 2 |
| | plain patched Geogram | 407 / 0 / 0 |
| | Manifold (serial) | 407 / 0 / 0 |
| all 420 | pipeline v2 | 410 / 0 / 10 |
| | plain patched Geogram | 420 / 0 / 0 (vacuous on the inverted pairs) |
| | Manifold | union 12 wrong, intersection 5, difference 12 (all on the 13 ambiguous pairs) |
## Refusals (all by the exact input validator; no hard-limit refusals)
Whole model inverted: p073, p138, p176, p209, p224, p261 (models 95420, 76541, 66773, 76540, 57896, 42840) and p301 (model 95410); the dataset's metadata says oriented and PWN for these, so it does not flag them. Exact zero-area triangle: p023 (model 153100) and p279 (model 95413, also inverted); the metadata says "no degenerate faces", so its test is not exact. p407 is a pair-builder artefact (rescaling merged two vertices of model 67817): the pipeline correctly refused the invalid B; plain Geogram accepted it and returned a non-manifold result.
## Manifold failures: all on the 13 ambiguous pairs: the 8 inverted ones, plus p001 (54228), p235 (53755), p304 (45413) with nested same-orientation shells, and p152 (44042), p302 (73236) with negative-volume components. These are input defects; Manifold does not refuse them.
## Validator gap
The pipeline and plain Geogram accepted p001, p152, p235, p302 and p304. The validator checks only the total volume sign, not per-component sign or nesting, so their "correct" covers only the unambiguous samples.
## Timing and memory (union; 6 parallel jobs, so upper bounds)
| tool | median s | p99 s | max s | peak RSS median / max MiB |
|---|---|---|---|---|
| pipeline v2 | 0.26 | 4.4 | 10.2 | 11 / 252 |
| plain Geogram | 0.28 | 4.4 | 10.2 | 10 / 232 |
| Manifold | 0.035 | 0.36 | 0.55 | 8 / 253 |
Pipeline over plain Geogram about 1.02 on runs over 1 s (Manifold about 8x faster at the median).
## Residual and other findings
|V(A u B) + V(A n B) - V(A) - V(B)| / V(A u B) on the clean pairs: pipeline median 4e-18, max 4.5e-15 (p340, same for plain Geogram); Manifold max 1.9e-14; none above 1e-12; never exactly zero (attributed to rounding of new intersection vertices, inferred from magnitude, not traced). p342's union (models 74934, 62592) is geometrically non-manifold (touching), identical for both Geogram runs. 26 clean pairs have an intersection with no sample inside the expected region (21 empty, 5 tiny), checked only by closedness and the residual. Pipeline paths: 618 re-centred, 612 c0. No pipeline failure and no checker failure found.
## Not tested
Models above 200k triangles, non-manifold or self-intersecting inputs, Linux, one machine; passing result files were deleted after scoring.
