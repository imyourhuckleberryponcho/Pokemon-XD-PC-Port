#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

#include "detmath.h"
#include "gf_internal.h"
#include "n3ds_decimate.h"
#include "n3ds_pose.h"
#include "pbr_hsd_read.h"
#include "pbr_to_xd.h"

namespace n3ds {

namespace {

using pbr::Key;
using pbr::Track;
using V3 = std::array<double, 3>;
constexpr int T_TRAU = 2, T_TRAV = 3;

template <class... A>
std::string sf(const char* f, A... a) {
  char buf[1024];
  snprintf(buf, sizeof buf, f, a...);
  return buf;
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
pbr::Weights quantise_weights(const pbr::Weights& w, double step, int most = 4) {
  std::vector<std::pair<int, double>> items = w;
  std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return -a.second < -b.second; });
  if ((int)items.size() > most) items.resize(most);
  std::vector<double> xs;
  for (auto& it : items) xs.push_back(it.second);
  double total = py_sum(xs);
  if (items.empty() || total == 0.0) total = 1.0;
  int n = (int)std::nearbyint(1.0 / step);
  std::vector<std::pair<int, double>> parts;
  for (auto& it : items) parts.emplace_back(it.first, it.second / total * n);
  std::vector<int> whole;
  for (auto& p : parts) whole.push_back((int)std::floor(p.second));
  int sum = 0;
  for (int x : whole) sum += x;
  int left = n - sum;
  std::vector<int> order(parts.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return -(parts[a].second - whole[a]) < -(parts[b].second - whole[b]); });
  int take = left >= 0 ? std::min<int>(left, (int)order.size()) : std::max<int>((int)order.size() + left, 0);
  for (int k = 0; k < take; k++) whole[order[k]] += 1;
  pbr::Weights out;
  for (size_t i = 0; i < parts.size(); i++)
    if (whole[i] > 0) out.emplace_back(parts[i].first, whole[i] / (double)n);
  std::sort(out.begin(), out.end());
  return out;
}

double pyround(double x, int d) { return pbr::py_round(x, d); }

bool has(const std::vector<std::pair<std::string, GfMotion>>& v, const std::string& k) {
  for (auto& p : v) if (p.first == k) return true;
  return false;
}

const char* SKIP_MATS[4] = {"Fire", "Flame", "Aura", "Glow"};

const GfMatChan* mat_chan(const GfMotion& mo, const std::string& name) {
  for (auto& mm : mo.mats) if (mm.name == name && mm.unit == 0) return &mm;
  return nullptr;
}

std::vector<std::string> animated_materials(const Species& sp) {
  std::vector<std::string> names;
  for (auto& kv : sp.mots) {
    const GfMotion& mo = kv.second;
    for (auto& mm : mo.mats) {
      if (mm.unit != 0) continue;
      bool skip = false;
      for (auto s : SKIP_MATS) if (mm.name.find(s) != std::string::npos) skip = true;
      if (skip) continue;
      if ((!mm.tx.empty() || !mm.ty.empty()) && std::find(names.begin(), names.end(), mm.name) == names.end()) {
        bool moves = false;
        for (const GfChannel* ch : {&mm.tx, &mm.ty}) {
          if (ch->empty()) continue;
          std::set<double> vals;
          for (int f = 0; f <= mo.frames; f++) {
            double v = 0.0;
            if (!herp(*ch, f, &v)) v = 0.0;
            vals.insert(pyround(v, 3));
          }
          if (vals.size() > 1) moves = true;
        }
        if (moves) names.push_back(mm.name);
      }
    }
  }
  return names;
}

std::array<double, 2> uv_at(const GfMotion& mo, const std::string& name, double f, std::array<double, 2> base) {
  const GfMatChan* mm = mat_chan(mo, name);
  if (!mm) return base;
  double a, b;
  bool ha = herp(mm->tx, f, &a), hb = herp(mm->ty, f, &b);
  return {ha ? a : base[0], hb ? b : base[1]};
}

std::array<double, 2> uv_base(const GfMotion& idle, const std::string& name) {
  double tx = 0, ty = 0;
  for (auto& mm : idle.mats) {
    if (mm.name == name && mm.unit == 0) {
      double a, b;
      tx = herp(mm.tx, 0, &a) ? a : 0.0;
      ty = herp(mm.ty, 0, &b) ? b : 0.0;
    }
  }
  return {tx, ty};
}

struct Levels { double op, half, shut; };
Levels shut_levels(const Species& sp, const std::string& name, std::array<double, 2> base, int* ties) {
  double op = base[1];
  std::vector<const GfMotion*> src;
  if (const GfMotion* b = sp.mot("_blink")) src.push_back(b);
  else for (auto& kv : sp.mots) if (kv.first != "idle") src.push_back(&kv.second);
  std::set<double> vals;
  for (auto* mo : src)
    for (int f = 0; f <= mo->frames; f++) vals.insert(pyround(uv_at(*mo, name, f, base)[1], 3));
  std::vector<double> cand;
  for (double v : vals) if (std::fabs(v - op) > 0.01 && v < 0.7) cand.push_back(v);
  double shut = op;
  if (!cand.empty()) {
    double best = -1;
    int nb = 0;
    for (double v : cand) {
      double d = std::fabs(v - op);
      if (d > best) { best = d; shut = v; nb = 1; } else if (d == best) nb++;
    }
    if (nb > 1) ++*ties;
  }
  double lo = std::min(op, shut) + 0.01, hi = std::max(op, shut) - 0.01;
  std::vector<double> between;
  for (double v : vals) if (lo < v && v < hi) between.push_back(v);
  double half = (op + shut) / 2;
  if (!between.empty()) {
    double best = 1e300, t = (op + shut) / 2;
    int nb = 0;
    for (double v : between) {
      double d = std::fabs(v - t);
      if (d < best) { best = d; half = v; nb = 1; } else if (d == best) nb++;
    }
    if (nb > 1) ++*ties;
  }
  return {op, half, shut};
}

std::vector<Key> step_keys(const std::vector<double>& v) {
  std::vector<std::pair<double, int>> runs;
  size_t start = 0;
  for (size_t f = 1; f <= v.size(); f++) {
    if (f == v.size() || std::fabs(v[f] - v[start]) > 1e-4) {
      runs.push_back({v[start], (int)(f - start)});
      start = f;
    }
  }
  std::vector<Key> keys;
  for (auto& r : runs) keys.push_back({pbr::OP_CON, r.first, 0.0, r.second});
  keys.back() = {pbr::OP_CON, keys.back().value, 0.0, std::max<long long>(keys.back().wait - 1, 1)};
  keys.push_back({pbr::OP_CON, keys.back().value, 0.0, 0});
  return keys;
}

std::vector<double> wink_pattern(int n, const std::string& kind, double op, double half, double shut) {
  if (kind == "wink") {
    std::vector<double> lv = n >= 4 ? std::vector<double>{op, half, shut, half} : std::vector<double>{half, shut};
    if (n < 4) lv.resize(std::min<size_t>(lv.size(), (size_t)std::max(n, 1)));
    std::vector<double> out;
    for (int k = 0; k < n; k++) {
      int i = (int)((double)k * (double)lv.size() / (double)std::max(n, 1));
      out.push_back(lv[std::min<int>(i, (int)lv.size() - 1)]);
    }
    out.push_back(op);
    return out;
  }
  std::vector<double> ramp;
  for (int k = 0; k <= n; k++) {
    double t = k / (double)std::max(n, 1);
    ramp.push_back(t < 0.34 ? op : t < 0.67 ? half : shut);
  }
  ramp.back() = shut;
  if (kind == "sleep") return ramp;
  std::reverse(ramp.begin(), ramp.end());
  return ramp;
}

double snap(double v) { return std::nearbyint(v * 4.0) / 4.0; }

