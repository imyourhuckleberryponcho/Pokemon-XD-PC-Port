#include "xd_playthrough.h"
#include "xd_challenges.h"
#include "xd_randomizer.h"
#include "xd_user_folder.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>

#include <gx_texture_decode.h>
#include <port_audio.h>
#include <port_menu.h>
#include <port_sfx.h>
#include <port_video.h>
#include <settings.h>

#include <aurora/imgui.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <game/bitfield.h>
#include <game/gstexture.h>
#include <game/pxdvs/GSAPI/GSsnd/sndData.h>
#include <game/winwork.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <chrono>
#include <random>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
void winSpriteDraw(void* window, void* sprite);
s32 GSmsgPrint2(s32 layer, s32 x, s32 y, u32 color, u32 msg);
void* windowSearchID(s32 id);
s32 windowGetActiveID(void);
void* GSresGetResourceAllGroup(u32 id);
uint32_t PortVI_GetFlipCount(void);
uint8_t* PortXD_GetDolTable(const char* name);
void PortInput_SetPadFilter(void (*filter)(PADStatus* status));
void PortInput_LatchHeld(void);
s16 menuTitleGetSelect(s32 index);
void* menuItemBiosGetPtr(s32 menuID, u32 index);
void* menuDataBiosGetPtr(s32 menuID);
void* menuSpriteBiosGetPtr(u32 id);
void* menuSpriteBiosGetValPtr(void* sprite);
void* menuSeBiosGetPtr(u32 id);
u16 menuSeBiosGetSndId(void* se, u8 kind);
extern void* snd_data;
extern u32* snd_data_number;
u8 sndPushGroup(void* prj, u16 gid, void* samples, void* sampdir, void* pool);
u8 GSsndPlayNow(u32 id);
s32 winSpriteGetLayerID(void* window, void* sprite);
void* GSgfxLayerFindByID(intptr_t id);
s32 GSfloorGetCurrent(void);
s32 floorGetNextChangeID(void);
int floorGetFloorState(void);
void PortXD_AudioCategoriesInit(void);
int PortXD_AudioCategoryVolumeChanged(const char* id, int percent);
}

namespace {

namespace es = engine::settings;

struct FieldMenu {
  s32 menuId;
  s16 exitIndex;
  s16 side, cap, button, bar, flash, text, hand;
};
constexpr FieldMenu kMenus[] = {{0x57, 4, 13, 14, 20, 25, 30, 35, 1647},
                                {0x58, 3, 700, 701, 706, 710, 714, 718, 1651},
                                {0x70, 4, 13, 14, 20, 25, 30, 35, 1647}};
constexpr int kRow = 36;

const FieldMenu* MenuOf(const void* window) {
  if (window == nullptr) return nullptr;
  const s32 id = static_cast<const WinWorkLayout*>(window)->menuID;
  for (const FieldMenu& m : kMenus) {
    if (m.menuId == id) return &m;
  }
  return nullptr;
}

constexpr s32 kTitleMenu = 0x116;
constexpr s16 kOptBox = 65, kOptFrame = 66, kOptText = 71, kTitleInfo = 73;
constexpr s16 kTitleOptions = 3;
constexpr u32 kOptionsInfoMsg = 0x42E3;
constexpr int kTitleDx = -166;
const char* kTitleInfoText = "Picture, sound and controls (PC version).";
constexpr uint16_t kTitleFont = 1;

bool IsTitleMenu(const void* window) {
  return window != nullptr && static_cast<const WinWorkLayout*>(window)->menuID == kTitleMenu;
}

bool sOn = false;
bool sSelected = false;
bool sTitleSelected = false;
bool sPageFromTitle = false;
enum MenuSound { kSoundMove = 1, kSoundChoose = 2, kSoundBack = 3 };
void PlayMenuSound(s32 menuId, int event);

struct Picture {
  ImTextureID id{};
  float w = 0, h = 0;
  bool ok = false;
};
std::map<const void*, Picture> sPictures;

Picture PictureOf(const GStexture* tex) {
  if (tex == nullptr) return {};
  const void* data = GXGetTexObjData(&tex->texObj);
  auto it = sPictures.find(data);
  if (it != sPictures.end()) return it->second;
  Picture p;
  const u16 w = GXGetTexObjWidth(&tex->texObj), h = GXGetTexObjHeight(&tex->texObj);
  const u32 fmt = GXGetTexObjFmt(&tex->texObj);
  const u32 tlutFmt = tex->tlutFormat == 2 ? GX_TL_RGB565 : tex->tlutFormat == 3 ? GX_TL_RGB5A3 : GX_TL_IA8;
  std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
  if (data != nullptr && w > 0 && h > 0 && PortGX_DecodeTexture(data, w, h, fmt, tex->tlutData, tlutFmt, rgba.data())) {
    p.id = aurora_imgui_add_texture(w, h, rgba.data());
    p.w = w, p.h = h, p.ok = true;
  } else {
    std::fprintf(stderr, "[settings menu] a field menu picture in format %u could not be read\n", fmt);
  }
  sPictures[data] = p;
  return p;
}

std::map<u32, Picture> sPicturesById;

Picture PictureById(u32 id) {
  auto it = sPicturesById.find(id);
  if (it != sPicturesById.end()) return it->second;
  const auto* tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(id));
  if (tex == nullptr) return {};
  const Picture p = PictureOf(tex);
  if (p.ok) sPicturesById[id] = p;
  return p;
}

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

struct Glyph {
  ImTextureID id{};
  int w = 0, h = 0, yofs = 0;
  bool ok = false;
};
struct Font {
  std::vector<const uint8_t*> infos;
  int w = 22;
  std::map<uint16_t, Glyph> glyphs;
  bool searched = false;
};
std::map<uint16_t, Font> sFonts;
uint16_t sFontId = 0;
#define sFontInfos (sFonts[sFontId].infos)
#define sFontW (sFonts[sFontId].w)
#define sGlyphs (sFonts[sFontId].glyphs)

struct UseFont {
  uint16_t before;
  explicit UseFont(uint16_t id) : before(sFontId) { sFontId = id; }
  ~UseFont() { sFontId = before; }
};

void FindFont() {
  Font& f = sFonts[sFontId];
  if (f.searched) return;
  f.searched = true;
  for (const char* name : {"resident_fnt", "common_fnt"}) {
    const uint8_t* blk = PortXD_GetDolTable(name);
    for (int guard = 0; blk != nullptr && guard < 64; guard++) {
      const uint16_t id = Be16(blk);
      if (id == 0xFFFF) break;
      if (id == sFontId) {
        f.w = blk[2];
        f.infos.push_back(blk + 8);
      }
      const uint32_t next = Be32(blk + 4);
      if (next == 0) break;
      blk += next;
    }
  }
}

const Glyph* GlyphOf(uint16_t code) {
  auto it = sGlyphs.find(code);
  if (it != sGlyphs.end()) return it->second.ok ? &it->second : nullptr;
  Glyph g;
  FindFont();
  for (const uint8_t* info : sFontInfos) {
    const uint16_t count = Be16(info);
    const uint32_t dataOff = Be32(info + 4);
    const uint8_t* entries = info + 16;
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) / 2;
      const uint8_t* e = entries + mid * 8;
      const uint16_t c = Be16(e);
      if (c == code) {
        g.w = e[2], g.h = e[3];
        const uint32_t bits = Be32(e + 4);
        g.yofs = static_cast<int8_t>(bits >> 24);
        const uint8_t* src = info + dataOff + (bits & 0xFFFFFF);
        const int rowBytes = ((g.w + 1) & ~1) / 2;
        std::vector<uint8_t> rgba(static_cast<size_t>(g.w) * g.h * 4);
        for (int y = 0; y < g.h; y++) {
          for (int x = 0; x < g.w; x++) {
            const uint8_t b = src[y * rowBytes + x / 2];
            const uint8_t v = static_cast<uint8_t>(((x & 1) ? (b & 15) : (b >> 4)) * 17);
            uint8_t* p = &rgba[(static_cast<size_t>(y) * g.w + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = v;
          }
        }
        if (g.w > 0 && g.h > 0) {
          g.id = aurora_imgui_add_texture(static_cast<uint32_t>(g.w), static_cast<uint32_t>(g.h), rgba.data());
          g.ok = true;
        }
        break;
      }
      if (c < code) lo = mid + 1;
      else hi = mid;
    }
    if (g.ok) break;
  }
  sGlyphs[code] = g;
  return g.ok ? &sGlyphs[code] : nullptr;
}

int TextWidth(const char* text) {
  int w = 0;
  for (const char* c = text; *c != '\0'; c++) {
    if (*c == ' ') {
      w += sFontW / 2;
    } else if (const Glyph* g = GlyphOf(static_cast<uint8_t>(*c))) {
      w += g->w;
    } else {
      w += sFontW;
    }
  }
  return w - 1;
}

struct Screen {
  float x = 0, y = 0, s = 1;
  ImVec2 At(float px, float py) const { return ImVec2(x + px * s, y + py * s); }
};

Screen ScreenNow() {
  Screen sc;
  PortMenu_PictureRect(&sc.x, &sc.y, &sc.s);
  return sc;
}

ImU32 Rgba(const uint8_t* c) { return IM_COL32(c[0], c[1], c[2], c[3]); }

void DrawText(ImDrawList* dl, const Screen& sc, float x, float y, const char* text, const uint8_t rgba[4],
              float squeeze = 1.f, float scale = 1.f) {
  const uint8_t shadowA = static_cast<uint8_t>(rgba[3] * 0xC0 / 0xFF);
  const float xs = squeeze * scale;
  float cx = x;
  for (const char* c = text; *c != '\0'; c++) {
    if (*c == ' ') {
      cx += static_cast<float>(sFontW / 2) * xs;
      continue;
    }
    const Glyph* g = GlyphOf(static_cast<uint8_t>(*c));
    if (g == nullptr) {
      cx += static_cast<float>(sFontW) * xs;
      continue;
    }
    const float gy = y + (static_cast<float>(g->yofs) - 2.f) * scale;
    const float gw = static_cast<float>(g->w) * xs, gh = static_cast<float>(g->h) * scale;
    dl->AddImage(g->id, sc.At(cx + 1, gy + 1), sc.At(cx + 1 + gw, gy + 1 + gh), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(0, 0, 0, shadowA));
    dl->AddImage(g->id, sc.At(cx, gy), sc.At(cx + gw, gy + gh), ImVec2(0, 0), ImVec2(1, 1), Rgba(rgba));
    cx += gw;
  }
}

struct Shot {
  SpriteWorkLayout sp{};
  s16 offX = 0, offY = 0;
  uint8_t winColor[4] = {255, 255, 255, 255};
  const GStexture* tex = nullptr;
  Picture pic;
  bool inset = false;
  bool valid = false;
  bool seen = false;
};

void UVOf(const SpriteWorkLayout& sp, const Picture& pic, float& s0, float& s1, float& t0, float& t1) {
  const int absW = std::abs(sp.w), absH = std::abs(sp.h);
  int left, width, top, height;
  if (sp.b76 == 0) {
    if (sp.uw != absW) {
      if (sp.uw < 0) left = sp.u, width = std::abs(sp.uw);
      else width = sp.uw - 2, left = sp.u + 1;
      width = std::max(width, 0);
    } else {
      left = sp.u, width = sp.uw;
    }
    if (sp.uh != absH) {
      if (sp.uh < 0) top = sp.v, height = std::abs(sp.uh);
      else height = sp.uh - 2, top = sp.v + 1;
      height = std::max(height, 0);
    } else {
      top = sp.v, height = sp.uh;
    }
  } else {
    left = sp.u, width = sp.uw, top = sp.v, height = sp.uh;
  }
  s0 = left / pic.w, s1 = (left + width) / pic.w;
  t0 = top / pic.h, t1 = (top + height) / pic.h;
}

void DrawSprite(ImDrawList* dl, const Screen& sc, const Shot& shot, float dy, const uint8_t* colorOverride = nullptr,
                float stretchW = 0.f, float atX = -1.f) {
  if (!shot.valid) return;
  const SpriteWorkLayout& sp = shot.sp;
  const Picture pic = shot.pic.ok ? shot.pic : PictureOf(shot.tex);
  if (!pic.ok) return;
  uint8_t c[4];
  const uint8_t* sc4 = colorOverride != nullptr ? colorOverride : sp.color;
  for (int i = 0; i < 4; i++) c[i] = static_cast<uint8_t>(sc4[i] * shot.winColor[i] / 255);
  if (c[3] == 0) return;
  const int absW = std::abs(sp.w), absH = std::abs(sp.h);
  float s0, s1, t0, t1;
  UVOf(sp, pic, s0, s1, t0, t1);
  if (shot.inset) s0 += 0.5f / pic.w, s1 -= 0.5f / pic.w, t0 += 0.5f / pic.h, t1 -= 0.5f / pic.h;
  float px = sp.x, py = sp.y, w = static_cast<float>(absW), h = static_cast<float>(absH);
  switch (sp.b76) {
  case 1: px = sp.x + (absW - sp.uw) / 2; py = sp.y + (absH - sp.uh) / 2; w = sp.uw, h = sp.uh; break;
  case 2: py = sp.y + (absH - sp.uh); w = sp.uw, h = sp.uh; break;
  case 3: px = sp.x + (absW - sp.uw); py = sp.y + (absH - sp.uh); w = sp.uw, h = sp.uh; break;
  default: break;
  }
  if (stretchW > 0.f) w = stretchW;
  if (atX >= 0.f) px = atX - shot.offX;
  px += shot.offX;
  py += shot.offY + dy;
  const float cx = w * 0.5f + px, cy = h * 0.5f + py;
  float xl = px - cx, xr = px + w - cx, yt = py - cy, yb = py + h - cy;
  if (sp.w < 0) std::swap(xl, xr);
  if (sp.h < 0) std::swap(yt, yb);
  const float cr = std::cos(sp.rot), sr = std::sin(sp.rot);
  auto corner = [&](float x, float y) {
    const float rx = x * cr + y * sr, ry = -x * sr + y * cr;
    return sc.At(cx + rx * sp.sx, cy + ry * sp.sy);
  };
  dl->AddImageQuad(pic.id, corner(xl, yb), corner(xl, yt), corner(xr, yt), corner(xr, yb), ImVec2(s0, t1),
                   ImVec2(s0, t0), ImVec2(s1, t0), ImVec2(s1, t1), Rgba(c));
}

