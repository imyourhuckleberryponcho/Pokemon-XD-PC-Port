#ifdef _WIN32
#define NOMINMAX
#endif
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#ifndef _WIN32
#include <fcntl.h>
#endif
#include <chrono>
#include <cstdio>
#include <set>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>

#include "n3ds_assemble.h"
#include "n3ds_data.h"
#include "n3ds_faces.h"
#include "n3ds_plan.h"

#if !defined(_WIN32) && !defined(__ANDROID__)
extern char** environ;
#endif

using namespace n3ds;
namespace fs = std::filesystem;

#ifdef __ANDROID__
#include <functional>
std::function<int(const std::string& name, const std::vector<std::string>& args, std::string& output)> g_usum_builtin_run;
#endif

namespace {

std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot read " + p);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void spit(const fs::path& p, const void* d, size_t n) {
  fs::create_directories(p.parent_path());
  fs::path tmp = p;
  tmp += ".part";
  {
    std::ofstream o(tmp, std::ios::binary);
    o.write((const char*)d, (std::streamsize)n);
  }
  fs::rename(tmp, p);
}

void be32(std::vector<uint8_t>& o, size_t at, uint32_t v) {
  o[at] = (uint8_t)(v >> 24);
  o[at + 1] = (uint8_t)(v >> 16);
  o[at + 2] = (uint8_t)(v >> 8);
  o[at + 3] = (uint8_t)v;
}
size_t align(size_t x, size_t a) { return (x + a - 1) / a * a; }

std::vector<uint8_t> build_fsys(uint32_t archive_id, uint32_t res, const std::string& name, const std::vector<uint8_t>& data) {
  const size_t n = 1, table = 0x60;
  size_t names_at = align(table + 4 * n, 16);
  size_t hdrs_at = align(names_at + name.size() + 1, 16);
  size_t data_at = align(hdrs_at + 0x70 * n, 32);
  size_t pos = data_at + align(data.size(), 32);
  size_t total = pos + 0x20;
  std::vector<uint8_t> out(total, 0);
  memcpy(&out[0], "FSYS", 4);
  uint32_t w[8] = {0x201, archive_id, (uint32_t)n, 0x80000000u, 3, 0x40, (uint32_t)data_at, (uint32_t)total};
  for (int i = 0; i < 8; i++) be32(out, 4 + 4 * i, w[i]);
  be32(out, 0x40, (uint32_t)table);
  be32(out, 0x44, (uint32_t)names_at);
  be32(out, 0x48, (uint32_t)data_at);
  memcpy(&out[names_at], name.c_str(), name.size() + 1);
  size_t h = hdrs_at;
  be32(out, table, (uint32_t)h);
  uint32_t hd[10] = {res, (uint32_t)data_at, (uint32_t)data.size(), 0, 0, (uint32_t)data.size(), 0, 0, 15, (uint32_t)names_at};
  for (int i = 0; i < 10; i++) be32(out, h + 4 * i, hd[i]);
  memcpy(&out[data_at], data.data(), data.size());
  memcpy(&out[total - 4], "FSYS", 4);
  return out;
}

std::string jesc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n' || c == '\r' || c == '\t') o += ' ';
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

std::mutex out_mu;
void say(const std::string& s) {
  std::lock_guard<std::mutex> g(out_mu);
  printf("%s\n", s.c_str());
  fflush(stdout);
}

std::vector<PlanItem> read_plan(const std::string& p) {
  std::ifstream f(p);
  if (!f) throw std::runtime_error("cannot read the plan " + p);
  std::vector<PlanItem> v;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> c;
    std::stringstream ss(line);
    std::string x;
    while (std::getline(ss, x, '\t')) c.push_back(x);
    if (c.size() < 7) throw std::runtime_error("a plan line has fewer than 7 columns: " + line);
    PlanItem it;
    it.pack = "plan";
    it.name = c[0];
    it.group = atoi(c[1].c_str());
    it.standin_path = c[2];
    it.height = atof(c[3].c_str());
    it.ladder = atoi(c[4].c_str());
    it.gid = (uint32_t)atoll(c[5].c_str());
    it.res = (uint32_t)atoll(c[6].c_str());
    it.model = c.size() > 7 ? c[7] : std::string("g57");
    v.push_back(std::move(it));
  }
  return v;
}

