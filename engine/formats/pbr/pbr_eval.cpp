#include "pbr_eval.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdexcept>

namespace pbr {

namespace {
constexpr double PI = 3.141592653589793;
using V9 = std::array<double, 9>;
}

M4 mm4(const double* a, const double* b) {
  M4 r;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      double acc = 0.0;
      for (int k = 0; k < 4; k++) acc = std::fma(a[i * 4 + k], b[k * 4 + j], acc);
      r[i * 4 + j] = acc;
    }
  return r;
}

std::array<double, 9> rot3(double rx, double ry, double rz) {
  double sx = std::sin(rx), cx = std::cos(rx), sy = std::sin(ry), cy = std::cos(ry), sz = std::sin(rz), cz = std::cos(rz);
  V9 m;
  m[0] = cy * cz;
  m[1] = sx * sy * cz - cx * sz;
  m[2] = cx * sy * cz + sx * sz;
  m[3] = cy * sz;
  m[4] = sx * sy * sz + cx * cz;
  m[5] = cx * sy * sz - sx * cz;
  m[6] = -sy;
  m[7] = sx * cy;
  m[8] = cx * cy;
  return m;
}

double det3(const double* m) {
  double a[3][3];
  for (int i = 0; i < 9; i++) a[i / 3][i % 3] = m[i];
  int sign = 1;
  double logdet = 0.0;
  for (int c = 0; c < 3; c++) {
    int p = c;
    for (int r = c + 1; r < 3; r++)
      if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
    if (a[p][c] == 0.0) return 0.0;
    if (p != c) {
      for (int k = 0; k < 3; k++) std::swap(a[p][k], a[c][k]);
      sign = -sign;
    }
    for (int r = c + 1; r < 3; r++) {
      double f = a[r][c] / a[c][c];
      for (int k = c; k < 3; k++) a[r][k] -= f * a[c][k];
    }
  }
  for (int c = 0; c < 3; c++) {
    if (a[c][c] < 0) sign = -sign;
    logdet += std::log(std::fabs(a[c][c]));
  }
  return sign * std::exp(logdet);
}

Eval::Eval(const sdr::Sdr& sdr_, const std::string& compensate) : src(sdr_) {
  const auto& nodes = src.nodes;
  n = (int)nodes.size();
  for (auto& x : nodes) parent.push_back(x.parent);
  for (auto& x : nodes) is_joint.push_back(x.kind == 2);
  for (auto& x : nodes) orient.push_back(rot3(x.orient[0], x.orient[1], x.orient[2]));
  for (auto& x : nodes) {
    bool c = false;
    const sdr::Node* par = x.parent >= 0 ? &nodes[x.parent] : nullptr;
    if (x.kind == 2) {
      if (compensate == "joint8_under_joint") c = (x.jflags & 8) && par && par->kind == 2;
      else if (compensate == "under_joint") c = par && par->kind == 2;
      else if (compensate == "not_joint8_under_joint") c = !(x.jflags & 8) && par && par->kind == 2;
      else if (compensate == "never") c = false;
      else if (compensate == "always") c = true;
      else if (compensate == "node8") c = (x.flags & 8) != 0;
      else c = (x.jflags & 8) != 0;
    }
    comp.push_back(c);
  }
  for (auto& x : nodes) {
    V9 p{};
    if (x.has_pivots && x.kind != 2)
      for (int k = 0; k < 9; k++) p[k] = x.pivots[k];
    piv.push_back(p);
  }
  for (auto& x : nodes) {
    st_pos.push_back({x.pos[0], x.pos[1], x.pos[2]});
    st_rot.push_back({x.rot[0], x.rot[1], x.rot[2]});
    st_scale.push_back({x.scale[0], x.scale[1], x.scale[2]});
  }
  for (auto& x : nodes) {
    M4 m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (x.has_inv_bind)
      for (int k = 0; k < 12; k++) m[k] = x.inv_bind[k];
    inv_bind.push_back(m);
  }
  for (int i = 0; i < n; i++) {
    const sdr::Node& x = nodes[i];
    if (x.kind != 3 || !x.has_mesh || x.name.rfind("col_", 0) == 0) continue;
    const sdr::Mesh& me = x.mesh;
    EvMesh em;
    em.node = i;
    for (auto& v : me.verts) em.pos.push_back({v.p[0], v.p[1], v.p[2]});
    if (me.has_weights) {
      em.has_terms = true;
      for (size_t vi = 0; vi < me.weights.size(); vi++)
        for (auto& w : me.weights[vi]) {
          if (!(w.second > 0)) continue;
          int node = -1;
          for (auto& bn : src.by_number)
            if (bn.first == w.first) node = bn.second;
          if (node < 0) throw std::runtime_error("KeyError: node number " + std::to_string(w.first));
          std::vector<std::pair<int, double>>* lst = nullptr;
          for (auto& t : em.terms)
            if (t.first == node) lst = &t.second;
          if (!lst) {
            em.terms.emplace_back(node, std::vector<std::pair<int, double>>());
            lst = &em.terms.back().second;
          }
          lst->emplace_back((int)vi, w.second);
        }
    }
    meshes.push_back(std::move(em));
  }
}

