#include "pbr_hsd_write.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

namespace pbr {

namespace {

enum : uint32_t {
  JOBJ_SKELETON = 1, JOBJ_SKELETON_ROOT = 2, JOBJ_ENVELOPE_MODEL = 4, JOBJ_HIDDEN = 0x10, JOBJ_LIGHTING = 0x80,
  JOBJ_OPA = 1u << 18, JOBJ_XLU = 1u << 19, JOBJ_TEXEDGE = 1u << 20,
  JOBJ_ROOT_OPA = 1u << 28, JOBJ_ROOT_XLU = 1u << 29, JOBJ_ROOT_TEXEDGE = 1u << 30
};
const int MAX_ENVELOPES = 10;

[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }

void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  if (o + 4 > b.size()) fail("pack out of range");
  b[o] = (uint8_t)(v >> 24); b[o + 1] = (uint8_t)(v >> 16); b[o + 2] = (uint8_t)(v >> 8); b[o + 3] = (uint8_t)v;
}
void put16(std::vector<uint8_t>& b, size_t o, long long v) {
  if (v < 0 || v > 0xFFFF) fail("unsigned short format requires 0 <= number <= 65535");
  if (o + 2 > b.size()) fail("pack out of range");
  b[o] = (uint8_t)(v >> 8); b[o + 1] = (uint8_t)v;
}
void put8(std::vector<uint8_t>& b, size_t o, long long v) {
  if (v < 0 || v > 255) fail("ubyte format requires 0 <= number <= 255");
  if (o >= b.size()) fail("pack out of range");
  b[o] = (uint8_t)v;
}
void putf(std::vector<uint8_t>& b, size_t o, double v) {
  float f = (float)v;
  if (std::isinf(f) && !std::isinf(v)) fail("float too large to pack with f format");
  uint32_t u;
  memcpy(&u, &f, 4);
  put32(b, o, u);
}
void app_f(std::vector<uint8_t>& b, double v) {
  size_t o = b.size();
  b.resize(o + 4);
  putf(b, o, v);
}

struct Blob {
  std::vector<uint8_t> b;
  int align = 4;
  struct P { size_t off; Blob* target; long long add; };
  std::vector<P> ptrs;
  long long at = -1;
  void ptr(size_t off, Blob* target, long long add = 0) {
    if (target) ptrs.push_back({off, target, add});
  }
};

struct Builder {
  std::deque<Blob> blobs;
  Blob* add(std::vector<uint8_t> data = {}, int align = 4) {
    blobs.emplace_back();
    Blob* b = &blobs.back();
    b->b = std::move(data);
    b->align = align;
    return b;
  }
  Blob* make(size_t size, int align = 4) { return add(std::vector<uint8_t>(size, 0), align); }

  std::vector<uint8_t> finish(const std::vector<std::tuple<std::string, Blob*, long long>>& publics) {
    long long at = 0;
    for (auto& b : blobs) {
      at = (at + b.align - 1) & ~(long long)(b.align - 1);
      b.at = at;
      at += (long long)b.b.size();
    }
    long long size = (at + 3) & ~3LL;
    std::vector<uint8_t> data((size_t)size, 0);
    std::vector<uint32_t> relocs;
    for (auto& b : blobs) {
      for (auto& p : b.ptrs) {
        put32(b.b, p.off, (uint32_t)(p.target->at + p.add));
        relocs.push_back((uint32_t)(b.at + p.off));
      }
      memcpy(&data[(size_t)b.at], b.b.data(), b.b.size());
    }
    std::sort(relocs.begin(), relocs.end());
    std::vector<uint8_t> strings, pub;
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
      v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
    };
    for (auto& p : publics) {
      be32(pub, (uint32_t)(std::get<1>(p)->at + std::get<2>(p)));
      be32(pub, (uint32_t)strings.size());
      for (char c : std::get<0>(p)) strings.push_back((uint8_t)c);
      strings.push_back(0);
    }
    std::vector<uint8_t> body = data;
    for (uint32_t r : relocs) be32(body, r);
    body.insert(body.end(), pub.begin(), pub.end());
    body.insert(body.end(), strings.begin(), strings.end());
    uint32_t total = (uint32_t)(0x20 + body.size());
    std::vector<uint8_t> head;
    be32(head, total); be32(head, (uint32_t)size); be32(head, (uint32_t)relocs.size()); be32(head, (uint32_t)publics.size()); be32(head, 0);
    head.resize(0x20, 0);
    head.insert(head.end(), body.begin(), body.end());
    return head;
  }
};

