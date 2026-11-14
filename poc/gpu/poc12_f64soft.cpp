// POC 12 part 1: exact binary64 addition in integer arithmetic (shaders/f64soft.inc) checked on the host against the hardware double.
//   ./run.sh poc12_f64soft host      targeted + random pairs, then DoubleDoubleSummation sequences, then planted mutants (must be caught)
//   ./run.sh poc12_f64soft device    the same pairs through the Vulkan device (build/shaders/softadd_probe.spv)
#include "vkctx.h"
#include <cmath>
#include <random>
int g_mut = 0;
#define F64SOFT_C 1
#include "shaders/f64soft.inc"

static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

// Operand pairs aimed at the corners of the algorithm, plus uniform random bit patterns restricted to normal results.
static std::vector<std::pair<uint64_t, uint64_t>> makePairs(size_t perCase, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::pair<uint64_t, uint64_t>> v;
    auto mant = [&] { return rng() & 0x000FFFFFFFFFFFFFull; };
    auto mk = [&](uint64_t s, int e, uint64_t m) { return (s << 63) | ((uint64_t)(e & 0x7FF) << 52) | m; };
    auto base = [&] { return 200 + (int)(rng() % 600); };     // exponent field, leaves ample room so results stay normal
    for (size_t i = 0; i < perCase; i++) {
        int e = base(); uint64_t s = rng() & 1;
        for (int dd : {0, 1, 2, 3, 10, 52, 53, 54, 55, 62, 63, 64, 65, 100, 1000})        // every alignment regime, incl. >= 64
            for (int opp = 0; opp < 2; opp++) {
                uint64_t a = mk(s, e, mant()), b = mk(opp ? 1 - s : s, e - dd, mant());
                v.push_back({a, b}); v.push_back({b, a});
            }
        // exact ties: operand b has the guard bit pattern 100..0 after alignment
        for (int dd : {1, 2, 11, 53}) {
            uint64_t a = mk(s, e, mant() & ~1ull), b = mk(s, e - dd, 0);
            v.push_back({a, b}); v.push_back({mk(s, e, mant() | 1ull), b});
        }
        // mantissa round-up overflow: all-ones mantissa plus a small value
        v.push_back({mk(s, e, 0x000FFFFFFFFFFFFFull), mk(s, e - 53, mant())});
        v.push_back({mk(s, e, 0x000FFFFFFFFFFFFFull), mk(s, e - 1, 0x000FFFFFFFFFFFFFull)});
        // x + (-x), near-cancellation, and zeros of both signs
        uint64_t x = mk(s, e, mant());
        v.push_back({x, x ^ (1ull << 63)});
        v.push_back({x, (x ^ (1ull << 63)) + 1});     // one ulp apart: cancels to a power of two or less
        v.push_back({x, 0}); v.push_back({0, x}); v.push_back({x, 1ull << 63}); v.push_back({1ull << 63, x});
    }
    v.push_back({0, 0}); v.push_back({1ull << 63, 1ull << 63}); v.push_back({0, 1ull << 63}); v.push_back({1ull << 63, 0});
    for (size_t i = 0; i < perCase * 40; i++) v.push_back({mk(rng() & 1, base(), mant()), mk(rng() & 1, base(), mant())});   // uniform
    return v;
}

static size_t checkPairs(const std::vector<std::pair<uint64_t, uint64_t>>& v, size_t* firstBad = nullptr) {
    size_t bad = 0;
    for (size_t i = 0; i < v.size(); i++) {
        double h = dbl(v[i].first) + dbl(v[i].second);
        if (f64_add(v[i].first, v[i].second) != bits(h)) { if (!bad && firstBad) *firstBad = i; bad++; }
    }
    return bad;
}

// DoubleDoubleSummation sequences: the integer implementation against the hardware-double one used by POC 2.
struct DDh { double sum = 0, addend = 0;
    void add(double x) { { double s = x + addend, z = s - x, t = addend - z; x = s; addend = t; }
                         { double s = sum + x, ap = s - x, bp = s - ap, da = sum - ap, db = x - bp; sum = s; x = da + db; } addend += x; } };