int Eval::frames_of(int act) const {
  double v = std::nearbyint(src.actions[act].f[2] * FPS);
  int f = (int)v;
  return f > 1 ? f : 1;
}

Pose Eval::sample_static() const { return sample(-1); }

Pose Eval::sample(int act, int frames, const std::function<double(double)>& tmap) const {
  const auto& nodes = src.nodes;
  int nf = act < 0 ? 1 : (frames < 0 ? frames_of(act) : frames) + 1;
  size_t N = (size_t)n;
  std::vector<double> pos(nf * N * 3), rot(nf * N * 3), sca(nf * N * 3);
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++)
      for (int k = 0; k < 3; k++) {
        pos[((size_t)f * N + i) * 3 + k] = st_pos[i][k];
        rot[((size_t)f * N + i) * 3 + k] = st_rot[i][k];
        sca[((size_t)f * N + i) * 3 + k] = st_scale[i][k];
      }
  std::vector<char> vis((size_t)nf * N, 1), animated(N, 0);
  if (act >= 0) {
    std::vector<double> times(nf);
    for (int f = 0; f < nf; f++) times[f] = tmap ? tmap(f / FPS) : f / FPS;
    for (int i = 0; i < n; i++) {
      const std::vector<sdr::Curve>* curves = nodes[i].anim.find(act);
      if (!curves || curves->empty()) continue;
      for (const sdr::Curve& c : *curves) {
        if (c.values.empty()) continue;
        if (c.what == 3) {
          for (int f = 0; f < nf; f++) vis[(size_t)f * N + i] = c.at(times[f]) >= 0.5;
        } else if ((c.what == 0 || c.what == 1 || c.what == 2) && 1 <= c.axis && c.axis <= 3) {
          std::vector<double>& dst = c.what == 0 ? pos : (c.what == 1 ? rot : sca);
          for (int f = 0; f < nf; f++) dst[((size_t)f * N + i) * 3 + c.axis - 1] = c.at(times[f]);
          animated[i] = 1;
        }
      }
    }
  }
  Pose p;
  p.frames = nf;
  p.n = n;
  p.scale = sca;
  p.animated = animated;
  p.rot_only.resize(nf * N * 9);
  p.local.assign(nf * N * 16, 0.0);
  p.dcomp.assign(nf * N * 3, 1.0);
  p.compensated.assign(N, 0);
  std::vector<double> t = pos;
  std::vector<double> m3(nf * N * 9);
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++) {
      size_t b = (size_t)f * N + i;
      V9 rm = rot3(rot[b * 3], rot[b * 3 + 1], rot[b * 3 + 2]);
      if (is_joint[i]) {
        V9 q;
        const V9& o = orient[i];
        for (int r = 0; r < 3; r++)
          for (int c = 0; c < 3; c++) q[r * 3 + c] = (o[r * 3 + 0] * rm[0 * 3 + c] + o[r * 3 + 1] * rm[1 * 3 + c] + o[r * 3 + 2] * rm[2 * 3 + c]) + 0.0;
        rm = q;
      }
      for (int k = 0; k < 9; k++) p.rot_only[b * 9 + k] = rm[k];
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m3[b * 9 + r * 3 + c] = rm[r * 3 + c] * sca[b * 3 + c];
      const V9& pv = piv[i];
      for (int r = 0; r < 3; r++) {
        double e1 = ((rm[r * 3 + 0] * pv[3] + rm[r * 3 + 2] * pv[5]) + rm[r * 3 + 1] * pv[4]) + 0.0;
        double e2 = ((m3[b * 9 + r * 3 + 0] * pv[0] + m3[b * 9 + r * 3 + 2] * pv[2]) + m3[b * 9 + r * 3 + 1] * pv[1]) + 0.0;
        t[b * 3 + r] = ((t[b * 3 + r] + pv[6 + r]) + e1) + e2;
      }
    }
  for (int i = 0; i < n; i++) {
    if (!(comp[i] && parent[i] >= 0)) continue;
    int pa = parent[i];
    bool any = false;
    for (int f = 0; f < nf && !any; f++)
      for (int k = 0; k < 3; k++)
        if (std::fabs(sca[((size_t)f * N + pa) * 3 + k] - 1.0) > 1e-5) any = true;
    if (!any) continue;
    for (int f = 0; f < nf; f++) {
      size_t b = (size_t)f * N + i;
      double safe[3];
      for (int k = 0; k < 3; k++) {
        double ps = sca[((size_t)f * N + pa) * 3 + k];
        safe[k] = std::fabs(ps) < 1e-8 ? 1.0 : ps;
      }
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) m3[b * 9 + r * 3 + c] = m3[b * 9 + r * 3 + c] / safe[r];
      for (int k = 0; k < 3; k++) p.dcomp[b * 3 + k] = 1.0 / safe[k];
      if (comp_t)
        for (int k = 0; k < 3; k++) t[b * 3 + k] = t[b * 3 + k] / safe[k];
    }
    p.compensated[i] = 1;
  }
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++) {
      size_t b = (size_t)f * N + i;
      double* m = &p.local[b * 16];
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) m[r * 4 + c] = m3[b * 9 + r * 3 + c];
        m[r * 4 + 3] = t[b * 3 + r];
      }
      m[15] = 1.0;
    }
  std::vector<char> eff = vis;
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++)
      if (parent[i] >= 0) eff[(size_t)f * N + i] = eff[(size_t)f * N + i] && eff[(size_t)f * N + parent[i]];
  p.visible = eff;
  p.own_visible = vis;
  return p;
}

