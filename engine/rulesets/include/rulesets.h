#ifndef ENGINE_RULESETS_H
#define ENGINE_RULESETS_H

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace engine::mods::json { struct Value; }

namespace engine::rulesets {

struct RuleSet {
  std::string id;
  std::string name;
  std::string info;
  int generation = 0;
  bool available = true;
  std::string unavailableWhy;
  std::vector<int> needsGens;
  std::vector<std::pair<std::string, std::string>> mechanics;
};

struct LooksSource {
  std::string id;
  std::string name;
};

struct LooksPreset {
  std::string id;
  std::string name, info;
  std::vector<std::string> sources;
};

struct Generation {
  int number = 0;
  std::string name;
  bool available = true;
  std::string unavailableWhy;
};

class Registry {
public:
  void AddRuleSet(RuleSet r) { mRuleSets.push_back(std::move(r)); }
  void AddSource(LooksSource s) { mSources.push_back(std::move(s)); }
  void AddPreset(LooksPreset p) { mPresets.push_back(std::move(p)); }
  void AddGeneration(Generation g) { mGens.push_back(std::move(g)); }
  void SetFallbackSource(std::string id) { mFallback = std::move(id); }
  void SetDefaults(std::string ruleSet, std::vector<int> gens, std::string preset) { mDefRuleSet = std::move(ruleSet); mDefGens = std::move(gens); mDefPreset = std::move(preset); }

  const std::vector<RuleSet>& RuleSets() const { return mRuleSets; }
  const std::vector<LooksSource>& Sources() const { return mSources; }
  const std::vector<LooksPreset>& Presets() const { return mPresets; }
  const std::vector<Generation>& Generations() const { return mGens; }
  const RuleSet* FindRuleSet(std::string_view id) const;
  const LooksSource* FindSource(std::string_view id) const;
  const LooksPreset* FindPreset(std::string_view id) const;
  const Generation* FindGeneration(int number) const;
  const std::string& FallbackSource() const { return mFallback; }
  const std::string& DefaultRuleSet() const { return mDefRuleSet; }
  const std::vector<int>& DefaultGens() const { return mDefGens; }
  const std::string& DefaultPreset() const { return mDefPreset; }

private:
  std::vector<RuleSet> mRuleSets;
  std::vector<LooksSource> mSources;
  std::vector<LooksPreset> mPresets;
  std::vector<Generation> mGens;
  std::string mFallback = "xd", mDefRuleSet, mDefPreset;
  std::vector<int> mDefGens;
};

struct Looks {
  std::string preset;
  std::vector<std::string> sources;
  std::map<int, std::string> overrides;
  bool Says() const { return !sources.empty(); }
  bool operator==(const Looks& o) const { return sources == o.sources && overrides == o.overrides; }
};

struct Choice {
  bool present = false;
  std::string ruleSet;
  std::vector<int> gens;
  std::map<std::string, std::string> mechanics;
  std::vector<std::string> pokemonOptions;
  Looks looks;

  bool HasGen(int g) const;
  bool HasOption(const std::string& id) const;
  void SetOption(const std::string& id, bool on);
  std::string Mechanic(const Registry& registry, const std::string& id) const;
  bool MechanicOn(const Registry& registry, const std::string& id) const { return Mechanic(registry, id) == "on"; }

  std::string Canonical() const;
  bool operator==(const Choice& o) const { return Canonical() == o.Canonical(); }
};

std::vector<std::string> Validate(const Choice& c, const Registry& registry);
Choice Normalize(Choice c, const Registry& registry);
Choice Default(const Registry& registry);
bool ParseCanonical(std::string_view text, Choice& out, std::string& error);

std::string ToJsonKeys(const Choice& c, int indent);
bool FromJson(const mods::json::Value& root, Choice& out, std::string& error);

std::vector<std::string> Ranked(const Looks& looks, const Registry& registry);
std::vector<std::string> LayerOrder(const Looks& looks, const Registry& registry);
using HasModel = std::function<bool(const std::string& source, int species)>;
struct Pick {
  std::string source;
  bool overridden = false;
  int rank = 0;
};
Pick ResolveSpecies(const Looks& looks, const Registry& registry, int species, const HasModel& has);

struct LegacyMap {
  std::string gen4Pokemon = "gen4_pokemon", gen4Rules = "gen4_rules", gen4Spots = "gen4_spots", modernTypes = "modern_types",
              modelsBr = "models_br", modelsUsum = "models_usum";
};

struct LevelRule {
  enum class Kind { AsIs, SetAll, Cap, Range } kind = Kind::AsIs;
  int a = 0, b = 0;
  std::string Text() const;
  static bool Parse(std::string_view text, LevelRule& out);
};

inline constexpr const char* kClauseSpecies = "species";
inline constexpr const char* kClauseItem = "item";
inline constexpr const char* kClauseSleep = "sleep";
inline constexpr const char* kClauseOhko = "ohko";
inline constexpr const char* kClauseEvasion = "evasion";
const std::vector<std::string>& KnownClauses();
bool ClauseCheckedAtSubmit(const std::string& id);

struct Format {
  std::string id, name, info;
  std::string ruleSet;
  std::vector<int> gens;
  std::map<std::string, std::string> details;
  LevelRule level;
  std::set<std::string> clauses;
  std::set<int> bannedSpecies, bannedItems, bannedMoves, bannedAbilities;
  std::set<std::string> bannedCategories;
  std::string style = "single";
  int pick = 0;
  int timerPreview = 0;
  int teamMin = 1, teamMax = 6;
  bool teamPreview = false;
  int timerPerChoice = 0;
  int timerPerBattle = 0;
  bool operator==(const Format& o) const { return ToText() == o.ToText(); }
  std::string ToText() const;
};
bool ParseFormat(std::string_view text, Format& out, std::string& error);

struct TeamMember {
  int species = 0, level = 0, item = 0, ability = 0;
  int moves[4] = {0, 0, 0, 0};
};
struct ClauseData {
  std::set<int> ohkoMoves, evasionMoves, endlessMoves, endlessItems;
  std::map<std::string, std::set<int>> categories;
  std::function<std::string(const char* kind, int id)> name;
};
std::vector<std::string> CheckTeam(const Format& format, const std::vector<TeamMember>& team, const ClauseData& data,
                                   const std::vector<int>& roomGens = {}, const std::function<int(int species)>& speciesGen = {});
struct MemberProblem {
  int member = -1;
  std::string kind, arg;
};
std::vector<MemberProblem> TeamProblems(const Format& format, const std::vector<TeamMember>& team, const ClauseData& data, const std::vector<int>& roomGens = {},
                                        const std::function<int(int species)>& speciesGen = {});
int LevelUnder(const LevelRule& rule, int ownLevel);

}

#endif
