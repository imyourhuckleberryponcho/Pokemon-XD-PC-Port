#ifndef XD_MODERN_TYPES_H
#define XD_MODERN_TYPES_H

#include <mods.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::modern {

inline constexpr int kFairyBigX = 78, kFairyBigY = 182;
inline constexpr int kFairySmallX = 48, kFairySmallY = 182;

void Prepare(bool wanted, bool gen4, std::vector<engine::mods::DataModFile>& dataFiles);

bool Loaded();

int Gen57FairyRows();

void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts);

uint16_t ChartCode(uint32_t attack, uint32_t defend);

}

#endif
