#include "xd_playthrough.h"
#include "i18n.h"
#include "xd_added_rows.h"
#include "xd_battles_won.h"
#include "xd_pokedex.h"
#include "discord_presence.h"
#include "xd_challenges.h"
#include "xd_shiny.h"
#include "xd_starter.h"
#include "xd_gen4.h"
#include "xd_gen57.h"
#include "xd_gen_rules.h"
#include "xd_battle_models.h"
#include "xd_rulesets.h"
#include "xd_modern_types.h"

#include "xd_playthrough_files.h"
#include "xd_speedrun.h"
#include "script_ext.h"
#include "xd_randomizer.h"
#include "xd_save_picker.h"
#include "xd_save_ext.h"
#include "xd_memo_overflow.h"
#include "xd_user_folder.h"
#include "xd_xg.h"
#include "tables_file.h"

#include <mods.h>
#include <playthrough.h>
#include <settings.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <port_win_compat.h>
#include <filesystem>
static const wchar_t* _wgetenv(const wchar_t* name) {
  static std::wstring value;
  const char* v = std::getenv(std::filesystem::path(name).string().c_str());
  if (v == nullptr) return nullptr;
  value = std::filesystem::path(v).wstring();
  return value.c_str();
}
static void SetEnvironmentVariableW(const wchar_t* name, const char* value) {
  const std::string n = std::filesystem::path(name).string();
  if (value != nullptr) {
    setenv(n.c_str(), value, 1);
  } else {
    unsetenv(n.c_str());
  }
}
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef XD_PORT_VERSION
#define XD_PORT_VERSION "unknown"
#endif

extern "C" {
uint32_t PortVI_GetFlipCount(void);
void PortInput_SettingsNote(const char* list);
void PortInput_PlaythroughNote(const char* what);
const char* PortInput_ReplaySettingsAt(size_t index, uint32_t* frame);
uint32_t PortInput_ReplayRecordedFrame(void);
void PortInput_LatchHeld(void);
void OSResetSystem(int reset, uint32_t resetCode, int forceMenu);
size_t PortXD_CheatsUsedCount(void);
const char* PortXD_CheatsUsedAt(size_t i);
void PortXD_CheatsSetUsed(const char* const* lines, size_t count);
const char* PortXD_CheatsStateText(void);
void PortXD_CheatsApplyState(const char* text);
void PortXD_CheatsAllOff(void);
int PortXD_CheatsWarned(void);
void PortXD_CheatsSetWarned(int warned);
bool PortXD_CheatHidesStart(void);
}

namespace {

namespace ep = engine::playthrough;
namespace em = engine::mods;
#include <port_audio.h>

namespace es = engine::settings;
namespace xp = xd::playthrough;
namespace fs = std::filesystem;

enum class CardState { NoSave, NoFile, Setup, Unreadable };
using engine::i18n::Tr;

void MergeCheats(ep::Setup& stamped) {
  for (size_t i = 0; i < PortXD_CheatsUsedCount(); i++) {
    const std::string used = PortXD_CheatsUsedAt(i);
    if (std::find(stamped.cheatsUsed.begin(), stamped.cheatsUsed.end(), used) == stamped.cheatsUsed.end()) stamped.cheatsUsed.push_back(used);
  }
  if (!stamped.cheatsUsed.empty()) {
    stamped.cheatState.clear();
    const std::string state = PortXD_CheatsStateText();
    for (size_t at = 0; !state.empty() && at <= state.size();) {
      const size_t end = std::min(state.find(';', at), state.size());
      stamped.cheatState.push_back(state.substr(at, end - at));
      at = end + 1;
    }
  }
  if (PortXD_CheatsWarned() != 0) stamped.cheatWarned = true;
}
bool WriteSetup(const fs::path& file, const ep::Setup& setup) {
  ep::Setup stamped = setup;
  MergeCheats(stamped);
  xd::rulesets::Stamp(stamped);
  if (xd::xg::On() && stamped.edition.empty()) stamped.edition = "xg";
  return ep::WriteFile(file, stamped);
}
void AdoptCheats(const ep::Setup* setup) {
  if (setup == nullptr) {
    PortXD_CheatsSetUsed(nullptr, 0);
    PortXD_CheatsAllOff();
    PortXD_CheatsSetWarned(0);
    return;
  }
  std::vector<const char*> lines;
  for (const std::string& u : setup->cheatsUsed) lines.push_back(u.c_str());
  PortXD_CheatsSetUsed(lines.data(), lines.size());
  PortXD_CheatsSetWarned(setup->cheatWarned ? 1 : 0);
  if (!setup->cheatsUsed.empty()) {
    std::string state;
    for (const std::string& c : setup->cheatState) state += (state.empty() ? "" : ";") + c;
    PortXD_CheatsApplyState(state.c_str());
  }
}

std::string Fmt(const char* format, ...) {
  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof buf, format, args);
  va_end(args);
  return buf;
}
const char* EngineNameTr(engine::playthrough::Engine e) {
  return e == engine::playthrough::Engine::Enhanced ? Tr("Enhanced") : Tr("Faithful");
}
const char* const kCardStateText[] = {"no save", "a save without a setup file", "a save with its setup",
                                      "a save whose setup file can't be read"};

xp::SaveId sCardId;
bool sTablesDue = true;
CardState sCardState = CardState::NoSave;
ep::Setup sCardSetup;
bool sRestarted = false;
ep::Setup sRestartSetup;
fs::path sRestartFile;

std::vector<em::ModFolder> sFound;
std::vector<std::string> sLibraryOn;
std::vector<std::pair<std::string, em::AppliesVerdict>> sVerdicts;
std::vector<ep::ModUse> sLoaded;
std::string sLoadedRandomizer;
std::string sLoadedMoreShadows;
std::string sBootKey;
bool sRestartNow = false;

enum class Chosen { None, NewGame, Continue };
Chosen sChosen = Chosen::None;
uint32_t sChosenFrame = 0;
ep::Setup sCurrent;
bool sCurrentUnknown = false;

bool sPending = false;
ep::Setup sPendingSetup;
std::string sKeyAtNewGame;

std::vector<std::string> sContinueProblems, sContinueNotes;
bool sContinueAccepted = false;

std::vector<ep::AddedRowRef> CurrentAddedRows() {
  std::vector<ep::AddedRowRef> now;
  for (const em::AddedRow& r : xd::added::Rows()) if (!r.unbound) now.push_back({r.mod, r.table, r.key, r.id, r.label});
  return now;
}
ep::Engine sPlayersEngine = ep::Engine::Faithful;
bool sAsk = false;
bool sEngineAnswered = false;
bool sNewGameTaken = false;
bool sNewGameHeld = false;
bool sNewGamePassOn = false;
int sInjectA = 0;
uint32_t sInjectFrom = 0;
bool sScreen = false;

std::vector<std::pair<uint32_t, std::string>> sReplaySettings;
size_t sReplayNext = 0;
std::string sReport;

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}
bool EnvSet(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

const em::ModFolder* FoundMod(const std::string& id) {
  for (const em::ModFolder& f : sFound) {
    if (Lower(f.manifest.id) == Lower(id)) return &f;
  }
  return nullptr;
}

const em::AppliesVerdict* VerdictOf(const std::string& id) {
  for (const auto& [mid, v] : sVerdicts) {
    if (Lower(mid) == Lower(id)) return &v;
  }
  return nullptr;
}

std::string CardLabel() {
  switch (sCardState) {
  case CardState::Setup: return ep::Label(sCardSetup);
  case CardState::NoFile:
  case CardState::Unreadable: return ep::UnknownLabel();
  default: return "no save";
  }
}

bool sInPadFilter = false;
std::vector<std::string> sNotesLater;
void Note(const std::string& what) {
  if (sInPadFilter) {
    sNotesLater.push_back(what);
  } else {
    PortInput_PlaythroughNote(what.c_str());
  }
}

constexpr const char* kSplitId = "gameplay.physical_special_split";
}

