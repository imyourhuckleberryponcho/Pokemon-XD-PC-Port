#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <dbghelp.h>
#else
#include <port_win_compat.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#pragma comment(lib, "dbghelp.lib")
#else
extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize);
#endif

extern "C" {
void* PortHeapReal_OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps);
int PortHeapReal_OSCreateHeap(void* start, void* end);
int32_t PortHeapReal_OSCreateExternalHeap(int32_t start, int32_t end, int32_t* cells, int32_t count);
void PortHeapReal_OSDestroyHeap(int heap);
void* PortHeapReal_OSAllocFromHeap(int heap, uint32_t size);
void PortHeapReal_OSFreeToHeap(int heap, void* ptr);
int32_t PortHeapReal_OSShrinkHeap(int heap, void* ptr, uint32_t size);
int32_t PortHeapReal_OSSetAllocMethodHeap(int32_t heap, int32_t method);
int32_t PortHeapReal_OSGetHeapFreeSize(int32_t heap, int32_t* total, int32_t* largest);
extern int32_t pokecoloFrameCounter;
void PortXD_HeapModelReport(void);
uint32_t gc_size_note(uint32_t pc, uint32_t gc);
void gc_pconly_note(void);
uint32_t GSmemGetCurrentHeap(void);
void PortHeapReal_GSmemInitSysdolAllocator(void);
void* PortHeapReal_GSmemGetSysdolAllocator(uint32_t size);
uint32_t PortHeapReal_GSmemGetFreePool(void* pool);
void* PortHeapReal_GSmemAllocPool(void* pool);
void PortHeapReal_GSmemFreePool(void* pool, void* cell);
void* PortHeapReal_HSD_Alloc(uint32_t size);
void PortHeapReal_HSD_Free(void* ptr);
void* PortHeapReal_HSD_ObjAlloc(void* data);
void PortHeapReal_HSD_ObjAllocInit(void* data, uint32_t size, uint32_t align);
}

