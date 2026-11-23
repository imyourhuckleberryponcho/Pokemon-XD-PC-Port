#ifdef _WIN32
#define NOMINMAX
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#include "detmath.h"
#include "n3ds_assemble.h"
#include "n3ds_faces.h"
#include "n3ds_image.h"
#include "n3ds_plan.h"
#include "n3ds_pose.h"
#include "pbr_hsd_read.h"
#include "pbr_model.h"

#ifndef _WIN32
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace n3ds {

namespace {

double hand_share(int nat) {
  static const int whole[] = {517, 518, 524, 535, 546, 550, 562, 568, 577, 602, 607, 608, 609, 611, 618, 626, 710, 712, 748, 761, 764, 773, 779, 780, 788, 790, 805};
  for (int n : whole) if (n == nat) return 1.0;
  if (nat == 590 || nat == 591) return 0.6;
  return 0.0;
}

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
bool has(const std::string& low, const char* w) { return low.find(w) != std::string::npos; }

bool thin_name(const std::string& name) {
  const std::string l = lower(name);
  static const char* W[] = {"ear", "feeler", "horn", "hair", "antenn", "crest", "flame", "fire", "leaf", "petal", "plume", "tentacle", "whisk", "tail", "wing", "spike", "mane",
                            "tuft", "ribbon", "bud", "hat", "cap", "hood", "smoke", "ember", "beard", "tusk", "stem", "vine", "branch"};
  for (const char* w : W) if (has(l, w)) return true;
  for (size_t p = l.find("fin"); p != std::string::npos; p = l.find("fin", p + 1)) {
    size_t e = p + 3;
    if (e >= l.size() || !(isalnum((unsigned char)l[e]) || l[e] == '_')) return true;
  }
  return false;
}

int head_bone(const Skeleton& sk) {
  for (int pat = 0; pat < 4; pat++)
    for (int i = 0; i < sk.nb; i++) {
      const std::string l = lower(sk.names[(size_t)i]);
      bool m = pat == 0 ? l == "head" : pat == 1 ? l.compare(0, 4, "head") == 0 : pat == 2 ? has(l, "head") : l.compare(0, 4, "neck") == 0;
      if (m && !has(l, "tail") && !has(l, "headband")) return i;
    }
  return -1;
}

std::set<int> subtree(const Skeleton& sk, int root, bool thin) {
  std::set<int> out = {root};
  for (int i = 0; i < sk.nb; i++) {
    std::vector<int> chain;
    int j = i;
    bool reached = false;
    while (j >= 0) {
      chain.push_back(j);
      if (j == root) { reached = true; break; }
      j = sk.parent[(size_t)j];
    }
    if (!reached) continue;
    bool skip = false;
    if (thin) for (size_t c = 0; c + 1 < chain.size(); c++) if (thin_name(sk.names[(size_t)chain[c]])) { skip = true; break; }
    if (skip) continue;
    out.insert(i);
  }
  return out;
}

std::vector<V3> head_points(const GfModel& gm, const std::set<int>& bones) {
  std::vector<V3> pts;
  for (auto& me : gm.meshes)
    for (auto& sm : me.subs) {
      const GfVerts& v = sm.verts;
      if (!v.has("pos")) continue;
      const Arr& pos = v.get("pos");
      std::vector<int> tab(sm.bone_idx.begin(), sm.bone_idx.end());
      tab.resize(tab.size() + 256, 0);
      if (!v.has("bidx")) {
        if (bones.count(tab[0])) for (int i = 0; i < pos.rows; i++) pts.push_back({pos.at(i, 0), pos.at(i, 1), pos.at(i, 2)});
        continue;
      }
      const Arr& bi = v.get("bidx");
      const Arr* bw = v.has("bw") ? &v.get("bw") : nullptr;
      for (int i = 0; i < pos.rows; i++) {
        double share = 0.0, tot = 0.0;
        for (int k = 0; k < bi.cols; k++) {
          double w = (bw && k < bw->cols) ? bw->at(i, k) : 0.0;
          size_t idx = (size_t)(long long)bi.at(i, k);
          int b = idx < tab.size() ? tab[idx] : 0;
          share += bones.count(b) ? w : 0.0;
        }
        if (bw) for (int k = 0; k < std::min(bi.cols, bw->cols); k++) tot += bw->at(i, k);
        if (share >= 0.5 * std::max(tot, 1e-9)) pts.push_back({pos.at(i, 0), pos.at(i, 1), pos.at(i, 2)});
      }
    }
  return pts;
}

double np_percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  const double vi = (double)(v.size() - 1) * (q / 100.0);
  size_t prev = (size_t)std::floor(vi), next = std::min(prev + 1, v.size() - 1);
  double g = vi - (double)prev, a = v[prev], b = v[next], d = b - a;
  double r = a + d * g;
  if (g >= 0.5) r = b - d * (1 - g);
  return r;
}

std::vector<int> dfs_order_of(const Skeleton& sk) {
  std::map<int, std::vector<int>> kids;
  for (int i = 0; i < sk.nb; i++) kids[sk.parent[(size_t)i]].push_back(i);
  std::vector<int> out;
  std::function<void(int)> go = [&](int i) { out.push_back(i); for (int c : kids[i]) go(c); };
  for (int r : kids[-1]) go(r);
  return out;
}

}

