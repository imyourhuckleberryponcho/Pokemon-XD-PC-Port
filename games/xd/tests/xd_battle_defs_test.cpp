
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

  Defs h;
  ParseFile("m", "battle/h.json", R"({
    "moves": { "Flame Charge": { "charge": { "donor": "Hex", "message": "{name} is charging!" }, "effects": [ {"status": "burn", "target": "target", "chance": 30} ] } },
    "abilities": {
      "Unnerve": { "id": 127, "unnerve": true },
      "Cursed Body": { "id": 130, "disable_on_hit": 30 },
      "Harvest": { "id": 139, "end_of_turn": [ {"restore_berry": true, "chance": 50, "when": "!weather:sun"} ] },
      "Cheek Pouch": { "id": 167, "berry_eaten": [ {"heal": 33.3} ] },
      "Imposter": { "id": 150, "switch_in": [ {"transform": true, "message": "{name} transformed into {foe}!"} ] },
      "Wimp Out": { "id": 193, "half_hp": [ {"switch_out": "user"} ] },
      "Corrosion": { "id": 212, "poison_any_type": true },
      "Infiltrator": { "id": 151, "infiltrator": true },
      "Delta Stream": { "id": 191, "strong_wind": true },
      "RKS System": { "id": 225, "type_by_item": [ {"item": "Leftovers", "type": "fire"}, {"item": "No Such Memory", "type": "water"} ] }
    },
    "hidden_abilities": { "555": 161, "Hex": "Clear Body", "Bite": 999 }
  })", Resolve, h);
  Finish(h);
  CHECK(h.moves.size() == 1 && h.moves[0].chargeDonor == 381 && h.moves[0].chargeMessage == "{name} is charging!" && h.moves[0].effects.size() == 1);
  CHECK(h.abilities.size() == 10);
  CHECK(h.abilities[0].unnerve && h.abilities[1].disableOnHit == 30 && h.abilities[6].poisonAnyType && h.abilities[7].infiltrator && h.abilities[8].strongWind);
  CHECK(h.abilities[2].endOfTurn.size() == 1 && h.abilities[2].endOfTurn[0].kind == BlockKind::RestoreBerry && h.abilities[2].endOfTurn[0].chance == 50);
  CHECK(h.abilities[3].berryEaten.size() == 1 && h.abilities[3].berryEaten[0].permille == 333);
  CHECK(h.abilities[4].switchIn[0].kind == BlockKind::Transform && !h.abilities[4].switchIn[0].onUser);
  CHECK(h.abilities[5].halfHp.size() == 1 && h.abilities[5].halfHp[0].kind == BlockKind::SwitchOut);
  CHECK(h.abilities[9].typeByItem.size() == 1 && h.abilities[9].typeByItem[0].item == 200 && h.abilities[9].typeByItem[0].type == 10);
  CHECK(h.hiddenNat.size() == 1 && h.hiddenNat.at(555) == 161 && h.hiddenRow.size() == 0);
  CHECK(h.problems.size() >= 1);
  const uint64_t hd = Digest(h);
  Defs h2 = h;
  h2.abilities[0].unnerve = false;
  CHECK(Digest(h2) != hd);
  Defs h3 = h;
  h3.moves[0].chargeMessage = "other";
  CHECK(Digest(h3) != hd);
  Defs h4 = h;
  h4.hiddenNat[555] = 1;
  CHECK(Digest(h4) != hd);

  Defs r3;
  ParseFile("m", "battle/r3.json", R"({
    "moves": { "Hex": { "needs_physical_hit": true }, "Bite": { "heats_up": true }, "Flame Charge": { "type_from_user": true } },
    "abilities": { "Dancer": { "id": 216, "dancer": ["Hex", "Bite", "No Such Move"] } }
  })", Resolve, r3);
  Finish(r3);
  CHECK(r3.moves.size() == 3 && r3.abilities.size() == 1);
  CHECK(r3.abilities[0].dancer.size() == 2);
  bool shell = false, beak = false, user = false;
  for (const auto& m : r3.moves) { shell |= m.needsPhysicalHit; beak |= m.heatsUp; user |= m.typeFromUser; }
  CHECK(shell && beak && user);
  const uint64_t r3d = Digest(r3);
  Defs r3b = r3;
  r3b.abilities[0].dancer.pop_back();
  CHECK(Digest(r3b) != r3d);
  Defs r3c = r3;
  r3c.moves[0].heatsUp = !r3c.moves[0].heatsUp;
  CHECK(Digest(r3c) != r3d);
  Defs none;
  ParseFile("m", "battle/n.json", R"({"abilities": { "Moxie": { "id": 153, "ko": [ {"stat": "atk", "stages": 1} ] } }})", Resolve, none);
  Finish(none);
  Defs none2 = none;
  none2.abilities[0].unnerve = false;
  CHECK(Digest(none) == Digest(none2));
  Defs broken;
  ParseFile("m", "battle/c.json", "{ not json", Resolve, broken);
  CHECK(broken.problems.size() == 1);
  std::printf(sFail == 0 ? "ALL PASSED\n" : "%d FAILED\n", sFail);
  return sFail == 0 ? 0 : 1;
}
