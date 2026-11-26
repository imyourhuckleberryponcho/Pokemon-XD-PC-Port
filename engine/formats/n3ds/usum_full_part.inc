#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

struct FullArgs {
  std::string rom, xd_disc, out, gen4_pack, gen57_pack, xd_exe, data_folder, pictures_out, data_mods_exe, pbr_models, usum_models_py, data_dir, gen57_only, gen57_skip, only, gen4_values = "on";
  int jobs = 0;
  bool eol_crlf = false;
};

const int GEN57_MAKE_VERSION = 7;
const char* kConverterVersion = "0.2.3";

int run_process(const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>* env, const std::string& logfile,
                const std::function<void(const std::string&)>& on_line = nullptr) {
#ifdef _WIN32
  std::string cmd = "\"" + exe + "\"";
  for (auto& a : args) cmd += " \"" + a + "\"";
  std::string block;
  if (env) { for (auto& kv : *env) { block += kv.first + "=" + kv.second; block.push_back('\0'); } block.push_back('\0'); }
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr, lf = INVALID_HANDLE_VALUE;
  if (on_line) { CreatePipe(&rd, &wr, &sa, 0); SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0); }
  else if (!logfile.empty()) lf = CreateFileA(logfile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = on_line ? wr : (lf != INVALID_HANDLE_VALUE ? lf : GetStdHandle(STD_OUTPUT_HANDLE));
  si.hStdError = si.hStdOutput;
  PROCESS_INFORMATION pi{};
  std::vector<char> cl(cmd.begin(), cmd.end());
  cl.push_back(0);
  BOOL ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, env ? (LPVOID)block.data() : nullptr, nullptr, &si, &pi);
  if (wr) CloseHandle(wr);
  if (!ok) { if (rd) CloseHandle(rd); if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf); return -1; }
  if (on_line) {
    std::string buf;
    char tmp[4096];
    DWORD n;
    while (ReadFile(rd, tmp, sizeof tmp, &n, nullptr) && n) {
      buf.append(tmp, n);
      size_t p;
      while ((p = buf.find('\n')) != std::string::npos) { std::string l = buf.substr(0, p); if (!l.empty() && l.back() == '\r') l.pop_back(); on_line(l); buf.erase(0, p + 1); }
    }
    CloseHandle(rd);
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  if (lf != INVALID_HANDLE_VALUE) CloseHandle(lf);
  return (int)code;
#else
  std::vector<std::string> argv_s = {exe};
  for (auto& a : args) argv_s.push_back(a);
  std::vector<char*> argv;
  for (auto& s : argv_s) argv.push_back((char*)s.c_str());
  argv.push_back(nullptr);
  std::vector<std::string> env_s;
  std::vector<char*> envp;
  if (env) { for (auto& kv : *env) env_s.push_back(kv.first + "=" + kv.second); for (auto& s : env_s) envp.push_back((char*)s.c_str()); envp.push_back(nullptr); }
  int fds[2] = {-1, -1};
  if (on_line && pipe(fds) != 0) return -1;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  if (on_line) { posix_spawn_file_actions_adddup2(&fa, fds[1], 1); posix_spawn_file_actions_adddup2(&fa, fds[1], 2); posix_spawn_file_actions_addclose(&fa, fds[0]); }
  else if (!logfile.empty()) { posix_spawn_file_actions_addopen(&fa, 1, logfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); posix_spawn_file_actions_adddup2(&fa, 1, 2); }
  pid_t pid;
  int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv.data(), env ? envp.data() : environ);
  posix_spawn_file_actions_destroy(&fa);
  if (rc != 0) return -1;
  if (on_line) {
    close(fds[1]);
    std::string buf;
    char tmp[4096];
    ssize_t n;
    while ((n = read(fds[0], tmp, sizeof tmp)) > 0) {
      buf.append(tmp, (size_t)n);
      size_t p;
      while ((p = buf.find('\n')) != std::string::npos) { on_line(buf.substr(0, p)); buf.erase(0, p + 1); }
    }
    close(fds[0]);
  }
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

std::string json_text(const pbr::Json& j, int indent) { return j.dump(indent, true); }

