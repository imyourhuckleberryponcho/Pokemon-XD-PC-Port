#include <dolphin/os.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern void* MEM1Start;
extern void* MEM1End;

namespace {
constexpr uintptr_t kLowCandidates[] = {0x70000000, 0x60000000, 0x50000000, 0x30000000, 0x20000000, 0x10000000};
constexpr size_t kLowSize = 0x10000000;
uintptr_t kLowBase = 0;
bool sLowReserved = false;

bool reserve_low() {
  for (const uintptr_t base : kLowCandidates) {
#ifdef _WIN32
    if (VirtualAlloc(reinterpret_cast<void*>(base), kLowSize, MEM_RESERVE, PAGE_NOACCESS) ==
        reinterpret_cast<void*>(base)) {
#else
    if (mmap(reinterpret_cast<void*>(base), kLowSize, PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0) ==
        reinterpret_cast<void*>(base)) {
#endif
      kLowBase = base;
      return true;
    }
  }
  return false;
}
size_t sLowUsed = 0;
size_t sLowCommitted = 0;
std::mutex sLowLock;

constexpr uintptr_t kMem1Base = 0x80000000;
constexpr uint32_t kMem1Size = 0x01800000;
constexpr uintptr_t kMem1UncachedBase = 0xC0000000;
constexpr uintptr_t kLockedCacheBase = 0xE0000000;
constexpr uint32_t kLockedCacheSize = 0x4000;
constexpr uint32_t kArenaStart = 0x3100;
constexpr uint32_t kBusClock = 162000000;
constexpr uint32_t kCpuClock = 486000000;
bool sReserved = false;
}

extern "C" {

bool PortMem_ReserveMEM1(void) {
#ifndef _WIN32
  const int fd = memfd_create("gc_mem1", 0);
  const bool sized = fd >= 0 && ftruncate(fd, kMem1Size) == 0;
  void* p = sized ? mmap(reinterpret_cast<void*>(kMem1Base), kMem1Size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0)
                  : MAP_FAILED;
  sReserved = p == reinterpret_cast<void*>(kMem1Base);
  if (sReserved && mmap(reinterpret_cast<void*>(kMem1UncachedBase), kMem1Size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0) != reinterpret_cast<void*>(kMem1UncachedBase)) {
    std::fprintf(stderr, "[platform/sdk] no uncached view of game memory at 0xC0000000 (range taken)\n");
  }
  if (fd >= 0) {
    close(fd);
  }
  void* lc = mmap(reinterpret_cast<void*>(kLockedCacheBase), kLockedCacheSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  sReserved = sReserved && lc == reinterpret_cast<void*>(kLockedCacheBase);
#else
  HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMem1Size, nullptr);
  void* p = section != nullptr ? MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, kMem1Size,
                                                 reinterpret_cast<void*>(kMem1Base))
                               : nullptr;
  sReserved = p == reinterpret_cast<void*>(kMem1Base);
  if (sReserved && MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, kMem1Size,
                                   reinterpret_cast<void*>(kMem1UncachedBase)) == nullptr) {
    std::fprintf(stderr, "[platform/sdk] no uncached view of game memory at 0xC0000000 (range taken)\n");
  }
  void* lc = VirtualAlloc(reinterpret_cast<void*>(kLockedCacheBase), 0x10000, MEM_RESERVE, PAGE_NOACCESS);
  sReserved = sReserved && lc == reinterpret_cast<void*>(kLockedCacheBase) &&
              VirtualAlloc(lc, kLockedCacheSize, MEM_COMMIT, PAGE_READWRITE) == lc;
#endif
  sLowReserved = sLowReserved || reserve_low();
  return sReserved && sLowReserved;
}

void* PortMem_LowAlloc(size_t size, size_t align) {
  std::lock_guard<std::mutex> guard(sLowLock);
  if (!sLowReserved) {
    sLowReserved = reserve_low();
    if (!sLowReserved) {
#ifdef _WIN32
      MEMORY_BASIC_INFORMATION mbi{};
      VirtualQuery(reinterpret_cast<void*>(kLowCandidates[0]), &mbi, sizeof(mbi));
      std::fprintf(stderr, "[platform/sdk] no free 256 MB range below 4 GB for game data (at 0x70000000: state %lx, "
                   "allocation base %p)\n", mbi.State, mbi.AllocationBase);
#else
      std::fprintf(stderr, "[platform/sdk] no free 256 MB range below 4 GB for game data (see /proc/self/maps)\n");
#endif
      std::fflush(stderr);
      std::abort();
    }
  }
  if (align == 0) {
    align = 16;
  }
  const size_t start = (sLowUsed + align - 1) & ~(align - 1);
  if (size > kLowSize || start > kLowSize - size) {
    std::fprintf(stderr, "[platform/sdk] out of memory for game data (%zu bytes)\n", size);
    std::abort();
  }
  const size_t end = start + (size ? size : 1);
  if (end > sLowCommitted) {
    const size_t commitEnd = std::min(kLowSize, (end + 0xFFFF) & ~size_t{0xFFFF});
#ifdef _WIN32
    if (VirtualAlloc(reinterpret_cast<void*>(kLowBase + sLowCommitted), commitEnd - sLowCommitted, MEM_COMMIT,
                     PAGE_READWRITE) == nullptr) {
#else
    if (mprotect(reinterpret_cast<void*>(kLowBase + sLowCommitted), commitEnd - sLowCommitted,
                 PROT_READ | PROT_WRITE) != 0) {
#endif
      std::fprintf(stderr, "[platform/sdk] could not commit memory for game data\n");
      std::abort();
    }
    sLowCommitted = commitEnd;
  }
  sLowUsed = end;
  return reinterpret_cast<void*>(kLowBase + start);
}

uint8_t* PortMem_MEM1(void) { return sReserved ? reinterpret_cast<uint8_t*>(kMem1Base) : nullptr; }

void PortMem_InitOS(uint32_t arenaLo) {
  OSInit();
  if (!sReserved) {
    return;
  }
  MEM1Start = reinterpret_cast<void*>(kMem1Base);
  MEM1End = reinterpret_cast<void*>(kMem1Base + kMem1Size);
  OSBaseAddress = kMem1Base;
  auto* low = static_cast<uint8_t*>(MEM1Start);
  auto put32 = [&](uint32_t offset, uint32_t value) { std::memcpy(low + offset, &value, 4); };
  put32(0x28, kMem1Size);
  put32(0xF8, kBusClock);
  put32(0xFC, kCpuClock);
  OSSetArenaLo(arenaLo != 0 ? reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(arenaLo)) : low + kArenaStart);
  OSSetArenaHi(MEM1End);
}

}
