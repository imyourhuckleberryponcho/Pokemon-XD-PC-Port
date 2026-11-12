#include <cstdint>

extern "C" {
void PortCpuCost_FrameStart(void);
void PortCpuCost_FrameEnd(uint32_t id);
void PortCpuCost_WaitBegin(void);
void PortCpuCost_WaitEnd(void);
int32_t GSgfxPerfbarCPUEntry(uint32_t color);
extern int32_t pokecoloFrameCounter;
}
void GSgfxPerfbarCPUEnd();
void GSgfxPerfbarCPUStart();

void PortXD_PerfbarCPUEnd() {
  PortCpuCost_FrameEnd(static_cast<uint32_t>(pokecoloFrameCounter));
  GSgfxPerfbarCPUEnd();
}

extern "C" void PortXD_PerfMarkFrameStart(void);

void PortXD_PerfbarCPUStart() {
  GSgfxPerfbarCPUStart();
  PortXD_PerfMarkFrameStart();
  PortCpuCost_FrameStart();
}

extern "C" int32_t PortXD_PerfbarCPUEntry(uint32_t color) {
  static bool sInnerOpen = false;
  const int32_t prev = GSgfxPerfbarCPUEntry(color);
  if (color != 0xFFFF00u) {
    PortCpuCost_WaitEnd();
  } else if (static_cast<uint32_t>(prev) == 0xFFFF00u && sInnerOpen) {
    sInnerOpen = false;
    PortCpuCost_WaitEnd();
  } else {
    sInnerOpen = static_cast<uint32_t>(prev) == 0xFFFF00u;
    PortCpuCost_WaitBegin();
  }
  return prev;
}
