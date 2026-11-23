#include <cmath>
#include <cstring>

#include "n3ds_gf.h"

namespace n3ds {

namespace {

struct R {
  const Bytes& b;
  size_t p;
  explicit R(const Bytes& bb, size_t pp = 0) : b(bb), p(pp) {}
  void need(size_t n) const {
    if (p + n > b.size()) throw Fail("model file ends too early");
  }
  uint8_t u8() { need(1); return b[p++]; }
  uint16_t u16() { need(2); uint16_t v = (uint16_t)(b[p] | b[p + 1] << 8); p += 2; return v; }
  uint32_t u32() { need(4); uint32_t v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | (uint32_t)b[p + 3] << 24; p += 4; return v; }
  int32_t i32() { return (int32_t)u32(); }
  double f32() { uint32_t u = u32(); float f; memcpy(&f, &u, 4); return (double)f; }
  template <size_t N> std::array<double, N> fl() { std::array<double, N> a; for (size_t i = 0; i < N; i++) a[i] = f32(); return a; }
  std::string cstr(size_t at, size_t n) const {
    std::string s;
    for (size_t i = 0; i < n && at + i < b.size(); i++) {
      if (b[at + i] == 0) break;
      s.push_back((char)b[at + i]);
    }
    return s;
  }
  std::string pstr(size_t n) { need(n); std::string s = cstr(p, n); p += n; return s; }
  std::string bstr() { size_t n = u8(); need(n); std::string s((const char*)&b[p], n); p += n; return s; }
  std::string istr() { size_t n = u32(); need(n); std::string s = cstr(p, n); p += n; return s; }
  void pad16() { if (p & 15) p += 16 - (p & 15); }
  uint32_t section() { pstr(8); uint32_t ln = u32(); u32(); return ln; }
};

double float24(uint32_t v) {
  uint32_t f;
  if (v & 0x7fffff) {
    uint32_t m = v & 0xffff, e = ((v >> 16) & 0x7f) + 64, s = (v >> 23) & 1;
    f = (m << 7) | (e << 23) | (s << 31);
  } else {
    f = (v & 0x800000) << 8;
  }
  float x;
  memcpy(&x, &f, 4);
  return (double)x;
}

std::array<double, 4> vec_f24(uint32_t w0, uint32_t w1, uint32_t w2) {
  return {float24(w2 & 0xffffff), float24((w2 >> 24) | ((w1 & 0xffff) << 8)), float24((w1 >> 16) | ((w0 & 0xff) << 16)), float24(w0 >> 8)};
}

struct PicaCmd {
  uint32_t reg;
  std::vector<uint32_t> ps;
};

std::vector<PicaCmd> pica_cmds(const std::vector<uint32_t>& w) {
  size_t i = 0, n = w.size();
  std::vector<PicaCmd> out;
  while (i + 1 < n) {
    uint32_t param = w[i], cmd = w[i + 1];
    i += 2;
    uint32_t rid = cmd & 0xffff, extra = (cmd >> 20) & 0x7ff, cons = cmd >> 31;
    if (cons) {
      for (uint32_t k = 0; k <= extra; k++) {
        out.push_back({rid, {param}});
        rid++;
        if (k < extra) {
          if (i >= n) throw Fail("model command list ends too early");
          param = w[i++];
        }
      }
    } else {
      PicaCmd c{rid, {param}};
      for (uint32_t k = 0; k < extra && i + k < n; k++) c.ps.push_back(w[i + k]);
      i += extra;
      out.push_back(c);
    }
    if (i & 1) i++;
  }
  return out;
}

const double SCALES[4] = {1 / 127.0, 1 / 255.0, 1 / 32767.0, 1.0};
const char* AN(int k) {
  static const char* names[9] = {"pos", "nrm", "tan", "col", "uv0", "uv1", "uv2", "bidx", "bw"};
  return k >= 0 && k <= 8 ? names[k] : "x";
}

std::vector<std::vector<uint32_t>> read_commands(R& r) {
  std::vector<std::vector<uint32_t>> lst;
  while (true) {
    uint32_t ln = r.u32(), idx = r.u32(), cnt = r.u32();
    r.u32();
    r.need(ln);
    std::vector<uint32_t> w(ln >> 2);
    memcpy(w.data(), &r.b[r.p], (ln >> 2) * 4);
    lst.push_back(std::move(w));
    r.p += ln;
    if ((int64_t)idx >= (int64_t)cnt - 1) break;
  }
  return lst;
}

}

std::string model_first_texture(const Bytes& package) {
  if (package.size() < 12 || package[0] != 'P' || package[1] != 'C') throw Fail("not a model package");
  size_t first = package[4] | package[5] << 8 | package[6] << 16 | (size_t)package[7] << 24;
  size_t last = package[8] | package[9] << 8 | package[10] << 16 | (size_t)package[11] << 24;
  if (first > package.size() || last < first) throw Fail("model package cut short");
  Bytes sub(package.begin() + first, package.begin() + std::min(last, package.size()));
  R x(sub);
  x.u32();
  x.u32();
  x.pad16();
  x.section();
  std::string tex;
  for (int t = 0; t < 4; t++) {
    uint32_t c = x.u32();
    std::vector<std::string> names;
    for (uint32_t i = 0; i < c; i++) {
      x.u32();
      names.push_back(x.pstr(0x40));
    }
    if (t == 1) tex = names.empty() ? std::string() : names[0];
  }
  return tex;
}

GfModel parse_model(const Bytes& b) {
  R r(b);
  r.u32();
  r.u32();
  r.pad16();
  uint32_t secl = r.section();
  size_t mend = r.p + secl;
  std::vector<std::string> tables[4];
  for (int t = 0; t < 4; t++) {
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) {
      r.u32();
      tables[t].push_back(r.pstr(0x40));
    }
  }
  GfModel gm;
  gm.tex_names = tables[1];
  const auto& mat_names = tables[2];
  const auto& mesh_names = tables[3];
  auto bbmin = r.fl<4>();
  auto bbmax = r.fl<4>();
  gm.bbmin = bbmin;
  gm.bbmax = bbmax;
  r.fl<16>();
  uint32_t ul = r.u32(), uo = r.u32();
  r.u32();
  r.u32();
  r.p += (size_t)uo + ul;
  int32_t nb = r.i32();
  r.p += 0xc;
  for (int32_t i = 0; i < nb; i++) {
    GfBone bo;
    bo.name = r.bstr();
    bo.parent = r.bstr();
    bo.flags = r.u8();
    bo.scale = r.fl<3>();
    bo.rot = r.fl<3>();
    bo.pos = r.fl<3>();
    gm.bones.push_back(std::move(bo));
  }
  r.pad16();
  r.i32();
  r.i32();
  r.pad16();
  r.p = mend;
  static const int NCOLS = 12;
  for (size_t mi = 0; mi < mat_names.size(); mi++) {
    uint32_t ln = r.section();
    size_t start = r.p;
    GfMaterial m;
    r.u32();
    m.name = r.bstr();
    r.u32();
    m.shader = r.bstr();
    r.u32();
    m.vtx_shader = r.bstr();
    r.u32();
    m.frag_shader = r.bstr();
    r.p += 16;
    r.p += 8;
    r.need(48);
    for (int k = 0; k < NCOLS; k++)
      for (int c = 0; c < 4; c++) m.colors[k][c] = r.b[r.p + 4 * k + c];
    r.p += 48;
    r.p += 16 + 16 + 8 + 36 + 4 + 16;
    uint32_t nu = r.u32();
    for (uint32_t u = 0; u < nu; u++) {
      r.u32();
      GfCoord co;
      co.name = r.bstr();
      co.unit = r.u8();
      co.mapping = r.u8();
      co.scale = r.fl<2>();
      co.rot = r.f32();
      co.trans = r.fl<2>();
      co.wrap[0] = r.u32();
      co.wrap[1] = r.u32();
      r.u32();
      r.u32();
      r.u32();
      m.coords.push_back(std::move(co));
    }
    r.p = start + ln;
    gm.materials.push_back(std::move(m));
  }
  for (size_t mi = 0; mi < mesh_names.size(); mi++) {
    uint32_t ln = r.section();
    size_t start = r.p;
    r.u32();
    GfMesh me;
    me.name = r.pstr(0x40);
    r.u32();
    r.fl<4>();
    r.fl<4>();
    uint32_t nsub = r.u32();
    r.i32();
    r.p += 0x10;
    auto cmds = read_commands(r);
    std::vector<std::array<int32_t, 4>> sizes;
    for (uint32_t s = 0; s < nsub; s++) {
      r.u32();
      GfSub sm;
      sm.name = r.istr();
      int nbi = r.u8();
      r.need(0x1f);
      for (int k = 0; k < nbi && k < 0x1f; k++) sm.bone_idx.push_back(r.b[r.p + k]);
      r.p += 0x1f;
      int32_t vc = r.i32(), ic = r.i32(), vl = r.i32(), il = r.i32();
      sizes.push_back({vc, ic, vl, il});
      me.subs.push_back(std::move(sm));
    }
    for (size_t si = 0; si < me.subs.size(); si++) {
      GfSub& sm = me.subs[si];
      if (si * 3 + 2 >= cmds.size()) throw Fail("model command lists missing");
      const auto& en = cmds[si * 3];
      const auto& idx = cmds[si * 3 + 2];
      uint64_t fmt = 0, perm = 0, battr = 0;
      int stride = 0;
      uint32_t ntot = 0, fidx = 0;
      std::map<uint32_t, std::array<uint32_t, 3>> fixed;
      for (auto& c : pica_cmds(en)) {
        uint32_t p = c.ps[0];
        switch (c.reg) {
          case 0x201: fmt |= p; break;
          case 0x202: fmt |= (uint64_t)p << 32; break;
          case 0x204: battr |= p; break;
          case 0x205: battr |= (uint64_t)(p & 0xffff) << 32; stride = (p >> 16) & 0xff; break;
          case 0x232: fidx = p; break;
          case 0x233: fixed.emplace(fidx, std::array<uint32_t, 3>{0, 0, 0}).first->second[0] = p; break;
          case 0x234: fixed.emplace(fidx, std::array<uint32_t, 3>{0, 0, 0}).first->second[1] = p; break;
          case 0x235: fixed.emplace(fidx, std::array<uint32_t, 3>{0, 0, 0}).first->second[2] = p; break;
          case 0x242: ntot = p + 1; break;
          case 0x2bb: perm |= p; break;
          case 0x2bc: perm |= (uint64_t)p << 32; break;
          default: break;
        }
      }
      for (uint32_t k = 0; k < ntot; k++) {
        if ((fmt >> (48 + k)) & 1) {
          std::string nm = AN((int)((perm >> (k * 4)) & 15));
          std::array<uint32_t, 3> fz = {0, 0, 0};
          auto it = fixed.find(k);
          if (it != fixed.end()) fz = it->second;
          auto v = vec_f24(fz[0], fz[1], fz[2]);
          if (nm == "col" || nm == "bw")
            for (auto& x : v) x = x / 255.0;
          sm.fixed.emplace_back(nm, v);
        } else {
          uint32_t pi = (uint32_t)((battr >> (k * 4)) & 15);
          GfAttr a;
          a.name = AN((int)((perm >> (pi * 4)) & 15));
          int f = (int)((fmt >> (pi * 4)) & 15);
          a.fmt = f & 3;
          a.n = (f >> 2) + 1;
          a.scale = a.name == "bidx" ? 1.0 : SCALES[f & 3];
          sm.attrs.push_back(a);
        }
      }
      uint32_t bufaddr = 0, cnt = 0, prim = 0;
      for (auto& c : pica_cmds(idx)) {
        if (c.reg == 0x227) bufaddr = c.ps[0];
        else if (c.reg == 0x228) cnt = c.ps[0];
        else if (c.reg == 0x25e) prim = c.ps[0] >> 8;
      }
      int32_t vl = sizes[si][2], il = sizes[si][3];
      (void)il;
      size_t rawn = (size_t)vl;
      if (r.p > b.size()) rawn = 0;
      else if (r.p + rawn > b.size()) rawn = b.size() - r.p;
      sm.raw.assign(b.begin() + std::min(r.p, b.size()), b.begin() + std::min(r.p, b.size()) + rawn);
      r.p += (size_t)vl;
      if (bufaddr >> 31) {
        if (r.p + (size_t)cnt * 2 > b.size()) throw Fail("index buffer past the end of the model");
        for (uint32_t k = 0; k < cnt; k++) sm.indices.push_back(b[r.p + k * 2] | b[r.p + k * 2 + 1] << 8);
      } else {
        if (r.p + cnt > b.size()) throw Fail("index buffer past the end of the model");
        for (uint32_t k = 0; k < cnt; k++) sm.indices.push_back(b[r.p + k]);
      }
      r.p += (size_t)sizes[si][3];
      sm.stride = stride;
      sm.prim = (int)prim;
      sm.verts = decode_verts(sm);
    }
    r.p = start + ln;
    gm.meshes.push_back(std::move(me));
  }
  return gm;
}

