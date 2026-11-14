#include <algorithm>
#include <cmath>
#include <cstdio>

#include "pbr_eval.h"
#include "pbr_to_xd.h"

namespace pbr {

namespace {

template <class... A>
std::string sf(const char* f, A... a) {
  char buf[1024];
  snprintf(buf, sizeof buf, f, a...);
  return buf;
}

using M44 = std::array<double, 16>;

M44 four(const Mat& m) { return {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], 0, 0, 0, 1}; }
M44 four(const std::vector<double>& m) { return {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], 0, 0, 0, 1}; }

std::array<double, 4> mv4(const M44& a, const std::array<double, 4>& v) {
  std::array<double, 4> r;
  for (int i = 0; i < 4; i++) {
    double acc = 0.0;
    for (int k = 0; k < 4; k++) acc = std::fma(a[i * 4 + k], v[k], acc);
    r[i] = acc;
  }
  return r;
}
V3 mv3(const double* a, const V3& v) {
  V3 r;
  for (int i = 0; i < 3; i++) {
    double acc = 0.0;
    for (int k = 0; k < 3; k++) acc = std::fma(a[i * 3 + k], v[k], acc);
    r[i] = acc;
  }
  return r;
}
double norm3(const V3& v) { return std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]); }

V3 solve3(const double* a_, const V3& b_) {
  double a[3][3], b[3] = {b_[0], b_[1], b_[2]};
  for (int i = 0; i < 9; i++) a[i / 3][i % 3] = a_[i];
  for (int c = 0; c < 3; c++) {
    int p = c;
    for (int r = c + 1; r < 3; r++)
      if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
    if (a[p][c] == 0.0) throw std::runtime_error("Singular matrix");
    if (p != c) {
      for (int k = 0; k < 3; k++) std::swap(a[p][k], a[c][k]);
      std::swap(b[p], b[c]);
    }
    for (int r = c + 1; r < 3; r++) {
      a[r][c] = a[r][c] / a[c][c];
      for (int k = c + 1; k < 3; k++) a[r][k] -= a[r][c] * a[c][k];
      b[r] -= a[r][c] * b[c];
    }
  }
  for (int r = 2; r >= 0; r--) {
    for (int k = r + 1; k < 3; k++) b[r] -= a[r][k] * b[k];
    b[r] /= a[r][r];
  }
  return {b[0], b[1], b[2]};
}

}

