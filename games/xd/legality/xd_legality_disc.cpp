#include <xd_legality_disc.h>

#include "common_rel.h"
#include "dol_tables.h"

#include <game/menuTables.h>
#include <game/pxdvs/app/item/itemData.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/waza/wazaData.h>

#include <port_disc.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <vector>
#include <cstring>

namespace xd::legality {

namespace {

uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

const void* Global(const XDCommonTables& t, const char* name) {
  for (uint32_t i = 0; i < t.numGlobals; i++) {
    if (t.globals[i].name != nullptr && std::strcmp(t.globals[i].name, name) == 0 && t.globals[i].converted) {
      return t.globals[i].value;
    }
  }
  return nullptr;
}
uint32_t Count(const XDCommonTables& t, const char* name) {
  const void* v = Global(t, name);
  return v != nullptr ? *static_cast<const uint32_t*>(v) : 0;
}

const uint8_t* DolAt(uint32_t address, uint32_t size) {
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol == nullptr || dolSize < 0xE4) return nullptr;
  for (int i = 0; i < 18; i++) {
    const uint32_t off = be32(dol + i * 4), addr = be32(dol + 0x48 + i * 4), len = be32(dol + 0x90 + i * 4);
    if (len != 0 && address >= addr && address - addr + size <= len && static_cast<size_t>(off) + (address - addr) + size <= dolSize) {
      return dol + off + (address - addr);
    }
  }
  return nullptr;
}
constexpr uint32_t kMachineTable = 0x804053A0, kMachineBytes = 0x1D0;
constexpr uint32_t kGrowTable = 0x803FEFB0, kGrowBytes = 0xCA0;

}

namespace {
bool Fill(const XDCommonTables& tables, DiscRules& out, std::string& error, const uint8_t* machines = nullptr, const uint8_t* grow = nullptr, bool running = false);
}
static bool LoadRulesImpl(const std::string& discPath, bool running, bool original, DiscRules& out, std::string& error);

bool LoadRulesFromDisc(const std::string& discPath, DiscRules& out, std::string& error) {
  return LoadRulesImpl(discPath, false, false, out, error);
}
bool LoadRulesFromRunningGame(DiscRules& out, std::string& error) { return LoadRulesImpl(std::string(), true, false, out, error); }
bool LoadRulesOriginalInRunningGame(DiscRules& out, std::string& error) { return LoadRulesImpl(std::string(), true, true, out, error); }

static bool LoadRulesImpl(const std::string& discPath, bool running, bool original, DiscRules& out, std::string& error) {
  out = DiscRules{};
  out.names = std::make_shared<xd::save::GameNames>();
  if (!(running ? out.names->LoadFromRunningGame(error) : out.names->Load(discPath, error))) {
    return false;
  }
  XDCommonTables tables{};
  char err[512] = {};
  const XDCommonTables* live = running && !original ? PortXD_LiveCommonTables() : nullptr;
  if (live != nullptr) {
    tables = *live;
  } else if (!(original ? PortXD_LoadCommonRelOriginal(&tables, err, sizeof(err)) : PortXD_LoadCommonRel(&tables, err, sizeof(err)))) {
    error = std::string("common_rel: ") + err;
    return false;
  }
  return Fill(tables, out, error, nullptr, nullptr, running);
}

bool BuildRulesFromRunningGame(const void* tables, const uint8_t* machinesBE, const uint8_t* growBE, DiscRules& out, std::string& error) {
  out = DiscRules{};
  out.names = std::make_shared<xd::save::GameNames>();
  if (!out.names->LoadFromRunningGame(tables, error)) return false;
  return Fill(*static_cast<const XDCommonTables*>(tables), out, error, machinesBE, growBE);
}

