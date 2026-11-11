// POC 4: fallback and failure handling, with no GPU involved. A mock device injects device-lost, OOM,
// timeout and bad-result faults. Checks: whole-batch CPU rerun (never mixed results), circuit breaker
// opening, exponential backoff, canary re-probe, and that an open breaker never touches the device.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

enum class Status { kOk, kDeviceLost, kOutOfMemory, kTimeout };

struct Device {  // what the Vulkan backend would implement
    virtual ~Device() = default;
    virtual Status sum(const double* v, size_t n, double* out) = 0;
};

static double cpuSum(const double* v, size_t n) { double s = 0; for (size_t i = 0; i < n; i++) s += v[i]; return s; }

struct MockDevice : Device {
    Status next = Status::kOk;
    bool writePartialOnFail = true;  // a failing device may scribble on the output before failing
    int calls = 0;
    Status sum(const double* v, size_t n, double* out) override {
        calls++;
        if (next != Status::kOk) { if (writePartialOnFail) *out = -12345.0; return next; }
        *out = cpuSum(v, n);
        return Status::kOk;
    }
};

// Circuit breaker: after `threshold` consecutive failures the device is skipped for `backoff` ms
// (doubling up to `maxBackoff`); then one canary call decides whether to close the breaker again.
struct GpuManager {
    Device& dev;
    std::function<double()> nowMs;
    int threshold = 3, consecutive = 0;
    double backoff = 1000, maxBackoff = 8000, openUntil = 0;
    int fallbacks = 0, gpuOk = 0;
    GpuManager(Device& d, std::function<double()> clock) : dev(d), nowMs(std::move(clock)) {}

    bool open() const { return openUntil > nowMs(); }

    double sum(const double* v, size_t n) {
        if (!open()) {
            double out = 0;  // the device writes into a scratch value; it is only used on kOk
            Status s = dev.sum(v, n, &out);
            if (s == Status::kOk) { consecutive = 0; backoff = 1000; gpuOk++; return out; }
            if (++consecutive >= threshold) { openUntil = nowMs() + backoff; backoff = std::min(backoff * 2, maxBackoff); }
        }
        fallbacks++;
        return cpuSum(v, n);  // the whole batch is recomputed on the CPU
    }
};

int main() {
    std::vector<double> v(1000); for (size_t i = 0; i < v.size(); i++) v[i] = 0.5 * (double)i;
    const double expect = cpuSum(v.data(), v.size());
    double now = 0;
    MockDevice dev; GpuManager mgr(dev, [&] { return now; });
    int failures = 0;
    auto check = [&](bool ok, const char* what) { printf("%-70s %s\n", what, ok ? "PASS" : "FAIL"); failures += !ok; };

    check(mgr.sum(v.data(), v.size()) == expect && mgr.gpuOk == 1, "healthy device is used");
    for (Status s : {Status::kDeviceLost, Status::kOutOfMemory, Status::kTimeout}) {
        dev.next = s;
        double r = mgr.sum(v.data(), v.size());
        check(r == expect, s == Status::kDeviceLost ? "device-lost: CPU result returned, partial device output ignored"
                         : s == Status::kOutOfMemory ? "OOM: CPU result returned" : "timeout: CPU result returned");
    }
    check(mgr.open(), "breaker opens after 3 consecutive failures");
    int before = dev.calls;
    for (int i = 0; i < 50; i++) assert(mgr.sum(v.data(), v.size()) == expect);
    check(dev.calls == before, "open breaker: 50 calls served by CPU, device untouched");

    now += 1001;  // backoff (1 s) elapsed: one canary call goes to the device, still failing
    before = dev.calls;
    mgr.sum(v.data(), v.size());
    check(dev.calls == before + 1 && mgr.open(), "canary probe fails -> breaker re-opens");
    check(mgr.openUntil - now > 1500 && mgr.openUntil - now <= 2000, "backoff doubled to 2 s");
    for (int k = 0; k < 6; k++) { now += mgr.openUntil - now + 1; mgr.sum(v.data(), v.size()); }
    check(mgr.openUntil - now <= 8000 && mgr.openUntil - now > 7000, "backoff capped at 8 s");

    dev.next = Status::kOk;  // device recovers
    now += 8001;
    check(mgr.sum(v.data(), v.size()) == expect && !mgr.open() && mgr.consecutive == 0, "canary succeeds -> breaker closes, GPU used again");
    int ok = mgr.gpuOk;
    mgr.sum(v.data(), v.size());
    check(mgr.gpuOk == ok + 1, "after recovery the device serves requests");

    dev.next = Status::kDeviceLost;  // a single transient failure must not open the breaker
    mgr.consecutive = 0;
    check(mgr.sum(v.data(), v.size()) == expect && !mgr.open(), "one transient failure: CPU result, breaker stays closed");
    printf("\nfallbacks=%d gpuOk=%d -> %s\n", mgr.fallbacks, mgr.gpuOk, failures ? "FAIL" : "ALL PASS");
    return failures ? 1 : 0;
}
