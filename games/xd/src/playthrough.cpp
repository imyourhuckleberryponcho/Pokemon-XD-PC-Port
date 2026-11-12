#include "xd_playthrough.h"
#include "xd_battles_won.h"
#include "xd_challenges.h"

#include "xd_playthrough_files.h"
#include "xd_randomizer.h"
#include "xd_user_folder.h"

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
}

namespace {

namespace ep = engine::playthrough;
namespace em = engine::mods;
namespace es = engine::settings;
namespace xp = xd::playthrough;
namespace fs = std::filesystem;

enum class CardState { NoSave, NoFile, Setup, Unreadable };
const char* const kCardStateText[] = {"no save", "a save without a setup file", "a save with its setup",
                                      "a save whose setup file can't be read"};

xp::SaveId sCardId;
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
ep::Engine sPlayersEngine = ep::Engine::Faithful;
bool sAsk = false;
bool sEngineAnswered = false;
bool sNewGameTaken = false;
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
  if (!ep::WriteFile(xp::SetupFileFor(sCardId), sCardSetup)) {
    std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(sCardId).wstring().c_str());
  }
}

}

extern "C" void PortXD_PlaythroughSaveWritten(const uint8_t* file, size_t size);
extern "C" void PortXD_SetSaveWrittenHook(void (*hook)(const uint8_t* file, size_t size));

extern "C" void PortXD_PlaythroughBoot(void) {
  PortXD_SetSaveWrittenHook(PortXD_PlaythroughSaveWritten);
  sCardId = xp::SaveIdOnCard();
  if (sCardId.found) sBootKey = sCardId.key;
  if (sCardId.found) {
    std::string error;
    const fs::path file = xp::SetupFileFor(sCardId);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
      sCardState = CardState::NoFile;
    } else if (ep::ReadFile(file, sCardSetup, error)) {
      sCardState = CardState::Setup;
    } else {
      sCardState = CardState::Unreadable;
      std::fprintf(stderr, "[playthrough] the save's setup file %ls %s: treated as a save without one\n", file.wstring().c_str(),
                   error.c_str());
    }
  }
  std::fprintf(stderr, "[playthrough] memory card: %s%s%s (%s)\n", kCardStateText[static_cast<int>(sCardState)],
               sCardId.found ? ", trainer " : "", sCardId.found ? sCardId.trainer.c_str() : "", CardLabel().c_str());
  if (!xd::challenges::TestSwitch().empty()) xd::challenges::SetActive({}, "XD_CHALLENGES");
  if (sCardState == CardState::NoSave) {
    xd::battles::Set({}, true, "start-up: no save on the card");
  } else if (sCardState == CardState::Setup) {
    xd::battles::Set(sCardSetup.battlesWon, sCardSetup.battlesWonKept, "start-up: the save on the card");
  } else {
    xd::battles::Set({}, false, "start-up: a save without a setup file");
  }
  if (const wchar_t* next = _wgetenv(L"XD_NEWGAME_SETUP"); next != nullptr && next[0] != L'\0') {
    std::string error;
    if (ep::ReadFile(next, sRestartSetup, error)) {
      sRestarted = true;
      sRestartFile = next;
      std::fprintf(stderr, "[playthrough] started again for a New Game setup: %s\n", ep::Label(sRestartSetup).c_str());
    } else {
      std::fprintf(stderr, "[playthrough] XD_NEWGAME_SETUP: %ls %s; ignored\n", next, error.c_str());
    }
  }
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
      if (!loaded && FoundMod(w->id) != nullptr) sContinueProblems.push_back((w->name.empty() ? w->id : w->name) + " could not be used (see the mods message)");
    }
    for (const std::string& p : sContinueProblems) std::fprintf(stderr, "[playthrough] the save's setup: %s\n", p.c_str());
    for (const std::string& n : sContinueNotes) std::fprintf(stderr, "[playthrough] the save's setup (note): %s\n", n.c_str());
  }
}

