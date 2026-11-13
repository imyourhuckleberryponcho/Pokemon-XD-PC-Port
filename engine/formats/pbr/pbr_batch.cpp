#include "pbr_batch.h"
#include "pbr_pack.h"

#include "pbr_gen4.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace pbr {

namespace {

uint32_t be32(const std::vector<uint8_t>& b, size_t o) {
  if (o + 4 > b.size()) throw std::runtime_error("unpack_from requires a buffer of at least " + std::to_string(o + 4) + " bytes");
  return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3];
}

std::string cstr_at(const std::vector<uint8_t>& data, size_t o) {
  if (o == 0 || o >= data.size()) return std::string();
  size_t e = o;
  while (e < data.size() && data[e] != 0) e++;
  if (e >= data.size()) throw std::runtime_error("subsection not found");
  return std::string(data.begin() + o, data.begin() + e);
}

std::string collapse(const std::string& s) {
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && isspace((unsigned char)s[i])) i++;
    size_t j = i;
    while (j < s.size() && !isspace((unsigned char)s[j])) j++;
    if (j > i) {
      if (!out.empty()) out += ' ';
      out += s.substr(i, j - i);
    }
    i = j;
  }
  return out;
}

std::string lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}
bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool ends(const std::string& s, const char* p) {
  size_t n = strlen(p);
  return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

}

