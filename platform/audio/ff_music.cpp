#include <port_menu_music.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern void* MEM1Start;

extern "C" {
void* ARGetStorageAddress(void);
uint32_t ARGetSize(void);
void* PortDsp_GhostClone(bool* pendingInterrupt);
void PortDsp_GhostDestroy(void* chip);
bool PortDsp_GhostEnter(void* chip, bool pendingInterrupt);
void* PortDsp_GhostLeave(bool* pendingInterrupt);
void PortDsp_GhostService(void);
bool PortAI_GhostSave(void);
void PortAI_GhostRestore(void);
uint32_t PortAI_GhostTake(int16_t* out, uint32_t maxBytes);
size_t PortAI_StateSize(void);
void PortAI_StateGet(void* out);
void PortAI_StateSet(const void* in);
void PortAudio_SetResumeShape(const int16_t* tail, size_t samples, uint32_t fadeInFrames);
void PortMusyX_SetFfMix(float music, float effects, float cries);
void PortMusyX_FfGhostWorld(int on);
void PortMusyX_FfFeedRecord(int on);
void PortMusyX_FfFeedServe(int on);
double PortMusyX_FfFeedSeconds(void);
void PortMusyX_FfDebugStreams(void);
void PortMusyX_FfReapply(void);
void PortMusyX_FfSyncMix(void);
uint32_t PortMusyX_FfStreamsPlaying(void);
void PortDVD_WaitWorkerIdle(void);
const PortMenuMusicEngine* PortMenuMusic_Engine(void);
}

namespace {

constexpr size_t kMem1Size = 0x01800000;
constexpr uint32_t kBlockBytes = 0x280;
constexpr int kRate = 32000;
constexpr int kSliceBlocks = 100;
constexpr size_t kRefillBelow = kRate / 4;
constexpr double kEffectsFadeSeconds = 0.1;
constexpr double kStreamFadeSeconds = 0.5;
constexpr double kRunwayMargin = 0.1;
constexpr double kSongChangeFadeSeconds = 0.1;

struct Extra {
  uint8_t* address;
  std::vector<uint8_t> bytes;
};

struct World {
  std::vector<uint8_t> block, mem1, aram;
  std::vector<Extra> extras;
  void* dsp = nullptr;
  bool dspIrq = false;
  std::vector<uint8_t> ai;
  void* blockAddress = nullptr;
};

World sGhost;
World sReal;
bool sFresh = false;
bool sOn = false;
bool sMuted = false;
int sMusicMode = 0;
int sEffectsMode = 0;
int sFadeMs = 400;
uint32_t (*sSignature)(void) = nullptr;
uint32_t sLastSignature = 0;
bool sCheck = false;

std::mutex sLock;
std::deque<int16_t> sMusic;
size_t sFadeKeep = 0;
size_t sFadeOverlap = 0;
size_t sOverlapDone = 0;

uint32_t sBlocks = 0;
double sRunway = 1e9;
int sQuietBlocks = 0;

uint32_t sSlices = 0, sForks = 0;
double sSwapMs = 0, sRenderMs = 0, sWorstSliceMs = 0;

uint32_t Signature() {
  uint32_t h = sSignature != nullptr ? sSignature() : 0;
  return (h * 16777619u) ^ PortMusyX_FfStreamsPlaying();
}

bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] == '1';
}

const char* EnvText(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' ? v : nullptr;
}

void CopyIn(std::vector<uint8_t>& dst, const void* src, size_t size) {
  const auto* p = static_cast<const uint8_t*>(src);
  dst.assign(p, p + size);
}

bool TakeWorld(World& w, const PortMenuMusicEngine* e) {
  void* blockAt = nullptr;
  size_t blockSize = 0;
  e->stateBlock(&blockAt, &blockSize);
  std::vector<uint8_t> keep;
  CopyIn(keep, blockAt, blockSize);
  bool pending = false;
  void* chip = PortDsp_GhostClone(&pending);
  if (chip == nullptr) return false;
  e->isolate();
  CopyIn(w.block, blockAt, blockSize);
  std::memcpy(blockAt, keep.data(), blockSize);
  w.blockAddress = blockAt;
  CopyIn(w.mem1, MEM1Start, kMem1Size);
  if (auto* aram = ARGetStorageAddress()) CopyIn(w.aram, aram, ARGetSize());
  PortMenuMusicRange ranges[16];
  const size_t n = e->extraRanges != nullptr ? e->extraRanges(ranges, 16) : 0;
  w.extras.clear();
  for (size_t i = 0; i < n; i++) {
    auto* p = static_cast<uint8_t*>(ranges[i].address);
    w.extras.push_back({p, std::vector<uint8_t>(p, p + ranges[i].size)});
  }
  if (w.dsp != nullptr) PortDsp_GhostDestroy(w.dsp);
  w.dsp = chip;
  w.dspIrq = pending;
  w.ai.resize(PortAI_StateSize());
  PortAI_StateGet(w.ai.data());
  return true;
}

