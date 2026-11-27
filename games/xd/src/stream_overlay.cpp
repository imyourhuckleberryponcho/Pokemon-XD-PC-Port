#include "stream_overlay.h"

#include "stream_overlay_assets.h"

#include "xd_forms.h"
#include "xd_pokedex.h"
#include "xd_speedrun.h"
#include "xd_test_run.h"
#include "ui_kit2.h"
#include "xd_user_folder.h"

#include <gs_formats.h>
#include <hd_textures.h>
#include <gx_texture_decode.h>
#include <settings.h>

#include <dolphin/types.h>
#include <game/faceData.h>
#include <game/gstexture.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/relglobal.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern "C" {
u16* GSmsgGetGSchar(u32 msg);
const char* PortXD_DiscPath(void);
void* itemDataBiosGetPtr(u16 id);
u32 itemDataBiosGetName(void* item);
uintptr_t heroGetStatus(void* hero, u32 selector, u16 index);
uint8_t pokemonCheckValid(void* pokemon);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
uint8_t pokemonBiosGetLevel(void* pokemon);
u32 pokemonGetJoutaiDataId(void* pokemon);
uint8_t pokemonIsDarkPokemon(void* pokemon);
f32 pokemonGetDp(void* pokemon);
uint16_t pokemonBiosGetDarkpokemonDataId(void* pokemon);
u32 darkPokemonBiosGetDarkPoint(u16 id);
u8 pokemonBiosGetTamagoFlag(void* pokemon);
u16 pokemonBiosGetItemDataId(void* pokemon);
char* pokemonBiosGetNicknamePtr(void* pokemon);
s32 menuSubGetPokemonSexForDisp(void* pokemon);
u32 pokemonGetLevelToExp(void* pokemon, uint8_t level);
s32 GSfloorGetCurrent(void);
extern void* floordata;
u8 fightIsFight(void);
uint32_t PortVI_GetFlipCount(void);
void* fightFloorGetGcHeroFightTrainerPtr(void* floor);
void* fightTrainerGetValidFightPokemonPtr(void* trainer, uint16_t index);
void* fightTrainerGetValidFightOutPokemonPtr(void* trainer, uint16_t index);
void* fightPokemon_GetPokemonBuffPtr(void* fightPokemon);
void* fightPokemon_GetMotoPokemonPtr(void* fightPokemon);
void* fightTrainer_GetFightoutPokemonPtr(void* trainer, uint16_t index);
u8 fightOutPokemonCheckValid(void* fop);
u32 joutaiDataBiosGetName(u16 id);
void* fightOutPokemon_GetMotoFightPokemonPtr(void* fop);
void* fightTrainerGetFightPokemonPtrToFightOutPokemonPtr(void* trainer, void* fightPokemon);
void* GSresGetResourceAllGroup(u32 id);
s8 menuFaceSearchEnable(u16 id);
}

namespace xd::stream {

namespace {
namespace fs = std::filesystem;
namespace es = engine::settings;
using Clock = std::chrono::steady_clock;

constexpr int kVersion = 2;
constexpr int kPageVersion = 2;
constexpr const char* kPageMarker = "<!-- xd-stream-overlay-page version ";

#include "stream_overlay_pages.inc"

const char* const kLooks[] = {"belt", "showcase", "native"};

std::string PageText(const std::string& look) {
  const char* const* chunks = look == "showcase" ? kPage_showcase : look == "native" ? kPage_native : kPage_belt;
  std::string t;
  for (; *chunks != nullptr; chunks++) t += *chunks;
  return t;
}

struct Member {
  int slot = 0;
  uint16_t row = 0;
  int national = 0;
  std::string name, nickname, item, status = "none";
  int level = 0, hp = 0, maxHp = 0, heart = 0, heartMax = 0;
  bool shiny = false, shadow = false, egg = false, onField = false;
  std::string face;
  std::string still, small;
  int stillW = 0, stillH = 0;
  std::string gender = "none";
  long exp = 0, expStart = 0, expNext = 0;
  struct Idle {
    std::string src;
    int frames = 0, w = 0, h = 0;
    double fps = 15;
  } idle;
};

struct WriteJob {
  std::string json, js;
  bool facesOnly = false;
};
struct FaceJob {
  uint16_t row = 0;
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  bool shiny = false;
};

fs::path sFolder;
bool sLog = false;
bool sActive = false;
bool sStarted = false;
bool sAtExit = false;
bool sHadTeam = false;

std::mutex sMu;
std::condition_variable sCv;
std::deque<WriteJob> sJobs;
std::deque<FaceJob> sFaceJobs;
bool sQuit = false;
std::thread sThread;
std::set<uint16_t> sSmallHave, sSmallShinyHave;
std::map<uint16_t, std::pair<int, int>> sStillHave, sStillShinyHave;
std::atomic<unsigned> sFaceVersion{0};
bool sWriteErrorTold = false;

int64_t sFrames = 0, sTotalNs = 0, sMaxNs = 0, sWrites = 0;

std::string sLastText;
Clock::time_point sLastPush{};
bool sDirty = false;
unsigned sSeq = 0;
std::map<uint16_t, int> sFaceTry;
std::map<uint16_t, std::pair<int, Member::Idle>> sIdleTry[2];
std::string sLook = "belt";

bool Explicit() {
  const char* e = std::getenv("XD_STREAM_OVERLAY");
  if (e != nullptr && e[0] == '1') return true;
  const char* s = std::getenv("XD_SETTINGS");
  return s != nullptr && std::strstr(s, "system.stream_overlay") != nullptr;
}

bool Allowed() {
  static int allowed = -1;
  if (allowed < 0) {
    if (std::getenv("XD_STREAM_OVERLAY_HELPER") != nullptr) allowed = 0;
    else if (Explicit()) allowed = 1;
    else {
      const char* replay = std::getenv("XD_BATTLE_REPLAY");
      allowed = (!xd::TestRun() && (replay == nullptr || replay[0] == 0)) ? 1 : 0;
    }
    if (!allowed) std::fprintf(stderr, "[stream overlay] off: this is a test run (XD_STREAM_OVERLAY=1 asks for it)\n");
  }
  return allowed != 0;
}

bool SettingOn() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("system.stream_overlay");
  return i != es::kNoSetting && reg.ValueOf(i).i != 0;
}

void TellOnce(const char* what, const fs::path& p, const std::error_code& ec) {
  if (sWriteErrorTold) return;
  sWriteErrorTold = true;
  std::fprintf(stderr, "[stream overlay] %s %s failed (%s); the game goes on, nothing is written until this works again\n", what, p.string().c_str(),
               ec ? ec.message().c_str() : "write error");
}

bool WriteWhole(const fs::path& file, const std::string& text) {
  fs::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) { TellOnce("opening", tmp, std::error_code()); return false; }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    f.flush();
    if (!f) { TellOnce("writing", tmp, std::error_code()); return false; }
  }
  std::error_code ec;
  fs::rename(tmp, file, ec);
  if (ec) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) { TellOnce("replacing", file, ec); fs::remove(tmp, ec); return false; }
  }
  sWriteErrorTold = false;
  return true;
}

