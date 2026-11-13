#include "pbr_to_xd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "pbr_eval.h"

namespace pbr {

namespace {

constexpr double PI = 3.141592653589793;
const char* FACE_ACTIONS[4] = {"tx_sleep", "tx_wakeup", "tx_wink", nullptr};
constexpr int T_TRAU = 2, T_TRAV = 3;

template <class... A>
std::string sf(const char* f, A... a) {
  char buf[1024];
  snprintf(buf, sizeof buf, f, a...);
  return buf;
}

const std::vector<std::string> PHYSICAL = {"tackle", "kime_phy", "punch", "kick", "hikkaku", "kyukoka", "binta", "kime", "kime_a"};
const std::vector<int> ROLE_ORDER = {0, 16, 8, 10, 1, 9, 2, 3, 4, 5, 7, 6, 12, 13, 14, 15, 11};

const std::vector<std::string>& role_names(int s) {
  static const std::map<int, std::vector<std::string>> R = {
      {0, {"wait", "kuchu_wait"}}, {16, {"kuchu_wait", "wait"}}, {8, {"damage", "damege", "damage_a"}},
      {10, {"down", "kuchu_down"}}, {9, {"damage", "damege", "down"}}, {6, {"kime", "wait_a", "wait"}},
      {1, {"kime", "kime_a", "kime_b"}}, {12, {"kime", "kime_a"}}, {13, {"kime", "kime_a"}}, {14, {"kime", "kime_a"}},
      {15, {"mamoru", "kime_a", "kime"}}, {11, {"wait"}}, {2, PHYSICAL}, {3, PHYSICAL}, {4, PHYSICAL}, {5, PHYSICAL}, {7, PHYSICAL}};
  static const std::vector<std::string> dflt = {"wait"};
  auto it = R.find(s);
  return it == R.end() ? dflt : it->second;
}

int role_rank(int s) {
  for (size_t i = 0; i < ROLE_ORDER.size(); i++)
    if (ROLE_ORDER[i] == s) return (int)i;
  return 99;
}

int index_in(const std::vector<std::string>& v, const std::string& s) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == s) return (int)i;
  return -1;
}

struct Users {
  std::map<int, std::vector<std::pair<int, int>>> by_slot;
  bool has(int slot) const { return by_slot.count(slot) > 0; }
};

Users make_users(const Pkx& xd_pkx, size_t nframes) {
  Users u;
  for (size_t s = 0; s < xd_pkx.sections.size(); s++) {
    auto mot = xd_pkx.section_motions(s);
    for (size_t mo = 0; mo < mot.size(); mo++) {
      int kind = mot[mo].first, idx = mot[mo].second;
      if (kind && 0 <= idx && idx < (int)nframes) u.by_slot[idx].emplace_back((int)s, (int)mo);
    }
  }
  return u;
}

struct Act { int idx; std::string name; int frames; };

std::pair<int, std::vector<std::string>> wanted(const Users& users, int slot) {
  auto it = users.by_slot.find(slot);
  if (it == users.by_slot.end() || it->second.empty()) return {-1, {}};
  auto us = it->second;
  std::stable_sort(us.begin(), us.end(), [](const auto& a, const auto& b) { return role_rank(a.first) < role_rank(b.first); });
  int s = us[0].first, mo = us[0].second;
  return {s, (s == 9 && mo >= 1) ? role_names(10) : role_names(s)};
}

struct PickResult {
  std::vector<std::optional<int>> pick;
  std::map<int, std::string> how;
  Users users;
};

PickResult pick_actions(const sdr::Sdr& sdr, const Pkx& xd_pkx, const std::vector<int>& xd_frames, const Logger& log) {
  std::vector<Act> acts;
  for (size_t i = 0; i < sdr.actions.size(); i++) {
    const auto& a = sdr.actions[i];
    if (a.name.rfind("tx_", 0) == 0) continue;
    acts.push_back({(int)i, a.name, std::max((int)std::nearbyint(a.f[2] * FPS), 1)});
  }
  Users users = make_users(xd_pkx, xd_frames.size());
  std::map<int, int> pick;
  std::map<int, std::string> how;
  std::set<int> taken;
  for (size_t slot = 0; slot < xd_frames.size(); slot++) {
    int f = xd_frames[slot];
    std::vector<Act> same;
    for (auto& a : acts)
      if (a.frames == f) same.push_back(a);
    if (same.empty()) continue;
    std::vector<Act> free_;
    for (auto& a : same)
      if (!taken.count(a.idx)) free_.push_back(a);
    if (free_.empty()) free_ = same;
    auto want = wanted(users, (int)slot).second;
    std::stable_sort(free_.begin(), free_.end(), [&](const Act& a, const Act& b) {
      int ra = index_in(want, a.name), rb = index_in(want, b.name);
      std::pair<int, int> ka = {ra >= 0 ? ra : 99, a.idx}, kb = {rb >= 0 ? rb : 99, b.idx};
      return ka < kb;
    });
    pick[(int)slot] = free_[0].idx;
    how[(int)slot] = "length";
    taken.insert(free_[0].idx);
  }
  for (size_t slot = 0; slot < xd_frames.size(); slot++) {
    int f = xd_frames[slot];
    if (pick.count((int)slot) || !users.has((int)slot)) continue;
    auto sw = wanted(users, (int)slot);
    int s = sw.first;
    const auto& want = sw.second;
    std::vector<Act> cands;
    for (auto& a : acts)
      if (index_in(want, a.name) >= 0) cands.push_back(a);
    if (s == 2 || s == 3 || s == 4 || s == 5 || s == 7) {
      std::vector<Act> nt;
      for (auto& a : cands)
        if (!taken.count(a.idx)) nt.push_back(a);
      if (!nt.empty()) cands = nt;
    }
    if (!cands.empty()) {
      std::stable_sort(cands.begin(), cands.end(), [&](const Act& a, const Act& b) {
        std::pair<int, int> ka = {index_in(want, a.name), std::abs(a.frames - f)}, kb = {index_in(want, b.name), std::abs(b.frames - f)};
        return ka < kb;
      });
      pick[(int)slot] = cands[0].idx;
      how[(int)slot] = "role";
      taken.insert(cands[0].idx);
      log(sf("NOTE: an XD animation (section %d) has no PBR action of its length: PBR's \"%s\" plays there", s, cands[0].name.c_str()));
    } else {
      log(sf("WRONG: an XD animation used by section %d has no PBR action to stand in: nothing plays there", s));
    }
  }
  PickResult r;
  for (size_t i = 0; i < xd_frames.size(); i++) {
    auto it = pick.find((int)i);
    r.pick.push_back(it == pick.end() ? std::nullopt : std::optional<int>(it->second));
  }
  r.how = how;
  r.users = users;
  return r;
}

PickResult pick_actions_named(const sdr::Sdr& sdr, const Pkx& xd_pkx, const std::vector<int>& xd_frames,
                              const std::map<int, std::string>& by_slot, const Logger& log) {
  std::map<std::string, int> names;
  for (size_t i = 0; i < sdr.actions.size(); i++)
    if (sdr.actions[i].name.rfind("tx_", 0) != 0 && !names.count(sdr.actions[i].name)) names[sdr.actions[i].name] = (int)i;
  Users users = make_users(xd_pkx, xd_frames.size());
  std::map<int, int> pick;
  std::map<int, std::string> how;
  std::set<int> taken;
  for (size_t slot = 0; slot < xd_frames.size(); slot++) {
    auto it = by_slot.find((int)slot);
    if (it != by_slot.end() && names.count(it->second)) {
      pick[(int)slot] = names[it->second];
      how[(int)slot] = "name";
      taken.insert(names[it->second]);
    }
  }
  for (size_t slot = 0; slot < xd_frames.size(); slot++) {
    if (pick.count((int)slot) || !users.has((int)slot)) continue;
    auto us = users.by_slot[(int)slot];
    std::stable_sort(us.begin(), us.end(), [](const auto& a, const auto& b) { return role_rank(a.first) < role_rank(b.first); });
    int sec = us[0].first, mo = us[0].second;
    std::vector<std::string> want = (sec == 9 && mo >= 1) ? role_names(10) : role_names(sec);
    auto bit = by_slot.find((int)slot);
    std::string bs = bit == by_slot.end() ? std::string() : bit->second;
    bool has_bs = bit != by_slot.end();
    if (has_bs && index_in(PHYSICAL, bs) >= 0) {
      std::vector<std::string> w = PHYSICAL;
      for (auto& x : want)
        if (index_in(PHYSICAL, x) < 0) w.push_back(x);
      want = w;
    }
    std::vector<int> cands;
    for (auto& w : want)
      if (names.count(w)) cands.push_back(names[w]);
    if (sec == 2 || sec == 3 || sec == 4 || sec == 5 || sec == 7) {
      std::vector<int> nt;
      for (int c : cands)
        if (!taken.count(c)) nt.push_back(c);
      if (!nt.empty()) cands = nt;
    }
    if (!cands.empty()) {
      pick[(int)slot] = cands[0];
      how[(int)slot] = "role";
      taken.insert(cands[0]);
      log(sf("NOTE: no PBR action called \"%s\" (what the stand-in plays in slot %d): \"%s\" plays there", has_bs ? bs.c_str() : "None",
             (int)slot, sdr.actions[cands[0]].name.c_str()));
    } else {
      log(sf("WRONG: slot %d (section %d) has no PBR action to stand in (\"%s\" missing): nothing plays there", (int)slot, sec,
             has_bs ? bs.c_str() : "None"));
    }
  }
  PickResult r;
  for (size_t i = 0; i < xd_frames.size(); i++) {
    auto it = pick.find((int)i);
    r.pick.push_back(it == pick.end() ? std::nullopt : std::optional<int>(it->second));
  }
  r.how = how;
  r.users = users;
  return r;
}

double pairwise_sum(const double* a, size_t n) {
  if (n < 8) {
    double res = 0.;
    for (size_t i = 0; i < n; i++) res += a[i];
    return res;
  } else if (n <= 128) {
    double r[8];
    for (int j = 0; j < 8; j++) r[j] = a[j];
    size_t i;
    for (i = 8; i < n - (n % 8); i += 8)
      for (int j = 0; j < 8; j++) r[j] += a[i + j];
    double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < n; i++) res += a[i];
    return res;
  }
  size_t n2 = n / 2;
  n2 -= n2 % 8;
  return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}

