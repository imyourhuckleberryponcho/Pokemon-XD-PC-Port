#include "xd_gen_rules.h"

#include "gen_numbers.h"
#include "xd_challenges.h"
#include "xd_xg.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

extern "C" {
uint8_t fightOutPokemonIsJoutaiDataId(void* fop, uint16_t id);
uint16_t fightOutPokemonGetTokuseiDataId(void* fop);
uint16_t fightOutPokemonGetZokuseiDataId(void* fop, uint8_t index);
uint32_t fightFloorGetNowTenkouDataId(void* floor, uint8_t now);
uint8_t fightWaza_GetCritical(void* waza);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
void* fightWazaCriticalDataBiosGetPtr(uint16_t index);
uint32_t PortXD_SplitCheckDarkWaza(uint16_t waza);
int32_t PortXD_SplitTypeKind(uint16_t type);
uint16_t fightOutPokemonMaxHpWaruValue(void* fop, uint16_t divisor);
}

namespace {
int sNumbers = 3;
int sLogged = 0;
constexpr uint16_t kQuickFeet = 95;

int32_t Scale(int32_t damage, int32_t num, int32_t den) { return (damage - 2) * num / den + 2; }

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool PackHas(const std::vector<engine::mods::DataModFile>& files, const std::string& name) {
  const std::string key = "\"" + Lower(name) + "\"";
  for (const engine::mods::DataModFile& f : files) {
    if (Lower(f.text).find(key) != std::string::npos) return true;
  }
  return false;
}
void Note(const char* what, int a, int b) {
  if (sLogged++ < 40) std::fprintf(stderr, "[rules] gen %d: %s (%d -> %d)\n", xd::rulegen::Gen(), what, a, b);
}
}

