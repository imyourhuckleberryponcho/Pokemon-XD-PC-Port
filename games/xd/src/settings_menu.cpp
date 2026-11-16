#include "i18n.h"
#include "xd_battle_models.h"
#include "xd_playthrough.h"
#include "xd_speedrun.h"
#include "xd_challenges.h"
#include "xd_shiny.h"
#include "xd_gen4.h"
#include "xd_languages.h"
#include "xd_randomizer.h"
#include "xd_starter.h"
#include "xd_user_folder.h"
#include "xd_test_run.h"
#include "online_battle.h"
#include "online_lobby.h"
#include "online_team.h"
#include "online_draft.h"
#include "online_draft_game.h"
#include "trainer_card_game.h"
#include <xd_trade.h>
#include <net_connection.h>
#include <fstream>
#include <thread>
#include <filesystem>
#include <memory>
#include <net_crypto.h>
#include <save_slots.h>
#include <xd_legality_disc.h>
#include <xd_trade_online.h>
#include "xd_pc_trade.h"
#include "xd_ui.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>

#include <gx_texture_decode.h>
#include <port_audio.h>
#include <port_disc.h>
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
#include <cstdarg>
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
s32 menuIsCheck(s32 id);
s32 menuCloseCustom(s32 id, u32 flags, u8 wait);
void PortVideo_SetNoticeDrawer(bool (*draw)(const char* text));
}
uint32_t PortXD_HintButtonStyle();
bool PortXD_HintButtonPicture(int gcInput, int w, int h, std::vector<uint8_t>& rgba, uint32_t& style);
extern "C" {
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
const char* PortXD_DiscPath(void);
struct XDCommonTables;
const struct XDCommonTables* PortXD_LiveCommonTables(void);
void PortXD_AudioCategoriesInit(void);
void PortXD_TrainerCardScriptPage(void);
const PortMenuPage* PortXD_TrainerCardPage(void);
int PortXD_AudioCategoryVolumeChanged(const char* id, int percent);
}

namespace {

namespace es = engine::settings;
using engine::i18n::Tr;

std::string Fmt(const char* format, ...) {
  va_list args, again;
  va_start(args, format);
  va_copy(again, args);
  const int n = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) std::vsnprintf(out.data(), out.size() + 1, format, again);
  va_end(again);
  return out;
}

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
const char* TitleInfoText() { return Tr("Online, mods and settings (PC version)."); }
const char* TitleExtrasText() { return Tr("EXTRAS"); }
constexpr int kModsW = 104, kModsGap = 10;
const char* ModsText() { return Tr("MODS"); }
const char* ModsInfoText() { return Tr("Switch the installed mods on or off (PC version)."); }
constexpr uint16_t kTitleFont = 1;

bool IsTitleMenu(const void* window) {
  return window != nullptr && static_cast<const WinWorkLayout*>(window)->menuID == kTitleMenu;
}

bool sOn = false;
bool sSelected = false;
bool sTitleSelected = false;
bool sTitleMods = false;
bool sTitleOnline = false;
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

uint32_t NextCodePoint(const char*& p) {
  const uint8_t c = static_cast<uint8_t>(*p);
  const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  if (extra == 0 || c >= 0xF8) {
    p++;
    return c;
  }
  uint32_t cp = c & (0x3F >> extra);
  const char* q = p + 1;
  for (int i = 0; i < extra; i++, q++) {
    if ((static_cast<uint8_t>(*q) & 0xC0) != 0x80) {
      p++;
      return c;
    }
    cp = cp << 6 | (static_cast<uint8_t>(*q) & 0x3F);
  }
  p = q;
  return cp;
}

const char* FoldOf(uint32_t cp) {
  switch (cp) {
  case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
  case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
  case 0xC6: return "AE";
  case 0xE6: return "ae";
  case 0xC7: return "C";
  case 0xE7: return "c";
  case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
  case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
  case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
  case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
  case 0xD1: return "N";
  case 0xF1: return "n";
  case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return "O";
  case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return "o";
  case 0x152: return "OE";
  case 0x153: return "oe";
  case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
  case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
  case 0xDD: return "Y";
  case 0xFD: case 0xFF: return "y";
  case 0xDF: return "ss";
  case 0xA1: case 0xBF: return "";
  case 0x2018: case 0x2019: return "'";
  case 0x201C: case 0x201D: case 0xAB: case 0xBB: return "\"";
  case 0x2013: case 0x2014: return "-";
  case 0x2026: return "...";
  case 0x2022: return "*";
  case 0xB0: return "o";
  default: return nullptr;
  }
}

struct Cell {
  const Glyph* g = nullptr;
  bool space = false;
};

template <class F> void EachCell(const char* text, F&& f) {
  for (const char* p = text; *p != '\0';) {
    const uint32_t cp = NextCodePoint(p);
    if (cp == ' ' || cp == 0xA0) {
      f(Cell{nullptr, true});
      continue;
    }
    if (cp < 0x10000) {
      if (const Glyph* g = GlyphOf(static_cast<uint16_t>(cp))) {
        f(Cell{g, false});
        continue;
      }
    }
    if (const char* fold = FoldOf(cp)) {
      for (; *fold != '\0'; fold++) f(Cell{GlyphOf(static_cast<uint8_t>(*fold)), false});
      continue;
    }
    f(Cell{nullptr, false});
  }
}

int TextWidth(const char* text) {
  int w = 0;
  EachCell(text, [&](const Cell& c) { w += c.space ? sFontW / 2 : c.g != nullptr ? c.g->w : sFontW; });
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
  EachCell(text, [&](const Cell& c) {
    if (c.space) {
      cx += static_cast<float>(sFontW / 2) * xs;
      return;
    }
    const Glyph* g = c.g;
    if (g == nullptr) {
      cx += static_cast<float>(sFontW) * xs;
      return;
    }
    const float gy = y + (static_cast<float>(g->yofs) - 2.f) * scale;
    const float gw = static_cast<float>(g->w) * xs, gh = static_cast<float>(g->h) * scale;
    dl->AddImage(g->id, sc.At(cx + 1, gy + 1), sc.At(cx + 1 + gw, gy + 1 + gh), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(0, 0, 0, shadowA));
    dl->AddImage(g->id, sc.At(cx, gy), sc.At(cx + gw, gy + gh), ImVec2(0, 0), ImVec2(1, 1), Rgba(rgba));
    cx += gw;
  });
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

const char* EntryName() { return Tr("SETTINGS"); }

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
    const char* name = EntryName();
    int rectW = TextWidth(name);
    const int rectH = sFontW + 1;
    const float squeeze = rectW > t.w && t.w > 0 ? static_cast<float>(t.w) / static_cast<float>(rectW) : 1.f;
    rectW = static_cast<int>(static_cast<float>(rectW) * squeeze);
    const int x = (t.w - rectW) / 2, y = (t.h - rectH) / 2 - 2;
    uint8_t col[4] = {0xF0, 0xF0, 0xF0, static_cast<uint8_t>(selected ? 0xFF : 0x80)};
    col[3] = static_cast<uint8_t>(col[3] * cap.text.winColor[3] / 255);
    DrawText(dl, sc, static_cast<float>(cap.text.offX + t.x + x), static_cast<float>(cap.text.offY + t.y + y + kRow),
             name, col, squeeze);
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
  float tw = static_cast<float>(TextWidth(text));
  const float th = static_cast<float>(sFontW + 1);
  float squeeze = 1.f;
  if (w > 8.f && tw > w) squeeze = w / tw, tw = w;
  DrawText(dl, sc, std::floor(x + (w - tw) / 2.f), std::floor(y + (h - th) / 2.f), text, rgba, squeeze);
}

void DrawFitted(ImDrawList* dl, const Screen& sc, float x, float y, const char* text, const uint8_t rgba[4], float maxW) {
  const float tw = static_cast<float>(TextWidth(text));
  DrawText(dl, sc, x, y, text, rgba, tw > maxW && maxW > 8.f ? maxW / tw : 1.f);
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
                    static_cast<float>(t.w), static_cast<float>(t.h), TitleExtrasText(), col);
  }
  if (selected && cap.info.seen) {
    uint8_t col[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.info.winColor[3] / 255);
    const float ix = static_cast<float>(cap.info.offX + cap.info.sp.x);
    DrawFitted(dl, sc, ix, static_cast<float>(cap.info.offY + cap.info.sp.y - 8), TitleInfoText(), col, 624.f - ix);
  }
}

void DrawTitleModsEntry(ImDrawList* dl, const TitleCapture& cap, bool selected) {
  if (!cap.any) return;
  const Screen sc = ScreenNow();
  const float boxW = static_cast<float>(std::abs(cap.box.sp.w));
  const float x = static_cast<float>(cap.box.offX + cap.box.sp.x + kTitleDx - kModsGap - kModsW);
  const float y = static_cast<float>(cap.box.offY + cap.box.sp.y);
  DrawSliced(dl, sc, cap.box, x, y, static_cast<float>(kModsW), 22.f, 22.f);
  if (selected && cap.frame.seen) {
    const float fx = x + static_cast<float>(cap.frame.sp.x - cap.box.sp.x) + static_cast<float>(cap.frame.offX - cap.box.offX);
    const float fw = static_cast<float>(kModsW) + static_cast<float>(std::abs(cap.frame.sp.w)) - boxW;
    DrawSliced(dl, sc, cap.frame, fx, static_cast<float>(cap.frame.offY + cap.frame.sp.y), fw, 24.f, 24.f);
  }
  if (cap.text.seen) {
    const UseFont font(kTitleFont);
    const SpriteWorkLayout& t = cap.text.sp;
    uint8_t col[4] = {0xF0, 0xF0, 0xF0, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.text.winColor[3] / 255);
    DrawTextCentred(dl, sc, x, static_cast<float>(cap.text.offY + t.y), static_cast<float>(kModsW), static_cast<float>(t.h), ModsText(),
                    col);
  }
  if (selected && cap.info.seen) {
    uint8_t col[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.info.winColor[3] / 255);
    const float ix = static_cast<float>(cap.info.offX + cap.info.sp.x);
    DrawFitted(dl, sc, ix, static_cast<float>(cap.info.offY + cap.info.sp.y - 8), ModsInfoText(), col, 624.f - ix);
  }
}