void WriteOurPage(const fs::path& page, const std::string& look, bool withLook) {
  std::error_code ec;
  const std::string marker = std::string(kPageMarker) + std::to_string(kPageVersion) + (withLook ? " look=" + look : std::string()) + " -->";
  bool write = true;
  if (fs::exists(page, ec)) {
    std::ifstream f(page, std::ios::binary);
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.compare(0, std::strlen(kPageMarker), kPageMarker) != 0) write = false;
    else write = line != marker;
  }
  if (write) WriteWhole(page, marker + "\n" + PageText(look));
}

void SyncPages() {
  for (const char* l : kLooks) WriteOurPage(sFolder / (std::string("overlay_") + l + ".html"), l, false);
  WriteOurPage(sFolder / "overlay.html", sLook, true);
}

void WriteReadme() {
  std::error_code ec;
  const fs::path readme = sFolder / "READ ME.txt";
  if (!fs::exists(readme, ec)) {
    WriteWhole(readme,
               "Stream overlay (Pokemon XD PC port)\r\n\r\n"
               "OBS: Sources > + > Browser. Tick \"Local file\" and pick a page from this folder. Width 1000, height 260.\r\n"
               "  overlay.html          the look chosen in the game's SETTINGS (Stream overlay look); it changes when you change the setting\r\n"
               "  overlay_belt.html     Battle belt   (always this look)\r\n"
               "  overlay_showcase.html Showcase      (always this look)\r\n"
               "  overlay_native.html   Native XD     (always this look)\r\n"
               "The game writes party.js and party.json here while the setting \"Stream overlay\" is on (nothing otherwise).\r\n"
               "The pictures (ui folder) and the letters (font folder) are made from YOUR copy of the game the first time the overlay runs\r\n"
               "(a few seconds, once). If a page shows plain boxes at first, wait a moment: it reloads by itself when they are there.\r\n"
               "A page is rewritten by the program only while its first line is its own version marker; for your own look copy a page to\r\n"
               "my_overlay.html and change the copy. Format and details: docs/STREAM_OVERLAY.md.\r\n"
               "No server and no internet is used. To stop: switch the setting off. To remove: delete this folder.\r\n");
  }
}

bool PngSize(const fs::path& file, int& w, int& h) {
  std::ifstream f(file, std::ios::binary);
  unsigned char b[24];
  static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (!f.read(reinterpret_cast<char*>(b), 24) || std::memcmp(b, kSignature, 8) != 0) return false;
  w = b[16] << 24 | b[17] << 16 | b[18] << 8 | b[19];
  h = b[20] << 24 | b[21] << 16 | b[22] << 8 | b[23];
  return w > 0 && h > 0 && w < 16384 && h < 16384;
}

bool CopyFile(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  if (!fs::exists(from, ec)) return false;
  fs::path tmp = to;
  tmp += ".tmp";
  fs::copy_file(from, tmp, fs::copy_options::overwrite_existing, ec);
  if (ec) return false;
  fs::rename(tmp, to, ec);
  return !ec;
}

struct StripWant {
  uint16_t row = 0;
  bool shiny = false;
  bool alone = false;
  bool operator<(const StripWant& o) const { return row != o.row ? row < o.row : shiny != o.shiny ? shiny < o.shiny : alone < o.alone; }
};
std::set<StripWant> sStripQueue, sStripDone, sStripBusy;
std::atomic<bool> sInBattle{false};
std::atomic<bool> sHelperStop{false};
std::thread sHelperThread;
std::atomic<int64_t> sHelperMs{0};
std::atomic<int> sIdleGen{0};
std::atomic<bool> sMovingOn{true};