std::vector<FsysEntry> fsys_entries(const std::vector<uint8_t>& data) {
  std::vector<FsysEntry> out;
  if (data.size() < 4 || memcmp(data.data(), "FSYS", 4) != 0) return out;
  uint32_t n = be32(data, 0x0C);
  uint32_t info = be32(data, 0x18);
  uint32_t table = be32(data, info);
  for (uint32_t k = 0; k < n; k++) {
    uint32_t h = be32(data, (size_t)table + 4 * k);
    uint32_t w[10];
    for (int i = 0; i < 10; i++) w[i] = be32(data, (size_t)h + 4 * i);
    FsysEntry e;
    e.index = (int)k;
    e.id = w[0] >> 16;
    e.offset = w[1];
    e.size = w[2];
    e.packed = (w[3] & 0x80000000u) != 0;
    e.packed_size = w[5];
    e.full = cstr_at(data, w[7]);
    e.type = w[8];
    e.name = cstr_at(data, w[9]);
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<uint8_t> lzss_unpack(const std::vector<uint8_t>& data, bool* ok) {
  if (data.size() < 4 || memcmp(data.data(), "LZSS", 4) != 0) {
    if (ok) *ok = false;
    return {};
  }
  if (ok) *ok = true;
  uint32_t total = be32(data, 4);
  std::vector<uint8_t> out(total, 0);
  std::vector<uint8_t> window(0x1000, 0);
  size_t wpos = 0xFEE, dpos = 0, i = 16, n = data.size();
  uint32_t flags = 0;
  auto at = [&](size_t k) -> uint8_t {
    if (k >= n) throw std::runtime_error("index out of range");
    return data[k];
  };
  while (dpos < total) {
    flags >>= 1;
    if (!(flags & 0x100)) {
      if (i >= n) break;
      flags = data[i] | 0xFF00;
      i++;
    }
    if (flags & 1) {
      uint8_t b = at(i);
      i++;
      out[dpos++] = b;
      window[wpos] = b;
      wpos = (wpos + 1) & 0xFFF;
    } else {
      uint8_t lo = at(i), hi = at(i + 1);
      i += 2;
      size_t off = lo | ((size_t)(hi & 0xF0) << 4);
      for (int k = 0; k < (hi & 0xF) + 3; k++) {
        if (dpos >= total) break;
        uint8_t c = window[(off + k) & 0xFFF];
        out[dpos++] = c;
        window[wpos] = c;
        wpos = (wpos + 1) & 0xFFF;
      }
    }
  }
  return out;
}

std::vector<uint8_t> fsys_extract(const std::vector<uint8_t>& data, const FsysEntry& e) {
  size_t len = e.packed ? e.packed_size : e.size;
  std::vector<uint8_t> s;
  if (e.offset < data.size()) s.assign(data.begin() + e.offset, data.begin() + std::min(data.size(), (size_t)e.offset + len));
  if (!e.packed) return s;
  bool ok;
  return lzss_unpack(s, &ok);
}

std::vector<PairRow> read_pairs(const DiscSource& pbr, const DiscSource& xd, std::vector<PairRow>* all) {
  struct XdModel { std::string arch; int idx; SharedBytes bytes; };
  std::map<std::string, XdModel> xdm;
  auto sorted_files = [](const DiscSource& d) {
    auto f = d.files();
    std::vector<std::string> p;
    for (auto& x : f) p.push_back(x.first);
    std::sort(p.begin(), p.end());
    return p;
  };
  for (const std::string& p : sorted_files(xd)) {
    std::string lp = lower(p);
    if (!(starts(lp, "pkx_") && ends(lp, ".fsys"))) continue;
    std::vector<uint8_t> data = xd.read(p);
    for (const FsysEntry& e : fsys_entries(data))
      if (e.type == 15) xdm[e.name] = {p, e.index, std::make_shared<const std::vector<uint8_t>>(fsys_extract(data, e))};
  }
  std::vector<PairRow> rows;
  std::set<std::string> seen;
  for (const std::string& p : sorted_files(pbr)) {
    if (!(starts(p, "pkx_") && ends(p, ".fsys"))) continue;
    std::vector<uint8_t> data = pbr.read(p);
    auto ents = fsys_entries(data);
    if (ents.empty()) continue;
    std::vector<uint8_t> b;
    std::string name;
    try {
      b = fsys_extract(data, ents[0]);
      uint32_t sk = be32(b, be32(b, 8));
      size_t o = be32(b, sk);
      size_t e = o;
      while (e < b.size() && b[e] != 0) e++;
      if (o > b.size() || e >= b.size()) continue;
      name.assign(b.begin() + o, b.begin() + e);
    } catch (const std::exception&) {
      continue;
    }
    auto it = xdm.find(name);
    SharedBytes sdr = std::make_shared<const std::vector<uint8_t>>(std::move(b));
    if (all) {
      PairRow r;
      r.name = name;
      r.sdr = sdr;
      r.pbr_archive = p;
      if (it != xdm.end()) { r.pkx = it->second.bytes; r.arch = it->second.arch; r.idx = it->second.idx; }
      all->push_back(std::move(r));
    }
    if (it != xdm.end() && !seen.count(name)) {
      seen.insert(name);
      PairRow r;
      r.name = name;
      r.sdr = sdr;
      r.pkx = it->second.bytes;
      r.arch = it->second.arch;
      r.idx = it->second.idx;
      r.pbr_archive = p;
      rows.push_back(std::move(r));
    }
  }
  return rows;
}

namespace {

struct JobResult {
  std::string name, state;
  long long size = 0;
  std::vector<std::string> lines;
  double secs = 0;
};

std::unique_ptr<DiscSource> open_checked(const DiscOpener& open_disc, const std::string& path, const char* want, const char* what) {
  std::error_code ec;
  if (!fs::is_regular_file(fs::u8path(path), ec)) throw Fail(std::string(what) + " disc image not found: " + path, 2);
  auto d = open_disc(path, what);
  if (d->game_id() != want)
    throw Fail(std::string("The ") + what + " disc image is " + d->game_id() + ", but this needs " + want + " (" +
                   (std::string(want) == PBR_ID ? "Pokemon Battle Revolution, USA" : "Pokemon XD, USA") + ").",
               2);
  return d;
}

void write_file(const fs::path& p, const void* data, size_t n) {
  std::ofstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + p.string());
  f.write((const char*)data, (std::streamsize)n);
  if (!f) throw std::runtime_error("cannot write " + p.string());
}

}

void run_models(const RunArgs& a, const DiscOpener& open_disc, const std::function<void(const std::string&)>& say,
                const XdTablesFactory& xd_tables) {
  if (!a.gen4_pack.empty() && !xd_tables) throw Fail("This program cannot make the Gen 4 pack (no reader for XD's tables).");
  fs::path out = fs::absolute(fs::u8path(a.out));
  fs::path tmp = out;
  tmp += ".tmp";
  std::vector<PairRow> rows, all_rows;
  const bool want_gen4 = !a.gen4_out.empty() || !a.gen4_pack.empty();
  std::unique_ptr<DiscSource> pbr_disc, xd_disc;
  {
    pbr_disc = open_checked(open_disc, a.pbr_disc, PBR_ID, "Battle Revolution");
    xd_disc = open_checked(open_disc, a.xd_disc, XD_ID, "XD");
    rows = read_pairs(*pbr_disc, *xd_disc, want_gen4 ? &all_rows : nullptr);
  }
  if (rows.empty()) throw Fail("No Battle Revolution model has an XD counterpart on these discs.");
  std::error_code ec;
  if (fs::is_directory(tmp, ec)) fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "fsys");
  int jobs = a.jobs ? a.jobs : std::max(1, std::min(8, (int)std::thread::hardware_concurrency() - 1));
  int total = (int)rows.size();
  jobs = std::min(jobs, total);
  std::map<std::string, JobResult> results;
  std::mutex mu;
  auto t0 = std::chrono::steady_clock::now();
  std::atomic<int> next(0);
  auto work = [&]() {
    while (true) {
      int k = next.fetch_add(1);
      if (k >= total) return;
      PairRow& row = rows[k];
      JobResult r;
      r.name = row.name;
      r.state = "converted";
      auto j0 = std::chrono::steady_clock::now();
      try {
        ConvertFileResult cr = convert_file(*row.sdr, *row.pkx, row.name, [&](const std::string& s) { r.lines.push_back(s); });
        r.size = (long long)cr.data.size();
        fs::path dst = tmp / "fsys" / fs::u8path(row.arch);
        fs::create_directories(dst);
        write_file(dst / ("#" + std::to_string(row.idx)), cr.data.data(), cr.data.size());
      } catch (const KeepXD& ex) {
        r.state = "kept_xd";
        r.size = 0;
        r.lines.push_back(std::string("REFUSED: ") + ex.what());
      } catch (const std::exception& ex) {
        r.state = "kept_xd";
        r.size = 0;
        r.lines.push_back(std::string("REFUSED: Error: ") + ex.what());
      }
      r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - j0).count();
      row.sdr.reset();
      row.pkx.reset();
      std::lock_guard<std::mutex> lk(mu);
      std::string nm = r.name;
      results[nm] = std::move(r);
      say("progress " + std::to_string(results.size()) + " " + std::to_string(total) + " " + nm);
    }
  };
  if (jobs <= 1) {
    work();
  } else {
    std::vector<std::thread> th;
    for (int i = 0; i < jobs; i++) th.emplace_back(work);
    for (auto& t : th) t.join();
  }
  int converted = 0;
  long long nbytes = 0;
  for (auto& kv : results)
    if (kv.second.state == "converted") {
      converted++;
      nbytes += kv.second.size;
    }
  int kept = total - converted;
  {
    std::string j = "{\n  \"id\": \"" + std::string(MOD_ID) + "\",\n  \"name\": \"Battle Revolution models\",\n  \"version\": \"" +
                    CONVERTER_VERSION +
                    "\",\n  \"author\": \"made on your PC from your own discs\",\n  \"description\": \"Pokemon Battle Revolution's "
                    "Pokemon models in Pokemon XD, converted from your own discs.\",\n  \"game\": \"" +
                    XD_ID + "\",\n  \"generated_from\": \"" + PBR_ID + "\",\n  \"applies\": \"restart\",\n  \"format\": 1\n}";
    write_file(tmp / "mod.json", j.data(), j.size());
  }
  {
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char head[256];
    snprintf(head, sizeof head, "pbr_models %s: %d models, %d converted, %d kept from XD, %lld bytes, %.0f s, %d jobs\n", CONVERTER_VERSION, total,
             converted, kept, nbytes, secs, jobs);
    std::string log = head;
    for (auto& kv : results) {
      const JobResult& r = kv.second;
      char h2[512];
      snprintf(h2, sizeof h2, "\n== %s: %s, %lld bytes, %.1f s\n", kv.first.c_str(), r.state.c_str(), r.size, r.secs);
      log += h2;
      for (auto& l : r.lines) log += l + "\n";
    }
    write_file(tmp / "conversion_log.txt", log.data(), log.size());
  }
  fs::path old = out;
  old += ".old";
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
  if (fs::is_directory(out, ec)) fs::rename(out, old);
  fs::rename(tmp, out);
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
  if (want_gen4) {
    std::vector<Gen4Source> src;
    for (auto& r : all_rows) {
      Gen4Source s;
      s.archive = r.pbr_archive;
      s.member = r.name;
      s.has_xd = r.pkx != nullptr;
      s.sdr = r.sdr;
      s.xd_pkx = r.pkx;
      src.push_back(std::move(s));
    }
    std::set<std::string> conv_names;
    for (auto& kv : results)
      if (kv.second.state == "converted") conv_names.insert(kv.first);
    const bool as_progress = !a.gen4_pack.empty();
    auto g4 = gen4_run(src, conv_names, jobs, [&](int d, int t, const std::string& n) {
      say(std::string(as_progress ? "progress " : "gen4 ") + std::to_string(d) + " " + std::to_string(t) + " " + n);
    });
    if (!a.gen4_pack.empty()) {
      std::map<std::string, int> dex_of;
      for (auto& r : all_rows)
        if (!r.pkx && r.pbr_archive.size() > 4 && r.pbr_archive.compare(0, 4, "pkx_") == 0) {
          const int d = std::atoi(r.pbr_archive.c_str() + 4);
          if (d >= 387 && d <= 493) dex_of[r.name.substr(0, r.name.find(':'))] = d;
        }
      std::vector<PackModel> models;
      for (auto& r : g4) {
        if (r.state != "converted" || !dex_of.count(r.name)) continue;
        PackModel m;
        m.dex = dex_of[r.name];
        m.name = r.name;
        m.standin = r.standin;
        m.data = r.data;
        models.push_back(std::move(m));
      }
      std::unique_ptr<XdTables> xdt = xd_tables(*xd_disc);
      xd_disc->activate();
      make_gen4_pack(a.gen4_pack, *pbr_disc, *xd_disc, *xdt, models, rows, say);
    }
    if (a.gen4_out.empty()) {
      say("done " + std::to_string(converted) + " " + std::to_string(kept) + " " + std::to_string(nbytes));
      return;
    }
    fs::path gout = fs::absolute(fs::u8path(a.gen4_out));
    fs::path gtmp = gout;
    gtmp += ".tmp";
    if (fs::is_directory(gtmp, ec)) fs::remove_all(gtmp, ec);
    fs::create_directories(gtmp);
    std::string glog;
    int gconv = 0;
    std::sort(g4.begin(), g4.end(), [](const Gen4Result& x, const Gen4Result& y) { return x.name < y.name; });
    for (auto& r : g4) {
      if (r.state == "converted") {
        gconv++;
        write_file(gtmp / (r.name + ".pkx"), r.data.data(), r.data.size());
      }
      char h2[512];
      snprintf(h2, sizeof h2, "\n== %s: %s, stand-in %s, %lld bytes, %.1f s\n", r.name.c_str(), r.state.c_str(), r.standin.c_str(),
               (long long)r.data.size(), r.secs);
      glog += h2;
      for (auto& l : r.lines) glog += l + "\n";
    }
    glog = "pbr_models " + std::string(CONVERTER_VERSION) + " Gen 4: " + std::to_string(g4.size()) + " models, " + std::to_string(gconv) +
           " converted\n" + glog;
    write_file(gtmp / "gen4_log.txt", glog.data(), glog.size());
    fs::path gold = gout;
    gold += ".old";
    if (fs::is_directory(gold, ec)) fs::remove_all(gold, ec);
    if (fs::is_directory(gout, ec)) fs::rename(gout, gold);
    fs::rename(gtmp, gout);
    if (fs::is_directory(gold, ec)) fs::remove_all(gold, ec);
    say("gen4done " + std::to_string(gconv) + " " + std::to_string(g4.size()));
  }
  say("done " + std::to_string(converted) + " " + std::to_string(kept) + " " + std::to_string(nbytes));
}