void FreeWorld(World& w) {
  if (w.dsp != nullptr) PortDsp_GhostDestroy(w.dsp);
  w = World{};
}

std::mutex sRunLock;
std::condition_variable sRunCv;
bool sRunGo = false, sRunDone = false;
int sRunBlocks = 0;
std::thread sWorker;
bool sWorkerQuit = false;
std::vector<int16_t> sRendered;

bool RenderBlock(const PortMenuMusicEngine* e) {
  int16_t block[kBlockBytes / 2 * 4] = {};
  const double now = (sBlocks + 1) * 0.005;
  if (e->setGains != nullptr) {
    const auto fall = [](double t) {
      return t <= 0.0 ? 1.f : t >= 1.0 ? 0.f : static_cast<float>(0.5 + 0.5 * std::cos(t * 3.141592653589793));
    };
    const float g = fall(now / kEffectsFadeSeconds);
    const double feed = PortMusyX_FfFeedSeconds();
    const float g3 = feed >= 0.3 ? 1.f : feed <= 0.05 ? 0.f : static_cast<float>((feed - 0.05) / 0.25);
    const float gains[4] = {1.f, g, g, g3};
    e->setGains(gains);
  }
  const uint32_t bytes = PortAI_GhostTake(block, sizeof(block));
  PortDsp_GhostService();
  if (bytes == 0) return false;
  sBlocks++;
  sRendered.insert(sRendered.end(), block, block + bytes / 2);
  return true;
}

void WorkerMain() {
  const PortMenuMusicEngine* e = PortMenuMusic_Engine();
  std::unique_lock<std::mutex> lock(sRunLock);
  for (;;) {
    sRunCv.wait(lock, [] { return sRunGo || sWorkerQuit; });
    if (sWorkerQuit) return;
    sRunGo = false;
    lock.unlock();
    for (int i = 0; i < sRunBlocks; i++) {
      if (!RenderBlock(e)) break;
    }
    lock.lock();
    sRunDone = true;
    sRunCv.notify_all();
  }
}

void StartWorker() {
  if (sWorker.joinable()) return;
  sWorkerQuit = false;
  sWorker = std::thread(WorkerMain);
}

void StopWorker() {
  if (!sWorker.joinable()) return;
  {
    std::lock_guard<std::mutex> g(sRunLock);
    sWorkerQuit = true;
  }
  sRunCv.notify_all();
  sWorker.join();
}

void CopyOut(std::vector<uint8_t>& dst, const void* src, size_t size) {
  if (dst.size() != size) dst.resize(size);
  std::memcpy(dst.data(), src, size);
}

