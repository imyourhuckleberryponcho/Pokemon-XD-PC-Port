#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <playthrough.h>
#include <settings.h>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
void PortInput_CheatNote(const char* action);
const char* PortInput_ReplayCheatAt(size_t index, uint32_t* frame);
uint32_t PortSpeedups_CheatMenuPresses(void);
bool PortSpeedups_CheatChordModifiersHeld(void);
bool PortXD_OpenCheatMenu(void);
bool PortXD_DevCheatAction(const char* action);
void PortXD_PlaythroughCheatsChanged(void);
uint32_t HSD_Rand(void);
uint8_t pokemonDB_GetGet(uint16_t id);
uint8_t darkPokemonBiosGetHokakuritu(uint16_t id);
int32_t heroItemDecItemDataId(void* hero, uint32_t item, uint32_t amount, uint32_t index);
uint8_t itemDB_GetKind(uint16_t id);
}

namespace {

namespace es = engine::settings;
namespace ep = engine::playthrough;

enum CheatId {
  kCatch = 0, kTrainers, kBalls, kExp, kLevelCap, kMoney, kCoupons, kShiny, kIvs, kEvs, kNoDamage, kWin, kCount
};
struct CheatInfo {
  const char* setting;
  const char* name;
};
constexpr CheatInfo kInfo[kCount] = {
    {"cheats.catch_always", "every ball catches"},
    {"cheats.catch_trainers", "catch trainers' Pokemon"},
    {"cheats.balls_infinite", "balls never used up"},
    {"cheats.exp", "experience multiplier"},
    {"cheats.level_cap_off", "ignore the level cap"},
    {"cheats.money_infinite", "infinite money"},
    {"cheats.coupons_infinite", "infinite Poke Coupons"},
    {"cheats.shiny", "shiny odds"},
    {"cheats.perfect_ivs", "perfect IVs"},
    {"cheats.max_evs", "maxed EVs"},
    {"cheats.no_damage", "no damage taken"},
    {"cheats.win_battles", "win every battle"},
};

std::atomic<int> sValue[kCount];
std::atomic<bool> sEnabledNow{false};
bool sInited = false;
bool sEnabledSeen = false;
int sPrev[kCount] = {};
bool sShown = false;
bool sReplayChecked = false;
std::deque<std::string> sUsed;
std::mutex sUsedLock;
std::vector<std::string> sLog;
struct Scheduled {
  uint32_t frame;
  std::string action;
};
std::vector<Scheduled> sScheduled;
size_t sNextScheduled = 0;
std::string sStatus;
bool sWindowOnStart = false;
bool sStandIn = false;
bool sWarned = false;

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}

int ExpFactor(int index) { return index == 1 ? 2 : index == 2 ? 5 : index == 3 ? 10 : 1; }

void ReadSettings(int out[kCount]) {
  es::Registry& r = es::Global();
  bool on = r.Bool("cheats.enabled").Get();
  if (r.CurrentPreset() == es::Preset::Original || EnvIs("XD_REQUIRE_ORIGINAL", "1") || EnvIs("XD_PRESET", "original")) on = false;
  for (int i = 0; i < kCount; i++) {
    out[i] = 0;
    if (!on) continue;
    const std::string id = kInfo[i].setting;
    if (i == kExp || i == kShiny) {
      out[i] = r.Enum(id).Get();
    } else if (i == kWin) {
      out[i] = r.Bool("cheats.win_battles").Get() ? 1 : 0;
    } else {
      out[i] = r.Bool(id).Get() ? 1 : 0;
    }
  }
  sEnabledNow.store(on);
}

std::string ChoiceId(const char* setting, int index) {
  es::Registry& r = es::Global();
  const size_t i = r.Find(setting);
  if (i == es::kNoSetting) return "";
  es::Value v;
  v.i = index;
  return r.TextOf(i, v);
}

std::string SetText(int cheat, int value) {
  std::string v;
  if (cheat == kExp || cheat == kShiny) {
    v = ChoiceId(kInfo[cheat].setting, value);
  } else {
    v = value != 0 ? "on" : "off";
  }
  return std::string("set ") + kInfo[cheat].setting + "=" + v;
}

bool HasUsed(const std::string& name) {
  std::lock_guard<std::mutex> lock(sUsedLock);
  for (const std::string& u : sUsed) {
    if (u.size() >= name.size() && u.compare(u.size() - name.size(), name.size(), name) == 0) return true;
  }
  return false;
}