namespace {

bool env_on(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

constexpr uint32_t kGcArenaLo = 0x804FFDA0;
constexpr uint32_t kGcArenaHi = 0x817EA6A0;
constexpr uint32_t kGcHeapDescSize = 0x30;

struct Cell {
  Cell* prev = nullptr;
  Cell* next = nullptr;
  int32_t size = 0;
  uint32_t addr = 0;
};

struct Heap {
  int32_t size = -1;
  Cell* free = nullptr;
  Cell* allocated = nullptr;
  int32_t type = 0;
  int32_t method = 0;
  int32_t spare = 0;
  uint32_t lo = 0, hi = 0;
  std::unordered_map<uintptr_t, Cell*> byPtr;
  int32_t header() const { return type == 0 ? 0x20 : 0; }
};

struct Stats {
  long long allocs = 0, frees = 0, noted = 0, pconly = 0, unknownFrees = 0, modelFailed = 0,
            realFailed = 0, noteMismatch = 0;
  long long hsdAllocs = 0, hsdToPool = 0, hsdToHeap = 0, hsdFrees = 0, poolQueries = 0, poolQueryUnknown = 0,
            poolShort = 0, poolDirectUnknown = 0;
};

struct SysPool {
  void* pc = nullptr;
  uint32_t gc = 0;
  uint32_t num = 0, used = 0, peak = 0;
};

std::mutex sLock;
bool sInit = false;
std::vector<Heap> sHeaps;
uint32_t sPcArenaStart = 0, sGcArenaStart = 0, sPcArenaEnd = 0;
Stats sStats;
FILE* sLog = nullptr;
std::vector<SysPool> sSysPools;
std::unordered_map<uintptr_t, SysPool*> sPoolCell;

SysPool* sys_pool_for(uint32_t gc) {
  if (gc != 8 && gc != 12 && gc != 20 && gc != 28 && gc != 48) return nullptr;
  for (SysPool& p : sSysPools) {
    if (p.gc == gc) return &p;
  }
  return nullptr;
}

uint32_t end_of(const Cell* c) { return c->addr + static_cast<uint32_t>(c->size); }

extern "C" void PortCpuCost_Count(unsigned counter, uint64_t amount);
enum : unsigned { kCountHeapOps = 8, kCountHeapWalk = 9, kCountPoolAllocs = 10, kCountHeapInstr = 28 };

uint64_t dl_extract_cost(const Cell* c) {
  return c->prev == nullptr ? (c->next == nullptr ? 8 : 11) : (c->next == nullptr ? 10 : 13);
}

void dl_add_front(Cell*& list, Cell* c) {
  c->next = list;
  c->prev = nullptr;
  if (list != nullptr) list->prev = c;
  list = c;
}

void dl_extract(Cell*& list, Cell* c) {
  if (c->next != nullptr) c->next->prev = c->prev;
  if (c->prev == nullptr) {
    list = c->next;
  } else {
    c->prev->next = c->next;
  }
}

void dl_insert(Heap& h, Cell* cell) {
  Cell* prev = nullptr;
  Cell* next = h.free;
  uint64_t walked = 0;
  for (; next != nullptr; prev = next, next = next->next) {
    if (cell->addr <= next->addr) break;
    ++walked;
  }
  PortCpuCost_Count(kCountHeapOps, 1);
  PortCpuCost_Count(kCountHeapWalk, walked);
  {
    int64_t instr = 43 + 21 + 8 * static_cast<int64_t>(walked);
    const int64_t arr = h.type == 1 ? 3 : 0;
    if (next == nullptr) {
      instr -= 11;
    } else if (end_of(cell) == next->addr) {
      instr += 16 + arr + (next->next != nullptr ? 1 : 0);
    }
    if (prev == nullptr) {
      instr -= 8;
    } else if (end_of(prev) == cell->addr) {
      const Cell* after = next != nullptr && end_of(cell) == next->addr ? next->next : next;
      instr += 15 + arr + (after != nullptr ? 1 : 0);
    }
    PortCpuCost_Count(kCountHeapInstr, static_cast<uint64_t>(instr));
  }
  cell->next = next;
  cell->prev = prev;
  if (next != nullptr) {
    next->prev = cell;
    if (end_of(cell) == next->addr) {
      cell->size += next->size;
      cell->next = next->next;
      if (cell->next != nullptr) cell->next->prev = cell;
      delete next;
      if (h.type == 1) h.spare++;
      next = cell->next;
    }
  }
  if (prev != nullptr) {
    prev->next = cell;
    if (end_of(prev) == cell->addr) {
      prev->size += cell->size;
      prev->next = next;
      if (next != nullptr) next->prev = prev;
      delete cell;
      if (h.type == 1) h.spare++;
    }
    return;
  }
  h.free = cell;
}

void clear_heap(Heap& h) {
  for (Cell* lists[2] = {h.free, h.allocated}; Cell* c : lists) {
    while (c != nullptr) {
      Cell* n = c->next;
      delete c;
      c = n;
    }
  }
  h.free = h.allocated = nullptr;
  h.byPtr.clear();
}

uint32_t to_gc(uint32_t pcAddr) {
  if (pcAddr >= sPcArenaEnd) return kGcArenaHi;
  return pcAddr - sPcArenaStart + sGcArenaStart;
}

uint32_t model_alloc(Heap& h, uint32_t request) {
  uint32_t size = (request + h.header() + 0x1F) & 0xFFFFFFE0u;
  Cell* cell = nullptr;
  uint64_t walked = 0;
  int64_t instr = 23 + (h.method == 1 ? 42 : h.method == 3 ? 40 : 39);
  struct Count {
    uint64_t& n;
    int64_t& instr;
    ~Count() {
      PortCpuCost_Count(kCountHeapOps, 1);
      PortCpuCost_Count(kCountHeapWalk, n);
      PortCpuCost_Count(kCountHeapInstr, static_cast<uint64_t>(instr > 0 ? instr : 0));
    }
  } count{walked, instr};
  if (h.method == 1) {
    bool exact = false;
    for (Cell* c = h.free; c != nullptr; c = c->next) {
      ++walked;
      if (static_cast<int32_t>(size) <= c->size) {
        if (static_cast<int32_t>(size) == c->size) {
          cell = c;
          exact = true;
          break;
        }
        if (cell == nullptr) {
          instr += 12;
          cell = c;
        } else if (c->size < cell->size) {
          instr += 17;
          cell = c;
        } else {
          instr += 15;
        }
      } else {
        instr += 6;
      }
    }
    if (!exact) instr -= 7;
  } else if (h.method == 3) {
    int32_t max = -1;
    for (Cell* c = h.free; c != nullptr; c = c->next) {
      ++walked;
      if (max <= c->size) {
        max = c->size;
        cell = c;
        instr += 10;
      } else {
        instr += 8;
      }
    }
    if (static_cast<int32_t>(size) > max) {
      cell = nullptr;
      instr += 2;
    }
  } else {
    for (cell = h.free; cell != nullptr; cell = cell->next) {
      ++walked;
      if (static_cast<int32_t>(size) <= cell->size) {
        instr += 3;
        break;
      }
      instr += 6;
    }
  }
  if (cell == nullptr) return 0;
  const uint32_t leftover = static_cast<uint32_t>(cell->size) - size;
  if (leftover < 0x40u) {
    instr += 16 + static_cast<int64_t>(dl_extract_cost(cell));
  } else if (h.method == 3 && h.type == 0) {
    instr += 25;
  } else if (h.type == 1 && h.spare <= 0) {
    instr += h.method == 3 ? 21 : 18;
  } else {
    instr += (h.type == 1 ? (h.method == 3 ? 50 : 47) : 38) - (cell->next == nullptr ? 2 : 0) -
             (cell->prev == nullptr ? 2 : 0);
  }
  if (leftover < 0x40u) {
    dl_extract(h.free, cell);
  } else if (h.method == 3 && h.type == 0) {
    Cell* rest = cell;
    rest->size = static_cast<int32_t>(leftover);
    cell = new Cell;
    cell->addr = rest->addr + leftover;
    cell->size = static_cast<int32_t>(size);
  } else {
    if (h.type == 1) {
      if (h.spare <= 0) return 0;
      h.spare--;
    }
    Cell* rest = new Cell;
    cell->size = static_cast<int32_t>(size);
    rest->size = static_cast<int32_t>(leftover);
    rest->addr = end_of(cell);
    rest->prev = cell->prev;
    rest->next = cell->next;
    if (rest->next != nullptr) rest->next->prev = rest;
    if (rest->prev != nullptr) {
      rest->prev->next = rest;
    } else {
      h.free = rest;
    }
  }
  dl_add_front(h.allocated, cell);
  return cell->addr + static_cast<uint32_t>(h.header());
}

void model_free(Heap& h, Cell* cell) {
  PortCpuCost_Count(kCountHeapInstr, 35 + 3 + dl_extract_cost(cell));
  dl_extract(h.allocated, cell);
  dl_insert(h, cell);
}

void model_shrink(Heap& h, Cell* cell, uint32_t request) {
  const uint32_t size = (request + h.header() + 0x1F) & 0xFFFFFFE0u;
  if (size > static_cast<uint32_t>(cell->size)) return;
  const uint32_t leftover = static_cast<uint32_t>(cell->size) - size;
  if (leftover < 0x40u) return;
  if (h.type == 1) {
    if (h.spare <= 0) return;
    h.spare--;
  }
  cell->size = static_cast<int32_t>(size);
  Cell* rest = new Cell;
  rest->size = static_cast<int32_t>(leftover);
  rest->addr = end_of(cell);
  dl_insert(h, rest);
}

void model_free_size(const Heap& h, int32_t* total, int32_t* largest, int32_t* count) {
  int32_t sum = 0, max = 0, n = 0;
  for (const Cell* c = h.free; c != nullptr; c = c->next, n++) {
    sum += c->size;
    max = std::max(max, c->size);
  }
  if (total != nullptr) *total = sum;
  if (largest != nullptr) *largest = max;
  if (count != nullptr) *count = n;
}

struct Note {
  bool set = false;
  bool pcOnly = false;
  uint32_t pc = 0, gc = 0;
};
thread_local Note tNote;
thread_local int tInHsd = 0;
thread_local bool tSysdolInit = false;
struct SysdolAsk {
  void* pool = nullptr;
  uint32_t gc = 0;
};
thread_local SysdolAsk tSysdolAsk;

bool sSymbols = false;
std::unordered_map<uintptr_t, std::string> sNameOf;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : (c == '\\' ? '/' : c));
  return s;
}

