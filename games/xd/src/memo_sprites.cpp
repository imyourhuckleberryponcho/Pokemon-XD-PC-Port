#include "memo_sprites.h"

#include "ui_kit2.h"
#include "xd_ui.h"

#include <game/bitfield.h>

#include <cstdint>

extern "C" {
void* menuSpriteBiosGetPtr(uint32_t id);
void* menuSpriteBiosGetValPtr(void* sprite);
}

namespace xd::memo {
namespace {

struct SpriteView {
  GC_BITS4(uint16_t last : 1, uint16_t fit : 1, uint16_t type : 2, uint16_t seq : 12)
  uint8_t pad2;
  int8_t offX, offY;
  uint8_t alpha;
};
struct ValView {
  int16_t u, v, w, h;
  uint32_t tex;
};

bool DrawPart(ImDrawList* dl, const SpriteView* info, float x, float y, float w, float h, ImU32 col) {
  const auto* val = static_cast<const ValView*>(menuSpriteBiosGetValPtr(const_cast<SpriteView*>(info)));
  if (val == nullptr || val->w <= 0 || val->h <= 0) return false;
  const bool fx = w < 0, fy = h < 0;
  const float aw = fx ? -w : w, ah = fy ? -h : h;
  const float px = x + info->offX, py = y + info->offY;
  xd::kit2::Piece p{val->tex >> 16, val->u, val->v, val->w, val->h};
  return xd::kit2::DrawPiece(dl, xd::ui::At(px, py), xd::ui::At(px + aw, py + ah), p, fx, fy, col);
}

}

bool Draw(ImDrawList* dl, uint32_t key, float x, float y, float w, float h, ImU32 col) {
  if (key == 0) return false;
  const auto* info = static_cast<const SpriteView*>(menuSpriteBiosGetPtr(key));
  bool any = false;
  for (int part = 0; info != nullptr && part < 8; part++) {
    any = DrawPart(dl, info, x, y, w, h, col) || any;
    if (info->last) break;
    info = static_cast<const SpriteView*>(menuSpriteBiosGetPtr(info->seq));
  }
  return any;
}

int NaturalW(uint32_t key) {
  const auto* info = static_cast<const SpriteView*>(menuSpriteBiosGetPtr(key));
  const auto* val = info ? static_cast<const ValView*>(menuSpriteBiosGetValPtr(const_cast<SpriteView*>(info))) : nullptr;
  return val ? val->w : 0;
}
int NaturalH(uint32_t key) {
  const auto* info = static_cast<const SpriteView*>(menuSpriteBiosGetPtr(key));
  const auto* val = info ? static_cast<const ValView*>(menuSpriteBiosGetValPtr(const_cast<SpriteView*>(info))) : nullptr;
  return val ? val->h : 0;
}

bool DrawNatural(ImDrawList* dl, uint32_t key, float x, float y, ImU32 col) {
  const int w = NaturalW(key), h = NaturalH(key);
  return w > 0 && Draw(dl, key, x, y, static_cast<float>(w), static_cast<float>(h), col);
}

bool Panel(ImDrawList* dl, float x0, float y0, float x1, float y1) {
  const float cw = x1 - x0 - 48.f, ch = y1 - y0 - 32.f;
  bool ok = Draw(dl, kPanelCentre, x0 + 24, y0 + 16, cw, ch);
  ok = Draw(dl, kPanelSide, x0, y0 + 16, 24, ch) && ok;
  ok = Draw(dl, kPanelSide, x1 - 24, y0 + 16, -24, ch) && ok;
  ok = Draw(dl, kPanelTopBottom, x0 + 24, y0, cw, 16) && ok;
  ok = Draw(dl, kPanelTopBottom, x0 + 24, y1 - 16, cw, -16) && ok;
  ok = Draw(dl, kPanelCorner, x0, y0, 24, 16) && ok;
  ok = Draw(dl, kPanelCorner, x1 - 24, y0, -24, 16) && ok;
  ok = Draw(dl, kPanelCorner, x0, y1 - 16, 24, -16) && ok;
  ok = Draw(dl, kPanelCorner, x1 - 24, y1 - 16, -24, -16) && ok;
  return ok;
}

bool Banner(ImDrawList* dl) {
  bool ok = Draw(dl, kBanner, 46, 73, 275, 36);
  return Draw(dl, kBanner, 321, 73, -275, 36) && ok;
}

bool BottomBar(ImDrawList* dl) {
  bool ok = Draw(dl, kBarEnd, 45, 376, 10, 30);
  ok = Draw(dl, kBarEnd, 585, 376, -10, 30) && ok;
  return Draw(dl, kBarCentre, 55, 376, 530, 30) && ok;
}

bool Grid(ImDrawList* dl, uint32_t key, float x, float y, float dx, float dy) {
  const float w = static_cast<float>(NaturalW(key)), h = static_cast<float>(NaturalH(key));
  if (w <= 0) return false;
  bool ok = Draw(dl, key, x, y, w, h);
  ok = Draw(dl, key, x + dx, y, -w, h) && ok;
  ok = Draw(dl, key, x, y + dy, w, -h) && ok;
  ok = Draw(dl, key, x + dx, y + dy, -w, -h) && ok;
  return ok;
}

}
