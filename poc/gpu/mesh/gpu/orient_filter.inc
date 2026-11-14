// MP09: Shewchuk's orient3d stage-A floating-point filter evaluated in exact soft binary64 (shaders/f64soft.inc), one text for C++ and GLSL.
// Returns +1 or -1 only when the sign of the rounded determinant is provably the sign of the exact determinant (the error bound of Shewchuk's
// o3derrboundA, validated against exact arithmetic on 1e7 inputs in oracle/predicates_check), and 0 ("uncertain, ask the exact CPU path") otherwise.
// Because every operation is an exact IEEE binary64 operation, the result is bit-for-bit the same on the CPU (hardware doubles, -ffp-contract=off) and on any device.
// Window: every coordinate must be zero or have |x| in [2^-100, 2^101); outside it the function returns 0, which keeps every intermediate normal (no overflow,
// no subnormal) and the filter sound. The CPU reference applies the same rule.
#ifndef F64SOFT_INC_INCLUDED
#error include f64soft.inc first
#endif
FN bool inWindow(uint64_t x) {
    if ((x & 0x7FFFFFFFFFFFFFFFul) == 0ul) return true;
    uint64_t e = (x >> 52) & 0x7FFul;
    return e >= 923ul && e <= 1123ul;           // 1023 - 100 .. 1023 + 100
}
// o3derrboundA = (7 + 56 eps) eps with eps = 2^-53, as a binary64 bit pattern (computed in double; the host test asserts it)
#define ORIENT_ERRBOUND_A_BITS 0x3CCC000000000007ul
// detOut and boundOut return the determinant and the error bound as binary64 bit patterns (0 outside the window), so a test can compare them bit for bit,
// not only the final sign (the bound has a large margin, so the sign alone hides one-ulp differences).
FN int orient3dFilterSoftEx(uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz,
                            uint64_t cx, uint64_t cy, uint64_t cz, uint64_t dx, uint64_t dy, uint64_t dz, IO(uint64_t) detOut, IO(uint64_t) boundOut) {
    detOut = 0ul; boundOut = 0ul;
    if (!(inWindow(ax) && inWindow(ay) && inWindow(az) && inWindow(bx) && inWindow(by) && inWindow(bz) &&
          inWindow(cx) && inWindow(cy) && inWindow(cz) && inWindow(dx) && inWindow(dy) && inWindow(dz))) return 0;
    uint64_t adx = f64_sub(ax, dx), bdx = f64_sub(bx, dx), cdx = f64_sub(cx, dx);
    uint64_t ady = f64_sub(ay, dy), bdy = f64_sub(by, dy), cdy = f64_sub(cy, dy);
    uint64_t adz = f64_sub(az, dz), bdz = f64_sub(bz, dz), cdz = f64_sub(cz, dz);
    uint64_t bdxcdy = f64_mul(bdx, cdy), cdxbdy = f64_mul(cdx, bdy);
    uint64_t cdxady = f64_mul(cdx, ady), adxcdy = f64_mul(adx, cdy);
    uint64_t adxbdy = f64_mul(adx, bdy), bdxady = f64_mul(bdx, ady);
    uint64_t t1 = f64_mul(adz, f64_sub(bdxcdy, cdxbdy));
    uint64_t t2 = f64_mul(bdz, f64_sub(cdxady, adxcdy));
    uint64_t t3 = f64_mul(cdz, f64_sub(adxbdy, bdxady));
    uint64_t det = f64_add(f64_add(t1, t2), t3);
    uint64_t p1 = f64_mul(f64_add(f64_abs(bdxcdy), f64_abs(cdxbdy)), f64_abs(adz));
    uint64_t p2 = f64_mul(f64_add(f64_abs(cdxady), f64_abs(adxcdy)), f64_abs(bdz));
    uint64_t p3 = f64_mul(f64_add(f64_abs(adxbdy), f64_abs(bdxady)), f64_abs(cdz));
    uint64_t permanent = f64_add(f64_add(p1, p2), p3);
    uint64_t bound = f64_mul(ORIENT_ERRBOUND_A_BITS, permanent);
    detOut = det; boundOut = bound;
    if (f64_gt(det, bound)) return 1;
    if (f64_gt(f64_neg(det), bound)) return -1;
    return 0;
}
FN int orient3dFilterSoft(uint64_t ax, uint64_t ay, uint64_t az, uint64_t bx, uint64_t by, uint64_t bz,
                          uint64_t cx, uint64_t cy, uint64_t cz, uint64_t dx, uint64_t dy, uint64_t dz) {
    uint64_t d, b;
    return orient3dFilterSoftEx(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz, d, b);
}