bool RunSlice(int blocks) {
  const PortMenuMusicEngine* e = PortMenuMusic_Engine();
  if (e == nullptr || sGhost.dsp == nullptr) return false;
  const auto t0 = std::chrono::steady_clock::now();
  PortDVD_WaitWorkerIdle();
  void* blockAt = nullptr;
  size_t blockSize = 0;
  e->stateBlock(&blockAt, &blockSize);
  if (blockSize != sGhost.block.size() || blockAt != sGhost.blockAddress) return false;
  if (!PortAI_GhostSave()) return false;
  CopyOut(sReal.block, blockAt, blockSize);
  CopyOut(sReal.mem1, MEM1Start, kMem1Size);
  void* aramAt = ARGetStorageAddress();
  const size_t aramSize = aramAt != nullptr ? ARGetSize() : 0;
  if (aramAt != nullptr) CopyOut(sReal.aram, aramAt, aramSize);
  std::vector<Extra> realExtras;
  {
    PortMenuMusicRange ranges[16];
    const size_t n = e->extraRanges != nullptr ? e->extraRanges(ranges, 16) : 0;
    const auto has = [&](const uint8_t* p) {
      for (const Extra& x : realExtras) if (x.address == p) return true;
      return false;
    };
    for (size_t i = 0; i < n; i++) {
      auto* p = static_cast<uint8_t*>(ranges[i].address);
      if (!has(p)) realExtras.push_back({p, std::vector<uint8_t>(p, p + ranges[i].size)});
    }
    for (const Extra& g : sGhost.extras) {
      if (!has(g.address)) realExtras.push_back({g.address, std::vector<uint8_t>(g.address, g.address + g.bytes.size())});
    }
  }
  std::vector<uint8_t> realAi(PortAI_StateSize());
  PortAI_StateGet(realAi.data());
  if (!PortDsp_GhostEnter(sGhost.dsp, sGhost.dspIrq)) {
    PortAI_GhostRestore();
    return false;
  }
  std::memcpy(blockAt, sGhost.block.data(), blockSize);
  std::memcpy(MEM1Start, sGhost.mem1.data(), kMem1Size);
  if (aramAt != nullptr && sGhost.aram.size() == aramSize) std::memcpy(aramAt, sGhost.aram.data(), aramSize);
  for (const Extra& x : sGhost.extras) std::memcpy(x.address, x.bytes.data(), x.bytes.size());
  PortAI_StateSet(sGhost.ai.data());
  PortMusyX_FfGhostWorld(1);
  PortMusyX_FfFeedServe(1);
  if (sFresh) PortMusyX_FfReapply();
  PortMusyX_FfSyncMix();
  sFresh = false;
  const auto t1 = std::chrono::steady_clock::now();
  sRendered.clear();
  {
    std::unique_lock<std::mutex> lock(sRunLock);
    sRunBlocks = blocks;
    sRunDone = false;
    sRunGo = true;
    sRunCv.notify_all();
    sRunCv.wait(lock, [] { return sRunDone; });
  }
  const auto t2 = std::chrono::steady_clock::now();
  static const bool dbg = EnvOn("XD_FF_MUSIC_DEBUG");
  if (dbg && (sSlices % 8 == 5)) PortMusyX_FfDebugStreams();
  if (dbg) {
    double sum = 0;
    for (int16_t v : sRendered) sum += static_cast<double>(v) * v;
    std::fprintf(stderr, "[ff debug] slice %u: feed %.2f s, rendered rms %.0f, store %zu frames\n", sSlices, PortMusyX_FfFeedSeconds(),
                 sRendered.empty() ? 0.0 : std::sqrt(sum / sRendered.size()), sMusic.size() / 2);
  }
  if (e->setGains != nullptr) {
    const float one[4] = {1.f, 1.f, 1.f, 1.f};
    e->setGains(one);
  }
  PortAI_StateGet(sGhost.ai.data());
  sGhost.dsp = PortDsp_GhostLeave(&sGhost.dspIrq);
  CopyOut(sGhost.block, blockAt, blockSize);
  CopyOut(sGhost.mem1, MEM1Start, kMem1Size);
  if (aramAt != nullptr) CopyOut(sGhost.aram, aramAt, aramSize);
  for (Extra& x : sGhost.extras) std::memcpy(x.bytes.data(), x.address, x.bytes.size());
  std::memcpy(blockAt, sReal.block.data(), blockSize);
  std::memcpy(MEM1Start, sReal.mem1.data(), kMem1Size);
  if (aramAt != nullptr) std::memcpy(aramAt, sReal.aram.data(), aramSize);
  for (const Extra& x : realExtras) std::memcpy(x.address, x.bytes.data(), x.bytes.size());
  PortAI_StateSet(realAi.data());
  PortAI_GhostRestore();
  PortMusyX_FfGhostWorld(0);
  PortMusyX_FfFeedServe(0);
  if (sCheck) {
    const bool same = std::memcmp(blockAt, sReal.block.data(), blockSize) == 0 &&
                      std::memcmp(MEM1Start, sReal.mem1.data(), kMem1Size) == 0 &&
                      (aramAt == nullptr || std::memcmp(aramAt, sReal.aram.data(), aramSize) == 0);
    if (!same) std::fprintf(stderr, "[ff music] check: the real sound state is NOT as it was after a slice\n");
    static uint32_t checked = 0;
    if (++checked % 20 == 1) std::fprintf(stderr, "[ff music] check: slice %u, the real sound state is %s\n", checked, same ? "as it was" : "NOT as it was");
  }
  const auto t3 = std::chrono::steady_clock::now();
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  sSwapMs += ms(t0, t1) + ms(t2, t3);
  sRenderMs += ms(t1, t2);
  sWorstSliceMs = std::max(sWorstSliceMs, ms(t0, t3));
  sSlices++;
  {
    std::lock_guard<std::mutex> g(sLock);
    const size_t frames = sRendered.size() / 2;
    for (size_t f = 0; f < frames; f++) {
      int16_t l = sRendered[f * 2], r = sRendered[f * 2 + 1];
      if (sOverlapDone < sFadeOverlap) {
        const double t = static_cast<double>(sOverlapDone) / static_cast<double>(sFadeOverlap);
        const double in = std::sin(t * 1.5707963267948966);
        l = static_cast<int16_t>(l * in);
        r = static_cast<int16_t>(r * in);
        const size_t at = (sMusic.size() / 2 - (sFadeKeep > 0 ? 0 : 0));
        (void)at;
        const size_t slot = (sOverlapDone) * 2;
        if (slot + 1 < sMusic.size()) {
          const int sumL = sMusic[slot] + l, sumR = sMusic[slot + 1] + r;
          sMusic[slot] = static_cast<int16_t>(std::clamp(sumL, -32768, 32767));
          sMusic[slot + 1] = static_cast<int16_t>(std::clamp(sumR, -32768, 32767));
          sOverlapDone++;
          continue;
        }
        sOverlapDone++;
      }
      sMusic.push_back(l);
      sMusic.push_back(r);
    }
  }
  return true;
}