#ifndef _WIN32
void init_symbols() { sSymbols = true; }
const std::string& name_of(uintptr_t a) {
  auto it = sNameOf.find(a);
  if (it != sNameOf.end()) return it->second;
  char where[512] = "?";
  PortCrash_DescribeAddress(reinterpret_cast<const void*>(a - 1), where, sizeof where);
  std::string n = where;
  for (char& c : n) {
    if (c == ' ' || c == '\t') c = '_';
  }
  return sNameOf.emplace(a, std::move(n)).first->second;
}
#else
void init_symbols() {
  static bool done = false;
  if (done) return;
  done = true;
  SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS |
                SYMOPT_FAIL_CRITICAL_ERRORS);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  sSymbols = true;
}

bool line_of(uintptr_t ret, std::string& file, int& line) {
  if (!sSymbols) return false;
  IMAGEHLP_LINE64 l = {sizeof(l)};
  DWORD disp = 0;
  if (!SymGetLineFromAddr64(GetCurrentProcess(), ret - 1, &disp, &l) || l.FileName == nullptr) return false;
  file = lower(l.FileName);
  line = static_cast<int>(l.LineNumber);
  return true;
}

const std::string& name_of(uintptr_t a) {
  auto it = sNameOf.find(a);
  if (it != sNameOf.end()) return it->second;
  std::string n = "?";
  if (sSymbols) {
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    if (SymFromAddr(GetCurrentProcess(), a - 1, &disp, sym)) n = sym->Name;
    std::string file;
    int line = 0;
    if (line_of(a, file, line)) {
      const size_t slash = file.rfind('/');
      n += "@" + (slash == std::string::npos ? file : file.substr(slash + 1)) + ":" + std::to_string(line);
    }
  }
  for (char& c : n) {
    if (c == ' ' || c == '\t') c = '_';
  }
  return sNameOf.emplace(a, std::move(n)).first->second;
}
#endif

