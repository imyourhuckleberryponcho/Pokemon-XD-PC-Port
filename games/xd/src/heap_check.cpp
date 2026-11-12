#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

extern "C" bool GSmemIsInitialized(void);

namespace {

struct Cell {
  Cell* prev;
  Cell* next;
  int32_t size;
  uint32_t addr;
  int16_t layer;
};
struct Heap {
  int32_t size;
  Cell* free;
  Cell* allocated;
  int32_t type, unk10, unk14;
  intptr_t unk18;
  int32_t unk1C;
  Cell* freeCells;
  uint8_t unk24[8];
  uint8_t unk2C;
};

constexpr uintptr_t kHeapArray = 0x80507DA0;
constexpr int kMaxHeaps = 4;
constexpr uintptr_t kArenaEnd = 0x81800000;
constexpr int kMaxRecords = 1000000;

bool plausible(const Cell* c, bool type0) {
  const auto a = reinterpret_cast<uintptr_t>(c);
  if (a == 0) return true;
  if ((a & 7) != 0) return false;
  return type0 ? a >= kHeapArray && a < kArenaEnd : a < 0x100000000ull;
}

const void* vp(const Cell* c) { return static_cast<const void*>(c); }

void report(int heap, const char* list, const Cell* c, const Cell* prev, const char* what) {
  const auto* b = reinterpret_cast<const uint8_t*>(c);
  std::fprintf(stderr,
               "\n[games/xd] heap check: heap %d, %s list: block record at %p is damaged: %s\n"
               "  (prev %p, next %p, addr %08x, size %d; previous record in the list %p).\n  Its bytes:",
               heap, list, vp(c), what, vp(c->prev), vp(c->next), c->addr, c->size, vp(prev));
  for (int i = 0; i < 32; i++) std::fprintf(stderr, " %02x", b[i]);
  std::fprintf(stderr, "\n  Find the writer with XD_WATCH=<address of the damaged field> (prev at +0, next at +8,"
                       " size at +0x10).\n");
  std::fflush(stderr);
}

}

extern "C" bool PortXD_HeapCheckEnabled(void) {
  static const int enabled = [] {
    const char* v = std::getenv("XD_HEAPCHECK");
    return v != nullptr && v[0] != '\0' && v[0] != '0' ? 1 : 0;
  }();
  return enabled != 0;
}

extern "C" bool PortXD_CheckGameHeap(void) {
  auto* heaps = reinterpret_cast<const Heap*>(kHeapArray);
  static std::vector<std::pair<uintptr_t, uintptr_t>> used;
  for (int h = 0; h < kMaxHeaps; h++) {
    if (heaps[h].size < 0) continue;
    const bool type0 = heaps[h].type == 0;
    used.clear();
    for (int list = 0; list < 2; list++) {
      const char* name = list == 0 ? "allocated" : "free";
      const Cell* c = list == 0 ? heaps[h].allocated : heaps[h].free;
      const Cell* prev = nullptr;
      if (c != nullptr && c->prev != nullptr) {
        report(h, name, c, nullptr, "the list's first record has a prev link");
        return false;
      }
      for (int n = 0; c != nullptr && n < kMaxRecords; n++) {
        if (!plausible(c->next, type0) || (c->next != nullptr && c->next->prev != c) ||
            (type0 && c->addr != reinterpret_cast<uintptr_t>(c))) {
          report(h, name, c, prev, "its links or addr are wrong");
          return false;
        }
        if (type0 && c->size <= 0) {
          report(h, name, c, prev, "its size is not positive");
          return false;
        }
        if (type0 && list == 0) {
          used.emplace_back(c->addr, uintptr_t{c->addr} + static_cast<uint32_t>(c->size));
        }
        if (type0 && list == 1 && prev != nullptr && uintptr_t{prev->addr} + static_cast<uint32_t>(prev->size) > c->addr) {
          report(h, name, c, prev, "it lies inside the previous free record, or before it (the list is kept sorted)");
          return false;
        }
        if (type0 && list == 1) {
          if (used.size() > 1 && n == 0) {
            std::sort(used.begin(), used.end());
          }
          const uintptr_t start = c->addr, end = start + static_cast<uint32_t>(c->size);
          auto it = std::upper_bound(used.begin(), used.end(), std::make_pair(start, UINTPTR_MAX));
          const bool overlapsPrev = it != used.begin() && std::prev(it)->second > start;
          const bool overlapsNext = it != used.end() && it->first < end;
          if (overlapsPrev || overlapsNext) {
            const auto& o = overlapsPrev ? *std::prev(it) : *it;
            char what[160];
            std::snprintf(what, sizeof what, "this free memory overlaps the allocated block record at %08llx (size %llu)",
                          static_cast<unsigned long long>(o.first), static_cast<unsigned long long>(o.second - o.first));
            report(h, name, c, prev, what);
            return false;
          }
        }
        prev = c;
        c = c->next;
      }
    }
  }
  return true;
}

