#include "xd_challenges.h"
#include "xd_gen4.h"
#include "xd_starter.h"
#include "xd_xg.h"

#include <port_user_folder.h>

#include <game/bitfield.h>
#include <game/gstexture.h>
#include <game/winwork.h>

#include <gx_texture_decode.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
void* menuSpriteBiosGetPtr(uint32_t id);
void* menuSpriteBiosGetValPtr(void* sprite);
void* GSresGetResourceAllGroup(uint32_t id);
void* GSmemAllocAlign(uint32_t size, uint32_t align);
int32_t GStextureCreateFromMemory(void* tex, uint16_t width, uint32_t height, void* data, int32_t format, int32_t tlutFormat,
                                  uint32_t mipLevels);
void windowDrawSprite(int16_t x, int16_t y, void* ctx, uint16_t key, uint32_t flags, int32_t layer);
uint8_t pokemonDataBiosGetTokuseiDataId(void* data, uint16_t index);
uint32_t pokemonTokuseiDataBiosGetPtr(uint16_t index);
uint32_t pokemonTokuseiDataBiosGetName(uint32_t* data);
uint32_t pokemonTokuseiDataBiosGetDoc(uint32_t* data);
}

namespace {

constexpr int32_t kEeveeMenu = 0xAF;
constexpr uint32_t kExtBase = 30000, kExtMax = 3, kExtCount = 3 * kExtMax;
constexpr int kRowsMax = 5 + kExtMax;

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
int sLaidRows = 0;

bool sOn = false;
uint32_t sSpecies[kExtMax], sItem[kExtMax];
int Extra() { return sSpecies[2] != 0 && sItem[2] != 0 ? 3 : 2; }
int Rows() { return 5 + Extra(); }
bool sStartDone = false;
int sArmed = -1;
bool sSubstitute = false;
int sSubWhich = 0;

constexpr uint16_t kStandInItem = 95;
constexpr uint32_t kStandInSpecies = 136;
constexpr uint32_t kMsgPokemonValue = 0x4E, kMsgItemValue = 0x2D;
constexpr int kStandInPosition = 2;

uint16_t sFace[kExtMax] = {0, 0, 0};

bool Active() { return sOn && xd::challenges::Active("gen4_pokemon") && !xd::starter::Randomized() && (!sLaidOut || sLaidRows == Rows()); }

Item* Real(int idx) { return static_cast<Item*>(PortXD_RealMenuItemPtr(kEeveeMenu, static_cast<uint32_t>(idx))); }

void LayOut() {
  if (sLaidOut) return;
  Item* src[3] = {Real(kHighlight[4]), Real(kArrow[4]), Real(kButton[4])};
  const int16_t after[3] = {src[0]->next, src[1]->next, src[2]->next};
  const int baseY[3] = {Real(kHighlight[0])->y, Real(kArrow[0])->y, Real(kButton[0])->y};
  const int extra = Extra();
  for (int i = 1237; i <= 1254; i++) {
    Item* e = Real(i);
    if (e->y == 174) e->y = static_cast<int16_t>(e->y + extra * kStride);
    if (e->h == 122) e->h = static_cast<int16_t>(e->h + extra * kStride);
  }
  for (int i : {1256, 1257, 1258, 1262}) Real(i)->y = static_cast<int16_t>(Real(i)->y + extra * kStride);
  for (int r = 0; r < 5; r++) {
    Real(kHighlight[r])->y = static_cast<int16_t>(baseY[0] + r * kStride);
    Real(kArrow[r])->y = static_cast<int16_t>(baseY[1] + r * kStride);
    Real(kButton[r])->y = static_cast<int16_t>(baseY[2] + r * kStride);
  }
  for (int k = 0; k < 3; k++) {
    for (int r = 5; r < Rows(); r++) {
      const uint32_t at = static_cast<uint32_t>(k * kExtMax + (r - 5));
      Item& e = sExt[at];
      e = *src[k];
      e.y = static_cast<int16_t>(baseY[k] + r * kStride);
      e.flags &= static_cast<uint8_t>(~0x40);
      e.next = r < Rows() - 1 ? static_cast<int16_t>(kExtBase + at + 1) : after[k];
    }
    src[k]->next = static_cast<int16_t>(kExtBase + k * kExtMax);
  }
  sLaidOut = true;
  sLaidRows = Rows();
}

void DrawName(void* w, void* sprite, uint32_t item) {
  static char16_t upper[40];
  const char16_t* name = GSmsgGetGSchar(itemDataBiosGetName(itemDataBiosGetPtr(static_cast<uint16_t>(item))));
  size_t n = 0;
  for (; name != nullptr && name[n] != 0 && n < 39; n++) upper[n] = (!xd::xg::On() && name[n] >= 'a' && name[n] <= 'z') ? static_cast<char16_t>(name[n] - 32) : name[n];
  upper[n] = 0;
  msgctrlSetValue(0x37, reinterpret_cast<uintptr_t>(upper));
  const uint8_t alpha = reinterpret_cast<const uint8_t*>(&static_cast<WinWorkLayout*>(w)->pad_90)[3];
  GSmsgPrint2(winSpriteGetLayerID(w, sprite), 0, 0, alpha | 0xFFFFFF00u, 0x4277);
}

void LoadFaces() {
  for (int k = 0; k < Extra(); k++) {
    sFace[k] = static_cast<uint16_t>(pokemonGetStatus(nullptr, static_cast<uint16_t>(sSpecies[k]), 0x5B, 0));
    if (sFace[k] != 0) menuFaceLoadFromID(sFace[k], nullptr, nullptr);
  }
  for (int tries = 0; tries < 900; tries++) {
    bool ready = true;
    for (int k = 0; k < Extra(); k++) ready = ready && (sFace[k] == 0 || menuFaceSearchEnable(sFace[k]) == 1);
    if (ready) return;
    GSthreadSwitch();
  }
}

struct MenuSpriteView {
  GC_BITS4(uint16_t last : 1, uint16_t fit : 1, uint16_t type : 2, uint16_t seq : 12)
  uint8_t pad2;
  int8_t offX, offY;
  uint8_t alpha;
};
struct MenuSpriteValView {
  int16_t u, v, w, h;
  uint32_t tex;
};

void DumpSprites() {
  static bool done = false;
  const char* dir = std::getenv("XD_EEVEE_DUMP");
  if (done || dir == nullptr) return;
  done = true;
  for (uint32_t key : {0x220u, 0x21Eu, 0x21Fu, 0x21Cu, 0x21Du}) {
    const auto* info = static_cast<const MenuSpriteView*>(menuSpriteBiosGetPtr(key));
    for (int piece = 0; info != nullptr && piece < 8; piece++) {
      const auto* val = static_cast<const MenuSpriteValView*>(menuSpriteBiosGetValPtr(const_cast<MenuSpriteView*>(info)));
      std::fprintf(stderr, "[eevee dump] sprite %x piece %d: type %d last %d off %d,%d alpha %d  uv %d,%d size %d x %d tex %08x\n", key, piece,
                   info->type, info->last, info->offX, info->offY, info->alpha, val->u, val->v, val->w, val->h, val->tex);
      const auto* tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(val->tex));
      if (tex != nullptr) {
        const uint32_t w = GXGetTexObjWidth(&tex->texObj), h = GXGetTexObjHeight(&tex->texObj), fmt = GXGetTexObjFmt(&tex->texObj);
        std::fprintf(stderr, "[eevee dump]   texture %u x %u format %u\n", w, h, fmt);
        std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
        if (PortGX_DecodeTexture(GXGetTexObjData(&tex->texObj), w, h, fmt, tex->tlutData, 2, rgba.data())) {
          char name[512];
          std::snprintf(name, sizeof name, "%s/%x_%d.rgba", dir, key, piece);
          if (FILE* f = std::fopen(name, "wb")) {
            std::fwrite(&w, 4, 1, f), std::fwrite(&h, 4, 1, f), std::fwrite(rgba.data(), 1, rgba.size(), f);
            std::fclose(f);
          }
        }
      }
      if (info->last) break;
      info = static_cast<const MenuSpriteView*>(menuSpriteBiosGetPtr(info->seq));
    }
  }
}