bool enabled_model() {
  static const bool on = [] {
    const char* v = std::getenv("XD_HEAP_MODEL");
    return v == nullptr || v[0] == '\0' || v[0] != '0';
  }();
  return on;
}

void open_log() {
  static bool done = false;
  if (done) return;
  done = true;
  if (const char* p = std::getenv("XD_HEAP_LOG"); p != nullptr && p[0] != '\0') {
    sLog = std::fopen(p, "w");
    if (sLog != nullptr) {
      init_symbols();
      std::fprintf(sLog, "# XD heap log (games/xd/src/heap_model.cpp): pass op heap ... (see tools/heap_diff.py)\n");
    }
  }
}

void log_callers(void* const* frames, int n) {
  for (int i = 0; i < n && i < 6; i++) {
    std::fprintf(sLog, "%s%s", i == 0 ? "\t" : "|", name_of(reinterpret_cast<uintptr_t>(frames[i])).c_str());
  }
}

int pass() { return pokecoloFrameCounter; }

Heap* heap_at(int heap) {
  if (!sInit || heap < 0 || heap >= static_cast<int>(sHeaps.size()) || sHeaps[heap].size < 0) return nullptr;
  return &sHeaps[heap];
}

}

extern "C" uint32_t gc_size_note(uint32_t pc, uint32_t gc) {
  tNote.set = true;
  tNote.pcOnly = false;
  tNote.pc = pc;
  tNote.gc = gc;
  return pc;
}

extern "C" void gc_pconly_note(void) {
  tNote.set = true;
  tNote.pcOnly = true;
  tNote.pc = tNote.gc = 0;
}

extern "C" void* PortHeapReal_GSmemCreatePool(uint32_t num, uint32_t size);
extern "C" void* GSmemCreatePool(uint32_t num, uint32_t size) {
  const Note note = tNote;
  tNote = Note{};
  uint32_t gcUser = size;
  if (note.set && !note.pcOnly && note.pc == size) {
    const uint32_t pcCell = (size + 8 + 7) & ~7u;
    const uint32_t gcCell = (note.gc + 7) & ~3u;
    gcUser = note.gc;
    gc_size_note((num + 1) * pcCell, (num + 1) * gcCell);
  } else if (note.set) {
    tNote = note;
  }
  void* pool = PortHeapReal_GSmemCreatePool(num, size);
  tNote = Note{};
  if (tSysdolInit) {
    std::lock_guard<std::mutex> g(sLock);
    sSysPools.push_back(SysPool{pool, gcUser, num, 0});
    if (sLog != nullptr) {
      std::fprintf(sLog, "%d syspool %u %u %llx\n", pass(), gcUser, num,
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pool)));
    }
  }
  return pool;
}

namespace {

void log_pools() {
  if (sLog == nullptr) return;
  std::fprintf(sLog, "%d pools", pass());
  for (const SysPool& p : sSysPools) std::fprintf(sLog, " %u:%u/%u/%u", p.gc, p.used, p.peak, p.num);
  std::fprintf(sLog, " pc");
  for (const SysPool& p : sSysPools) {
    const uint32_t free = p.pc != nullptr ? PortHeapReal_GSmemGetFreePool(p.pc) : 0;
    std::fprintf(sLog, " %u:%u", p.gc, p.num - free);
  }
  std::fputc('\n', sLog);
}

void model_hsd_free(void* ptr, void* const* frames, int n) {
  sStats.hsdFrees++;
  const uintptr_t key = reinterpret_cast<uintptr_t>(ptr);
  if (auto it = sPoolCell.find(key); it != sPoolCell.end()) {
    SysPool* p = it->second;
    if (p->used > 0) p->used--;
    sPoolCell.erase(it);
    if (sLog != nullptr) {
      std::fprintf(sLog, "%d pool free %u %llx %u", pass(), p->gc, static_cast<unsigned long long>(key), p->used);
      log_callers(frames, n);
      std::fputc('\n', sLog);
    }
    return;
  }
  const int cur = static_cast<int>(GSmemGetCurrentHeap());
  for (int pass2 = 0; pass2 < 2; pass2++) {
    for (int i = 0; i < static_cast<int>(sHeaps.size()); i++) {
      if ((pass2 == 0) != (i == cur)) continue;
      Heap* h = heap_at(i);
      if (h == nullptr) continue;
      auto it = h->byPtr.find(key);
      if (it == h->byPtr.end()) continue;
      Cell* c = it->second;
      const uint32_t at = c->addr + static_cast<uint32_t>(h->header());
      h->byPtr.erase(it);
      model_free(*h, c);
      sStats.frees++;
      if (sLog != nullptr) {
        std::fprintf(sLog, "%d free %d %llx %08x", pass(), i, static_cast<unsigned long long>(key), at);
        log_callers(frames, n);
        std::fputc('\n', sLog);
      }
      return;
    }
  }
  sStats.unknownFrees++;
}

}