GfVerts decode_verts(const GfSub& sm) {
  int stride = sm.stride;
  const Bytes& raw = sm.raw;
  int n = stride ? (int)(raw.size() / stride) : 0;
  GfVerts out;
  size_t off = 0;
  static const int sz_of[4] = {1, 1, 2, 4};
  std::map<std::string, std::vector<Arr>> cols;
  for (auto& a : sm.attrs) {
    int sz = sz_of[a.fmt];
    if (off % sz) off += sz - off % sz;
    Arr arr;
    arr.rows = n;
    arr.cols = a.n;
    arr.v.assign((size_t)n * a.n, 0.0);
    for (int e = 0; e < a.n; e++) {
      for (int i = 0; i < n; i++) {
        size_t at = off + (size_t)e * sz + (size_t)i * stride;
        if (at + sz > raw.size()) throw Fail("vertex data past its end");
        double v;
        switch (a.fmt) {
          case 0: v = (double)(int8_t)raw[at]; break;
          case 1: v = (double)raw[at]; break;
          case 2: v = (double)(int16_t)(raw[at] | raw[at + 1] << 8); break;
          default: { uint32_t u = raw[at] | raw[at + 1] << 8 | raw[at + 2] << 16 | (uint32_t)raw[at + 3] << 24; float f; memcpy(&f, &u, 4); v = (double)f; }
        }
        arr.at(i, e) = v;
      }
    }
    off += (size_t)a.n * sz;
    for (auto& x : arr.v) x *= a.scale;
    if (a.name == "bidx" || a.name == "bw") cols[a.name].push_back(std::move(arr));
    else out.a[a.name] = std::move(arr);
  }
  for (auto& kv : cols) {
    Arr cat;
    cat.rows = n;
    for (auto& p : kv.second) cat.cols += p.cols;
    cat.v.assign((size_t)n * cat.cols, 0.0);
    int c0 = 0;
    for (auto& p : kv.second) {
      for (int i = 0; i < n; i++)
        for (int c = 0; c < p.cols; c++) cat.at(i, c0 + c) = p.at(i, c);
      c0 += p.cols;
    }
    out.a[kv.first] = std::move(cat);
  }
  for (auto& f : sm.fixed) {
    const std::string& nm = f.first;
    if ((nm == "bidx" || nm == "bw") && !out.a.count(nm)) {
      Arr t;
      t.rows = n;
      t.cols = 3;
      t.v.resize((size_t)n * 3);
      for (int i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) t.at(i, c) = f.second[c];
      out.a[nm] = std::move(t);
    } else if (!out.a.count(nm)) {
      Arr t;
      t.rows = 1;
      t.cols = 4;
      t.v.assign(f.second.begin(), f.second.end());
      out.a["fixed_" + nm] = std::move(t);
    }
  }
  out.n = n;
  return out;
}

