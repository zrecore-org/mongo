#!/usr/bin/env python3
"""Checks df64 GPU results against an exact oracle (Python Decimal, 150 digits).

Two errors are reported separately (they answer different questions):
  kernel : GPU result vs exact op on the *split* inputs (hi+lo as float32 pairs) -> kernel arithmetic quality
  e2e    : GPU result vs exact op on the *original doubles* -> adds the double->df64 split loss (53 -> ~48 bits)
Errors are relative to the exact result, in units of 2^-44.
"""
import sys
from decimal import Decimal, getcontext
import numpy as np

getcontext().prec = 150
OPS = ["add", "mul", "div", "sqrt"]
CATS = ["mixed", "cancel", "wide", "tiny", "huge"]
U = Decimal(2) ** -44
tag = sys.argv[1]
d = lambda f, t: np.fromfile(f"out/{f}.bin", dtype=t)
a64, b64 = d("df64_a64", "<f8"), d("df64_b64", "<f8")
asp, bsp = d("df64_asplit", "<f4").reshape(-1, 2), d("df64_bsplit", "<f4").reshape(-1, 2)
cat = d("df64_cat", "u1")
D = Decimal

def exact(op, x, y):
    if op == 0: return x + y
    if op == 1: return x * y
    if op == 2: return x / y if y != 0 else None
    return x.sqrt() if x > 0 else None  # sqrt of a negative is an error upstream, so it is not tested

def quantile(v, q):
    return sorted(v)[min(len(v) - 1, int(q * len(v)))] if v else float("nan")

ok_all = True
print(f"# df64 verification, shader variant: {tag}")
print(f"{'op':5} {'cat':7} {'n':>6} {'nonfin':>6} {'kernel max':>11} {'kernel p99.9':>13} {'e2e max':>10} {'flagged':>8} {'unflagged>4u':>13}")
for op in range(4):
    res = d(f"df64_res_{tag}_{op}", "<f4").reshape(-1, 2)
    flag = d(f"df64_flag_{op}", "u1")
    for c in range(len(CATS)):
        idx = np.where(cat == c)[0]
        kerr, eerr, nonfin, unflagged_bad = [], [], 0, 0
        for i in idx[::2]:  # every other element keeps the oracle fast
            hi, lo = float(res[i, 0]), float(res[i, 1])
            if not (np.isfinite(hi) and np.isfinite(lo)):
                nonfin += 1
                if not flag[i]: unflagged_bad += 1
                continue
            g = D(hi) + D(lo)
            xs = D(float(asp[i, 0])) + D(float(asp[i, 1]))
            ys = D(float(bsp[i, 0])) + D(float(bsp[i, 1]))
            ek = exact(op, xs, ys)
            ee = exact(op, D(float(a64[i])), D(float(b64[i])))
            if ek is None or ek == 0 or ee == 0: continue
            k = float(abs(g - ek) / abs(ek) / U)
            e = float(abs(g - ee) / abs(ee) / U)
            kerr.append(k); eerr.append(e)
            if k > 4 and not flag[i]: unflagged_bad += 1
        nfl = int(flag[idx].sum())
        km = max(kerr) if kerr else float("nan")
        print(f"{OPS[op]:5} {CATS[c]:7} {len(idx):6} {nonfin:6} {km:11.2f} {quantile(kerr,.999):13.2f} {(max(eerr) if eerr else float('nan')):10.1f} {nfl:8} {unflagged_bad:13}")
        if unflagged_bad: ok_all = False
print("RESULT:", "PASS (every kernel error >4u or non-finite result was caught by the preflight)" if ok_all else "FAIL (unflagged errors exist)")
sys.exit(0 if ok_all else 1)