double framing_share(int nat) { return hand_share(nat); }

Bytes fsys_pkx(const Bytes& f) {
  auto be = [&](size_t o) { if (o + 4 > f.size()) throw std::runtime_error("fsys cut"); return (uint32_t)f[o] << 24 | (uint32_t)f[o + 1] << 16 | (uint32_t)f[o + 2] << 8 | f[o + 3]; };
  if (f.size() < 0x70 || memcmp(f.data(), "FSYS", 4) != 0) throw std::runtime_error("not an fsys");
  const uint32_t n = be(0x0C), table = be(0x40);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t h = be(table + 4 * i);
    if (be(h + 0x20) == 15) {
      const uint32_t off = be(h + 4), len = be(h + 8);
      if ((size_t)off + len > f.size()) throw std::runtime_error("fsys entry cut");
      return Bytes(f.begin() + off, f.begin() + off + len);
    }
  }
  throw std::runtime_error("no model in the fsys");
}

std::string view_key(int nat, const std::string& file_hash, bool wide) {
  const double sh = hand_share(nat);
  std::string fr = "[]";
  if (sh != 0.0) fr = sh == 1.0 ? "[('share', 1.0)]" : "[('share', 0.6)]";
  const std::string s = file_hash + "|" + fr + "|" + (wide ? "w" : "f") + "|1";
  return sha1_hex((const uint8_t*)s.data(), s.size()).substr(0, 20);
}

