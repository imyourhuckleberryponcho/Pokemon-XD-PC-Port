#ifndef XD_LEGALITY_TABLES_H
#define XD_LEGALITY_TABLES_H

#include <xd_legality.h>
#include <xd_legality_disc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xd::legality {

struct TableNames {
  std::vector<std::string> species, moves, items, abilities;
  std::vector<std::string> natures;
};

struct TablesFile {
  Rules rules;
  std::shared_ptr<TableNames> names;
  std::string stamp;
  std::string Species(uint16_t id) const { return names && id < names->species.size() ? names->species[id] : std::string(); }
  std::string Move(uint16_t id) const { return names && id < names->moves.size() ? names->moves[id] : std::string(); }
  std::string Item(uint16_t id) const { return names && id < names->items.size() ? names->items[id] : std::string(); }
  std::string Ability(uint16_t id) const { return names && id < names->abilities.size() ? names->abilities[id] : std::string(); }
  std::string Nature(uint8_t id) const { return names && id < names->natures.size() ? names->natures[id] : std::string(); }
};

TableNames NamesOf(const DiscRules& game);

TablesFile TablesOf(const DiscRules& game, const std::string& stamp);

std::vector<uint8_t> Serialize(const TablesFile& tables);
bool Parse(const std::vector<uint8_t>& bytes, TablesFile& out, std::string& error);

}

#endif