namespace xd::pt {
std::string SplitNow(ep::Engine engine) {
  if (engine != ep::Engine::Enhanced) return "";
  es::Registry& reg = es::Global();
  const size_t i = reg.Find(kSplitId);
  if (i == es::kNoSetting) return "";
  const bool overridden = reg.IsOverridden(i);
  std::string v;
  if (overridden) {
    const std::string now = reg.ValueText(i);
    reg.ClearOverride(i);
    v = reg.ValueText(i);
    reg.Override(kSplitId, now);
  } else {
    v = reg.ValueText(i);
  }
  return v == "on" ? "on" : "off";
}

void ApplySplitOf(const ep::Setup* setup, const char* why);
void ApplySplit(const char* why) { ApplySplitOf(sChosen != Chosen::None && !sCurrentUnknown ? &sCurrent : nullptr, why); }
void ApplySplitOf(const ep::Setup* setup, const char* why) {
  es::Registry& reg = es::Global();
  const size_t i = reg.Find(kSplitId);
  if (i == es::kNoSetting || EnvIs("XD_REQUIRE_ORIGINAL", "1")) return;
  const bool inPlay = setup != nullptr && setup->engine == ep::Engine::Enhanced && !setup->split.empty();
  static bool mine = false;
  if (inPlay) {
    reg.Override(kSplitId, setup->split);
    mine = true;
    std::fprintf(stderr, "[playthrough] physical/special split %s: kept with this playthrough (%s)\n", setup->split.c_str(), why);
  } else if (mine) {
    mine = false;
    if (setup == nullptr || setup->engine == ep::Engine::Enhanced) reg.ClearOverride(i);
  }
}

void AdoptSplit() {
  if (sCardState != CardState::Setup || sCardSetup.engine != ep::Engine::Enhanced || !sCardSetup.split.empty()) return;
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return;
  sCardSetup.split = SplitNow(ep::Engine::Enhanced);
  if (sCardSetup.split.empty()) return;
  if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls (the split is adopted for this run only)\n", xp::SetupFileFor(sCardId).wstring().c_str());
  }
  std::fprintf(stderr, "[playthrough] physical/special split adopted from the settings for this playthrough: %s\n", sCardSetup.split.c_str());
  Note("physical/special split kept with the playthrough from now on: " + sCardSetup.split);
}
}

namespace {
ep::Engine EngineNow() {
  const es::Registry& reg = es::Global();
  for (size_t i = 0; i < reg.Count(); i++) {
    if (ep::IsEngineSetting(reg.Get(i)) && reg.ValueText(i) != reg.TextOf(i, reg.DefaultOf(i, es::Preset::Original))) {
      return ep::Engine::Enhanced;
    }
  }
  return ep::Engine::Faithful;
}

void RecordSetupChanged(const std::string& what) {
  sCardSetup.setupChanged.push_back(ep::Now() + ": " + what);
  if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(sCardId).wstring().c_str());
  }
}

}

extern "C" void PortXD_PlaythroughSaveWritten(const uint8_t* file, size_t size);
extern "C" void PortXD_SetSaveWrittenHook(void (*hook)(const uint8_t* file, size_t size));

extern "C" void PortXD_SetSaveExtHooks(int (*before)(const uint8_t*, size_t), void (*after)(int),
                                       int (*onRead)(const uint8_t*, size_t, size_t, char*, size_t));

extern "C" void PortXD_PlaythroughBoot(void) {
  PortXD_SetSaveWrittenHook(PortXD_PlaythroughSaveWritten);
  xd::memo::RegisterSaveExt();
  PortXD_SetSaveExtHooks(PortXD_SaveExtBeforeWrite, PortXD_SaveExtAfterWrite, PortXD_SaveExtOnRead);
  sCardId = xp::SaveIdOnCard();
  if (sCardId.found) sBootKey = sCardId.key;
  if (sCardId.found) {
    std::string error;
    const fs::path file = xp::SetupFileFor(sCardId);
    std::fprintf(stderr, "[playthrough] the save's setup file: %s\n", file.filename().string().c_str());
    std::error_code ec;
    if (!fs::exists(file, ec)) {
      sCardState = CardState::NoFile;
    } else if (ep::ReadFile(file, sCardSetup, error)) {
      sCardState = CardState::Setup;
      xd::rulesets::Resolve(sCardSetup);
    } else {
      sCardState = CardState::Unreadable;
      std::fprintf(stderr, "[playthrough] the save's setup file %ls %s: treated as a save without one\n", file.wstring().c_str(),
                   error.c_str());
    }
  }
  std::fprintf(stderr, "[playthrough] memory card: %s%s%s (%s)\n", kCardStateText[static_cast<int>(sCardState)],
               sCardId.found ? ", trainer " : "", sCardId.found ? sCardId.trainer.c_str() : "", CardLabel().c_str());
  if (!xd::challenges::TestSwitch().empty()) xd::challenges::SetActive({}, "XD_CHALLENGES");
  if (!xd::shiny::TestSwitch().empty()) xd::shiny::SetActive(xd::shiny::TestSwitch(), "XD_SHINY");
  if (sCardState == CardState::NoSave) {
    xd::battles::Set({}, true, "start-up: no save on the card");
    xd::pokedex::SetLists("", "", true);
    xd::pokedex::SetLookPins({});
  } else if (sCardState == CardState::Setup) {
    xd::pokedex::SetLookPins(xd::rulesets::ChoiceOf(sCardSetup).looks.overrides);
    xd::battles::Set(sCardSetup.battlesWon, sCardSetup.battlesWonKept, "start-up: the save on the card");
    xd::pokedex::SetLists(sCardSetup.dexCaught, sCardSetup.dexShiny, sCardSetup.dexKept);
    xd::scriptext::LoadValues(sCardSetup.modData, "start-up: the save on the card");
  } else {
    xd::battles::Set({}, false, "start-up: a save without a setup file");
  }
  xd::picker::ReadIntent();
  if (const wchar_t* next = _wgetenv(L"XD_NEWGAME_SETUP"); next != nullptr && next[0] != L'\0') {
    std::string error;
    if (ep::ReadFile(next, sRestartSetup, error)) {
      xd::rulesets::Resolve(sRestartSetup);
      sRestarted = true;
      sRestartFile = next;
      std::fprintf(stderr, "[playthrough] started again for a New Game setup: %s\n", ep::Label(sRestartSetup).c_str());
    } else {
      std::fprintf(stderr, "[playthrough] XD_NEWGAME_SETUP: %ls %s; ignored\n", next, error.c_str());
    }
  }
  xd::picker::DecideStartUp();
  es::Registry& reg = es::Global();
  sPlayersEngine = ep::DefaultEngine(reg);
  std::string changed;
  if (sRestarted) {
    changed = ep::ApplyEngine(reg, sRestartSetup.engine);
  } else if (sCardState == CardState::Setup) {
    changed = ep::ApplyEngine(reg, sCardSetup.engine);
  }
  if (!changed.empty()) {
    std::fprintf(stderr, "[playthrough] Faithful engine: for this run %s\n", changed.c_str());
  }
  const char* why = nullptr;
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) {
    why = "XD_REQUIRE_ORIGINAL (comparison run)";
  } else if (EnvIs("XD_NEWGAME_SCREEN", "1")) {
    why = nullptr;
  } else if (EnvIs("XD_NEWGAME_SCREEN", "0")) {
    why = "XD_NEWGAME_SCREEN=0";
  } else if (!reg.Bool("system.newgame_setup").Get()) {
    why = "system.newgame_setup is off (Original)";
  } else if (EnvSet("XD_REPLAY")) {
    why = "a replay";
  } else if (EnvSet("XD_INPUT_SCRIPT")) {
    why = "scripted presses";
  } else if (EnvSet("XD_RENDER_ICON")) {
    why = "icon pictures";
  }
  sScreen = why == nullptr;
  std::fprintf(stderr, "[playthrough] New Game setup screen %s%s%s\n", sScreen ? "on" : "off this run", why ? ": " : "",
               why ? why : "");
  if (sCardState == CardState::Setup && !sRestarted && ep::EngineQuestionDue(sCardSetup, sPlayersEngine)) {
    const char* no = nullptr;
    if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) {
      no = "XD_REQUIRE_ORIGINAL (comparison run)";
    } else if (EnvIs("XD_NEWGAME_SCREEN", "1")) {
      no = nullptr;
    } else if (EnvIs("XD_NEWGAME_SCREEN", "0")) {
      no = "XD_NEWGAME_SCREEN=0";
    } else if (EnvSet("XD_REPLAY")) {
      no = "a replay";
    } else if (EnvSet("XD_INPUT_SCRIPT")) {
      no = "scripted presses";
    } else if (EnvSet("XD_RENDER_ICON")) {
      no = "icon pictures";
    }
    sAsk = no == nullptr;
    std::fprintf(stderr, "[playthrough] this save is %s and the settings are %s now: CONTINUE %s%s\n", ep::EngineName(sCardSetup.engine),
                 ep::EngineName(sPlayersEngine), sAsk ? "asks which it is to be" : "would ask, but not in this run: ", sAsk ? "" : no);
  }
}