double brightness(const sdr::Texture& t) {
  Rgba px = gx_decode(t.format, t.width, t.height, t.data.data(), t.data.size());
  size_t n = (size_t)px.w * px.h;
  std::vector<double> v(n);
  for (size_t i = 0; i < n; i++) {
    const uint8_t* p = &px.px[i * 4];
    v[i] = (p[0] * 0.3 + p[1] * 0.59) + p[2] * 0.11;
  }
  return pairwise_sum(v.data(), n) / (double)n / 255.0;
}

bool mid_sheet(const sdr::Texture& tl) {
  Rgba px = gx_decode(tl.format, tl.width, tl.height, tl.data.data(), tl.data.size());
  size_t n = (size_t)px.w * px.h;
  double sum = 0;
  size_t mid = 0;
  for (size_t i = 0; i < n; i++) {
    double v = px.px[i * 4];
    sum += v;
    if (v > 90 && v < 170) mid++;
  }
  double mean = sum / (double)n;
  return 0.4 <= mean / 255.0 && mean / 255.0 <= 0.7 && (double)mid / (double)n > 0.8;
}

int dimmed(Model& m, std::vector<std::string>& classes, int i, double f, std::map<std::string, int>& cache, int lift = 0) {
  std::string key = sf("%d|%.3f|%d", i, f, lift);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  const Image& src = m.images[i];
  Rgba rgba = gx_decode(src.format, src.width, src.height, src.data.data(), src.data.size());
  size_t n = (size_t)rgba.w * rgba.h;
  std::vector<double> px(n * 4);
  for (size_t k = 0; k < n; k++) {
    for (int c = 0; c < 3; c++) px[k * 4 + c] = rgba.px[k * 4 + c] * f;
    px[k * 4 + 3] = rgba.px[k * 4 + 3];
  }
  if (lift) {
    for (size_t k = 0; k < n; k++) {
      double mx = std::max(std::max(px[k * 4], px[k * 4 + 1]), px[k * 4 + 2]);
      if (mx < (double)lift) px[k * 4] = px[k * 4 + 1] = px[k * 4 + 2] = 255;
    }
  }
  Rgba out(rgba.w, rgba.h);
  for (size_t k = 0; k < n; k++) {
    for (int c = 0; c < 3; c++) {
      double v = px[k * 4 + c] + 0.5;
      v = v < 0 ? 0 : (v > 255 ? 255 : v);
      out.px[k * 4 + c] = (uint8_t)v;
    }
    out.px[k * 4 + 3] = 255;
  }
  Image im;
  im.width = src.width;
  im.height = src.height;
  im.format = 14;
  im.data = gx_encode_cmpr(out);
  m.images.push_back(std::move(im));
  classes.push_back("opa");
  int idx = (int)m.images.size() - 1;
  cache[key] = idx;
  return idx;
}

int matte_cut(Model& m, std::vector<std::string>& classes, int i, std::map<int, int>& cache) {
  auto ci = cache.find(i);
  if (ci != cache.end()) return ci->second;
  cache[i] = i;
  const Image& src = m.images[i];
  Rgba rg = gx_decode(src.format, src.width, src.height, src.data.data(), src.data.size());
  int h = rg.h, w = rg.w;
  std::vector<uint8_t>& px = rg.px;
  auto P = [&](int y, int x, int c) -> uint8_t& { return px[((size_t)y * w + x) * 4 + c]; };
  std::vector<char> opaque((size_t)h * w), white((size_t)h * w);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      opaque[(size_t)y * w + x] = P(y, x, 3) >= 128;
      int mn = std::min(std::min(P(y, x, 0), P(y, x, 1)), P(y, x, 2));
      white[(size_t)y * w + x] = opaque[(size_t)y * w + x] && mn > 225;
    }
  std::vector<char> seen((size_t)h * w, 0);
  struct Piece { int y0, y1, x0, x1, n; };
  std::vector<Piece> pieces;
  const int DY[4] = {1, -1, 0, 0}, DX[4] = {0, 0, 1, -1};
  for (int y0 = 0; y0 < h; y0++)
    for (int x0 = 0; x0 < w; x0++) {
      if (!opaque[(size_t)y0 * w + x0] || seen[(size_t)y0 * w + x0]) continue;
      std::vector<std::pair<int, int>> st = {{y0, x0}};
      seen[(size_t)y0 * w + x0] = 1;
      Piece pc{y0, y0, x0, x0, 0};
      while (!st.empty()) {
        auto [y, x] = st.back();
        st.pop_back();
        pc.n++;
        pc.y0 = std::min(pc.y0, y); pc.y1 = std::max(pc.y1, y); pc.x0 = std::min(pc.x0, x); pc.x1 = std::max(pc.x1, x);
        for (int d = 0; d < 4; d++) {
          int qy = y + DY[d], qx = x + DX[d];
          if (0 <= qy && qy < h && 0 <= qx && qx < w && opaque[(size_t)qy * w + qx] && !seen[(size_t)qy * w + qx]) {
            seen[(size_t)qy * w + qx] = 1;
            st.push_back({qy, qx});
          }
        }
      }
      pieces.push_back(pc);
    }
  if (pieces.size() < 2) return i;
  std::vector<std::pair<int, int>> rim;
  for (auto& pc : pieces) {
    if (pc.n < 0.98 * (pc.y1 - pc.y0 + 1) * (pc.x1 - pc.x0 + 1)) return i;
    for (int y = pc.y0; y <= pc.y1; y++) { rim.push_back({y, pc.x0}); rim.push_back({y, pc.x1}); }
    for (int x = pc.x0; x <= pc.x1; x++) { rim.push_back({pc.y0, x}); rim.push_back({pc.y1, x}); }
  }
  long long white_rim = 0;
  for (auto& p : rim) white_rim += white[(size_t)p.first * w + p.second];
  if ((double)white_rim < 0.95 * (double)rim.size()) return i;
  const int lo = 200;
  std::vector<char> light((size_t)h * w), cut((size_t)h * w, 0);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int mn = std::min(std::min(P(y, x, 0), P(y, x, 1)), P(y, x, 2));
      light[(size_t)y * w + x] = opaque[(size_t)y * w + x] && mn > lo;
    }
  std::vector<std::pair<int, int>> st;
  for (auto& p : rim)
    if (light[(size_t)p.first * w + p.second]) st.push_back(p);
  for (auto& p : st) cut[(size_t)p.first * w + p.second] = 1;
  while (!st.empty()) {
    auto [y, x] = st.back();
    st.pop_back();
    for (int d = 0; d < 4; d++) {
      int qy = y + DY[d], qx = x + DX[d];
      if (0 <= qy && qy < h && 0 <= qx && qx < w && light[(size_t)qy * w + qx] && !cut[(size_t)qy * w + qx]) {
        cut[(size_t)qy * w + qx] = 1;
        st.push_back({qy, qx});
      }
    }
  }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      if (cut[(size_t)y * w + x]) {
        double mn = (double)std::min(std::min(P(y, x, 0), P(y, x, 1)), P(y, x, 2));
        double v = (255 - mn) * 255 / (255 - lo);
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        P(y, x, 3) = (uint8_t)v;
      }
    }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (!opaque[(size_t)y * w + x]) { P(y, x, 0) = 255; P(y, x, 1) = 255; P(y, x, 2) = 255; P(y, x, 3) = 0; }
  Image im;
  im.width = src.width;
  im.height = src.height;
  im.format = 5;
  im.data = gx_encode_rgb5a3(rg);
  m.images.push_back(std::move(im));
  classes.push_back(classes[i]);
  cache[i] = (int)m.images.size() - 1;
  return cache[i];
}

long long pyround_i(double x) { return (long long)std::nearbyint(x); }

void layer_placement(Texture& tx, const sdr::Layer& l, const std::set<int>& slid) {
  double su = l.scale[0], sv = l.scale[1];
  int rep[2] = {1, 1};
  double sc[2] = {1.0, 1.0};
  double xs[2] = {su, sv};
  for (int i = 0; i < 2; i++) {
    double x = xs[i];
    long long r = pyround_i(x);
    if (x > 0 && std::fabs(x - (double)r) < 1e-4 && 1 <= r && r <= 255) rep[i] = (int)r;
    else if (std::fabs(x) > 1e-6) sc[i] = 1.0 / x;
  }
  tx.repeat_s = rep[0];
  tx.repeat_t = rep[1];
  tx.scale = {sc[0], sc[1], 1.0};
  double tr[2] = {0.0, 0.0};
  uint32_t wraps[2] = {tx.wrap_s, tx.wrap_t};
  for (int i = 0; i < 2; i++) {
    double o = l.offset[i];
    if (slid.count(i + 1)) continue;
    long long r = pyround_i(o);
    if (std::fabs(o - (double)r) > 1e-4) tr[i] = -(o - std::floor(o));
    else if (wraps[i] == 0 && r) tr[i] = -(double)r;
  }
  if (!(tr[0] == 0.0 && tr[1] == 0.0)) tx.trans = {tr[0], tr[1], 0.0};
}

M4 to4(const Mat& m) {
  M4 r = {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], 0, 0, 0, 1};
  return r;
}

std::vector<double> mul_all(const M4& root, const std::vector<double>& w) {
  std::vector<double> out(w.size());
  for (size_t b = 0; b + 16 <= w.size(); b += 16) {
    M4 r = mm4(root.data(), &w[b]);
    std::copy(r.begin(), r.end(), out.begin() + b);
  }
  return out;
}

std::array<double, 3> mean_extent(const std::vector<double>& bo, int nf) {
  std::array<double, 3> s = {0, 0, 0};
  for (int f = 0; f < nf; f++)
    for (int k = 0; k < 3; k++) s[k] += bo[(size_t)f * 6 + 3 + k] - bo[(size_t)f * 6 + k];
  for (int k = 0; k < 3; k++) s[k] = s[k] / (double)nf;
  return s;
}

double sum3(const std::array<double, 3>& a) { return (a[0] + a[1]) + a[2]; }

