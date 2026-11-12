#include <atomic>
#include <cstdint>
#ifdef _MSC_VER
#include <intrin.h>
#else
#define _ReturnAddress() __builtin_return_address(0)
#define __declspec(x) __attribute__((x))
#endif

extern "C" {
int64_t OSGetTime(void);
int64_t PortTime_Now(void);
bool PortTime_IsDeterministic(void);

using PortTimeGameReadHook = int64_t (*)(int64_t live, const void* caller);
}

namespace {
std::atomic<PortTimeGameReadHook> sHook{nullptr};
std::atomic<int64_t (*)(int64_t)> sFilter{nullptr};

int64_t game_read(const void* caller, bool realTime = false) {
  static int64_t lastVirtual = 0, reads = 0;
  int64_t live;
  if (PortTime_IsDeterministic()) {
    const int64_t v = PortTime_Now();
    reads = (v == lastVirtual) ? reads + 1 : 0;
    lastVirtual = v;
    live = v + reads;
  } else {
    live = OSGetTime();
    if (const auto filter = sFilter.load(std::memory_order_acquire); filter != nullptr && !realTime) {
      static int64_t lastGame = 0, gameReads = 0;
      int64_t v = filter(live);
      if (v < lastGame) {
        v = lastGame;
      }
      gameReads = (v == lastGame) ? gameReads + 1 : 0;
      lastGame = v;
      live = v + gameReads;
    }
  }
  const PortTimeGameReadHook hook = sHook.load(std::memory_order_acquire);
  return hook != nullptr ? hook(live, caller) : live;
}
}

extern "C" {

void PortTime_SetGameReadHook(PortTimeGameReadHook hook) { sHook.store(hook, std::memory_order_release); }

void PortTime_SetGameClockFilter(int64_t (*filter)(int64_t pcClock)) {
  sFilter.store(filter, std::memory_order_release);
}

__declspec(noinline) int64_t PortTime_GameGetTime(void) { return game_read(_ReturnAddress()); }

__declspec(noinline) int64_t PortTime_GameGetRealTime(void) { return game_read(_ReturnAddress(), true); }

__declspec(noinline) uint32_t PortTime_GameGetTick(void) {
  return static_cast<uint32_t>(game_read(_ReturnAddress()) & 0xFFFFFFFF);
}

int64_t PortTime_GamePerfGetTime(void) { return PortTime_IsDeterministic() ? PortTime_Now() : OSGetTime(); }

uint32_t PortTime_CardSeconds(void) { return static_cast<uint32_t>(PortTime_Now() / 40500000); }

}
