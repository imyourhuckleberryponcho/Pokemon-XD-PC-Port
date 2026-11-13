#include "pbr_hsd_read.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace pbr {

namespace {

[[noreturn]] void fail(const std::string& s) { throw std::runtime_error(s); }

uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
  if (o + 4 > b.size()) fail("unpack_from requires a buffer of at least " + std::to_string(o + 4) + " bytes");
  return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3];
}
uint32_t rd16(const std::vector<uint8_t>& b, size_t o) {
  if (o + 2 > b.size()) fail("unpack_from requires a buffer of at least " + std::to_string(o + 2) + " bytes");
  return ((uint32_t)b[o] << 8) | b[o + 1];
}
void wr32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  if (o + 4 > b.size()) fail("pack_into requires a buffer of at least " + std::to_string(o + 4) + " bytes");
  b[o] = v >> 24; b[o + 1] = v >> 16; b[o + 2] = v >> 8; b[o + 3] = v;
}
void wr16(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  if (o + 2 > b.size()) fail("pack_into requires a buffer of at least " + std::to_string(o + 2) + " bytes");
  b[o] = v >> 8; b[o + 1] = v;
}
double rdf(const std::vector<uint8_t>& b, size_t o) {
  uint32_t u = rd32(b, o);
  float f;
  memcpy(&f, &u, 4);
  return (double)f;
}
std::vector<uint8_t> slice(const std::vector<uint8_t>& b, size_t o, size_t n) {
  if (o >= b.size()) return {};
  size_t e = std::min(b.size(), o + n);
  return std::vector<uint8_t>(b.begin() + o, b.begin() + e);
}
size_t align32(size_t x) { return (x + 0x1F) & ~(size_t)0x1F; }

}

Pkx Pkx::parse(const std::vector<uint8_t>& b) {
  Pkx p;
  uint32_t size = rd32(b, 0), psize = rd32(b, 8), count = rd32(b, 0x10);
  rd32(b, 4); rd32(b, 0xC);
  if (rd16(b, 0x1A) != 0xC || size == 0) fail("not an XD PKX (type word is not 0x000C or the archive size is 0)");
  p.header = slice(b, 0, 0x84);
  for (uint32_t i = 0; i < count; i++) p.sections.push_back(slice(b, 0x84 + (size_t)i * 0xD0, 0xD0));
  size_t at = align32(0x84 + (size_t)count * 0xD0);
  if (psize) {
    p.particles = slice(b, at, psize);
    at += align32(psize);
  }
  p.archive = slice(b, at, size);
  return p;
}

std::vector<uint8_t> Pkx::build() const {
  std::vector<uint8_t> h = header;
  wr32(h, 0, (uint32_t)archive.size());
  wr32(h, 4, 0);
  wr32(h, 8, (uint32_t)particles.size());
  wr32(h, 0xC, 0);
  wr32(h, 0x10, (uint32_t)sections.size());
  std::vector<uint8_t> out = h;
  for (auto& s : sections) out.insert(out.end(), s.begin(), s.end());
  out.resize(align32(out.size()), 0);
  if (!particles.empty()) {
    out.insert(out.end(), particles.begin(), particles.end());
    out.resize(align32(out.size()), 0);
  }
  out.insert(out.end(), archive.begin(), archive.end());
  out.resize(align32(out.size()), 0);
  return out;
}

int Pkx::head_joint() const { return (int)rd16(header, 0x6E); }
void Pkx::set_head_joint(int j) { wr16(header, 0x6E, (uint32_t)j); }

std::vector<int> Pkx::section_joints(size_t s) const {
  std::vector<int> r;
  for (int i = 0; i < 16; i++) r.push_back((int32_t)rd32(sections[s], 0x4C + i * 4));
  return r;
}
void Pkx::set_section_joints(size_t s, const std::vector<int>& joints) {
  if (joints.size() != 16) fail("pack_into expected 16 items");
  for (int i = 0; i < 16; i++) wr32(sections[s], 0x4C + i * 4, (uint32_t)joints[i]);
}
std::vector<std::pair<int, int>> Pkx::section_motions(size_t s, int* count) const {
  if (count) *count = (int32_t)rd32(sections[s], 4);
  std::vector<std::pair<int, int>> r;
  for (int m = 0; m < 8; m++)
    r.emplace_back((int32_t)rd32(sections[s], 0x8C + m * 8), (int32_t)rd32(sections[s], 0x8C + m * 8 + 4));
  return r;
}