bool HelperExe(fs::path& exe) {
#if defined(_WIN32)
  wchar_t buf[4096];
  const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
  if (n == 0 || n >= 4096) return false;
  exe = fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) != 0) return false;
  exe = fs::path(buf);
#else
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return false;
  buf[n] = 0;
  exe = fs::path(buf);
#endif
  return true;
}

std::vector<std::string> HelperEnvironment(const std::string& items, bool packs, const fs::path& faces, const fs::path& scratch) {
  static const char* const kDrop[] = {"XD_RENDER_", "XD_INPUT_", "XD_REPLAY", "XD_WARP", "XD_DEV_", "XD_PHOTO", "XD_MENU_SCRIPT", "XD_STREAM_OVERLAY", "XD_STATE_",
                                      "XD_CALL_", "XD_BATTLE_REPLAY", "XD_CAPTURE", "XD_AUDIO_DUMP", "XD_RECORD", "XD_SHOT", "XD_CARD_DIR", "XD_CACHE_DIR",
                                      "XD_PARENT_PID", "XD_TURBO", "XD_DETERMINISTIC", "XD_CHALLENGES", "XD_RUN_LABEL"};
  std::vector<std::string> env;
#if defined(_WIN32)
  LPWCH block = GetEnvironmentStringsW();
  for (const wchar_t* p = block; p != nullptr && *p != 0; p += std::wcslen(p) + 1) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
    std::string e(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, p, -1, e.data(), n, nullptr, nullptr);
    env.push_back(std::move(e));
  }
  FreeEnvironmentStringsW(block);
#else
  for (char** e = environ; e != nullptr && *e != nullptr; e++) env.emplace_back(*e);
#endif
  std::vector<std::string> out;
  for (const std::string& e : env) {
    bool drop = e.empty() || e[0] == '=';
    for (const char* d : kDrop) drop = drop || e.compare(0, std::strlen(d), d) == 0;
    if (!drop) out.push_back(e);
  }
  const auto u8 = [](const fs::path& p) { const std::u8string t = p.u8string(); return std::string(t.begin(), t.end()); };
  out.push_back("XD_RENDER_ICON=" + items);
  out.push_back("XD_RENDER_ICON_DIR=" + u8(faces));
  out.push_back("XD_RENDER_ICON_SIZE=512");
  out.push_back("XD_CARD_DIR=" + u8(scratch / "card"));
  out.push_back("XD_CACHE_DIR=" + u8(scratch / "cache"));
  out.push_back("XD_STREAM_OVERLAY_HELPER=1");
  out.push_back("XD_RUN_LABEL=stream overlay pictures");
  out.push_back("XD_NO_MESSAGE_BOXES=1");
  out.push_back("SDL_AUDIO_DRIVER=dummy");
  out.push_back("SDL_AUDIODRIVER=dummy");
  if (packs) out.push_back("XD_CHALLENGES=gen4_pokemon,gen57_pokemon");
#if defined(_WIN32)
  out.push_back("XD_PARENT_PID=" + std::to_string(static_cast<unsigned long>(GetCurrentProcessId())));
#else
  out.push_back("XD_PARENT_PID=" + std::to_string(static_cast<long>(getpid())));
#endif
  return out;
}

int RunHelper(const std::string& items, bool packs) {
  fs::path exe;
  const char* disc = PortXD_DiscPath();
  if (!HelperExe(exe) || disc == nullptr || disc[0] == 0) return -1;
  const fs::path faces = sFolder / "faces", scratch = sFolder / "helper";
  std::error_code ec;
  fs::create_directories(faces, ec);
  fs::create_directories(scratch / "card", ec);
  fs::create_directories(scratch / "cache", ec);
  const std::vector<std::string> env = HelperEnvironment(items, packs, faces, scratch);
#if defined(_WIN32)
  std::wstring envBlock;
  const auto wide = [](const std::string& t) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, w.data(), n);
    return w;
  };
  for (const std::string& e : env) {
    envBlock += wide(e);
    envBlock += L'\0';
  }
  envBlock += L'\0';
  std::wstring cmd = L"\"" + exe.wstring() + L"\" \"" + wide(disc) + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | BELOW_NORMAL_PRIORITY_CLASS, envBlock.data(),
                                 exe.parent_path().c_str(), &si, &pi);
  if (!ok) return -1;
  CloseHandle(pi.hThread);
  DWORD code = 0;
  for (int waited = 0;; waited += 250) {
    if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) break;
    if (sHelperStop.load() || waited > 15 * 60 * 1000) {
      TerminateProcess(pi.hProcess, 124);
      WaitForSingleObject(pi.hProcess, 5000);
      break;
    }
  }
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
#else
  std::vector<char*> envp;
  for (const std::string& e : env) envp.push_back(const_cast<char*>(e.c_str()));
  envp.push_back(nullptr);
  const std::string exeText = exe.string();
  std::string discText = disc;
  char* argv[] = {const_cast<char*>(exeText.c_str()), discText.data(), nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, exeText.c_str(), nullptr, nullptr, argv, envp.data()) != 0) return -1;
  setpriority(PRIO_PROCESS, static_cast<id_t>(pid), 10);
  int status = 0;
  for (int waited = 0;; waited += 250) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
    if (sHelperStop.load() || waited > 15 * 60 * 1000) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      return -3;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
#endif
}

