#include "xd_forms.h"
#include "xd_gen57.h"
#include "xd_gen4.h"
#include "xd_gen_rules.h"
#include "xd_hidden_ability.h"
#include "xd_xg.h"
#include "xd_modern_types.h"
#include <game/winwork.h>

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
constexpr uint32_t kSpare = 220;
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
namespace {
struct ShopPage { uint32_t msg; uint32_t start; uint32_t count; };
std::vector<ShopPage> sPages;
int sPage = 0;
constexpr uint32_t kPageMsg = 0xBF30;
}

static void SetPage(int page) {
  if (page < 0 || page >= static_cast<int>(sPages.size())) return;
  sPage = page;
  for (int d = 9; d <= 11; d++) shopData[d].goodsIdx = static_cast<uint16_t>(sPages[page].start);
}

static void ShopTablesLinked() {
  LogShops();
  sPages.clear();
  sPage = 0;
  if (xd::xg::Off(xd::xg::Feature::PrizeDesk) != nullptr) return;
  const bool xg = xd::xg::On();
  if (xg ? !xd::gen4::Loaded() : !xd::forms::Loaded()) return;
  if (shopGoods == nullptr || shopGoods_number == nullptr || shopData == nullptr || *shopGoods_number < xd::forms::ShopSpareRows() + 8) return;
  const uint32_t start = *shopGoods_number - xd::forms::ShopSpareRows();
  const uint32_t old = shopData[9].goodsIdx;
  if (old == start) return;
  const size_t kOwnCount = xg ? 21 : 18;
  std::vector<uint16_t> own, gen4goods;
  for (const uint16_t* p = shopGoods + old; *p != 0 && (own.size() + gen4goods.size()) < 140; p++) (own.size() < kOwnCount ? own : gen4goods).push_back(*p);
  std::vector<uint16_t> evo, stonesPage, memories;
  auto has = [&](uint32_t item) {
    for (const auto* l : {&own, &gen4goods, &evo, &stonesPage, &memories}) for (uint16_t g : *l) if (g == item) return true;
    return false;
  };
  auto add = [&](std::vector<uint16_t>& l, uint32_t item) { if (item != 0 && !has(item) && l.size() < 60) l.push_back(static_cast<uint16_t>(item)); };
  if (xg) for (uint16_t g : xd::gen4::DeskItems()) add(evo, g);
  if (xd::gen57::Loaded()) for (uint16_t g : xd::gen57::EvolutionShopItems()) add(evo, g);
  if (xd::gen57::Loaded()) {
    const uint32_t prism = xd::gen57::PrismScaleItem();
    if (prism != 0 && xd::rulegen::AtLeast(5)) add(evo, prism);
  }
  const uint32_t charm = xd::forms::Loaded() ? xd::forms::CharmItem() : 0;
  add(evo, charm);
  const uint32_t patch = xd::forms::Loaded() ? xd::forms::PatchItem() : 0;
  if (patch != 0 && xd::hidden::Enabled()) add(memories, patch);
  if (xd::forms::Loaded() && xd::hidden::Enabled()) for (uint32_t m : xd::forms::MemoryItems()) add(memories, m);
  size_t plates = 0;
  if (xd::forms::Loaded() && xd::rulegen::AtLeast(4)) {
    for (const auto& f : xd::forms::All()) {
      if (f.kind != xd::forms::Kind::Plate || f.stone == 0) continue;
      if (f.key.find("(Fairy)") != std::string::npos && !xd::modern::Loaded()) continue;
      add(memories, f.stone);
      plates++;
    }
  }
  size_t stones = 0;
  if (xd::forms::Loaded() && xd::forms::MegaRule()) {
    for (const auto& f : xd::forms::All()) {
      if (f.kind != xd::forms::Kind::Mega || f.stone == 0 || has(f.stone) || stonesPage.size() >= 60) continue;
      if (pokemonDB_GetTukamaetaFlag(static_cast<uint16_t>(f.base)) != 0) { stonesPage.push_back(static_cast<uint16_t>(f.stone)); stones++; }
    }
  }
  const size_t extras = gen4goods.size() + evo.size() + stonesPage.size() + memories.size();
  std::fprintf(stderr, "[forms] prize shop: %zu goods (charm %u, %zu Mega Stones, %zu Plates)\n", own.size() + extras, charm, stones, plates);
  if (extras == 0) return;
  struct Src { uint32_t msg; const std::vector<uint16_t>* list; };
  const Src srcs[5] = {{kPageMsg + 0, &own}, {kPageMsg + 4, &gen4goods}, {kPageMsg + 1, &evo}, {kPageMsg + 2, &stonesPage}, {kPageMsg + 3, &memories}};
  uint32_t row = start;
  const uint32_t limit = start + xd::forms::ShopSpareRows();
  for (const Src& sr : srcs) {
    if (sr.list->empty()) continue;
    if (row + sr.list->size() + 1 > limit) break;
    for (size_t i = 0; i < sr.list->size(); i++) shopGoods[row + i] = (*sr.list)[i];
    shopGoods[row + sr.list->size()] = 0;
    sPages.push_back({sr.msg, row, static_cast<uint32_t>(sr.list->size())});
    row += static_cast<uint32_t>(sr.list->size()) + 1;
  }
  SetPage(0);
  std::fprintf(stderr, "[forms] prize shop pages: %zu (", sPages.size());
  for (const ShopPage& pg : sPages) std::fprintf(stderr, " %u", pg.count);
  std::fprintf(stderr, " )\n");
  LogShops("[shops after]");
  if (const char* on = std::getenv("XD_SHOP_LOG"); on != nullptr && on[0] == '1') {
    for (size_t i = 0; i < sPages.size(); i++) {
      std::string line;
      for (uint32_t k = 0; k < sPages[i].count; k++) line += " " + std::to_string(shopGoods[sPages[i].start + k]);
      std::fprintf(stderr, "[shops pages] page %zu (%u goods):%s\n", i + 1, sPages[i].count, line.c_str());
    }
  }
}

