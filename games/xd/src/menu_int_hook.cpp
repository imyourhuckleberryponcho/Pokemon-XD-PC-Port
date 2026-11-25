#include <dolphin/types.h>
#include <game/gcptr.h>
#include <game/shared/menu_work.h>

#include <cmath>

extern "C" {
void winSpriteSetDisp(tagSPRITE_WORK* sprite, bool on);
f32 timeGetLastFrameSec(void);
}

namespace {

float sPulseTime = 0.0f;

u8 PulseAlpha() {
  const float c = std::cos(6.2831855f * sPulseTime / 1.5f);
  u16 alpha = static_cast<u16>(300.0f * ((1.0f + c) * 0.5f));
  if (alpha > 0xFF) alpha = 0xFF;
  if (alpha < 50) alpha = 50;
  sPulseTime += timeGetLastFrameSec();
  return static_cast<u8>(alpha);
}

}

extern "C" void menuIntHook(tagWINDOW_WORK* window, tagSPRITE_WORK* item) {
  const s32* data = reinterpret_cast<const s32*>(window->params[0]);
  s32 kind;
  switch (item->id) {
    case 0x4BE: kind = 0; break;
    case 0x505: kind = 1; break;
    case 0x506: kind = 2; break;
    default: return;
  }
  if (data == nullptr || *data != kind) {
    winSpriteSetDisp(item, false);
    return;
  }
  winSpriteSetDisp(item, true);
  if (window->state != 0) item->alpha = PulseAlpha();
}
