#include "pbr_model_io.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace pbr {
struct Tok {
  std::string text;
  size_t at = 0;
  std::string next() {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r')) at++;
    size_t s = at;
    while (at < text.size() && text[at] != ' ' && text[at] != '\n' && text[at] != '\r') at++;
    if (s == at) throw std::runtime_error("model file ended early");
    return text.substr(s, at - s);
  }
  long long i() { return std::stoll(next()); }
  double f() {
    uint64_t u = std::stoull(next(), nullptr, 16);
    double d;
    memcpy(&d, &u, 8);
    return d;
  }
  bool yes() { std::string t = next(); return t == "Y"; }
  std::vector<uint8_t> bytes(bool* none) {
    std::string t = next();
    *none = t == "-";
    std::vector<uint8_t> v;
    if (*none) return v;
    for (size_t k = 1; k + 2 <= t.size(); k += 2) v.push_back((uint8_t)std::stoi(t.substr(k, 2), nullptr, 16));
    return v;
  }
  Bytes ob() { bool none; auto v = bytes(&none); return none ? Bytes() : Bytes(v); }
  std::optional<int> oi() { if (!yes()) return std::nullopt; return (int)i(); }
  template <size_t N> std::array<double, N> fa() { std::array<double, N> a; for (auto& x : a) x = f(); return a; }
  template <size_t N> std::optional<std::array<double, N>> ofa() { if (!yes()) return std::nullopt; return fa<N>(); }
};

static std::optional<AnimObj> read_aobj(Tok& t) {
  if (!t.yes()) return std::nullopt;
  AnimObj a;
  a.flags = (uint32_t)t.i();
  a.end = t.f();
  long long nt = t.i();
  for (long long k = 0; k < nt; k++) {
    Track tr;
    tr.kind = (int)t.i();
    tr.start = t.f();
    if (t.yes()) { int a0 = (int)t.i(); int a1 = (int)t.i(); tr.src_format = std::make_pair(a0, a1); }
    long long nk = t.i();
    for (long long q = 0; q < nk; q++) {
      Key key;
      key.op = (int)t.i(); key.value = t.f(); key.slope = t.f(); key.wait = t.i();
      tr.keys.push_back(key);
    }
    a.tracks.push_back(std::move(tr));
  }
  return a;
}

