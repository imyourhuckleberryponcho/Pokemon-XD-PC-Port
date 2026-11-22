#ifndef XD_EXTRACT_H
#define XD_EXTRACT_H

#include <xd_legality_disc.h>
#include <xd_save_write.h>
#include <xd_trade.h>
#include <xd_trade_online.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xd::extract {

struct Place {
  int box = -1;
  int slot = 0;
  xd::save::Pokemon pokemon;
};

struct Names {
  const xd::legality::Rules* rules;
  const xd::save::GameNames* names;
  std::string Species(uint16_t id) const;
  std::string Move(uint16_t id) const;
  std::string Item(uint16_t id) const;
  bool SpeciesOk(uint16_t id) const;
  bool MoveOk(uint16_t id) const;
  bool ItemOk(uint16_t id) const;
};

struct Maps {
  std::map<uint16_t, uint16_t> species, moves, items;
  bool dropMoves = false;
  bool dropItem = false;
};

std::map<uint16_t, uint16_t> ParseMap(const std::string& text);

struct Source {
  xd::trade::CardSave card;
  xd::save::Save save;
  std::vector<Place> places;
};
bool LoadSource(const std::string& path, Source& out, std::string& error);

std::vector<std::string> NotPlain(const Names& names, const xd::save::Pokemon& p);
std::string WhereText(const Place& p);

struct TakeResult {
  bool done = false;
  bool fatal = false;
  std::vector<std::string> refuse;
  std::vector<std::string> notes;
  std::string species, fromWhere, toWhere;
  int level = 0;
  uint16_t speciesId = 0;
  std::vector<uint8_t> file;
  std::vector<uint8_t> record;
  std::string memoNote;
  std::string metNote;
};

TakeResult Take(const xd::legality::DiscRules& disc, const Source& source, size_t index, const xd::trade::CardSave& targetCard, const xd::save::Save& targetSave,
                const xd::save::ReadReport& targetReport, const Maps& maps, bool toParty);

}

#endif
