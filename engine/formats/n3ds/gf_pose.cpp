#include "n3ds_pose.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "detmath.h"

namespace n3ds {

static const double PI = 3.14159265358979323846;

M4 m4_identity() {
  M4 r;
  memset(&r, 0, sizeof r);
  for (int i = 0; i < 4; i++) r.m[i][i] = 1;
  return r;
}

M4 m4_mul(const M4& a, const M4& b) {
  M4 c;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      double acc = a.m[i][0] * b.m[0][j];
      acc = acc + a.m[i][1] * b.m[1][j];
      acc = acc + a.m[i][2] * b.m[2][j];
      acc = acc + a.m[i][3] * b.m[3][j];
      c.m[i][j] = acc;
    }
  return c;
}

bool m4_inv(const M4& A, M4* out) {
  double a[4][8];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      a[i][j] = A.m[i][j];
      a[i][4 + j] = i == j ? 1.0 : 0.0;
    }
  for (int c = 0; c < 4; c++) {
    int p = c;
    double mx = std::fabs(a[c][c]);
    for (int i = c + 1; i < 4; i++)
      if (std::fabs(a[i][c]) > mx) { mx = std::fabs(a[i][c]); p = i; }
    if (a[p][c] == 0.0) return false;
    if (p != c)
      for (int j = 0; j < 8; j++) std::swap(a[p][j], a[c][j]);
    double d = a[c][c];
    for (int j = 0; j < 8; j++) a[c][j] = a[c][j] / d;
    for (int i = 0; i < 4; i++) {
      if (i == c) continue;
      double f = a[i][c];
      if (f == 0.0) continue;
      for (int j = 0; j < 8; j++) a[i][j] = a[i][j] - f * a[c][j];
    }
  }
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) out->m[i][j] = a[i][4 + j];
  return true;
}

Mat3 rot_mat(double rx, double ry, double rz) {
  double sx = dm::sin(rx), cx = dm::cos(rx), sy = dm::sin(ry), cy = dm::cos(ry), sz = dm::sin(rz), cz = dm::cos(rz);
  Mat3 R;
  R[0] = {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz};
  R[1] = {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz};
  R[2] = {-sy, sx * cy, cx * cy};
  return R;
}

static double norm3(double a, double b, double c) { return std::sqrt((a * a + b * b) + c * c); }

Mat3 axis_angle_mat(const V3& v) {
  double ang = norm3(v[0], v[1], v[2]) * 2.0;
  Mat3 I = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  if (ang <= 1e-12) return I;
  double d = ang / 2.0;
  double x = v[0] / d, y = v[1] / d, z = v[2] / d;
  double c = dm::cos(ang), s = dm::sin(ang);
  double t = 1 - c;
  Mat3 R;
  R[0] = {t * x * x + c, t * x * y - s * z, t * x * z + s * y};
  R[1] = {t * x * y + s * z, t * y * y + c, t * y * z - s * x};
  R[2] = {t * x * z - s * y, t * y * z + s * x, t * z * z + c};
  return R;
}

V3 euler_from_mat(const Mat3& R) {
  double r20 = R[2][0], rx, ry, rz;
  if (std::fabs(r20) < 0.999999) {
    ry = -dm::asin(r20);
    rx = dm::atan2(R[2][1], R[2][2]);
    rz = dm::atan2(R[1][0], R[0][0]);
  } else {
    ry = -std::copysign(PI / 2, r20);
    rz = 0.0;
    rx = dm::atan2(-R[1][2], R[1][1]);
  }
  return {rx, ry, rz};
}

static double det3(const Mat3& a) {
  return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
         a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}

static void split(const Mat3& A, V3& sc, V3& eul) {
  Mat3 R;
  for (int j = 0; j < 3; j++) {
    double s = norm3(A[0][j], A[1][j], A[2][j]);
    sc[j] = s > 1e-9 ? s : 1e-9;
  }
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) R[i][j] = A[i][j] / sc[j];
  if (det3(R) < 0) {
    for (int i = 0; i < 3; i++) R[i][0] = -R[i][0];
    sc[0] = -sc[0];
  }
  eul = euler_from_mat(R);
}

static M4 local4(const V3& sc, const V3& r, const V3& t) {
  M4 L = m4_identity();
  Mat3 R = rot_mat(r[0], r[1], r[2]);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) L.m[i][j] = R[i][j] * sc[j];
  for (int i = 0; i < 3; i++) L.m[i][3] = t[i];
  return L;
}