static size_t checkDds(size_t seqs, size_t len) {
    std::mt19937_64 rng(5); std::normal_distribution<double> nd(0, 1); size_t bad = 0;
    for (size_t k = 0; k < seqs; k++) {
        DDh h; uint64_t s = 0, a = 0;
        for (size_t i = 0; i < len; i++) {
            double x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
            h.add(x); ddsAdd(s, a, bits(x));
        }
        bad += (s != bits(h.sum)) || (a != bits(h.addend));
    }
    return bad;
}

static int host() {
    auto pairs = makePairs(20000, 1);
    size_t fb = 0, bad = checkPairs(pairs, &fb);
    printf("host: f64_add vs hardware double on %zu pairs: %s (%zu differ)\n", pairs.size(), bad ? "FAIL" : "PASS", bad);
    if (bad) printf("  first mismatch: %016llx + %016llx -> %016llx, expected %016llx\n", (unsigned long long)pairs[fb].first, (unsigned long long)pairs[fb].second,
                    (unsigned long long)f64_add(pairs[fb].first, pairs[fb].second), (unsigned long long)bits(dbl(pairs[fb].first) + dbl(pairs[fb].second)));
    size_t dbad = checkDds(200, 20000);
    printf("host: DoubleDoubleSummation, integer vs hardware, 200 sequences x 20000 values (sum AND addend bit for bit): %s (%zu differ)\n", dbad ? "FAIL" : "PASS", dbad);
    int rc = bad || dbad;
    auto few = makePairs(2000, 9);
    const char* names[] = {"", "drop the sticky bit", "round half up", "ties always down"};
    for (int m = 1; m <= 3; m++) {
        g_mut = m; size_t mb = checkPairs(few); g_mut = 0;
        printf("  mutant %d (%s): %s (%zu of %zu pairs differ)\n", m, names[m], mb ? "caught" : "NOT CAUGHT", mb, few.size());
        rc |= mb == 0;
    }
    return rc;
}

static int device() {
    gpu::Ctx ctx;
    if (!ctx.init(gpu::defaultLoaderPaths())) { printf("no GPU: %s\n", ctx.error.c_str()); return 2; }
    auto pairs = makePairs(8000, 3);
    const uint32_t n = (uint32_t)pairs.size();
    printf("device=%s memory=%s pairs=%u\n", ctx.props.deviceName, ctx.memMode(), n);
    auto k = ctx.makeKernel("build/shaders/softadd_probe.spv", 2, 4);
    auto A = ctx.alloc((size_t)n * 16), R = ctx.alloc((size_t)n * 8);
    memcpy(A.ptr, pairs.data(), (size_t)n * 16);
    ctx.begin(); ctx.upload(A); ctx.dispatch(k, {&A, &R}, &n, (n + 63) / 64); ctx.download(R); ctx.submitWait();
    size_t bad = 0, first = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t want = bits(dbl(pairs[i].first) + dbl(pairs[i].second));
        if (((uint64_t*)R.ptr)[i] != want) { if (!bad) first = i; bad++; }
    }
    printf("device: f64_add vs hardware double: %s (%zu of %u differ)\n", bad ? "FAIL" : "PASS", bad, n);
    if (bad) printf("  first: %016llx + %016llx -> %016llx, expected %016llx\n", (unsigned long long)pairs[first].first, (unsigned long long)pairs[first].second,
                    (unsigned long long)((uint64_t*)R.ptr)[first], (unsigned long long)bits(dbl(pairs[first].first) + dbl(pairs[first].second)));
    ctx.release(A); ctx.release(R); ctx.release(k); ctx.destroy();
    return bad ? 3 : 0;
}

int main(int argc, char** argv) {
    std::string m = argc > 1 ? argv[1] : "host";
    return m == "device" ? device() : host();
}
