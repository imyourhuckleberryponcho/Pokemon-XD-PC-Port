#include "xd_extra_shadows.h"

#include "xd_playthrough.h"
#include "xd_save_ext.h"
#include "xd_spare.h"

#include <xd_save.h>
#include <xd_shadow_orders.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

extern "C" {
extern uint16_t darkPokemonList[];
extern uint32_t darkPokemonList_number;
extern uint8_t pc_deck_dark_pokemon_extra[];
extern void* (*PortXD_ExtraDarkPokemon)(uint32_t index);
uint8_t xd_extra_shadow_records[XD_PC_SPARE_SHADOWS][0x48];
}

namespace {

constexpr uint16_t kStandIn = 280;
constexpr size_t kMaxRows = XD_PC_SPARE_SHADOWS;
static_assert(XD_PC_SPARE_SHADOWS == xd::save::kExtraShadowRows && xd::shadowrows::kRecordSize == 0x48, "the editor and the game agree on the spare rows");

std::map<uint32_t, xd::randomizer::ExtraShadowRow> sRows;
uint32_t sCount = 0;
bool sApplied = false;

void Swap(uint8_t* r) {
  const auto swap = [&](size_t at, size_t count, size_t width) {
    for (size_t i = 0; i < count; i++) std::reverse(r + at + i * width, r + at + (i + 1) * width);
  };
  swap(0x14, 4, 2);
  swap(0x1C, 3, 4);
  swap(0x28, 11, 2);
  swap(0x3E, 1, 2);
  swap(0x44, 1, 4);
}

void InitRecord(size_t i) {
  uint8_t* r = xd_extra_shadow_records[i];
  std::memset(r, 0, xd::shadowrows::kRecordSize);
  const auto it = sRows.find(static_cast<uint32_t>(xd::shadowrows::kFirstRow + i));
  const uint16_t species = it != sRows.end() ? it->second.species : kStandIn;
  const uint32_t gauge = it != sRows.end() ? (static_cast<uint32_t>(it->second.deck[8]) << 8 | it->second.deck[9]) : 0;
  const int32_t point = static_cast<int32_t>(std::min<uint32_t>(gauge, 65535));
  const uint16_t row = static_cast<uint16_t>(xd::shadowrows::kFirstRow + i);
  std::memcpy(r + 0x1A, &species, 2);
  std::memcpy(r + 0x24, &point, 4);
  std::memcpy(r + 0x3E, &row, 2);
}

void* Hook(uint32_t index) { return index < sCount ? xd_extra_shadow_records[index] : nullptr; }

std::vector<uint8_t> SaveSection() {
  std::vector<uint8_t> out;
  if (sCount == 0 && xd::pt::LoadedMoreShadows().rfind("pct:", 0) != 0 && xd::pt::LoadedMoreShadows().rfind("own:", 0) != 0 && xd::pt::LoadedMoreShadows().rfind("own2:", 0) != 0) return out;
  for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(sCount >> (8 * i)));
  for (uint32_t i = 0; i < sCount; i++) {
    uint8_t r[xd::shadowrows::kRecordSize];
    std::memcpy(r, xd_extra_shadow_records[i], sizeof r);
    Swap(r);
    out.insert(out.end(), r, r + sizeof r);
  }
  return out;
}

bool LoadSection(const uint8_t* d, size_t n, uint16_t version) {
  if (version != 1 || n < 4) return false;
  const uint32_t count = static_cast<uint32_t>(d[0]) | static_cast<uint32_t>(d[1]) << 8 | static_cast<uint32_t>(d[2]) << 16 | static_cast<uint32_t>(d[3]) << 24;
  if (count > kMaxRows || n != 4 + static_cast<size_t>(count) * xd::shadowrows::kRecordSize) return false;
  if (count > sCount) std::fprintf(stderr, "[extra shadows] the save's extension has %u rows past 127, this start-up only %u: the rest are left out\n", count, sCount);
  for (uint32_t i = 0; i < count && i < sCount; i++) {
    std::memcpy(xd_extra_shadow_records[i], d + 4 + static_cast<size_t>(i) * xd::shadowrows::kRecordSize, xd::shadowrows::kRecordSize);
    Swap(xd_extra_shadow_records[i]);
  }
  return true;
}

void ResetSection() {
  for (uint32_t i = 0; i < sCount; i++) InitRecord(i);
}

}

namespace xd::shadowrows {

void Clear() {
  if (sApplied) return;
  sRows.clear();
  sCount = 0;
}

void Add(const std::vector<xd::randomizer::ExtraShadowRow>& rows) {
  if (sApplied) return;
  for (const auto& r : rows) {
    if (r.row < kFirstRow || r.row >= kFirstRow + kMaxRows) continue;
    sRows[r.row] = r;
    sCount = std::max<uint32_t>(sCount, r.row - kFirstRow + 1);
  }
}

uint32_t Count() { return sCount; }

void Apply() {
  if (sCount == 0 || sApplied) return;
  sApplied = true;
  for (uint32_t i = 0; i < sCount; i++) {
    const auto it = sRows.find(kFirstRow + i);
    darkPokemonList[kFirstRow + i] = it != sRows.end() ? it->second.species : kStandIn;
    std::memset(pc_deck_dark_pokemon_extra + static_cast<size_t>(i) * 0x18, 0, 0x18);
    if (it != sRows.end()) std::memcpy(pc_deck_dark_pokemon_extra + static_cast<size_t>(i) * 0x18, it->second.deck, 0x18);
    InitRecord(i);
  }
  darkPokemonList_number = kFirstRow + sCount;
  PortXD_ExtraDarkPokemon = Hook;
  std::fprintf(stderr, "[extra shadows] %u Shadow row(s) past 127 in use (rows 128..%u of the game's %zu spare ones)\n", sCount, kFirstRow + sCount - 1, kMaxRows);
}

void RegisterSaveExt() {
  xd::saveext::Handler h;
  h.name = "shadow";
  h.version = 1;
  h.required = true;
  h.save = SaveSection;
  h.load = LoadSection;
  h.reset = ResetSection;
  xd::saveext::Register(h);
}

}

extern "C" void PortXD_ExtraShadowsApply(void) { xd::shadowrows::Apply(); }