extern "C" int32_t pokecoloFrameCounter;
extern "C" int32_t __OSCurrHeap;
extern "C" bool PortXD_HeapModelStats(int heap, int32_t* total, int32_t* largest, int32_t* freeBlocks,
                                      int32_t* usedBlocks);
extern "C" void PortXD_HeapModelFlush(void);
namespace {
void heap_stats() {
  static int from = -1, to = -1, last = -1;
  static bool read = false;
  if (!read) {
    read = true;
    if (const char* v = std::getenv("XD_HEAP_STATS"); v != nullptr && v[0] != '\0') {
      std::sscanf(v, "%d-%d", &from, &to);
    }
  }
  const int pass = pokecoloFrameCounter;
  if (from < 0 || pass < from || pass > to || pass == last || !GSmemIsInitialized()) return;
  last = pass;
  auto* heaps = reinterpret_cast<const Heap*>(kHeapArray);
  for (int h = 0; h < kMaxHeaps; h++) {
    if (heaps[h].size < 0) continue;
    long long freeSum = 0, largest = 0, usedSum = 0;
    int nFree = 0, nUsed = 0;
    for (const Cell* c = heaps[h].free; c != nullptr && nFree < kMaxRecords; c = c->next, nFree++) {
      freeSum += c->size;
      largest = c->size > largest ? c->size : largest;
    }
    for (const Cell* c = heaps[h].allocated; c != nullptr && nUsed < kMaxRecords; c = c->next, nUsed++) {
      usedSum += c->size;
    }
    std::fprintf(stderr, "[heap stats] pass %d heap %d%s size %d free %lld largest %lld (%lld %%) blocks free %d used %d "
                 "used bytes %lld\n", pass, h, h == __OSCurrHeap ? " (current)" : "", heaps[h].size, freeSum, largest,
                 freeSum > 0 ? largest * 100 / freeSum : 0, nFree, nUsed, usedSum);
    int32_t mt = 0, ml = 0, mf = 0, mu = 0;
    if (PortXD_HeapModelStats(h, &mt, &ml, &mf, &mu)) {
      std::fprintf(stderr, "[heap stats] pass %d heap %d model free %d largest %d (%lld %%) blocks free %d used %d\n",
                   pass, h, mt, ml, mt > 0 ? static_cast<long long>(ml) * 100 / mt : 0LL, mf, mu);
    }
  }
}
}

extern "C" void PortXD_HeapCheckFrame(void) {
  static bool reported = false;
  heap_stats();
  PortXD_HeapModelFlush();
  if (reported || !PortXD_HeapCheckEnabled() || !GSmemIsInitialized()) return;
  if (!PortXD_CheckGameHeap()) {
    reported = true;
    std::fprintf(stderr, "[games/xd] heap check: found after a video field (XD_HEAPCHECK=1)\n");
    std::fflush(stderr);
    std::abort();
  }
}

extern "C" uint32_t GSmemGetHeapFreeSize(int32_t heap, int32_t* total, uint32_t* largest);
extern "C" void PortXD_FloorHeapFreeSize(int32_t heap, uint32_t* total, uint32_t* largest) {
  GSmemGetHeapFreeSize(heap, reinterpret_cast<int32_t*>(total), largest);
  static const char* force = std::getenv("XD_FLOOR_HEAP");
  if (std::getenv("XD_HEAP_STATS") != nullptr) {
    std::fprintf(stderr, "[heap stats] pass %d floor check: heap %d free %u largest %u (%u %%)%s\n", pokecoloFrameCounter,
                 heap, *total, *largest, *total > 0 ? static_cast<uint32_t>(uint64_t{*largest} * 100 / *total) : 0u,
                 force != nullptr ? " (XD_FLOOR_HEAP given)" : "");
  }
  int from = 0, to = 0x7FFFFFFF;
  if (force != nullptr) {
    if (const char* r = std::strchr(force, ':')) {
      std::sscanf(r + 1, "%d-%d", &from, &to);
    }
  }
  if (force == nullptr || pokecoloFrameCounter < from || pokecoloFrameCounter > to) {
    return;
  }
  if (std::strncmp(force, "keep", 4) == 0) {
    *largest = *total;
  } else if (std::strncmp(force, "free", 4) == 0) {
    *largest = 0;
  }
}
