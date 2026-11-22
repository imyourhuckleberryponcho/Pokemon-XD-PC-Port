#ifndef XD_MOD_BATTLE_H
#define XD_MOD_BATTLE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine::mods {
struct LoadResult;
struct ModFolder;
}

namespace xd::modbattle {

void Load(const engine::mods::LoadResult& result, const std::vector<engine::mods::ModFolder>& found, const std::string& gameId, std::vector<std::string>& problems);
bool Active();
std::string ActiveText();
uint64_t Digest();
void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts);
void InstallAbilityRows();
void AppendAbilityNames(std::vector<std::pair<std::string, int64_t>>& out);

void EnsureInstalled();
bool PortCommand(uint8_t id);
uint16_t PowerOf(uint16_t waza, uint16_t power, void* attacker, void* defender);
int32_t Damage(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage);
uint8_t Accuracy(uint16_t waza, uint8_t avg);
uint32_t Speed(void* fop, uint32_t speed);
uint32_t CriticalValue(uint32_t value);
void AfterCritical();
void ProtectPenalty(void* attacker, void* defender, uint16_t move);
uint8_t WazaType(void* fop, uint8_t type);
class DamageGuard {
 public:
  DamageGuard(void* attacker, void* defender, uint16_t waza);
  ~DamageGuard();
  DamageGuard(const DamageGuard&) = delete;
  DamageGuard& operator=(const DamageGuard&) = delete;

 private:
  bool on_ = false;
  void* atk_ = nullptr;
  void* def_ = nullptr;
  uint8_t saved_[3] = {};
};
uint16_t PhyAtkRead(void* pokemon);
uint16_t SpeDefRead(void* pokemon);

bool AbsorbHit(void* attacker, void* defender, uint16_t waza, uint8_t* result);
void NoteDamage(void* attacker, void* defender, int32_t dealt);
void HitAfter(void* attacker, void* defender);
void MoveStarting(void* attacker, uint16_t move);
void Appear(void* p);
void SwitchingOut(void* p);
void EndOfTurn();
bool StatusBlocked(void* p, uint8_t effect);
int PriorityOf(void* fop, uint16_t waza, int pri);
void StatsMayHaveFallen(void* defender, const uint8_t before[7]);
bool PriorityBlocked();
void BadgeCheck();
void TsuikaCheck();
uint16_t SoubiKind(void* fop, uint16_t kind);
uint16_t TypeOf(void* fop, uint8_t index, uint16_t type);
uint32_t ActsAs(uint32_t ability);
uint16_t InverseJoutai(uint16_t result);
void BattleEnded();
void RuleLevel(void* pokemon);
bool SetRules(const std::string& nameOrNone);
void ForgetCopy(void* pokemon);
bool RestoreRealLevels(void* trainer);
bool Rules();

}

#endif