namespace xd::pt {

std::string RandomizerOf(const ep::Setup& s);
std::string MoreShadowsOf(const ep::Setup& s);

bool BootWorldMods(std::vector<std::string>& ids) {
  if (sRestarted) {
    ids = ep::WorldModIds(sRestartSetup);
    return true;
  }
  switch (sCardState) {
  case CardState::Setup: ids = ep::WorldModIds(sCardSetup); return true;
  case CardState::NoFile:
  case CardState::Unreadable: ids.clear(); return true;
  default: return false;
  }
}

void AfterModsLoaded(const em::LoadResult& result, const fs::path& modsFolder, const fs::path& listFile,
                     const em::DiscView& disc) {
  std::vector<em::Message> messages;
  sFound = em::FindMods(modsFolder, messages);
  for (const em::ModFolder& f : sFound) {
    sVerdicts.emplace_back(f.manifest.id, em::CheckApplies(f, xp::LooksOrSound, &disc));
  }
  em::ModList list;
  std::vector<std::string> warnings;
  em::ReadModListFile(listFile, list, warnings);
  for (const auto& [id, on] : list.Active().mods) {
    if (on && FoundMod(id) != nullptr) sLibraryOn.push_back(id);
  }
  for (size_t i = 0; i < result.active.size(); i++) {
    const em::Manifest& m = result.active[i];
    ep::ModUse u{m.id, m.name, m.version, i < result.activeApplies.size() ? result.activeApplies[i].effective : em::Applies::NewPlaythrough};
    sLoaded.push_back(u);
  }
  if (sCardState == CardState::Setup) {
    const ep::Check c = ep::Compare(sCardSetup, sFound);
    sContinueProblems = c.worldProblems;
    sContinueNotes = c.notes;
    for (const ep::ModUse* w : sCardSetup.WorldMods()) {
      const bool loaded = std::any_of(sLoaded.begin(), sLoaded.end(), [&](const ep::ModUse& l) { return Lower(l.id) == Lower(w->id); });
      if (!loaded && FoundMod(w->id) != nullptr) sContinueProblems.push_back(Fmt(Tr("%s could not be used (see the mods message)"), (w->name.empty() ? w->id : w->name).c_str()));
    }
    for (const std::string& p : sContinueProblems) std::fprintf(stderr, "[playthrough] the save's setup: %s\n", p.c_str());
    for (const std::string& n : sContinueNotes) std::fprintf(stderr, "[playthrough] the save's setup (note): %s\n", n.c_str());
  }
}

bool WorldModsFixed(std::vector<std::string>& ids) { return BootWorldMods(ids); }

std::vector<std::string> LoadedModIds() {
  std::vector<std::string> ids;
  for (const ep::ModUse& m : sLoaded) ids.push_back(m.id);
  return ids;
}

em::AppliesVerdict ModApplies(const em::ModFolder& mod) {
  if (const em::AppliesVerdict* v = VerdictOf(mod.manifest.id)) return *v;
  return em::CheckApplies(mod, xp::LooksOrSound, nullptr);
}

void RestartForMods(const std::vector<ep::ModUse>& use) {
  if (sRestarted) {
    sRestartSetup.mods = use;
    if (!WriteSetup(sRestartFile, sRestartSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls: the restart keeps the New Game setup's old mods\n",
                   sRestartFile.wstring().c_str());
    }
  }
  Note("mods changed on the MODS page: starting again");
  std::fprintf(stderr, "[playthrough] starting the game again with the mod list the MODS page wrote\n");
  std::fflush(stderr);
  sRestartNow = true;
  xd::picker::SetForRestart(xd::picker::Intent::Title);
}

void RestartToTitle() {
  Note("starting again (back to the title menu)");
  std::fflush(stderr);
  sRestartNow = true;
  xd::picker::SetForRestart(xd::picker::Intent::Title);
}

bool PickerPossible() { return xd::picker::Possible(); }

void PickerAnswer(const xd::picker::Row* row) {
  if (row == nullptr) {
    Note("save picker: back");
    return;
  }
  xd::picker::SetAnswered(true);
  if (row->active) {
    Note("save picker: " + row->name + " (" + row->trainer + "), already loaded");
    sInjectA = 4;
    sInjectFrom = PortVI_GetFlipCount();
    return;
  }
  if (!xd::picker::SwitchTo(*row)) {
    PortXD_ShowNote("That save could not be made the active one.", 5.f);
    return;
  }
  Note("save picker: " + row->name + " (" + row->trainer + "): starting again to load it");
  SetEnvironmentVariableW(L"XD_NEWGAME_SETUP", nullptr);
  xd::picker::SetForRestart(xd::picker::Intent::Continue);
  std::fflush(stderr);
  sRestartNow = true;
}

void RestartNow(const char* why) {
  Note(std::string(why) + ": starting again");
  std::fprintf(stderr, "[playthrough] starting the game again (%s)\n", why);
  std::fflush(stderr);
  sRestartNow = true;
}

bool RestartForRoom(bool gen4, bool gen4Rules, const std::string& ruleSet) {
  ep::Setup setup;
  setup.engine = ep::DefaultEngine(es::Global());
  setup.mods = sLoaded;
  if (gen4) setup.challenges.push_back("gen4_pokemon");
  if (gen4 && gen4Rules) setup.challenges.push_back("gen4_rules");
  if (gen4 && (ruleSet == "gen5" || ruleSet == "gen6" || ruleSet == "gen7")) {
    setup.challenges.push_back("gen_numbers");
    setup.challenges.push_back(ruleSet + "_rules");
    if (ruleSet != "gen5") setup.challenges.push_back("modern_types");
  }
  const fs::path file = xd::CardRoot() / L"playthroughs" / L"next_new_game.json";
  if (!WriteSetup(file, setup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls: no restart for the room\n", file.wstring().c_str());
    return false;
  }
  SetEnvironmentVariableW(L"XD_NEWGAME_SETUP", file.c_str());
  std::fprintf(stderr, "[playthrough] starting the game again for an online room (Gen 4 rules %s)\n", gen4 ? "on" : "off");
  std::fflush(stderr);
  xd::picker::SetForRestart(xd::picker::Intent::Title);
  sRestartNow = true;
  return true;
}

static std::vector<std::string> TestOptions() {
  std::vector<std::string> o = xd::shiny::TestSwitch();
  for (const std::string& s : xd::starter::TestOptions()) o.push_back(s);
  return o;
}

Defaults NewGameDefaults() {
  Defaults d;
  d.restarted = sRestarted;
  d.engine = sRestarted ? sRestartSetup.engine : ep::DefaultEngine(es::Global());
  if (sRestarted) d.randomizer = RandomizerOf(sRestartSetup);
  if (sRestarted) d.moreShadows = MoreShadowsOf(sRestartSetup);
  if (sRestarted) d.challenges = sRestartSetup.challenges;
  d.options = sRestarted ? sRestartSetup.options : TestOptions();
  d.rules = sRestarted ? xd::rulesets::ChoiceOf(sRestartSetup) : xd::rulesets::LauncherDefaults(xd::gen4::PackPresent());
  std::vector<std::string> ids = sLibraryOn;
  for (const ep::ModUse& l : sLoaded) {
    if (std::none_of(ids.begin(), ids.end(), [&](const std::string& i) { return Lower(i) == Lower(l.id); })) ids.push_back(l.id);
  }
  if (sRestarted) {
    for (const ep::ModUse& m : sRestartSetup.mods) {
      if (std::none_of(ids.begin(), ids.end(), [&](const std::string& i) { return Lower(i) == Lower(m.id); })) ids.push_back(m.id);
    }
  }
  for (const std::string& id : ids) {
    const em::ModFolder* f = FoundMod(id);
    if (f == nullptr) continue;
    ModChoice c;
    c.id = f->manifest.id;
    c.name = f->manifest.name;
    c.version = f->manifest.version;
    c.description = f->manifest.description;
    if (const em::AppliesVerdict* v = VerdictOf(id)) {
      c.applies = v->effective;
      c.reason = v->reason;
    }
    if (sRestarted) {
      c.on = std::any_of(sRestartSetup.mods.begin(), sRestartSetup.mods.end(), [&](const ep::ModUse& m) { return Lower(m.id) == Lower(id); });
    } else {
      c.on = std::any_of(sLibraryOn.begin(), sLibraryOn.end(), [&](const std::string& i) { return Lower(i) == Lower(id); });
    }
    d.mods.push_back(std::move(c));
  }
  return d;
}

void SetRandomizer(ep::Setup& s, const std::string& code) {
  s.randomizer.clear(), s.randomizerVersion.clear(), s.seed.clear();
  if (code.empty()) return;
  s.randomizer = "XD";
  s.randomizerVersion = std::to_string(xd::randomizer::CodeVersionOf(code));
  s.seed = code;
}

ep::Setup SetupOf(ep::Engine engine, const std::vector<ModChoice>& mods, const std::string& randomizer,
                  const std::vector<std::string>& challenges, const std::vector<std::string>& options) {
  ep::Setup s;
  s.engine = engine;
  s.challenges = challenges;
  s.options = options;
  for (const ModChoice& m : mods) {
    if (m.on) s.mods.push_back({m.id, m.name, m.version, m.applies});
  }
  SetRandomizer(s, randomizer);
  s.speedrun = xd::speedrun::NewGameDefault();
  s.started = ep::Now();
  s.portVersion = XD_PORT_VERSION;
  return s;
}

const std::string& LoadedRandomizer() { return sLoadedRandomizer; }

static std::vector<std::string> sRunStarter;
static bool sRunStarterInit = false;
const std::vector<std::string>& CurrentOptions() {
  if (sChosen == Chosen::NewGame) return sCurrent.options;
  if (!sRunStarterInit) {
    sRunStarterInit = true;
    sRunStarter = xd::starter::TestOptions();
  }
  return sRunStarter;
}

void OptionsChanged(const std::vector<std::string>& options, const std::string& note) {
  if (sChosen == Chosen::NewGame) {
    sCurrent.options = options;
    sPendingSetup.options = options;
  } else {
    sRunStarter = options;
  }
  if (!note.empty()) Note(note);
}

std::string RandomizerOf(const ep::Setup& s) {
  return s.randomizer.empty() || xd::xg::Off(xd::xg::Feature::Randomizer) != nullptr ? std::string() : s.seed;
}

std::string BootRandomizer() {
  const char* env = std::getenv("XD_RANDOMIZER");
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return {};
  if (env != nullptr && std::strcmp(env, "0") == 0) {
    std::fprintf(stderr, "[randomizer] XD_RANDOMIZER=0: off this run\n");
    return {};
  }
  std::string code;
  if (sRestarted) {
    code = RandomizerOf(sRestartSetup);
  } else if (sCardState == CardState::Setup) {
    code = RandomizerOf(sCardSetup);
  } else if (env != nullptr && env[0] != '\0') {
    std::string error;
    if (!xd::randomizer::NormalizeCode(env, code, error)) {
      std::fprintf(stderr, "[randomizer] XD_RANDOMIZER=%s: %s; off\n", env, error.c_str());
      code.clear();
    }
  }
  return code;
}

void RandomizerMade(const std::string& code) { sLoadedRandomizer = code; }

std::string MoreShadowsOf(const ep::Setup& s) { return xd::xg::Off(xd::xg::Feature::Randomizer) != nullptr ? std::string() : s.moreShadows; }

std::string BootMoreShadows() {
  const char* env = std::getenv("XD_MORE_SHADOWS");
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return {};
  if (env != nullptr && std::strcmp(env, "0") == 0) {
    std::fprintf(stderr, "[more shadows] XD_MORE_SHADOWS=0: off this run\n");
    return {};
  }
  std::string text;
  if (sRestarted) {
    text = MoreShadowsOf(sRestartSetup);
  } else if (sCardState == CardState::Setup) {
    text = MoreShadowsOf(sCardSetup);
  } else if (env != nullptr && env[0] != '\0') {
    xd::randomizer::MoreShadowsPlan plan;
    if (xd::randomizer::ParseMoreShadows(env, plan)) {
      text = env;
    } else {
      std::fprintf(stderr, "[more shadows] XD_MORE_SHADOWS=%s: expected few|many|all:<seed number>; off\n", env);
    }
  }
  return text;
}

void MoreShadowsMade(const std::string& text) { sLoadedMoreShadows = text; }
const std::string& LoadedMoreShadows() { return sLoadedMoreShadows; }

namespace {
bool HasRule(const std::vector<std::string>& rules, const char* id) {
  return xd::xg::OffForRule(id) == nullptr && std::find(rules.begin(), rules.end(), id) != rules.end();
}
bool RandoGen57(const std::string& code) { return xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr && xd::randomizer::CodeIncludesGen57(code); }
bool RandoGen4(const std::string& code) { return xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr && xd::randomizer::CodeIncludesGen4(code); }
bool HasGen57(const std::vector<std::string>& rules) { return HasRule(rules, "gen57_pokemon"); }
bool HasGen4(const std::vector<std::string>& rules) { return HasRule(rules, "gen4_pokemon") || HasRule(rules, "gen4_rules") || HasGen57(rules); }
bool HasGen4Spots(const std::vector<std::string>& rules) { return HasGen4(rules) && HasRule(rules, "gen4_spots"); }
}

bool BootGen4() {
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return false;
  if (HasGen4(xd::challenges::TestSwitch())) return true;
  if (RandoGen4(BootRandomizer())) return true;
  if (sRestarted) return HasGen4(sRestartSetup.challenges);
  if (sCardState == CardState::Setup) return HasGen4(sCardSetup.challenges);
  return false;
}

bool BootGen57() {
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return false;
  if (HasGen57(xd::challenges::TestSwitch())) return true;
  if (RandoGen57(BootRandomizer())) return true;
  if (sRestarted) return HasGen57(sRestartSetup.challenges);
  if (sCardState == CardState::Setup) return HasGen57(sCardSetup.challenges);
  return false;
}

int BootRuleGen() {
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1") || xd::xg::On()) return 3;
  int g = xd::rulegen::NumbersOfIds(xd::challenges::TestSwitch());
  if (sRestarted) g = std::max(g, xd::rulegen::NumbersOfIds(sRestartSetup.challenges));
  else if (sCardState == CardState::Setup) g = std::max(g, xd::rulegen::NumbersOfIds(sCardSetup.challenges));
  return g;
}

bool BootGen4Spots() {
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return false;
  if (HasGen4Spots(xd::challenges::TestSwitch())) return true;
  if (sRestarted) return HasGen4Spots(sRestartSetup.challenges);
  if (sCardState == CardState::Setup) return HasGen4Spots(sCardSetup.challenges);
  return false;
}

bool BootModernTypes() {
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) return false;
  if (HasRule(xd::challenges::TestSwitch(), "modern_types")) return true;
  if (sRestarted) return HasRule(sRestartSetup.challenges, "modern_types");
  if (sCardState == CardState::Setup) return HasRule(sCardSetup.challenges, "modern_types");
  return false;
}

bool BootLooks(engine::rulesets::Looks& out) {
  out = {};
  if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) {
    out.sources = {"xd"};
    return true;
  }
  const xd::rulesets::TestChoice t = xd::rulesets::Test();
  if (t.hasLooks) { out = t.choice.looks; return true; }
  {
    const engine::rulesets::Looks legacy = xd::rulesets::FromLegacy(xd::challenges::TestSwitch(), "").looks;
    if (legacy.Says()) { out = legacy; return true; }
  }
  if (sRestarted) out = xd::rulesets::ChoiceOf(sRestartSetup).looks;
  else if (sCardState == CardState::Setup) out = xd::rulesets::ChoiceOf(sCardSetup).looks;
  return out.Says();
}
static std::string sLoadedLooks = "?";
static std::string sLooksMissing;
void LooksMissing(const std::string& names) { sLooksMissing = names; }
void LooksLoaded(const std::string& key) { sLoadedLooks = key; }
static std::string LooksKey(const engine::rulesets::Looks& looks) {
  const es::BoolSetting b = es::Global().Bool(xd::battle_models::kSetting);
  std::string key;
  for (const std::string& l : xd::rulesets::LayerOrderOf(looks, b.Valid() && b.Get())) key += (key.empty() ? "" : ",") + l;
  for (const auto& p : looks.overrides) key += ";" + std::to_string(p.first) + ":" + p.second;
  return key;
}
std::string LooksKeyOfBoot() {
  engine::rulesets::Looks l;
  BootLooks(l);
  return LooksKey(l);
}

