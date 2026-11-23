#include "xd_speedrun.h"

#include "xd_battles_won.h"
#include "xd_playthrough.h"
#include "xd_save_picker.h"
#include "xd_user_folder.h"

#include <port_fps.h>
#include <port_freeze.h>
#include <port_menu.h>
#include <port_speedrun.h>
#include <port_speedups.h>

#include <imgui.h>

#include <settings.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <port_seh.h>
#else
#include <port_win_compat.h>
#endif

extern "C" {
int floorGetFloorState(void);
int floorGetCurrent(void);
uint32_t fadeGetType(void);
const char* PortXD_DevMenuReport(void);
int PortXD_CheatsUsedHere(void);
int32_t menuNameEntryOpen(int32_t mode, intptr_t param);
uint32_t deckGetDarkPokemonSize(void);
extern uint32_t* deckdata_virtual_number;
extern uint32_t* bingo_data_number;
int windowGetActiveID(void);
int fadeCheck(bool wait);
void GSmoviePlay(uint32_t movie, int32_t, int loop, void* tex);
uint32_t GSflagGet(uint32_t id);
uintptr_t itemGetStatus(void* item, uint32_t itemId, uint32_t kind, uint32_t index);
void* savedataGetStatus(void* save, int kind);
uintptr_t gamedatasaveGetStatus(void* gamedata, uint32_t kind);
int GSmoviePlayCheck(uint32_t movie);
void GSmovieStop(uint32_t movie);
}

bool deckDataVirtualDB_GetFlagData(uint16_t id);
struct DarkPokemon {
  bool isSnach() const;
  bool isRelive() const;
};
struct DeckDarkPokemon {
  bool isScenarioPokemon() const;
};
extern "C" {
DeckDarkPokemon* deckGetDeckDarkPokemon(uint32_t id);
DarkPokemon* darkPokemonGetDarkPokemon(uint32_t id);
}

namespace {

namespace ep = engine::playthrough;
namespace es = engine::settings;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct SplitDef {
  const char* id;
  const char* name;
  unsigned after;
  std::vector<unsigned> battles;
};
const SplitDef kSplits[] = {
    {"hq_kidnapper", "HQ Lab kidnapper", 100, {224}},
    {"agate_elder", "Agate elder", 270, {235}},
    {"cipher_lab", "Cipher Lab admin", 350, {268}},
    {"cave_dancer", "Poke Spot dancer", 480, {278}},
    {"pyrite_agent", "Pyrite agent", 490, {289}},
    {"phenac_stadium", "Phenac Stadium admin", 580, {306}},
    {"robot", "The robot", 680, {326, 327, 329}},
    {"ss_libra", "S.S. Libra boss", 730, {330}},
    {"outskirt_dancer", "Outskirt dancer", 770, {336}},
    {"snagem_boss", "Snagem boss", 800, {347}},
    {"key_lair", "Key Lair admin", 860, {367}},
    {"citadark_admins", "Citadark admins", 900, {375, 393}},
    {"citadark_finale", "Citadark trio", 940, {396, 395, 398}},
    {"greevil", "Greevil (the end)", 960, {401, 402}},
};
constexpr size_t kSplitCount = sizeof kSplits / sizeof kSplits[0];
constexpr size_t kFinal = kSplitCount - 1;

bool SplitReached(const SplitDef& d, uint32_t story);

bool sChosen = false;
bool sOn = false;
bool sEver = false;
bool sWithoutLoads = false;
int sStartMode = 0, sEndMode = 0;
bool sStarted = true;
bool sControl = false;
bool sCredits = false;
bool sCinematic = false;
int sCategory = 0;
std::vector<ep::SpeedBreak> sBreaks;
std::vector<std::string> sInvalid;
bool sEnhanced = false;
double sBreakSub = 0.0;
bool sInBreak = false;
bool sBreakPending = false;
uint32_t sBreakPendEpoch = 0, sBreakPendAtMs = 0;
bool sBreakByKey = false;
bool sHaveSave = false;
Clock::time_point sSavedAt;
bool sRematchWon = false;
Clock::time_point sRematchAt;
bool sFirstContinue = true;
uint32_t sFarewellPrev = 1;
bool sFarewellArmed = false;
bool WriteBreakFilesLater = false;
PortSpeedrunCheck sChecks[PORT_SPEEDRUN_MAX_CHECKS];
int sCheckCount = 0;
Clock::time_point sBreakStartWall;
double sBreakAtRta = 0.0;
int64_t sBreakEpoch = 0;
int sFadePrev = 0;
bool sBlackSeen = false;
double sBlackRta = 0.0, sBlackLoad = 0.0;
Clock::time_point sBlackWall;
Clock::time_point sValidityAt;
bool sFinished = false;
double sRta = 0.0, sLoad = 0.0;
double sFinalRta = 0.0, sFinalLoad = 0.0;
std::vector<ep::SpeedSplit> sSplits;
ep::PlayStats sStats;
bool sArmed = false;
bool sPre[kSplitCount] = {};
bool sHaveLast = false;
Clock::time_point sLast;
double sLastFrozen = 0.0;
bool sLoadPrev = false;
Clock::time_point sLastStatus, sLastCarry;
std::string sKey;

bool Log() {
  static const bool on = std::getenv("XD_SPEEDRUN_LOG") != nullptr;
  return on;
}

int EnumSetting(const char* id) {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(id);
  return i == es::kNoSetting ? 0 : reg.ValueOf(i).i;
}

int SettingMode() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("system.speedrun_timer_new_games");
  return i == es::kNoSetting ? 0 : reg.ValueOf(i).i;
}

bool SkipMovies() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("gameplay.skip_intro_movies");
  return i != es::kNoSetting && reg.ValueOf(i).i != 0;
}

bool SplitReached(const SplitDef& d, uint32_t story) {
  return story >= d.after || std::all_of(d.battles.begin(), d.battles.end(), [](unsigned b) { return xd::battles::Won(b); });
}

bool Custom() { return sCategory == 0; }
int EffStart() { return Custom() ? sStartMode : 2; }
int EffEnd() { return Custom() ? sEndMode : sCategory == 2 ? 3 : sCategory == 3 ? 4 : 2; }
const char* const kCategoryNames[] = {"Custom", "Any%", "Any% ENG", "100%"};

