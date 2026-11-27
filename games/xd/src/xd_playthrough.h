#ifndef XD_PLAYTHROUGH_H
#define XD_PLAYTHROUGH_H

#include "xd_save_picker.h"

#include <mods.h>
#include <playthrough.h>
#include <rulesets.h>

#include <dolphin/pad.h>

#include <string>
#include <vector>

namespace xd::pt {

struct ModChoice {
  std::string id, name, version, description;
  engine::mods::Applies applies = engine::mods::Applies::NewPlaythrough;
  std::string reason;
  bool on = false;
};

struct Defaults {
  engine::playthrough::Engine engine = engine::playthrough::Engine::Faithful;
  std::vector<ModChoice> mods;
  bool restarted = false;
  std::string randomizer;
  std::string moreShadows;
  std::vector<std::string> challenges;
  std::vector<std::string> options;
  engine::rulesets::Choice rules;
};

Defaults NewGameDefaults();
engine::playthrough::Setup SetupOf(engine::playthrough::Engine engine, const std::vector<ModChoice>& mods,
                                   const std::string& randomizer = {}, const std::vector<std::string>& challenges = {},
                                   const std::vector<std::string>& options = {});
const std::vector<std::string>& CurrentOptions();
void OptionsChanged(const std::vector<std::string>& options, const std::string& note);
const std::string& LoadedRandomizer();
std::string BootRandomizer();
void RandomizerMade(const std::string& code);
std::string BootMoreShadows();
void MoreShadowsMade(const std::string& text);
const std::string& LoadedMoreShadows();
bool NeedsRestart(const engine::playthrough::Setup& setup);
void Begin(const engine::playthrough::Setup& setup, const std::string& settingsChanged);
void NewGameBack();

bool WorldModsFixed(std::vector<std::string>& ids);
std::vector<std::string> LoadedModIds();
engine::mods::AppliesVerdict ModApplies(const engine::mods::ModFolder& mod);
void RestartForMods(const std::vector<engine::playthrough::ModUse>& use);
void RestartNow(const char* why);
void RestartToTitle();
bool PickerPossible();
void PickerAnswer(const xd::picker::Row* row);

bool RestartForRoom(bool gen4, bool gen4Rules, const std::string& ruleSet = "");

const std::vector<std::string>& ContinueProblems();
void ContinueAnswer(bool carryOn);
bool EngineQuestionPossible();
engine::playthrough::Engine EngineQuestionSave();
void EngineAnswer(int answer);
void ApplySplit(const char* why);
std::string SplitNow(engine::playthrough::Engine engine);
std::vector<std::string> EngineQuestionSettings();

bool TitleInput(PADStatus* status, int select, u16 newButtons);
void PadPoll(PADStatus* status);
bool ScreenEnabled();
std::string RestartLabel();
bool TimerWatch();
bool ContinueWatch();
void TimerPersist();
bool ChallengesPossible();
void RuleNote(const std::string& what);

}

bool PortXD_OpenNewGameSetup();
bool PortXD_OpenSavePicker();
bool PortXD_OpenContinueQuestion();
bool PortXD_OpenContinueRefusal(const std::string& head, const std::string& red, const std::string& soft, const std::string& who);
bool PortXD_OpenEngineQuestion();
bool PortXD_ShowRuleMessage(const std::string& text);
void PortXD_ShowNote(const std::string& text, float seconds);

#endif