Model model_parse(const std::string& text) {
  Tok t;
  t.text = text;
  Model m;
  long long n = t.i();
  for (long long k = 0; k < n; k++) {
    Joint j;
    j.parent = (int)t.i();
    if (t.yes()) j.flags = (uint32_t)t.i();
    j.rot = t.fa<3>(); j.scale = t.fa<3>(); j.pos = t.fa<3>();
    if (t.yes()) { auto a = t.fa<12>(); j.inv_bind = std::vector<double>(a.begin(), a.end()); }
    j.hidden = t.i() != 0;
    j.billboard = (int)t.i();
    m.joints.push_back(j);
  }
  m.skeleton_root = (int)t.i();
  n = t.i();
  for (long long k = 0; k < n; k++) {
    Mesh me;
    me.joint = (int)t.i();
    me.material = t.oi();
    me.rigid = t.i() != 0;
    me.pobj_flags = t.oi();
    long long nv = t.i();
    for (long long q = 0; q < nv; q++) {
      Vertex v;
      v.pos = t.fa<3>();
      v.nrm = t.ofa<3>();
      v.uv = t.ofa<2>();
      if (t.yes()) { long long c = t.i(); std::vector<int> col; for (long long z = 0; z < c; z++) col.push_back((int)t.i()); v.color = col; }
      if (t.yes()) {
        long long c = t.i();
        Weights w;
        for (long long z = 0; z < c; z++) { int j = (int)t.i(); double x = t.f(); w.emplace_back(j, x); }
        v.weights = w;
      }
      if (t.yes()) {
        v.has_uvx = true;
        long long c = t.i();
        for (long long z = 0; z < c; z++) v.uvx.push_back(t.ofa<2>());
      }
      me.verts.push_back(std::move(v));
    }
    long long nt = t.i();
    for (long long q = 0; q < nt; q++) { int a = (int)t.i(), b = (int)t.i(), c = (int)t.i(); me.tris.push_back({a, b, c}); }
    m.meshes.push_back(std::move(me));
  }
  n = t.i();
  for (long long k = 0; k < n; k++) {
    Material ma;
    ma.draw_class = t.next();
    ma.rendermode = (uint32_t)t.i();
    for (auto* c : {&ma.ambient, &ma.diffuse, &ma.specular}) { auto a = t.fa<4>(); c->assign(a.begin(), a.end()); }
    ma.alpha = t.f(); ma.shininess = t.f();
    long long nt = t.i();
    for (long long q = 0; q < nt; q++) {
      Texture x;
      x.image = t.oi(); x.tlut = t.oi();
      x.tex_id = (uint32_t)t.i(); x.src = (uint32_t)t.i();
      x.rot = t.fa<3>(); x.scale = t.fa<3>(); x.trans = t.fa<3>();
      x.wrap_s = (uint32_t)t.i(); x.wrap_t = (uint32_t)t.i(); x.repeat_s = (int)t.i(); x.repeat_t = (int)t.i();
      x.flags = (uint32_t)t.i(); x.blending = t.f(); x.mag_filter = (uint32_t)t.i();
      x.lod = t.ob(); x.tev = t.ob();
      ma.textures.push_back(std::move(x));
    }
    ma.pe = t.ob();
    m.materials.push_back(std::move(ma));
  }
  n = t.i();
  for (long long k = 0; k < n; k++) {
    Image im;
    im.width = (int)t.i(); im.height = (int)t.i(); im.format = (int)t.i(); im.mipmap = (int)t.i();
    im.min_lod = t.f(); im.max_lod = t.f();
    bool none;
    im.data = t.bytes(&none);
    m.images.push_back(std::move(im));
  }
  n = t.i();
  for (long long k = 0; k < n; k++) {
    Tlut tl;
    tl.format = (uint32_t)t.i(); tl.name = (uint32_t)t.i(); tl.count = (int)t.i();
    bool none;
    tl.data = t.bytes(&none);
    m.tluts.push_back(std::move(tl));
  }
  n = t.i();
  for (long long k = 0; k < n; k++) {
    Animation an;
    long long c = t.i();
    for (long long q = 0; q < c; q++) {
      int jx = (int)t.i();
      auto a = read_aobj(t);
      if (!a) throw std::runtime_error("joint animation that is None");
      an.joints.emplace_back(jx, std::move(*a));
    }
    c = t.i();
    for (long long q = 0; q < c; q++) { int jx = (int)t.i(); uint32_t v = (uint32_t)t.i(); an.joint_flags.emplace_back(jx, v); }
    c = t.i();
    for (long long q = 0; q < c; q++) {
      int jx = (int)t.i();
      if (!t.yes()) { an.materials.emplace_back(jx, std::nullopt); continue; }
      MatList ml;
      long long cnt = t.i();
      for (long long z = 0; z < cnt; z++) {
        if (!t.yes()) { ml.items.push_back(std::nullopt); continue; }
        MatAnim ma;
        ma.aobj = read_aobj(t);
        long long nta = t.i();
        for (long long y = 0; y < nta; y++) {
          TexAnim ta;
          ta.tex_id = (uint32_t)t.i();
          ta.aobj = read_aobj(t);
          long long ni = t.i();
          for (long long w = 0; w < ni; w++) ta.images.push_back((int)t.i());
          ni = t.i();
          for (long long w = 0; w < ni; w++) ta.tluts.push_back((int)t.i());
          ma.texanims.push_back(std::move(ta));
        }
        ml.items.push_back(std::move(ma));
      }
      an.materials.emplace_back(jx, std::move(ml));
    }
    if (t.yes()) {
      std::vector<std::array<double, 6>> bs;
      long long nb = t.i();
      for (long long z = 0; z < nb; z++) bs.push_back(t.fa<6>());
      an.bounds = std::move(bs);
    }
    m.animations.push_back(std::move(an));
  }
  if (t.yes()) {
    std::vector<int> bh;
    long long c = t.i();
    for (long long z = 0; z < c; z++) bh.push_back((int)t.i());
    m.bound_head = std::move(bh);
  }
  return m;
}

namespace {
struct Out {
  std::string t;
  void tok(const std::string& s) {
    if (!t.empty()) t += ' ';
    t += s;
  }
  void i(long long v) { tok(std::to_string(v)); }
  void f(double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    char b[24];
    snprintf(b, sizeof b, "%016llx", (unsigned long long)u);
    tok(b);
  }
  template <class C> void fs(const C& c) { for (double v : c) f(v); }
  void b(const Bytes& v) {
    if (!v) { tok("-"); return; }
    std::string s = "x";
    char h[4];
    for (uint8_t c : *v) { snprintf(h, sizeof h, "%02x", c); s += h; }
    tok(s);
  }
  void bytes(const std::vector<uint8_t>& v) { b(Bytes(v)); }
  void oi(const std::optional<long long>& v) { if (!v) tok("N"); else { tok("Y"); i(*v); } }
  template <class C> void ofs(const std::optional<C>& v) { if (!v) tok("N"); else { tok("Y"); fs(*v); } }
};

void write_aobj(Out& o, const std::optional<AnimObj>& a) {
  if (!a) { o.tok("N"); return; }
  o.tok("Y"); o.i(a->flags); o.f(a->end); o.i((long long)a->tracks.size());
  for (auto& t : a->tracks) {
    o.i(t.kind); o.f(t.start);
    if (!t.src_format) o.tok("N");
    else { o.tok("Y"); o.i(t.src_format->first); o.i(t.src_format->second); }
    o.i((long long)t.keys.size());
    for (auto& k : t.keys) { o.i(k.op); o.f(k.value); o.f(k.slope); o.i(k.wait); }
  }
}

std::optional<long long> L(const std::optional<int>& v) { if (!v) return std::nullopt; return (long long)*v; }
std::optional<long long> L(const std::optional<uint32_t>& v) { if (!v) return std::nullopt; return (long long)*v; }
}