struct BreakRule {
  size_t maxBreaks;
  double pool;
};
BreakRule RuleOf() { return sCategory == 3 ? BreakRule{static_cast<size_t>(sRta / 36000.0), 32400.0} : BreakRule{3, 900.0}; }
double PoolTotal() { return sCategory == 3 ? 32400.0 * static_cast<double>(std::max<size_t>(1, RuleOf().maxBreaks)) : RuleOf().pool; }
double BreakUsed() {
  double used = 0.0;
  for (const ep::SpeedBreak& b : sBreaks) used += b.lengthMs / 1000.0;
  return used;
}
std::string Hms(double seconds) {
  const long long s = static_cast<long long>(std::max(0.0, seconds));
  char t[32];
  std::snprintf(t, sizeof t, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
  return t;
}
std::string LocalTime(int64_t epoch) {
  const std::time_t t = static_cast<std::time_t>(epoch);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char text[32];
  std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &tm);
  return text;
}
std::string InvalidText() {
  std::string t;
  for (const std::string& r : sInvalid) t += (t.empty() ? "" : "; ") + r;
  return t;
}

uint32_t Ms(double seconds) { return seconds <= 0.0 ? 0u : static_cast<uint32_t>(std::min(seconds * 1000.0, 4.0e9)); }

double ShownSeconds(double rta, double load) { return std::max(0.0, sWithoutLoads ? rta - load : rta); }

std::string Format(double seconds, bool sign = false) {
  const bool neg = seconds < 0.0;
  const long long cs = static_cast<long long>(std::llround(std::fabs(seconds) * 100.0));
  const long long h = cs / 360000, m = cs / 6000 % 60, s = cs / 100 % 60, c = cs % 100;
  char t[40];
  if (h > 0) {
    std::snprintf(t, sizeof t, "%lld:%02lld:%02lld.%02lld", h, m, s, c);
  } else if (m > 0 || sign) {
    std::snprintf(t, sizeof t, "%lld:%02lld.%02lld", m, s, c);
  } else {
    std::snprintf(t, sizeof t, "%lld.%02lld", s, c);
  }
  return std::string(sign ? (neg ? "-" : "+") : "") + t;
}