bool Fork() {
  const PortMenuMusicEngine* e = PortMenuMusic_Engine();
  if (e == nullptr) return false;
  char why[160] = {};
  if (e->canRun == nullptr || !e->canRun(why, sizeof(why))) {
    static std::string last;
    if (last != why) std::fprintf(stderr, "[ff music] no copy of the sound engine yet: %s\n", why);
    last = why;
    return false;
  }
  PortDVD_WaitWorkerIdle();
  if (!PortAI_GhostSave()) return false;
  PortAI_GhostRestore();
  sRunway = e->streamRunway != nullptr ? e->streamRunway() : 1e9;
  if (EnvOn("XD_FF_MUSIC_DEBUG")) std::fprintf(stderr, "[ff debug] MEM1Start %p\n", MEM1Start);
  PortMusyX_FfFeedRecord(1);
  if (!TakeWorld(sGhost, e)) return false;
  sBlocks = 0;
  sQuietBlocks = 0;
  sFresh = true;
  sForks++;
  return true;
}

void DropStore() {
  std::lock_guard<std::mutex> g(sLock);
  sMusic.clear();
  sFadeKeep = sFadeOverlap = sOverlapDone = 0;
}

void StartSongChange() {
  std::lock_guard<std::mutex> g(sLock);
  const size_t keep = std::min<size_t>(sMusic.size() / 2, static_cast<size_t>(kSongChangeFadeSeconds * kRate));
  sMusic.resize(keep * 2);
  for (size_t f = 0; f < keep; f++) {
    const double t = static_cast<double>(f) / static_cast<double>(keep);
    const double out = std::cos(t * 1.5707963267948966);
    sMusic[f * 2] = static_cast<int16_t>(sMusic[f * 2] * out);
    sMusic[f * 2 + 1] = static_cast<int16_t>(sMusic[f * 2 + 1] * out);
  }
  sFadeKeep = keep;
  sFadeOverlap = keep;
  sOverlapDone = 0;
}

void Begin() {
  const PortMenuMusicEngine* e = PortMenuMusic_Engine();
  if (e == nullptr || MEM1Start == nullptr) return;
  sLastSignature = Signature();
  if (!Fork()) return;
  StartWorker();
  DropStore();
  sOn = true;
  RunSlice(kSliceBlocks);
  if (!EnvOn("XD_FF_MUSIC_NOMUTE")) PortMusyX_SetFfMix(0.f, 1.f, 1.f);
  sMuted = true;
  std::fprintf(stderr, "[ff music] on: the copy of the sound engine plays the music at real time (effects: %s, fade %d ms)\n",
               sEffectsMode == 1 ? "muted" : sEffectsMode == 2 ? "higher pitch" : "sped up", sFadeMs);
}

