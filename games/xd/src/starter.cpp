#include "xd_starter.h"

#include "xd_playthrough.h"
#include "xd_pokedex.h"
#include "xd_test_run.h"
#include "xd_xg.h"

#include <dolphin/types.h>
#include <game/pxdvs/app/hero/hero.hpp>
#include <game/pxdvs/app/memcard/savedata.hpp>
#include <game/pxdvs/app/pokemon/pokemonBios.hpp>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/menuTables.h>
#include <game/relglobal.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

extern "C" {
void savedataCreate(void* save, uint16_t* name);
uint8_t heroBiosGetSexDataId(void* hero);
uint32_t heroBiosGetRnd(void* hero);
uint16_t* heroBiosGetNamePtr(void* hero);
char16_t* GSmsgGetGSchar(uint32_t key);
void PortXD_CheatsNoteUsed(const char* name);
const char* PortInput_ReplayPlaythroughNote(const char* prefix);
}

namespace {

namespace xr = xd::randomizer;

xr::StarterPlan sPlan;
bool sSwappedLog = false;

constexpr uint16_t kFriendLevel = 0xD7;
constexpr uint32_t kEeveeExpIntoLevel = 250, kEeveeExpLevelSpan = 331;

uint32_t StartExp(uint16_t species, uint32_t level, bool intoLevel) {
  PokemonGrowDataBios* grow = pokemonGrowDataBiosGetPtr(static_cast<uint8_t>(pokemonDB_GetGrowDataId(species)));
  if (grow == nullptr) return 0;
  const uint32_t from = static_cast<uint32_t>(pokemonGrowDataBiosGetExp(grow, static_cast<uint8_t>(level)));
  const uint32_t to = static_cast<uint32_t>(pokemonGrowDataBiosGetExp(grow, static_cast<uint8_t>(level + 1)));
  if (to <= from || !intoLevel) return from;
  return from + (to - from) * kEeveeExpIntoLevel / kEeveeExpLevelSpan;
}

void SwapSlot(int slot, uint32_t species, uint32_t plannedLevel, const uint32_t* moves) {
  Hero* hero = static_cast<Hero*>(savedataGetStatus(nullptr, 2));
  if (hero == nullptr) return;
  Pokemon* orig = hero->getPokemon(slot);
  const bool xg = xd::xg::On() && orig != nullptr && orig->checkValid();
  HeroPokemonGetParam param;
  std::memset(&param, 0, sizeof param);
  param.pokemonDataID = static_cast<u16>(species);
  param.shadowPokemonDataID = 0;
  uint32_t level = plannedLevel;
  if (xg) {
    level = pokemonBiosGetLevel(orig);
    if (level != plannedLevel) std::fprintf(stderr, "[starter] XG slot %d: the game's own is level %u (the plan said %u): level %u is used\n", slot + 1, level, plannedLevel, level);
    param.friendLevel = pokemonBiosGetFriend(orig);
    param.catchFloorID = pokemonBiosGetCatchFloorId(orig);
  } else {
    param.friendLevel = kFriendLevel;
    param.catchFloorID = 0;
  }
  param.catchLevel = static_cast<u8>(level);
  for (int m = 0; m < 4; m++) param.moves[m] = static_cast<u16>(moves[m]);
  param.trainerSexID = heroBiosGetSexDataId(hero);
  param.trainerRnd = heroBiosGetRnd(hero);
  param.trainerNamePtr = reinterpret_cast<char16*>(heroBiosGetNamePtr(hero));
  param.exp = xg ? StartExp(static_cast<uint16_t>(species), level, false) : StartExp(static_cast<uint16_t>(species), level, true);
  if (orig != nullptr && orig->checkValid()) hero->deletePokemon(slot);
  const int got = hero->pokemonGet(&param, true);
  std::fprintf(stderr, "[starter] party slot %d: species %u, level %u%s%s\n", got + 1, species, level, sSwappedLog ? " (chosen)" : "", xg ? " (XG: level, friendship and place as the game's own)" : "");
  std::fprintf(stderr, "[starter] party slot %d: experience %u, moves %u %u %u %u\n", got + 1, static_cast<unsigned>(param.exp), static_cast<unsigned>(param.moves[0]), static_cast<unsigned>(param.moves[1]),
               static_cast<unsigned>(param.moves[2]), static_cast<unsigned>(param.moves[3]));
}

void Swap(uint32_t k) {
  if (k >= sPlan.count) return;
  const uint32_t species = sPlan.species[k];
  if (species == xr::kStarterEevee && !sPlan.natural) {
    std::fprintf(stderr, "[starter] the starter stays Eevee (the plan's species and moves are the game's)\n");
    return;
  }
  SwapSlot(0, species, sPlan.level, sPlan.moves[k]);
}

void SwapSecond() {
  if (!sPlan.second || !xd::xg::On()) return;
  if (sPlan.species2 == sPlan.base.species[1] && !sPlan.natural) {
    std::fprintf(stderr, "[starter] the second starter stays as the game has it\n");
    return;
  }
  SwapSlot(1, sPlan.species2, sPlan.level2, sPlan.moves2);
}

std::vector<xd::starter::Candidate> sCandidates;
bool sCandidatesBuilt = false;

void NaturalMovesOf(uint16_t species, uint32_t level, uint32_t out[4]) {
  std::vector<uint32_t> moves, first;
  const PokemonData* d = pokemon_data != nullptr && pokemon_data_number != nullptr && species < *pokemon_data_number ? &pokemon_data[species] : nullptr;
  if (d != nullptr) {
    for (const PokemonDataWaza& w : d->waza) {
      if (w.dataId == 0) break;
      if (first.size() < 4 && std::find(first.begin(), first.end(), static_cast<uint32_t>(w.dataId)) == first.end()) first.push_back(w.dataId);
      if (w.level > level) continue;
      moves.erase(std::remove(moves.begin(), moves.end(), static_cast<uint32_t>(w.dataId)), moves.end());
      moves.push_back(w.dataId);
    }
  }
  if (moves.size() > 4) moves.erase(moves.begin(), moves.end() - 4);
  if (moves.empty()) moves = first;
  if (moves.empty()) moves.push_back(0x21);
  for (int m = 0; m < 4; m++) out[m] = m < static_cast<int>(moves.size()) ? moves[static_cast<size_t>(m)] : 0;
}

const xd::starter::Candidate* CandidateOf(uint16_t index) {
  for (const xd::starter::Candidate& c : xd::starter::Candidates()) {
    if (c.index == index) return &c;
  }
  return nullptr;
}

void GiveChosen(int slot, uint16_t index) {
  const uint32_t level = slot == 0 ? 10 : xd::starter::kSecondLevel;
  if (slot == 0 && index == xr::kStarterEevee) {
    std::fprintf(stderr, "[starter] party slot 1: the player chose Eevee: the game's own stays\n");
    return;
  }
  uint32_t moves[4];
  NaturalMovesOf(index, level, moves);
  SwapSlot(slot, index, level, moves);
}

std::string DecidedLine(const char* key, const xd::starter::Candidate& c) {
  return std::string(key) + "=" + (c.national != 0 ? std::to_string(c.national) : "r" + std::to_string(c.index)) + ":" + c.name;
}
bool Has(const std::vector<std::string>& options, const char* line) { return std::find(options.begin(), options.end(), line) != options.end(); }
bool Decided(const std::vector<std::string>& options, const char* key) {
  const std::string k = std::string(key) + "=";
  for (const std::string& o : options) {
    if (o.rfind(k, 0) == 0) return true;
  }
  return false;
}

int sTestAsk = 0;
std::vector<int> TestNumbers() {
  static std::vector<int> v;
  static bool done = false;
  if (done) return v;
  done = true;
  const char* env = std::getenv("XD_STARTER_CHOOSE");
  const char* orig = std::getenv("XD_REQUIRE_ORIGINAL");
  if (env == nullptr || env[0] == 0 || std::strcmp(env, "0") == 0 || (orig != nullptr && std::strcmp(orig, "1") == 0)) return v;
  if (std::strcmp(env, "page") == 0 || std::strcmp(env, "page2") == 0) {
    sTestAsk = std::strcmp(env, "page2") == 0 ? 2 : 1;
    return v;
  }
  for (const char* p = env; *p != 0;) {
    char* end = nullptr;
    const long n = std::strtol(p, &end, 10);
    if (end == p) break;
    if (n > 0 && v.size() < 2) v.push_back(static_cast<int>(n));
    p = *end == ',' ? end + 1 : end;
  }
  return v;
}

bool PickByScript() { return std::getenv("XD_MENU_SCRIPT") != nullptr && std::getenv("XD_MENU_SCRIPT")[0] != 0; }

}

