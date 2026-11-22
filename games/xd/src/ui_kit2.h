#ifndef XD_UI_KIT2_H
#define XD_UI_KIT2_H

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace xd::kit2 {

struct Image {
  ImTextureID id{};
  int w = 0, h = 0;
  bool ok = false;
};

const Image& Texture(uint32_t resId);

bool Loaded(uint32_t resId);

struct Piece {
  uint32_t tex = 0;
  int u = 0, v = 0, w = 0, h = 0;
};

bool DrawPiece(ImDrawList* dl, ImVec2 a, ImVec2 b, const Piece& p, bool flipX = false, bool flipY = false, ImU32 col = IM_COL32_WHITE);

std::vector<uint8_t> DiscFile(const char* name);

void Reset();
int Count();

}

#endif