bool NeedsRestart(const ep::Setup& setup) {
  if (sLoadedLooks != "?" && !EnvIs("XD_MODS", "0")) {
    engine::rulesets::Looks want = xd::rulesets::ChoiceOf(setup).looks;
    const xd::rulesets::TestChoice t = xd::rulesets::Test();
    if (t.hasLooks) want = t.choice.looks;
    else if (const engine::rulesets::Looks legacy = xd::rulesets::FromLegacy(xd::challenges::TestSwitch(), "").looks; legacy.Says()) want = legacy;
    if (LooksKey(want) != sLoadedLooks) return true;
  }
  if ((HasRule(setup.challenges, "modern_types") || HasRule(xd::challenges::TestSwitch(), "modern_types")) != xd::modern::Loaded() &&
      !EnvIs("XD_MODS", "0")) {
    return true;
  }
  if (RandomizerOf(setup) != sLoadedRandomizer) return true;
  if (!EnvIs("XD_MODS", "0") && xd::gen4::Loaded() &&
      std::max(xd::rulegen::NumbersOfIds(setup.challenges), xd::rulegen::NumbersOfIds(xd::challenges::TestSwitch())) != xd::rulegen::NumbersLoaded()) {
    return true;
  }
  if (MoreShadowsOf(setup) != sLoadedMoreShadows) return true;
  if ((HasGen4(setup.challenges) || HasGen4(xd::challenges::TestSwitch())) && xd::gen4::PackPresent() != xd::gen4::Loaded()) return true;
  if (!HasGen4(setup.challenges) && !HasGen4(xd::challenges::TestSwitch()) && !RandoGen4(RandomizerOf(setup)) &&
      xd::gen4::Loaded()) {
    return true;
  }
  if (xd::gen4::Loaded() && (HasGen57(setup.challenges) || HasGen57(xd::challenges::TestSwitch()) || RandoGen57(RandomizerOf(setup))) != xd::gen57::Loaded() &&
      xd::gen57::PackPresent() && !EnvIs("XD_MODS", "0")) {
    return true;
  }
  if (xd::gen4::Loaded() && (HasGen4Spots(setup.challenges) || HasGen4Spots(xd::challenges::TestSwitch())) != xd::gen4::SpotsLoaded()) return true;
  if (xd::xg::Off(xd::xg::Feature::Mods) != nullptr) return false;
  std::vector<std::string> want, have;
  for (const ep::ModUse& m : setup.mods) want.push_back(Lower(m.id));
  for (const ep::ModUse& m : sLoaded) have.push_back(Lower(m.id));
  std::sort(want.begin(), want.end());
  std::sort(have.begin(), have.end());
  return want != have;
}