constexpr uint32_t kOfferTexBase = 0x7F4E0000;
constexpr uint16_t kStandInSprite = 0x21F;

struct OfferPicture {
  bool tried = false;
  void* tex = nullptr;
};
OfferPicture sPicture[kExtMax];

std::string OfferFile(uint32_t species) {
  return std::string(PortUser_FolderUtf8("Pokemon XD")) + "/icons/offer/offer_" + std::to_string(species) + ".rgba";
}

void LoadOfferPicture(int k) {
  OfferPicture& op = sPicture[k];
  if (op.tried) return;
  op.tried = true;
  const std::string path = OfferFile(sSpecies[k]);
  std::ifstream f(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())), std::ios::binary);
  if (!f) return;
  uint32_t head[3] = {};
  std::vector<uint8_t> px;
  bool ok = static_cast<bool>(f.read(reinterpret_cast<char*>(head), sizeof head)) && head[0] == 0x3152464Fu && head[1] >= 4 &&
            head[2] >= 4 && head[1] <= 256 && head[2] <= 256;
  if (ok) {
    px.resize(static_cast<size_t>(head[1]) * head[2] * 4);
    ok = static_cast<bool>(f.read(reinterpret_cast<char*>(px.data()), static_cast<std::streamsize>(px.size())));
  }
  if (!ok) {
    std::fprintf(stderr, "[gen4] the Eevee offer picture %s is not readable: the face is shown instead\n", path.c_str());
    return;
  }
  {
    const uint32_t xs[4] = {8, head[1] - 9, 8, head[1] - 9}, ys[4] = {8, 8, head[2] - 9, head[2] - 9};
    int bright = 0;
    for (int i = 0; i < 4; i++) {
      const uint8_t* p = &px[(static_cast<size_t>(ys[i]) * head[1] + xs[i]) * 4];
      bright += p[0] > 200 && p[1] > 200 && p[2] > 200 && p[3] > 200;
    }
    if (bright == 4) {
      std::fprintf(stderr, "[gen4] the Eevee offer picture %s has a white background (a bad capture): the face is shown instead\n", path.c_str());
      return;
    }
  }
  const uint32_t w = (head[1] + 3) & ~3u, h = (head[2] + 3) & ~3u;
  uint8_t* mem = static_cast<uint8_t*>(GSmemAllocAlign(w * h * 4, 32));
  void* rec = GSmemAllocAlign(sizeof(GStexture), 32);
  if (mem == nullptr || rec == nullptr) return;
  std::memset(mem, 0, static_cast<size_t>(w) * h * 4);
  for (uint32_t ty = 0; ty < h / 4; ty++) {
    for (uint32_t tx = 0; tx < w / 4; tx++) {
      uint8_t* block = mem + (static_cast<size_t>(ty) * (w / 4) + tx) * 64;
      for (uint32_t i = 0; i < 16; i++) {
        const uint32_t x = tx * 4 + i % 4, y = ty * 4 + i / 4;
        if (x >= head[1] || y >= head[2]) continue;
        const uint8_t* p = &px[(static_cast<size_t>(y) * head[1] + x) * 4];
        block[i * 2] = p[3], block[i * 2 + 1] = p[0];
        block[32 + i * 2] = p[1], block[32 + i * 2 + 1] = p[2];
      }
    }
  }
  if (GStextureCreateFromMemory(rec, static_cast<uint16_t>(w), h, mem, 0x45, 0, 0) == 0) return;
  op.tex = rec;
  std::fprintf(stderr, "[gen4] the Eevee offer picture of species %u: %s (%u x %u)\n", sSpecies[k], path.c_str(), head[1], head[2]);
}