void graft_xd_pieces(Model& m, const Model& xd, const std::vector<GraftRegion>& regions, int idle_slot, std::vector<std::string>& alpha,
                     const Logger& log, double step) {
  const Animation* xa = idle_slot < (int)xd.animations.size() ? &xd.animations[idle_slot] : nullptr;
  std::vector<M44> xw, ow;
  for (auto& w : pose_at(xd, xa, 0.0)) xw.push_back(four(w));
  for (auto& w : pose_at(m, idle_slot < (int)m.animations.size() ? &m.animations[idle_slot] : nullptr, 0.0)) ow.push_back(four(w));
  std::vector<Mat> world_static = m.world_static();
  Mat root_inv = mat_inv(world_static[1]);
  std::vector<int> dobj;
  std::map<int, int> count;
  for (auto& me : xd.meshes) {
    dobj.push_back(count.count(me.joint) ? count[me.joint] : 0);
    count[me.joint] = dobj.back() + 1;
  }
  auto xd_matanim = [&](const Animation& an, int k) -> const MatAnim* {
    for (auto& kv : an.materials)
      if (kv.first == xd.meshes[k].joint) {
        if (!kv.second) return nullptr;
        const auto& lst = kv.second->items;
        return (int)lst.size() > dobj[k] && lst[dobj[k]] ? &*lst[dobj[k]] : nullptr;
      }
    return nullptr;
  };
  std::vector<int> picked;
  for (size_t k = 0; k < xd.meshes.size(); k++) {
    const auto& me = xd.meshes[k];
    if (!me.material) continue;
    const Material& ma = xd.materials[*me.material];
    if (ma.textures.empty() || !(ma.rendermode & 0x40000000)) continue;
    bool any = false;
    for (auto& an : xd.animations) any = any || xd_matanim(an, (int)k);
    if (!any) continue;
    picked.push_back((int)k);
  }
  if (picked.empty()) {
    log("NOTE: no XD flame pieces found to copy");
    return;
  }
  std::map<int, int> img_map, tlut_map, mat_map;
  auto image = [&](int i) {
    if (!img_map.count(i)) {
      img_map[i] = (int)m.images.size();
      m.images.push_back(xd.images.at(i));
      alpha.push_back("xlu");
    }
    return img_map[i];
  };
  auto tlut = [&](int i) {
    if (!tlut_map.count(i)) {
      tlut_map[i] = (int)m.tluts.size();
      m.tluts.push_back(xd.tluts.at(i));
    }
    return tlut_map[i];
  };
  std::map<std::string, int> by_name;
  for (size_t i = 0; i < m.joints.size(); i++)
    if (!m.joints[i].name.empty()) by_name[m.joints[i].name] = (int)i;
  std::vector<double> hs;
  for (auto& r : regions)
    if (std::fabs(xw[r.xb[0]][7]) > 1e-3) hs.push_back(ow[by_name.at(r.pts[0])][7] / xw[r.xb[0]][7]);
  double ratio = 1.0;
  if (!hs.empty()) {
    std::sort(hs.begin(), hs.end());
    size_t n = hs.size();
    ratio = n % 2 ? hs[n / 2] : (hs[n / 2 - 1] + hs[n / 2]) / 2;
  }
  struct Place {
    V3 x0, y0;
    std::array<double, 9> rot;
    int qa, qb;
    V3 u;
    double ln;
  };
  std::vector<Place> places;
  std::map<int, int> region_of;
  for (auto& r : regions) {
    int qa = by_name.at(r.pts[0]);
    V3 x0 = {xw[r.xb[0]][3], xw[r.xb[0]][7], xw[r.xb[0]][11]}, y0 = {ow[qa][3], ow[qa][7], ow[qa][11]};
    std::array<double, 9> rot = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    int qb = -1;
    V3 u{0, 0, 0};
    double ln = 0;
    if (r.pts.size() == 2) {
      qb = by_name.at(r.pts[1]);
      V3 x1 = {xw[r.xb[1]][3], xw[r.xb[1]][7], xw[r.xb[1]][11]}, y1 = {ow[qb][3], ow[qb][7], ow[qb][11]};
      V3 dx = {x1[0] - x0[0], x1[1] - x0[1], x1[2] - x0[2]}, dy = {y1[0] - y0[0], y1[1] - y0[1], y1[2] - y0[2]};
      ln = norm3(dx);
      double nyl = norm3(dy);
      u = {dx[0] / ln, dx[1] / ln, dx[2] / ln};
      V3 v_ = {dy[0] / nyl, dy[1] / nyl, dy[2] / nyl};
      V3 ax = {u[1] * v_[2] - u[2] * v_[1], u[2] * v_[0] - u[0] * v_[2], u[0] * v_[1] - u[1] * v_[0]};
      double sn = norm3(ax);
      double cs = (u[0] * v_[0] + u[1] * v_[1]) + u[2] * v_[2];
      double k_[9] = {0, -ax[2], ax[1], ax[2], 0, -ax[0], -ax[1], ax[0], 0};
      double kk[9];
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
          double acc = 0.0;
          for (int q = 0; q < 3; q++) acc = std::fma(k_[i * 3 + q], k_[q * 3 + j], acc);
          kk[i * 3 + j] = acc;
        }
      double fac = sn > 1e-9 ? (1 - cs) / (sn * sn) : 0.0;
      for (int i = 0; i < 9; i++) rot[i] = (rot[i] + k_[i]) + kk[i] * fac;
    }
    places.push_back({x0, y0, rot, qa, qb, u, ln});
    for (int b : r.bones) region_of[b] = (int)places.size() - 1;
  }
  log(sf("flames: body %.2f x the XD size", ratio));
  int holder = (int)m.joints.size();
  Joint hj;
  hj.name = "xd_flames";
  hj.parent = 1;
  m.joints.push_back(hj);
  std::vector<int> mine;
  for (int k : picked) {
    const Mesh& me = xd.meshes[k];
    int matk = *me.material;
    if (!mat_map.count(matk)) {
      Material ma = xd.materials[matk];
      for (auto& t : ma.textures) {
        t.image = t.image ? std::optional<int>(image(*t.image)) : std::nullopt;
        t.tlut = t.tlut ? std::optional<int>(tlut(*t.tlut)) : std::nullopt;
      }
      ma.draw_class = "xlu";
      mat_map[matk] = (int)m.materials.size();
      m.materials.push_back(std::move(ma));
    }
    Mesh out;
    out.joint = holder;
    out.material = mat_map[matk];
    out.rigid = false;
    out.pobj_flags = me.pobj_flags;
    out.name = "xd_flame_" + std::to_string(k);
    for (const Vertex& v : me.verts) {
      std::array<double, 4> p = {v.pos[0], v.pos[1], v.pos[2], 1.0};
      std::array<double, 4> world;
      std::optional<V3> nw;
      std::vector<std::pair<int, double>> ws;
      if (me.rigid || !v.weights || v.weights->empty()) {
        world = mv4(xw[me.joint], p);
        if (v.nrm) {
          double r3[9] = {xw[me.joint][0], xw[me.joint][1], xw[me.joint][2], xw[me.joint][4], xw[me.joint][5], xw[me.joint][6],
                          xw[me.joint][8], xw[me.joint][9], xw[me.joint][10]};
          nw = mv3(r3, *v.nrm);
        }
        ws = {{me.joint, 1.0}};
      } else {
        double tw = 0;
        bool first = true;
        for (auto& jw : *v.weights) { tw = first ? jw.second : tw + jw.second; first = false; }
        if (tw == 0.0) tw = 1.0;
        std::array<double, 4> acc = {0, 0, 0, 0};
        V3 nacc = {0, 0, 0};
        bool f1 = true;
        for (auto& jw : *v.weights) {
          int j = jw.first;
          M44 ib = four(*xd.joints[j].inv_bind);
          M44 xi;
          for (int i = 0; i < 4; i++)
            for (int c = 0; c < 4; c++) {
              double a = 0.0;
              for (int q = 0; q < 4; q++) a = std::fma(xw[j][i * 4 + q], ib[q * 4 + c], a);
              xi[i * 4 + c] = a;
            }
          auto t = mv4(xi, p);
          double f = jw.second / tw;
          for (int c = 0; c < 4; c++) acc[c] = f1 ? f * t[c] : acc[c] + f * t[c];
          if (v.nrm) {
            double a3[9], b3[9], x3[9];
            for (int r = 0; r < 3; r++)
              for (int c = 0; c < 3; c++) { a3[r * 3 + c] = xw[j][r * 4 + c]; b3[r * 3 + c] = ib[r * 4 + c]; }
            for (int i = 0; i < 3; i++)
              for (int c = 0; c < 3; c++) {
                double a = 0.0;
                for (int q = 0; q < 3; q++) a = std::fma(a3[i * 3 + q], b3[q * 3 + c], a);
                x3[i * 3 + c] = a;
              }
            V3 t3 = mv3(x3, *v.nrm);
            for (int c = 0; c < 3; c++) nacc[c] = f1 ? jw.second * t3[c] : nacc[c] + jw.second * t3[c];
          }
          f1 = false;
        }
        world = acc;
        if (v.nrm) nw = nacc;
        for (auto& jw : *v.weights) {
          bool found = false;
          for (auto& e : ws)
            if (e.first == jw.first) { e.second = jw.second / tw; found = true; }
          if (!found) ws.emplace_back(jw.first, jw.second / tw);
        }
      }
      std::map<int, double> share;
      std::vector<int> share_order;
      for (auto& e : ws) {
        auto it = region_of.find(e.first);
        if (it == region_of.end()) throw std::runtime_error(sf("XD flame bone %d has no region in GRAFT_XD", e.first));
        if (!share.count(it->second)) { share[it->second] = 0.0; share_order.push_back(it->second); }
        share[it->second] = share[it->second] + e.second;
      }
      int best = share_order[0];
      for (int r : share_order)
        if (share[r] > share[best]) best = r;
      const Place& pl = places[best];
      V3 d = {world[0] - pl.x0[0], world[1] - pl.x0[1], world[2] - pl.x0[2]};
      V3 rd = mv3(pl.rot.data(), d);
      V3 moved = {pl.y0[0] + ratio * rd[0], pl.y0[1] + ratio * rd[1], pl.y0[2] + ratio * rd[2]};
      std::vector<std::pair<int, double>> ours;
      if (pl.qb < 0) {
        ours = {{pl.qa, 1.0}};
      } else {
        double f = std::min(std::max(((d[0] * pl.u[0] + d[1] * pl.u[1]) + d[2] * pl.u[2]) / pl.ln, 0.0), 1.0);
        ours = {{pl.qa, 1 - f}, {pl.qb, f}};
        if (pl.qa == pl.qb) ours = {{pl.qa, f}};
      }
      Weights wq = quantise_weights(ours, step);
      V3 wpos = moved;
      double a[16] = {0};
      for (auto& jw : wq) {
        int j = jw.first;
        if (!m.joints[j].inv_bind) {
          Mat r = mat_inv(mat_mul(root_inv, world_static[j]));
          m.joints[j].inv_bind = std::vector<double>(r.begin(), r.end());
        }
        M44 ibj = four(*m.joints[j].inv_bind);
        for (int i = 0; i < 4; i++)
          for (int c = 0; c < 4; c++) {
            double x = 0.0;
            for (int q = 0; q < 4; q++) x = std::fma(ow[j][i * 4 + q], ibj[q * 4 + c], x);
            a[i * 4 + c] = a[i * 4 + c] + jw.second * x;
          }
      }
      double a33[9] = {a[0], a[1], a[2], a[4], a[5], a[6], a[8], a[9], a[10]};
      V3 rhs = {wpos[0] - a[3], wpos[1] - a[7], wpos[2] - a[11]};
      V3 pos = solve3(a33, rhs);
      std::optional<V3> nrm;
      if (nw) {
        double at[9] = {a[0], a[4], a[8], a[1], a[5], a[9], a[2], a[6], a[10]};
        V3 nb = mv3(at, *nw);
        double nl = norm3(nb);
        if (nl == 0.0) nl = 1.0;
        nrm = V3{nb[0] / nl, nb[1] / nl, nb[2] / nl};
      }
      Vertex ov;
      ov.pos = pos;
      ov.nrm = nrm;
      ov.uv = v.uv;
      ov.color = v.color;
      ov.weights = wq;
      ov.has_uvx = v.has_uvx;
      ov.uvx = v.uvx;
      out.verts.push_back(std::move(ov));
    }
    out.tris = me.tris;
    m.meshes.push_back(std::move(out));
    mine.push_back(k);
  }
  for (size_t slot = 0; slot < m.animations.size(); slot++) {
    Animation& an = m.animations[slot];
    double e = 1.0;
    bool have = false;
    for (auto& kv : an.joints) {
      e = have ? std::max(e, kv.second.end) : kv.second.end;
      have = true;
    }
    int frames = std::max((int)std::nearbyint(e), 1);
    const Animation* src = slot < xd.animations.size() ? &xd.animations[slot] : nullptr;
    std::vector<std::optional<MatAnim>> lst;
    bool anyl = false;
    for (int k : mine) {
      const MatAnim* xm = src ? xd_matanim(*src, k) : nullptr;
      if (!xm) xm = xd_matanim(xd.animations.at(idle_slot), k);
      if (!xm) {
        lst.push_back(std::nullopt);
        continue;
      }
      MatAnim mm;
      for (const TexAnim& ta : xm->texanims) {
        TexAnim t;
        t.tex_id = ta.tex_id;
        for (int i : ta.images) t.images.push_back(image(i));
        for (int i : ta.tluts) t.tluts.push_back(tlut(i));
        AnimObj ao;
        ao.flags = ta.aobj->flags;
        ao.end = (double)frames;
        double end = std::max(ta.aobj->end, 1.0);
        for (const Track& tr : ta.aobj->tracks) {
          if (tr.keys.empty()) continue;
          std::vector<double> vals;
          for (int f = 0; f <= frames; f++) vals.push_back(track_value(tr, std::fmod((double)f, end)));
          Track nt;
          nt.kind = tr.kind;
          nt.keys = keys_of(vals, 1e-4);
          ao.tracks.push_back(std::move(nt));
        }
        t.aobj = ao;
        mm.texanims.push_back(std::move(t));
      }
      lst.push_back(std::move(mm));
      anyl = true;
    }
    if (anyl) {
      MatList ml;
      ml.items = std::move(lst);
      an.set_material(holder, std::move(ml));
      an.has_material_tree = true;
    }
  }
  int nv = 0;
  for (int k : picked) nv += (int)xd.meshes[k].verts.size();
  log(sf("INFO: XD flame pieces copied onto the PBR body: %d mesh(es), %d vertices", (int)mine.size(), nv));
}