std::vector<double> Eval::world_of(const std::vector<double>& local, int frames) const {
  size_t N = (size_t)n;
  std::vector<double> w(local.size());
  for (int f = 0; f < frames; f++)
    for (int i = 0; i < n; i++) {
      size_t b = ((size_t)f * N + i) * 16;
      int pa = parent[i];
      if (pa < 0) {
        std::copy(local.begin() + b, local.begin() + b + 16, w.begin() + b);
      } else {
        M4 r = mm4(&w[((size_t)f * N + pa) * 16], &local[b]);
        std::copy(r.begin(), r.end(), w.begin() + b);
      }
    }
  return w;
}

std::vector<double> Eval::world(const Pose& p) const { return world_of(p.local, p.frames); }

std::vector<PointSet> Eval::points(const std::vector<double>& world, int frames, const char* visible) const {
  size_t N = (size_t)n;
  std::vector<PointSet> out;
  for (const EvMesh& em : meshes) {
    PointSet ps;
    ps.node = em.node;
    ps.nv = (int)em.pos.size();
    ps.pts.assign((size_t)frames * ps.nv * 3, 0.0);
    auto ein = [](const double* w3x4, const double* h, double* o) {
      for (int i = 0; i < 3; i++) {
        const double* w = w3x4 + i * 4;
        o[i] = ((w[0] * h[0] + w[2] * h[2]) + (w[1] * h[1] + w[3] * h[3])) + 0.0;
      }
    };
    for (int f = 0; f < frames; f++) {
      double* P = &ps.pts[(size_t)f * ps.nv * 3];
      if (!em.has_terms) {
        const double* w = &world[((size_t)f * N + em.node) * 16];
        for (int v = 0; v < ps.nv; v++) {
          double h[4] = {em.pos[v][0], em.pos[v][1], em.pos[v][2], 1.0};
          ein(w, h, P + v * 3);
        }
      } else {
        for (auto& jt : em.terms) {
          int j = jt.first;
          M4 sk = mm4(&world[((size_t)f * N + j) * 16], inv_bind[j].data());
          for (auto& va : jt.second) {
            int v = va.first;
            double h[4] = {em.pos[v][0], em.pos[v][1], em.pos[v][2], 1.0};
            double o[3];
            ein(sk.data(), h, o);
            for (int i = 0; i < 3; i++) P[v * 3 + i] = P[v * 3 + i] + o[i] * va.second;
          }
        }
      }
    }
    if (visible) {
      for (int f = 0; f < frames; f++)
        if (!visible[(size_t)f * N + em.node])
          for (size_t k = 0; k < (size_t)ps.nv * 3; k++) ps.pts[(size_t)f * ps.nv * 3 + k] = std::nan("");
    }
    out.push_back(std::move(ps));
  }
  return out;
}