void HelperMain() {
  for (;;) {
    std::vector<StripWant> batch;
    for (;;) {
      if (sHelperStop.load()) return;
      {
        std::lock_guard<std::mutex> g(sMu);
        if (!sStripQueue.empty() && !sInBattle.load()) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    for (int i = 0; i < 10 && !sHelperStop.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (sHelperStop.load()) return;
    {
      std::lock_guard<std::mutex> g(sMu);
      for (const StripWant& w : sStripQueue) {
        if (!batch.empty() && (w.alone || batch.front().alone)) continue;
        if (batch.size() < 6) batch.push_back(w);
      }
      for (StripWant w : batch) {
        sStripQueue.erase(w);
        w.alone = false;
        sStripBusy.insert(w);
      }
    }
    if (batch.empty()) continue;
    std::string items;
    bool packs = false;
    for (const StripWant& w : batch) {
      items += (items.empty() ? "" : ",") + std::string("strip:") + std::to_string(w.row) + (w.shiny ? ":shiny" : "");
      packs = packs || w.row >= 415;
    }
    const auto t0 = Clock::now();
    const int code = RunHelper(items, packs);
    const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    sHelperMs += ms;
    if (sLog || code != 0) std::fprintf(stderr, "[stream overlay] moving pictures for %s: helper ended with %d after %.1f s\n", items.c_str(), code, ms / 1000.0);
    {
      std::error_code ec;
      std::lock_guard<std::mutex> g(sMu);
      for (StripWant w : batch) {
        w.alone = false;
        sStripBusy.erase(w);
      }
      for (StripWant w : batch) {
        const bool made = fs::exists(sFolder / "faces" / (std::to_string(w.row) + (w.shiny ? "_idle_shiny" : "_idle") + ".txt"), ec);
        if (!made && batch.size() > 1 && !w.alone) {
          w.alone = true;
          sStripQueue.insert(w);
        } else {
          w.alone = false;
          sStripDone.insert(w);
        }
      }
    }
    sIdleGen++;
  }
}

void WantIdle(const Member& m) {
  if (!sMovingOn || m.egg || m.idle.frames > 0) return;
  std::lock_guard<std::mutex> g(sMu);
  const StripWant w{m.row, m.shiny, false};
  const StripWant alone{m.row, m.shiny, true};
  if (sStripDone.count(w) == 0 && sStripBusy.count(w) == 0 && sStripQueue.count(w) == 0 && sStripQueue.count(alone) == 0) sStripQueue.insert(w);
}

void ThreadMain() {
  const fs::path pictures = xd::UserFolder() / "icons" / "pokedex";
  std::unique_lock<std::mutex> lk(sMu);
  for (;;) {
    sCv.wait(lk, [] { return sQuit || !sJobs.empty() || !sFaceJobs.empty(); });
    while (!sFaceJobs.empty()) {
      FaceJob j = std::move(sFaceJobs.front());
      sFaceJobs.pop_front();
      lk.unlock();
      std::error_code ec;
      fs::create_directories(sFolder / "faces", ec);
      const std::string n = std::to_string(j.row);
      bool changed = false;
      if (j.rgba.empty()) {
        for (int sh = 0; sh < 2; sh++) {
          const std::string suffix = sh ? "_shiny.png" : ".png";
          bool have;
          {
            std::lock_guard<std::mutex> g(sMu);
            have = (sh ? sStillShinyHave : sStillHave).count(j.row) != 0;
          }
          if (have) continue;
          const fs::path to = sFolder / "faces" / (n + suffix);
          if (CopyFile(pictures / ("pokemon_" + n + suffix), to)) {
            int w = 0, h = 0;
            if (!PngSize(to, w, h)) { fs::remove(to, ec); continue; }
            std::lock_guard<std::mutex> g(sMu);
            (sh ? sStillShinyHave : sStillHave)[j.row] = {w, h};
            changed = true;
          }
        }
      } else {
        const bool have = [&] {
          std::lock_guard<std::mutex> g(sMu);
          return (j.shiny ? sSmallShinyHave : sSmallHave).count(j.row) != 0;
        }();
        if (!have && engine::hdtex::WritePngRgba(sFolder / "faces" / (n + (j.shiny ? "_small_shiny.png" : "_small.png")), static_cast<uint32_t>(j.w), static_cast<uint32_t>(j.h), j.rgba.data())) {
          std::lock_guard<std::mutex> g(sMu);
          (j.shiny ? sSmallShinyHave : sSmallHave).insert(j.row);
          changed = true;
        }
      }
      lk.lock();
      if (changed) sFaceVersion++;
    }
    if (!sJobs.empty()) {
      WriteJob j = std::move(sJobs.back());
      sJobs.clear();
      lk.unlock();
      std::error_code ec;
      fs::create_directories(sFolder, ec);
      const bool a = WriteWhole(sFolder / "party.json", j.json);
      const bool b = WriteWhole(sFolder / "party.js", j.js);
      if (a && b) sWrites++;
      lk.lock();
    }
    if (sQuit && sJobs.empty() && sFaceJobs.empty()) return;
  }
}

std::string Utf8Of(const uint16_t* chars) { return xd::pokedex::Utf8(chars); }

std::string StatusOf(uint32_t condition) {
  switch (condition) {
    case 3: return "poison";
    case 4: return "bad_poison";
    case 5: return "paralysis";
    case 6: return "burn";
    case 7: return "freeze";
    case 8: return "sleep";
    default: return "none";
  }
}

void SpeciesInfo(uint16_t row, int& national, std::string& name) {
  national = 0;
  name.clear();
  if (pokemon_data == nullptr || pokemon_data_number == nullptr || row == 0 || row >= *pokemon_data_number) return;
  const PokemonData& d = pokemon_data[row];
  national = d.numPokemon;
  if (national == 0 && xd::forms::Loaded()) {
    const uint32_t base = xd::forms::BaseOf(row);
    if (base != 0 && base < *pokemon_data_number) national = pokemon_data[base].numPokemon;
  }
  if (d.name != 0) name = Utf8Of(GSmsgGetGSchar(d.name));
}

bool DiscFaceTexture(u32 texID, std::vector<uint8_t>& rgba, int& w, int& h) {
  static std::vector<uint8_t> bytes;
  static FsysArchive fsys{};
  static int state = 0;
  if (state == 0) {
    bytes = xd::kit2::DiscFile("poke_face.fsys");
    state = !bytes.empty() && Fsys_Open(bytes.data(), bytes.size(), &fsys) ? 1 : 2;
  }
  if (state != 1) return false;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e) || e.type != 9 || e.id != texID) continue;
    uint32_t size = 0;
    uint8_t* data = Fsys_Extract(&fsys, i, &size);
    if (data == nullptr) return false;
    const bool ok = xd::kit2::DecodeTexture(data, size, rgba, w, h);
    std::free(data);
    return ok;
  }
  return false;
}