void DrawSliced(ImDrawList* dl, const Screen& sc, const Shot& shot, float x, float y, float w, float edgeL, float edgeR,
                const uint8_t* colorOverride = nullptr) {
  if (!shot.seen) return;
  const SpriteWorkLayout& sp = shot.sp;
  const Picture pic = shot.pic.ok ? shot.pic : PictureOf(shot.tex);
  if (!pic.ok) return;
  uint8_t c[4];
  const uint8_t* sc4 = colorOverride != nullptr ? colorOverride : sp.color;
  for (int i = 0; i < 4; i++) c[i] = static_cast<uint8_t>(sc4[i] * shot.winColor[i] / 255);
  if (c[3] == 0) return;
  float s0, s1, t0, t1;
  UVOf(sp, pic, s0, s1, t0, t1);
  if (shot.inset) s0 += 0.5f / pic.w, s1 -= 0.5f / pic.w, t0 += 0.5f / pic.h, t1 -= 0.5f / pic.h;
  const float sw = static_cast<float>(std::abs(sp.w)), h = static_cast<float>(std::abs(sp.h));
  const float du = (s1 - s0) / sw;
  const float xs[4] = {x, x + edgeL, x + w - edgeR, x + w};
  const float us[4] = {s0, s0 + edgeL * du, s1 - edgeR * du, s1};
  for (int k = 0; k < 3; k++) {
    dl->AddImage(pic.id, sc.At(xs[k], y), sc.At(xs[k + 1], y + h), ImVec2(us[k], t0), ImVec2(us[k + 1], t1), Rgba(c));
  }
}

int sLogMenu = -1;
std::map<int, bool> sLoggedSprites;

void LogSprite(const WinWorkLayout* w, const SpriteWorkLayout* sp) {
  if (sLogMenu < 0 || w == nullptr || w->menuID != sLogMenu) return;
  const int key = sp->id * 8 + (sp->kind & 7);
  if (sLoggedSprites[key]) return;
  sLoggedSprites[key] = true;
  const auto* tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(sp->tex2));
  int tw = 0, th = 0, fmt = -1;
  if (tex != nullptr) {
    tw = GXGetTexObjWidth(&tex->texObj), th = GXGetTexObjHeight(&tex->texObj), fmt = (int)GXGetTexObjFmt(&tex->texObj);
  }
  std::fprintf(stderr,
               "[menu sprites] menu 0x%X id %d kind %d flags 0x%X pos %d,%d size %d,%d tex2 0x%X (%dx%d fmt %d) uv %d,%d "
               "%dx%d colour %02X%02X%02X%02X b76 %d off %d,%d winA %d msg 0x%X\n",
               w->menuID, sp->id, sp->kind, sp->flags, sp->x, sp->y, sp->w, sp->h, sp->tex2, tw, th, fmt, sp->u, sp->v, sp->uw,
               sp->uh, sp->color[0], sp->color[1], sp->color[2], sp->color[3], sp->b76, w->offX, w->offY,
               reinterpret_cast<const uint8_t*>(&w->pad_90)[3], static_cast<unsigned>(sp->msg));
}

struct MenuItemView {
  GC_BITS4(u8 selectable : 1, u8 last : 1, u8 spriteType : 2, u8 low : 4)
  GC_BITS2(u8 top2 : 2, u8 rest1 : 6)
  u8 alpha;
  u8 pad3;
  s16 spriteID;
  s16 x, y, w, h;
  s16 cursorSprite;
  void* msg;
  void* callback;
  s16 next;
};
static_assert(sizeof(MenuItemView) == 0x28, "MENU_ITEM on PC");
struct MenuSpriteView {
  GC_BITS4(u16 last : 1, u16 fit : 1, u16 type : 2, u16 seq : 12)
  u8 pad2;
  s8 offX, offY;
  u8 alpha;
};
struct MenuSpriteValView {
  s16 u, v, w, h;
  u32 tex;
};

struct ItemPic {
  Shot shot;
  bool ok = false;
};

ItemPic ItemPicture(s32 menuId, int index) {
  ItemPic r;
  const auto* item = static_cast<const MenuItemView*>(menuItemBiosGetPtr(menuId, static_cast<u32>(index)));
  if (item == nullptr || item->spriteID == 0) return r;
  const auto* ms = static_cast<const MenuSpriteView*>(menuSpriteBiosGetPtr(static_cast<u32>(item->spriteID)));
  if (ms == nullptr || ms->type != 1) return r;
  const auto* val = static_cast<const MenuSpriteValView*>(menuSpriteBiosGetValPtr(const_cast<MenuSpriteView*>(ms)));
  if (val == nullptr) return r;
  SpriteWorkLayout& sp = r.shot.sp;
  sp.id = static_cast<s16>(index);
  sp.x = item->x, sp.y = item->y, sp.w = item->w, sp.h = item->h;
  sp.tex2 = val->tex;
  sp.u = val->u, sp.v = val->v, sp.uw = val->w, sp.uh = val->h;
  sp.color[0] = sp.color[1] = sp.color[2] = 255;
  sp.color[3] = static_cast<u8>(item->alpha * ms->alpha / 255);
  sp.sx = sp.sy = 1.f;
  sp.b76 = item->top2;
  r.shot.pic = PictureById(val->tex);
  r.shot.valid = r.shot.seen = r.shot.pic.ok;
  r.shot.inset = true;
  r.ok = r.shot.valid;
  return r;
}

int sProbeMenu = -1;
s32 sProbeLastActive = -2;
void ProbeItems() {
  if (sProbeMenu < 0) return;
  const s32 active = windowGetActiveID();
  if (active == sProbeLastActive) return;
  sProbeLastActive = active;
  const auto* md = static_cast<const uint8_t*>(menuDataBiosGetPtr(sProbeMenu));
  int index = *reinterpret_cast<const s16*>(md + 6);
  int loaded = 0, total = 0;
  for (int guard = 0; guard < 200; guard++) {
    const auto* item = static_cast<const MenuItemView*>(menuItemBiosGetPtr(sProbeMenu, static_cast<u32>(index)));
    const ItemPic p = ItemPicture(sProbeMenu, index);
    if (item->spriteID != 0) {
      total++;
      loaded += p.ok ? 1 : 0;
    }
    std::fprintf(stderr, "[menu probe] active 0x%X: menu 0x%X item %d sprite %d tex 0x%X %s pos %d,%d size %d,%d\n", active,
                 sProbeMenu, index, item->spriteID, p.shot.sp.tex2, p.ok ? "loaded" : "-", item->x, item->y, item->w, item->h);
    if (item->last) break;
    index = item->next;
  }
  std::fprintf(stderr, "[menu probe] active 0x%X: %d of %d pictures of menu 0x%X loaded\n", active, loaded, total, sProbeMenu);
}

struct Capture {
  const FieldMenu* menu = nullptr;
  Shot button, bar, flash, text, hand;
  uint32_t flip = 0;
  bool any = false;
};
Capture sBuilding, sShown;
float sFlashSpeed = 0.f;
float sLastRot = 0.f;
uint32_t sLastRotFlip = 0;

extern "C" int PortXD_WideMenuShiftNow(void);

Shot ShotOf(const WinWorkLayout* w, const SpriteWorkLayout* sp, s16 realY) {
  Shot s;
  s.sp = *sp;
  s.sp.y = realY;
  s.offX = static_cast<s16>(w->offX + PortXD_WideMenuShiftNow());
  s.offY = w->offY;
  std::memcpy(s.winColor, &w->pad_90, 4);
  s.tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(sp->tex2));
  s.valid = (sp->kind & 2) != 0;
  s.seen = true;
  return s;
}

void BarColour(const uint8_t* c, uint8_t* out) {
  if (c[1] == 0xD0 && c[2] == 0xC0 && c[3] == 0xC0) {
    out[0] = c[3], out[1] = c[2], out[2] = c[1], out[3] = c[0];
  } else {
    std::memcpy(out, c, 4);
  }
}

const char* kEntryName = "SETTINGS";

void DrawEntry(ImDrawList* dl, const Capture& cap, bool selected) {
  if (!cap.any || cap.menu == nullptr) return;
  const Screen sc = ScreenNow();
  DrawSprite(dl, sc, cap.button, kRow);
  if (selected) {
    uint8_t bar[4];
    BarColour(cap.bar.sp.color, bar);
    DrawSprite(dl, sc, cap.bar, kRow, bar);
    DrawSprite(dl, sc, cap.flash, kRow);
  }
  if (cap.text.seen) {
    const SpriteWorkLayout& t = cap.text.sp;
    const int rectW = TextWidth(kEntryName), rectH = sFontW + 1;
    const int x = (t.w - rectW) / 2, y = (t.h - rectH) / 2 - 2;
    uint8_t col[4] = {0xF0, 0xF0, 0xF0, static_cast<uint8_t>(selected ? 0xFF : 0x80)};
    col[3] = static_cast<uint8_t>(col[3] * cap.text.winColor[3] / 255);
    DrawText(dl, sc, static_cast<float>(cap.text.offX + t.x + x), static_cast<float>(cap.text.offY + t.y + y + kRow),
             kEntryName, col);
  }
  if (selected) DrawSprite(dl, sc, cap.hand, kRow);
}

struct TitleCapture {
  Shot box, frame, text, info;
  uint32_t flip = 0;
  bool any = false;
};
TitleCapture sTitleBuilding, sTitleShown;

void DrawTextCentred(ImDrawList* dl, const Screen& sc, float x, float y, float w, float h, const char* text,
                     const uint8_t rgba[4]) {
  const float tw = static_cast<float>(TextWidth(text)), th = static_cast<float>(sFontW + 1);
  DrawText(dl, sc, std::floor(x + (w - tw) / 2.f), std::floor(y + (h - th) / 2.f), text, rgba);
}

void DrawTitleEntry(ImDrawList* dl, const TitleCapture& cap, bool selected) {
  if (!cap.any) return;
  const Screen sc = ScreenNow();
  Shot box = cap.box;
  box.sp.x = static_cast<s16>(box.sp.x + kTitleDx);
  DrawSprite(dl, sc, box, 0.f);
  if (selected) {
    Shot frame = cap.frame;
    frame.sp.x = static_cast<s16>(frame.sp.x + kTitleDx);
    DrawSprite(dl, sc, frame, 0.f);
  }
  if (cap.text.seen) {
    const UseFont font(kTitleFont);
    const SpriteWorkLayout& t = cap.text.sp;
    uint8_t col[4] = {0xF0, 0xF0, 0xF0, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.text.winColor[3] / 255);
    DrawTextCentred(dl, sc, static_cast<float>(cap.text.offX + t.x + kTitleDx), static_cast<float>(cap.text.offY + t.y),
                    static_cast<float>(t.w), static_cast<float>(t.h), kEntryName, col);
  }
  if (selected && cap.info.seen) {
    uint8_t col[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.info.winColor[3] / 255);
    DrawText(dl, sc, static_cast<float>(cap.info.offX + cap.info.sp.x), static_cast<float>(cap.info.offY + cap.info.sp.y - 8),
             kTitleInfoText, col);
  }
}

struct Row {
  const char* id;
  const char* label;
  int step;
  bool enhancedOnly;
  const char* unit;
};
constexpr Row kRows[] = {
    {"video.resolution_scale", "Resolution", 1, false, ""},
    {"video.widescreen", "Widescreen", 0, false, ""},
    {"video.fullscreen", "Full screen", 0, false, ""},
    {"video.borderless_window", "Borderless window", 0, false, ""},
    {"video.fps_counter", "Frame rate counter", 0, false, ""},
    {"audio.master_volume", "Volume", 10, false, "%"},
    {"audio.music_volume", "Music", 10, false, "%"},
    {"audio.sfx_volume", "Sound effects", 10, false, "%"},
    {"audio.cry_volume", "Cries", 10, false, "%"},
    {"audio.menu_music", "Music in menus", 0, false, ""},
    {"controls.button_icons", "Button pictures", 0, false, ""},
    {"gameplay.text_speed", "Text speed", 0, true, ""},
    {"gameplay.fast_forward", "Fast-forward", 0, true, ""},
    {"gameplay.fast_forward_mode", "Fast-forward key", 0, true, ""},
    {"gameplay.fast_forward_speed", "Fast-forward speed", 1, true, "x"},
    {"gameplay.fast_battle_animations", "Fast animations", 0, true, ""},
    {"system.pause_on_focus_loss", "Pause in the background", 0, true, ""},
    {nullptr, "Back", 0, false, ""},
};