namespace xd::starter {

void SetPlan(const xr::StarterPlan& plan) { sPlan = plan; }
const xr::StarterPlan& Plan() { return sPlan; }
bool Randomized() { return sPlan.on; }

std::string SpeciesName(uint32_t species) {
  auto* data = pokemonDataBiosGetPtr(static_cast<uint16_t>(species));
  const char16_t* name = data != nullptr ? GSmsgGetGSchar(pokemonDataBiosGetName(data)) : nullptr;
  std::string out;
  for (size_t i = 0; name != nullptr && name[i] != 0 && i < 24; i++) {
    const char16_t c = name[i];
    out += c < 0x80 ? static_cast<char>(c) : '?';
  }
  return out.empty() ? "#" + std::to_string(species) : out;
}

bool IsLegendary(int n) {
  static const std::set<int> kLegends = {144, 145, 146, 150, 151, 243, 244, 245, 249, 250, 251, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386,
                                         480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 638, 639, 640, 641, 642, 643,
                                         644, 645, 646, 647, 648, 649, 716, 717, 718, 719, 720, 721, 785, 786, 787, 788, 789, 790, 791, 792, 793,
                                         794, 795, 796, 797, 798, 799, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809};
  return kLegends.count(n) != 0;
}

void InvalidateCandidates() { sCandidatesBuilt = false; }

static uint8_t GenOfNational(int national, uint8_t listed) {
  if (national <= 0) return listed;
  if (national <= 151) return 1;
  if (national <= 251) return 2;
  if (national <= 386) return 3;
  if (national <= 493) return 4;
  if (national <= 649) return 5;
  if (national <= 721) return 6;
  if (national <= 809) return 7;
  return listed;
}

const std::vector<Candidate>& Candidates() {
  if (sCandidatesBuilt) return sCandidates;
  sCandidates.clear();
  sCandidatesBuilt = true;
  std::set<uint16_t> seen;
  for (const engine::pokedex::Species& s : xd::pokedex::All()) {
    if (s.formOf != 0) continue;
    uint32_t total = 0;
    for (uint16_t b : s.base) total += b;
    if (total == 0) continue;
    if (s.national == 0 && !s.mod) continue;
    if (s.national != 0 && !seen.insert(s.national).second) continue;
    if (IsLegendary(s.national) && !kLegendaryAllowed) continue;
    Candidate c;
    c.index = s.index, c.national = s.national, c.gen = GenOfNational(s.national, s.gen), c.legendary = IsLegendary(s.national), c.name = s.name;
    for (int k = 0; k < 6; k++) c.base[k] = s.base[k];
    c.types[0] = s.types[0], c.types[1] = s.types[1], c.typeNames[0] = s.typeNames[0], c.typeNames[1] = s.typeNames[1];
    sCandidates.push_back(std::move(c));
  }
  std::stable_sort(sCandidates.begin(), sCandidates.end(), [](const Candidate& a, const Candidate& b) {
    if ((a.national == 0) != (b.national == 0)) return a.national != 0;
    return a.national != b.national ? a.national < b.national : a.index < b.index;
  });
  std::fprintf(stderr, "[starter] %zu species can be chosen as a starter\n", sCandidates.size());
  return sCandidates;
}

std::vector<std::string> ChoiceOptions(bool choose, bool two) {
  std::vector<std::string> o;
  if (choose) o.push_back(kOptChoose);
  if (choose && two) o.push_back(kOptTwo);
  return o;
}
bool ChooseOn(const std::vector<std::string>& options) { return Has(options, kOptChoose) || Decided(options, "starter"); }
bool TwoOn(const std::vector<std::string>& options) { return Has(options, kOptTwo) || Decided(options, "starter2"); }
bool ChooseWanted(const std::vector<std::string>& options) { return Has(options, kOptChoose) && !Decided(options, "starter"); }
bool TwoWanted(const std::vector<std::string>& options) { return Has(options, kOptTwo) && !Decided(options, "starter2"); }
std::vector<std::string> NamesOf(const std::vector<std::string>& options) {
  std::vector<std::string> out;
  for (const char* key : {"starter=", "starter2="}) {
    for (const std::string& o : options) {
      if (o.rfind(key, 0) != 0) continue;
      const size_t colon = o.find(':');
      out.push_back(colon == std::string::npos ? o.substr(std::strlen(key)) : o.substr(colon + 1));
    }
  }
  return out;
}

void Answer(uint16_t first, uint16_t second) {
  sSwappedLog = true;
  std::vector<std::string> options = xd::pt::CurrentOptions();
  options.erase(std::remove_if(options.begin(), options.end(),
                               [](const std::string& o) { return o == kOptChoose || o == kOptTwo || o.rfind("starter=", 0) == 0 || o.rfind("starter2=", 0) == 0; }),
                options.end());
  std::string note = "starter";
  if (const Candidate* c = CandidateOf(first)) {
    GiveChosen(0, first);
    options.push_back(DecidedLine("starter", *c));
    note += " 1 " + c->name + " (species " + std::to_string(first) + ")";
  }
  if (second != 0) {
    if (const Candidate* c = CandidateOf(second)) {
      GiveChosen(1, second);
      options.push_back(DecidedLine("starter2", *c));
      note += ", 2 " + c->name + " (species " + std::to_string(second) + ")";
    }
  }
  xd::pt::OptionsChanged(options, note);
  const Candidate* c1 = CandidateOf(first);
  const bool own = second == 0 && first == xr::kStarterEevee;
  if (!own && c1 != nullptr) {
    std::string who = c1->name;
    if (const Candidate* c2 = second != 0 ? CandidateOf(second) : nullptr) who += " and " + c2->name;
    PortXD_CheatsNoteUsed(("starter chosen: " + who).c_str());
  }
}

std::vector<std::string> TestOptions() {
  const std::vector<int> v = TestNumbers();
  if (v.empty() && sTestAsk != 0) return ChoiceOptions(true, sTestAsk == 2);
  return v.empty() ? std::vector<std::string>() : ChoiceOptions(true, v.size() > 1);
}
uint16_t TestPick(int slot) {
  const std::vector<int> v = TestNumbers();
  if (slot < 0 || slot >= static_cast<int>(v.size())) return 0;
  for (const Candidate& c : Candidates()) {
    if (c.national == v[static_cast<size_t>(slot)]) return c.index;
  }
  std::fprintf(stderr, "[starter] XD_STARTER_CHOOSE: no species with National number %d in this playthrough\n", v[static_cast<size_t>(slot)]);
  return 0;
}

void Choose(int index) {
  sSwappedLog = true;
  Swap(static_cast<uint32_t>(index < 0 ? 0 : index));
}

}