Skeleton::Skeleton(const std::vector<GfBone>& bones) {
  nb = (int)bones.size();
  std::map<std::string, int> idx;
  for (int i = 0; i < nb; i++) {
    names.push_back(bones[i].name);
    idx[bones[i].name] = i;
  }
  for (int i = 0; i < nb; i++) {
    int p = -1;
    if (bones[i].parent != bones[i].name) {
      auto it = idx.find(bones[i].parent);
      if (it != idx.end()) p = it->second;
    }
    parent.push_back(p);
  }
  for (int i = 0; i < nb; i++) {
    ssc.push_back((bones[i].flags & 1) && parent[i] >= 0);
    raw_s.push_back(bones[i].scale);
    raw_r.push_back(bones[i].rot);
    raw_t.push_back(bones[i].pos);
  }
  bind_s = raw_s;
  bind_r = raw_r;
  bind_t = raw_t;
  ssc_to_plain(bind_s, bind_r, bind_t);
  bind_world = world(bind_s, bind_r, bind_t);
  inv_bind.resize(nb);
  for (int i = 0; i < nb; i++)
    if (!m4_inv(bind_world[i], &inv_bind[i])) throw Fail("a bone's bind matrix cannot be inverted");
}

bool Skeleton::needs_ssc(const std::vector<V3>& s) const {
  for (int i = 0; i < nb; i++)
    if (ssc[i])
      for (int k = 0; k < 3; k++)
        if (std::fabs(s[parent[i]][k] - 1.0) > 1e-4) return true;
  return false;
}

void Skeleton::ssc_to_plain(std::vector<V3>& s, std::vector<V3>& r, std::vector<V3>& t) const {
  if (!needs_ssc(s)) return;
  std::vector<V3> s2 = s, r2 = r, t2 = t;
  std::vector<M4> Wt(nb), Wa(nb);
  for (int i = 0; i < nb; i++) {
    int p = parent[i];
    M4 L = local4(s[i], r[i], t[i]);
    if (p < 0) {
      Wt[i] = Wa[i] = L;
      continue;
    }
    const V3& sp = s[p];
    if (ssc[i] && std::min({std::fabs(sp[0]), std::fabs(sp[1]), std::fabs(sp[2])}) > 0.05) {
      M4 Lt = m4_identity();
      for (int k = 0; k < 3; k++) Lt.m[k][3] = sp[k] * t[i][k];
      M4 Lr = m4_identity();
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) Lr.m[a][b] = L.m[a][b];
      M4 dg = m4_identity();
      for (int k = 0; k < 3; k++) dg.m[k][k] = 1 / sp[k];
      Wt[i] = m4_mul(m4_mul(m4_mul(Wt[p], dg), Lt), Lr);
      Mat3 Rr = rot_mat(r[i][0], r[i][1], r[i][2]);
      Mat3 Mm;
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) Mm[a][b] = (Rr[a][b] * s[i][b]) / sp[a];
      V3 cs, ce;
      split(Mm, cs, ce);
      bool ok = false;
      V3 es{}, ee{}, Dt{};
      M4 inv;
      if (m4_inv(Wa[p], &inv)) {
        M4 D = m4_mul(inv, Wt[i]);
        Mat3 D3;
        for (int a = 0; a < 3; a++)
          for (int b = 0; b < 3; b++) D3[a][b] = D.m[a][b];
        split(D3, es, ee);
        for (int a = 0; a < 3; a++) Dt[a] = D.m[a][3];
        bool fin = true;
        for (int a = 0; a < 4; a++)
          for (int b = 0; b < 4; b++)
            if (!std::isfinite(D.m[a][b])) fin = false;
        double mxe = std::max({std::fabs(es[0]), std::fabs(es[1]), std::fabs(es[2])});
        double mxc = std::max({std::fabs(cs[0]), std::fabs(cs[1]), std::fabs(cs[2])});
        ok = fin && mxe <= 2.0 * std::max(mxc, 1.0);
      }
      if (ok) {
        s2[i] = es;
        r2[i] = ee;
        t2[i] = Dt;
      } else {
        s2[i] = cs;
        r2[i] = ce;
        t2[i] = t[i];
      }
    } else {
      Wt[i] = m4_mul(Wt[p], L);
    }
    Wa[i] = m4_mul(Wa[p], local4(s2[i], r2[i], t2[i]));
  }
  s = s2;
  r = r2;
  t = t2;
}

