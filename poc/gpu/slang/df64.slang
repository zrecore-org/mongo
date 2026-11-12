// Slang port of shaders/df64.comp (POC 1 kernel: op 0 add, 1 mul, 2 div, 3 sqrt).
#include "df64_ops.slang"
struct P { uint n; uint op; };
[[vk::push_constant]] ConstantBuffer<P> pc;
[[vk::binding(0, 0)]] StructuredBuffer<float2> a;
[[vk::binding(1, 0)]] StructuredBuffer<float2> b;
[[vk::binding(2, 0)]] RWStructuredBuffer<float2> r;
[shader("compute")] [numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID)
{
    uint i = gid.x;
    if (i >= pc.n) return;
    float2 x = a[i], y = b[i], z;
    if (pc.op == 0u) z = dfAdd(x, y);
    else if (pc.op == 1u) z = dfMul(x, y);
    else if (pc.op == 2u) z = dfDiv(x, y);
    else z = dfSqrt(x);
    r[i] = z;
}
