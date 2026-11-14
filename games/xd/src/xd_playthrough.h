#ifndef XD_PLAYTHROUGH_H
#define XD_PLAYTHROUGH_H

#include "xd_save_picker.h"

#include <mods.h>
#include <playthrough.h>

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
  std::vector<std::string> challenges;
};

Defaults NewGameDefaults();
engine::playthrough::Setup SetupOf(engine::playthrough::Engine engine, const std::vector<ModChoice>& mods,
                                   const std::string& randomizer = {}, const std::vector<std::string>& challenges = {});
const std::string& LoadedRandomizer();
std::string BootRandomizer();
void RandomizerMade(const std::string& code);
bool NeedsRestart(const engine::playthrough::Setup& setup);
void Begin(const engine::playthrough::Setup& setup, const std::string& settingsChanged);

bool WorldModsFixed(std::vector<std::string>& ids);
std::vector<std::string> LoadedModIds();
engine::mods::AppliesVerdict ModApplies(const engine::mods::ModFolder& mod);
void RestartForMods(const std::vector<engine::playthrough::ModUse>& use);
void RestartToTitle();
bool PickerPossible();
void PickerAnswer(const xd::picker::Row* row);

const std::vector<std::string>& ContinueProblems();
void ContinueAnswer(bool carryOn);
bool EngineQuestionPossible();
engine::playthrough::Engine EngineQuestionSave();
void EngineAnswer(int answer);
std::vector<std::string> EngineQuestionSettings();

bool TitleInput(PADStatus* status, int select, u16 newButtons);
void PadPoll(PADStatus* status);
bool ScreenEnabled();
bool ChallengesPossible();
void RuleNote(const std::string& what);

}

bool PortXD_OpenNewGameSetup();
bool PortXD_OpenSavePicker();
bool PortXD_OpenContinueQuestion();
bool PortXD_OpenEngineQuestion();
bool PortXD_ShowRuleMessage(const std::string& text);
void PortXD_ShowNote(const std::string& text, float seconds);

#endif