extern "C" void GSmemInitSysdolAllocator(void) {
  {
    std::lock_guard<std::mutex> g(sLock);
    sSysPools.clear();
    sPoolCell.clear();
  }
  tSysdolInit = true;
  PortHeapReal_GSmemInitSysdolAllocator();
  tSysdolInit = false;
}

extern "C" void* HSD_Alloc(uint32_t size) {
  const Note note = tNote;
  tNote = Note{};
  void* frames[12];
  const int n = sLog != nullptr ? RtlCaptureStackBackTrace(1, 12, frames, nullptr) : 0;
  tInHsd++;
  void* p = PortHeapReal_HSD_Alloc(size);
  tInHsd--;
  tNote = Note{};
  std::lock_guard<std::mutex> g(sLock);
  if (!sInit || p == nullptr) return p;
  sStats.hsdAllocs++;
  uint32_t gc = size;
  char how = 'r';
  if (note.set && note.pcOnly) {
    how = 'p';
  } else if (note.set && note.pc == size) {
    gc = note.gc;
    how = 'n';
    sStats.noted++;
  } else if (note.set) {
    if (sStats.noteMismatch++ < 20) {
      std::fprintf(stderr, "[heap model] GC size note for %u bytes (GameCube %u), but HSD_Alloc asks for %u (pass %d):"
                   " requested size used\n", note.pc, note.gc, size, pass());
    }
  }
  if (how == 'p' || gc == 0) {
    sStats.pconly++;
    return p;
  }
  if (gc < 0x40) {
    if (SysPool* pool = sys_pool_for(gc); pool != nullptr && pool->used < pool->num) {
      pool->used++;
      pool->peak = std::max(pool->peak, pool->used);
      sPoolCell[reinterpret_cast<uintptr_t>(p)] = pool;
      sStats.hsdToPool++;
      if (sLog != nullptr) {
        std::fprintf(sLog, "%d pool alloc %u %llx %u %c", pass(), gc,
                     static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)), pool->used, how);
        log_callers(frames, n);
        std::fputc('\n', sLog);
      }
      return p;
    }
  }
  const int heap = static_cast<int>(GSmemGetCurrentHeap());
  Heap* h = heap_at(heap);
  sStats.allocs++;
  sStats.hsdToHeap++;
  uint32_t at = 0;
  if (h != nullptr) {
    at = model_alloc(*h, gc);
    if (at == 0) {
      sStats.modelFailed++;
    } else {
      h->byPtr[reinterpret_cast<uintptr_t>(p)] = h->allocated;
    }
  }
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d alloc %d %u %u h %llx %08x", pass(), heap, size, gc,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)), at);
    log_callers(frames, n);
    std::fputc('\n', sLog);
  }
  return p;
}

extern "C" void HSD_Free(void* ptr) {
  if (ptr != nullptr) {
    void* frames[12];
    const int n = sLog != nullptr ? RtlCaptureStackBackTrace(1, 12, frames, nullptr) : 0;
    std::lock_guard<std::mutex> g(sLock);
    if (sInit) model_hsd_free(ptr, frames, n);
  }
  tInHsd++;
  PortHeapReal_HSD_Free(ptr);
  tInHsd--;
}

namespace {
struct ObjSizes {
  uint32_t pc = 0, gc = 0;
};
std::unordered_map<uintptr_t, ObjSizes> sObjSizes;
}

extern "C" void HSD_ObjAllocInit(void* data, uint32_t size, uint32_t align) {
  const Note note = tNote;
  tNote = Note{};
  PortHeapReal_HSD_ObjAllocInit(data, size, align);
  std::lock_guard<std::mutex> g(sLock);
  const uint32_t a = align == 0 ? 1 : align;
  if (note.set && !note.pcOnly && note.pc == size) {
    sObjSizes[reinterpret_cast<uintptr_t>(data)] = ObjSizes{(size + a - 1) & ~(a - 1), (note.gc + a - 1) & ~(a - 1)};
  } else {
    sObjSizes.erase(reinterpret_cast<uintptr_t>(data));
  }
}

extern "C" void* HSD_ObjAlloc(void* data) {
  PortCpuCost_Count(kCountPoolAllocs, 1);
  ObjSizes s;
  {
    std::lock_guard<std::mutex> g(sLock);
    if (auto it = sObjSizes.find(reinterpret_cast<uintptr_t>(data)); it != sObjSizes.end()) s = it->second;
  }
  if (s.pc != 0) gc_size_note(s.pc, s.gc);
  void* p = PortHeapReal_HSD_ObjAlloc(data);
  tNote = Note{};
  return p;
}