struct EyePlan {
  const Species* sp;
  std::vector<std::string> names;
  std::map<std::string, std::array<double, 2>> base, sxy;
  std::map<std::string, Levels> levels;
  int ties = 0;
  EyePlan(const Species& s, const std::map<std::string, std::array<double, 2>>& sxy_) : sp(&s), sxy(sxy_) {
    names = animated_materials(s);
    const GfMotion& idle = *s.mot("idle");
    for (auto& n : names) {
      base[n] = uv_base(idle, n);
      levels[n] = shut_levels(s, n, base[n], &ties);
    }
  }
  using Vals = std::vector<std::pair<std::string, std::pair<std::vector<double>, std::vector<double>>>>;
  Vals role_values(const std::string& role, const std::vector<double>& src_frames) const {
    const GfMotion* mo = sp->mot(role);
    if (!mo) mo = sp->mot("idle");
    Vals out;
    for (auto& n : names) {
      auto sc = sxy.at(n);
      auto b = base.at(n);
      std::vector<double> us, vs;
      for (double f : src_frames) {
        auto t = uv_at(*mo, n, f, b);
        double tx = snap(t[0]), ty = snap(t[1]);
        us.push_back(1.0 * sc[0] * (tx - b[0]));
        vs.push_back(1.0 * sc[1] * (ty - b[1]));
      }
      out.push_back({n, {us, vs}});
    }
    return out;
  }
  Vals face_values(const std::string& kind, int n) const {
    Vals out;
    for (auto& nm : names) {
      if (nm.find("Eye") == std::string::npos && nm.find("Iris") == std::string::npos) continue;
      auto sc = sxy.at(nm);
      Levels lv = levels.at(nm);
      auto b = base.at(nm);
      auto v = wink_pattern(n, kind, lv.op, lv.half, lv.shut);
      std::vector<double> vs;
      for (double x : v) vs.push_back(1.0 * sc[1] * (x - b[1]));
      out.push_back({nm, {std::vector<double>(n + 1, 0.0), vs}});
    }
    return out;
  }
};

double mainline_box_height(double h) {
  const double A = -1.5484, B = 0.8229;
  return dm::exp(A + B * dm::log(std::max(h, 10.0)));
}

const std::map<int, std::string>& role_of_section() {
  static const std::map<int, std::string> R = {{0, "idle"}, {16, "idle"}, {8, "hit"}, {10, "faint"}, {1, "special"}, {6, "special"}, {12, "special"},
                                              {13, "special"}, {14, "special"}, {15, "special"}, {2, "phys"}, {3, "phys"}, {4, "phys"}, {5, "phys"}, {7, "phys"}};
  return R;
}

std::vector<int> dfs_order(const Skeleton& sk) {
  int nb = sk.nb;
  std::map<int, std::vector<int>> kids;
  for (int i = 0; i < nb; i++) kids[sk.parent[i]].push_back(i);
  std::vector<int> out;
  std::function<void(int)> go = [&](int i) {
    out.push_back(i);
    for (int c : kids[i]) go(c);
  };
  for (int r : kids[-1]) go(r);
  return out;
}

double np_mean(const std::vector<double>& v) { return np_sum(v.data(), v.size()) / (double)v.size(); }

struct Ctx {
  const Species& sp;
  const GfModel& gm;
  std::map<const GfSub*, std::vector<int>> used;
  const std::vector<int>& used_of(const GfSub& sm) {
    auto it = used.find(&sm);
    if (it != used.end()) return it->second;
    std::set<int64_t> s;
    for (auto& t : triangles(sm)) for (int k = 0; k < 3; k++) s.insert(t[k]);
    return used[&sm] = std::vector<int>(s.begin(), s.end());
  }
};

bool mesh_hidden_at_idle(const GfMotion& idle, const std::string& name) {
  const std::vector<bool>* v = nullptr;
  for (auto& p : idle.vis) if (p.first == name) v = &p.second;
  return v && !(*v)[0];
}

bool xd_has_particles(const pbr::Model& m) {
  for (auto& an : m.animations)
    for (auto& jo : an.joints)
      for (auto& t : jo.second.tracks)
        if (t.kind == 40) return true;
  return false;
}

std::vector<double> skin_ys(const pbr::Model& model, const std::vector<pbr::Mat>& world) {
  std::map<int, pbr::Mat> skin;
  std::vector<double> ys;
  for (auto& me : model.meshes) {
    if (model.joints[me.joint].hidden) continue;
    if (me.rigid) {
      const pbr::Mat& m = world[me.joint];
      for (auto& v : me.verts) ys.push_back(pbr::mat_point(m, v.pos)[1]);
      continue;
    }
    for (auto& v : me.verts) {
      double y = 0.0;
      if (!v.weights) throw std::runtime_error("vertex without weights");
      for (auto& jw : *v.weights) {
        int j = jw.first;
        double w = jw.second;
        auto it = skin.find(j);
        if (it == skin.end()) {
          if (!model.joints[j].inv_bind) throw std::runtime_error("joint without inverse bind");
          pbr::Mat ib;
          for (int k = 0; k < 12; k++) ib[k] = (*model.joints[j].inv_bind)[k];
          it = skin.emplace(j, pbr::mat_mul(world[j], ib)).first;
        }
        pbr::V3 p = pbr::mat_point(it->second, v.pos);
        y += p[1] * w;
      }
      ys.push_back(y);
    }
  }
  return ys;
}

double xd_idle_lowest(const std::vector<uint8_t>& xd_bytes, const pbr::Model& xd_model, const pbr::Pkx& xd_pkx) {
  int idle_i = 0;
  std::map<int, std::vector<int>> users;
  for (size_t s_ = 0; s_ < xd_pkx.sections.size(); s_++) {
    auto mo = xd_pkx.section_motions(s_);
    for (auto& km : mo)
      if (km.first && km.second >= 0 && km.second < (int)xd_model.animations.size()) users[km.second].push_back((int)s_);
  }
  for (auto& kv : users) {
    bool idle = false;
    for (int s_ : kv.second) if (s_ == 0 || s_ == 16) idle = true;
    if (idle) { idle_i = kv.first; break; }
  }
  double box = 0.0;
  if (!xd_model.animations.empty() && xd_model.animations[idle_i].bounds && !xd_model.animations[idle_i].bounds->empty()) {
    std::vector<double> c;
    for (auto& b : *xd_model.animations[idle_i].bounds) c.push_back(b[1]);
    box = np_sum(c.data(), c.size()) / (double)c.size();
  }
  double posed = 0.0;
  try {
    pbr::ReadResult xr = pbr::hsd_read_pkx(xd_bytes, true);
    const pbr::Model& xm = xr.model;
    const pbr::Animation* an = xm.animations.empty() ? nullptr : &xm.animations[idle_i];
    double end = an ? an->end() : 0.0;
    std::vector<double> fr = end > 1 ? std::vector<double>{0.0, end * 0.25, end * 0.5, end * 0.75} : std::vector<double>{0.0};
    std::vector<double> lows;
    for (double f : fr) {
      auto ys = skin_ys(xm, pbr::pose_at(xm, an, f));
      if (!ys.empty()) lows.push_back(*std::min_element(ys.begin(), ys.end()));
    }
    posed = lows.empty() ? 0.0 : np_sum(lows.data(), lows.size()) / (double)lows.size();
  } catch (const std::exception&) {
    posed = 0.0;
  }
  return std::max(box, posed);
}

const char* ptcl_bone(int nat, int xj) {
  switch (nat) {
    case 4: return xj == 48 ? "TailA01" : nullptr;
    case 5: return xj == 22 ? "TailA01" : nullptr;
    case 6: return (xj == 54 || xj == 56) ? "TailA01" : nullptr;
    case 126: return xj == 91 ? "TailA01" : nullptr;
  }
  return nullptr;
}

