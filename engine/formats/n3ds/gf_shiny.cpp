#include <algorithm>
#include <cmath>

#include "gf_internal.h"

namespace n3ds {
namespace gfi {

namespace {

struct Sample {
  uint8_t x[4], y[3];
  float w;
};

std::vector<Sample> collect(const std::vector<pbr::Rgba>& normal, const std::vector<pbr::Rgba>& shiny, const std::vector<double>& wt) {
  std::vector<Sample> out;
  const size_t n = std::min(normal.size(), shiny.size());
  std::vector<size_t> cnt(n, 0);
  size_t total = 0;
  for (size_t i = 0; i < n; i++) {
    if (normal[i].w != shiny[i].w || normal[i].h != shiny[i].h || i >= wt.size() || wt[i] <= 0) continue;
    const size_t px = normal[i].px.size() / 4;
    for (size_t p = 0; p < px; p++) if (normal[i].px[p * 4 + 3] >= 128 && shiny[i].px[p * 4 + 3] >= 128) cnt[i]++;
    total += cnt[i];
  }
  const size_t stride = std::max<size_t>(1, (total + 79999) / 80000);
  for (size_t i = 0; i < n; i++) {
    if (!cnt[i]) continue;
    const double w = wt[i] / (double)cnt[i];
    size_t k = 0;
    const size_t px = normal[i].px.size() / 4;
    for (size_t p = 0; p < px; p++) {
      if (!(normal[i].px[p * 4 + 3] >= 128 && shiny[i].px[p * 4 + 3] >= 128)) continue;
      if (k++ % stride) continue;
      Sample s;
      for (int c = 0; c < 3; c++) { s.x[c] = normal[i].px[p * 4 + c]; s.y[c] = shiny[i].px[p * 4 + c]; }
      s.x[3] = normal[i].px[p * 4 + 3];
      s.w = (float)(w * (double)stride);
      out.push_back(s);
    }
  }
  return out;
}

inline double chan_err(const std::vector<Sample>& S, int src, int c, int mod, double cap = 0) {
  const double g = mod / 127.5;
  const double cap2 = cap * cap;
  double e = 0;
  for (const Sample& s : S) {
    double v = std::min(255.0, s.x[src] * g);
    double d = v - s.y[c];
    double q = d * d;
    e += s.w * (cap > 0 && q > cap2 ? cap2 : q);
  }
  return e;
}
constexpr double kCap = 45.0;

}

double shiny_error(const std::vector<pbr::Rgba>& normal, const std::vector<pbr::Rgba>& shiny, const std::vector<double>& wt, const int swap[3], const int mod[3]) {
  auto S = collect(normal, shiny, wt);
  double tw = 0, e = 0;
  for (auto& s : S) tw += s.w;
  if (tw <= 0) return 0;
  for (int c = 0; c < 3; c++) e += chan_err(S, swap[c], c, mod[c]);
  return std::sqrt(e / (3 * tw));
}

ShinyFit fit_shiny(const std::vector<pbr::Rgba>& normal, const std::vector<pbr::Rgba>& shiny, const std::vector<double>& wt) {
  ShinyFit f;
  auto S = collect(normal, shiny, wt);
  double tw = 0;
  for (auto& s : S) tw += s.w;
  if (tw <= 0) return f;
  f.weight = tw;
  const int id[3] = {0, 1, 2}, m0[3] = {127, 127, 127};
  double e0 = 0;
  for (int c = 0; c < 3; c++) e0 += chan_err(S, id[c], c, m0[c]);
  f.err_none = std::sqrt(e0 / (3 * tw));
  double eb = 0;
  for (int c = 0; c < 3; c++) {
    double best = 1e300;
    int bs = c, bm = 127;
    for (int src = 0; src < 3; src++) {
      int lm = 127;
      double le = 1e300;
      for (int m = 0; m <= 255; m += 5) {
        double e = chan_err(S, src, c, m, kCap);
        if (e < le) { le = e; lm = m; }
      }
      for (int m = std::max(0, lm - 4); m <= std::min(255, lm + 4); m++) {
        double e = chan_err(S, src, c, m, kCap);
        if (e < le) { le = e; lm = m; }
      }
      if (le < best - 1e-9 * tw || (src == c && le <= best + 1e-9 * tw)) { best = le; bs = src; bm = lm; }
    }
    if (bs == c && std::abs(bm - 127) <= 2) bm = 127;
    f.swap[c] = bs;
    f.mod[c] = bm;
    eb += chan_err(S, bs, c, bm);
  }
  f.err_fit = std::sqrt(eb / (3 * tw));
  double ok = 0;
  for (const Sample& s : S) {
    bool good = true;
    for (int c = 0; c < 3; c++) {
      double v = std::min(255.0, s.x[f.swap[c]] * (f.mod[c] / 127.5));
      if (std::abs(v - s.y[c]) > 32) good = false;
    }
    if (good) ok += s.w;
  }
  f.cover = ok / tw;
  return f;
}

}
}
