#include <algorithm>
#include <cmath>
#include <cstring>

#include "detmath.h"
#include "gf_internal.h"
#include "n3ds_pose.h"
#include "pbr_hsd_read.h"
#include "pbr_to_xd.h"

namespace n3ds {
namespace gfi {

namespace {

inline uint8_t u8(double x) { return (uint8_t)(int)x; }
inline double clip(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double pymod(double a, double b) {
  double r = std::fmod(a, b);
  if (r != 0.0 && ((r < 0) != (b < 0))) r += b;
  return r;
}
bool contains(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }

using V3 = std::array<double, 3>;

struct MOpt {
  double strength = 1.0, strength_l = 0.3, strength_c = 0.9, strength_h = 0.5;
  int k = 5;
  double skip_de = 12.0, max_dl = 12.0, min_gain = 0.75, max_gain = 1.7, max_dh = 15.0, max_match = 50.0, sigma = 12.0, min_share = 0.03, share_weight = 12.0,
         max_hue_gap = 45.0, min_piece = 0.02;
  int sample = 6000;
};

double hue_of(double a, double b) { return dm::atan2(b, a) * 57.29577951308232; }
double wrap(double d) { return pymod(d + 180.0, 360.0) - 180.0; }

struct Pool {
  std::vector<V3> lab;
  std::vector<double> w;
  void add(const std::vector<V3>& rgb, double share) {
    if (rgb.empty() || !(share > 0)) return;
    double each = share / (double)rgb.size();
    for (auto& p : rgb) {
      V3 l;
      rgb_to_lab(p.data(), l.data());
      lab.push_back(l);
      w.push_back(each);
    }
  }
  bool ok() const { return !lab.empty(); }
};

V3 weighted_mean(const std::vector<V3>& lab, const std::vector<double>& w) {
  V3 s = {0, 0, 0};
  for (size_t i = 0; i < lab.size(); i++)
    for (int c = 0; c < 3; c++) s[c] = (i == 0) ? lab[i][c] * w[i] : s[c] + lab[i][c] * w[i];
  double ws = np_sum(w.data(), w.size());
  return {s[0] / ws, s[1] / ws, s[2] / ws};
}

double d3(const V3& a, const V3& b) {
  double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return (x * x + y * y) + z * z;
}

void kmeans(const std::vector<V3>& lab, std::vector<double> w, int k, std::vector<V3>* cen_out, std::vector<double>* share_out, int iters = 14) {
  size_t N = lab.size();
  double ws = np_sum(w.data(), w.size());
  for (auto& x : w) x = x / ws;
  std::vector<V3> cen;
  {
    V3 s = {0, 0, 0};
    for (size_t i = 0; i < N; i++)
      for (int c = 0; c < 3; c++) s[c] = (i == 0) ? lab[i][c] * w[i] : s[c] + lab[i][c] * w[i];
    cen.push_back(s);
  }
  std::vector<double> d(N);
  for (size_t i = 0; i < N; i++) d[i] = d3(lab[i], cen[0]);
  for (int q = 1; q < k; q++) {
    size_t best = 0;
    double bv = -1e300;
    for (size_t i = 0; i < N; i++) {
      double v = d[i] * (w[i] > 0 ? 1.0 : 0.0);
      if (v > bv) { bv = v; best = i; }
    }
    cen.push_back(lab[best]);
    for (size_t i = 0; i < N; i++) d[i] = std::min(d[i], d3(lab[i], cen.back()));
  }
  std::vector<int> a(N);
  auto assign = [&]() {
    for (size_t i = 0; i < N; i++) {
      int bj = 0;
      double bd = d3(lab[i], cen[0]);
      for (size_t j = 1; j < cen.size(); j++) {
        double dd = d3(lab[i], cen[j]);
        if (dd < bd) { bd = dd; bj = (int)j; }
      }
      a[i] = bj;
    }
  };
  for (int it = 0; it < iters; it++) {
    assign();
    for (size_t j = 0; j < cen.size(); j++) {
      std::vector<double> wm;
      std::vector<size_t> idx;
      for (size_t i = 0; i < N; i++) if (a[i] == (int)j) { wm.push_back(w[i]); idx.push_back(i); }
      if (idx.empty()) continue;
      double wsum = np_sum(wm.data(), wm.size());
      if (!(wsum > 0)) continue;
      V3 s = {0, 0, 0};
      for (size_t q = 0; q < idx.size(); q++)
        for (int c = 0; c < 3; c++) s[c] = (q == 0) ? lab[idx[q]][c] * wm[q] : s[c] + lab[idx[q]][c] * wm[q];
      cen[j] = {s[0] / wsum, s[1] / wsum, s[2] / wsum};
    }
  }
  assign();
  for (size_t j = 0; j < cen.size(); j++) {
    std::vector<double> wm;
    for (size_t i = 0; i < N; i++) if (a[i] == (int)j) wm.push_back(w[i]);
    double sh = np_sum(wm.data(), wm.size());
    if (sh > 0) { cen_out->push_back(cen[j]); share_out->push_back(sh); }
  }
}

std::vector<V3> sample_pixels(const pbr::Rgba& p, int n) {
  std::vector<size_t> opaque;
  size_t cnt = p.px.size() / 4;
  for (size_t i = 0; i < cnt; i++) if (p.px[i * 4 + 3] > 128) opaque.push_back(i);
  std::vector<V3> out;
  if ((long long)opaque.size() > n) {
    double stop = (double)(opaque.size() - 1), step = stop / (double)(n - 1);
    for (int i = 0; i < n; i++) {
      double y = (i == n - 1) ? stop : (double)i * step;
      out.push_back({(double)p.px[opaque[(size_t)(long long)y] * 4], (double)p.px[opaque[(size_t)(long long)y] * 4 + 1], (double)p.px[opaque[(size_t)(long long)y] * 4 + 2]});
    }
  } else {
    for (size_t i : opaque) out.push_back({(double)p.px[i * 4], (double)p.px[i * 4 + 1], (double)p.px[i * 4 + 2]});
  }
  return out;
}

double frac_alpha_below(const pbr::Rgba& p, int thr) {
  size_t c = 0, n = p.px.size() / 4;
  for (size_t i = 0; i < n; i++) if (p.px[i * 4 + 3] < thr) c++;
  return n ? (double)c / (double)n : 0.0;
}

struct PlanInfo {
  double de_before = 0, de_after = 0;
  std::string why;
  bool measured = false;
};
std::unique_ptr<MatchPlan> make_plan(const Pool& us, const Pool& xd, const MOpt& o, PlanInfo* info) {
  if (!us.ok() || !xd.ok()) return nullptr;
  V3 mu = weighted_mean(us.lab, us.w), mx = weighted_mean(xd.lab, xd.w);
  double de0 = std::sqrt(d3(mu, mx));
  info->de_before = de0;
  info->measured = true;
  if (de0 < o.skip_de) { info->why = "close already"; return nullptr; }
  std::vector<V3> uc, xc;
  std::vector<double> ush, xsh;
  kmeans(us.lab, us.w, o.k, &uc, &ush);
  kmeans(xd.lab, xd.w, o.k, &xc, &xsh);
  {
    std::vector<V3> c2;
    std::vector<double> s2;
    for (size_t i = 0; i < xc.size(); i++) if (xsh[i] >= 0.04) { c2.push_back(xc[i]); s2.push_back(xsh[i]); }
    xc = c2;
    xsh = s2;
  }
  size_t n = uc.size();
  std::unique_ptr<MatchPlan> plan(new MatchPlan());
  plan->dl.assign(n, 0.0);
  plan->gain.assign(n, 1.0);
  plan->dh.assign(n, 0.0);
  plan->cen = uc;
  plan->sigma = o.sigma;
  double s = o.strength;
  double sl = s * o.strength_l, sc = s * o.strength_c, sh = s * o.strength_h;
  for (size_t j = 0; j < n; j++) {
    if (ush[j] < o.min_share) continue;
    size_t m = xc.size();
    std::vector<double> d(m), cost(m);
    for (size_t i = 0; i < m; i++) {
      double a1 = xc[i][1] - uc[j][1], a2 = xc[i][2] - uc[j][2], a0 = 0.6 * (xc[i][0] - uc[j][0]);
      d[i] = std::sqrt((a1 * a1 + a2 * a2) + a0 * a0);
      cost[i] = d[i] + o.share_weight * std::fabs(dm::log(std::max(xsh[i], 1e-3) / ush[j]));
    }
    double cu_ = dm::hypot(uc[j][1], uc[j][2]);
    if (cu_ >= 15.0) {
      for (size_t i = 0; i < m; i++) {
        double xh = dm::hypot(xc[i][1], xc[i][2]);
        double dh_ = std::fabs(wrap(hue_of(xc[i][1], xc[i][2]) - hue_of(uc[j][1], uc[j][2])));
        if (xh >= 15.0 && dh_ > o.max_hue_gap) cost[i] = 1e9;
      }
    }
    size_t bi = 0;
    for (size_t i = 1; i < m; i++) if (cost[i] < cost[bi]) bi = i;
    if (m == 0 || cost[bi] >= 1e9 || d[bi] > o.max_match) continue;
    plan->dl[j] = clip(xc[bi][0] - uc[j][0], -o.max_dl, o.max_dl) * sl;
    double cu = dm::hypot(uc[j][1], uc[j][2]), ct = dm::hypot(xc[bi][1], xc[bi][2]);
    if (cu >= 6.0) {
      plan->gain[j] = 1.0 + (clip(ct / cu, o.min_gain, o.max_gain) - 1.0) * sc;
      if (cu >= 10.0 && ct >= 10.0)
        plan->dh[j] = clip(wrap(hue_of(xc[bi][1], xc[bi][2]) - hue_of(uc[j][1], uc[j][2])), -o.max_dh, o.max_dh) * sh;
    }
  }
  if (getenv("N3DS_DBG_MATCH"))
    for (size_t j = 0; j < n; j++)
      fprintf(stderr, "  us group %zu: %.2f %.2f %.2f share %.4f dl %.2f gain %.3f dh %.2f\n", j, uc[j][0], uc[j][1], uc[j][2], ush[j], plan->dl[j], plan->gain[j], plan->dh[j]);
  size_t step = std::max<size_t>(1, us.lab.size() / 4000);
  pbr::Rgba probe;
  {
    std::vector<size_t> idx;
    for (size_t i = 0; i < us.lab.size(); i += step) idx.push_back(i);
    probe = pbr::Rgba((int)idx.size(), 1);
    for (size_t q = 0; q < idx.size(); q++) {
      double rgb[3];
      lab_to_rgb(us.lab[idx[q]].data(), rgb);
      for (int c = 0; c < 3; c++) probe.px[q * 4 + c] = u8(clip(rgb[c], 0, 255));
      probe.px[q * 4 + 3] = 255;
    }
  }
  pbr::Rgba moved = plan->apply(probe, false);
  if (getenv("N3DS_DBG_MATCH")) {
    double si[3] = {0, 0, 0}, so[3] = {0, 0, 0};
    for (size_t q = 0; q < probe.px.size() / 4; q++) for (int c = 0; c < 3; c++) { si[c] += probe.px[q * 4 + c]; so[c] += moved.px[q * 4 + c]; }
    fprintf(stderr, "probe in %.0f %.0f %.0f out %.0f %.0f %.0f n %zu\n", si[0], si[1], si[2], so[0], so[1], so[2], probe.px.size() / 4);
  }
  std::vector<V3> ml;
  std::vector<double> wsub;
  for (size_t q = 0, i = 0; i < us.lab.size(); i += step, q++) {
    double rgb[3] = {(double)moved.px[q * 4], (double)moved.px[q * 4 + 1], (double)moved.px[q * 4 + 2]}, l[3];
    rgb_to_lab(rgb, l);
    ml.push_back({l[0], l[1], l[2]});
    wsub.push_back(us.w[i]);
  }
  V3 mm = weighted_mean(ml, wsub);
  info->de_after = pbr::py_round(std::sqrt(d3(mm, mx)), 1);
  if (info->de_after >= de0) { info->why = "no gain"; return nullptr; }
  return plan;
}

std::vector<std::pair<std::string, double>> usum_areas(const GfModel& gm) {
  std::vector<std::pair<std::string, double>> out;
  for (auto& me : gm.meshes)
    for (auto& sm : me.subs) {
      bool known = false;
      for (auto& mt : gm.materials) if (mt.name == sm.name) { known = true; break; }
      if (!known) continue;
      auto tr = triangles(sm);
      if (tr.empty()) continue;
      const Arr& P = sm.verts.get("pos");
      std::vector<double> nrm;
      for (auto& t : tr) {
        double u[3], w[3];
        for (int k = 0; k < 3; k++) { u[k] = P.at((int)t[1], k) - P.at((int)t[0], k); w[k] = P.at((int)t[2], k) - P.at((int)t[0], k); }
        double c0 = u[1] * w[2] - u[2] * w[1], c1 = u[2] * w[0] - u[0] * w[2], c2 = u[0] * w[1] - u[1] * w[0];
        nrm.push_back(std::sqrt((c0 * c0 + c1 * c1) + c2 * c2));
      }
      double a = np_sum(nrm.data(), nrm.size()) / 2.0;
      bool found = false;
      for (auto& p : out) if (p.first == sm.name) { p.second = p.second + a; found = true; break; }
      if (!found) out.push_back({sm.name, 0.0 + a});
    }
  return out;
}

std::optional<pbr::Rgba> xd_picture(const pbr::Model& mod, const pbr::Texture& tx) {
  const pbr::Image& im = mod.images[*tx.image];
  if (im.format == 8 || im.format == 9) {
    if (!tx.tlut) return std::nullopt;
    const pbr::Tlut& tl = mod.tluts[*tx.tlut];
    int n = (int)tl.data.size() / 2;
    int hh = ((n + 15) / 16) * 4;
    std::vector<uint8_t> pd((size_t)hh * 8, 0);
    memcpy(pd.data(), tl.data.data(), (size_t)n * 2);
    int pf = tl.format == 0 ? 3 : tl.format == 1 ? 4 : 5;
    pbr::Rgba pal = pbr::gx_decode(pf, 4, hh, pd.data(), pd.size());
    int npal = (int)(pal.px.size() / 4);
    pbr::Rgba raw = pbr::gx_decode(im.format == 8 ? 0 : 1, im.width, im.height, im.data.data(), im.data.size());
    pbr::Rgba out(im.width, im.height);
    for (size_t i = 0; i < (size_t)im.width * im.height; i++) {
      int r = raw.px[i * 4];
      int idx = im.format == 8 ? r / 17 : r;
      idx = std::min(idx, npal - 1);
      memcpy(&out.px[i * 4], &pal.px[(size_t)idx * 4], 4);
    }
    return out;
  }
  if (im.format == 0 || im.format == 1) return std::nullopt;
  int bw, bh;
  if (!pbr::gx_tile(im.format, &bw, &bh)) return std::nullopt;
  return pbr::gx_decode(im.format, im.width, im.height, im.data.data(), im.data.size());
}

Pool xd_colour_pool(const std::vector<uint8_t>& xd_bytes, const MOpt& o) {
  std::vector<std::pair<int, double>> area;
  auto add = [&](int mi, double a) {
    for (auto& p : area) if (p.first == mi) { p.second = p.second + a; return; }
    area.push_back({mi, 0.0 + a});
  };
  pbr::ReadResult rr;
  bool meshes_ok = true;
  try {
    rr = pbr::hsd_read_pkx(xd_bytes, true);
    for (auto& me : rr.model.meshes) {
      size_t nt = me.tris.size();
      if (!nt || !me.material) continue;
      std::vector<double> nrm;
      for (auto& t : me.tris) {
        const auto& p0 = me.verts[t[0]].pos;
        const auto& p1 = me.verts[t[1]].pos;
        const auto& p2 = me.verts[t[2]].pos;
        double u[3], w[3];
        for (int k = 0; k < 3; k++) { u[k] = p1[k] - p0[k]; w[k] = p2[k] - p0[k]; }
        double c0 = u[1] * w[2] - u[2] * w[1], c1 = u[2] * w[0] - u[0] * w[2], c2 = u[0] * w[1] - u[1] * w[0];
        nrm.push_back(std::sqrt((c0 * c0 + c1 * c1) + c2 * c2));
      }
      add(*me.material, np_sum(nrm.data(), nrm.size()) / 2.0);
    }
  } catch (const std::exception&) {
    meshes_ok = false;
  }
  if (!meshes_ok) {
    area.clear();
    rr = pbr::hsd_read_pkx(xd_bytes, false);
    for (size_t mi = 0; mi < rr.model.materials.size(); mi++) {
      const auto& ma = rr.model.materials[mi];
      if (!ma.textures.empty() && ma.textures[0].image) {
        const auto& im = rr.model.images[*ma.textures[0].image];
        area.push_back({(int)mi, (double)im.width * (double)im.height});
      }
    }
  }
  const pbr::Model& mod = rr.model;
  std::vector<std::pair<int, double>> keep;
  for (auto& p : area) {
    const auto& ma = mod.materials[p.first];
    if (!ma.textures.empty() && ma.textures[0].image) keep.push_back(p);
  }
  std::vector<double> vals;
  for (auto& p : keep) vals.push_back(p.second);
  double tot = py_sum(vals);
  if (tot == 0.0) tot = 1.0;
  Pool pool;
  for (auto& p : keep) {
    const auto& ma = mod.materials[p.first];
    double a = p.second;
    if (a / tot < o.min_piece) continue;
    auto rgba = xd_picture(mod, ma.textures[0]);
    if (!rgba || frac_alpha_below(*rgba, 128) > 0.5 || rgba->w * rgba->h < 64) continue;
    std::vector<V3> rgb = sample_pixels(*rgba, o.sample);
    for (auto& px : rgb)
      for (int c = 0; c < 3; c++) px[c] = px[c] * (ma.diffuse[c] / 255.0);
    pool.add(rgb, a / tot);
  }
  return pool;
}

Pool usum_colour_pool(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const Opts& opts, const MOpt& o, int nat) {
  auto area = usum_areas(gm);
  std::vector<double> vals;
  for (auto& p : area) vals.push_back(p.second);
  double tot = py_sum(vals);
  if (tot == 0.0) tot = 1.0;
  Pool pool;
  for (auto& mt : gm.materials) {
    const GfCoord* co = colour_coord(mt);
    std::vector<int> fixes = opts.recolour ? recolour_fixes_for(nat, mt.name) : std::vector<int>();
    double a = 0.0;
    for (auto& p : area) if (p.first == mt.name) a = p.second;
    a = a / tot;
    if (!co || !texs.count(co->name) || contains(mt.name, "Eye") || !match_piece_ok(mt, fixes) || a < o.min_piece) continue;
    const pbr::Rgba& rgba = texs.at(co->name);
    if (frac_alpha_below(rgba, 128) > 0.5) continue;
    pbr::Rgba lt = light_tweak(rgba, opts.light);
    pool.add(sample_pixels(lt, o.sample), a);
  }
  return pool;
}

}

double py_sum(const std::vector<double>& v) {
  double f = 0.0, c = 0.0;
  for (double x : v) {
    double t = f + x;
    if (std::fabs(f) >= std::fabs(x)) c += (f - t) + x;
    else c += (x - t) + f;
    f = t;
  }
  if (c != 0.0 && std::isfinite(c)) f += c;
  return f;
}

pbr::Rgba MatchPlan::apply(const pbr::Rgba& in, bool skin_only) const {
  pbr::Rgba out = in;
  size_t n = cen.size();
  size_t np = in.px.size() / 4;
  const double two_s2 = 2.0 * (sigma * sigma);
  std::vector<double> d2(n), wt(n);
  for (size_t p = 0; p < np; p++) {
    double f[3] = {(double)in.px[p * 4], (double)in.px[p * 4 + 1], (double)in.px[p * 4 + 2]}, lab[3];
    rgb_to_lab(f, lab);
    double mn = 1e300;
    for (size_t j = 0; j < n; j++) {
      double x = lab[0] - cen[j][0], y = lab[1] - cen[j][1], z = lab[2] - cen[j][2];
      d2[j] = (x * x + y * y) + z * z;
      mn = std::min(mn, d2[j]);
    }
    double wsum = 0;
    for (size_t j = 0; j < n; j++) {
      wt[j] = dm::exp(-(d2[j] - mn) / two_s2);
      wsum = j == 0 ? wt[j] : wsum + wt[j];
    }
    double sdl = 0, sg = 0, sh = 0;
    for (size_t j = 0; j < n; j++) {
      double w = wt[j] / wsum;
      sdl = j == 0 ? w * dl[j] : sdl + w * dl[j];
      sg = j == 0 ? w * gain[j] : sg + w * gain[j];
      sh = j == 0 ? w * dh[j] : sh + w * dh[j];
    }
    double L = clip(lab[0] + sdl, 0.0, 100.0);
    double a = lab[1], b = lab[2];
    double c = dm::hypot(a, b) * sg;
    double h = (hue_of(a, b) + sh) * 0.017453292519943295;
    double l3[3] = {L, c * dm::cos(h), c * dm::sin(h)}, nw[3];
    lab_to_rgb(l3, nw);
    double wskin = 1.0;
    if (skin_only) {
      double chroma = dm::hypot(a, b);
      wskin = clip((chroma - 8.0) / 8.0, 0.0, 1.0) * clip((lab[0] - 30.0) / 10.0, 0.0, 1.0);
    }
    for (int k = 0; k < 3; k++) {
      double v = skin_only ? f[k] + (nw[k] - f[k]) * wskin : nw[k];
      out.px[p * 4 + k] = u8(v + 0.5);
    }
  }
  return out;
}

bool hand_match_enabled(int nat) {
  static const int OFF[] = {28, 109, 178, 213, 216, 237, 250, 256, 258, 293, 308, 687, 639, 883, 874, 316};
  for (int x : OFF) if (x == nat) return false;
  return true;
}
bool hand_plates_follow(int nat, const std::string& name) {
  return nat == 25 && (contains(name, "Mouth") || contains(name, "BodyB"));
}
double hand_glow_strength(int nat) { return nat == 197 ? 0.35 : 1.0; }

bool match_piece_ok(const GfMaterial& mt, const std::vector<int>& fixes) {
  const std::string& n = mt.name;
  static const char* SKIP[4] = {"Iris", "Mouth", "Line", "Tongue"};
  bool skip = false;
  for (auto k : SKIP) if (contains(n, k)) skip = true;
  return fixes.empty() && !skip && !contains(mt.shader, "Neon") && !contains(mt.shader, "Fire") && !contains(n, "Fire") && !is_effect_layer(mt);
}

std::unique_ptr<MatchPlan> colour_match_plan(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const std::vector<uint8_t>& xd_bytes, const Opts& opts,
                                             int nat, const Logger& log) {
  try {
    MOpt o;
    Pool xp = xd_colour_pool(xd_bytes, o);
    Pool up = usum_colour_pool(gm, texs, opts, o, nat);
    PlanInfo info;
    auto plan = make_plan(up, xp, o, &info);
    if (getenv("N3DS_DBG_MATCH")) {
      V3 mu = up.ok() ? weighted_mean(up.lab, up.w) : V3{0, 0, 0}, mx = xp.ok() ? weighted_mean(xp.lab, xp.w) : V3{0, 0, 0};
      fprintf(stderr, "match: us %zu px, xd %zu px, mean us %.4f %.4f %.4f, xd %.4f %.4f %.4f, de %.3f..%.3f\n", up.lab.size(), xp.lab.size(), mu[0], mu[1], mu[2], mx[0], mx[1], mx[2],
              info.de_before, info.de_after);
      if (plan) for (size_t j = 0; j < plan->cen.size(); j++)
        fprintf(stderr, "  group %zu us %.2f %.2f %.2f dl %.2f gain %.3f dh %.2f\n", j, plan->cen[j][0], plan->cen[j][1], plan->cen[j][2], plan->dl[j], plan->gain[j], plan->dh[j]);
    }
    char buf[256];
    if (!(up.ok() && xp.ok())) {
      snprintf(buf, sizeof buf, "INFO: colour match: no pictures to measure (XD side %s, 3DS side %s)", xp.ok() ? "True" : "False", up.ok() ? "True" : "False");
      log(buf);
    } else if (!plan) {
      snprintf(buf, sizeof buf, "INFO: colour match: left alone (dE %.1f, %s)", info.de_before, info.why.c_str());
      log(buf);
    } else {
      snprintf(buf, sizeof buf, "INFO: colour match: texture dE %.1f -> %.1f", info.de_before, info.de_after);
      log(buf);
    }
    return plan;
  } catch (const std::exception& ex) {
    log(std::string("INFO: colour match skipped: ") + ex.what());
    return nullptr;
  }
}

bool is_fire_material(const GfMaterial& mt) { return contains(mt.shader, "Fire") || contains(mt.name, "Fire"); }
bool is_lure_glow(const GfMaterial& mt) { return contains(mt.shader, "chonchie_Glow"); }
int species_of(const GfModel& gm) {
  if (gm.bones.empty()) return 0;
  const std::string& nm = gm.bones[0].name;
  if (nm.size() >= 6 && nm.compare(0, 2, "pm") == 0 && isdigit((unsigned char)nm[2]) && isdigit((unsigned char)nm[3]) && isdigit((unsigned char)nm[4]) && isdigit((unsigned char)nm[5]))
    return atoi(nm.substr(2, 4).c_str());
  return 0;
}

pbr::Rgba fire_alpha(const pbr::Rgba& in) {
  pbr::Rgba out = in;
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double g = (double)in.px[i + 1];
    double a = clip((g - 55.0) / 135.0, 0.0, 1.0);
    out.px[i + 3] = u8(std::sqrt(a) * 255.0);
  }
  return out;
}

pbr::Rgba lure_fill(const pbr::Rgba& in) {
  pbr::Rgba out = in;
  for (size_t i = 0; i < in.px.size(); i += 4) { out.px[i] = 255; out.px[i + 1] = 250; out.px[i + 2] = 190; out.px[i + 3] = 255; }
  return out;
}

pbr::Rgba steel_tint(const pbr::Rgba& in, double k) {
  pbr::Rgba out = in;
  const double mult[3] = {1.0 - 0.10 * k, 1.0 - 0.02 * k, 1.0 + 0.12 * k};
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double f[3] = {(double)in.px[i], (double)in.px[i + 1], (double)in.px[i + 2]};
    double chroma = std::max(std::max(f[0], f[1]), f[2]) - std::min(std::min(f[0], f[1]), f[2]);
    double w = clip(1.0 - chroma / 40.0, 0.0, 1.0);
    for (int c = 0; c < 3; c++) out.px[i + c] = u8(clip(f[c] * (1.0 + (mult[c] - 1.0) * w) + 0.5, 0, 255));
  }
  return out;
}

pbr::Rgba sat_boost(const pbr::Rgba& in, double k) {
  pbr::Rgba out = in;
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double f[3] = {(double)in.px[i], (double)in.px[i + 1], (double)in.px[i + 2]};
    double lum = (f[0] * 0.30 + f[1] * 0.59) + f[2] * 0.11;
    for (int c = 0; c < 3; c++) out.px[i + c] = u8(clip(lum + (f[c] - lum) * (1.0 + k) + 0.5, 0, 255));
  }
  return out;
}