void add_particle_tracks(pbr::Model& m, const pbr::Model& xd_model, const std::vector<pbr::Mat>& xwm, const std::map<int, int>& jn, const Skeleton& sk, const GfModel& gm, int nb,
                         const Logger& log) {
  std::map<std::string, int> names;
  for (int i = 0; i < nb; i++) names[sk.names[i]] = jn.at(i);
  int nat = gfi::species_of(gm);
  auto wm = m.world_static();
  std::vector<std::pair<int, V3>> ours;
  for (int i = 0; i < nb; i++) { int j = jn.at(i); ours.push_back({j, {wm[j][3], wm[j][7], wm[j][11]}}); }
  std::vector<V3> xj_pos;
  for (auto& w : xwm) xj_pos.push_back({w[3], w[7], w[11]});
  double xmin = 1e300, xmax = -1e300, omin = 1e300, omax = -1e300;
  for (auto& p : xj_pos) { xmin = std::min(xmin, p[1]); xmax = std::max(xmax, p[1]); }
  for (auto& p : ours) { omin = std::min(omin, p.second[1]); omax = std::max(omax, p.second[1]); }
  double h_x = std::max(xmax - xmin, 1e-6), h_o = std::max(omax - omin, 1e-6);
  std::map<int, int> cache;
  auto target = [&](int xj) -> int {
    auto it = cache.find(xj);
    if (it != cache.end()) return it->second;
    const char* nm = ptcl_bone(nat, xj);
    int r;
    if (nm && names.count(nm)) {
      r = names[nm];
    } else {
      size_t best = 0;
      double bd = 1e300;
      for (size_t c = 0; c < ours.size(); c++) {
        double dx = ours[c].second[0] / h_o - xj_pos[xj][0] / h_x, dy = ours[c].second[1] / h_o - xj_pos[xj][1] / h_x, dz = ours[c].second[2] / h_o - xj_pos[xj][2] / h_x;
        double d = (dx * dx + dy * dy) + dz * dz;
        if (d < bd) { bd = d; best = c; }
      }
      r = ours[best].first;
    }
    return cache[xj] = r;
  };
  int n = 0;
  for (size_t slot = 0; slot < xd_model.animations.size(); slot++) {
    if (slot >= m.animations.size() || m.animations[slot].name == "none") continue;
    pbr::Animation& an = m.animations[slot];
    const pbr::Animation& xa = xd_model.animations[slot];
    std::set<int> used;
    for (auto& jo : xa.joints) {
      int xj = jo.first;
      for (auto& t : jo.second.tracks) {
        if (t.kind != 40) continue;
        int tj = target(xj);
        if (used.count(tj)) continue;
        used.insert(tj);
        const pbr::AnimObj* ao = an.joint(tj);
        if (!ao) {
          pbr::AnimObj na;
          double e = 1.0;
          if (!an.joints.empty()) { e = -1e300; for (auto& p : an.joints) e = std::max(e, p.second.end); }
          na.end = e;
          ao = &an.set_joint(tj, na);
        }
        const_cast<pbr::AnimObj*>(ao)->tracks.push_back(t);
        n++;
      }
    }
  }
  if (n) log(sf("INFO: fx: %d particle-spawn tracks copied from XD (flame / spark generators)", n));
}

void add_fire_scroll(pbr::Model& m, const GfModel& gm, const GfMotion* fxmo, const Logger& log, double sign = -1.0) {
  std::set<int> fire;
  for (size_t i = 0; i < gm.materials.size(); i++) if (gfi::is_fire_material(gm.materials[i])) fire.insert((int)i);
  if (fire.empty()) return;
  struct Place { int joint, k, mat; };
  std::vector<Place> place;
  std::map<int, int> cnt;
  for (auto& me : m.meshes) {
    int k = cnt[me.joint];
    cnt[me.joint] = k + 1;
    if (me.material && fire.count(*me.material)) place.push_back({me.joint, k, *me.material});
  }
  if (place.empty()) return;
  int period = fxmo ? std::max(fxmo->frames, 1) : 40;
  auto channel = [&](const std::string& name, bool tx) -> const GfChannel* {
    if (!fxmo) return nullptr;
    for (auto& c : fxmo->mats) {
      const GfChannel& ch = tx ? c.tx : c.ty;
      if (c.name == name && c.unit == 0 && !ch.empty()) return &ch;
    }
    return nullptr;
  };
  for (size_t slot = 0; slot < m.animations.size(); slot++) {
    pbr::Animation& an = m.animations[slot];
    if (an.name == "none") continue;
    double e = 1.0;
    if (!an.joints.empty()) { e = -1e300; for (auto& p : an.joints) e = std::max(e, p.second.end); }
    int frames = std::max((int)std::nearbyint(e), 1);
    for (auto& pl : place) {
      const GfMaterial& mt = gm.materials[pl.mat];
      const GfCoord* co = nullptr;
      for (auto& c : mt.coords) if (c.unit == 0) { co = &c; break; }
      double sx = co ? co->scale[0] : 1.0, sy = co ? co->scale[1] : 1.0;
      pbr::AnimObj ao;
      ao.end = (double)frames;
      for (int which = 0; which < 2; which++) {
        double scale = which == 0 ? sx : sy, fb = which == 0 ? 0.01 : 0.05;
        const GfChannel* ch = channel(mt.name, which == 0);
        std::vector<double> vals;
        for (int f = 0; f <= frames; f++) {
          if (ch) {
            double v = 0.0;
            herp(*ch, (double)(f % period), &v);
            vals.push_back(sign * scale * v);
          } else {
            vals.push_back(sign * scale * fb * (double)f);
          }
        }
        Track t;
        t.kind = which == 0 ? 2 : 3;
        t.keys = pbr::keys_of(vals, 1e-4);
        ao.tracks.push_back(std::move(t));
      }
      pbr::TexAnim ta;
      ta.tex_id = 0;
      ta.aobj = ao;
      pbr::MatAnim ma;
      ma.texanims.push_back(ta);
      bool present = false;
      const pbr::MatList* old = an.material(pl.joint, &present);
      pbr::MatList lst;
      if (old) lst = *old;
      else lst.items.assign(cnt[pl.joint], std::nullopt);
      if ((int)lst.items.size() < cnt[pl.joint]) lst.items.resize(cnt[pl.joint], std::nullopt);
      lst.items[pl.k] = ma;
      an.set_material(pl.joint, std::optional<pbr::MatList>(std::move(lst)));
    }
    an.has_material_tree = true;
  }
  log(sf("INFO: fx: %d flame piece(s) slide their picture (%s)", (int)place.size(), fxmo ? "the dump's flame motion" : "fallback speed"));
}

struct Built {
  pbr::Model m;
  pbr::Pkx pkx;
  int tris = 0;
  double scale = 0;
  bool eyes_ties = false;
};