struct Page {
  std::vector<const Row*> rows;
  int cursor = 0;
  bool changed = false;
  float flashRot = 0.f;
  float pulse = 0.f;
  float titleTimer = 0.f;
  int first = 0;
  std::vector<float> rowAlpha;
  float age = 0.f;
  std::chrono::steady_clock::time_point lastRefresh{};
};
constexpr int kVisibleRows = 10;
Page sPage;

bool RowShown(const Row& r) {
  if (r.id == nullptr) return true;
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  if (i == es::kNoSetting || !reg.Get(i).implemented) return false;
  return !r.enhancedOnly || reg.CurrentPreset() != es::Preset::Original;
}

std::string ValueText(const Row& r) {
  if (r.id == nullptr) return "";
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  const es::Definition& d = reg.Get(i);
  const es::Value v = reg.ValueOf(i);
  switch (d.type) {
  case es::Type::Bool: return v.i ? "On" : "Off";
  case es::Type::Enum: return v.i >= 0 && v.i < static_cast<int32_t>(d.choices.size()) ? d.choices[v.i].name : "?";
  case es::Type::Int: {
    std::string name = d.IntName(v.i);
    if (!name.empty()) {
      if (name == "Match the window") return "Window";
      const size_t paren = name.find(" (");
      return paren != std::string::npos ? name.substr(0, paren) : name;
    }
    return std::to_string(v.i) + r.unit;
  }
  default: return reg.ValueText(i);
  }
}

void ApplyMenuSoundVolume() {
  const es::Registry& reg = es::Global();
  float gain = static_cast<float>(reg.Int("audio.master_volume").Get()) / 100.f;
  if (reg.Find("audio.sfx_volume") != es::kNoSetting) gain *= static_cast<float>(reg.Int("audio.sfx_volume").Get()) / 100.f;
  PortSfx_SetVolume(gain);
}

void Change(const Row& r, int dir) {
  es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  const es::Definition& d = reg.Get(i);
  es::Value v = reg.ValueOf(i);
  switch (d.type) {
  case es::Type::Bool: v.i = v.i ? 0 : 1; break;
  case es::Type::Enum: {
    const int n = static_cast<int>(d.choices.size());
    v.i = (v.i + dir + n) % n;
    break;
  }
  case es::Type::Int:
    if (!d.intNames.empty()) {
      size_t at = 0;
      for (size_t k = 0; k < d.intNames.size(); k++) {
        if (d.intNames[k].first == v.i) at = k;
      }
      at = (at + d.intNames.size() + dir) % d.intNames.size();
      v.i = d.intNames[at].first;
    } else {
      v.i = std::clamp(v.i + dir * std::max(r.step, 1), d.minInt, d.maxInt);
    }
    break;
  default: return;
  }
  if (!reg.Set(i, v)) return;
  sPage.changed = true;
  std::fprintf(stderr, "[settings menu] %s = %s\n", r.id, reg.ValueText(i).c_str());
  if (std::strncmp(r.id, "video.", 6) == 0) {
    PortVideo_ApplySettings();
  } else if (std::strcmp(r.id, "audio.master_volume") == 0) {
    PortAudio_SetVolume(static_cast<float>(v.i) / 100.f);
    ApplyMenuSoundVolume();
  } else {
    PortXD_AudioCategoryVolumeChanged(r.id, v.i);
    if (std::strcmp(r.id, "audio.sfx_volume") == 0) ApplyMenuSoundVolume();
  }
}

void PageOpen(void*) {
  sPage.rows.clear();
  for (const Row& r : kRows) {
    if (RowShown(r)) sPage.rows.push_back(&r);
  }
  sPage.cursor = 0;
  sPage.first = 0;
  sPage.changed = false;
  sPage.rowAlpha.clear();
  sPage.age = 0.f;
  sPage.titleTimer = 0.f;
  sPage.lastRefresh = {};
}

void PageClose(void*) {
  if (sPage.changed) {
    if (xd::SaveSettings(es::Global()) == xd::SettingsSave::Saved) {
      std::fprintf(stderr, "[settings menu] saved to %ls\n", (xd::SettingsFolder() / es::Registry::kFileName).wstring().c_str());
    }
  }
}

constexpr s32 kOptMenu = 0x117;
constexpr int kOptBgTop = 74, kOptBgBottom = 75, kOptDescFirst = 76, kOptDescLast = 84, kOptPanel = 85, kOptRowBar = 86,
              kOptHeader = 89, kOptExit = 90, kOptChoice = 91, kOptChoice2 = 92, kOptExitCursor = 95;
constexpr float kScrRowTop = 96.f, kScrPitch = 44.f, kScrBarH = 40.f;
constexpr int kScrRows = 5;
constexpr uint16_t kScrFont = 1;
constexpr uint16_t kScrInfoFont = 0;
bool sLookScreen = false;

void KeepOptionsPictures() {
  static bool kept = false;
  if (kept) return;
  int n = 0;
  for (int i = kOptBgTop; i <= kOptExitCursor; i++) n += ItemPicture(kOptMenu, i).ok ? 1 : 0;
  if (n == kOptExitCursor - kOptBgTop + 1) {
    kept = true;
    std::fprintf(stderr, "[settings menu] the OPTIONS screen's pictures kept for the settings screen\n");
  }
}

bool TwoChoices(const Row& r) {
  if (r.id == nullptr) return false;
  const es::Registry& reg = es::Global();
  const es::Definition& d = reg.Get(reg.Find(r.id));
  return d.type == es::Type::Bool || (d.type == es::Type::Enum && d.choices.size() == 2);
}

int ChoiceNames(const Row& r, std::string& left, std::string& right) {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  const es::Definition& d = reg.Get(i);
  const es::Value v = reg.ValueOf(i);
  if (d.type == es::Type::Bool) {
    left = "On", right = "Off";
    return v.i ? 0 : 1;
  }
  left = d.choices[0].name, right = d.choices[1].name;
  return v.i == 0 ? 0 : 1;
}

void Choose(const Row& r, int side) {
  std::string left, right;
  const int now = ChoiceNames(r, left, right);
  if (now != side) Change(r, side > now ? +1 : -1);
}

std::vector<std::string> Wrap(const std::string& text, float w) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush = [&]() {
    if (word.empty()) return;
    const std::string tryLine = line.empty() ? word : line + " " + word;
    if (!line.empty() && static_cast<float>(TextWidth(tryLine.c_str())) > w) {
      lines.push_back(line);
      line = word;
    } else {
      line = tryLine;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flush();
    else word += c;
  }
  flush();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string Description(const Row& r) {
  if (r.id == nullptr) return "Save any changes and leave the settings.";
  const es::Registry& reg = es::Global();
  return reg.Get(reg.Find(r.id)).description;
}

uint8_t PulseAlpha(float t) {
  return t < 0.5f ? static_cast<uint8_t>(255 - static_cast<int>(128.f * std::sin(3.14f * t / 0.5f))) : 255;
}

void DrawItem(ImDrawList* dl, const Screen& sc, int index, float x, float y, float w = -1.f, float h = -1.f,
              uint8_t alpha = 255, float edge = 0.f) {
  ItemPic p = ItemPicture(kOptMenu, index);
  if (!p.ok) return;
  SpriteWorkLayout& sp = p.shot.sp;
  sp.color[3] = static_cast<uint8_t>(sp.color[3] * alpha / 255);
  if (h >= 0.f) sp.h = static_cast<s16>(sp.h < 0 ? -h : h);
  if (edge > 0.f) {
    DrawSliced(dl, sc, p.shot, x, y, w >= 0.f ? w : std::abs(static_cast<float>(sp.w)), edge, edge);
    return;
  }
  sp.x = static_cast<s16>(x), sp.y = static_cast<s16>(y);
  if (w >= 0.f) sp.w = static_cast<s16>(sp.w < 0 ? -w : w);
  DrawSprite(dl, sc, p.shot, 0.f);
}

void DrawItemAsIs(ImDrawList* dl, const Screen& sc, int index) {
  const auto* item = static_cast<const MenuItemView*>(menuItemBiosGetPtr(kOptMenu, static_cast<u32>(index)));
  DrawItem(dl, sc, index, item->x, item->y);
}

void DrawTriangle(ImDrawList* dl, const Screen& sc, float x, float y, float size, int dir, ImU32 col) {
  const float h = size * 0.5f, d = h * 0.6f;
  switch (dir) {
  case 0: dl->AddTriangleFilled(sc.At(x - h, y + d), sc.At(x + h, y + d), sc.At(x, y - d), col); break;
  case 1: dl->AddTriangleFilled(sc.At(x - h, y - d), sc.At(x, y + d), sc.At(x + h, y - d), col); break;
  case 2: dl->AddTriangleFilled(sc.At(x + d, y - h), sc.At(x + d, y + h), sc.At(x - d, y), col); break;
  default: dl->AddTriangleFilled(sc.At(x - d, y - h), sc.At(x + d, y), sc.At(x - d, y + h), col); break;
  }
}

void FillWindow(ImDrawList* dl, ImU32 col) {
  float W = 640.f, H = 480.f;
  if (ImGui::GetCurrentContext() != nullptr) W = ImGui::GetIO().DisplaySize.x, H = ImGui::GetIO().DisplaySize.y;
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), col);
}

