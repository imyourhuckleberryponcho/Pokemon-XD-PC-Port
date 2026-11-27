#ifndef ENGINE_ROOM_PREVIEW_H
#define ENGINE_ROOM_PREVIEW_H

#include "room_ccd.h"

namespace engine::rooms {

std::vector<uint8_t> RgbToPng(const std::vector<uint8_t>& rgb, int width, int height);
std::vector<uint8_t> CcdTopViewPng(const Ccd& ccd, int size);

}

#endif