std::vector<double> boxes(const std::vector<PointSet>& sets, int frames) {
  if (sets.empty()) throw std::runtime_error("need at least one array to concatenate");
  std::vector<double> out((size_t)frames * 6);
  for (int f = 0; f < frames; f++) {
    for (int ax = 0; ax < 3; ax++) {
      double lo = std::nan(""), hi = std::nan("");
      bool have = false;
      for (auto& s : sets)
        for (int v = 0; v < s.nv; v++) {
          double x = s.pts[((size_t)f * s.nv + v) * 3 + ax];
          if (x != x) continue;
          if (!have) { lo = hi = x; have = true; continue; }
          if (x < lo || (x == lo && std::signbit(x))) lo = x;
          if (x > hi || (x == hi && !std::signbit(x))) hi = x;
        }
      auto fix = [](double v) {
        if (v != v) return 0.0;
        if (v == INFINITY) return DBL_MAX;
        if (v == -INFINITY) return -DBL_MAX;
        return v;
      };
      out[(size_t)f * 6 + ax] = fix(lo);
      out[(size_t)f * 6 + 3 + ax] = fix(hi);
    }
  }
  return out;
}

std::vector<double> continuous_euler(const std::vector<double>& rm, int frames, int k) {
  std::vector<double> e((size_t)frames * k * 3), out(e.size());
  for (size_t b = 0; b < (size_t)frames * k; b++) {
    const double* m = &rm[b * 9];
    double r20 = m[6];
    if (r20 < -1.0) r20 = -1.0;
    if (r20 > 1.0) r20 = 1.0;
    double ry = -std::asin(r20);
    bool plain = std::fabs(r20) < 0.999999;
    double rx = plain ? std::atan2(m[7], m[8]) : std::atan2(-m[5], m[4]);
    double rz = plain ? std::atan2(m[3], m[0]) : 0.0;
    e[b * 3] = rx;
    e[b * 3 + 1] = ry;
    e[b * 3 + 2] = rz;
  }
  double two = 2 * PI;
  for (int j = 0; j < k; j++)
    for (int c = 0; c < 3; c++) out[(size_t)j * 3 + c] = e[(size_t)j * 3 + c];
  for (int f = 1; f < frames; f++)
    for (int j = 0; j < k; j++) {
      const double* prev = &out[((size_t)(f - 1) * k + j) * 3];
      const double* a0 = &e[((size_t)f * k + j) * 3];
      double a[3] = {a0[0], a0[1], a0[2]};
      double b[3] = {a0[0] + PI, PI - a0[1], a0[2] + PI};
      for (int c = 0; c < 3; c++) {
        a[c] = a[c] - std::nearbyint((a[c] - prev[c]) / two) * two;
        b[c] = b[c] - std::nearbyint((b[c] - prev[c]) / two) * two;
      }
      double da = (std::fabs(a[0] - prev[0]) + std::fabs(a[1] - prev[1])) + std::fabs(a[2] - prev[2]);
      double db = (std::fabs(b[0] - prev[0]) + std::fabs(b[1] - prev[1])) + std::fabs(b[2] - prev[2]);
      double* o = &out[((size_t)f * k + j) * 3];
      const double* pick = db < da ? b : a;
      for (int c = 0; c < 3; c++) o[c] = pick[c];
    }
  return out;
}