std::vector<M4> Skeleton::world(const std::vector<V3>& s, const std::vector<V3>& r, const std::vector<V3>& t, const std::vector<char>* axis) const {
  std::vector<M4> W(nb);
  for (int i = 0; i < nb; i++) {
    Mat3 R = (axis && (*axis)[i]) ? axis_angle_mat(r[i]) : rot_mat(r[i][0], r[i][1], r[i][2]);
    M4 L = m4_identity();
    for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++) L.m[a][b] = R[a][b] * s[i][b];
    for (int a = 0; a < 3; a++) L.m[a][3] = t[i][a];
    int p = parent[i];
    W[i] = p < 0 ? L : m4_mul(W[p], L);
  }
  return W;
}

Pose Skeleton::sample_motion(const GfMotion& mo, double frame) const {
  Pose ps;
  ps.s = raw_s;
  ps.r = raw_r;
  ps.t = raw_t;
  ps.axis.assign(nb, 0);
  for (auto& mb : mo.bones) {
    int i = -1;
    for (int k = 0; k < nb; k++)
      if (names[k] == mb.name) { i = k; break; }
    if (i < 0) continue;
    ps.axis[i] = mb.axis;
    if (mb.axis) ps.r[i] = {0.0, 0.0, 0.0};
    for (int k = 0; k < 9; k++) {
      double v;
      if (!herp(mb.ch[k], frame, &v)) continue;
      std::vector<V3>& dst = k / 3 == 0 ? ps.s : k / 3 == 1 ? ps.r : ps.t;
      dst[i][k % 3] = v;
    }
  }
  if (needs_ssc(ps.s)) {
    for (int i = 0; i < nb; i++)
      if (ps.axis[i]) ps.r[i] = euler_from_mat(axis_angle_mat(ps.r[i]));
    ps.axis.assign(nb, 0);
    ssc_to_plain(ps.s, ps.r, ps.t);
  }
  return ps;
}

void Skeleton::sample_at(const GfMotion& mo, const std::vector<double>& frames, std::vector<std::vector<V3>>& S, std::vector<std::vector<V3>>& R,
                         std::vector<std::vector<V3>>& T) const {
  size_t n = frames.size();
  S.assign(n, {});
  R.assign(n, {});
  T.assign(n, {});
  for (size_t k = 0; k < n; k++) {
    Pose ps = sample_motion(mo, frames[k]);
    for (int i = 0; i < nb; i++)
      if (ps.axis[i]) ps.r[i] = euler_from_mat(axis_angle_mat(ps.r[i]));
    S[k] = ps.s;
    R[k] = ps.r;
    T[k] = ps.t;
  }
  const double period = 2 * PI, lowv = -PI;
  std::vector<std::vector<V3>> orig = R;
  for (int i = 0; i < nb; i++)
    for (int c = 0; c < 3; c++) {
      double run = 0.0;
      for (size_t k = 1; k < n; k++) {
        double dd = orig[k][i][c] - orig[k - 1][i][c];
        double md = std::fmod(dd - lowv, period);
        if (md != 0) {
          if (md < 0) md += period;
        } else {
          md = 0.0;
        }
        double ddmod = md + lowv;
        if (ddmod == lowv && dd > 0) ddmod = PI;
        double ph = ddmod - dd;
        if (std::fabs(dd) < PI) ph = 0;
        run = run + ph;
        R[k][i][c] = orig[k][i][c] + run;
      }
    }
}

