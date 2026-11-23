#include "gf_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "detmath.h"

namespace n3ds {
namespace gfi {

static inline uint8_t u8(double x) { return (uint8_t)(int)x; }
static inline double clip(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline double pymod(double a, double b) {
  double r = std::fmod(a, b);
  if (r != 0.0 && ((r < 0) != (b < 0))) r += b;
  return r;
}

pbr::Rgba fire_tint(const pbr::Rgba& in) {
  pbr::Rgba o = in;
  const double lo[3] = {235.0, 55.0, 15.0}, mid[3] = {255.0, 150.0, 25.0}, hi[3] = {255.0, 235.0, 120.0};
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double lum = clip((double)in.px[i] / 255.0 * 1.7, 0.0, 1.0);
    for (int k = 0; k < 3; k++) {
      double c = lum < 0.5 ? lo[k] + (mid[k] - lo[k]) * (lum / 0.5) : mid[k] + (hi[k] - mid[k]) * ((lum - 0.5) / 0.5);
      o.px[i + k] = u8(clip(c, 0, 255));
    }
    o.px[i + 3] = 255;
  }
  return o;
}

pbr::Rgba light_tweak(const pbr::Rgba& in, double k) {
  pbr::Rgba o = in;
  const double expo = 1.0 - 0.12 * k;
  double lut[256];
  for (int v = 0; v < 256; v++) lut[v] = dm::pow((double)v / 255.0, expo);
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double f[3];
    for (int c = 0; c < 3; c++) f[c] = lut[in.px[i + c]];
    double lum = (f[0] * 0.30 + f[1] * 0.59) + f[2] * 0.11;
    for (int c = 0; c < 3; c++) {
      double v = lum + (f[c] - lum) * (1.0 + 0.14 * k);
      o.px[i + c] = u8(clip(v * 255.0 + 0.5, 0, 255));
    }
  }
  return o;
}

static const double MX[3][3] = {{0.4124564, 0.3575761, 0.1804375}, {0.2126729, 0.7151522, 0.0721750}, {0.0193339, 0.1191920, 0.9503041}};
static const double WH[3] = {0.95047, 1.0, 1.08883};
static const double INVT[3][3] = {{3.2404548360214087, -0.9692663898756538, 0.05564341960421367},
                                  {-1.5371388501025751, 1.876010928842491, -0.20402585426769818},
                                  {-0.498531546868481, 0.041556082346673545, 1.057225162457929}};
void rgb_to_lab(const double* rgb, double* lab) {
  double lin[3];
  for (int k = 0; k < 3; k++) {
    double c = rgb[k] / 255.0;
    if (rgb[k] >= 0.0 && rgb[k] <= 255.0 && rgb[k] == std::floor(rgb[k])) {
      static const std::array<double, 256> T = [] {
        std::array<double, 256> t{};
        for (int v = 0; v < 256; v++) {
          double cc = (double)v / 255.0;
          t[v] = cc <= 0.04045 ? cc / 12.92 : dm::pow((cc + 0.055) / 1.055, 2.4);
        }
        return t;
      }();
      lin[k] = T[(int)rgb[k]];
      continue;
    }
    lin[k] = c <= 0.04045 ? c / 12.92 : dm::pow((c + 0.055) / 1.055, 2.4);
  }
  double f[3];
  for (int i = 0; i < 3; i++) {
    double x = ((lin[0] * MX[i][0] + lin[1] * MX[i][1]) + lin[2] * MX[i][2]) / WH[i];
    f[i] = x > 0.008856 ? dm::cbrt(x) : 7.787 * x + 16.0 / 116.0;
  }
  lab[0] = 116.0 * f[1] - 16.0;
  lab[1] = 500.0 * (f[0] - f[1]);
  lab[2] = 200.0 * (f[1] - f[2]);
}
void lab_to_rgb(const double* lab, double* rgb) {
  double fy = (lab[0] + 16.0) / 116.0, fx = fy + lab[1] / 500.0, fz = fy - lab[2] / 200.0;
  double f[3] = {fx, fy, fz}, xyz[3];
  for (int i = 0; i < 3; i++) {
    double c3 = f[i] * f[i] * f[i];
    c3 = dm::pow(f[i], 3.0);
    xyz[i] = (c3 > 0.008856 ? c3 : (f[i] - 16.0 / 116.0) / 7.787) * WH[i];
  }
  for (int j = 0; j < 3; j++) {
    double lin = clip((xyz[0] * INVT[0][j] + xyz[1] * INVT[1][j]) + xyz[2] * INVT[2][j], 0.0, 1.0);
    double c = lin <= 0.0031308 ? lin * 12.92 : 1.055 * dm::pow(lin, 1.0 / 2.4) - 0.055;
    rgb[j] = clip(c * 255.0, 0, 255);
  }
}

static double box(double x, const std::optional<std::pair<double, double>>& r, double feather) {
  if (!r) return 1.0;
  return clip((x - (r->first - feather)) / feather, 0, 1) * clip(((r->second + feather) - x) / feather, 0, 1);
}
static double hue_box(double h, const std::optional<std::pair<double, double>>& r, double feather = 12.0) {
  if (!r) return 1.0;
  double mid = (r->first + r->second) / 2.0, half = (r->second - r->first) / 2.0;
  double d = std::fabs(pymod(h - mid + 180.0, 360.0) - 180.0);
  return clip((half + feather - d) / feather, 0, 1);
}

struct HandRule {
  int nat;
  const char* key;
  std::optional<std::pair<double, double>> hue, L, C;
  double dL, gain, dh;
};
#define R_(a, b) std::make_pair((double)(a), (double)(b))
static const std::vector<HandRule>& hand_rules_all() {
  static const std::vector<HandRule> T = {
      {25, "BodyB", R_(10, 70), std::nullopt, R_(18, 80), -9, 1.8, 5},
      {25, "BodyA", std::nullopt, R_(0, 42), R_(0, 12), -22, 1.0, 0},
      {28, "*", R_(70, 115), std::nullopt, R_(15, 80), -5, 1.2, 0},
      {94, "*", R_(270, 360), R_(30, 85), R_(10, 60), -14, 0.85, 0},
      {109, "*+Mouth", R_(270, 350), R_(45, 95), R_(5, 45), -19, 1.5, 8},
      {109, "*+Mouth", R_(270, 350), R_(10, 45), R_(3, 40), -6, 1.3, 6},
      {110, "*", R_(270, 360), R_(35, 95), R_(5, 40), -13, 1.3, 4},
      {213, "*", R_(5, 55), R_(25, 62), R_(30, 90), -8, 1.0, -12},
      {216, "*", R_(30, 90), std::nullopt, R_(15, 90), -3, 1.0, -9},
      {51, "*", R_(40, 100), R_(40, 95), R_(15, 70), -10, 1.25, -6},
      {100, "*", R_(10, 70), R_(55, 92), R_(14, 58), -19, 2.5, 0},
      {172, "BodyA", std::nullopt, R_(0, 52), R_(0, 14), -22, 1.0, 0},
      {251, "*", R_(95, 175), R_(55, 100), R_(8, 60), 0, 1.35, 0},
      {256, "*", R_(35, 80), R_(40, 82), R_(15, 80), -5, 1.5, 0},
      {256, "*", R_(80, 118), R_(65, 100), R_(15, 70), 2, 1.35, 6},
      {258, "*", R_(235, 290), std::nullopt, R_(8, 70), -7, 1.5, 0},
      {293, "*+Mouth", R_(280, 360), R_(60, 100), R_(5, 40), -6, 1.9, -12},
      {293, "*+Mouth", R_(60, 112), R_(65, 98), R_(15, 70), 9, 0.9, 12},
      {308, "*", R_(-20, 35), R_(40, 85), R_(15, 70), -8, 1.4, -7},
      {687, "*", R_(200, 285), R_(45, 100), R_(5, 60), -38, 1.3, 22},
      {639, "*", std::nullopt, R_(55, 95), R_(0, 18), -10, 1.0, 0},
      {883, "BodyA", std::nullopt, R_(55, 100), std::nullopt, -60, 1.0, 0},
  };
  return T;
}
std::vector<int> hand_rules_for(int nat, const std::string& name) {
  std::vector<int> out;
  const char* skip[4] = {"Iris", "Line", "Tongue", "Mouth"};
  const auto& T = hand_rules_all();
  for (size_t i = 0; i < T.size(); i++) {
    const auto& r = T[i];
    if (r.nat != nat) continue;
    std::string key = r.key;
    if (key == "*" || key == "*+Mouth") {
      int n = key == "*" ? 4 : 3;
      bool hit = false;
      for (int k = 0; k < n; k++)
        if (name.find(skip[k]) != std::string::npos) hit = true;
      if (hit) continue;
    } else if (name.find(key) == std::string::npos) {
      continue;
    }
    out.push_back((int)i);
  }
  return out;
}
std::optional<std::array<uint8_t, 3>> flat_for(int nat, const std::string& name) {
  if (nat == 874 && name.find("BodyB") != std::string::npos) return std::array<uint8_t, 3>{62, 40, 116};
  return std::nullopt;
}

pbr::Rgba hand_tune(const pbr::Rgba& in, const std::vector<int>& rules) {
  if (rules.empty()) return in;
  pbr::Rgba o = in;
  const auto& T = hand_rules_all();
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232;
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double rgb[3] = {(double)in.px[i], (double)in.px[i + 1], (double)in.px[i + 2]}, lab[3];
    rgb_to_lab(rgb, lab);
    double L = lab[0], a = lab[1], b = lab[2];
    for (int ri : rules) {
      const HandRule& r = T[ri];
      double C = dm::hypot(a, b);
      double h = pymod(dm::atan2(b, a) * R2D, 360.0);
      double w = box(L, r.L, 6.0) * box(C, r.C, 4.0) * hue_box(h, r.hue);
      double nL = clip(L + r.dL, 0.0, 100.0), nC = C * r.gain, nh = (h + r.dh) * D2R;
      L = L + (nL - L) * w;
      double na = nC * dm::cos(nh), nb = nC * dm::sin(nh);
      a = a + (na - a) * w;
      b = b + (nb - b) * w;
    }
    double l3[3] = {L, a, b}, out[3];
    lab_to_rgb(l3, out);
    for (int k = 0; k < 3; k++) o.px[i + k] = u8(out[k] + 0.5);
  }
  return o;
}