Decomp decompose(const Pose& pose, const Eval& ev, const std::set<int>& pairs) {
  int nf = pose.frames, n = pose.n;
  size_t N = (size_t)n;
  std::vector<double> t((size_t)nf * N * 3), s = pose.scale;
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++)
      for (int k = 0; k < 3; k++) t[((size_t)f * N + i) * 3 + k] = pose.loc(f, i)[k * 4 + 3];
  std::vector<double> r = continuous_euler(pose.rot_only, nf, n);
  for (int i = 0; i < n; i++) {
    if (!pose.compensated[i] || pairs.count(i)) continue;
    std::vector<double> rr((size_t)nf * 9), sc2((size_t)nf * 3), scv((size_t)nf * 3);
    std::vector<char> ok(nf);
    for (int f = 0; f < nf; f++) {
      const double* L = pose.loc(f, i);
      double m[9] = {L[0], L[1], L[2], L[4], L[5], L[6], L[8], L[9], L[10]};
      double sc[3];
      for (int c = 0; c < 3; c++) sc[c] = std::sqrt((m[c] * m[c] + m[3 + c] * m[3 + c]) + m[6 + c] * m[6 + c]);
      double det = det3(m);
      bool good = sc[0] > 1e-8 && sc[1] > 1e-8 && sc[2] > 1e-8;
      double q[3] = {det < 0 ? -sc[0] : sc[0], sc[1], sc[2]};
      for (int c = 0; c < 3; c++) {
        double dv = q[c] == 0 ? 1.0 : q[c];
        for (int rw = 0; rw < 3; rw++) rr[(size_t)f * 9 + rw * 3 + c] = m[rw * 3 + c] / dv;
        sc2[(size_t)f * 3 + c] = q[c];
      }
      ok[f] = good;
    }
    std::vector<double> eu = continuous_euler(rr, nf, 1);
    for (int f = 0; f < nf; f++) {
      size_t b = ((size_t)f * N + i) * 3;
      if (ok[f]) {
        for (int c = 0; c < 3; c++) {
          r[b + c] = eu[(size_t)f * 3 + c];
          s[b + c] = sc2[(size_t)f * 3 + c];
        }
      }
    }
  }
  Decomp d;
  d.frames = nf;
  std::vector<int> pl(pairs.begin(), pairs.end());
  if (pl.empty()) {
    d.ne = n;
    d.s = s; d.r = r; d.t = t;
    return d;
  }
  int ne = n + (int)pl.size();
  d.ne = ne;
  d.s.assign((size_t)nf * ne * 3, 0.0);
  d.r.assign((size_t)nf * ne * 3, 0.0);
  d.t.assign((size_t)nf * ne * 3, 0.0);
  for (int f = 0; f < nf; f++) {
    for (int i = 0; i < n; i++)
      for (int c = 0; c < 3; c++) {
        d.s[((size_t)f * ne + i) * 3 + c] = s[((size_t)f * N + i) * 3 + c];
        d.r[((size_t)f * ne + i) * 3 + c] = r[((size_t)f * N + i) * 3 + c];
        d.t[((size_t)f * ne + i) * 3 + c] = t[((size_t)f * N + i) * 3 + c];
      }
    for (size_t k = 0; k < pl.size(); k++)
      for (int c = 0; c < 3; c++) {
        d.s[((size_t)f * ne + n + k) * 3 + c] = pose.dcomp[((size_t)f * N + pl[k]) * 3 + c];
        d.r[((size_t)f * ne + n + k) * 3 + c] = 0.0;
        d.t[((size_t)f * ne + n + k) * 3 + c] = t[((size_t)f * N + pl[k]) * 3 + c];
        d.t[((size_t)f * ne + pl[k]) * 3 + c] = 0.0;
      }
  }
  return d;
}