pbr::Rgba cheek_boost(const pbr::Rgba& in, double k) {
  size_t n = in.px.size() / 4;
  std::map<int, long long> counts;
  for (size_t i = 0; i < n; i++) {
    if (in.px[i * 4 + 3] <= 128) continue;
    int q0 = in.px[i * 4] / 8, q1 = in.px[i * 4 + 1] / 8, q2 = in.px[i * 4 + 2] / 8;
    counts[q0 * 1024 + q1 * 32 + q2]++;
  }
  if (counts.empty()) return in;
  int kk = 0;
  long long best = -1;
  for (auto& kv : counts) if (kv.second > best) { best = kv.second; kk = kv.first; }
  double skin[3] = {(double)(kk / 1024) * 8.0 + 4.0, (double)((kk / 32) % 32) * 8.0 + 4.0, (double)(kk % 32) * 8.0 + 4.0};
  pbr::Rgba out = in;
  for (size_t i = 0; i < n; i++) {
    double f[3] = {(double)in.px[i * 4], (double)in.px[i * 4 + 1], (double)in.px[i * 4 + 2]};
    double dist = std::max(std::max(std::fabs(f[0] - skin[0]), std::fabs(f[1] - skin[1])), std::fabs(f[2] - skin[2]));
    double w = clip((dist - 15.0) / 25.0, 0.0, 1.0);
    double mx = std::max(std::max(f[0], f[1]), f[2]), mn = std::min(std::min(f[0], f[1]), f[2]);
    double sat = mx > 0 ? (mx - mn) / std::max(mx, 1e-6) : 0.0;
    double gain = 1.0 + 1.4 * k * clip(1.0 - sat / 0.55, 0.0, 1.0);
    double mean = ((f[0] + f[1]) + f[2]) / 3.0;
    for (int c = 0; c < 3; c++) {
      double boosted = clip(mean + (f[c] - mean) * gain, 0, 255);
      out.px[i * 4 + c] = u8((f[c] + (boosted - f[c]) * w) + 0.5);
    }
  }
  return out;
}