Built build(const Species& sp, const std::vector<uint8_t>& xd_bytes, const Opts& opts, const Logger& log, double step) {
  if (opts.scale != 0) throw Unsupported("option scale");

  const GfModel& gm = sp.gm;
  Ctx cx{sp, gm, {}};
  pbr::ReadResult xdr = pbr::hsd_read_pkx(xd_bytes, false);
  const pbr::Model& xd_model = xdr.model;
  const pbr::Pkx& xd_pkx = xdr.pkx;
  const int nat_sp = gfi::species_of(gm);
  const bool skip_fire = (opts.fx_skip_fire || (opts.fx && xd_has_particles(xd_model))) && nat_sp != gfi::KEEP_FIRE_MESH;
  Skeleton sk(gm.bones);
  int nb = sk.nb;
  auto order = dfs_order(sk);
  Built B;
  pbr::Model& m = B.m;
  pbr::Joint j0;
  const pbr::Joint& x0 = xd_model.joints[0];
  j0.rot = x0.rot; j0.scale = x0.scale; j0.pos = x0.pos;
  pbr::Joint j1;
  j1.parent = 0;
  m.joints.push_back(j0);
  m.joints.push_back(j1);
  m.skeleton_root = 1;
  std::map<int, int> jn;
  for (size_t pos = 0; pos < order.size(); pos++) jn[order[pos]] = 2 + (int)pos;
  for (int i : order) {
    pbr::Joint j;
    j.name = sk.names[i];
    j.parent = sk.parent[i] >= 0 ? jn[sk.parent[i]] : 1;
    j.scale = sk.bind_s[i]; j.rot = sk.bind_r[i]; j.pos = sk.bind_t[i];
    m.joints.push_back(j);
  }
  std::map<std::string, int> holder;
  for (auto& me : gm.meshes) {
    if (holder.count(me.name)) continue;
    pbr::Joint j;
    j.name = me.name;
    j.parent = 1;
    holder[me.name] = (int)m.joints.size();
    m.joints.push_back(j);
  }
  const GfMotion& idle = *sp.mot("idle");
  std::map<std::string, int> mat_index;
  for (size_t i = 0; i < gm.materials.size(); i++) mat_index[gm.materials[i].name] = (int)i;
  auto hidden_mesh = [&](const GfMesh& me) { return mesh_hidden_at_idle(idle, me.name); };
  auto drawn = [&](const GfMesh& me, const GfSub& sm) {
    if (hidden_mesh(me)) return false;
    auto it = mat_index.find(sm.name);
    if (it == mat_index.end()) return false;
    if (skip_fire && gfi::is_fire_material(gm.materials[it->second])) return false;
    return !gfi::is_effect_layer(gm.materials[it->second]);
  };
  auto drawn_points_y = [&](const std::vector<M4>& W, bool want_all, std::vector<double>* ys) {
    for (auto& me : gm.meshes)
      for (auto& sm : me.subs)
        if (drawn(me, sm)) {
          auto pts = skin_points(sk, W, sm.bone_idx, sm.verts);
          for (int u : cx.used_of(sm)) ys->push_back(pts[u][1]);
        }
    (void)want_all;
  };
  auto minmax = [](const std::vector<double>& y, double* mn, double* mx) {
    *mn = *std::min_element(y.begin(), y.end());
    *mx = *std::max_element(y.begin(), y.end());
  };
  auto idle_stats = [&](double* h0, double* hmean, double* y0, double* ymean) {
    int n_ = std::max(idle.frames, 1);
    std::vector<double> hs, ms;
    double fr[4] = {0, n_ * 0.25, n_ * 0.5, n_ * 0.75};
    for (double f : fr) {
      Pose p = sk.sample_motion(idle, f);
      auto W = sk.world(p.s, p.r, p.t, &p.axis);
      std::vector<double> ys;
      drawn_points_y(W, true, &ys);
      double mn, mx;
      minmax(ys, &mn, &mx);
      hs.push_back(mx - mn);
      ms.push_back(mn);
    }
    *h0 = hs[0]; *hmean = np_mean(hs); *y0 = ms[0]; *ymean = np_mean(ms);
  };
  Pose p0 = sk.sample_motion(idle, 0);
  auto W0 = sk.world(p0.s, p0.r, p0.t, &p0.axis);
  std::vector<double> allp_y;
  drawn_points_y(W0, true, &allp_y);
  double allp_min, allp_max;
  minmax(allp_y, &allp_min, &allp_max);
  double my_h = allp_max - allp_min, my_h0 = my_h, idle_ymin_mean = allp_min;
  if (opts.size_rule) {
    double h0, y0;
    idle_stats(&h0, &my_h, &y0, &idle_ymin_mean);
  }
  const std::vector<std::array<double, 6>>* xb = (!xd_model.animations.empty() && xd_model.animations[0].bounds && !xd_model.animations[0].bounds->empty())
                                                     ? &*xd_model.animations[0].bounds : nullptr;
  double xd_h = -1, size_factor = 0.1;
  if (xb) {
    std::vector<double> d;
    for (auto& b : *xb) d.push_back(b[4] - b[1]);
    xd_h = np_mean(d);
    size_factor = xd_h / my_h;
  }
  if (opts.size_rule && opts.height_cm > 0 && (xd_h <= 0 || opts.size_rule == 2)) size_factor = mainline_box_height(opts.height_cm) / my_h;
  log(sf("INFO: height %.1f (3DS units), XD idle box height %.1f -> scale %.4f", my_h, xd_h, size_factor));
  if (opts.size_rule) log(sf("INFO: size rule: frame-0 height %.1f, idle mean %.1f, scale before the rule %.4f", my_h0, my_h, xd_h > 0 ? xd_h / my_h0 : size_factor));
  double xd_ymin = 0.0;
  if (xb) {
    std::vector<double> c;
    for (auto& b : *xb) c.push_back(b[1]);
    xd_ymin = np_mean(c);
  }
  double hover;
  if (opts.hover_model) {
    hover = (allp_min > -1e300 && my_h > 0 && allp_min / my_h > 0.12) ? allp_min * size_factor : 0.0;
  } else {
    double xd_low = opts.hover_idle ? xd_idle_lowest(xd_bytes, xd_model, xd_pkx) : xd_ymin;
    hover = xd_low > HOVER_MIN ? xd_low : 0.0;
  }
  double shift = 0.0;
  if (opts.ground) {
    shift = hover / size_factor - (hover > HOVER_MIN ? idle_ymin_mean : allp_min);
    m.joints[1].pos = {0.0, shift * size_factor, 0.0};
    log(sf("INFO: grounding: %s, shift %.2f", hover > 0.5 ? sf("hovers at %.1f like the XD flyers", hover).c_str() : "feet on the floor", shift));
  }
  const bool bake = opts.bake_scale;
  double sf_root = bake ? 1.0 : size_factor;
  m.joints[1].scale = {sf_root, sf_root, sf_root};
  if (bake)
    for (int i = 0; i < nb; i++) m.joints[jn[i]].pos = {sk.bind_t[i][0] * size_factor, sk.bind_t[i][1] * size_factor, sk.bind_t[i][2] * size_factor};
  m.joints[1].rot = {0.0, 0.0, 0.0};
  auto ws = m.world_static();
  pbr::Mat root_inv = pbr::mat_inv(ws[1]);
  for (int i = 0; i < nb; i++) {
    pbr::Mat ib = pbr::mat_inv(pbr::mat_mul(root_inv, ws[jn[i]]));
    m.joints[jn[i]].inv_bind = std::vector<double>(ib.begin(), ib.end());
  }
  int nat = 0;
  if (!gm.bones.empty()) {
    const std::string& nm0 = gm.bones[0].name;
    if (nm0.compare(0, 2, "pm") == 0 && nm0.size() >= 6 && isdigit((unsigned char)nm0[2]) && isdigit((unsigned char)nm0[3]) && isdigit((unsigned char)nm0[4]) &&
        isdigit((unsigned char)nm0[5]))
      nat = atoi(nm0.substr(2, 4).c_str());
  }
  std::map<std::string, pbr::Rgba> texs;
  for (auto& kv : sp.texs) {
    pbr::Rgba r(kv.second.w, kv.second.h);
    r.px = kv.second.px;
    texs[kv.first] = std::move(r);
  }
  std::unique_ptr<gfi::MatchPlan> mplan;
  if (opts.match && opts.hand && !gfi::hand_match_enabled(nat)) log("INFO: colour match: off for this species (hand table usum_hand)");
  else if (opts.match) mplan = gfi::colour_match_plan(gm, texs, xd_bytes, opts, nat, log);
  gfi::MadeImages mi = gfi::make_images(gm, texs, opts, nat, mplan.get());
  std::set<std::string> baked_iris;
  for (auto& e : gfi::iris_plan(gm, texs, opts)) for (auto& n : e.second) baked_iris.insert(n);
  if (!baked_iris.empty()) log("INFO: iris plates painted into the eye picture");
  m.images = mi.images;
  for (size_t k = 0; k < gm.materials.size(); k++) {
    const GfMaterial& mt = gm.materials[k];
    pbr::Material ma;
    ma.name = mt.name;
    int ima = mi.mat_img[k].first;
    ma.draw_class = mi.mat_img[k].second;
    if (ima >= 0) {
      pbr::Texture tx;
      tx.image = ima;
      const GfCoord* co = gfi::colour_coord(mt);
      auto wrap = [](uint32_t w) -> uint32_t { return w == 0 ? 0 : w == 1 ? 0 : w == 2 ? 1 : w == 3 ? 2 : 1; };
      tx.wrap_s = wrap(co->wrap[0]);
      tx.wrap_t = wrap(co->wrap[1]);
      if (opts.fx && gfi::is_fire_material(mt)) tx.wrap_s = tx.wrap_t = 1;
      if (ma.draw_class == "texedge") {
        tx.flags = 0x00230010;
        ma.rendermode = 0x40002015;
      } else if (ma.draw_class == "xlu") {
        tx.flags = 0x00230010;
        ma.rendermode = 0x60002015;
      }
      ma.textures.push_back(tx);
    } else {
      ma.rendermode = 0x2005;
      const uint8_t* c = mt.colors[11];
      ma.ambient = ma.diffuse = {(double)c[0], (double)c[1], (double)c[2], 255.0};
    }
    m.materials.push_back(ma);
  }
  m.image_classes = mi.classes;
  std::unique_ptr<EyePlan> eyes;
  if (opts.eyes) {
    std::map<std::string, std::array<double, 2>> sxy;
    for (auto& mt : gm.materials)
      for (auto& c : mt.coords) if (c.unit == 0) { sxy[mt.name] = c.scale; break; }
    eyes.reset(new EyePlan(sp, sxy));
    std::vector<std::string> keep;
    for (auto& n : eyes->names) if (sxy.count(n) && !baked_iris.count(n)) keep.push_back(n);
    eyes->names = keep;
    B.eyes_ties = eyes->ties > 0;
    std::string lv;
    for (auto& n : eyes->names) lv += sf("%s ", n.c_str());
    log("INFO: eyes: sliding materials " + lv);
  }
  std::set<std::string> combos_s;
  int tri_total = 0, tri_before = 0;
  double ratio = 1.0;
  if (opts.decimate) {
    long long tot = 0;
    for (auto& me_ : gm.meshes) {
      if (hidden_mesh(me_)) continue;
      for (auto& sm : me_.subs) tot += (long long)triangles(sm).size();
    }
    ratio = std::min(1.0, (double)opts.decimate / (double)std::max<long long>(tot, 1));
  }
  std::vector<std::string> hidden;
  for (auto& me_ : gm.meshes) {
    if (hidden_mesh(me_)) { hidden.push_back(me_.name); continue; }
    for (auto& sm : me_.subs) {
      auto mit = mat_index.find(sm.name);
      if (mit == mat_index.end()) continue;
      int mi_ = mit->second;
      const GfMaterial& mt = gm.materials[mi_];
      if (skip_fire && gfi::is_fire_material(mt)) continue;
      if (baked_iris.count(mt.name)) continue;
      if (gfi::is_effect_layer(mt)) continue;
      const GfVerts& V = sm.verts;
      const GfCoord* co = gfi::colour_coord(mt);
      std::vector<std::array<double, 2>> uv;
      bool has_uv = false;
      if (co && V.has("uv0")) {
        has_uv = true;
        double sx = co->scale[0], sy = co->scale[1], tx = co->trans[0], ty = co->trans[1];
        if (co->unit == 0) {
          for (auto& mm : idle.mats) {
            if (mm.name == mt.name && mm.unit == 0) {
              double v;
              if (herp(mm.tx, 0, &v)) tx = v;
              if (herp(mm.ty, 0, &v)) ty = v;
            }
          }
        }
        const Arr& a = V.get("uv0");
        for (int r = 0; r < V.n; r++) uv.push_back({sx * (a.at(r, 0) - tx), sy * (a.at(r, 1) - ty)});
      }
      const std::vector<int>& tab = sm.bone_idx;
      bool skinned = V.has("bidx") && V.has("bw");
      pbr::Mesh me;
      me.material = mi_;
      me.joint = holder[me_.name];
      me.rigid = false;
      me.name = me_.name;
      auto tris = triangles(sm);
      std::vector<int64_t> used;
      {
        std::set<int64_t> s;
        for (auto& t : tris) for (int k = 0; k < 3; k++) s.insert(t[k]);
        used.assign(s.begin(), s.end());
      }
      std::map<int64_t, int> remap;
      for (size_t k = 0; k < used.size(); k++) remap[used[k]] = (int)k;
      const Arr* nrm = V.has("nrm") ? &V.get("nrm") : nullptr;
      const Arr& pos = V.get("pos");
      bool lifted = mt.shader.find("patcheel") != std::string::npos || mt.name.compare(0, 4, "Line") == 0;
      for (int64_t v : used) {
        pbr::Weights w;
        auto add = [&](int jj, double wk) {
          for (auto& p : w) if (p.first == jj) { p.second = p.second + wk; return; }
          w.push_back({jj, 0.0 + wk});
        };
        if (!skinned) {
          w.push_back({jn[tab.empty() ? 0 : tab[0]], 1.0});
        } else {
          const Arr& bi = V.get("bidx");
          const Arr& bw = V.get("bw");
          for (int k = 0; k < bi.cols; k++) {
            double wk = k < bw.cols ? bw.at((int)v, k) : 0.0;
            int bix = (int)bi.at((int)v, k);
            if (wk > 0 && bix < (int)tab.size()) add(jn[tab[bix]], wk);
          }
          if (w.empty()) w.push_back({jn[tab.empty() ? 0 : tab[0]], 1.0});
        }
        pbr::Weights wq = quantise_weights(w, step);
        std::string key;
        for (auto& p : wq) key += sf("%d:%.17g,", p.first, p.second);
        combos_s.insert(key);
        double nx = 0, ny = 1, nz = 0;
        if (nrm) { nx = nrm->at((int)v, 0); ny = nrm->at((int)v, 1); nz = nrm->at((int)v, 2); }
        double ln = std::sqrt((nx * nx + ny * ny) + nz * nz);
        if (ln == 0.0) ln = 1.0;
        double pv[3] = {pos.at((int)v, 0), pos.at((int)v, 1), pos.at((int)v, 2)};
        if (lifted) {
          double lift = 0.006 * my_h / ln;
          pv[0] = pv[0] + nx * lift; pv[1] = pv[1] + ny * lift; pv[2] = pv[2] + nz * lift;
        }
        V3 nv = {nx / ln, ny / ln, nz / ln};
        std::optional<std::array<double, 2>> uvv;
        if (has_uv) uvv = uv[(size_t)v];
        if (opts.quant) {
          double q = opts.quant;
          for (auto& x : nv) x = std::nearbyint(x * q) / q;
          if (uvv) { (*uvv)[0] = std::nearbyint((*uvv)[0] * 512) / 512; (*uvv)[1] = std::nearbyint((*uvv)[1] * 512) / 512; }
        }
        pbr::Vertex vt;
        double bk = bake ? size_factor : 1.0;
        vt.pos = {pv[0] * bk, pv[1] * bk, pv[2] * bk};
        vt.nrm = nv;
        vt.uv = uvv;
        vt.weights = wq;
        me.verts.push_back(vt);
      }
      std::vector<std::array<int, 3>> keep;
      for (auto& t : tris) {
        int64_t a = t[0], b = t[1], c = t[2];
        if (a != b && a != c && b != c) {
          bool out_ccw;
          if (nrm) {
            double p0_[3], p1_[3], p2_[3];
            for (int k = 0; k < 3; k++) { p0_[k] = pos.at((int)a, k); p1_[k] = pos.at((int)b, k); p2_[k] = pos.at((int)c, k); }
            double u[3], w[3];
            for (int k = 0; k < 3; k++) { u[k] = p1_[k] - p0_[k]; w[k] = p2_[k] - p0_[k]; }
            double gn[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
            double vn[3];
            for (int k = 0; k < 3; k++) vn[k] = (nrm->at((int)a, k) + nrm->at((int)b, k)) + nrm->at((int)c, k);
            double dot = (gn[0] * vn[0] + gn[1] * vn[1]) + gn[2] * vn[2];
            out_ccw = dot > 0;
          } else {
            out_ccw = false;
          }
          int ra = remap[a], rb = remap[b], rc = remap[c];
          keep.push_back(out_ccw ? std::array<int, 3>{ra, rc, rb} : std::array<int, 3>{ra, rb, rc});
        }
      }
      tri_before += (int)keep.size();
      if (ratio < 1.0 && keep.size() > 40) {
        int tgt = std::max((int)((double)keep.size() * ratio), 30);
        std::vector<std::array<double, 3>> dp;
        std::vector<std::array<double, 2>> du;
        std::vector<Weights> dw;
        for (auto& v : me.verts) {
          dp.push_back(v.pos);
          if (has_uv) du.push_back(*v.uv);
          dw.push_back(*v.weights);
        }
        DecimateResult dr = decimate(dp, du, dw, keep, tgt);
        std::vector<pbr::Vertex> nv;
        for (int i : dr.keep) nv.push_back(me.verts[i]);
        me.verts = std::move(nv);
        keep = dr.tris;
      }
      me.tris = keep;
      tri_total += (int)keep.size();
      if (!keep.empty()) m.meshes.push_back(std::move(me));
    }
  }
  std::map<int, int> n_dobj;
  std::map<int, std::vector<std::pair<int, int>>> mat_places;
  for (auto& me : m.meshes) {
    int k_ = n_dobj[me.joint]++;
    mat_places[*me.material].push_back({me.joint, k_});
  }
  auto eye_anims = [&](const EyePlan::Vals& values, int nfr) {
    std::vector<std::pair<int, pbr::MatList>> out;
    auto slot = [&](int j) -> pbr::MatList& {
      for (auto& p : out) if (p.first == j) return p.second;
      out.push_back({j, pbr::MatList()});
      out.back().second.items.assign(n_dobj[j], std::nullopt);
      return out.back().second;
    };
    for (auto& nv : values) {
      auto it = mat_index.find(nv.first);
      if (it == mat_index.end() || !mat_places.count(it->second)) continue;
      pbr::AnimObj ao;
      ao.end = (double)nfr;
      ao.tracks.push_back(Track{T_TRAU, 0.0, step_keys(nv.second.first), std::nullopt});
      ao.tracks.push_back(Track{T_TRAV, 0.0, step_keys(nv.second.second), std::nullopt});
      pbr::TexAnim ta;
      ta.tex_id = 0;
      ta.aobj = ao;
      for (auto& pl : mat_places[it->second]) {
        pbr::MatAnim ma;
        ma.texanims.push_back(ta);
        slot(pl.first).items[pl.second] = ma;
      }
    }
    return out;
  };
  if (ratio < 1.0) log(sf("INFO: slimmed %d -> %d triangles", tri_before, tri_total));
  log(sf("INFO: %d triangles, %d meshes, %d weight combinations", tri_total, (int)m.meshes.size(), (int)combos_s.size()));
  B.tris = tri_total;
  B.scale = size_factor;
  std::vector<int> xd_frames;
  for (auto& a : xd_model.animations) xd_frames.push_back((int)std::nearbyint(a.end()));
  std::map<int, std::vector<std::pair<int, int>>> users;
  for (size_t s = 0; s < xd_pkx.sections.size(); s++) {
    auto mots_ = xd_pkx.section_motions(s);
    for (size_t mo = 0; mo < mots_.size(); mo++) {
      int kind = mots_[mo].first, idx = mots_[mo].second;
      if (kind && idx >= 0 && idx < (int)xd_frames.size()) users[idx].push_back({(int)s, (int)mo});
    }
  }
  static const int order_roles[17] = {0, 16, 8, 10, 1, 9, 2, 3, 4, 5, 7, 6, 12, 13, 14, 15, 11};
  auto rank = [&](int s) { for (int i = 0; i < 17; i++) if (order_roles[i] == s) return i; return 99; };
  std::vector<std::string> slot_role;
  for (size_t i = 0; i < xd_frames.size(); i++) {
    auto us = users[(int)i];
    if (us.empty()) { slot_role.push_back(""); continue; }
    std::stable_sort(us.begin(), us.end(), [&](auto& a, auto& b) { return rank(a.first) < rank(b.first); });
    int s = us[0].first, mo = us[0].second;
    if (s == 9 && mo >= 1) { slot_role.push_back("faint"); continue; }
    auto it = role_of_section().find(s);
    slot_role.push_back(it == role_of_section().end() ? "" : it->second);
  }
  std::vector<int> roots;
  for (int i = 0; i < nb; i++) if (sk.parent[i] < 0) roots.push_back(i);
  std::vector<GfVerts> gverts;
  std::vector<const std::vector<int>*> gtabs;
  if (opts.ground) {
    for (auto& me_ : gm.meshes) {
      if (hidden_mesh(me_)) continue;
      for (auto& sm : me_.subs) {
        if (!drawn(me_, sm)) continue;
        const auto& uall = cx.used_of(sm);
        std::vector<int> u;
        for (size_t i = 0; i < uall.size(); i += 2) u.push_back(uall[i]);
        GfVerts vv;
        for (auto& kv : sm.verts.a) {
          if (kv.second.rows != sm.verts.n) continue;
          Arr a;
          a.rows = (int)u.size();
          a.cols = kv.second.cols;
          a.v.resize((size_t)a.rows * a.cols);
          for (int r = 0; r < a.rows; r++) for (int c = 0; c < a.cols; c++) a.at(r, c) = kv.second.at(u[r], c);
          vv.a[kv.first] = std::move(a);
        }
        vv.n = (int)u.size();
        if (vv.has("pos") && vv.get("pos").rows) { gverts.push_back(std::move(vv)); gtabs.push_back(&sm.bone_idx); }
      }
    }
  }
  std::vector<SubRef> gpts;
  for (size_t i = 0; i < gverts.size(); i++) gpts.push_back({gtabs[i], &gverts[i]});
  struct Samp { std::vector<std::vector<V3>> S, R, T; int n; std::vector<double> src; };
  std::map<std::pair<std::string, int>, Samp> cache;
  auto sampled = [&](const std::string& role, int nframes) -> Samp& {
    auto key = std::make_pair(role, nframes);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const GfMotion* mo = sp.mot(role);
    if (!mo) mo = sp.mot("idle");
    int nat_ = std::max(mo->frames, 1);
    std::vector<double> e = motion_energy(sk, *mo);
    std::vector<double> fr = role_warp(role, nat_, nframes, e);
    Samp sm;
    sm.src = fr;
    sk.sample_at(*mo, fr, sm.S, sm.R, sm.T);
    sm.n = (int)fr.size() - 1;
    if (opts.ground) {
      auto adj = ground_offsets(sk, sm.S, sm.R, sm.T, gpts, shift, hover);
      for (int r_ : roots)
        for (size_t k = 0; k < sm.T.size(); k++) sm.T[k][r_][1] += adj[k];
    }
    return cache[key] = std::move(sm);
  };
  std::map<int, const Samp*> uses;
  for (size_t slot = 0; slot < slot_role.size(); slot++) {
    if (slot_role[slot].empty()) continue;
    uses[(int)slot] = &sampled(slot_role[slot], xd_frames[slot]);
  }
  double atol = opts.anim_tol;
  double tols[3] = {0.001 * atol, 0.0003 * atol, 0.001 * atol};
  double ctol = opts.chan_tol;
  std::vector<char> chan((size_t)nb * 9, 0);
  for (auto& kv : uses) {
    const Samp& sm = *kv.second;
    const std::vector<std::vector<V3>>* AA[3] = {&sm.S, &sm.R, &sm.T};
    const std::vector<V3>* bind[3] = {&sk.bind_s, &sk.bind_r, &sk.bind_t};
    for (int w = 0; w < 3; w++)
      for (int i = 0; i < nb; i++)
        for (int ax = 0; ax < 3; ax++) {
          double mx = 0;
          for (size_t k = 0; k < AA[w]->size(); k++) mx = std::max(mx, std::fabs((*AA[w])[k][i][ax] - (*bind[w])[i][ax]));
          if (mx > tols[w] * ctol) chan[(size_t)i * 9 + w * 3 + ax] = 1;
        }
  }
  const int kinds[3] = {pbr::J_SCAX, pbr::J_ROTX, pbr::J_TRAX};
  const char* FACE_KINDS[4] = {"sleep", "wake", "wink", nullptr};
  std::map<int, std::pair<std::string, int>> face_slots;
  if (eyes && !eyes->names.empty()) {
    for (int r_ = 0; r_ < 4; r_++) {
      int rec = 0x1C + r_ * 0x13;
      int idx_ = xd_pkx.header[rec + 0x12];
      if (xd_pkx.header[rec] && FACE_KINDS[r_] && idx_ < (int)xd_frames.size() && slot_role[idx_].empty() && !face_slots.count(idx_))
        face_slots[idx_] = {FACE_KINDS[r_], r_};
    }
  }
  auto scale_track = [&](int frames) {
    pbr::AnimObj ao;
    ao.end = (double)frames;
    ao.tracks.push_back(Track{pbr::J_SCAX, 0.0, {Key{pbr::OP_CON, sf_root, 0.0, frames}, Key{pbr::OP_CON, sf_root, 0.0, 0}}, std::nullopt});
    return ao;
  };
  auto set_mats = [&](pbr::Animation& an, std::vector<std::pair<int, pbr::MatList>> v) {
    for (auto& p : v) an.set_material(p.first, std::optional<pbr::MatList>(std::move(p.second)));
  };
  for (size_t slot = 0; slot < xd_frames.size(); slot++) {
    int frames = xd_frames[slot];
    pbr::Animation an;
    auto fs = face_slots.find((int)slot);
    if (fs != face_slots.end()) {
      int nfr = std::max(frames, 1);
      an.name = "face_" + fs->second.first;
      an.set_joint(1, scale_track(nfr));
      set_mats(an, eye_anims(eyes->face_values(fs->second.first, nfr), nfr));
      m.animations.push_back(std::move(an));
      continue;
    }
    if (!uses.count((int)slot)) {
      an.name = "none";
      if (eyes) set_mats(an, eye_anims(eyes->role_values("idle", std::vector<double>(std::max(frames, 1) + 1, 0.0)), std::max(frames, 1)));
      an.set_joint(1, scale_track(std::max(frames, 1)));
      m.animations.push_back(std::move(an));
      continue;
    }
    const Samp& sm = *uses[(int)slot];
    int n = sm.n;
    an.name = slot_role[slot];
    if (eyes) set_mats(an, eye_anims(eyes->role_values(slot_role[slot], sampled(slot_role[slot], xd_frames[slot]).src), n));
    const std::vector<std::vector<V3>>* AA[3] = {&sm.S, &sm.R, &sm.T};
    for (int i = 0; i < nb; i++) {
      pbr::AnimObj ao;
      ao.end = (double)n;
      for (int w = 0; w < 3; w++)
        for (int ax = 0; ax < 3; ax++)
          if (chan[(size_t)i * 9 + w * 3 + ax]) {
            std::vector<double> s;
            double k_ = (bake && w == 2) ? size_factor : 1.0;
            for (size_t k = 0; k < AA[w]->size(); k++) s.push_back((*AA[w])[k][i][ax] * k_);
            Track t;
            t.kind = kinds[w] + ax;
            t.keys = pbr::keys_of(s, tols[w] * k_ * 1.5);
            ao.tracks.push_back(std::move(t));
          }
      if (!ao.tracks.empty()) an.set_joint(jn[i], std::move(ao));
    }
    std::vector<std::array<double, 6>> boxes;
    std::array<double, 6> last{};
    for (int k = 0; k <= n; k++) {
      if (k % 3 == 0 || k == n) {
        auto W = sk.world(sm.S[k], sm.R[k], sm.T[k]);
        double mn[3] = {1e300, 1e300, 1e300}, mx[3] = {-1e300, -1e300, -1e300};
        for (auto& me_ : gm.meshes) {
          if (std::find(hidden.begin(), hidden.end(), me_.name) != hidden.end()) continue;
          for (auto& s2 : me_.subs) {
            if (!drawn(me_, s2)) continue;
            auto pts = skin_points(sk, W, s2.bone_idx, s2.verts);
            const auto& u = cx.used_of(s2);
            for (size_t q = 0; q < u.size(); q += 4) {
              double p[3] = {pts[u[q]][0], pts[u[q]][1] + shift, pts[u[q]][2]};
              for (int c = 0; c < 3; c++) {
                double v = p[c] * size_factor;
                mn[c] = std::min(mn[c], v);
                mx[c] = std::max(mx[c], v);
              }
            }
          }
        }
        last = {mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]};
      }
      boxes.push_back(last);
    }
    an.bounds = boxes;
    m.animations.push_back(std::move(an));
  }
  m.bound_head = xd_model.bound_head ? *xd_model.bound_head : std::vector<int>();
  int idle_slot = 0;
  for (size_t i = 0; i < slot_role.size(); i++) if (slot_role[i] == "idle") { idle_slot = (int)i; break; }
  auto xwm = pbr::pose_at(xd_model, xd_model.animations.empty() ? nullptr : &xd_model.animations[idle_slot], 0.0);
  auto wm = m.world_static();
  std::vector<std::pair<int, V3>> cand;
  for (int i = 0; i < nb; i++) cand.push_back({jn[i], {wm[jn[i]][3], wm[jn[i]][7], wm[jn[i]][11]}});
  auto remapj = [&](int xj) -> int {
    if (xj < 0) return xj;
    if (xj >= (int)xd_model.joints.size()) return 0;
    if (xj == 0 || xj == xd_model.skeleton_root) return xj == 0 ? 0 : 1;
    V3 p = {xwm[xj][3], xwm[xj][7], xwm[xj][11]};
    int best = 0;
    double bd = 1e300;
    for (size_t c = 0; c < cand.size(); c++) {
      double dx = cand[c].second[0] - p[0], dy = cand[c].second[1] - p[1], dz = cand[c].second[2] - p[2];
      double d = (dx * dx + dy * dy) + dz * dz;
      if (d < bd) { bd = d; best = (int)c; }
    }
    return cand[best].first;
  };
  if (opts.fx) {
    add_particle_tracks(m, xd_model, xwm, jn, sk, gm, nb, log);
    add_fire_scroll(m, gm, sp.mot("_fx"), log);
  }
  pbr::Pkx pkx;
  pkx.header = xd_pkx.header;
  pkx.sections = xd_pkx.sections;
  pkx.particles = xd_pkx.particles;
  pkx.set_head_joint(remapj(xd_pkx.head_joint()));
  for (size_t s = 0; s < pkx.sections.size(); s++) {
    std::vector<int> nw;
    for (int j : pkx.section_joints(s)) nw.push_back(remapj(j));
    pkx.set_section_joints(s, nw);
  }
  auto& h = pkx.header;
  for (int r = 0; r < 4; r++) {
    int rec = 0x1C + r * 0x13;
    bool any = false;
    for (auto& kv : face_slots) if (kv.second.second == r) any = true;
    if (eyes && any) {
      if (h[rec] == 2) {
        h[rec] = 1;
        h[rec + 1] = 0;
        for (int k = 2; k < 0x12; k++) h[rec + k] = 255;
      }
    } else {
      h[rec] = 0;
    }
  }
  B.pkx = std::move(pkx);
  return B;
}

}

