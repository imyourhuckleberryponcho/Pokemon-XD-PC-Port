#include "xd_forms.h"
#include "xd_gen57.h"
#include "xd_gen4.h"
#include "xd_gen_rules.h"
#include "xd_hidden_ability.h"
#include "xd_xg.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
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
static void LogShops(const char* tag = "[shops]") {
  const char* on = std::getenv("XD_SHOP_LOG");
  if (on == nullptr || on[0] != '1' || shopGoods == nullptr || shopGoods_number == nullptr || shopData == nullptr) return;
  for (int d = 0; d < 21; d++) {
    std::string line;
    const uint32_t from = shopData[d].goodsIdx;
    for (uint32_t i = from; i < *shopGoods_number && shopGoods[i] != 0 && i < from + 120; i++) line += " " + std::to_string(shopGoods[i]);
    std::fprintf(stderr, "%s shop %d (mode %u):%s\n", tag, d, static_cast<unsigned>(shopData[d].mode), line.c_str());
  }
}
static void ShopTablesLinked() {
  LogShops();
  if (xd::xg::Off(xd::xg::Feature::PrizeDesk) != nullptr) return;
  const bool xg = xd::xg::On();
  if (xg ? !xd::gen4::Loaded() : !xd::forms::Loaded()) return;
  if (shopGoods == nullptr || shopGoods_number == nullptr || shopData == nullptr || *shopGoods_number < xd::forms::ShopSpareRows() + 8) return;
  const uint32_t start = *shopGoods_number - xd::forms::ShopSpareRows();
  const uint32_t old = shopData[9].goodsIdx;
  if (old == start) return;
  std::vector<uint16_t> list;
  for (const uint16_t* p = shopGoods + old; *p != 0 && list.size() < 60; p++) list.push_back(*p);
  auto has = [&](uint32_t item) { for (uint16_t g : list) if (g == item) return true; return false; };
  if (xg) for (uint16_t g : xd::gen4::DeskItems()) if (!has(g) && list.size() < 100) list.push_back(g);
  if (xd::gen57::Loaded()) for (uint16_t g : xd::gen57::EvolutionShopItems()) if (!has(g) && list.size() < 100) list.push_back(g);
  if (xd::gen57::Loaded()) {
    const uint32_t prism = xd::gen57::PrismScaleItem();
    if (prism != 0 && xd::rulegen::AtLeast(5) && !has(prism) && list.size() < 100) list.push_back(static_cast<uint16_t>(prism));
  }
  const uint32_t charm = xd::forms::Loaded() ? xd::forms::CharmItem() : 0;
  if (charm != 0 && !has(charm)) list.push_back(static_cast<uint16_t>(charm));
  if (const uint32_t patch = xd::forms::Loaded() ? xd::forms::PatchItem() : 0; patch != 0 && xd::hidden::Enabled() && !has(patch)) list.push_back(static_cast<uint16_t>(patch));
  if (xd::forms::Loaded() && xd::hidden::Enabled()) for (uint32_t m : xd::forms::MemoryItems()) if (!has(m) && list.size() < 100) list.push_back(static_cast<uint16_t>(m));
  size_t stones = 0;
  if (xd::forms::Loaded() && xd::forms::MegaRule()) {
    for (const auto& f : xd::forms::All()) {
      if (f.kind != xd::forms::Kind::Mega || f.stone == 0 || has(f.stone) || list.size() >= 100) continue;
      if (pokemonDB_GetTukamaetaFlag(static_cast<uint16_t>(f.base)) != 0) { list.push_back(static_cast<uint16_t>(f.stone)); stones++; }
    }
  }
  list.push_back(0);
  for (size_t i = 0; i < list.size(); i++) shopGoods[start + i] = list[i];
  for (int d = 9; d <= 11; d++) shopData[d].goodsIdx = static_cast<uint16_t>(start);
  std::fprintf(stderr, "[forms] prize shop: %zu goods (charm %u, %zu Mega Stones)\n", list.size() - 1, charm, stones);
  LogShops("[shops after]");
}