std::vector<V3> skin_points(const Skeleton& sk, const std::vector<M4>& W, const std::vector<int>& tab0, const GfVerts& verts) {
  const Arr& pos = verts.get("pos");
  int n = pos.rows;
  std::vector<M4> bm(sk.nb);
  std::vector<char> have(sk.nb, 0);
  auto bone_mat = [&](int b) -> const M4& {
    if (b < 0 || b >= sk.nb) throw Fail("skin bone index out of range");
    if (!have[b]) { bm[b] = m4_mul(W[b], sk.inv_bind[b]); have[b] = 1; }
    return bm[b];
  };
  auto xf = [&](const M4& M, int v) {
    V3 o;
    for (int j = 0; j < 3; j++) {
      double acc = pos.at(v, 0) * M.m[j][0];
      acc = acc + pos.at(v, 1) * M.m[j][1];
      acc = acc + pos.at(v, 2) * M.m[j][2];
      o[j] = acc + M.m[j][3];
    }
    return o;
  };
  std::vector<V3> out(n, V3{0, 0, 0});
  if (!verts.has("bidx")) {
    const M4& M = bone_mat(tab0.empty() ? 0 : tab0[0]);
    for (int v = 0; v < n; v++) out[v] = xf(M, v);
    return out;
  }
  const Arr& bi = verts.get("bidx");
  const Arr& bw = verts.get("bw");
  std::vector<int> tab = tab0;
  tab.resize(tab0.size() + 256, 0);
  for (int k = 0; k < bi.cols; k++) {
    bool any = false;
    if (k < bw.cols)
      for (int v = 0; v < n; v++)
        if (bw.at(v, k) != 0.0) { any = true; break; }
    if (!any) continue;
    for (int v = 0; v < n; v++) {
      double w = bw.at(v, k);
      int idx = (int)bi.at(v, k);
      if (idx < 0 || idx >= (int)tab.size()) throw Fail("skin index out of range");
      V3 p = xf(bone_mat(tab[idx]), v);
      for (int c = 0; c < 3; c++) out[v][c] = out[v][c] + p[c] * w;
    }
  }
  return out;
}

std::vector<double> ground_offsets(const Skeleton& sk, const std::vector<std::vector<V3>>& S, const std::vector<std::vector<V3>>& R,
                                   const std::vector<std::vector<V3>>& T, const std::vector<SubRef>& pts, double shift, double hover) {
  int n = (int)S.size();
  std::vector<int> idx;
  for (int i = 0; i < n; i += 3) idx.push_back(i);
  if (idx.back() != n - 1) idx.push_back(n - 1);
  std::vector<double> ym;
  for (int k : idx) {
    auto W = sk.world(S[k], R[k], T[k]);
    double best = 0;
    bool first = true;
    for (auto& sr : pts) {
      auto pp = skin_points(sk, W, *sr.bone_idx, *sr.verts);
      double mn = pp[0][1];
      for (auto& q : pp) mn = std::min(mn, q[1]);
      if (first || mn < best) best = mn;
      first = false;
    }
    ym.push_back(best);
  }
  std::vector<double> y(n);
  for (int i = 0; i < n; i++) {
    size_t j = 0;
    while (j + 1 < idx.size() && idx[j + 1] <= i) j++;
    double v;
    if (j + 1 >= idx.size()) v = ym.back();
    else {
      double slope = (ym[j + 1] - ym[j]) / (double)(idx[j + 1] - idx[j]);
      v = slope * (double)(i - idx[j]) + ym[j];
    }
    y[i] = v + shift;
  }
  std::vector<double> adj(n);
  for (int i = 0; i < n; i++) adj[i] = y[i] < 0 ? -y[i] : 0.0;
  if (hover <= HOVER_MIN)
    for (int i = 0; i < n; i++) adj[i] = adj[i] - 0.85 * (y[i] > 0 ? y[i] : 0.0);
  const double k5[5] = {1 / 9.0, 2 / 9.0, 3 / 9.0, 2 / 9.0, 1 / 9.0};
  std::vector<double> pad(n + 4);
  for (int i = 0; i < n + 4; i++) pad[i] = adj[std::max(0, std::min(n - 1, i - 2))];
  std::vector<double> out(n);
  for (int i = 0; i < n; i++) {
    double acc = pad[i] * k5[0];
    for (int j = 1; j < 5; j++) acc = acc + pad[i + j] * k5[j];
    out[i] = acc;
  }
  return out;
}

