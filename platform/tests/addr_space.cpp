#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int gGlobal;
static uint64_t gHighest = 0;
static bool gAllLow = true;

static void report(const char* what, const void* p) {
  const auto a = reinterpret_cast<uint64_t>(p);
  const bool low = a < 0x80000000ull;
  gAllLow &= low;
  if (a > gHighest) {
    gHighest = a;
  }
  std::printf("  %-34s 0x%012llx %s\n", what, (unsigned long long)a, low ? "" : "  <-- above 2 GB");
}

static void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
  if (level >= LOG_WARNING) {
    std::fprintf(stderr, "[%s] %.*s\n", module, (int)len, message);
  }
  if (level == LOG_FATAL) {
    std::abort();
  }
}

alignas(32) static u8 fifo[256 * 1024];

int main(int argc, char* argv[]) {
  void* arena = VirtualAlloc(reinterpret_cast<void*>(0x20000000), 256u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  AuroraConfig config{};
  config.appName = "XD port - address space test";
  config.logCallback = &log_callback;
  config.vsync = true;
  aurora_initialize(argc, argv, &config);
  GXInit(fifo, sizeof(fifo));
  GXSetCopyClear(GXColor{40, 20, 20, 255}, GX_MAX_Z24);

  for (int frame = 0; frame < 180;) {
    bool exiting = false;
    for (const AuroraEvent* e = aurora_update(); e != nullptr && e->type != AURORA_NONE; ++e) {
      exiting |= e->type == AURORA_EXIT;
    }
    if (exiting) {
      break;
    }
    if (aurora_begin_frame()) {
      aurora_end_frame();
      frame++;
    }
  }

  std::printf("Addresses after 180 frames with Aurora running:\n");
  int local = 0;
  report("program globals", &gGlobal);
  report("program code", reinterpret_cast<const void*>(&main));
  report("main thread stack", &local);
  report("small malloc (1 KB)", std::malloc(1024));
  report("big malloc (256 MB)", std::malloc(256u << 20));
  std::vector<char> v(64u << 20);
  report("C++ new (64 MB)", v.data());
  report("VirtualAlloc (512 MB)", VirtualAlloc(nullptr, 512u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  report("game arena (reserved at start)", arena);
  std::printf("  %-34s %s\n", "fixed low arena at 0x20000000", arena ? "OK" : "FAILED (address taken)");

  HMODULE mods[1024];
  DWORD needed = 0;
  if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
    uint64_t highestEnd = 0;
    char highestName[MAX_PATH] = "";
    for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
      MODULEINFO mi{};
      GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi));
      const uint64_t end = reinterpret_cast<uint64_t>(mi.lpBaseOfDll) + mi.SizeOfImage;
      if (end > highestEnd) {
        highestEnd = end;
        GetModuleBaseNameA(GetCurrentProcess(), mods[i], highestName, sizeof(highestName));
      }
    }
    std::printf("  %-34s 0x%012llx (%s, %lu modules)\n", "highest DLL end", (unsigned long long)highestEnd,
                highestName, (unsigned long)(needed / sizeof(HMODULE)));
    gAllLow &= highestEnd <= 0x80000000ull;
  }

  uint64_t highestRegion = 0;
  MEMORY_BASIC_INFORMATION mbi;
  for (uint8_t* p = nullptr; VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi);
       p = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
    if (mbi.State != MEM_FREE) {
      highestRegion = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
  }
  std::printf("  %-34s 0x%012llx\n", "highest used region (any kind)", (unsigned long long)highestRegion);

  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof(pmc);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
  std::printf("Memory in use: %llu MB private, %llu MB working set (of 2048 MB available below 2 GB)\n",
              (unsigned long long)(pmc.PrivateUsage >> 20), (unsigned long long)(pmc.WorkingSetSize >> 20));
  std::printf("Result: %s\n", gAllLow ? "everything below 2 GB" : "some memory is above 2 GB");
  std::printf("Game-owned memory (globals, code, arena): %s\n",
              reinterpret_cast<uint64_t>(&gGlobal) < 0x80000000ull && arena != nullptr ? "below 2 GB" : "NOT below 2 GB");

  aurora_shutdown();
  return gAllLow ? 0 : 1;
}