namespace xd::rulegen {

int GenOfIds(const std::vector<std::string>& ids) {
  const auto has = [&](const char* id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
  return has("gen7_rules") ? 7 : has("gen6_rules") ? 6 : has("gen5_rules") ? 5 : has("gen4_rules") ? 4 : 3;
}

int NumbersOfIds(const std::vector<std::string>& ids) {
  return std::find(ids.begin(), ids.end(), "gen_numbers") != ids.end() ? GenOfIds(ids) : 3;
}

int Gen() {
  if (xd::xg::On()) return 3;
  for (int g = 7; g >= 5; g--) {
    if (xd::challenges::Active((std::string("gen") + std::to_string(g) + "_rules").c_str())) return g;
  }
  return xd::challenges::Active("gen4_rules") ? 4 : 3;
}

int NumbersLoaded() { return sNumbers; }

void PrepareNumbers(int gen, std::vector<engine::mods::DataModFile>& files) {
  sNumbers = 3;
  const gennum::GenTable* t = gennum::Table(gen);
  if (t == nullptr) return;
  const std::vector<engine::mods::DataModFile> packs = files;
  static const char* const kStat[6] = {"maxHp", "phyAtk", "phyDef", "speAtk", "speDef", "nimbleness"};
  std::string pokemon, moves;
  int ns = 0, nm = 0;
  for (size_t i = 0; i < t->nSpecies; i++) {
    const gennum::SpeciesDiff& d = t->species[i];
    const std::string key = d.gameId != 0 ? "#" + std::to_string(d.gameId) : std::string(d.name);
    if (d.gameId == 0 && !PackHas(packs, d.name)) continue;
    std::string body;
    for (int k = 0; k < 6; k++) {
      if (d.stat[k] != 0) body += std::string(body.empty() ? "" : ", ") + "\"basisStatus[0]." + kStat[k] + "\": " + std::to_string(d.stat[k]);
    }
    if (d.type[0] != 255 || d.type[1] != 255) {
      const auto one = [](uint8_t v) { return v == 255 ? std::string("null") : std::to_string(v); };
      body += std::string(body.empty() ? "" : ", ") + "\"zokuseiDataId\": [" + one(d.type[0]) + ", " + one(d.type[1]) + "]";
    }
    if (body.empty()) continue;
    pokemon += std::string(pokemon.empty() ? "" : ",\n") + "  \"" + key + "\": {" + body + "}";
    ns++;
  }
  for (size_t i = 0; i < t->nMoves; i++) {
    const gennum::MoveDiff& d = t->moves[i];
    const bool own = d.id <= 354;
    if (!own && !PackHas(packs, d.name)) continue;
    std::string body;
    const auto add = [&](const char* f, int v) { body += std::string(body.empty() ? "" : ", ") + "\"" + f + "\": " + std::to_string(v); };
    if (d.power != 65535) add("iryoku", d.power);
    if (d.pp != 0) add("pp", d.pp);
    if (d.accuracy != 255) add("avg", d.accuracy);
    if (d.type != 255) add("zokuseiDataId", d.type);
    if (body.empty()) continue;
    moves += std::string(moves.empty() ? "" : ",\n") + "  \"" + (own ? "#" + std::to_string(d.id) : std::string(d.name)) + "\": {" + body + "}";
    nm++;
  }
  engine::mods::DataModFile f;
  f.mod = "gen_numbers";
  f.builtIn = true;
  f.modName = "Generation numbers";
  f.label = "gen_numbers/gen" + std::to_string(gen) + ".json";
  f.text = "{\n \"_note\": \"made by the port (games/xd/src/gen_rules.cpp): the numbers of generation " + std::to_string(gen) +
           " from public data (tools/gen_numbers.py)\",\n \"pokemon\": {\n" + pokemon + "\n },\n \"moves\": {\n" + moves + "\n }\n}\n";
  files.push_back(std::move(f));
  sNumbers = gen;
  std::fprintf(stderr, "[rules] gen %d numbers: %d species rows and %d move rows changed from the game's own\n", gen, ns, nm);
}

uint32_t ParalysisSpeed(void* fop, uint32_t speed) {
  if (fop == nullptr || Gen() < 7 || fightOutPokemonIsJoutaiDataId(fop, 5) != 1) return speed;
  if (fightOutPokemonGetTokuseiDataId(fop) == kQuickFeet) return speed;
  Note("paralysed Speed halved, not quartered", static_cast<int>(speed), static_cast<int>(speed * 2));
  return speed * 2;
}

int32_t DamageRules(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage) {
  const int g = Gen();
  if (g < 5 || attacker == nullptr || defender == nullptr || damage <= 2) return damage;
  if (g >= 6 && fightWaza_GetCritical(fightOutPokemon_GetFightWazaPtr(attacker)) == 2) {
    const int32_t was = damage;
    damage = Scale(damage, 3, 4);
    Note("critical hit x1.5 instead of x2", was, damage);
  }
  if (fightFloorGetNowTenkouDataId(nullptr, 1) == 3 &&
      (fightOutPokemonGetZokuseiDataId(defender, 0) == 5 || fightOutPokemonGetZokuseiDataId(defender, 1) == 5)) {
    PortXD_SplitCheckDarkWaza(waza);
    if (PortXD_SplitTypeKind(type) == 2) {
      const int32_t was = damage;
      damage = Scale(damage, 2, 3);
      Note("sandstorm: Rock Pokemon's Sp. Def x1.5", was, damage);
    }
  }
  return damage;
}

void SetCritTable() {
  static const uint8_t k3[5] = {16, 8, 4, 3, 2}, k6[5] = {16, 8, 2, 1, 1}, k7[5] = {24, 8, 2, 1, 1};
  static int last = -1;
  const int g = Gen();
  if (g == last) return;
  last = g;
  const uint8_t* t = g >= 7 ? k7 : g == 6 ? k6 : k3;
  for (uint16_t i = 0; i < 5; i++) {
    if (auto* p = static_cast<uint8_t*>(fightWazaCriticalDataBiosGetPtr(i))) *p = t[i];
  }
  if (g >= 6) std::fprintf(stderr, "[rules] gen %d: critical hit table 1 in %u / %u / %u / %u / %u\n", g, t[0], t[1], t[2], t[3], t[4]);
}

int32_t BurnDamage(void* fop, uint16_t divisor) {
  if (fop == nullptr || Gen() < 7) return -1;
  const int32_t d = fightOutPokemonMaxHpWaruValue(fop, divisor);
  return d < 1 ? 1 : d;
}

}

namespace xd::gennum {
#include "data/gen_numbers_data.inc"
const GenTable* Table(int gen) {
  for (const GenTable& t : kTables) {
    if (t.gen == gen) return &t;
  }
  return nullptr;
}
}
