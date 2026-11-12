#include <dolphin/types.h>
#include <game/gsgfxstate.h>

extern "C" GSgfxState* gsgfxState;
extern "C" void PortGX_SetFrameEndProbe(int (*probe)(void));

namespace {
int FrameEnd(void) { return gsgfxState != nullptr && gsgfxState->phase == 1 ? 1 : 0; }
}

extern "C" void PortXD_ReplayPacingInit(void) { PortGX_SetFrameEndProbe(FrameEnd); }
