#include <dolphin/ai.h>
#include <port_audio.h>
#include "time_stretch.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#else
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

extern void* MEM1Start;

extern "C" {
int64_t OSGetTime(void);
int64_t PortTime_Now(void);
uint32_t PortVI_GetFlipCount(void);
u32 VIGetRetraceCount(void);
void OSSetPeriodicAlarm(void* alarm, int64_t start, int64_t period, void (*handler)(void*, void*));
void PortOS_InterruptCost(int64_t ticks);
void OSCancelAlarm(void* alarm);
}

namespace {
bool musyx_clock_stands_still() {
  static const bool still = [] {
    const char* e = std::getenv("XD_MUSYX_CLOCK");
    return !(e != nullptr && std::strcmp(e, "thread") == 0);
  }();
  return still;
}
}
#ifndef _WIN32
namespace {
struct MusyxClock {
  long thread = 0;
  uint64_t cpuNs = 0;
  int64_t wall = 0;
  uint64_t ticks = 0;
} sMusyxClock;
}

extern "C" bool PortTime_IsDeterministic(void);

extern "C" u32 PortMusyX_GetTick(void) {
  if (musyx_clock_stands_still()) return 0;
  if (PortTime_IsDeterministic()) {
    return static_cast<u32>(PortTime_Now());
  }
  MusyxClock& c = sMusyxClock;
  const long thread = static_cast<long>(syscall(SYS_gettid));
  timespec t{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
  const uint64_t cpuNs = static_cast<uint64_t>(t.tv_sec) * 1000000000ull + static_cast<uint64_t>(t.tv_nsec);
  const int64_t wall = OSGetTime();
  if (c.thread == thread) {
    c.ticks += static_cast<uint64_t>(static_cast<double>(cpuNs - c.cpuNs) * 40500000.0 / 1e9);
  } else if (c.thread != 0) {
    c.ticks += static_cast<uint64_t>(wall - c.wall);
  }
  c.thread = thread;
  c.cpuNs = cpuNs;
  c.wall = wall;
  return static_cast<u32>(c.ticks);
}
#else
namespace {
struct MusyxClock {
  DWORD thread = 0;
  ULONG64 cycles = 0;
  int64_t wall = 0;
  uint64_t ticks = 0;
  LARGE_INTEGER qpc0{};
  uint64_t tsc0 = 0;
  double cyclesPerTick = 3.0e9 / 40500000.0;
} sMusyxClock;

void measure_cycle_rate(MusyxClock& c) {
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  const uint64_t tsc = __rdtsc();
  if (c.tsc0 == 0) {
    c.qpc0 = now;
    c.tsc0 = tsc;
    return;
  }
  const double seconds = static_cast<double>(now.QuadPart - c.qpc0.QuadPart) / static_cast<double>(freq.QuadPart);
  if (seconds > 0.05) {
    c.cyclesPerTick = static_cast<double>(tsc - c.tsc0) / seconds / 40500000.0;
  }
}
}

extern "C" bool PortTime_IsDeterministic(void);

extern "C" u32 PortMusyX_GetTick(void) {
  if (musyx_clock_stands_still()) return 0;
  if (PortTime_IsDeterministic()) {
    return static_cast<u32>(PortTime_Now());
  }
  MusyxClock& c = sMusyxClock;
  const DWORD thread = GetCurrentThreadId();
  ULONG64 cycles = 0;
  QueryThreadCycleTime(GetCurrentThread(), &cycles);
  const int64_t wall = OSGetTime();
  measure_cycle_rate(c);
  if (c.thread == thread) {
    c.ticks += static_cast<uint64_t>(static_cast<double>(cycles - c.cycles) / c.cyclesPerTick);
  } else if (c.thread != 0) {
    c.ticks += static_cast<uint64_t>(wall - c.wall);
  }
  c.thread = thread;
  c.cycles = cycles;
  c.wall = wall;
  return static_cast<u32>(c.ticks);
}
#endif

namespace {

constexpr int64_t kClock54 = 54000000;
constexpr int64_t kDivider32k = 1686;
constexpr int kSampleRate = 32028;
constexpr int64_t kTimerClock = 40500000;
constexpr int kTargetQueuedMs = 40;
constexpr int kMaxQueuedMs = 200;
constexpr int kMaxReserveMs = 300;

AIDCallback sCallback = nullptr;
u32 sNextAddr = 0, sNextLength = 0;
bool sRunning = false;
unsigned char sAlarm[0x30];
SDL_AudioStream* sStream = nullptr;
float sGain = 1.f;
bool sPaused = false;
std::vector<int16_t> sConvert;
uint32_t sBlocks = 0, sDropped = 0, sNonSilent = 0, sUnderruns = 0;
std::atomic<uint64_t> sHandlerNs{0};

FILE* sDump = nullptr;
FILE* sDumpFrames = nullptr;
uint32_t sDumpLastFrame = ~0u;
uint32_t sDumpBytes = 0;
std::atomic<uint32_t> sStarved{0}, sStarvedBytes{0};

void dump_open() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  const char* path = std::getenv("XD_AUDIO_DUMP");
  if (path == nullptr || path[0] == '\0') return;
  sDump = std::fopen(path, "wb");
  if (sDump == nullptr) {
    std::fprintf(stderr, "[platform/audio] can't write %s\n", path);
    return;
  }
  uint8_t header[44] = {};
  std::fwrite(header, 1, sizeof(header), sDump);
  const std::string framesPath = std::string(path) + ".frames.txt";
  sDumpFrames = std::fopen(framesPath.c_str(), "w");
}

void put32le(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void dump_block(const int16_t* samples, uint32_t bytes) {
  if (sDump == nullptr) return;
  const uint32_t frame = PortVI_GetFlipCount();
  if (sDumpFrames != nullptr && frame != sDumpLastFrame) {
    sDumpLastFrame = frame;
    std::fprintf(sDumpFrames, "sample %u frame %u field %u\n", sDumpBytes / 4, frame, VIGetRetraceCount());
    std::fflush(sDumpFrames);
  }
  std::fwrite(samples, 1, bytes, sDump);
  sDumpBytes += bytes;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                   0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
  put32le(h + 4, 36 + sDumpBytes);
  put32le(h + 24, kSampleRate);
  put32le(h + 28, kSampleRate * 4);
  put32le(h + 40, sDumpBytes);
  std::fseek(sDump, 0, SEEK_SET);
  std::fwrite(h, 1, sizeof(h), sDump);
  std::fseek(sDump, 0, SEEK_END);
  std::fflush(sDump);
  if (sBlocks % 2000 == 0) {
    std::fprintf(stderr, "[platform/audio] %u blocks, %u dropped, %u underruns; sound card short of data %u times "
                         "(%u bytes)\n",
                 sBlocks, sDropped, sUnderruns, sStarved.load(), sStarvedBytes.load());
  }
}

struct OutDump {
  FILE* file = nullptr;
  uint32_t bytes = 0;
  bool tried = false;
} sOut;

void out_dump(const void* samples, uint32_t bytes) {
  if (!sOut.tried) {
    sOut.tried = true;
    const char* path = std::getenv("XD_AUDIO_DUMP_OUT");
    if (path != nullptr && path[0] != '\0') {
      sOut.file = std::fopen(path, "wb");
      if (sOut.file == nullptr) std::fprintf(stderr, "[platform/audio] can't write %s\n", path);
      uint8_t header[44] = {};
      if (sOut.file != nullptr) std::fwrite(header, 1, sizeof(header), sOut.file);
    }
  }
  if (sOut.file == nullptr || bytes == 0) return;
  std::fwrite(samples, 1, bytes, sOut.file);
  sOut.bytes += bytes;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                   0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
  put32le(h + 4, 36 + sOut.bytes);
  put32le(h + 24, kSampleRate);
  put32le(h + 28, kSampleRate * 4);
  put32le(h + 40, sOut.bytes);
  std::fseek(sOut.file, 0, SEEK_SET);
  std::fwrite(h, 1, sizeof(h), sOut.file);
  std::fseek(sOut.file, 0, SEEK_END);
  std::fflush(sOut.file);
}

void put_out(const void* samples, int bytes) {
  SDL_PutAudioStreamData(sStream, samples, bytes);
  out_dump(samples, static_cast<uint32_t>(bytes));
}

std::vector<int16_t> sTail;
size_t sTailPos = 0;
uint32_t sFadeIn = 0, sFadeInPos = 0;

void shape_block() {
  const size_t frames = sConvert.size() / 2;
  if (sFadeInPos < sFadeIn) {
    for (size_t f = 0; f < frames && sFadeInPos < sFadeIn; f++, sFadeInPos++) {
      const double t = static_cast<double>(sFadeInPos) / sFadeIn;
      const double g = 0.5 - 0.5 * std::cos(t * 3.141592653589793);
      sConvert[f * 2] = static_cast<int16_t>(sConvert[f * 2] * g);
      sConvert[f * 2 + 1] = static_cast<int16_t>(sConvert[f * 2 + 1] * g);
    }
  }
  if (sTailPos < sTail.size()) {
    const size_t total = sTail.size() / 2;
    for (size_t f = 0; f < frames && sTailPos < sTail.size(); f++, sTailPos += 2) {
      const double t = static_cast<double>(sTailPos / 2) / static_cast<double>(total);
      const double inGain = std::sin(t * 1.5707963267948966), outGain = std::cos(t * 1.5707963267948966);
      for (int c = 0; c < 2; c++) {
        const double v = sConvert[f * 2 + c] * inGain + sTail[sTailPos + c] * outGain;
        sConvert[f * 2 + c] = static_cast<int16_t>(v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : v);
      }
    }
    if (sTailPos >= sTail.size()) {
      sTail.clear();
      sTailPos = 0;
    }
  }
}

void SDLCALL on_device_wants(void*, SDL_AudioStream*, int additional, int) {
  if (additional > 0) {
    sStarved++;
    sStarvedBytes += static_cast<uint32_t>(additional);
  }
}

void open_output() {
  if (sStream != nullptr) return;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    std::fprintf(stderr, "[platform/audio] no sound: SDL audio didn't start (%s)\n", SDL_GetError());
    return;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, kSampleRate};
  sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (sStream == nullptr) {
    std::fprintf(stderr, "[platform/audio] no sound: no playback device (%s)\n", SDL_GetError());
    return;
  }
  SDL_SetAudioStreamGetCallback(sStream, on_device_wants, nullptr);
  SDL_SetAudioStreamGain(sStream, sGain);
  if (!sPaused) SDL_ResumeAudioStreamDevice(sStream);
}

struct Reserve {
  double targetMs = kTargetQueuedMs;
  bool adaptive = true, log = false, init = false;
  uint64_t lastArrivalNs = 0;
  uint64_t bucketStartNs = 0;
  double bucketMaxMs = 0, prevBucketMaxMs = 0;
  uint64_t lastUnderrunNs = 0;
  uint32_t starvedSeen = 0;
  std::atomic<uint32_t> targetNow{kTargetQueuedMs}, longestWait{0}, grown{0};
} sReserve;

void reserve_init() {
  Reserve& r = sReserve;
  if (r.init) return;
  r.init = true;
  const char* e = std::getenv("XD_AUDIO_RESERVE");
  if (e != nullptr && e[0] != '\0') {
    r.adaptive = false;
    const int ms = std::atoi(e);
    if (ms > 0) r.targetMs = ms < 10 ? 10 : ms > kMaxReserveMs ? kMaxReserveMs : ms;
    r.targetNow = static_cast<uint32_t>(r.targetMs);
  }
  const char* l = std::getenv("XD_AUDIO_RESERVE_LOG");
  r.log = l != nullptr && l[0] == '1';
}

double reserve_wanted(double waitMs) {
  const double w = waitMs * 0.75 + 20.0;
  return w < kTargetQueuedMs ? kTargetQueuedMs : w > kMaxReserveMs ? kMaxReserveMs : w;
}

int reserve_max_queued_bytes() {
  const double ms = kMaxQueuedMs + (sReserve.targetMs - kTargetQueuedMs) * 2.0;
  return static_cast<int>(kSampleRate * ms / 1000.0) * 4;
}

int reserve_on_block(int queuedBytes, bool counted) {
  Reserve& r = sReserve;
  reserve_init();
  const uint64_t now = SDL_GetTicksNS();
  double waitMs = 0;
  if (r.lastArrivalNs != 0) waitMs = static_cast<double>(now - r.lastArrivalNs) / 1e6;
  r.lastArrivalNs = now;
  if (r.bucketStartNs == 0) r.bucketStartNs = now;
  if (waitMs > r.bucketMaxMs) r.bucketMaxMs = waitMs;
  int padBytes = 0;
  const uint32_t starved = sStarved.load();
  const bool ranDry = starved != r.starvedSeen || queuedBytes == 0;
  r.starvedSeen = starved;
  if (r.adaptive && counted && ranDry) {
    r.lastUnderrunNs = now;
    const double longest = r.bucketMaxMs > r.prevBucketMaxMs ? r.bucketMaxMs : r.prevBucketMaxMs;
    const double wanted = reserve_wanted(longest);
    if (wanted > r.targetMs + 1.0) {
      if (r.log) {
        std::fprintf(stderr,
                     "[platform/audio] sound reserve %.0f -> %.0f ms (the sound card ran dry; blocks came up to "
                     "%.0f ms apart)\n",
                     r.targetMs, wanted, longest);
      }
      r.targetMs = wanted;
      r.grown++;
    }
    const double padMs = r.targetMs - waitMs * 0.5;
    if (r.targetMs > kTargetQueuedMs && padMs > 1.0) padBytes = static_cast<int>(kSampleRate * padMs / 1000.0) * 4;
  }
  if (now - r.bucketStartNs > 10'000'000'000ull) {
    const double longest = r.bucketMaxMs > r.prevBucketMaxMs ? r.bucketMaxMs : r.prevBucketMaxMs;
    r.longestWait = static_cast<uint32_t>(longest);
    if (r.adaptive && r.targetMs > kTargetQueuedMs && now - r.lastUnderrunNs > 30'000'000'000ull) {
      const double wanted = reserve_wanted(longest);
      if (wanted < r.targetMs) {
        const double next = r.targetMs - 10.0 > wanted ? r.targetMs - 10.0 : wanted;
        if (r.log) {
          std::fprintf(stderr, "[platform/audio] sound reserve %.0f -> %.0f ms (blocks on time again)\n", r.targetMs,
                       next);
        }
        r.targetMs = next;
      }
    }
    r.prevBucketMaxMs = r.bucketMaxMs;
    r.bucketMaxMs = 0;
    r.bucketStartNs = now;
  }
  r.targetNow = static_cast<uint32_t>(r.targetMs);
  return padBytes;
}

void steer_rate(int queuedBytes) {
  static double smoothedMs = kTargetQueuedMs;
  const double ms = queuedBytes / 4.0 * 1000.0 / kSampleRate;
  smoothedMs += (ms - smoothedMs) * 0.05;
  double ratio = 1.0 + (smoothedMs - sReserve.targetMs) / kTargetQueuedMs * 0.005;
  ratio = ratio < 0.995 ? 0.995 : ratio > 1.005 ? 1.005 : ratio;
  SDL_SetAudioStreamFrequencyRatio(sStream, static_cast<float>(ratio));
}

std::atomic<int> sSpeedFactor{1};
port_audio::TimeStretch sStretch;
std::vector<int16_t> sStretched;
bool fold_wanted() {
  static const bool fold = [] {
    const char* e = std::getenv("XD_FAST_SOUND");
    return e != nullptr && std::strcmp(e, "fold") == 0;
  }();
  return fold;
}
int sFoldCount = 0;
int32_t sFold[2] = {0, 0};
std::vector<int16_t> sFolded;

size_t fold_block(int n) {
  sFolded.clear();
  for (size_t i = 0; i + 1 < sConvert.size(); i += 2) {
    sFold[0] += sConvert[i];
    sFold[1] += sConvert[i + 1];
    if (++sFoldCount >= n) {
      sFolded.push_back(static_cast<int16_t>(sFold[0] / n));
      sFolded.push_back(static_cast<int16_t>(sFold[1] / n));
      sFold[0] = sFold[1] = 0;
      sFoldCount = 0;
    }
  }
  return sFolded.size() * sizeof(int16_t);
}

void take_block(void*, void*) {
  const u32 addr = sNextAddr, length = sNextLength;
  sBlocks++;
  if (length != 0) {
    const auto* src = reinterpret_cast<const int16_t*>(static_cast<const uint8_t*>(MEM1Start) + addr);
    sConvert.assign(src, src + length / 2);
    for (size_t i = 0; i + 1 < sConvert.size(); i += 2) {
      const int16_t right = sConvert[i];
      sConvert[i] = sConvert[i + 1];
      sConvert[i + 1] = right;
    }
    bool sound = false;
    for (const int16_t v : sConvert) sound |= v != 0;
    sNonSilent += sound ? 1 : 0;
    dump_open();
    dump_block(sConvert.data(), length);
    if (sSpeedFactor.load(std::memory_order_relaxed) <= 1 && sStretch.Active()) {
      sStretched.clear();
      sStretch.Flush(sStretched);
      if (sStream != nullptr && !sStretched.empty()) {
        put_out(sStretched.data(), static_cast<int>(sStretched.size() * sizeof(int16_t)));
      }
      if (sFadeInPos >= sFadeIn) {
        sFadeIn = port_audio::TimeStretch::kHop;
        sFadeInPos = 0;
      }
    }
    shape_block();
    if (sStream != nullptr) {
      int queued = SDL_GetAudioStreamQueued(sStream);
      if (queued == 0 && sBlocks > 20) sUnderruns++;
      const int pad = reserve_on_block(queued, sBlocks > 20);
      if (pad > 0) {
        static std::vector<int16_t> silence;
        silence.assign(static_cast<size_t>(pad) / 2, 0);
        put_out(silence.data(), pad);
        queued += pad;
      }
      const int maxQueued = reserve_max_queued_bytes();
      const int factor = sSpeedFactor.load(std::memory_order_relaxed);
      if (factor > 1 && !fold_wanted()) {
        sStretched.clear();
        sStretch.Push(sConvert.data(), sConvert.size() / 2, factor, sStretched);
        if (!sStretched.empty() && queued < maxQueued) {
          put_out(sStretched.data(), static_cast<int>(sStretched.size() * sizeof(int16_t)));
        }
      } else if (factor > 1) {
        const size_t bytes = fold_block(factor);
        if (bytes > 0 && queued < maxQueued) {
          put_out(sFolded.data(), static_cast<int>(bytes));
        }
      } else if (queued < maxQueued) {
        put_out(sConvert.data(), static_cast<int>(length));
      } else {
        sDropped++;
      }
      steer_rate(queued);
    }
  }
  if (sCallback != nullptr) {
    const uint64_t t0 = SDL_GetTicksNS();
    sCallback();
    static const char* const testCost = std::getenv("XD_TEST_SOUND_COST");
    static const bool testTimes = testCost != nullptr && (testCost[0] == 'x' || testCost[0] == 'X');
    static const double testFactor = testTimes ? std::strtod(testCost + 1, nullptr) : 0.0;
    static const uint64_t testCostNs =
        testCost != nullptr && testCost[0] != '\0' && !testTimes ? static_cast<uint64_t>(std::strtod(testCost, nullptr) * 1e6) : 0ull;
    const uint64_t testUntilNs =
        testFactor > 1.0 ? static_cast<uint64_t>(static_cast<double>(SDL_GetTicksNS() - t0) * testFactor) : testCostNs;
    while (testUntilNs != 0 && SDL_GetTicksNS() - t0 < testUntilNs) {
    }
    sHandlerNs.fetch_add(SDL_GetTicksNS() - t0, std::memory_order_relaxed);
  }
  static const int64_t cost = [] {
    const char* e = std::getenv("XD_AUDIO_IRQ_COST");
    return e != nullptr && e[0] != 0 ? static_cast<int64_t>(std::strtod(e, nullptr) * kTimerClock / 60.0) : 0;
  }();
  if (cost > 0) PortOS_InterruptCost(cost);
}

}