std::string read_all(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void write_all(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write(s.data(), (std::streamsize)s.size());
}
std::string eolfix(const std::string& s, bool crlf) {
  if (!crlf) return s;
  std::string o;
  for (char c : s) { if (c == '\n') o += '\r'; o += c; }
  return o;
}
void wipe(const fs::path& p) { std::error_code ec; fs::remove_all(p, ec); }
uint64_t dir_bytes(const fs::path& p) {
  uint64_t n = 0;
  std::error_code ec;
  for (auto& e : fs::recursive_directory_iterator(p, ec)) if (e.is_regular_file(ec)) n += (uint64_t)e.file_size(ec);
  return n;
}
bool whole_png(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  char sig[8];
  f.read(sig, 8);
  if (f.gcount() != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
  f.seekg(-12, std::ios::end);
  char tail[12];
  f.read(tail, 12);
  return f.gcount() == 12 && memcmp(tail + 4, "IEND", 4) == 0;
}
long long available_memory_mb() {
#ifdef _WIN32
  MEMORYSTATUSEX m{sizeof m};
  if (GlobalMemoryStatusEx(&m)) return (long long)(m.ullAvailPhys / (1024 * 1024));
  return -1;
#else
  std::ifstream f("/proc/meminfo");
  std::string k;
  long long v;
  std::string unit;
  while (f >> k >> v >> unit) if (k == "MemAvailable:") return v / 1024;
  return -1;
#endif
}
int xd_runs() {
  if (const char* v = std::getenv("GEN57_XD_RUNS")) if (atoi(v) > 0) return atoi(v);
  const long long avail = available_memory_mb();
  if (std::thread::hardware_concurrency() < 4 || (avail >= 0 && avail < 4500)) return 1;
  return 2;
}

void make_pictures(const FullArgs& a, const std::string& pack, const std::string& gen4, const std::string& out, int first_row,
                   const std::function<void(int, int, const std::string&)>& progress, const std::function<void(const std::string&)>& log) {
  fs::create_directories(out);
  pbr::Json info = pbr::Json::parse(read_all(fs::path(pack) / "gen57.json"));
  const int species = info.find("species") ? (int)info.find("species")->i : 314, forms = info.find("forms") ? (int)info.find("forms")->i : 0;
  const int last = 494 + 26 + species + forms - 1;
  std::set<int> gen4_rows;
  {
    std::ifstream tf(fs::path(gen4) / "tables.txt");
    std::string ln;
    while (std::getline(tf, ln)) {
      int nat = 0, row = 0;
      if (ln.compare(0, 8, "species ") == 0 && std::sscanf(ln.c_str() + 8, "%d %d", &nat, &row) == 2 && row > 0) gen4_rows.insert(row);
    }
  }
  auto has_row = [&](int r) {
    if (r >= 252 && r <= 276) return false;
    if (r == 412) return false;
    if (r >= 412 && r < 520) return gen4_rows.empty() || gen4_rows.count(r) > 0;
    return true;
  };
  std::vector<std::string> names = {"michael.png"}, items = {"michael"};
  for (int r = first_row; r <= last; r++) {
    if (!has_row(r)) continue;
    items.push_back("row:" + std::to_string(r));
    items.push_back("row:" + std::to_string(r) + ":shiny");
    names.push_back("pokemon_" + std::to_string(r) + ".png");
    names.push_back("pokemon_" + std::to_string(r) + "_shiny.png");
  }
  for (auto& e : fs::directory_iterator(out)) {
    if (e.path().extension() == ".png" && !whole_png(e.path())) fs::remove(e.path());
  }
  std::vector<std::pair<std::string, std::string>> todo;
  for (size_t i = 0; i < items.size(); i++) if (!fs::exists(fs::path(out) / names[i])) todo.push_back({items[i], names[i]});
  const int total = (int)items.size();
  const int runs = std::max(1, std::min(xd_runs(), (int)std::max<size_t>(todo.size(), 1)));
  std::vector<std::vector<std::string>> pieces((size_t)runs);
  for (int k = 0; k < runs; k++) for (size_t i = todo.size() * (size_t)k / (size_t)runs; i < todo.size() * (size_t)(k + 1) / (size_t)runs; i++) pieces[(size_t)k].push_back(todo[i].first);
  fs::path base_work = fs::temp_directory_path() / ("dexpics_" + std::to_string((long long)std::time(nullptr)) + "_" + std::to_string(std::rand()));
  std::vector<fs::path> links;
  std::mutex mu;
  std::atomic<bool> stop{false}, failed{false};
  std::string fail_text;
  std::thread watcher([&]() {
    while (!stop) {
      int n = 0;
      for (auto& nm : names) if (fs::exists(fs::path(out) / nm)) n++;
      progress(std::min(n, total), total, "pictures");
      for (int i = 0; i < 10 && !stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  log("pictures: " + std::to_string(todo.size()) + " to draw in " + std::to_string(runs) + " xd runs");
  auto draw = [&](int k) {
    try {
      const fs::path work = base_work / ("run" + std::to_string(k));
      for (const char* d : {"card", "settings", "cache", "appdata"}) fs::create_directories(work / d);
      write_all(work / "settings" / "settings.ini", "format = 1\npreset = enhanced\n");
      fs::create_directories(work / "settings" / "extra_assets");
      const std::pair<std::string, const char*> lk[2] = {{gen4, "pbr_gen4"}, {pack, "usum_gen57"}};
      for (auto& l : lk) {
        const fs::path link = work / "settings" / "extra_assets" / l.second;
#ifdef _WIN32
        std::string cmd = "mklink /J \"" + link.string() + "\" \"" + fs::absolute(l.first).string() + "\" >nul";
        if (std::system(("\"" + cmd + "\"").c_str()) != 0) throw std::runtime_error("mklink failed");
#else
        fs::create_directory_symlink(fs::absolute(l.first), link);
#endif
        std::lock_guard<std::mutex> g(mu);
        links.push_back(link);
      }
      std::map<std::string, std::string> env;
#ifdef _WIN32
      for (char** e = _environ; e && *e; e++) { std::string s = *e; if (s.compare(0, 3, "XD_") != 0 && s.find('=') != std::string::npos) env[s.substr(0, s.find('='))] = s.substr(s.find('=') + 1); }
#else
      for (char** e = environ; e && *e; e++) { std::string s = *e; if (s.compare(0, 3, "XD_") != 0 && s.find('=') != std::string::npos) env[s.substr(0, s.find('='))] = s.substr(s.find('=') + 1); }
#endif
      env["XD_NO_MESSAGE_BOXES"] = "1"; env["XD_WINDOW_OFFSCREEN"] = "1"; env["SDL_WINDOW_ACTIVATE_WHEN_SHOWN"] = "0"; env["XD_RECORD"] = "0";
      env["SDL_AUDIO_DRIVER"] = "dummy"; env["SDL_AUDIODRIVER"] = "dummy"; env["XD_CARD_DIR"] = (work / "card").string();
      env["XD_SETTINGS_DIR"] = (work / "settings").string(); env["XD_CACHE_DIR"] = (work / "cache").string();
      env["XD_RUN_LABEL"] = "pokedex pictures " + std::to_string(k); env["XD_DETERMINISTIC"] = "1"; env["XD_RENDER_ICON_POSE"] = "0"; env["XD_CHALLENGES"] = "gen4_pokemon,gen57_pokemon";
      env["XD_RENDER_ICON_DIR"] = out; env["XD_RENDER_ICON_SIZE"] = "256"; env["APPDATA"] = (work / "appdata").string();
      if (const char* rc = std::getenv("GEN57_REAL_CLOCK")) if (std::string(rc) == "1") env.erase("XD_DETERMINISTIC");
      std::vector<std::vector<std::string>> chunks;
      size_t size = 0;
      for (auto& it : pieces[(size_t)k]) {
        if (!chunks.empty() && size + it.size() + 1 <= 24000) { chunks.back().push_back(it); size += it.size() + 1; }
        else { chunks.push_back({it}); size = it.size() + 1; }
      }
      int n = 0;
      for (auto& ch : chunks) {
        std::string list;
        for (size_t i = 0; i < ch.size(); i++) list += (i ? "," : "") + ch[i];
        env["XD_RENDER_ICON"] = list;
        const auto t0 = std::chrono::steady_clock::now();
        run_process(a.xd_exe, {a.xd_disc}, &env, (work / ("run" + std::to_string(n) + ".log")).string());
        log("pictures run " + std::to_string(k) + "." + std::to_string(n) + ": " + std::to_string(ch.size()) + " asked, " +
            std::to_string((long long)std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) + " s");
        n++;
      }
    } catch (const std::exception& e) {
      std::lock_guard<std::mutex> g(mu);
      failed = true;
      fail_text = e.what();
    }
  };
  std::vector<std::thread> th;
  for (int k = 0; k < runs; k++) if (!pieces[(size_t)k].empty()) th.emplace_back(draw, k);
  for (auto& t : th) t.join();
  stop = true;
  watcher.join();
  for (auto& l : links) { std::error_code ec; fs::remove(l, ec); }
  wipe(base_work);
  int made = 0;
  for (auto& nm : names) if (fs::exists(fs::path(out) / nm)) made++;
  log("pictures: " + std::to_string(made) + " of " + std::to_string(total));
  if (failed || made < total) {
    const std::string why = failed ? " (" + fail_text + ")" : std::string();
    log("pictures MISSING: " + std::to_string(total - made) + " of " + std::to_string(total) + why + "; the pack is finished without them: make it again to draw them");
    say("progress 1 1 " + std::to_string(total - made) + " Pokedex pictures could not be drawn: they show a question mark; making the pack again draws them if it was only a stop");
  }
}

std::vector<std::string> check_pack(const std::string& pack, size_t nats_n, const std::string& skip, bool faces_run) {
  std::vector<std::string> problems;
  using pbr::Json;
  auto rd = [&](const std::string& rel) { return Json::parse(read_all(fs::path(pack) / rel)); };
  try {
    const int first = 488;
    Json mj = rd("data/01_moves.json"), report = rd("moves_report.json");
    const Json& moves = *mj.find("add")->find("moves");
    if (moves.o.size() != report.a.size()) problems.push_back("(move rows check failed: the move file adds " + std::to_string(moves.o.size()) + " moves, the report has " + std::to_string(report.a.size()) + ")");
    for (size_t i = 0; i < moves.o.size() && i < report.a.size(); i++)
      if (report.a[i].find("row")->i != (int64_t)(first + i)) { problems.push_back("(move rows check failed: move " + std::to_string(i) + ")"); break; }
    const int64_t lastrow = first + (int64_t)moves.o.size() - 1;
    Json sp = rd("data/02_species.json");
    for (auto& kv : sp.find("add")->find("pokemon")->o) {
      const Json* w = kv.second.find("waza.dataId");
      if (w) for (auto& v : w->a) if (v.kind == Json::Int && v.i > lastrow && v.i >= first) { problems.push_back("(move rows check failed: " + kv.first + " uses move row " + std::to_string(v.i) + " past the file)"); goto moves_done; }
    }
  moves_done:
    if (sp.find("add")->find("pokemon")->o.size() != nats_n) problems.push_back("(" + std::to_string(sp.find("add")->find("pokemon")->o.size()) + " species rows instead of " + std::to_string(nats_n) + ")");
    for (const char* d : {"data_fr", "data_ge", "data_it", "data_sp", "data_jp"}) if (!fs::exists(fs::path(pack) / d / "02_species.json")) problems.push_back(std::string("(no ") + d + ")");
    if (nats_n == 314) {
      std::string t = read_all(fs::path(pack) / "tables.txt");
      size_t have = 0;
      std::istringstream is(t);
      std::string ln;
      while (std::getline(is, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.compare(0, 4, "pkx ") == 0) have++;
      }
      if (have < 314) problems.push_back("(models check failed: " + std::to_string(have) + " pkx rows)");
    }
    if (skip.find("faces") == std::string::npos && faces_run && fs::exists(fs::path(pack) / "faces.json")) {
      Json fj = rd("faces.json");
      size_t bad = 0;
      for (auto& kv : fj.o) if (!(kv.second.kind == Json::Str && kv.second.s == "rendered")) bad++;
      if (fj.o.size() != nats_n || bad > std::max<size_t>(3, nats_n / 50)) problems.push_back("(faces: " + std::to_string(fj.o.size() - bad) + " of " + std::to_string(nats_n) + " drawn)");
    }
  } catch (const std::exception& e) {
    problems.push_back(std::string("(check failed: ") + e.what() + ")");
  }
  return problems;
}

int run_full(FullArgs& a) {
  using pbr::Json;
  const auto t_all = std::chrono::steady_clock::now();
  if (a.jobs <= 0) a.jobs = (int)std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
  if (a.data_dir.empty()) {
    for (const char* cand : {"tools/n3ds", "../tools/n3ds", "data", "."}) if (fs::exists(fs::path(cand) / "usum_gen4_plan.json")) { a.data_dir = cand; break; }
    if (a.data_dir.empty()) throw Fail("the plan lists (usum_gen4_plan.json ...) were not found: give --data <folder>");
  }
  const std::string out = fs::absolute(a.out).string();
  const std::string fp = dump_fingerprint(a.rom);
  RomFs r = open_dump(a.rom);
  Detected d = detect(r);
  Garc g = garc_at(r, d.models);
  std::unique_ptr<pbr::DiscSource> disc = open_iso(a.xd_disc);
  if (disc->game_id() != "GXXE01") throw Fail("the XD image is not the XD disc (GXXE01)", 2);
  XdSide xd(*disc);
  PlanOptions po;
  po.rom = a.rom;
  po.data_dir = a.data_dir;
  po.packs = {"usum", "gen4", "g57", "form"};
  { std::stringstream os(a.only); std::string x; while (std::getline(os, x, ',')) if (!x.empty()) po.only.insert(atoi(x.c_str())); }
  const std::set<int> only57 = [&] { std::set<int> s; std::stringstream os(a.gen57_only); std::string x; while (std::getline(os, x, ',')) if (!x.empty()) s.insert(atoi(x.c_str())); return s; }();
  const bool want57 = !a.gen57_pack.empty();

  bool base_done = false;
  if (want57) {
    try {
      Json m = Json::parse(read_all(fs::path(out) / "mod.json"));
      Json g4 = Json::parse(read_all(fs::path(a.gen4_pack) / "gen4.json"));
      base_done = m.find("version") && m.find("version")->s == kConverterVersion && m.find("count") && m.find("count")->i >= 1 &&
                  (!m.find("dump") || m.find("dump")->s == fp) && g4.find("version") && g4.find("version")->i == 2 &&
                  g4.find("cries") && g4.find("cries")->i >= 1;
    } catch (const std::exception&) {}
  }
  say("stage base 0 8");
  Plan plan_all;
  if (base_done) {
    say("progress 1 1 models and Gen 4 pack are already made");
  } else {
    plan_all = make_plan(r, d, g, *disc, po);
    const fs::path tmp = out + ".tmp", src = out + ".gen4src";
    wipe(tmp);
    wipe(src);
    fs::create_directories(tmp / "fsys");
    fs::create_directories(src);
    const fs::path repd = fs::path(out + ".reports");
    wipe(repd);
    Ctx c{&g, &xd, out, false, fp, tmp.string(), "", src.string(), repd.string()};
    std::vector<const PlanItem*> list;
    for (auto& it : plan_all.items) if (it.pack == "usum" || it.pack == "gen4") list.push_back(&it);
    std::atomic<size_t> next{0}, done{0};
    std::mutex mu;
    std::map<int, std::string> res_json;
    const size_t total = list.size();
    auto work = [&]() {
      while (true) {
        const size_t k = next.fetch_add(1);
        if (k >= total) return;
        const PlanItem& it = *list[k];
        try { make_one(c, it); } catch (const std::exception&) {}
        const size_t dn = ++done;
        say("progress " + std::to_string(dn) + " " + std::to_string(total) + " " + it.name);
      }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < a.jobs; i++) th.emplace_back(work);
    for (auto& t : th) t.join();
    Json species = Json::object(), failed = Json::array(), lookup = Json::object();
    std::vector<int> tris;
    long long nbytes = 0, nconv = 0;
    std::map<int, std::string> log_chunks;
    for (const PlanItem* it : list) {
      Json rep;
      try { rep = Json::parse(read_all(repd / (it->name + ".json"))); } catch (const std::exception&) { rep = Json::object(); rep.set("error", Json("no report")); }
      Json res = Json::object();
      const bool ok = !rep.find("error");
      res.set("nat", Json(it->nat));
      res.set("name", Json(it->model));
      res.set("state", Json(ok ? "converted" : "kept_xd"));
      res.set("group", Json(it->group));
      if (ok) {
        for (const char* k : {"tris", "size", "xd_size"}) if (const Json* v = rep.find(k)) res.set(k, *v);
        if (const Json* v = rep.find("scale")) res.set("scale", *v);
        if (const Json* v = rep.find("roles")) res.set("roles", *v);
        if (it->pack == "gen4") res.set("standin", Json(it->standin_nat));
        tris.push_back((int)rep.find("tris")->i);
        nbytes += rep.find("size")->i;
        nconv++;
      } else {
        res.set("error", *rep.find("error"));
        Json f = Json::object();
        f.set("nat", Json(it->nat)); f.set("name", res.find("name") ? *res.find("name") : Json("")); f.set("error", *rep.find("error"));
        failed.push(f);
      }
      res.set("secs", rep.find("secs") ? *rep.find("secs") : Json(0));
      species.set(std::to_string(it->nat), res);
      {
        std::ostringstream ch;
        std::string sb = "0.0";
        if (const Json* sv = rep.find("secs")) sb = sv->kind == Json::Num ? sv->s : std::to_string(sv->i) + ".0";
        ch << "\n== " << it->model << " (#" << it->nat << "): " << (ok ? "converted" : "kept_xd") << ", " << (ok ? rep.find("size")->i : 0) << " bytes, " << sb << " s\n";
        if (const Json* lj = rep.find("log")) for (auto& l : lj->a) ch << l.s << "\n";
        if (!ok) ch << "REFUSED: " << rep.find("error")->s << "\n";
        log_chunks[it->nat] = ch.str();
      }
    }
    for (auto& kv : plan_all.lookup) lookup.set(std::to_string(kv.first), Json(kv.second));
    Json mod = Json::object();
    mod.set("id", Json("ultra-sun-moon-models"));
    mod.set("name", Json("Ultra Sun / Ultra Moon models"));
    mod.set("version", Json(kConverterVersion));
    mod.set("author", Json("made on your PC from your own 3DS dump"));
    mod.set("description", Json("Pokemon Ultra Sun / Ultra Moon's Pokemon models in Pokemon XD, converted from your own dump."));
    mod.set("dump", Json(fp));
    mod.set("game", Json("GXXE01"));
    mod.set("generated_from", Json(r.product));
    mod.set("applies", Json("restart"));
    mod.set("format", Json(1));
    mod.set("count", Json((int)nconv));
    mod.set("unit", Json("model"));
    mod.set("units", Json("models"));
    write_all(tmp / "mod.json", eolfix(mod.dump(2, true), a.eol_crlf));
    Json report = Json::object();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_all).count();
    report.set("version", Json(kConverterVersion));
    { char sb[32]; snprintf(sb, sizeof sb, "%.1f", secs); report.set("seconds", Json::num(sb)); }
    report.set("jobs", Json(a.jobs));
    report.set("species_in_xd", Json((int)xd.species.size()));
    report.set("converted", Json((int)nconv));
    report.set("failed", failed);
    Json nomodel = Json::array();
    for (int n : plan_all.no_model_in_dump) nomodel.push(Json(n));
    report.set("no_model_in_dump", nomodel);
    report.set("bytes", Json((int64_t)nbytes));
    report.set("tris_min", Json(tris.empty() ? 0 : *std::min_element(tris.begin(), tris.end())));
    report.set("tris_max", Json(tris.empty() ? 0 : *std::max_element(tris.begin(), tris.end())));
    long long ts = 0;
    for (int t : tris) ts += t;
    report.set("tris_mean", Json(tris.empty() ? 0 : (int)std::nearbyint((double)ts / (double)tris.size())));
    report.set("species", species);
    report.set("lookup", lookup);
    write_all(tmp / "usum_report.json", eolfix(report.dump(1, true), a.eol_crlf));
    char head[256];
    snprintf(head, sizeof head, "usum_models %s: %zu models, %lld converted, %lld kept from XD, %lld bytes, %.0f s, %d jobs\n", kConverterVersion, total, nconv,
             (long long)total - nconv, nbytes, secs, a.jobs);
    write_all(tmp / "conversion_log.txt", eolfix(std::string(head) + [&] { std::string b; for (auto& kv : log_chunks) b += kv.second; return b; }(), a.eol_crlf));
    const fs::path old = out + ".old";
    wipe(old);
    if (fs::exists(out)) fs::rename(out, old);
    fs::rename(tmp, out);
    wipe(old);
    if (!a.gen4_pack.empty()) {
      Gen4SourceOptions g4;
      g4.rom = a.rom; g4.data_dir = a.data_dir; g4.src = src.string(); g4.values_on = a.gen4_values != "off"; g4.eol_crlf = a.eol_crlf;
      for (auto& it : plan_all.items)
        if (it.pack == "gen4" && fs::exists(src / "models" / (it.model + ".pkx"))) g4.plan_lines.push_back(std::to_string(it.nat) + "\t" + it.model + "\t" + std::to_string(it.standin_nat));
      std::sort(g4.plan_lines.begin(), g4.plan_lines.end(), [](const std::string& x, const std::string& y) { return atoi(x.c_str()) < atoi(y.c_str()); });
      write_gen4_source(g4, nullptr);
      std::string exe = a.pbr_models;
      if (exe.empty()) {
        const fs::path self = fs::path(a.usum_models_py.empty() ? "." : a.usum_models_py).parent_path();
        for (const fs::path& cand : {self / "pbr_models.exe", self.parent_path() / "pbr_models" / "pbr_models.exe", self.parent_path() / "pbr_models.exe", self / "pbr_models",
                                     self.parent_path() / "pbr_models" / "pbr_models", self.parent_path() / "pbr_models"})
          if (exe.empty() && fs::is_regular_file(cand)) exe = cand.string();
      }
      if (exe.empty()) throw Fail("the Gen 4 pack builder (pbr_models) was not found next to this program");
      std::vector<std::string> lines;
      const int rc = run_process(exe, {"--gen4-source", src.string(), "--xd-disc", a.xd_disc, "--gen4-pack", fs::absolute(a.gen4_pack).string()}, nullptr, "",
                                 [&](const std::string& l) { if (l.compare(0, 5, "error") == 0) lines.push_back(l); });
      if (!lines.empty()) throw Fail(lines[0]);
      if (rc != 0) throw Fail("the Gen 4 pack builder stopped (exit " + std::to_string(rc) + ")");
      {
        const fs::path gj = fs::path(a.gen4_pack) / "gen4.json";
        Json g4j = Json::parse(read_all(gj));
        g4j.set("recipe", Json(2));
        write_all(gj, eolfix(g4j.dump(1, true), a.eol_crlf));
      }
      if (!std::getenv("USUM_KEEP_SOURCE")) wipe(src);
    }
    wipe(repd);
  }
  if (!want57) {
    say("done 0 0 0");
    return 0;
  }

  const std::string pack_out = fs::absolute(a.gen57_pack).string();
  const fs::path work = pack_out + ".work";
  auto pack_is_current = [&]() {
    try {
      Json j = Json::parse(read_all(fs::path(pack_out) / "gen57.json"));
      return j.find("converter") && j.find("converter")->s == kConverterVersion && j.find("dump") && j.find("dump")->s == fp && j.find("make") &&
             j.find("make")->i == GEN57_MAKE_VERSION && j.find("complete") && j.find("complete")->kind == Json::Bool && j.find("complete")->b;
    } catch (const std::exception&) { return false; }
  };
  if (pack_is_current() && a.gen57_only.empty()) { say("done 0 0 0"); return 0; }
  fs::create_directories(work);
  std::ofstream lg(work / "gen57.log", std::ios::app);
  auto log = [&](const std::string& s) { lg << s << "\n"; lg.flush(); };
  log("==== run (usum_models_cpp) converter " + std::string(kConverterVersion) + " dump " + fp);
  Json st = Json::object();
  bool fresh = true;
  try {
    st = Json::parse(read_all(work / "state.json"));
    fresh = !(st.find("converter") && st.find("converter")->s == kConverterVersion && st.find("dump") && st.find("dump")->s == fp && st.find("make") &&
              st.find("make")->i == GEN57_MAKE_VERSION && st.find("engine") && st.find("engine")->s == "cpp" && st.find("only") && st.find("only")->s == a.gen57_only);
  } catch (const std::exception&) {}
  if (fresh) {
    log("a new start (the work folder was for another dump, converter or subset): wiped");
    for (auto& e : fs::directory_iterator(work)) if (e.path().filename() != "gen57.log") wipe(e.path());
    st = Json::object();
    st.set("converter", Json(kConverterVersion)); st.set("dump", Json(fp)); st.set("make", Json(GEN57_MAKE_VERSION)); st.set("engine", Json("cpp"));
    st.set("only", Json(a.gen57_only)); st.set("done", Json::array());
    write_all(work / "state.json", st.dump(-1));
  }
  auto is_done = [&](const char* n) { for (auto& x : st.find("done")->a) if (x.s == n) return true; return false; };
  auto mark = [&](const char* n) { if (!is_done(n)) st.find("done")->push(Json(n)); write_all(work / "state.json.t", st.dump(-1)); fs::rename(work / "state.json.t", work / "state.json"); };
  const std::set<std::string> skip = [&] { std::set<std::string> s; std::stringstream os(a.gen57_skip); std::string x; while (std::getline(os, x, ',')) if (!x.empty()) s.insert(x); return s; }();
  if (!fs::is_regular_file(fs::path(a.gen4_pack) / "files" / "GSfsys.toc")) throw Fail("The Gen 4 pack is not there yet (" + a.gen4_pack + "): it is made first.");
  if (plan_all.items.empty()) {
    PlanOptions p2 = po;
    p2.packs = {"g57", "form", "gen4"};
    plan_all = make_plan(r, d, g, *disc, p2);
  }
  size_t nspecies = 0;
  for (int n = 494; n <= 807; n++) if (only57.empty() || only57.count(n)) nspecies++;

  say("stage tables 8 9");
  DataOptions dopt;
  dopt.rom = a.rom; dopt.xd_disc = a.xd_disc; dopt.data_dir = a.data_dir; dopt.gen4_pack = a.gen4_pack; dopt.work = work.string(); dopt.data_mods_exe = a.data_mods_exe;
  dopt.eol_crlf = a.eol_crlf; dopt.only = only57; dopt.converter_version = kConverterVersion; dopt.models_dir = (work / "models").string();
  dopt.log = log;
  load_xd_tables(a.data_mods_exe, a.xd_disc, work.string());
  say("progress 1 1 tables");

  const fs::path mdir = work / "models";
  Ctx c{&g, &xd, mdir.string(), false, fp, "", (mdir / "gen57").string(), "", ""};
  auto run_models = [&](const char* pack, const char* stage_label) {
    std::vector<const PlanItem*> list;
    for (auto& it : plan_all.items) if (it.pack == pack && (only57.empty() || only57.count(it.nat))) list.push_back(&it);
    std::atomic<size_t> next{0}, done{0};
    const size_t total = list.size();
    auto wk = [&]() {
      while (true) {
        const size_t k = next.fetch_add(1);
        if (k >= total) return;
        try { make_one(c, *list[k]); } catch (const std::exception&) {}
        const size_t dn = ++done;
        say(std::string(pack == std::string("g57") ? "progress " : "progress ") + std::to_string(dn) + " " + std::to_string(total) + " " + stage_label + " " + list[k]->name);
      }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < a.jobs; i++) th.emplace_back(wk);
    for (auto& t : th) t.join();
  };
  fs::path pack_dir = work / "pack", pack_f = work / "pack_forms";
  if (!is_done("pack") && fs::exists(pack_dir / "gen57.json")) mark("pack");
  ModelResults mr;
  mr.present = true;
  if (!is_done("pack")) {
    say("stage models 9 44");
    run_models("g57", "models");
    say("stage data 44 47");
    for (auto& it : plan_all.items) (void)it;
    read_model_results(mdir.string(), a.data_dir, plan_all.items, mr);
    dopt.out = pack_dir.string();
    wipe(pack_dir);
    make_gen57_data(dopt, plan_all, mr);
    mark("pack");
  } else {
    read_model_results(mdir.string(), a.data_dir, plan_all.items, mr);
  }
  if (!is_done("forms")) {
    say("stage forms 47 55");
    if (!skip.count("forms")) run_models("form", "form model");
    read_model_results(mdir.string(), a.data_dir, plan_all.items, mr);
    wipe(pack_f);
    fs::copy(pack_dir, pack_f, fs::copy_options::recursive);
    if (!skip.count("forms")) {
      dopt.out = pack_f.string();
      make_gen57_forms_data(dopt, plan_all, mr);
    }
    mark("forms");
    wipe(pack_dir);
  }
  if (!is_done("faces")) {
    say("stage faces 55 76");
    if (skip.count("faces")) log("faces skipped (test run)");
    else {
      FacesArgs fa;
      fa.xd_exe = a.xd_exe; fa.xd_disc = a.xd_disc; fa.pack = pack_f.string(); fa.gen4_pack = fs::absolute(a.gen4_pack).string(); fa.work = (work / "faces").string();
      fa.data_dir = a.data_dir; fa.dump_fp = fp; fa.models = &g; fa.jobs = a.jobs; fa.xd_runs = xd_runs(); fa.eol_crlf = a.eol_crlf;
      for (auto& it : plan_all.items) if (it.pack == "g57" && it.skip.empty()) fa.nat_group[it.nat] = it.group;
      for (int n = 494; n <= 807; n++) if (only57.empty() || only57.count(n)) fa.nats.insert(n);
      Json gen4species;
      const fs::path gen4json = fs::path(a.gen4_pack) / "data" / "02_species.json";
      const bool gen4_from_dump = [&] {
        try { return Json::parse(read_all(fs::path(a.gen4_pack) / "gen4.json")).find("generated_from")->s.compare(0, 6, "CTR-P-") == 0; } catch (const std::exception&) { return false; }
      }();
      if (gen4_from_dump) {
        try {
          gen4species = Json::parse(read_all(gen4json));
          const Json* add = gen4species.find("add");
          const Json* pk = add ? add->find("pokemon") : nullptr;
          std::map<int, int> rowOf;
          if (pk) for (auto& kv : pk->o) {
            const Json* np = kv.second.find("numPokemon");
            const Json* pr = kv.second.find("pkxDataId");
            if (np && pr && pr->i != 418) rowOf[(int)np->i] = (int)pr->i;
          }
          const Json* ed = gen4species.find("pokemon");
          if (ed) for (auto& kv : ed->o) {
            const Json* np = kv.second.find("numPokemon");
            const Json* pr = kv.second.find("pkxDataId");
            if (np && pr && pr->i != 418) rowOf[(int)np->i] = (int)pr->i;
          }
          for (auto& it : plan_all.items) {
            if (it.pack != "gen4" || it.nat == 438 || !it.skip.empty() || !rowOf.count(it.nat)) continue;
            const fs::path f = fs::path(a.gen4_pack) / "files" / ("pkx_g4_" + it.model + ".fsys");
            if (!fs::exists(f)) continue;
            fa.extra[it.nat] = {f.string(), rowOf[it.nat]};
            fa.nat_group[it.nat] = it.group;
            fa.nats.insert(it.nat);
          }
          fa.extra_pack = fs::absolute(a.gen4_pack).string();
          log("Gen 4 faces: " + std::to_string(fa.extra.size()) + " species to draw");
        } catch (const std::exception& e) { log(std::string("Gen 4 faces: not drawn (") + e.what() + ")"); fa.extra.clear(); }
      }
      fa.run = [](const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>* env, const std::string& lf) { return run_process(exe, args, env, lf); };
      fa.progress = [](int dn, int tt, const std::string& w) { say("progress " + std::to_string(dn) + " " + std::to_string(tt) + " " + w); };
      fa.log = log;
      if (a.xd_exe.empty() || !fs::is_regular_file(a.xd_exe)) {
        log("faces: the game program (xd) was not found: the faces are not drawn");
        say("progress 1 1 faces (not drawn: xd was not found)");
      } else {
        try { make_faces(fa); } catch (const Fail&) { throw; } catch (const std::exception& e) { throw Fail(std::string("The faces could not be drawn (") + e.what() + ")."); }
        if (!fa.extra.empty()) {
          try {
            const std::string before = read_all(gen4json);
            if (eolfix(gen4species.dump(1, true), a.eol_crlf) != before) {
              log("Gen 4 faces: the species file does not read back as it is (left alone: the species keep their stand-ins' faces)");
            } else {
              Json* pk = gen4species.find("add")->find("pokemon");
              int n = 0;
              for (auto& kv : pk->o) {
                const Json* np = kv.second.find("numPokemon");
                if (!np || !fa.extra.count((int)np->i)) continue;
                const fs::path png = fs::path(a.gen4_pack) / "textures" / "faces" / (std::to_string(np->i) + ".png");
                if (!fs::exists(png)) continue;
                kv.second.set("face", Json("textures/faces/" + std::to_string(np->i) + ".png"));
                if (fs::exists(fs::path(a.gen4_pack) / "textures" / "icons" / (std::to_string(np->i) + ".png"))) kv.second.set("icon", Json("textures/icons/" + std::to_string(np->i) + ".png"));
                n++;
              }
              if (Json* ed = gen4species.find("pokemon")) {
                for (auto& kv : ed->o) {
                  const Json* np = kv.second.find("numPokemon");
                  if (!np || !fa.extra.count((int)np->i)) continue;
                  const fs::path png = fs::path(a.gen4_pack) / "textures" / "faces" / (std::to_string(np->i) + ".png");
                  if (fs::exists(png)) kv.second.set("face", Json("textures/faces/" + std::to_string(np->i) + ".png"));
                  if (fs::exists(fs::path(a.gen4_pack) / "textures" / "icons" / (std::to_string(np->i) + ".png"))) kv.second.set("icon", Json("textures/icons/" + std::to_string(np->i) + ".png"));
                  if (fs::exists(png)) n++;
                }
              }
              write_all(gen4json, eolfix(gen4species.dump(1, true), a.eol_crlf));
              log("Gen 4 faces: " + std::to_string(n) + " species rows point at their own face");
            }
          } catch (const std::exception& e) { log(std::string("Gen 4 faces: species file not changed (") + e.what() + ")"); }
        }
      }
    }
    mark("faces");
  }
  const fs::path ent = work / "entries";
  if (!is_done("entries")) {
    say("stage entries 76 78");
    wipe(ent);
    fs::create_directories(ent);
    if (make_dex_entries(a.rom, ent.string(), log) == 0) throw Fail("The Pokedex entry texts could not be read from your dump.");
    say("progress 1 1 entries");
    mark("entries");
  }
  const fs::path pics = work / "pictures";
  if (!is_done("pictures")) {
    say("stage pictures 78 95");
    if (skip.count("pictures")) log("pictures skipped (test run)");
    else make_pictures(a, pack_f.string(), fs::absolute(a.gen4_pack).string(), pics.string(), 1, [](int dn, int tt, const std::string& w) { say("progress " + std::to_string(dn) + " " + std::to_string(tt) + " " + w); }, log);
    mark("pictures");
  }
  say("stage check 95 100");
  auto problems = check_pack(pack_f.string(), nspecies, a.gen57_skip, !skip.count("faces"));
  if (!problems.empty()) {
    std::string t;
    for (size_t i = 0; i < problems.size() && i < 4; i++) t += (i ? " " : "") + problems[i];
    throw Fail("The finished pack did not pass its own checks: " + t);
  }
  Json info = Json::parse(read_all(pack_f / "gen57.json"));
  info.set("converter", Json(kConverterVersion));
  info.set("dump", Json(fp));
  info.set("make", Json(GEN57_MAKE_VERSION));
  info.set("complete", Json(true));
  write_all(pack_f / "gen57.json", eolfix(info.dump(1, true), a.eol_crlf));
  {
    const fs::path old = pack_out + ".old";
    wipe(old);
    if (fs::exists(pack_out)) fs::rename(pack_out, old);
    fs::rename(pack_f, pack_out);
    wipe(old);
  }
  for (auto& e : fs::directory_iterator(ent)) {
    const fs::path dst = fs::path(pack_out).parent_path() / "pokedex";
    fs::create_directories(dst);
    fs::rename(e.path(), dst / e.path().filename());
  }
  int n_pics = 0;
  std::string pics_out = a.pictures_out;
  if (pics_out.empty() && !a.data_folder.empty()) pics_out = (fs::path(a.data_folder) / "icons" / "pokedex").string();
  if (!pics_out.empty() && fs::is_directory(pics)) {
    fs::create_directories(pics_out);
    for (auto& e : fs::directory_iterator(pics)) if (e.path().extension() == ".png") { fs::rename(e.path(), fs::path(pics_out) / e.path().filename()); n_pics++; }
  }
  const long long size = (long long)dir_bytes(pack_out);
  const int sp_n = info.find("species") ? (int)info.find("species")->i : 0, fo_n = info.find("forms") ? (int)info.find("forms")->i : 0;
  log("done: " + std::to_string(sp_n) + " species, " + std::to_string(fo_n) + " forms, " + std::to_string(size) + " bytes, " + std::to_string(n_pics) + " pictures");
  lg.close();
  wipe(work);
  say("done " + std::to_string(sp_n) + " " + std::to_string(fo_n) + " " + std::to_string(size));
  return 0;
}