static bool RestartAlreadyDone(const ep::Setup& setup) {
  if (!sRestarted) return false;
  if (ep::Label(setup) != ep::Label(sRestartSetup)) return false;
  std::vector<std::string> a, b;
  for (const ep::ModUse& m : setup.mods) a.push_back(Lower(m.id));
  for (const ep::ModUse& m : sRestartSetup.mods) b.push_back(Lower(m.id));
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  return a == b && LooksKey(xd::rulesets::ChoiceOf(setup).looks) == LooksKey(xd::rulesets::ChoiceOf(sRestartSetup).looks);
}

void Begin(const ep::Setup& setup, const std::string& settingsChanged) {
  if (NeedsRestart(setup) && RestartAlreadyDone(setup)) {
    std::fprintf(stderr, "[playthrough] this New Game setup (%s) already started the game again once and still wants packs or mods that are not loaded "
                         "(Gen 4: %d, Gen 5-7: %d, other mods: see [mods] above): NOT starting again, carrying on with what is loaded\n",
                 ep::Label(setup).c_str(), xd::gen4::Loaded() ? 1 : 0, xd::gen57::Loaded() ? 1 : 0);
    Note(Tr("Some of the packs or mods chosen for this New Game could not be loaded, even after starting the game again. The game carries on without them (the log says which)."));
  } else if (NeedsRestart(setup)) {
    const fs::path file = xd::CardRoot() / L"playthroughs" / L"next_new_game.json";
    if (WriteSetup(file, setup)) {
      Note("new game setup needs other mods: starting again (" + ep::Label(setup) + ")");
      SetEnvironmentVariableW(L"XD_NEWGAME_SETUP", file.c_str());
      std::fprintf(stderr, "[playthrough] starting the game again with the mods of this setup: %s\n", ep::Label(setup).c_str());
      std::fflush(stderr);
      sRestartNow = true;
      sNewGameHeld = false;
      xd::picker::SetForRestart(xd::picker::Intent::NewGame);
      return;
    }
    std::fprintf(stderr, "[playthrough] could not write %ls: the new game keeps the mods loaded now\n", file.wstring().c_str());
  }
  if (!settingsChanged.empty()) PortInput_SettingsNote(settingsChanged.c_str());
  sPending = true;
  sPendingSetup = setup;
  sPendingSetup.mods = sLoaded;
  sPendingSetup.addedRows = CurrentAddedRows();
  SetRandomizer(sPendingSetup, sLoadedRandomizer);
  sPendingSetup.moreShadows = sLoadedMoreShadows;
  sPendingSetup.chosenBy = sScreen ? "screen" : "run";
  {
    const xd::rulesets::TestChoice t = xd::rulesets::Test();
    engine::rulesets::Choice c = xd::rulesets::ChoiceOf(sPendingSetup);
    if (sRestarted && sRestartSetup.rules.present) {
      std::vector<std::string> a = xd::rulesets::LegacyIds(sRestartSetup.rules), b = xd::rulesets::LegacyIds(c);
      std::sort(a.begin(), a.end());
      std::sort(b.begin(), b.end());
      if (a == b) c = sRestartSetup.rules;
    } else if (sPendingSetup.rules.present) {
      c = sPendingSetup.rules;
    }
    if (t.hasRuleSet && !t.choice.ruleSet.empty()) { c.ruleSet = t.choice.ruleSet; c.mechanics = t.choice.mechanics; }
    if (t.hasPokemon) {
      if (!t.choice.gens.empty()) c.gens = t.choice.gens;
      if (!t.choice.pokemonOptions.empty()) c.pokemonOptions = t.choice.pokemonOptions;
    }
    if (t.hasLooks) c.looks = t.choice.looks;
    c.present = true;
    xd::rulesets::Apply(sPendingSetup, c);
  }
  if (sPendingSetup.split.empty()) sPendingSetup.split = SplitNow(sPendingSetup.engine);
  sKeyAtNewGame = sCardId.found ? sCardId.key : std::string();
  sChosen = Chosen::NewGame;
  sChosenFrame = PortVI_GetFlipCount();
  sCurrent = sPendingSetup;
  sCurrentUnknown = false;
  AdoptCheats(nullptr);
  xd::challenges::SetActive(sCurrent.challenges, "new game");
  xd::shiny::SetActive(sCurrent.options, "new game");
  ApplySplit("new game");
  xd::challenges::SetState({}, "new game");
  xd::battles::Set({}, true, "new game");
  xd::pokedex::SetLookPins(xd::rulesets::ChoiceOf(sCurrent).looks.overrides);
  xd::scriptext::ClearValues("new game");
  xd::speedrun::NewGame(sCurrent.speedrun);
  Note("new game: " + ep::Label(sCurrent));
  if (sNewGameHeld) {
    sNewGameHeld = false;
    sNewGamePassOn = true;
    sInjectA = 4;
    sInjectFrom = PortVI_GetFlipCount();
  }
}

void NewGameBack() {
  sNewGameHeld = false;
  sNewGameTaken = false;
  Note("new game setup: back to the title menu (nothing chosen)");
}

bool ChallengesPossible() {
  return (sCardState == CardState::Setup && !sCardSetup.challenges.empty()) || (sRestarted && !sRestartSetup.challenges.empty()) ||
         !xd::challenges::TestSwitch().empty();
}

void RuleNote(const std::string& what) { sNotesLater.push_back("rule " + what); }

const std::vector<std::string>& ContinueProblems() { return sContinueProblems; }

void ContinueAnswer(bool carryOn) {
  if (!carryOn) {
    Note("continue stopped: the save's setup can't be met");
    return;
  }
  sContinueAccepted = true;
  std::string what;
  for (const std::string& p : sContinueProblems) what += (what.empty() ? "" : "; ") + p;
  RecordSetupChanged(what);
  sInjectA = 4;
  sInjectFrom = PortVI_GetFlipCount();
  Note("continue anyway (setup changed: " + what + ")");
}