void TryGameFace(uint16_t row) {
  if (pokemon_data == nullptr || pokemon_data_number == nullptr || row >= *pokemon_data_number || poke_face == nullptr || poke_face_number == nullptr) return;
  const uint16_t faceId = pokemon_data[row].cgData[0].spriteId;
  if (faceId == 0 || faceId >= *poke_face_number) return;
  const u32 texID = static_cast<const MenuFaceData*>(poke_face)[faceId].texID;
  if (texID == 0) return;
  std::vector<uint8_t> all;
  int w = 0, h = 0;
  const auto* tex = menuFaceSearchEnable(faceId) == 1 ? static_cast<const GStexture*>(GSresGetResourceAllGroup(texID)) : nullptr;
  if (tex != nullptr) {
    const void* data = GXGetTexObjData(&tex->texObj);
    w = GXGetTexObjWidth(&tex->texObj), h = GXGetTexObjHeight(&tex->texObj);
    const u32 fmt = GXGetTexObjFmt(&tex->texObj);
    const u32 tlutFmt = tex->tlutFormat == 2 ? GX_TL_RGB565 : tex->tlutFormat == 3 ? GX_TL_RGB5A3 : GX_TL_IA8;
    all.assign(static_cast<size_t>(w) * h * 4, 0);
    if (data == nullptr || w == 0 || h == 0 || !PortGX_DecodeTexture(data, w, h, fmt, tex->tlutData, tlutFmt, all.data())) all.clear();
  }
  if (all.empty() && !DiscFaceTexture(texID, all, w, h)) return;
  if (w > 42 && w % 42 == 0 && h % 42 == 0) {
    const int tile = 42;
    std::vector<uint8_t> first(static_cast<size_t>(tile) * h * 4);
    for (int y = 0; y < h; y++) std::memcpy(&first[static_cast<size_t>(y) * tile * 4], &all[static_cast<size_t>(y) * w * 4], static_cast<size_t>(tile) * 4);
    all = std::move(first);
    w = tile;
  }
  const bool sheet = h >= 2 * w;
  const int part = sheet ? h / 2 : h;
  std::lock_guard<std::mutex> g(sMu);
  for (int k = 0; k < (sheet ? 2 : 1); k++) {
    FaceJob j;
    j.row = row;
    j.w = w;
    j.h = part;
    j.shiny = k == 1;
    j.rgba.assign(all.begin() + static_cast<size_t>(k) * part * w * 4, all.begin() + static_cast<size_t>(k + 1) * part * w * 4);
    sFaceJobs.push_back(std::move(j));
  }
  sCv.notify_one();
}

void WantFace(uint16_t row) {
  bool needStill, needSmall;
  {
    std::lock_guard<std::mutex> g(sMu);
    needStill = sStillHave.count(row) == 0 || sStillShinyHave.count(row) == 0;
    needSmall = sSmallHave.count(row) == 0;
  }
  if (!needStill && !needSmall) return;
  int& wait = sFaceTry[row];
  if (wait > 0) { wait--; return; }
  wait = 180;
  if (needStill) {
    FaceJob j;
    j.row = row;
    std::lock_guard<std::mutex> g(sMu);
    sFaceJobs.push_back(j);
    sCv.notify_one();
  }
  if (needSmall) TryGameFace(row);
}

void PicturesOf(Member& m) {
  std::lock_guard<std::mutex> g(sMu);
  const std::string n = std::to_string(m.row);
  if (m.shiny && sStillShinyHave.count(m.row)) {
    m.still = "faces/" + n + "_shiny.png";
    m.stillW = sStillShinyHave[m.row].first, m.stillH = sStillShinyHave[m.row].second;
  } else if (sStillHave.count(m.row)) {
    m.still = "faces/" + n + ".png";
    m.stillW = sStillHave[m.row].first, m.stillH = sStillHave[m.row].second;
  }
  if (m.shiny && sSmallShinyHave.count(m.row)) m.small = "faces/" + n + "_small_shiny.png";
  else if (sSmallHave.count(m.row)) m.small = "faces/" + n + "_small.png";
  m.face = !m.still.empty() ? m.still : m.small;
}