void pack_num(std::vector<uint8_t>& out, double v, int frac) {
  int kind = frac & 0xE0;
  auto le = [&](uint64_t x, int n) { for (int i = 0; i < n; i++) out.push_back((uint8_t)(x >> (8 * i))); };
  if (kind == 0) {
    float f = (float)v;
    if (std::isinf(f) && !std::isinf(v)) fail("float too large to pack with f format");
    uint32_t u;
    memcpy(&u, &f, 4);
    le(u, 4);
    return;
  }
  double scaled = v * (double)(1LL << (frac & 0x1F));
  long long q = (long long)std::nearbyint(scaled);
  if (kind == 0x20) le((uint16_t)std::max(-32768LL, std::min(32767LL, q)), 2);
  else if (kind == 0x40) le((uint16_t)std::max(0LL, std::min(65535LL, q)), 2);
  else if (kind == 0x60) le((uint8_t)std::max(-128LL, std::min(127LL, q)), 1);
  else le((uint8_t)std::max(0LL, std::min(255LL, q)), 1);
}

int choose_format(const std::vector<double>& values, double tolerance) {
  if (values.empty()) return 0x20;
  double top = std::fabs(values[0]);
  for (double v : values) top = std::max(top, std::fabs(v));
  if (top == 0) return 0x60;
  for (int frac = 15; frac >= 0; frac--) {
    if (top * (double)(1 << frac) <= 32767) {
      if (0.5 / (double)(1 << frac) <= tolerance) return 0x20 | frac;
      break;
    }
  }
  return 0;
}

std::vector<uint8_t> write_fobj_keys(const std::vector<Key>& keys, int frac_value, int frac_slope) {
  std::vector<uint8_t> out;
  size_t i = 0, n = keys.size();
  while (i < n) {
    int op = keys[i].op;
    size_t j = i;
    while (j < n && keys[j].op == op) j++;
    long long count = (long long)(j - i) - 1;
    int d = op | ((int)(count & 7) << 4);
    count >>= 3;
    if (count) d |= 0x80;
    out.push_back((uint8_t)d);
    while (count) {
      d = (int)(count & 0x7F);
      count >>= 7;
      if (count) d |= 0x80;
      out.push_back((uint8_t)d);
    }
    for (size_t k = i; k < j; k++) {
      const Key& key = keys[k];
      bool hasv = op == OP_CON || op == OP_LIN || op == OP_SPL0 || op == OP_SPL || op == OP_KEY;
      if (hasv) pack_num(out, key.value, frac_value);
      if (op == OP_SPL || op == OP_SLP) pack_num(out, key.slope, frac_slope);
      if (hasv) {
        long long w = key.wait;
        if (k == n - 1 && w == 0) break;
        if (w < 0) fail("negative wait");
        while (true) {
          int dd = (int)(w & 0x7F);
          w >>= 7;
          if (w) dd |= 0x80;
          out.push_back((uint8_t)dd);
          if (!w) break;
        }
      }
    }
    i = j;
  }
  return out;
}

}

std::vector<uint32_t> hsd_joint_flags(const Model& model) {
  size_t n = model.joints.size();
  std::vector<uint32_t> flags(n, 0);
  std::set<int> bound;
  for (auto& me : model.meshes)
    if (!me.rigid)
      for (auto& v : me.verts)
        if (v.weights)
          for (auto& jw : *v.weights) bound.insert(jw.first);
  for (size_t i = 0; i < n; i++) {
    const Joint& j = model.joints[i];
    if ((int)i == model.skeleton_root) flags[i] |= JOBJ_SKELETON_ROOT;
    else if (bound.count((int)i) || (j.inv_bind && !j.inv_bind->empty())) flags[i] |= JOBJ_SKELETON;
    if (j.hidden) flags[i] |= JOBJ_HIDDEN;
    flags[i] |= 0x8;
    flags[i] |= (uint32_t)j.billboard;
  }
  for (auto& me : model.meshes) {
    uint32_t f = JOBJ_OPA | JOBJ_LIGHTING;
    if (!me.rigid) f |= JOBJ_ENVELOPE_MODEL;
    std::string mode = me.material ? model.materials.at(*me.material).draw_class : "opa";
    if (mode == "xlu") f = (f & ~JOBJ_OPA) | JOBJ_XLU | JOBJ_TEXEDGE;
    else if (mode == "texedge") f = (f & ~JOBJ_OPA) | JOBJ_TEXEDGE;
    flags.at(me.joint) |= f;
  }
  for (int i = (int)n - 1; i >= 0; i--) {
    uint32_t up = 0;
    if (flags[i] & (JOBJ_OPA | JOBJ_ROOT_OPA)) up |= JOBJ_ROOT_OPA;
    if (flags[i] & (JOBJ_XLU | JOBJ_ROOT_XLU)) up |= JOBJ_ROOT_XLU;
    if (flags[i] & (JOBJ_TEXEDGE | JOBJ_ROOT_TEXEDGE)) up |= JOBJ_ROOT_TEXEDGE;
    int p = model.joints[i].parent;
    if (p >= 0) flags[p] |= up;
  }
  return flags;
}