long long hsd_image_bytes(int w, int h, int fmt, int mipmap, double max_lod) {
  auto level = [&](long long w_, long long h_) -> long long {
    if (fmt == 0 || fmt == 8) return ((w_ + 7) / 8) * ((h_ + 7) / 8) * 32;
    if (fmt == 1 || fmt == 2 || fmt == 9) return ((w_ + 7) / 8) * ((h_ + 3) / 4) * 32;
    if (fmt == 3 || fmt == 4 || fmt == 5 || fmt == 10) return ((w_ + 3) / 4) * ((h_ + 3) / 4) * 32;
    if (fmt == 6) return ((w_ + 3) / 4) * ((h_ + 3) / 4) * 64;
    if (fmt == 14) return ((w_ + 7) / 8) * ((h_ + 7) / 8) * 32;
    fail("texture format " + std::to_string(fmt));
  };
  long long n = level(w, h);
  if (mipmap) {
    int levels = max_lod != 0.0 ? (int)max_lod + 1 : 1;
    long long ww = w, hh = h;
    for (int i = 1; i < levels; i++) {
      ww = std::max<long long>(ww / 2, 1);
      hh = std::max<long long>(hh / 2, 1);
      n += level(ww, hh);
    }
  }
  return n;
}

std::vector<Key> hsd_read_fobj_keys(const uint8_t* data, size_t n, int frac_value, int frac_slope) {
  auto at = [&](size_t pos) -> uint8_t {
    if (pos >= n) fail("index out of range");
    return data[pos];
  };
  auto need = [&](size_t pos, size_t k) {
    if (pos + k > n) fail("unpack_from requires a buffer of at least " + std::to_string(pos + k) + " bytes");
  };
  auto num = [&](size_t& pos, int frac) -> double {
    int kind = frac & 0xE0;
    double den = (double)(1ull << (frac & 0x1F));
    if (kind == 0x00) {
      need(pos, 4);
      uint32_t u = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | ((uint32_t)data[pos + 3] << 24);
      float f;
      memcpy(&f, &u, 4);
      pos += 4;
      return (double)f;
    }
    if (kind == 0x20) {
      need(pos, 2);
      int16_t v = (int16_t)(data[pos] | (data[pos + 1] << 8));
      pos += 2;
      return (double)v / den;
    }
    if (kind == 0x40) {
      need(pos, 2);
      uint16_t v = (uint16_t)(data[pos] | (data[pos + 1] << 8));
      pos += 2;
      return (double)v / den;
    }
    if (kind == 0x60) {
      need(pos, 1);
      int8_t v = (int8_t)data[pos];
      pos += 1;
      return (double)v / den;
    }
    if (kind == 0x80) {
      need(pos, 1);
      uint8_t v = data[pos];
      pos += 1;
      return (double)v / den;
    }
    fail("unknown track number format");
  };
  std::vector<Key> keys;
  size_t pos = 0;
  while (pos < n) {
    int d = at(pos);
    pos++;
    int op = d & 0xF;
    long long count = ((d >> 4) & 7) + 1;
    int shift = 3;
    while (d & 0x80) {
      d = at(pos);
      pos++;
      count += (long long)(d & 0x7F) << shift;
      shift += 7;
    }
    if (op == 0) break;
    for (long long q = 0; q < count; q++) {
      double v = 0.0, s = 0.0;
      long long w = 0;
      bool hasv = op == OP_CON || op == OP_LIN || op == OP_SPL0 || op == OP_SPL || op == OP_KEY;
      if (hasv) v = num(pos, frac_value);
      if (op == OP_SPL || op == OP_SLP) s = num(pos, frac_slope);
      if (hasv) {
        if (pos >= n) {
          keys.push_back({op, v, s, 0});
          return keys;
        }
        int sh = 0;
        while (true) {
          int dd = at(pos);
          pos++;
          w |= (long long)(dd & 0x7F) << sh;
          sh += 7;
          if (!(dd & 0x80)) break;
        }
      }
      keys.push_back({op, v, s, w});
    }
  }
  return keys;
}