bool DrawOfferPicture(void* window, void* sprite, int k) {
  LoadOfferPicture(k);
  if (sPicture[k].tex == nullptr) return false;
  auto* info = static_cast<MenuSpriteView*>(menuSpriteBiosGetPtr(kStandInSprite));
  if (info == nullptr || info->type != 1 || !info->last) return false;
  auto* val = static_cast<MenuSpriteValView*>(menuSpriteBiosGetValPtr(info));
  const MenuSpriteValView saved = *val;
  val->tex = kOfferTexBase + static_cast<uint32_t>(k);
  val->u = 0, val->v = 0;
  windowDrawSprite(0, 0, window, kStandInSprite, 0, winSpriteGetLayerID(window, sprite));
  *val = saved;
  return true;
}

struct Size2 { int16_t x, y; };

}

namespace xd::gen4 {

bool EeveeOfferSpecies(uint32_t& leaf, uint32_t& ice, uint32_t* sylveon) {
  if (!sOn || sSpecies[0] == 0 || sSpecies[1] == 0) return false;
  leaf = sSpecies[0], ice = sSpecies[1];
  if (sylveon != nullptr) *sylveon = Extra() == 3 ? sSpecies[2] : 0;
  return true;
}

void SetEeveeChoices(bool on, uint32_t leafSpecies, uint32_t leafItem, uint32_t iceSpecies, uint32_t iceItem) {
  sOn = on;
  sSpecies[0] = leafSpecies, sSpecies[1] = iceSpecies, sSpecies[2] = 0;
  sItem[0] = leafItem, sItem[1] = iceItem, sItem[2] = 0;
}

void SetEeveeSylveon(uint32_t species, uint32_t item) {
  if (!sOn) return;
  sSpecies[2] = species, sItem[2] = item;
  std::fprintf(stderr, "[gen4] Eevee offer 8 choices (Sylveon, item %u)\n", item);
}

}

