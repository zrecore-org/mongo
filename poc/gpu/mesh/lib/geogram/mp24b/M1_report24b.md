# MP24b pipeline v2 (validate, exact scale, exact re-centre or c = 0 or hard-limit refusal, Geogram MP20+MP22, translate back, scale back) on Darwin-arm64

## g1 (93 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| bad | 21 | 3 | 0 | 18 | 0 | 0 | 0 | 3 | 0 |
| box | 57 | 57 | 0 | 0 | 0 | 57 | 0 | 57 | 0 |
| sphere | 12 | 12 | 0 | 0 | 0 | 3 | 0 | 9 | 3 |
| torus | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 3 |
| ALL | 93 | 75 | 0 | 18 | 0 | 60 | 0 | 69 | 6 |

## off (1533 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| b53 | 27 | 27 | 0 | 0 | 0 | 27 | 0 | 27 | 0 |
| coplanar | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| edge | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| face | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| gap | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| identical | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| lattice | 96 | 96 | 0 | 0 | 0 | 96 | 0 | 96 | 0 |
| nested | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| on | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| overlap | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| shell | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| shift | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| sliver | 144 | 144 | 0 | 0 | 0 | 144 | 0 | 144 | 0 |
| span54 | 18 | 18 | 0 | 0 | 0 | 18 | 0 | 0 | 18 |
| sphere | 96 | 96 | 0 | 0 | 0 | 24 | 0 | 96 | 0 |
| vertex | 72 | 72 | 0 | 0 | 0 | 72 | 0 | 72 | 0 |
| ALL | 1533 | 1533 | 0 | 0 | 0 | 1461 | 0 | 1515 | 18 |

## thin (513 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| flush | 54 | 54 | 0 | 0 | 0 | 54 | 0 | 54 | 0 |
| gap | 54 | 54 | 0 | 0 | 0 | 54 | 0 | 54 | 0 |
| overlap | 54 | 54 | 0 | 0 | 0 | 54 | 0 | 54 | 0 |
| plate | 54 | 54 | 0 | 0 | 0 | 54 | 0 | 54 | 0 |
| shell | 54 | 54 | 0 | 0 | 0 | 54 | 0 | 54 | 0 |
| shift | 108 | 108 | 0 | 0 | 0 | 108 | 0 | 108 | 0 |
| sliver | 108 | 108 | 0 | 0 | 0 | 108 | 0 | 108 | 0 |
| sphere | 27 | 27 | 0 | 0 | 0 | 0 | 0 | 27 | 0 |
| ALL | 513 | 513 | 0 | 0 | 0 | 486 | 0 | 513 | 0 |

## set3 (1602 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| nested | 360 | 360 | 0 | 0 | 0 | 360 | 0 | 360 | 0 |
| nonconvex | 270 | 270 | 0 | 0 | 0 | 270 | 0 | 270 | 0 |
| randvv | 300 | 300 | 0 | 0 | 0 | 300 | 0 | 300 | 0 |
| sphere | 282 | 282 | 0 | 0 | 0 | 0 | 0 | 0 | 282 |
| threecomp | 180 | 180 | 0 | 0 | 0 | 180 | 0 | 180 | 0 |
| threepoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| twopoint | 120 | 120 | 0 | 0 | 0 | 120 | 0 | 120 | 0 |
| ALL | 1602 | 1602 | 0 | 0 | 0 | 1320 | 0 | 1320 | 282 |