std::vector<std::array<int64_t, 3>> triangles(const GfSub& sm) {
  const auto& ix = sm.indices;
  std::vector<std::array<int64_t, 3>> t;
  if (sm.prim == 0) {
    for (size_t i = 0; i + 3 <= ix.size(); i += 3) t.push_back({ix[i], ix[i + 1], ix[i + 2]});
  } else if (sm.prim == 1) {
    for (size_t i = 0; i + 2 < ix.size(); i++) {
      int64_t a = ix[i], b = ix[i + 1], c = ix[i + 2];
      if (a == b || b == c || a == c) continue;
      if (i % 2 == 0) t.push_back({a, b, c});
      else t.push_back({b, a, c});
    }
  } else if (sm.prim == 2) {
    for (size_t i = 1; i + 1 < ix.size(); i++) t.push_back({ix[0], ix[i], ix[i + 1]});
  }
  return t;
}

GfTexture parse_texture(const Bytes& b) {
  R r(b);
  r.u32();
  r.u32();
  r.section();
  uint32_t ln = r.u32();
  r.p += 12;
  GfTexture t;
  t.name = r.pstr(0x40);
  t.w = r.u16();
  t.h = r.u16();
  t.fmt = r.u16();
  t.mip = r.u16();
  r.p += 0x10;
  size_t e = std::min(b.size(), r.p + (size_t)ln);
  size_t s = std::min(b.size(), r.p);
  t.data.assign(b.begin() + s, b.begin() + e);
  return t;
}