std::vector<int> recolour_fixes_for(int nat, const std::string& name) {
  struct F { int a, b, c; const char* key; };
  static const F T[5] = {{100, 101, 0, "Body"}, {228, 229, 0, "Body"}, {207, 0, 0, "Body"}, {187, 188, 189, "Cotton"}, {120, 121, 0, "Hitodeman"}};
  std::vector<int> out;
  for (int i = 0; i < 5; i++) {
    bool in = nat == T[i].a || (T[i].b && nat == T[i].b) || (T[i].c && nat == T[i].c);
    if (in && name.find(T[i].key) != std::string::npos) out.push_back(i);
  }
  return out;
}

static void hsv(const double* f, double* h, double* s, double* v) {
  double r = f[0], g = f[1], b = f[2];
  double mx = std::max(std::max(r, g), b), mn = std::min(std::min(r, g), b), d = mx - mn;
  *s = mx > 0 ? d / std::max(mx, 1e-6) : 0.0;
  double dd = std::max(d, 1e-6);
  double hh = mx == r ? pymod((g - b) / dd, 6.0) : (mx == g ? (b - r) / dd + 2 : (r - g) / dd + 4);
  *h = hh * 60.0;
  *v = mx;
}
static void hsv_rgb(double h, double s, double v, double* out) {
  double c = v * s, hp = pymod(h, 360.0) / 60.0;
  double x = c * (1 - std::fabs(pymod(hp, 2.0) - 1)), z = 0.0, m = v - c;
  double rs, gs, bs;
  if (hp < 1) { rs = c; gs = x; bs = z; }
  else if (hp < 2) { rs = x; gs = c; bs = z; }
  else if (hp < 3) { rs = z; gs = c; bs = x; }
  else if (hp < 4) { rs = z; gs = x; bs = c; }
  else if (hp < 5) { rs = x; gs = z; bs = c; }
  else { rs = c; gs = z; bs = x; }
  out[0] = rs + m; out[1] = gs + m; out[2] = bs + m;
}
struct Retone {
  std::optional<double> hue;
  std::optional<std::pair<double, double>> hue_from;
  double sat_mul = 1, sat_min = 0, val_mul = 1, val_lift = 0, min_sat = 0, max_val = 1, max_sat = 1, min_val = 0;
};
static pbr::Rgba retone(const pbr::Rgba& in, const Retone& p) {
  pbr::Rgba o = in;
  for (size_t i = 0; i < in.px.size(); i += 4) {
    double f[3] = {in.px[i] / 255.0, in.px[i + 1] / 255.0, in.px[i + 2] / 255.0}, h, s, v;
    hsv(f, &h, &s, &v);
    bool sel = s >= p.min_sat && s <= p.max_sat && v <= p.max_val && v >= p.min_val;
    if (sel && p.hue_from) {
      double lo = p.hue_from->first, hi = p.hue_from->second;
      sel = lo <= hi ? (h >= lo && h <= hi) : (h >= lo || h <= hi);
    }
    double nh = sel ? (p.hue ? *p.hue : h) : h;
    double ns = sel ? clip(std::max(s * p.sat_mul, p.sat_min), 0, 1) : s;
    double nv = sel ? clip(v * p.val_mul + p.val_lift, 0, 1) : v;
    double r[3];
    hsv_rgb(nh, ns, nv, r);
    for (int k = 0; k < 3; k++) o.px[i + k] = u8(clip(r[k] * 255.0 + 0.5, 0, 255));
  }
  return o;
}
static pbr::Rgba whiten(const pbr::Rgba& in, double k = 0.85, double floor_ = 0.93) {
  pbr::Rgba o = in;
  size_t n = in.px.size() / 4;
  std::vector<double> lum(n);
  double mxl = -1e300;
  for (size_t p = 0; p < n; p++) {
    double f0 = in.px[p * 4] / 255.0, f1 = in.px[p * 4 + 1] / 255.0, f2 = in.px[p * 4 + 2] / 255.0;
    lum[p] = (f0 * 0.30 + f1 * 0.59) + f2 * 0.11;
    mxl = std::max(mxl, lum[p]);
  }
  double den = std::max(mxl, 1e-6);
  for (size_t p = 0; p < n; p++) {
    double shade = floor_ + (1 - floor_) * clip(lum[p] / den, 0, 1);
    for (int c = 0; c < 3; c++) {
      double f = in.px[p * 4 + c] / 255.0;
      o.px[p * 4 + c] = u8(clip((f * (1 - k) + shade * k) * 255.0 + 0.5, 0, 255));
    }
  }
  return o;
}
pbr::Rgba apply_fix(int id, const pbr::Rgba& in) {
  Retone r;
  switch (id) {
    case 0: r.hue = 0.0; r.sat_mul = 3.0; r.sat_min = 0.82; r.min_sat = 0.18; r.val_lift = -0.04; return retone(in, r);
    case 1: r.val_mul = 0.42; r.max_sat = 0.3; r.max_val = 0.62; return retone(in, r);
    case 2: r.hue = 268.0; r.hue_from = std::make_pair(285.0, 350.0); r.sat_mul = 0.95; r.min_sat = 0.12; r.val_mul = 0.88; return retone(in, r);
    case 3: return whiten(in);
    default: r.hue = 338.0; r.sat_mul = 1.6; r.sat_min = 0.8; r.val_lift = 0.35; return retone(in, r);
  }
}