bool face_view(const GfModel& gm, const Bytes& pkx_bytes, int nat, bool wide, FaceView* out) {
  const bool whole = wide;
  const double fscale = wide ? 2.2 : 1.0;
  Skeleton sk(gm.bones);
  int hb = head_bone(sk);
  std::vector<V3> pts;
  if (hb >= 0 && !whole) {
    pts = head_points(gm, subtree(sk, hb, true));
    if (pts.size() < 20) pts = head_points(gm, subtree(sk, hb, false));
  }
  bool how_top;
  if (pts.size() < 20) {
    hb = 1;
    for (int i = 0; i < sk.nb; i++) if (sk.names[(size_t)i] == "Waist") { hb = i; break; }
    std::set<int> all;
    for (int i = 0; i < sk.nb; i++) all.insert(i);
    pts = head_points(gm, all);
    if (pts.size() < 20) return false;
    const double top = whole ? 1.0 : 0.30;
    double mn = 1e300, mx = -1e300;
    for (auto& p : pts) { mn = std::min(mn, p[1]); mx = std::max(mx, p[1]); }
    std::vector<V3> keep;
    for (auto& p : pts) if (p[1] >= mn + (1.0 - top) * (mx - mn)) keep.push_back(p);
    pts = keep;
    if (pts.size() < 20) return false;
    how_top = true;
  } else {
    how_top = false;
  }
  double lo[3], hi[3], ctr[3];
  for (int c = 0; c < 3; c++) {
    std::vector<double> col;
    col.reserve(pts.size());
    for (auto& p : pts) col.push_back(p[(size_t)c]);
    lo[c] = np_percentile(col, 5);
    hi[c] = np_percentile(col, 95);
    ctr[c] = (lo[c] + hi[c]) * 0.5;
  }
  const double half = std::max(std::max(hi[0] - lo[0], hi[1] - lo[1]), 0.8 * (hi[2] - lo[2])) * 0.5;
  pbr::ReadResult rr = pbr::hsd_read_pkx(pkx_bytes, false);
  const pbr::Model& m = rr.model;
  std::vector<pbr::Mat> wm = m.world_static();
  if (wm.size() < 2) throw std::runtime_error("the model has no root joint");
  const pbr::Mat& root = wm[1];
  const double sf = std::sqrt((root[0] * root[0] + root[4] * root[4]) + root[8] * root[8]);
  double c[3];
  for (int i = 0; i < 3; i++) c[i] = ((root[(size_t)i * 4] * ctr[0] + root[(size_t)i * 4 + 1] * ctr[1]) + root[(size_t)i * 4 + 2] * ctr[2]) + root[(size_t)i * 4 + 3];
  const std::vector<int> order = dfs_order_of(sk);
  int jh = -1;
  for (size_t p = 0; p < order.size(); p++) if (order[p] == hb) jh = 2 + (int)p;
  const pbr::Animation* anim = nullptr;
  for (auto& a : m.animations) if (!a.joints.empty()) { anim = &a; break; }
  if (anim) {
    if (jh < 0 || (size_t)jh >= wm.size()) throw std::runtime_error("joint number out of range");
    std::vector<pbr::Mat> pw = pbr::pose_at(m, anim, 0.0);
    if ((size_t)jh >= pw.size()) throw std::runtime_error("joint number out of range");
    pbr::Mat mm = pbr::mat_mul(pw[(size_t)jh], pbr::mat_inv(wm[(size_t)jh]));
    pbr::V3 p = pbr::mat_point(mm, {c[0], c[1], c[2]});
    c[0] = p[0]; c[1] = p[1]; c[2] = p[2];
  }
  const double r = half * sf * 1.45 * fscale;
  out->x = c[0]; out->y = c[1]; out->z = c[2]; out->r = r;
  out->yaw = 18.0; out->pitch = 6.0;
  char hw[128];
  if (how_top) snprintf(hw, sizeof hw, "top part, %d vertices", (int)pts.size());
  else snprintf(hw, sizeof hw, "bone %s, %d vertices", sk.names[(size_t)hb].c_str(), (int)pts.size());
  out->how = hw;
  (void)nat;
  return true;
}

namespace {

std::string shortest_d(double v) {
  char b[64];
  for (int p = 1; p <= 17; p++) { snprintf(b, sizeof b, "%.*g", p, v); if (strtod(b, nullptr) == v) break; }
  std::string s = b;
  if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
  return s;
}
std::string jstr(const std::string& s) {
  std::string o = "\"";
  for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; }
  return o + "\"";
}
Bytes read_bytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void write_text(const fs::path& p, const std::string& s, bool crlf) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  if (!crlf) { f << s; return; }
  for (char c : s) { if (c == '\n') f << '\r'; f << c; }
}
void write_bytes(const fs::path& p, const Bytes& b) {
  fs::create_directories(p.parent_path());
  fs::path t = p;
  t += ".part";
  { std::ofstream f(t, std::ios::binary); f.write((const char*)b.data(), (std::streamsize)b.size()); }
  fs::rename(t, p);
}

