#ifndef XD_RANDOMIZER_H
#define XD_RANDOMIZER_H

#include <data_mods.h>
#include <randomizer.h>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xd::randomizer {

constexpr const char* kModId = "randomizer";

enum class Style { Light, Classic, Chaos, Custom };
bool StyleFromName(const std::string& name, Style& out);

const engine::randomizer::Schema& GetSchema();
void ApplyStyle(engine::randomizer::Settings& s, Style style);

struct TextFix {
  std::map<uint32_t, std::string> messages;
  std::set<uint32_t> ownDescription;
  bool Empty() const { return messages.empty(); }
};

struct StarterBase {
  uint32_t count = 1;
  uint32_t species[2] = {0x85, 0};
  uint32_t level[2] = {10, 0};
  uint32_t moves[2][4] = {{0x21, 0x27, 0x2C, 0x1C}, {}};
};
constexpr StarterBase kXGStarterBase = {2, {0x85, 0x120}, {15, 14}, {{0x1D, 0x2C, 0x130, 0xB6}, {0x21, 0x2D, 0xB6, 0x10E}}};

struct StarterPlan {
  bool on = false;
  uint32_t choice = 0;
  uint32_t species[3] = {};
  uint32_t count = 0;
  uint32_t level = 10;
  bool natural = true;
  uint32_t moves[3][4] = {};
  StarterBase base;
  bool second = false;
  uint32_t species2 = 0;
  uint32_t level2 = 0;
  uint32_t moves2[4] = {};
};
constexpr uint32_t kStarterEevee = 0x85;
constexpr uint32_t kStarterEeveeMoves[4] = {0x21, 0x27, 0x2C, 0x1C};

struct Result {
  engine::randomizer::ModOutput mod;
  StarterPlan starter;
  TextFix text;
  std::string code;
  std::string GameJson(const std::string& note) const;
};

bool Generate(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read,
              const engine::randomizer::Settings& s, Result& out, std::string& error, const StarterBase* starterBase = nullptr);

bool WriteMod(const Result& r, const engine::randomizer::Settings& s, const std::filesystem::path& folder, std::string& error);

struct Gen4Species {
  uint32_t id = 0, dex = 0;
  bool legendary = false;
  std::string name;
  uint32_t stats[6] = {}, types[2] = {}, abilities[2] = {}, evolves[5] = {}, level[20] = {}, move[20] = {};
};
void SetGen4Species(std::vector<Gen4Species> species);
bool LoadGen4Species(const std::filesystem::path& randomizerTxt, std::string& error);
void TranslateGen4Species(const std::function<void(Gen4Species&)>& fix);
void TranslateGen57Species(const std::function<void(Gen4Species&)>& fix);
bool CodeIncludesGen4(const std::string& code);
bool LoadGen57Species(const std::filesystem::path& randomizerTxt, std::string& error);
bool CodeIncludesGen57(const std::string& code);

Style StyleOf(const engine::randomizer::Settings& s);
uint32_t CodeVersionOf(const std::string& code);
std::string CodeFor(Style style, uint64_t seed);
bool NormalizeCode(const std::string& text, std::string& code, std::string& error);

struct MoreShadowsPlan {
  int amount = 0;
  uint64_t seed = 0;
};
bool ParseMoreShadows(const std::string& text, MoreShadowsPlan& plan);
std::string MoreShadowsText(int amount, uint64_t seed);
uint32_t MoreShadowsRows();
bool GenerateMoreShadows(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, const MoreShadowsPlan& plan,
                         const Result* rando, bool gen4, engine::randomizer::ModOutput& out, std::string& error, bool gen57 = false);

}

#endif