static bool contains(const std::string& s, const char* k) { return s.find(k) != std::string::npos; }
static bool starts(const std::string& s, const char* k) { return s.compare(0, strlen(k), k) == 0; }

const GfCoord* colour_coord(const GfMaterial& mt) {
  const GfCoord* c0 = nullptr;
  for (auto& c : mt.coords) if (c.unit == 0) { c0 = &c; break; }
  if (contains(mt.shader, "patcheel")) {
    for (auto& c : mt.coords) if (c.unit == 1) return &c;
  }
  if (c0 && starts(c0->name, "DummyTex")) {
    for (auto& c : mt.coords) if (c.unit == 1 && !starts(c.name, "DummyTex")) return &c;
  }
  return c0;
}

bool is_effect_layer(const GfMaterial& mt) {
  const GfCoord* c0 = nullptr;
  for (auto& c : mt.coords) if (c.unit == 0) { c0 = &c; break; }
  std::string n0 = c0 ? c0->name : "";
  if (mt.coords.empty() && contains(mt.shader, "Mask")) return true;
  if (contains(mt.shader, "_Thunder") || starts(mt.name, "AuraEf") || (starts(mt.name, "GlowInc") && contains(mt.shader, "_Glow"))) return true;
  return contains(n0, "Mask") || contains(mt.shader, "pokabu_Tail") ||
         (contains(mt.shader, "Fire") && contains(mt.name, "Inc") && !contains(mt.name, "Fire"));
}

