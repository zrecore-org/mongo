// Cost of assembling a batch: gather scattered ~1000-double blocks into one contiguous buffer.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>
int main() {
    std::mt19937_64 rng(1);
    for (size_t blocks : {64, 256, 1024, 4096, 16384}) {
        const size_t per = 1000;
        std::vector<std::vector<double>> bs(blocks, std::vector<double>(per, 1.5));
        std::vector<double*> order; for (auto& b : bs) order.push_back(b.data());
        std::shuffle(order.begin(), order.end(), rng);
        std::vector<double> dst(blocks * per);
        double best = 1e9;
        for (int r = 0; r < 20; r++) {
            auto t0 = std::chrono::steady_clock::now();
            size_t o = 0; for (double* p : order) { memcpy(&dst[o], p, per * 8); o += per; }
            auto t1 = std::chrono::steady_clock::now();
            best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        printf("blocks=%6zu n=%9zu gather=%.4f ms (%.3f ns/elt)\n", blocks, blocks * per, best, best * 1e6 / (blocks * per));
    }
}
