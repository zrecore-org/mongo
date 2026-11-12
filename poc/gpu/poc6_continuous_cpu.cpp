#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
    std::mt19937_64 rng(7); std::normal_distribution<double> nd(0, 1);
    for (size_t n : {1u << 20, 1u << 24}) {
        std::vector<double> data(n); for (auto& x : data) x = nd(rng) * std::pow(10.0, std::uniform_real_distribution<double>(-3, 3)(rng));
        std::vector<double> ps = {0.5, 0.9, 0.99};
        int bad = 0; double tps = 0, tnth = 0;
        auto a = data; double t0 = now(); std::vector<double> r1;
        for (double p : ps) { double rk = p * (n - 1); size_t lo = floor(rk), hi = ceil(rk); std::partial_sort(a.begin(), a.begin() + hi + 1, a.end()); r1.push_back((hi - rk) * a[lo] + (rk - lo) * a[hi]); }
        tps = now() - t0;
        auto b = data; t0 = now(); std::vector<double> r2;
        for (double p : ps) { double rk = p * (n - 1); size_t lo = floor(rk), hi = ceil(rk);
            std::nth_element(b.begin(), b.begin() + hi, b.end()); double vh = b[hi];
            double vl = lo == hi ? vh : *std::max_element(b.begin(), b.begin() + hi);
            r2.push_back((hi - rk) * vl + (rk - lo) * vh); }
        tnth = now() - t0;
        for (int i = 0; i < 3; i++) bad += r1[i] != r2[i];
        printf("n=%zu partial_sort x3 %.1f ms | nth_element+max_element x3 %.1f ms (%.1fx) | mismatches %d\n", n, tps, tnth, tps / tnth, bad);
    }
}