void write_plan(const std::string& path, const std::vector<PlanItem>& items) {
  std::ofstream f(path);
  for (auto& it : items) {
    if (!it.skip.empty()) { f << "# skipped " << it.name << ": " << it.skip << "\n"; continue; }
    const char* kind = it.pack == "usum" ? "xd" : it.pack == "gen4" ? "g4" : "g57";
    f << it.name << '\t' << it.group << '\t' << "xd:" << it.standin_nat << '\t' << it.height << '\t' << it.ladder << '\t' << it.gid << '\t' << it.res << '\t' << kind << '\n';
  }
}

struct Ctx {
  const Garc* g;
  const XdSide* xd;
  std::string out;
  bool legacy;
  std::string dump_fp;
  std::string usum_dir, g57_dir, g4src_dir, rep_dir;
};

std::string shortest(double v) {
  char b[64];
  auto r = std::to_chars(b, b + sizeof b, v);
  std::string s(b, r.ptr);
  if (s.find_first_of(".eE") == std::string::npos) s += ".0";
  return s;
}

struct Made {
  size_t bytes = 0;
  bool skipped = false;
  std::string report;
};

fs::path model_path(const Ctx& c, const PlanItem& it) {
  fs::path o = c.out;
  if (c.legacy) return o / "files" / ("pkx_" + it.name + ".fsys");
  const fs::path ud = c.usum_dir.empty() ? o / "usum" : fs::path(c.usum_dir), gd = c.g57_dir.empty() ? o / "gen57" : fs::path(c.g57_dir);
  if (it.pack == "usum") return ud / "fsys" / it.arch / ("#" + std::to_string(it.idx));
  if (it.pack == "gen4") return ud / "fsys" / ("pkx_g4_" + it.model + ".fsys") / "#0";
  return gd / "files" / ("pkx_" + it.name + ".fsys");
}
fs::path report_path(const Ctx& c, const PlanItem& it) {
  fs::path o = c.out;
  if (c.legacy) return o / "reports" / (it.name + ".json");
  if (it.pack == "usum" || it.pack == "gen4") return (c.rep_dir.empty() ? o / "usum" / "reports" : fs::path(c.rep_dir)) / (it.name + ".json");
  return (c.g57_dir.empty() ? o / "gen57" : fs::path(c.g57_dir)) / "reports" / (it.name + ".json");
}

Made make_one(const Ctx& c, const PlanItem& it) {
  Made m;
  const auto t_start = std::chrono::steady_clock::now();
  fs::path fpath = model_path(c, it), rpath = report_path(c, it);
  if (fs::exists(fpath) && fs::exists(rpath)) {
    m.skipped = true;
    m.bytes = (size_t)fs::file_size(fpath);
    return m;
  }
  char rep[4096];
  try {
    if (!it.skip.empty()) throw std::runtime_error(it.skip);
    Species sp = load_species(*c.g, it.group, it.motion_group);
    std::vector<uint8_t> xd;
    if (!it.standin_path.empty()) {
      if (it.standin_path.compare(0, 3, "xd:") == 0) xd = *c.xd->pkx_of(atoi(it.standin_path.c_str() + 3));
      else xd = slurp(it.standin_path);
    } else {
      xd = *c.xd->pkx_of(it.standin_nat);
    }
    ConvertInfo info;
    std::vector<uint8_t> pkx;
    std::string kind = it.pack == "usum" ? "xd" : it.pack == "gen4" ? "g4" : it.pack == "plan" ? it.model : "g57";
    std::vector<std::string> logv;
    if (kind == "xd") pkx = convert_one(sp, xd, opts_xd_species(), &info, &logv);
    else if (kind == "g4") pkx = convert_one(sp, xd, opts_gen4(it.height), &info, &logv);
    else pkx = convert_ladder(sp, xd, it.height, it.ladder, &info);
    const std::vector<uint8_t>* out = &pkx;
    std::vector<uint8_t> fsys;
    if (it.pack == "g57" || it.pack == "form" || it.pack == "plan") {
      fsys = build_fsys(it.gid, it.res, it.name, pkx);
      out = &fsys;
    }
    spit(fpath, out->data(), out->size());
    if (it.pack == "gen4") spit((c.g4src_dir.empty() ? fs::path(c.out) / "gen4src" : fs::path(c.g4src_dir)) / "models" / (it.model + ".pkx"), pkx.data(), pkx.size());
    std::string roles = "{";
    for (size_t ri = 0; ri < sp.mots.size(); ri++) roles += (ri ? ", " : "") + std::string("\"") + sp.mots[ri].first + "\": " + std::to_string(sp.mots[ri].second.frames);
    roles += "}";
    snprintf(rep, sizeof rep, "{\"tris\": %d, \"size\": %zu, \"group\": %d, \"step\": %d, \"scale\": %s, \"gid\": %u, \"nat\": %d, \"xd_size\": %zu, \"secs\": %.1f, \"roles\": %s}", info.tris, info.size, it.group,
             info.step, shortest(info.scale).c_str(), it.gid, it.nat, xd.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count(), roles.c_str());
    m.bytes = out->size();
    m.report = rep;
    if (!logv.empty()) {
      m.report.pop_back();
      m.report += ", \"log\": [";
      for (size_t li = 0; li < logv.size(); li++) m.report += (li ? ", \"" : "\"") + jesc(logv[li]) + "\"";
      m.report += "]}";
    }
  } catch (const std::exception& e) {
    std::string msg = jesc(e.what());
    snprintf(rep, sizeof rep, "{\"error\": \"%.300s\", \"group\": %d, \"nat\": %d}", msg.c_str(), it.group, it.nat);
    m.report = rep;
    m.bytes = 0;
    spit(rpath, rep, strlen(rep));
    throw std::runtime_error(msg);
  }
  spit(rpath, m.report.data(), m.report.size());
  return m;
}