namespace {

const int ETC_MOD[8][4] = {{2, 8, -2, -8}, {5, 17, -5, -17}, {9, 29, -9, -29}, {13, 42, -13, -42},
                           {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}};

void etc1_block(const uint8_t* d, uint8_t out[4][4][3]) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | d[i];
  uint32_t lo = (uint32_t)(v & 0xffffffffu), hi = (uint32_t)(v >> 32);
  int b0 = (hi >> 24) & 255, b1 = (hi >> 16) & 255, b2 = (hi >> 8) & 255, b3 = hi & 255;
  int diff = (b3 >> 1) & 1, flip = b3 & 1, t1 = (b3 >> 5) & 7, t2 = (b3 >> 2) & 7;
  int c1[3], c2[3];
  if (diff) {
    int r1 = b0 >> 3, g1 = b1 >> 3, bl1 = b2 >> 3;
    int r2 = r1 + (((b0 & 7) ^ 4) - 4), g2 = g1 + (((b1 & 7) ^ 4) - 4), bl2 = bl1 + (((b2 & 7) ^ 4) - 4);
    int a1[3] = {r1, g1, bl1}, a2[3] = {r2, g2, bl2};
    for (int i = 0; i < 3; i++) {
      c1[i] = (a1[i] << 3) | (a1[i] >> 2);
      c2[i] = ((a2[i] & 31) << 3) | ((a2[i] & 31) >> 2);
    }
  } else {
    c1[0] = (b0 >> 4) * 17; c1[1] = (b1 >> 4) * 17; c1[2] = (b2 >> 4) * 17;
    c2[0] = (b0 & 15) * 17; c2[1] = (b1 & 15) * 17; c2[2] = (b2 & 15) * 17;
  }
  uint32_t msb = (lo >> 16) & 0xffff, lsb = lo & 0xffff;
  for (int x = 0; x < 4; x++)
    for (int y = 0; y < 4; y++) {
      int k = x * 4 + y;
      int idx = (((msb >> k) & 1) << 1) | ((lsb >> k) & 1);
      int sub = flip == 0 ? (x < 2 ? 0 : 1) : (y < 2 ? 0 : 1);
      const int* base = sub == 0 ? c1 : c2;
      int mod = ETC_MOD[sub == 0 ? t1 : t2][idx];
      for (int c = 0; c < 3; c++) out[y][x][c] = (uint8_t)std::max(0, std::min(255, base[c] + mod));
    }
}

}