void End() {
  if (!sOn) return;
  const PortMenuMusicEngine* e = PortMenuMusic_Engine();
  const size_t want = static_cast<size_t>(sFadeMs) * kRate / 1000;
  {
    std::unique_lock<std::mutex> lk(sLock);
    const bool short_ = sMusic.size() / 2 < want;
    lk.unlock();
    if (short_) RunSlice(kSliceBlocks);
  }
  std::vector<int16_t> tail;
  {
    std::lock_guard<std::mutex> g(sLock);
    const size_t n = std::min(want * 2, sMusic.size());
    tail.assign(sMusic.begin(), sMusic.begin() + static_cast<std::ptrdiff_t>(n));
    sMusic.clear();
  }
  PortMusyX_SetFfMix(1.f, 1.f, 1.f);
  PortMusyX_FfFeedRecord(0);
  sMuted = false;
  PortAudio_SetResumeShape(tail.empty() ? nullptr : tail.data(), tail.size(), 0);
  sOn = false;
  FreeWorld(sGhost);
  sReal = World{};
  (void)e;
  std::fprintf(stderr,
               "[ff music] off: %u slices (%.1f ms each: %.1f ms of swapping + %.1f ms of sound; worst %.1f ms), %u "
               "copies taken, the music crossfaded back over %d ms\n",
               sSlices, sSlices > 0 ? (sSwapMs + sRenderMs) / sSlices : 0.0, sSlices > 0 ? sSwapMs / sSlices : 0.0,
               sSlices > 0 ? sRenderMs / sSlices : 0.0, sWorstSliceMs, sForks, sFadeMs);
  sSlices = sForks = 0;
  sSwapMs = sRenderMs = sWorstSliceMs = 0;
}

}

extern "C" {

void PortFfMusic_SetSignature(uint32_t (*fn)(void)) { sSignature = fn; }

void PortFfMusic_Abort(void) {
  if (!sOn) return;
  DropStore();
  PortMusyX_SetFfMix(1.f, 1.f, 1.f);
  PortMusyX_FfFeedRecord(0);
  sMuted = false;
  sOn = false;
  FreeWorld(sGhost);
}

void PortFfMusic_Frame(int factor, int musicMode, int effectsMode, int fadeMs) {
  static bool once = false;
  if (!once) {
    once = true;
    sCheck = EnvOn("XD_FF_MUSIC_CHECK");
  }
  sMusicMode = musicMode;
  sEffectsMode = effectsMode;
  sFadeMs = fadeMs;
  if (const char* m = EnvText("XD_FF_MUSIC")) sMusicMode = std::strcmp(m, "realtime") == 0 ? 1 : 0;
  if (const char* m = EnvText("XD_FF_EFFECTS")) sEffectsMode = std::strcmp(m, "muted") == 0 ? 1 : std::strcmp(m, "tape") == 0 ? 2 : 0;
  if (const char* m = EnvText("XD_FF_FADE_MS")) sFadeMs = std::max(20, std::atoi(m));
  const bool want = factor > 1 && sMusicMode == 1;
  static int wait = 0;
  if (want && !sOn) {
    if (wait > 0) {
      wait--;
      return;
    }
    Begin();
    if (!sOn) wait = 60;
  } else if (!want && sOn) {
    End();
  }
  if (!sOn) return;
  {
    const uint32_t sig = Signature();
    if (sig != sLastSignature) {
      sLastSignature = sig;
      StartSongChange();
      if (Fork()) {
        sMuted = sMuted;
        RunSlice(kSliceBlocks);
        std::fprintf(stderr, "[ff music] the game's music changed: a new copy plays it from the start\n");
      }
      return;
    }
  }
  bool low;
  {
    std::lock_guard<std::mutex> g(sLock);
    low = sMusic.size() / 2 < kRefillBelow;
  }
  if (low) RunSlice(kSliceBlocks);
}

void PortFfMusic_Mix(int16_t* samples, size_t frames) {
  if (!sOn) return;
  if (sEffectsMode == 1) std::memset(samples, 0, frames * 4);
  std::lock_guard<std::mutex> g(sLock);
  const size_t have = std::min(frames, sMusic.size() / 2);
  for (size_t f = 0; f < have; f++) {
    for (int c = 0; c < 2; c++) {
      const int v = samples[f * 2 + static_cast<size_t>(c)] + sMusic[static_cast<size_t>(c)];
      sMusic.pop_front();
      samples[f * 2 + static_cast<size_t>(c)] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
    }
  }
  if (sOverlapDone > 0 || sFadeKeep > 0) {
    const size_t used = std::min(sFadeKeep, have);
    sFadeKeep -= used;
    if (sFadeKeep == 0 && sOverlapDone >= sFadeOverlap) sFadeOverlap = sOverlapDone = 0;
    else sOverlapDone -= std::min(sOverlapDone, used);
    sFadeOverlap -= std::min(sFadeOverlap, used);
  }
}

bool PortFfMusic_TapeEffects(void) { return sOn && sEffectsMode == 2; }

}
