// Slang port of shaders/rsel_hist.comp and shaders/rsel_pick.comp (GPU radix select), line for line where
// possible, so the SPIR-V can be compared with the GLSL build. Same descriptor layout, push constants and
// specialization constant (id 0 = R). Not part of the CMake build; compile by hand (slangc 2026.20):
//   mkdir -p build/slang
//   for e in hist pick; do slangc slang/rsel.slang -target spirv -entry ${e}Main -stage compute -o build/slang/rsel_$e.spv; done
//   GPU_POC_RSEL_DIR=build/slang ./run.sh poc3_select     # default is the GLSL build in build/shaders
[vk::constant_id(0)] const uint R = 1;

struct P { uint n; uint stride; uint pass; };
[[vk::push_constant]] ConstantBuffer<P> pc;
[[vk::binding(0, 0)]] StructuredBuffer<uint2> x;     // IEEE double bits (x = low word, y = high word)
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> hist;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> st;   // [3r]=prefix hi, [3r+1]=prefix lo, [3r+2]=rank left

static const uint kMaxR = 16u;
groupshared uint sh[kMaxR * 256u];

uint2 shr64(uint2 k, uint s)
{
    if (s == 0u) return k;
    if (s >= 32u) return uint2(k.y >> (s - 32u), 0u);
    return uint2((k.x >> s) | (k.y << (32u - s)), k.y >> s);
}
uint2 orderedKey(uint2 w)
{
    return (w.y & 0x80000000u) != 0u ? uint2(~w.x, ~w.y) : uint2(w.x, w.y | 0x80000000u);
}

[shader("compute")] [numthreads(256, 1, 1)]
void histMain(uint3 gid : SV_DispatchThreadID, uint3 lid : SV_GroupThreadID)
{
    uint t = lid.x;
    for (uint j = t; j < R * 256u; j += 256u) sh[j] = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint shiftPrefix = 64u - 8u * pc.pass;
    for (uint i = gid.x; i < pc.n; i += pc.stride) {
        uint2 k = orderedKey(x[i]);
        uint digit = shr64(k, 56u - 8u * pc.pass).x & 255u;
        for (uint r = 0u; r < R; r++) {
            bool match = true;
            if (pc.pass > 0u) {
                uint2 pre = shr64(k, shiftPrefix);
                match = pre.x == st[3u * r + 1u] && pre.y == st[3u * r];
            }
            if (match) { uint old; InterlockedAdd(sh[r * 256u + digit], 1u, old); }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint j2 = t; j2 < R * 256u; j2 += 256u)
        if (sh[j2] != 0u) { uint old; InterlockedAdd(hist[j2], sh[j2], old); }
}

[shader("compute")] [numthreads(64, 1, 1)]
void pickMain(uint3 lid : SV_GroupThreadID)
{
    uint r = lid.x;
    if (r >= R) return;
    uint rank = st[3u * r + 2u], digit = 0u, cum = 0u;
    for (uint d = 0u; d < 256u; d++) {
        uint c = hist[r * 256u + d];
        if (cum + c > rank) { digit = d; break; }
        cum += c;
    }
    uint hi = st[3u * r], lo = st[3u * r + 1u];
    st[3u * r]      = (hi << 8) | (lo >> 24);
    st[3u * r + 1u] = (lo << 8) | digit;
    st[3u * r + 2u] = rank - cum;
    for (uint d2 = 0u; d2 < 256u; d2++) hist[r * 256u + d2] = 0u;
}
