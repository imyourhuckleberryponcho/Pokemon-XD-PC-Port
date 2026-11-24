#include <xd_legality_tables.h>

#include <algorithm>

namespace xd::legality {

namespace {
void Bind(TablesFile& t) {
  const std::shared_ptr<TableNames> names = t.names;
  t.rules.speciesName = [names](uint16_t id) { return id < names->species.size() ? names->species[id] : std::string(); };
  t.rules.moveName = [names](uint16_t id) { return id < names->moves.size() ? names->moves[id] : std::string(); };
  t.rules.itemName = [names](uint16_t id) { return id < names->items.size() ? names->items[id] : std::string(); };
}
}

TableNames NamesOf(const DiscRules& game) {
  TableNames n;
  const Rules& r = game.rules;
  if (!game.names) return n;
  n.species.resize(r.species.size());
  for (size_t i = 1; i < r.species.size(); i++) {
    if (r.species[i].exists) n.species[i] = game.names->Species(static_cast<uint16_t>(i));
  }
  n.moves.resize(r.moves.size());
  for (size_t i = 1; i < r.moves.size(); i++) {
    if (r.moves[i].exists) n.moves[i] = game.names->Move(static_cast<uint16_t>(i));
  }
  n.items.resize(r.items.size());
  for (size_t i = 1; i < r.items.size(); i++) {
    if (r.items[i].exists) n.items[i] = game.names->Item(static_cast<uint16_t>(i));
  }
  unsigned maxAbility = 0;
  for (const SpeciesRules& s : r.species) maxAbility = std::max<unsigned>(maxAbility, std::max(s.abilities[0], s.abilities[1]));
  n.abilities.resize(static_cast<size_t>(maxAbility) + 1);
  for (unsigned i = 1; i <= maxAbility; i++) n.abilities[i] = game.names->Ability(static_cast<uint16_t>(i));
  n.natures.resize(25);
  for (unsigned i = 0; i < 25; i++) n.natures[i] = game.names->Nature(static_cast<uint8_t>(i));
  return n;
}

TablesFile TablesOf(const DiscRules& game, const std::string& stamp) {
  TablesFile t;
  t.rules = game.rules;
  t.names = std::make_shared<TableNames>(NamesOf(game));
  t.stamp = stamp;
  Bind(t);
  return t;
}

}