extern "C" void* GSmemGetSysdolAllocator(uint32_t size) {
  const Note note = tNote;
  tNote = Note{};
  void* pool = PortHeapReal_GSmemGetSysdolAllocator(size);
  tSysdolAsk = SysdolAsk{pool, note.set && !note.pcOnly && note.pc == size ? note.gc : 0};
  return pool;
}

extern "C" uint32_t GSmemGetFreePool(void* pool) {
  const uint32_t real = PortHeapReal_GSmemGetFreePool(pool);
  std::lock_guard<std::mutex> g(sLock);
  if (!sInit || pool == nullptr) return real;
  sStats.poolQueries++;
  SysPool* p = tSysdolAsk.pool == pool && tSysdolAsk.gc != 0 ? sys_pool_for(tSysdolAsk.gc) : nullptr;
  uint32_t answer = real;
  uint32_t model = 0;
  if (p == nullptr) {
    sStats.poolQueryUnknown++;
  } else {
    model = p->num - p->used;
    if (enabled_model()) {
      answer = real == 0 ? 0 : model;
      if (real == 0 && model != 0) sStats.poolShort++;
    }
  }
  if (sLog != nullptr) {
    void* frames[8];
    const int n = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    std::fprintf(sLog, "%d poolq %u real %u model %u answer %u", pass(), p != nullptr ? p->gc : 0, real, model, answer);
    log_callers(frames, n);
    std::fputc('\n', sLog);
  }
  return answer;
}

extern "C" void* GSmemAllocPool(void* pool) {
  void* cell = PortHeapReal_GSmemAllocPool(pool);
  const SysdolAsk ask = tSysdolAsk;
  tSysdolAsk = SysdolAsk{};
  std::lock_guard<std::mutex> g(sLock);
  if (!sInit || cell == nullptr) return cell;
  SysPool* p = ask.pool == pool && ask.gc != 0 ? sys_pool_for(ask.gc) : nullptr;
  if (p == nullptr) {
    sStats.poolDirectUnknown++;
    return cell;
  }
  if (p->used < p->num) {
    p->used++;
    p->peak = std::max(p->peak, p->used);
    sPoolCell[reinterpret_cast<uintptr_t>(cell)] = p;
  } else {
    sStats.modelFailed++;
  }
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d pool alloc %u %llx %u d\n", pass(), p->gc,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(cell)), p->used);
  }
  return cell;
}

extern "C" void GSmemFreePool(void* pool, void* cell) {
  tSysdolAsk = SysdolAsk{};
  if (cell != nullptr) {
    void* frames[12];
    const int n = sLog != nullptr ? RtlCaptureStackBackTrace(1, 12, frames, nullptr) : 0;
    std::lock_guard<std::mutex> g(sLock);
    if (sInit) model_hsd_free(cell, frames, n);
  }
  PortHeapReal_GSmemFreePool(pool, cell);
}

extern "C" void* OSInitAlloc(void* arenaStart, void* arenaEnd, int maxHeaps) {
  void* r = PortHeapReal_OSInitAlloc(arenaStart, arenaEnd, maxHeaps);
  std::lock_guard<std::mutex> g(sLock);
  open_log();
  for (Heap& h : sHeaps) clear_heap(h);
  sHeaps.assign(std::max(maxHeaps, 0), Heap{});
  sPcArenaStart = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r));
  sPcArenaEnd = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arenaEnd)) & 0xFFFFFFE0u;
  sGcArenaStart = (kGcArenaLo + maxHeaps * kGcHeapDescSize + 0x1F) & 0xFFFFFFE0u;
  if (!sInit && (sLog != nullptr || enabled_model())) {
    std::atexit([] {
      PortXD_HeapModelReport();
      if (sLog != nullptr) std::fflush(sLog);
    });
  }
  sInit = true;
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d init pc %08x-%08x gc %08x max %d\n", pass(), sPcArenaStart, sPcArenaEnd, sGcArenaStart,
                 maxHeaps);
  }
  return r;
}

extern "C" int OSCreateHeap(void* start, void* end) {
  const int heap = PortHeapReal_OSCreateHeap(start, end);
  std::lock_guard<std::mutex> g(sLock);
  if (!sInit || heap < 0 || heap >= static_cast<int>(sHeaps.size())) return heap;
  Heap& h = sHeaps[heap];
  clear_heap(h);
  const uint32_t lo = (to_gc(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start))) + 0x1F) & 0xFFFFFFE0u;
  const uint32_t hi = to_gc(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end))) & 0xFFFFFFE0u;
  h = Heap{};
  h.type = 0;
  h.lo = lo;
  h.hi = hi;
  h.size = static_cast<int32_t>(hi - lo);
  h.free = new Cell;
  h.free->addr = lo;
  h.free->size = h.size;
  if (sLog != nullptr) std::fprintf(sLog, "%d create %d gc %08x-%08x size %d\n", pass(), heap, lo, hi, h.size);
  return heap;
}

