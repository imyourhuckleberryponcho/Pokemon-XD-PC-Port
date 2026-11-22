#include "xd_mods_reload.h"

#include "i18n.h"
#include "xd_scripts.h"
#include "xd_test_run.h"
#include "xd_user_folder.h"
#include "xd_xg.h"

#include <data_mods.h>
#include <mods.h>
#include <settings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void PortVideo_Notice(const char* text, double seconds);
void PortInput_ModsNote(const char* what);
uint32_t PortVI_GetFlipCount(void);
unsigned char fightIsFight(void);
int floorGetFloorState(void);
void PortXD_SetActiveMods(const char* scriptsText);
}

namespace fs = std::filesystem;
namespace em = engine::mods;
using engine::i18n::Tr;

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof buf, format, args);
  va_end(args);
  return buf;
}

constexpr uint64_t kBasis = 0xcbf29ce484222325ull;
uint64_t Fnv(uint64_t h, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}
uint64_t FnvText(uint64_t h, const std::string& s) {
  h = Fnv(h, s.data(), s.size());
  return Fnv(h, "|", 1);
}
uint64_t FnvNum(uint64_t h, uint64_t v) { return Fnv(h, &v, sizeof v); }

std::string Utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

uint64_t FileStamp(uint64_t h, const fs::path& p) {
  std::error_code ec;
  h = FnvText(h, Utf8(p));
  h = FnvNum(h, static_cast<uint64_t>(fs::file_size(p, ec)));
  const auto t = fs::last_write_time(p, ec);
  return FnvNum(h, static_cast<uint64_t>(t.time_since_epoch().count()));
}

uint64_t TreeStamp(uint64_t h, const fs::path& root, bool recursive) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return h;
  std::vector<fs::path> files;
  if (recursive) {
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      const std::string name = Utf8(it->path().filename());
      if (name == ".git" || name == "__pycache__") {
        it.disable_recursion_pending();
        continue;
      }
      std::error_code e2;
      if (it->is_regular_file(e2)) files.push_back(it->path());
    }
  } else {
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      if (it->is_regular_file(e2)) files.push_back(it->path());
    }
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) h = FileStamp(h, f);
  return h;
}

uint64_t RestartStamp() {
  const fs::path user = xd::SettingsFolder();
  std::error_code ec;
  uint64_t h = FnvText(kBasis, "restart");
  std::vector<fs::path> files;
  for (fs::recursive_directory_iterator it(user / L"mods", fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = Utf8(it->path().filename());
    std::error_code e2;
    if (it->is_directory(e2)) {
      if (name == "scripts" || name == "textures" || name == ".git" || name == "__pycache__") it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(e2)) files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) h = FileStamp(h, f);
  return FileStamp(h, user / em::kListFile);
}

uint64_t ScriptsDigest(const em::LoadResult& r) {
  uint64_t h = kBasis;
  for (const em::ScriptFile& s : r.scripts) h = FnvText(FnvText(FnvText(h, s.modId), s.name), s.text);
  return h;
}

uint64_t PacksStamp(const std::vector<xd::hd::Pack>& packs) {
  uint64_t h = kBasis;
  for (const xd::hd::Pack& p : packs) {
    h = FnvText(h, Utf8(p.folder));
    h = FnvNum(h, p.loose ? 1 : 0);
    h = TreeStamp(h, p.folder, !p.loose);
  }
  return h;
}

std::mutex sMutex;
em::LoadOptions sOptions;
bool sHave = false;
uint64_t sRestartBoot = 0, sScriptsNow = 0, sPacksNow = 0;
std::shared_ptr<const em::DataCatalog> sCatalog;
std::atomic<bool> sPending{false}, sWatchChanged{false}, sStopWatch{false}, sEnabledFlag{false};
std::string sPendingWhy;
bool sWaitingSaid = false;
std::thread sWatcher;

uint64_t WatchStamp() {
  const fs::path user = xd::SettingsFolder();
  uint64_t h = kBasis;
  h = TreeStamp(h, user / L"mods", true);
  h = TreeStamp(h, user / L"textures", true);
  return FileStamp(h, user / em::kListFile);
}

void WatchMain() {
  bool have = false;
  uint64_t last = 0;
  while (!sStopWatch.load()) {
    for (int i = 0; i < 25 && !sStopWatch.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (sStopWatch.load()) break;
    if (!sEnabledFlag.load()) {
      have = false;
      continue;
    }
    const uint64_t now = WatchStamp();
    if (!have) {
      last = now;
      have = true;
    } else if (now != last) {
      last = now;
      sWatchChanged = true;
    }
  }
}

struct WatcherJoiner {
  ~WatcherJoiner() {
    sStopWatch = true;
    if (sWatcher.joinable()) sWatcher.join();
  }
} sJoiner;

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}

}

