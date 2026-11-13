#include "xd_challenges.h"
#include "xd_gen4.h"

#include <game/winwork.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int32_t PortXD_RealEevee(void);
void PortXD_RealEeveeHook(void* window, void* sprite);
void* PortXD_RealMenuItemPtr(int32_t menuID, uint32_t index);
void* PortXD_EeveeDataPtr(uint16_t species);
void* pokemonDataBiosGetPtr(uint16_t species);
uint32_t pokemonDataBiosGetName(void* data);
void* itemDataBiosGetPtr(uint16_t item);
uint32_t itemDataBiosGetName(void* itemData);
char16_t* GSmsgGetGSchar(uint32_t key);
void msgctrlSetValue(int32_t id, uintptr_t value);
void GSmsgPrint2(int32_t layer, int32_t x, int32_t y, uint32_t color, uint32_t msg);
int32_t winSpriteGetLayerID(void* window, void* sprite);
void winSpriteSetDisp(void* sprite, bool on);
int32_t scriptGetItem(int32_t item, int32_t count);
uintptr_t pokemonGetStatus(void* pokemon, uint16_t id, uint16_t kind, uint16_t index);
int32_t menuFaceLoadFromID(uint16_t id, void* fn, void* arg);
int8_t menuFaceSearchEnable(uint16_t id);
int32_t menuFaceDispFromID3(void* window, void* sprite, uint16_t id, void* size, void* offset);
void GSthreadSwitch(void);
}

namespace {

constexpr int32_t kEeveeMenu = 0xAF;
constexpr uint32_t kExtBase = 30000, kExtCount = 6;
constexpr int kRows = 7;

struct Item {
  uint8_t flags;
  uint8_t flags2;
  uint8_t unk2, unk3;
  uint16_t imageId;
  int16_t x, y, w, h;
  int16_t spriteId;
  uint32_t msgId;
  void* hook;
  int16_t next;
  int16_t unk1A;
};
static_assert(sizeof(Item) == 0x28, "the item table's entry");

constexpr int kHighlight[5] = {1260, 1274, 1275, 1276, 1277};
constexpr int kArrow[5] = {1261, 1270, 1271, 1272, 1273};
constexpr int kButton[5] = {1263, 1266, 1267, 1268, 1269};
constexpr uint16_t kHighlightIds[5] = {0x4EC, 0x4FA, 0x4FB, 0x4FC, 0x4FD};
constexpr uint16_t kButtonIds[5] = {0x4EF, 0x4F2, 0x4F3, 0x4F4, 0x4F5};

constexpr int kStride = 26;

Item sExt[kExtCount];
bool sLaidOut = false;

bool sOn = false;
uint32_t sSpecies[2], sItem[2];
bool sStartDone = false;
int sArmed = -1;
bool sSubstitute = false;
int sSubWhich = 0;

constexpr uint16_t kStandInItem = 95;
constexpr uint32_t kStandInSpecies = 136;
constexpr uint32_t kMsgPokemonValue = 0x4E, kMsgItemValue = 0x2D;
constexpr int kStandInPosition = 2;

uint16_t sFace[2] = {0, 0};

bool Active() { return sOn && xd::challenges::Active("gen4_rules"); }

Item* Real(int idx) { return static_cast<Item*>(PortXD_RealMenuItemPtr(kEeveeMenu, static_cast<uint32_t>(idx))); }

void LayOut() {
  if (sLaidOut) return;
  Item* src[3] = {Real(kHighlight[4]), Real(kArrow[4]), Real(kButton[4])};
  const int16_t after[3] = {src[0]->next, src[1]->next, src[2]->next};
  const int baseY[3] = {Real(kHighlight[0])->y, Real(kArrow[0])->y, Real(kButton[0])->y};
  for (int i = 1237; i <= 1254; i++) {
    Item* e = Real(i);
    if (e->y == 174) e->y = static_cast<int16_t>(e->y + 2 * kStride);
    if (e->h == 122) e->h = static_cast<int16_t>(e->h + 2 * kStride);
  }
  for (int i : {1256, 1257, 1258, 1262}) Real(i)->y = static_cast<int16_t>(Real(i)->y + 2 * kStride);
  for (int r = 0; r < 5; r++) {
    Real(kHighlight[r])->y = static_cast<int16_t>(baseY[0] + r * kStride);
    Real(kArrow[r])->y = static_cast<int16_t>(baseY[1] + r * kStride);
    Real(kButton[r])->y = static_cast<int16_t>(baseY[2] + r * kStride);
  }
  for (int k = 0; k < 3; k++) {
    for (int r = 5; r < kRows; r++) {
      const uint32_t at = static_cast<uint32_t>(k * 2 + (r - 5));
      Item& e = sExt[at];
      e = *src[k];
      e.y = static_cast<int16_t>(baseY[k] + r * kStride);
      e.flags &= static_cast<uint8_t>(~0x40);
      e.next = r == 5 ? static_cast<int16_t>(kExtBase + at + 1) : after[k];
    }
    src[k]->next = static_cast<int16_t>(kExtBase + k * 2);
  }
  sLaidOut = true;
}

void DrawName(void* w, void* sprite, uint32_t item) {
  static char16_t upper[40];
  const char16_t* name = GSmsgGetGSchar(itemDataBiosGetName(itemDataBiosGetPtr(static_cast<uint16_t>(item))));
  size_t n = 0;
  for (; name != nullptr && name[n] != 0 && n < 39; n++) upper[n] = (name[n] >= 'a' && name[n] <= 'z') ? static_cast<char16_t>(name[n] - 32) : name[n];
  upper[n] = 0;
  msgctrlSetValue(0x37, reinterpret_cast<uintptr_t>(upper));
  const uint8_t alpha = reinterpret_cast<const uint8_t*>(&static_cast<WinWorkLayout*>(w)->pad_90)[3];
  GSmsgPrint2(winSpriteGetLayerID(w, sprite), 0, 0, alpha | 0xFFFFFF00u, 0x4277);
}

void LoadFaces() {
  for (int k = 0; k < 2; k++) {
    sFace[k] = static_cast<uint16_t>(pokemonGetStatus(nullptr, static_cast<uint16_t>(sSpecies[k]), 0x5B, 0));
    if (sFace[k] != 0) menuFaceLoadFromID(sFace[k], nullptr, nullptr);
  }
  for (int tries = 0; tries < 900; tries++) {
    bool ready = true;
    for (int k = 0; k < 2; k++) ready = ready && (sFace[k] == 0 || menuFaceSearchEnable(sFace[k]) == 1);
    if (ready) return;
    GSthreadSwitch();
  }
}

struct Size2 { int16_t x, y; };

}

