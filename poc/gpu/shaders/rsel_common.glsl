// GPU radix select (k-th smallest of n doubles) using only 32-bit integer ops. Doubles are mapped to an
// order-preserving 64-bit key; each of 8 passes histograms one byte of the keys that still match the
// prefix chosen so far, and a tiny kernel picks the byte holding the wanted rank. No float math, so the
// answer is bit-exact (up to the -0/+0 ordering noted in RESULTS.md).
layout(constant_id = 0) const uint R = 1;   // number of ranks selected at once (percentile array)
// state[3*r+0]=prefix high word, [3*r+1]=prefix low word (right-aligned), [3*r+2]=rank still to find
layout(std430, binding = 1) buffer Hist { uint hist[]; };
layout(std430, binding = 2) buffer State { uint st[]; };
layout(push_constant) uniform P { uint n; uint stride; uint pass; } pc;

uvec2 shr64(uvec2 k, uint s) {  // k.x = low word, k.y = high word; shift right by 0 <= s < 64
    if (s == 0u) return k;
    if (s >= 32u) return uvec2(k.y >> (s - 32u), 0u);
    return uvec2((k.x >> s) | (k.y << (32u - s)), k.y >> s);
}
uvec2 orderedKey(uvec2 w) {      // IEEE double bits -> unsigned key with the same order
    return (w.y & 0x80000000u) != 0u ? uvec2(~w.x, ~w.y) : uvec2(w.x, w.y | 0x80000000u);
}
