#include <port_audio.h>

#include <cstdint>
#include <cstdio>

extern "C" uint32_t PortVI_GetFlipCount(void);

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

uint64_t ft(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

struct Times {
  uint64_t idle = 0, system = 0;
  uint64_t process = 0, mainThread = 0;
  uint32_t frames = 0;
  PortAudioStats audio{};
};

Times read_times(HANDLE mainThread) {
  Times t;
  FILETIME idle, kernel, user, created, exited;
  if (GetSystemTimes(&idle, &kernel, &user)) {
    t.idle = ft(idle);
    t.system = ft(kernel) + ft(user);
  }
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) t.process = ft(kernel) + ft(user);
  if (mainThread != nullptr && GetThreadTimes(mainThread, &created, &exited, &kernel, &user)) {
    t.mainThread = ft(kernel) + ft(user);
  }
  t.frames = PortVI_GetFlipCount();
  PortAudio_GetStats(&t.audio);
  return t;
}

struct ProcessorPower {
  ULONG number, maxMhz, currentMhz, mhzLimit, maxIdleState, currentIdleState;
};

bool processor_speed(unsigned processors, ULONG* now, ULONG* full, ULONG* limit) {
  static HMODULE dll = LoadLibraryW(L"powrprof.dll");
  if (dll == nullptr) return false;
  using Fn = LONG(WINAPI*)(int, void*, ULONG, void*, ULONG);
  static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(dll, "CallNtPowerInformation")));
  if (fn == nullptr || processors == 0) return false;
  std::vector<ProcessorPower> info(processors);
  if (fn(11, nullptr, 0, info.data(),
         static_cast<ULONG>(info.size() * sizeof(ProcessorPower))) != 0) {
    return false;
  }
  uint64_t sumNow = 0;
  ULONG maxFull = 0, minLimit = ~0ul;
  for (const ProcessorPower& p : info) {
    sumNow += p.currentMhz;
    if (p.maxMhz > maxFull) maxFull = p.maxMhz;
    if (p.mhzLimit < minLimit) minLimit = p.mhzLimit;
  }
  *now = static_cast<ULONG>(sumNow / info.size());
  *full = maxFull;
  *limit = minLimit;
  return true;
}

void power_text(char* out, size_t size) {
  SYSTEM_POWER_STATUS s{};
  if (!GetSystemPowerStatus(&s)) {
    std::snprintf(out, size, "power: unknown");
    return;
  }
  const char* line = s.ACLineStatus == 1 ? "plugged in" : s.ACLineStatus == 0 ? "battery" : "unknown";
  if (s.BatteryFlag & 128) {
    std::snprintf(out, size, "power: %s (no battery)", line);
  } else {
    std::snprintf(out, size, "power: %s, saver %s", line, s.SystemStatusFlag == 1 ? "ON" : "off");
  }
}

std::atomic<bool> sStarted{false};

void run(HANDLE mainThread, double seconds) {
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const unsigned processors = si.dwNumberOfProcessors;
  Times before = read_times(mainThread);
  ULONGLONG t0 = GetTickCount64();
  for (;;) {
    Sleep(static_cast<DWORD>(seconds * 1000.0));
    const Times now = read_times(mainThread);
    const ULONGLONG t1 = GetTickCount64();
    const double wall = static_cast<double>(t1 - t0) / 1000.0;
    if (wall <= 0.0) continue;
    const double all = static_cast<double>(now.system - before.system);
    const double busy = all - static_cast<double>(now.idle - before.idle);
    const double game = static_cast<double>(now.process - before.process);
    const double pcBusy = all > 0 ? busy / all * 100.0 : 0.0;
    const double gameShare = all > 0 ? game / all * 100.0 : 0.0;
    const double others = pcBusy - gameShare > 0.0 ? pcBusy - gameShare : 0.0;
    const double mainShare = static_cast<double>(now.mainThread - before.mainThread) / (wall * 1e7) * 100.0;
    char speed[96] = "processor speed unknown";
    ULONG mhzNow = 0, mhzFull = 0, mhzLimit = 0;
    if (processor_speed(processors, &mhzNow, &mhzFull, &mhzLimit)) {
      std::snprintf(speed, sizeof(speed), "processor at %lu of %lu MHz (limit %lu)", mhzNow, mhzFull, mhzLimit);
    }
    char power[64];
    power_text(power, sizeof(power));
    const PortAudioStats& a = now.audio;
    const PortAudioStats& b = before.audio;
    const double gapMs = static_cast<double>(a.outShortBytes - b.outShortBytes) / 4.0 * 1000.0 / 32028.0;
    const double frozenMs = static_cast<double>(a.frozenQuietBytes - b.frozenQuietBytes) / 4.0 * 1000.0 / 32028.0;
    char frozen[96] = "";
    if (frozenMs >= 1.0) std::snprintf(frozen, sizeof(frozen), " (+ %.0f ms of quiet while a menu held the game)", frozenMs);
    const uint32_t pieces = a.aiBlocks - b.aiBlocks;
    const double pieceMs = pieces > 0 ? static_cast<double>(a.aiHandlerNs - b.aiHandlerNs) / 1e6 / pieces : 0.0;
    const double chipMs = pieces > 0 ? static_cast<double>(a.dspNs - b.dspNs) / 1e6 / pieces : 0.0;
    std::fprintf(stderr,
                 "[pc load] %.0f s: PC %.0f%% busy (the game %.0f%%, other programs %.0f%%) of %u processors; the "
                 "game's main thread computed %.0f%% of the time; %s; %s; %.1f game frames a second; sound: %u pieces, "
                 "%u dropped, %u gaps heard (%.0f ms)%s, queue found empty %u times; making a piece (5 ms of sound) "
                 "took %.2f ms, of it the sound chip %.2f ms (%.0f of its instructions); pieces came up to %u ms "
                 "apart; sound reserve %u ms (raised %u times)\n",
                 wall, pcBusy, gameShare, others, processors, mainShare, speed, power,
                 static_cast<double>(now.frames - before.frames) / wall, pieces,
                 a.aiBlocksDropped - b.aiBlocksDropped, a.outShortTimes - b.outShortTimes, gapMs, frozen,
                 a.aiUnderruns - b.aiUnderruns, pieceMs, chipMs,
                 pieces > 0 ? static_cast<double>(a.dspInstructions - b.dspInstructions) / pieces : 0.0,
                 a.longestWaitMs, a.reserveMs,
                 a.reserveGrown - b.reserveGrown);
    before = now;
    t0 = t1;
  }
}

}

extern "C" void PortXD_PcLoadStart(double seconds) {
  if (seconds <= 0.0 || sStarted.exchange(true)) return;
  HANDLE mainThread = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread, THREAD_QUERY_INFORMATION,
                  FALSE, 0);
  std::thread(run, mainThread, seconds).detach();
}

#else

extern "C" void PortXD_PcLoadStart(double) {}

#endif