int pbr_models_main(const std::vector<std::string>& args, const DiscOpener& open_disc, const XdTablesFactory& xd_tables) {
  auto say = [](const std::string& s) {
    fputs(s.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
  };
  if (args.size() == 1 && args[0] == "--version") {
    say(std::string("version ") + CONVERTER_VERSION);
    return 0;
  }
  RunArgs a;
  bool have_pbr = false, have_xd = false, have_out = false, help = false, bad = false;
  for (size_t i = 0; i < args.size(); i++) {
    std::string k = args[i], v;
    bool has_v = false;
    size_t eq = k.find('=');
    if (starts(k, "--") && eq != std::string::npos) {
      v = k.substr(eq + 1);
      k = k.substr(0, eq);
      has_v = true;
    }
    if (k == "-h" || k == "--help") { help = true; continue; }
    auto take = [&](std::string& dst, bool& flag) {
      if (!has_v) {
        if (i + 1 >= args.size()) { bad = true; return; }
        v = args[++i];
      }
      dst = v;
      flag = true;
    };
    if (k == "--pbr-disc") take(a.pbr_disc, have_pbr);
    else if (k == "--xd-disc") take(a.xd_disc, have_xd);
    else if (k == "--out") take(a.out, have_out);
    else if (k == "--gen4-out") { bool f = false; take(a.gen4_out, f); }
    else if (k == "--gen4-pack") { bool f = false; take(a.gen4_pack, f); }
    else if (k == "--jobs") {
      std::string js;
      bool f = false;
      take(js, f);
      if (f) {
        char* end = nullptr;
        long n = strtol(js.c_str(), &end, 10);
        if (js.empty() || *end) bad = true;
        else a.jobs = (int)n;
      }
    } else bad = true;
  }
  if (help && !bad) return 0;
  if (bad || !have_pbr || !have_xd || !have_out) {
    say("error Wrong command line: use --pbr-disc, --xd-disc and --out.");
    return 1;
  }
  try {
    run_models(a, open_disc, say, xd_tables);
    return 0;
  } catch (const Fail& ex) {
    say("error " + collapse(ex.what()));
    return ex.code;
  } catch (const std::exception& ex) {
    say("error " + collapse(std::string("Error: ") + ex.what()));
    return 1;
  }
}

}
