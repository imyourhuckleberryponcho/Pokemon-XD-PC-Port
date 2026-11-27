#include <dolphin/os.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#ifdef __ANDROID__
#include "port_android_layout.h"
#include <sys/syscall.h>
#if __ANDROID_API__ < 30
#define memfd_create(name, flags) static_cast<int>(syscall(__NR_memfd_create, (name), (flags)))
#endif
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <mutex>

extern void* MEM1Start;
extern void* MEM1End;

namespace {
constexpr uintptr_t kLowCandidates[] = {0x70000000, 0x60000000, 0x50000000, 0x30000000, 0x20000000, 0x10000000};
#ifdef __ANDROID__
size_t kLowSize = 0x10000000;
#else
constexpr size_t kLowSize = 0x10000000;
#endif
uintptr_t kLowBase = 0;
bool sLowReserved = false;
bool sGameProgram = false;
bool sLowIsOrdinary = false;

bool reserve_low() {
  if (const char* none = std::getenv("PORT_TEST_NO_LOW_MEMORY"); none != nullptr && none[0] == '1') {
    return false;
  }
#ifdef __ANDROID__
  if (const uintptr_t base = PortAndroid_LowBlockBase(); base != 0) {
    kLowBase = base;
    kLowSize = PortAndroid_LowBlockSize();
    return true;
  }
  return false;
#endif
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
#ifdef __ANDROID__
char sFailure[700] = "";
#endif
#ifndef _WIN32
#ifdef __ANDROID__
int fixed_flags(uintptr_t at) { return PortAndroid_FixedRangeReserved(at) ? MAP_FIXED : MAP_FIXED_NOREPLACE; }
#else
int fixed_flags(uintptr_t) { return MAP_FIXED_NOREPLACE; }
#endif
#endif
}

extern "C" {

bool PortMem_ReserveMEM1(void) {
  sGameProgram = true;
#ifndef _WIN32
  const int fd = memfd_create("gc_mem1", 0);
  [[maybe_unused]] const int fdError = errno;
  const bool sized = fd >= 0 && ftruncate(fd, kMem1Size) == 0;
  [[maybe_unused]] const int sizeError = errno;
  void* p = sized ? mmap(reinterpret_cast<void*>(kMem1Base), kMem1Size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | fixed_flags(kMem1Base), fd, 0)
                  : MAP_FAILED;
  [[maybe_unused]] const int mapError = errno;
#ifdef __ANDROID__
  if (!sized) {
    std::snprintf(sFailure, sizeof sFailure, "%s failed (%s)", fd < 0 ? "memfd_create" : "sizing the memory file", std::strerror(fd < 0 ? fdError : sizeError));
  } else if (p != reinterpret_cast<void*>(kMem1Base)) {
    std::snprintf(sFailure, sizeof sFailure, "main memory at 0x80000000: %s; held by: %.300s", p == MAP_FAILED ? std::strerror(mapError) : "the system placed it elsewhere", PortAndroid_RangeOwners(kMem1Base, kMem1Size));
  }
  if (p != MAP_FAILED && p != reinterpret_cast<void*>(kMem1Base)) munmap(p, kMem1Size);
#endif
  sReserved = p == reinterpret_cast<void*>(kMem1Base);
  if (sReserved && mmap(reinterpret_cast<void*>(kMem1UncachedBase), kMem1Size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | fixed_flags(kMem1UncachedBase), fd, 0) != reinterpret_cast<void*>(kMem1UncachedBase)) {
    std::fprintf(stderr, "[platform/sdk] no uncached view of game memory at 0xC0000000 (range taken)\n");
  }
  if (fd >= 0) {
    close(fd);
  }
  void* lc = mmap(reinterpret_cast<void*>(kLockedCacheBase), kLockedCacheSize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | fixed_flags(kLockedCacheBase), -1, 0);
#ifdef __ANDROID__
  if (sFailure[0] == 0 && lc != reinterpret_cast<void*>(kLockedCacheBase)) {
    std::snprintf(sFailure, sizeof sFailure, "the locked cache at 0xE0000000: %s; held by: %.300s", lc == MAP_FAILED ? std::strerror(errno) : "the system placed it elsewhere", PortAndroid_RangeOwners(kLockedCacheBase, kLockedCacheSize));
  }
  if (lc != MAP_FAILED && lc != reinterpret_cast<void*>(kLockedCacheBase)) munmap(lc, kLockedCacheSize);
#endif
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
#ifdef __ANDROID__
  if (sFailure[0] == 0 && !sLowReserved) {
    std::snprintf(sFailure, sizeof sFailure, "no free block of 96 MB or more below 4 GB for the game's converted data (free below 2 GB: %s)", PortAndroid_LargestGaps());
  }
#endif
  return sReserved && sLowReserved;
}

#ifdef __ANDROID__
const char* PortMem_StartFailureNote(void) { return sFailure; }
#endif

void* PortMem_LowAlloc(size_t size, size_t align) {
  std::lock_guard<std::mutex> guard(sLowLock);
  if (!sLowReserved && !sLowIsOrdinary) {
    sLowReserved = reserve_low();
    if (!sLowReserved) {
      if (!sGameProgram) {
        sLowIsOrdinary = true;
        std::fprintf(stderr, "[platform/sdk] no free 256 MB range below 4 GB: this program (not the game) uses ordinary memory for game data\n");
        std::fflush(stderr);
      } else {
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
  }
  if (sLowIsOrdinary) {
    if (align < 16) {
      align = 16;
    }
    void* raw = std::calloc(1, (size ? size : 1) + align);
    if (raw == nullptr) {
      std::fprintf(stderr, "[platform/sdk] out of memory for game data (%zu bytes)\n", size);
      std::abort();
    }
    return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(raw) + align - 1) & ~(static_cast<uintptr_t>(align) - 1));
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

void PortMem_LowRange(uint8_t** base, size_t* used) {
  std::lock_guard<std::mutex> guard(sLowLock);
  *base = sLowReserved ? reinterpret_cast<uint8_t*>(kLowBase) : nullptr;
  *used = sLowReserved ? sLowUsed : 0;
}

bool PortMem_LowEnsureUsed(size_t used) {
  std::lock_guard<std::mutex> guard(sLowLock);
  if (!sLowReserved || used > kLowSize || used < sLowUsed) return false;
  if (used > sLowCommitted) {
    const size_t commitEnd = std::min(kLowSize, (used + 0xFFFF) & ~size_t{0xFFFF});
#ifdef _WIN32
    if (VirtualAlloc(reinterpret_cast<void*>(kLowBase + sLowCommitted), commitEnd - sLowCommitted, MEM_COMMIT, PAGE_READWRITE) == nullptr) return false;
#else
    if (mprotect(reinterpret_cast<void*>(kLowBase + sLowCommitted), commitEnd - sLowCommitted, PROT_READ | PROT_WRITE) != 0) return false;
#endif
    sLowCommitted = commitEnd;
  }
  sLowUsed = used;
  return true;
}

bool PortMem_LowIsOrdinary(void) { return sLowIsOrdinary; }

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
