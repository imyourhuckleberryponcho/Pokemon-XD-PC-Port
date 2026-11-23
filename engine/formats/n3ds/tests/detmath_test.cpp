#include <cmath>
#include <cstdio>
#include <vector>

#include "detmath.h"

int main(int argc, char** argv) {
  if (argc < 3) return 2;
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