void EngineAnswer(int answer) {
  if (answer != 1 && answer != 2) {
    Note("continue stopped: Faithful or Enhanced not answered");
    return;
  }
  const ep::Engine was = sCardSetup.engine;
  std::string changed;
  if (answer == 2) {
    ep::SwitchEngine(sCardSetup, sPlayersEngine, ep::Now());
    if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls (the save keeps its engine; asked again next time)\n",
                   xp::SetupFileFor(sCardId).wstring().c_str());
    }
    sAsk = false;
    changed = ep::ApplyEngine(es::Global(), sCardSetup.engine);
    std::fprintf(stderr, "[playthrough] the playthrough is %s from now on (was %s): for the game %s\n", ep::EngineName(sCardSetup.engine),
                 ep::EngineName(was), changed.empty() ? "nothing changes this run" : changed.c_str());
    if (changed.find("system.disc_timing") != std::string::npos || changed.find("system.card_timing") != std::string::npos) {
      PortXD_ShowNote(Fmt(Tr("Switched to %s. Loading speed and Memory card speed change the next time you start the game."),
                          EngineNameTr(sCardSetup.engine)),
                      7.f);
    }
  } else {
    std::fprintf(stderr, "[playthrough] the playthrough stays %s (kept at the Continue question; nothing written)\n", ep::EngineName(was));
  }
  sEngineAnswered = true;
  sChosen = Chosen::Continue;
  sChosenFrame = PortVI_GetFlipCount();
  AdoptSplit();
  sCurrent = sCardSetup;
  sCurrentUnknown = false;
  ApplySplit("continue, after the engine question");
  sInjectA = 4;
  sInjectFrom = PortVI_GetFlipCount();
  Note("continue: " + ep::Label(sCurrent) +
       (answer == 2 ? std::string(" (asked: switched from ") + ep::EngineName(was) + " to " + ep::EngineName(sCardSetup.engine) +
                          (changed.empty() ? std::string() : ": " + changed) + ")"
                    : std::string(" (asked: kept ") + ep::EngineName(was) + ")"));
}

bool EngineQuestionPossible() { return sAsk; }
ep::Engine EngineQuestionSave() { return sCardSetup.engine; }
std::vector<std::string> EngineQuestionSettings() { return ep::EngineSettingNames(es::Global()); }

bool ScreenEnabled() { return sScreen; }
std::string RestartLabel() { return sRestarted ? ep::DisplayLabel(sRestartSetup) : std::string(); }
bool TimerWatch() { return xd::speedrun::TitleWatchNeeded(sCardState == CardState::Setup && sCardSetup.speedrun.present); }
void TimerPersist() {
  if (sCardState != CardState::Setup || !sCardId.found) return;
  sCardSetup.speedrun = xd::speedrun::TimerState();
  sCardSetup.stats = xd::speedrun::StatsState();
  if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(sCardId).wstring().c_str());
  }
}

void PadPoll(PADStatus* status) {
  if (PortXD_CheatHidesStart()) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) status[i].button &= static_cast<u16>(~PAD_BUTTON_START);
  }
  if (status[0].err == PAD_ERR_NONE) xd::speedrun::NotePad(status[0].stickX, status[0].stickY);
  if (sInjectA > 0) {
    if (PortVI_GetFlipCount() >= sInjectFrom + static_cast<uint32_t>(sInjectA)) {
      sInjectA = 0;
      sNewGamePassOn = false;
    } else if (status[0].err == PAD_ERR_NONE) {
      status[0].button |= PAD_BUTTON_A;
    }
  }
}

bool TitleInputImpl(PADStatus* status, int select, u16 newButtons);
bool TitleInput(PADStatus* status, int select, u16 newButtons) {
  sInPadFilter = true;
  const bool kept = TitleInputImpl(status, select, newButtons);
  sInPadFilter = false;
  return kept;
}

bool TitleInputImpl(PADStatus* status, int select, u16 newButtons) {
  if ((newButtons & PAD_BUTTON_A) == 0) return false;
  if (select == 0 && sNewGamePassOn) {
    sNewGamePassOn = false;
    return false;
  }
  if (select == 1 && xd::picker::Possible() && !xd::picker::AnsweredThisRun() && xd::picker::Current() != xd::picker::Intent::Continue) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
      if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
    }
    PortInput_LatchHeld();
    if (!PortXD_OpenSavePicker()) {
      xd::picker::SetAnswered(true);
      sInjectA = 4;
      sInjectFrom = PortVI_GetFlipCount();
    }
    return true;
  }
  if (select == 0 && sScreen && xd::picker::WantsNewGameNow() && sRestarted) {
    static bool begun = false;
    if (begun) return false;
    begun = true;
    sNewGameTaken = true;
    sContinueAccepted = false;
    ep::Setup s = sRestartSetup;
    s.mods = sLoaded;
    Begin(s, "");
    return false;
  }
  if (!sScreen) {
    if (select == 0) {
      xd::speedrun::NewGame(xd::speedrun::NewGameDefault());
      AdoptCheats(nullptr);
    } else if (select == 1 && !(sAsk && !sEngineAnswered)) {
      xd::speedrun::Continue(sCardState == CardState::Setup ? sCardSetup : ep::Setup{}, sCardId.key, sCardState == CardState::Setup);
      AdoptCheats(sCardState == CardState::Setup ? &sCardSetup : nullptr);
      AdoptSplit();
      ApplySplitOf(sCardState == CardState::Setup ? &sCardSetup : nullptr, "continue");
    }
    if (select == 1 && sAsk && !sEngineAnswered) {
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
      }
      PortInput_LatchHeld();
      return PortXD_OpenEngineQuestion();
    }
    return false;
  }
  if (select == 0) {
    if (sNewGameTaken && sChosen == Chosen::NewGame && sPending) {
      Note("new game again: the setup chosen before (" + ep::Label(sCurrent) + ")");
      std::string label = ep::DisplayLabel(sCurrent);
      for (size_t at; (at = label.find("\xC2\xB7")) != std::string::npos;) label.replace(at, 2, "-");
      PortXD_ShowNote(Fmt(Tr("New Game: %s (as chosen before)"), label.c_str()), 4.f);
      return false;
    }
    sNewGameTaken = true;
    sContinueAccepted = false;
    if (sScreen) {
      if (!PortXD_OpenNewGameSetup()) {
        std::fprintf(stderr, "[playthrough] the New Game setup screen could not open\n");
        return false;
      }
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
      }
      PortInput_LatchHeld();
      sNewGameHeld = true;
      return true;
    } else {
      ep::Setup s = SetupOf(EngineNow(), {}, sLoadedRandomizer,
                            sRestarted ? sRestartSetup.challenges : xd::challenges::TestSwitch(),
                            sRestarted ? sRestartSetup.options : TestOptions());
      s.moreShadows = sLoadedMoreShadows;
      s.mods = sLoaded;
      if (sRestarted && sRestartSetup.rules.present) s.rules = sRestartSetup.rules;
      Begin(s, "");
    }
    return false;
  }
  if (select == 1) {
    if (sScreen && sCardState == CardState::Setup && HasGen4(sCardSetup.challenges) && !xd::gen4::PackPresent()) {
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
      }
      PortInput_LatchHeld();
      std::fprintf(stderr, "[playthrough] CONTINUE refused: the save has Gen 4 Pokemon and the Gen 4 pack is not there (the save is not touched)\n");
      if (PortXD_OpenContinueRefusal(Tr("This playthrough has Gen 4 Pokemon."), Tr("Add your Battle Revolution disc or your Ultra Sun / Ultra Moon dump in the launcher (Extra assets) to continue it."),
                                     Tr("Your save is safe and unchanged. It needs the Gen 4 pack because the Pokemon in it come from there."), std::string())) return true;
      return false;
    }
    const bool again = sChosen == Chosen::Continue;
    sPending = false;
    sChosen = Chosen::Continue;
    sChosenFrame = PortVI_GetFlipCount();
    sCurrentUnknown = sCardState != CardState::Setup;
    AdoptSplit();
    if (sCardState == CardState::Setup) sCurrent = sCardSetup;
    AdoptCheats(sCardState == CardState::Setup ? &sCardSetup : nullptr);
    ApplySplit("continue");
    xd::challenges::SetActive(sCardState == CardState::Setup ? sCurrent.challenges : std::vector<std::string>(), "continue");
    xd::shiny::SetActive(!xd::shiny::TestSwitch().empty() ? xd::shiny::TestSwitch()
                         : sCardState == CardState::Setup ? sCurrent.options : std::vector<std::string>(), "continue");
    xd::challenges::SetState(sCardState == CardState::Setup ? sCurrent.challengeState : std::vector<std::string>(), "continue");
    if (sCardState == CardState::Setup) {
      xd::battles::Set(sCurrent.battlesWon, sCurrent.battlesWonKept, "continue");
      xd::pokedex::SetLists(sCurrent.dexCaught, sCurrent.dexShiny, sCurrent.dexKept);
      xd::pokedex::SetLookPins(xd::rulesets::ChoiceOf(sCurrent).looks.overrides);
      xd::scriptext::LoadValues(sCurrent.modData, "continue");
    } else {
      xd::battles::Set({}, false, "continue");
      xd::pokedex::SetLists("", "", false);
      xd::scriptext::ClearValues("continue: a save without a setup file");
    }
    if (sCardState == CardState::Setup && !sContinueProblems.empty() && !sContinueAccepted && sScreen) {
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
      }
      PortInput_LatchHeld();
      if (PortXD_OpenContinueQuestion()) return true;
      return false;
    }
    if (sAsk && !sEngineAnswered) {
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
      }
      PortInput_LatchHeld();
      if (PortXD_OpenEngineQuestion()) return true;
      return false;
    }
    if (!sContinueNotes.empty() && sScreen) {
      std::string list;
      for (size_t i = 0; i < sContinueNotes.size(); i++) list += (i > 0 ? "; " : "") + sContinueNotes[i];
      PortXD_ShowNote(Fmt(Tr("This save's setup had: %s. The game carries on without it."), list.c_str()), 6.f);
    }
    if (!sLooksMissing.empty() && sScreen && !again) {
      PortXD_ShowNote(Fmt(Tr("%s not found: the next source in this playthrough's list shows instead."), sLooksMissing.c_str()), 5.f);
      sLooksMissing.clear();
    }
    if (!again) {
      xd::speedrun::Continue(sCardState == CardState::Setup ? sCardSetup : ep::Setup{}, sCardId.key, sCardState == CardState::Setup);
      Note(std::string("continue: ") + (sCurrentUnknown ? ep::UnknownLabel() : ep::Label(sCurrent)));
    }
  }
  return false;
}

}