std::vector<uint8_t> convert(const Species& sp, const std::vector<uint8_t>& xd_bytes, const Opts& opts, const Logger& log, ConvertInfo* info, double step) {
  static const bool math_set = [] {
    pbr::mat_rot_sin = [](double x) { return dm::sin(x); };
    pbr::mat_rot_cos = [](double x) { return dm::cos(x); };
    return true;
  }();
  (void)math_set;
  double st = step;
  while (true) {
    Built B = build(sp, xd_bytes, opts, log, st);
    pbr::WriteReport rep;
    B.pkx.archive = pbr::hsd_write_archive(B.m, &rep);
    if (rep.pobj > pbr::MAX_POBJ && st < 0.34) {
      log(sf("INFO: %d drawing pieces: skin weights redone coarser", (int)rep.pobj));
      st = pyround(st + 0.05, 2);
      continue;
    }
    std::vector<uint8_t> data = B.pkx.build();
    if (info) {
      info->size = data.size();
      info->tris = B.tris;
      info->scale = B.scale;
      info->report = rep;
    }
    log(sf("written %d bytes (XD original %d)", (int)data.size(), (int)xd_bytes.size()));
    return data;
  }
}

Opts opts_xd_species() {
  Opts o;
  o.ground = true; o.decimate = 2600; o.fire = true; o.light = 1.0; o.fx = true; o.bake_scale = true; o.eyes = true; o.size_rule = 1;
  o.eye_blend = true; o.ghost_alpha = 0.6; o.steel = 1.2; o.lure = true; o.cheeks = 1.0; o.match = true;
  o.hover_model = false;
  return o;
}
Opts opts_gen4(double height_cm) {
  Opts o;
  o.ground = true; o.decimate = 2600; o.fire = true; o.light = 1.0; o.fx = true; o.bake_scale = true; o.eyes = true; o.size_rule = 2; o.height_cm = height_cm;
  o.hover_model = true; o.eye_blend = true; o.ghost_alpha = 0.6; o.steel = 1.2; o.lure = true; o.cheeks = 1.0;
  return o;
}
std::vector<uint8_t> convert_one(const Species& sp, const std::vector<uint8_t>& xd_bytes, const Opts& o, ConvertInfo* info, std::vector<std::string>* log) {
  Logger lg = [log](const std::string& s) { if (log) log->push_back(s); };
  return convert(sp, xd_bytes, o, lg, info);
}

