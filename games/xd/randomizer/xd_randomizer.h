#ifndef XD_RANDOMIZER_H
#define XD_RANDOMIZER_H

#include <data_mods.h>
#include <randomizer.h>

#include <filesystem>
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

struct Result {
  engine::randomizer::ModOutput mod;
  TextFix text;
  std::string code;
  std::string GameJson(const std::string& note) const;
};

bool Generate(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read,
              const engine::randomizer::Settings& s, Result& out, std::string& error);

bool WriteMod(const Result& r, const engine::randomizer::Settings& s, const std::filesystem::path& folder, std::string& error);

struct Gen4Species {
  uint32_t id = 0, dex = 0;
  bool legendary = false;
  std::string name;
  uint32_t stats[6] = {}, types[2] = {}, abilities[2] = {}, evolves[5] = {}, level[20] = {}, move[20] = {};
};
void SetGen4Species(std::vector<Gen4Species> species);
bool LoadGen4Species(const std::filesystem::path& randomizerTxt, std::string& error);
bool CodeIncludesGen4(const std::string& code);

Style StyleOf(const engine::randomizer::Settings& s);
uint32_t CodeVersionOf(const std::string& code);
std::string CodeFor(Style style, uint64_t seed);
bool NormalizeCode(const std::string& text, std::string& code, std::string& error);

}

#endif