void IdleOf(Member& m) {
  if (!sMovingOn.load()) { m.idle = Member::Idle{}; return; }
  auto& e = sIdleTry[m.shiny ? 1 : 0][m.row];
  if (e.first > 0) e.first--;
  else {
    e.first = 180;
    e.second = Member::Idle{};
    const std::string base = "faces/" + std::to_string(m.row) + (m.shiny ? "_idle_shiny" : "_idle");
    std::error_code ec;
    const char* ext = fs::exists(sFolder / (base + ".png"), ec) ? ".png" : ".webp";
    if (fs::exists(sFolder / (base + ext), ec)) {
      std::ifstream f(sFolder / (base + ".txt"));
      int frames = 0, w = 0, h = 0;
      double fps = 0;
      if (f && (f >> frames >> fps >> w >> h) && frames > 0 && frames <= 600 && fps > 0 && fps <= 60 && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
        e.second.src = base + ext, e.second.frames = frames, e.second.fps = fps, e.second.w = w, e.second.h = h;
      }
    }
  }
  m.idle = e.second;
}

bool ReadParty(std::vector<Member>& out, bool& battle, bool& any) {
  out.clear();
  battle = false;
  any = false;
  if (floordata == nullptr || GSfloorGetCurrent() == 0 || pokemon_data == nullptr || !xd::speedrun::PlaythroughChosen()) return true;
  void* party[6];
  for (int s = 0; s < 6; s++) party[s] = reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(s)));
  void* copy[6] = {};
  void* copyFp[6] = {};
  bool onField[6] = {};
  if (fightIsFight() != 0) {
    void* trainer = fightFloorGetGcHeroFightTrainerPtr(nullptr);
    if (trainer != nullptr) {
      battle = true;
      void* fps[6] = {};
      for (uint16_t i = 0; i < 6; i++) fps[i] = fightTrainerGetValidFightPokemonPtr(trainer, i);
      const auto same = [](void* a, void* b) {
        const Pokemon* x = static_cast<const Pokemon*>(a);
        const Pokemon* y = static_cast<const Pokemon*>(b);
        return x != nullptr && y != nullptr && x->dataID == y->dataID && x->rnd == y->rnd && x->catchTrainerRnd == y->catchTrainerRnd;
      };
      for (int s = 0; s < 6; s++) {
        if (party[s] == nullptr) continue;
        for (int i = 0; i < 6 && copyFp[s] == nullptr; i++) {
          if (fps[i] == nullptr || !same(fightPokemon_GetMotoPokemonPtr(fps[i]), party[s])) continue;
          void* buff = fightPokemon_GetPokemonBuffPtr(fps[i]);
          if (buff == nullptr) continue;
          copy[s] = buff;
          copyFp[s] = fps[i];
        }
      }
      for (int s = 0; s < 6; s++) {
        if (copyFp[s] != nullptr || party[s] == nullptr || fps[s] == nullptr) continue;
        bool taken = false;
        for (int t = 0; t < 6; t++) taken = taken || copyFp[t] == fps[s];
        if (taken) continue;
        void* buff = fightPokemon_GetPokemonBuffPtr(fps[s]);
        if (buff == nullptr || pokemonBiosGetPokemonDataId(buff) != pokemonBiosGetPokemonDataId(party[s])) continue;
        copy[s] = buff;
        copyFp[s] = fps[s];
      }
      for (int s = 0; s < 6; s++) {
        if (copyFp[s] != nullptr && fightTrainerGetFightPokemonPtrToFightOutPokemonPtr(trainer, copyFp[s]) != nullptr) onField[s] = true;
      }
    }
  }
  for (int s = 0; s < 6; s++) {
    void* rec = party[s];
    if (rec == nullptr || !(pokemonCheckValid(rec) & 0xFF)) continue;
    const uint16_t row = pokemonBiosGetPokemonDataId(rec);
    if (row == 0) continue;
    void* live = copy[s] != nullptr ? copy[s] : rec;
    Member m;
    m.slot = s + 1;
    m.row = row;
    SpeciesInfo(row, m.national, m.name);
    m.nickname = Utf8Of(reinterpret_cast<const uint16_t*>(pokemonBiosGetNicknamePtr(rec)));
    m.level = pokemonBiosGetLevel(live);
    m.hp = static_cast<int>(pokemon_GetHp(static_cast<Pokemon*>(live)));
    m.maxHp = static_cast<int>(pokemon_GetMaxHp(static_cast<Pokemon*>(live)));
    m.shiny = (pokemonCheckRare(static_cast<Pokemon*>(rec)) & 0xFF) != 0;
    m.egg = pokemonBiosGetTamagoFlag(rec) != 0;
    const uint16_t item = pokemonBiosGetItemDataId(rec);
    if (item != 0) {
      if (void* p = itemDataBiosGetPtr(item)) m.item = Utf8Of(GSmsgGetGSchar(itemDataBiosGetName(p)));
    }
    if (pokemonIsDarkPokemon(rec) != 0) {
      m.shadow = true;
      const f32 dp = pokemonGetDp(rec);
      m.heart = dp > 0 ? static_cast<int>(dp) : 0;
      m.heartMax = static_cast<int>(darkPokemonBiosGetDarkPoint(pokemonBiosGetDarkpokemonDataId(rec)));
    }
    if (!m.egg) {
      const int sex = menuSubGetPokemonSexForDisp(rec);
      m.gender = sex == 0 ? "male" : sex == 1 ? "female" : "none";
      m.exp = static_cast<long>(pokemon_GetExp(static_cast<Pokemon*>(rec)));
      if (m.level > 0 && m.level < 100) {
        m.expStart = static_cast<long>(pokemonGetLevelToExp(rec, static_cast<uint8_t>(m.level)));
        m.expNext = static_cast<long>(pokemonGetLevelToExp(rec, static_cast<uint8_t>(m.level + 1)));
      }
    }
    m.status = m.hp <= 0 && !m.egg ? "fainted" : StatusOf(pokemonGetJoutaiDataId(live));
    m.onField = onField[s] && m.hp > 0;
    out.push_back(std::move(m));
  }
  any = !out.empty();
  return true;
}

