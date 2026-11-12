// POC 0: dlopen-based Vulkan probe, compute smoke test, and CPU fallback on a missing loader.
#include "vkctx.h"

#include <vector>

int main(int argc, char** argv) {
    const std::string spv = argc > 1 ? argv[1] : "build/shaders/smoke.spv";
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) {
        // This is the fallback path a server would take: log, then use the CPU backend.
        printf("FALLBACK: GPU unavailable (%s) -> using CPU backend\n", ctx.error.c_str());
        std::vector<float> a(1000), r(1000);
        for (int i = 0; i < 1000; i++) a[i] = i, r[i] = a[i] * 2.f + 1.f;
        printf("cpu result[999]=%g (expect 1999)\n", r[999]);
        return r[999] == 1999.f ? 0 : 1;
    }
    printf("device: %s  type=%d  api=%u.%u.%u  unifiedMemory=%d memory=%s\n", ctx.props.deviceName,
           ctx.props.deviceType, VK_VERSION_MAJOR(ctx.props.apiVersion),
           VK_VERSION_MINOR(ctx.props.apiVersion), VK_VERSION_PATCH(ctx.props.apiVersion),
           (int)ctx.unifiedMemory, ctx.memMode());
    printf("shaderFloat64=%d shaderInt64=%d\n", ctx.feats.shaderFloat64, ctx.feats.shaderInt64);
    const uint32_t n = 1 << 20;
    auto a = ctx.alloc(n * 4), r = ctx.alloc(n * 4);
    for (uint32_t i = 0; i < n; i++) ((float*)a.ptr)[i] = (float)i;
    auto k = ctx.makeKernel(spv, 2, 4);
    printf("pipeline create (incl. MSL compile): %.2f ms\n", k.createMs);
    ctx.begin();
    ctx.upload(a);
    ctx.dispatch(k, {&a, &r}, &n, (n + 63) / 64);
    ctx.download(r);
    double ms = ctx.submitWait();
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; i++)
        if (((float*)r.ptr)[i] != (float)i * 2.f + 1.f) bad++;
    printf("dispatch %u elems: %.3f ms, mismatches=%u -> %s\n", n, ms, bad, bad ? "FAIL" : "PASS");
    ctx.release(a); ctx.release(r); ctx.release(k); ctx.destroy();
    return bad ? 1 : 0;
}
