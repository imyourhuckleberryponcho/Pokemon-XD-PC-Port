#ifndef XD_GEN4_ABILITIES_H
#define XD_GEN4_ABILITIES_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::gen4 {

void AppendAbilityTexts(std::vector<std::pair<uint32_t, std::string>>& texts);
void InstallAbilityRows();

class StageGuard {
 public:
  StageGuard(void* attacker, void* defender);
  ~StageGuard();
  StageGuard(const StageGuard&) = delete;
  StageGuard& operator=(const StageGuard&) = delete;

 private:
  void* atk_ = nullptr;
  void* def_ = nullptr;
  uint8_t saved_[4] = {};
  bool on_ = false;
};
class MoldScope {
 public:
  MoldScope(void* attacker, void* defender);
  ~MoldScope();
  MoldScope(const MoldScope&) = delete;
  MoldScope& operator=(const MoldScope&) = delete;

 private:
  void* def_ = nullptr;
  uint16_t old_ = 0;
};
int32_t AbilityDamage(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage);
uint32_t AbilitySpeed(void* fop, uint32_t speed);
uint8_t AbilityAccuracy(void* attacker, void* defender, uint8_t avg);
bool MotorDriveHit(void* attacker, void* defender, uint16_t waza, uint8_t* result);
void MagicGuardHpDec();

}

#endif
