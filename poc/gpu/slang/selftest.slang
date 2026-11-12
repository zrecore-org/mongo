// Slang port of shaders/selftest.comp (POC 8 probes: op 0 fma, op 1 twoProd, op 2 double -> df64 split).
#include "df64_ops.slang"
struct P { uint n; uint op; };
[[vk::push_constant]] ConstantBuffer<P> pc;
[[vk::binding(0, 0)]] StructuredBuffer<uint2> x;
[[vk::binding(1, 0)]] RWStructuredBuffer<float2> r;
[shader("compute")] [numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID)
{
    uint i = gid.x;
    if (i >= pc.n) return;
    float a = asfloat(x[i].x), b = asfloat(x[i].y);
    if (pc.op == 0u) { precise float p = a * b; r[i] = float2(p, fma(a, b, -p)); }
    else if (pc.op == 1u) r[i] = twoProd(a, b);
    else { uint bad = 0u; r[i] = splitDouble(x[i], bad); }
}