namespace {

struct Layout {
  uint32_t attr, type, cnt, comp;
  int frac;
  uint32_t stride, ptr;
};

struct Archive {
  std::vector<uint8_t> d;
  std::vector<std::pair<std::string, uint32_t>> pub;
  explicit Archive(const std::vector<uint8_t>& a) {
    uint32_t ds = rd32(a, 4), nr = rd32(a, 8), npub = rd32(a, 12), next_ = rd32(a, 16);
    rd32(a, 0);
    d = slice(a, 0x20, ds);
    for (uint32_t i = 0; i < nr; i++) rd32(a, 0x20 + (size_t)ds + (size_t)i * 4);
    size_t o = 0x20 + (size_t)ds + (size_t)nr * 4;
    size_t strs = o + ((size_t)npub + next_) * 8;
    for (uint32_t i = 0; i < npub; i++) {
      uint32_t off = rd32(a, o + (size_t)i * 8), name = rd32(a, o + (size_t)i * 8 + 4);
      size_t s = strs + name, e = s;
      if (s > a.size()) fail("substring not found");
      while (e < a.size() && a[e] != 0) e++;
      if (e >= a.size()) fail("subsequence not found");
      std::string nm(a.begin() + s, a.begin() + e);
      bool found = false;
      for (auto& p : pub)
        if (p.first == nm) { p.second = off; found = true; }
      if (!found) pub.emplace_back(nm, off);
    }
  }
  const uint32_t* get(const std::string& k) const {
    for (auto& p : pub) if (p.first == k) return &p.second;
    return nullptr;
  }
};

struct Reader {
  Archive arc;
  const std::vector<uint8_t>& d;
  Model model;
  std::unordered_map<uint32_t, int> joint_at, image_at, tlut_at, mat_at;
  std::vector<uint32_t> joint_off;
  std::vector<std::string> warnings;
  std::vector<Mat> world;
  bool with_meshes = true, skin_root_set = false;
  bool skeleton_root_none = true;

  explicit Reader(const std::vector<uint8_t>& archive_bytes) : arc(archive_bytes), d(arc.d) {}

  uint32_t u32(size_t o) const { return rd32(d, o); }
  uint32_t u16(size_t o) const { return rd16(d, o); }
  double f32(size_t o) const { return rdf(d, o); }
  uint8_t byte(size_t o) const {
    if (o >= d.size()) fail("index out of range");
    return d[o];
  }
  bool cstr(size_t o, std::string* out) const {
    if (!o) return false;
    if (o > d.size()) fail("subsection not found");
    size_t e = o;
    while (e < d.size() && d[e] != 0) e++;
    if (e >= d.size()) fail("subsection not found");
    out->assign(d.begin() + o, d.begin() + e);
    return true;
  }
  void note_once(const std::string& t) {
    if (std::find(warnings.begin(), warnings.end(), t) == warnings.end()) warnings.push_back(t);
  }

  void read_joints(uint32_t o, int parent) {
    while (o) {
      model.joints.emplace_back();
      int idx = (int)model.joints.size() - 1;
      joint_at[o] = idx;
      joint_off.push_back(o);
      {
        Joint& j = model.joints[idx];
        std::string nm;
        if (cstr(u32(o), &nm)) j.name = nm;
        j.parent = parent;
        j.flags = u32(o + 4);
        j.hidden = (*j.flags & JOBJ_HIDDEN) != 0;
        j.rot = {f32(o + 0x14), f32(o + 0x18), f32(o + 0x1C)};
        j.scale = {f32(o + 0x20), f32(o + 0x24), f32(o + 0x28)};
        j.pos = {f32(o + 0x2C), f32(o + 0x30), f32(o + 0x34)};
        uint32_t m = u32(o + 0x38);
        if (m) {
          std::vector<double> ib;
          for (int k = 0; k < 12; k++) ib.push_back(f32(m + k * 4));
          j.inv_bind = ib;
        }
        if (u32(o + 0x3C)) warnings.push_back("joint " + std::to_string(idx) + " has a constraint (robj): not carried over");
      }
      read_joints(u32(o + 8), idx);
      o = u32(o + 0xC);
    }
  }

