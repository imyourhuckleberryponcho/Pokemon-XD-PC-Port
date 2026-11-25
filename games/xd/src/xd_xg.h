#ifndef XD_XG_H
#define XD_XG_H

#include "dol_tables.h"

#include <cstdint>
#include <cstring>

namespace xd::xg {

inline bool On() { return PortXD_IsXG() != 0; }

enum class Feature { Gen4, Gen4Pokemon, Gen57Pokemon, PrizeDesk, ModernTypes, LanguagePacks, DataAddRows, Mods, Randomizer, Online };
inline const char* Off(Feature f) {
  if (!On()) return nullptr;
  switch (f) {
  case Feature::Gen4: return "XG has its own modern rules.";
  case Feature::Gen4Pokemon: return nullptr;
  case Feature::Gen57Pokemon: return nullptr;
  case Feature::PrizeDesk: return nullptr;
  case Feature::ModernTypes: return "XG has its own modern rules.";
  case Feature::LanguagePacks: return "Would replace XG's own text.";
  case Feature::DataAddRows: return "Only new Pokemon, moves and items can be added on XG (tried there: add, save, CONTINUE, refused without the mod); story flags, rooms, battles, trainer decks and a Pokemon's own model, cry and face were not tried on XG.";
  case Feature::Mods: return nullptr;
  case Feature::Randomizer: return nullptr;
  case Feature::Online: return nullptr;
  }
  return nullptr;
}

inline void SetAbilityRow(void* table, uint32_t id, uint32_t nameMsg, uint32_t docMsg) {
  auto* rows = static_cast<uint32_t*>(table);
  if (On()) {
    rows[2 * id] = nameMsg;
    rows[2 * id + 1] = docMsg;
  } else {
    rows[3 * id] = 0;
    rows[3 * id + 1] = nameMsg;
    rows[3 * id + 2] = docMsg;
  }
}

inline const char* Edition() { return On() ? "xg" : "xd"; }

inline const char* OffForRule(const char* ruleId) {
  if (std::strcmp(ruleId, "gen4_pokemon") == 0) return Off(Feature::Gen4Pokemon);
  if (std::strcmp(ruleId, "gen57_pokemon") == 0) return Off(Feature::Gen57Pokemon);
  if (std::strncmp(ruleId, "gen4_", 5) == 0) return Off(Feature::Gen4);
  if (std::strcmp(ruleId, "modern_types") == 0) return Off(Feature::ModernTypes);
  return nullptr;
}

}

#endif