static const char* alpha_class(const pbr::Rgba& p, bool use_alpha) {
  if (!use_alpha) return "opa";
  bool all = true;
  size_t mid = 0, n = p.px.size() / 4;
  for (size_t i = 0; i < n; i++) {
    int a = p.px[i * 4 + 3];
    if (a < 250) all = false;
    if (a > 8 && a < 247) mid++;
  }
  if (all) return "opa";
  return ((double)mid / (double)n) > 0.02 ? "xlu" : "texedge";
}

static double frac_alpha_below(const pbr::Rgba& p, int thr) {
  size_t c = 0, n = p.px.size() / 4;
  for (size_t i = 0; i < n; i++) if (p.px[i * 4 + 3] < thr) c++;
  return (double)c / (double)n;
}

MadeImages make_images(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const Opts& opts, int nat, const MatchPlan* mplan) {
  MadeImages out;
  std::map<std::string, int> name_to_img;
  std::map<int, std::string> cls;
  for (auto& mt : gm.materials) {
    const GfCoord* co = colour_coord(mt);
    std::string nm = co ? co->name : "";
    bool have_nm = co != nullptr;
    bool patch = contains(mt.shader, "patcheel") && co && co->unit == 1;
    bool tra = contains(mt.shader, "Tra") || contains(mt.shader, "Alp") || contains(mt.shader, "Xlu") || contains(mt.shader, "Iris") ||
               contains(mt.name, "Iris") || starts(mt.name, "Line");
    auto it = texs.find(nm);
    bool in_texs = have_nm && it != texs.end();
    if (!tra && in_texs && starts(mt.name, "Mouth") && frac_alpha_below(it->second, 8) > 0.25) tra = true;
    if (!tra && in_texs && starts(mt.name, "Eye") && !contains(mt.name, "Iris") && frac_alpha_below(it->second, 8) > 0.6) tra = true;
    bool eye_mat = opts.eye_blend && contains(mt.name, "Eye") && !contains(mt.name, "Iris") && have_nm && contains(nm, "Eye");
    const char* inc_c = opts.fx ? emission_map(mt, texs) : nullptr;
    std::string inc = inc_c ? inc_c : "";
    bool ghost = opts.ghost_alpha > 0 && contains(mt.shader, "ghos_BodyAdd") && !contains(mt.name, "Eye") && !contains(mt.name, "Iris");
    bool cell = contains(mt.shader, "uniran_Cell");
    if (ghost || cell) tra = true;
    if (patch) tra = true;
    std::vector<int> fixes = opts.recolour ? recolour_fixes_for(nat, mt.name) : std::vector<int>();
    std::vector<int> hrules = opts.hand ? hand_rules_for(nat, mt.name) : std::vector<int>();
    auto flat = opts.hand ? flat_for(nat, mt.name) : std::nullopt;
    bool plate = opts.hand && hand_plates_follow(nat, mt.name);
    bool do_match = mplan != nullptr && (match_piece_ok(mt, fixes) || (plate && fixes.empty())) && !ghost;
    if (!in_texs) {
      out.mat_img.push_back({-1, "opa"});
      continue;
    }
    char kb[512];
    std::string fx, hr;
    for (int f : fixes) fx += std::to_string(f) + ",";
    for (int f : hrules) hr += std::to_string(f) + ",";
    snprintf(kb, sizeof kb, "%s|%d|%d|%d|%d|%s|%s|%d|%s|%d|%d", nm.c_str(), (int)tra, (int)ghost, (int)cell, (int)patch, fx.c_str(), hr.c_str(),
             flat ? (((*flat)[0] << 16) | ((*flat)[1] << 8) | (*flat)[2]) : -1, inc.c_str(), (int)do_match, (int)plate);
    std::string key = kb;
    if (!name_to_img.count(key)) {
      pbr::Rgba rgba = it->second;
      int w = rgba.w, h = rgba.h;
      if (patch) {
        const GfCoord* c0 = nullptr;
        for (auto& x : mt.coords) if (x.unit == 0) { c0 = &x; break; }
        double col[3] = {214.0, 110.0, 108.0};
        if (c0 && texs.count(c0->name)) {
          const auto& tp = texs.at(c0->name);
          double s[3] = {0, 0, 0};
          size_t n = tp.px.size() / 4;
          for (size_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) s[k] += tp.px[i * 4 + k];
          for (int k = 0; k < 3; k++) col[k] = s[k] / (double)n;
        }
        size_t n = rgba.px.size() / 4;
        for (size_t i = 0; i < n; i++) {
          double lum = ((double)rgba.px[i * 4] + rgba.px[i * 4 + 1] + rgba.px[i * 4 + 2]) / 3.0;
          double a = rgba.px[i * 4 + 3];
          for (int k = 0; k < 3; k++) rgba.px[i * 4 + k] = u8(clip(col[k], 0, 255));
          rgba.px[i * 4 + 3] = u8(clip(lum * (a / 255.0), 0, 255));
        }
      }
      std::string c = alpha_class(rgba, tra);
      if (cell) {
        double c0_[3], c3_[3];
        for (int k = 0; k < 3; k++) { c0_[k] = mt.colors[0][k]; c3_[k] = mt.colors[3][k]; }
        double mn = std::min(std::min(c0_[0], c0_[1]), c0_[2]);
        if (mn > 200) { c0_[0] = 61; c0_[1] = 165; c0_[2] = 101; }
        rgba = pbr::Rgba(16, 16);
        for (size_t i = 0; i < 256; i++) {
          for (int k = 0; k < 3; k++) rgba.px[i * 4 + k] = u8(clip(c3_[k] * 0.4 + c0_[k] * 0.6, 0, 255));
          rgba.px[i * 4 + 3] = 150;
        }
        w = h = 16;
        c = "xlu";
      }
      if (ghost) {
        c = "xlu";
        size_t n = rgba.px.size() / 4;
        const double gas[3] = {80.0, 30.0, 115.0};
        for (size_t i = 0; i < n; i++) {
          rgba.px[i * 4 + 3] = (uint8_t)(int)(255 * opts.ghost_alpha);
          for (int k = 0; k < 3; k++) rgba.px[i * 4 + k] = u8((double)rgba.px[i * 4 + k] * 0.75 + gas[k] * 0.25);
        }
      }
      if (!tra) for (size_t i = 3; i < rgba.px.size(); i += 4) rgba.px[i] = 255;
      bool is_fire = contains(mt.shader, "Fire") || contains(mt.name, "Fire");
      bool grey = true;
      if (opts.fire && is_fire) {
        for (size_t i = 0; i < rgba.px.size(); i += 4) if (rgba.px[i] != rgba.px[i + 1]) { grey = false; break; }
      }
      if (opts.lure && is_lure_glow(mt)) {
        rgba = lure_fill(rgba);
        c = "opa";
      } else if (opts.fire && is_fire && grey) {
        rgba = fire_tint(rgba);
        c = "opa";
        if (opts.fx) {
          rgba = fire_alpha(rgba);
          c = "xlu";
        }
      } else if (opts.light > 0 && (!contains(mt.name, "Eye") || eye_mat)) {
        for (int f : fixes) rgba = apply_fix(f, rgba);
        rgba = light_tweak(rgba, opts.light);
        if (do_match) rgba = mplan->apply(rgba, contains(mt.name, "Eye") || plate);
        if (opts.sat_boost != 0 && !contains(mt.name, "Eye") && !contains(mt.name, "Iris")) rgba = sat_boost(rgba, opts.sat_boost);
        if (!hrules.empty()) rgba = hand_tune(rgba, hrules);
        if (flat) for (size_t i = 0; i < rgba.px.size(); i += 4) for (int k = 0; k < 3; k++) rgba.px[i + k] = (*flat)[k];
        if (opts.steel != 0 && (contains(mt.shader, "PTS_Body_Add") ||
                                ((nat == 304 || nat == 305 || nat == 306) && !contains(mt.name, "Eye") && !contains(mt.name, "Iris") && !contains(mt.name, "Mouth"))))
          rgba = steel_tint(rgba, opts.steel);
        if (opts.cheeks != 0 && contains(mt.shader, "Neon")) rgba = cheek_boost(rgba, opts.cheeks);
        if (!inc.empty()) rgba = bake_emission(rgba, texs.at(inc), hand_glow_strength(nat), &it->second);
      }
      int nw = std::min(w, opts.max_side), nh = std::min(h, opts.max_side);
      if (nw != w || nh != h) rgba = pbr::gx_resize(rgba, nw, nh);
      pbr::Image im;
      im.width = nw;
      im.height = nh;
      if (c == "xlu") {
        im.format = 5;
        im.data = pbr::gx_encode_rgb5a3(rgba);
      } else {
        im.format = 14;
        im.data = pbr::gx_encode_cmpr(rgba);
      }
      name_to_img[key] = (int)out.images.size();
      out.images.push_back(std::move(im));
      cls[name_to_img[key]] = c;
    }
    out.mat_img.push_back({name_to_img[key], cls[name_to_img[key]]});
  }
  out.classes.resize(out.images.size());
  for (auto& kv : cls) out.classes[kv.first] = kv.second;
  return out;
}

}
}
