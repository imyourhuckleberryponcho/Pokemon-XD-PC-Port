#ifndef XD_RULESETS_H
#define XD_RULESETS_H

#include <rulesets.h>

#include <string>
#include <vector>

namespace engine::playthrough { struct Setup; }

namespace xd::rulesets {

const engine::rulesets::Registry& Registry();

inline constexpr const char* kGen4Rules = "gen4_rules";
inline constexpr const char* kSplit = "split";
inline constexpr const char* kMegaEvolution = "mega_evolution";
inline constexpr const char* kNumbers = "numbers";
inline constexpr const char* kModernTypes = "modern_types";
inline constexpr const char* kOptionSpots = "spots";

bool IsOwnedId(const std::string& id);
engine::rulesets::Choice FromLegacy(const std::vector<std::string>& challenges, const std::string& split);
std::vector<std::string> LegacyIds(const engine::rulesets::Choice& choice);

engine::rulesets::Choice ChoiceOf(const engine::playthrough::Setup& setup);
bool Resolve(engine::playthrough::Setup& setup);
void Stamp(engine::playthrough::Setup& setup);
void Apply(engine::playthrough::Setup& setup, const engine::rulesets::Choice& choice);

struct TestChoice {
  bool any = false;
  bool hasRuleSet = false, hasPokemon = false, hasLooks = false;
  engine::rulesets::Choice choice;
};
TestChoice Test();
std::vector<std::string> TestLegacyIds();

engine::rulesets::Choice LauncherDefaults(bool gen4Ready = true);

struct SourceFolder {
  std::string id;
  const char* layerId;
  const char* name;
};
const SourceFolder* SourceLayer(const std::string& id);
std::vector<std::string> LayerOrderOf(const engine::rulesets::Looks& looks, bool settingOn);
std::string RankedText(const engine::rulesets::Looks& looks, bool settingOn);

int GenerationOfNational(int national);
bool IsLegendary(int national);
bool IsMythical(int national);

const std::vector<engine::rulesets::Format>& Formats();
const engine::rulesets::Format* FindFormat(const std::string& id);
std::string FormatDigest(const engine::rulesets::Format& f);
engine::rulesets::ClauseData ClauseDataFor(bool gen4Moves);

}

#endif
