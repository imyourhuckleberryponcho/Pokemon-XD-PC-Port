#ifndef XD_FORMS_H
#define XD_FORMS_H

#include <mods.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::forms {

enum class Kind : uint8_t { None, Alola, Mega, Primal, Battle, Cosmetic };

struct Form {
  uint32_t row = 0;
  uint32_t base = 0;
  Kind kind = Kind::None;
  uint32_t stone = 0;
  std::string key;
};

void Load(const engine::mods::LoadResult& result);
bool Loaded();
const std::vector<Form>& All();
uint32_t BaseOf(uint32_t row);
uint32_t AlolaEvolution(uint32_t current, uint32_t held, uint32_t result);
bool IsForm(uint32_t row);
uint32_t CharmItem();
uint32_t ShopSpareRows();
void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts);

bool MegaRule();
void BattleEnded();
void TurnStart();
void FriendMega(void* fop);
bool TakeChoiceMega(void* fop);
void SetFriendApplying(bool on);
void EntryPrimal(void* fop);
void BattleFormEntry(void* fop);
void BattleFormTurnEnd();
void BattleFormMove(void* attacker, uint16_t move, bool kingsShield, bool damaging);
void BattleFormHit(void* attacker, void* defender, uint16_t move, bool ko);
bool DisguiseIntact(void* defender);
void DisguiseBust(void* defender);

}

#endif
