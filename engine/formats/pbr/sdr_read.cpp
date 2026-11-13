#include "pbr_sdr.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace pbr {
namespace sdr {

static std::string fmt(const char* f, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

[[noreturn]] static void fail(const std::string& s) { throw std::runtime_error(s); }

double Curve::at(double t) const {
  const std::vector<double>& v = values;
  if (v.empty()) return 0.0;
  if (!has_keys) {
    double x = t * fps;
    long long i = (long long)x;
    if (i >= (long long)v.size() - 1) return v.back();
    if (i < 0) return v[0];
    double f = x - (double)i;
    return v[i] + (v[i + 1] - v[i]) * f;
  }
  const std::vector<Key>& k = keys;
  if (k.empty()) fail("curve without keys");
  if (t <= k[0].time) return v.at(k[0].value);
  for (size_t n = 0; n + 1 < k.size(); n++) {
    const Key &a = k[n], &b = k[n + 1];
    if (t < b.time) {
      double dt = b.time - a.time;
      double p0 = v.at(a.value), p1 = v.at(b.value);
      if (b.interp == 0 || dt <= 0) return p0;
      double s = (t - a.time) / dt;
      if (b.interp == 1) return p0 + (p1 - p0) * s;
      double m0 = a.slope_out < (int)slopes.size() ? slopes[a.slope_out] : 0.0;
      double m1 = b.slope_in < (int)slopes.size() ? slopes[b.slope_in] : 0.0;
      double s2 = s * s, s3 = s * s * s;
      return ((2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * dt * m0 + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * dt * m1);
    }
  }
  return v.at(k.back().value);
}

uint32_t Sdr::u32(uint64_t o) const {
  if (o + 4 > b.size()) fail(fmt("read past the end of the file at 0x%llX", (unsigned long long)o));
  return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3];
}
int Sdr::u16(uint64_t o) const {
  if (o + 2 > b.size()) fail(fmt("read past the end of the file at 0x%llX", (unsigned long long)o));
  return b[o] << 8 | b[o + 1];
}
int Sdr::u8(uint64_t o) const {
  if (o >= b.size()) fail(fmt("read past the end of the file at 0x%llX", (unsigned long long)o));
  return b[o];
}
double Sdr::f32(uint64_t o) const {
  uint32_t v = u32(o);
  float f;
  memcpy(&f, &v, 4);
  return (double)f;
}
std::string Sdr::cstr(uint64_t o) const {
  if (!o) return "";
  if (o >= b.size()) fail("string past the end of the file");
  size_t e = o;
  while (e < b.size() && b[e]) e++;
  if (e >= b.size()) fail("unterminated string");
  return std::string((const char*)&b[o], e - o);
}
void Sdr::vec(uint64_t o, double* out, const double* dflt, int n) const {
  for (int i = 0; i < n; i++) out[i] = o ? f32(o + 4 * i) : dflt[i];
}
int Sdr::tex_at(uint32_t off) const {
  for (int i = (int)tex_keys_.size() - 1; i >= 0; i--)
    if (tex_keys_[i] == off) return i;
  return -1;
}
int Sdr::mat_at(uint32_t off) const {
  for (int i = (int)mat_keys_.size() - 1; i >= 0; i--)
    if (mat_keys_[i] == off) return i;
  return -1;
}

static int gs_to_gx(uint32_t code) {
  switch (code) {
    case 0x40: return 0; case 0x42: return 1; case 0x41: return 2; case 0x43: return 3; case 0x44: return 4;
    case 0x90: return 5; case 0x45: return 6; case 0x00: return 8; case 0x01: return 9; case 0x30: return 10;
    case 0xB0: return 14;
  }
  return -1;
}

Sdr::Sdr(std::vector<uint8_t> bytes) : b(std::move(bytes)) {
  if (b.size() < 4 || b[0] != 1 || b[1] != 0 || b[2] != 0 || b[3] != 4) fail("not a Battle Revolution model file");
  read_textures();
  read_materials();
  uint32_t sk = u32(u32(8));
  if (u16(0x18) != 1) warnings.push_back(fmt("%d skeletons in the file: only the first is read", u16(0x18)));
  name = cstr(u32(sk));
  for (int a = 0; a < u16(sk + 8); a++) {
    uint64_t o = (uint64_t)u32(sk + 0xC) + (uint64_t)a * 0x30;
    Action ac;
    ac.name = cstr(u32(o));
    for (int i = 0; i < 3; i++) ac.f[i] = f32(o + 4 + 4 * i);
    actions.push_back(ac);
  }
  walk(u32(sk + 0x10), -1);
  for (size_t i = 0; i < nodes.size(); i++) {
    bool found = false;
    for (auto& p : by_number)
      if (p.first == nodes[i].number) { p.second = (int)i; found = true; }
    if (!found) by_number.emplace_back(nodes[i].number, (int)i);
  }
  std::sort(by_number.begin(), by_number.end());
  material_anims();
}

void Sdr::read_textures() {
  int n = u16(0x1A);
  for (int i = 0; i < n; i++) {
    uint32_t t = u32((uint64_t)u32(0xC) + 4 * i);
    tex_keys_.push_back(t);
    Texture x;
    x.code = u32(t + 8);
    uint32_t size = u32(t + 0x4C);
    uint64_t data = (uint64_t)t + u32(t + 0x28);
    x.width = u16(t);
    x.height = u16(t + 2);
    x.format = gs_to_gx(x.code);
    x.wrap_s = u32(t + 0x10);
    x.wrap_t = u32(t + 0x14);
    if (data < b.size()) {
      uint64_t e = std::min<uint64_t>(data + size, b.size());
      x.data.assign(b.begin() + data, b.begin() + e);
    }
    x.palette_format = u32(t + 0xC);
    x.palette_at = u32(t + 0x48);
    x.levels = u8((uint64_t)t + 5);
    textures.push_back(std::move(x));
  }
}

void Sdr::read_materials() {
  int n = u16(0x1E);
  for (int i = 0; i < n; i++) {
    uint32_t m = u32((uint64_t)u32(0x14) + 4 * i);
    mat_keys_.push_back(m);
    uint32_t tex = u32(m + 0x18);
    Material mt;
    int nl = std::min(u8(m + 0x5A), 4);
    for (int k = 0; k < nl; k++) {
      uint32_t r = u32(m + 0x2C + 4 * k);
      Layer L;
      if (r) {
        if ((uint64_t)r + 0x18 > b.size()) fail("layer record past the end of the file");
        memcpy(L.raw, &b[r], 0x18);
        L.offset[0] = f32(r + 4); L.offset[1] = f32(r + 8); L.rotation = f32(r + 0xC);
        L.scale[0] = f32(r + 0x10); L.scale[1] = f32(r + 0x14);
      }
      L.texture = tex_at(u32(m + 0x18 + 4 * k));
      L.uv = L.raw[0] == 1;
      L.uvset = L.uv ? std::max((int)L.raw[1] - 4, 0) : -1;
      mt.layers.push_back(L);
    }
    mt.name = cstr(u32(m));
    mt.texture = tex_at(tex);
    for (int k = 0; k < 4; k++) mt.ambient[k] = (uint8_t)u8(m + 0x60 + k);
    for (int k = 0; k < 4; k++) mt.diffuse[k] = (uint8_t)u8(m + 0x64 + k);
    if (m < b.size()) mt.raw.assign(b.begin() + m, b.begin() + std::min<uint64_t>((uint64_t)m + 0x80, b.size()));
    materials.push_back(std::move(mt));
  }
}

void Sdr::walk(uint64_t o, int parent) {
  while (o) {
    int idx = (int)nodes.size();
    nodes.emplace_back();
    nodes[idx].parent = parent;
    {
      Node n;
      n.parent = parent;
      n.kind = u32(o);
      n.name = cstr(u32(o + 4));
      n.number = u16(o + 8);
      n.flags = u16(o + 0xA);
      static const double z[3] = {0, 0, 0}, one[3] = {1, 1, 1};
      vec(u32(o + 0xC), n.pos, z);
      vec(u32(o + 0x10), n.rot, z);
      vec(u32(o + 0x14), n.scale, one);
      if (u32(o + 0x18)) {
        n.has_pivots = true;
        for (int i = 0; i < 12; i++) n.pivots[i] = f32(u32(o + 0x18) + 4 * i);
      }
      if (n.kind == 2) {
        n.jflags = u32(o + 0x30);
        for (int i = 0; i < 3; i++) n.orient[i] = f32(o + 0x34 + 4 * i);
        n.has_inv_bind = true;
        for (int i = 0; i < 12; i++) n.inv_bind[i] = f32(o + 0x44 + 4 * i);
      } else if (n.kind == 3) {
        n.has_mesh = true;
        n.mesh = read_mesh(u32(o + 0x30));
      }
      uint64_t a = u32(o + 0x20);
      while (a) {
        int act = u16(a);
        std::vector<Curve> cs;
        int nc = u16(a + 2);
        for (int c = 0; c < nc; c++) cs.push_back(curve((uint64_t)u32(a + 4) + c * 0x10));
        n.anim.set(act, std::move(cs));
        a = u32(a + 0xC);
      }
      nodes[idx] = std::move(n);
    }
    walk(u32(o + 0x24), idx);
    o = u32(o + 0x28);
  }
}

Curve Sdr::curve(uint64_t q) {
  Curve c;
  c.what = u8(q + 1);
  c.axis = u8(q + 2);
  int kind = u8(q + 6), exp = u8(q + 7);
  uint64_t kd = u32(q + 8);
  uint64_t vals = u32(kd), slopes = u32(kd + 4);
  int nv = u16(kd + 8);
  uint64_t keys = u32(kd + 0x10);
  int nk = u16(kd + 0x14), fps = u16(kd + 0x16);
  if (kind != 0 && kind != 5 && kind != 6 && kind != 7 && kind != 8) {
    warnings.push_back(fmt("curve number kind %d not read (taken as constant 0)", kind));
    return c;
  }
  double div = std::ldexp(1.0, exp);
  for (int i = 0; i < nv; i++) {
    switch (kind) {
      case 0: c.values.push_back(f32(vals + 4 * i)); break;
      case 5: c.values.push_back((double)u8(vals + i) / div); break;
      case 6: c.values.push_back((double)(int8_t)u8(vals + i) / div); break;
      case 7: c.values.push_back((double)u16(vals + 2 * i) / div); break;
      case 8: c.values.push_back((double)(int16_t)u16(vals + 2 * i) / div); break;
    }
  }
  if (keys) {
    c.has_keys = true;
    int top = 0;
    for (int k = 0; k < nk; k++) {
      uint64_t o = keys + (uint64_t)k * 12;
      Curve::Key key;
      key.interp = u16(o);
      key.value = u16(o + 2);
      key.slope_in = u16(o + 4);
      key.slope_out = u16(o + 6);
      key.time = f32(o + 8);
      c.keys.push_back(key);
      top = std::max(top, std::max(key.slope_in, key.slope_out));
    }
    if (slopes)
      for (int i = 0; i < top + 1; i++) c.slopes.push_back(f32(slopes + 4 * i));
  } else {
    c.fps = (fps & 0xFF) ? (fps & 0xFF) : 30;
  }
  return c;
}

Mesh Sdr::read_mesh(uint64_t m) {
  Mesh me;
  int nv = u16(m + 2);
  uint64_t vp = u32(m + 8);
  int stride = (u8(m + 1) & 0x20) ? 48 : 24;
  uint64_t p0 = u32(m + 0x18);
  if (p0) {
    uint64_t k = u32(p0 + 0x10);
    while (u8(k) != 0xFF) {
      if (u8(k) == 9 && u8(k + 5) != 0 && u8(k + 5) != stride) {
        warnings.push_back(fmt("vertex stride %d in the attribute list, %d by the mesh flag: list taken", u8(k + 5), stride));
        stride = u8(k + 5);
      }
      k += 8;
    }
  }
  for (int i = 0; i < nv; i++) {
    Mesh::Vert v;
    for (int j = 0; j < 3; j++) v.p[j] = f32(vp + (uint64_t)i * stride + 4 * j);
    for (int j = 0; j < 3; j++) v.n[j] = f32(vp + (uint64_t)i * stride + 12 + 4 * j);
    me.verts.push_back(v);
  }
  uint64_t uvl = u32(m + 0x14);
  if (uvl) {
    int nsets = std::max((int)std::min<uint32_t>(u32(m + 4), 4), 1);
    for (int k = 0; k < nsets; k++) {
      uint64_t ua = u32(uvl + 8 * k);
      int un = u16(uvl + 8 * k + 4);
      std::vector<std::pair<double, double>> set;
      if (ua)
        for (int i = 0; i < un; i++) set.emplace_back(f32(ua + (uint64_t)i * 8), f32(ua + (uint64_t)i * 8 + 4));
      me.uvsets.push_back(std::move(set));
    }
  }
  uint64_t w = u32(m + 0xC);
  if (w) {
    me.has_weights = true;
    std::vector<Weights>& ws = me.weights;
    int n1 = u16(w);
    uint64_t p1 = u32(w + 4);
    for (int i = 0; i < n1; i++) {
      int cnt = u16(p1 + i * 4), node = u16(p1 + i * 4 + 2);
      for (int j = 0; j < cnt; j++) ws.push_back(Weights{{node, 1.0}});
    }
    int n2 = u16(w + 8);
    uint64_t p2 = u32(w + 0xC), pa = u32(w + 0x10);
    int k = 0;
    for (int i = 0; i < n2; i++) {
      int cnt = u16(p2 + i * 6), na = u16(p2 + i * 6 + 2), nb = u16(p2 + i * 6 + 4);
      for (int j = 0; j < cnt; j++) {
        double a = u16(pa + (uint64_t)k * 2) / 65535.0;
        k++;
        Weights d{{na, a}};
        double cur = 0.0;
        Weights::iterator it = d.end();
        for (auto e = d.begin(); e != d.end(); ++e)
          if (e->first == nb) it = e;
        if (it != d.end()) { cur = it->second; it->second = (cur + 1.0) - a; }
        else d.emplace_back(nb, (cur + 1.0) - a);
        ws.push_back(std::move(d));
      }
    }
    int n3 = u16(w + 0x14);
    uint64_t p3 = u32(w + 0x18);
    for (int i = 0; i < n3; i++) {
      uint64_t o = p3 + (uint64_t)i * 10;
      int v = u16(o), na = u16(o + 2), nb = u16(o + 4);
      double wa = u16(o + 6), wb = u16(o + 8);
      wa /= 65535.0;
      wb = nb != 0xFFFF ? wb / 65535.0 : 0.0;
      if (v >= (int)ws.size()) {
        warnings.push_back(fmt("weight extra for vertex %d beyond the lists", v));
        continue;
      }
      double keep = 1.0 - wa - wb;
      Weights d;
      for (auto& e : ws[v]) d.emplace_back(e.first, e.second * keep);
      auto add = [&](int key, double amount) {
        for (auto& e : d)
          if (e.first == key) { e.second = e.second + amount; return; }
        d.emplace_back(key, 0.0 + amount);
      };
      add(na, wa);
      if (nb != 0xFFFF) add(nb, wb);
      ws[v] = std::move(d);
    }
    if ((int)ws.size() != nv) {
      warnings.push_back(fmt("weight lists cover %d of %d vertices", (int)ws.size(), nv));
      while ((int)ws.size() < nv) ws.emplace_back();
    }
  }
  uint64_t p = u32(m + 0x18);
  while (p) {
    std::vector<std::pair<int, int>> attrs;
    uint64_t k = u32(p + 0x10);
    while (u8(k) != 0xFF) {
      attrs.emplace_back(u8(k), u8(k + 4));
      k += 8;
    }
    for (auto& a : attrs)
      if (a.second != 2 && a.second != 3) fail("mesh part with direct vertex data: not handled");
    Part part;
    uint64_t q = u32(p + 0x14);
    int np = u16(p + 0xC);
    for (int pi = 0; pi < np; pi++) {
      int op = u8(q);
      int cnt = u16(q + 1);
      q += 3;
      std::vector<Corner> corners;
      for (int ci = 0; ci < cnt; ci++) {
        int val[256];
        bool have[256] = {false};
        for (auto& a : attrs) {
          if (a.second == 2) { val[a.first] = u8(q); q += 1; }
          else { val[a.first] = u16(q); q += 2; }
          have[a.first] = true;
        }
        auto get = [&](int id) { return have[id] ? val[id] : -1; };
        Corner c;
        c.pos = get(9);
        c.nrm = have[10] ? val[10] : get(0x19);
        c.uv0 = get(13);
        c.uv1 = get(14);
        c.uv2 = get(15);
        corners.push_back(c);
      }
      int prim = op & 0xF8;
      auto tri = [&](int a, int bb, int c) {
        Tri t;
        t.c[0] = corners[a]; t.c[1] = corners[bb]; t.c[2] = corners[c];
        part.tris.push_back(t);
      };
      if (prim == 0x90) {
        for (int i = 0; i < cnt - 2; i += 3) tri(i, i + 1, i + 2);
      } else if (prim == 0x98) {
        for (int i = 0; i < cnt - 2; i++) {
          if (i % 2 == 0) tri(i, i + 1, i + 2);
          else tri(i + 1, i, i + 2);
        }
      } else if (prim == 0xA0) {
        for (int i = 1; i < cnt - 1; i++) tri(0, i, i + 1);
      } else if (prim == 0x80) {
        for (int i = 0; i < cnt - 3; i += 4) { tri(i, i + 1, i + 2); tri(i, i + 2, i + 3); }
      } else {
        fail(fmt("mesh primitive 0x%02X: not handled", op));
      }
    }
    part.material = mat_at(u32(p + 8));
    me.parts.push_back(std::move(part));
    p = u32(p + 0x1C);
  }
  return me;
}

void Sdr::material_anims() {
  for (size_t i = 0; i < materials.size(); i++) {
    Material& m = materials[i];
    uint64_t mo = u32((uint64_t)u32(0x14) + 4 * i);
    int nk = std::max((int)m.layers.size(), 1);
    for (int k = 0; k < nk; k++) {
      uint64_t p = u32(mo + 0x40 + 4 * k);
      if (!p || u8(p) != 0) continue;
      bool ff = p + 2 < b.size() && p + 3 < b.size() && b[p + 2] == 0xFF && b[p + 3] == 0xFF;
      if (!ff) continue;
      Anim blocks;
      uint64_t blk = p + 8;
      bool ok = true;
      while (blk && blocks.size() <= actions.size()) {
        int act = u16(blk), nc = u16(blk + 2);
        uint64_t ct = u32(blk + 4);
        double ln = f32(blk + 8);
        uint64_t nxt = u32(blk + 0xC);
        if (act >= (int)actions.size() || std::fabs(ln - actions[act].f[2]) > 0.02 || nc > 16) {
          ok = false;
          break;
        }
        std::vector<Curve> cs;
        for (int c = 0; c < nc; c++) cs.push_back(curve(ct + c * 0x10));
        blocks.set(act, std::move(cs));
        blk = nxt;
      }
      if (ok && !blocks.empty()) {
        bool replaced = false;
        for (auto& e : m.layer_anims)
          if (e.first == k) { e.second = blocks; replaced = true; }
        if (!replaced) m.layer_anims.emplace_back(k, blocks);
      } else if (!blocks.empty()) {
        warnings.push_back(fmt("material %d layer %d: animation blocks that do not fit the actions: not read", (int)i, k));
      }
    }
  }
}

}
}