extern "C" void* PortXD_VsRowItem(int32_t menuID, uint32_t index);
extern "C" void* PortXD_PcExtItem(uint32_t index);
extern "C" void* menuItemBiosGetPtr(int32_t menuID, uint32_t index) {
  if (menuID == 0x118) {
    if (void* own = PortXD_VsRowItem(menuID, index)) return own;
  }
  if (index >= 31000) {
    if (void* pc = PortXD_PcExtItem(index)) return pc;
  }
  if (sLaidOut && index >= kExtBase && index < kExtBase + kExtCount) return &sExt[index - kExtBase];
  return PortXD_RealMenuItemPtr(menuID, index);
}

extern "C" int32_t menuScriptEevee(void) {
  if (sOn && xd::challenges::Active("gen4_pokemon") && xd::starter::Randomized()) {
    static bool said = false;
    if (!said) std::fprintf(stderr, "[gen4] Eevee offer: the starter is randomized, the game's own five choices are shown\n"), said = true;
  }
  if (!Active()) return PortXD_RealEevee();
  LayOut();
  LoadFaces();
  sStartDone = false;
  sArmed = -1;
  const int32_t pick = PortXD_RealEevee();
  if (pick >= 5 && pick < Rows()) {
    sArmed = pick - 5;
    return kStandInPosition;
  }
  return pick;
}

bool AbilityTextThere(uint16_t species, bool description) {
  void* pd = pokemonDataBiosGetPtr(species);
  if (pd == nullptr) return false;
  const uint8_t tid = pokemonDataBiosGetTokuseiDataId(pd, 0);
  if (tid == 0) return true;
  const uint32_t row = pokemonTokuseiDataBiosGetPtr(tid);
  if (row == 0) return false;
  auto* rowPtr = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(row));
  const uint32_t msg = description ? pokemonTokuseiDataBiosGetDoc(rowPtr) : pokemonTokuseiDataBiosGetName(rowPtr);
  return GSmsgGetGSchar(msg) != nullptr;
}

constexpr uint16_t kGameRowSpecies[5] = {0x86, 0x87, 0x88, 0xC5, 0xC4};

extern "C" void menuScriptEeveeHook(void* wv, void* sv) {
  auto* w = static_cast<WinWorkLayout*>(wv);
  auto* sp = static_cast<SpriteWorkLayout*>(sv);
  if (w->menuID == kEeveeMenu && (sp->id == 0x4FE || sp->id == 0x4FF)) {
    const int row = w->cursor;
    const uint16_t species = row >= 0 && row < 5 ? kGameRowSpecies[row]
                             : row >= 5 && row < Rows() && Active() ? static_cast<uint16_t>(sSpecies[row - 5]) : 0;
    if (species != 0 && !AbilityTextThere(species, sp->id == 0x4FF)) {
      static bool said = false;
      if (!said) {
        said = true;
        std::fprintf(stderr, "[gen4] Eevee offer: species %u's ability has no %s text here: that line is left empty\n", species,
                     sp->id == 0x4FF ? "description" : "name");
      }
      return;
    }
  }
  if (!Active() || w->menuID != kEeveeMenu) {
    PortXD_RealEeveeHook(wv, sv);
    return;
  }
  DumpSprites();
  static const char* testPos = std::getenv("XD_EEVEE_TEST_POS");
  if (testPos != nullptr) {
    static int frames = 0;
    if (sp->id == 0x4EB) frames++;
    int n = 1;
    for (const char* c = testPos; *c != 0; c++) n += *c == ',';
    int index = n > 1 ? (frames / 40) % n : 0;
    const char* c = testPos;
    while (index-- > 0 && (c = std::strchr(c, ',')) != nullptr) c++;
    w->cursor = static_cast<int16_t>(c != nullptr ? std::atoi(c) : 0);
  }
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
    const uint32_t kind = k / kExtMax, j = k % kExtMax;
    if (kind == 0) {
      winSpriteSetDisp(sv, pos == 5 + static_cast<int>(j));
    } else if (kind == 2) {
      DrawName(wv, sv, sItem[j]);
    }
    return;
  }
  if (pos >= 5) {
    if (id == 0x4EB) {
      if (DrawOfferPicture(wv, sv, pos - 5)) return;
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

extern "C" void* PortXD_ResLookup(uint32_t id) {
  if (id >= kOfferTexBase && id < kOfferTexBase + kExtMax) return sPicture[id - kOfferTexBase].tex;
  return GSresGetResourceAllGroup(id);
}
