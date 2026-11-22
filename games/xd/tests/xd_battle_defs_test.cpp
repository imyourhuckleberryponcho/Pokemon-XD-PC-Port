
#include "xd_battle_defs.h"

#include <cstdio>
#include <string>

using namespace xd::battle;

static int sFail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); sFail++; } } while (0)

static bool Resolve(const std::string& cat, const std::string& name, int64_t& v) {
  static const struct { const char* c; const char* n; int64_t v; } t[] = {
      {"move", "Flame Charge", 380}, {"move", "Hex", 381}, {"move", "Bite", 382}, {"move", "Crunch", 383}, {"type", "ice", 15}, {"type", "normal", 0}, {"type", "dark", 17}, {"type", "dragon", 16}, {"type", "fire", 10}, {"type", "grass", 12}, {"type", "water", 11}, {"item", "Leftovers", 200}, {"ability", "Clear Body", 29}};
  for (const auto& e : t) if (cat == e.c && name == e.n) { v = e.v; return true; }
  return false;
}

int main() {
  Defs d;
  ParseFile("m", "battle/a.json", R"({
    "moves": {
      "Flame Charge": { "effects": [ {"stat": "spe", "stages": 1, "target": "user"} ] },
      "Hex": { "power_rules": [ {"when": ["target_has_status"], "mult": 2} ] }
    },
    "abilities": {
      "Moxie": { "id": 153, "text": "Boosts Attack after a KO.", "ko": [ {"stat": "atk", "stages": 1} ] },
      "Sap Sipper": { "absorb": [ {"type": "grass", "stat": "atk", "stages": 1} ] },
      "Regenerator": { "switch_out": [ {"heal": 33} ] },
      "Prankster": { "priority": [ {"when": "status_move", "add": 1} ] },
      "Clear Mind": { "acts_as": "Clear Body" }
    },
    "rule_sets": { "cap50": { "level_cap": 50, "banned_items": ["Leftovers"], "inverse": true, "weather": "rain" } }
  })", Resolve, d);
  Finish(d);
  CHECK(d.problems.empty());
  CHECK(d.moves.size() == 2);
  CHECK(d.abilities.size() == 5);
  CHECK(d.abilities[4].actsAs == 29);
  CHECK(d.abilities[0].id == 153);
  CHECK(d.abilities[1].id == 124);
  CHECK(d.abilities[2].id == 125);
  CHECK(d.moves[0].effects.size() == 1 && d.moves[0].effects[0].stat == 3 && d.moves[0].effects[0].stages == 1);
  CHECK(d.moves[1].power.size() == 1 && d.moves[1].power[0].percent == 200 && d.moves[1].power[0].when[0].kind == Cond::TargetHasStatus);
  CHECK(d.abilities[2].switchOut[0].permille == 330);
  CHECK(d.abilities[3].priority[0].add == 1);
  CHECK(d.ruleSets.size() == 1 && d.ruleSets[0].levelCap == 50 && d.ruleSets[0].bannedItems[0] == 200 && d.ruleSets[0].inverse && d.ruleSets[0].weather == 2);
  const uint64_t a = Digest(d);
  CHECK(a != 0);
  Defs d2;
  ParseFile("m", "battle/a.json", R"({"moves": {"Hex": { "power_rules": [ {"when": "target_has_status", "mult": 2.5} ] }}})", Resolve, d2);
  Finish(d2);
  CHECK(Digest(d2) != a);


  Defs bad;
  ParseFile("m", "battle/b.json", R"({
    "moves": {
      "Nope": { "effects": [] },
      "Flame Charge": { "effects": [ {"stat": "speed", "stages": 9} ] },
      "Hex": { "effects": [ {"status": "burn", "chance": 30}, {"heal": 50, "stat": "atk", "stages": 1} ] }
    },
    "abilities": { "X": { "id": 12 }, "Y": { "id": 150 }, "Z": { "id": 150 } }
  })", Resolve, bad);
  Finish(bad);
  CHECK(bad.moves.size() == 0);
  CHECK(bad.abilities.size() == 1);
  CHECK(bad.problems.size() >= 5);

  Defs g;
  ParseFile("m", "battle/g.json", R"({
    "moves": {
      "Flame Charge": { "crit": "always", "ignore_boosts": true, "use_target_attack": true, "defend_stat": "def", "first_turn_only": true, "power_formula": "weight", "protect": "kings_shield",
                        "effects": [ {"terrain": "electric"}, {"side": "sticky_web", "target": "target"}, {"set_ability": 152, "target": "target"}, {"cure_status": true}, {"hurt_dealt": 50}, {"moody": true},
                                     {"stat": "best", "stages": 1}, {"force_switch": true}, {"switch_out": "user"} ] },
      "Hex": { "power_rules": [ {"when": ["target_status:poison", "terrain:grassy", "target_grounded", "hp_crossed_half", {"move_in": ["Bite", "Not A Move", "Crunch"]}], "mult": 2} ] }
    },
    "abilities": {
      "A": { "id": 160, "hit": [ {"stat": "def", "stages": 1} ], "hit_dealt": [ {"status": "poison", "chance": 30} ], "fainted": [ {"hurt_dealt": 100, "target": "target"} ],
             "stat_dropped": [ {"stat": "atk", "stages": 2} ], "contrary": true, "breaks_auras": true, "crit_when": ["target_status:poison"],
             "convert": [ {"from": "normal", "to": "ice", "mult": 1.2}, {"to": "water", "when": "sound"} ], "aura": {"type": "dark", "mult": 1.33},
             "immune_moves": ["Bite", "Unknown Move"], "accuracy_against": [ {"when": "status_move", "accuracy": 50} ], "accuracy_dealt": [ {"mult": 1.1} ] }
    }
  })", Resolve, g);
  Finish(g);
  CHECK(g.problems.empty());
  CHECK(g.moves.size() == 2);
  const MoveDef& m0 = g.moves[0];
  CHECK(m0.crit == 1 && m0.ignoreBoosts && m0.useTargetAttack && m0.defendWithDef && m0.firstTurnOnly && m0.powerFormula == 1 && m0.protectKind == 1);
  CHECK(m0.effects.size() == 9 && m0.effects[0].kind == BlockKind::Terrain && m0.effects[0].field == 2 && m0.effects[1].kind == BlockKind::Side && m0.effects[1].field == 2);
  CHECK(m0.effects[2].kind == BlockKind::SetAbility && m0.effects[2].ability == 152 && m0.effects[4].kind == BlockKind::HurtDealt && m0.effects[4].permille == 500);
  CHECK(m0.effects[6].kind == BlockKind::Stat && m0.effects[6].stat == 8);
  const auto& w = g.moves[1].power[0].when;
  CHECK(w.size() == 5 && w[0].kind == Cond::TargetStatusKind && w[0].arg == 3 && w[1].kind == Cond::Terrain && w[1].arg == 1 && w[4].kind == Cond::MoveInList && w[4].list.size() == 2);
  const AbilityDef& ab = g.abilities[0];
  CHECK(ab.hit.size() == 1 && ab.hitDealt.size() == 1 && ab.fainted.size() == 1 && ab.statDropped.size() == 1 && ab.contrary && ab.breaksAuras && ab.critWhen.size() == 1);
  CHECK(ab.convert.size() == 2 && ab.convert[0].from == 0 && ab.convert[0].to == 15 && ab.convert[0].percent == 120 && ab.convert[1].from == -1 && ab.convert[1].percent == 100);
  CHECK(ab.auraType == 17 && ab.auraPercent == 133 && ab.immuneMoves.size() == 1 && ab.accuracyAgainst.size() == 1 && ab.accuracyAgainst[0].accuracy == 50 && ab.accuracyDealt[0].percent == 110);
  const uint64_t ga = Digest(g);
  Defs g2 = g;
  g2.abilities[0].contrary = false;
  g2.abilities[0].immuneMoves.clear();
  CHECK(Digest(g2) != ga);
  Defs gb;
  ParseFile("m", "battle/gb.json", R"({"moves": {"Hex": {"crit": "sometimes"}, "Flame Charge": {"protect": "x"}}})", Resolve, gb);
  CHECK(gb.moves.empty() && gb.problems.size() == 2);
  Defs broken;
  ParseFile("m", "battle/c.json", "{ not json", Resolve, broken);
  CHECK(broken.problems.size() == 1);
  std::printf(sFail == 0 ? "ALL PASSED\n" : "%d FAILED\n", sFail);
  return sFail == 0 ? 0 : 1;
}
