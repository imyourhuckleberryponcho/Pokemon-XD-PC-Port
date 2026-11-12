#ifndef PORT_BUTTON_ART_H
#define PORT_BUTTON_ART_H

#include <controls.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace port_button_art {

struct Font {
  std::function<bool(char32_t ch, std::vector<uint8_t>& alpha, int& w, int& h)> glyph;
};

enum class Kind : uint8_t { Face, Symbol, Plate, Dpad, Key, None };
Kind KindOf(const engine::controls::ButtonLabel& label);

void Draw(const engine::controls::ButtonLabel& label, uint8_t* rgba, int stride, int x, int y, int w, int h,
          const Font* font,
          bool withShadow = true);

}

#endif