namespace {

const std::map<std::string, std::vector<GraftRegion>>& graft_table() {
  static const std::map<std::string, std::vector<GraftRegion>> T = {
      {"ponyta",
       {{{"jnt3_1"}, {7}, {16, 17, 18}}, {{"jnt8_1"}, {20}, {29, 30, 31}}, {{"jnt4_1"}, {5}, {32, 33, 34, 35}},
        {{"jnt10_1"}, {39}, {47, 48}}, {{"jnt2_1"}, {49}, {57, 58}},
        {{"jnt4_2", "head"}, {37, 64}, {77, 78, 79, 80, 81, 99, 100, 101}}}},
      {"gallop",
       {{{"jnt1_1"}, {6}, {15, 16}}, {{"jnt9_1"}, {18}, {27, 28}}, {{"jnt13_1"}, {36}, {43, 44}}, {{"jnt14_1"}, {46}, {53, 54}},
        {{"jnt11_1"}, {4}, {30, 93, 94, 95, 96}},
        {{"jnt10_1", "jnt7_1"}, {2, 59}, {70, 4, 31, 32, 33, 34, 98, 99, 100, 101, 102}}}},
      {"fire",
       {{{"WL1j"}, {93}, {93}}, {{"WL2_J"}, {94}, {94}}, {{"WL3_J"}, {95}, {95}}, {{"WR1_J"}, {110}, {110}}, {{"WR2_J"}, {111}, {111}},
        {{"WR3_J"}, {112}, {112}}, {{"KOSIjnt"}, {2}, {2, 99, 100, 101, 102, 103, 104, 108}}}},
  };
  return T;
}

std::string report_repr(const WriteReport& r) {
  return sf("{'pobj': %lld, 'reflect_pieces': %lld, 'most_envelopes': %lld, 'display_list_bytes': %lld, 'track_bytes': %lld, "
            "'texture_bytes': %lld, 'archive_bytes': %lld}",
            r.pobj, r.reflect_pieces, r.most_envelopes, r.display_list_bytes, r.track_bytes, r.texture_bytes, r.archive_bytes);
}

}

