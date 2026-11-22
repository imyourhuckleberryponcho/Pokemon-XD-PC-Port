#ifndef XD_RULESET_TEXTS_H
#define XD_RULESET_TEXTS_H

#include "i18n.h"

#include <cstring>
#include <string>

namespace xd::rulesets::text {
using engine::i18n::Tr;

inline const char* RuleSetName(const std::string& id) {
  if (id == "xd") return Tr("XD original (Gen 3)");
  if (id == "gen4") return Tr("Gen 4 (Diamond / Pearl / Platinum)");
  if (id == "gen5") return Tr("Gen 5 (Black / White)");
  if (id == "gen6") return Tr("Gen 6 (X / Y)");
  if (id == "gen7") return Tr("Gen 7 (Sun / Moon)");
  return Tr("Unknown rule set");
}
inline const char* RuleSetShort(const std::string& id) {
  if (id == "xd") return Tr("XD original");
  if (id == "gen4") return Tr("Gen 4");
  if (id == "gen5") return Tr("Gen 5");
  if (id == "gen6") return Tr("Gen 6");
  if (id == "gen7") return Tr("Gen 7");
  return Tr("Unknown rule set");
}
inline const char* RuleSetBlurb(const std::string& id) {
  if (id == "xd") return Tr("the GameCube game's own rules");
  if (id == "gen4") return Tr("Abilities, moves and the split as in Gen 4");
  if (id == "gen5") return Tr("Gen 4 rules, Gen 5 numbers");
  if (id == "gen6") return Tr("Fairy type, softer critical hits");
  if (id == "gen7") return Tr("Milder paralysis and burn");
  return Tr("later");
}
inline const char* RuleSetInfo(const std::string& id) {
  if (id == "xd") return Tr("XD original (Gen 3): the GameCube game's own battle rules.");
  if (id == "gen4") return Tr("Gen 4: abilities and moves work as in the Gen 4 games. Physical and special moves are split.");
  if (id == "gen5") return Tr("Gen 5: the Gen 4 battle rules with the Black / White numbers (stats, move power, accuracy and PP). Rock Pokemon get more Sp. Def in a sandstorm.");
  if (id == "gen6") return Tr("Gen 6: the Fairy type and the newer type chart, critical hits that do 1.5 times (and are surer with Focus Energy), the X / Y numbers.");
  if (id == "gen7") return Tr("Gen 7: the Gen 6 rules, paralysis halves Speed (it quartered it before), a burn hurts a sixteenth, the Sun / Moon numbers.");
  return Tr("Not available yet: it needs packs that do not exist yet.");
}

inline const char* PokemonName(int gen) {
  switch (gen) {
  case 3: return Tr("Gen 1-3 (XD's own)");
  case 4: return Tr("Gen 1-4");
  case 5: return Tr("Gen 1-5");
  case 6: return Tr("Gen 1-6");
  case 7: return Tr("Gen 1-7");
  default: return Tr("Unknown");
  }
}
inline const char* PokemonShort(int gen) {
  switch (gen) {
  case 3: return Tr("Gen 1-3");
  case 4: return Tr("Gen 1-4");
  case 5: return Tr("Gen 1-5");
  case 6: return Tr("Gen 1-6");
  case 7: return Tr("Gen 1-7");
  default: return Tr("Unknown");
  }
}
inline const char* PokemonBlurb(int gen) {
  switch (gen) {
  case 3: return Tr("386 Pokemon");
  case 4: return Tr("493 Pokemon");
  case 5: case 6: case 7: return Tr("Ultra Sun / Moon dump");
  default: return Tr("later");
  }
}
inline const char* PokemonInfo(int gen) {
  switch (gen) {
  case 3: return Tr("The 386 Pokemon of the GameCube game.");
  case 4: return Tr("Adds the 107 Gen 4 Pokemon, their moves, items and evolutions. \"Also at the Poke Spots\": some of them can turn up at the Poke Spots too.");
  case 5: case 6: case 7: return Tr("Adds the Gen 5 to 7 Pokemon (so far a few), made from your own Ultra Sun / Moon dump.");
  default: return Tr("Not available yet: it needs packs that do not exist yet.");
  }
}

inline const char* StyleName(const std::string& id) {
  if (id == "original_xd") return Tr("Original XD look");
  if (id == "most_modern") return Tr("Most modern");
  if (id == "br_first") return Tr("Battle Revolution first");
  return Tr("Custom...");
}
inline const char* StyleOrder(const std::string& id) {
  if (id == "original_xd") return Tr("XD > Battle Revolution > Ultra Sun/Moon");
  if (id == "most_modern") return Tr("Ultra Sun/Moon > Battle Revolution > XD");
  if (id == "br_first") return Tr("Battle Revolution > XD > Ultra Sun/Moon");
  return Tr("your own order");
}
inline const char* StyleShort(const std::string& id) {
  if (id == "original_xd") return Tr("XD look");
  if (id == "most_modern") return Tr("modern look");
  if (id == "br_first") return Tr("BR look");
  return Tr("custom look");
}
inline const char* SourceName(const std::string& id) {
  if (id == "xd") return Tr("XD (your disc)");
  if (id == "br") return Tr("Battle Revolution");
  if (id == "usum") return Tr("Ultra Sun / Ultra Moon");
  if (id == "xy") return Tr("X / Y");
  return Tr("Unknown source");
}
inline const char* SourceShort(const std::string& id) {
  if (id == "xd") return Tr("XD");
  if (id == "br") return Tr("Battle Revolution");
  if (id == "usum") return Tr("Ultra Sun/Moon");
  if (id == "xy") return Tr("X/Y");
  return Tr("Unknown source");
}
inline const char* SourceAdd(const std::string& id) {
  if (id == "usum" || id == "xy") return Tr("add your dump");
  return Tr("add your disc");
}

inline std::string FormatName(const std::string& id, const std::string& fallback) {
  if (id == "xd_classic") return Tr("XD Classic");
  if (id == "xd_classic_doubles") return Tr("XD Classic Doubles");
  if (id == "level50_singles") return Tr("Level 50 Singles");
  if (id == "gen4_singles") return Tr("Gen 4 Singles");
  if (id == "gen4_doubles") return Tr("Gen 4 Doubles");
  if (id == "anything_goes") return Tr("Anything goes");
  return fallback;
}
inline const char* FormatInfo(const std::string& id) {
  if (id == "xd_classic") return Tr("XD Classic: the GameCube game's rules, teams and levels as they are, no extra rules.");
  if (id == "xd_classic_doubles") return Tr("XD Classic Doubles: double battles under the GameCube game's rules, no extra rules.");
  if (id == "level50_singles") return Tr("Level 50 Singles: XD rules with the split, all Pokemon set to level 50 for the battle, one of each species, no two same items, sleep clause, no OHKO / evasion moves. Bring 6, pick 3.");
  if (id == "gen4_singles") return Tr("Gen 4 Singles: Gen 4 rules, level 50, one of each species, no two same items, sleep clause, no OHKO / evasion moves. Bring 6, pick 3.");
  if (id == "gen4_doubles") return Tr("Gen 4 Doubles: Gen 4 rules in double battles, level 50, one of each species, no two same items. Bring 6, pick 4.");
  if (id == "anything_goes") return Tr("Anything goes: the rows below are yours; no extra rules.");
  return "";
}
inline const char* ClauseName(const std::string& id) {
  if (id == "species") return Tr("Species");
  if (id == "item") return Tr("Item");
  if (id == "sleep") return Tr("Sleep");
  if (id == "ohko") return Tr("OHKO");
  if (id == "evasion") return Tr("Evasion");
  return Tr("Other");
}
inline const char* ClauseInfo(const std::string& id) {
  if (id == "species") return Tr("Species clause: one of each Pokemon per team.");
  if (id == "item") return Tr("Item clause: no two Pokemon hold the same item.");
  if (id == "sleep") return Tr("Sleep clause: a player cannot put a second foe to sleep while one is still asleep from their move; the move fails.");
  if (id == "ohko") return Tr("OHKO clause: Fissure, Sheer Cold, Horn Drill and Guillotine are not allowed.");
  if (id == "evasion") return Tr("Evasion clause: Double Team and Minimize are not allowed.");
  return "";
}
inline const char* CategoryName(const std::string& id) {
  if (id == "legendary") return Tr("legendary Pokemon");
  if (id == "mythical") return Tr("mythical Pokemon");
  return Tr("Other");
}

}

#endif