  int image(uint32_t o) {
    auto it = image_at.find(o);
    if (it != image_at.end()) return it->second;
    Image im;
    uint32_t data = u32(o);
    im.width = (int)u16(o + 4);
    im.height = (int)u16(o + 6);
    im.format = (int)u32(o + 8);
    im.mipmap = (int)u32(o + 12);
    im.min_lod = f32(o + 16);
    im.max_lod = f32(o + 20);
    im.data = slice(d, data, (size_t)hsd_image_bytes(im.width, im.height, im.format, im.mipmap, im.max_lod));
    int idx = (int)model.images.size();
    image_at[o] = idx;
    model.images.push_back(std::move(im));
    return idx;
  }

  int tlut(uint32_t o) {
    auto it = tlut_at.find(o);
    if (it != tlut_at.end()) return it->second;
    Tlut t;
    uint32_t data = u32(o);
    t.format = u32(o + 4);
    t.name = u32(o + 8);
    t.count = (int)u16(o + 12);
    t.data = slice(d, data, (size_t)t.count * 2);
    int idx = (int)model.tluts.size();
    tlut_at[o] = idx;
    model.tluts.push_back(std::move(t));
    return idx;
  }

  int material(uint32_t o) {
    auto it = mat_at.find(o);
    if (it != mat_at.end()) return it->second;
    Material m;
    {
      std::string nm;
      if (cstr(u32(o), &nm)) m.name = nm;
    }
    m.rendermode = u32(o + 4);
    uint32_t mat = u32(o + 12);
    auto take = [&](size_t at, std::vector<double>& out) {
      out.clear();
      for (size_t k = at; k < at + 4 && k < d.size(); k++) out.push_back((double)d[k]);
    };
    take(mat, m.ambient);
    take(mat + 4, m.diffuse);
    take(mat + 8, m.specular);
    m.alpha = f32(mat + 12);
    m.shininess = f32(mat + 16);
    if (u32(o + 16)) warnings.push_back("material has a render description: not carried over");
    uint32_t pe = u32(o + 20);
    if (pe) m.pe = slice(d, pe, 12);
    uint32_t t = u32(o + 8);
    while (t) {
      Texture x;
      x.tex_id = u32(t + 8);
      x.src = u32(t + 12);
      x.rot = {f32(t + 0x10), f32(t + 0x14), f32(t + 0x18)};
      x.scale = {f32(t + 0x1C), f32(t + 0x20), f32(t + 0x24)};
      x.trans = {f32(t + 0x28), f32(t + 0x2C), f32(t + 0x30)};
      x.wrap_s = u32(t + 0x34);
      x.wrap_t = u32(t + 0x38);
      x.repeat_s = byte(t + 0x3C);
      x.repeat_t = byte(t + 0x3D);
      x.flags = u32(t + 0x40);
      x.blending = f32(t + 0x44);
      x.mag_filter = u32(t + 0x48);
      if (u32(t + 0x4C)) x.image = image(u32(t + 0x4C));
      if (u32(t + 0x50)) x.tlut = tlut(u32(t + 0x50));
      if (u32(t + 0x54)) x.lod = slice(d, u32(t + 0x54), 16);
      if (u32(t + 0x58)) x.tev = slice(d, u32(t + 0x58), 32);
      m.textures.push_back(std::move(x));
      t = u32(t + 4);
    }
    int idx = (int)model.materials.size();
    mat_at[o] = idx;
    model.materials.push_back(std::move(m));
    return idx;
  }

  std::vector<Layout> vertex_layout(uint32_t o) {
    std::vector<Layout> lay;
    while (u32(o) != 0xFF) {
      Layout a;
      a.attr = u32(o);
      a.type = u32(o + 4);
      a.cnt = u32(o + 8);
      a.comp = u32(o + 12);
      a.frac = byte(o + 16);
      a.stride = u16(o + 18);
      a.ptr = u32(o + 20);
      lay.push_back(a);
      o += 0x18;
    }
    std::stable_sort(lay.begin(), lay.end(), [](const Layout& x, const Layout& y) { return x.attr < y.attr; });
    return lay;
  }

  void component(const Layout& a, uint32_t index, int n, double* out) {
    size_t o = (size_t)a.ptr + (size_t)a.stride * index;
    if (a.comp == 4) {
      for (int k = 0; k < n; k++) out[k] = f32(o + k * 4);
      return;
    }
    double den = (double)(1ull << a.frac);
    for (int k = 0; k < n; k++) {
      double v;
      switch (a.comp) {
        case 0: v = byte(o + k); break;
        case 1: v = (int8_t)byte(o + k); break;
        case 2: v = (double)u16(o + k * 2); break;
        case 3: v = (double)(int16_t)u16(o + k * 2); break;
        default: fail("KeyError");
      }
      out[k] = v / den;
    }
  }

