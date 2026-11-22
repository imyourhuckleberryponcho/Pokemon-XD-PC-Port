#ifndef XD_GEN_RULES_H
#define XD_GEN_RULES_H

#include <mods.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::rulegen {

int Gen();
inline bool AtLeast(int g) { return Gen() >= g; }
int GenOfIds(const std::vector<std::string>& ids);
int NumbersOfIds(const std::vector<std::string>& ids);

int NumbersLoaded();
void PrepareNumbers(int gen, std::vector<engine::mods::DataModFile>& dataFiles);

uint32_t ParalysisSpeed(void* fop, uint32_t speed);
int32_t DamageRules(void* attacker, void* defender, uint16_t waza, uint16_t type, int32_t damage);
void SetCritTable();
int32_t BurnDamage(void* fop, uint16_t divisorGameUsed);

}
#endif
