#include <cstdio>
#include <cstring>
#include <fstream>
#include <cinttypes>
#include <iterator>
#include <string>
#include <vector>

#include <algorithm>
#include <set>

#include "pbr_eval.h"
#include "pbr_hsd_read.h"
#include "pbr_model_io.h"
#include "pbr_to_xd.h"

using namespace pbr;

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + p);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  try {
    if (argc >= 3 && !strcmp(argv[1], "hsd")) {
      bool meshes = !(argc >= 4 && !strcmp(argv[3], "nomesh"));
      ReadResult r = hsd_read_pkx(slurp(argv[2]), meshes);
      std::string t = model_save(r.model);
      fwrite(t.data(), 1, t.size(), stdout);
      for (auto& w : r.warnings) fprintf(stderr, "warning: %s\n", w.c_str());
      return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "pose")) {
      ReadResult r = hsd_read_pkx(slurp(argv[2]), false);
      auto pr = [](const std::vector<Mat>& w) {
        for (auto& m : w) {
          for (double v : m) { uint64_t u; memcpy(&u, &v, 8); printf("%016" PRIx64 " ", u); }
          printf("\n");
        }
      };
      pr(pose_at(r.model, nullptr, 0.0));
      for (auto& an : r.model.animations) {
        double e = an.end();
        for (double f : {0.0, 1.0, 2.5, e / 2, e - 0.25, e}) {
          printf("frame %.17g\n", f);
          pr(pose_at(r.model, &an, f));
        }
      }
      return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "eval")) {
      sdr::Sdr sdr(slurp(argv[2]));
      Eval ev(sdr);
      auto dumpd = [](const char* label, const std::vector<double>& v) {
        printf("%s %zu\n", label, v.size());
        for (double d : v) { uint64_t u; memcpy(&u, &d, 8); printf("%016" PRIx64 "\n", u); }
      };
      std::vector<int> acts = {-1};
      int na = (int)sdr.actions.size();
      for (int a : {0, 1, 2, na - 1})
        if (a >= 0 && a < na && std::find(acts.begin(), acts.end(), a) == acts.end()) acts.push_back(a);
      for (int a : acts) {
        printf("action %d\n", a);
        Pose p = ev.sample(a);
        dumpd("local", p.local);
        dumpd("rot_only", p.rot_only);
        dumpd("scale", p.scale);
        dumpd("dcomp", p.dcomp);
        std::vector<double> vv(p.visible.begin(), p.visible.end());
        dumpd("visible", vv);
        std::vector<double> cc(p.compensated.begin(), p.compensated.end());
        dumpd("compensated", cc);
        std::vector<double> w = ev.world(p);
        dumpd("world", w);
        auto pts = ev.points(w, p.frames, p.visible.data());
        for (auto& ps : pts) dumpd("points", ps.pts);
        if (!pts.empty()) dumpd("boxes", boxes(pts, p.frames));
        Decomp d0 = decompose(p, ev, {});
        dumpd("dec_s", d0.s); dumpd("dec_r", d0.r); dumpd("dec_t", d0.t);
        std::set<int> pairs;
        for (int i = 0; i < p.n && pairs.size() < 2; i++)
          if (p.compensated[i]) pairs.insert(i);
        Decomp d1 = decompose(p, ev, pairs);
        dumpd("pair_s", d1.s); dumpd("pair_r", d1.r); dumpd("pair_t", d1.t);
        dumpd("compose", compose_local(d1, p.n, pairs));
        dumpd("shear", shear_error(p, ev));
      }
      return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "convdump")) {
      std::string sp = argv[2], xp = sp;
      size_t p1 = xp.rfind("pbr"), p2 = xp.rfind(".sdr");
      xp = xp.substr(0, p1) + "xd" + xp.substr(p1 + 3, p2 - p1 - 3) + ".pkx";
      sdr::Sdr sdr(slurp(sp));
      ReadResult xd = hsd_read_pkx(slurp(xp), false);
      double step = argc >= 4 ? atof(argv[3]) : 0.1;
      ConvertInfo inf;
      auto co = convert(sdr, xd.pkx, xd.model, step, [](const std::string&) {}, "joint8_under_joint", 1.0, nullptr, nullptr, nullptr,
                        std::nullopt, &inf);
      std::string t = model_save(co->m);
      fwrite(t.data(), 1, t.size(), stdout);
      std::vector<uint8_t> hdr = co->pkx.header;
      for (auto& s : co->pkx.sections) hdr.insert(hdr.end(), s.begin(), s.end());
      for (uint8_t b : hdr) printf("%02x", b);
      printf("\n");
      return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "conv")) {
      std::string name = argv[4];
      size_t sl = name.find_last_of("/\\");
      if (sl != std::string::npos) name = name.substr(sl + 1);
      size_t dot = name.rfind('.');
      if (dot != std::string::npos) name = name.substr(0, dot);
      ConvertFileResult r = convert_file(slurp(argv[2]), slurp(argv[3]), name, [](const std::string& s) { fprintf(stderr, "%s\n", s.c_str()); });
      std::ofstream f(argv[4], std::ios::binary);
      f.write((const char*)r.data.data(), (std::streamsize)r.data.size());
      printf("%s %zu\n", name.c_str(), r.data.size());
      return 0;
    }
    fprintf(stderr, "usage: pbr_tool hsd <model.pkx> [nomesh]\n");
    return 2;
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