void DrawSettingsScreen(ImDrawList* dl, float dt) {
  const Screen sc = ScreenNow();
  const int n = static_cast<int>(sPage.rows.size());
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  DrawItemAsIs(dl, sc, kOptBgTop);
  DrawItemAsIs(dl, sc, kOptBgBottom);
  DrawItemAsIs(dl, sc, kOptPanel);
  DrawItemAsIs(dl, sc, kOptHeader);
  for (int i = kOptDescFirst; i <= kOptDescLast; i++) DrawItemAsIs(dl, sc, i);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  {
    const UseFont font(kScrFont);
    DrawText(dl, sc, 82.f, 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), kEntryName, white);
  }
  const int shown = std::min(n, kScrRows);
  sPage.first = std::clamp(sPage.first, std::max(0, sPage.cursor - shown + 1), sPage.cursor);
  sPage.first = std::clamp(sPage.first, 0, n - shown);
  if (static_cast<int>(sPage.rowAlpha.size()) != n) sPage.rowAlpha.assign(n, 0.f);
  for (int i = 0; i < n; i++) {
    const float target = i == sPage.cursor ? 255.f : 0.f;
    const float step = 255.f * dt / 0.15f;
    float& a = sPage.rowAlpha[i];
    a = a < target ? std::min(target, a + step) : std::max(target, a - step);
  }
  const uint8_t pulse = PulseAlpha(sPage.titleTimer);
  for (int i = sPage.first; i < sPage.first + shown; i++) {
    const Row& r = *sPage.rows[i];
    const bool sel = i == sPage.cursor;
    const float y = kScrRowTop + kScrPitch * static_cast<float>(i - sPage.first);
    const float mid = y + kScrBarH / 2.f;
    DrawItem(dl, sc, kOptRowBar, 0.f, y, 640.f, kScrBarH, static_cast<uint8_t>(sPage.rowAlpha[i]));
    const UseFont font(kScrFont);
    if (r.id == nullptr) {
      DrawItem(dl, sc, kOptExit, 437.f, mid - 16.f);
      if (sel) DrawItem(dl, sc, kOptExitCursor, 434.f, mid - 19.f, -1.f, -1.f, pulse);
      DrawTextCentred(dl, sc, 437.f, mid - 16.f, 144.f, 32.f, "EXIT", white);
      continue;
    }
    const float labelW = static_cast<float>(TextWidth(r.label)), room = 321.f - 8.f - 74.f;
    DrawText(dl, sc, 74.f, std::floor(mid - static_cast<float>(sFontW + 1) / 2.f), r.label, white,
             labelW > room ? room / labelW : 1.f);
    const float boxY = mid - 12.f;
    if (TwoChoices(r)) {
      std::string left, right;
      const int set = ChoiceNames(r, left, right);
      DrawItem(dl, sc, set == 0 ? kOptChoice : kOptChoice2, set == 0 ? 321.f : 449.f, boxY, -1.f, -1.f, sel ? pulse : 255);
      DrawTextCentred(dl, sc, 321.f, boxY, 121.f, 24.f, left.c_str(), white);
      DrawTextCentred(dl, sc, 449.f, boxY, 121.f, 24.f, right.c_str(), white);
    } else {
      DrawItem(dl, sc, kOptChoice, 321.f, boxY, 249.f, -1.f, sel ? pulse : 255, 14.f);
      const std::string v = ValueText(r);
      DrawTextCentred(dl, sc, 321.f, boxY, 249.f, 24.f, v.c_str(), white);
      if (sel) {
        DrawTriangle(dl, sc, 321.f + 20.f, mid, 12.f, 2, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
        DrawTriangle(dl, sc, 321.f + 249.f - 20.f, mid, 12.f, 3, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
      }
    }
  }
  if (sPage.first > 0) DrawTriangle(dl, sc, 320.f, 75.f, 16.f, 0, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  if (sPage.first + shown < n) DrawTriangle(dl, sc, 320.f, 328.f, 16.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  {
    const UseFont font(kScrInfoFont);
    const std::string text = Description(*sPage.rows[sPage.cursor]);
    constexpr float kBoxW = 452.f;
    struct Fit {
      size_t lines;
      float pitch, scale, squeeze, top;
    };
    constexpr Fit kFits[] = {{2, 33.f, 1.f, 1.f, 363.f},    {3, 20.f, 1.f, 0.9f, 360.f}, {4, 14.5f, 0.75f, 0.9f, 359.f},
                             {5, 11.5f, 0.6f, 0.9f, 358.f}, {6, 9.6f, 0.5f, 0.9f, 358.f}};
    std::vector<std::string> lines;
    Fit fit = kFits[0];
    for (const Fit& f : kFits) {
      fit = f;
      lines = Wrap(text, kBoxW / (f.scale * f.squeeze));
      if (lines.size() <= f.lines) break;
    }
    const size_t count = std::min(lines.size(), fit.lines);
    const float top = count <= 2 ? fit.top : 392.f - fit.pitch * static_cast<float>(count) / 2.f;
    for (size_t k = 0; k < count; k++) {
      DrawText(dl, sc, 90.f, top + fit.pitch * static_cast<float>(k), lines[k].c_str(), white, fit.squeeze, fit.scale);
    }
  }
  if (sPage.age < 0.25f) FillWindow(dl, IM_COL32(0, 0, 0, std::clamp(static_cast<int>(255.f * (1.f - sPage.age / 0.25f)), 0, 255)));
}

void PlayMenuSound(s32 menuId, int event);
s32 PageSoundMenu();

bool PageFrame(void*, const PortMenuInput* in) {
  const int n = static_cast<int>(sPage.rows.size());
  const int cursorBefore = sPage.cursor;
  const bool changedBefore = sPage.changed;
  sPage.changed = false;
  struct Sound {
    int event = 0;
    ~Sound() {
      if (event != 0) PlayMenuSound(PageSoundMenu(), event);
    }
  } sound;
  if (in->pressed != 0 && std::getenv("XD_SETTINGS_DEBUG") != nullptr) {
    std::fprintf(stderr, "[settings menu] pressed 0x%X held 0x%X cursor %d of %d\n", in->pressed, in->held, sPage.cursor, n);
  }
  if (in->repeat & PAD_BUTTON_UP) sPage.cursor = (sPage.cursor + n - 1) % n;
  if (in->repeat & PAD_BUTTON_DOWN) sPage.cursor = (sPage.cursor + 1) % n;
  const bool screen = (sPageFromTitle || sLookScreen) && ItemPicture(kOptMenu, kOptPanel).ok;
  const Row& row = *sPage.rows[sPage.cursor];
  if (row.id != nullptr) {
    if (screen && TwoChoices(row)) {
      if (in->repeat & PAD_BUTTON_LEFT) Choose(row, 0);
      if (in->repeat & PAD_BUTTON_RIGHT) Choose(row, 1);
    } else {
      if (in->repeat & PAD_BUTTON_LEFT) Change(row, -1);
      if (in->repeat & PAD_BUTTON_RIGHT) Change(row, +1);
    }
    if (in->pressed & PAD_BUTTON_A) Change(row, +1);
  } else if (in->pressed & PAD_BUTTON_A) {
    sPage.changed = changedBefore;
    sound.event = kSoundChoose;
    return false;
  }
  if (sPage.cursor != cursorBefore || sPage.changed) sound.event = kSoundMove;
  sPage.changed = sPage.changed || changedBefore;
  if (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_START)) {
    sound.event = kSoundBack;
    return false;
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const auto now = std::chrono::steady_clock::now();
  float dt = sPage.lastRefresh.time_since_epoch().count() == 0
                 ? 1.f / 60.f
                 : std::chrono::duration<float>(now - sPage.lastRefresh).count();
  sPage.lastRefresh = now;
  dt = std::clamp(dt, 0.f, 0.1f);
  sPage.flashRot += sFlashSpeed * dt;
  sPage.pulse = std::fmod(sPage.pulse + 6.2831855f * dt / 0.8f, 6.2831855f);
  sPage.titleTimer = std::fmod(sPage.titleTimer + dt, 1.f);
  sPage.age += dt;
  if (screen) {
    DrawSettingsScreen(dl, dt);
    return true;
  }
  if (sPageFromTitle) {
    DrawTitleEntry(dl, sTitleShown, true);
  }
  DrawEntry(dl, sShown, true);
  const Capture& cap = sShown;
  if (!cap.any) return true;
  const Screen sc = ScreenNow();
  const int shownRows = std::min(n, kVisibleRows);
  sPage.first = std::clamp(sPage.first, std::max(0, sPage.cursor - shownRows + 1), sPage.cursor);
  sPage.first = std::clamp(sPage.first, 0, n - shownRows);
  const float colRight = 404.f, rowW = 340.f, colLeft = colRight - rowW;
  const float top = 81.f - kRow;
  const float panelTop = top - 10.f, panelBottom = top + kRow * (shownRows + 1) + 14.f;
  dl->AddRectFilled(sc.At(colLeft - 34.f, panelTop), sc.At(colRight + 6.f, panelBottom), IM_COL32(0, 0, 0, 150),
                    10.f * sc.s);
  const uint8_t titleCol[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  DrawText(dl, sc, colLeft + 4.f, top + 4.f, "SETTINGS", titleCol);
  const ImU32 arrowCol = IM_COL32(0xF0, 0xF0, 0xF0, 0xE0);
  const float ax = colRight - 30.f;
  if (sPage.first > 0) {
    dl->AddTriangleFilled(sc.At(ax - 9.f, top + 24.f), sc.At(ax + 9.f, top + 24.f), sc.At(ax, top + 12.f), arrowCol);
  }
  if (sPage.first + shownRows < n) {
    const float by = top + kRow * (shownRows + 1) + 1.f;
    dl->AddTriangleFilled(sc.At(ax - 9.f, by), sc.At(ax + 9.f, by), sc.At(ax, by + 11.f), arrowCol);
  }
  for (int i = sPage.first; i < sPage.first + shownRows; i++) {
    const Row& r = *sPage.rows[i];
    const float y = top + kRow * (i - sPage.first + 1);
    DrawSliced(dl, sc, cap.button, colLeft, y, rowW, 64.f, 40.f);
    const bool sel = i == sPage.cursor;
    if (sel) {
      uint8_t col[4];
      BarColour(cap.bar.sp.color, col);
      col[3] = static_cast<uint8_t>(200.f + 55.f * (1.f + std::sin(sPage.pulse)) / 2.f);
      DrawSliced(dl, sc, cap.bar, colLeft + 34.f, y + (cap.bar.sp.y - cap.button.sp.y), rowW - 44.f, 24.f, 24.f, col);
      Shot flash = cap.flash;
      flash.offX = 0, flash.offY = 0, flash.sp.y = static_cast<s16>(y + (cap.flash.sp.y - cap.button.sp.y));
      flash.sp.rot = sPage.flashRot;
      flash.valid = true;
      DrawSprite(dl, sc, flash, 0.f, nullptr, 0.f, colLeft + 5.f);
    }
    const uint8_t col[4] = {0xF0, 0xF0, 0xF0, static_cast<uint8_t>(sel ? 0xFF : 0x80)};
    const float ty = y + (cap.text.sp.y - cap.button.sp.y) + ((cap.text.sp.h - (sFontW + 1)) / 2 - 2);
    const std::string v = ValueText(r);
    const float textLeft = colLeft + 60.f, textRight = colLeft + rowW - 26.f, gap = 14.f;
    const float labelW = static_cast<float>(TextWidth(r.label));
    const float valueW = v.empty() ? 0.f : static_cast<float>(TextWidth(("< " + v + " >").c_str()));
    const float need = labelW + (v.empty() ? 0.f : gap + valueW);
    const float squeeze = need > textRight - textLeft ? (textRight - textLeft) / need : 1.f;
    DrawText(dl, sc, textLeft, ty, r.label, col, squeeze);
    if (!v.empty()) {
      const std::string shown = sel ? "< " + v + " >" : v;
      DrawText(dl, sc, textRight - TextWidth(shown.c_str()) * squeeze, ty, shown.c_str(), col, squeeze);
    }
    if (sel && cap.hand.seen) {
      Shot hand = cap.hand;
      hand.offX = 0, hand.offY = 0, hand.sp.y = static_cast<s16>(y + (cap.hand.sp.y - cap.button.sp.y));
      hand.valid = true;
      DrawSprite(dl, sc, hand, 0.f, nullptr, 0.f, colLeft + 12.f);
    }
  }
  return true;
}

const PortMenuPage kPage{"settings", PageOpen, PageFrame, PageClose, nullptr};

namespace ng {

enum class Kind { Original, Engine, Setting, Mod, Randomizer, RandomizerSwitch, RandomizerCode, RandomizerPaste, Challenges, Start };
struct SRow {
  Kind kind;
  const Row* setting = nullptr;
  int mod = -1;
  int sw = -1;
};

struct State {
  xd::pt::Defaults defaults;
  engine::playthrough::Engine engine = engine::playthrough::Engine::Faithful;
  std::vector<SRow> rows;
  std::vector<std::pair<size_t, std::string>> before;
  bool settingsChanged = false;
  int cursor = 0, first = 0;
  std::vector<float> rowAlpha;
  float titleTimer = 0.f, age = 0.f;
  std::chrono::steady_clock::time_point last{};
  bool original = false;
  int rStyle = 0;
  std::string rCode;
  std::string rMessage;
  std::vector<bool> rules;
} s;

const char* const kRandomizerStyles[] = {"Off", "Light", "Classic", "Chaos", "Custom"};

struct SwitchText {
  const char* id;
  const char* label;
  const char* info;
};
const SwitchText kSwitchTexts[] = {
    {"wild", "Poke Spot Pokemon", "Other wild Pokemon at the three Poke Spots. The visitors (Munchlax, Bonsly) stay."},
    {"wild.similar", "Similar strength", "Poke Spots: each Pokemon is replaced by one about as strong."},
    {"trainers", "Trainers' Pokemon", "Other Pokemon in the story trainers' teams. Team sizes stay; levels, held items and natures have "
                 "their own rows."},
    {"trainers.similar", "Similar strength", "Trainers: each Pokemon is replaced by one about as strong."},
    {"trainers.moves", "Moves", "Trainers' new Pokemon. Kept: the moves the disc gave that slot. By level: the last four "
                                "moves the new Pokemon learns up to its level."},
    {"trainers.no_dupes", "No repeats","Trainers: a team never gets the same Pokemon twice."},
    {"shadow", "Shadow Pokemon", "Other species are the Shadow Pokemon (all 83 different; the P*DA's list follows). "
                                 "Shadow moves, catch rates and heart gauges stay."},
    {"shadow.similar", "Similar strength", "Shadows: each one is replaced by a Pokemon about as strong."},
    {"shadow.keep_lugia", "Keep Shadow Lugia", "Shadow Lugia stays Lugia (the story and its scenes show Lugia)."},
    {"items.ground", "Items on the ground", "The item boxes on the maps. Random: any item. Shuffle: the same items in "
                                            "other places. Important items never move."},
    {"abilities", "Abilities", "Pokemon get other abilities. Wonder Guard and Forecast are never handed out."},
    {"abilities.family", "Same in a family", "Abilities: a Pokemon keeps its ability when it evolves."},
    {"tm_compat", "TM learners", "Which Pokemon can learn each TM; each TM stays about as common as on the disc."},
    {"legendaries", "Legendaries", "Whether legendary Pokemon can be picked: never, only where the disc had one, or anywhere."},
    {"tm_moves", "TM moves", "Each TM teaches another move, all different (never a Shadow move). The bag and the shops "
                             "show the new move and its description. One-hit-KO moves can be ruled out."},
    {"trainer_levels", "Trainers' levels", "The story trainers' Pokemon are stronger by that much (never above level 100), or "
                                           "\"To strength\": a stronger new Pokemon gets a lower level, a weaker one a higher "
                                           "level. Shadow Pokemon keep their levels."},
    {"trainer_items", "Trainers' held items", "What the story trainers' Pokemon hold. Random: each holds one of the items "
                                              "trainers' Pokemon hold in the game. None: nothing. Shadow Pokemon keep theirs."},
    {"trainer_natures", "Trainers' natures", "Random: each of the story trainers' Pokemon gets any of the 25 natures. Shadow "
                                             "Pokemon get theirs from the game, as always."},
};
const SwitchText* SwitchTextOf(const std::string& id) {
  for (const SwitchText& t : kSwitchTexts) {
    if (id == t.id) return &t;
  }
  return nullptr;
}
int SwitchParent(int sw) {
  const auto& schema = xd::randomizer::GetSchema();
  const std::string& id = schema.switches[sw].id;
  const size_t dot = id.find('.');
  return dot == std::string::npos ? -1 : schema.Find(id.substr(0, dot));
}
std::string ChoiceShown(std::string name) {
  if (name == "natural (by level)") name = "by level";
  if (name == "only where the disc had one") name = "as on the disc";
  if (name == "matched to the new Pokemon's strength") name = "to strength";
  if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  return name;
}

uint64_t NewSeed() {
  std::random_device rd;
  return (static_cast<uint64_t>(rd()) << 32) ^ rd() ^
         static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

int StyleIndexOf(const engine::randomizer::Settings& rs) {
  switch (xd::randomizer::StyleOf(rs)) {
  case xd::randomizer::Style::Light: return 1;
  case xd::randomizer::Style::Classic: return 2;
  case xd::randomizer::Style::Chaos: return 3;
  default: return 4;
  }
}

engine::randomizer::Settings CodeSettings(const std::string& code) {
  engine::randomizer::Settings rs;
  xd::randomizer::ApplyStyle(rs, xd::randomizer::Style::Classic);
  bool seedOnly = false;
  std::string error;
  engine::randomizer::DecodeCode(xd::randomizer::GetSchema(), code, rs, seedOnly, error);
  return rs;
}

void SetRandomizerStyle(int style) {
  s.rStyle = style;
  if (style >= 1 && style <= 3) {
    const xd::randomizer::Style st[] = {xd::randomizer::Style::Light, xd::randomizer::Style::Classic, xd::randomizer::Style::Chaos};
    s.rCode = xd::randomizer::CodeFor(st[style - 1], s.rCode.empty() ? NewSeed() : CodeSettings(s.rCode).seed);
  } else if (style == 4 && s.rCode.empty()) {
    s.rCode = xd::randomizer::CodeFor(xd::randomizer::Style::Classic, NewSeed());
  }
}

void SetRandomizerSwitch(int sw, uint32_t value) {
  const auto& schema = xd::randomizer::GetSchema();
  engine::randomizer::Settings rs = CodeSettings(s.rCode);
  rs.Set(schema, schema.switches[sw].id, value);
  s.rCode = engine::randomizer::EncodeCode(schema, rs);
  s.rMessage.clear();
}

void NewRandomizerSeed() {
  engine::randomizer::Settings rs = CodeSettings(s.rCode);
  rs.seed = NewSeed();
  s.rCode = engine::randomizer::EncodeCode(xd::randomizer::GetSchema(), rs);
  s.rMessage.clear();
}

void PasteRandomizerCode() {
  char* clip = SDL_GetClipboardText();
  std::string text = clip != nullptr ? clip : "";
  SDL_free(clip);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
  std::string code, error;
  if (text.empty()) {
    s.rMessage = "Nothing to paste: copy a code first (Ctrl+C in a message or a web page).";
  } else if (text.size() > 80 || !xd::randomizer::NormalizeCode(text, code, error)) {
    s.rMessage = "That is not a randomizer code (" + (error.empty() ? std::string("too long") : error) + ").";
  } else {
    s.rCode = code;
    s.rStyle = StyleIndexOf(CodeSettings(code));
    s.rMessage = "Code taken: " + code + ".";
  }
}

const Row* SettingRow(const char* id) {
  for (const Row& r : kRows) {
    if (r.id != nullptr && std::strcmp(r.id, id) == 0) return RowShown(r) || true ? &r : nullptr;
  }
  return nullptr;
}

void BuildRows() {
  s.rows.clear();
  s.rows.push_back({Kind::Original});
  s.rows.push_back({Kind::Engine});
  if (s.engine == engine::playthrough::Engine::Enhanced) {
    for (const char* id : {"gameplay.text_speed", "gameplay.fast_forward", "gameplay.fast_battle_animations"}) {
      const es::Registry& reg = es::Global();
      const size_t i = reg.Find(id);
      if (i != es::kNoSetting && reg.Get(i).implemented) {
        if (const Row* r = SettingRow(id)) s.rows.push_back({Kind::Setting, r});
      }
    }
  }
  for (int m = 0; m < static_cast<int>(s.defaults.mods.size()); m++) s.rows.push_back({Kind::Mod, nullptr, m});
  s.rows.push_back({Kind::Randomizer});
  if (s.rStyle == 4) {
    const auto& schema = xd::randomizer::GetSchema();
    const engine::randomizer::Settings rs = CodeSettings(s.rCode);
    for (int i = 0; i < static_cast<int>(schema.switches.size()); i++) {
      const int parent = SwitchParent(i);
      if (parent >= 0 && rs.Get(schema, schema.switches[parent].id) == 0) continue;
      s.rows.push_back({Kind::RandomizerSwitch, nullptr, -1, i});
    }
  }
  if (s.rStyle != 0) {
    s.rows.push_back({Kind::RandomizerCode});
    s.rows.push_back({Kind::RandomizerPaste});
  }
  for (int i = 0; i < static_cast<int>(xd::challenges::Rules().size()); i++) s.rows.push_back({Kind::Challenges, nullptr, -1, i});
  s.rows.push_back({Kind::Start});
  s.cursor = std::clamp(s.cursor, 0, static_cast<int>(s.rows.size()) - 1);
}

engine::playthrough::Setup Chosen() {
  std::vector<std::string> rules;
  for (size_t i = 0; i < s.rules.size(); i++) {
    if (s.rules[i]) rules.push_back(xd::challenges::Rules()[i].id);
  }
  return xd::pt::SetupOf(s.engine, s.defaults.mods, s.rStyle != 0 ? s.rCode : std::string(), rules);
}

std::string Shown(std::string text) {
  const std::string dot = "\xC2\xB7";
  const std::string with = GlyphOf(0xB7) != nullptr ? std::string(1, static_cast<char>(0xB7)) : std::string("-");
  for (size_t at; (at = text.find(dot)) != std::string::npos;) text.replace(at, dot.size(), with);
  return text;
}

std::string RowLabel(const SRow& r) {
  switch (r.kind) {
  case Kind::Original: return "Play the original game";
  case Kind::Engine: return "Engine";
  case Kind::Setting: return r.setting->label;
  case Kind::Mod: return s.defaults.mods[r.mod].name;
  case Kind::Randomizer: return "Randomizer";
  case Kind::RandomizerSwitch: {
    const auto& def = xd::randomizer::GetSchema().switches[r.sw];
    const SwitchText* t = SwitchTextOf(def.id);
    return t != nullptr ? t->label : def.label;
  }
  case Kind::RandomizerCode: return "Randomizer code";
  case Kind::RandomizerPaste: return "Use a friend's code";
  case Kind::Challenges: return xd::challenges::Rules()[r.sw].label;
  default: return "START";
  }
}

std::string RowInfo(const SRow& r) {
  const bool restart = xd::pt::NeedsRestart(Chosen());
  switch (r.kind) {
  case Kind::Original:
    return std::string("Faithful, no mods: exactly the GameCube game. Starts at once") +
           (xd::pt::NeedsRestart(xd::pt::SetupOf(engine::playthrough::Engine::Faithful, {}))
                ? " (the game first starts again without the mods it has loaded)."
                : ".");
  case Kind::Engine:
    return "Faithful: the game decides everything exactly as on the GameCube. Enhanced: speed-ups (text, fast-forward, "
           "battle animations) can be used.";
  case Kind::Setting: return Description(*r.setting);
  case Kind::Mod: {
    const xd::pt::ModChoice& m = s.defaults.mods[r.mod];
    std::string t = std::string(engine::mods::AppliesName(m.applies)) + " mod, version " + m.version + ".";
    if (!m.reason.empty()) t += " Counted as new playthrough only: " + m.reason + ".";
    if (!m.description.empty()) t += " " + m.description;
    return t;
  }
  case Kind::Randomizer:
    return "Makes a new game from your disc: other Pokemon in the Poke Spots, other Shadow Pokemon, items in other "
           "places. Light: those only. Classic: also trainers, abilities, TM learners, similar strength. Chaos: all, no "
           "fairness. Custom: each switch yourself.";
  case Kind::RandomizerSwitch: {
    const auto& def = xd::randomizer::GetSchema().switches[r.sw];
    const SwitchText* t = SwitchTextOf(def.id);
    return t != nullptr ? t->info : def.label + ".";
  }
  case Kind::RandomizerCode:
    return "Code: " + s.rCode + ". It holds the seed and every switch: a friend who uses it gets the same game. "
           "Left / right or A: a new random seed.";
  case Kind::RandomizerPaste:
    return s.rMessage.empty() ? "Copy a code (or a seed number) on the PC, then press A here to use it." : s.rMessage;
  case Kind::Challenges:
    return std::string("Challenge rule, for this playthrough from start to end. ") + xd::challenges::Rules()[r.sw].info;
  default:
    return restart ? "Start the new game with this setup. The game starts again with these mods first; then choose NEW "
                     "GAME again."
                   : (s.defaults.restarted ? "The game started again with your setup: start the new game with it."
                                           : "Start the new game with this setup.");
  }
}

void SetEngine(engine::playthrough::Engine e) {
  if (s.engine == e) return;
  s.engine = e;
  engine::playthrough::ApplyEngine(es::Global(), e);
  BuildRows();
}

void Open(void*) {
  s = State{};
  s.defaults = xd::pt::NewGameDefaults();
  s.engine = s.defaults.engine;
  if (!s.defaults.randomizer.empty()) {
    s.rCode = s.defaults.randomizer;
    s.rStyle = StyleIndexOf(CodeSettings(s.rCode));
  }
  s.rules.assign(xd::challenges::Rules().size(), false);
  for (size_t i = 0; i < s.rules.size(); i++) {
    const std::vector<std::string>& had = s.defaults.challenges;
    s.rules[i] = std::find(had.begin(), had.end(), xd::challenges::Rules()[i].id) != had.end();
  }
  const es::Registry& reg = es::Global();
  for (size_t i = 0; i < reg.Count(); i++) {
    if (engine::playthrough::IsEngineSetting(reg.Get(i))) s.before.emplace_back(i, reg.ValueText(i));
  }
  engine::playthrough::ApplyEngine(es::Global(), s.engine);
  BuildRows();
  if (s.defaults.restarted) s.cursor = static_cast<int>(s.rows.size()) - 1;
  sPage.rowAlpha.clear();
}

void Close(void*) {
  if (s.original) {
    s.engine = engine::playthrough::Engine::Faithful;
    engine::playthrough::ApplyEngine(es::Global(), s.engine);
    for (xd::pt::ModChoice& m : s.defaults.mods) m.on = false;
    s.rStyle = 0;
    s.rules.assign(s.rules.size(), false);
  }
  const es::Registry& reg = es::Global();
  std::string changed;
  for (const auto& [i, was] : s.before) {
    if (reg.ValueText(i) != was) changed += (changed.empty() ? "" : ";") + reg.Get(i).id + "=" + reg.ValueText(i);
  }
  if (s.settingsChanged && xd::SaveSettings(es::Global()) == xd::SettingsSave::Saved) {
    std::fprintf(stderr, "[new game setup] settings saved\n");
  }
  const engine::playthrough::Setup setup = Chosen();
  std::fprintf(stderr, "[new game setup] %s\n", engine::playthrough::Label(setup).c_str());
  xd::pt::Begin(setup, changed);
}

void Draw(ImDrawList* dl, float dt) {
  const Screen sc = ScreenNow();
  const int n = static_cast<int>(s.rows.size());
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  DrawItemAsIs(dl, sc, kOptBgTop);
  DrawItemAsIs(dl, sc, kOptBgBottom);
  DrawItemAsIs(dl, sc, kOptPanel);
  DrawItemAsIs(dl, sc, kOptHeader);
  for (int i = kOptDescFirst; i <= kOptDescLast; i++) DrawItemAsIs(dl, sc, i);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF}, grey[4] = {0xA0, 0xA0, 0xA0, 0xFF};
  {
    const UseFont font(kScrFont);
    DrawText(dl, sc, 82.f, 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), "NEW GAME", white);
  }
  const int shown = std::min(n, kScrRows);
  s.first = std::clamp(s.first, std::max(0, s.cursor - shown + 1), s.cursor);
  s.first = std::clamp(s.first, 0, n - shown);
  if (static_cast<int>(s.rowAlpha.size()) != n) s.rowAlpha.assign(n, 0.f);
  for (int i = 0; i < n; i++) {
    const float target = i == s.cursor ? 255.f : 0.f, step = 255.f * dt / 0.15f;
    float& a = s.rowAlpha[i];
    a = a < target ? std::min(target, a + step) : std::max(target, a - step);
  }
  const uint8_t pulse = PulseAlpha(s.titleTimer);
  for (int i = s.first; i < s.first + shown; i++) {
    const SRow& r = s.rows[i];
    const bool sel = i == s.cursor;
    const float y = kScrRowTop + kScrPitch * static_cast<float>(i - s.first), mid = y + kScrBarH / 2.f;
    DrawItem(dl, sc, kOptRowBar, 0.f, y, 640.f, kScrBarH, static_cast<uint8_t>(s.rowAlpha[i]));
    const UseFont font(kScrFont);
    const std::string label = RowLabel(r);
    if (r.kind == Kind::Original) {
      DrawItem(dl, sc, kOptExit, 120.f, mid - 16.f, 400.f, -1.f, 255, 16.f);
      if (sel) DrawItem(dl, sc, kOptExitCursor, 117.f, mid - 19.f, 406.f, -1.f, pulse, 16.f);
      DrawTextCentred(dl, sc, 120.f, mid - 16.f, 400.f, 32.f, label.c_str(), white);
      continue;
    }
    if (r.kind == Kind::Start) {
      DrawItem(dl, sc, kOptExit, 437.f, mid - 16.f);
      if (sel) DrawItem(dl, sc, kOptExitCursor, 434.f, mid - 19.f, -1.f, -1.f, pulse);
      DrawTextCentred(dl, sc, 437.f, mid - 16.f, 144.f, 32.f, label.c_str(), white);
      continue;
    }
    const float labelW = static_cast<float>(TextWidth(label.c_str())), room = 321.f - 8.f - 74.f;
    const float indent =
        r.kind == Kind::RandomizerSwitch && SwitchParent(r.sw) >= 0 ? 16.f : 0.f;
    DrawText(dl, sc, 74.f + indent, std::floor(mid - static_cast<float>(sFontW + 1) / 2.f), label.c_str(), white,
             labelW > room - indent ? (room - indent) / labelW : 1.f);
    const float boxY = mid - 12.f;
    std::string left, right, value;
    int set = -1;
    if (r.kind == Kind::Engine) {
      left = "Faithful", right = "Enhanced";
      set = s.engine == engine::playthrough::Engine::Faithful ? 0 : 1;
    } else if (r.kind == Kind::Mod) {
      left = "On", right = "Off";
      set = s.defaults.mods[r.mod].on ? 0 : 1;
    } else if (r.kind == Kind::Challenges) {
      left = "On", right = "Off";
      set = s.rules[r.sw] ? 0 : 1;
    } else if (r.kind == Kind::Setting && TwoChoices(*r.setting)) {
      set = ChoiceNames(*r.setting, left, right);
    } else if (r.kind == Kind::Setting) {
      value = ValueText(*r.setting);
    } else if (r.kind == Kind::Randomizer) {
      value = kRandomizerStyles[s.rStyle];
    } else if (r.kind == Kind::RandomizerSwitch) {
      const auto& schema = xd::randomizer::GetSchema();
      const auto& def = schema.switches[r.sw];
      const uint32_t v = std::min(CodeSettings(s.rCode).Get(schema, def.id), def.Choices() - 1);
      if (def.Choices() == 2 && def.choiceNames[0] == "off") {
        left = ChoiceShown(def.choiceNames[1]), right = "Off";
        set = v != 0 ? 0 : 1;
      } else if (def.Choices() == 2) {
        left = ChoiceShown(def.choiceNames[0]), right = ChoiceShown(def.choiceNames[1]);
        set = static_cast<int>(v);
      } else {
        value = ChoiceShown(def.choiceNames[v]);
      }
    } else if (r.kind == Kind::RandomizerCode) {
      value = s.rCode;
    } else if (r.kind == Kind::RandomizerPaste) {
      value = "Paste (A)";
    }
    if (set >= 0) {
      DrawItem(dl, sc, set == 0 ? kOptChoice : kOptChoice2, set == 0 ? 321.f : 449.f, boxY, -1.f, -1.f, sel ? pulse : 255);
      DrawTextCentred(dl, sc, 321.f, boxY, 121.f, 24.f, left.c_str(), white);
      DrawTextCentred(dl, sc, 449.f, boxY, 121.f, 24.f, right.c_str(), white);
    } else {
      const bool soon = false;
      DrawItem(dl, sc, kOptChoice, 321.f, boxY, 249.f, -1.f, soon ? 110 : (sel ? pulse : 255), 14.f);
      const float valueW = static_cast<float>(TextWidth(value.c_str())), valueRoom = 249.f - 64.f;
      if (valueW > valueRoom) {
        DrawText(dl, sc, 321.f + 32.f, std::floor(boxY + (24.f - static_cast<float>(sFontW + 1)) / 2.f), value.c_str(), white,
                 valueRoom / valueW);
      } else {
        DrawTextCentred(dl, sc, 321.f, boxY, 249.f, 24.f, value.c_str(), soon ? grey : white);
      }
      if (sel && !soon && r.kind != Kind::RandomizerPaste) {
        DrawTriangle(dl, sc, 321.f + 20.f, mid, 12.f, 2, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
        DrawTriangle(dl, sc, 321.f + 249.f - 20.f, mid, 12.f, 3, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
      }
    }
  }
  if (s.first > 0) DrawTriangle(dl, sc, 320.f, 75.f, 16.f, 0, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  if (s.first + shown < n) DrawTriangle(dl, sc, 320.f, 328.f, 16.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  {
    const UseFont font(kScrInfoFont);
    const SRow& r = s.rows[s.cursor];
    const std::string summary =
        Shown(engine::playthrough::Label(r.kind == Kind::Original ? xd::pt::SetupOf(engine::playthrough::Engine::Faithful, {})
                                                                  : Chosen()));
    const float summaryW = std::max(1.f, static_cast<float>(TextWidth(summary.c_str())));
    DrawText(dl, sc, 90.f, 356.f, summary.c_str(), white, std::min(0.9f, 452.f / summaryW));
    const std::vector<std::string> lines = Wrap(RowInfo(r), 452.f / (0.75f * 0.9f));
    for (size_t k = 0; k < std::min<size_t>(lines.size(), 4); k++) {
      DrawText(dl, sc, 90.f, 375.f + 14.5f * static_cast<float>(k), lines[k].c_str(), white, 0.9f, 0.75f);
    }
  }
  if (s.age < 0.25f) FillWindow(dl, IM_COL32(0, 0, 0, std::clamp(static_cast<int>(255.f * (1.f - s.age / 0.25f)), 0, 255)));
}

bool Frame(void*, const PortMenuInput* in) {
  const int n = static_cast<int>(s.rows.size());
  int event = 0;
  const int before = s.cursor;
  if (in->repeat & PAD_BUTTON_UP) s.cursor = (s.cursor + n - 1) % n;
  if (in->repeat & PAD_BUTTON_DOWN) s.cursor = (s.cursor + 1) % n;
  if (s.cursor != before) event = kSoundMove;
  SRow& r = s.rows[s.cursor];
  const bool left = (in->repeat & PAD_BUTTON_LEFT) != 0, right = (in->repeat & PAD_BUTTON_RIGHT) != 0;
  const bool a = (in->pressed & PAD_BUTTON_A) != 0;
  bool close = false;
  switch (r.kind) {
  case Kind::Engine:
    if (left || right || a) {
      SetEngine(left ? engine::playthrough::Engine::Faithful
                     : right ? engine::playthrough::Engine::Enhanced
                             : (s.engine == engine::playthrough::Engine::Faithful ? engine::playthrough::Engine::Enhanced
                                                                                  : engine::playthrough::Engine::Faithful));
      event = kSoundMove;
    }
    break;
  case Kind::Setting:
    if (left || right || a) {
      sPage.changed = false;
      if (TwoChoices(*r.setting) && (left || right)) Choose(*r.setting, left ? 0 : 1);
      else Change(*r.setting, left ? -1 : +1);
      s.settingsChanged |= sPage.changed;
      event = kSoundMove;
    }
    break;
  case Kind::Mod:
    if (left || right || a) {
      bool& on = s.defaults.mods[r.mod].on;
      on = left ? true : right ? false : !on;
      event = kSoundMove;
    }
    break;
  case Kind::Randomizer:
    if (left || right || a) {
      SetRandomizerStyle(a ? (s.rStyle == 0 ? 2 : 0) : (s.rStyle + (left ? 4 : 1)) % 5);
      BuildRows();
      event = kSoundMove;
    }
    break;
  case Kind::RandomizerSwitch:
    if (left || right || a) {
      const auto& schema = xd::randomizer::GetSchema();
      const auto& def = schema.switches[r.sw];
      const uint32_t n2 = def.Choices(), v = std::min(CodeSettings(s.rCode).Get(schema, def.id), n2 - 1);
      uint32_t to = (v + (left ? n2 - 1 : 1)) % n2;
      if (n2 == 2 && !a) {
        const bool onLeft = def.choiceNames[0] == "off";
        to = left ? (onLeft ? 1 : 0) : (onLeft ? 0 : 1);
      }
      SetRandomizerSwitch(r.sw, to);
      BuildRows();
      event = kSoundMove;
    }
    break;
  case Kind::Challenges:
    if (left || right || a) {
      s.rules[r.sw] = left ? true : right ? false : !s.rules[r.sw];
      event = kSoundMove;
    }
    break;
  case Kind::RandomizerCode:
    if (left || right || a) {
      NewRandomizerSeed();
      event = kSoundMove;
    }
    break;
  case Kind::RandomizerPaste:
    if (a) {
      PasteRandomizerCode();
      BuildRows();
      event = kSoundMove;
    }
    break;
  case Kind::Original:
    if (a) s.original = close = true;
    break;
  case Kind::Start:
    if (a) close = true;
    break;
  default: break;
  }
  if (in->pressed & PAD_BUTTON_START) close = true;
  if (in->pressed & PAD_BUTTON_B) { s.cursor = n - 1; event = kSoundBack; }
  if (close) event = kSoundChoose;
  if (event != 0) PlayMenuSound(kTitleMenu, event);
  if (close) return false;
  const auto now = std::chrono::steady_clock::now();
  float dt = s.last.time_since_epoch().count() == 0 ? 1.f / 60.f : std::chrono::duration<float>(now - s.last).count();
  s.last = now;
  dt = std::clamp(dt, 0.f, 0.1f);
  s.titleTimer = std::fmod(s.titleTimer + dt, 1.f);
  s.age += dt;
  if (!ItemPicture(kOptMenu, kOptPanel).ok) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    FillWindow(dl, IM_COL32(0, 0, 0, 230));
    const Screen sc = ScreenNow();
    const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
    for (int i = 0; i < n; i++) {
      DrawText(dl, sc, 80.f, 60.f + 30.f * i, ((i == s.cursor ? "> " : "  ") + RowLabel(s.rows[i])).c_str(), white);
    }
    return true;
  }
  Draw(ImGui::GetForegroundDrawList(), dt);
  return true;
}

const PortMenuPage kSetupPage{"new game setup", Open, Frame, Close, nullptr};

constexpr s32 kMsgMenu = 0x33, kYesNoMenu = 0x35;
constexpr int kMsgFrameFirst = 36, kMsgFrameLast = 44, kYesNoFrameFirst = 66, kYesNoFrameLast = 74;
constexpr float kYesNoX = 60.f, kYesNoY = 166.f;
int qCursor = 1;
bool qCarryOn = false;
float qAge = 0.f;
std::chrono::steady_clock::time_point qLast{};
void QOpen(void*) { qCursor = 1, qCarryOn = false, qAge = 0.f, qLast = {}; }
void QClose(void*) { xd::pt::ContinueAnswer(qCarryOn); }

bool DrawFrame(ImDrawList* dl, const Screen& sc, s32 menu, int first, int last, float dx, float dy) {
  for (int i = first; i <= last; i++) {
    if (!ItemPicture(menu, i).ok) return false;
  }
  for (int i = first; i <= last; i++) {
    ItemPic p = ItemPicture(menu, i);
    p.shot.sp.x = static_cast<s16>(p.shot.sp.x + dx), p.shot.sp.y = static_cast<s16>(p.shot.sp.y + dy);
    DrawSprite(dl, sc, p.shot, 0.f);
  }
  return true;
}

std::vector<std::string> QuestionLines(float& scale, float& pitch) {
  const std::vector<std::string>& problems = xd::pt::ContinueProblems();
  const float room = 520.f;
  for (size_t keep = problems.size();; keep--) {
    std::string text = "This save was started with mods that are missing or changed: ";
    for (size_t i = 0; i < keep; i++) text += (i > 0 ? "; " : "") + problems[i];
    if (keep < problems.size()) text += (keep > 0 ? "; and " : "") + std::to_string(problems.size() - keep) + " more";
    text += ". Carry on without them? (It can change the game.)";
    scale = 1.f, pitch = 28.f;
    std::vector<std::string> lines = Wrap(text, room);
    if (lines.size() <= 4) return lines;
    scale = 0.8f, pitch = 23.f;
    lines = Wrap(text, room / scale);
    if (lines.size() <= 5 || keep == 0) {
      lines.resize(std::min<size_t>(lines.size(), 5));
      return lines;
    }
  }
}

bool QFrame(void*, const PortMenuInput* in) {
  if (in->pressed & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
    qCursor ^= 1;
    PlayMenuSound(kTitleMenu, kSoundMove);
  }
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    return false;
  }
  if (in->pressed & PAD_BUTTON_A) {
    qCarryOn = qCursor == 0;
    PlayMenuSound(kTitleMenu, kSoundChoose);
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  qAge += qLast.time_since_epoch().count() == 0 ? 0.f : std::clamp(std::chrono::duration<float>(now - qLast).count(), 0.f, 0.1f);
  qLast = now;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  FillWindow(dl, IM_COL32(0, 0, 0, 120));
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  const UseFont font(0);
  if (!DrawFrame(dl, sc, kMsgMenu, kMsgFrameFirst, kMsgFrameLast, 0.f, 0.f)) {
    dl->AddRectFilled(sc.At(27.f, 290.f), sc.At(613.f, 440.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  float scale = 1.f, pitch = 28.f;
  const std::vector<std::string> lines = QuestionLines(scale, pitch);
  for (size_t i = 0; i < lines.size(); i++) {
    DrawText(dl, sc, 52.f, 307.f + pitch * static_cast<float>(i), lines[i].c_str(), white, 1.f, scale);
  }
  if (!DrawFrame(dl, sc, kYesNoMenu, kYesNoFrameFirst, kYesNoFrameLast, kYesNoX, kYesNoY)) {
    dl->AddRectFilled(sc.At(kYesNoX, kYesNoY), sc.At(kYesNoX + 159.f, kYesNoY + 89.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  const char* const answers[2] = {"YES", "NO"};
  for (int i = 0; i < 2; i++) {
    const float rowY = kYesNoY + (i == 0 ? 10.f : 47.f);
    if (i == qCursor) {
      const float pulse = 0.75f + 0.25f * std::cos(qAge * 6.28f);
      dl->AddRect(sc.At(kYesNoX + 21.f, rowY), sc.At(kYesNoX + 138.f, rowY + 28.f),
                  IM_COL32(0xA8, 0xA8, 0xA8, static_cast<int>(255.f * pulse)), 14.f * sc.s, 0, 1.5f * sc.s);
      const ImVec2 c = sc.At(kYesNoX + 37.f, rowY + 14.f);
      dl->AddCircleFilled(c, 8.5f * sc.s, IM_COL32(0xD8, 0x18, 0x20, 255), 24);
      const ImVec2 pts[3] = {ImVec2(c.x - 2.5f * sc.s, c.y - 5.f * sc.s), ImVec2(c.x + 3.f * sc.s, c.y),
                             ImVec2(c.x - 2.5f * sc.s, c.y + 5.f * sc.s)};
      dl->AddPolyline(pts, 3, IM_COL32(0x18, 0x18, 0x18, 255), 0, 2.6f * sc.s);
    }
    DrawText(dl, sc, kYesNoX + 52.f, rowY + 5.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), answers[i], white);
  }
  return true;
}
const PortMenuPage kQuestionPage{"continue question", QOpen, QFrame, QClose, nullptr};

std::vector<std::string> ePages;
int ePage = 0, eCursor = 0, eAnswer = 0;
float eAge = 0.f;
std::chrono::steady_clock::time_point eLast{};
const char* kEngineChoices[2] = {"Keep Faithful", "Switch to Enhanced"};

void EOpen(void*) {
  ePage = 0, eCursor = 0, eAnswer = 0, eAge = 0.f, eLast = {};
  std::string held;
  const std::vector<std::string> names = xd::pt::EngineQuestionSettings();
  for (size_t i = 0; i < names.size(); i++) held += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
  if (xd::pt::EngineQuestionSave() == engine::playthrough::Engine::Faithful) {
    kEngineChoices[0] = "Keep Faithful", kEngineChoices[1] = "Switch to Enhanced";
    ePages = {
        "This playthrough is marked Faithful: it plays exactly like the GameCube game.",
        "For a Faithful playthrough some settings stay as on the GameCube, whatever you have set.",
        "They are: " + held + ".",
        "Your settings are Enhanced now. You can keep this playthrough Faithful, or switch it to Enhanced so that those "
        "settings work in it.",
        "Warning: you can switch back later, but a playthrough that was Enhanced for a while no longer counts as "
        "Faithful from the start.",
        "Its label will always show that it was switched. Picture, sound and controls settings are yours either way.",
        "Keep this playthrough Faithful, or switch it to Enhanced?",
    };
  } else {
    kEngineChoices[0] = "Keep Enhanced", kEngineChoices[1] = "Switch to Faithful";
    ePages = {
        "This playthrough is marked Enhanced: it plays with your own gameplay settings.",
        "They are: " + held + ".",
        "Your settings are the Original ones now, so it plays like the GameCube game anyway. But it stays marked "
        "Enhanced.",
        "You can keep it Enhanced, or switch it to Faithful. Faithful holds those settings as on the GameCube, whatever "
        "you set later.",
        "Its label will always show that it was Enhanced for a while: it does not count as Faithful from the start. "
        "You can switch again later.",
        "Keep this playthrough Enhanced, or switch it to Faithful?",
    };
  }
}
void EClose(void*) { xd::pt::EngineAnswer(eAnswer); }

std::vector<std::string> PageLines(const std::string& text, float& scale, float& pitch) {
  const float room = 520.f;
  scale = 1.f, pitch = 28.f;
  std::vector<std::string> lines = Wrap(text, room);
  if (lines.size() <= 4) return lines;
  scale = 0.8f, pitch = 23.f;
  lines = Wrap(text, room / scale);
  if (lines.size() > 5) {
    scale = 0.7f, pitch = 19.f;
    lines = Wrap(text, room / scale);
    lines.resize(std::min<size_t>(lines.size(), 6));
  }
  return lines;
}

bool DrawFrameWide(ImDrawList* dl, const Screen& sc, s32 menu, int first, int last, float dx, float dy, float extra) {
  int minX = INT_MAX, maxX = INT_MIN;
  for (int i = first; i <= last; i++) {
    const ItemPic p = ItemPicture(menu, i);
    if (!p.ok) return false;
    minX = std::min<int>(minX, p.shot.sp.x), maxX = std::max<int>(maxX, p.shot.sp.x);
  }
  for (int i = first; i <= last; i++) {
    ItemPic p = ItemPicture(menu, i);
    const int x = p.shot.sp.x;
    float stretch = 0.f;
    if (x == maxX && maxX != minX) p.shot.sp.x = static_cast<s16>(x + extra);
    else if (x != minX) stretch = static_cast<float>(std::abs(p.shot.sp.w)) + extra;
    p.shot.sp.x = static_cast<s16>(p.shot.sp.x + dx), p.shot.sp.y = static_cast<s16>(p.shot.sp.y + dy);
    DrawSprite(dl, sc, p.shot, 0.f, nullptr, stretch);
  }
  return true;
}

bool EFrame(void*, const PortMenuInput* in) {
  bool choice = ePage + 1 >= static_cast<int>(ePages.size());
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    return false;
  }
  if (choice && (in->pressed & (PAD_BUTTON_UP | PAD_BUTTON_DOWN))) {
    eCursor ^= 1;
    PlayMenuSound(kTitleMenu, kSoundMove);
  }
  if (in->pressed & PAD_BUTTON_A) {
    if (choice) {
      eAnswer = eCursor == 0 ? 1 : 2;
      PlayMenuSound(kTitleMenu, kSoundChoose);
      return false;
    }
    ePage++;
    choice = ePage + 1 >= static_cast<int>(ePages.size());
    PlayMenuSound(kTitleMenu, kSoundMove);
  }
  const auto now = std::chrono::steady_clock::now();
  eAge += eLast.time_since_epoch().count() == 0 ? 0.f : std::clamp(std::chrono::duration<float>(now - eLast).count(), 0.f, 0.1f);
  eLast = now;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  FillWindow(dl, IM_COL32(0, 0, 0, 120));
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  const UseFont font(0);
  if (!DrawFrame(dl, sc, kMsgMenu, kMsgFrameFirst, kMsgFrameLast, 0.f, 0.f)) {
    dl->AddRectFilled(sc.At(27.f, 290.f), sc.At(613.f, 440.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  float scale = 1.f, pitch = 28.f;
  const std::vector<std::string> lines = PageLines(ePages[ePage], scale, pitch);
  for (size_t i = 0; i < lines.size(); i++) {
    DrawText(dl, sc, 52.f, 307.f + pitch * static_cast<float>(i), lines[i].c_str(), white, 1.f, scale);
  }
  const float pulse = 0.75f + 0.25f * std::cos(eAge * 6.28f);
  if (!choice) {
    const float by = 421.f + 2.f * std::sin(eAge * 6.28f);
    dl->AddTriangleFilled(sc.At(578.f, by), sc.At(592.f, by), sc.At(585.f, by + 8.f), IM_COL32(0xF0, 0xF0, 0xF0, 255));
    return true;
  }
  float textW = 0.f;
  for (const char* c : kEngineChoices) textW = std::max(textW, static_cast<float>(TextWidth(c)));
  const float extra = std::max(0.f, std::ceil(textW) - 64.f);
  if (!DrawFrameWide(dl, sc, kYesNoMenu, kYesNoFrameFirst, kYesNoFrameLast, kYesNoX, kYesNoY, extra)) {
    dl->AddRectFilled(sc.At(kYesNoX, kYesNoY), sc.At(kYesNoX + 159.f + extra, kYesNoY + 89.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  for (int i = 0; i < 2; i++) {
    const float rowY = kYesNoY + (i == 0 ? 10.f : 47.f);
    if (i == eCursor) {
      dl->AddRect(sc.At(kYesNoX + 21.f, rowY), sc.At(kYesNoX + 138.f + extra, rowY + 28.f),
                  IM_COL32(0xA8, 0xA8, 0xA8, static_cast<int>(255.f * pulse)), 14.f * sc.s, 0, 1.5f * sc.s);
      const ImVec2 c = sc.At(kYesNoX + 37.f, rowY + 14.f);
      dl->AddCircleFilled(c, 8.5f * sc.s, IM_COL32(0xD8, 0x18, 0x20, 255), 24);
      const ImVec2 pts[3] = {ImVec2(c.x - 2.5f * sc.s, c.y - 5.f * sc.s), ImVec2(c.x + 3.f * sc.s, c.y),
                             ImVec2(c.x - 2.5f * sc.s, c.y + 5.f * sc.s)};
      dl->AddPolyline(pts, 3, IM_COL32(0x18, 0x18, 0x18, 255), 0, 2.6f * sc.s);
    }
    DrawText(dl, sc, kYesNoX + 52.f, rowY + 5.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), kEngineChoices[i], white);
  }
  return true;
}
const PortMenuPage kEnginePage{"faithful or enhanced", EOpen, EFrame, EClose, nullptr};

std::string ruleText;
bool RuleFrame(void*, const PortMenuInput* in) {
  if (in->pressed & (PAD_BUTTON_A | PAD_BUTTON_B)) return false;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  const UseFont font(0);
  if (!DrawFrame(dl, sc, kMsgMenu, kMsgFrameFirst, kMsgFrameLast, 0.f, 0.f)) {
    dl->AddRectFilled(sc.At(27.f, 290.f), sc.At(613.f, 440.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  const std::vector<std::string> lines = Wrap(ruleText, 520.f);
  for (size_t i = 0; i < std::min<size_t>(lines.size(), 4); i++) {
    DrawText(dl, sc, 52.f, 307.f + 28.f * static_cast<float>(i), lines[i].c_str(), white);
  }
  return true;
}
const PortMenuPage kRulePage{"challenge rule", nullptr, RuleFrame, nullptr, nullptr};

std::string noteText;
std::chrono::steady_clock::time_point noteUntil{};
void DrawNote() {
  if (noteText.empty() || std::chrono::steady_clock::now() > noteUntil) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  const UseFont font(0);
  const std::vector<std::string> lines = Wrap(noteText, 520.f / 0.85f);
  const float h = 12.f + 18.f * static_cast<float>(lines.size());
  dl->AddRectFilled(sc.At(50.f, 440.f - h), sc.At(590.f, 448.f), IM_COL32(0, 0, 0, 200), 8.f * sc.s);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  for (size_t i = 0; i < lines.size(); i++) DrawText(dl, sc, 60.f, 446.f - h + 18.f * i, lines[i].c_str(), white, 0.85f, 0.85f);
}

}

constexpr u16 kUp = PAD_BUTTON_UP, kDown = PAD_BUTTON_DOWN, kLeft = PAD_BUTTON_LEFT, kRight = PAD_BUTTON_RIGHT;
u16 sPrevDirs = 0, sPrevButtons = 0;

u16 DirsOf(const PADStatus& s) {
  u16 d = s.button & (kUp | kDown | kLeft | kRight);
  const int x = s.stickX, y = s.stickY;
  if (std::abs(x) > 0x48 || std::abs(y) > 0x48) {
    const float sa = std::atan2(static_cast<float>(x), static_cast<float>(-y));
    const float a = std::fabs(sa);
    if (a < 0.9599311f) d |= kDown;
    else if (a > 2.1816616f) d |= kUp;
    if (a > 0.61086524f && a < 2.5307274f) d |= sa < 0.f ? kLeft : kRight;
  }
  return d;
}

void Neutral(PADStatus& s) {
  if (s.err != PAD_ERR_NONE) return;
  s.button = 0;
  s.stickX = s.stickY = s.substickX = s.substickY = 0;
  s.triggerLeft = s.triggerRight = 0;
  s.analogA = s.analogB = 0;
}

void TitleFilter(PADStatus* status, const WinWorkLayout* w, u16 newDirs, u16 newButtons) {
  const bool open = w->state == 2 && w->pad_a4 == 0;
  if (!open || w->cursor < 0 || w->cursor >= 5) {
    if (sTitleSelected) std::fprintf(stderr, "[settings menu] the title menu is no longer taking input: entry off\n");
    sTitleSelected = false;
    return;
  }
  if (!sTitleSelected) {
    if (menuTitleGetSelect(w->cursor) == kTitleOptions && (newDirs & kLeft) != 0) {
      sTitleSelected = true;
      PlayMenuSound(w->menuID, kSoundMove);
      PortInput_LatchHeld();
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
    }
    return;
  }
  if (newDirs & kRight) {
    sTitleSelected = false;
    PlayMenuSound(w->menuID, kSoundMove);
    PortInput_LatchHeld();
  } else if ((newDirs & (kLeft | kUp | kDown)) != 0 || (newButtons & PAD_BUTTON_B) != 0) {
    sTitleSelected = false;
    return;
  } else if (newButtons & PAD_BUTTON_A) {
    sPageFromTitle = true;
    if (PortMenu_Open(&kPage)) PlayMenuSound(w->menuID, kSoundChoose);
  }
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
}

void EntryFilter(PADStatus* status);

u16 sPtPrevButtons = 0;
void Filter(PADStatus* status) {
  xd::pt::PadPoll(status);
  const auto* w = static_cast<const WinWorkLayout*>(windowSearchID(windowGetActiveID()));
  u16 buttons = 0;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
    if (status[i].err == PAD_ERR_NONE) buttons |= status[i].button;
  }
  const u16 newButtons = buttons & ~sPtPrevButtons;
  sPtPrevButtons = buttons;
  if (std::string message; xd::challenges::RefusePress(w, newButtons, message)) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
      if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~newButtons);
    }
    PortInput_LatchHeld();
    xd::pt::RuleNote(message);
    PortXD_ShowRuleMessage(message);
    return;
  }
  if (xd::pt::ScreenEnabled() || xd::pt::EngineQuestionPossible()) {
    if (IsTitleMenu(w) && w->state == 2 && w->pad_a4 == 0 && w->cursor >= 0 && w->cursor < 5 && !sTitleSelected &&
        xd::pt::TitleInput(status, menuTitleGetSelect(w->cursor), newButtons)) {
      return;
    }
  }
  EntryFilter(status);
}

void EntryFilter(PADStatus* status) {
  if (!sOn) return;
  const auto* w = static_cast<const WinWorkLayout*>(windowSearchID(windowGetActiveID()));
  const FieldMenu* m = MenuOf(w);
  const bool open = m != nullptr && w->state == 2 && w->pad_a4 == 0;
  u16 dirs = 0, buttons = 0;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
    if (status[i].err != PAD_ERR_NONE) continue;
    dirs |= DirsOf(status[i]);
    buttons |= status[i].button;
  }
  const u16 newDirs = dirs & ~sPrevDirs, newButtons = buttons & ~sPrevButtons;
  sPrevDirs = dirs;
  sPrevButtons = buttons;
  if (IsTitleMenu(w)) {
    TitleFilter(status, w, newDirs, newButtons);
    return;
  }
  if (sTitleSelected) {
    std::fprintf(stderr, "[settings menu] the title menu is no longer taking input: entry off\n");
    sTitleSelected = false;
  }
  if (!open) {
    if (sSelected) std::fprintf(stderr, "[settings menu] the field menu is no longer taking input: entry off\n");
    sSelected = false;
    return;
  }
  if (!sSelected) {
    if (w->cursor == m->exitIndex && (newDirs & kDown) != 0) {
      sSelected = true;
      PlayMenuSound(w->menuID, kSoundMove);
      PortInput_LatchHeld();
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
    }
    return;
  }
  if (newDirs & kDown) {
    sSelected = false;
    return;
  }
  if (newButtons & (PAD_BUTTON_B | PAD_BUTTON_START)) {
    sSelected = false;
    return;
  }
  if (newDirs & kUp) {
    sSelected = false;
    PlayMenuSound(w->menuID, kSoundMove);
    PortInput_LatchHeld();
  } else if (newButtons & PAD_BUTTON_A) {
    sPageFromTitle = false;
    if (PortMenu_Open(&kPage)) PlayMenuSound(w->menuID, kSoundChoose);
  }
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
}

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}
bool EnvSet(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0';
}

void PlayMenuSound(s32 menuId, int event) {
  const auto* md = static_cast<const uint8_t*>(menuDataBiosGetPtr(menuId));
  if (md == nullptr || snd_data == nullptr || snd_data_number == nullptr) return;
  void* se = menuSeBiosGetPtr(md[1] >> 4);
  if (se == nullptr) return;
  const u16 snd = menuSeBiosGetSndId(se, static_cast<u8>(event));
  if (snd == 0 || snd >= *snd_data_number) return;
  const SndData& d = static_cast<const SndData*>(snd_data)[snd];
  if (d.se != 1) return;
  if (const char* dir = std::getenv("XD_SFX_WAV_DIR")) {
    static std::map<int, bool> written;
    if (!written[d.fxid]) {
      written[d.fxid] = true;
      const std::string path = std::string(dir) + "/menu_sound_" + std::to_string(event) + "_fx" + std::to_string(d.fxid) + ".wav";
      std::fprintf(stderr, "[settings menu] menu sound %d: GSsnd sound %u, sound effect %u -> %s (%s)\n", event, snd, d.fxid,
                   path.c_str(), PortSfx_WriteWav(d.fxid, 0x7F, 0x40, path.c_str()) ? "written" : "could not be made");
    }
  }
  if (!PortSfx_Play(d.fxid, 0x7F, 0x40)) {
    static bool told = false;
    if (!told) std::fprintf(stderr, "[settings menu] menu sound %u (sound effect %u) could not be played\n", snd, d.fxid);
    told = true;
  }
}

s32 PageSoundMenu() { return sPageFromTitle ? kTitleMenu : 0x57; }

bool MenuLayerMissing(void* window, void* sprite) {
  if (window == nullptr || sprite == nullptr) return false;
  const s32 layer = winSpriteGetLayerID(window, sprite);
  if (GSgfxLayerFindByID(layer) != nullptr) return false;
  const auto* w = static_cast<const WinWorkLayout*>(window);
  const auto* sp = static_cast<const SpriteWorkLayout*>(sprite);
  static std::map<int64_t, bool> told;
  const int64_t key = static_cast<int64_t>(w->menuID) << 16 | static_cast<uint16_t>(sp->id);
  if (!told[key]) {
    told[key] = true;
    unsigned layers = 0;
    for (int i = 0; i < 16; i++) layers |= GSgfxLayerFindByID(i) != nullptr ? 1u << i : 0u;
    std::fprintf(stderr,
                 "[menu guard] menu 0x%X sprite %d (b74 %u) is on layer %d, which doesn't exist: not drawn (game frame "
                 "%u; floor 0x%X state %d, next floor 0x%X, layers 0x%04X; a port/decomp bug, please report)\n",
                 w->menuID, sp->id, sp->b74, layer, PortVI_GetFlipCount(), GSfloorGetCurrent(), floorGetFloorState(),
                 floorGetNextChangeID(), layers);
  }
  return true;
}

}

extern "C" {

void PortXD_SettingsMenuInit(void) {
  const es::Registry& reg = es::Global();
  PortAudio_SetVolume(static_cast<float>(reg.Int("audio.master_volume").Get()) / 100.f);
  PortXD_AudioCategoriesInit();
  ApplyMenuSoundVolume();
  PortMenu_SetScriptPage(&kPage);
  if (const char* v = std::getenv("XD_MENU_SPRITE_LOG")) sLogMenu = static_cast<int>(std::strtol(v, nullptr, 0));
  if (const char* v = std::getenv("XD_MENU_TEX_PROBE")) sProbeMenu = static_cast<int>(std::strtol(v, nullptr, 0));
  sLookScreen = EnvIs("XD_SETTINGS_LOOK", "screen");
  const char* why = nullptr;
  if (EnvIs("XD_SETTINGS_ENTRY", "1")) {
    sOn = true;
  } else if (EnvIs("XD_SETTINGS_ENTRY", "0")) {
    why = "XD_SETTINGS_ENTRY=0";
  } else if (!reg.Bool("system.settings_entry").Get()) {
    why = "system.settings_entry is off";
  } else if (EnvIs("XD_REQUIRE_ORIGINAL", "1")) {
    why = "XD_REQUIRE_ORIGINAL (comparison run)";
  } else if (EnvSet("XD_REPLAY")) {
    why = "a replay";
  } else if (EnvSet("XD_INPUT_SCRIPT")) {
    why = "scripted presses";
  } else if (EnvSet("XD_RENDER_ICON")) {
    why = "icon pictures";
  } else {
    sOn = true;
  }
  if (sOn || xd::pt::ScreenEnabled() || xd::pt::ChallengesPossible() || xd::pt::EngineQuestionPossible()) PortInput_SetPadFilter(Filter);
  if (sOn) {
    std::fprintf(stderr, "[settings menu] SETTINGS after EXIT in the field menu, left of OPTIONS in the title menu\n");
  } else {
    std::fprintf(stderr, "[settings menu] no SETTINGS entry this run (%s)\n", why);
  }
}

u8 PortXD_SndPushGroup(void* prj, u16 gid, void* samples, void* sampdir, void* pool) {
  const u8 ok = sndPushGroup(prj, gid, samples, sampdir, pool);
  if (ok) PortSfx_NoteGroup(prj, gid, sampdir, pool);
  return ok;
}

void PortXD_WideMenuSprite(const void* window, const void* sprite);
void PortXD_WideMenuSpriteDone(void);
static void MenuWinSpriteDraw(void* window, void* sprite);
void PortXD_MenuWinSpriteDraw(void* window, void* sprite) {
  PortXD_WideMenuSprite(window, sprite);
  MenuWinSpriteDraw(window, sprite);
  PortXD_WideMenuSpriteDone();
}
static void MenuWinSpriteDraw(void* window, void* sprite) {
  LogSprite(static_cast<const WinWorkLayout*>(window), static_cast<const SpriteWorkLayout*>(sprite));
  if (MenuLayerMissing(window, sprite)) return;
  if (sOn && IsTitleMenu(window)) {
    auto* w = static_cast<WinWorkLayout*>(window);
    auto* sp = static_cast<SpriteWorkLayout*>(sprite);
    const uint32_t flip = PortVI_GetFlipCount();
    if (sTitleBuilding.flip != flip) {
      sTitleBuilding = TitleCapture{};
      sTitleBuilding.flip = flip;
    }
    KeepOptionsPictures();
    const s16 id = sp->id, y = sp->y;
    if (sTitleSelected && id == kOptFrame) sp->y = -4000;
    winSpriteDraw(window, sprite);
    sp->y = y;
    TitleCapture& c = sTitleBuilding;
    if (id == kOptBox) c.box = ShotOf(w, sp, y);
    else if (id == kOptFrame) c.frame = ShotOf(w, sp, y);
    else if (id == kOptText) c.text = ShotOf(w, sp, y);
    else if (id == kTitleInfo) c.info = ShotOf(w, sp, y);
    c.any = c.box.valid;
    return;
  }
  const FieldMenu* m = sOn ? MenuOf(window) : nullptr;
  if (m == nullptr) {
    winSpriteDraw(window, sprite);
    return;
  }
  auto* w = static_cast<WinWorkLayout*>(window);
  auto* sp = static_cast<SpriteWorkLayout*>(sprite);
  const uint32_t flip = PortVI_GetFlipCount();
  if (sBuilding.flip != flip || sBuilding.menu != m) {
    sBuilding = Capture{};
    sBuilding.menu = m;
    sBuilding.flip = flip;
  }
  const s16 id = sp->id;
  const s16 y = sp->y;
  const bool hide = sSelected && (id == m->bar || id == m->flash || id == m->hand);
  const s16 h = sp->h;
  if (id == m->cap) {
    sp->y = static_cast<s16>(y + kRow);
  } else if (id == m->side) {
    sp->h = static_cast<s16>(h + kRow);
  } else if (hide) {
    sp->y = -4000;
  }
  winSpriteDraw(window, sprite);
  sp->y = y;
  sp->h = h;
  Capture& c = sBuilding;
  if (id == m->button) c.button = ShotOf(w, sp, y);
  else if (id == m->bar) c.bar = ShotOf(w, sp, y);
  else if (id == m->flash) {
    c.flash = ShotOf(w, sp, y);
    if (c.flash.valid && flip > sLastRotFlip) {
      float d = sp->rot - sLastRot;
      if (flip - sLastRotFlip <= 2 && d > 0.f && d < 1.f) sFlashSpeed = d / (flip - sLastRotFlip) * 30.f;
      sLastRot = sp->rot;
      sLastRotFlip = flip;
    }
  } else if (id == m->text) c.text = ShotOf(w, sp, y);
  else if (id == m->hand) c.hand = ShotOf(w, sp, y);
  c.any = c.button.valid;
}

s32 PortXD_FieldMenuPrint2(s32 layer, s32 x, s32 y, s32 color, u32 msg) {
  if (sOn && sSelected && msg == 0x429B) {
    color = static_cast<s32>((static_cast<u32>(color) & 0xFFFFFF00u) | 0x80u);
  }
  return GSmsgPrint2(layer, x, y, static_cast<u32>(color), msg);
}

void PortXD_TitleMenuPrint2(s32 layer, s32 x, s32 y, s32 color, s32 msg) {
  if (sOn && sTitleSelected && static_cast<u32>(msg) == kOptionsInfoMsg) {
    color = static_cast<s32>(static_cast<u32>(color) & 0xFFFFFF00u);
  }
  GSmsgPrint2(layer, x, y, static_cast<u32>(color), static_cast<u32>(msg));
}

void PortXD_SettingsMenuOverlay(void) {
  ProbeItems();
  if (const char* t = std::getenv("XD_SFX_GAME_TEST")) {
    static bool done = false;
    unsigned frame = 0, snd = 0;
    if (!done && std::sscanf(t, "%u:%u", &frame, &snd) == 2 && PortVI_GetFlipCount() >= frame) {
      done = true;
      std::fprintf(stderr, "[settings menu] XD_SFX_GAME_TEST: the game plays sound %u at game frame %u\n", snd,
                   PortVI_GetFlipCount());
      GSsndPlayNow(snd);
    }
  }
  if (!PortMenu_Active()) ng::DrawNote();
  if (!sOn || PortMenu_Active()) return;
  const uint32_t flip = PortVI_GetFlipCount();
  if (sTitleBuilding.any) sTitleShown = sTitleBuilding;
  if (sTitleShown.any && flip <= sTitleShown.flip + 2) {
    DrawTitleEntry(ImGui::GetBackgroundDrawList(), sTitleShown, sTitleSelected);
  } else {
    sTitleShown.any = false;
  }
  if (sBuilding.any) sShown = sBuilding;
  if (!sShown.any || flip > sShown.flip + 2) {
    sShown.any = false;
    return;
  }
  DrawEntry(ImGui::GetBackgroundDrawList(), sShown, sSelected);
}

}

bool PortXD_OpenNewGameSetup() { return PortMenu_Open(&ng::kSetupPage); }
bool PortXD_OpenContinueQuestion() { return PortMenu_Open(&ng::kQuestionPage); }
bool PortXD_OpenEngineQuestion() { return PortMenu_Open(&ng::kEnginePage); }
bool PortXD_ShowRuleMessage(const std::string& text) {
  if (PortMenu_Active() || PortMenu_Pending()) return false;
  ng::ruleText = text;
  return PortMenu_Open(&ng::kRulePage);
}
void PortXD_ShowNote(const std::string& text, float seconds) {
  ng::noteText = text;
  ng::noteUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000.f));
}