extern "C" int32_t OSCreateExternalHeap(int32_t start, int32_t end, int32_t* cells, int32_t count) {
  const int32_t heap = PortHeapReal_OSCreateExternalHeap(start, end, cells, count);
  std::lock_guard<std::mutex> g(sLock);
  if (!sInit || heap < 0 || heap >= static_cast<int>(sHeaps.size())) return heap;
  Heap& h = sHeaps[heap];
  clear_heap(h);
  h = Heap{};
  const uint32_t lo = (static_cast<uint32_t>(start) + 0x1F) & 0xFFFFFFE0u;
  const uint32_t hi = static_cast<uint32_t>(end) & 0xFFFFFFE0u;
  h.type = 1;
  h.lo = lo;
  h.hi = hi;
  h.size = static_cast<int32_t>(hi - lo);
  h.spare = count - 1;
  h.free = new Cell;
  h.free->addr = lo;
  h.free->size = h.size;
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d create %d external %08x-%08x size %d cells %d\n", pass(), heap, lo, hi, h.size, count);
  }
  return heap;
}

extern "C" void OSDestroyHeap(int heap) {
  PortHeapReal_OSDestroyHeap(heap);
  std::lock_guard<std::mutex> g(sLock);
  if (Heap* h = heap_at(heap)) {
    clear_heap(*h);
    h->size = -1;
    if (sLog != nullptr) std::fprintf(sLog, "%d destroy %d\n", pass(), heap);
  }
}

extern "C" int32_t OSSetAllocMethodHeap(int32_t heap, int32_t method) {
  const int32_t old = PortHeapReal_OSSetAllocMethodHeap(heap, method);
  std::lock_guard<std::mutex> g(sLock);
  if (sInit && heap >= 0 && heap < static_cast<int>(sHeaps.size())) {
    sHeaps[heap].method = method;
    if (sLog != nullptr) std::fprintf(sLog, "%d method %d %d\n", pass(), heap, method);
  }
  return old;
}

extern "C" void* OSAllocFromHeap(int heap, uint32_t size) {
  const Note note = tNote;
  tNote = Note{};
  void* frames[12];
  const int n = sLog != nullptr ? RtlCaptureStackBackTrace(1, 12, frames, nullptr) : 0;
  void* p = PortHeapReal_OSAllocFromHeap(heap, size);
  std::lock_guard<std::mutex> g(sLock);
  Heap* h = heap_at(heap);
  if (h == nullptr) return p;
  sStats.allocs++;
  if (p == nullptr) sStats.realFailed++;
  uint32_t gc = size;
  char how = 'r';
  if (tInHsd > 0) {
    how = 'x';
  } else if (note.set && note.pcOnly) {
    how = 'p';
  } else if (note.set) {
    if (note.pc == size) {
      gc = static_cast<uint32_t>(note.gc);
      how = 'n';
    } else {
      if (sStats.noteMismatch++ < 20) {
        std::fprintf(stderr, "[heap model] GC size note for %u bytes (GameCube %u), but the allocation asks for %u"
                     " (heap %d, pass %d): requested size used\n", note.pc, note.gc, size, heap, pass());
      }
    }
  }
  if (how == 'n') sStats.noted++;
  uint32_t at = 0;
  if (how == 'x') {
    sStats.allocs--;
  } else if (how == 'p') {
    sStats.pconly++;
  } else if (p != nullptr) {
    at = model_alloc(*h, gc);
    if (at == 0) {
      sStats.modelFailed++;
    } else {
      h->byPtr[reinterpret_cast<uintptr_t>(p)] = h->allocated;
    }
  }
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d alloc %d %u %u %c %llx %08x", pass(), heap, size, gc, how,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p)), at);
    log_callers(frames, n);
    std::fputc('\n', sLog);
  }
  return p;
}

extern "C" void OSFreeToHeap(int heap, void* ptr) {
  void* frames[12];
  const int n = sLog != nullptr ? RtlCaptureStackBackTrace(1, 12, frames, nullptr) : 0;
  PortHeapReal_OSFreeToHeap(heap, ptr);
  std::lock_guard<std::mutex> g(sLock);
  Heap* h = heap_at(heap);
  if (h == nullptr) return;
  if (tInHsd > 0) {
    if (sLog != nullptr) {
      std::fprintf(sLog, "%d freex %d %llx\n", pass(), heap, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr)));
    }
    return;
  }
  sStats.frees++;
  uint32_t at = 0;
  auto it = h->byPtr.find(reinterpret_cast<uintptr_t>(ptr));
  if (it != h->byPtr.end()) {
    Cell* c = it->second;
    at = c->addr + static_cast<uint32_t>(h->header());
    h->byPtr.erase(it);
    model_free(*h, c);
  } else {
    sStats.unknownFrees++;
  }
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d free %d %llx %08x", pass(), heap, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr)),
                 at);
    log_callers(frames, n);
    std::fputc('\n', sLog);
  }
}

