#!/usr/bin/env python3
"""Counts per (library, op) from corpus.tsv; prints the breakdowns used in REPORT.md. usage: summarize.py <corpus.tsv>"""
import csv, collections, sys
rows = list(csv.DictReader(open(sys.argv[1]), delimiter='\t'))
fam = lambda r: r['case'].split('_')[1]
valid = [r for r in rows if fam(r) != 'inv']; inv = [r for r in rows if fam(r) == 'inv']
print("== valid corpus: lib op : CORRECT WRONG(area) WRONG_INVALID(structure) REFUSED CRASH  | exact-equal area")
c = collections.defaultdict(collections.Counter)
for r in valid: c[(r['lib'], r['op'])][r['verdict']] += 1
for k in sorted(c):
    v = c[k]; ex = sum(1 for r in valid if (r['lib'], r['op']) == k and r.get('exact') == '1')
    print(f"{k[0]:9s}{k[1]:13s}: {v['CORRECT']:4d} {v['WRONG']:4d} {v['WRONG_INVALID']:4d} {v['REFUSED']:4d} {v['CRASH']:4d} | exact {ex}")
print("== non-correct by family")
c2 = collections.Counter((r['lib'], r['op'], fam(r), r['verdict']) for r in valid if r['verdict'] != 'CORRECT')
for k in sorted(c2): print(k, c2[k])
print("== structural-invalid outputs by code, and whether their winding area was within tolerance")
for r in valid:
    if r['verdict'] == 'WRONG_INVALID': print(r['lib'], r['op'], r['case'], r['valid'], 'area_ok_rel=' + r.get('ok_rel', ''), 'area_ok_grid1=' + r.get('ok_grid1', ''), 'diff=' + r.get('diff', ''))
print("== Manifold at its own 1e-8 grid (secondary): bool/offset rows")
m = collections.Counter()
for r in valid:
    if r['lib'] == 'manifold' and r['op'] in ('union', 'intersection', 'difference', 'offset') and r['verdict'] in ('CORRECT', 'WRONG'):
        m[(r['op'], 'ok_grid8' if r['ok_grid8'] == '1' else 'not_ok_grid8')] += 1
print(dict(m))
print("== symdiff strictness: results with area OK but symdiff beyond tolerance")
for r in valid:
    if r['verdict'] == 'CORRECT' and r['op'] in ('union', 'intersection', 'difference', 'offset'):
        key = {'clipper2': 'sd_ok_grid1'}.get(r['lib'], 'sd_ok_rel')
        if r.get(key) != '1': print(r['lib'], r['op'], r['case'], 'symdiff=' + r['symdiff'], 'area diff=' + r['diff'])
print("== triangulation extras (valid corpus)")
t = collections.defaultdict(collections.Counter)
for r in valid:
    if r['op'] == 'triangulate' and r['verdict'] in ('CORRECT', 'WRONG'):
        k = r['lib']; t[k]['n'] += 1
        t[k]['degenerate_tris'] += int(r['degenerate'] or 0); t[k]['cases_with_degenerate'] += 1 if int(r['degenerate'] or 0) else 0
        t[k]['steiner'] += int(r['steiner'] or 0); t[k]['cases_with_steiner'] += 1 if int(r['steiner'] or 0) else 0
        t[k]['delaunay_edges'] += int(r['delaunay_edges'] or 0); t[k]['delaunay_viol'] += int(r['delaunay_violations'] or 0)
        t[k]['cases_with_delaunay_viol'] += 1 if int(r['delaunay_violations'] or 0) else 0
        t[k]['not_covered'] += 1 if r['covers'] != '1' else 0; t[k]['overlap'] += 1 if r['no_overlap'] != '1' else 0
for k in t: print(k, dict(t[k]))
print("== triangulation non-correct detail")
for r in valid:
    if r['op'] == 'triangulate' and r['verdict'] != 'CORRECT': print(r['lib'], r['case'], r['verdict'], 'covers=' + r.get('covers', ''), 'no_overlap=' + r.get('no_overlap', ''), 'symdiff=' + r.get('symdiff', ''), r.get('msg', ''))
print("== hull wrong")
for r in valid:
    if r['op'] == 'hull' and r['verdict'] == 'WRONG': print(r['lib'], r['case'], r.get('nwant'), r.get('ngot'))
print("== boost isvalid / intersects wrong, area wrong")
for r in rows:
    if r['lib'] == 'boost' and r['op'] in ('isvalid', 'intersects') and r['verdict'] == 'WRONG': print(r['case'], r['op'], 'want', r.get('want'), 'got', r.get('got'), r.get('why', ''))
print("== INVALID inputs (20), classification of each library/op")
ci = collections.defaultdict(collections.Counter)
for r in inv:
    if r['verdict'] in ('REFUSED', 'CRASH'): cls = r['verdict']
    elif r['op'] in ('isvalid', 'intersects'): cls = 'answered:' + (r.get('got') or '?')
    elif r['op'] == 'area': cls = 'area_returned'
    elif r['op'] == 'triangulate': cls = 'output_covers_polygon' if r.get('covers') == '1' else 'output_nonsense'
    elif r['op'] == 'hull': cls = 'hull_output'
    else: cls = 'output_valid' if r.get('valid') == 'ok' else 'output_invalid:' + (r.get('valid') or '?')
    ci[(r['lib'], r['op'])][cls.split(':')[0] if cls.startswith('output_invalid') else cls] += 1
for k in sorted(ci): print(k, dict(ci[k]))
print("== isvalid on invalid corpus: boost says valid for")
for r in inv:
    if r['lib'] == 'boost' and r['op'] == 'isvalid': print(r['case'], 'boost_is_valid=' + str(r.get('got')), 'checker_why=' + r.get('why', ''))
print("== crashes/timeouts/checker errors")
for r in rows:
    if r['verdict'] in ('CRASH', 'CHECKER_ERROR'): print(r['lib'], r['op'], r['case'], r['verdict'], r['msg'])
print("== refusal messages"); mc = collections.Counter((r['lib'], r['msg']) for r in rows if r['verdict'] == 'REFUSED')
for k, v in sorted(mc.items()): print(v, k)
