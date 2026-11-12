// df64: an unevaluated sum hi+lo of two float32s (~48 mantissa bits).
// PREC expands to `precise` (SPIR-V NoContraction) or nothing, so the same source gives both variants.
#ifdef NOPRECISE
#define PREC
#else
#define PREC precise
#endif

PREC vec2 twoSum(float a, float b) {
    PREC float s = a + b;
    PREC float v = s - a;
    PREC float e = (a - (s - v)) + (b - v);
    return vec2(s, e);
}
PREC vec2 quickTwoSum(float a, float b) {  // requires |a| >= |b|
    PREC float s = a + b;
    PREC float e = b - (s - a);
    return vec2(s, e);
}
#ifdef NOFMA
// Dekker/Veltkamp product without fma, for drivers whose fma is not fused (see RESULTS.md, lavapipe).
PREC vec2 split12(float a) {
    PREC float c = 4097.0 * a;
    PREC float hi = c - (c - a);
    return vec2(hi, a - hi);
}
PREC vec2 twoProd(float a, float b) {
    PREC float p = a * b;
    PREC vec2 sa = split12(a);
    PREC vec2 sb = split12(b);
    PREC float e = ((sa.x * sb.x - p) + sa.x * sb.y + sa.y * sb.x) + sa.y * sb.y;
    return vec2(p, e);
}
#else
PREC vec2 twoProd(float a, float b) {
    PREC float p = a * b;
    PREC float e = fma(a, b, -p);
    return vec2(p, e);
}
#endif
PREC vec2 dfAdd(vec2 a, vec2 b) {  // accurate (IEEE-style) addition
    PREC vec2 s = twoSum(a.x, b.x);
    PREC vec2 t = twoSum(a.y, b.y);
    s.y += t.x;
    s = quickTwoSum(s.x, s.y);
    s.y += t.y;
    return quickTwoSum(s.x, s.y);
}
PREC vec2 dfNeg(vec2 a) { return vec2(-a.x, -a.y); }
PREC vec2 dfMul(vec2 a, vec2 b) {
    PREC vec2 p = twoProd(a.x, b.x);
    p.y += a.x * b.y + a.y * b.x;
    return quickTwoSum(p.x, p.y);
}
PREC vec2 dfMulF(vec2 a, float b) {
    PREC vec2 p = twoProd(a.x, b);
    p.y += a.y * b;
    return quickTwoSum(p.x, p.y);
}
PREC vec2 dfDiv(vec2 a, vec2 b) {  // long division, three quotient terms
    PREC float q1 = a.x / b.x;
    PREC vec2 r = dfAdd(a, dfNeg(dfMulF(b, q1)));
    PREC float q2 = r.x / b.x;
    r = dfAdd(r, dfNeg(dfMulF(b, q2)));
    PREC float q3 = r.x / b.x;
    PREC vec2 q = quickTwoSum(q1, q2);
    return dfAdd(q, vec2(q3, 0.0));
}
PREC vec2 dfSqrt(vec2 a) {
    if (a.x <= 0.0) return vec2(0.0);
    PREC float x = inversesqrt(a.x);
    PREC float ax = a.x * x;
    PREC vec2 d = dfAdd(a, dfNeg(twoProd(ax, ax)));
    return twoSum(ax, d.x * (x * 0.5));
}
