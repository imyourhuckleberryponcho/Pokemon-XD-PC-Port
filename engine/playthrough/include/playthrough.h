#ifndef ENGINE_PLAYTHROUGH_H
#define ENGINE_PLAYTHROUGH_H

#include <mod_values.h>
#include <mods.h>
#include <rulesets.h>
#include <settings.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::playthrough {

enum class Engine { Faithful, Enhanced };
const char* EngineId(Engine engine);
const char* EngineName(Engine engine);
bool ParseEngine(std::string_view text, Engine& out);

struct ModUse {
  std::string id, name, version;
  mods::Applies applies = mods::Applies::NewPlaythrough;
};

struct AddedRowRef {
  std::string mod, table, name;
  uint32_t id = 0;
  std::string label;
  bool operator==(const AddedRowRef& o) const { return mod == o.mod && table == o.table && name == o.name && id == o.id && label == o.label; }
};

struct SpeedSplit {
  std::string id;
  uint32_t rtaMs = 0, loadMs = 0;
};
struct SpeedBreak {
  uint32_t atMs = 0;
  uint32_t lengthMs = 0;
  uint32_t epoch = 0;
};
struct Speedrun {
  bool present = false;
  bool on = false;
  bool withoutLoads = false;
  int start = 2, end = 2;
  int category = 0;
  std::vector<SpeedBreak> breaks;
  bool breakPending = false;
  uint32_t breakEpoch = 0, breakAtMs = 0;
  std::vector<std::string> invalid;
  uint32_t rtaMs = 0, loadMs = 0;
  std::vector<SpeedSplit> splits;
  bool finished = false;
  uint32_t finalRtaMs = 0, finalLoadMs = 0;
};

struct PlayStats {
  bool kept = false;
  uint32_t battlesWon = 0, battlesLost = 0, battlesOther = 0;
};

struct Setup {
  Speedrun speedrun;
  PlayStats stats;
  Engine engine = Engine::Faithful;
  std::vector<AddedRowRef> addedRows;
  std::vector<ModUse> mods;
  std::string randomizer, randomizerVersion, seed;
  std::vector<std::string> challenges;
  std::vector<std::string> options;
  std::string split;
  std::string moreShadows;
  std::string edition;
  rulesets::Choice rules;
  std::string ruleSetStarted;
  std::vector<std::string> ruleSetChanged;
  std::string started;
  std::string portVersion;
  std::string chosenBy;
  std::vector<std::string> engineChanged;
  Engine startedEngine = Engine::Faithful;
  Engine StartedAs() const { return engineChanged.empty() ? engine : startedEngine; }
  bool PureFaithful() const { return engine == Engine::Faithful && engineChanged.empty(); }
  std::vector<std::string> cheatsUsed;
  std::vector<std::string> cheatState;
  bool cheatWarned = false;
  std::vector<std::string> setupChanged;
  std::vector<std::string> challengeLog;
  std::vector<std::string> challengeState;
  std::vector<mods::ModValueSet> modData;
  bool battlesWonKept = false;
  std::vector<uint32_t> battlesWon;
  bool dexKept = false;
  std::string dexCaught, dexShiny;
  std::vector<std::string> extRequired;
  bool Failed() const;
  std::string trainer;
  uint32_t trainerId = 0, secretId = 0;

  std::vector<const ModUse*> WorldMods() const;
  bool Vanilla() const { return WorldMods().empty() && randomizer.empty() && challenges.empty() && options.empty() && moreShadows.empty(); }
};

std::string Label(const Setup& setup);
std::string DisplayLabel(const Setup& setup);
std::string UnknownLabel();

std::string ToJson(const Setup& setup);
bool FromJson(std::string_view text, Setup& out, std::string& error);
bool ReadFile(const std::filesystem::path& file, Setup& out, std::string& error);
bool WriteFile(const std::filesystem::path& file, const Setup& setup);

std::filesystem::path FileFor(const std::filesystem::path& folder, std::string_view key);

struct Check {
  std::vector<std::string> worldProblems;
  std::vector<std::string> notes;
  bool Ok() const { return worldProblems.empty() && notes.empty(); }
};
Check Compare(const Setup& setup, const std::vector<mods::ModFolder>& found);

std::vector<std::string> BoundProblems(const Setup& setup, const std::vector<AddedRowRef>& now);

struct BoundCheck {
  mods::RemapPlan plan;
};
BoundCheck CheckBound(const Setup& setup, const std::vector<AddedRowRef>& now);

std::vector<std::string> WorldModIds(const Setup& setup);

std::string Now();

bool IsEngineSetting(const settings::Definition& def);
std::string ApplyEngine(settings::Registry& registry, Engine engine);
Engine DefaultEngine(const settings::Registry& registry);

bool EngineQuestionDue(const Setup& setup, Engine playersEngine);
void SwitchEngine(Setup& setup, Engine to, const std::string& when);
std::vector<std::string> EngineSettingNames(const settings::Registry& registry);

}

#endif