ConvertFileResult convert_file(const std::vector<uint8_t>& sdr_bytes, const std::vector<uint8_t>& xd_bytes, const std::string& name,
                               const Logger& log, double step, const std::string& compensate, long long cap, const Gen4Info* gen4) {
  sdr::Sdr sdr(sdr_bytes);
  for (auto& w : sdr.warnings) log("NOTE: " + w);
  ReadResult xd = hsd_read_pkx(xd_bytes, false);
  std::optional<Model> xd_full;
  const std::vector<GraftRegion>* graft = nullptr;
  auto gi = graft_table().find(name);
  if (gi != graft_table().end()) {
    xd_full = hsd_read_pkx(xd_bytes, true).model;
    graft = &gi->second;
  }
  WriteReport report;
  auto build = [&](double tol_scale, bool quiet) {
    double st = step;
    Logger lg = quiet ? Logger([](const std::string&) {}) : log;
    std::optional<ConvertOut> co;
    WriteReport rep;
    while (true) {
      co = convert(sdr, xd.pkx, xd.model, st, lg, compensate, tol_scale, xd_full ? &*xd_full : nullptr, graft, gen4, std::nullopt, nullptr);
      hsd_write_archive(co->m, &rep);
      if (rep.pobj <= MAX_POBJ || st >= 0.34) break;
      log(sf("INFO: %d drawing pieces (%d with a reflection layer): skin weights redone in steps of %g", (int)rep.pobj,
             (int)rep.reflect_pieces, py_round(st + 0.05, 2)));
      st = py_round(st + 0.05, 2);
      lg = [](const std::string&) {};
    }
    Model& model = co->m;
    Pkx& pkx = co->pkx;
    fit_textures(model, model.image_classes, log);
    pkx.archive = hsd_write_archive(model, &rep);
    std::vector<uint8_t> data = pkx.build();
    if (cap && (long long)data.size() > cap) {
      long long tex = 0;
      for (int k : used_images(model)) tex += (long long)model.images[k].data.size();
      fit_textures(model, model.image_classes, log, std::max(tex - ((long long)data.size() - cap), 0LL));
      pkx.archive = hsd_write_archive(model, &rep);
      data = pkx.build();
    }
    return std::make_pair(data, rep);
  };
  auto res = build(1.0, false);
  for (double ts : {3.0, 10.0}) {
    if (!cap || (long long)res.first.size() <= cap) break;
    log(sf("INFO: %d bytes: animation keys thinned (tolerance x%g)", (int)res.first.size(), ts));
    res = build(ts, true);
  }
  if (cap && (long long)res.first.size() > cap)
    log(sf("NOTE: %d bytes after shrinking pictures and thinning keys: still over %d", (int)res.first.size(), (int)cap));
  log(sf("written %s: %d bytes (XD original %d, PBR file %d); %s", name.c_str(), (int)res.first.size(), (int)xd_bytes.size(),
         (int)sdr_bytes.size(), report_repr(res.second).c_str()));
  ConvertFileResult out;
  out.data = std::move(res.first);
  out.report = res.second;
  return out;
}

}
