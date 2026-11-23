#include "n3ds_decimate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace n3ds {

namespace {

using V3 = std::array<double, 3>;
using Q4 = std::array<std::array<double, 4>, 4>;

V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot3(const V3& a, const V3& b) { return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2]; }
double norm3(const V3& a) { return std::sqrt(dot3(a, a)); }

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

}

DecimateResult decimate(const std::vector<V3>& pos, const std::vector<std::array<double, 2>>& uv, const std::vector<Weights>& wts,
                        const std::vector<std::array<int, 3>>& tris, int target, double uv_weight, double skin_weight) {
  DecimateResult res;
  int n = (int)pos.size();
  if ((int)tris.size() <= target || tris.size() < 8) {
    for (int i = 0; i < n; i++) res.keep.push_back(i);
    res.tris = tris;
    return res;
  }
  V3 mn = pos[0], mx = pos[0];
  for (auto& p : pos)
    for (int k = 0; k < 3; k++) {
      mn[k] = std::min(mn[k], p[k]);
      mx[k] = std::max(mx[k], p[k]);
    }
  double scale = norm3(sub(mx, mn));
  if (scale == 0.0) scale = 1.0;
  std::vector<Q4> Q(n);
  for (auto& q : Q)
    for (auto& r : q) r.fill(0.0);
  for (auto& t : tris) {
    const V3 &pa = pos[t[0]], &pb = pos[t[1]], &pc = pos[t[2]];
    V3 nn = cross(sub(pb, pa), sub(pc, pa));
    double area = norm3(nn);
    if (area < 1e-12) continue;
    nn = {nn[0] / area, nn[1] / area, nn[2] / area};
    double p[4] = {nn[0], nn[1], nn[2], -dot3(nn, pa)};
    for (int v : t)
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) Q[v][i][j] = Q[v][i][j] + (p[i] * p[j]) * (area * 0.5);
  }
  bool has_uv = !uv.empty();
  std::vector<std::map<int, double>> wd(n);
  for (int i = 0; i < n && i < (int)wts.size(); i++)
    for (auto& kv : wts[i]) wd[i][kv.first] = kv.second;
  std::vector<std::array<int, 3>> faces = tris;
  std::vector<char> face_alive(faces.size(), 1);
  std::vector<std::set<int>> vf(n);
  std::map<std::pair<int, int>, int> edge_faces;
  for (size_t i = 0; i < faces.size(); i++)
    for (int k = 0; k < 3; k++) {
      int a = faces[i][k], b = faces[i][(k + 1) % 3];
      vf[a].insert((int)i);
      edge_faces[{std::min(a, b), std::max(a, b)}]++;
    }
  std::set<int> locked;
  for (auto& kv : edge_faces)
    if (kv.second != 2) {
      locked.insert(kv.first.first);
      locked.insert(kv.first.second);
    }
  int alive = (int)faces.size();
  std::vector<int> ver(n, 0);

  auto wdiff = [&](int a, int b) {
    std::set<int> keys;
    for (auto& kv : wd[a]) keys.insert(kv.first);
    for (auto& kv : wd[b]) keys.insert(kv.first);
    std::vector<double> terms;
    for (int k : keys) {
      auto ia = wd[a].find(k), ib = wd[b].find(k);
      double x = ia == wd[a].end() ? 0.0 : ia->second, y = ib == wd[b].end() ? 0.0 : ib->second;
      terms.push_back(std::fabs(x - y));
    }
    return py_sum(terms);
  };
  auto cost = [&](int a, int b) {
    double v[4] = {pos[b][0], pos[b][1], pos[b][2], 1.0};
    double M[4][4];
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) M[i][j] = Q[a][i][j] + Q[b][i][j];
    double tmp[4];
    for (int j = 0; j < 4; j++) tmp[j] = ((v[0] * M[0][j] + v[1] * M[1][j]) + v[2] * M[2][j]) + v[3] * M[3][j];
    double c = (((tmp[0] * v[0] + tmp[1] * v[1]) + tmp[2] * v[2]) + tmp[3] * v[3]) / (scale * scale);
    if (has_uv) {
      double d0 = uv[a][0] - uv[b][0], d1 = uv[a][1] - uv[b][1];
      c += uv_weight * (d0 * d0 + d1 * d1) * 0.01;
    }
    c += skin_weight * wdiff(a, b) * 0.001;
    return c > 0.0 ? c : 0.0;
  };
  auto neighbours = [&](int a) {
    std::set<int> s;
    for (int f : vf[a])
      for (int x : faces[f]) s.insert(x);
    s.erase(a);
    return s;
  };
  using Entry = std::tuple<double, int, int, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
  auto push = [&](int a) {
    if (locked.count(a)) return;
    for (int b : neighbours(a)) heap.emplace(cost(a, b), ver[a], a, b);
  };
  for (int a = 0; a < n; a++)
    if (!vf[a].empty() || true) {
      bool used = !vf[a].empty();
      if (used) push(a);
    }
  while (alive > target && !heap.empty()) {
    auto [c, v, a, b] = heap.top();
    heap.pop();
    (void)c;
    if (vf[a].empty() || ver[a] != v || vf[b].empty()) continue;
    std::vector<int> shared;
    for (int f : vf[a])
      if (faces[f][0] == b || faces[f][1] == b || faces[f][2] == b) shared.push_back(f);
    if (shared.empty()) continue;
    std::set<int> na = neighbours(a), nbs = neighbours(b), common;
    for (int x : na)
      if (nbs.count(x)) common.insert(x);
    std::set<int> apex;
    for (int f : shared)
      for (int x : faces[f])
        if (x != a && x != b) apex.insert(x);
    if (common != apex) continue;
    bool bad = false;
    for (int f : vf[a]) {
      if (std::find(shared.begin(), shared.end(), f) != shared.end()) continue;
      auto t = faces[f];
      std::array<int, 3> q = t;
      for (int& x : q)
        if (x == a) x = b;
      V3 p0 = cross(sub(pos[t[1]], pos[t[0]]), sub(pos[t[2]], pos[t[0]]));
      V3 p1 = cross(sub(pos[q[1]], pos[q[0]]), sub(pos[q[2]], pos[q[0]]));
      double n0 = norm3(p0), n1 = norm3(p1);
      if (n1 < 1e-12 || n0 < 1e-12 || dot3(p0, p1) / (n0 * n1) < 0.3) {
        bad = true;
        break;
      }
    }
    if (bad) continue;
    for (int f : shared) {
      for (int x : faces[f]) vf[x].erase(f);
      face_alive[f] = 0;
      alive--;
    }
    std::vector<int> mine(vf[a].begin(), vf[a].end());
    for (int f : mine) {
      for (int& x : faces[f])
        if (x == a) x = b;
      vf[b].insert(f);
    }
    vf[a].clear();
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) Q[b][i][j] = Q[b][i][j] + Q[a][i][j];
    ver[b]++;
    std::set<int> nb2 = neighbours(b);
    for (int x : nb2) ver[x]++;
    nb2.insert(b);
    for (int x : nb2) push(x);
  }
  std::set<int> keepset;
  for (size_t f = 0; f < faces.size(); f++)
    if (face_alive[f])
      for (int x : faces[f]) keepset.insert(x);
  res.keep.assign(keepset.begin(), keepset.end());
  std::map<int, int> idx;
  for (size_t k = 0; k < res.keep.size(); k++) idx[res.keep[k]] = (int)k;
  for (size_t f = 0; f < faces.size(); f++)
    if (face_alive[f]) res.tris.push_back({idx[faces[f][0]], idx[faces[f][1]], idx[faces[f][2]]});
  return res;
}

}
