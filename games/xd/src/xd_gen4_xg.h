#ifndef XD_GEN4_XG_H
#define XD_GEN4_XG_H

#include <mods.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace xd::gen4::xg {

struct Tables {
  std::vector<std::string> moves;
  std::vector<int> movePp;
  std::vector<std::string> abilities;
  std::vector<std::string> packAbilities;
  std::vector<std::string> items;
  std::vector<uint16_t> machineMoves;
  std::vector<uint16_t> tutorMoves;
  uint32_t firstNewMove = 375;
};

struct MoveRef {
  int id = -1;
  std::string name;
};

class Translator {
public:
  explicit Translator(Tables t);

  bool Run(std::vector<engine::mods::DataModFile>& files, const std::string& machinesText, std::string& error);

  void MapLearnset(uint32_t level[20], uint32_t move[20]) const;
  uint32_t MapAbility(uint32_t xdAbility) const;
  void MapTypes(const std::string& species, uint32_t types[2]) const;

  const std::vector<std::string>& Report() const { return mReport; }
  const std::vector<std::string>& AddedMoves() const { return mAddedOrder; }
  uint32_t AddedMoveNumber(const std::string& name) const;

private:
  Tables mT;
  std::map<std::string, int> mMoveKey;
  std::map<std::string, int> mAbilityKey;
  std::map<std::string, int> mItemKey;
  std::vector<std::string> mPackMoves;
  std::map<std::string, char> mPackKind;
  std::map<std::string, std::string> mPackBase;
  std::vector<std::string> mAddedOrder;
  std::vector<std::string> mReport;

  std::string XdMoveName(uint32_t number) const;
  MoveRef ResolveMove(uint32_t xdNumber, const std::set<std::string>& used) const;
  static std::string RefKey(const MoveRef& r);
  std::vector<std::pair<uint32_t, MoveRef>> Learnset(const std::vector<std::pair<uint32_t, uint32_t>>& in) const;
  int FindMove(const std::string& name) const;
  int AbilityFor(const std::string& xdName) const;
  std::string XdAbilityName(uint32_t id) const;
};

bool BuildTables(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, Tables& out, std::string& error);

void SetActive(const Translator* t);
const Translator* Active();

}

#endif