void read_model_results(const std::string& out, const std::string& data_dir, const std::vector<PlanItem>& items, ModelResults& mr) {
  const pbr::Json mnj = pbr::Json::parse(std::string((std::istreambuf_iterator<char>(*std::make_unique<std::ifstream>(fs::path(data_dir) / "gen57_model_numbers.json", std::ios::binary))), std::istreambuf_iterator<char>()));
  for (const PlanItem& it : items) {
    if (it.pack != "g57" && it.pack != "form") continue;
    fs::path rp = fs::path(out) / "gen57" / "reports" / (it.name + ".json");
    if (it.pack == "form" && !it.skip.empty()) {
      pbr::Json j = pbr::Json::object();
      j.set("status", pbr::Json(it.skip == "nogroup" || it.skip == "noshell" ? it.skip : "error"));
      if (j.find("status")->s == "error") j.set("error", pbr::Json(it.skip));
      mr.forms[it.name] = j;
      continue;
    }
    if (!fs::exists(rp)) continue;
    pbr::Json r = pbr::Json::parse(std::string((std::istreambuf_iterator<char>(*std::make_unique<std::ifstream>(rp, std::ios::binary))), std::istreambuf_iterator<char>()));
    if (it.pack == "g57") {
      if (r.find("error")) { pbr::Json e = pbr::Json::object(); e.set("error", *r.find("error")); e.set("model_number", pbr::Json(0)); mr.species[it.nat] = e; continue; }
      pbr::Json m = pbr::Json::object();
      for (const char* k : {"tris", "size", "group"}) m.set(k, *r.find(k));
      { const pbr::Json* v = mnj.find("model_number")->find(std::to_string(it.nat)); m.set("model_number", pbr::Json(it.nat < 650 ? it.nat : (v->kind == pbr::Json::Str ? atoi(v->s.c_str()) : (int)v->i))); }
      m.set("gid", *r.find("gid"));
      mr.species[it.nat] = m;
    } else {
      pbr::Json m = pbr::Json::object();
      if (r.find("error")) { m.set("status", pbr::Json("error")); m.set("error", *r.find("error")); }
      else {
        const int64_t sz = r.find("size")->i;
        m.set("status", pbr::Json(sz > 614400 ? "overcap" : "ok"));
        for (const char* k : {"tris", "size", "group"}) m.set(k, *r.find(k));
      }
      mr.forms[it.name] = m;
    }
  }
}

#include "usum_full_part.inc"

}

