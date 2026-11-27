#include "tables_file.h"

#include "data/common_rel.h"
#include "xd_forms.h"
#include "xd_hidden_ability.h"
#include "xd_pc_trade.h"
#include "xd_xg.h"

#include <port_disc.h>
#include <xd_legality_tables.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

extern "C" {
extern void* pokemon_data;
extern uint32_t* pokemon_data_number;
extern uint8_t* item_data_prime;
extern uint32_t* item_data_prime_number;
extern uint16_t* item_data_index;
extern int32_t* item_data_index_number;
extern uint8_t pokemon_grow_data[];
extern uint8_t item_waza_machine_no[];
extern void** table_res;
extern uint32_t* table_res_number;
extern uint8_t* waza_data;
extern uint32_t* waza_data_number;
extern void* wazaoshie_data;
extern uint8_t pokemon_tokusei_data[];
extern uint32_t pokemon_tokusei_data_number;
extern uint8_t* pokemon_seikaku_data;
extern uint32_t* pokemon_seikaku_data_number;
extern int32_t* wazaoshie_data_number;
}

namespace xd::tablesfile {

namespace {

bool MakeRules(xd::legality::DiscRules& rules, std::string& error) {
  static XDCommonTables t;
  t = XDCommonTables{};
  auto add = [](const char* name, void* value) {
    if (t.numGlobals < XD_COMMON_MAX_GLOBALS) t.globals[t.numGlobals++] = XDRelGlobalValue{name, value, value != nullptr};
  };
  add("pokemon_data", pokemon_data);
  add("pokemon_data_number", pokemon_data_number);
  add("item_data_prime", item_data_prime);
  add("item_data_prime_number", item_data_prime_number);
  add("item_data_index", item_data_index);
  add("item_data_index_number", item_data_index_number);
  add("table_res", table_res);
  add("table_res_number", table_res_number);
  add("waza_data", waza_data);
  add("waza_data_number", waza_data_number);
  add("wazaoshie_data", wazaoshie_data);
  add("wazaoshie_data_number", wazaoshie_data_number);
  add("pokemon_seikaku_data", pokemon_seikaku_data);
  add("pokemon_seikaku_data_number", pokemon_seikaku_data_number);
  t.pokemon_data = static_cast<struct PokemonData*>(pokemon_data);
  t.pokemon_data_number = pokemon_data_number ? *pokemon_data_number : 0;
  static uint8_t grow[8 * 101 * 4], mach[58 * 8];
  auto u32at = [](const uint8_t* p, bool big) {
    return big ? (uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]) : (uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]);
  };
  bool bigGrow = true;
  for (int l = 1; l <= 100; l++) {
    if (u32at(pokemon_grow_data + l * 4, true) < u32at(pokemon_grow_data + (l - 1) * 4, true)) {
      bigGrow = false;
      break;
    }
  }
  const bool bigMach = u32at(item_waza_machine_no + 4, true) < 1000;
  for (int i = 0; i < 8 * 101; i++) {
    const uint32_t v = u32at(pokemon_grow_data + i * 4, bigGrow);
    for (int b = 0; b < 4; b++) grow[i * 4 + b] = static_cast<uint8_t>(v >> (24 - 8 * b));
  }
  for (int i = 0; i < 58 * 2; i++) {
    const uint32_t v = u32at(item_waza_machine_no + i * 4, bigMach);
    for (int b = 0; b < 4; b++) mach[i * 4 + b] = static_cast<uint8_t>(v >> (24 - 8 * b));
  }
  return xd::legality::BuildRulesFromRunningGame(&t, mach, grow, rules, error);
}

void AddPlaythroughExtras(xd::legality::TablesFile& tables) {
  auto& names = *tables.names;
  if (!xd::xg::On() && pokemon_tokusei_data_number > 0 && pokemon_tokusei_data_number <= 1024) {
    const auto* rows = reinterpret_cast<const uint32_t*>(pokemon_tokusei_data);
    const uint32_t n = pokemon_tokusei_data_number;
    if (names.abilities.size() < n) names.abilities.resize(n);
    names.abilityText.assign(n, std::string());
    for (uint32_t i = 1; i < n; i++) {
      const std::string name = PortXD_LiveMessage(rows[3 * i + 1]);
      if (!name.empty()) names.abilities[i] = name;
      names.abilityText[i] = PortXD_LiveMessage(rows[3 * i + 2]);
    }
  }
  names.formKey.assign(tables.rules.species.size(), std::string());
  for (const xd::forms::Form& f : xd::forms::All()) {
    if (f.row >= tables.rules.species.size() || f.base >= 65536) continue;
    auto& sp = tables.rules.species[f.row];
    sp.formOf = static_cast<uint16_t>(f.base);
    sp.formKind = static_cast<uint8_t>(f.kind);
    sp.plateItem = f.kind == xd::forms::Kind::Plate && f.stone < 65536 ? static_cast<uint16_t>(f.stone) : 0;
    names.formKey[f.row] = f.key;
  }
  if (xd::hidden::Enabled()) {
    for (size_t row = 1; row < tables.rules.species.size(); row++) {
      if (!tables.rules.species[row].exists) continue;
      const int id = xd::hidden::OfRow(static_cast<uint32_t>(row));
      if (id > 0 && id < 256) tables.rules.species[row].hiddenAbility = static_cast<uint8_t>(id);
    }
  }
}

}

bool Write(const std::filesystem::path& file, const std::string& stamp) {
  xd::legality::DiscRules game;
  std::string error;
  if (!MakeRules(game, error)) {
    std::fprintf(stderr, "[tables] the game's tables could not be listed: %s\n", error.c_str());
    return false;
  }
  game.names->SetLiveText(&PortXD_LiveMessage);
  xd::legality::TablesFile tables = xd::legality::TablesOf(game, stamp);
  AddPlaythroughExtras(tables);
  const std::vector<uint8_t> bytes = xd::legality::Serialize(tables);
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      std::fprintf(stderr, "[tables] could not write %ls\n", tmp.wstring().c_str());
      return false;
    }
  }
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  std::fprintf(stderr, "[tables] wrote %ls: %zu species, %zu moves, %zu items (%zu bytes)\n", file.wstring().c_str(), tables.rules.species.size(), tables.rules.moves.size(),
               tables.rules.items.size(), bytes.size());
  return true;
}

}