constexpr int kOnlineW = 140, kOnlineDy = 33;
const char* OnlineText() { return Tr("ONLINE"); }
const char* OnlineInfoText() { return Tr("Trade Pokemon with a friend online (PC version)."); }
void DrawTitleOnlineEntry(ImDrawList* dl, const TitleCapture& cap, bool selected) {
  if (!cap.any) return;
  const Screen sc = ScreenNow();
  const float boxW = static_cast<float>(std::abs(cap.box.sp.w));
  const float x = static_cast<float>(cap.box.offX + cap.box.sp.x + kTitleDx - kModsGap - kModsW);
  const float y = static_cast<float>(cap.box.offY + cap.box.sp.y + kOnlineDy);
  DrawSliced(dl, sc, cap.box, x, y, static_cast<float>(kOnlineW), 22.f, 22.f);
  if (selected && cap.frame.seen) {
    const float fx = x + static_cast<float>(cap.frame.sp.x - cap.box.sp.x) + static_cast<float>(cap.frame.offX - cap.box.offX);
    const float fw = static_cast<float>(kOnlineW) + static_cast<float>(std::abs(cap.frame.sp.w)) - boxW;
    DrawSliced(dl, sc, cap.frame, fx, static_cast<float>(cap.frame.offY + cap.frame.sp.y + kOnlineDy), fw, 24.f, 24.f);
  }
  if (cap.text.seen) {
    const UseFont font(kTitleFont);
    const SpriteWorkLayout& t = cap.text.sp;
    uint8_t col[4] = {0xF0, 0xF0, 0xF0, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.text.winColor[3] / 255);
    DrawTextCentred(dl, sc, x, static_cast<float>(cap.text.offY + t.y + kOnlineDy), static_cast<float>(kOnlineW), static_cast<float>(t.h),
                    OnlineText(), col);
  }
  if (selected && cap.info.seen) {
    uint8_t col[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    col[3] = static_cast<uint8_t>(col[3] * cap.info.winColor[3] / 255);
    DrawText(dl, sc, static_cast<float>(cap.info.offX + cap.info.sp.x), static_cast<float>(cap.info.offY + cap.info.sp.y - 8),
             OnlineInfoText(), col);
  }
}

struct Row {
  const char* id;
  const char* (*label)();
  int step;
  bool enhancedOnly;
  const char* unit;
};
const Row kRows[] = {
    {"video.resolution_scale", [] { return Tr("Resolution"); }, 1, false, ""},
    {"video.widescreen", [] { return Tr("Widescreen"); }, 0, false, ""},
    {"video.fullscreen", [] { return Tr("Full screen"); }, 0, false, ""},
    {"video.borderless_window", [] { return Tr("Borderless window"); }, 0, false, ""},
    {"video.frame_rate", [] { return Tr("Frame rate"); }, 0, false, ""},
    {"video.fps_counter", [] { return Tr("Frame rate counter"); }, 0, false, ""},
    {"x.speedrun_timer", [] { return Tr("Speedrun timer"); }, 0, false, ""},
    {"x.speedrun_category", [] { return Tr("Timer category"); }, 0, false, ""},
    {"x.speedrun_start", [] { return Tr("Timer start (Custom)"); }, 0, false, ""},
    {"x.speedrun_end", [] { return Tr("Timer end (Custom)"); }, 0, false, ""},
    {"video.speedrun_timer_position", [] { return Tr("Timer position"); }, 0, false, ""},
    {"video.speedrun_timer_size", [] { return Tr("Timer size"); }, 10, false, "%"},
    {"video.speedrun_timer_splits", [] { return Tr("Timer split list"); }, 0, false, ""},
    {"x.play_stats", [] { return Tr("Play statistics"); }, 0, false, ""},
    {"audio.master_volume", [] { return Tr("Volume"); }, 10, false, "%"},
    {"audio.music_volume", [] { return Tr("Music"); }, 10, false, "%"},
    {"audio.sfx_volume", [] { return Tr("Sound effects"); }, 10, false, "%"},
    {"audio.cry_volume", [] { return Tr("Cries"); }, 10, false, "%"},
    {"audio.menu_music", [] { return Tr("Music in menus"); }, 0, false, ""},
    {"controls.button_icons", [] { return Tr("Button pictures"); }, 0, false, ""},
    {"system.language", [] { return Tr("Language"); }, 0, false, ""},
    {"language.restart_now", [] { return Tr("Restart now"); }, 0, false, ""},
    {"language.next_start", [] { return Tr("Next start"); }, 0, false, ""},
    {"gameplay.text_speed", [] { return Tr("Text speed"); }, 0, true, ""},
    {"gameplay.fast_forward", [] { return Tr("Fast-forward"); }, 0, true, ""},
    {"gameplay.fast_forward_mode", [] { return Tr("Fast-forward key"); }, 0, true, ""},
    {"gameplay.fast_forward_speed", [] { return Tr("Fast-forward speed"); }, 1, true, "x"},
    {"gameplay.fast_battle_animations", [] { return Tr("Fast animations"); }, 0, true, ""},
    {"gameplay.physical_special_split", [] { return Tr("Physical/special split"); }, 0, true, ""},
    {"gameplay.tm_move_names", [] { return Tr("TMs show their move"); }, 0, true, ""},
    {"video.battle_revolution_models", [] { return Tr("Battle Revolution models"); }, 0, true, ""},
    {"system.pause_on_focus_loss", [] { return Tr("Pause in the background"); }, 0, true, ""},
    {nullptr, [] { return Tr("Back"); }, 0, false, ""},
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

int TimerWhich(const Row& r) {
  if (r.id == nullptr) return -1;
  if (std::strcmp(r.id, "x.speedrun_timer") == 0) return 0;
  if (std::strcmp(r.id, "x.speedrun_start") == 0) return 1;
  if (std::strcmp(r.id, "x.speedrun_end") == 0) return 2;
  if (std::strcmp(r.id, "x.speedrun_category") == 0) return 3;
  return -1;
}
bool IsTimerRow(const Row& r) { return TimerWhich(r) >= 0; }
bool IsStatsRow(const Row& r) { return r.id != nullptr && std::strcmp(r.id, "x.play_stats") == 0; }
bool IsSpecialRow(const Row& r) { return IsTimerRow(r) || IsStatsRow(r); }
const char* const kTimerModes[] = {"Off", "Real time", "Without loads"};

bool IsLanguageRow(const char* id) { return id != nullptr && std::strcmp(id, xd::languages::kSetting) == 0; }
xd::languages::Pack LanguagePack() {
  static xd::languages::Pack pack;
  static std::chrono::steady_clock::time_point at{};
  const auto now = std::chrono::steady_clock::now();
  if (at == std::chrono::steady_clock::time_point{} || now - at > std::chrono::seconds(1)) {
    pack = xd::languages::ReadPack(xd::SettingsFolder());
    at = now;
  }
  return pack;
}
bool LanguageInGame(const std::string& id) { return id == "us" || LanguagePack().Has(id); }
bool sLangAck = false;
bool sLangConfirm = false;
bool IsActionRow(const char* id) { return id != nullptr && std::strncmp(id, "language.", 9) == 0; }
bool LanguagePending() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(xd::languages::kSetting);
  if (i == es::kNoSetting) return false;
  const std::string want = reg.ValueText(i);
  return (LanguageInGame(want) ? want : std::string("us")) != xd::languages::Active()->id;
}

bool RowShown(const Row& r) {
  if (r.id == nullptr) return true;
  if (IsTimerRow(r)) return true;
  if (IsStatsRow(r)) return xd::speedrun::PlaythroughChosen();
  if (IsActionRow(r.id)) return LanguagePending() && !sLangAck;
  if (std::strcmp(r.id, "gameplay.physical_special_split") == 0) return false;
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  if (i == es::kNoSetting || !reg.Get(i).implemented) return false;
  if (std::strcmp(r.id, xd::battle_models::kSetting) == 0 && !PortXD_BattleModelsFound()) return false;
  return !r.enhancedOnly || reg.CurrentPreset() != es::Preset::Original;
}

std::string ValueText(const Row& r) {
  if (r.id == nullptr) return "";
  if (IsTimerRow(r)) return xd::speedrun::RowName(TimerWhich(r), xd::speedrun::RowValue(TimerWhich(r)));
  if (IsStatsRow(r)) return Tr("See below");
  if (IsActionRow(r.id)) return std::strcmp(r.id, "language.restart_now") == 0 && sLangConfirm ? Tr("Press A again") : "";
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find(r.id);
  const es::Definition& d = reg.Get(i);
  const es::Value v = reg.ValueOf(i);
  switch (d.type) {
  case es::Type::Bool: return v.i ? Tr("On") : Tr("Off");
  case es::Type::Enum:
    if (v.i < 0 || v.i >= static_cast<int32_t>(d.choices.size())) return "?";
    if (IsLanguageRow(r.id)) return engine::i18n::LanguageName(d.choices[v.i].id);
    return Tr(d.choices[v.i].name);
  case es::Type::Int: {
    std::string name = d.IntName(v.i);
    if (!name.empty()) {
      if (name == "Match the window") return Tr("Window");
      name = Tr(name);
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

void RebuildRows() {
  const Row* cur = sPage.cursor < static_cast<int>(sPage.rows.size()) ? sPage.rows[sPage.cursor] : nullptr;
  sPage.rows.clear();
  for (const Row& r : kRows) {
    if (RowShown(r)) sPage.rows.push_back(&r);
  }
  sPage.cursor = 0;
  for (size_t k = 0; k < sPage.rows.size(); k++) {
    if (sPage.rows[k] == cur) sPage.cursor = static_cast<int>(k);
  }
}

void Change(const Row& r, int dir) {
  if (IsTimerRow(r)) {
    const int w = TimerWhich(r), n = xd::speedrun::RowCount(w);
    xd::speedrun::SetRow(w, (xd::speedrun::RowValue(w) + dir + n) % n);
    sPage.changed = true;
    return;
  }
  if (IsStatsRow(r)) return;
  if (IsActionRow(r.id)) return;
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
  if (IsLanguageRow(r.id)) {
    engine::i18n::SetLanguage(d.choices[v.i].id);
    sLangAck = false;
    sLangConfirm = false;
    RebuildRows();
  }
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
  static bool kept = false, framesKept = false;
  if (!framesKept) {
    int f = 0;
    for (int i = 36; i <= 44; i++) f += ItemPicture(0x33, i).ok ? 1 : 0;
    for (int i = 66; i <= 74; i++) f += ItemPicture(0x35, i).ok ? 1 : 0;
    framesKept = f == 18;
  }
  if (kept) return;
  int n = 0;
  for (int i = kOptBgTop; i <= kOptExitCursor; i++) n += ItemPicture(kOptMenu, i).ok ? 1 : 0;
  if (n == kOptExitCursor - kOptBgTop + 1) {
    kept = true;
    std::fprintf(stderr, "[settings menu] the OPTIONS screen's pictures kept for the settings screen\n");
  }
}

bool TwoChoices(const Row& r) {
  if (r.id == nullptr || IsSpecialRow(r) || IsActionRow(r.id)) return false;
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
    left = Tr("On"), right = Tr("Off");
    return v.i ? 0 : 1;
  }
  left = Tr(d.choices[0].name), right = Tr(d.choices[1].name);
  if (std::strcmp(r.id, "video.frame_rate") == 0) left = "30", right = "60";
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
  if (r.id == nullptr) return Tr("Save any changes and leave the settings.");
  if (IsTimerRow(r)) return xd::speedrun::TimerRowInfo(TimerWhich(r));
  if (IsStatsRow(r)) return xd::speedrun::StatsText();
  const es::Registry& reg = es::Global();
  if (IsActionRow(r.id)) {
    return std::strcmp(r.id, "language.restart_now") == 0
               ? Tr("Starts the game again now in the new language. Progress you have not saved is lost.")
               : Tr("Keeps the new language for the next time the game starts. Nothing happens now.");
  }
  if (IsLanguageRow(r.id)) {
    if (!LanguageInGame(reg.ValueText(reg.Find(r.id)))) {
      return Tr("Game text needs the European disc pack; menus added by this port are translated.");
    }
    if (LanguagePending() && sLangAck) return Tr("The new language is used the next time the game starts.");
    return Tr("Takes effect the next time the game starts.");
  }
  return Tr(reg.Get(reg.Find(r.id)).description);
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

#include "ui_kit.inc"

void DrawSettingsScreen(ImDrawList* dl, float dt) {
  const Screen sc = ScreenNow();
  const int n = static_cast<int>(sPage.rows.size());
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  kit::OptionsScreen(dl, sc);
  for (int i = kOptDescFirst; i <= kOptDescLast; i++) DrawItemAsIs(dl, sc, i);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  {
    const UseFont font(kScrFont);
    DrawText(dl, sc, 82.f, 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), EntryName(), white);
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
      DrawTextCentred(dl, sc, 437.f, mid - 16.f, 144.f, 32.f, Tr("EXIT"), white);
      continue;
    }
    const char* rowLabel = r.label();
    const float labelW = static_cast<float>(TextWidth(rowLabel)), room = 321.f - 8.f - 74.f;
    DrawText(dl, sc, 74.f, std::floor(mid - static_cast<float>(sFontW + 1) / 2.f), rowLabel, white,
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
    if (IsActionRow(row.id)) {
      if (in->pressed & PAD_BUTTON_A) {
        if (std::strcmp(row.id, "language.restart_now") == 0) {
          if (!sLangConfirm) {
            sLangConfirm = true;
          } else {
            xd::SaveSettings(es::Global());
            xd::pt::RestartNow("language changed");
            sound.event = kSoundChoose;
            return false;
          }
        } else {
          sLangAck = true;
          sLangConfirm = false;
          RebuildRows();
        }
        sPage.changed = true;
      }
    } else if (in->pressed & PAD_BUTTON_A) {
      Change(row, +1);
    }
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
  DrawText(dl, sc, colLeft + 4.f, top + 4.f, EntryName(), titleCol);
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
    const char* rowLabel = r.label();
    const float labelW = static_cast<float>(TextWidth(rowLabel));
    const float valueW = v.empty() ? 0.f : static_cast<float>(TextWidth(("< " + v + " >").c_str()));
    const float need = labelW + (v.empty() ? 0.f : gap + valueW);
    const float squeeze = need > textRight - textLeft ? (textRight - textLeft) / need : 1.f;
    DrawText(dl, sc, textLeft, ty, rowLabel, col, squeeze);
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

namespace ls {
void CoverNewGame(const std::string& label, const char* why);
void CoverContinue(const char* why);
void CoverBattle(const std::vector<std::string>& chips, bool rematch);
void CoverRoomResume();
}

namespace ng {

enum class Kind { Original, Engine, Timer, Setting, Mod, Randomizer, RandomizerSwitch, RandomizerCode, RandomizerPaste, Challenges, ShinyOdds, ShinyShadow, ShinyGift, ShinySpot, Start };
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
  bool back = false;
  engine::playthrough::Engine engineBefore = engine::playthrough::Engine::Faithful;
  es::Preset presetBefore = es::Preset::Original;
  std::vector<std::pair<size_t, std::string>> savedBefore;
  int rStyle = 0;
  std::string rCode;
  std::string rMessage;
  int shinyOdds = 0;
  int shinyUnlock = 0;
  std::vector<bool> rules;
  int timer[4] = {0, 0, 0, 0};
  std::string gen4Why;
} s;

const char* RandomizerStyleName(int style) {
  switch (style) {
  case 1: return Tr("Light");
  case 2: return Tr("Classic");
  case 3: return Tr("Chaos");
  case 4: return Tr("Custom");
  default: return Tr("Off");
  }
}

struct SwitchText {
  const char* id;
  const char* (*label)();
  const char* (*info)();
};
const SwitchText kSwitchTexts[] = {
    {"wild", [] { return Tr("Poke Spot Pokemon"); },
     [] { return Tr("Other wild Pokemon at the three Poke Spots. The visitors (Munchlax, Bonsly) stay."); }},
    {"wild.similar", [] { return Tr("Similar strength"); },
     [] { return Tr("Poke Spots: each Pokemon is replaced by one about as strong."); }},
    {"trainers", [] { return Tr("Trainers' Pokemon"); },
     [] { return Tr("Other Pokemon in the story trainers' teams. Team sizes stay; levels, held items and natures have "
                 "their own rows."); }},
    {"trainers.similar", [] { return Tr("Similar strength"); },
     [] { return Tr("Trainers: each Pokemon is replaced by one about as strong."); }},
    {"trainers.moves", [] { return Tr("Moves"); },
     [] { return Tr("Trainers' new Pokemon. Kept: the moves the disc gave that slot. By level: the last four "
                                "moves the new Pokemon learns up to its level."); }},
    {"trainers.no_dupes", [] { return Tr("No repeats"); },
     [] { return Tr("Trainers: a team never gets the same Pokemon twice."); }},
    {"shadow", [] { return Tr("Shadow Pokemon"); },
     [] { return Tr("Other species are the Shadow Pokemon (all 83 different; the P*DA's list follows). "
                                 "Shadow moves, catch rates and heart gauges stay."); }},
    {"shadow.similar", [] { return Tr("Similar strength"); },
     [] { return Tr("Shadows: each one is replaced by a Pokemon about as strong."); }},
    {"shadow.keep_lugia", [] { return Tr("Keep Shadow Lugia"); },
     [] { return Tr("Shadow Lugia stays Lugia (the story and its scenes show Lugia)."); }},
    {"items.ground", [] { return Tr("Items on the ground"); },
     [] { return Tr("The item boxes on the maps. Random: any item. Shuffle: the same items in "
                                            "other places. Important items never move."); }},
    {"abilities", [] { return Tr("Abilities"); },
     [] { return Tr("Pokemon get other abilities. Wonder Guard and Forecast are never handed out."); }},
    {"abilities.family", [] { return Tr("Same in a family"); },
     [] { return Tr("Abilities: a Pokemon keeps its ability when it evolves."); }},
    {"tm_compat", [] { return Tr("TM learners"); },
     [] { return Tr("Which Pokemon can learn each TM; each TM stays about as common as on the disc."); }},
    {"legendaries", [] { return Tr("Legendaries"); },
     [] { return Tr("Whether legendary Pokemon can be picked: never, only where the disc had one, or anywhere."); }},
    {"tm_moves", [] { return Tr("TM moves"); },
     [] { return Tr("Each TM teaches another move, all different (never a Shadow move). The bag and the shops "
                             "show the new move and its description. One-hit-KO moves can be ruled out."); }},
    {"trainer_levels", [] { return Tr("Trainers' levels"); },
     [] { return Tr("The story trainers' Pokemon are stronger by that much (never above level 100), or "
                                           "\"To strength\": a stronger new Pokemon gets a lower level, a weaker one a higher "
                                           "level. Shadow Pokemon keep their levels."); }},
    {"trainer_items", [] { return Tr("Trainers' held items"); },
     [] { return Tr("What the story trainers' Pokemon hold. Random: each holds one of the items "
                                              "trainers' Pokemon hold in the game. None: nothing. Shadow Pokemon keep theirs."); }},
    {"trainer_natures", [] { return Tr("Trainers' natures"); },
     [] { return Tr("Random: each of the story trainers' Pokemon gets any of the 25 natures. Shadow "
                                             "Pokemon get theirs from the game, as always."); }},
    {"shops", [] { return Tr("Poke Mart stock"); },
     [] { return Tr("What the Poke Marts sell. Shuffled: the Marts' own goods in other Marts. Everyday goods: "
                                 "medicine, status cures, repels, battle items, berries. Anything: also held items, TMs, "
                                 "vitamins, stones, valuables. Poke Balls and Poke Snacks stay where they are."); }},
    {"tutor_moves", [] { return Tr("Tutor moves"); },
     [] { return Tr("The move tutor teaches other moves, all different and none a TM teaches. Each is "
                                   "learned by the Pokemon that could learn the move it replaces. One-hit-KO moves can be "
                                   "ruled out."); }},
    {"wild.scope", [] { return Tr("Scope"); },
     [] { return Tr("Poke Spots, \"Same swap everywhere\": a Pokemon is replaced by the same new one in every spot, and no "
                                           "two by the same one."); }},
    {"wild.types", [] { return Tr("Type theme"); },
     [] { return Tr("Poke Spots, \"Similar types\": the new Pokemon shares a type with the one it replaces."); }},
    {"wild_levels", [] { return Tr("Poke Spot levels"); },
     [] { return Tr("Poke Spot Pokemon are that much higher in level (never above level 100)."); }},
    {"starter", [] { return Tr("Starter Pokemon"); },
     [] { return Tr("You start with another Pokemon instead of Eevee, at level 10. The film scenes and the Eevee evolution "
                    "offer still say Eevee."); }},
    {"starter.choice", [] { return Tr("Starter choice"); },
     [] { return Tr("Random: one Pokemon, chosen for you. Pick 1 of 3: you choose from three when the game would give you "
                    "Eevee. Eevee family: Eevee or one of its evolutions."); }},
    {"starter.similar", [] { return Tr("Similar to Eevee"); },
     [] { return Tr("On: the starter's base stats add up to about Eevee's (never a legendary). Off: any Pokemon the "
                    "Legendaries row allows. Does not apply to Eevee family."); }},
    {"starter.moves", [] { return Tr("Starter's moves"); },
     [] { return Tr("By level: the last four moves the starter learns up to level 10. Kept: Eevee's four (Tackle, Tail Whip, "
                    "Bite, Sand-Attack)."); }},
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
  if (name == "natural (by level)") return Tr("By level");
  if (name == "only where the disc had one") return Tr("As on the disc");
  if (name == "matched to the new Pokemon's strength") return Tr("To strength");
  if (name == "off") return Tr("Off");
  if (name == "on") return Tr("On");
  if (name == "kept") return Tr("Kept");
  if (name == "random") return Tr("Random");
  if (name == "none") return Tr("None");
  if (name == "shuffle") return Tr("Shuffle");
  if (name == "shuffled") return Tr("Shuffled");
  if (name == "everyday goods") return Tr("Everyday goods");
  if (name == "anything") return Tr("Anything");
  if (name == "never") return Tr("Never");
  if (name == "any") return Tr("Any");
  if (name == "similar types") return Tr("Similar types");
  if (name == "each spot its own") return Tr("Each spot its own");
  if (name == "same swap everywhere") return Tr("Same swap everywhere");
  if (name == "random, no one-hit-KO moves") return Tr("Random, no one-hit-KO moves");
  if (name == "pick 1 of 3") return Tr("Pick 1 of 3");
  if (name == "Eevee family only") return Tr("Eevee family");
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
    s.rMessage = Tr("Nothing to paste: copy a code first (Ctrl+C in a message or a web page).");
  } else if (text.size() > 80 || !xd::randomizer::NormalizeCode(text, code, error)) {
    s.rMessage = Fmt(Tr("That is not a randomizer code (%s)."), (error.empty() ? std::string(Tr("too long")) : error).c_str());
  } else {
    s.rCode = code;
    s.rStyle = StyleIndexOf(CodeSettings(code));
    s.rMessage = Fmt(Tr("Code taken: %s."), code.c_str());
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
  for (int w : {0, 3, 1, 2}) s.rows.push_back({Kind::Timer, nullptr, -1, w});
  if (s.engine == engine::playthrough::Engine::Enhanced) {
    for (const char* id : {"gameplay.text_speed", "gameplay.fast_forward", "gameplay.fast_battle_animations",
                            "gameplay.physical_special_split"}) {
      const es::Registry& reg = es::Global();
      const size_t i = reg.Find(id);
      if (i != es::kNoSetting && reg.Get(i).implemented) {
        if (const Row* r = SettingRow(id)) s.rows.push_back({Kind::Setting, r});
      }
    }
  }
  if (s.engine == engine::playthrough::Engine::Enhanced) {
    s.rows.push_back({Kind::ShinyOdds});
    s.rows.push_back({Kind::ShinyShadow});
    s.rows.push_back({Kind::ShinyGift});
    s.rows.push_back({Kind::ShinySpot});
  }
  for (int m = 0; m < static_cast<int>(s.defaults.mods.size()); m++) s.rows.push_back({Kind::Mod, nullptr, m});
  s.rows.push_back({Kind::Randomizer});
  if (s.rStyle == 4) {
    const auto& schema = xd::randomizer::GetSchema();
    const engine::randomizer::Settings rs = CodeSettings(s.rCode);
    for (int i = 0; i < static_cast<int>(schema.switches.size()); i++) {
      const int parent = SwitchParent(i);
      if (parent >= 0 && rs.Get(schema, schema.switches[parent].id) == 0) continue;
      if (schema.switches[i].id == "starter.similar" && rs.Get(schema, "starter.choice") == 2) continue;
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
  const std::vector<std::string> options = s.engine == engine::playthrough::Engine::Enhanced
                                               ? xd::shiny::OptionsOf(s.shinyOdds, s.shinyUnlock)
                                               : std::vector<std::string>();
  engine::playthrough::Setup setup = xd::pt::SetupOf(s.engine, s.defaults.mods, s.rStyle != 0 ? s.rCode : std::string(), rules, options);
  setup.speedrun.present = setup.speedrun.on = s.timer[0] != 0;
  setup.speedrun.withoutLoads = s.timer[0] == 2;
  setup.speedrun.start = s.timer[1];
  setup.speedrun.end = s.timer[2];
  setup.speedrun.category = s.timer[3];
  return setup;
}

std::string Shown(std::string text) {
  const std::string dot = "\xC2\xB7";
  const std::string with = GlyphOf(0xB7) != nullptr ? std::string(1, static_cast<char>(0xB7)) : std::string("-");
  for (size_t at; (at = text.find(dot)) != std::string::npos;) text.replace(at, dot.size(), with);
  return text;
}

std::string RowLabel(const SRow& r) {
  switch (r.kind) {
  case Kind::Original: return Tr("Play the original game");
  case Kind::Engine: return Tr("Engine");
  case Kind::Timer: {
    switch (r.sw) {
    case 0: return Tr("Speedrun timer");
    case 1: return Tr("Timer start (Custom)");
    case 2: return Tr("Timer end (Custom)");
    default: return Tr("Timer category");
    }
  }
  case Kind::Setting: return r.setting->label();
  case Kind::Mod: return s.defaults.mods[r.mod].name;
  case Kind::Randomizer: return Tr("Randomizer");
  case Kind::RandomizerSwitch: {
    const auto& def = xd::randomizer::GetSchema().switches[r.sw];
    const SwitchText* t = SwitchTextOf(def.id);
    if (t != nullptr) return t->label();
    return Tr(def.label);
  }
  case Kind::RandomizerCode: return Tr("Randomizer code");
  case Kind::RandomizerPaste: return Tr("Use a friend's code");
  case Kind::ShinyOdds: return Tr("Shiny odds");
  case Kind::ShinyShadow: return Tr("Shiny Shadow Pokemon");
  case Kind::ShinyGift: return Tr("Shiny gift Pokemon");
  case Kind::ShinySpot: return Tr("Shiny Bonsly / Munchlax");
  case Kind::Challenges:
    if (!s.gen4Why.empty() && std::string(xd::challenges::Rules()[r.sw].id).rfind("gen4_", 0) == 0) {
      return Fmt(Tr("%s (not available)"), xd::challenges::Rules()[r.sw].label());
    }
    return xd::challenges::Rules()[r.sw].label();
  default: return Tr("START");
  }
}

std::string RowInfo(const SRow& r) {
  const bool restart = xd::pt::NeedsRestart(Chosen());
  switch (r.kind) {
  case Kind::Original:
    return xd::pt::NeedsRestart(xd::pt::SetupOf(engine::playthrough::Engine::Faithful, {}))
               ? Tr("Faithful, no mods: exactly the GameCube game. Starts at once (the game first starts again without the mods it has loaded).")
               : Tr("Faithful, no mods: exactly the GameCube game. Starts at once.");
  case Kind::Engine:
    return Tr("Faithful: the game decides everything exactly as on the GameCube. Enhanced: speed-ups (text, fast-forward, "
              "battle animations) can be used.");
  case Kind::Timer:
    if (r.sw != 0) return xd::speedrun::TimerRowInfo(r.sw);
    return Tr("A timer over the picture for this playthrough, kept with the save: Real time counts from NEW GAME (what speedruns "
              "use); Without loads takes the game's loading time out. Only drawn: the game plays exactly the same with it on or "
              "off, also Faithful. Change it later on the SETTINGS page.");
  case Kind::Setting: return Description(*r.setting);
  case Kind::Mod: {
    const xd::pt::ModChoice& m = s.defaults.mods[r.mod];
    std::string t = Fmt(Tr("%s mod, version %s."), engine::mods::AppliesName(m.applies), m.version.c_str());
    if (!m.reason.empty()) t += " " + Fmt(Tr("Counted as new playthrough only: %s."), m.reason.c_str());
    if (!m.description.empty()) t += " " + m.description;
    return t;
  }
  case Kind::Randomizer:
    return Tr("Makes a new game from your disc: other Pokemon in the Poke Spots, other Shadow Pokemon, items in other "
              "places. Light: those only. Classic: also trainers, abilities, TM learners, similar strength. Chaos: all, no "
              "fairness. Custom: each switch yourself.");
  case Kind::RandomizerSwitch: {
    const auto& def = xd::randomizer::GetSchema().switches[r.sw];
    const SwitchText* t = SwitchTextOf(def.id);
    if (t != nullptr) return t->info();
    return Fmt("%s.", Tr(def.label).c_str());
  }
  case Kind::RandomizerCode:
    return Fmt(Tr("Code: %s. It holds the seed and every switch: a friend who uses it gets the same game. "
                  "Left / right or A: a new random seed."),
               s.rCode.c_str());
  case Kind::ShinyOdds:
    return Tr("How often a Pokemon is shiny: Original 1 in 8192 (the GameCube game), Modern 1 in 4096 (Gen 6 and later), or 1 in 1024, "
              "512, 128. Applies where the game allows shiny Pokemon (Poke Spot Pokemon, your Eevee, trades). Kept with this "
              "playthrough. A shiny is just a shiny in online battles and trades.");
  case Kind::ShinyShadow:
    return Tr("On: Shadow Pokemon can be shiny, with the odds above (in the GameCube game they never are). Each Shadow Pokemon "
              "gets its looks when you first meet it and keeps them, also after purifying. Off: as the game.");
  case Kind::ShinyGift:
    return Tr("On: the gift Pokemon the game never makes shiny (Jolteon, Vaporeon, Plusle, Ho-Oh, Pikachu, Celebi) can be shiny, with the odds "
              "above. Your starter Eevee, trades and the Mt. Battle starters already follow the odds. Off: as the game.");
  case Kind::ShinySpot:
    return Tr("On: the Poke Spot visitors Bonsly and Munchlax, which are never shiny in the game, can be shiny, with the odds above. "
              "Other Poke Spot Pokemon already follow the odds. Off: as the game.");
  case Kind::RandomizerPaste:
    return s.rMessage.empty() ? std::string(Tr("Copy a code (or a seed number) on the PC, then press A here to use it.")) : s.rMessage;
  case Kind::Challenges:
    if (!s.gen4Why.empty() && std::string(xd::challenges::Rules()[r.sw].id).rfind("gen4_", 0) == 0) {
      return Fmt(Tr("Not available: %s. The Gen 4 Pokemon, moves, cries and models are made from your own Pokemon Battle "
                    "Revolution disc (launcher, Extra assets page)."),
                 s.gen4Why.c_str());
    }
    return Fmt(Tr("Challenge rule, for this playthrough from start to end. %s"), xd::challenges::Rules()[r.sw].info());
  default:
    return restart ? Tr("Start the new game with this setup. The game starts again with these mods first; then choose NEW "
                        "GAME again.")
                   : (s.defaults.restarted ? Tr("The game started again with your setup: start the new game with it.")
                                           : Tr("Start the new game with this setup."));
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
  for (int w = 0; w < 4; w++) s.timer[w] = xd::speedrun::RowValue(w);
  if (!s.defaults.randomizer.empty()) {
    s.rCode = s.defaults.randomizer;
    s.rStyle = StyleIndexOf(CodeSettings(s.rCode));
  }
  s.shinyOdds = xd::shiny::OddsIndexOf(s.defaults.options);
  s.shinyUnlock = xd::shiny::UnlocksOf(s.defaults.options);
  s.rules.assign(xd::challenges::Rules().size(), false);
  if (!xd::gen4::PackPresent(&s.gen4Why)) {
  } else {
    s.gen4Why.clear();
  }
  for (size_t i = 0; i < s.rules.size(); i++) {
    const std::vector<std::string>& had = s.defaults.challenges;
    s.rules[i] = std::find(had.begin(), had.end(), xd::challenges::Rules()[i].id) != had.end();
  }
  const es::Registry& reg = es::Global();
  s.engineBefore = engine::playthrough::Engine::Faithful;
  for (size_t i = 0; i < reg.Count(); i++) {
    s.savedBefore.emplace_back(i, reg.TextOf(i, reg.SavedValueOf(i)));
    if (!engine::playthrough::IsEngineSetting(reg.Get(i))) continue;
    s.before.emplace_back(i, reg.ValueText(i));
    if (reg.ValueText(i) != reg.TextOf(i, reg.DefaultOf(i, es::Preset::Original))) s.engineBefore = engine::playthrough::Engine::Enhanced;
  }
  s.presetBefore = reg.CurrentPreset();
  engine::playthrough::ApplyEngine(es::Global(), s.engine);
  BuildRows();
  if (s.defaults.restarted) s.cursor = static_cast<int>(s.rows.size()) - 1;
  sPage.rowAlpha.clear();
}

void GoBack() {
  es::Registry& reg = es::Global();
  for (const auto& [i, was] : s.savedBefore) {
    if (reg.TextOf(i, reg.SavedValueOf(i)) != was) reg.SetFromText(reg.Get(i).id, was);
  }
  if (reg.CurrentPreset() != s.presetBefore && s.presetBefore != es::Preset::Custom) reg.ApplyPreset(s.presetBefore);
  engine::playthrough::ApplyEngine(reg, s.engineBefore);
  std::string differs;
  for (const auto& [i, was] : s.before) {
    if (reg.ValueText(i) != was) differs += (differs.empty() ? "" : ";") + reg.Get(i).id + "=" + reg.ValueText(i) + " (was " + was + ")";
  }
  std::fprintf(stderr, "[new game setup] back to the title menu: nothing chosen%s%s\n", differs.empty() ? "" : "; NOT AS BEFORE: ",
               differs.c_str());
  xd::pt::NewGameBack();
}

void Close(void*) {
  if (s.back) {
    GoBack();
    return;
  }
  if (s.original) {
    s.engine = engine::playthrough::Engine::Faithful;
    engine::playthrough::ApplyEngine(es::Global(), s.engine);
    for (xd::pt::ModChoice& m : s.defaults.mods) m.on = false;
    s.rStyle = 0;
    s.shinyOdds = 0, s.shinyUnlock = 0;
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
  xd::speedrun::NewGameStartPressed();
  ls::CoverNewGame(engine::playthrough::DisplayLabel(setup), "the New Game setup's START");
  xd::pt::Begin(setup, changed);
}

void Draw(ImDrawList* dl, float dt) {
  const Screen sc = ScreenNow();
  const int n = static_cast<int>(s.rows.size());
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  kit::OptionsScreen(dl, sc);
  for (int i = kOptDescFirst; i <= kOptDescLast; i++) DrawItemAsIs(dl, sc, i);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF}, grey[4] = {0xA0, 0xA0, 0xA0, 0xFF};
  {
    const UseFont font(kScrFont);
    DrawText(dl, sc, 82.f, 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), Tr("NEW GAME"), white);
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
      left = Tr("Faithful"), right = Tr("Enhanced");
      set = s.engine == engine::playthrough::Engine::Faithful ? 0 : 1;
    } else if (r.kind == Kind::Timer) {
      value = xd::speedrun::RowName(r.sw, s.timer[r.sw]);
    } else if (r.kind == Kind::Mod) {
      left = Tr("On"), right = Tr("Off");
      set = s.defaults.mods[r.mod].on ? 0 : 1;
    } else if (r.kind == Kind::Challenges) {
      left = Tr("On"), right = Tr("Off");
      set = s.rules[r.sw] ? 0 : 1;
    } else if (r.kind == Kind::Setting && TwoChoices(*r.setting)) {
      set = ChoiceNames(*r.setting, left, right);
    } else if (r.kind == Kind::Setting) {
      value = ValueText(*r.setting);
    } else if (r.kind == Kind::ShinyOdds) {
      value = xd::shiny::kOdds[s.shinyOdds].name;
    } else if (r.kind == Kind::ShinyShadow) {
      left = "On", right = "Off";
      set = (s.shinyUnlock & xd::shiny::kUnlockShadow) != 0 ? 0 : 1;
    } else if (r.kind == Kind::ShinyGift) {
      left = "On", right = "Off";
      set = (s.shinyUnlock & xd::shiny::kUnlockGifts) != 0 ? 0 : 1;
    } else if (r.kind == Kind::ShinySpot) {
      left = "On", right = "Off";
      set = (s.shinyUnlock & xd::shiny::kUnlockSpots) != 0 ? 0 : 1;
    } else if (r.kind == Kind::Randomizer) {
      value = RandomizerStyleName(s.rStyle);
    } else if (r.kind == Kind::RandomizerSwitch) {
      const auto& schema = xd::randomizer::GetSchema();
      const auto& def = schema.switches[r.sw];
      const uint32_t v = std::min(CodeSettings(s.rCode).Get(schema, def.id), def.Choices() - 1);
      if (def.Choices() == 2 && def.choiceNames[0] == "off") {
        left = ChoiceShown(def.choiceNames[1]), right = Tr("Off");
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
      value = Tr("Paste (A)");
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
        Shown(engine::playthrough::DisplayLabel(r.kind == Kind::Original ? xd::pt::SetupOf(engine::playthrough::Engine::Faithful, {})
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
  case Kind::Timer:
    if (left || right || a) {
      const int n = xd::speedrun::RowCount(r.sw);
      s.timer[r.sw] = (s.timer[r.sw] + (left ? n - 1 : 1)) % n;
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
      const bool blocked = !s.gen4Why.empty() && std::string(xd::challenges::Rules()[r.sw].id).rfind("gen4_", 0) == 0;
      if (blocked) break;
      s.rules[r.sw] = left ? true : right ? false : !s.rules[r.sw];
      event = kSoundMove;
    }
    break;
  case Kind::ShinyOdds:
    if (left || right || a) {
      s.shinyOdds = (s.shinyOdds + (left ? xd::shiny::kOddsCount - 1 : 1)) % xd::shiny::kOddsCount;
      event = kSoundMove;
    }
    break;
  case Kind::ShinyShadow:
  case Kind::ShinyGift:
  case Kind::ShinySpot:
    if (left || right || a) {
      const int bit = r.kind == Kind::ShinyShadow ? xd::shiny::kUnlockShadow
                      : r.kind == Kind::ShinyGift ? xd::shiny::kUnlockGifts : xd::shiny::kUnlockSpots;
      const bool on = left ? true : right ? false : (s.shinyUnlock & bit) == 0;
      s.shinyUnlock = on ? (s.shinyUnlock | bit) : (s.shinyUnlock & ~bit);
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
  if (!close && (in->pressed & PAD_BUTTON_B)) { s.back = close = true; event = kSoundBack; }
  else if (close) event = kSoundChoose;
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
    std::string list;
    for (size_t i = 0; i < keep; i++) list += (i > 0 ? "; " : "") + problems[i];
    if (keep < problems.size()) {
      const int more = static_cast<int>(problems.size() - keep);
      list += keep > 0 ? "; " + Fmt(Tr("and %d more"), more) : Fmt(Tr("%d more"), more);
    }
    const std::string text = Fmt(Tr("This save was started with mods that are missing or changed: %s. Carry on without them? (It can "
                                    "change the game.)"),
                                 list.c_str());
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
  const char* const answers[2] = {Tr("YES"), Tr("NO")};
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
  for (size_t i = 0; i < names.size(); i++) {
    if (i == 0) held = names[i];
    else if (i + 1 == names.size()) held = Fmt(Tr("%s and %s"), held.c_str(), names[i].c_str());
    else held += ", " + names[i];
  }
  if (xd::pt::EngineQuestionSave() == engine::playthrough::Engine::Faithful) {
    kEngineChoices[0] = Tr("Keep Faithful"), kEngineChoices[1] = Tr("Switch to Enhanced");
    ePages = {
        Tr("This playthrough is marked Faithful: it plays exactly like the GameCube game."),
        Tr("For a Faithful playthrough some settings stay as on the GameCube, whatever you have set."),
        Fmt(Tr("They are: %s."), held.c_str()),
        Tr("Your settings are Enhanced now. You can keep this playthrough Faithful, or switch it to Enhanced so that those "
           "settings work in it."),
        Tr("Warning: you can switch back later, but a playthrough that was Enhanced for a while no longer counts as "
           "Faithful from the start."),
        Tr("Its label will always show that it was switched. Picture, sound and controls settings are yours either way."),
        Tr("Keep this playthrough Faithful, or switch it to Enhanced?"),
    };
  } else {
    kEngineChoices[0] = Tr("Keep Enhanced"), kEngineChoices[1] = Tr("Switch to Faithful");
    ePages = {
        Tr("This playthrough is marked Enhanced: it plays with your own gameplay settings."),
        Fmt(Tr("They are: %s."), held.c_str()),
        Tr("Your settings are the Original ones now, so it plays like the GameCube game anyway. But it stays marked "
           "Enhanced."),
        Tr("You can keep it Enhanced, or switch it to Faithful. Faithful holds those settings as on the GameCube, whatever "
           "you set later."),
        Tr("Its label will always show that it was Enhanced for a while: it does not count as Faithful from the start. "
           "You can switch again later."),
        Tr("Keep this playthrough Enhanced, or switch it to Faithful?"),
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

namespace sk {
int cursor = 0;
float age = 0.f;
std::chrono::steady_clock::time_point last{};
std::vector<std::string> names;
void Open(void*) {
  cursor = 0, age = 0.f, last = {};
  names.clear();
  const xd::randomizer::StarterPlan& plan = xd::starter::Plan();
  for (uint32_t k = 0; k < plan.count; k++) names.push_back(xd::starter::SpeciesName(plan.species[k]));
}
bool Frame(void*, const PortMenuInput* in) {
  const int n = static_cast<int>(names.size());
  if (n == 0) return false;
  if (in->repeat & PAD_BUTTON_UP) {
    cursor = (cursor + n - 1) % n;
    PlayMenuSound(kTitleMenu, kSoundMove);
  } else if (in->repeat & PAD_BUTTON_DOWN) {
    cursor = (cursor + 1) % n;
    PlayMenuSound(kTitleMenu, kSoundMove);
  }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    xd::starter::Choose(cursor);
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  age += last.time_since_epoch().count() == 0 ? 0.f : std::clamp(std::chrono::duration<float>(now - last).count(), 0.f, 0.1f);
  last = now;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  FillWindow(dl, IM_COL32(0, 0, 0, 120));
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  const UseFont font(0);
  if (!DrawFrame(dl, sc, kMsgMenu, kMsgFrameFirst, kMsgFrameLast, 0.f, 0.f)) {
    dl->AddRectFilled(sc.At(27.f, 290.f), sc.At(613.f, 440.f), IM_COL32(26, 26, 26, 250), 8.f * sc.s);
  }
  const std::vector<std::string> lines = Wrap(Tr("Your partner Pokemon is not Eevee this time. Choose one of these three to start with."), 520.f);
  for (size_t i = 0; i < std::min<size_t>(lines.size(), 4); i++) {
    DrawText(dl, sc, 52.f, 307.f + 28.f * static_cast<float>(i), lines[i].c_str(), white);
  }
  const float boxX = 190.f, boxY = 86.f, boxW = 260.f, rowH = 38.f;
  const float boxH = rowH * static_cast<float>(n) + 22.f;
  if (!kit::Nine(dl, sc, kYesNoMenu, kYesNoFrameFirst, kYesNoFrameLast, boxX, boxY, boxX + boxW, boxY + boxH)) kit::Window(dl, sc, boxX, boxY, boxX + boxW, boxY + boxH);
  for (int i = 0; i < n; i++) {
    const float rowY = boxY + 11.f + rowH * static_cast<float>(i);
    if (i == cursor) {
      kit::Oval(dl, sc, boxX + 18.f, rowY + 3.f, boxW - 36.f, true);
      kit::Cursor(dl, sc, boxX + 34.f, rowY + 15.f);
    }
    kit::Text(dl, sc, boxX + 52.f, rowY + 4.f, names[i], i == cursor ? kit::kText : kit::kSoft, 1.f, boxW - 80.f, 1);
  }
  (void)age;
  return true;
}
const PortMenuPage kPage{"starter choice", Open, Frame, nullptr, nullptr};
}

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
  const std::vector<std::string> lines = Wrap(noteText, 500.f / 0.85f);
  const float h = 16.f + 18.f * static_cast<float>(lines.size());
  kit::Window(dl, sc, 44.f, 440.f - h, 596.f, 452.f);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
  for (size_t i = 0; i < lines.size(); i++) DrawText(dl, sc, 44.f + kit::kInset + 4.f, 448.f - h + 18.f * i, lines[i].c_str(), white, 0.85f, 0.85f);
}

}

namespace mm {

namespace em = engine::mods;

enum class Kind { Mod, Broken, Apply, Exit };
struct PRow {
  Kind kind;
  int index = -1;
};

struct State {
  em::ModList list;
  em::ModList original;
  std::vector<em::ModFolder> found;
  std::vector<em::BrokenFolder> broken;
  em::Review review;
  std::string gameId;
  bool worldFixed = false;
  std::vector<std::string> worldIds;
  std::vector<std::string> loaded;
  std::vector<PRow> rows;
  int cursor = 0, first = 0;
  std::vector<float> rowAlpha;
  float titleTimer = 0.f, age = 0.f;
  std::chrono::steady_clock::time_point last{};
  std::string notice;
  std::vector<std::pair<std::string, bool>> hidden;
} s;

std::filesystem::path ListFile() { return xd::SettingsFolder() / em::kListFile; }

bool Changed() { return em::FormatModList(s.list) != em::FormatModList(s.original); }

std::string LowerOf(std::string t) {
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t;
}
bool InIds(const std::vector<std::string>& ids, const std::string& id) {
  return std::any_of(ids.begin(), ids.end(), [&](const std::string& x) { return LowerOf(x) == LowerOf(id); });
}

void Rebuild() {
  s.review = em::ReviewList(s.list.Active(), s.found, s.gameId);
  s.rows.clear();
  for (int i = 0; i < static_cast<int>(s.review.rows.size()); i++) s.rows.push_back({Kind::Mod, i});
  for (int i = 0; i < static_cast<int>(s.broken.size()); i++) s.rows.push_back({Kind::Broken, i});
  s.rows.push_back({Kind::Apply});
  s.rows.push_back({Kind::Exit});
  s.cursor = std::clamp(s.cursor, 0, static_cast<int>(s.rows.size()) - 1);
}

void Open(void*) {
  s = State{};
  const std::filesystem::path modsFolder = xd::SettingsFolder() / L"mods";
  std::vector<em::Message> messages;
  s.found = em::FindMods(modsFolder, messages, s.broken);
  std::vector<std::string> warnings;
  em::ReadModListFile(ListFile(), s.list, warnings);
  const auto hiddenId = [](const std::string& id) { return LowerOf(id) == "battle-revolution-models"; };
  s.found.erase(std::remove_if(s.found.begin(), s.found.end(), [&](const em::ModFolder& f) { return hiddenId(f.manifest.id); }), s.found.end());
  auto& listed = s.list.Active().mods;
  for (const auto& e : listed) {
    if (hiddenId(e.first)) s.hidden.push_back(e);
  }
  listed.erase(std::remove_if(listed.begin(), listed.end(), [&](const auto& e) { return hiddenId(e.first); }), listed.end());
  em::AddNewMods(s.list, s.found);
  s.original = s.list;
  const char* id = PortDisc_GameId();
  s.gameId = id != nullptr ? id : "";
  s.worldFixed = xd::pt::WorldModsFixed(s.worldIds);
  s.loaded = xd::pt::LoadedModIds();
  Rebuild();
  s.rowAlpha.clear();
  std::fprintf(stderr, "[mods menu] %zu mod(s) in the list \"%s\", %zu folder(s) that can't be used; %zu in use now\n",
               s.list.Active().mods.size(), s.list.active.c_str(), s.broken.size(), s.loaded.size());
}

void Close(void*) {}

void Details(const PRow& r, std::string& summary, std::string& text) {
  const int mods = static_cast<int>(std::count_if(s.review.rows.begin(), s.review.rows.end(),
                                                  [](const em::ReviewRow& e) { return e.on && e.Usable(); }));
  switch (r.kind) {
  case Kind::Mod: {
    const em::ReviewRow& row = s.review.rows[r.index];
    if (row.mod == nullptr) {
      summary = row.id;
      text = row.problems.empty() ? std::string() : row.problems.front();
      return;
    }
    const em::Manifest& m = row.mod->manifest;
    summary = m.name + (m.version.empty() ? "" : "  " + m.version) + (m.author.empty() ? "" : "  " + Fmt(Tr("by %s"), m.author.c_str()));
    const auto add = [&](const std::string& part) {
      if (!part.empty()) text += (text.empty() ? "" : " ") + part;
    };
    for (const std::string& p : row.problems) add(p);
    for (const std::string& w : row.warnings) add(w);
    add(m.description);
    add(Fmt(Tr("Changes: %s."), em::DescribeChanges(em::ChangesOf(*row.mod)).c_str()));
    const em::AppliesVerdict v = xd::pt::ModApplies(*row.mod);
    switch (v.effective) {
    case em::Applies::AnyTime: add(Tr("Looks and sound only.")); break;
    case em::Applies::Restart: add(Tr("Read when the game starts.")); break;
    default:
      if (s.worldFixed && InIds(s.worldIds, row.id)) {
        add(Tr("New playthrough only: your save is set up with it, so it stays on for that save whatever is chosen here."));
      } else if (s.worldFixed) {
        add(Tr("New playthrough only: it is chosen when you start a new game. Your save keeps the mods it was started with."));
      } else {
        add(Tr("New playthrough only: it is chosen when you start a new game."));
      }
      break;
    }
    if (v.Downgraded()) add(Fmt(Tr("(It says %s, but %s.)"), em::AppliesName(v.claimed), v.reason.c_str()));
    if (InIds(s.loaded, row.id)) add(Tr("In use now."));
    return;
  }
  case Kind::Broken: {
    const em::BrokenFolder& b = s.broken[r.index];
    summary = b.folderName;
    text = Fmt(Tr("Can't be used: %s. Fix or remove it in the mods folder, then open this page again."), b.reason.c_str());
    return;
  }
  case Kind::Apply:
    summary = mods == 1 ? Fmt(Tr("List \"%s\": 1 mod on"), s.list.active.c_str())
                        : Fmt(Tr("List \"%s\": %d mods on"), s.list.active.c_str(), mods);
    if (!s.notice.empty()) {
      text = s.notice;
    } else if (Changed()) {
      text = Tr("Save this mod list and start the game again with it. Until you do, nothing changes: EXIT or B leaves the list as it was.");
    } else {
      text = Tr("Nothing has changed yet. Switch mods on or off above (L / R, or X / Y, move one up or down the order: further down wins "
                "when two mods change the same file).");
    }
    return;
  default:
    summary = Fmt(Tr("List \"%s\""), s.list.active.c_str());
    text = Tr("Leave without applying: the mod list stays as it was.");
    return;
  }
}

bool Apply() {
  if (!Changed()) {
    s.notice = Tr("Nothing has changed, so there is nothing to apply.");
    return false;
  }
  if (xd::PlayerFilesReadOnly()) {
    s.notice = Fmt(Tr("This test run can't save the mod list: %s"), xd::kPlayerFilesReadOnlyNote);
    std::fprintf(stderr, "[mods menu] %s (mods.ini)\n", xd::kPlayerFilesReadOnlyNote);
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(ListFile().parent_path(), ec);
  em::ModList toWrite = s.list;
  for (const auto& e : s.hidden) toWrite.Active().mods.push_back(e);
  if (!em::WriteModListFile(ListFile(), toWrite)) {
    s.notice = Tr("The mod list couldn't be saved to mods.ini, so nothing was changed.");
    std::fprintf(stderr, "[mods menu] could not write %ls\n", ListFile().wstring().c_str());
    return false;
  }
  std::vector<engine::playthrough::ModUse> use;
  std::string ids;
  for (const em::ReviewRow& row : s.review.rows) {
    if (!row.on || !row.Usable()) continue;
    use.push_back({row.mod->manifest.id, row.mod->manifest.name, row.mod->manifest.version, xd::pt::ModApplies(*row.mod).effective});
    ids += (ids.empty() ? "" : ", ") + row.mod->manifest.id;
  }
  std::fprintf(stderr, "[mods menu] applied: mods.ini written (%ls); on, in order: %s; starting the game again\n", ListFile().wstring().c_str(),
               ids.empty() ? "none" : ids.c_str());
  xd::pt::RestartForMods(use);
  return true;
}

void Draw(ImDrawList* dl, float dt) {
  const Screen sc = ScreenNow();
  const int n = static_cast<int>(s.rows.size());
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  kit::OptionsScreen(dl, sc);
  for (int i = kOptDescFirst; i <= kOptDescLast; i++) DrawItemAsIs(dl, sc, i);
  const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF}, grey[4] = {0xA0, 0xA0, 0xA0, 0xFF}, amber[4] = {0xFF, 0xD0, 0x60, 0xFF},
                red[4] = {0xFF, 0x90, 0x90, 0xFF};
  {
    const UseFont font(kScrFont);
    DrawText(dl, sc, 82.f, 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f), ModsText(), white);
    const UseFont small(kScrInfoFont);
    const char* orderHint = Tr("L / R: move up / down the order");
    const float hintW = static_cast<float>(TextWidth(orderHint)) * 0.9f * 0.8f;
    DrawText(dl, sc, 232.f, 41.f, orderHint, grey, hintW > 380.f ? 0.9f * 380.f / hintW : 0.9f, 0.8f);
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
    const PRow& r = s.rows[i];
    const bool sel = i == s.cursor;
    const float y = kScrRowTop + kScrPitch * static_cast<float>(i - s.first), mid = y + kScrBarH / 2.f;
    DrawItem(dl, sc, kOptRowBar, 0.f, y, 640.f, kScrBarH, static_cast<uint8_t>(s.rowAlpha[i]));
    const UseFont font(kScrFont);
    if (r.kind == Kind::Apply) {
      const uint8_t a = Changed() ? 255 : 150;
      DrawItem(dl, sc, kOptExit, 120.f, mid - 16.f, 400.f, -1.f, a, 16.f);
      if (sel) DrawItem(dl, sc, kOptExitCursor, 117.f, mid - 19.f, 406.f, -1.f, pulse, 16.f);
      DrawTextCentred(dl, sc, 120.f, mid - 16.f, 400.f, 32.f, Tr("Apply and restart"), Changed() ? white : grey);
      continue;
    }
    if (r.kind == Kind::Exit) {
      DrawItem(dl, sc, kOptExit, 437.f, mid - 16.f);
      if (sel) DrawItem(dl, sc, kOptExitCursor, 434.f, mid - 19.f, -1.f, -1.f, pulse);
      DrawTextCentred(dl, sc, 437.f, mid - 16.f, 144.f, 32.f, Tr("EXIT"), white);
      continue;
    }
    std::string label, value;
    const uint8_t* labelColour = white;
    int set = -1;
    if (r.kind == Kind::Mod) {
      const em::ReviewRow& row = s.review.rows[r.index];
      label = std::to_string(r.index + 1) + ". " + (row.mod != nullptr ? row.mod->manifest.name : row.id);
      if (row.mod == nullptr) {
        value = Tr("Missing");
        labelColour = red;
      } else {
        set = row.on ? 0 : 1;
        if (!row.problems.empty() && row.on) labelColour = red;
        else if (!row.warnings.empty() && row.on) labelColour = amber;
      }
    } else {
      label = s.broken[r.index].folderName;
      value = Tr("Can't load");
      labelColour = red;
    }
    const float labelW = static_cast<float>(TextWidth(label.c_str())), room = 321.f - 8.f - 74.f;
    DrawText(dl, sc, 74.f, std::floor(mid - static_cast<float>(sFontW + 1) / 2.f), label.c_str(), labelColour, labelW > room ? room / labelW : 1.f);
    const float boxY = mid - 12.f;
    if (set >= 0) {
      DrawItem(dl, sc, set == 0 ? kOptChoice : kOptChoice2, set == 0 ? 321.f : 449.f, boxY, -1.f, -1.f, sel ? pulse : 255);
      DrawTextCentred(dl, sc, 321.f, boxY, 121.f, 24.f, Tr("On"), white);
      DrawTextCentred(dl, sc, 449.f, boxY, 121.f, 24.f, Tr("Off"), white);
    } else {
      DrawItem(dl, sc, kOptChoice, 321.f, boxY, 249.f, -1.f, 110, 14.f);
      DrawTextCentred(dl, sc, 321.f, boxY, 249.f, 24.f, value.c_str(), grey);
    }
  }
  if (s.first > 0) DrawTriangle(dl, sc, 320.f, 75.f, 16.f, 0, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  if (s.first + shown < n) DrawTriangle(dl, sc, 320.f, 328.f, 16.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  {
    const UseFont font(kScrInfoFont);
    std::string summary, text;
    Details(s.rows[s.cursor], summary, text);
    const float summaryW = std::max(1.f, static_cast<float>(TextWidth(summary.c_str())));
    DrawText(dl, sc, 90.f, 352.f, summary.c_str(), white, std::min(0.9f, 452.f / summaryW), 0.85f);
    struct Fit {
      size_t lines;
      float pitch, scale;
    };
    constexpr Fit kFits[] = {{3, 16.f, 0.75f}, {4, 13.f, 0.65f}, {5, 10.6f, 0.52f}};
    Fit fit = kFits[0];
    std::vector<std::string> lines;
    for (const Fit& f : kFits) {
      fit = f;
      lines = Wrap(text, 452.f / (f.scale * 0.9f));
      if (lines.size() <= f.lines) break;
    }
    if (lines.size() > fit.lines) {
      lines.resize(fit.lines);
      lines.back() += " ...";
    }
    for (size_t k = 0; k < lines.size(); k++) {
      DrawText(dl, sc, 90.f, 379.f + fit.pitch * static_cast<float>(k), lines[k].c_str(), white, 0.9f, fit.scale);
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
  if (s.cursor != before) {
    event = kSoundMove;
    s.notice.clear();
  }
  const PRow r = s.rows[s.cursor];
  const bool left = (in->repeat & PAD_BUTTON_LEFT) != 0, right = (in->repeat & PAD_BUTTON_RIGHT) != 0;
  const bool a = (in->pressed & PAD_BUTTON_A) != 0;
  bool close = false;
  if (r.kind == Kind::Mod) {
    const em::ReviewRow& row = s.review.rows[r.index];
    auto& entry = s.list.Active().mods[static_cast<size_t>(r.index)];
    if (row.mod != nullptr && (left || right || a)) {
      entry.second = left ? true : right ? false : !entry.second;
      s.notice.clear();
      Rebuild();
      event = kSoundMove;
    }
    const int dir = (in->pressed & (PAD_TRIGGER_L | PAD_BUTTON_X)) != 0 ? -1 : (in->pressed & (PAD_TRIGGER_R | PAD_BUTTON_Y)) != 0 ? 1 : 0;
    if (dir != 0 && em::MoveMod(s.list.Active(), static_cast<size_t>(r.index), dir)) {
      s.cursor += dir;
      s.notice.clear();
      Rebuild();
      event = kSoundMove;
    }
  } else if (r.kind == Kind::Apply && a) {
    if (Apply()) close = true;
    else event = kSoundBack;
  } else if (r.kind == Kind::Exit && a) {
    close = true;
  }
  if (in->pressed & PAD_BUTTON_B) {
    close = true;
    event = kSoundBack;
  } else if (close) {
    event = kSoundChoose;
  }
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
      std::string label = i == s.cursor ? "> " : "  ";
      const PRow& row = s.rows[i];
      if (row.kind == Kind::Mod) {
        const em::ReviewRow& rr = s.review.rows[row.index];
        label += (rr.on ? "[on]  " : "[off] ") + (rr.mod != nullptr ? rr.mod->manifest.name : rr.id);
      } else if (row.kind == Kind::Broken) {
        label += Fmt(Tr("(can't load) %s"), s.broken[row.index].folderName.c_str());
      } else {
        label += row.kind == Kind::Apply ? Tr("Apply and restart") : Tr("EXIT");
      }
      DrawText(dl, sc, 80.f, 60.f + 30.f * i, label.c_str(), white);
    }
    return true;
  }
  Draw(ImGui::GetForegroundDrawList(), dt);
  return true;
}

const PortMenuPage kModsPage{"mods", Open, Frame, Close, nullptr};

}

#include "online_trade_page.inc"
void Neutral(PADStatus& s);
void Filter(PADStatus* status);
#include "online_lobby_page.inc"
#include "save_picker_page.inc"
#include "loading_screen.inc"

namespace ex {

enum class Level { Top, Online };
Level level = Level::Top;
int cursor = 0;
const PortMenuPage* sub = nullptr;
const PortMenuPage* sBattle = &ol::kPage;

kit::Age age;
void Open(void*) {
  level = Level::Top;
  cursor = 0;
  sub = nullptr;
  age.Reset();
}

void RunSub(const PortMenuPage* page) {
  sub = page;
  if (page->open) page->open(page->ctx);
}

bool Frame(void*, const PortMenuInput* in) {
  const Screen sc = ScreenNow();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (sub != nullptr) {
    if (!sub->frame(sub->ctx, in)) {
      if (sub->close) sub->close(sub->ctx);
      sub = nullptr;
      PortInput_LatchHeld();
      if (ol::gClose) { ol::gClose = false; return false; }
    }
    return true;
  }
  const bool top = level == Level::Top;
  const int n = top ? 4 : 4;
  if (in->repeat & PAD_BUTTON_UP) cursor = (cursor + n - 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_DOWN) cursor = (cursor + 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    if (top) return false;
    level = Level::Top, cursor = 0;
    return true;
  }
  if (in->pressed & PAD_BUTTON_A) {
    if (top) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      if (cursor == 0) level = Level::Online, cursor = 0;
      else if (cursor == 1) sPageFromTitle = true, RunSub(&mm::kModsPage);
      else if (cursor == 2) sPageFromTitle = true, RunSub(&kPage);
      else return false;
    } else if (cursor == 0) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      RunSub(&ot::kPage);
    } else if (cursor == 1 && sBattle != nullptr) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      RunSub(sBattle);
    } else if (cursor == 2) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      RunSub(PortXD_TrainerCardPage());
    } else if (cursor == 3) {
      PlayMenuSound(kTitleMenu, kSoundBack);
      level = Level::Top, cursor = 0;
    } else {
      PlayMenuSound(kTitleMenu, kSoundBack);
    }
  }
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, top ? Tr("EXTRAS") : Tr("ONLINE"));
  const char* topItems[4] = {Tr("ONLINE"), Tr("MODS"), Tr("SETTINGS"), Tr("Back")};
  const char* onItems[4] = {Tr("Trade with a friend"), Tr("Battle a friend"), Tr("Trainer card"), Tr("Back")};
  const char* topInfo[4] = {Tr("Trade or battle with a friend over the internet."), ModsInfoText(),
                            Tr("The PC version's settings: picture, sound, controls and more."), Tr("Back to the title menu.")};
  const char* onInfo[4] = {Tr("Trade Pokemon with a friend over the internet."), Tr("Battle a friend over the internet."),
                           Tr("Your title, name and character for online battles."), Tr("Back to EXTRAS.")};
  kit::Window(dl, sc, 150.f, 96.f, 490.f, 96.f + 52.f * static_cast<float>(n) + 20.f);
  for (int i = 0; i < n; i++) {
    const float y = 112.f + 52.f * static_cast<float>(i);
    const bool grey = !top && i == 1 && sBattle == nullptr;
    const bool sel = i == cursor;
    if (sel) kit::RowSel(dl, sc, 156.f, 484.f, y, 40.f);
    kit::Text(dl, sc, 192.f, y + 8.f, (top ? topItems : onItems)[i], grey ? kit::kDim : sel ? kit::kText : kit::kSoft, 1.f, 270.f, kTitleFont);
  }
  kit::Window(dl, sc, 60.f, 342.f, 580.f, 432.f);
  const std::string info = !top && cursor == 1 && sBattle == nullptr ? std::string(Tr("Online battles are not in this build yet."))
                                                                     : std::string((top ? topInfo : onInfo)[cursor]);
  kit::Para(dl, sc, 86.f, 364.f, 468.f, info, kit::kText, 0.9f, 24.f, 2);
  kit::HintBar(dl, sc, Tr("A: choose   B: back"));
  kit::FadeIn(dl, age.Tick());
  return true;
}
const PortMenuPage kPage{"extras", Open, Frame, nullptr, nullptr};

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
    sTitleSelected = sTitleMods = sTitleOnline = false;
    return;
  }
  if (!sTitleSelected) {
    if (menuTitleGetSelect(w->cursor) == kTitleOptions && (newDirs & kLeft) != 0) {
      sTitleSelected = true;
      sTitleMods = sTitleOnline = false;
      PlayMenuSound(w->menuID, kSoundMove);
      PortInput_LatchHeld();
      for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
    }
    return;
  }
  if (newDirs & kRight) {
    if (sTitleOnline) sTitleOnline = false;
    else if (sTitleMods) sTitleMods = false;
    else sTitleSelected = false;
    PlayMenuSound(w->menuID, kSoundMove);
    PortInput_LatchHeld();
    PlayMenuSound(w->menuID, kSoundMove);
    PortInput_LatchHeld();
  } else if ((newDirs & (kLeft | kUp | kDown)) != 0 || (newButtons & PAD_BUTTON_B) != 0) {
    sTitleSelected = sTitleMods = sTitleOnline = false;
    return;
  } else if (newButtons & PAD_BUTTON_A) {
    sPageFromTitle = true;
    if (PortMenu_Open(&ex::kPage)) PlayMenuSound(w->menuID, kSoundChoose);
  }
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
}

void EntryFilter(PADStatus* status);

u16 sPtPrevButtons = 0;
struct DropPressGuard {
  PADStatus* status;
  ~DropPressGuard() {
    if (!xd::picker::DropPressNow()) return;
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
      if (status[i].err == PAD_ERR_NONE) status[i].button &= static_cast<u16>(~PAD_BUTTON_A);
    }
  }
};
void Filter(PADStatus* status) {
  const DropPressGuard dropPress{status};
  ls::CoverMute(status);
  xd::pt::PadPoll(status);
  if (PortXD_PcTradeSwallowInput()) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
      if (status[i].err == PAD_ERR_NONE) status[i].button = 0;
    }
    PortInput_LatchHeld();
    return;
  }
  const auto* w = static_cast<const WinWorkLayout*>(windowSearchID(windowGetActiveID()));
  if (ol::D.on) ol::Drive(status, w);
  if (ol::ResumeDue(w)) { sPageFromTitle = true; PortMenu_Open(&ol::kPage); }
  if (xd::picker::Current() != xd::picker::Intent::None) {
    const bool open = IsTitleMenu(w) && w->state == 2 && w->pad_a4 == 0 && w->cursor >= 0 && w->cursor < 5;
    xd::picker::Drive(status, w != nullptr ? w->menuID : 0, open, open ? menuTitleGetSelect(w->cursor) : -1);
  }
  ls::CoverDrive(status, w);
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
  if (xd::pt::ScreenEnabled() || xd::pt::EngineQuestionPossible() || xd::pt::TimerWatch() || xd::pt::PickerPossible()) {
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
    sTitleSelected = sTitleMods = sTitleOnline = false;
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

namespace xd::ui {
ImVec2 At(float x, float y) { return ScreenNow().At(x, y); }
float Scale() { return ScreenNow().s; }
void Backdrop(ImDrawList* dl) { kit::Backdrop(dl, ScreenNow()); }
void Dim(ImDrawList* dl, uint8_t alpha) { kit::Dim(dl, alpha); }
void Header(ImDrawList* dl, const std::string& title, const std::string& right) { kit::Header(dl, ScreenNow(), title, right); }
void Window(ImDrawList* dl, float x0, float y0, float x1, float y1, bool accent) { kit::Window(dl, ScreenNow(), x0, y0, x1, y1, accent); }
void TitledWindow(ImDrawList* dl, float x0, float y0, float x1, float y1, const std::string& title, bool accent) {
  kit::TitledWindow(dl, ScreenNow(), x0, y0, x1, y1, title, accent);
}
void RowSel(ImDrawList* dl, float x0, float x1, float y, float h, bool cursor) { kit::RowSel(dl, ScreenNow(), x0, x1, y, h, cursor); }
void Cursor(ImDrawList* dl, float cx, float cy) { kit::Cursor(dl, ScreenNow(), cx, cy); }
void Button(ImDrawList* dl, float x, float y, float w, const std::string& label, bool selected, bool enabled) {
  kit::Button(dl, ScreenNow(), x, y, w, label, selected, enabled);
}
void Value(ImDrawList* dl, float x, float y, float w, const std::string& value, bool selected) { kit::Value(dl, ScreenNow(), x, y, w, value, selected); }
void HintBar(ImDrawList* dl, const std::string& hints) { kit::HintBar(dl, ScreenNow(), hints); }
void Ring(ImDrawList* dl, float cx, float cy, float r) { kit::Ring(dl, ScreenNow(), cx, cy, r); }
uint8_t Pulse() { return kit::Pulse(); }
void Text(ImDrawList* dl, float x, float y, const std::string& t, const uint8_t* col, float scale, float maxW, int font) {
  kit::Text(dl, ScreenNow(), x, y, t, col, scale, maxW, static_cast<uint16_t>(font));
}
void TextC(ImDrawList* dl, float x, float y, float w, const std::string& t, const uint8_t* col, float scale, int font) {
  kit::TextC(dl, ScreenNow(), x, y, w, t, col, scale, static_cast<uint16_t>(font));
}
int Para(ImDrawList* dl, float x, float y, float w, const std::string& t, const uint8_t* col, float scale, float pitch, int maxLines) {
  return kit::Para(dl, ScreenNow(), x, y, w, t, col, scale, pitch, maxLines);
}
float Width(const std::string& t, float scale, int font) { return kit::Width(t, scale, static_cast<uint16_t>(font)); }
}

extern "C" {

void PortXD_SettingsMenuInit(void) {
  const es::Registry& reg = es::Global();
  PortAudio_SetVolume(static_cast<float>(reg.Int("audio.master_volume").Get()) / 100.f);
  PortXD_AudioCategoriesInit();
  ApplyMenuSoundVolume();
  PortMenu_SetScriptPage(EnvIs("XD_MENU_SCRIPT_PAGE", "online") ? &ot::kPage : EnvIs("XD_MENU_SCRIPT_PAGE", "battle") ? &ol::kPage : &kPage);
  PortXD_TrainerCardScriptPage();
  if (std::getenv("XD_LOADSCREEN_TEST") != nullptr) PortMenu_SetScriptPage(&ls::kPage);
  PortVideo_SetNoticeDrawer(&kit::Notice);
  if (xd::picker::Current() == xd::picker::Intent::NewGame) ls::CoverStart(ls::Cover::NewGame, xd::pt::RestartLabel(), "started again for the New Game");
  if (xd::picker::Current() == xd::picker::Intent::Continue) ls::CoverStart(ls::Cover::Continue, "", "started again for the chosen save");
  if (const char* r = std::getenv("XD_ONLINE_ROOM_RESUME"); r != nullptr && r[0] != '\0') ls::CoverRoomResume();
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
  if (sOn || xd::pt::ScreenEnabled() || xd::pt::ChallengesPossible() || xd::pt::EngineQuestionPossible() || xd::pt::TimerWatch() || xd::pt::PickerPossible() || std::getenv("XD_ONLINE_ROOM_RESUME") != nullptr ||
      xd::picker::Current() != xd::picker::Intent::None) {
    PortInput_SetPadFilter(Filter);
  }
  if (sOn) {
    std::fprintf(stderr, "[settings menu] SETTINGS after EXIT in the field menu, left of OPTIONS in the title menu; MODS left of SETTINGS\n");
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
  if (!PortMenu_Active()) ls::CoverDraw();
  if (!sOn || PortMenu_Active()) return;
  const uint32_t flip = PortVI_GetFlipCount();
  if (sTitleBuilding.any) sTitleShown = sTitleBuilding;
  const auto* active = static_cast<const WinWorkLayout*>(windowSearchID(windowGetActiveID()));
  const bool titleInFront = active == nullptr || IsTitleMenu(active);
  if (sTitleShown.any && flip <= sTitleShown.flip + 2 && titleInFront) {
    DrawTitleEntry(ImGui::GetBackgroundDrawList(), sTitleShown, sTitleSelected && !sTitleMods);
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

void PortXD_RegisterOnlineBattlePage(const PortMenuPage* page) { ex::sBattle = page; }
namespace xd::online {
void LobbyBattleOver() { PortMenu_Open(&::ol::kResultPage); }
}
bool PortXD_OpenOnlineTrade() { ot::sLive = false; return PortMenu_Open(&ot::kPage); }
bool PortXD_OpenOnlineTradeLive() {
  ot::sLive = true;
  if (!PortMenu_Open(&ot::kPage)) { ot::sLive = false; return false; }
  return true;
}
bool PortXD_PcTradeAvailable() { return sOn; }
bool PortXD_OpenNewGameSetup() { return PortMenu_Open(&ng::kSetupPage); }
bool PortXD_OpenSavePicker() { return sp::Open() && PortMenu_Open(&sp::kPage); }
extern "C" {
bool PortXD_VsRowEnabled(void) { return sOn && ex::sBattle != nullptr; }
int32_t PortXD_VsMenuExtraSelected(void) {
  if (!PortXD_VsRowEnabled()) return 0;
  if (menuIsCheck(0x118) == 1) menuCloseCustom(0x118, 2, 1);
  sPageFromTitle = true;
  std::fprintf(stderr, "[online lobby] VS MODE > ONLINE BATTLE chosen: opening the lobby\n");
  return PortMenu_Open(ex::sBattle) ? 1 : 0;
}
}

extern "C" int PortXD_ActiveWindowState(int* id) {
  const auto* w = static_cast<const WinWorkLayout*>(windowSearchID(windowGetActiveID()));
  if (w == nullptr) return -1;
  *id = w->menuID;
  return static_cast<int>(w->state) + 256 * static_cast<int>(w->pad_a4);
}
bool PortXD_OpenContinueQuestion() { return PortMenu_Open(&ng::kQuestionPage); }
bool PortXD_OpenEngineQuestion() { return PortMenu_Open(&ng::kEnginePage); }
bool PortXD_OpenStarterPick() { return PortMenu_Open(&ng::sk::kPage); }
bool PortXD_ShowRuleMessage(const std::string& text) {
  if (PortMenu_Active() || PortMenu_Pending()) return false;
  ng::ruleText = text;
  return PortMenu_Open(&ng::kRulePage);
}
extern "C" bool PortXD_LandingCoverActive(void) { return ls::CoverActive(); }
void PortXD_ShowNote(const std::string& text, float seconds) {
  ng::noteText = text;
  ng::noteUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000.f));
}