std::array<double, 3> mean_extent_arr(const std::vector<std::array<double, 6>>& b) {
  std::array<double, 3> s = {0, 0, 0};
  for (auto& r : b)
    for (int k = 0; k < 3; k++) s[k] += r[3 + k] - r[k];
  for (int k = 0; k < 3; k++) s[k] = s[k] / (double)b.size();
  return s;
}

std::vector<std::array<double, 3>> smooth(const std::vector<std::array<double, 3>>& a, int half = 4) {
  std::vector<std::array<double, 3>> out(a.size());
  for (int i = 0; i < (int)a.size(); i++) {
    int lo = std::max(0, i - half), hi = std::min((int)a.size(), i + half + 1);
    std::array<double, 3> s = {0, 0, 0};
    for (int k = lo; k < hi; k++)
      for (int c = 0; c < 3; c++) s[c] += a[k][c];
    for (int c = 0; c < 3; c++) out[i][c] = s[c] / (double)(hi - lo);
  }
  return out;
}

std::array<double, 9> inv3(const double* m) {
  double a[3][3];
  for (int i = 0; i < 9; i++) a[i / 3][i % 3] = m[i];
  int perm[3] = {0, 1, 2};
  for (int c = 0; c < 3; c++) {
    int p = c;
    for (int r = c + 1; r < 3; r++)
      if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
    if (a[p][c] == 0.0) throw std::runtime_error("Singular matrix");
    if (p != c) {
      for (int k = 0; k < 3; k++) std::swap(a[p][k], a[c][k]);
      std::swap(perm[p], perm[c]);
    }
    for (int r = c + 1; r < 3; r++) {
      a[r][c] = a[r][c] / a[c][c];
      for (int k = c + 1; k < 3; k++) a[r][k] -= a[r][c] * a[c][k];
    }
  }
  std::array<double, 9> inv;
  for (int col = 0; col < 3; col++) {
    double b[3];
    for (int r = 0; r < 3; r++) b[r] = perm[r] == col ? 1.0 : 0.0;
    for (int r = 1; r < 3; r++)
      for (int k = 0; k < r; k++) b[r] -= a[r][k] * b[k];
    for (int r = 2; r >= 0; r--) {
      for (int k = r + 1; k < 3; k++) b[r] -= a[r][k] * b[k];
      b[r] /= a[r][r];
    }
    for (int r = 0; r < 3; r++) inv[r * 3 + col] = b[r];
  }
  return inv;
}

struct ActData {
  Decomp d;
  std::vector<char> visible;
  std::vector<double> bx;
  int frames = 0;
};

}

