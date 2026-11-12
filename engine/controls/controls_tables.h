#ifndef ENGINE_CONTROLS_TABLES_H
#define ENGINE_CONTROLS_TABLES_H

#include <cstdint>

namespace engine::controls::detail {

struct KeyInfo {
  uint16_t scancode;
  const char* token;
  const char* name;
};
const KeyInfo* FindKey(uint16_t scancode);

extern const char* const kPadButtonTokens[];
struct AxisToken {
  uint8_t axis;
  int8_t sign;
  const char* token;
};
extern const AxisToken kAxisTokens[];
extern const int kAxisTokenCount;

}

#endif