namespace {
struct ShopMenuArg {
  int index;
  uint16_t item, pad6;
  uint16_t* goods;
  int count;
  float* curPos;
  int* isScroll;
  float* rollUpTime;
  uint8_t mode, flags;
};
bool Paged(const ShopMenuArg* arg) {
  if (sPages.size() < 2 || arg == nullptr || shopGoods == nullptr) return false;
  const uint16_t* lo = shopGoods + sPages.front().start;
  const uint16_t* hi = shopGoods + sPages.back().start + sPages.back().count + 1;
  return arg->goods >= lo && arg->goods < hi;
}
}

extern "C" {
int PortXD_ShopCursorGame(void* w);
void PortXD_ShopOpenGame(int shop);
int PortXD_ShopTitleGame(void* w, void* sp);
int32_t GSmsgPrint2(int32_t layer, int32_t x, int32_t y, uint32_t color, uint32_t msg);
int GSmsgGetRect(int msg);
bool winSeqCheckMove(int menuID);
uint32_t windowGetKeyTrg(void);
void menuSubCursorAnimeStart(void);
}
void menuPtPlaySe(uint32_t id);

extern "C" void menuShopOpen(int shop) {
  if (shop >= 9 && shop <= 11 && sPages.size() >= 2) SetPage(0);
  PortXD_ShopOpenGame(shop);
}

extern "C" int menuShopCursor(void* window) {
  auto* w = static_cast<WinWorkLayout*>(window);
  auto* arg = reinterpret_cast<ShopMenuArg*>(w->params[0]);
  if (Paged(arg) && *arg->curPos == 0.0f && !winSeqCheckMove(w->menuID)) {
    const uint32_t trg = windowGetKeyTrg();
    int dir = 0;
    if ((trg & 0x200) != 0) dir = -1;
    if ((trg & 0x400) != 0) dir = 1;
    if (dir != 0) {
      const int n = static_cast<int>(sPages.size());
      SetPage((sPage + dir + n) % n);
      arg->goods = shopGoods + sPages[sPage].start;
      arg->count = static_cast<int>(sPages[sPage].count);
      w->cursorX = 0;
      w->cursor = 0;
      *arg->isScroll = 0;
      menuSubCursorAnimeStart();
      menuPtPlaySe(0x11);
      std::fprintf(stderr, "[forms] prize desk page %d of %d (%u goods)\n", sPage + 1, n, sPages[sPage].count);
    }
  }
  return PortXD_ShopCursorGame(window);
}

extern "C" int menuShopDrawTitleText(void* window, void* sprite) {
  const int r = PortXD_ShopTitleGame(window, sprite);
  auto* w = static_cast<WinWorkLayout*>(window);
  auto* arg = reinterpret_cast<ShopMenuArg*>(w->params[0]);
  if (Paged(arg)) {
    static int dx = 99, dy = 353;
    static bool read = false;
    if (!read) { read = true; if (const char* e = std::getenv("XD_DESK_HEADER")) std::sscanf(e, "%d,%d", &dx, &dy); }
    const uint32_t msg = (0x7D0u << 20) | sPages[sPage].msg;
    const int width = static_cast<int>(static_cast<uint32_t>(GSmsgGetRect(static_cast<int>(msg))) >> 16);
    GSmsgPrint2(w->layerID, dx - width / 2, dy, 0xFFFFFFFFu, msg);
  }
  return r;
}
