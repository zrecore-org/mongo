// genpoints <N> <out.txt>: N random points in [-2.5,2.5]^3, fixed-seed mt19937_64 (seed 12345), uniform_real_distribution<double>.
#include <random>
#include <cstdio>
#include <cstdlib>
int main(int c, char** v) { size_t n = atol(v[1]); FILE* f = fopen(v[2], "w"); std::mt19937_64 g(12345); std::uniform_real_distribution<double> u(-2.5, 2.5);
  for (size_t i = 0; i < n; i++) { double x = u(g), y = u(g), z = u(g); fprintf(f, "%.17g %.17g %.17g\n", x, y, z); } fclose(f); }
