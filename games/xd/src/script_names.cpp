#include "xd_gen4_abilities.h"
#include "xd_modern_types.h"
#include "xd_scripts.h"

#include <mods.h>
#include <script_names.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

extern const char kStoryFlagNamesText[];
extern const unsigned long kStoryFlagNamesText_size;

namespace xd::scripts {

namespace {

using engine::script::NameEntry;

void FromDisc(const engine::mods::DataCatalog& catalog, const char* list, const char* category, engine::script::Names& out) {
  const engine::mods::NameList* l = catalog.FindList(list);
  if (l == nullptr) return;
  std::vector<NameEntry> entries;
  for (const auto& [name, number] : l->names) entries.push_back({name, static_cast<int64_t>(number)});
  out.Add(category, entries);
}

}

engine::script::Names BuildNames(const engine::mods::LoadResult& result) {
  engine::script::Names names;
  if (result.catalog != nullptr) {
    const engine::mods::DataCatalog& c = *result.catalog;
    FromDisc(c, "type", "type", names);
    FromDisc(c, "species", "species", names);
    FromDisc(c, "move", "move", names);
    FromDisc(c, "item", "item", names);
    FromDisc(c, "ability", "ability", names);
    FromDisc(c, "nature", "nature", names);
    FromDisc(c, "story_step", "story", names);
  }
  const struct { const char* table; const char* category; } kAdded[] = {
      {"pokemon_data", "species"}, {"waza_data", "move"}, {"item_data_prime", "item"}, {"fight_encount_data", "battle"}};
  for (const auto& a : kAdded) {
    std::vector<NameEntry> entries;
    for (const engine::mods::AddedRow& r : result.addedRows) {
      if (r.table == a.table) entries.push_back({r.key, static_cast<int64_t>(r.id)});
    }
    if (!entries.empty()) names.Add(a.category, entries);
  }
  if (xd::modern::Loaded()) names.Add("type", {{"Fairy", 18}});
  {
    std::vector<std::pair<std::string, int64_t>> abilities;
    xd::gen4::AppendAbilityNames(abilities);
    std::vector<NameEntry> entries;
    for (const auto& [n, id] : abilities) entries.push_back({n, id});
    if (!entries.empty()) names.Add("ability", entries);
  }
  {
    std::vector<NameEntry> flags;
    std::vector<std::string> problems;
    engine::script::ParseNameFile(std::string_view(kStoryFlagNamesText, kStoryFlagNamesText_size), flags, problems);
    for (const std::string& p : problems) std::fprintf(stderr, "[script] story_flag_names.txt %s\n", p.c_str());
    if (!flags.empty()) names.Add("flag", flags);
  }
  if (result.catalog != nullptr) FromDisc(*result.catalog, "flag", "flag", names);
  return names;
}

}