std::string BuildText(const std::vector<Member>& party, bool battle) {
  std::string t = "\"party\":[";
  for (size_t i = 0; i < party.size(); i++) {
    const Member& m = party[i];
    if (i) t += ",";
    t += "{\"slot\":" + std::to_string(m.slot);
    t += ",\"species_id\":" + std::to_string(m.row);
    t += ",\"national\":" + std::to_string(m.national);
    t += ",\"name\":" + JsonString(m.name);
    t += ",\"nickname\":" + JsonString(m.nickname);
    t += ",\"level\":" + std::to_string(m.level);
    t += ",\"hp\":" + std::to_string(m.hp);
    t += ",\"max_hp\":" + std::to_string(m.maxHp);
    t += ",\"status\":" + JsonString(m.status);
    t += std::string(",\"shiny\":") + (m.shiny ? "true" : "false");
    t += std::string(",\"shadow\":") + (m.shadow ? "true" : "false");
    t += ",\"heart\":" + std::to_string(m.heart);
    t += ",\"heart_max\":" + std::to_string(m.heartMax);
    t += std::string(",\"egg\":") + (m.egg ? "true" : "false");
    t += ",\"item\":" + JsonString(m.item);
    t += std::string(",\"on_field\":") + (m.onField ? "true" : "false");
    t += ",\"face\":" + JsonString(m.face);
    t += ",\"gender\":" + JsonString(m.gender);
    t += ",\"exp\":" + std::to_string(m.exp);
    t += ",\"exp_level_start\":" + std::to_string(m.expStart);
    t += ",\"exp_next\":" + std::to_string(m.expNext);
    t += ",\"still\":" + JsonString(m.still);
    t += ",\"still_w\":" + std::to_string(m.stillW);
    t += ",\"still_h\":" + std::to_string(m.stillH);
    t += ",\"small\":" + JsonString(m.small);
    if (m.idle.frames > 0) {
      char fps[32];
      std::snprintf(fps, sizeof fps, "%g", m.idle.fps);
      t += ",\"idle\":{\"src\":" + JsonString(m.idle.src) + ",\"frames\":" + std::to_string(m.idle.frames) + ",\"fps\":" + fps + ",\"w\":" + std::to_string(m.idle.w) + ",\"h\":" + std::to_string(m.idle.h) + "}";
    } else {
      t += ",\"idle\":null";
    }
    t += "}";
  }
  t += "],\"in_battle\":";
  t += battle ? "true" : "false";
  return t;
}

void Push(const std::string& text) {
  const std::string body = "{\"version\":" + std::to_string(kVersion) + ",\"game\":\"xd\",\"seq\":" + std::to_string(++sSeq) + "," + text + "}";
  if (sLog) std::fprintf(stderr, "[stream overlay] frame %u json %s\n", static_cast<unsigned>(PortVI_GetFlipCount()), body.c_str());
  WriteJob j;
  j.json = body + "\n";
  j.js = "window.XDParty(" + body + ");\n";
  std::lock_guard<std::mutex> g(sMu);
  sJobs.push_back(std::move(j));
  sCv.notify_one();
}

std::string LookSetting() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("system.stream_overlay_look");
  if (i == es::kNoSetting) return "belt";
  const std::string t = reg.ValueText(i);
  for (const char* l : kLooks)
    if (t == l) return t;
  return "belt";
}

void SyncMoving() {
  const char* mv = std::getenv("XD_STREAM_OVERLAY_MOVING");
  bool on = mv == nullptr || mv[0] != '0';
  if (on) {
    const es::Registry& reg = es::Global();
    const size_t i = reg.Find("system.stream_overlay_moving");
    if (i != es::kNoSetting) on = reg.ValueOf(i).i != 0;
  }
  if (on == sMovingOn.load() && (!on || sHelperThread.joinable())) return;
  sMovingOn = on;
  if (on) {
    if (!sHelperThread.joinable()) sHelperThread = std::thread(HelperMain);
  } else {
    std::lock_guard<std::mutex> g(sMu);
    sStripQueue.clear();
  }
  if (sLog) std::fprintf(stderr, "[stream overlay] moving pictures: %s\n", on ? "on" : "off");
}