const std::vector<LadderStep>& ladder() {
  static const std::vector<LadderStep> L = {{256, 2600, 1, 1, 0}, {256, 2600, 3, 2, 0}, {128, 2600, 3, 2, 0}, {128, 2600, 8, 3, 0}, {128, 2600, 16, 3, 32},
                                            {128, 2600, 16, 4, 16}, {128, 2000, 24, 4, 16}, {64, 1600, 24, 5, 12}, {64, 1200, 32, 6, 8}, {64, 900, 32, 6, 8},
                                            {64, 600, 32, 6, 8}};
  return L;
}

std::vector<uint8_t> convert_ladder(const Species& sp, const std::vector<uint8_t>& xd_bytes, double height_cm, int start, ConvertInfo* info) {
  const auto& L = ladder();
  start = std::max(0, std::min(start, (int)L.size() - 1));
  std::vector<uint8_t> pkx;
  int step = start;
  for (step = start; step < (int)L.size(); step++) {
    Opts o;
    o.ground = true; o.fire = true; o.light = 1.0; o.eyes = true; o.size_rule = 2; o.max_side = L[step].side; o.height_cm = height_cm;
    o.hover_model = true; o.anim_tol = L[step].atol; o.chan_tol = L[step].ctol; o.quant = L[step].quant; o.decimate = L[step].dec;
    pkx = convert(sp, xd_bytes, o, [](const std::string&) {}, info);
    if (pkx.size() <= FILE_CAP_BYTES) break;
  }
  if (step >= (int)L.size()) step = (int)L.size() - 1;
  if (info) info->step = step;
  return pkx;
}

