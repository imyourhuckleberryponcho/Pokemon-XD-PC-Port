#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pbr_sdr.h"

using namespace pbr;
using namespace pbr::sdr;

static std::string D(double x) {
  uint64_t u;
  memcpy(&u, &x, 8);
  char b[24];
  snprintf(b, sizeof b, "%016llx", (unsigned long long)u);
  return b;
}
static std::string hex(const uint8_t* p, size_t n) {
  std::string s;
  char b[4];
  for (size_t i = 0; i < n; i++) { snprintf(b, sizeof b, "%02x", p[i]); s += b; }
  return s;
}
static std::string esc(const std::string& s) {
  std::string o;
  char b[8];
  for (unsigned char c : s) {
    if (c >= 0x20 && c < 0x7f && c != 0x5c) o += (char)c;
    else { snprintf(b, sizeof b, "\\x%02x", c); o += b; }
  }
  return o;
}
static std::string fnv(const std::vector<uint8_t>& v) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (uint8_t c : v) h = (h ^ c) * 0x100000001b3ULL;
  char b[24];
  snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
  return b;
}
static std::string I(long long v) { return std::to_string(v); }

static void curve(std::string& o, const Curve& c) {
  o += " curve " + I(c.what) + " " + I(c.axis) + " " + I(c.fps) + " nv " + I((long long)c.values.size());
  for (double v : c.values) o += " " + D(v);
  o += "\n  slopes " + I((long long)c.slopes.size());
  for (double v : c.slopes) o += " " + D(v);
  o += "\n";
  if (!c.has_keys) {
    o += "  keys none\n";
  } else {
    o += "  keys " + I((long long)c.keys.size());
    for (auto& k : c.keys) o += " " + I(k.interp) + "," + I(k.value) + "," + I(k.slope_in) + "," + I(k.slope_out) + "," + D(k.time);
    o += "\n";
  }
}
static void anim(std::string& o, const Anim& a, const char* tag) {
  for (auto& e : a.items) {
    o += std::string(" ") + tag + " act " + I(e.first) + " n " + I((long long)e.second.size()) + "\n";
    for (auto& c : e.second) curve(o, c);
  }
}
static void dl(std::string& o, const double* v, int n) {
  for (int i = 0; i < n; i++) o += (i ? " " : "") + D(v[i]);
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: pbr_sdr_dump <model.sdr>\n"); return 2; }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
  std::vector<uint8_t> b;
  static uint8_t buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
  fclose(f);
  try {
    Sdr s(std::move(b));
    std::string o;
    o += "name " + esc(s.name) + "\n";
    for (auto& w : s.warnings) o += "warn " + esc(w) + "\n";
    for (size_t i = 0; i < s.actions.size(); i++)
      o += "action " + I((long long)i) + " " + esc(s.actions[i].name) + " " + D(s.actions[i].f[0]) + " " + D(s.actions[i].f[1]) + " " + D(s.actions[i].f[2]) + "\n";
    for (size_t i = 0; i < s.textures.size(); i++) {
      auto& t = s.textures[i];
      o += "tex " + I((long long)i) + " " + I(t.width) + " " + I(t.height) + " code " + I(t.code) + " fmt " + I(t.format) + " wrap " + I(t.wrap_s) + " " + I(t.wrap_t) +
           " pal " + I(t.palette_format) + " " + I(t.palette_at) + " levels " + I(t.levels) + " len " + I((long long)t.data.size()) + " fnv " + fnv(t.data) + "\n";
    }
    for (size_t i = 0; i < s.materials.size(); i++) {
      auto& m = s.materials[i];
      o += "mat " + I((long long)i) + " " + esc(m.name) + " tex " + I(m.texture) + " amb " + I(m.ambient[0]) + "," + I(m.ambient[1]) + "," + I(m.ambient[2]) + "," + I(m.ambient[3]) +
           " dif " + I(m.diffuse[0]) + "," + I(m.diffuse[1]) + "," + I(m.diffuse[2]) + "," + I(m.diffuse[3]) + " raw " + hex(m.raw.data(), m.raw.size()) + "\n";
      for (size_t k = 0; k < m.layers.size(); k++) {
        auto& L = m.layers[k];
        o += " layer " + I((long long)k) + " tex " + I(L.texture) + " uv " + I(L.uv) + " uvset " + I(L.uvset) + " off " + D(L.offset[0]) + " " + D(L.offset[1]) + " rot " + D(L.rotation) +
             " scale " + D(L.scale[0]) + " " + D(L.scale[1]) + " raw " + hex(L.raw, 0x18) + "\n";
      }
      for (auto& e : m.layer_anims) {
        o += " manim layer " + I(e.first) + "\n";
        anim(o, e.second, "mblock");
      }
      o += " mfirst " + I(m.layer_anims.empty() ? -1 : m.layer_anims[0].first) + "\n";
    }
    for (size_t i = 0; i < s.nodes.size(); i++) {
      auto& nd = s.nodes[i];
      o += "node " + I((long long)i) + " parent " + I(nd.parent) + " kind " + I(nd.kind) + " " + esc(nd.name) + " num " + I(nd.number) + " flags " + I(nd.flags) + " pos ";
      dl(o, nd.pos, 3); o += " rot "; dl(o, nd.rot, 3); o += " scale "; dl(o, nd.scale, 3); o += "\n pivots ";
      if (nd.has_pivots) dl(o, nd.pivots, 12); else o += "-";
      o += "\n joint " + I(nd.jflags) + " orient "; dl(o, nd.orient, 3); o += " inv ";
      if (nd.has_inv_bind) dl(o, nd.inv_bind, 12); else o += "-";
      o += "\n";
      anim(o, nd.anim, "nanim");
      if (nd.has_mesh) {
        auto& me = nd.mesh;
        o += " mesh nv " + I((long long)me.verts.size()) + "\n";
        for (auto& v : me.verts) { o += "  v "; dl(o, v.p, 3); o += " "; dl(o, v.n, 3); o += "\n"; }
        o += " uvsets " + I((long long)me.uvsets.size()) + "\n";
        for (size_t k = 0; k < me.uvsets.size(); k++) {
          o += "  uvset " + I((long long)k) + " n " + I((long long)me.uvsets[k].size()) + "\n";
          for (auto& u : me.uvsets[k]) o += "   uv " + D(u.first) + " " + D(u.second) + "\n";
        }
        if (!me.has_weights) {
          o += " weights none\n";
        } else {
          o += " weights " + I((long long)me.weights.size()) + "\n";
          for (auto& d : me.weights) {
            o += "  w " + I((long long)d.size());
            for (auto& e : d) o += " " + I(e.first) + ":" + D(e.second);
            o += "\n";
          }
        }
        o += " parts " + I((long long)me.parts.size()) + "\n";
        for (auto& p : me.parts) {
          o += "  part mat " + I(p.material) + " tris " + I((long long)p.tris.size()) + "\n";
          for (auto& t : p.tris) {
            o += "   t";
            for (int c = 0; c < 3; c++) {
              auto& q = t.c[c];
              o += " " + I(q.pos) + "," + I(q.nrm) + "," + I(q.uv0) + "," + I(q.uv1) + "," + I(q.uv2);
            }
            o += "\n";
          }
        }
      }
    }
    o += "by_number " + I((long long)s.by_number.size());
    for (auto& e : s.by_number) o += " " + I(e.first) + ":" + I(e.second);
    o += "\n";
    fwrite(o.data(), 1, o.size(), stdout);
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
