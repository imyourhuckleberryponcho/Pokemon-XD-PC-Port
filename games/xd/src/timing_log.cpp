#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
const void* PortXD_FindVariable(const char* path);
void PortVI_SetFrameWorkHook(void (*hook)(void));
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
int64_t PortTime_Now(void);
}

namespace {

FILE* sOut = nullptr;
uint32_t sLines = 0;
struct Vars {
  const uint32_t* counter;
  const int32_t *vsync, *drop, *flip, *lockRate;
  const float *render, *real;
  const int64_t* last;
  const uint8_t *lockCount, *lockActive;
  const int32_t* pause;
  const uint64_t* frameUs;
} sV;

template <typename T> T get(const T* p) { return p != nullptr ? *p : T{}; }

void on_work() {
  uint32_t realBits = 0;
  const float real = get(sV.real);
  std::memcpy(&realBits, &real, 4);
  std::fprintf(sOut,
               "it %u flip %u vflip %d field %u vsync %d drop %d rate %d cnt %u act %u pause %d render %.9g real "
               "%.9g realx %08x last %" PRId64 " us %" PRIu64 " now %" PRId64 "\n",
               get(sV.counter), PortVI_GetFlipCount(), get(sV.flip), VIGetRetraceCount(), get(sV.vsync),
               get(sV.drop), get(sV.lockRate), static_cast<unsigned>(get(sV.lockCount)),
               static_cast<unsigned>(get(sV.lockActive)), get(sV.pause), static_cast<double>(get(sV.render)),
               static_cast<double>(real), realBits, get(sV.last), get(sV.frameUs), PortTime_Now());
  if (++sLines % 256 == 0) {
    std::fflush(sOut);
  }
}

template <typename T> const T* find(const char* path) {
  const void* p = PortXD_FindVariable(path);
  if (p == nullptr) {
    std::fprintf(stderr, "[timing log] %s not found in xd.pdb (logged as 0)\n", path);
  }
  return static_cast<const T*>(p);
}

}

extern "C" void PortXD_TimingLogStart(void) {
  const char* path = std::getenv("XD_TIMING_LOG");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  sOut = std::fopen(path, "w");
  if (sOut == nullptr) {
    std::fprintf(stderr, "[timing log] can't write %s\n", path);
    return;
  }
  std::setvbuf(sOut, nullptr, _IOFBF, 1 << 16);
  sV.counter = find<uint32_t>("pokecoloFrameCounter");
  sV.vsync = find<int32_t>("_videoState.vsyncCount");
  sV.drop = find<int32_t>("_videoState.dropCount");
  sV.flip = find<int32_t>("_videoState.flipCount");
  sV.render = find<float>("_videoState.renderTime");
  sV.real = find<float>("_videoState.realTime");
  sV.last = find<int64_t>("_videoState.lastTime");
  sV.lockRate = find<int32_t>("_videoState.lockRate");
  sV.lockCount = find<uint8_t>("_videoState.lockCount");
  sV.lockActive = find<uint8_t>("_videoState.lockActive");
  sV.pause = find<int32_t>("_renderPauseCount");
  sV.frameUs = find<uint64_t>("_frameTime");
  std::fprintf(sOut, "# XD_TIMING_LOG (games/xd/src/timing_log.cpp): one line each time the game prepares a frame\n");
  std::fflush(sOut);
  std::atexit([] {
    if (sOut != nullptr) std::fflush(sOut);
  });
  PortVI_SetFrameWorkHook(on_work);
  std::fprintf(stderr, "[timing log] XD_TIMING_LOG: writing %s\n", path);
}