const char* emission_map(const GfMaterial& mt, const std::map<std::string, pbr::Rgba>& texs) {
  for (auto& c : mt.coords)
    if ((c.unit == 1 || c.unit == 2) && contains(c.name, "Inc") && texs.count(c.name)) return c.name.c_str();
  return nullptr;
}

const char* emission_mask(const GfMaterial& mt, const std::map<std::string, pbr::Rgba>& texs) {
  for (auto& c : mt.coords) {
    if (c.unit != 2 || !contains(c.name, "Nor")) continue;
    auto it = texs.find(c.name);
    if (it == texs.end()) continue;
    int mn = 255;
    for (size_t i = 3; i < it->second.px.size(); i += 4) mn = std::min<int>(mn, it->second.px[i]);
    if (mn < 250) return c.name.c_str();
  }
  return nullptr;
}

const GfCoord* coord_of(const GfMaterial& mt, const std::string& name) {
  for (auto& c : mt.coords) if (c.name == name && c.unit != 0) return &c;
  return nullptr;
}

namespace {
double wrap_coord(double q, uint32_t mode) {
  if (mode == 2) return q - std::floor(q);
  if (mode == 3) {
    double m = q - 2.0 * std::floor(q / 2.0);
    return m > 1.0 ? 2.0 - m : m;
  }
  return q < 0.0 ? 0.0 : (q > 1.0 ? 1.0 : q);
}
}