extern "C" int32_t OSShrinkHeap(int heap, void* ptr, uint32_t size) {
  const Note note = tNote;
  tNote = Note{};
  const int32_t r = PortHeapReal_OSShrinkHeap(heap, ptr, size);
  std::lock_guard<std::mutex> g(sLock);
  Heap* h = heap_at(heap);
  if (h == nullptr || size == 0) return r;
  const uint32_t gc = note.set && !note.pcOnly && note.pc == size ? static_cast<uint32_t>(note.gc) : size;
  auto it = h->byPtr.find(reinterpret_cast<uintptr_t>(ptr));
  if (it != h->byPtr.end()) model_shrink(*h, it->second, gc);
  if (sLog != nullptr) {
    std::fprintf(sLog, "%d shrink %d %llx %u %u\n", pass(), heap, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ptr)),
                 size, gc);
  }
  return r;
}

extern "C" int32_t OSGetHeapFreeSize(int32_t heap, int32_t* total, int32_t* largest) {
  int32_t realTotal = 0, realLargest = 0;
  const int32_t r = PortHeapReal_OSGetHeapFreeSize(heap, &realTotal, &realLargest);
  int32_t t = realTotal, l = realLargest;
  std::lock_guard<std::mutex> g(sLock);
  Heap* h = heap_at(heap);
  int32_t mt = 0, ml = 0, mn = 0;
  if (h != nullptr) model_free_size(*h, &mt, &ml, &mn);
  if (enabled_model() && h != nullptr && r != 0) {
    t = mt;
    l = ml;
  }
  if (sLog != nullptr) {
    void* frames[8];
    const int n = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    std::fprintf(sLog, "%d query %d real %d %d model %d %d blocks %d", pass(), heap, realTotal, realLargest, mt, ml, mn);
    log_callers(frames, n);
    std::fputc('\n', sLog);
    log_pools();
  }
  if (total != nullptr) *total = t;
  if (largest != nullptr) *largest = l;
  return r;
}

extern "C" bool PortXD_HeapModelStats(int heap, int32_t* total, int32_t* largest, int32_t* freeBlocks,
                                      int32_t* usedBlocks) {
  std::lock_guard<std::mutex> g(sLock);
  Heap* h = heap_at(heap);
  if (h == nullptr) return false;
  model_free_size(*h, total, largest, freeBlocks);
  int32_t used = 0;
  for (const Cell* c = h->allocated; c != nullptr; c = c->next) used++;
  if (usedBlocks != nullptr) *usedBlocks = used;
  return true;
}

extern "C" bool PortXD_HeapModelEnabled(void) { return enabled_model(); }

extern "C" bool PortXD_HeapModelPoolStats(uint32_t gc, uint32_t* used, uint32_t* num) {
  std::lock_guard<std::mutex> g(sLock);
  SysPool* p = sys_pool_for(gc);
  if (p == nullptr) return false;
  if (used != nullptr) *used = p->used;
  if (num != nullptr) *num = p->num;
  return true;
}

extern "C" void PortXD_HeapModelFlush(void) {
  std::lock_guard<std::mutex> g(sLock);
  if (sLog != nullptr) std::fflush(sLog);
}

extern "C" void PortXD_HeapModelReport(void) {
  log_pools();
  for (const SysPool& p : sSysPools) {
    std::fprintf(stderr, "[heap model] GameCube HSD pool %u: %u of %u cells used, at most %u\n", p.gc, p.used, p.num,
                 p.peak);
  }
  std::fprintf(stderr, "[heap model] %lld allocations (%lld GameCube size from a note, %lld PC only), "
               "%lld frees (%lld of blocks the model doesn't have); model out of memory %lld times, PC heap %lld times; "
               "%lld notes not matching their allocation\n", sStats.allocs, sStats.noted, sStats.pconly,
               sStats.frees, sStats.unknownFrees, sStats.modelFailed, sStats.realFailed, sStats.noteMismatch);
  std::fprintf(stderr, "[heap model] HSD: %lld requests (%lld to a GameCube pool, %lld to the heap), %lld frees; pool "
               "free counts asked %lld times (%lld without a GameCube size, %lld times the PC pool was empty first); "
               "%lld direct pool cells without a GameCube size\n", sStats.hsdAllocs, sStats.hsdToPool,
               sStats.hsdToHeap, sStats.hsdFrees, sStats.poolQueries, sStats.poolQueryUnknown, sStats.poolShort,
               sStats.poolDirectUnknown);
}