int main(int argc, char** argv) {
  std::string rom, plan, out, xd_disc, data_dir, write_plan_path, stage = "models";
  std::string packs = "usum,gen4,g57,form", only;
  int jobs = 0, lo = 0, hi = 100;
  bool plan_only = false, data_only = false, eol_crlf = false, no_models = false;
  std::string gen4_pack, pack_out, work, data_mods, dex_out, pbr_models_exe, gen4_values = "on", gen57_pack, xd_exe, data_folder, pictures_out, usum_py, gen57_only, gen57_skip;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--rom") rom = next();
    else if (a == "--plan") plan = next();
    else if (a == "--out") out = next();
    else if (a == "--xd-disc") xd_disc = next();
    else if (a == "--data") data_dir = next();
    else if (a == "--packs") packs = next();
    else if (a == "--only") only = next();
    else if (a == "--write-plan") write_plan_path = next();
    else if (a == "--plan-only") plan_only = true;
    else if (a == "--gen4-pack") gen4_pack = next();
    else if (a == "--dex-out") dex_out = next();
    else if (a == "--gen57-pack") gen57_pack = next();
    else if (a == "--xd-exe") xd_exe = next();
    else if (a == "--data-folder") data_folder = next();
    else if (a == "--pictures-out") pictures_out = next();
    else if (a == "--usum-models") usum_py = next();
    else if (a == "--gen57-only") gen57_only = next();
    else if (a == "--gen57-skip") gen57_skip = next();
    else if (a == "--decimate") next();
    else if (a == "--pbr-models") pbr_models_exe = next();
    else if (a == "--gen4-values") gen4_values = next();
    else if (a == "--pack-out") pack_out = next();
    else if (a == "--work") work = next();
    else if (a == "--data-mods-exe") data_mods = next();
    else if (a == "--data-only") data_only = true;
    else if (a == "--no-models") no_models = true;
    else if (a == "--eol") eol_crlf = next() == "crlf";
    else if (a == "--jobs") jobs = atoi(next().c_str());
    else if (a == "--stage") stage = next();
    else if (a == "--percent") { lo = atoi(next().c_str()); hi = atoi(next().c_str()); }
    else if (a == "--version") { printf("version %s\n", kConverterVersion); return 0; }
  }
  try {
    if (rom.empty() || out.empty() || (plan.empty() && xd_disc.empty()))
      throw Fail("usage: usum_models_cpp --rom <dump> --xd-disc <XD image> --out <folder> [--data <json folder>] [--jobs N]   (or --plan <plan.tsv> --out <pack>)");
    if (plan.empty() && !data_only && (!gen57_pack.empty() || !gen4_pack.empty())) {
      FullArgs fa;
      fa.rom = rom; fa.xd_disc = xd_disc; fa.out = out; fa.gen4_pack = gen4_pack; fa.gen57_pack = gen57_pack; fa.xd_exe = xd_exe; fa.data_folder = data_folder;
      fa.pictures_out = pictures_out; fa.data_mods_exe = data_mods; fa.pbr_models = pbr_models_exe; fa.data_dir = data_dir; fa.gen57_only = gen57_only;
      fa.gen57_skip = gen57_skip; fa.only = only; fa.gen4_values = gen4_values; fa.jobs = jobs; fa.eol_crlf = eol_crlf;
      std::string self = argv[0];
      fs::path selfdir = fs::absolute(fs::path(self)).parent_path();
#ifdef _WIN32
      const char* pyname = "usum_models.exe";
#else
      const char* pyname = "usum_models";
#endif
      fa.usum_models_py = !usum_py.empty() ? usum_py : (selfdir / pyname).string();
      if (fa.data_dir.empty() && fs::exists(selfdir / "usum_gen4_plan.json")) fa.data_dir = selfdir.string();
      if (fa.data_mods_exe.empty()) {
#ifdef _WIN32
        const char* dm = "xd_data_mods.exe";
#else
        const char* dm = "xd_data_mods";
#endif
        for (const fs::path& cand : {selfdir / dm, selfdir.parent_path() / "pbr_models" / dm, selfdir.parent_path() / dm})
          if (fa.data_mods_exe.empty() && fs::is_regular_file(cand)) fa.data_mods_exe = cand.string();
      }
      return run_full(fa);
    }
    if (jobs <= 0) jobs = (int)std::max(1u, std::thread::hardware_concurrency());
    RomFs r = open_dump(rom);
    Detected d = detect(r);
    Garc g = garc_at(r, d.models);
    Ctx c{&g, nullptr, out, !plan.empty(), ""};
    std::unique_ptr<pbr::DiscSource> disc;
    std::unique_ptr<XdSide> xd;
    std::vector<PlanItem> items;
    int span = hi - lo;
    if (!plan.empty()) {
      items = read_plan(plan);
      if (!xd_disc.empty()) { disc = open_iso(xd_disc); xd.reset(new XdSide(*disc)); c.xd = xd.get(); }
      for (auto& it : items) if (it.standin_path.compare(0, 3, "xd:") == 0 && !c.xd) throw Fail("a plan line asks for the XD image (--xd-disc)");
      fs::create_directories(fs::path(out) / "files");
      fs::create_directories(fs::path(out) / "reports");
    } else {
      say("stage plan " + std::to_string(lo) + " " + std::to_string(lo + span / 20));
      disc = open_iso(xd_disc);
      if (disc->game_id() != "GXXE01") throw Fail("the XD image is not the XD disc (GXXE01)", 2);
      xd.reset(new XdSide(*disc));
      c.xd = xd.get();
      PlanOptions po;
      po.rom = rom;
      if (data_dir.empty()) {
        for (const char* cand : {"tools/n3ds", "../tools/n3ds", "data", "."}) if (fs::exists(fs::path(cand) / "usum_gen4_plan.json")) { data_dir = cand; break; }
        if (data_dir.empty()) throw Fail("the plan lists (usum_gen4_plan.json ...) were not found: give --data <folder>");
      }
      po.data_dir = data_dir;
      po.packs.clear();
      std::stringstream ps(packs);
      std::string x;
      while (std::getline(ps, x, ',')) po.packs.insert(x);
      std::stringstream os(only);
      while (std::getline(os, x, ',')) if (!x.empty()) po.only.insert(atoi(x.c_str()));
      po.log = [](const std::string& s) { say(s); };
      c.dump_fp = dump_fingerprint(rom);
      Plan p = make_plan(r, d, g, *disc, po);
      items = std::move(p.items);
      if (!write_plan_path.empty()) write_plan(write_plan_path, items);
      if (plan_only) { say("done 0 0 0"); return 0; }
      if (data_only) {
        DataOptions dopt;
        dopt.rom = rom; dopt.xd_disc = xd_disc; dopt.data_dir = data_dir; dopt.gen4_pack = gen4_pack; dopt.out = pack_out;
        dopt.models_dir = out; dopt.work = work.empty() ? out + "/work" : work; dopt.data_mods_exe = data_mods; dopt.eol_crlf = eol_crlf;
        dopt.only = po.only;
        dopt.log = [](const std::string& s) { say(s); };
        p.items = items;
        ModelResults mr;
        mr.present = !no_models;
        if (mr.present) read_model_results(out, data_dir, items, mr);
        if (!gen4_pack.empty()) {
          Gen4SourceOptions g4;
          g4.rom = rom; g4.data_dir = data_dir; g4.src = out + "/gen4src"; g4.values_on = gen4_values != "off"; g4.eol_crlf = eol_crlf;
          for (auto& it : items) {
            if (it.pack != "gen4" || !fs::exists(fs::path(g4.src) / "models" / (it.model + ".pkx"))) continue;
            g4.plan_lines.push_back(std::to_string(it.nat) + "	" + it.model + "	" + std::to_string(it.standin_nat));
          }
          std::sort(g4.plan_lines.begin(), g4.plan_lines.end(), [](const std::string& a, const std::string& b) { return atoi(a.c_str()) < atoi(b.c_str()); });
          write_gen4_source(g4, [](const std::string& l) { say(l); });
          if (!pbr_models_exe.empty()) {
            std::string cmd = "\"" + pbr_models_exe + "\" --gen4-source \"" + g4.src + "\" --xd-disc \"" + xd_disc + "\" --gen4-pack \"" + gen4_pack + "\"";
#ifdef _WIN32
            cmd = "\"" + cmd + "\"";
#endif
            if (std::system(cmd.c_str()) != 0) throw Fail("the Gen 4 pack builder (pbr_models) stopped");
          }
        }
        say("stage data " + std::to_string(lo) + " " + std::to_string(hi));
        make_gen57_data(dopt, p, mr);
        make_gen57_forms_data(dopt, p, mr);
        if (!dex_out.empty()) make_dex_entries(rom, dex_out, [](const std::string& l) { say(l); });
        say("done 0 0 0");
        return 0;
      }
      fs::create_directories(out);
    }
    struct Stage { const char* id; std::vector<const PlanItem*> list; };
    std::vector<Stage> stages = {{"base", {}}, {"models", {}}, {"forms", {}}};
    if (!plan.empty()) stages = {{stage.c_str(), {}}};
    for (auto& it : items) {
      if (!plan.empty()) { stages[0].list.push_back(&it); continue; }
      if (it.pack == "usum" || it.pack == "gen4") stages[0].list.push_back(&it);
      else if (it.pack == "g57") stages[1].list.push_back(&it);
      else stages[2].list.push_back(&it);
    }
    size_t all = 0;
    for (auto& s : stages) all += s.list.size();
    std::atomic<long long> bytes{0}, failed{0}, made{0};
    size_t acc = 0;
    int from = plan.empty() ? lo + span / 20 : lo;
    std::vector<std::string> failures;
    std::mutex fail_mu;
    for (auto& st : stages) {
      if (st.list.empty()) continue;
      int a = from + (int)((double)(hi - from) * (double)acc / (double)std::max<size_t>(all, 1));
      acc += st.list.size();
      int b = from + (int)((double)(hi - from) * (double)acc / (double)std::max<size_t>(all, 1));
      say(std::string("stage ") + st.id + " " + std::to_string(a) + " " + std::to_string(b));
      std::atomic<size_t> next{0}, done{0};
      size_t total = st.list.size();
      auto work = [&]() {
        while (true) {
          size_t k = next.fetch_add(1);
          if (k >= total) return;
          const PlanItem& it = *st.list[k];
          try {
            Made m = make_one(c, it);
            bytes += (long long)m.bytes;
            made++;
          } catch (const std::exception& e) {
            failed++;
            std::lock_guard<std::mutex> gl(fail_mu);
            failures.push_back(it.name + ": " + e.what());
          }
          size_t dn = ++done;
          say("progress " + std::to_string(dn) + " " + std::to_string(total) + " " + it.name);
        }
      };
      std::vector<std::thread> th;
      for (int i = 0; i < jobs; i++) th.emplace_back(work);
      for (auto& t : th) t.join();
    }
    if (plan.empty()) {
      long long nusum = 0;
      for (auto& it : items) if ((it.pack == "usum" || it.pack == "gen4") && fs::exists(model_path(c, it))) nusum++;
      std::ostringstream mj;
      mj << "{\"id\": \"ultra-sun-moon-models\", \"name\": \"Ultra Sun / Ultra Moon models\", \"version\": \"0.2.3\", \"author\": \"made on your PC from your own 3DS dump\", "
         << "\"description\": \"Pokemon Ultra Sun / Ultra Moon's Pokemon models in Pokemon XD, converted from your own dump.\", \"dump\": \"" << c.dump_fp
         << "\", \"game\": \"GXXE01\", \"generated_from\": \"" << jesc(r.product) << "\", \"applies\": \"restart\", \"format\": 1, \"count\": " << nusum
         << ", \"unit\": \"model\", \"units\": \"models\"}";
      if (fs::exists(fs::path(out) / "usum")) { std::string s = mj.str(); spit(fs::path(out) / "usum" / "mod.json", s.data(), s.size()); }
      std::ostringstream rj;
      rj << "{\"converter\": \"usum_models_cpp 0.2.3\", \"failed\": [";
      for (size_t i = 0; i < failures.size(); i++) rj << (i ? ", " : "") << "\"" << jesc(failures[i]) << "\"";
      rj << "], \"made\": " << (long long)made << ", \"bytes\": " << (long long)bytes << "}";
      std::string s = rj.str();
      spit(fs::path(out) / "usum_cpp_report.json", s.data(), s.size());
    }
    say("done " + std::to_string((long long)made) + " " + std::to_string((long long)failed) + " " + std::to_string((long long)bytes));
    return 0;
  } catch (const Fail& e) {
    say(std::string("error ") + e.what());
    return e.code == 2 ? 2 : 1;
  } catch (const pbr::Fail& e) {
    say(std::string("error ") + e.what());
    return e.code == 2 ? 2 : 1;
  } catch (const std::exception& e) {
    say(std::string("error ") + e.what());
    return 1;
  }
}