bool whole_png_file(const fs::path& p) {
  try { png_decode(read_bytes(p)); return true; } catch (const std::exception&) { return false; }
}

using Views = std::map<int, FaceView>;
using Why = std::map<int, std::string>;

void write_views(const fs::path& p, const Views& v, const Why* why, bool crlf) {
  std::string s = "{\n";
  if (why) s += " \"views\": {\n";
  bool first = true;
  for (auto& kv : v) {
    char b[512];
    snprintf(b, sizeof b, "%s  \"%d\": {\"x\": %s, \"y\": %s, \"z\": %s, \"r\": %s, \"yaw\": %s, \"pitch\": %s, \"how\": %s}", first ? "" : ",\n", kv.first, shortest_d(kv.second.x).c_str(),
             shortest_d(kv.second.y).c_str(), shortest_d(kv.second.z).c_str(), shortest_d(kv.second.r).c_str(), shortest_d(kv.second.yaw).c_str(), shortest_d(kv.second.pitch).c_str(),
             jstr(kv.second.how).c_str());
    s += b;
    first = false;
  }
  if (why) {
    s += "\n },\n \"why\": {\n";
    first = true;
    for (auto& kv : *why) { s += (first ? "" : ",\n") + std::string("  \"") + std::to_string(kv.first) + "\": " + jstr(kv.second); first = false; }
    s += "\n }\n}\n";
  } else {
    s += "\n}\n";
  }
  write_text(p, s, crlf);
}
FaceView view_of(const JV& j) {
  FaceView v;
  v.x = j.at("x").num; v.y = j.at("y").num; v.z = j.at("z").num; v.r = j.at("r").num; v.yaw = j.at("yaw").num; v.pitch = j.at("pitch").num;
  if (const JV* h = j.get("how")) v.how = h->str;
  return v;
}

struct Faces {
  const FacesArgs& a;
  JV table;
  Faces(const FacesArgs& args) : a(args) {}

  void load_table() {
    try {
      JV t = json_read_file(a.data_dir + "/gen57_speed_numbers.json");
      const JV* v = t.get("version");
      if (!v || v->as_int() != 1) return;
      const JV* d = t.at("dumps").get(a.dump_fp);
      if (d) table = *d;
    } catch (const std::exception&) {}
  }

  void views_parallel(const std::vector<int>& nats, bool wide, const char* label, Views& views, Why& why) {
    const JV* tab = table.type == JV::OBJ ? table.get(wide ? "wide" : "views") : nullptr;
    struct Todo { int nat; fs::path file; std::string hash; };
    std::vector<Todo> todo;
    int hits = 0;
    for (int nat : nats) {
      const fs::path f = fs::path(a.pack) / "files" / ("pkx_n3_" + std::to_string(nat) + ".fsys");
      if (!fs::exists(f)) { why[nat] = "model not in the pack"; continue; }
      Bytes data = read_bytes(f);
      const std::string h = sha1_hex(data.data(), data.size()).substr(0, 20);
      const JV* ent = tab ? tab->get(std::to_string(nat)) : nullptr;
      if (ent && ent->get("key") && ent->at("key").str == view_key(nat, h, wide)) {
        hits++;
        const JV* vw = ent->get("view");
        if (!vw || vw->type == JV::NUL) why[nat] = "no head bone";
        else views[nat] = view_of(*vw);
        continue;
      }
      todo.push_back({nat, f, h});
    }
    const int total = hits + (int)todo.size();
    if (hits) a.progress(hits, total, "face cameras");
    std::atomic<size_t> next{0};
    std::atomic<int> done{hits};
    std::mutex mu;
    auto work = [&]() {
      for (;;) {
        const size_t k = next.fetch_add(1);
        if (k >= todo.size()) return;
        const Todo& t = todo[k];
        FaceView v;
        bool ok = false;
        std::string w;
        try {
          auto g = a.nat_group.find(t.nat);
          if (g == a.nat_group.end()) throw std::runtime_error("no model group in the dump");
          GfModel gm = load_model_only(*a.models, g->second);
          ok = face_view(gm, fsys_pkx(read_bytes(t.file)), t.nat, wide, &v);
          if (!ok) w = "no head bone";
        } catch (const std::exception& e) {
          w = std::string("camera: ") + e.what();
          w = w.substr(0, 130);
        }
        {
          std::lock_guard<std::mutex> g(mu);
          if (ok) views[t.nat] = v; else why[t.nat] = w;
        }
        a.progress(++done, total, "face cameras");
      }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < std::max(1, std::min(a.jobs, (int)todo.size())); i++) th.emplace_back(work);
    for (auto& t : th) t.join();
    (void)label;
  }