  void read_meshes(int jidx) {
    uint32_t o = joint_off[jidx];
    Joint& j = model.joints[jidx];
    if (*j.flags & (JOBJ_PTCL | JOBJ_SPLINE)) {
      if (u32(o + 0x10)) warnings.push_back("joint " + std::to_string(jidx) + " holds a particle / spline: not carried over");
      return;
    }
    uint32_t dob = u32(o + 0x10);
    while (dob) {
      Mesh me;
      me.joint = jidx;
      {
        std::string nm;
        if (cstr(u32(dob), &nm)) me.name = nm;
      }
      if (u32(dob + 8)) me.material = material(u32(dob + 8));
      else me.material = std::nullopt;
      std::map<std::vector<double>, int> index;
      uint32_t po = u32(dob + 12);
      bool first = true;
      while (po && with_meshes) {
        read_pobj(po, me, index, first);
        first = false;
        po = u32(po + 4);
      }
      model.meshes.push_back(std::move(me));
      dob = u32(dob + 4);
    }
  }

  void skin_space(int jidx, int* root, std::optional<Mat>* conv) {
    Model& m = model;
    if (*m.joints[jidx].flags & JOBJ_SKELETON_ROOT) {
      *root = jidx;
      conv->reset();
      return;
    }
    int x = jidx;
    while (x >= 0 && !(*m.joints[x].flags & (JOBJ_SKELETON | JOBJ_SKELETON_ROOT))) x = m.joints[x].parent;
    if (x < 0) fail("skinned mesh on joint " + std::to_string(jidx) + " has no skeleton above it");
    if (x == jidx || !(*m.joints[x].flags & JOBJ_SKELETON_ROOT)) {
      if (!m.joints[x].inv_bind) fail("skinned mesh on joint " + std::to_string(jidx) + ": joint " + std::to_string(x) + " has no inverse bind matrix");
      Mat ib;
      std::copy(m.joints[x].inv_bind->begin(), m.joints[x].inv_bind->end(), ib.begin());
      *root = -1;
      if (x == jidx) {
        *conv = mat_inv(ib);
        return;
      }
      *conv = mat_mul(mat_inv(mat_mul(world[x], ib)), world[jidx]);
      return;
    }
    *root = x;
    *conv = mat_mul(mat_inv(world[x]), world[jidx]);
  }

  struct Ref { bool has = false; const Layout* a = nullptr; uint32_t i = 0; };