namespace {

struct Piece {
  std::vector<int> envs;
  std::vector<std::array<int, 3>> tris;
};

std::vector<Piece> cut_into_pieces(const Mesh& me, const std::vector<int>& vid, const std::vector<const Weights*>& combos) {
  auto less_id = [&](int a, int b) { return *combos[a] < *combos[b]; };
  std::vector<std::array<int, 3>> left = me.tris;
  std::vector<Piece> pieces;
  while (!left.empty()) {
    Piece pc;
    std::set<int> have;
    bool progress = true;
    while (progress && !left.empty()) {
      progress = false;
      std::vector<std::array<int, 3>> rest;
      const std::array<int, 3>* best = nullptr;
      std::vector<int> best_new;
      bool have_best = false;
      for (auto& t : left) {
        std::set<int> need;
        for (int k = 0; k < 3; k++) need.insert(vid.at(t[k]));
        std::vector<int> nw;
        for (int e : need)
          if (!have.count(e)) nw.push_back(e);
        if (nw.empty()) {
          pc.tris.push_back(t);
          progress = true;
          continue;
        }
        rest.push_back(t);
        if ((int)have.size() + (int)nw.size() <= MAX_ENVELOPES && (!have_best || nw.size() < best_new.size())) {
          best = &t;
          have_best = true;
          best_new = nw;
        }
      }
      (void)best;
      left = rest;
      if (have_best) {
        std::sort(best_new.begin(), best_new.end(), less_id);
        for (int e : best_new) {
          have.insert(e);
          pc.envs.push_back(e);
        }
        progress = true;
      }
    }
    if (pc.tris.empty()) fail("a triangle needs more than 10 weight combinations");
    pieces.push_back(std::move(pc));
  }
  return pieces;
}

struct F3 { float v[3]; bool operator<(const F3& o) const { return std::lexicographical_compare(v, v + 3, o.v, o.v + 3); } };
struct F2 { float v[2]; bool operator<(const F2& o) const { return std::lexicographical_compare(v, v + 2, o.v, o.v + 2); } };
struct C4 { std::vector<int> v; bool operator<(const C4& o) const { return v < o.v; } };

class Writer {
 public:
  explicit Writer(const Model& m) : m_(m) { b_.make(32, 32); }
  std::vector<uint8_t> write();
  WriteReport report;

 private:
  const Model& m_;
  Builder b_;
  std::map<int, Blob*> images_, tluts_, materials_;
  std::vector<Mat> world_;
  std::vector<Blob*> joint_blob_;
  size_t child_off_ = 8, next_off_ = 12;

