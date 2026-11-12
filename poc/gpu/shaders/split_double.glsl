// Double (raw IEEE bits) -> df64 using only 32-bit integer and float ops. Needs df64_ops.glsl.
// Exact-ish double -> df64 conversion from the raw bits, using only 32-bit integer and float ops.
// Sets `bad` for NaN/Inf/denormal and for exponents outside [-99, 99] (the df64-safe range).
vec2 splitDouble(uvec2 w, inout uint bad) {
    uint hw = w.y;
    uint e = (hw >> 20) & 0x7FFu;
    uint mh = hw & 0xFFFFFu;
    if (e == 0u) {
        if ((mh | w.x) != 0u) bad = 1u;
        return vec2(0.0);
    }
    int E = int(e) - 1023;
    if (E < -99 || E > 99) { bad = 1u; return vec2(0.0); }
    uint mhi = mh | 0x100000u;                         // 21 bits incl. the implicit one
    float p1 = float(mhi) * 4294967296.0;              // exact
    float p2 = float(w.x >> 8) * 256.0;                // 24 bits, exact
    float p3 = float(w.x & 255u);                      // exact
    vec2 s = dfAdd(twoSum(p1, p2), vec2(p3, 0.0));     // significand as an integer
    int ex = E - 52;
    int h = ex >> 1;
    float s1 = uintBitsToFloat(uint(127 + h) << 23);
    float s2 = uintBitsToFloat(uint(127 + ex - h) << 23);
    s = vec2((s.x * s1) * s2, (s.y * s1) * s2);       // powers of two: exact
    return (hw >> 31) != 0u ? -s : s;
}

