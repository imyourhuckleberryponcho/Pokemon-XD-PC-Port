#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "n3ds_data.h"
#include "n3ds_dump.h"
#include "n3ds_gf.h"
#include "n3ds_pose.h"

namespace n3ds {
namespace {

uint32_t U32(const Bytes& b, size_t o) {
  if (o + 4 > b.size()) throw Fail("cut short");
  return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
}
uint16_t U16(const Bytes& b, size_t o) {
  if (o + 2 > b.size()) throw Fail("cut short");
  return (uint16_t)(b[o] | b[o + 1] << 8);
}
float F32(const Bytes& b, size_t o) {
  const uint32_t v = U32(b, o);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
std::string CStr(const Bytes& b, size_t o) {
  std::string s;
  while (o < b.size() && b[o]) s += (char)b[o++];
  return s;
}

struct PackEntry {
  std::string name;
  Bytes data;
};
bool SplitModelPack(const Bytes& b, std::vector<PackEntry>& models, std::vector<PackEntry>& textures) {
  if (b.size() < 0x18 || U32(b, 0) != 0x10000) return false;
  uint32_t cnt[5], n = 0;
  for (int i = 0; i < 5; i++) n += cnt[i] = U32(b, 4 + 4 * (size_t)i);
  if (n == 0 || n > 4096) return false;
  std::vector<std::pair<std::string, uint32_t>> ents;
  std::vector<uint32_t> addrs;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t p = U32(b, 0x18 + 4 * (size_t)i);
    if (p >= b.size()) return false;
    const uint8_t l = b[p];
    std::string nm(b.begin() + p + 1, b.begin() + std::min<size_t>(b.size(), p + 1 + l));
    const uint32_t a = U32(b, p + 1 + l);
    ents.push_back({nm, a});
    addrs.push_back(a);
  }
  addrs.push_back((uint32_t)b.size());
  std::sort(addrs.begin(), addrs.end());
  uint32_t k = 0;
  for (int kind = 0; kind < 2; kind++)
    for (uint32_t j = 0; j < cnt[kind]; j++, k++) {
      const uint32_t a = ents[k].second;
      const uint32_t e = *std::upper_bound(addrs.begin(), addrs.end(), a);
      if (a > b.size() || e > b.size() || e < a) return false;
      (kind == 0 ? models : textures).push_back({ents[k].first, Bytes(b.begin() + a, b.begin() + e)});
    }
  return true;
}

int SpbdFormat(uint32_t f) {
  switch (f) {
    case 18: return 0x2b;
    case 19: return 0x2a;
    case 20: return 0x24;
    case 21: return 0x28;
    case 22: return 0x29;
    case 23: return 0x25;
    case 24: return 0x26;
    case 25: return 0x27;
    case 26: return 0x23;
    case 27: return 0x17;
    case 28: return 0x16;
    case 29: return 0x4;
    case 30: return 0x3;
    case 31: return 0x2;
    default: return -1;
  }
}

struct OutTex {
  std::string name;
  int w = 0, h = 0;
  Bytes px;
};
struct Writer {
  Bytes b;
  void u8(uint32_t v) { b.push_back((uint8_t)v); }
  void u16(uint32_t v) { u8(v); u8(v >> 8); }
  void u32(uint32_t v) { u16(v); u16(v >> 16); }
  void i16(int v) { u16((uint32_t)(uint16_t)(int16_t)v); }
  void f32(double d) {
    const float f = (float)d;
    uint32_t v;
    std::memcpy(&v, &f, 4);
    u32(v);
  }
  void str(const std::string& s) {
    const size_t n = std::min<size_t>(s.size(), 255);
    u8((uint32_t)n);
    b.insert(b.end(), s.begin(), s.begin() + (long)n);
  }
};

int BlendKind(const GfMaterial& m) {
  if (!m.has_blend) return 0;
  const uint32_t eq = m.blend & 7, src = (m.blend >> 16) & 15, dst = (m.blend >> 20) & 15;
  if (eq == 1 || eq == 2) return 3;
  if (dst == 1) return 1;
  if (src == 1 && dst == 0) return 2;
  return 0;
}

}

Bytes mega_fx_bin(const RomFs& r, std::string* why) {
  auto no = [&](const std::string& w) { if (why) *why = w; return Bytes{}; };
  const char* path = "a/0/8/7";
  if (!r.has(path)) return no("the dump has no battle effect archive a/0/8/7");
  try {
    const Garc g = garc_at(r, path);
    std::vector<std::pair<size_t, Bytes>> modelFiles, partFiles;
    std::vector<size_t> modelIdx;
    for (size_t i = 0; i < g.count(); i++) {
      if (g.nsub(i) == 0) continue;
      Bytes b;
      try { b = lz11_maybe(g.read(i)); } catch (const std::exception&) { continue; }
      if (b.size() > 0x20 && U32(b, 0) == 0x10000) {
        std::vector<PackEntry> ms, ts;
        if (SplitModelPack(b, ms, ts) && !ms.empty() && ms[0].name.rfind("em000_", 0) == 0) { modelFiles.push_back({i, b}); modelIdx.push_back(i); }
      } else if (b.size() > 0x60 && std::memcmp(b.data(), "SPBD3", 5) == 0) {
        const std::string nm = CStr(b, U32(b, 0x10));
        if (nm.rfind("em000_", 0) == 0) partFiles.push_back({i, b});
      }
    }
    if (modelFiles.size() != 7 || partFiles.empty()) return no("the Mega Evolution effect (em000) was not found in a/0/8/7 (" + std::to_string(modelFiles.size()) + " models)");
    const size_t motion0 = modelIdx.back() + 1;

    std::vector<OutTex> texs;
    std::map<std::string, int> texByKey;
    auto addTex = [&](const std::string& key, const std::string& name, int w, int h, int fmt, const Bytes& data) -> int {
      auto it = texByKey.find(key);
      if (it != texByKey.end()) return it->second;
      GfTexture t;
      t.name = name; t.w = w; t.h = h; t.fmt = fmt; t.data = data;
      Rgba px = decode_texture(t);
      if (fmt == 0x25 || fmt == 0x28) {
        for (size_t k = 0; k + 3 < px.px.size(); k += 4) { px.px[k + 3] = px.px[k]; px.px[k] = px.px[k + 1] = px.px[k + 2] = 255; }
      }
      texs.push_back({name, px.w, px.h, px.px});
      return texByKey[key] = (int)texs.size() - 1;
    };

    Writer body;
    for (size_t mi = 0; mi < modelFiles.size(); mi++) {
      std::vector<PackEntry> ms, ts;
      SplitModelPack(modelFiles[mi].second, ms, ts);
      const GfModel gm = parse_model(ms[0].data);
      std::map<std::string, int> texIdx;
      for (auto& t : ts) {
        const GfTexture T = parse_texture(t.data);
        texIdx[T.name] = addTex(ms[0].name + "/" + T.name, T.name, T.w, T.h, T.fmt, T.data);
      }
      GfMotion mo;
      bool haveMo = false;
      if (motion0 + mi < g.count() && g.nsub(motion0 + mi) > 0) haveMo = parse_motion(lz11_maybe(g.read(motion0 + mi)), mo);
      const int frames = haveMo ? std::max(1, mo.frames) : 1;
      const Skeleton sk(gm.bones);
      std::vector<std::vector<M4>> W((size_t)frames);
      for (int f = 0; f < frames; f++) {
        if (haveMo && !mo.bones.empty()) {
          const Pose ps = sk.sample_motion(mo, f);
          W[(size_t)f] = sk.world(ps.s, ps.r, ps.t, &ps.axis);
        } else {
          W[(size_t)f] = sk.bind_world;
        }
      }
      std::map<std::string, const GfMaterial*> mats;
      for (auto& m : gm.materials) mats[m.name] = &m;
      uint32_t nsubs = 0;
      for (auto& me : gm.meshes) nsubs += (uint32_t)me.subs.size();
      body.str(ms[0].name);
      body.u32((uint32_t)frames);
      body.u32(nsubs);
      for (auto& me : gm.meshes)
        for (auto& sm : me.subs) {
          const GfMaterial* m = mats.count(sm.name) ? mats[sm.name] : nullptr;
          const GfVerts& v = sm.verts;
          body.str(me.name);
          body.str(sm.name);
          body.u8(m ? (uint32_t)BlendKind(*m) : 0);
          const size_t nc = m ? std::min<size_t>(m->coords.size(), 3) : 0;
          body.u8((uint32_t)nc);
          for (size_t c = 0; c < nc; c++) {
            const GfCoord& cc = m->coords[c];
            body.i16(texIdx.count(cc.name) ? texIdx[cc.name] : -1);
            body.u8(cc.wrap[0]);
            body.u8(cc.wrap[1]);
            body.u8(cc.unit != 0 && v.has("uv1") ? 1 : 0);
          }
          const auto tris = triangles(sm);
          body.u32((uint32_t)v.n);
          body.u32((uint32_t)tris.size());
          for (int k = 0; k < v.n; k++) {
            double col[4] = {1, 1, 1, 1};
            if (v.has("col")) for (int e = 0; e < 4 && e < v.get("col").cols; e++) col[e] = v.get("col").at(k, e);
            else if (v.has("fixed_col")) for (int e = 0; e < 4 && e < v.get("fixed_col").cols; e++) col[e] = v.get("fixed_col").at(0, e);
            for (double c : col) body.u8((uint32_t)std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
            for (const char* uvn : {"uv0", "uv1"}) {
              const char* src = v.has(uvn) ? uvn : "uv0";
              body.f32(v.has(src) ? v.get(src).at(k, 0) : 0.0);
              body.f32(v.has(src) ? v.get(src).at(k, 1) : 0.0);
            }
          }
          for (auto& t : tris)
            for (int e = 0; e < 3; e++) body.u16((uint32_t)t[(size_t)e]);
          const std::vector<bool>* vis = nullptr;
          if (haveMo)
            for (auto& vv : mo.vis)
              if (vv.first == me.name) vis = &vv.second;
          for (int f = 0; f < frames; f++) {
            body.u8(vis == nullptr || (size_t)f >= vis->size() || (*vis)[(size_t)f] ? 1 : 0);
            const auto P = skin_points(sk, W[(size_t)f], sm.bone_idx, v);
            for (auto& p : P) { body.f32(p[0]); body.f32(p[1]); body.f32(p[2]); }
            for (size_t c = 0; c < nc; c++) {
              const GfCoord& cc = m->coords[c];
              double t5[5] = {cc.scale[0], cc.scale[1], cc.rot, cc.trans[0], cc.trans[1]};
              if (haveMo)
                for (auto& mc : mo.mats)
                  if (mc.name == sm.name && mc.unit == cc.unit) {
                    const GfChannel* ch[5] = {&mc.sx, &mc.sy, &mc.rot, &mc.tx, &mc.ty};
                    for (int e = 0; e < 5; e++) { double x; if (herp(*ch[e], f, &x)) t5[e] = x; }
                  }
              for (double x : t5) body.f32(x);
            }
          }
        }
    }
    Writer parts;
    uint32_t nsets = 0;
    for (auto& pf : partFiles) {
      const Bytes& b = pf.second;
      const uint32_t names = U32(b, 0x10), texOff = U32(b, 0x14), texSize = U32(b, 0x18), sets = U32(b, 8);
      for (uint32_t s = 0; s < sets && s < 16; s++) {
        const size_t so = 0x48 + 28 * (size_t)s;
        const uint32_t cnt = U32(b, so + 16), tab = U32(b, so + 20);
        parts.str(CStr(b, names + U32(b, so + 8)));
        parts.u32(cnt);
        nsets++;
        for (uint32_t e = 0; e < cnt; e++) {
          const uint32_t eo = U32(b, tab + 8 * (size_t)e);
          parts.str(CStr(b, names + U32(b, eo + 56)));
          std::vector<int> ti;
          for (int k = 0; k < 2; k++) {
            const size_t q = eo + 64 + 124 * (size_t)k;
            const int w = U16(b, q), h = U16(b, q + 2);
            if (!w || !h) continue;
            const int fmt = SpbdFormat(U32(b, q + 108));
            const uint32_t size = U32(b, q + 112), pos = U32(b, q + 116);
            if (fmt < 0 || pos + size > texSize || (size_t)texOff + pos + size > b.size()) continue;
            const std::string key = std::to_string(pf.first) + ":" + std::to_string(pos) + ":" + std::to_string(size);
            ti.push_back(addTex(key, CStr(b, names + U32(b, so + 8)) + "/" + std::to_string(pos), w, h, fmt,
                                Bytes(b.begin() + texOff + pos, b.begin() + texOff + pos + size)));
          }
          parts.u8((uint32_t)ti.size());
          for (int t : ti) parts.i16(t);
          for (int c = 0; c < 3; c++)
            for (int k = 0; k < 4; k++) parts.f32(F32(b, eo + 1072 + 16 * (size_t)c + 4 * (size_t)k));
        }
      }
    }
    Writer out;
    for (char c : std::string("XDMEGA01")) out.u8((uint8_t)c);
    out.u32(1);
    out.u32((uint32_t)texs.size());
    out.u32((uint32_t)modelFiles.size());
    out.u32(nsets);
    for (auto& t : texs) {
      out.str(t.name);
      out.u16((uint32_t)t.w);
      out.u16((uint32_t)t.h);
      out.b.insert(out.b.end(), t.px.begin(), t.px.end());
    }
    out.b.insert(out.b.end(), body.b.begin(), body.b.end());
    out.b.insert(out.b.end(), parts.b.begin(), parts.b.end());
    return out.b;
  } catch (const std::exception& e) {
    return no(std::string("the Mega Evolution effect could not be read: ") + e.what());
  }
}

}