Model model_load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  return model_parse(ss.str());
}

std::string model_save(const Model& m) {
  Out o;
  o.i((long long)m.joints.size());
  for (auto& j : m.joints) {
    o.i(j.parent); o.oi(L(j.flags));
    o.fs(j.rot); o.fs(j.scale); o.fs(j.pos);
    o.ofs(j.inv_bind);
    o.i(j.hidden ? 1 : 0); o.i(j.billboard);
  }
  o.i(m.skeleton_root);
  o.i((long long)m.meshes.size());
  for (auto& me : m.meshes) {
    o.i(me.joint); o.oi(L(me.material)); o.i(me.rigid ? 1 : 0); o.oi(L(me.pobj_flags));
    o.i((long long)me.verts.size());
    for (auto& v : me.verts) {
      o.fs(v.pos); o.ofs(v.nrm); o.ofs(v.uv);
      if (!v.color) o.tok("N");
      else { o.tok("Y"); o.i((long long)v.color->size()); for (int c : *v.color) o.i(c); }
      if (!v.weights) o.tok("N");
      else { o.tok("Y"); o.i((long long)v.weights->size()); for (auto& w : *v.weights) { o.i(w.first); o.f(w.second); } }
      if (!v.has_uvx) o.tok("N");
      else { o.tok("Y"); o.i((long long)v.uvx.size()); for (auto& x : v.uvx) o.ofs(x); }
    }
    o.i((long long)me.tris.size());
    for (auto& t : me.tris) { o.i(t[0]); o.i(t[1]); o.i(t[2]); }
  }
  o.i((long long)m.materials.size());
  for (auto& ma : m.materials) {
    o.tok(ma.draw_class);
    o.i(ma.rendermode); o.fs(ma.ambient); o.fs(ma.diffuse); o.fs(ma.specular);
    o.f(ma.alpha); o.f(ma.shininess);
    o.i((long long)ma.textures.size());
    for (auto& t : ma.textures) {
      o.oi(L(t.image)); o.oi(L(t.tlut)); o.i(t.tex_id); o.i(t.src);
      o.fs(t.rot); o.fs(t.scale); o.fs(t.trans);
      o.i(t.wrap_s); o.i(t.wrap_t); o.i(t.repeat_s); o.i(t.repeat_t); o.i(t.flags); o.f(t.blending); o.i(t.mag_filter);
      o.b(t.lod); o.b(t.tev);
    }
    o.b(ma.pe);
  }
  o.i((long long)m.images.size());
  for (auto& im : m.images) {
    o.i(im.width); o.i(im.height); o.i(im.format); o.i(im.mipmap); o.f(im.min_lod); o.f(im.max_lod); o.bytes(im.data);
  }
  o.i((long long)m.tluts.size());
  for (auto& t : m.tluts) { o.i(t.format); o.i(t.name); o.i(t.count); o.bytes(t.data); }
  o.i((long long)m.animations.size());
  for (auto& an : m.animations) {
    o.i((long long)an.joints.size());
    for (auto& kv : an.joints) { o.i(kv.first); write_aobj(o, kv.second); }
    o.i((long long)an.joint_flags.size());
    for (auto& kv : an.joint_flags) { o.i(kv.first); o.i(kv.second); }
    o.i((long long)an.materials.size());
    for (auto& kv : an.materials) {
      o.i(kv.first);
      if (!kv.second) { o.tok("N"); continue; }
      o.tok("Y"); o.i((long long)kv.second->items.size());
      for (auto& ma : kv.second->items) {
        if (!ma) { o.tok("N"); continue; }
        o.tok("Y"); write_aobj(o, ma->aobj); o.i((long long)ma->texanims.size());
        for (auto& ta : ma->texanims) {
          o.i(ta.tex_id); write_aobj(o, ta.aobj);
          o.i((long long)ta.images.size()); for (int x : ta.images) o.i(x);
          o.i((long long)ta.tluts.size()); for (int x : ta.tluts) o.i(x);
        }
      }
    }
    if (!an.bounds) o.tok("N");
    else { o.tok("Y"); o.i((long long)an.bounds->size()); for (auto& bx : *an.bounds) o.fs(bx); }
  }
  if (!m.bound_head) o.tok("N");
  else { o.tok("Y"); o.i((long long)m.bound_head->size()); for (int x : *m.bound_head) o.i(x); }
  return o.t + "\n";
}

}
