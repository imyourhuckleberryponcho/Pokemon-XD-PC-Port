#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
uint32_t GSgfxPerfbarCPUEntry_Game(uint32_t color);
unsigned PortCpuCost_ReadCounters(uint64_t* out, unsigned max);
const char* PortCpuCost_CounterName(unsigned i);
int64_t PortTime_Now(void);
extern int32_t pokecoloFrameCounter;
}

namespace {
struct Log {
  FILE* f = nullptr;
  int32_t lo = 0;
  int32_t hi = INT32_MAX;
};

Log& mark_log() {
  static Log log = [] {
    Log l;
    const char* path = std::getenv("XD_PERF_MARK_LOG");
    if (path == nullptr || path[0] == '\0') return l;
    l.f = std::fopen(path, "w");
    if (const char* r = std::getenv("XD_PERF_MARK_PASSES"); r != nullptr) {
      std::sscanf(r, "%d-%d", &l.lo, &l.hi);
    }
    if (l.f != nullptr) {
      std::fprintf(l.f, "# pm <pass> <colour> <clock>");
      for (unsigned i = 0; PortCpuCost_CounterName(i) != nullptr; ++i) {
        std::fprintf(l.f, " %s", PortCpuCost_CounterName(i));
      }
      std::fprintf(l.f, "\n");
    }
    return l;
  }();
  return log;
}
void line(Log& l, const char* kind, uint32_t color) {
  uint64_t c[16];
  const unsigned n = PortCpuCost_ReadCounters(c, 16);
  std::fprintf(l.f, "%s %d %06x %lld", kind, pokecoloFrameCounter, color, static_cast<long long>(PortTime_Now()));
  for (unsigned i = 0; i < n; ++i) {
    std::fprintf(l.f, " %llu", static_cast<unsigned long long>(c[i]));
  }
  std::fprintf(l.f, "\n");
}
}

extern "C" void PortXD_PerfMarkFrameStart(void) {
  Log& l = mark_log();
  if (l.f != nullptr && pokecoloFrameCounter >= l.lo - 1 && pokecoloFrameCounter <= l.hi) {
    line(l, "fs", 0);
  }
}

extern "C" uint32_t GSgfxPerfbarCPUEntry(uint32_t color) {
  const uint32_t prev = GSgfxPerfbarCPUEntry_Game(color);
  Log& l = mark_log();
  if (l.f != nullptr && pokecoloFrameCounter >= l.lo && pokecoloFrameCounter <= l.hi) {
    line(l, "pm", color);
  }
  return prev;
}