void NoteUsed(const std::string& name) {
  if (HasUsed(": " + name)) return;
  {
    std::lock_guard<std::mutex> lock(sUsedLock);
    sUsed.push_back(ep::Now() + ": " + name);
  }
  std::fprintf(stderr, "[cheats] first use in this playthrough: %s\n", name.c_str());
  PortXD_PlaythroughCheatsChanged();
}

void Perform(const std::string& action) {
  if (!PortXD_DevCheatAction(action.c_str())) {
    sStatus = "not possible now: " + action;
    std::fprintf(stderr, "[cheats] %s\n", sStatus.c_str());
  }
}

bool Do(const std::string& action, bool fromReplay) {
  if (!sEnabledNow.load()) return false;
  if (!fromReplay) PortInput_CheatNote(("do " + action).c_str());
  const std::string verb = action.substr(0, action.find(' '));
  NoteUsed("action " + verb);
  sLog.push_back("game frame " + std::to_string(PortVI_GetFlipCount()) + ": do " + action);
  Perform(action);
  return true;
}

void DrawWindow() {
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 460), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Cheats (stand-in page) - F5", &sShown)) {
    ImGui::End();
    return;
  }
  es::Registry& r = es::Global();
  ImGui::TextWrapped("A playthrough that uses a cheat is marked for good: no competitive online rooms, no valid speedrun.");
  for (int i = 0; i < kCount; i++) {
    const std::string id = i == kWin ? "cheats.win_battles" : kInfo[i].setting;
    if (i == kExp || i == kShiny) {
      const size_t at = r.Find(id);
      if (at == es::kNoSetting) continue;
      int now = r.Enum(id).Get();
      const char* labels5[] = {"off", "1 in 512", "1 in 64", "1 in 8", "always"};
      const char* labels4[] = {"off", "x2", "x5", "x10"};
      const char* const* labels = i == kShiny ? labels5 : labels4;
      const int n = i == kShiny ? 5 : 4;
      ImGui::SetNextItemWidth(160);
      if (ImGui::BeginCombo(kInfo[i].name, labels[std::clamp(now, 0, n - 1)])) {
        for (int k = 0; k < n; k++) {
          if (ImGui::Selectable(labels[k], k == now)) {
            es::Value v;
            v.i = k;
            r.SetEnum(id, r.TextOf(at, v));
          }
        }
        ImGui::EndCombo();
      }
    } else {
      bool v = r.Bool(id).Get();
      if (ImGui::Checkbox(kInfo[i].name, &v)) r.SetBool(id, v);
    }
  }
  ImGui::Separator();
  static int item = 1, count = 1;
  ImGui::SetNextItemWidth(100);
  ImGui::InputInt("item number", &item);
  ImGui::SetNextItemWidth(100);
  ImGui::InputInt("count", &count);
  if (ImGui::Button("Give item")) Do("give_item " + std::to_string(item) + " " + std::to_string(std::clamp(count, 1, 999)), false);
  ImGui::SameLine();
  if (ImGui::Button("Rare Candies x99")) Do("candy 99", false);
  if (ImGui::Button("Max out party")) Do("maxparty", false);
  ImGui::SameLine();
  if (ImGui::Button("Open every heart")) Do("purify", false);
  ImGui::SameLine();
  if (ImGui::Button("Win this battle")) Do("win", false);
  if (ImGui::Button("+100000 money")) Do("money 100000", false);
  ImGui::SameLine();
  if (ImGui::Button("+10000 coupons")) Do("coupons 10000", false);
  if (!sStatus.empty()) ImGui::TextWrapped("%s", sStatus.c_str());
  ImGui::End();
}

void CheckReplay() {
  sReplayChecked = true;
  const char* untilText = std::getenv("XD_REPLAY_UNTIL");
  const uint32_t until = untilText != nullptr && untilText[0] != '\0' ? std::strtoul(untilText, nullptr, 10) : UINT32_MAX;
  uint32_t frame = 0;
  size_t count = 0;
  for (size_t i = 0; const char* action = PortInput_ReplayCheatAt(i, &frame); i++) {
    if (frame >= until) break;
    count++;
    if (sEnabledNow.load()) {
      sScheduled.push_back({frame, action});
    } else {
      std::fprintf(stderr, "[cheats] the replayed recording used a cheat at game frame %u (%s): turn on \"Enable cheats\" to do that again, else the replay goes its own way from there\n",
                   frame, action);
    }
  }
  if (count > 0 && sEnabledNow.load()) std::fprintf(stderr, "[cheats] replaying %zu cheat lines from the recording\n", count);
  std::stable_sort(sScheduled.begin(), sScheduled.end(), [](const Scheduled& a, const Scheduled& b) { return a.frame < b.frame; });
}

