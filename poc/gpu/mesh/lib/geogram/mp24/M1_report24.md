# MP24 assembled pipeline (validate, exact re-centre, Geogram MP20+MP22, translate back) on Darwin-arm64

## g1 (93 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |
|---|---:|---:|---:|---:|---:|---:|---:|
| bad | 21 | 3 | 0 | 18 | 0 | 0 | 0 |
| box | 57 | 57 | 0 | 0 | 0 | 57 | 0 |
| sphere | 12 | 9 | 0 | 3 | 0 | 3 | 0 |
| torus | 3 | 0 | 0 | 3 | 0 | 0 | 0 |
| ALL | 93 | 69 | 0 | 24 | 0 | 60 | 0 |

## off (1533 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |
|---|---:|---:|---:|---:|---:|---:|---:|
| b53 | 27 | 27 | 0 | 0 | 0 | 27 | 0 |
| coplanar | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| edge | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| face | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| gap | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| identical | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| lattice | 96 | 96 | 0 | 0 | 0 | 96 | 0 |
| nested | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| on | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| overlap | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| shell | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| shift | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| sliver | 144 | 144 | 0 | 0 | 0 | 144 | 0 |
| span54 | 18 | 0 | 0 | 18 | 0 | 0 | 0 |
| sphere | 96 | 96 | 0 | 0 | 0 | 24 | 0 |
| vertex | 72 | 72 | 0 | 0 | 0 | 72 | 0 |
| ALL | 1533 | 1515 | 0 | 18 | 0 | 1443 | 0 |

## thin (513 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |
|---|---:|---:|---:|---:|---:|---:|---:|
| flush | 54 | 54 | 0 | 0 | 0 | 54 | 0 |
| gap | 54 | 54 | 0 | 0 | 0 | 54 | 0 |
| overlap | 54 | 54 | 0 | 0 | 0 | 54 | 0 |
| plate | 54 | 54 | 0 | 0 | 0 | 54 | 0 |
| shell | 54 | 54 | 0 | 0 | 0 | 54 | 0 |
| shift | 108 | 108 | 0 | 0 | 0 | 108 | 0 |
| sliver | 108 | 108 | 0 | 0 | 0 | 108 | 0 |
| sphere | 27 | 27 | 0 | 0 | 0 | 0 | 0 |
| ALL | 513 | 513 | 0 | 0 | 0 | 486 | 0 |

## set3 (1602 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |
|---|---:|---:|---:|---:|---:|---:|---:|
| nested | 360 | 360 | 0 | 0 | 0 | 360 | 0 |
| nonconvex | 270 | 270 | 0 | 0 | 0 | 270 | 0 |
| randvv | 300 | 300 | 0 | 0 | 0 | 300 | 0 |
| sphere | 282 | 0 | 0 | 282 | 0 | 0 | 0 |
| threecomp | 180 | 180 | 0 | 0 | 0 | 180 | 0 |
| threepoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| twopoint | 120 | 120 | 0 | 0 | 0 | 120 | 0 |
| ALL | 1602 | 1320 | 0 | 282 | 0 | 1320 | 0 |

## set5 (3300 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close |
|---|---:|---:|---:|---:|---:|---:|---:|
| axdeep | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| axnested | 75 | 75 | 0 | 0 | 0 | 75 | 0 |
| axpath4 | 120 | 120 | 0 | 0 | 0 | 120 | 0 |
| axsliver | 45 | 45 | 0 | 0 | 0 | 45 | 0 |
| axstar3 | 150 | 150 | 0 | 0 | 0 | 150 | 0 |
| axstar3r | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| axthreepoint | 75 | 75 | 0 | 0 | 0 | 75 | 0 |
| axtwopoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| axvv | 210 | 210 | 0 | 0 | 0 | 210 | 0 |
| curved | 180 | 0 | 0 | 180 | 0 | 0 | 0 |
| nestededge | 45 | 45 | 0 | 0 | 0 | 45 | 0 |
| nestedface | 45 | 45 | 0 | 0 | 0 | 45 | 0 |
| nestedvertex | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| nonconvex | 120 | 120 | 0 | 0 | 0 | 120 | 0 |
| octoct | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| octvert | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| path4 | 180 | 180 | 0 | 0 | 0 | 180 | 0 |
| randvv | 150 | 150 | 0 | 0 | 0 | 150 | 0 |
| rayabove | 240 | 240 | 0 | 0 | 0 | 240 | 0 |
| sliverany | 60 | 60 | 0 | 0 | 0 | 57 | 3 |
| sliverstar | 45 | 45 | 0 | 0 | 0 | 45 | 0 |
| slivervv | 60 | 60 | 0 | 0 | 0 | 60 | 0 |
| star3 | 210 | 210 | 0 | 0 | 0 | 210 | 0 |
| star3deep | 150 | 150 | 0 | 0 | 0 | 150 | 0 |
| star3r | 150 | 150 | 0 | 0 | 0 | 150 | 0 |
| threecomp | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| threepoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 |
| twopoint | 120 | 120 | 0 | 0 | 0 | 120 | 0 |
| twosided | 150 | 150 | 0 | 0 | 0 | 150 | 0 |
| ALL | 3300 | 3120 | 0 | 180 | 0 | 3117 | 3 |

