#include <xd_legality.h>

#include <algorithm>

namespace xd::legality {

namespace {

using xd::save::Pokemon;

struct Collector {
  const Rules& rules;
  Level level;
  Result result;

  void Add(Level at, const char* code, std::string text) {
    if (at == Level::Legal && level != Level::Legal) return;
    result.findings.push_back({at, code, std::move(text)});
  }
};

std::string SpeciesText(const Rules& r, uint16_t id) {
  std::string n = r.speciesName ? r.speciesName(id) : std::string();
  return n.empty() ? "Pokemon #" + std::to_string(id) : n;
}
std::string MoveText(const Rules& r, uint16_t id) {
  std::string n = r.moveName ? r.moveName(id) : std::string();
  return n.empty() ? "move #" + std::to_string(id) : n;
}
std::string ItemText(const Rules& r, uint16_t id) {
  std::string n = r.itemName ? r.itemName(id) : std::string();
  return n.empty() ? "item #" + std::to_string(id) : n;
}

bool Terminated(const uint8_t* field, size_t chars) {
  for (size_t i = 0; i < chars; i++) {
    if (field[i * 2] == 0 && field[i * 2 + 1] == 0) return true;
  }
  return false;
}

std::vector<const SpeciesRules*> Line(const Rules& r, uint16_t id) {
  std::vector<const SpeciesRules*> line;
  for (int step = 0; step < 8 && id != 0 && id < r.species.size(); step++) {
    line.push_back(&r.species[id]);
    id = r.species[id].evolvesFrom;
  }
  return line;
}

bool Learnable(const Rules& r, const Pokemon& p, uint16_t move) {
  const auto line = Line(r, p.species);
  {
    uint16_t id = p.species;
    for (int step = 0; step < 8 && id != 0 && id < r.species.size(); step++) {
      for (const auto& g : r.giftMoves) {
        if (g.first == id && g.second == move) return true;
      }
      id = r.species[id].evolvesFrom;
    }
  }
  for (const SpeciesRules* s : line) {
    for (const LevelUpMove& l : s->levelUp) {
      if (l.move == move && l.level <= p.level) return true;
    }
    for (size_t n = 0; n < s->machine.size(); n++) {
      if (s->machine[n] != 0 && r.machineMove[n] == move) return true;
    }
    for (size_t n = 0; n < s->tutor.size(); n++) {
      if (s->tutor[n] != 0 && r.tutorMove[n] == move) return true;
    }
    for (uint16_t m : s->eggMoves) {
      if (m == move) return true;
    }
  }
  return false;
}

int NatureFactor(uint8_t nature, int statIndexInOurOrder) {
  static const int kOrder[5] = {1, 2, 5, 3, 4};
  const int inc = nature / 5, dec = nature % 5;
  if (inc == dec) return 100;
  if (kOrder[inc] == statIndexInOurOrder) return 110;
  if (kOrder[dec] == statIndexInOurOrder) return 90;
  return 100;
}

}

std::string Result::Text(const char* separator) const {
  std::string out;
  for (const Finding& f : findings) {
    if (!out.empty()) out += separator;
    out += f.text;
  }
  return out;
}

bool ExpForLevel(const Rules& rules, uint8_t growth, int level, uint32_t& exp) {
  if (growth >= rules.expCurves.size() || level < 0 || level > 100) return false;
  exp = rules.expCurves[growth][static_cast<size_t>(level)];
  return true;
}

bool ExpectedStats(const Rules& rules, const Pokemon& p, std::array<uint16_t, 6>& stats) {
  if (p.species >= rules.species.size() || !rules.species[p.species].exists) return false;
  const SpeciesRules& s = rules.species[p.species];
  const uint8_t nature = p.Nature();
  for (int i = 0; i < 6; i++) {
    const uint32_t core = (2u * s.baseStats[i] + p.ivs[i] + p.evs[i] / 4u) * p.level / 100u;
    if (i == 0) {
      stats[0] = static_cast<uint16_t>(s.baseStats[0] == 1 ? 1 : core + p.level + 10);
    } else {
      stats[static_cast<size_t>(i)] = static_cast<uint16_t>((core + 5) * static_cast<uint32_t>(NatureFactor(nature, i)) / 100u);
    }
  }
  return true;
}

Result Check(const Pokemon& p, const Rules& rules, Level level, const Context& ctx) {
  Collector c{rules, level, {}};
  if (p.IsEmpty()) return c.result;
  const std::string who = SpeciesText(rules, p.species);

  const bool speciesInRange = p.species < rules.species.size() && rules.species[p.species].exists;
  if (!speciesInRange) {
    if (p.species >= rules.vanillaSpecies && p.species < rules.vanillaSpecies + rules.gen4Species && !rules.Gen4On()) {
      c.Add(Level::Safe, "species.gen4", "Pokemon #" + std::to_string(p.species) +
                                              " is a Generation 4 Pokemon, and this game has the Gen 4 option off.");
    } else {
      c.Add(Level::Safe, "species.range", "Pokemon #" + std::to_string(p.species) + " is not in this game's list of Pokemon.");
    }
    return c.result;
  }
  const SpeciesRules& sp = rules.species[p.species];
  if (p.level < 1 || p.level > 100) {
    c.Add(Level::Safe, "level.range", who + " is level " + std::to_string(p.level) + ", but levels go from 1 to 100.");
  }
  if (p.flags & 0x20) {
    c.Add(Level::Safe, "flags.invalid", who + " is marked as invalid (a \"bad egg\"), the game refuses it.");
  }
  for (int i = 0; i < 4; i++) {
    const uint16_t m = p.moves[static_cast<size_t>(i)].id;
    if (m == 0) continue;
    if (m >= rules.moves.size() || !rules.moves[m].exists) {
      if (m >= rules.vanillaMoves && m < rules.vanillaMoves + rules.gen4Moves && !rules.Gen4On()) {
        c.Add(Level::Safe, "moves.gen4", who + " knows a Generation 4 move (#" + std::to_string(m) +
                                              "), and this game has the Gen 4 option off.");
      } else {
        c.Add(Level::Safe, "moves.range", who + " knows move #" + std::to_string(m) + ", which this game doesn't have.");
      }
    }
    if (p.moves[static_cast<size_t>(i)].ppUps > 3) {
      c.Add(Level::Safe, "moves.ppups", who + "'s " + MoveText(rules, m) + " has " +
                                             std::to_string(p.moves[static_cast<size_t>(i)].ppUps) + " PP Ups (3 is the most).");
    }
  }
  if (p.heldItem != 0 && (p.heldItem >= rules.items.size() || !rules.items[p.heldItem].exists)) {
    if (p.heldItem >= rules.vanillaItems && p.heldItem < rules.vanillaItems + rules.gen4Items && !rules.Gen4On()) {
      c.Add(Level::Safe, "item.gen4", who + " holds a Generation 4 item (#" + std::to_string(p.heldItem) +
                                           "), and this game has the Gen 4 option off.");
    } else {
      c.Add(Level::Safe, "item.range", who + " holds item #" + std::to_string(p.heldItem) + ", which this game doesn't have.");
    }
  }
  {
    const uint16_t base = sp.formOf != 0 ? sp.formOf : p.species;
    uint16_t want = 0;
    bool plates = false;
    for (size_t i = 0; i < rules.species.size(); i++) {
      const SpeciesRules& f = rules.species[i];
      if (!f.exists || f.formKind != 6 || f.plateItem == 0 || f.formOf != base) continue;
      plates = true;
      if (f.plateItem == p.heldItem) want = static_cast<uint16_t>(i);
    }
    if (plates && p.species != (want != 0 ? want : base) && !p.IsEgg())
      c.Add(Level::Legal, "form.plate", who + (sp.formKind == 6 ? " is in a Plate form without holding that Plate."
                                                               : " holds a Plate but is not in its form."));
  }
  for (int i = 0; i < 6; i++) {
    if (p.ivs[static_cast<size_t>(i)] > 31) {
      c.Add(Level::Safe, "iv.range", who + " has an IV of " + std::to_string(p.ivs[static_cast<size_t>(i)]) + " (31 is the most).");
      break;
    }
  }
  for (int i = 0; i < 6; i++) {
    if (p.evs[static_cast<size_t>(i)] > 255) {
      c.Add(Level::Safe, "ev.range", who + " has an EV of " + std::to_string(p.evs[static_cast<size_t>(i)]) + " (255 is the most).");
      break;
    }
  }
  if (p.friendship > 255) {
    c.Add(Level::Safe, "friendship.range", who + " has friendship " + std::to_string(p.friendship) + " (255 is the most).");
  }
  if (p.hp > p.stats[0] || p.stats[0] == 0) {
    c.Add(Level::Safe, "hp.range", who + "'s HP (" + std::to_string(p.hp) + ") is more than its maximum HP (" +
                                        std::to_string(p.stats[0]) + ").");
  }
  if (p.otName.size() > 10 || p.nickname.size() > 10 || !Terminated(p.raw.data() + 0x38, 11) ||
      !Terminated(p.raw.data() + 0x4E, 11)) {
    c.Add(Level::Safe, "name.length", who + "'s name or its trainer's name is too long (10 letters at most).");
  }
  {
    uint32_t top = 0;
    if (sp.growth >= rules.expCurves.size()) {
      c.Add(Level::Safe, "exp.curve", who + " uses an experience curve (" + std::to_string(sp.growth) + ") this game doesn't have.");
    } else if (ExpForLevel(rules, sp.growth, 100, top) && p.exp > top) {
      c.Add(Level::Safe, "exp.range", who + " has " + std::to_string(p.exp) + " experience points: more than level 100 needs.");
    }
  }
  if (p.shadowId != 0) {
    const bool purified = ctx.shadow != nullptr && ctx.shadow->Purified();
    if (!purified && !ctx.shadowStaysInItsSave) {
      c.Add(Level::Safe, "shadow.open",
            who + " is (or was marked as) a Shadow Pokemon that is not purified: it can't be taken out of its own save, "
                  "its Shadow record stays behind.");
    }
  }

  if (p.IsEgg() && !rules.eggsExist) {
    c.Add(Level::Legal, "egg", "An egg: " + rules.gameName + " has no eggs, so this one comes from another game.");
  }
  uint32_t lo = 0, hi = 0;
  if (p.level >= 1 && p.level <= 100 && ExpForLevel(rules, sp.growth, p.level, lo)) {
    const bool haveHi = p.level < 100 && ExpForLevel(rules, sp.growth, p.level + 1, hi);
    if (p.exp < lo || (haveHi && p.exp >= hi)) {
      c.Add(Level::Legal, "exp.level",
            who + " is level " + std::to_string(p.level) + " with " + std::to_string(p.exp) +
                " experience points, but that level means " + std::to_string(lo) +
                (haveHi ? " to " + std::to_string(hi - 1) : " or more") + ".");
    }
  }
  if (p.metLevel > p.level) {
    c.Add(Level::Legal, "met.level", who + " was met at level " + std::to_string(p.metLevel) +
                                          " but is level " + std::to_string(p.level) + " now: levels don't go down.");
  }
  if (p.SecondAbility() && (sp.abilities[1] == 0 || sp.abilities[1] == sp.abilities[0])) {
    c.Add(Level::Legal, "ability.slot", who + " has its second ability, but " + SpeciesText(rules, p.species) +
                                             " only has one.");
  }
  {
    uint32_t total = 0;
    for (uint16_t e : p.evs) total += e;
    if (total > rules.maxEvTotal) {
      c.Add(Level::Legal, "ev.total", who + " has " + std::to_string(total) + " EVs in all (" +
                                           std::to_string(rules.maxEvTotal) + " is the most).");
    }
  }
  if (p.shadowId != 0 && p.IsShiny()) {
    c.Add(Level::Legal, "shiny.shadow", who + " is a shiny Shadow Pokemon: Shadow Pokemon can't be shiny.");
  }
  if (p.shadowId == 0) {
    bool gap = false;
    for (int i = 0; i < 4; i++) {
      const uint16_t m = p.moves[static_cast<size_t>(i)].id;
      if (m == 0) {
        gap = true;
        continue;
      }
      if (gap) {
        c.Add(Level::Legal, "moves.gap", who + " has an empty move slot before a move.");
        gap = false;
      }
      if (m >= rules.moves.size() || !rules.moves[m].exists) continue;
      for (int j = 0; j < i; j++) {
        if (p.moves[static_cast<size_t>(j)].id == m) {
          c.Add(Level::Legal, "moves.twice", who + " knows " + MoveText(rules, m) + " twice.");
        }
      }
      if (rules.Gen4On() && m >= rules.vanillaMoves) {
        continue;
      }
      if (rules.moves[m].shadow) {
        c.Add(Level::Legal, "moves.shadow", who + " knows the Shadow move " + MoveText(rules, m) + " without ever being a Shadow Pokemon.");
      } else if (!ctx.movesNotJudged && !Learnable(rules, p, m)) {
        c.Add(Level::Legal, "moves.unlearnable",
              who + " knows " + MoveText(rules, m) + ", which it can't learn by level-up, TM, HM, tutor or egg at level " +
                  std::to_string(p.level) + ".");
      }
    }
  }
  if (p.level >= 1 && p.level <= 100) {
    std::array<uint16_t, 6> want{};
    if (ExpectedStats(rules, p, want)) {
      static const char* const kStat[6] = {"HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed"};
      for (size_t i = 0; i < 6; i++) {
        if (want[i] != p.stats[i]) {
          c.Add(Level::Legal, "stats.formula",
                who + "'s " + kStat[i] + " is " + std::to_string(p.stats[i]) + ", but its level, IVs, EVs and nature make " +
                    std::to_string(want[i]) + ".");
          break;
        }
      }
    }
  }
  return c.result;
}

}