GfModel load_model_only(const Garc& g, int group) {
  return parse_model(pc_split(lz11_maybe(g.read(1 + 9 * (size_t)group)))[0]);
}

Species load_species(const Garc& g, int group) {
  Species sp;
  sp.group = group;
  size_t base = 1 + 9 * (size_t)group;
  sp.gm = parse_model(pc_split(lz11_maybe(g.read(base)))[0]);
  for (auto& t : pc_split(lz11_maybe(g.read(base + 1)))) {
    GfTexture T = parse_texture(t);
    sp.texs[T.name] = decode_texture(T);
  }
  auto subs = pc_split(lz11_maybe(g.read(base + 4)));
  static const std::pair<const char*, int> GF_SLOT[5] = {{"idle", 0}, {"hit", 16}, {"faint", 17}, {"phys", 8}, {"special", 12}};
  for (auto& rs : GF_SLOT) {
    size_t slot = rs.second;
    if (slot < subs.size() && !subs[slot].empty()) {
      GfMotion mo;
      if (parse_motion(subs[slot], mo) && !mo.bones.empty()) sp.mots.push_back({rs.first, std::move(mo)});
    }
  }
  for (size_t slot = 0; slot < subs.size(); slot++) {
    bool is_role = false;
    for (auto& rs : GF_SLOT) if ((size_t)rs.second == slot) is_role = true;
    if (is_role || subs[slot].empty()) continue;
    GfMotion mo;
    bool ok = false;
    try { ok = parse_motion(subs[slot], mo); } catch (const std::exception&) { continue; }
    bool fire_keys = false;
    if (ok) for (auto& c : mo.mats) if (c.name.find("Fire") != std::string::npos && (!c.tx.empty() || !c.ty.empty())) fire_keys = true;
    if (ok && mo.bones.empty() && mo.loop && fire_keys) {
      sp.mots.push_back({"_fx", std::move(mo)});
      break;
    }
  }
  if (!has(sp.mots, "idle")) throw Fail("no idle motion");
  for (int slot : {1, 6, 3, 5, 9, 12}) {
    if ((size_t)slot < subs.size() && !subs[slot].empty()) {
      GfMotion mo;
      bool ok = false;
      try { ok = parse_motion(subs[slot], mo); } catch (const std::exception&) { ok = false; }
      bool eye = false;
      if (ok) for (auto& mm : mo.mats) if (mm.unit == 0 && !mm.ty.empty() && mm.name.find("Eye") != std::string::npos) eye = true;
      if (ok && eye) {
        sp.mots.push_back({"_blink", std::move(mo)});
        break;
      }
    }
  }
  for (const char* role : {"hit", "faint", "phys", "special"}) {
    if (has(sp.mots, role)) continue;
    const GfMotion* src = sp.mot("phys");
    if (!src) src = sp.mot("special");
    if (!src) src = sp.mot("idle");
    GfMotion copy = *src;
    sp.mots.push_back({role, std::move(copy)});
  }
  return sp;
}

}
