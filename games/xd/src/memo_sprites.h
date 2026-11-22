#ifndef XD_MEMO_SPRITES_H
#define XD_MEMO_SPRITES_H

#include <imgui.h>

#include <cstdint>

namespace xd::memo {

enum Key : uint32_t {
  kBanner = 0x169,
  kPanelCentre = 0x174,
  kPanelSide = 0x175,
  kPanelTopBottom = 0x176,
  kPanelCorner = 0x177,
  kBarEnd = 0x173,
  kBarCentre = 0x172,
  kLine = 0x170,
  kRowGreen = 0x17D,
  kGridList = 0x179,
  kGridMatchup = 0x17A,
  kGridView = 0x17C,
  kArrowUp = 0x178,
  kSideArrow = 0x17E,
};

bool Draw(ImDrawList* dl, uint32_t key, float x, float y, float w, float h, ImU32 col = IM_COL32_WHITE);
bool DrawNatural(ImDrawList* dl, uint32_t key, float x, float y, ImU32 col = IM_COL32_WHITE);
int NaturalW(uint32_t key);
int NaturalH(uint32_t key);

bool Panel(ImDrawList* dl, float x0, float y0, float x1, float y1);
bool Banner(ImDrawList* dl);
bool BottomBar(ImDrawList* dl);
bool Grid(ImDrawList* dl, uint32_t gridKey, float x, float y, float dx, float dy);

}

#endif
