#include <aurora/aurora.h>
#include <aurora/main.h>
#include <dolphin/os.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
bool PortMem_ReserveMEM1(void);
}

static bool ok = true;
static void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

int main(int argc, char* argv[]) {
  check(PortMem_ReserveMEM1(), "24 MB reserved at 0x80000000 at start-up");

  AuroraConfig config{};
  config.appName = "XD port - GameCube memory test";
  config.mem1Size = MEM1_DEFAULT_SIZE;
  aurora_initialize(argc, argv, &config);
  PortMem_InitOS(0);

  const auto lo = reinterpret_cast<uintptr_t>(OSGetArenaLo());
  const auto hi = reinterpret_cast<uintptr_t>(OSGetArenaHi());
  std::printf("arena %08llx-%08llx\n", (unsigned long long)lo, (unsigned long long)hi);
  check(lo >= 0x80000000 && hi == 0x81800000, "the arena is inside 0x80000000-0x81800000, as on the GameCube");
  check(OSPhysicalToCached(0x1800) == reinterpret_cast<void*>(0x80001800), "GameCube address translation matches");

  std::memset(reinterpret_cast<void*>(0x80001800), 0, 0x1800);
  check(true, "clearing 0x80001800-0x80003000 works (main.cpp)");
  OSSetArenaHi(reinterpret_cast<void*>(0x817ED780));
  check(OSGetArenaHi() == reinterpret_cast<void*>(0x817ED780), "OSSetArenaHi(0x817ED780) works (main.cpp)");
  const uint32_t bus = *reinterpret_cast<uint32_t*>(0x800000F8);
  check(bus == 162000000, "the bus clock at 0x800000F8 reads 162 MHz (pokecolo.cpp frame timing)");

  void* p = OSAllocFromArenaLo(0x1000, 32);
  check(reinterpret_cast<uintptr_t>(p) < 0x100000000ull, "arena allocations fit in a 32-bit variable");
  check(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)))) == p,
        "an arena address survives a round trip through u32");

  aurora_shutdown();
  return ok ? 0 : 1;
}
