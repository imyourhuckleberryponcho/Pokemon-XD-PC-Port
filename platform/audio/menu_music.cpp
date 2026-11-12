#include <port_audio.h>
#include <port_menu_music.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

extern void* MEM1Start;

extern "C" {
void* ARGetStorageAddress(void);
uint32_t ARGetSize(void);
bool PortDsp_GhostBegin(void);
void PortDsp_GhostService(void);
void PortDsp_GhostEnd(void);
uint64_t PortDsp_Instructions(void);
bool PortAI_GhostSave(void);
void PortAI_GhostRestore(void);
uint32_t PortAI_GhostTake(int16_t* out, uint32_t maxBytes);
int PortAudio_OutQueuedBytes(void);
int PortAudio_OutRate(void);
void PortAudio_PutOut(const int16_t* samples, uint32_t bytes);
void PortAudio_SetResumeShape(const int16_t* tail, size_t samples, uint32_t fadeInFrames);
}

namespace {

constexpr size_t kMem1Size = 0x01800000;
constexpr uint32_t kBlockBytes = 0x280;
constexpr double kEffectsFadeSeconds = 0.1;
constexpr double kFadeSeconds = 0.4;
constexpr double kCrossfadeSeconds = 0.2;
constexpr int kQueuedMs = 50;
constexpr int kAheadMs = 250;

const PortMenuMusicEngine* sEngine = nullptr;
int sNext = PORT_MENU_MUSIC_STOP;

enum class State { Off, Paused, Copy };
State sState = State::Off;
int sMode = PORT_MENU_MUSIC_STOP;

struct Saved {
  void* block = nullptr;
  std::vector<uint8_t> blockBytes, mem1, aram;
  struct Extra {
    uint8_t* address;
    std::vector<uint8_t> bytes;
  };
  std::vector<Extra> extras;
} sSaved;

std::thread sThread;
std::atomic<bool> sStop{false};
std::atomic<bool> sFinish{false};
std::deque<int16_t> sAhead;
uint32_t sBlocks = 0;
double sRenderSeconds = 0;
uint64_t sDspInstructions = 0;
std::chrono::steady_clock::time_point sStart;
bool sFadeDone = false;
uint32_t sRefusedBefore = 0;
double sRunway = 1e9;
int sQuietBlocks = 0;
constexpr double kStreamFadeSeconds = 0.5;
constexpr double kRunwayMargin = 0.1;

bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] == '1';
}

bool RenderBlock() {
  int16_t block[kBlockBytes / 2 * 4] = {};
  const auto t0 = std::chrono::steady_clock::now();
  const double now = (sBlocks + 1) * 0.005;
  if (sMode == PORT_MENU_MUSIC_KEEP && sEngine->setGains != nullptr) {
    const auto fall = [](double t) {
      return t <= 0.0 ? 1.f : t >= 1.0 ? 0.f : static_cast<float>(0.5 + 0.5 * std::cos(t * 3.141592653589793));
    };
    const float g = fall(now / kEffectsFadeSeconds);
    const double streamFadeAt = std::max(0.0, sRunway - kRunwayMargin - kStreamFadeSeconds);
    const float gains[4] = {1.f, g, g, fall((now - streamFadeAt) / kStreamFadeSeconds)};
    sEngine->setGains(gains);
  }
  const uint64_t i0 = PortDsp_Instructions();
  const uint32_t bytes = PortAI_GhostTake(block, sizeof(block));
  PortDsp_GhostService();
  sDspInstructions += PortDsp_Instructions() - i0;
  sRenderSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (bytes == 0) return false;
  const size_t n = bytes / 2;
  const int rate = PortAudio_OutRate();
  if (sMode == PORT_MENU_MUSIC_FADE) {
    const size_t frames = n / 2;
    const double total = kFadeSeconds * rate;
    for (size_t f = 0; f < frames; f++) {
      const double t = (static_cast<double>(sBlocks) * frames + f) / total;
      const double g = t >= 1.0 ? 0.0 : 0.5 + 0.5 * std::cos(t * 3.141592653589793);
      block[f * 2] = static_cast<int16_t>(block[f * 2] * g);
      block[f * 2 + 1] = static_cast<int16_t>(block[f * 2 + 1] * g);
    }
    if ((static_cast<double>(sBlocks) + 1) * frames >= total) sFadeDone = true;
  } else if (now > kEffectsFadeSeconds) {
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += static_cast<double>(block[i]) * block[i];
    sQuietBlocks = n > 0 && std::sqrt(sum / n) < 164.0 ? sQuietBlocks + 1 : 0;
    if (sQuietBlocks >= 100 && now > sRunway) {
      sFadeDone = true;
      for (size_t f = 0; f < n / 2; f++) {
        const double g = 1.0 - static_cast<double>(f + 1) / (n / 2);
        block[f * 2] = static_cast<int16_t>(block[f * 2] * g);
        block[f * 2 + 1] = static_cast<int16_t>(block[f * 2 + 1] * g);
      }
    }
  }
  sAhead.insert(sAhead.end(), block, block + n);
  sBlocks++;
  return true;
}

