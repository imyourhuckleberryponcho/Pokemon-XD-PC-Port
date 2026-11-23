#ifndef XD_HIDDEN_ABILITY_H
#define XD_HIDDEN_ABILITY_H

#include <cstdint>
#include <map>

namespace xd::hidden {

void SetTables(const std::map<int, int>& byNational, const std::map<int, int>& byRow);
bool Enabled();
int OfRow(uint32_t row);
void MaybeGive(void* pokemon);
bool Give(void* pokemon, bool on);
bool Has(void* pokemon);
constexpr uint32_t kTextPatchDone = 0xBF1C, kTextPatchNone = 0xBF1D, kTextPatchAlready = 0xBF1E, kTextPatchShadow = 0xBF1F;

}

#endif