namespace xd::gen4 {

void SetEeveeChoices(bool on, uint32_t leafSpecies, uint32_t leafItem, uint32_t iceSpecies, uint32_t iceItem) {
  sOn = on;
  sSpecies[0] = leafSpecies, sSpecies[1] = iceSpecies;
  sItem[0] = leafItem, sItem[1] = iceItem;
}

}

extern "C" void* menuItemBiosGetPtr(int32_t menuID, uint32_t index) {
  if (sLaidOut && index >= kExtBase && index < kExtBase + kExtCount) return &sExt[index - kExtBase];
  return PortXD_RealMenuItemPtr(menuID, index);
}

extern "C" int32_t menuScriptEevee(void) {
  if (!Active()) return PortXD_RealEevee();
  LayOut();
  LoadFaces();
  sStartDone = false;
  sArmed = -1;
  const int32_t pick = PortXD_RealEevee();
  if (pick >= 5 && pick < kRows) {
    sArmed = pick - 5;
    return kStandInPosition;
  }
  return pick;
}

extern "C" void menuScriptEeveeHook(void* wv, void* sv) {
  auto* w = static_cast<WinWorkLayout*>(wv);
  auto* sp = static_cast<SpriteWorkLayout*>(sv);
  if (!Active() || w->menuID != kEeveeMenu) {
    PortXD_RealEeveeHook(wv, sv);
    return;
  }
  static const char* testPos = std::getenv("XD_EEVEE_TEST_POS");
  if (testPos != nullptr) w->cursor = static_cast<int16_t>(std::atoi(testPos));
  static const char* testStart = std::getenv("XD_EEVEE_TEST_START");
  if (testStart != nullptr && !sStartDone) {
    w->cursor = static_cast<int16_t>(std::atoi(testStart));
    sStartDone = true;
  }
  const int pos = w->cursor;
  const uint16_t id = static_cast<uint16_t>(sp->id);
  for (int r = 0; r < 5; r++) {
    if (id == kHighlightIds[r]) {
      winSpriteSetDisp(sv, pos == r);
      return;
    }
  }
  if (id >= kExtBase && id < kExtBase + kExtCount) {
    const uint32_t k = id - kExtBase;
    if (k < 2) {
      winSpriteSetDisp(sv, pos == 5 + static_cast<int>(k));
    } else if (k >= 4) {
      DrawName(wv, sv, sItem[k - 4]);
    }
    return;
  }
  if (pos >= 5) {
    if (id == 0x4EB) {
      Size2 size{80, 80}, at{15, 20};
      if (sFace[pos - 5] != 0) menuFaceDispFromID3(wv, sv, sFace[pos - 5], &size, &at);
      return;
    }
    sSubstitute = true;
    sSubWhich = pos - 5;
    w->cursor = static_cast<int16_t>(kStandInPosition);
    PortXD_RealEeveeHook(wv, sv);
    w->cursor = static_cast<int16_t>(pos);
    sSubstitute = false;
    return;
  }
  PortXD_RealEeveeHook(wv, sv);
}

extern "C" void* PortXD_EeveeDataPtr(uint16_t species) {
  if (sSubstitute && species == kStandInSpecies) return pokemonDataBiosGetPtr(static_cast<uint16_t>(sSpecies[sSubWhich]));
  return pokemonDataBiosGetPtr(species);
}

extern "C" void PortXD_EeveeMsgValue(int32_t id, uintptr_t value) {
  if (sArmed >= 0) {
    if (id == static_cast<int32_t>(kMsgItemValue) && value == kStandInItem) value = sItem[sArmed];
    else if (id == static_cast<int32_t>(kMsgPokemonValue) && value == kStandInSpecies) value = sSpecies[sArmed];
  }
  msgctrlSetValue(id, value);
}

extern "C" int32_t PortXD_EeveeGetItem(int32_t item, int32_t count) {
  if (sArmed >= 0 && item == kStandInItem) {
    const uint32_t real = sItem[sArmed];
    sArmed = -1;
    return scriptGetItem(static_cast<int32_t>(real), count);
  }
  return scriptGetItem(item, count);
}