bar 1 (G1 93 rows: 0 FAIL on valid and on malformed; the 6 huge-offset and 3 vertex-touch rows PASS): rows 93, valid FAIL 0, malformed FAIL 0, valid REFUSED 6, malformed REFUSED 18; must-pass rows PASS 9/9; non-PASS valid: sphere_vs_star.difference=REFUSED, sphere_vs_star.intersection=REFUSED, sphere_vs_star.union=REFUSED, torus_vs_sphere.difference=REFUSED, torus_vs_sphere.intersection=REFUSED, torus_vs_sphere.union=REFUSED

bar 2 (offset family, 511 pairs / 1533 rows: 0 FAIL, refusals only where the span exceeds 53 bits, counted): FAIL 0, REFUSED 18 (of which span54 with the 53-bit reason: 18; other: 0), span54 rows refused 18/18, b53 rows PASS 27/27

bar 3 (thin family, 171 pairs / 513 rows: 0 FAIL): FAIL 0, REFUSED 0, volume exact 486, close 0

bar 4 (touch set 3 and set 5 through the whole pipeline: 0 FAIL, 0 REFUSED): set3 1602 rows FAIL 0 REFUSED 282; set5 3300 rows FAIL 0 REFUSED 180

bar 5 (overhead of validate + re-centre + translate back <= 10% of the boolean's time, median over valid PASS rows): median of per-row ratios 2.75% (ratio of medians 2.70%) over 6534 rows; per family median ratio g1 1.46%, off 2.38%, thin 1.04%, set3 2.50%, set5 3.79% (in-process stage timers; scoring ran in parallel)

translate-back error: maximum 0.5 ulps of the original-frame value (row set5:s5_sliverany_012.union); 25109 of 386304 result coordinates rounded by the translate-back addition over 6537 PASS rows (the representation limit; first-rounding by Geogram in the translated frame is not included)

refusals by reason: g1: invalid input A: inverted orientation (exact volume not positive) x3; g1: invalid input A: non-manifold edge (as returned, 1 edges shared by more than two triangles or with equal direc x3; g1: invalid input A: non-manifold edge (as returned, 3 edges shared by more than two triangles or with equal direc x3; g1: invalid input A: open surface (as returned, 3 boundary edges) x6; g1: invalid input A: open surface (as returned, 36 boundary edges) x3; g1: re-centring needs more than 53 bits  x6; off: re-centring needs more than 53 bits  x18; set3: re-centring needs more than 53 bits  x282; set5: re-centring needs more than 53 bits  x180

bar 6 (platforms): MP24 outputs: 7041 rows; aggregate sha256 of the sorted '<sha256>  <family:case.op>  <verdict>' list = bdeddfbb2fe8291212de33542482157f66813ab3304b1ca4415e685e80f0634e; aggregate of the verdict list alone = 1059cc25230ab120b0119d3aacf506acc812ed33e5e75cb60a16d036466e66df. Identical verdicts and identical bytes between x86_64 and aarch64 are decided by comparing these two lines across the two artifacts (M1 vs Linux bytes may differ).

MP24: bars 1-5 on this platform: NOT MET (bar 1 MET, bar 2 MET, bar 3 MET, bar 4 NOT MET, bar 5 MET); max translate-back error 0.5 ulps; aggregate output hash bdeddfbb2fe82912