extern "C" {

int PortXD_SaveBoundToOtherRows(char* text, size_t size) {
  if (sCardState != CardState::Setup || sCardSetup.addedRows.empty()) return 0;
  const std::vector<ep::AddedRowRef> now = CurrentAddedRows();
  const ep::BoundCheck check = ep::CheckBound(sCardSetup, now);
  std::string list;
  if (check.plan.Refused()) {
    for (size_t i = 0; i < check.plan.problems.size() && i < 3; i++) list += (i > 0 ? "; " : "") + check.plan.problems[i];
  } else if (check.plan.Needed()) {
    xd::save::IdRemap map;
    for (const em::RemapStep& st : check.plan.steps) {
      auto& target = st.kind == "species" ? map.species : st.kind == "move" ? map.moves : map.items;
      target[static_cast<uint16_t>(st.from)] = static_cast<uint16_t>(st.to);
      std::fprintf(stderr, "[playthrough] labels: %s was number %u in the save, is number %u now\n", st.label.c_str(), st.from, st.to);
    }
    size_t changed = 0;
    std::string error;
    if (xp::RemapCardSave(sCardId, map, changed, error)) {
      std::fprintf(stderr, "[playthrough] labels: the save's numbers were changed to the numbers the mods give now (%zu values; the old file is in the save backups)\n", changed);
      sCardSetup.addedRows = now;
      if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
        std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(sCardId).wstring().c_str());
      }
      return 0;
    }
    list = Fmt(Tr("the numbers of the mods' rows are not what they were when it was saved, and the save could not be adjusted (%s)"), error.c_str());
  } else {
    return 0;
  }
  const std::string out = Fmt(Tr("The save on the memory card holds Pokemon, moves or items that mods added, and their numbers would "
                                 "mean other things without the same mods: %s. Nothing on the card is read or changed."),
                              list.c_str());
  std::snprintf(text, size, "%s", out.c_str());
  return 1;
}

const char* PortXD_PlaythroughHeader(void) {
  static std::string text;
  text = "playthrough on the memory card: " + CardLabel();
  if (!sLoadedRandomizer.empty()) text += "; randomizer this run: " + sLoadedRandomizer;
  return text.c_str();
}

const char* PortXD_PlaythroughReport(void) {
  sReport.clear();
  switch (sChosen) {
  case Chosen::NewGame:
    sReport = "playthrough: " + ep::Label(sCurrent) + " (new game at game frame " + std::to_string(sChosenFrame) + ")";
    break;
  case Chosen::Continue:
    sReport = "playthrough: " + (sCurrentUnknown ? ep::UnknownLabel() : ep::Label(sCurrent)) + " (continued at game frame " +
              std::to_string(sChosenFrame) + ")";
    break;
  default: sReport = "playthrough: not chosen yet; on the memory card: " + CardLabel(); break;
  }
  return sReport.c_str();
}