  void read_pobj(uint32_t po, Mesh& me, std::map<std::vector<double>, int>& index, bool first) {
    uint32_t flags = u16(po + 0xC), ndisp = u16(po + 0xE), disp = u32(po + 0x10), u = u32(po + 0x14);
    int kind = (flags >> 12) & 3;
    std::vector<Layout> lay = vertex_layout(u32(po + 8));
    if (first) {
      me.pobj_flags = (int)flags;
    } else if ((flags & ~0x3000u) != ((uint32_t)*me.pobj_flags & ~0x3000u)) {
      char buf[96];
      snprintf(buf, sizeof buf, "polygon objects of one display object differ in flags (%04X / %04X)", flags, (unsigned)*me.pobj_flags);
      warnings.push_back(buf);
    }
    std::vector<Weights> envs;
    std::optional<Mat> conv;
    if (kind == 2) {
      int root;
      skin_space(me.joint, &root, &conv);
      if (root >= 0) {
        if (!skeleton_root_none && model.skeleton_root != root && skin_root_set) fail("two skeleton roots in one model: not handled");
        model.skeleton_root = root;
        skeleton_root_none = false;
        skin_root_set = true;
      }
      uint32_t k = 0;
      while (u32(u + k * 4)) {
        uint32_t e = u32(u + k * 4);
        Weights ws;
        while (u32(e)) {
          auto it = joint_at.find(u32(e));
          if (it == joint_at.end()) fail("KeyError");
          ws.emplace_back(it->second, f32(e + 4));
          e += 8;
        }
        envs.push_back(std::move(ws));
        k++;
      }
    } else if (kind == 1) {
      warnings.push_back("shape (morph) animation mesh: read as a plain mesh");
      me.rigid = true;
    } else {
      if (u) fail("shared-vertex skin (two-joint polygon object): not handled");
      me.rigid = true;
    }
    size_t p = disp, end = (size_t)disp + (size_t)ndisp * 32;
    std::map<std::array<long long, 5>, int> cache;
    while (p < end) {
      int op = byte(p);
      if (op == 0) break;
      int cnt = (int)u16(p + 1);
      p += 3;
      int prim = op & 0xF8;
      std::vector<int> idxs;
      for (int q = 0; q < cnt; q++) {
        long long mtx = -1;
        Ref pos, nrm, uv, col;
        for (auto& a : lay) {
          uint32_t t = a.type;
          if (t == 0) continue;
          if (a.attr == 0 && t == 1) {
            mtx = byte(p) / 3;
            p += 1;
            continue;
          }
          uint32_t i;
          if (t == 2) {
            i = byte(p);
            p += 1;
          } else if (t == 3) {
            i = u16(p);
            p += 2;
          } else {
            fail("direct vertex data for attribute " + std::to_string(a.attr) + ": not handled");
          }
          if (a.attr == 9) pos = {true, &a, i};
          else if (a.attr == 10) nrm = {true, &a, i};
          else if (a.attr == 11) col = {true, &a, i};
          else if (a.attr == 13) uv = {true, &a, i};
          else note_once("vertex attribute " + std::to_string(a.attr) + " ignored");
        }
        if (!pos.has) fail("TypeError: 'NoneType' object is not subscriptable");
        std::array<long long, 5> key = {mtx, (long long)pos.i, nrm.has ? (long long)nrm.i : -1, uv.has ? (long long)uv.i : -1,
                                        col.has ? (long long)col.i : -1};
        auto it = cache.find(key);
        int vi;
        if (it == cache.end()) {
          vi = make_vertex(me, index, envs, conv, mtx, pos, nrm, uv, col);
          cache[key] = vi;
        } else {
          vi = it->second;
        }
        idxs.push_back(vi);
      }
      if (prim == 0x90) {
        for (int i = 0; i < cnt - 2; i += 3) me.tris.push_back({idxs[i], idxs[i + 1], idxs[i + 2]});
      } else if (prim == 0x98) {
        for (int i = 0; i < cnt - 2; i++) {
          if (i % 2 == 0) me.tris.push_back({idxs[i], idxs[i + 1], idxs[i + 2]});
          else me.tris.push_back({idxs[i + 1], idxs[i], idxs[i + 2]});
        }
      } else if (prim == 0xA0) {
        for (int i = 1; i < cnt - 1; i++) me.tris.push_back({idxs[0], idxs[i], idxs[i + 1]});
      } else if (prim == 0x80) {
        for (int i = 0; i < cnt - 3; i += 4) {
          me.tris.push_back({idxs[i], idxs[i + 1], idxs[i + 2]});
          me.tris.push_back({idxs[i], idxs[i + 2], idxs[i + 3]});
        }
      } else {
        char buf[64];
        snprintf(buf, sizeof buf, "display list command 0x%02X: not handled", op);
        fail(buf);
      }
    }
  }

  static std::vector<double> vkey(const Vertex& v) {
    std::vector<double> k = {v.pos[0], v.pos[1], v.pos[2]};
    k.push_back(v.nrm ? 1 : 0);
    if (v.nrm) k.insert(k.end(), v.nrm->begin(), v.nrm->end());
    k.push_back(v.uv ? 1 : 0);
    if (v.uv) k.insert(k.end(), v.uv->begin(), v.uv->end());
    k.push_back(v.color ? 1 : 0);
    if (v.color) {
      k.push_back((double)v.color->size());
      for (int c : *v.color) k.push_back(c);
    }
    k.push_back(v.weights ? 1 : 0);
    if (v.weights) {
      k.push_back((double)v.weights->size());
      for (auto& w : *v.weights) { k.push_back(w.first); k.push_back(w.second); }
    }
    k.push_back(v.has_uvx ? 1 : 0);
    return k;
  }