  Blob* floats(const std::vector<double>& values, int align = 4) {
    std::vector<uint8_t> d;
    for (double v : values) app_f(d, v);
    return b_.add(std::move(d), align);
  }
  Blob* image_blob(int i) {
    auto it = images_.find(i);
    if (it != images_.end()) return it->second;
    const Image& im = m_.images.at(i);
    Blob* data = b_.add(im.data, 32);
    report.texture_bytes += (long long)im.data.size();
    Blob* d = b_.make(0x18);
    d->ptr(0, data);
    put16(d->b, 4, im.width); put16(d->b, 6, im.height);
    put32(d->b, 8, (uint32_t)im.format); put32(d->b, 12, (uint32_t)im.mipmap);
    putf(d->b, 16, im.min_lod); putf(d->b, 20, im.max_lod);
    images_[i] = d;
    return d;
  }
  Blob* tlut_blob(int i) {
    auto it = tluts_.find(i);
    if (it != tluts_.end()) return it->second;
    const Tlut& t = m_.tluts.at(i);
    Blob* data = b_.add(t.data, 32);
    Blob* d = b_.make(0x10);
    d->ptr(0, data);
    put32(d->b, 4, t.format); put32(d->b, 8, t.name); put16(d->b, 12, t.count);
    tluts_[i] = d;
    return d;
  }
  Blob* material_blob(int i);
  Blob* mesh_blobs(const Mesh& me);
  Blob* aobj_blob(const AnimObj* a, double (*tol)(int));
  std::vector<Blob*> tree(const std::function<Blob*(int)>& make);
};

Blob* Writer::material_blob(int i) {
  auto it = materials_.find(i);
  if (it != materials_.end()) return it->second;
  const Material& ma = m_.materials.at(i);
  std::vector<Blob*> tobjs;
  for (const Texture& t : ma.textures) {
    Blob* o = b_.make(0x5C);
    put32(o->b, 8, t.tex_id); put32(o->b, 12, t.src);
    double v9[9] = {t.rot[0], t.rot[1], t.rot[2], t.scale[0], t.scale[1], t.scale[2], t.trans[0], t.trans[1], t.trans[2]};
    for (int k = 0; k < 9; k++) putf(o->b, 0x10 + 4 * k, v9[k]);
    put32(o->b, 0x34, t.wrap_s); put32(o->b, 0x38, t.wrap_t);
    put8(o->b, 0x3C, t.repeat_s); put8(o->b, 0x3D, t.repeat_t);
    put32(o->b, 0x40, t.flags); putf(o->b, 0x44, t.blending); put32(o->b, 0x48, t.mag_filter);
    if (t.image) o->ptr(0x4C, image_blob(*t.image));
    if (t.tlut) o->ptr(0x50, tlut_blob(*t.tlut));
    if (t.lod && !t.lod->empty()) o->ptr(0x54, b_.add(*t.lod));
    if (t.tev && !t.tev->empty()) o->ptr(0x58, b_.add(*t.tev));
    tobjs.push_back(o);
  }
  for (size_t k = 0; k + 1 < tobjs.size(); k++) tobjs[k]->ptr(4, tobjs[k + 1]);
  std::vector<uint8_t> cd;
  for (const std::vector<double>* c : {&ma.ambient, &ma.diffuse, &ma.specular})
    for (double x : *c) {
      if (x < 0 || x > 255) fail("bytes must be in range(0, 256)");
      cd.push_back((uint8_t)x);
    }
  app_f(cd, ma.alpha);
  app_f(cd, ma.shininess);
  Blob* col = b_.add(std::move(cd));
  Blob* o = b_.make(0x18);
  put32(o->b, 4, ma.rendermode);
  if (!tobjs.empty()) o->ptr(8, tobjs[0]);
  o->ptr(12, col);
  if (ma.pe && !ma.pe->empty()) o->ptr(20, b_.add(*ma.pe));
  materials_[i] = o;
  return o;
}

Blob* Writer::mesh_blobs(const Mesh& me) {
  const Model& m = m_;
  bool single_local = false;
  std::optional<Mat> conv;
  if (!me.rigid) {
    if (me.joint == m.skeleton_root) {
      single_local = true;
    } else {
      Mat c = mat_mul(mat_inv(world_.at(m.skeleton_root)), world_.at(me.joint));
      conv = mat_inv(c);
    }
  }
  bool has_nrm = false, has_uv = false, has_col = false;
  for (auto& v : me.verts) {
    has_nrm |= v.nrm.has_value();
    has_uv |= v.uv.has_value();
    has_col |= v.color.has_value();
  }
  size_t n_uvx = 0;
  if (has_uv)
    for (auto& v : me.verts)
      if (v.has_uvx && !v.uvx.empty()) n_uvx = std::max(n_uvx, v.uvx.size());
  std::map<F3, int> pos_i, nrm_i;
  std::map<C4, int> col_i;
  std::map<F2, int> uv_i;
  std::vector<F3> pos_l, nrm_l;
  std::vector<C4> col_l;
  std::vector<F2> uv_l;
  std::vector<std::map<F2, int>> uvx_i(n_uvx);
  std::vector<std::vector<F2>> uvx_l(n_uvx);
  auto idx = [](auto& table, auto& lst, const auto& value) {
    auto it = table.find(value);
    if (it != table.end()) return it->second;
    int i = (int)lst.size();
    table[value] = i;
    lst.push_back(value);
    return i;
  };
  std::vector<std::vector<int>> corner;
  for (const Vertex& v : me.verts) {
    V3 p = v.pos;
    std::optional<V3> n = v.nrm;
    if (!me.rigid) {
      std::optional<Mat> mt = conv;
      if (single_local && v.weights && v.weights->size() == 1) {
        const auto& ib = m.joints.at((*v.weights)[0].first).inv_bind;
        if (ib) { Mat t; for (int k = 0; k < 12; k++) t[k] = (*ib).at(k); mt = t; } else mt.reset();
      }
      if (mt) {
        p = mat_point(*mt, p);
        if (n) {
          V3 nn = mat_vec(*mt, *n);
          double l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
          if (l == 0.0) l = 1.0;
          n = V3{nn[0] / l, nn[1] / l, nn[2] / l};
        }
      }
    }
    F3 pf{{(float)p[0], (float)p[1], (float)p[2]}};
    std::vector<int> c;
    c.push_back(idx(pos_i, pos_l, pf));
    if (has_nrm) {
      V3 nv = n ? *n : V3{0.0, 1.0, 0.0};
      c.push_back(idx(nrm_i, nrm_l, F3{{(float)nv[0], (float)nv[1], (float)nv[2]}}));
    }
    if (has_col) {
      C4 cc;
      cc.v = v.color ? *v.color : std::vector<int>{255, 255, 255, 255};
      c.push_back(idx(col_i, col_l, cc));
    }
    if (has_uv) {
      std::array<double, 2> uv = v.uv ? *v.uv : std::array<double, 2>{0.0, 0.0};
      c.push_back(idx(uv_i, uv_l, F2{{(float)uv[0], (float)uv[1]}}));
      for (size_t k = 0; k < n_uvx; k++) {
        std::array<double, 2> x = uv;
        if (v.has_uvx && !v.uvx.empty() && k < v.uvx.size() && v.uvx[k]) x = *v.uvx[k];
        c.push_back(idx(uvx_i[k], uvx_l[k], F2{{(float)x[0], (float)x[1]}}));
      }
    }
    corner.push_back(std::move(c));
  }
  size_t biggest = std::max({pos_l.size(), nrm_l.size(), uv_l.size()});
  for (auto& l : uvx_l) biggest = std::max(biggest, l.size());
  if (biggest > 65535) fail("mesh too large for 16-bit indices");
  auto flat3 = [](const std::vector<F3>& l) { std::vector<double> o; for (auto& p : l) for (float x : p.v) o.push_back(x); return o; };
  auto flat2 = [](const std::vector<F2>& l) { std::vector<double> o; for (auto& p : l) for (float x : p.v) o.push_back(x); return o; };
  Blob* pos_b = floats(flat3(pos_l), 32);
  Blob* nrm_b = has_nrm ? floats(flat3(nrm_l), 32) : nullptr;
  Blob* col_b = nullptr;
  if (has_col) {
    std::vector<uint8_t> cd;
    for (auto& c : col_l)
      for (int x : c.v) {
        if (x < 0 || x > 255) fail("bytes must be in range(0, 256)");
        cd.push_back((uint8_t)x);
      }
    col_b = b_.add(std::move(cd), 32);
  }
  Blob* uv_b = has_uv ? floats(flat2(uv_l), 32) : nullptr;
  std::vector<Blob*> uvx_b;
  for (auto& l : uvx_l) uvx_b.push_back(floats(flat2(l), 32));

  auto layout = [&](bool skinned) {
    struct Ent { uint32_t attr, typ, cnt, comp; int frac; int stride; Blob* blob; };
    std::vector<Ent> ents;
    if (skinned) ents.push_back({0, 1, 0, 4, 0, 0, nullptr});
    ents.push_back({9, 3, 1, 4, 0, 12, pos_b});
    if (has_nrm) ents.push_back({10, 3, 0, 4, 0, 12, nrm_b});
    if (has_col) ents.push_back({11, 3, 1, 5, 0, 4, col_b});
    if (has_uv) {
      ents.push_back({13, 3, 1, 4, 0, 8, uv_b});
      for (size_t k = 0; k < n_uvx; k++) ents.push_back({(uint32_t)(14 + k), 3, 1, 4, 0, 8, uvx_b[k]});
    }
    Blob* o = b_.make(0x18 * (ents.size() + 1));
    for (size_t k = 0; k < ents.size(); k++) {
      const Ent& e = ents[k];
      size_t at = k * 0x18;
      put32(o->b, at, e.attr); put32(o->b, at + 4, e.typ); put32(o->b, at + 8, e.cnt); put32(o->b, at + 12, e.comp);
      put8(o->b, at + 16, e.frac); put8(o->b, at + 17, 0); put16(o->b, at + 18, e.stride);
      o->ptr(at + 0x14, e.blob);
    }
    put32(o->b, ents.size() * 0x18, 0xFF);
    return o;
  };

  std::vector<int> vid;
  std::vector<const Weights*> combos;
  std::map<Weights, int> combo_id;
  if (!me.rigid) {
    for (auto& v : me.verts) {
      if (!v.weights) fail("skinned vertex without weights");
      auto it = combo_id.find(*v.weights);
      if (it == combo_id.end()) {
        it = combo_id.emplace(*v.weights, (int)combos.size()).first;
        combos.push_back(&it->first);
      }
      vid.push_back(it->second);
    }
  }
  std::vector<Piece> pieces;
  if (me.rigid) {
    Piece p;
    p.tris = me.tris;
    pieces.push_back(std::move(p));
  } else {
    pieces = cut_into_pieces(me, vid, combos);
  }
  Blob* lay = layout(!me.rigid);
  std::vector<Blob*> pobjs;
  for (size_t pi = 0; pi < pieces.size(); pi++) {
    const Piece& pc = pieces[pi];
    bool has_envs = !me.rigid;
    std::map<int, int> slot;
    if (has_envs)
      for (size_t k = 0; k < pc.envs.size(); k++) slot[pc.envs[k]] = (int)k;
    std::vector<uint8_t> dl;
    for (size_t start = 0; start < pc.tris.size(); start += 21845) {
      size_t cnt = std::min<size_t>(21845, pc.tris.size() - start);
      dl.push_back(0x90);
      dl.push_back((uint8_t)((cnt * 3) >> 8));
      dl.push_back((uint8_t)(cnt * 3));
      for (size_t ti = start; ti < start + cnt; ti++)
        for (int k = 0; k < 3; k++) {
          int vi = pc.tris[ti][k];
          if (has_envs) {
            int s = slot.at(vid.at(vi)) * 3;
            if (s > 255) fail("matrix slot too large");
            dl.push_back((uint8_t)s);
          }
          for (int ci : corner.at(vi)) { dl.push_back((uint8_t)(ci >> 8)); dl.push_back((uint8_t)ci); }
        }
    }
    dl.resize(dl.size() + ((32 - dl.size() % 32) % 32), 0);
    report.display_list_bytes += (long long)dl.size();
    size_t dllen = dl.size();
    Blob* dlb = b_.add(std::move(dl), 32);
    Blob* o = b_.make(0x18);
    o->ptr(8, lay);
    long long flags = me.pobj_flags ? *me.pobj_flags : 0x8000;
    flags &= ~0x3000LL;
    if (!me.rigid) flags |= 0x2001;
    put16(o->b, 0xC, flags);
    put16(o->b, 0xE, (long long)(dllen / 32));
    o->ptr(0x10, dlb);
    if (has_envs) {
      report.most_envelopes = std::max<long long>(report.most_envelopes, (long long)pc.envs.size());
      Blob* tab = b_.make(4 * (pc.envs.size() + 1));
      for (size_t k = 0; k < pc.envs.size(); k++) {
        const Weights& e = *combos[pc.envs[k]];
        Blob* eb = b_.make(8 * (e.size() + 1));
        for (size_t q = 0; q < e.size(); q++) {
          eb->ptr(q * 8, joint_blob_.at(e[q].first));
          putf(eb->b, q * 8 + 4, e[q].second);
        }
        tab->ptr(k * 4, eb);
      }
      o->ptr(0x14, tab);
    }
    pobjs.push_back(o);
    report.pobj++;
    if (me.material) {
      const Material& mt = m_.materials.at(*me.material);
      for (const Texture& t : mt.textures)
        if ((t.flags & 0xF) == 1) { report.reflect_pieces++; break; }
    }
  }
  for (size_t k = 0; k + 1 < pobjs.size(); k++) pobjs[k]->ptr(4, pobjs[k + 1]);
  return pobjs.empty() ? nullptr : pobjs[0];
}

Blob* Writer::aobj_blob(const AnimObj* a, double (*tol)(int)) {
  if (!a) return nullptr;
  Blob* o = b_.make(0x10);
  put32(o->b, 0, a->flags);
  putf(o->b, 4, a->end);
  std::vector<Blob*> fobjs;
  for (const Track& t : a->tracks) {
    std::vector<Key> keys = t.keys;
    if (keys.empty()) continue;
    std::vector<const Key*> nonslp;
    for (auto& k : keys)
      if (k.op != OP_SLP) nonslp.push_back(&k);
    if (nonslp.size() < 2) {
      if (nonslp.empty()) fail("list index out of range");
      double v = nonslp[0]->value;
      keys = {Key{OP_CON, v, 0.0, (long long)std::max(a->end, 1.0)}, Key{OP_CON, v, 0.0, 0}};
    }
    std::pair<int, int> fmt;
    if (t.src_format) {
      fmt = *t.src_format;
    } else {
      double tl = tol(t.kind);
      std::vector<double> vals, slopes;
      for (auto& k : keys) {
        if (k.op != OP_SLP) vals.push_back(k.value);
        if (k.op == OP_SPL || k.op == OP_SLP) slopes.push_back(k.slope);
      }
      fmt = {choose_format(vals, tl), choose_format(slopes, tl)};
    }
    std::vector<uint8_t> data = write_fobj_keys(keys, fmt.first, fmt.second);
    report.track_bytes += (long long)data.size();
    size_t dlen = data.size();
    data.resize(dlen + ((4 - dlen % 4) % 4), 0);
    Blob* db = b_.add(std::move(data));
    Blob* f = b_.make(0x14);
    put32(f->b, 4, (uint32_t)dlen);
    putf(f->b, 8, t.start);
    put8(f->b, 12, t.kind); put8(f->b, 13, fmt.first); put8(f->b, 14, fmt.second);
    f->ptr(0x10, db);
    fobjs.push_back(f);
  }
  for (size_t k = 0; k + 1 < fobjs.size(); k++) fobjs[k]->ptr(0, fobjs[k + 1]);
  if (!fobjs.empty()) o->ptr(8, fobjs[0]);
  return o;
}

std::vector<Blob*> Writer::tree(const std::function<Blob*(int)>& make) {
  std::vector<Blob*> nodes;
  for (size_t i = 0; i < m_.joints.size(); i++) nodes.push_back(make((int)i));
  std::map<int, int> last_child;
  for (size_t i = 0; i < m_.joints.size(); i++) {
    int p = m_.joints[i].parent;
    if (p >= 0) {
      auto it = last_child.find(p);
      if (it != last_child.end()) nodes[it->second]->ptr(next_off_, nodes[i]);
      else nodes[p]->ptr(child_off_, nodes[i]);
      last_child[p] = (int)i;
    }
  }
  std::vector<int> roots;
  for (size_t i = 0; i < m_.joints.size(); i++)
    if (m_.joints[i].parent < 0) roots.push_back((int)i);
  for (size_t k = 0; k + 1 < roots.size(); k++) nodes[roots[k]]->ptr(next_off_, nodes[roots[k + 1]]);
  return nodes;
}

double tol_joint(int kind) { return (1 <= kind && kind <= 3) ? 0.0005 : 0.002; }
double tol_mat(int) { return 0.002; }
double tol_tex(int) { return 0.0005; }

std::vector<uint8_t> Writer::write() {
  const Model& m = m_;
  world_ = m.world_static();
  std::vector<uint32_t> flags(m.joints.size());
  bool any_none = false;
  for (auto& j : m.joints) any_none |= !j.flags.has_value();
  std::vector<uint32_t> made;
  if (any_none) made = hsd_joint_flags(m);
  for (size_t i = 0; i < flags.size(); i++) flags[i] = m.joints[i].flags ? *m.joints[i].flags : made[i];
  child_off_ = 8; next_off_ = 12;
  std::vector<Blob*> blobs;
  for (size_t i = 0; i < m.joints.size(); i++) blobs.push_back(b_.make(0x40));
  joint_blob_ = blobs;
  tree([&](int i) { return blobs[i]; });
  std::map<int, std::vector<const Mesh*>> by_joint;
  for (auto& me : m.meshes) by_joint[me.joint].push_back(&me);
  for (size_t i = 0; i < m.joints.size(); i++) {
    const Joint& j = m.joints[i];
    Blob* o = blobs[i];
    put32(o->b, 4, flags[i]);
    double v9[9] = {j.rot[0], j.rot[1], j.rot[2], j.scale[0], j.scale[1], j.scale[2], j.pos[0], j.pos[1], j.pos[2]};
    for (int k = 0; k < 9; k++) putf(o->b, 0x14 + 4 * k, v9[k]);
    if (j.inv_bind && !j.inv_bind->empty()) o->ptr(0x38, floats(*j.inv_bind));
    std::vector<Blob*> dobjs;
    auto bj = by_joint.find((int)i);
    if (bj != by_joint.end())
      for (const Mesh* me : bj->second) {
        Blob* d = b_.make(0x10);
        if (me->material) d->ptr(8, material_blob(*me->material));
        d->ptr(12, mesh_blobs(*me));
        dobjs.push_back(d);
      }
    for (size_t k = 0; k + 1 < dobjs.size(); k++) dobjs[k]->ptr(4, dobjs[k + 1]);
    if (!dobjs.empty()) o->ptr(0x10, dobjs[0]);
  }
  std::vector<Blob*> anim_roots, mat_roots;
  child_off_ = 0; next_off_ = 4;
  for (const Animation& an : m.animations) {
    auto aj = tree([&](int i) {
      Blob* o = b_.make(0x14);
      o->ptr(8, aobj_blob(an.joint(i), tol_joint));
      put32(o->b, 16, an.jflags(i));
      return o;
    });
    if (aj.empty()) fail("list index out of range");
    anim_roots.push_back(aj[0]);
    auto mj = tree([&](int i) {
      Blob* o = b_.make(0x0C);
      bool present;
      const MatList* lst = an.material(i, &present);
      size_t n = by_joint.count(i) ? by_joint[i].size() : 0;
      std::vector<const std::optional<MatAnim>*> items;
      MatList filler;
      if (!lst && n) {
        filler.items.assign(n, std::nullopt);
        lst = &filler;
      }
      std::vector<Blob*> mats;
      if (lst)
        for (auto& ma : lst->items) {
          Blob* mb = b_.make(0x10);
          if (ma) {
            mb->ptr(4, aobj_blob(ma->aobj ? &*ma->aobj : nullptr, tol_mat));
            std::vector<Blob*> tas;
            for (const TexAnim& ta : ma->texanims) {
              Blob* tb = b_.make(0x18);
              put32(tb->b, 4, ta.tex_id);
              tb->ptr(8, aobj_blob(ta.aobj ? &*ta.aobj : nullptr, tol_tex));
              if (!ta.images.empty()) {
                Blob* it = b_.make(4 * ta.images.size());
                for (size_t q = 0; q < ta.images.size(); q++) it->ptr(q * 4, image_blob(ta.images[q]));
                tb->ptr(12, it);
              }
              if (!ta.tluts.empty()) {
                Blob* tt = b_.make(4 * ta.tluts.size());
                for (size_t q = 0; q < ta.tluts.size(); q++) tt->ptr(q * 4, tlut_blob(ta.tluts[q]));
                tb->ptr(16, tt);
              }
              put16(tb->b, 20, (long long)ta.images.size());
              put16(tb->b, 22, (long long)ta.tluts.size());
              tas.push_back(tb);
            }
            for (size_t k = 0; k + 1 < tas.size(); k++) tas[k]->ptr(0, tas[k + 1]);
            if (!tas.empty()) mb->ptr(8, tas[0]);
          }
          mats.push_back(mb);
        }
      for (size_t k = 0; k + 1 < mats.size(); k++) mats[k]->ptr(0, mats[k + 1]);
      if (!mats.empty()) o->ptr(8, mats[0]);
      return o;
    });
    mat_roots.push_back(mj[0]);
  }
  auto ptr_list = [&](const std::vector<Blob*>& items) {
    Blob* o = b_.make(4 * (items.size() + 1));
    for (size_t k = 0; k < items.size(); k++) o->ptr(k * 4, items[k]);
    return o;
  };
  if (blobs.empty()) fail("list index out of range");
  Blob* ms = b_.make(0x10);
  ms->ptr(0, blobs[0]);
  if (!anim_roots.empty()) {
    ms->ptr(4, ptr_list(anim_roots));
    ms->ptr(8, ptr_list(mat_roots));
  }
  Blob* sets = ptr_list({ms});
  Blob* scene = b_.make(0x10);
  scene->ptr(0, sets);
  std::vector<std::tuple<std::string, Blob*, long long>> publics;
  publics.emplace_back("scene_data", scene, 0);
  bool all_bounds = !m.animations.empty();
  for (auto& a : m.animations) all_bounds = all_bounds && a.bounds && !a.bounds->empty();
  if (all_bounds) {
    std::vector<uint8_t> bb(4, 0);
    put16(bb, 0, (long long)m.animations.size());
    for (size_t k = 0; k < m.animations.size(); k++) {
      const Animation& a = m.animations[k];
      long long head = (m.bound_head && !m.bound_head->empty() && k < m.bound_head->size()) ? (*m.bound_head)[k] : 0;
      size_t o = bb.size();
      bb.resize(o + 4);
      put16(bb, o, head);
      put16(bb, o + 2, (long long)a.bounds->size());
      for (auto& box : *a.bounds)
        for (double x : box) app_f(bb, x);
    }
    publics.emplace_back("bound_box", b_.add(std::move(bb)), 0);
  }
  return b_.finish(publics);
}

}

std::vector<uint8_t> hsd_write_archive(const Model& model, WriteReport* report) {
  Writer w(model);
  std::vector<uint8_t> data = w.write();
  w.report.archive_bytes = (long long)data.size();
  if (report) *report = w.report;
  return data;
}

}
