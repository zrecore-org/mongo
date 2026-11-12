// Slang port of shaders/df64_ops.glsl: df64 = unevaluated sum hi+lo of two float32s. `precise` on every
// intermediate (SPIR-V NoContraction). Compile with -DNOFMA for the Dekker twoProd (no fma), as for the GLSL build.
float2 twoSum(float a, float b)
{
    precise float s = a + b;
    precise float v = s - a;
    precise float e = (a - (s - v)) + (b - v);
    return float2(s, e);
}
float2 quickTwoSum(float a, float b)   // requires |a| >= |b|
{
    precise float s = a + b;
    precise float e = b - (s - a);
    return float2(s, e);
}
#ifdef NOFMA
float2 split12(float a)
{
    precise float c = 4097.0 * a;
    precise float hi = c - (c - a);
    precise float lo = a - hi;
    return float2(hi, lo);
}
float2 twoProd(float a, float b)
{
    precise float p = a * b;
    float2 sa = split12(a);
    float2 sb = split12(b);
    precise float e = ((sa.x * sb.x - p) + sa.x * sb.y + sa.y * sb.x) + sa.y * sb.y;
    return float2(p, e);
}
#else
float2 twoProd(float a, float b)
{
    precise float p = a * b;
    precise float e = fma(a, b, -p);
    return float2(p, e);
}
#endif
float2 dfAdd(float2 a, float2 b)
{
    float2 s = twoSum(a.x, b.x);
    float2 t = twoSum(a.y, b.y);
    precise float sy = s.y + t.x;
    s = quickTwoSum(s.x, sy);
    precise float sy2 = s.y + t.y;
    return quickTwoSum(s.x, sy2);
}
float2 dfNeg(float2 a) { return float2(-a.x, -a.y); }
float2 dfMul(float2 a, float2 b)
{
    float2 p = twoProd(a.x, b.x);
    precise float py = p.y + (a.x * b.y + a.y * b.x);
    return quickTwoSum(p.x, py);
}
float2 dfMulF(float2 a, float b)
{
    float2 p = twoProd(a.x, b);
    precise float py = p.y + a.y * b;
    return quickTwoSum(p.x, py);
}
float2 dfDiv(float2 a, float2 b)
{
    precise float q1 = a.x / b.x;
    float2 r = dfAdd(a, dfNeg(dfMulF(b, q1)));
    precise float q2 = r.x / b.x;
    r = dfAdd(r, dfNeg(dfMulF(b, q2)));
    precise float q3 = r.x / b.x;
    float2 q = quickTwoSum(q1, q2);
    return dfAdd(q, float2(q3, 0.0));
}
float2 dfSqrt(float2 a)
{
    if (a.x <= 0.0) return float2(0.0, 0.0);
    precise float x = rsqrt(a.x);
    precise float ax = a.x * x;
    float2 d = dfAdd(a, dfNeg(twoProd(ax, ax)));
    precise float hx = x * 0.5;
    precise float dh = d.x * hx;
    return twoSum(ax, dh);
}

// Double (raw IEEE bits; x = low word, y = high word) -> df64 using only 32-bit integer and float ops.
// Sets `bad` for NaN/Inf/denormal and for exponents outside [-99, 99].
float2 splitDouble(uint2 w, inout uint bad)
{
    uint hw = w.y;
    uint e = (hw >> 20) & 0x7FFu;
    uint mh = hw & 0xFFFFFu;
    if (e == 0u) {
        if ((mh | w.x) != 0u) bad = 1u;
        return float2(0.0, 0.0);
    }
    int E = int(e) - 1023;
    if (E < -99 || E > 99) { bad = 1u; return float2(0.0, 0.0); }
    uint mhi = mh | 0x100000u;
    float p1 = float(mhi) * 4294967296.0;
    float p2 = float(w.x >> 8) * 256.0;
    float p3 = float(w.x & 255u);
    float2 s = dfAdd(twoSum(p1, p2), float2(p3, 0.0));
    int ex = E - 52;
    int h = ex >> 1;
    float s1 = asfloat(uint(127 + h) << 23);
    float s2 = asfloat(uint(127 + ex - h) << 23);
    s = float2((s.x * s1) * s2, (s.y * s1) * s2);
    return (hw >> 31) != 0u ? -s : s;
}