double np_sum(const double* a, size_t n) {
  if (n < 8) {
    double res = 0.;
    for (size_t i = 0; i < n; i++) res += a[i];
    return res;
  }
  if (n <= 128) {
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
  return np_sum(a, n2) + np_sum(a + n2, n - n2);
}

static std::vector<double> smooth121(const std::vector<double>& e) {
  size_t n = e.size();
  std::vector<double> out(n);
  for (size_t i = 0; i < n; i++) {
    double a = e[i ? i - 1 : 0], b = e[i], c = e[i + 1 < n ? i + 1 : n - 1];
    double acc = a * 0.25;
    acc = acc + b * 0.5;
    acc = acc + c * 0.25;
    out[i] = acc;
  }
  return out;
}

std::vector<double> motion_energy(const Skeleton& sk, const GfMotion& mo) {
  int nat = std::max(mo.frames, 1);
  std::vector<double> frames;
  for (int k = 0; k <= nat; k++) frames.push_back((double)k);
  std::vector<std::vector<V3>> S, R, T;
  sk.sample_at(mo, frames, S, R, T);
  std::vector<double> e(nat);
  std::vector<double> rowr((size_t)sk.nb * 3), rowt((size_t)sk.nb * 3);
  for (int k = 0; k < nat; k++) {
    for (int i = 0; i < sk.nb; i++)
      for (int c = 0; c < 3; c++) {
        rowr[i * 3 + c] = std::fabs(R[k + 1][i][c] - R[k][i][c]);
        rowt[i * 3 + c] = std::fabs(T[k + 1][i][c] - T[k][i][c]);
      }
    e[k] = np_sum(rowr.data(), rowr.size()) + np_sum(rowt.data(), rowt.size()) / 50.0;
  }
  for (int pass = 0; pass < 2; pass++) e = smooth121(e);
  return e;
}

std::vector<double> role_warp(const std::string& role, int nat, int xd_len, const std::vector<double>& e) {
  double emax = 0;
  for (double x : e) emax = std::max(emax, x);
  if (emax == 0.0) emax = 1.0;
  int s0 = 0, s1 = nat - 1, nhot = 0;
  for (size_t i = 0; i < e.size(); i++)
    if (e[i] >= 0.35 * emax) {
      if (!nhot) s0 = (int)i;
      s1 = (int)i;
      nhot++;
    }
  if (!nhot) { s0 = 0; s1 = nat - 1; }
  int pk = 0;
  for (size_t i = 1; i < e.size(); i++)
    if (e[i] > e[pk]) pk = (int)i;
  std::vector<double> d(nat, 1.0);
  int hold_at = -1;
  int hold = 0;
  auto fill = [&](int a, int b, double v) { for (int i = std::max(a, 0); i < std::min(b, nat); i++) d[i] = v; };
  if (role == "idle") {
    fill(0, nat, 1.25);
  } else if (role == "phys" || role == "special") {
    fill(0, s0, 1.8);
    fill(s0, s1 + 1, 0.95);
    fill(s1 + 1, nat, 1.7);
    hold_at = s1;
    hold = std::max(3, (int)(0.08 * xd_len));
  } else if (role == "hit") {
    fill(0, pk + 1, 0.9);
    fill(pk + 1, nat, 2.4);
    hold_at = pk;
    hold = std::max(2, (int)(0.05 * xd_len));
  } else if (role == "faint") {
    fill(0, s1 + 1, 1.35);
    fill(s1 + 1, nat, 1.5);
    hold_at = nat - 1;
    hold = std::max(0, (int)(0.85 * xd_len - np_sum(d.data(), d.size())));
  }
  for (int pass = 0; pass < 2; pass++) d = smooth121(d);
  double total = np_sum(d.data(), d.size()) + hold;
  double capf = role == "idle" ? 1e9 : role == "hit" ? 0.75 : 0.9;
  double cap = capf * xd_len;
  if (total > cap) {
    double g = cap / total;
    for (auto& x : d) x *= g;
    hold = (int)(hold * g);
  }
  std::vector<double> ts{0.0}, fs{0.0};
  double cum = 0.0;
  for (int k = 0; k < nat; k++) {
    cum += d[k];
    ts.push_back(cum);
    fs.push_back((double)(k + 1));
    if (hold_at >= 0 && k == hold_at && hold) {
      cum += hold;
      ts.push_back(cum);
      fs.push_back((double)(k + 1));
    }
  }
  int n = (int)std::nearbyint(cum);
  std::vector<double> out;
  for (int j = 0; j <= n; j++) {
    double x = (double)j;
    size_t lo = 0, hi = ts.size();
    while (lo + 1 < hi) {
      size_t mid = (lo + hi) / 2;
      if (ts[mid] <= x) lo = mid; else hi = mid;
    }
    double v;
    if (x < ts[0]) v = fs[0];
    else if (lo + 1 >= ts.size()) v = fs.back();
    else {
      double slope = (fs[lo + 1] - fs[lo]) / (ts[lo + 1] - ts[lo]);
      v = slope * (x - ts[lo]) + fs[lo];
    }
    out.push_back(v);
  }
  return out;
}

}
