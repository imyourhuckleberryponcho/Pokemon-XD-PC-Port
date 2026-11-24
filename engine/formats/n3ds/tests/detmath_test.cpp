#include <cmath>
#include <cstdio>
#include <vector>

#include "detmath.h"

static int DefaultRun() {
  unsigned long long state = 12345;
  auto next = [&]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) / 9007199254740992.0;
  };
  int bad = 0;
  for (int i = 0; i < 20000; i++) {
    const double a = (next() - 0.5) * 2000.0, b = (next() - 0.5) * 2000.0;
    const double u = std::fmod(a * 0.1234567891, 1.0);
    const double px = std::fabs(a) / 20.0 + 1e-3, py = b / 10.0;
    const double got[10] = {dm::sin(a), dm::cos(a), dm::atan2(a, b), dm::asin(u), dm::acos(u), dm::pow(px, py), dm::exp(py), dm::log(px), dm::cbrt(a), dm::hypot(a, b)};
    const double want[10] = {std::sin(a), std::cos(a), std::atan2(a, b), std::asin(u), std::acos(u), std::pow(px, py), std::exp(py), std::log(px), std::cbrt(a), std::hypot(a, b)};
    for (int k = 0; k < 10; k++) {
      const double err = std::fabs(got[k] - want[k]) / std::fmax(1.0, std::fabs(want[k]));
      if (!std::isfinite(got[k]) || err > 1e-9) {
        if (bad++ < 5) std::printf("detmath_test: function %d, input %g %g: dm %.17g, C library %.17g\n", k, a, b, got[k], want[k]);
      }
    }
  }
  std::printf("detmath_test: default run, 20000 pairs x 10 functions, %d outside 1e-9 of the C library\n", bad);
  return bad == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  if (argc < 3) return DefaultRun();
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 1;
  std::vector<double> in;
  double v;
  while (fread(&v, 8, 1, f) == 1) in.push_back(v);
  fclose(f);
  std::vector<double> out;
  for (size_t i = 0; i + 1 < in.size(); i += 2) {
    double a = in[i], b = in[i + 1];
    double u = std::fmax(-1.0, std::fmin(1.0, a / 8.0 - std::floor(a / 8.0) * 2.0 + b * 0.0));
    u = std::fmod(a * 0.1234567891, 1.0);
    double px = std::fabs(a) / 20.0 + 1e-3, py = b / 10.0;
    double r[20] = {dm::sin(a), dm::cos(a), dm::atan2(a, b), dm::asin(u), dm::acos(u),
                    dm::pow(px, py), dm::exp(py), dm::log(px), dm::cbrt(a), dm::hypot(a, b),
                    std::sin(a), std::cos(a), std::atan2(a, b), std::asin(u), std::acos(u), std::pow(px, py), std::exp(py), std::log(px), std::cbrt(a), std::hypot(a, b)};
    for (double x : r) out.push_back(x);
  }
  f = fopen(argv[2], "wb");
  fwrite(out.data(), 8, out.size(), f);
  fclose(f);
  return 0;
}