static M4 compose4(const double* s, const double* r, const double* t) {
  std::array<double, 9> rm = rot3(r[0], r[1], r[2]);
  M4 m{};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) m[i * 4 + j] = rm[i * 3 + j] * s[j];
    m[i * 4 + 3] = t[i];
  }
  m[15] = 1.0;
  return m;
}

std::vector<double> compose_local(const Decomp& d, int n, const std::set<int>& pairs) {
  int nf = d.frames, ne = d.ne;
  std::vector<double> loc((size_t)nf * n * 16);
  for (int f = 0; f < nf; f++)
    for (int i = 0; i < n; i++) {
      size_t b = ((size_t)f * ne + i) * 3;
      M4 m = compose4(&d.s[b], &d.r[b], &d.t[b]);
      std::copy(m.begin(), m.end(), loc.begin() + ((size_t)f * n + i) * 16);
    }
  int k = 0;
  for (int i : pairs) {
    for (int f = 0; f < nf; f++) {
      size_t b = ((size_t)f * ne + n + k) * 3;
      M4 a = compose4(&d.s[b], &d.r[b], &d.t[b]);
      M4 r = mm4(a.data(), &loc[((size_t)f * n + i) * 16]);
      std::copy(r.begin(), r.end(), loc.begin() + ((size_t)f * n + i) * 16);
    }
    k++;
  }
  return loc;
}

std::vector<double> shear_error(const Pose& pose, const Eval& ev) {
  Decomp d = decompose(pose, ev, {});
  int nf = pose.frames, n = pose.n;
  std::vector<double> err(n, 0.0);
  for (int i = 0; i < n; i++) {
    if (!pose.compensated[i]) continue;
    double worst = 0.0;
    for (int f = 0; f < nf; f++) {
      const double* L = pose.loc(f, i);
      double m[9] = {L[0], L[1], L[2], L[4], L[5], L[6], L[8], L[9], L[10]};
      size_t b = ((size_t)f * d.ne + i) * 3;
      std::array<double, 9> rm = rot3(d.r[b], d.r[b + 1], d.r[b + 2]);
      double nrm = 0.0;
      for (int c = 0; c < 3; c++) {
        double v = std::sqrt((m[c] * m[c] + m[3 + c] * m[3 + c]) + m[6 + c] * m[6 + c]);
        if (v > nrm) nrm = v;
      }
      double mx = 0.0;
      for (int rw = 0; rw < 3; rw++)
        for (int c = 0; c < 3; c++) {
          double rec = rm[rw * 3 + c] * d.s[b + c];
          double df = std::fabs(m[rw * 3 + c] - rec);
          if (df > mx) mx = df;
        }
      double q = mx / (nrm > 1e-8 ? nrm : 1e-8);
      if (q > worst) worst = q;
    }
    err[i] = worst;
  }
  return err;
}

}