## set5 (3300 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| axdeep | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| axnested | 75 | 75 | 0 | 0 | 0 | 75 | 0 | 75 | 0 |
| axpath4 | 120 | 120 | 0 | 0 | 0 | 120 | 0 | 120 | 0 |
| axsliver | 45 | 45 | 0 | 0 | 0 | 45 | 0 | 45 | 0 |
| axstar3 | 150 | 150 | 0 | 0 | 0 | 150 | 0 | 150 | 0 |
| axstar3r | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| axthreepoint | 75 | 75 | 0 | 0 | 0 | 75 | 0 | 75 | 0 |
| axtwopoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| axvv | 210 | 210 | 0 | 0 | 0 | 210 | 0 | 210 | 0 |
| curved | 180 | 180 | 0 | 0 | 0 | 180 | 0 | 0 | 180 |
| nestededge | 45 | 45 | 0 | 0 | 0 | 45 | 0 | 45 | 0 |
| nestedface | 45 | 45 | 0 | 0 | 0 | 45 | 0 | 45 | 0 |
| nestedvertex | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| nonconvex | 120 | 120 | 0 | 0 | 0 | 120 | 0 | 120 | 0 |
| octoct | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| octvert | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| path4 | 180 | 180 | 0 | 0 | 0 | 180 | 0 | 180 | 0 |
| randvv | 150 | 150 | 0 | 0 | 0 | 150 | 0 | 150 | 0 |
| rayabove | 240 | 240 | 0 | 0 | 0 | 240 | 0 | 240 | 0 |
| sliverany | 60 | 60 | 0 | 0 | 0 | 57 | 3 | 60 | 0 |
| sliverstar | 45 | 45 | 0 | 0 | 0 | 45 | 0 | 45 | 0 |
| slivervv | 60 | 60 | 0 | 0 | 0 | 60 | 0 | 60 | 0 |
| star3 | 210 | 210 | 0 | 0 | 0 | 210 | 0 | 210 | 0 |
| star3deep | 150 | 150 | 0 | 0 | 0 | 150 | 0 | 150 | 0 |
| star3r | 150 | 150 | 0 | 0 | 0 | 150 | 0 | 150 | 0 |
| threecomp | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| threepoint | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 90 | 0 |
| twopoint | 120 | 120 | 0 | 0 | 0 | 120 | 0 | 120 | 0 |
| twosided | 150 | 150 | 0 | 0 | 0 | 150 | 0 | 150 | 0 |
| ALL | 3300 | 3300 | 0 | 0 | 0 | 3297 | 3 | 3120 | 180 |

## f2 (2412 rows)
| class | rows | PASS | FAIL | REFUSED | other | volume exact | volume close | path recentred | path c0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| cfar | 222 | 0 | 0 | 222 | 0 | 0 | 0 | 0 | 0 |
| cfarrc | 84 | 81 | 3 | 0 | 0 | 42 | 0 | 84 | 0 |
| cnear | 372 | 372 | 0 | 0 | 0 | 180 | 0 | 21 | 351 |
| nest | 468 | 468 | 0 | 0 | 0 | 390 | 0 | 468 | 0 |
| off | 645 | 645 | 0 | 0 | 0 | 645 | 0 | 645 | 0 |
| thin | 456 | 456 | 0 | 0 | 0 | 420 | 0 | 456 | 0 |
| touch | 159 | 159 | 0 | 0 | 0 | 159 | 0 | 132 | 27 |
| uflow | 6 | 0 | 0 | 6 | 0 | 0 | 0 | 0 | 0 |
| ALL | 2412 | 2181 | 3 | 228 | 0 | 1836 | 0 | 1806 | 378 |

bar 1 (G1 93 rows: 0 FAIL on valid and on malformed; the 9 huge-offset and vertex-touch rows PASS): rows 93, valid FAIL 0, malformed FAIL 0, valid REFUSED 0, malformed REFUSED 18; must-pass rows PASS 9/9; non-PASS valid: none

bar 2 (offset family, 511 pairs / 1533 rows: 0 FAIL; under v2 the span54 pairs take the c = 0 path instead of refusing): FAIL 0, REFUSED 0, span54 rows PASS 18/18, b53 rows PASS 27/27

bar 3 (thin family, 171 pairs / 513 rows: 0 FAIL): FAIL 0, REFUSED 0, volume exact 486, close 0

bar 4 (touch set 3 and set 5: 0 FAIL, 0 REFUSED; bar-4 style refusals): set3 1602 rows FAIL 0 REFUSED 0; set5 3300 rows FAIL 0 REFUSED 0