Defaults NewGameDefaults() {
  Defaults d;
  d.restarted = sRestarted;
  d.engine = sRestarted ? sRestartSetup.engine : ep::DefaultEngine(es::Global());
  if (sRestarted) d.randomizer = RandomizerOf(sRestartSetup);
  if (sRestarted) d.challenges = sRestartSetup.challenges;
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
                  const std::vector<std::string>& challenges) {
  ep::Setup s;
  s.engine = engine;
  s.challenges = challenges;
  for (const ModChoice& m : mods) {
    if (m.on) s.mods.push_back({m.id, m.name, m.version, m.applies});
  }
  SetRandomizer(s, randomizer);
  s.started = ep::Now();
  s.portVersion = XD_PORT_VERSION;
  return s;
}

const std::string& LoadedRandomizer() { return sLoadedRandomizer; }

std::string RandomizerOf(const ep::Setup& s) { return s.randomizer.empty() ? std::string() : s.seed; }

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

bool NeedsRestart(const ep::Setup& setup) {
  if (RandomizerOf(setup) != sLoadedRandomizer) return true;
  std::vector<std::string> want, have;
  for (const ep::ModUse& m : setup.mods) want.push_back(Lower(m.id));
  for (const ep::ModUse& m : sLoaded) have.push_back(Lower(m.id));
  std::sort(want.begin(), want.end());
  std::sort(have.begin(), have.end());
  return want != have;
}

void Begin(const ep::Setup& setup, const std::string& settingsChanged) {
  if (NeedsRestart(setup)) {
    const fs::path file = xd::CardRoot() / L"playthroughs" / L"next_new_game.json";
    if (ep::WriteFile(file, setup)) {
      Note("new game setup needs other mods: starting again (" + ep::Label(setup) + ")");
      SetEnvironmentVariableW(L"XD_NEWGAME_SETUP", file.c_str());
      std::fprintf(stderr, "[playthrough] starting the game again with the mods of this setup: %s\n", ep::Label(setup).c_str());
      std::fflush(stderr);
      sRestartNow = true;
      return;
    }
    std::fprintf(stderr, "[playthrough] could not write %ls: the new game keeps the mods loaded now\n", file.wstring().c_str());
  }
  if (!settingsChanged.empty()) PortInput_SettingsNote(settingsChanged.c_str());
  sPending = true;
  sPendingSetup = setup;
  sPendingSetup.mods = sLoaded;
  SetRandomizer(sPendingSetup, sLoadedRandomizer);
  sPendingSetup.chosenBy = sScreen ? "screen" : "run";
  sKeyAtNewGame = sCardId.found ? sCardId.key : std::string();
  sChosen = Chosen::NewGame;
  sChosenFrame = PortVI_GetFlipCount();
  sCurrent = sPendingSetup;
  sCurrentUnknown = false;
  xd::challenges::SetActive(sCurrent.challenges, "new game");
  xd::challenges::SetState({}, "new game");
  xd::battles::Set({}, true, "new game");
  Note("new game: " + ep::Label(sCurrent));
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
    if (!ep::WriteFile(xp::SetupFileFor(sCardId), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls (the save keeps its engine; asked again next time)\n",
                   xp::SetupFileFor(sCardId).wstring().c_str());
    }
    sAsk = false;
    changed = ep::ApplyEngine(es::Global(), sCardSetup.engine);
    std::fprintf(stderr, "[playthrough] the playthrough is %s from now on (was %s): for the game %s\n", ep::EngineName(sCardSetup.engine),
                 ep::EngineName(was), changed.empty() ? "nothing changes this run" : changed.c_str());
    if (changed.find("system.disc_timing") != std::string::npos || changed.find("system.card_timing") != std::string::npos) {
      PortXD_ShowNote(std::string("Switched to ") + ep::EngineName(sCardSetup.engine) +
                          ". Loading speed and Memory card speed change the next time you start the game.",
                      7.f);
    }
  } else {
    std::fprintf(stderr, "[playthrough] the playthrough stays %s (kept at the Continue question; nothing written)\n", ep::EngineName(was));
  }
  sEngineAnswered = true;
  sChosen = Chosen::Continue;
  sChosenFrame = PortVI_GetFlipCount();
  sCurrent = sCardSetup;
  sCurrentUnknown = false;
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