void LoadTestActions() {
  const char* list = std::getenv("XD_CHEAT_ACTIONS");
  for (const char* p = list != nullptr ? list : ""; *p != '\0';) {
    const char* end = std::strchr(p, ',');
    const std::string item(p, end != nullptr ? static_cast<size_t>(end - p) : std::strlen(p));
    const size_t colon = item.find(':');
    if (colon != std::string::npos) sScheduled.push_back({static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)), "do " + item.substr(colon + 1)});
    p = end != nullptr ? end + 1 : p + item.size();
  }
}

void ReplayLine(const std::string& line) {
  if (line.rfind("set ", 0) == 0) {
    const std::string kv = line.substr(4);
    const size_t eq = kv.find('=');
    if (eq != std::string::npos) es::Global().SetFromText(kv.substr(0, eq), kv.substr(eq + 1));
  } else if (line.rfind("do ", 0) == 0) {
    Do(line.substr(3), true);
  }
}

}

extern "C" {

int PortXD_CheatOn(int id) { return id >= 0 && id < kCount ? sValue[id].load(std::memory_order_relaxed) : 0; }
int PortXD_CheatExpFactor(void) { return ExpFactor(sValue[kExp].load(std::memory_order_relaxed)); }
int PortXD_CheatShinyDivisor(void) {
  switch (sValue[kShiny].load(std::memory_order_relaxed)) {
  case 1: return 512;
  case 2: return 64;
  case 3: return 8;
  case 4: return 1;
  default: return 0;
  }
}

uint32_t PortXD_CatchRand(void) { return sValue[kCatch].load(std::memory_order_relaxed) ? 0u : HSD_Rand(); }
uint32_t PortXD_CatchRate(uint16_t id) { return sValue[kCatch].load(std::memory_order_relaxed) ? 255u : pokemonDB_GetGet(id); }
uint32_t PortXD_CatchRateDark(uint16_t id) { return darkPokemonBiosGetHokakuritu(id); }
int32_t PortXD_BallDec(void* hero, uint32_t item, uint32_t amount, uint32_t index) {
  if (sValue[kBalls].load(std::memory_order_relaxed) && itemDB_GetKind(static_cast<uint16_t>(item)) == 1) {
    std::fprintf(stderr, "[cheats] ball %u not used up%c", item, 10);
    return 0;
  }
  return heroItemDecItemDataId(hero, item, amount, index);
}

void PortXD_CheatsInit(void) {
  sInited = true;
  int now[kCount];
  ReadSettings(now);
  sWindowOnStart = EnvIs("XD_CHEAT_WINDOW", "1");
  if (sEnabledNow.load()) {
    sEnabledSeen = true;
    LoadTestActions();
    std::fprintf(stderr, "[cheats] on: F5 (or hold L + R and press Start) opens the cheat menu; a playthrough that uses a cheat is marked as such\n");
  }
}

int PortXD_CheatsEnabled(void) { return sEnabledNow.load() ? 1 : 0; }
void PortXD_CheatMenuToggle(void) {
  if (sEnabledNow.load()) sShown = !sShown;
}
int PortXD_CheatMenuShown(void) { return sShown && sEnabledNow.load() ? 1 : 0; }

int PortXD_CheatDo(const char* action) { return action != nullptr && Do(action, false) ? 1 : 0; }

void PortXD_CheatsFrame(void) {
  if (!sInited) PortXD_CheatsInit();
  int now[kCount];
  ReadSettings(now);
  const uint32_t frame = PortVI_GetFlipCount();
  if (sEnabledNow.load() && !sEnabledSeen) {
    sEnabledSeen = true;
    LoadTestActions();
  }
  if (!sReplayChecked && sEnabledNow.load()) CheckReplay();
  for (int i = 0; i < kCount; i++) {
    if (now[i] == sPrev[i]) continue;
    const std::string line = SetText(i, now[i]);
    PortInput_CheatNote(line.c_str());
    sLog.push_back("game frame " + std::to_string(frame) + ": " + line);
    if (now[i] != 0) {
      std::string name = kInfo[i].name;
      if (i == kExp) name += " x" + std::to_string(ExpFactor(now[i]));
      if (i == kShiny) name += " " + ChoiceId(kInfo[i].setting, now[i]);
      NoteUsed(name);
    }
    sPrev[i] = now[i];
    sValue[i].store(now[i]);
  }
  for (int i = 0; i < kCount; i++) sValue[i].store(now[i]);
  while (sNextScheduled < sScheduled.size() && sScheduled[sNextScheduled].frame <= frame) {
    ReplayLine(sScheduled[sNextScheduled++].action);
  }
  const uint32_t presses = PortSpeedups_CheatMenuPresses();
  if (!sEnabledNow.load()) {
    sShown = false;
    return;
  }
  if (sWindowOnStart) {
    sWindowOnStart = false;
    sShown = true;
    sStandIn = true;
  }
  if (presses > 0 && !sStandIn && !PortXD_OpenCheatMenu()) std::fprintf(stderr, "[cheats] the cheat menu could not open now (another page is open)\n");
  if (presses > 0 && sStandIn) sShown = !sShown;
  if (sStandIn && sShown && ImGui::GetCurrentContext() != nullptr) DrawWindow();
}

bool PortXD_CheatHidesStart(void) { return sEnabledNow.load() && PortSpeedups_CheatChordModifiersHeld(); }

size_t PortXD_CheatsUsedCount(void) {
  std::lock_guard<std::mutex> lock(sUsedLock);
  return sUsed.size();
}
const char* PortXD_CheatsUsedAt(size_t i) {
  std::lock_guard<std::mutex> lock(sUsedLock);
  return i < sUsed.size() ? sUsed[i].c_str() : "";
}
void PortXD_CheatsSetUsed(const char* const* lines, size_t count) {
  {
    std::lock_guard<std::mutex> lock(sUsedLock);
    sUsed.clear();
    for (size_t i = 0; i < count; i++) sUsed.push_back(lines[i]);
  }
  for (int i = 0; i < kCount; i++) sPrev[i] = 0;
  if (count > 0) std::fprintf(stderr, "[cheats] this playthrough used cheats before (%zu): %s%c", count, lines[0], 10);
}
int PortXD_CheatsWarned(void) { return sWarned ? 1 : 0; }
void PortXD_CheatsSetWarned(int warned) {
  if (sWarned == (warned != 0)) return;
  sWarned = warned != 0;
  if (sWarned) PortXD_PlaythroughCheatsChanged();
}
int PortXD_CheatsUsedHere(void) {
  std::lock_guard<std::mutex> lock(sUsedLock);
  return sUsed.empty() ? 0 : 1;
}

const char* PortXD_CheatsStateText(void) {
  static std::string text;
  text.clear();
  es::Registry& r = es::Global();
  for (int i = 0; i < kCount; i++) {
    const std::string id = i == kWin ? "cheats.win_battles" : kInfo[i].setting;
    const size_t at = r.Find(id);
    if (at == es::kNoSetting) continue;
    bool nondefault = false;
    std::string v;
    if (i == kExp || i == kShiny) {
      nondefault = r.Enum(id).Get() != 0;
      v = r.ValueText(at);
    } else {
      nondefault = r.Bool(id).Get();
      v = "on";
    }
    if (nondefault) text += (text.empty() ? "" : ";") + id.substr(7) + "=" + v;
  }
  return text.c_str();
}
void PortXD_CheatsApplyState(const char* text) {
  es::Registry& r = es::Global();
  if (!r.Bool("cheats.enabled").Get()) return;
  for (int i = 0; i < kCount; i++) {
    const std::string id = i == kWin ? "cheats.win_battles" : kInfo[i].setting;
    if (i == kExp || i == kShiny) r.SetEnum(id, "off"); else r.SetBool(id, false);
  }
  const std::string t = text != nullptr ? text : "";
  for (size_t at = 0; at <= t.size();) {
    const size_t end = std::min(t.find(';', at), t.size());
    const std::string part = t.substr(at, end - at);
    at = end + 1;
    const size_t eq = part.find('=');
    if (eq != std::string::npos) r.SetFromText("cheats." + part.substr(0, eq), part.substr(eq + 1));
  }
}
void PortXD_CheatsAllOff(void) {
  es::Registry& r = es::Global();
  for (int i = 0; i < kCount; i++) {
    const std::string id = i == kWin ? "cheats.win_battles" : kInfo[i].setting;
    if (i == kExp || i == kShiny) r.SetEnum(id, "off"); else r.SetBool(id, false);
  }
}

const char* PortXD_CheatsReport(void) {
  static std::string text;
  std::lock_guard<std::mutex> lock(sUsedLock);
  if (!sEnabledNow.load() && sLog.empty() && sUsed.empty()) return "";
  text = std::string("cheats: ") + (sUsed.empty() && sLog.empty() ? "on, not used\n" : "USED in this playthrough (it is marked)\n");
  for (const std::string& u : sUsed) text += "  first use " + u + "\n";
  for (const std::string& l : sLog) text += "  " + l + "\n";
  return text.c_str();
}

}