MP24b acceptance (f2: 804 pairs / 2412 rows; T = 2^12): FAIL 3 (all families: 3), REFUSED 228 (hard-limit refusals in documented classes 228; undocumented 0), non-hard-limit refusals of valid rows in any family 0, PASS 2181
  FAIL f2:f2_cfarrc_L3r_j40_shift.difference: f2_cfarrc_L3r_j40_shift difference: FAIL | inputs=valid | result tri=1676 closed=1 geomClosed=0 euler=5 components=1 zeroArea=1 | volume=n/a | cells 0 bad 0 skipped 0 | samples 5000 bad 0 skipped 0 | 
  FAIL f2:f2_cfarrc_L3r_j40_shift.intersection: f2_cfarrc_L3r_j40_shift intersection: FAIL | inputs=valid | result tri=1560 closed=1 geomClosed=0 euler=5 components=1 zeroArea=1 | volume=n/a | cells 0 bad 0 skipped 0 | samples 5000 bad 0 skipped 0 
  FAIL f2:f2_cfarrc_L3r_j40_shift.union: f2_cfarrc_L3r_j40_shift union: FAIL | inputs=valid | result tri=1800 closed=1 geomClosed=0 euler=7 components=1 zeroArea=1 | volume=n/a | cells 0 bad 0 skipped 0 | samples 5000 bad 0 skipped 0 | resul
hard-limit refusals by class (f2): cfar / far from the origin: 222; uflow / underflow: 6; hard-limit refusals in the older families: 0
f2 verdicts by class: cfar: REFUSED 222, cfarrc: PASS 81/FAIL 3, cnear: PASS 372, nest: PASS 468, off: PASS 645, thin: PASS 456, touch: PASS 159, uflow: REFUSED 6
cfar rows that PASSED anyway (exactly re-centrable or ratio <= T): 0 of 222

overhead of validate + scale + re-centre + translate/scale back vs the boolean (median over valid PASS rows): median of per-row ratios 2.80% (ratio of medians 2.56%) over 9201 rows; per family g1 1.55%, off 2.46%, thin 1.20%, set3 2.93%, set5 4.06%, f2 2.12% (in-process stage timers; scoring ran in parallel)
translate-back error: maximum 0.5 ulps of the original-frame value (row set5:s5_sliverany_012.union); 42890 of 1320765 result coordinates rounded by the translate-back addition over 9204 PASS rows (the representation limit; Geogram's own rounding in the working frame is not included)
paths of PASS rows: f2/c0 378, f2/recentred 1803, g1/c0 6, g1/recentred 69, off/c0 18, off/recentred 1515, set3/c0 282, set3/recentred 1320, set5/c0 180, set5/recentred 3120, thin/recentred 513

refusals by reason: f2: hard limit (far from the origin): not exactly re-c x222; f2: hard limit (underflow): scaling by 2^-483 would un x3; f2: hard limit (underflow): scaling by 2^-983 would un x3; g1: invalid input A: inverted orientation (exact volum x3; g1: invalid input A: non-manifold edge (as returned, 1 x3; g1: invalid input A: non-manifold edge (as returned, 3 x3; g1: invalid input A: open surface (as returned, 3 boun x6; g1: invalid input A: open surface (as returned, 36 bou x3

platforms: MP24b outputs: 9453 rows; aggregate sha256 of the sorted '<sha256>  <family:case.op>  <verdict>' list = a46b1a3b8b6f3049d252aba52dc2be1a865c71d67293c01938dc315a0ce9a990; aggregate of the verdict list alone = e2edd6f7d6ad9862e6cdcc3ff77ea962ccff689893a7baca0d291d673e20a184. Identical verdicts and identical bytes between x86_64 and aarch64 are decided by comparing these two lines across the two artifacts (M1 vs Linux bytes may differ).

MP24 bars under v2: bar 1 MET, bar 2 MET, bar 3 MET, bar 4 MET
MP24b: 0 FAIL and only hard-limit refusals: NOT MET (3 FAIL rows over all families, 228 hard-limit refusals of valid rows (all documented classes: True), 0 other refusals of valid rows); max translate-back error 0.5 ulps; aggregate output hash a46b1a3b8b6f3049
