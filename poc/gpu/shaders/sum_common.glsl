// Shared by the POC 2 segmented-sum kernels. Each thread keeps GROUPS private df64 accumulators
// (grid-stride loop), then the workgroup tree-reduces each group. The host combines the per-workgroup
// partials with DoubleDoubleSummation semantics. No float atomics are needed, so results are deterministic
// for a given workgroup size and grid.
#extension GL_GOOGLE_include_directive : require
#include "df64_ops.glsl"

layout(local_size_x_id = 0) in;
layout(constant_id = 1) const uint GROUPS = 1;

layout(std430, binding = 1) readonly buffer Gid { uint gid[]; };
layout(std430, binding = 2) writeonly buffer Out { vec2 part[]; };
layout(std430, binding = 3) writeonly buffer Flag { uint badOut[]; };
layout(push_constant) uniform P { uint n; uint stride; } pc;

shared vec2 sh[1024];
shared uint shBad;

const float kLo = 1e-30;
const float kHi = 1e30;

void reduceAndStore(vec2 acc[GROUPS], uint bad) {
    uint t = gl_LocalInvocationID.x;
    uint wgs = gl_WorkGroupSize.x;
    if (t == 0u) shBad = 0u;
    barrier();
    if (bad != 0u) atomicOr(shBad, 1u);
    for (uint k = 0u; k < GROUPS; k++) {
        sh[t] = acc[k];
        barrier();
        for (uint s = wgs >> 1; s > 0u; s >>= 1) {
            if (t < s) sh[t] = dfAdd(sh[t], sh[t + s]);
            barrier();
        }
        if (t == 0u) part[gl_WorkGroupID.x * GROUPS + k] = sh[0];
        barrier();
    }
    if (t == 0u) badOut[gl_WorkGroupID.x] = shBad;
}