Rgba decode_texture(const GfTexture& t) {
  int w = t.w, h = t.h, fmt = t.fmt;
  const Bytes& d = t.data;
  std::vector<uint8_t> img((size_t)w * h * 4, 0);
  for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
  auto put = [&](int x, int y, int c, int v) { img[((size_t)y * w + x) * 4 + c] = (uint8_t)v; };
  auto flip_out = [&]() {
    Rgba o;
    o.w = w;
    o.h = h;
    o.px.resize(img.size());
    for (int y = 0; y < h; y++) memcpy(&o.px[(size_t)y * w * 4], &img[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    return o;
  };
  if (fmt == 0x2a || fmt == 0x2b) {
    bool has_a = fmt == 0x2b;
    int bs = has_a ? 16 : 8;
    int nbx = w / 4, nby = h / 4;
    size_t n_ = 0;
    for (int ty = 0; ty < nby; ty += 2)
      for (int tx = 0; tx < nbx; tx += 2)
        for (int q = 0; q < 4; q++) {
          int bx = tx + (q & 1), by = ty + (q >> 1);
          size_t o = n_++ * bs;
          if (o + bs > d.size()) return flip_out();
          if (bx >= nbx || by >= nby) throw Fail("ETC1 picture size is not a multiple of 8");
          uint8_t blk[4][4][3];
          etc1_block(&d[o + (has_a ? 8 : 0)], blk);
          for (int yy = 0; yy < 4; yy++)
            for (int xx = 0; xx < 4; xx++)
              for (int c = 0; c < 3; c++) put(bx * 4 + xx, by * 4 + yy, c, blk[yy][xx][c]);
          if (has_a) {
            uint64_t av = 0;
            for (int i = 7; i >= 0; i--) av = av << 8 | d[o + i];
            for (int k = 0; k < 16; k++) put(bx * 4 + k / 4, by * 4 + (k % 4), 3, (int)((av >> (4 * k)) & 15) * 17);
          }
        }
    return flip_out();
  }
  double bpp;
  switch (fmt) {
    case 0x2: case 0x16: case 0x17: case 0x23: case 0x24: bpp = 2; break;
    case 0x3: bpp = 3; break;
    case 0x4: bpp = 4; break;
    case 0x25: case 0x26: case 0x27: bpp = 1; break;
    case 0x28: case 0x29: bpp = 0.5; break;
    default: throw Fail("unknown 3DS picture format");
  }
  size_t n = (size_t)w * h;
  size_t need = bpp == 0.5 ? n / 2 : (size_t)(n * bpp);
  if (d.size() < need) throw Fail("picture data too short");
  size_t i = 0;
  for (int ty = 0; ty < h; ty += 8)
    for (int tx = 0; tx < w; tx += 8)
      for (int k = 0; k < 64; k++, i++) {
        int x = tx + ((k & 1) | ((k >> 2) & 1) << 1 | ((k >> 4) & 1) << 2);
        int y = ty + (((k >> 1) & 1) | ((k >> 3) & 1) << 1 | ((k >> 5) & 1) << 2);
        int a[4] = {0, 0, 0, 0};
        if (bpp == 0.5) {
          uint8_t byte = d[i / 2];
          a[0] = ((i & 1) ? (byte >> 4) : (byte & 15)) * 17;
        } else {
          int nb = (int)bpp;
          for (int c = 0; c < nb; c++) a[c] = d[i * nb + c];
        }
        int px[4] = {0, 0, 0, 255};
        switch (fmt) {
          case 0x4: px[0] = a[3]; px[1] = a[2]; px[2] = a[1]; px[3] = a[0]; break;
          case 0x3: px[0] = a[2]; px[1] = a[1]; px[2] = a[0]; break;
          case 0x2: case 0x16: case 0x17: {
            int v = a[0] | a[1] << 8;
            if (fmt == 0x2) { px[0] = ((v >> 11) & 31) * 255 / 31; px[1] = ((v >> 5) & 63) * 255 / 63; px[2] = (v & 31) * 255 / 31; }
            else if (fmt == 0x16) { px[0] = ((v >> 12) & 15) * 17; px[1] = ((v >> 8) & 15) * 17; px[2] = ((v >> 4) & 15) * 17; px[3] = (v & 15) * 17; }
            else { px[0] = ((v >> 11) & 31) * 255 / 31; px[1] = ((v >> 6) & 31) * 255 / 31; px[2] = ((v >> 1) & 31) * 255 / 31; px[3] = (v & 1) * 255; }
            break;
          }
          case 0x23: px[0] = px[1] = px[2] = a[1]; px[3] = a[0]; break;
          case 0x27: px[0] = px[1] = px[2] = (a[0] >> 4) * 17; px[3] = (a[0] & 15) * 17; break;
          case 0x28: px[0] = px[1] = px[2] = a[0]; break;
          case 0x29: px[0] = px[1] = px[2] = 255; px[3] = a[0]; break;
          case 0x25: px[0] = px[1] = px[2] = a[0]; break;
          case 0x26: px[0] = px[1] = px[2] = 255; px[3] = a[0]; break;
          case 0x24: px[0] = a[1]; px[1] = a[0]; px[2] = 0; break;
        }
        for (int c = 0; c < 4; c++) put(x, y, c, px[c]);
      }
  return flip_out();
}

namespace {

GfChannel read_keys(R& r, uint32_t flags, int frames) {
  uint32_t f = flags & 7;
  GfChannel out;
  if (f == 3) {
    out.push_back({0.0, r.f32(), 0.0});
    return out;
  }
  if (f == 4 || f == 5) {
    uint32_t n = r.u32();
    std::vector<int> fr;
    for (uint32_t i = 0; i < n; i++) fr.push_back(frames > 0xff ? r.u16() : r.u8());
    while (r.p & 3) r.p++;
    if (flags & 1) {
      for (uint32_t i = 0; i < n; i++) {
        double v = r.f32(), s = r.f32();
        out.push_back({(double)fr[i], v, s});
      }
    } else {
      double vs = r.f32(), vo = r.f32(), ss = r.f32(), so = r.f32();
      for (uint32_t i = 0; i < n; i++) {
        double v = r.u16() / 65535.0 * vs + vo;
        double s = r.u16() / 65535.0 * ss + so;
        out.push_back({(double)fr[i], v, s});
      }
    }
  }
  return out;
}

}

bool parse_motion(const Bytes& b, GfMotion& mo) {
  if (b.size() < 16) return false;
  R r(b);
  r.u32();
  uint32_t nsec = r.u32();
  if (nsec > 16 || 8 + (size_t)nsec * 12 > b.size()) return false;
  struct Sec { uint32_t kind, ln, addr; };
  std::vector<Sec> secs;
  for (uint32_t i = 0; i < nsec; i++) {
    Sec s;
    s.kind = r.u32();
    s.ln = r.u32();
    s.addr = r.u32();
    secs.push_back(s);
  }
  r.p = secs.at(0).addr;
  mo = GfMotion();
  mo.frames = (int)r.u32();
  mo.loop = r.u16() & 1;
  r.u16();
  r.fl<3>();
  r.fl<3>();
  r.u32();
  int frames = mo.frames;
  for (size_t si = 1; si < secs.size(); si++) {
    if (secs[si].kind != 1) continue;
    r.p = secs[si].addr;
    int32_t nn = r.i32();
    uint32_t nl = r.u32();
    size_t st = r.p;
    std::vector<std::string> names;
    for (int32_t i = 0; i < nn; i++) names.push_back(r.bstr());
    r.p = st + nl;
    for (auto& nm : names) {
      uint32_t fl = r.u32();
      r.u32();
      GfMotionBone mb;
      mb.name = nm;
      mb.axis = (fl >> 31) == 0;
      uint32_t f = fl;
      for (int e = 0; e < 9; e++) {
        mb.ch[e] = read_keys(r, f, frames);
        f >>= 3;
      }
      bool done = false;
      for (auto& ex : mo.bones)
        if (ex.name == nm) { ex = mb; done = true; break; }
      if (!done) mo.bones.push_back(std::move(mb));
    }
  }
  for (size_t si = 1; si < secs.size(); si++) {
    if (secs[si].kind == 6) {
      r.p = secs[si].addr;
      int32_t nn = r.i32();
      uint32_t nl = r.u32();
      size_t st = r.p;
      std::vector<std::string> names;
      for (int32_t i = 0; i < nn; i++) names.push_back(r.bstr());
      r.p = st + nl;
      for (auto& nm : names) {
        std::vector<bool> vals;
        int byte = 0;
        for (int i = 0; i <= frames; i++) {
          if ((i & 7) == 0) byte = r.u8();
          vals.push_back((byte >> (i & 7)) & 1);
        }
        bool done = false;
        for (auto& ex : mo.vis)
          if (ex.first == nm) { ex.second = vals; done = true; break; }
        if (!done) mo.vis.emplace_back(nm, vals);
      }
    } else if (secs[si].kind == 3) {
      r.p = secs[si].addr;
      int32_t nn = r.i32();
      uint32_t nl = r.u32();
      std::vector<uint32_t> units;
      for (int32_t i = 0; i < nn; i++) units.push_back(r.u32());
      size_t st = r.p;
      std::vector<std::string> names;
      for (int32_t i = 0; i < nn; i++) names.push_back(r.bstr());
      r.p = st + nl;
      for (size_t k = 0; k < names.size(); k++)
        for (uint32_t u = 0; u < units[k]; u++) {
          GfMatChan mc;
          mc.name = names[k];
          mc.unit = (int)r.u32();
          uint32_t fl = r.u32();
          r.u32();
          GfChannel ch[5];
          for (int e = 0; e < 5; e++) {
            ch[e] = read_keys(r, fl, frames);
            fl >>= 3;
          }
          mc.sx = ch[0]; mc.sy = ch[1]; mc.rot = ch[2]; mc.tx = ch[3]; mc.ty = ch[4];
          mo.mats.push_back(std::move(mc));
        }
    }
  }
  return true;
}

bool herp(const GfChannel& k, double frame, double* out) {
  if (k.empty()) return false;
  size_t n = k.size();
  if (n == 1 || frame <= k[0].frame) { *out = k[0].value; return true; }
  if (frame >= k[n - 1].frame) { *out = k[n - 1].value; return true; }
  size_t lo = 0;
  for (size_t i = 0; i + 1 < n; i++)
    if (k[i].frame <= frame) lo = i;
  const GfKey &a = k[lo], &b = k[lo + 1];
  double d = b.frame - a.frame;
  if (d == 0) { *out = a.value; return true; }
  double t = (frame - a.frame) / d;
  double t2 = t * t, t3 = t * t * t;
  *out = ((2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * a.slope * d + (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * b.slope * d);
  return true;
}

}