  int make_vertex(Mesh& me, std::map<std::vector<double>, int>& index, const std::vector<Weights>& envs, const std::optional<Mat>& conv,
                  long long mtx, const Ref& pos, const Ref& nrm, const Ref& uv, const Ref& col) {
    V3 p;
    component(*pos.a, pos.i, 3, p.data());
    std::optional<V3> n;
    if (nrm.has) { V3 t; component(*nrm.a, nrm.i, 3, t.data()); n = t; }
    std::optional<std::array<double, 2>> t;
    if (uv.has) { std::array<double, 2> tt; component(*uv.a, uv.i, 2, tt.data()); t = tt; }
    std::optional<std::vector<int>> c;
    if (col.has) {
      const Layout& a = *col.a;
      size_t o = (size_t)a.ptr + (size_t)a.stride * col.i;
      if (a.comp == 5) {
        std::vector<int> v;
        for (size_t k = o; k < o + 4 && k < d.size(); k++) v.push_back(d[k]);
        c = v;
      } else if (a.comp == 1) {
        std::vector<int> v;
        for (size_t k = o; k < o + 3 && k < d.size(); k++) v.push_back(d[k]);
        v.push_back(255);
        c = v;
      } else {
        note_once("vertex colour format " + std::to_string(a.comp) + " read as white");
        c = std::vector<int>{255, 255, 255, 255};
      }
    }
    std::optional<Weights> w;
    if (!me.rigid) {
      if (mtx < 0) mtx = 0;
      if ((size_t)mtx >= envs.size()) fail("list index out of range");
      w = envs[mtx];
      std::optional<Mat> m = conv;
      if (!conv) {
        if (w->size() == 1 && (*w)[0].second >= 1.0 - 1.2e-7) {
          const auto& jb = model.joints[(*w)[0].first].inv_bind;
          if (jb) {
            Mat jm;
            std::copy(jb->begin(), jb->end(), jm.begin());
            m = mat_inv(jm);
          } else {
            m = mat_mul(mat_inv(world[model.skeleton_root]), world[(*w)[0].first]);
          }
        }
      }
      if (m) {
        p = mat_point(*m, p);
        if (n) n = mat_vec(*m, *n);
      }
    }
    Vertex v;
    v.pos = p;
    v.nrm = n;
    v.uv = t;
    v.color = c;
    v.weights = w;
    std::vector<double> k = vkey(v);
    for (double x : k)
      if (x != x) {
        me.verts.push_back(std::move(v));
        return (int)me.verts.size() - 1;
      }
    auto it = index.find(k);
    if (it != index.end()) return it->second;
    int i = (int)me.verts.size();
    index[k] = i;
    me.verts.push_back(std::move(v));
    return i;
  }

  std::optional<AnimObj> aobj(uint32_t o) {
    if (!o) return std::nullopt;
    AnimObj a;
    a.flags = u32(o);
    a.end = f32(o + 4);
    if (u32(o + 12)) note_once("animation object refers to another object (obj_id): not carried over");
    uint32_t f = u32(o + 8);
    while (f) {
      uint32_t length = u32(f + 4);
      double start = f32(f + 8);
      int kind = byte(f + 12), fv = byte(f + 13), fs = byte(f + 14);
      uint32_t ad = u32(f + 16);
      std::vector<uint8_t> bytes = slice(d, ad, length);
      Track t;
      t.kind = kind;
      t.start = start;
      t.keys = hsd_read_fobj_keys(bytes.data(), bytes.size(), fv, fs);
      t.src_format = std::make_pair(fv, fs);
      a.tracks.push_back(std::move(t));
      f = u32(f);
    }
    return a;
  }

  void read_animjoint(uint32_t o, int& counter, Animation& anim) {
    while (o) {
      int jidx = counter++;
      uint32_t a = u32(o + 8);
      if (a) anim.set_joint(jidx, *aobj(a));
      if (u32(o + 12)) note_once("constraint animation (robj): not carried over");
      anim.set_joint_flags(jidx, u32(o + 16));
      read_animjoint(u32(o), counter, anim);
      o = u32(o + 4);
    }
  }