extern "C" {

void PortAudio_SetVolume(float gain) {
  sGain = gain < 0.f ? 0.f : gain > 1.f ? 1.f : gain;
  if (sStream != nullptr) {
    SDL_SetAudioStreamGain(sStream, sGain);
  }
}

void PortAudio_SetPaused(bool paused) {
  sPaused = paused;
  sReserve.lastArrivalNs = 0;
  if (sStream == nullptr) return;
  if (paused) {
    SDL_PauseAudioStreamDevice(sStream);
  } else {
    SDL_ResumeAudioStreamDevice(sStream);
  }
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
  const AIDCallback old = sCallback;
  sCallback = callback;
  return old;
}

void AIInitDMA(u32 start_addr, u32 length) {
  sNextAddr = start_addr;
  sNextLength = length;
}

void AIStartDMA(void) {
  if (sRunning) return;
  open_output();
  sRunning = true;
  const int64_t frames = sNextLength / 4;
  const int64_t period = kTimerClock * frames * kDivider32k / kClock54;
  OSSetPeriodicAlarm(sAlarm, PortTime_Now() + period, period, take_block);
}

void PortAudio_GetDspStats(PortAudioStats* out);

void PortAudio_GetStats(PortAudioStats* out) {
  *out = PortAudioStats{};
  PortAudio_GetDspStats(out);
  out->aiBlocks = sBlocks;
  out->aiBlocksDropped = sDropped;
  out->aiNonSilentBlocks = sNonSilent;
  out->aiUnderruns = sUnderruns;
  out->outShortTimes = sStarved.load();
  out->outShortBytes = sStarvedBytes.load();
  out->aiHandlerNs = sHandlerNs.load();
  out->reserveMs = sReserve.targetNow.load();
  out->reserveGrown = sReserve.grown.load();
  out->longestWaitMs = sReserve.longestWait.load();
}

void PortAudio_SetFastForward(int factor) {
  sSpeedFactor.store(factor < 1 ? 1 : factor > 16 ? 16 : factor, std::memory_order_relaxed);
}

void AIStopDMA(void) {
  if (!sRunning) return;
  OSCancelAlarm(sAlarm);
  sRunning = false;
}

namespace {
struct SavedAi {
  u32 nextAddr, nextLength;
  MusyxClock clock;
} sSavedAi{};
}

bool PortAI_GhostSave(void) {
  if (!sRunning || sCallback == nullptr) return false;
  sSavedAi = {sNextAddr, sNextLength, sMusyxClock};
  return true;
}

void PortAI_GhostRestore(void) {
  sNextAddr = sSavedAi.nextAddr;
  sNextLength = sSavedAi.nextLength;
  sMusyxClock = sSavedAi.clock;
}

uint32_t PortAI_GhostTake(int16_t* out, uint32_t maxBytes) {
  const u32 addr = sNextAddr, length = sNextLength;
  uint32_t got = 0;
  if (length != 0 && length <= maxBytes && addr + static_cast<uint64_t>(length) <= 0x01800000u) {
    const auto* src = reinterpret_cast<const int16_t*>(static_cast<const uint8_t*>(MEM1Start) + addr);
    for (u32 i = 0; i + 1 < length / 2; i += 2) {
      out[i] = src[i + 1];
      out[i + 1] = src[i];
    }
    got = length;
  }
  if (sCallback != nullptr) sCallback();
  return got;
}

int PortAudio_OutQueuedBytes(void) { return sStream != nullptr ? SDL_GetAudioStreamQueued(sStream) : -1; }

int PortAudio_OutRate(void) { return kSampleRate; }

void PortAudio_PutOut(const int16_t* samples, uint32_t bytes) {
  if (sStream != nullptr) {
    put_out(samples, static_cast<int>(bytes));
  } else {
    out_dump(samples, bytes);
  }
}

void PortAudio_SetResumeShape(const int16_t* tail, size_t samples, uint32_t fadeInFrames) {
  sTail.assign(tail, tail + (tail != nullptr ? samples : 0));
  sTailPos = 0;
  sFadeIn = fadeInFrames;
  sFadeInPos = 0;
}

}