void PortXD_PlaythroughSaveWritten(const uint8_t* file, size_t size) {
  const xp::SaveId id = xp::SaveIdOf(std::vector<uint8_t>(file, file + size));
  if (!id.found) return;
  xd::speedrun::SaveWritten();
  if (sPending && id.key != sKeyAtNewGame) {
    sPendingSetup.trainer = id.trainer;
    sPendingSetup.trainerId = id.trainerId;
    sPendingSetup.secretId = id.secretId;
    const fs::path f = xp::SetupFileFor(id);
    sPendingSetup.extRequired = xd::saveext::RequiredNow();
    sPendingSetup.battlesWonKept = xd::battles::Complete();
    sPendingSetup.battlesWon = xd::battles::List();
    if (xd::pokedex::Enabled()) {
      xd::pokedex::RefreshPlayer();
      sPendingSetup.dexKept = true;
      sPendingSetup.dexCaught = xd::pokedex::CaughtText();
      sPendingSetup.dexShiny = xd::pokedex::ShinyText();
    }
    sPendingSetup.modData = xd::scriptext::ExportValues();
    sPendingSetup.speedrun = xd::speedrun::TimerState();
    sPendingSetup.stats = xd::speedrun::StatsState();
    if (WriteSetup(f, sPendingSetup)) {
      std::fprintf(stderr, "[playthrough] the new playthrough's setup is kept next to its save: %ls\n", f.wstring().c_str());
      Note("setup kept for " + id.trainer + ": " + ep::Label(sPendingSetup));
      sCardSetup = sPendingSetup;
      sCardState = CardState::Setup;
      sPending = false;
      if (sRestarted) {
        SetEnvironmentVariableW(L"XD_NEWGAME_SETUP", nullptr);
        std::error_code ec;
        fs::remove(sRestartFile, ec);
      }
    } else {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", f.wstring().c_str());
    }
  } else if (!sPending && !sScreen && id.key != sBootKey) {
    const fs::path f = xp::SetupFileFor(id);
    std::error_code ec;
    if (!fs::exists(f, ec)) {
      std::vector<std::string> runOptions = xd::shiny::Active();
      for (const std::string& o : xd::pt::CurrentOptions()) runOptions.push_back(o);
      ep::Setup s = xd::pt::SetupOf(EngineNow(), {}, sLoadedRandomizer, xd::challenges::TestSwitch(), runOptions);
      s.moreShadows = sLoadedMoreShadows;
      s.mods = sLoaded;
      s.addedRows = CurrentAddedRows();
      s.trainer = id.trainer;
      s.trainerId = id.trainerId;
      s.secretId = id.secretId;
      s.chosenBy = "run";
      MergeCheats(s);
      xd::battles::Set(xd::battles::SinceStart(), true, "a new game's first save");
      s.battlesWonKept = true;
      s.battlesWon = xd::battles::List();
      if (xd::pokedex::Enabled()) {
        xd::pokedex::RefreshPlayer();
        s.dexKept = true;
        s.dexCaught = xd::pokedex::CaughtText();
        s.dexShiny = xd::pokedex::ShinyText();
      }
      s.modData = xd::scriptext::ExportValues();
      s.speedrun = xd::speedrun::TimerState();
      s.stats = xd::speedrun::StatsState();
      if (WriteSetup(f, s)) {
        std::fprintf(stderr, "[playthrough] a new game's first save (no setup screen this run): setup kept next to it: %ls\n",
                     f.wstring().c_str());
        Note("setup kept for " + id.trainer + " (new game, no setup screen): " + ep::Label(s));
        sCardSetup = s;
        sCardState = CardState::Setup;
        if (sChosen == Chosen::None) {
          sChosen = Chosen::NewGame;
          sCurrent = s;
        }
      } else {
        std::fprintf(stderr, "[playthrough] could not write %ls\n", f.wstring().c_str());
      }
    }
  }
  if (sCardState == CardState::Setup && xd::saveext::RequiredNow() != sCardSetup.extRequired) {
    sCardSetup.extRequired = xd::saveext::RequiredNow();
    if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
    }
  }
  if (sCardState == CardState::Setup) {
    const std::vector<std::string> log = xd::challenges::TakeLog();
    const std::vector<std::string> state = xd::challenges::AnyActive() ? xd::challenges::State() : sCardSetup.challengeState;
    if (!log.empty() || state != sCardSetup.challengeState) {
      for (const std::string& e : log) sCardSetup.challengeLog.push_back(ep::Now() + ": " + e);
      sCardSetup.challengeState = state;
      if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
        std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
      }
      if (sChosen != Chosen::None && !sCurrentUnknown) {
        sCurrent.challengeLog = sCardSetup.challengeLog;
        sCurrent.challengeState = sCardSetup.challengeState;
      }
      std::fprintf(stderr, "[playthrough] challenge log kept with the save: %zu new line(s); challenge state: %zu line(s)%s\n",
                   log.size(), state.size(), sCardSetup.Failed() ? "; the run is marked as failed" : "");
    }
  }
  if (sCardState == CardState::Setup && sCardSetup.battlesWonKept && xd::battles::Complete() &&
      xd::battles::List() != sCardSetup.battlesWon) {
    sCardSetup.battlesWon = xd::battles::List();
    if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
    }
    if (sChosen != Chosen::None && !sCurrentUnknown) sCurrent.battlesWon = sCardSetup.battlesWon;
    std::fprintf(stderr, "[playthrough] battles won kept with the save: %zu\n", sCardSetup.battlesWon.size());
  }
  if (sCardState == CardState::Setup && xd::pokedex::Enabled()) {
    xd::pokedex::RefreshPlayer();
    if (!sCardSetup.dexKept || xd::pokedex::CaughtText() != sCardSetup.dexCaught || xd::pokedex::ShinyText() != sCardSetup.dexShiny) {
      sCardSetup.dexKept = true;
      sCardSetup.dexCaught = xd::pokedex::CaughtText();
      sCardSetup.dexShiny = xd::pokedex::ShinyText();
      if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
        std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
      }
      if (sChosen != Chosen::None && !sCurrentUnknown) {
        sCurrent.dexKept = true;
        sCurrent.dexCaught = sCardSetup.dexCaught;
        sCurrent.dexShiny = sCardSetup.dexShiny;
      }
    }
  }
  if (sCardState == CardState::Setup && xd::pokedex::Enabled() && xd::pokedex::LookPinsChanged()) {
    {
      engine::rulesets::Choice c = xd::rulesets::ChoiceOf(sCardSetup);
      c.looks.overrides = xd::pokedex::LookPins();
      c.present = true;
      sCardSetup.rules = c;
    }
    xd::pokedex::ClearLookPinsChanged();
    std::fprintf(stderr, "[playthrough] Looks pins kept with this save: %zu\n", sCardSetup.rules.looks.overrides.size());
    if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
    }
    if (sChosen != Chosen::None && !sCurrentUnknown) sCurrent.rules = sCardSetup.rules;
  }
  if (sCardState == CardState::Setup && xd::scriptext::ValuesChanged(sCardSetup.modData)) {
    sCardSetup.modData = xd::scriptext::ExportValues();
    if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
    } else {
      xd::scriptext::Values().MarkSaved();
      std::fprintf(stderr, "[playthrough] the numbers script mods keep were written next to the save: %zu mod(s)\n", sCardSetup.modData.size());
    }
    if (sChosen != Chosen::None && !sCurrentUnknown) sCurrent.modData = sCardSetup.modData;
  }
  if (sCardState == CardState::Setup && xd::speedrun::PlaythroughChosen()) {
    sCardSetup.speedrun = xd::speedrun::TimerState();
    sCardSetup.stats = xd::speedrun::StatsState();
    if (sCardSetup.speedrun.present || sCardSetup.stats.kept) {
      if (!WriteSetup(xp::SetupFileFor(id), sCardSetup)) {
        std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
      } else if (sCardSetup.speedrun.present) {
        std::fprintf(stderr, "[playthrough] timer kept with the save: real time %u ms, loads %u ms, %zu split(s)\n",
                     sCardSetup.speedrun.rtaMs, sCardSetup.speedrun.loadMs, sCardSetup.speedrun.splits.size());
      }
    }
    if (sChosen != Chosen::None && !sCurrentUnknown) {
      sCurrent.speedrun = sCardSetup.speedrun;
      sCurrent.stats = sCardSetup.stats;
    }
  }
  sCardId = id;
  sTablesDue = true;
  xd::picker::ThumbnailAfterSave(xp::SetupFolder() / (id.key + ".png"));
}

void PortXD_PlaythroughFrame(void) {
  if (sTablesDue && sCardState == CardState::Setup && sCardId.found && !sRestartNow && xd::speedrun::MapStarted()) {
    sTablesDue = false;
    const char* off = std::getenv("XD_TABLES_FILE");
    if (off == nullptr || std::strcmp(off, "0") != 0) {
      xd::tablesfile::Write(xp::SetupFolder() / (sCardId.key + ".tables"), ep::TablesStamp(sCardSetup));
    }
  }
  xd::picker::ThumbnailFrame();
  xd::discord::Frame();
  xd::battles::Frame();
  if (!xd::speedrun::PlaythroughChosen() && xd::speedrun::MapStarted()) {
    if (sCardState == CardState::NoSave) {
      xd::speedrun::NewGame(xd::speedrun::NewGameDefault());
    } else {
      xd::speedrun::Continue(sCardState == CardState::Setup ? sCardSetup : ep::Setup{}, sCardId.key, sCardState == CardState::Setup);
    }
  }
  xd::speedrun::SetEngine(sChosen != Chosen::None && !sCurrentUnknown && !sCurrent.PureFaithful());
  {
    const char* req = std::getenv("XD_REQUIRE_ORIGINAL");
    const bool plain = es::Global().CurrentPreset() == es::Preset::Original || (req != nullptr && std::strcmp(req, "1") == 0) ||
                       (sChosen != Chosen::None && !sCurrentUnknown && sCurrent.PureFaithful());
    PortAudio_SetPeakLimiter(plain ? 0 : 1);
  }
  xd::speedrun::Frame();
  xd::challenges::Frame();
  if (sRestartNow) {
    OSResetSystem(0, 0, 0);
  }
  if (!sNotesLater.empty()) {
    const std::vector<std::string> notes = std::move(sNotesLater);
    sNotesLater.clear();
    for (const std::string& n : notes) Note(n);
  }
  static bool read = false;
  if (!read) {
    read = true;
    uint32_t frame = 0;
    for (size_t i = 0; const char* list = PortInput_ReplaySettingsAt(i, &frame); i++) sReplaySettings.emplace_back(frame, list);
    if (!sReplaySettings.empty()) std::fprintf(stderr, "[playthrough] replaying %zu settings changes\n", sReplaySettings.size());
  }
  while (sReplayNext < sReplaySettings.size()) {
    const uint32_t at = PortInput_ReplayRecordedFrame();
    if (at == UINT32_MAX || at < sReplaySettings[sReplayNext].first) break;
    const auto& [frame, recorded] = sReplaySettings[sReplayNext++];
    std::string list = recorded;
    static const bool video = [] {
      const char* e = std::getenv("XD_REPLAY_VIDEO_SETTINGS");
      return e == nullptr || e[0] != '0';
    }();
    if (!video) {
      list.clear();
      for (size_t from = 0; from <= recorded.size();) {
        size_t end = recorded.find(';', from);
        if (end == std::string::npos) end = recorded.size();
        std::string item = recorded.substr(from, end - from);
        const size_t first = item.find_first_not_of(' ');
        if (first != std::string::npos && item.compare(first, 6, "video.") != 0) list += (list.empty() ? "" : ";") + item;
        from = end + 1;
      }
    }
    const int bad = es::Global().ApplyOverrides(list);
    std::fprintf(stderr, "[playthrough] replay: recorded frame %u (game frame %u here): settings %s%s\n", frame,
                 PortVI_GetFlipCount(), list.c_str(), bad ? " (some refused)" : "");
  }
}

void PortXD_PlaythroughCheatsChanged(void) {
  if (sCardState != CardState::Setup || !sCardId.found) return;
  if (!WriteSetup(xp::SetupFileFor(sCardId), sCardSetup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(sCardId).wstring().c_str());
  }
}

}