int SafeFloorState() {
  PORT_SEH_TRY {
    return floorGetFloorState();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
uint32_t SafeStory() {
  PORT_SEH_TRY {
    return GSflagGet(0x3C4);
  } PORT_SEH_EXCEPT {
    return 0;
  }
}
int SafeWindow() {
  PORT_SEH_TRY {
    return windowGetActiveID();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
int SafeFade() {
  PORT_SEH_TRY {
    return fadeCheck(false);
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
bool SafeFadeOut() {
  PORT_SEH_TRY {
    return fadeGetType() == 3;
  } PORT_SEH_EXCEPT {
    return false;
  }
}
int SafeFloor() {
  PORT_SEH_TRY {
    return floorGetCurrent();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
int64_t SafePlayTime() {
  PORT_SEH_TRY {
    void* game = savedataGetStatus(nullptr, 1);
    return game != nullptr ? static_cast<int64_t>(static_cast<uint32_t>(gamedatasaveGetStatus(game, 6))) : -1;
  } PORT_SEH_EXCEPT {
    return -1;
  }
}

struct Best {
  bool have = false;
  double final = 0.0;
  std::map<std::string, double> splits;
};
Best sBest[2];
bool sBestRead = false;

fs::path BestFile() { return xd::CardRoot() / L"speedrun" / L"best_run.txt"; }

void ReadBest() {
  sBestRead = true;
  std::ifstream in(BestFile());
  std::string line;
  while (std::getline(in, line)) {
    char table[16] = {}, what[16] = {}, id[80] = {};
    long long ms = 0;
    if (std::sscanf(line.c_str(), "%15s %15s %lld", table, what, &ms) >= 3 && std::strcmp(what, "final") == 0) {
      Best& b = sBest[std::strcmp(table, "noloads") == 0 ? 1 : 0];
      b.have = true;
      b.final = static_cast<double>(ms) / 1000.0;
    } else if (std::sscanf(line.c_str(), "%15s split %79s %lld", table, id, &ms) == 3) {
      sBest[std::strcmp(table, "noloads") == 0 ? 1 : 0].splits[id] = static_cast<double>(ms) / 1000.0;
    }
  }
}

void WriteBest() {
  std::error_code ec;
  fs::create_directories(BestFile().parent_path(), ec);
  const fs::path tmp = BestFile().parent_path() / L"best_run.tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    out << "# Pokemon XD speedrun timer: your best finished runs (docs/SPEEDRUN.md). Times in milliseconds.\n";
    for (int t = 0; t < 2; t++) {
      if (!sBest[t].have) continue;
      const char* name = t == 0 ? "rta" : "noloads";
      out << name << " final " << Ms(sBest[t].final) << "\n";
      for (const auto& [id, sec] : sBest[t].splits) out << name << " split " << id << " " << Ms(sec) << "\n";
    }
  }
  fs::rename(tmp, BestFile(), ec);
}

void FinishedRun() {
  if (!sBestRead) ReadBest();
  bool changed = false;
  for (int t = 0; t < 2; t++) {
    const double total = t == 0 ? sFinalRta : sFinalRta - sFinalLoad;
    if (sBest[t].have && sBest[t].final <= total) continue;
    sBest[t].have = true;
    sBest[t].final = total;
    sBest[t].splits.clear();
    for (const ep::SpeedSplit& s : sSplits) sBest[t].splits[s.id] = t == 0 ? s.rtaMs / 1000.0 : (s.rtaMs - s.loadMs) / 1000.0;
    changed = true;
    std::fprintf(stderr, "[speedrun] a best run (%s): %s\n", t == 0 ? "real time" : "without loads", Format(total).c_str());
  }
  if (changed) WriteBest();
}

void WriteStatus(double shown) {
  const fs::path file = xd::CardRoot() / L"speedrun" / L"status.json";
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path tmp = file.parent_path() / L"status.tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    char head[400];
    std::snprintf(head, sizeof head,
                  "{\n  \"running\": %s,\n  \"finished\": %s,\n  \"mode\": \"%s\",\n  \"time_ms\": %u,\n  \"real_time_ms\": %u,\n"
                  "  \"load_time_ms\": %u,\n  \"splits_done\": %zu,\n  \"splits_total\": %zu,\n  \"splits\": [",
                  sOn && !sFinished ? "true" : "false", sFinished ? "true" : "false", sWithoutLoads ? "without_loads" : "real_time",
                  Ms(shown), Ms(sFinished ? sFinalRta : sRta), Ms(sFinished ? sFinalLoad : sLoad), sSplits.size(), kSplitCount);
    out << head;
    for (size_t i = 0; i < sSplits.size(); i++) {
      out << (i > 0 ? ", " : "") << "{\"id\": \"" << sSplits[i].id << "\", \"real_time_ms\": " << sSplits[i].rtaMs
          << ", \"load_time_ms\": " << sSplits[i].loadMs << "}";
    }
    out << "],\n  \"category\": \"" << kCategoryNames[sCategory] << "\",\n  \"valid_for_speedrun_com\": "
        << (sCategory != 0 && sInvalid.empty() ? "true" : "false") << ",\n  \"invalid_reasons\": [";
    for (size_t i = 0; i < sInvalid.size(); i++) out << (i > 0 ? ", " : "") << "\"" << sInvalid[i] << "\"";
    out << "],\n  \"breaks\": [";
    for (size_t i = 0; i < sBreaks.size(); i++) {
      out << (i > 0 ? ", " : "") << "{\"run_time_ms\": " << sBreaks[i].atMs << ", \"length_ms\": " << sBreaks[i].lengthMs
          << ", \"started_at\": \"" << LocalTime(sBreaks[i].epoch) << "\"}";
    }
    out << "],\n  \"break_pool_left_ms\": " << Ms(PoolTotal() - BreakUsed()) << "\n}\n";
  }
  fs::rename(tmp, file, ec);
}

void SetCarryEnv(const std::string& text) {
#ifdef _WIN32
  SetEnvironmentVariableA("XD_SPEEDRUN_CARRY", text.empty() ? nullptr : text.c_str());
#else
  if (text.empty()) unsetenv("XD_SPEEDRUN_CARRY");
  else setenv("XD_SPEEDRUN_CARRY", text.c_str(), 1);
#endif
}

int64_t EpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void StoreCarry() {
  if (!sOn || sKey.empty()) return;
  char text[160];
  std::snprintf(text, sizeof text, "%s|%lld|%.3f|%.3f|%d", sKey.c_str(), static_cast<long long>(EpochMs()), sRta, sLoad,
                sFinished ? 1 : 0);
  SetCarryEnv(text);
}

bool TakeCarry(const std::string& key, double& rta, double& load, bool& finished) {
  const char* v = std::getenv("XD_SPEEDRUN_CARRY");
  if (v == nullptr || v[0] == 0 || key.empty()) return false;
  const std::string text = v;
  const size_t bar = text.find('|');
  if (bar == std::string::npos || text.substr(0, bar) != key) return false;
  long long epoch = 0;
  double r = 0, l = 0;
  int fin = 0;
  if (std::sscanf(text.c_str() + bar + 1, "%lld|%lf|%lf|%d", &epoch, &r, &l, &fin) < 3) return false;
  const double gap = std::clamp(static_cast<double>(EpochMs() - epoch) / 1000.0, 0.0, 300.0);
  rta = r + (fin ? 0.0 : gap);
  load = l + (fin ? 0.0 : gap);
  finished = fin != 0;
  return true;
}

void Persist() { xd::pt::TimerPersist(); }

void Publish(double shown) {
  PortSpeedrunView v{};
  v.visible = sOn ? 1 : 0;
  std::snprintf(v.time, sizeof v.time, "%s", Format(shown).c_str());
  std::snprintf(v.label, sizeof v.label, "%s", sFinished ? "Finished" : sWithoutLoads ? "Without loads" : "Real time");
  if (!sStarted) std::snprintf(v.label, sizeof v.label, "%s", "Waiting for the start");
  if (sCategory == 0) {
    std::snprintf(v.info, sizeof v.info, "Custom: the port's menus stop the clock");
  } else {
    const double left = PoolTotal() - BreakUsed();
    if (sCategory == 3) {
      bool over = false;
      for (const ep::SpeedBreak& b : sBreaks) over = over || b.lengthMs > 32400000u;
      std::snprintf(v.info, sizeof v.info, "100%%  breaks %zu of %zu earned (1 per 10 h, up to 9 h each)", sBreaks.size(), RuleOf().maxBreaks);
      v.warn = over ? 1 : 0;
    } else {
      std::snprintf(v.info, sizeof v.info, "%s  breaks %zu/%zu  pool %s%s", kCategoryNames[sCategory], sBreaks.size(),
                    RuleOf().maxBreaks, left < 0 ? "OVER by " : "", Hms(std::fabs(left)).c_str());
      v.warn = left < 0 ? 1 : 0;
    }
  }
  if (!sInvalid.empty()) {
    std::snprintf(v.info2, sizeof v.info2, "NOT VALID FOR SPEEDRUN.COM: %s", sInvalid.front().c_str());
  } else if (sCategory == 0) {
    std::snprintf(v.info2, sizeof v.info2, "Custom rules: not for speedrun.com");
  }
  v.current = -1;
  v.splitCount = static_cast<int>(kSplitCount);
  if (!sBestRead) ReadBest();
  const Best& best = sBest[sWithoutLoads ? 1 : 0];
  for (size_t i = 0; i < kSplitCount; i++) {
    PortSpeedrunSplit& out = v.splits[i];
    std::snprintf(out.name, sizeof out.name, "%s", kSplits[i].name);
    const auto done = std::find_if(sSplits.begin(), sSplits.end(), [&](const ep::SpeedSplit& s) { return s.id == kSplits[i].id; });
    if (done == sSplits.end()) {
      if (v.current < 0) v.current = static_cast<int>(i);
      const auto b = best.splits.find(kSplits[i].id);
      if (b != best.splits.end()) std::snprintf(out.delta, sizeof out.delta, "%s", Format(b->second).c_str());
      continue;
    }
    const double t = sWithoutLoads ? (done->rtaMs - done->loadMs) / 1000.0 : done->rtaMs / 1000.0;
    std::snprintf(out.time, sizeof out.time, "%s", Format(t).c_str());
    const auto b = best.splits.find(kSplits[i].id);
    if (b != best.splits.end()) {
      std::snprintf(out.delta, sizeof out.delta, "%s", Format(t - b->second, true).c_str());
      out.ahead = t < b->second ? -1 : t > b->second ? 1 : 0;
    }
  }
  if (sCategory == 3) {
    v.checkCount = sCheckCount;
    for (int i = 0; i < sCheckCount; i++) v.checks[i] = sChecks[i];
  }
  PortSpeedrun_SetView(&v);
}

void ResetClocks() {
  sArmed = false;
  sRta = sLoad = sFinalRta = sFinalLoad = 0.0;
  sFinished = false;
  sSplits.clear();
  sHaveLast = false;
  sLoadPrev = false;
}

}

namespace xd::speedrun {

void NewGame(const ep::Speedrun& timer) {
  sChosen = true;
  ResetClocks();
  sOn = timer.on;
  sEver = timer.on;
  sWithoutLoads = timer.withoutLoads;
  sStartMode = timer.start;
  sEndMode = timer.end;
  sCategory = timer.category;
  sBreaks.clear();
  sInvalid.clear();
  sCinematic = sBlackSeen = false;
  sBreakSub = 0.0;
  sBreakPending = sHaveSave = sRematchWon = false;
  sFarewellPrev = 1;
  sStarted = EffStart() == 0;
  sControl = sCredits = false;
  sStats = ep::PlayStats{};
  sStats.kept = true;
  sKey.clear();
  SetCarryEnv("");
  std::fprintf(stderr, "[speedrun] new game: timer %s\n", sOn ? (sWithoutLoads ? "on (without loads shown)" : "on (real time)") : "off");
  if (!sOn) PortSpeedrun_SetView(nullptr);
}

void Continue(const ep::Setup& setup, const std::string& key, bool hasSetup) {
  sChosen = true;
  ResetClocks();
  const ep::Speedrun& t = setup.speedrun;
  sOn = hasSetup && t.present && t.on;
  sEver = hasSetup && t.present;
  sWithoutLoads = t.withoutLoads;
  sStartMode = t.start;
  sEndMode = t.end;
  sCategory = t.category;
  sBreaks = t.breaks;
  sInvalid = t.invalid;
  sCinematic = sBlackSeen = false;
  sBreakSub = 0.0;
  sBreakPending = sHaveSave = sRematchWon = false;
  sFarewellPrev = 1;
  sStarted = EffStart() == 0 || t.rtaMs > 0 || t.finished;
  sControl = sCredits = false;
  sKey = key;
  sSplits = t.splits;
  sStats = hasSetup ? setup.stats : ep::PlayStats{};
  sRta = t.rtaMs / 1000.0;
  sLoad = t.loadMs / 1000.0;
  sFinished = t.finished;
  sFinalRta = t.finalRtaMs / 1000.0;
  sFinalLoad = t.finalLoadMs / 1000.0;
  if (sFinished) {
    sRta = sFinalRta;
    sLoad = sFinalLoad;
  }
  bool carried = false;
  if (sOn) {
    double r = 0, l = 0;
    bool fin = false;
    if (TakeCarry(key, r, l, fin)) {
      if (r >= sRta) {
        sRta = r;
        sLoad = l;
        if (fin && !sFinished) sFinished = true, sFinalRta = r, sFinalLoad = l;
        carried = true;
      }
    }
  }
  if (sOn && hasSetup && t.breakPending && sCategory == 3) {
    const double length = std::max(0.0, static_cast<double>(EpochMs() / 1000 - static_cast<int64_t>(t.breakEpoch)));
    ep::SpeedBreak b;
    b.atMs = t.breakAtMs;
    b.lengthMs = Ms(length);
    b.epoch = t.breakEpoch;
    sBreaks.push_back(b);
    std::fprintf(stderr, "[speedrun] 100%% break ends at CONTINUE: %s\n", Hms(length).c_str());
    WriteBreakFilesLater = true;
  } else if (sOn && hasSetup && sCategory != 0 && sStarted && !sFinished && !carried && sFirstContinue && sRta > 0.0) {
    if (std::find(sInvalid.begin(), sInvalid.end(), "the game was closed") == sInvalid.end()) sInvalid.push_back("the game was closed");
    std::fprintf(stderr, "[speedrun] not valid for speedrun.com: the game was closed\n");
  }
  sFirstContinue = false;
  std::fprintf(stderr, "[speedrun] continue: timer %s%s, real time %s (loads %s), %zu split(s)\n", sOn ? "on" : "off",
               carried ? " (carried over a soft reset)" : "", Format(sRta).c_str(), Format(sLoad).c_str(), sSplits.size());
  if (!sOn) PortSpeedrun_SetView(nullptr);
}

ep::Speedrun TimerState() {
  ep::Speedrun s;
  s.present = sEver;
  s.on = sOn;
  s.withoutLoads = sWithoutLoads;
  s.start = sStartMode;
  s.end = sEndMode;
  s.category = sCategory;
  s.breakPending = sBreakPending;
  s.breakEpoch = sBreakPendEpoch;
  s.breakAtMs = sBreakPendAtMs;
  s.breaks = sBreaks;
  s.invalid = sInvalid;
  s.rtaMs = Ms(sFinished ? sFinalRta : sRta);
  s.loadMs = Ms(sFinished ? sFinalLoad : sLoad);
  s.splits = sSplits;
  s.finished = sFinished;
  s.finalRtaMs = Ms(sFinalRta);
  s.finalLoadMs = Ms(sFinalLoad);
  return s;
}

ep::PlayStats StatsState() { return sStats; }

void BattleEnded(bool won, unsigned result, unsigned battle) {
  if (!sChosen) return;
  if (won && (battle == 401 || battle == 402) && std::any_of(sSplits.begin(), sSplits.end(), [](const ep::SpeedSplit& s) { return s.id == "greevil"; })) {
    sRematchWon = true;
    sRematchAt = Clock::now();
    std::fprintf(stderr, "[speedrun] the final boss was beaten again (battle %u)\n", battle);
  }
  if (won) sStats.battlesWon++;
  else if (result == 2 || result == 3) sStats.battlesLost++;
  else sStats.battlesOther++;
}

bool TitleWatchNeeded(bool) { return true; }

bool MapStarted() {
  if (SafeFloorState() != 5) return false;
  const int floor = SafeFloor();
  return floor >= 0 && floor != 0x384 && floor != 0x399 && floor != 0x39A;
}

ep::Speedrun NewGameDefault() {
  ep::Speedrun s;
  const int m = SettingMode();
  s.present = s.on = m != 0;
  s.withoutLoads = m == 2;
  s.start = EnumSetting("system.speedrun_start");
  s.end = EnumSetting("system.speedrun_end");
  s.category = EnumSetting("system.speedrun_category");
  return s;
}

bool Active() { return sChosen && sOn; }
bool PlaythroughChosen() { return sChosen; }

const char* const kTimerNames[] = {"Off", "Real time", "Without loads"};
const char* const kStartNames[] = {"A on NEW GAME", "First control", "Confirm New Game"};
const char* const kEndNames[] = {"Greevil defeated", "Credits start", "Black before cinematic", "Black after last text"};
int RowCount(int which) { return which == 0 ? 3 : which == 1 ? 3 : which == 2 ? 4 : 4; }
const char* RowName(int which, int index) {
  index = std::clamp(index, 0, RowCount(which) - 1);
  return which == 0 ? kTimerNames[index] : which == 1 ? kStartNames[index] : which == 2 ? kEndNames[index] : kCategoryNames[index];
}
const char* RowSetting(int which) {
  return which == 1 ? "system.speedrun_start" : which == 2 ? "system.speedrun_end" : "system.speedrun_category";
}
int RowValue(int which) {
  if (which == 0) return RowMode();
  if (!sChosen) return EnumSetting(RowSetting(which));
  return which == 1 ? sStartMode : which == 2 ? sEndMode : sCategory;
}
bool SetRow(int which, int index) {
  if (which == 0) return SetRowMode(index);
  index = std::clamp(index, 0, RowCount(which) - 1);
  if (!sChosen) {
    es::Registry& reg = es::Global();
    const size_t i = reg.Find(RowSetting(which));
    if (i == es::kNoSetting) return false;
    es::Value v = reg.ValueOf(i);
    v.i = index;
    return reg.Set(i, v);
  }
  if (which == 1) {
    if (sStarted && sStartMode != index) return false;
    sStartMode = index;
    if (Custom()) sStarted = sStartMode == 0;
  } else if (which == 2) {
    sEndMode = index;
  } else {
    if (sStarted && (sCategory == 0) != (index == 0) && sRta > 0.0) return false;
    if (!sBreaks.empty() && index == 0) return false;
    sCategory = index;
    if (!Custom()) sStarted = sStarted || EffStart() == 0;
  }
  std::fprintf(stderr, "[speedrun] this playthrough's %s: %s\n", which == 1 ? "start point" : which == 2 ? "end point" : "category",
               RowName(which, index));
  Persist();
  return false;
}

int RowMode() {
  if (!sChosen) return SettingMode();
  return !sOn ? 0 : sWithoutLoads ? 2 : 1;
}

bool SetRowMode(int mode) {
  mode = std::clamp(mode, 0, 2);
  if (!sChosen) {
    es::Registry& reg = es::Global();
    const size_t i = reg.Find("system.speedrun_timer_new_games");
    if (i == es::kNoSetting) return false;
    es::Value v = reg.ValueOf(i);
    v.i = mode;
    return reg.Set(i, v);
  }
  if (mode == RowMode()) return false;
  if (mode == 0) {
    sOn = false;
    PortSpeedrun_SetView(nullptr);
    SetCarryEnv("");
  } else {
    if (!sEver) {
      ResetClocks();
      sEver = true;
    }
    sOn = true;
    sStarted = true;
    sWithoutLoads = mode == 2;
    sHaveLast = false;
  }
  std::fprintf(stderr, "[speedrun] this playthrough's timer: %s\n", mode == 0 ? "off" : mode == 1 ? "real time" : "without loads");
  Persist();
  return false;
}

void WriteBreakFiles() {
  const fs::path file = xd::CardRoot() / L"speedrun" / L"breaks.txt";
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::trunc);
  if (!out) return;
  out << "Pokemon XD speedrun: break list (paste this into a speedrun.com submission)\n";
  out << "Category: " << kCategoryNames[sCategory] << "\n";
  out << "Run time (breaks not counted): " << Format(sFinished ? sFinalRta : sRta) << (sFinished ? " (finished)" : "") << "\n";
  for (size_t i = 0; i < sBreaks.size(); i++) {
    out << "Break " << (i + 1) << ": started at run time " << Format(sBreaks[i].atMs / 1000.0) << " (real clock "
        << LocalTime(sBreaks[i].epoch) << "), lasted " << Hms(sBreaks[i].lengthMs / 1000.0) << "." << (sBreaks[i].lengthMs % 1000) / 100
        << "\n";
  }
  if (sCategory != 0) {
    out << "Breaks used: " << sBreaks.size() << " of " << RuleOf().maxBreaks << "; pool used " << Hms(BreakUsed()) << " of "
        << Hms(PoolTotal()) << (BreakUsed() > PoolTotal() ? " (OVER THE POOL)" : "") << "\n";
  }
  out << "Valid for speedrun.com: " << (sCategory != 0 && sInvalid.empty() ? "yes" : "NO") << (sCategory == 0 ? " (custom rules)" : "")
      << (sInvalid.empty() ? "" : " (" + InvalidText() + ")") << "\n";
}

void FinishAt(double rta, double load) {
  sFinished = true;
  sFinalRta = rta;
  sFinalLoad = load;
  std::fprintf(stderr, "[speedrun] THE RUN IS OVER: real time %s, without loads %s\n", Format(rta).c_str(),
               Format(rta - load).c_str());
  FinishedRun();
  Persist();
  WriteBreakFiles();
}
void FinishNow() { FinishAt(sRta, sLoad); }

void NotePad(int stickX, int stickY) {
  if (sChosen && sOn && !sStarted && sArmed && (std::abs(stickX) >= 40 || std::abs(stickY) >= 40)) sControl = true;
}

void CreditsStarted() {
  if (sChosen && sOn) sCredits = true;
}
void EndingCinematic() {
  if (sChosen && sOn) sCinematic = true;
}
void SetEngine(bool enhanced) { sEnhanced = enhanced; }

void NewGameStartPressed() {
  char text[32];
  std::snprintf(text, sizeof text, "%lld", static_cast<long long>(EpochMs()));
#ifdef _WIN32
  SetEnvironmentVariableA("XD_SPEEDRUN_NEWGAME_AT", text);
#else
  setenv("XD_SPEEDRUN_NEWGAME_AT", text, 1);
#endif
}

void NewGameConfirmed() {
  if (!sChosen) NewGame(NewGameDefault());
  double already = 0.0;
  if (const char* at = std::getenv("XD_SPEEDRUN_NEWGAME_AT"); at != nullptr && at[0] != 0) {
    already = std::clamp(static_cast<double>(EpochMs() - std::atoll(at)) / 1000.0, 0.0, 300.0);
#ifdef _WIN32
    SetEnvironmentVariableA("XD_SPEEDRUN_NEWGAME_AT", nullptr);
#else
    unsetenv("XD_SPEEDRUN_NEWGAME_AT");
#endif
  }
  if (sOn && EffStart() == 2 && !sStarted) {
    sStarted = true;
    sRta = sLoad = already;
    sHaveLast = false;
    std::fprintf(stderr, "[speedrun] New Game confirmed: the clock starts\n");
    Persist();
  }
}

namespace {

bool InBreakMenu() {
  const int w = SafeWindow();
  return w == 0x2C || w == 0x4C;
}

void BreakOpen(void*) {
  sInBreak = true;
  sBreakByKey = false;
  if (sCategory == 3) {
    sBreakPending = true;
    sBreakPendEpoch = static_cast<uint32_t>(EpochMs() / 1000);
    sBreakPendAtMs = Ms(sRta);
    Persist();
  }
  sBreakStartWall = Clock::now();
  sBreakAtRta = sRta;
  sBreakEpoch = EpochMs() / 1000;
  std::fprintf(stderr, "[speedrun] break %zu starts at run time %s\n", sBreaks.size() + 1, Format(sRta).c_str());
}

bool BreakFrame(void*, const PortMenuInput*) {
  const double elapsed = std::chrono::duration<double>(Clock::now() - sBreakStartWall).count();
  const double left = PoolTotal() - BreakUsed() - elapsed;
  if (ImGui::GetCurrentContext() != nullptr) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float px = std::max(16.f, d.y / 24.f);
    char l1[96], l2[128];
    std::snprintf(l1, sizeof l1, "BREAK  %s", Hms(elapsed).c_str());
    if (sCategory == 3) {
      std::snprintf(l2, sizeof l2, "%s - quit if you like: CONTINUE ends the break. Break key (F6 / Back) to resume now",
                    elapsed > 32400.0 ? "OVER 9 hours" : "up to 9:00:00");
    } else {
      std::snprintf(l2, sizeof l2, "%s%s left in the pool - press the Break key (F6 / controller Back) to resume",
                    left < 0 ? "OVER the pool by " : "", Hms(std::fabs(left)).c_str());
    }
    const ImVec2 s1 = font->CalcTextSizeA(px * 1.6f, 1e5f, 0.f, l1), s2 = font->CalcTextSizeA(px, 1e5f, 0.f, l2);
    const float w = std::max(s1.x, s2.x) + px * 2.f, h = s1.y + s2.y + px * 2.f;
    const float x = (d.x - w) / 2.f, y = d.y * 0.4f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 215), px * 0.5f);
    const ImU32 col = left < 0 ? IM_COL32(255, 120, 90, 255) : IM_COL32(244, 244, 244, 255);
    dl->AddText(font, px * 1.6f, ImVec2(x + (w - s1.x) / 2.f, y + px * 0.6f), col, l1);
    dl->AddText(font, px, ImVec2(x + (w - s2.x) / 2.f, y + px * 0.6f + s1.y + px * 0.3f), col, l2);
  }
  const uint32_t presses = PortSpeedups_BreakPresses();
  static const double autoEnd = [] {
    const char* v = std::getenv("XD_SPEEDRUN_BREAK_SECONDS");
    return v != nullptr ? std::atof(v) : 0.0;
  }();
  if (autoEnd > 0.0 && elapsed >= autoEnd) {
    sBreakByKey = true;
    return false;
  }
  if (presses > 0 && elapsed > 0.5) {
    sBreakByKey = true;
    return false;
  }
  return true;
}

void BreakClose(void*) {
  if (sCategory == 3 && !sBreakByKey) {
    sInBreak = false;
    return;
  }
  sBreakPending = false;
  const double length = std::chrono::duration<double>(Clock::now() - sBreakStartWall).count();
  ep::SpeedBreak b;
  b.atMs = Ms(sBreakAtRta);
  b.lengthMs = Ms(length);
  b.epoch = static_cast<uint32_t>(sBreakEpoch);
  sBreaks.push_back(b);
  sBreakSub += length;
  sInBreak = false;
  std::fprintf(stderr, "[speedrun] break %zu over: %s (pool left %s)\n", sBreaks.size(), Hms(length).c_str(),
               Hms(PoolTotal() - BreakUsed()).c_str());
  WriteBreakFiles();
  Persist();
}

void TryBreak() {
  const char* no = nullptr;
  if (Custom()) no = "Breaks are for the speedrun.com categories (Custom: the port's menus already stop the timer)";
  else if (!sStarted || sFinished) no = "No break: the run is not running";
  else if (sBreaks.size() >= RuleOf().maxBreaks) {
    no = sCategory == 3 ? "No 100% break earned yet (one per 10 hours of run time)" : "No breaks left in this category";
  } else if (sCategory == 3) {
    if (!sHaveSave || std::chrono::duration<double>(Clock::now() - sSavedAt).count() > 60.0) no = "A 100% break can only start right after a save";
  } else if (!InBreakMenu()) {
    no = "A break can only start in the bag or the party screen";
  }
  if (no == nullptr) {
    static const PortMenuPage page{"speedrun break", BreakOpen, BreakFrame, BreakClose, nullptr};
    if (PortMenu_Open(&page)) return;
    no = "No break now (another screen is open)";
  }
  std::fprintf(stderr, "[speedrun] break refused: %s\n", no);
  PortVideo_Notice(no, 4.0);
}

int CountShadows(bool purified, int* total) {
  int n = 0, all = 0;
  PORT_SEH_TRY {
    const uint32_t size = deckGetDarkPokemonSize();
    for (uint32_t i = 0; i < size && i < 4096; i++) {
      const DeckDarkPokemon* deck = deckGetDeckDarkPokemon(i);
      const DarkPokemon* dark = darkPokemonGetDarkPokemon(i);
      if (deck == nullptr || dark == nullptr || !deck->isScenarioPokemon()) continue;
      all++;
      n += (purified ? dark->isRelive() : dark->isSnach()) ? 1 : 0;
    }
  } PORT_SEH_EXCEPT {
    return -1;
  }
  *total = all;
  return n;
}
int CountDisks(int* total) {
  int n = 0;
  PORT_SEH_TRY {
    const int decks = static_cast<int>(*deckdata_virtual_number);
    for (int id = 1; id <= 50 && id < decks; id++) n += deckDataVirtualDB_GetFlagData(static_cast<uint16_t>(id)) ? 1 : 0;
  } PORT_SEH_EXCEPT {
    return -1;
  }
  *total = 50;
  return n;
}
uint32_t SafeFlag(uint32_t id) {
  PORT_SEH_TRY {
    return GSflagGet(id);
  } PORT_SEH_EXCEPT {
    return 0;
  }
}
int CountBingo(int* total) {
  PORT_SEH_TRY {
    const uint32_t* save = static_cast<const uint32_t*>(savedataGetStatus(nullptr, 0x1B));
    if (save == nullptr) return -1;
    const int all = static_cast<int>(*bingo_data_number);
    int n = 0;
    for (int i = 0; i < all && i < 16; i++) n += (save[0] & (1u << (i * 2))) ? 1 : 0;
    *total = all;
    return n;
  } PORT_SEH_EXCEPT {
    return -1;
  }
}

void AddCheck(const char* text, int state) {
  if (sCheckCount >= PORT_SPEEDRUN_MAX_CHECKS) return;
  PortSpeedrunCheck& c = sChecks[sCheckCount++];
  std::snprintf(c.text, sizeof c.text, "%s", text);
  c.state = state;
}
void AddCount(const char* what, int n, int total) {
  char t[64];
  if (n < 0) {
    AddCheck(what, 2);
    return;
  }
  std::snprintf(t, sizeof t, "%s %d/%d", what, n, total);
  AddCheck(t, n >= total ? 1 : 0);
}

void UpdateChecklist();
bool UpdateChecklistDone() {
  UpdateChecklist();
  static const bool assume = std::getenv("XD_SPEEDRUN_ASSUME_DONE") != nullptr;
  if (assume) return true;
  for (int i = 0; i + 1 < sCheckCount; i++) {
    if (sChecks[i].state != 1) return false;
  }
  return sCheckCount > 1;
}

void UpdateChecklist() {
  sCheckCount = 0;
  AddCheck("Beat the game (Greevil)", SafeStory() >= 960 ? 1 : 0);
  int total = 0;
  int n = CountShadows(false, &total);
  AddCount("Shadow Pokemon snagged", n, total);
  n = CountShadows(true, &total);
  AddCount("Shadow Pokemon purified", n, total);
  n = CountDisks(&total);
  AddCount("Battle CD Sim Battles", n, total);
  n = CountBingo(&total);
  AddCount("Battle Bingo cards", n, total);
  AddCheck("Lucky Egg quest", SafeFlag(2251) == 1 ? 1 : 0);
  AddCheck("Colosseum win: Pyrite", SafeFlag(1754) >= 1 ? 1 : 0);
  AddCheck("Colosseum win: Tower", SafeFlag(1755) >= 1 ? 1 : 0);
  AddCheck("Colosseum win: Orre (flag inferred)", SafeFlag(1858) >= 1 ? 1 : 0);
  AddCheck("Defeat Justy", (SafeFlag(1227) != 0 || xd::battles::Won(302)) ? 1 : 0);
  AddCheck("Mt. Battle: 100 wins, Johto starter", (SafeFlag(1469) != 0 || SafeFlag(1470) != 0 || SafeFlag(1471) != 0) ? 1 : 0);
  AddCheck("Greevil again + bonus cutscene", sFinished ? 1 : 0);
}

void UpdateValidity() {
  const auto add = [](const char* why) {
    if (std::find(sInvalid.begin(), sInvalid.end(), why) == sInvalid.end()) {
      sInvalid.push_back(why);
      std::fprintf(stderr, "[speedrun] not valid for speedrun.com: %s\n", why);
    }
  };
  if (std::strstr(PortXD_DevMenuReport(), "USED") != nullptr) add("developer menu used");
  if (PortXD_CheatsUsedHere() != 0) add("cheats used");
  if (std::strstr(PortSpeedups_Report(), "none used") == nullptr) add("speed-ups used");
  if (sEnhanced) add("Enhanced engine (the categories are for the original game)");
  const es::Registry& reg = es::Global();
  if (const size_t i = reg.Find("system.language"); i != es::kNoSetting) {
    std::string v = reg.ValueText(i);
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool english = v.empty() || v == "us" || v == "uk" || v == "en" || v.rfind("english", 0) == 0;
    if (!english) add("language is not English");
  }
}

}

void SaveWritten() {
  sHaveSave = true;
  sSavedAt = Clock::now();
}

void Frame() {
  const uint32_t presses = PortSpeedups_BreakPresses();
  if (!sChosen || !sOn) return;
  if (presses > 0) TryBreak();
  const auto now = Clock::now();
  const double frozen = PortFreeze_FrozenSeconds();
  const int state = SafeFloorState();
  const bool loading = state != 5;
  if (!sHaveLast) {
    sHaveLast = true;
    sLast = now;
    sLastFrozen = frozen;
    sLoadPrev = loading;
    sLastStatus = sLastCarry = sValidityAt = now;
    sBreakSub = 0.0;
  }
  double dt = std::chrono::duration<double>(now - sLast).count();
  if (Custom()) {
    dt -= frozen - sLastFrozen;
  } else {
    dt -= sBreakSub;
  }
  sBreakSub = 0.0;
  sLast = now;
  sLastFrozen = frozen;
  if (dt < 0.0) dt = 0.0;
  if (!sFinished && sStarted) {
    sRta += dt;
    if (loading || sLoadPrev) sLoad += dt;
    if (Log() && loading != sLoadPrev) {
      std::fprintf(stderr, "[speedrun] %s at %s (floor state %d)\n", loading ? "loading starts" : "loading ends",
                   Format(sRta).c_str(), state);
    }
  }
  sLoadPrev = loading;
  const int fade = SafeFade();
  const bool blackNow = sFadePrev == 1 && fade == 0 && SafeFadeOut();
  sFadePrev = fade > 0 ? 1 : 0;
  if (blackNow) {
    sBlackSeen = true;
    sBlackRta = sRta;
    sBlackLoad = sLoad;
    sBlackWall = now;
  }
  if (!sArmed && state == 5) {
    sArmed = true;
    const uint32_t story = SafeStory();
    for (size_t i = 0; i < kSplitCount; i++) sPre[i] = SplitReached(kSplits[i], story);
    if (Log()) std::fprintf(stderr, "[speedrun] armed at %s (story %u)\n", Format(sRta).c_str(), story);
  }
  if (!sFinished && sArmed) {
    const uint32_t story = SafeStory();
    for (size_t i = 0; i < kSplitCount; i++) {
      const SplitDef& d = kSplits[i];
      if (sPre[i]) continue;
      if (std::any_of(sSplits.begin(), sSplits.end(), [&](const ep::SpeedSplit& s) { return s.id == d.id; })) continue;
      if (!SplitReached(d, story)) continue;
      sSplits.push_back({d.id, Ms(sRta), Ms(sLoad)});
      std::fprintf(stderr, "[speedrun] split \"%s\": real time %s, loads %s, without loads %s\n", d.name, Format(sRta).c_str(),
                   Format(sLoad).c_str(), Format(sRta - sLoad).c_str());
      if (i == kFinal && EffEnd() == 0 && sStarted) {
        FinishNow();
        break;
      }
    }
  }
  if (!sStarted && sArmed && sControl && EffStart() == 1) {
    if (SafeWindow() == 0 && SafeFade() <= 0) {
      sStarted = true;
      sRta = sLoad = 0.0;
      std::fprintf(stderr, "[speedrun] first control: the clock starts\n");
      Persist();
    }
    sControl = false;
  }
  if (!sFinished && sStarted) {
    const bool bossDone = SplitReached(kSplits[kFinal], SafeStory());
    if (EffEnd() == 1 && sCredits) {
      FinishNow();
    } else if (EffEnd() == 2 && sCinematic && bossDone) {
      const bool recent = sBlackSeen && std::chrono::duration<double>(now - sBlackWall).count() < 15.0;
      if (recent) FinishAt(sBlackRta, sBlackLoad);
      else FinishNow();
    } else if (EffEnd() == 3 && sCredits && blackNow && bossDone) {
      FinishNow();
    } else if (EffEnd() == 4) {
      const uint32_t farewell = SafeFlag(1979);
      if (farewell == 1 && sFarewellPrev == 0 && UpdateChecklistDone()) {
        const bool recent = sBlackSeen && std::chrono::duration<double>(now - sBlackWall).count() < 10.0;
        if (recent) FinishAt(sBlackRta, sBlackLoad);
        else FinishNow();
      }
      sFarewellPrev = farewell;
    }
  }
  if (now - sValidityAt >= std::chrono::seconds(1)) {
    sValidityAt = now;
    UpdateValidity();
    if (sCategory == 3) UpdateChecklist();
  }
  if (WriteBreakFilesLater) {
    WriteBreakFilesLater = false;
    WriteBreakFiles();
    Persist();
  }
  const double shown = sFinished ? ShownSeconds(sFinalRta, sFinalLoad) : ShownSeconds(sRta, sLoad);
  Publish(shown);
  if (now - sLastStatus >= std::chrono::milliseconds(500)) {
    sLastStatus = now;
    WriteStatus(shown);
  }
  if (now - sLastCarry >= std::chrono::milliseconds(100)) {
    sLastCarry = now;
    StoreCarry();
  }
}

std::string TimerRowInfo(int which) {
  if (which == 3) {
    return "How the run is timed. Any%, Any% ENG, 100%: the speedrun.com rules (clock from Confirm New Game, real time, only breaks stop it, "
           "a fully black picture ends it; breaks only in the bag or party screen: Any% 3 / 15 min, 100% 7 / 12 h). Custom: your own "
           "start and end, the port's menus stop the clock.";
  }
  if (which == 1) return "Custom only. Where the clock starts: A on NEW GAME, First control (first moment you can move after the opening), or Confirm New Game (the YES). Fixed once the clock has started.";
  if (which == 2) return "Custom only. Where the run ends: Greevil defeated (the battle is won), Credits start, or the speedrun.com ends: the first fully black picture before the ending cinematic / after the last textbox.";
  if (!sChosen) {
    return "Whether a NEW playthrough gets a speedrun timer. Real time counts everything from NEW GAME on; Without loads "
           "takes the game's loading time out. Only drawn over the picture: the game plays the same with it on or off, "
           "also in Original. A save keeps its own timer (change it here while playing).";
  }
  return "This playthrough's speedrun timer (kept with the save). Real time counts from NEW GAME; Without loads shows it "
         "minus the loading time. Only drawn over the picture: the game plays the same with it on or off. Where it is "
         "drawn: the Video settings.";
}

std::string StatsText() {
  char buf[400];
  std::string t;
  const int64_t play = SafePlayTime();
  if (play >= 0) {
    std::snprintf(buf, sizeof buf, "Play time %lld:%02lld:%02lld. ", static_cast<long long>(play / 3600),
                  static_cast<long long>(play / 60 % 60), static_cast<long long>(play % 60));
    t += buf;
  }
  size_t bosses = 0;
  const uint32_t story = SafeStory();
  for (const SplitDef& d : kSplits) {
    if (SplitReached(d, story)) bosses++;
  }
  std::snprintf(buf, sizeof buf, "Battles won %u, lost %u, other %u%s. Story bosses beaten %zu of %zu. ", sStats.battlesWon,
                sStats.battlesLost, sStats.battlesOther, sStats.kept ? "" : " (counted from the last Continue)", bosses, kSplitCount);
  t += buf;
  if (sEver) {
    t += "Timer " + Format(ShownSeconds(sFinished ? sFinalRta : sRta, sFinished ? sFinalLoad : sLoad)) +
         (sFinished ? " (finished)." : ".");
  }
  return t;
}

}

namespace {
struct SkipLogosRegistration {
  SkipLogosRegistration() { xd::picker::RegisterSkipLogosQuery([]() -> bool { return SkipMovies(); }); }
} sSkipLogosRegistration;
}

extern "C" void PortXD_MoviePlay(uint32_t movie, int32_t a, int loop, void* tex) {
  if (movie == 0x7dd) xd::speedrun::CreditsStarted();
  if (movie == 0x8eb) xd::speedrun::EndingCinematic();
  GSmoviePlay(movie, a, loop, tex);
}

extern "C" int32_t PortXD_MenuNameEntryOpen(int32_t mode, intptr_t param) {
  xd::speedrun::NewGameConfirmed();
  return menuNameEntryOpen(mode, param);
}