namespace {
bool Fill(const XDCommonTables& tables, DiscRules& out, std::string& error, const uint8_t* machines, const uint8_t* grow, bool running) {
  Rules& r = out.rules;
  const uint32_t nSpecies = tables.pokemon_data_number;
  const uint32_t nMoves = Count(tables, "waza_data_number");
  const uint32_t nItemIds = Count(tables, "item_data_index_number");
  const auto* moves = static_cast<const WazaData*>(Global(tables, "waza_data"));
  const auto* itemIndex = static_cast<const uint16_t*>(Global(tables, "item_data_index"));
  const auto* items = static_cast<const ItemDataBiosRef*>(Global(tables, "item_data_prime"));
  if (tables.pokemon_data == nullptr || moves == nullptr || itemIndex == nullptr || items == nullptr || nSpecies == 0 ||
      nMoves == 0) {
    error = "the game's species, move or item table could not be read from the disc";
    return false;
  }
  r.vanillaSpecies = nSpecies;
  r.vanillaMoves = nMoves;
  r.vanillaItems = nItemIds;
  if (running) {
    r.vanillaSpecies = std::min<uint32_t>(nSpecies, 415);
    r.vanillaMoves = std::min<uint32_t>(nMoves, 375);
    r.vanillaItems = std::min<uint32_t>(nItemIds, 594);
  }
  r.moves.resize(nMoves);
  for (uint32_t i = 1; i < nMoves; i++) {
    r.moves[i].exists = moves[i].name != 0;
    r.moves[i].shadow = moves[i].darkWazaKind != 0;
    r.moves[i].basePp = moves[i].pp;
  }
  const uint32_t nPrime = Count(tables, "item_data_prime_number");
  r.items.resize(nItemIds);
  for (uint32_t i = 1; i < nItemIds; i++) {
    const uint16_t slot = itemIndex[i];
    r.items[i].exists = slot < nPrime && items[slot].name != 0;
    if (r.items[i].exists) r.items[i].soubi = items[slot].itemSoubiDataId;
  }
  if (const uint8_t* mach = machines ? machines : DolAt(kMachineTable, kMachineBytes)) {
    const uint32_t n = kMachineBytes / 8;
    for (uint32_t i = 0; i < n; i++) r.machineMove[i] = static_cast<uint16_t>(be32(mach + i * 8 + 4));
  }
  if (const auto* tutor = static_cast<const WAZAOSHIE_DATA*>(Global(tables, "wazaoshie_data"))) {
    static const int rowToIndex[12] = {3, 5, 2, 7, 6, 4, 8, 0, 11, 9, 1, 10};
    const uint32_t n = std::min<uint32_t>(12, Count(tables, "wazaoshie_data_number"));
    for (uint32_t row = 0; row < n; row++) r.tutorMove[static_cast<size_t>(rowToIndex[row])] = tutor[row].id;
  }
  if (const uint8_t* growTable = grow ? grow : DolAt(kGrowTable, kGrowBytes)) {
    const uint32_t curves = kGrowBytes / (101 * 4);
    r.expCurves.resize(curves);
    for (uint32_t c = 0; c < curves; c++) {
      for (uint32_t l = 0; l <= 100; l++) r.expCurves[c][l] = be32(growTable + (c * 101 + l) * 4);
    }
  }
  r.species.resize(nSpecies);
  for (uint32_t i = 1; i < nSpecies; i++) {
    const PokemonData& d = tables.pokemon_data[i];
    SpeciesRules& s = r.species[i];
    s.exists = d.numPokemon != 0;
    s.national = d.numPokemon;
    s.growth = d.growDataId;
    s.abilities[0] = d.tokuseiDataId[0];
    s.abilities[1] = d.tokuseiDataId[1];
    const BasisStatus& b = d.basisStatus[0];
    s.baseStats[0] = b.maxHp;
    s.baseStats[1] = b.phyAtk;
    s.baseStats[2] = b.phyDef;
    s.baseStats[3] = b.speAtk;
    s.baseStats[4] = b.speDef;
    s.baseStats[5] = b.nimbleness;
    for (size_t m = 0; m < 58; m++) s.machine[m] = d.wazaMcn[m] != 0;
    for (size_t t = 0; t < 12; t++) s.tutor[t] = d.teachWaza[t] != 0;
    for (const PokemonDataWaza& w : d.waza) {
      if (w.dataId != 0) s.levelUp.push_back({w.level, w.dataId});
    }
    for (uint16_t m : d.kowaza) {
      if (m != 0) s.eggMoves.push_back(m);
    }
    for (const Sinka& e : d.sinka) {
      if (e.kind != 0 && e.pokemonDataId != 0) s.evolutions.push_back({e.kind, e.buff, e.pokemonDataId});
    }
  }
  for (uint32_t i = 1; i < nSpecies; i++) {
    for (const Sinka& e : tables.pokemon_data[i].sinka) {
      if (e.kind != 0 && e.pokemonDataId != 0 && e.pokemonDataId < nSpecies && e.pokemonDataId != i &&
          r.species[e.pokemonDataId].evolvesFrom == 0) {
        r.species[e.pokemonDataId].evolvesFrom = static_cast<uint16_t>(i);
      }
    }
  }
  {
    struct Gift {
      const char* species;
      std::vector<const char*> moves;
    };
    static const Gift kGifts[] = {
        {"EEVEE", {"TACKLE", "TAILWHIP", "BITE", "SANDATTACK"}},
        {"VAPOREON", {"HELPINGHAND", "WATERGUN", "QUICKATTACK", "BITE"}},
        {"JOLTEON", {"QUICKATTACK", "SANDATTACK", "HELPINGHAND", "THUNDERSHOCK"}},
        {"TOGEPI", {"FOLLOWME", "TRIATTACK", "ANCIENTPOWER", "HELPINGHAND"}},
        {"ELEKID", {"ICEPUNCH", "FIREPUNCH", "THUNDERPUNCH", "CROSSCHOP"}},
        {"MEDITITE", {"DYNAMICPUNCH", "CONFUSION", "SHADOWBALL", "DETECT"}},
        {"SHUCKLE", {"TOXIC", "SUBSTITUTE", "SLUDGEBOMB", "ENCORE"}},
        {"LARVITAR", {"SANDSTORM", "DRAGONDANCE", "BITE", "OUTRAGE"}},
        {"CHIKORITA", {"ANCIENTPOWER", "TACKLE", "GROWL", "FRENZYPLANT"}},
        {"CYNDAQUIL", {"REVERSAL", "TACKLE", "LEER", "BLASTBURN"}},
        {"TOTODILE", {"CRUNCH", "SCRATCH", "LEER", "HYDROCANNON"}},
    };
    const auto norm = [](std::string n) {
      std::string o;
      for (char c : n) {
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
      return o;
    };
    std::map<std::string, uint16_t> speciesByName, moveByName;
    for (uint32_t i = 1; i < nSpecies; i++) speciesByName.emplace(norm(out.names->Species(static_cast<uint16_t>(i))), static_cast<uint16_t>(i));
    for (uint32_t i = 1; i < nMoves; i++) moveByName.emplace(norm(out.names->Move(static_cast<uint16_t>(i))), static_cast<uint16_t>(i));
    for (const Gift& g : kGifts) {
      const auto sp = speciesByName.find(g.species);
      if (sp == speciesByName.end()) continue;
      for (const char* m : g.moves) {
        const auto mv = moveByName.find(m);
        if (mv != moveByName.end()) r.giftMoves.push_back({sp->second, mv->second});
      }
    }
  }
  auto names = out.names;
  r.speciesName = [names](uint16_t id) { return names->Species(id); };
  r.moveName = [names](uint16_t id) { return names->Move(id); };
  r.itemName = [names](uint16_t id) { return names->Item(id); };
  return true;
}
}

}
