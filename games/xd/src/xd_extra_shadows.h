#ifndef XD_EXTRA_SHADOWS_H
#define XD_EXTRA_SHADOWS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "xd_randomizer.h"

namespace xd::shadowrows {

constexpr uint32_t kFirstRow = 128;
constexpr size_t kRecordSize = 0x48;

void Clear();
void Add(const std::vector<xd::randomizer::ExtraShadowRow>& rows);
uint32_t Count();
void Apply();
void RegisterSaveExt();

}

#endif