void PadPoll(PADStatus* status) {
  if (sInjectA > 0) {
    if (PortVI_GetFlipCount() >= sInjectFrom + static_cast<uint32_t>(sInjectA)) {
      sInjectA = 0;
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
  if (!sScreen) {
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
      std::string label = ep::Label(sCurrent);
      for (size_t at; (at = label.find("\xC2\xB7")) != std::string::npos;) label.replace(at, 2, "-");
      PortXD_ShowNote("New Game: " + label + " (as chosen before)", 4.f);
      return false;
    }
    sNewGameTaken = true;
    sContinueAccepted = false;
    if (sScreen) {
      if (!PortXD_OpenNewGameSetup()) std::fprintf(stderr, "[playthrough] the New Game setup screen could not open\n");
    } else {
      ep::Setup s = SetupOf(EngineNow(), {}, sLoadedRandomizer,
                            sRestarted ? sRestartSetup.challenges : xd::challenges::TestSwitch());
      s.mods = sLoaded;
      Begin(s, "");
    }
    return false;
  }
  if (select == 1) {
    const bool again = sChosen == Chosen::Continue;
    sPending = false;
    sChosen = Chosen::Continue;
    sChosenFrame = PortVI_GetFlipCount();
    sCurrentUnknown = sCardState != CardState::Setup;
    if (sCardState == CardState::Setup) sCurrent = sCardSetup;
    xd::challenges::SetActive(sCardState == CardState::Setup ? sCurrent.challenges : std::vector<std::string>(), "continue");
    xd::challenges::SetState(sCardState == CardState::Setup ? sCurrent.challengeState : std::vector<std::string>(), "continue");
    if (sCardState == CardState::Setup) {
      xd::battles::Set(sCurrent.battlesWon, sCurrent.battlesWonKept, "continue");
    } else {
      xd::battles::Set({}, false, "continue");
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
      std::string text = "This save's setup had: ";
      for (size_t i = 0; i < sContinueNotes.size(); i++) text += (i > 0 ? "; " : "") + sContinueNotes[i];
      PortXD_ShowNote(text + ". The game carries on without it.", 6.f);
    }
    if (!again) Note(std::string("continue: ") + (sCurrentUnknown ? ep::UnknownLabel() : ep::Label(sCurrent)));
  }
  return false;
}

}

extern "C" {

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
  if (sPending && id.key != sKeyAtNewGame) {
    sPendingSetup.trainer = id.trainer;
    sPendingSetup.trainerId = id.trainerId;
    sPendingSetup.secretId = id.secretId;
    const fs::path f = xp::SetupFileFor(id);
    sPendingSetup.battlesWonKept = xd::battles::Complete();
    sPendingSetup.battlesWon = xd::battles::List();
    if (ep::WriteFile(f, sPendingSetup)) {
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
      ep::Setup s = xd::pt::SetupOf(EngineNow(), {}, sLoadedRandomizer, xd::challenges::TestSwitch());
      s.mods = sLoaded;
      s.trainer = id.trainer;
      s.trainerId = id.trainerId;
      s.secretId = id.secretId;
      s.chosenBy = "run";
      xd::battles::Set(xd::battles::SinceStart(), true, "a new game's first save");
      s.battlesWonKept = true;
      s.battlesWon = xd::battles::List();
      if (ep::WriteFile(f, s)) {
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
  if (sCardState == CardState::Setup) {
    const std::vector<std::string> log = xd::challenges::TakeLog();
    const std::vector<std::string> state = xd::challenges::AnyActive() ? xd::challenges::State() : sCardSetup.challengeState;
    if (!log.empty() || state != sCardSetup.challengeState) {
      for (const std::string& e : log) sCardSetup.challengeLog.push_back(ep::Now() + ": " + e);
      sCardSetup.challengeState = state;
      if (!ep::WriteFile(xp::SetupFileFor(id), sCardSetup)) {
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
    if (!ep::WriteFile(xp::SetupFileFor(id), sCardSetup)) {
      std::fprintf(stderr, "[playthrough] could not write %ls\n", xp::SetupFileFor(id).wstring().c_str());
    }
    if (sChosen != Chosen::None && !sCurrentUnknown) sCurrent.battlesWon = sCardSetup.battlesWon;
    std::fprintf(stderr, "[playthrough] battles won kept with the save: %zu\n", sCardSetup.battlesWon.size());
  }
  sCardId = id;
}

void PortXD_PlaythroughFrame(void) {
  xd::battles::Frame();
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

}
