// POC 11: does pre-warming help? Two questions, both measured with the radix-select query (4M values, 3 ranks):
//  1. Startup: what does the first query after pipeline creation cost compared with the second? (A server would run
//     the df64 self-test and one dummy query at startup to pay that once.)
//  2. Idle clocks: a GPU that has been idle for a while drops its clocks (a hosted T4: 300 MHz idle, 1590 MHz under
//     load), so the first query after an idle gap is slow. Does a tiny "heartbeat" dispatch every 20 ms during the
//     gap keep the clocks up, and at what power cost? Prints query time, SM clock and power draw (from nvidia-smi,
//     when present) taken right before each query.
#include "rsel.h"

#include <chrono>
#include <thread>

static std::string smi() {  // "clock MHz, power W" or "n/a"
    FILE* p = popen("nvidia-smi --query-gpu=clocks.sm,power.draw --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) return "n/a";
    char buf[128] = {0};
    if (!fgets(buf, sizeof buf, p)) { pclose(p); return "n/a"; }
    pclose(p);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "n/a" : s;
}
static void sleepMs(double ms) { std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(ms * 1000))); }

int main() {
    const size_t n = 4u << 20;
    Sel g;
    if (!g.init(n, 3)) { printf("no GPU: %s\n", g.ctx.error.c_str()); return 2; }
    printf("device=%s memory=%s\n", g.ctx.props.deviceName, g.ctx.memMode());
    std::mt19937_64 rng(11);
    std::normal_distribution<double> nd(0, 1);
    std::vector<double> data(n);
    for (auto& x : data) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
    std::vector<uint32_t> ranks = {(uint32_t)(n / 2), (uint32_t)(n * 9 / 10), (uint32_t)(n * 99 / 100)};
    auto query = [&] { double t[3]; g.select(data.data(), n, ranks, t); return t[0] + t[1] + t[2]; };

    printf("startup: pipeline create hist %.1f ms, pick %.1f ms\n", g.kh.createMs, g.kp.createMs);
    double first = query(), second = query(), third = query();
    printf("startup: first query %.2f ms, second %.2f ms, third %.2f ms (a dummy query at startup pays the difference once)\n", first, second, third);

    auto smoke = g.ctx.makeKernel("build/shaders/smoke.spv", 2, 4);
    auto ha = g.ctx.alloc(256 * 4), hr = g.ctx.alloc(256 * 4);
    auto heartbeat = [&] { uint32_t cnt = 256; g.ctx.begin(); g.ctx.upload(ha); g.ctx.dispatch(smoke, {&ha, &hr}, &cnt, 4); g.ctx.download(hr); g.ctx.submitWait(); };

    printf("\nidle gap before one query (2 runs each; each run starts with 2 s of sustained load to reach full clocks)\n");
    printf("%-10s %8s | %-22s | %s\n", "condition", "gap s", "query ms (run1, run2)", "clock MHz, power W just before the query");
    const bool idleOnly = getenv("POC11_IDLE_ONLY") != nullptr;   // e.g. when the clocks have been locked by the operator
    for (int hb = 0; hb < (idleOnly ? 1 : 2); hb++)
        for (double gap : {0.0, 0.5, 2.0, 5.0, 12.0}) {
            double q[2]; std::string state[2];
            for (int r = 0; r < 2; r++) {
                gpu::warmup([&] { query(); }, 2000);
                double t0 = gpu::nowMs();
                while (gpu::nowMs() - t0 < gap * 1000) { if (hb) heartbeat(); sleepMs(20); }
                state[r] = smi();
                q[r] = query();
            }
            printf("%-10s %8.1f | %9.2f, %9.2f   | %s ; %s\n", hb ? "heartbeat" : "idle", gap, q[0], q[1], state[0].c_str(), state[1].c_str());
        }
    g.ctx.release(smoke); g.ctx.release(ha); g.ctx.release(hr);
    g.shutdown();
    return 0;
}