void Start() {
  sFolder = xd::SettingsFolder() / "stream_overlay";
  if (const char* own = std::getenv("XD_STREAM_OVERLAY_DIR"); own != nullptr && own[0] != 0 && Explicit()) sFolder = fs::path(own);
  std::error_code ec;
  fs::create_directories(sFolder / "faces", ec);
  sLook = LookSetting();
  SyncPages();
  WriteReadme();
  {
    std::lock_guard<std::mutex> g(sMu);
    sQuit = false;
    for (const auto& e : fs::directory_iterator(sFolder / "faces", ec)) {
      const std::string n = e.path().stem().string();
      if (n.empty() || e.path().extension() != ".png") continue;
      const int row = std::atoi(n.c_str());
      if (row <= 0 || row > 65535) continue;
      const bool shiny = n.size() > 6 && n.compare(n.size() - 6, 6, "_shiny") == 0;
      const bool small = n.find("_small") != std::string::npos;
      if (small) {
        (shiny ? sSmallShinyHave : sSmallHave).insert(static_cast<uint16_t>(row));
      } else if (n == std::to_string(row) || n == std::to_string(row) + "_shiny") {
        int w = 0, h = 0;
        if (PngSize(e.path(), w, h)) (shiny ? sStillShinyHave : sStillHave)[static_cast<uint16_t>(row)] = {w, h};
      }
    }
  }
  sThread = std::thread(ThreadMain);
  {
    sHelperStop = false;
    SyncMoving();
  }
  sStarted = true;
  sLastText.clear();
  sDirty = false;
  const char* l = std::getenv("XD_STREAM_OVERLAY_LOG");
  sLog = l != nullptr && l[0] == '1';
  std::fprintf(stderr, "[stream overlay] on: writing %s\n", sFolder.string().c_str());
  if (!sAtExit) {
    sAtExit = true;
    std::atexit([] { Shutdown(); });
  }
}

}

void Shutdown() {
  if (!sStarted) return;
  sStarted = false;
  sActive = false;
  Push("\"party\":[],\"in_battle\":false");
  {
    std::lock_guard<std::mutex> g(sMu);
    sQuit = true;
    sCv.notify_one();
  }
  sHelperStop = true;
  if (sHelperThread.joinable()) sHelperThread.join();
  if (sThread.joinable()) sThread.join();
  assets::Join();
  if (sFrames > 0) {
    std::fprintf(stderr, "[stream overlay] %lld frames, %.2f us each on average, longest %.1f us; %lld writes\n", static_cast<long long>(sFrames),
                 static_cast<double>(sTotalNs) / sFrames / 1000.0, static_cast<double>(sMaxNs) / 1000.0, static_cast<long long>(sWrites));
  }
}

void Frame() {
  if (!Allowed()) return;
  const bool want = SettingOn();
  if (!want) {
    if (sStarted && sActive) {
      sActive = false;
      Push("\"party\":[],\"in_battle\":false");
      sLastText.clear();
    }
    return;
  }
  const auto t0 = Clock::now();
  if (!sStarted) Start();
  static bool namesTold = false;
  if (sLog && !namesTold && pokemon_data != nullptr && fightIsFight() != 0) {
    namesTold = true;
    for (uint16_t id = 3; id <= 8; id++) std::fprintf(stderr, "[stream overlay] status %u = \"%s\" (written as %s)\n", id, Utf8Of(GSmsgGetGSchar(joutaiDataBiosGetName(id))).c_str(), StatusOf(id).c_str());
  }
  if (!sActive) { sActive = true; sLastText.clear(); sDirty = true; }
  std::vector<Member> party;
  bool battle = false, any = false;
  ReadParty(party, battle, any);
  {
    static int idleSeen = 0;
    const int gen = sIdleGen.load();
    if (gen != idleSeen) {
      idleSeen = gen;
      for (auto& tbl : sIdleTry)
        for (auto& kv : tbl) kv.second.first = 0;
    }
  }
  for (Member& m : party) {
    WantFace(m.row);
    PicturesOf(m);
    IdleOf(m);
    if (m.idle.frames == 0 && sIdleTry[m.shiny ? 1 : 0][m.row].first < 170) WantIdle(m);
  }
  sInBattle = battle;
  SyncMoving();
  {
    const std::string look = LookSetting();
    if (look != sLook) {
      sLook = look;
      WriteOurPage(sFolder / "overlay.html", sLook, true);
      if (sLog) std::fprintf(stderr, "[stream overlay] look: %s\n", sLook.c_str());
    }
  }
  assets::Pump(sFolder);
  const std::string text = BuildText(party, battle);
  if (text != sLastText) {
    sLastText = text;
    sDirty = true;
  }
  const auto now = Clock::now();
  if (sDirty && (sLastPush == Clock::time_point{} || now - sLastPush >= std::chrono::milliseconds(250))) {
    sDirty = false;
    sLastPush = now;
    Push(text);
    if (sLog) std::fprintf(stderr, "[stream overlay] write: %zu Pokemon%s\n", party.size(), battle ? " (in battle)" : "");
  }
  const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
  sFrames++;
  sTotalNs += ns;
  if (ns > sMaxNs) sMaxNs = ns;
  if (sLog && sFrames % 1800 == 0) {
    std::fprintf(stderr, "[stream overlay] cost: %lld frames, %.2f us each on average, longest %.1f us, %lld writes\n", static_cast<long long>(sFrames),
                 static_cast<double>(sTotalNs) / sFrames / 1000.0, static_cast<double>(sMaxNs) / 1000.0, static_cast<long long>(sWrites));
  }
}

}
