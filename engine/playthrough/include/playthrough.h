#ifndef ENGINE_PLAYTHROUGH_H
#define ENGINE_PLAYTHROUGH_H

#include <mods.h>
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
  bool operator==(const AddedRowRef& o) const { return mod == o.mod && table == o.table && name == o.name && id == o.id; }
};

struct Setup {
  Engine engine = Engine::Faithful;
  std::vector<AddedRowRef> addedRows;
  std::vector<ModUse> mods;
  std::string randomizer, randomizerVersion, seed;
  std::vector<std::string> challenges;
  std::string started;
  std::string portVersion;
  std::string chosenBy;
  std::vector<std::string> engineChanged;
  Engine startedEngine = Engine::Faithful;
  Engine StartedAs() const { return engineChanged.empty() ? engine : startedEngine; }
  bool PureFaithful() const { return engine == Engine::Faithful && engineChanged.empty(); }
  std::vector<std::string> setupChanged;
  std::vector<std::string> challengeLog;
  std::vector<std::string> challengeState;
  bool battlesWonKept = false;
  std::vector<uint32_t> battlesWon;
  bool Failed() const;
  std::string trainer;
  uint32_t trainerId = 0, secretId = 0;

  std::vector<const ModUse*> WorldMods() const;
  bool Vanilla() const { return WorldMods().empty() && randomizer.empty() && challenges.empty(); }
};

std::string Label(const Setup& setup);
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