  static std::string face_item(int nat, const FaceView& v, const std::string& name) {
    char b[256];
    snprintf(b, sizeof b, "pokemon:%d:face=%.4f/%.4f/%.4f/%.4f/%.1f/%.1f:%s", nat + 31, v.x, v.y, v.z, v.r, v.yaw, v.pitch, name.c_str());
    return b;
  }

  void link(const fs::path& l, const std::string& target) {
#ifdef _WIN32
    std::string cmd = "mklink /J \"" + l.string() + "\" \"" + fs::absolute(target).string() + "\" >nul";
    if (std::system(("\"" + cmd + "\"").c_str()) != 0) throw Fail("mklink failed");
#else
    fs::create_directory_symlink(fs::absolute(target), l);
#endif
  }
  void unlink_(const fs::path& l) { std::error_code ec; fs::remove(l, ec); }

  void run_renderer(const std::vector<std::string>& items, const std::string& tag) {
    const fs::path work = a.work, out = work / "render";
    const fs::path st = work / ("settings_" + tag), ad = work / ("appdata_" + tag);
    for (const fs::path& d : {out, st / "extra_assets", ad / "Pokemon XD", work / ("card_" + tag), work / ("cache_" + tag)}) fs::create_directories(d);
    const fs::path l1 = st / "extra_assets" / "usum_gen57", l2 = st / "extra_assets" / "pbr_gen4";
    unlink_(l1); unlink_(l2);
    link(l1, a.pack);
    if (!a.gen4_pack.empty()) link(l2, a.gen4_pack);
    write_text(st / "settings.ini", "format = 1\npreset = original\n", false);
    std::map<std::string, std::string> env;
#ifdef _WIN32
    for (char** e = _environ; e && *e; e++) { std::string s = *e; if (s.compare(0, 3, "XD_") != 0 && s.find('=') != std::string::npos && s[0] != '=') env[s.substr(0, s.find('='))] = s.substr(s.find('=') + 1); }
#else
    for (char** e = environ; e && *e; e++) { std::string s = *e; if (s.compare(0, 3, "XD_") != 0 && s.find('=') != std::string::npos) env[s.substr(0, s.find('='))] = s.substr(s.find('=') + 1); }
#endif
    env["APPDATA"] = ad.string(); env["XD_SETTINGS_DIR"] = st.string(); env["XD_CARD_DIR"] = (work / ("card_" + tag)).string(); env["XD_CACHE_DIR"] = (work / ("cache_" + tag)).string();
    env["XD_WINDOW_OFFSCREEN"] = "1"; env["SDL_WINDOW_ACTIVATE_WHEN_SHOWN"] = "0"; env["XD_NO_MESSAGE_BOXES"] = "1"; env["SDL_AUDIODRIVER"] = "dummy"; env["SDL_AUDIO_DRIVER"] = "dummy";
    env["XD_DETERMINISTIC"] = "1"; env["XD_RENDER_ICON_POSE"] = "0"; env["XD_CHALLENGES"] = "gen4_pokemon,gen57_pokemon"; env["XD_RENDER_ICON_DIR"] = out.string(); env["XD_RENDER_ICON_SIZE"] = "512";
    std::string list;
    for (size_t i = 0; i < items.size(); i++) list += (i ? "," : "") + items[i];
    env["XD_RENDER_ICON"] = list;
    try { a.run(a.xd_exe, {a.xd_disc}, &env, (work / ("render_" + tag + ".log")).string()); } catch (...) {}
    unlink_(l1); unlink_(l2);
  }