namespace xd::modreload {

void Remember(const em::LoadOptions& options, const em::LoadResult& result) {
  std::lock_guard<std::mutex> lock(sMutex);
  sOptions = options;
  sOptions.listReadOnly = true;
  sHave = true;
  sRestartBoot = RestartStamp();
  sCatalog = result.catalog;
  sScriptsNow = ScriptsDigest(result);
  sPacksNow = PacksStamp(TexturePacksOf(result, options));
}

bool Enabled() {
  engine::settings::Registry& reg = engine::settings::Global();
  static const size_t sIndex = reg.Find("system.mods_hot_reload");
  if (sIndex == engine::settings::kNoSetting) return false;
  if (reg.IsOriginal() || reg.CurrentPreset() == engine::settings::Preset::Original) return false;
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return false;
  if (xd::TestRun() && !EnvIs("XD_MODS_RELOAD_TEST", "1")) return false;
  if (xd::xg::Off(xd::xg::Feature::Mods) != nullptr) return false;
  return reg.Bool("system.mods_hot_reload").Get() || EnvIs("XD_MODS_RELOAD_TEST", "1");
}

bool QuietNow() {
  if (fightIsFight() != 0) return false;
  return floorGetFloorState() != 0;
}

Outcome ReloadNow(const char* why) {
  Outcome out;
  em::LoadOptions options;
  bool have;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    options = sOptions;
    have = sHave;
  }
  if (!have) {
    out.waiting = Tr("No mods were loaded when the game started.");
    return out;
  }
  const auto start = std::chrono::steady_clock::now();
  {
    const std::shared_ptr<const em::DataCatalog> catalog = sCatalog;
    options.dataCatalog = [catalog](const em::EntryReader&, em::DataCatalog& out, std::string& error) {
      if (!catalog) {
        error = "no tables were read at start-up";
        return false;
      }
      out = *catalog;
      return true;
    };
  }
  const em::LoadResult result = em::Load(options);
  out.ran = true;
  for (const em::Message& m : result.messages) {
    if (m.level != em::Message::Level::Info && m.text.find("script") != std::string::npos) {
      std::fprintf(stderr, "[mods reload] %s: %s\n", m.level == em::Message::Level::Problem ? "problem" : "warning", m.text.c_str());
    }
  }
  std::string parts;
  const uint64_t scripts = ScriptsDigest(result);
  if (scripts != sScriptsNow) {
    xd::scripts::Install(result);
    sScriptsNow = scripts;
    out.scripts = true;
    parts += Tr("scripts");
    PortXD_SetActiveMods(xd::scripts::ActiveText().c_str());
  }
  const std::vector<xd::hd::Pack> packs = TexturePacksOf(result, options);
  const uint64_t stamp = PacksStamp(packs);
  if (stamp != sPacksNow) {
    if (xd::hd::ReloadPacks(packs)) {
      out.textures = true;
      parts += (parts.empty() ? "" : ", ") + std::string(Tr("textures"));
    }
    sPacksNow = stamp;
  }
  if (RestartStamp() != sRestartBoot) {
    out.restart = true;
    out.restartWhat = Tr("data values and text, replaced files, or which mods are on");
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::string line = parts.empty() ? std::string(Tr("Mods reloaded: no script or texture changes.")) : Fmt(Tr("Mods reloaded: %s."), parts.c_str());
  if (out.restart) line += " " + Fmt(Tr("Restart the game to use changes to %s."), out.restartWhat.c_str());
  if (out.scripts && !xd::scripts::LoadProblems().empty()) line += " " + std::string(Tr("A script could not be loaded: see the log."));
  out.notice = line;
  std::fprintf(stderr, "[mods reload] %s: %s | scripts %s, textures %s, restart needed: %s | %.0f ms\n", why, line.c_str(), out.scripts ? "reloaded" : "same",
               out.textures ? "reloaded" : "same", out.restart ? out.restartWhat.c_str() : "no", ms);
  const std::string note = Fmt("reload %s scripts=%s textures=%s restart=%s", why, out.scripts ? "yes" : "no", out.textures ? "yes" : "no",
                               out.restart ? "needed" : "no");
  PortInput_ModsNote(note.c_str());
  PortVideo_Notice(line.c_str(), 8.0);
  return out;
}

void Request(const char* why) {
  if (!Enabled()) return;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    sPendingWhy = why;
  }
  sWaitingSaid = false;
  sPending = true;
}

}

extern "C" {

void PortXD_ModReloadKey(void) { xd::modreload::Request("key F6"); }

void PortXD_ModReloadFrame(void) {
  static bool testRead = false;
  static std::vector<uint32_t> testFrames;
  if (!testRead) {
    testRead = true;
    if (const char* at = std::getenv("XD_MODS_RELOAD_AT")) {
      for (const char* p = at; *p;) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(p, &end, 10);
        if (end == p) break;
        testFrames.push_back(static_cast<uint32_t>(v));
        p = *end == ',' ? end + 1 : end;
      }
    }
  }
  const bool enabled = xd::modreload::Enabled();
  sEnabledFlag = enabled;
  if (!enabled) return;
  if (!sWatcher.joinable() && !sStopWatch.load()) sWatcher = std::thread(WatchMain);
  const uint32_t frame = PortVI_GetFlipCount();
  for (size_t i = 0; i < testFrames.size(); i++) {
    if (frame >= testFrames[i]) {
      testFrames.erase(testFrames.begin() + static_cast<long>(i));
      xd::modreload::Request("test");
      break;
    }
  }
  if (sWatchChanged.exchange(false)) xd::modreload::Request("a mod file changed");
  if (!sPending.load()) return;
  if (!xd::modreload::QuietNow()) {
    if (!sWaitingSaid) {
      sWaitingSaid = true;
      std::fprintf(stderr, "[mods reload] waiting for a quiet moment (not in a battle, a map loaded)\n");
    }
    return;
  }
  sPending = false;
  std::string why;
  {
    std::lock_guard<std::mutex> lock(sMutex);
    why = sPendingWhy;
  }
  xd::modreload::ReloadNow(why.c_str());
}

const char* PortXD_ModReloadMenu(void) {
  static std::string line;
  if (!xd::modreload::Enabled()) {
    line = Tr("Reloading mods while playing is off. Turn on \"Reload mods while playing\" on the SETTINGS page (a developer setting).");
    return line.c_str();
  }
  const xd::modreload::Outcome o = xd::modreload::ReloadNow("menu");
  line = o.ran ? o.notice : (o.waiting.empty() ? std::string(Tr("Nothing was reloaded.")) : o.waiting);
  return line.c_str();
}

bool PortXD_ModReloadEnabled(void) { return xd::modreload::Enabled(); }

}
