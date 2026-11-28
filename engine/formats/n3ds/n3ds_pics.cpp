#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gf_internal.h"
#include "n3ds_assemble.h"
#include "n3ds_image.h"

using namespace n3ds;
namespace fs = std::filesystem;

static void save(const fs::path& p, const pbr::Rgba& r) {
  Image8 im;
  im.w = r.w; im.h = r.h; im.ch = 4; im.px = r.px;
  auto b = png_encode(im);
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)b.data(), (std::streamsize)b.size());
}

int main(int argc, char** argv) {
  try {
    if (argc < 4) { fprintf(stderr, "usage: n3ds_pics <dump> <group> <out folder>\n"); return 2; }
    RomFs r = open_dump(argv[1]);
    Detected d = detect(r);
    Garc g = garc_at(r, d.models);
    Species sp = load_species(g, atoi(argv[2]));
    fs::path out = argv[3];
    fs::create_directories(out);
    std::map<std::string, pbr::Rgba> texs, stex;
    for (auto& kv : sp.texs) { pbr::Rgba t(kv.second.w, kv.second.h); t.px = kv.second.px; texs[kv.first] = t; save(out / ("tex_" + kv.first + ".png"), t); }
    stex = texs;
    for (auto& kv : sp.shiny_texs) { pbr::Rgba t(kv.second.w, kv.second.h); t.px = kv.second.px; stex[kv.first] = t; save(out / ("shiny_" + kv.first + ".png"), t); }
    const GfModel& gm = sp.gm;
    FILE* tf = fopen((out / "materials.txt").string().c_str(), "w");
    for (auto& mt : gm.materials) {
      fprintf(tf, "material %s shader %s\n", mt.name.c_str(), mt.shader.c_str());
      for (auto& c : mt.coords) fprintf(tf, "   unit %d %s scale %.3f %.3f trans %.3f %.3f wrap %u %u\n", c.unit, c.name.c_str(), c.scale[0], c.scale[1], c.trans[0], c.trans[1], c.wrap[0], c.wrap[1]);
      fprintf(tf, "   colours:");
      for (int i = 0; i < 12; i++) fprintf(tf, " [%d %d %d %d]", mt.colors[i][0], mt.colors[i][1], mt.colors[i][2], mt.colors[i][3]);
      fprintf(tf, "\n");
    }
    Opts o;
    o.ground = true; o.fire = true; o.light = 1.0; o.eyes = true; o.size_rule = 2; o.size_flat = true; o.hover_model = true;
    auto plan = gfi::iris_plan(gm, texs, o);
    for (auto& e : plan) { fprintf(tf, "iris plan: eye %s <-", e.first.c_str()); for (auto& n : e.second) fprintf(tf, " %s", n.c_str()); fprintf(tf, "\n"); }
    int nat = gfi::species_of(gm);
    std::vector<pbr::Rgba> raw, rawsh;
    auto mi = gfi::make_images(gm, texs, o, nat, nullptr, &raw, true);
    auto ms = gfi::make_images(gm, stex, o, nat, nullptr, &rawsh, true);
    for (size_t k = 0; k < gm.materials.size(); k++) fprintf(tf, "material %s -> image %d (%s)\n", gm.materials[k].name.c_str(), mi.mat_img[k].first, mi.mat_img[k].second.c_str());
    for (auto& mp : sp.mots) {
      fprintf(tf, "motion %s frames %d\n", mp.first.c_str(), mp.second.frames);
      for (auto& v : mp.second.vis) { int on = 0; for (bool b : v.second) on += b ? 1 : 0; fprintf(tf, "   vis %s: %d of %zu frames shown (first %d)\n", v.first.c_str(), on, v.second.size(), v.second.empty() ? -1 : (int)v.second[0]); }
    }
    for (auto& me : gm.meshes) fprintf(tf, "mesh %s\n", me.name.c_str());
    fclose(tf);
    for (size_t i = 0; i < raw.size(); i++) save(out / ("img_" + std::to_string(i) + ".png"), raw[i]);
    for (size_t i = 0; i < rawsh.size(); i++) save(out / ("imgshiny_" + std::to_string(i) + ".png"), rawsh[i]);
    gfi::ShinyFit f;
    std::vector<double> w(raw.size(), 1.0);
    f = gfi::fit_shiny(raw, rawsh, w);
    printf("images %zu, shiny textures %zu, fit swap %d%d%d mod %d/%d/%d err %.2f none %.2f\n", raw.size(), sp.shiny_texs.size(), f.swap[0], f.swap[1], f.swap[2], f.mod[0], f.mod[1], f.mod[2], f.err_fit, f.err_none);
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