  void read_matanimjoint(uint32_t o, int& counter, Animation& anim) {
    while (o) {
      int jidx = counter++;
      uint32_t ma = u32(o + 8);
      MatList lst;
      while (ma) {
        MatAnim m;
        m.aobj = aobj(u32(ma + 4));
        if (u32(ma + 12)) {
          m.has_render_anim = true;
          note_once("render animation: not carried over");
        }
        uint32_t ta = u32(ma + 8);
        while (ta) {
          TexAnim t;
          t.tex_id = u32(ta + 4);
          t.aobj = aobj(u32(ta + 8));
          uint32_t it = u32(ta + 12), tt = u32(ta + 16);
          uint32_t ni = u16(ta + 20), nt = u16(ta + 22);
          for (uint32_t i = 0; i < ni; i++) t.images.push_back(image(u32(it + 4 * i)));
          for (uint32_t i = 0; i < nt; i++) t.tluts.push_back(tlut(u32(tt + 4 * i)));
          m.texanims.push_back(std::move(t));
          ta = u32(ta);
        }
        lst.items.push_back(std::move(m));
        ma = u32(ma);
      }
      if (!lst.items.empty()) anim.set_material(jidx, std::move(lst));
      read_matanimjoint(u32(o), counter, anim);
      o = u32(o + 4);
    }
  }

  Model& read(bool meshes) {
    with_meshes = meshes;
    const uint32_t* scenep = arc.get("scene_data");
    if (!scenep) fail("archive has no scene_data");
    uint32_t scene = *scenep;
    uint32_t sets = u32(scene);
    uint32_t ms = u32(sets);
    if (u32(sets + 4)) warnings.push_back("more than one model set: only the first is read");
    if (u32(scene + 4) || u32(scene + 8) || u32(scene + 12)) warnings.push_back("scene has camera / lights / fog: not carried over");
    uint32_t root = u32(ms), aj = u32(ms + 4), maj = u32(ms + 8), saj = u32(ms + 12);
    read_joints(root, -1);
    Model& m = model;
    world = m.world_static();
    skeleton_root_none = true;
    skin_root_set = false;
    for (size_t i = 0; i < m.joints.size(); i++) read_meshes((int)i);
    if (skeleton_root_none && !meshes) {
      for (size_t i = 0; i < m.joints.size(); i++)
        if (*m.joints[i].flags & JOBJ_SKELETON_ROOT) {
          m.skeleton_root = (int)i;
          skeleton_root_none = false;
          break;
        }
    }
    if (skeleton_root_none) m.skeleton_root = 0;
    int k = 0;
    while (aj && u32(aj + k * 4)) {
      Animation an;
      int c = 0;
      read_animjoint(u32(aj + k * 4), c, an);
      if (c != (int)m.joints.size())
        warnings.push_back("animation " + std::to_string(k) + " has " + std::to_string(c) + " nodes for " + std::to_string(m.joints.size()) + " joints");
      m.animations.push_back(std::move(an));
      k++;
    }
    k = 0;
    while (maj && u32(maj + k * 4)) {
      if (k >= (int)m.animations.size()) m.animations.emplace_back();
      m.animations[k].has_material_tree = true;
      int c = 0;
      read_matanimjoint(u32(maj + k * 4), c, m.animations[k]);
      if (c != (int)m.joints.size())
        warnings.push_back("material animation " + std::to_string(k) + " has " + std::to_string(c) + " nodes for " + std::to_string(m.joints.size()) + " joints");
      k++;
    }
    if (saj && u32(saj)) warnings.push_back("shape animations: not carried over");
    const uint32_t* bbp = arc.get("bound_box");
    if (bbp) {
      uint32_t bb = *bbp;
      uint32_t count = u16(bb);
      m.bound_head = std::vector<int>();
      size_t p = (size_t)bb + 4;
      for (uint32_t i = 0; i < count; i++) {
        uint32_t a = u16(p), frames = u16(p + 2);
        m.bound_head->push_back((int)a);
        std::vector<std::array<double, 6>> boxes;
        for (uint32_t f = 0; f < frames; f++) {
          std::array<double, 6> bx;
          for (int q = 0; q < 6; q++) bx[q] = f32(p + 4 + (size_t)f * 24 + q * 4);
          boxes.push_back(bx);
        }
        if (i < m.animations.size()) m.animations[i].bounds = std::move(boxes);
        p += 4 + (size_t)frames * 24;
      }
    }
    return m;
  }
};

}

ReadResult hsd_read_pkx(const std::vector<uint8_t>& b, bool meshes) {
  ReadResult r;
  r.pkx = Pkx::parse(b);
  Reader rd(r.pkx.archive);
  rd.read(meshes);
  r.model = std::move(rd.model);
  r.warnings = std::move(rd.warnings);
  return r;
}

}
