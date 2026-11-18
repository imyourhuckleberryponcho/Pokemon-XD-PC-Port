#ifndef XD_XG_H
#define XD_XG_H

#include "dol_tables.h"

#include <cstring>

namespace xd::xg {

inline bool On() { return PortXD_IsXG() != 0; }

enum class Feature { Gen4, Gen4Pokemon, ModernTypes, LanguagePacks, DataAddRows, Mods, Randomizer, Online };
inline const char* Off(Feature f) {
  if (!On()) return nullptr;
  switch (f) {
  case Feature::Gen4: return "XG has its own modern rules.";
  case Feature::Gen4Pokemon: return nullptr;
  case Feature::ModernTypes: return "XG has its own modern rules.";
  case Feature::LanguagePacks: return "Would replace XG's own text.";
  case Feature::DataAddRows: return "Adding rows is not available on XG: its tables have their own layout.";
  case Feature::Mods: return nullptr;
  case Feature::Randomizer: return nullptr;
  case Feature::Online: return nullptr;
  }
  return nullptr;
}

inline const char* Edition() { return On() ? "xg" : "xd"; }

inline const char* OffForRule(const char* ruleId) {
  if (std::strcmp(ruleId, "gen4_pokemon") == 0) return Off(Feature::Gen4Pokemon);
  if (std::strncmp(ruleId, "gen4_", 5) == 0) return Off(Feature::Gen4);
  if (std::strcmp(ruleId, "modern_types") == 0) return Off(Feature::ModernTypes);
  return nullptr;
}

}

#endif