pbr::Rgba register_to_colour(const pbr::Rgba& src, const GfCoord& ck, const GfCoord& c0, int w, int h) {
  if (ck.scale == c0.scale && ck.trans == c0.trans) return src;
  pbr::Rgba out(w, h);
  for (int y = 0; y < h; y++) {
    double py = ((double)y + 0.5) / (double)h;
    double uy = py / c0.scale[1] + c0.trans[1];
    double qy = wrap_coord(ck.scale[1] * (uy - ck.trans[1]), ck.wrap[1]);
    int iy = std::min((int)(qy * (double)src.h), src.h - 1);
    for (int x = 0; x < w; x++) {
      double px = ((double)x + 0.5) / (double)w;
      double ux = px / c0.scale[0] + c0.trans[0];
      double qx = wrap_coord(ck.scale[0] * (ux - ck.trans[0]), ck.wrap[0]);
      int ix = std::min((int)(qx * (double)src.w), src.w - 1);
      for (int c = 0; c < 4; c++) out.px[((size_t)y * w + x) * 4 + c] = src.px[((size_t)iy * src.w + ix) * 4 + c];
    }
  }
  return out;
}

pbr::Rgba bake_emission(const pbr::Rgba& rgba, const pbr::Rgba& inc_in, double strength, const pbr::Rgba* raw, const pbr::Rgba* mask) {
  pbr::Rgba inc = inc_in;
  if (inc.h != rgba.h || inc.w != rgba.w) inc = pbr::gx_resize(inc, rgba.w, rgba.h);
  size_t n = inc.px.size() / 4;
  pbr::Rgba mk;
  if (mask) {
    mk = *mask;
    if (mk.h != inc.h || mk.w != inc.w) mk = pbr::gx_resize(mk, inc.w, inc.h);
  }
  if (strength == 1.0) {
    std::vector<double> v;
    v.reserve(n * 3);
    for (size_t i = 0; i < n; i++) for (int c = 0; c < 3; c++) v.push_back((double)inc.px[i * 4 + c] * (mask ? (double)mk.px[i * 4 + 3] / 255.0 : 1.0));
    double mean = np_sum(v.data(), v.size()) / (double)v.size();
    strength = (mean / 255.0 < 0.12) ? 1.0 : 0.3;
  }
  bool has_alpha = false;
  for (size_t i = 0; i < n; i++) if (inc.px[i * 4 + 3] < 250) { has_alpha = true; break; }
  pbr::Rgba rw;
  if (raw) {
    rw = *raw;
    if (rw.h != inc.h || rw.w != inc.w) rw = pbr::gx_resize(rw, inc.w, inc.h);
  }
  pbr::Rgba out = rgba;
  for (size_t i = 0; i < n; i++) {
    double al = has_alpha ? (double)inc.px[i * 4 + 3] / 255.0 : 1.0;
    double wgt = 1.0;
    if (raw) {
      double d = 0;
      for (int c = 0; c < 3; c++) d = std::max(d, std::fabs((double)inc.px[i * 4 + c] - (double)rw.px[i * 4 + c]));
      wgt = clip((d - 10.0) / 30.0, 0.0, 1.0);
    }
    for (int c = 0; c < 3; c++) {
      double glow = (double)inc.px[i * 4 + c] * al;
      if (mask) glow = glow * ((double)mk.px[i * 4 + 3] / 255.0);
      if (raw) glow = glow * wgt;
      out.px[i * 4 + c] = u8(clip((double)rgba.px[i * 4 + c] + glow * strength, 0, 255));
    }
  }
  return out;
}

}
}
