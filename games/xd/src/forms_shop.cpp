#include "xd_forms.h"

#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" {
extern uint16_t* shopGoods;
extern uint32_t* shopGoods_number;
struct PortShopData { uint8_t unk0, mode; uint16_t goodsIdx; };
extern PortShopData* shopData;
int32_t pokemonDB_GetTukamaetaFlag(uint16_t id);
}

namespace xd::forms {
constexpr uint32_t kSpare = 140;
uint32_t ShopSpareRows() { return kSpare; }
}

extern "C" void (*PortXD_ShopTablesLinkedHook)(void);
static void ShopTablesLinked();
namespace { struct Register { Register() { PortXD_ShopTablesLinkedHook = &ShopTablesLinked; } } sRegister; }
static void ShopTablesLinked() {
  if (!xd::forms::Loaded() || shopGoods == nullptr || shopGoods_number == nullptr || shopData == nullptr || *shopGoods_number < xd::forms::ShopSpareRows() + 8) return;
  const uint32_t start = *shopGoods_number - xd::forms::ShopSpareRows();
  const uint32_t old = shopData[9].goodsIdx;
  if (old == start) return;
  std::vector<uint16_t> list;
  for (const uint16_t* p = shopGoods + old; *p != 0 && list.size() < 60; p++) list.push_back(*p);
  auto has = [&](uint32_t item) { for (uint16_t g : list) if (g == item) return true; return false; };
  const uint32_t charm = xd::forms::CharmItem();
  if (charm != 0 && !has(charm)) list.push_back(static_cast<uint16_t>(charm));
  size_t stones = 0;
  if (xd::forms::MegaRule()) {
    for (const auto& f : xd::forms::All()) {
      if (f.kind != xd::forms::Kind::Mega || f.stone == 0 || has(f.stone) || list.size() >= 100) continue;
      if (pokemonDB_GetTukamaetaFlag(static_cast<uint16_t>(f.base)) != 0) { list.push_back(static_cast<uint16_t>(f.stone)); stones++; }
    }
  }
  list.push_back(0);
  for (size_t i = 0; i < list.size(); i++) shopGoods[start + i] = list[i];
  for (int d = 9; d <= 11; d++) shopData[d].goodsIdx = static_cast<uint16_t>(start);
  std::fprintf(stderr, "[forms] prize shop: %zu goods (charm %u, %zu Mega Stones)\n", list.size() - 1, charm, stones);
}