void CopyThread() {
  const int rate = PortAudio_OutRate();
  const size_t aheadSamples = static_cast<size_t>(rate) * 2 * kAheadMs / 1000;
  const size_t tailSamples = static_cast<size_t>(kCrossfadeSeconds * rate) * 2;
  const int queuedTarget = rate * 4 * kQueuedMs / 1000;
  uint64_t given = 0;
  bool dead = false;
  while (!sStop.load()) {
    bool busy = false;
    if (sFinish.load()) {
      if (sMode != PORT_MENU_MUSIC_KEEP || dead || sFadeDone || sAhead.size() >= tailSamples) break;
      if (!RenderBlock()) dead = true;
      continue;
    }
    if (!dead && !sFadeDone && sAhead.size() < aheadSamples) {
      if (!RenderBlock()) dead = true;
      busy = true;
    }
    int queued = PortAudio_OutQueuedBytes();
    if (queued < 0) {
      const double played = std::chrono::duration<double>(std::chrono::steady_clock::now() - sStart).count() * rate * 4;
      queued = static_cast<int>(std::max(0.0, static_cast<double>(given) - played));
    }
    while (queued < queuedTarget && !sAhead.empty() &&
           (sMode != PORT_MENU_MUSIC_KEEP || sAhead.size() > tailSamples || !sFinish.load())) {
      const size_t n = std::min<size_t>(sAhead.size(), kBlockBytes / 2);
      int16_t out[kBlockBytes / 2];
      std::copy(sAhead.begin(), sAhead.begin() + static_cast<std::ptrdiff_t>(n), out);
      sAhead.erase(sAhead.begin(), sAhead.begin() + static_cast<std::ptrdiff_t>(n));
      PortAudio_PutOut(out, static_cast<uint32_t>(n * 2));
      given += n * 2;
      queued += static_cast<int>(n * 2);
      busy = true;
    }
    if (!busy) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

size_t RestoreChanged(uint8_t* live, const uint8_t* saved, size_t size) {
  constexpr size_t kPiece = 4096;
  size_t changed = 0;
  for (size_t at = 0; at < size; at += kPiece) {
    const size_t n = std::min(kPiece, size - at);
    if (std::memcmp(live + at, saved + at, n) != 0) {
      std::memcpy(live + at, saved + at, n);
      changed += n;
    }
  }
  return changed;
}

void Pause(const char* why) {
  if (why != nullptr) std::fprintf(stderr, "[menu music] %s: the sound waits (stop)\n", why);
  PortAudio_SetPaused(true);
  sState = State::Paused;
}

const char* ModeName(int mode) {
  return mode == PORT_MENU_MUSIC_KEEP ? "keep playing" : mode == PORT_MENU_MUSIC_FADE ? "fade" : "stop";
}

}

extern "C" {

void PortMenuMusic_SetEngine(const PortMenuMusicEngine* engine) { sEngine = engine; }

void PortMenuMusic_SetNext(int mode) { sNext = mode; }

int PortMenuMusic_TakeNext(void) {
  const int m = sNext;
  sNext = PORT_MENU_MUSIC_STOP;
  return m;
}

int PortMenuMusic_ModeFromName(const char* name, int fallback) {
  if (name == nullptr) return fallback;
  if (std::strcmp(name, "keep") == 0) return PORT_MENU_MUSIC_KEEP;
  if (std::strcmp(name, "fade") == 0) return PORT_MENU_MUSIC_FADE;
  if (std::strcmp(name, "stop") == 0) return PORT_MENU_MUSIC_STOP;
  return fallback;
}

void PortMenuMusic_Begin(int mode) {
  if (sState != State::Off) return;
  if (const char* force = std::getenv("XD_MENU_MUSIC")) mode = PortMenuMusic_ModeFromName(force, mode);
  sMode = mode;
  if (mode == PORT_MENU_MUSIC_STOP) {
    Pause(nullptr);
    return;
  }
  if (sEngine == nullptr || MEM1Start == nullptr) {
    Pause("no sound engine to copy");
    return;
  }
  char why[160] = {};
  if (!sEngine->canRun(why, sizeof(why))) {
    Pause(why);
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  if (!PortAI_GhostSave()) {
    Pause("the audio interface isn't running");
    return;
  }
  size_t blockSize = 0;
  sEngine->stateBlock(&sSaved.block, &blockSize);
  const auto* block = static_cast<const uint8_t*>(sSaved.block);
  sSaved.blockBytes.assign(block, block + blockSize);
  const auto* mem1 = static_cast<const uint8_t*>(MEM1Start);
  sSaved.mem1.assign(mem1, mem1 + kMem1Size);
  if (auto* aram = static_cast<const uint8_t*>(ARGetStorageAddress())) {
    sSaved.aram.assign(aram, aram + ARGetSize());
  }
  PortMenuMusicRange ranges[16];
  const size_t nRanges = sEngine->extraRanges != nullptr ? sEngine->extraRanges(ranges, 16) : 0;
  sSaved.extras.clear();
  for (size_t i = 0; i < nRanges; i++) {
    auto* p = static_cast<uint8_t*>(ranges[i].address);
    sSaved.extras.push_back({p, std::vector<uint8_t>(p, p + ranges[i].size)});
  }
  if (!PortDsp_GhostBegin()) {
    PortAI_GhostRestore();
    sSaved = Saved{};
    Pause("the audio chip has no task");
    return;
  }
  sRunway = sEngine->streamRunway != nullptr ? sEngine->streamRunway() : 1e9;
  sQuietBlocks = 0;
  sEngine->isolate();
  sRefusedBefore = sEngine->refusedCalls != nullptr ? sEngine->refusedCalls() : 0;
  sState = State::Copy;
  sStop = false;
  sFinish = false;
  sAhead.clear();
  sBlocks = 0;
  sRenderSeconds = 0;
  sDspInstructions = 0;
  sFadeDone = false;
  sStart = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(sStart - t0).count();
  char runway[64] = "no music stream";
  if (sRunway < 1e8) std::snprintf(runway, sizeof(runway), "music streams loaded for %.2f s", sRunway);
  std::fprintf(stderr,
               "[menu music] %s: the sound engine's state set aside (%.1f MB, %.1f ms); its copy plays on (%s)\n",
               ModeName(mode),
               (sSaved.blockBytes.size() + sSaved.mem1.size() + sSaved.aram.size()) / (1024.0 * 1024.0), ms, runway);
  sThread = std::thread(CopyThread);
}

void PortMenuMusic_End(void) {
  if (sState == State::Paused) {
    PortAudio_SetPaused(false);
    sState = State::Off;
    return;
  }
  if (sState != State::Copy) return;
  const auto t0 = std::chrono::steady_clock::now();
  sFinish = true;
  if (sMode != PORT_MENU_MUSIC_KEEP) sStop = true;
  if (sThread.joinable()) sThread.join();
  const double wall = std::chrono::duration<double>(t0 - sStart).count();
  const int rate = PortAudio_OutRate();
  const bool quiet = sFadeDone;
  const size_t tailSamples = quiet                             ? 0
                             : sMode == PORT_MENU_MUSIC_KEEP ? static_cast<size_t>(kCrossfadeSeconds * rate) * 2
                                                             : static_cast<size_t>(0.05 * rate) * 2;
  std::vector<int16_t> tail(sAhead.begin(),
                            sAhead.begin() + static_cast<std::ptrdiff_t>(std::min(tailSamples, sAhead.size())));
  if (sMode == PORT_MENU_MUSIC_FADE) {
    for (size_t i = 0; i < tail.size(); i++) {
      tail[i] = static_cast<int16_t>(tail[i] * (1.0 - static_cast<double>(i / 2) / (tail.size() / 2)));
    }
  }
  sAhead.clear();
  const uint32_t refused = (sEngine->refusedCalls != nullptr ? sEngine->refusedCalls() : 0) - sRefusedBefore;
  if (sEngine->setGains != nullptr) {
    const float one[4] = {1.f, 1.f, 1.f, 1.f};
    sEngine->setGains(one);
  }
  PortDsp_GhostEnd();
  PortAI_GhostRestore();
  const size_t blockChanged =
      RestoreChanged(static_cast<uint8_t*>(sSaved.block), sSaved.blockBytes.data(), sSaved.blockBytes.size());
  const size_t mem1Changed = RestoreChanged(static_cast<uint8_t*>(MEM1Start), sSaved.mem1.data(), sSaved.mem1.size());
  size_t aramChanged = 0;
  if (!sSaved.aram.empty()) {
    aramChanged = RestoreChanged(static_cast<uint8_t*>(ARGetStorageAddress()), sSaved.aram.data(), sSaved.aram.size());
  }
  size_t extraChanged = 0;
  for (Saved::Extra& e : sSaved.extras) extraChanged += RestoreChanged(e.address, e.bytes.data(), e.bytes.size());
  if (EnvOn("XD_MENU_MUSIC_CHECK")) {
    const bool same = std::memcmp(sSaved.block, sSaved.blockBytes.data(), sSaved.blockBytes.size()) == 0 &&
                      std::memcmp(MEM1Start, sSaved.mem1.data(), sSaved.mem1.size()) == 0 &&
                      (sSaved.aram.empty() ||
                       std::memcmp(ARGetStorageAddress(), sSaved.aram.data(), sSaved.aram.size()) == 0);
    std::fprintf(stderr, "[menu music] check: the real sound state is %s\n", same ? "as it was" : "NOT as it was");
  }
  sSaved = Saved{};
  PortAudio_SetResumeShape(tail.empty() ? nullptr : tail.data(), tail.size(),
                           sMode == PORT_MENU_MUSIC_FADE || quiet ? static_cast<uint32_t>(kFadeSeconds * rate) : 0);
  const double endMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::fprintf(stderr,
               "[menu music] %s ended after %.1f s: the copy made %u blocks (%.2f ms of CPU each, %.1f%% of one core; "
               "%.1f M DSP instructions/s), %u calls into the game refused; put back %zu + %zu + %zu + %zu bytes "
               "(engine, main memory, ARAM, effects) in %.1f ms\n",
               ModeName(sMode), wall, sBlocks, sBlocks > 0 ? sRenderSeconds * 1000.0 / sBlocks : 0.0,
               wall > 0 ? sRenderSeconds * 100.0 / wall : 0.0, wall > 0 ? sDspInstructions / wall / 1e6 : 0.0,
               refused, blockChanged, mem1Changed, aramChanged, extraChanged, endMs);
  sState = State::Off;
}

}