  std::vector<std::string> render_all(const std::vector<std::string>& items, const std::vector<std::string>& names, const std::string& tag, const char* label) {
    const fs::path out = fs::path(a.work) / "render";
    fs::create_directories(out);
    for (size_t i = 0; i < items.size(); i++) { const fs::path p = out / names[i]; if (fs::exists(p) && !whole_png_file(p)) fs::remove(p); }
    std::vector<std::string> need;
    for (size_t i = 0; i < items.size(); i++) if (!fs::exists(out / names[i])) need.push_back(items[i]);
    const int total = (int)items.size();
    const int runs = std::max(1, std::min(a.xd_runs, (int)std::max<size_t>(need.size(), 1)));
    std::vector<std::vector<std::string>> pieces((size_t)runs);
    for (int k = 0; k < runs; k++) for (size_t i = need.size() * (size_t)k / (size_t)runs; i < need.size() * (size_t)(k + 1) / (size_t)runs; i++) pieces[(size_t)k].push_back(need[i]);
    std::atomic<bool> stop{false};
    std::thread watcher([&]() {
      while (!stop) {
        int n = 0;
        for (auto& nm : names) if (fs::exists(out / nm)) n++;
        a.progress(std::min(n, total), total, label);
        for (int i = 0; i < 10 && !stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
    auto draw = [&](int k) {
      int n = 0;
      std::vector<std::string> part;
      size_t size = 0;
      auto flush = [&]() {
        if (part.empty()) return;
        const auto t0 = std::chrono::steady_clock::now();
        run_renderer(part, tag + std::to_string(k) + "_" + std::to_string(n));
        char b[160];
        snprintf(b, sizeof b, "render %s%d.%d: %d pictures asked, %.0f s", tag.c_str(), k, n, (int)part.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        a.log(b);
        n++;
        part.clear();
        size = 0;
      };
      for (auto& it : pieces[(size_t)k]) {
        if (!part.empty() && size + it.size() + 1 > 24000) flush();
        part.push_back(it);
        size += it.size() + 1;
      }
      flush();
    };
    std::vector<std::thread> th;
    for (int k = 0; k < runs; k++) if (!pieces[(size_t)k].empty()) th.emplace_back(draw, k);
    for (auto& t : th) t.join();
    stop = true;
    watcher.join();
    a.progress(total, total, label);
    std::vector<std::string> missing;
    for (auto& nm : names) if (!fs::exists(out / nm)) missing.push_back(nm);
    return missing;
  }

  static double rad(double d) { return d * (3.14159265358979323846 / 180.0); }

  static bool head_from_silhouette(const fs::path& path, const FaceView& view, double share, FaceView* out) {
    Image8 im;
    try { im = png_decode(read_bytes(path)); } catch (const std::exception&) { return false; }
    const int w = im.w, h = im.h;
    std::vector<double> rows((size_t)h, 0.0);
    double total = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (im.px[((size_t)y * w + x) * 4 + 3] > 128) { rows[(size_t)y] += 1; total += 1; }
    if (total < 50) return false;
    std::vector<double> cum((size_t)h);
    double acc = 0;
    for (int y = 0; y < h; y++) { acc += rows[(size_t)y]; cum[(size_t)y] = acc; }
    for (auto& c : cum) c = c / total;
    int r0 = 0;
    while (r0 < h && rows[(size_t)r0] == 0) r0++;
    int r1 = (int)(std::lower_bound(cum.begin(), cum.end(), share) - cum.begin()) + 1;
    r1 = std::max(r1, r0 + 8);
    r1 = std::min(r1, h);
    std::vector<double> xs;
    int ymin = h, ymax = -1;
    for (int y = r0; y < r1; y++) for (int x = 0; x < w; x++) if (im.px[((size_t)y * w + x) * 4 + 3] > 128) { xs.push_back(x); ymin = std::min(ymin, y - r0); ymax = std::max(ymax, y - r0); }
    if (xs.empty()) return false;
    const double lo_x = np_percentile(xs, 4), hi_x = np_percentile(xs, 96);
    const double cx = (lo_x + hi_x) * 0.5, cy = r0 + (double)(ymin + ymax) * 0.5;
    const double half_px = std::max(hi_x - lo_x, (double)(ymax - ymin)) * 0.5 * 1.2;
    const double k = view.r / dm::cos(rad(15.0));
    const double u = (cx / w - 0.5) * 2.0, v = (0.5 - cy / h) * 2.0;
    const double yaw = rad(view.yaw), pitch = rad(view.pitch);
    const double right[3] = {dm::cos(yaw), 0.0, -dm::sin(yaw)};
    const double look[3] = {-(dm::sin(yaw) * dm::cos(pitch)), -dm::sin(pitch), -(dm::cos(yaw) * dm::cos(pitch))};
    const double up[3] = {right[1] * look[2] - right[2] * look[1], right[2] * look[0] - right[0] * look[2], right[0] * look[1] - right[1] * look[0]};
    const double base[3] = {view.x, view.y, view.z};
    double p[3];
    for (int i = 0; i < 3; i++) p[i] = (base[i] + right[i] * u * k) + up[i] * v * k;
    out->x = p[0]; out->y = p[1]; out->z = p[2];
    out->r = std::max(half_px / (w / 2.0) * k, 0.05);
    out->yaw = view.yaw; out->pitch = view.pitch;
    out->how = "silhouette top part";
    return true;
  }

  void run() {
    load_table();
    const fs::path work = a.work;
    fs::create_directories(work);
    std::vector<int> nats(a.nats.begin(), a.nats.end());
    Views views;
    Why why;
    const fs::path vf = work / "views.json";
    bool have_views = false;
    if (fs::exists(vf)) {
      try {
        JV d = json_read_file(vf.string());
        for (auto& kv : d.at("views").obj) views[atoi(kv.first.c_str())] = view_of(kv.second);
        for (auto& kv : d.at("why").obj) why[atoi(kv.first.c_str())] = kv.second.str;
        have_views = true;
      } catch (const std::exception&) { views.clear(); why.clear(); }
    }
    if (!have_views) {
      views_parallel(nats, false, "faces", views, why);
      write_views(vf, views, &why, a.eol_crlf);
    }
    std::vector<int> order;
    for (auto& kv : views) order.push_back(kv.first);
    {
      std::vector<std::string> items, names;
      for (int n : order) { items.push_back(face_item(n, views[n], std::to_string(n))); names.push_back(std::to_string(n) + ".png"); }
      auto missing = render_all(items, names, "f", "faces");
      if (!missing.empty()) { std::string s = "faces not drawn: ["; for (size_t i = 0; i < missing.size(); i++) s += (i ? ", " : "") + missing[i]; a.log(s + "]"); }
    }
    const fs::path out = work / "render";
    std::vector<int> bad;
    for (int n : order) {
      bool b = hand_share(n) != 0.0;
      if (!b) { try { b = !face_ok(png_decode(read_bytes(out / (std::to_string(n) + ".png")))); } catch (const std::exception&) { b = true; } }
      if (b) bad.push_back(n);
    }
    if (!bad.empty()) {
      { std::string s = "mending: ["; for (size_t i = 0; i < bad.size(); i++) s += (i ? ", " : "") + std::to_string(bad[i]); a.log(s + "]"); }
      const fs::path wf = work / "wide_views.json";
      Views wide;
      if (fs::exists(wf)) {
        try { JV d = json_read_file(wf.string()); for (auto& kv : d.obj) wide[atoi(kv.first.c_str())] = view_of(kv.second); } catch (const std::exception&) { wide.clear(); }
      }
      std::vector<int> need;
      for (int n : bad) if (!wide.count(n)) need.push_back(n);
      if (!need.empty()) {
        Views got;
        Why w2;
        views_parallel(need, true, "faces", got, w2);
        for (auto& kv : got) wide[kv.first] = kv.second;
        write_views(wf, wide, nullptr, a.eol_crlf);
      }
      std::vector<int> wl;
      for (int n : bad) if (wide.count(n)) wl.push_back(n);
      {
        std::vector<std::string> items, names;
        for (int n : wl) { items.push_back(face_item(n, wide[n], "w" + std::to_string(n))); names.push_back("w" + std::to_string(n) + ".png"); }
        render_all(items, names, "w", "faces (second look)");
      }
      Views nv;
      for (int n : wl) {
        const fs::path p = out / ("w" + std::to_string(n) + ".png");
        double share = hand_share(n) != 0.0 ? hand_share(n) : 0.22;
        FaceView v;
        if (fs::exists(p) && head_from_silhouette(p, wide[n], share, &v)) nv[n] = v;
      }
      const fs::path mf = work / "mended.json";
      std::set<int> done_m;
      if (fs::exists(mf)) { try { JV d = json_read_file(mf.string()); for (auto& x : d.arr) done_m.insert(x.as_int()); } catch (const std::exception&) {} }
      for (auto& kv : nv) if (!done_m.count(kv.first) && fs::exists(out / (std::to_string(kv.first) + ".png"))) fs::remove(out / (std::to_string(kv.first) + ".png"));
      {
        std::vector<std::string> items, names;
        for (auto& kv : nv) { items.push_back(face_item(kv.first, kv.second, std::to_string(kv.first))); names.push_back(std::to_string(kv.first) + ".png"); }
        render_all(items, names, "m", "faces (second look)");
      }
      std::string s = "[";
      bool first = true;
      for (auto& kv : nv) { s += (first ? "" : ", ") + std::to_string(kv.first); first = false; }
      write_text(mf, s + "]", false);
    }
    cut(nats, why);
  }

  void cut(const std::vector<int>& nats, const Why& why) {
    const fs::path out = fs::path(a.pack) / "textures" / "faces";
    fs::create_directories(out);
    std::vector<std::pair<int, std::string>> rep;
    for (int nat : nats) {
      const fs::path src = fs::path(a.work) / "render" / (std::to_string(nat) + ".png");
      if (fs::exists(src)) {
        try {
          write_bytes(out / (std::to_string(nat) + ".png"), png_encode(round_face(png_decode(read_bytes(src)))));
          rep.push_back({nat, "rendered"});
          continue;
        } catch (const std::exception& e) {
          std::string m = std::string("cut: ") + e.what();
          rep.push_back({nat, "box icon (" + m.substr(0, 100) + ")"});
          continue;
        }
      }
      auto w = why.find(nat);
      rep.push_back({nat, "box icon (" + (w != why.end() && !w->second.empty() ? w->second : std::string("render missing")) + ")"});
    }
    std::string s = rep.empty() ? "{}" : "{\n";
    for (size_t i = 0; i < rep.size(); i++) s += " \"" + std::to_string(rep[i].first) + "\": " + jstr(rep[i].second) + (i + 1 < rep.size() ? ",\n" : "\n");
    if (!rep.empty()) s += "}";
    write_text(fs::path(a.pack) / "faces.json", s, a.eol_crlf);
    int ok = 0;
    std::string fb;
    for (auto& r : rep) { if (r.second == "rendered") ok++; else fb += (fb.empty() ? "" : ", ") + std::to_string(r.first); }
    a.log("faces: " + std::to_string(ok) + " rendered, " + std::to_string((int)rep.size() - ok) + " from the box icon" + (fb.empty() ? "" : ": " + fb));
  }
};

}

void make_faces(const FacesArgs& a) {
  Faces f(a);
  f.run();
}

}