extern "C" void PortXD_NewGameCreate(void* save, uint16_t* name) {
  savedataCreate(save, name);
  if (!sPlan.on || sPlan.count == 0) {
    const std::vector<std::string>& options = xd::pt::CurrentOptions();
    if (!xd::starter::ChooseWanted(options)) return;
    const bool two = xd::starter::TwoWanted(options);
    if (const uint16_t first = xd::starter::TestPick(0); first != 0) {
      xd::starter::Answer(first, two ? xd::starter::TestPick(1) : 0);
      return;
    }
    if (const char* note = PortInput_ReplayPlaythroughNote("starter 1 ")) {
      unsigned first = 0, second = 0;
      const char* a = std::strstr(note, "(species ");
      if (a != nullptr) first = static_cast<unsigned>(std::atoi(a + 9));
      const char* b = a != nullptr ? std::strstr(a + 9, "(species ") : nullptr;
      if (b != nullptr) second = static_cast<unsigned>(std::atoi(b + 9));
      if (first != 0) {
        std::fprintf(stderr, "[starter] a replay: the recorded choice is taken (species %u%s)\n", first, second != 0 ? " and another" : "");
        xd::starter::Answer(static_cast<uint16_t>(first), two ? static_cast<uint16_t>(second) : 0);
        return;
      }
    }
    if (xd::TestRun() && !PickByScript()) {
      const auto& list = xd::starter::Candidates();
      std::fprintf(stderr, "[starter] a test run: the first species of the list is taken (XD_STARTER_CHOOSE=<national>[,<national>] chooses)\n");
      if (!list.empty()) xd::starter::Answer(list[0].index, two && list.size() > 1 ? list[1].index : 0);
      return;
    }
    if (!PortXD_OpenStarterChoose(two ? 1 : 0)) std::fprintf(stderr, "[starter] the choice page could not open: the game's Eevee stays\n");
    return;
  }
  if (xd::starter::ChooseOn(xd::pt::CurrentOptions())) std::fprintf(stderr, "[starter] the randomizer's starter wins over \"Choose your starter\"\n");
  SwapSecond();
  if (sPlan.choice != 1 || sPlan.count < 2) {
    Swap(0);
    return;
  }
  const char* pick = std::getenv("XD_STARTER_PICK");
  if (pick != nullptr && pick[0] != 0) {
    sSwappedLog = true;
    Swap(static_cast<uint32_t>(std::atoi(pick)) % sPlan.count);
    return;
  }
  if (xd::TestRun() && !PickByScript()) {
    std::fprintf(stderr, "[starter] a test run: the first of the three is taken (XD_STARTER_PICK=<0..2> chooses)\n");
    sSwappedLog = true;
    Swap(0);
    return;
  }
  if (!PortXD_OpenStarterPick()) {
    std::fprintf(stderr, "[starter] the choice page could not open: the first of the three is taken\n");
    Swap(0);
  }
}
