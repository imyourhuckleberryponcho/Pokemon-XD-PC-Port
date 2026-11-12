#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
uint8_t* PortMem_MEM1(void);
}

namespace {
constexpr uint32_t kMem1Size = 0x01800000;
enum : uint8_t { kFifo = 1, kDisplayList = 2, kFrame = 3, kMem1 = 4, kFlip = 5, kMemNow = 6, kPage = 7 };
constexpr uint32_t kPageSize = 0x1000;

struct Range {
  uint32_t first, last;
};

std::mutex sLock;
bool sConfigured = false;
std::vector<Range> sFrames;
std::vector<Range> sMemFrames;
uint32_t sStart = 0, sEnd = 0;
bool sExit = false;
std::string sPath;
FILE* sFile = nullptr;
bool sDone = false;
std::vector<uint8_t> sShadow;

bool in(const std::vector<Range>& ranges, uint32_t frame) {
  for (const Range& r : ranges) {
    if (frame >= r.first && frame <= r.last) return true;
  }
  return false;
}

std::vector<Range> parse_frames(const char* list) {
  std::vector<Range> out;
  if (list == nullptr) return out;
  for (const char* p = list; *p != '\0';) {
    char* end = nullptr;
    const unsigned long a = std::strtoul(p, &end, 10);
    if (end == p) break;
    unsigned long b = a;
    p = end;
    if (*p == '-') {
      b = std::strtoul(p + 1, &end, 10);
      p = end;
    }
    out.push_back({static_cast<uint32_t>(a), static_cast<uint32_t>(b < a ? a : b)});
    while (*p == ',' || *p == ' ') p++;
  }
  return out;
}

void configure() {
  sConfigured = true;
  sFrames = parse_frames(std::getenv("XD_GX_DUMP"));
  if (sFrames.empty()) return;
  const char* mem = std::getenv("XD_GX_DUMP_MEM");
  sMemFrames = mem != nullptr && mem[0] != '\0' ? parse_frames(mem) : sFrames;
  uint32_t lo = UINT32_MAX, hi = 0;
  for (const Range& r : sFrames) {
    lo = r.first < lo ? r.first : lo;
    hi = r.last > hi ? r.last : hi;
  }
  const char* warm = std::getenv("XD_GX_DUMP_WARMUP");
  const uint32_t warmup = warm != nullptr && warm[0] != '\0' ? static_cast<uint32_t>(std::strtoul(warm, nullptr, 10)) : 30;
  sStart = lo > warmup ? lo - warmup : 0;
  sEnd = hi + 2;
  const char* dir = std::getenv("XD_GX_DUMP_DIR");
  sPath = dir != nullptr && dir[0] != '\0' ? std::string(dir) + "\\gxdump.bin" : std::string("gxdump.bin");
  const char* ex = std::getenv("XD_GX_DUMP_EXIT");
  sExit = ex != nullptr && ex[0] == '1';
  std::fprintf(stderr, "[gx dump] XD_GX_DUMP: frames %u-%u (from %u for the chip's state) into %s\n", lo, hi, sStart,
               sPath.c_str());
}

void put(uint8_t kind, const void* a, uint32_t na, const void* b = nullptr, uint32_t nb = 0) {
  const uint32_t size = na + nb;
  std::fwrite(&kind, 1, 1, sFile);
  std::fwrite(&size, 4, 1, sFile);
  if (na != 0) std::fwrite(a, 1, na, sFile);
  if (nb != 0) std::fwrite(b, 1, nb, sFile);
}

bool active() {
  if (!sConfigured) configure();
  if (sFrames.empty() || sDone) return false;
  if (sFile == nullptr) {
    if (PortVI_GetFlipCount() < sStart) return false;
    sFile = std::fopen(sPath.c_str(), "wb");
    if (sFile == nullptr) {
      std::fprintf(stderr, "[gx dump] can't write %s\n", sPath.c_str());
      sDone = true;
      return false;
    }
    std::fwrite("XDGXDMP1", 1, 8, sFile);
  }
  return true;
}
void record_memory() {
  const uint8_t* mem1 = PortMem_MEM1();
  if (mem1 == nullptr) return;
  if (sShadow.empty()) {
    sShadow.assign(mem1, mem1 + kMem1Size);
    put(kMemNow, mem1, kMem1Size);
    return;
  }
  for (uint32_t off = 0; off < kMem1Size; off += kPageSize) {
    if (std::memcmp(mem1 + off, sShadow.data() + off, kPageSize) != 0) {
      std::memcpy(sShadow.data() + off, mem1 + off, kPageSize);
      put(kPage, &off, 4, mem1 + off, kPageSize);
    }
  }
}
}

extern "C" {

void PortGXDump_Fifo(const void* data, uint32_t size) {
  std::lock_guard<std::mutex> lock(sLock);
  if (size == 0 || !active()) return;
  static uint32_t sLastFlip = UINT32_MAX;
  const uint32_t flip = PortVI_GetFlipCount();
  if (flip != sLastFlip) {
    sLastFlip = flip;
    put(kFlip, &flip, 4);
  }
  if (in(sMemFrames, flip) || in(sMemFrames, flip + 1)) record_memory();
  put(kFifo, data, size);
}

void PortGXDump_DisplayList(const void* list, uint32_t size) {
  std::lock_guard<std::mutex> lock(sLock);
  if (size == 0 || !active()) return;
  uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
  const uint8_t* mem1 = PortMem_MEM1();
  if (mem1 != nullptr && static_cast<const uint8_t*>(list) >= mem1 && static_cast<const uint8_t*>(list) < mem1 + kMem1Size) {
    address = static_cast<uint32_t>(static_cast<const uint8_t*>(list) - mem1);
  }
  put(kDisplayList, &address, 4, list, size);
}

void PortGXDump_EndOfFrame(void) {
  std::lock_guard<std::mutex> lock(sLock);
  if (!active()) return;
  const uint32_t frame = PortVI_GetFlipCount();
  const uint32_t info[3] = {frame, VIGetRetraceCount(), in(sFrames, frame) ? 1u : 0u};
  put(kFrame, info, sizeof info);
  if (frame > sEnd) {
    std::fclose(sFile);
    sFile = nullptr;
    sDone = true;
    std::fprintf(stderr, "[gx dump] written: %s\n", sPath.c_str());
    if (sExit) {
      std::fflush(stdout);
      std::fflush(stderr);
      std::_Exit(0);
    }
  }
}

}