namespace {

std::optional<ConvertOut> convert_pass(const sdr::Sdr& sdr, const Pkx& xd_pkx, const Model& xd_model, double step, const Logger& log,
                                       const std::string& compensate, double tol_scale, const Model* xd_full,
                                       const std::vector<GraftRegion>* graft, const Gen4Info* gen4, const std::set<int>& pairs,
                                       ConvertInfo* info, StandinStats* stats) {
  Eval ev(sdr, compensate);
  if (gen4 && gen4->comp_t) ev.comp_t = true;
  {
    std::set<uint32_t> odd;
    for (auto& n : sdr.nodes)
      if (n.kind != 0 && n.kind != 2 && n.kind != 3 && n.kind != 6) odd.insert(n.kind);
    if (!odd.empty()) {
      std::string s = "[";
      bool first = true;
      for (uint32_t k : odd) { s += (first ? "" : ", ") + std::to_string(k); first = false; }
      log("NOTE: node kind " + s + "] (not known what it is): kept as a plain joint");
    }
  }
  Model m;
  Joint x0 = gen4 ? Joint() : xd_model.joints[0];
  Joint x1;
  if (xd_model.skeleton_root > 0 && !gen4) {
    auto xs = xd_model.world_static();
    Mat xm1 = mat_mul(mat_inv(xs[0]), xs[xd_model.skeleton_root]);
    mat_decompose(xm1, &x1.scale, &x1.rot, &x1.pos);
    Pose sp0 = ev.sample_static();
    std::vector<double> w0all = ev.world(sp0);
    M4 xm = to4(xm1);
    for (size_t i = 0; i < sdr.nodes.size(); i++) {
      const auto& nd = sdr.nodes[i];
      if (nd.kind == 2) break;
      const double* w0 = &w0all[i * 16];
      double dd[3] = {w0[3] - xm[3], w0[7] - xm[7], w0[11] - xm[11]};
      double dt = std::sqrt((dd[0] * dd[0] + dd[1] * dd[1]) + dd[2] * dd[2]);
      double ra[9] = {w0[0], w0[1], w0[2], w0[4], w0[5], w0[6], w0[8], w0[9], w0[10]};
      double rb[9] = {xm[0], xm[1], xm[2], xm[4], xm[5], xm[6], xm[8], xm[9], xm[10]};
      double ca = std::cbrt(std::max(std::fabs(det3(ra)), 1e-12)), cb = std::cbrt(std::max(std::fabs(det3(rb)), 1e-12));
      for (int k = 0; k < 9; k++) { ra[k] /= ca; rb[k] /= cb; }
      double tr = 0;
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) tr += ra[c * 3 + r] * rb[c * 3 + r];
      double cs = (tr - 1) / 2;
      double turn = std::acos(std::max(-1.0, std::min(1.0, cs))) * 180.0 / PI;
      double nw = std::sqrt((w0[3] * w0[3] + w0[7] * w0[7]) + w0[11] * w0[11]);
      double mx = 0;
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) mx = std::max(mx, std::fabs(ra[r * 3 + c] - (r == c ? 1.0 : 0.0)));
      bool plain = nw < 0.01 && mx < 0.01;
      double xn = std::sqrt((xm[3] * xm[3] + xm[7] * xm[7]) + xm[11] * xm[11]);
      if (!plain && dt < 0.1 * xn + 0.01 && turn < 20) {
        log("INFO: PBR node " + nd.name + " already holds the XD transform above the skeleton: not added twice");
        x1 = Joint();
        break;
      }
    }
  }
  for (const Joint* src : {&x0, &x1}) {
    Joint j;
    j.parent = (int)m.joints.size() - 1;
    j.rot = src->rot;
    j.scale = src->scale;
    j.pos = src->pos;
    m.joints.push_back(j);
  }
  m.skeleton_root = 1;
  Pose spose = ev.sample_static();
  Decomp d0 = decompose(spose, ev, pairs);
  std::vector<int> pl(pairs.begin(), pairs.end());
  int nn = (int)sdr.nodes.size();
  std::vector<int> jn;
  std::map<int, int> ja;
  int j_ = 2;
  for (int i = 0; i < nn; i++) {
    if (pairs.count(i)) {
      ja[i] = j_;
      j_++;
    }
    jn.push_back(j_);
    j_++;
  }
  std::map<int, int> node_of;
  for (int i = 0; i < nn; i++) node_of[jn[i]] = i;
  std::vector<int> ext_joint = jn;
  for (int i : pl) ext_joint.push_back(ja[i]);
  auto ds = [&](const std::vector<double>& a, int e) { return V3{a[(size_t)e * 3], a[(size_t)e * 3 + 1], a[(size_t)e * 3 + 2]}; };
  for (int i = 0; i < nn; i++) {
    const auto& n = sdr.nodes[i];
    int par = n.parent >= 0 ? jn[n.parent] : 1;
    if (pairs.count(i)) {
      Joint j;
      j.parent = par;
      int k = (int)(std::find(pl.begin(), pl.end(), i) - pl.begin());
      j.scale = ds(d0.s, nn + k);
      j.rot = ds(d0.r, nn + k);
      j.pos = ds(d0.t, nn + k);
      m.joints.push_back(j);
      par = ja[i];
    }
    Joint j;
    j.name = n.name;
    j.parent = par;
    j.scale = ds(d0.s, i);
    j.rot = ds(d0.r, i);
    j.pos = ds(d0.t, i);
    if (n.has_inv_bind) j.inv_bind = std::vector<double>(n.inv_bind, n.inv_bind + 12);
    m.joints.push_back(j);
  }
  std::map<int, int> holder;
  for (int i = 0; i < nn; i++) {
    const auto& n = sdr.nodes[i];
    if (n.kind == 3 && n.has_mesh && n.mesh.has_weights) {
      Joint j;
      j.name = n.name;
      j.parent = 1;
      holder[i] = (int)m.joints.size();
      m.joints.push_back(j);
    }
  }
  std::vector<std::string> alpha;
  for (auto& t : sdr.textures) {
    Image im;
    if (t.format < 0) throw std::runtime_error(sf("texture format code 0x%X not known", t.code));
    im.width = t.width;
    im.height = t.height;
    im.format = t.format;
    im.data = t.data;
    if (im.format == 10) {
      Rgba px = gx_decode_c14x2(im.width, im.height, im.data.data(), im.data.size(), (int)t.palette_format);
      im.data = gx_encode_rgb5a3(px);
      im.format = 5;
    }
    if (im.format == 8 || im.format == 9 || im.format == 10) throw std::runtime_error("palette texture: not handled");
    long long want = hsd_image_bytes(im.width, im.height, im.format);
    if ((long long)im.data.size() < want) throw std::runtime_error("texture data shorter than its size says");
    im.data.resize((size_t)want);
    alpha.push_back(texture_alpha(im.format, im.width, im.height, im.data));
    m.images.push_back(std::move(im));
  }
  std::map<std::string, int> classes;
  std::set<int> no_uv, with_uv;
  for (auto& n : sdr.nodes)
    if (n.kind == 3 && n.has_mesh && n.name.rfind("col_", 0) != 0)
      for (auto& part : n.mesh.parts) {
        bool has_uvs = !n.mesh.uvsets.empty() && !n.mesh.uvsets[0].empty();
        (has_uvs ? with_uv : no_uv).insert(part.material);
      }
  std::set<int> reflect;
  for (int x : no_uv)
    if (!with_uv.count(x)) reflect.insert(x);
  std::set<std::string> ref_names;
  for (int i : reflect)
    if (i >= 0) ref_names.insert(sdr.materials[i].name);
  for (size_t i = 0; i < sdr.materials.size(); i++)
    if (ref_names.count(sdr.materials[i].name) && sdr.materials[i].texture >= 0) reflect.insert((int)i);
  std::map<std::pair<int, int>, int> layer_tex;
  std::map<std::string, int> dimmed_cache;
  std::map<int, int> mattes;
  const std::vector<uint8_t> pe_glow = {0x19, 0x00, 0x00, 0x00, 0x01, 0x04, 0x01, 0x05, 0x03, 0x07, 0x00, 0x07};
  for (size_t mi = 0; mi < sdr.materials.size(); mi++) {
    const auto& pm = sdr.materials[mi];
    Material ma;
    ma.name = pm.name;
    ma.draw_class = "opa";
    std::vector<int> orig;
    for (size_t k = 0; k < pm.layers.size(); k++)
      if (pm.layers[k].texture >= 0) orig.push_back((int)k);
    std::vector<sdr::Layer> layers;
    for (int k : orig) layers.push_back(pm.layers[k]);
    if (layers.empty() && pm.texture >= 0) {
      sdr::Layer l;
      l.texture = pm.texture;
      l.uv = true;
      l.uvset = 0;
      l.offset[0] = l.offset[1] = 0.0;
      l.rotation = 0.0;
      l.scale[0] = l.scale[1] = 1.0;
      layers.push_back(l);
    }
    std::vector<int> order;
    for (size_t k = 0; k < layers.size(); k++)
      if (layers[k].uv) order.push_back((int)k);
    for (size_t k = 0; k < layers.size(); k++)
      if (!layers[k].uv) order.push_back((int)k);
    double shine = 1.0;
    bool masked = false;
    const std::vector<uint8_t>& raw = pm.raw;
    std::map<int, int> pos;
    for (size_t k = 0; k < orig.size(); k++) pos[orig[k]] = (int)k;
    std::optional<int> spec;
    if (raw[0x76] != 0xFF && pos.count(raw[0x76])) spec = pos[raw[0x76]];
    if (spec && layers[*spec].uv) spec.reset();
    if (spec && *spec == 0) {
      bool anyuv = false;
      for (auto& l : layers) anyuv = anyuv || l.uv;
      if (anyuv) spec.reset();
    }
    double spec_f = 0.0;
    if (spec) {
      spec_f = (double)(raw[0x78] + raw[0x79] + raw[0x7A]) / 765.0;
      std::optional<int> mk;
      if (raw[0x74] != 0xFF && pos.count(raw[0x74])) mk = pos[raw[0x74]];
      if (mk && *mk != *spec && layers[*mk].uv) {
        const auto& tm = sdr.textures[layers[*mk].texture];
        if (tm.format == 0 || tm.format == 1) {
          spec_f *= brightness(tm);
          std::vector<int> no;
          for (int k : order)
            if (k != *mk) no.push_back(k);
          order = no;
        }
      }
    }
    if (spec) {
    } else if (!order.empty()) {
      bool anyrefl = false;
      for (size_t q = 1; q < order.size(); q++) anyrefl = anyrefl || !layers[order[q]].uv;
      if (anyrefl) {
        std::vector<int> masks;
        for (size_t q = 1; q < order.size(); q++) {
          int k = order[q];
          int f = sdr.textures[layers[k].texture].format;
          if (layers[k].uv && (f == 0 || f == 1)) masks.push_back(k);
        }
        shine = 0.5;
        if (!masks.empty()) {
          shine = 1.0;
          masked = true;
          for (int k : masks) shine *= brightness(sdr.textures[layers[k].texture]);
          std::vector<int> no;
          for (int k : order)
            if (std::find(masks.begin(), masks.end(), k) == masks.end()) no.push_back(k);
          order = no;
        }
      }
    }
    if (!order.empty()) {
      const auto& base_px = sdr.textures[layers[order[0]].texture].data;
      std::vector<int> no = {order[0]};
      for (size_t q = 1; q < order.size(); q++)
        if (sdr.textures[layers[order[q]].texture].data != base_px) no.push_back(order[q]);
      order = no;
    }
    if (!(raw[0x5D] & 0x01) && (raw[0x5D] & 0x02) && !order.empty()) {
      std::vector<int> no = {order[0]};
      for (size_t q = 1; q < order.size(); q++) {
        int k = order[q];
        int f = sdr.textures[layers[k].texture].format;
        if (!(layers[k].uv && (f == 0 || f == 1))) no.push_back(k);
      }
      order = no;
    }
    std::map<int, std::set<int>> animated;
    for (auto& la : pm.layer_anims) {
      std::set<int> axes;
      for (auto& act : la.second.items)
        for (auto& c : act.second)
          if (c.what == 0x14) axes.insert(c.axis);
      animated[la.first] = axes;
    }
    if (!layers.empty()) {
      const sdr::Layer& first = layers[order[0]];
      Texture tx;
      tx.image = first.texture;
      const auto& t = sdr.textures[first.texture];
      tx.wrap_s = t.wrap_s;
      tx.wrap_t = t.wrap_t;
      ma.draw_class = alpha[first.texture];
      bool amb_not_white = !(pm.ambient[0] == 255 && pm.ambient[1] == 255 && pm.ambient[2] == 255);
      if (reflect.count((int)mi) || !first.uv) {
        ma.draw_class = "opa";
        tx.flags = 0x00030081;
        tx.blending = 0.65;
        if (spec && order[0] == *spec) tx.blending = std::max(0.05, spec_f);
        if (!first.uv) {
          ma.ambient = {(double)pm.ambient[0], (double)pm.ambient[1], (double)pm.ambient[2], (double)pm.ambient[3]};
          ma.diffuse = {(double)pm.diffuse[0], (double)pm.diffuse[1], (double)pm.diffuse[2], (double)pm.diffuse[3]};
        }
      } else if ((t.format == 0 || t.format == 1) && (raw[0x5D] & 0x60) == 0x60 && amb_not_white) {
        ma.draw_class = "opa";
      } else if (ma.draw_class != "opa") {
        tx.flags = 0x00230010;
        ma.rendermode = ma.draw_class == "texedge" ? 0x40002015 : 0x60002015;
        if (t.format == 0 || t.format == 1) {
          ma.diffuse = {(double)pm.diffuse[0], (double)pm.diffuse[1], (double)pm.diffuse[2], (double)pm.diffuse[3]};
          tx.flags = 0x00240010;
          int fl0 = raw[0x5D];
          if ((fl0 & 0x01) && !(fl0 & 0x62)) {
            int top = std::max(std::max((int)pm.diffuse[0], std::max((int)pm.diffuse[1], (int)pm.diffuse[2])), 1);
            std::vector<double> c;
            for (int k = 0; k < 3; k++) c.push_back((double)std::min<long long>(255, pyround_i(pm.diffuse[k] * 255.0 / top)));
            c.push_back(255);
            ma.diffuse = ma.ambient = c;
            ma.rendermode &= ~0x4u;
          }
        }
      }
      int fl = raw[0x5D];
      uint8_t glow[3] = {raw[0x70], raw[0x71], raw[0x72]};
      if (!(fl & 0x01) && (fl & 0x02) && !(glow[0] == 0 && glow[1] == 0 && glow[2] == 0)) {
        std::vector<double> c;
        for (int k = 0; k < 3; k++) c.push_back((double)std::min(255, (int)pm.ambient[k] + glow[k]));
        c.push_back(255);
        ma.diffuse = ma.ambient = c;
        tx.flags = 0x00240010;
        if (fl & 0x60) {
          ma.rendermode = ma.rendermode & ~0x4u;
        } else {
          ma.draw_class = "xlu";
          ma.rendermode = 0x60002011;
          ma.pe = pe_glow;
        }
      }
      ma.textures.push_back(tx);
      for (size_t q = 0; q < order.size(); q++) {
        int k = order[q];
        const sdr::Layer& l = layers[k];
        if (q) {
          Texture t2;
          t2.image = l.texture;
          const auto& tl = sdr.textures[l.texture];
          t2.wrap_s = tl.wrap_s;
          t2.wrap_t = tl.wrap_t;
          bool grey = tl.format == 0 || tl.format == 1;
          bool see = alpha[l.texture] != "opa";
          bool mid = l.uv && grey && k > 1 && mid_sheet(tl);
          bool ring_add = false;
          for (int j : order) {
            if (j > 1 && layers[j].uv) {
              int f = sdr.textures[layers[j].texture].format;
              if ((f == 0 || f == 1) && mid_sheet(sdr.textures[layers[j].texture])) ring_add = true;
            }
          }
          if (spec && k == *spec) {
            t2.flags = 0x00070081;
            t2.blending = 1.0;
            if (spec_f < 0.98) t2.image = dimmed(m, alpha, l.texture, spec_f, dimmed_cache);
          } else if (l.uv && (grey || !see) && brightness(tl) < 0.3) {
            t2.flags = 0x00070010;
            t2.blending = 1.0;
            if (ring_add) t2.image = dimmed(m, alpha, l.texture, 0.3, dimmed_cache);
          } else if (l.uv) {
            t2.flags = (see && !grey) ? 0x00010010 : 0x00040010;
            t2.blending = 1.0;
            if (mid) t2.image = dimmed(m, alpha, l.texture, 2.0, dimmed_cache, 160);
            if (see && !grey) t2.image = matte_cut(m, alpha, l.texture, mattes);
          } else if (shine < 1.0 && !masked && (tl.format == 0 || tl.format == 1)) {
            t2.flags = 0x00010081;
            t2.blending = 1.0;
          } else if (!masked && tl.format != 2 && tl.format != 3 && !see) {
            t2.flags = 0x00030081;
            t2.blending = 0.4;
          } else {
            t2.flags = 0x00070081;
            t2.blending = 1.0;
            if (shine < 0.98) t2.image = dimmed(m, alpha, l.texture, shine, dimmed_cache);
          }
          ma.textures.push_back(t2);
          ma.rendermode |= 0x10u << q;
        }
        Texture& tq = ma.textures[q];
        tq.tex_id = (uint32_t)q;
        layer_tex[{(int)mi, k}] = (int)q;
        if (l.uv && !(q == 0 && reflect.count((int)mi))) {
          tq.src = 4 + std::min(l.uvset, 2);
          static const std::set<int> none;
          auto ai = animated.find(k);
          layer_placement(tq, l, ai == animated.end() ? none : ai->second);
        }
      }
      bool all_uv = true, all_refl = true;
      for (auto& l : layers) {
        all_uv = all_uv && l.uv;
        all_refl = all_refl && !l.uv;
      }
      if (pm.diffuse[3] < 255 && ((all_uv && (raw[0x5D] & 0x60) == 0x60) || all_refl)) {
        ma.alpha = pm.diffuse[3] / 255.0;
        ma.draw_class = "xlu";
        ma.rendermode |= 0x60002000u;
      }
    } else {
      ma.rendermode = 0x2005;
      ma.ambient = {(double)pm.ambient[0], (double)pm.ambient[1], (double)pm.ambient[2], (double)pm.ambient[3]};
      ma.diffuse = {(double)pm.diffuse[0], (double)pm.diffuse[1], (double)pm.diffuse[2], (double)pm.diffuse[3]};
    }
    m.materials.push_back(std::move(ma));
  }
  std::vector<double> sw_all = ev.world(spose);
  std::set<Weights> combos;
  std::vector<Mat> world_static = m.world_static();
  Mat root_inv = mat_inv(world_static[1]);
  int hidden_boxes = 0;
  for (int i = 0; i < nn; i++) {
    const auto& n = sdr.nodes[i];
    if (n.kind != 3 || !n.has_mesh) continue;
    if (n.name.rfind("col_", 0) == 0) {
      hidden_boxes++;
      continue;
    }
    {
      std::string lower = n.name;
      for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
      if (lower.find("shadow") != std::string::npos) {
        bool all_none = true;
        for (auto& p_ : n.mesh.parts) {
          if (p_.material < 0) throw std::runtime_error("TypeError: list indices must be integers or slices, not NoneType");
          if (sdr.materials[p_.material].texture >= 0) all_none = false;
        }
        if (all_none) {
          hidden_boxes++;
          continue;
        }
      }
    }
    const auto& pmesh = n.mesh;
    const double* sw = &sw_all[(size_t)i * 16];
    double wm[9] = {sw[0], sw[1], sw[2], sw[4], sw[5], sw[6], sw[8], sw[9], sw[10]};
    bool mirrored = det3(wm) < 0;
    for (auto& part : pmesh.parts) {
      Mesh me;
      me.rigid = !pmesh.has_weights;
      me.joint = me.rigid ? jn[i] : holder[i];
      me.material = part.material >= 0 ? std::optional<int>(part.material) : std::nullopt;
      me.name = n.name;
      std::map<std::vector<double>, int> index;
      for (auto& tri : part.tris) {
        std::vector<int> out;
        for (int ci = 0; ci < 3; ci++) {
          const sdr::Corner& c = tri.c[ci];
          if (c.pos < 0) throw std::runtime_error("TypeError: list indices must be integers or slices, not NoneType");
          const auto& vp = pmesh.verts.at(c.pos);
          const auto& vn = pmesh.verts.at(c.nrm >= 0 ? c.nrm : c.pos);
          V3 pos = {vp.p[0], vp.p[1], vp.p[2]};
          V3 nrm = {vn.n[0], vn.n[1], vn.n[2]};
          const std::vector<std::pair<double, double>> empty;
          const auto& uvs0 = pmesh.uvsets.empty() ? empty : pmesh.uvsets[0];
          std::optional<std::array<double, 2>> uv;
          if (c.uv0 >= 0 && c.uv0 < (int)uvs0.size()) uv = std::array<double, 2>{uvs0[c.uv0].first, uvs0[c.uv0].second};
          Vertex v;
          v.pos = pos;
          v.nrm = nrm;
          v.uv = uv;
          if (uv && pmesh.uvsets.size() > 1) {
            v.has_uvx = true;
            int more[2] = {c.uv1, c.uv2};
            for (int k = 0; k < 2; k++) {
              int x = more[k];
              if (x >= 0 && k + 1 < (int)pmesh.uvsets.size() && x < (int)pmesh.uvsets[k + 1].size())
                v.uvx.push_back(std::array<double, 2>{pmesh.uvsets[k + 1][x].first, pmesh.uvsets[k + 1][x].second});
              else
                v.uvx.push_back(std::nullopt);
            }
          }
          if (!me.rigid) {
            std::vector<std::pair<int, double>> wd = pmesh.weights[c.pos];
            if (wd.empty()) wd.emplace_back(n.number, 1.0);
            Weights w = quantise_weights(wd, step);
            Weights w2;
            for (auto& jw : w) {
              int node = -1;
              for (auto& bn : sdr.by_number)
                if (bn.first == jw.first) node = bn.second;
              if (node < 0) throw std::runtime_error("KeyError");
              w2.emplace_back(jn[node], jw.second);
            }
            combos.insert(w2);
            v.weights = w2;
          }
          std::vector<double> k = {v.pos[0], v.pos[1], v.pos[2], (*v.nrm)[0], (*v.nrm)[1], (*v.nrm)[2], v.uv ? 1.0 : 0.0};
          if (v.uv) { k.push_back((*v.uv)[0]); k.push_back((*v.uv)[1]); }
          k.push_back(v.weights ? 1.0 : 0.0);
          if (v.weights) {
            k.push_back((double)v.weights->size());
            for (auto& jw : *v.weights) { k.push_back(jw.first); k.push_back(jw.second); }
          }
          k.push_back(v.has_uvx ? 1.0 : 0.0);
          if (v.has_uvx)
            for (auto& x : v.uvx) {
              k.push_back(x ? 1.0 : 0.0);
              if (x) { k.push_back((*x)[0]); k.push_back((*x)[1]); }
            }
          bool has_nan = false;
          for (double x : k) has_nan = has_nan || x != x;
          int vi;
          auto it = has_nan ? index.end() : index.find(k);
          if (it == index.end()) {
            vi = (int)me.verts.size();
            if (!has_nan) index[k] = vi;
            me.verts.push_back(std::move(v));
          } else {
            vi = it->second;
          }
          out.push_back(vi);
        }
        if (mirrored) out = {out[0], out[2], out[1]};
        if (out[0] != out[1] && out[0] != out[2] && out[1] != out[2]) me.tris.push_back({out[0], out[1], out[2]});
      }
      if (!me.tris.empty()) {
        std::string c = me.material ? m.materials[*me.material].draw_class : "opa";
        classes[c]++;
        m.meshes.push_back(std::move(me));
      }
    }
  }
  if (hidden_boxes) log(sf("INFO: %d touch box(es) (col_ meshes) left out", hidden_boxes));
  if (!reflect.empty()) log(sf("INFO: %d reflection-mapped material(s) (texture on a mesh without coordinates)", (int)reflect.size()));
  if (classes.count("texedge") && classes["texedge"]) log(sf("INFO: %d cut-out mesh part(s)", classes["texedge"]));
  if (classes.count("xlu") && classes["xlu"]) log(sf("INFO: %d see-through (blended) mesh part(s)", classes["xlu"]));
  for (auto& me : m.meshes) {
    if (me.rigid) continue;
    for (auto& v : me.verts)
      for (auto& jw : *v.weights)
        if (!m.joints[jw.first].inv_bind) {
          log("NOTE: skin weight on a node that is not a joint: bound where it stands");
          Mat r = mat_inv(mat_mul(root_inv, world_static[jw.first]));
          m.joints[jw.first].inv_bind = std::vector<double>(r.begin(), r.end());
        }
  }
  log(sf("skin: %d different weight combinations at step %g", (int)combos.size(), step));
  std::vector<int> xd_frames;
  for (auto& a : xd_model.animations) xd_frames.push_back((int)std::nearbyint(a.end()));
  PickResult pr = gen4 ? pick_actions_named(sdr, xd_pkx, xd_frames, gen4->pick_names, log) : pick_actions(sdr, xd_pkx, xd_frames, log);
  std::vector<std::optional<int>>& pick = pr.pick;
  std::map<int, std::string>& how = pr.how;
  const Users& users = pr.users;
  if (stats) {
    for (size_t i = 0; i < pick.size(); i++)
      if (pick[i]) {
        stats->pick_names[(int)i] = sdr.actions[*pick[i]].name;
        stats->pick_times[(int)i] = {sdr.actions[*pick[i]].f[1], sdr.actions[*pick[i]].f[2]};
      }
  }
  struct TMap {
    int frames;
    std::function<double(double)> fn;
  };
  std::map<int, TMap> tmaps;
  if (gen4) {
    for (size_t slot = 0; slot < pick.size(); slot++) {
      auto tg = gen4->pick_times.find((int)slot);
      if (!pick[slot] || how.find((int)slot) == how.end() || how[(int)slot] != "name" || tmaps.count(*pick[slot]) || tg == gen4->pick_times.end()) continue;
      int a = *pick[slot];
      double hs = tg->second.first, es = tg->second.second;
      double hg = sdr.actions[a].f[1], eg = sdr.actions[a].f[2];
      std::function<double(double)> fn;
      if (hs <= 0.05 || hg <= 0.05 || hs >= es - 0.05 || hg >= eg - 0.05) {
        double k = eg / es;
        fn = [k](double t) { return t * k; };
      } else {
        fn = [hs, es, hg, eg](double t) { return t <= hs ? t * hg / hs : hg + (t - hs) * (eg - hg) / (es - hs); };
      }
      tmaps[a] = {std::max((int)std::nearbyint(es * FPS), 1), fn};
    }
  }
  std::vector<std::string> names;
  for (auto& a : sdr.actions) names.push_back(a.name);
  std::vector<int> eye_mats;
  for (size_t i = 0; i < sdr.materials.size(); i++)
    if (sdr.materials[i].anim()) eye_mats.push_back((int)i);
  std::map<int, int> face_slots;
  std::set<int> face_kept;
  for (int r = 0; r < 4; r++) {
    const char* want = FACE_ACTIONS[r];
    int rec = 0x1C + r * 0x13;
    int on = xd_pkx.header[rec], idx = xd_pkx.header[rec + 0x12];
    int ni = want ? index_in(names, want) : -1;
    if (on && want && ni >= 0 && !eye_mats.empty() && idx < (int)xd_frames.size()) {
      pick[idx] = ni;
      how[idx] = "face";
      face_slots[idx] = ni;
      face_kept.insert(r);
    }
  }
  if (!eye_mats.empty()) {
    std::string fk;
    for (int r : face_kept) fk += (fk.empty() ? "" : ", ") + std::string(FACE_ACTIONS[r]);
    log(sf("INFO: eyes: %d material(s) animated; face records kept: %s", (int)eye_mats.size(), fk.empty() ? "none" : fk.c_str()));
  }
  int idle_slot = 0;
  if (!xd_pkx.sections.empty()) {
    for (auto& ki : xd_pkx.section_motions(0))
      if (ki.first && 0 <= ki.second && ki.second < (int)xd_frames.size()) {
        idle_slot = ki.second;
        break;
      }
  }
  M4 j0m = to4(mat_srt(x0.scale, x0.rot, x0.pos));
  auto root_matrix = [&](const V3& scale) {
    M4 r = to4(mat_srt(scale, x1.rot, x1.pos));
    return mm4(j0m, r);
  };
  std::map<int, Pose> poses;
  {
    std::set<int> acts;
    for (auto& p : pick)
      if (p) acts.insert(*p);
    for (int a : acts) {
      auto tm = tmaps.find(a);
      poses.emplace(a, tm != tmaps.end() ? ev.sample(a, tm->second.frames, tm->second.fn) : ev.sample(a));
    }
  }
  std::optional<int> idle_act = pick.empty() ? std::nullopt : pick[idle_slot];
  const std::vector<std::array<double, 6>>* xb = nullptr;
  if (!xd_model.animations.empty() && xd_model.animations[idle_slot].bounds && !xd_model.animations[idle_slot].bounds->empty())
    xb = &*xd_model.animations[idle_slot].bounds;
  bool x1_nontrivial = false;
  for (int k = 0; k < 3; k++) x1_nontrivial = x1_nontrivial || std::fabs(x1.rot[k]) > 1e-4 || std::fabs(x1.pos[k]) > 1e-4 || std::fabs(x1.scale[k] - 1) > 1e-4;
  if (idle_act && xb && x1_nontrivial) {
    const Pose& po = poses.at(*idle_act);
    auto fit = [&](const Joint& c) {
      M4 r = to4(mat_srt(c.scale, c.rot, c.pos));
      std::vector<double> pts = ev.world(po);
      auto bo_of = [&](const M4& mr) {
        M4 jr = mm4(j0m, mr);
        return boxes(ev.points(mul_all(jr, pts), po.frames, po.visible.data()), po.frames);
      };
      std::vector<double> bo = bo_of(r);
      auto xe = mean_extent_arr(*xb);
      double q = sum3(xe) / std::max(sum3(mean_extent(bo, po.frames)), 1e-9);
      for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) r[i * 4 + k] *= q;
      bo = bo_of(r);
      double h = 1e-6;
      for (int k = 0; k < 3; k++) h = std::max(h, bo[3 + k] - bo[k]);
      int nfr = std::min(po.frames, (int)xb->size());
      std::vector<double> dd;
      for (int f = 0; f < nfr; f++)
        for (int k = 0; k < 6; k++) dd.push_back(std::fabs(bo[(size_t)f * 6 + k] - (*xb)[f][k]));
      return pairwise_sum(dd.data(), dd.size()) / (double)dd.size() / h;
    };
    Joint plain;
    double d_xd = fit(x1), d_plain = fit(plain);
    if (d_plain < 0.7 * d_xd) {
      log(sf("INFO: the XD transform above the skeleton left out: idle box %.1f %% apart with it, %.1f %% without", 100 * d_xd, 100 * d_plain));
      x1 = plain;
      m.joints[1].rot = x1.rot;
      m.joints[1].pos = x1.pos;
    }
  }
  V3 scale1 = x1.scale;
  if (gen4) {
    log("INFO: size: PBR's own (its units are XD's: median XD/PBR idle box 1.005 over the 387 shared models)");
  } else if (idle_act && xb) {
    const Pose& po = poses.at(*idle_act);
    M4 rm_ = root_matrix(scale1);
    auto ours = boxes(ev.points(mul_all(rm_, ev.world(po)), po.frames, po.visible.data()), po.frames);
    auto eo = mean_extent(ours, po.frames);
    auto ex = mean_extent_arr(*xb);
    double ratio = sum3(ex) / std::max(sum3(eo), 1e-9);
    if (std::fabs(ratio - 1.0) > 0.04) {
      for (int k = 0; k < 3; k++) scale1[k] = scale1[k] * ratio;
      log(sf("INFO: size factor %.3f on top of XD's own (the PBR model is made in another size)", ratio));
    }
    if (ratio > 2.5 || ratio < 0.4) log("NOTE: size factor far from 1: PBR's model or its idle pose is very different from XD's");
  }
  m.joints[1].scale = scale1;
  M4 root = root_matrix(scale1);
  std::map<int, ActData> data;
  double worst = 0.0;
  std::string worst_name;
  int worst_frame = 0;
  double height = -1;
  for (auto& [a, po] : poses) {
    Decomp dd = decompose(po, ev, pairs);
    std::vector<double> wld = ev.world(po);
    std::vector<double> rw = mul_all(root, wld);
    auto ref = ev.points(rw, po.frames, nullptr);
    std::vector<double> gotw = mul_all(root, ev.world_of(compose_local(dd, nn, pairs), po.frames));
    auto got = ev.points(gotw, po.frames, nullptr);
    std::vector<double> bx = boxes(ev.points(rw, po.frames, po.visible.data()), po.frames);
    if (height < 0 || (idle_act && a == *idle_act)) {
      double h = 1e-6;
      for (int k = 0; k < 3; k++) h = std::max(h, bx[3 + k] - bx[k]);
      height = h;
    }
    for (size_t s = 0; s < ref.size(); s++) {
      int nv = ref[s].nv;
      for (int f = 0; f < po.frames; f++) {
        double mx = -1;
        for (int k = 0; k < nv * 3; k++) {
          double dv = std::fabs(ref[s].pts[(size_t)f * nv * 3 + k] - got[s].pts[(size_t)f * nv * 3 + k]);
          if (dv > mx || dv != dv) mx = dv;
        }
        if (nv && mx > worst) {
          worst = mx;
          worst_name = sdr.actions[a].name;
          worst_frame = f;
        }
      }
    }
    ActData ad;
    ad.d = std::move(dd);
    ad.visible = po.visible;
    ad.bx = std::move(bx);
    ad.frames = po.frames;
    data[a] = std::move(ad);
  }
  if (height > 0) {
    double pct = 100.0 * worst / height;
    if (info && pairs.empty() && pct > POSE_TOL_PCT) {
      std::vector<double> err(nn, 0.0);
      for (auto& [a, po] : poses) {
        auto e = shear_error(po, ev);
        for (int i = 0; i < nn; i++) err[i] = std::max(err[i], e[i]);
      }
      std::vector<int> need;
      for (int i = 0; i < nn; i++)
        if (err[i] > SHEAR_TOL) need.push_back(i);
      if (!need.empty()) {
        info->need = need;
        info->pct = pct;
        return std::nullopt;
      }
    }
    log(sf("CHECK: converted pose against the source, largest distance %.2f %% of the height (%s frame %d)", pct, worst_name.c_str(), worst_frame));
    if (pct > 3.0) log("WRONG: pose differs from the source by more than 3 % of the height (scale compensation shear)");
    if (xb && idle_act && !gen4) {
      const auto& ob = data.at(*idle_act).bx;
      int nfr = std::min(data.at(*idle_act).frames, (int)xb->size());
      std::vector<double> dd;
      for (int f = 0; f < nfr; f++)
        for (int k = 0; k < 6; k++) dd.push_back(std::fabs(ob[(size_t)f * 6 + k] - (*xb)[f][k]));
      double d = pairwise_sum(dd.data(), dd.size()) / (double)dd.size() / height;
      log(sf("CHECK: idle box against XD's own model: %.1f %% of the height apart on average", 100.0 * d));
    }
  }
  std::map<int, std::vector<std::array<double, 3>>> travel;
  for (size_t slot = 0; slot < pick.size(); slot++) {
    if (!pick[slot] || !users.has((int)slot) || travel.count(*pick[slot])) continue;
    int a = *pick[slot];
    const ActData& ad = data.at(a);
    int nf = ad.frames;
    std::vector<std::array<double, 3>> c(nf);
    for (int f = 0; f < nf; f++)
      for (int k = 0; k < 3; k++) c[f][k] = (ad.bx[(size_t)f * 6 + k] + ad.bx[(size_t)f * 6 + 3 + k]) / 2;
    double own = 0.0;
    for (int f = 0; f < nf; f++)
      for (int k : {0, 2}) own = std::max(own, std::fabs(c[f][k] - c[0][k]));
    double xown = 0.0;
    const auto& xbs = xd_model.animations[slot].bounds;
    if (xbs && !xbs->empty()) {
      std::vector<std::array<double, 3>> xc;
      for (auto& r : *xbs) xc.push_back({(r[0] + r[3]) / 2, (r[1] + r[4]) / 2, (r[2] + r[5]) / 2});
      for (size_t f = 0; f < xc.size(); f++)
        for (int k : {0, 2}) xown = std::max(xown, std::fabs(xc[f][k] - xc[0][k]));
    }
    if (own > 0.3 * height && xown < 0.5 * own) {
      std::vector<std::array<double, 3>> off(nf);
      for (int f = 0; f < nf; f++)
        for (int k = 0; k < 3; k++) off[f][k] = c[f][k] - c[0][k];
      off = smooth(off);
      for (auto& o : off) o[1] = 0.0;
      for (auto& o : off)
        for (int k = 0; k < 3; k++) o[k] = -o[k];
      travel[a] = off;
      log(sf("NOTE: \"%s\" carries the body %.0f %% of its height away and XD's animation stays in place: travel taken out",
             sdr.actions[a].name.c_str(), 100.0 * own / height));
    }
  }
  int ne = nn + (int)pl.size();
  std::vector<char> chan((size_t)ne * 9, 0);
  double tols[3] = {0.001 * tol_scale, 0.0003 * tol_scale, 0.001 * tol_scale};
  std::vector<char> switched(nn, 0);
  const std::vector<double>* stat[3] = {&d0.s, &d0.r, &d0.t};
  for (auto& [a, ad] : data) {
    const std::vector<double>* arrs[3] = {&ad.d.s, &ad.d.r, &ad.d.t};
    for (int w = 0; w < 3; w++)
      for (int i = 0; i < ne; i++)
        for (int ax = 0; ax < 3; ax++) {
          double mx = 0;
          for (int f = 0; f < ad.frames; f++) {
            double dv = std::fabs((*arrs[w])[((size_t)f * ne + i) * 3 + ax] - (*stat[w])[(size_t)i * 3 + ax]);
            if (dv > mx || dv != dv) mx = dv;
          }
          if (mx > tols[w]) chan[((size_t)i * 3 + w) * 3 + ax] = 1;
        }
    for (int i = 0; i < nn; i++) {
      bool all = true;
      for (int f = 0; f < ad.frames; f++) all = all && ad.visible[(size_t)f * nn + i];
      if (!all) switched[i] = 1;
    }
  }
  std::vector<int> vis_nodes;
  for (int i = 0; i < nn; i++)
    if (sdr.nodes[i].kind == 3 && sdr.nodes[i].has_mesh && switched[i]) vis_nodes.push_back(i);
  if (!vis_nodes.empty()) log(sf("INFO: %d mesh node(s) switched on and off by the animations (visibility tracks written)", (int)vis_nodes.size()));
  const int kinds[3] = {J_SCAX, J_ROTX, J_TRAX};
  double j0m3[9] = {j0m[0], j0m[1], j0m[2], j0m[4], j0m[5], j0m[6], j0m[8], j0m[9], j0m[10]};
  auto j0inv = inv3(j0m3);
  std::map<int, int> n_dobj;
  std::map<int, std::vector<std::pair<int, int>>> mat_places;
  for (auto& me : m.meshes) {
    int k = n_dobj.count(me.joint) ? n_dobj[me.joint] : 0;
    n_dobj[me.joint] = k + 1;
    mat_places[me.material ? *me.material : -1].emplace_back(me.joint, k);
  }
  auto material_anims = [&](int a, int frames) {
    std::vector<std::pair<int, MatList>> out;
    for (int mi : eye_mats) {
      auto layer_list = sdr.materials[mi].layer_anims;
      std::sort(layer_list.begin(), layer_list.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
      std::vector<TexAnim> tas;
      for (auto& [layer, blocks] : layer_list) {
        std::vector<Track> tracks;
        const std::vector<sdr::Curve>* cs = blocks.find(a);
        if (cs)
          for (const auto& c : *cs) {
            if (c.what == 0x14 && (c.axis == 1 || c.axis == 2) && !c.values.empty()) {
              auto tmi = tmaps.find(a);
              std::vector<double> vals;
              for (int f = 0; f <= frames; f++) {
                double t = f / FPS;
                if (tmi != tmaps.end()) t = tmi->second.fn(t);
                vals.push_back(-py_round(c.at(t), 4));
              }
              Track tk;
              tk.kind = c.axis == 1 ? T_TRAU : T_TRAV;
              tk.keys = step_keys(vals);
              tracks.push_back(std::move(tk));
            }
          }
        if (!tracks.empty() && layer_tex.count({mi, layer})) {
          AnimObj ao;
          ao.end = (double)frames;
          ao.tracks = tracks;
          TexAnim ta;
          ta.tex_id = (uint32_t)layer_tex[{mi, layer}];
          ta.aobj = ao;
          tas.push_back(std::move(ta));
        }
      }
      if (tas.empty()) continue;
      auto pl_it = mat_places.find(mi);
      if (pl_it == mat_places.end()) continue;
      for (auto& [j, d] : pl_it->second) {
        MatAnim ma;
        ma.texanims = tas;
        MatList* ml = nullptr;
        for (auto& kv : out)
          if (kv.first == j) ml = &kv.second;
        if (!ml) {
          out.emplace_back(j, MatList());
          ml = &out.back().second;
          ml->items.assign(n_dobj[j], std::nullopt);
        }
        ml->items[d] = std::move(ma);
      }
    }
    return out;
  };
  auto set_materials = [&](Animation& an, std::vector<std::pair<int, MatList>>&& v) {
    for (auto& kv : v) an.materials.emplace_back(kv.first, std::move(kv.second));
  };
  std::map<int, size_t> made;
  for (size_t slot = 0; slot < pick.size(); slot++) {
    Animation an;
    std::optional<int> aopt = pick[slot];
    if (face_slots.count((int)slot)) {
      int a = *aopt;
      int frames = std::max((int)std::nearbyint(sdr.actions[a].f[2] * FPS), 1);
      an.name = sdr.actions[a].name;
      AnimObj ao;
      ao.end = (double)frames;
      Track tk;
      tk.kind = J_SCAX;
      tk.keys = {{OP_CON, scale1[0], 0.0, frames}, {OP_CON, scale1[0], 0.0, 0}};
      ao.tracks.push_back(tk);
      an.set_joint(1, ao);
      set_materials(an, material_anims(a, frames));
      m.animations.push_back(std::move(an));
      continue;
    }
    if (!aopt) {
      int frames = std::max(xd_frames[slot], 1);
      an.name = "none";
      AnimObj ao;
      ao.end = (double)frames;
      Track tk;
      tk.kind = J_SCAX;
      tk.keys = {{OP_CON, scale1[0], 0.0, frames}, {OP_CON, scale1[0], 0.0, 0}};
      ao.tracks.push_back(tk);
      an.set_joint(1, ao);
      m.animations.push_back(std::move(an));
      continue;
    }
    int a = *aopt;
    if (made.count(a)) {
      const Animation& src = m.animations[made[a]];
      an.name = src.name;
      an.joints = src.joints;
      an.bounds = src.bounds;
      an.materials = src.materials;
      m.animations.push_back(std::move(an));
      continue;
    }
    an.name = sdr.actions[a].name;
    ActData& ad = data.at(a);
    int frames = ad.frames - 1;
    set_materials(an, material_anims(a, frames));
    const std::vector<double>* arrs[3] = {&ad.d.s, &ad.d.r, &ad.d.t};
    for (int i = 0; i < ne; i++) {
      AnimObj ao;
      ao.end = (double)frames;
      for (int w = 0; w < 3; w++)
        for (int ax = 0; ax < 3; ax++)
          if (chan[((size_t)i * 3 + w) * 3 + ax]) {
            std::vector<double> samples;
            for (int f = 0; f < ad.frames; f++) samples.push_back((*arrs[w])[((size_t)f * ne + i) * 3 + ax]);
            Track tk;
            tk.kind = kinds[w] + ax;
            tk.keys = keys_of(samples, tols[w]);
            ao.tracks.push_back(std::move(tk));
          }
      if (i < nn && std::find(vis_nodes.begin(), vis_nodes.end(), i) != vis_nodes.end() && !holder.count(i)) {
        std::vector<char> col;
        for (int f = 0; f < ad.frames; f++) col.push_back(ad.visible[(size_t)f * nn + i]);
        Track tk;
        tk.kind = 11;
        tk.keys = vis_keys(col);
        ao.tracks.push_back(std::move(tk));
      }
      if (!ao.tracks.empty()) an.set_joint(ext_joint[i], std::move(ao));
    }
    for (int i : vis_nodes)
      if (holder.count(i)) {
        AnimObj ao;
        ao.end = (double)frames;
        std::vector<char> col;
        for (int f = 0; f < ad.frames; f++) col.push_back(ad.visible[(size_t)f * nn + i]);
        Track tk;
        tk.kind = 11;
        tk.keys = vis_keys(col);
        ao.tracks.push_back(std::move(tk));
        an.set_joint(holder[i], std::move(ao));
      }
    std::vector<double> bx = ad.bx;
    if (!travel.empty()) {
      AnimObj ao;
      ao.end = (double)frames;
      auto tr = travel.find(a);
      if (tr != travel.end()) {
        const auto& tv_ = tr->second;
        std::vector<std::array<double, 3>> tv(tv_.size());
        for (size_t f = 0; f < tv_.size(); f++)
          for (int c = 0; c < 3; c++) {
            double acc = 0.0;
            for (int k = 0; k < 3; k++) acc = std::fma(tv_[f][k], j0inv[c * 3 + k], acc);
            tv[f][c] = acc + x1.pos[c];
          }
        for (size_t f = 0; f < tv_.size(); f++)
          for (int k = 0; k < 3; k++) {
            bx[f * 6 + k] += tv_[f][k];
            bx[f * 6 + 3 + k] += tv_[f][k];
          }
        for (int ax : {0, 2}) {
          std::vector<double> samples;
          for (auto& r : tv) samples.push_back(r[ax]);
          Track tk;
          tk.kind = J_TRAX + ax;
          tk.keys = keys_of(samples, 0.001);
          ao.tracks.push_back(std::move(tk));
        }
      } else {
        for (int ax : {0, 2}) {
          Track tk;
          tk.kind = J_TRAX + ax;
          tk.keys = {{OP_CON, x1.pos[ax], 0.0, frames}, {OP_CON, x1.pos[ax], 0.0, 0}};
          ao.tracks.push_back(std::move(tk));
        }
      }
      an.set_joint(1, std::move(ao));
    }
    std::vector<std::array<double, 6>> bounds;
    for (int f = 0; f < ad.frames; f++) {
      std::array<double, 6> r;
      for (int k = 0; k < 6; k++) r[k] = bx[(size_t)f * 6 + k];
      bounds.push_back(r);
    }
    an.bounds = std::move(bounds);
    made[a] = m.animations.size();
    m.animations.push_back(std::move(an));
  }
  std::array<double, 6> idle_box{};
  {
    bool have = false;
    if (!m.animations.empty() && m.animations[idle_slot].bounds && !m.animations[idle_slot].bounds->empty()) {
      idle_box = (*m.animations[idle_slot].bounds)[0];
      have = true;
    }
    if (!have) {
      auto sp = boxes(ev.points(mul_all(root, ev.world(spose)), spose.frames, nullptr), spose.frames);
      for (int k = 0; k < 6; k++) idle_box[k] = sp[k];
    }
  }
  for (size_t slot = 0; slot < m.animations.size(); slot++)
    if (!m.animations[slot].bounds)
      m.animations[slot].bounds = std::vector<std::array<double, 6>>((size_t)std::max(xd_frames[slot], 1) + 1, idle_box);
  m.bound_head = xd_model.bound_head ? *xd_model.bound_head : std::vector<int>();
  std::vector<Mat> xw = pose_at(xd_model, xd_model.animations.empty() ? nullptr : &xd_model.animations[idle_slot], 0.0);
  const Pose& ipose = idle_act ? poses.at(*idle_act) : spose;
  std::vector<double> loc0(ipose.local.begin(), ipose.local.begin() + (size_t)nn * 16);
  std::vector<double> iw = mul_all(root, ev.world_of(loc0, 1));
  auto iwt = [&](int node) { return V3{iw[(size_t)node * 16 + 3], iw[(size_t)node * 16 + 7], iw[(size_t)node * 16 + 11]}; };
  std::vector<std::pair<int, V3>> cands;
  for (int i = 0; i < nn; i++)
    if (sdr.nodes[i].kind == 0 || sdr.nodes[i].kind == 2) cands.emplace_back(jn[i], iwt(i));
  std::vector<std::pair<int, V3>> body, efs;
  V3 blo{0, 0, 0}, bext{1, 1, 1};
  if (gen4 || stats) {
    for (int i = 0; i < nn; i++)
      if (sdr.nodes[i].kind == 2 || sdr.nodes[i].name.rfind("ef_", 0) == 0) body.emplace_back(jn[i], iwt(i));
    std::vector<V3> bpos;
    for (auto& b : body) bpos.push_back(b.second);
    if (bpos.empty()) bpos.push_back({0, 0, 0});
    V3 lo = bpos[0], hi = bpos[0];
    for (auto& p : bpos)
      for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
    for (int k = 0; k < 3; k++) { blo[k] = lo[k]; bext[k] = std::max(hi[k] - lo[k], 1e-6); }
    for (auto& b : body)
      if (m.joints[b.first].name.rfind("ef_", 0) == 0) efs.push_back(b);
    if (efs.empty()) efs = body;
  }
  auto norm_ = [&](const V3& p) { return V3{(p[0] - blo[0]) / bext[0] - 0.5, (p[1] - blo[1]) / bext[1], (p[2] - blo[2]) / bext[2] - 0.5}; };
  auto gen4_node = [&](int xj) {
    auto it = gen4->roles.find(xj);
    if (it == gen4->roles.end()) throw std::runtime_error("KeyError");
    const std::string& nm = it->second.first;
    for (auto& jq : efs)
      if (m.joints[jq.first].name == nm) return jq.first;
    for (auto& jq : body)
      if (m.joints[jq.first].name == nm) return jq.first;
    V3 nr = it->second.second;
    int best = -1;
    double bd = 0;
    for (auto& c : efs) {
      V3 q = norm_(c.second);
      double d = ((q[0] - nr[0]) * (q[0] - nr[0]) + (q[1] - nr[1]) * (q[1] - nr[1])) + (q[2] - nr[2]) * (q[2] - nr[2]);
      if (best < 0 || d < bd) { best = c.first; bd = d; }
    }
    return best;
  };
  auto remap = [&](int xj) -> int {
    if (xj < 0) return xj;
    if (xj >= (int)xd_model.joints.size()) return 0;
    if (xj == 0 || xj == xd_model.skeleton_root) return xj == 0 ? 0 : 1;
    V3 p = {xw[xj][3], xw[xj][7], xw[xj][11]};
    if (gen4) return gen4_node(xj);
    if (stats) {
      int bj = -1;
      double bdist = 0;
      for (auto& c : efs) {
        double d = ((c.second[0] - p[0]) * (c.second[0] - p[0]) + (c.second[1] - p[1]) * (c.second[1] - p[1])) + (c.second[2] - p[2]) * (c.second[2] - p[2]);
        if (bj < 0 || d < bdist) { bj = c.first; bdist = d; }
      }
      stats->roles[xj] = {m.joints[bj].name, norm_(iwt(node_of[bj]))};
    }
    int best = 0;
    double bd = 0;
    for (size_t i = 0; i < cands.size(); i++) {
      const V3& c = cands[i].second;
      double d = ((c[0] - p[0]) * (c[0] - p[0]) + (c[1] - p[1]) * (c[1] - p[1])) + (c[2] - p[2]) * (c[2] - p[2]);
      if (i == 0 || d < bd) { best = (int)i; bd = d; }
    }
    return cands.at(best).first;
  };
  std::vector<std::pair<int, V3>> mesh_nodes;
  for (int i = 0; i < nn; i++) {
    const auto& n = sdr.nodes[i];
    if (n.kind == 3 && n.has_mesh && !n.mesh.has_weights && n.name.rfind("col_", 0) != 0) {
      bool any = false;
      for (auto& p_ : n.mesh.parts) any = any || sdr.materials.at(p_.material).texture >= 0;
      if (any) mesh_nodes.emplace_back(jn[i], iwt(i));
    }
  }
  for (size_t xj = 0; xj < xd_model.joints.size(); xj++) {
    uint32_t bb = (xd_model.joints[xj].flags ? *xd_model.joints[xj].flags : 0) & 0xE00;
    if (bb && !mesh_nodes.empty() && xj < xw.size() && !gen4) {
      V3 p = {xw[xj][3], xw[xj][7], xw[xj][11]};
      size_t k = 0;
      double bd = 0;
      for (size_t q = 0; q < mesh_nodes.size(); q++) {
        const V3& c = mesh_nodes[q].second;
        double d = ((c[0] - p[0]) * (c[0] - p[0]) + (c[1] - p[1]) * (c[1] - p[1])) + (c[2] - p[2]) * (c[2] - p[2]);
        if (q == 0 || d < bd) { k = q; bd = d; }
      }
      m.joints[mesh_nodes[k].first].billboard = (int)bb;
      log(sf("INFO: camera-facing: XD joint %d (0x%X) -> %s", (int)xj, bb, m.joints[mesh_nodes[k].first].name.c_str()));
    }
  }
  ConvertOut out;
  out.pkx.header = xd_pkx.header;
  out.pkx.sections = xd_pkx.sections;
  out.pkx.particles = xd_pkx.particles;
  Pkx& pkx = out.pkx;
  auto jname = [&](int j) {
    int n = (int)m.joints.size();
    int k = j < 0 ? j + n : j;
    if (k < 0 || k >= n) throw std::runtime_error("list index out of range");
    return m.joints[k].name;
  };
  int head = pkx.head_joint();
  pkx.set_head_joint(remap(head));
  log(sf("head joint: XD %d -> %d (%s)", head, pkx.head_joint(), jname(pkx.head_joint()).c_str()));
  std::map<int, int> table;
  for (size_t s = 0; s < pkx.sections.size(); s++) {
    auto js = pkx.section_joints(s);
    std::vector<int> nw;
    for (int j : js) nw.push_back(remap(j));
    for (size_t k = 0; k < js.size(); k++) table[js[k]] = nw[k];
    pkx.set_section_joints(s, nw);
  }
  {
    std::string t = "attachment joints, XD -> ours: ";
    bool first = true;
    for (auto& [a, b] : table) {
      t += (first ? "" : ", ") + sf("%d -> %d %s", a, b, b >= 0 ? jname(b).c_str() : "");
      first = false;
    }
    log(t);
  }
  std::vector<uint8_t> h = pkx.header;
  for (int r = 0; r < 4; r++) {
    int rec = 0x1C + r * 0x13;
    if (!face_kept.count(r)) {
      h[rec] = 0;
    } else if (h[rec] == 2) {
      h[rec] = 1;
      h[rec + 1] = 0;
      for (int k = 0; k < 16; k++) h[rec + 2 + k] = 0xFF;
      log(sf("face record %d: part kind -> whole model (slot %d)", r, (int)h[rec + 0x12]));
    }
  }
  pkx.header = h;
  if (stats && stats->stop_roles) throw StopConversion();
  if (xd_full && graft) graft_xd_pieces(m, *xd_full, *graft, idle_slot, alpha, log, step);
  m.image_classes = alpha;
  {
    std::string t = "slots: ";
    for (size_t i = 0; i < pick.size(); i++) {
      if (i) t += " ";
      auto hw = how.find((int)i);
      t += sf("%d=%s%s", (int)i, pick[i] ? sdr.actions[*pick[i]].name.c_str() : "-", (hw == how.end() || hw->second != "role") ? "" : "(role)");
    }
    log(t);
  }
  out.m = std::move(m);
  return out;
}

}

