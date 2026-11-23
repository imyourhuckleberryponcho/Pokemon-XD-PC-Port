#pragma once
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "n3ds_dump.h"
#include "n3ds_gf.h"

namespace n3ds_test {
using namespace n3ds;

inline uint32_t crc32(const uint8_t* p, size_t n) {
  static uint32_t tab[256];
  if (!tab[1])
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t v = i;
      for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      tab[i] = v;
    }
  uint32_t c = ~0u;
  for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255] ^ (c >> 8);
  return ~c;
}
inline uint32_t crcd(const std::vector<double>& v) { return crc32((const uint8_t*)v.data(), v.size() * 8); }
inline uint32_t crcch(const GfChannel& ch) {
  std::vector<double> v;
  for (auto& k : ch) { v.push_back(k.frame); v.push_back(k.value); v.push_back(k.slope); }
  return crcd(v);
}
inline uint32_t crc3(const std::array<double, 3>& a) { return crcd(std::vector<double>(a.begin(), a.end())); }

inline void dump_model(const GfModel& m) {
  printf("bones %zu\n", m.bones.size());
  for (auto& b : m.bones) printf("bone %s|%s|%d s:%08x r:%08x t:%08x\n", b.name.c_str(), b.parent.c_str(), b.flags, crc3(b.scale), crc3(b.rot), crc3(b.pos));
  printf("tex_names");
  for (auto& t : m.tex_names) printf(" %s", t.c_str());
  printf("\n");
  for (auto& mt : m.materials) {
    printf("mat %s|%s|%s|%s ", mt.name.c_str(), mt.shader.c_str(), mt.vtx_shader.c_str(), mt.frag_shader.c_str());
    for (int k = 0; k < 12; k++) printf("%02x%02x%02x%02x", mt.colors[k][0], mt.colors[k][1], mt.colors[k][2], mt.colors[k][3]);
    printf("\n");
    for (auto& c : mt.coords) {
      std::vector<double> v = {c.scale[0], c.scale[1], c.rot, c.trans[0], c.trans[1]};
      printf(" coord %s %d %d %08x %u %u\n", c.name.c_str(), c.unit, c.mapping, crcd(v), c.wrap[0], c.wrap[1]);
    }
  }
  for (auto& me : m.meshes) {
    printf("mesh %s\n", me.name.c_str());
    for (auto& s : me.subs) {
      printf(" sub %s bi", s.name.c_str());
      for (int b : s.bone_idx) printf(" %d", b);
      std::vector<double> ix(s.indices.begin(), s.indices.end());
      printf(" stride %d prim %d raw %zu:%08x idx %zu:%08x tri %zu\n", s.stride, s.prim, s.raw.size(), crc32(s.raw.data(), s.raw.size()),
             s.indices.size(), crcd(ix), triangles(s).size());
      for (auto& a : s.attrs) printf("  attr %s %d %d\n", a.name.c_str(), a.fmt, a.n);
      for (auto& f : s.fixed) printf("  fixed %s %08x\n", f.first.c_str(), crcd(std::vector<double>(f.second.begin(), f.second.end())));
      printf("  n %d\n", s.verts.n);
      for (auto& kv : s.verts.a) printf("  v %s %dx%d %08x\n", kv.first.c_str(), kv.second.rows, kv.second.cols, crcd(kv.second.v));
    }
  }
}

inline void dump_motion(const char* role, const GfMotion& mo) {
  printf("motion %s frames %d loop %d\n", role, mo.frames, (int)mo.loop);
  for (auto& b : mo.bones) {
    printf(" b %s %d", b.name.c_str(), (int)b.axis);
    for (int k = 0; k < 9; k++) printf(" %zu:%08x", b.ch[k].size(), crcch(b.ch[k]));
    printf("\n");
  }
  for (auto& v : mo.vis) {
    std::string s;
    for (bool x : v.second) s.push_back(x ? '1' : '0');
    printf(" vis %s %s\n", v.first.c_str(), s.c_str());
  }
  for (auto& c : mo.mats)
    printf(" mat %s %d %08x %08x %08x %08x %08x\n", c.name.c_str(), c.unit, crcch(c.sx), crcch(c.sy), crcch(c.rot), crcch(c.tx), crcch(c.ty));
}

inline void dump_species(const RomFs& r, int group) {
  Detected d = detect(r);
  Garc g = garc_at(r, d.models);
  size_t base = 1 + 9 * (size_t)group;
  GfModel gm = parse_model(pc_split(lz11_maybe(g.read(base)))[0]);
  dump_model(gm);
  for (auto& t : pc_split(lz11_maybe(g.read(base + 1)))) {
    GfTexture tx = parse_texture(t);
    Rgba px = decode_texture(tx);
    printf("tex %s %d %d %d %d %zu %08x\n", tx.name.c_str(), tx.w, tx.h, tx.fmt, tx.mip, tx.data.size(), crc32(px.px.data(), px.px.size()));
  }
  auto subs = pc_split(lz11_maybe(g.read(base + 4)));
  for (size_t slot = 0; slot < subs.size(); slot++) {
    if (subs[slot].empty()) continue;
    GfMotion mo;
    bool ok = false;
    try {
      ok = parse_motion(subs[slot], mo);
    } catch (const std::exception&) {
      printf("slot %zu error\n", slot);
      continue;
    }
    if (!ok) {
      printf("slot %zu none\n", slot);
      continue;
    }
    char nm[32];
    snprintf(nm, sizeof nm, "slot%zu", slot);
    dump_motion(nm, mo);
  }
}
}