std::optional<ConvertOut> convert(const sdr::Sdr& sdr, const Pkx& xd_pkx, const Model& xd_model, double step, const Logger& log,
                                  const std::string& compensate, double tol_scale, const Model* xd_full,
                                  const std::vector<GraftRegion>* graft, const Gen4Info* gen4, std::optional<std::set<int>> pairs,
                                  ConvertInfo* info, StandinStats* stats) {
  if (!pairs) {
    std::vector<std::string> buf;
    ConvertInfo inf;
    auto res = convert_pass(sdr, xd_pkx, xd_model, step, [&](const std::string& s) { buf.push_back(s); }, compensate, tol_scale, xd_full,
                            graft, gen4, std::set<int>(), &inf, stats);
    if (res) {
      for (auto& l : buf) log(l);
      return res;
    }
    log(sf("INFO: pose %.2f %% of the height from the source with one joint per bone: %d sheared bone(s) become a pair of joints", inf.pct,
           (int)inf.need.size()));
    return convert_pass(sdr, xd_pkx, xd_model, step, log, compensate, tol_scale, xd_full, graft, gen4,
                        std::set<int>(inf.need.begin(), inf.need.end()), nullptr, stats);
  }
  return convert_pass(sdr, xd_pkx, xd_model, step, log, compensate, tol_scale, xd_full, graft, gen4, *pairs, info, stats);
}

}
