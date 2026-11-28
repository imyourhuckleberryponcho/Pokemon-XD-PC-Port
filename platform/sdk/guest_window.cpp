#include "include/port_addr_window.h"

#ifdef XD_ADDR_WINDOW
#ifndef _WIN32
#include "port_low_arena.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <execinfo.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/ldsyms.h>
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif
#else
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifdef __ANDROID__
#include <dlfcn.h>
#else
extern "C" char __executable_start;
#endif
#endif
#endif

extern "C" {

void (*g_portWinDescribe)(const void* address, char* out, size_t outSize) = nullptr;

uintptr_t g_portWinBase = 0;
uint32_t g_portWinStandinLow = PORT_WIN_STANDIN_LOW;
int g_portWinStrict = 0;
uintptr_t g_portWinImage = PORT_WIN_IMG_LO;
uint32_t g_portWinImgLo = PORT_WIN_IMG_LO;
uint32_t g_portWinImgSize = PORT_WIN_IMG_SIZE;

void PortWin_Fail(const char* what, uint64_t value) {
  std::fprintf(stderr, "[window] STOP: %s (address 0x%llx, window at 0x%llx)\n", what,
               static_cast<unsigned long long>(value), static_cast<unsigned long long>(g_portWinBase));
#if !defined(_WIN32) && !defined(__APPLE__)
  {
    if (FILE* m = std::fopen("/proc/self/maps", "r")) {
      char line[400];
      while (std::fgets(line, sizeof line, m) != nullptr) {
        unsigned long long lo = 0, hi = 0;
        if (std::sscanf(line, "%llx-%llx", &lo, &hi) == 2 && value >= lo && value < hi) {
          std::fprintf(stderr, "[window] that address lies in: %s", line);
        }
      }
      std::fclose(m);
    }
  }
#endif
  std::fflush(stderr);
  std::abort();
}

}

namespace {

struct NoteSite {
  uint64_t key;
  unsigned long long count;
  unsigned long long firstValue;
  std::string text;
};
std::mutex sNoteLock;
std::vector<NoteSite> sNotes;
bool sNoteExit = false;

void note_summary() {
  std::lock_guard<std::mutex> g(sNoteLock);
  std::fprintf(stderr, "[window] strict count: %zu distinct places\n", sNotes.size());
  for (size_t i = 0; i < sNotes.size(); ++i) {
    std::fprintf(stderr, "[window]   #%zu  %llu times  %s\n", i + 1, sNotes[i].count, sNotes[i].text.c_str());
  }
  std::fflush(stderr);
}

}

extern "C" void PortWin_NoteFrames(const char* what, uint64_t value, void* const* fr, int n) {
  uint64_t key = 1469598103934665603ull;
  for (int i = 0; i < n && i < 4; ++i) {
    key = (key ^ reinterpret_cast<uintptr_t>(fr[i])) * 1099511628211ull;
  }
  std::lock_guard<std::mutex> g(sNoteLock);
  for (NoteSite& s : sNotes) {
    if (s.key == key) {
      ++s.count;
      return;
    }
  }
  std::string text;
  std::fprintf(stderr, "[window] NOTE #%zu: %s (0x%llx)\n", sNotes.size() + 1, what, static_cast<unsigned long long>(value));
  for (int i = 0; i < n && i < 8; ++i) {
    char where[300] = {};
    if (g_portWinDescribe != nullptr) {
      g_portWinDescribe(fr[i], where, sizeof where);
    } else {
      std::snprintf(where, sizeof where, "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(fr[i])));
    }
    std::fprintf(stderr, "[window]     <- %s\n", where);
    if (i < 3) {
      text += (i ? " <- " : "") + std::string(where);
    }
  }
  std::fflush(stderr);
  sNotes.push_back({key, 1, static_cast<unsigned long long>(value), text});
  if (!sNoteExit) {
    sNoteExit = true;
    std::atexit(note_summary);
  }
}

extern "C" void PortWin_Note(const char* what, uint64_t value) {
  void* fr[10] = {};
#ifdef _WIN32
  const int n = static_cast<int>(CaptureStackBackTrace(1, 10, fr, nullptr));
#else
  const int n = backtrace(fr, 10);
#endif
  PortWin_NoteFrames(what, value, fr, n);
}

int g_portWinStackLog = 0;
int g_portWinTrace = 0;
static long sTraceTask = -1;
static FILE* sTraceFile = nullptr;
static unsigned long long sTraceCount = 0;
namespace {
struct TaskStackInfo { uintptr_t start, size; long index; };
std::mutex sTaskInfoLock;
std::vector<TaskStackInfo> sTaskInfo;
}
namespace {
bool trace_norm(uintptr_t a, long* task, uintptr_t* rel) {
  if (a >= g_portWinBase + PORT_WIN_STACK_OFF && a < g_portWinBase + PORT_WIN_STACK_OFF + PORT_WIN_STACK_SIZE) { }
  std::lock_guard<std::mutex> g(sTaskInfoLock);
  for (size_t k = sTaskInfo.size(); k-- > 0;) {
    const TaskStackInfo& t = sTaskInfo[k];
    if (a >= t.start && a < t.start + t.size) { *task = t.index; *rel = a - t.start; return true; }
  }
  return false;
}
}
extern "C" void PortWin_TraceHit(int kind, uintptr_t in, uintptr_t out) {
#ifdef _WIN32
  (void)kind; (void)in; (void)out;
#else
  if (sTraceFile == nullptr) return;
  char here;
  long cur = -1; uintptr_t rel0 = 0;
  trace_norm(reinterpret_cast<uintptr_t>(&here), &cur, &rel0);
  if (sTraceTask != -2 && cur != sTraceTask) return;
  if (sTraceCount > 400000000ull) return;
  ++sTraceCount;
  uintptr_t hostA = kind == 0 ? out : in;
  long t2; uintptr_t r2; char buf[96]; buf[0] = 0;
  if (trace_norm(hostA, &t2, &r2)) std::snprintf(buf, sizeof buf, "S%ld+%llx", t2, static_cast<unsigned long long>(r2));
  const unsigned long long ret = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_portWinImage);
  if (buf[0]) std::fprintf(sTraceFile, "t%ld %d %s %llx\n", cur, kind, buf, ret);
  else std::fprintf(sTraceFile, "t%ld %d %llx %llx %llx\n", cur, kind, static_cast<unsigned long long>(kind == 0 ? in : in), static_cast<unsigned long long>(kind == 0 ? out : out), ret);
#endif
}
extern "C" void PortWin_StackLogHit(uintptr_t address) {
  void* fr[10] = {};
#ifdef _WIN32
  const int n = static_cast<int>(CaptureStackBackTrace(1, 10, fr, nullptr));
#else
  const int n = backtrace(fr, 10);
#endif
  char what[160];
  long task = -2;
  {
    std::lock_guard<std::mutex> g(sTaskInfoLock);
    for (const TaskStackInfo& t : sTaskInfo) {
      if (address >= t.start && address < t.start + t.size) {
        task = t.index;
      }
    }
  }
  if (task >= 0) {
    std::snprintf(what, sizeof what, "STACKLOG a pointer into the stack of game task #%ld stored as a 4-byte value (offset 0x%llx)", task,
                  static_cast<unsigned long long>(address - g_portWinBase));
  } else {
    std::snprintf(what, sizeof what, "STACKLOG a pointer into a stack that is not a game task's (main / thread) stored as a 4-byte value (offset 0x%llx)",
                  static_cast<unsigned long long>(address - g_portWinBase));
  }
  PortWin_NoteFrames(what, address - g_portWinBase, fr, n);
}

namespace {

constexpr uint32_t kHeapOff = 0x90000000u;
constexpr size_t kHeapSize = 0x30000000u;
constexpr size_t kChunk = 1u << 20;

std::mutex sLock;
bool sReady = false;
size_t sBump = 0;
size_t sCommitted = 0;
void* sFree[40];

struct Hdr {
  uint64_t cls;
  uint64_t pad;
};
static_assert(sizeof(Hdr) == 16, "header");

uint8_t* heap_base() { return reinterpret_cast<uint8_t*>(g_portWinBase + kHeapOff); }

bool commit(size_t from, size_t to) {
#ifdef _WIN32
  return VirtualAlloc(heap_base() + from, to - from, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
  return mprotect(heap_base() + from, to - from, PROT_READ | PROT_WRITE) == 0;
#endif
}

#ifdef _WIN32
bool probe(uintptr_t b) {
  void* p = VirtualAlloc(reinterpret_cast<void*>(b), PORT_WIN_SIZE, MEM_RESERVE, PAGE_NOACCESS);
  if (p == nullptr) {
    return false;
  }
  VirtualFree(p, 0, MEM_RELEASE);
  return true;
}
#endif

}

extern "C" {

int PortWin_Active(void) { return g_portWinBase != 0; }

int PortWin_Init(void) {
  if (g_portWinBase != 0) {
    return 1;
  }
  const char* skewEnv = std::getenv("XD_WINDOW_SKEW");
  const uintptr_t skew = (skewEnv != nullptr && skewEnv[0] == '1') ? 0x01230000u : 0;
#ifdef __ANDROID__
  static const uint64_t kHi[] = {0x60, 0x50, 0x48, 0x40, 0x38, 0x30, 0x28, 0x20, 0x18, 0x10};
#else
  static const uint64_t kHi[] = {0x2000, 0x3000, 0x1800, 0x4000, 0x2800, 0x1000};
#endif
  uintptr_t chosen = 0;
  for (const uint64_t hi : kHi) {
    const uintptr_t b = static_cast<uintptr_t>(hi << 32) + skew;
#ifdef _WIN32
    if (probe(b)) {
      chosen = b;
      break;
    }
#else
    void* p = mmap(reinterpret_cast<void*>(b), PORT_WIN_SIZE, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == reinterpret_cast<void*>(b)) {
      chosen = b;
      break;
    }
    if (p != MAP_FAILED) {
      munmap(p, PORT_WIN_SIZE);
    }
#endif
  }
  if (chosen == 0) {
    std::fprintf(stderr, "[window] could not place a 4 GB window far above 4 GB\n");
    return 0;
  }
  g_portWinBase = chosen;
  if (const char* tt = std::getenv("XD_WINDOW_TRACE_TASK"); tt != nullptr) {
    sTraceTask = std::strtol(tt, nullptr, 0);
    const char* tf = std::getenv("XD_WINDOW_TRACE_FILE");
    sTraceFile = std::fopen(tf != nullptr ? tf : "window_trace.txt", "w");
    if (sTraceFile != nullptr) setvbuf(sTraceFile, nullptr, _IOLBF, 0);
    g_portWinTrace = sTraceFile != nullptr;
  }
  if (const char* sl = std::getenv("XD_WINDOW_STACKLOG"); sl != nullptr && sl[0] == '1') {
    g_portWinStackLog = 1;
    g_portWinStrict = g_portWinStrict ? g_portWinStrict : 0;
  }
  if (const char* strict = std::getenv("XD_WINDOW_STRICT"); strict != nullptr && (strict[0] == '1' || strict[0] == '2')) {
    g_portWinStrict = strict[0] - '0';
  }
#ifdef _WIN32
  g_portWinImage = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
#elif defined(__APPLE__)
  g_portWinImage = reinterpret_cast<uintptr_t>(&_mh_execute_header);
#elif defined(__ANDROID__)
  {
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&PortWin_Init), &info) == 0 || info.dli_fbase == nullptr) {
      std::fprintf(stderr, "[window] could not find where the game library is loaded (dladdr)\n");
      return 0;
    }
    g_portWinImage = reinterpret_cast<uintptr_t>(info.dli_fbase);
    if (g_portWinImage < PORT_WIN_STACK_OFF) {
      g_portWinImgLo = static_cast<uint32_t>(g_portWinImage);
      g_portWinImgSize = PORT_WIN_STACK_OFF - g_portWinImgLo < PORT_WIN_IMG_SIZE ? PORT_WIN_STACK_OFF - g_portWinImgLo : PORT_WIN_IMG_SIZE;
    }
  }
#else
  g_portWinImage = reinterpret_cast<uintptr_t>(&__executable_start);
#endif
  std::fprintf(stderr, "[window] 4 GB window at 0x%llx (%s), image at 0x%llx = value 0x%x\n",
               static_cast<unsigned long long>(g_portWinBase), skew ? "skewed: low 32 bits differ" : "4 GB aligned",
               static_cast<unsigned long long>(g_portWinImage), g_portWinImgLo, g_portWinImgSize);
  std::fflush(stderr);
  return 1;
}

#ifndef _WIN32
static std::mutex sStackLock;
static size_t sStackBump = 0;
static std::vector<std::pair<size_t, void*>> sStackFreed;

void* PortWin_StackMap(size_t bytes) {
  if (g_portWinBase == 0) {
    return nullptr;
  }

  std::lock_guard<std::mutex> g(sStackLock);
  static const bool noReuse = std::getenv("XD_WINDOW_STACK_NOREUSE") != nullptr;
  for (size_t i = 0; i < sStackFreed.size() && !noReuse; ++i) {
    if (sStackFreed[i].first == bytes) {
      void* p = sStackFreed[i].second;
      sStackFreed.erase(sStackFreed.begin() + static_cast<std::ptrdiff_t>(i));
      mprotect(p, bytes, PROT_READ | PROT_WRITE);
      std::memset(p, std::getenv("XD_WINDOW_STACK_FILL") != nullptr ? static_cast<int>(std::strtol(std::getenv("XD_WINDOW_STACK_FILL"), nullptr, 0)) : 0, bytes);
      return p;
    }
  }
  static const size_t skip = [] { const char* e = std::getenv("XD_WINDOW_STACK_SKIP"); return e != nullptr ? static_cast<size_t>(std::strtoull(e, nullptr, 0)) : size_t{0}; }();
  if (sStackBump == 0) {
    sStackBump = (skip + 0xFFFF) & ~static_cast<size_t>(0xFFFF);
  }
  const size_t aligned = (bytes + 0xFFFF) & ~static_cast<size_t>(0xFFFF);
  if (sStackBump + aligned > PORT_WIN_STACK_SIZE) {
    return nullptr;
  }
  void* p = reinterpret_cast<void*>(g_portWinBase + PORT_WIN_STACK_OFF + sStackBump);
  if (mprotect(p, bytes, PROT_READ | PROT_WRITE) != 0) {
    return nullptr;
  }
  sStackBump += aligned;
  static const int fill = [] { const char* e = std::getenv("XD_WINDOW_STACK_FILL"); return e != nullptr ? static_cast<int>(std::strtol(e, nullptr, 0)) : 0; }();
  if (fill != 0) {
    std::memset(p, fill, bytes);
  }
  return p;
}

int PortWin_StackKindOn(int kind) {
  static const int mask = [] {
    const char* e = std::getenv("XD_WINDOW_STACKS");
    return e != nullptr ? static_cast<int>(std::strtol(e, nullptr, 0)) : 7;
  }();
  return (mask & kind) != 0;
}

void* PortWin_StackMapKind(size_t bytes, int kind) {
  return PortWin_StackKindOn(kind) ? PortWin_StackMap(bytes) : nullptr;
}

void PortWin_StackUnmap(void* p, size_t bytes) {
  if (p == nullptr || g_portWinBase == 0) {
    return;
  }
  std::lock_guard<std::mutex> g(sStackLock);
  sStackFreed.emplace_back(bytes, p);
}

static bool in_stack_range(void* p) {
  const uintptr_t a = reinterpret_cast<uintptr_t>(p);
  return a >= g_portWinBase + PORT_WIN_STACK_OFF && a < g_portWinBase + PORT_WIN_STACK_OFF + PORT_WIN_STACK_SIZE;
}

#ifndef MAP_32BIT
#define MAP_32BIT 0
#endif
#ifndef MAP_STACK
#define MAP_STACK 0
#endif
void* PortWin_TaskStackMap(size_t bytes) {
  static const long limit = [] { const char* e = std::getenv("XD_WINDOW_TASKS_UNTIL"); return e != nullptr ? std::strtol(e, nullptr, 0) : -1L; }();
  static const bool log = std::getenv("XD_WINDOW_TASKS_LOG") != nullptr;
  static long count = 0;
  const long i = count++;
  if (log) {
    void* fr[8];
    const int n = backtrace(fr, 8);
    std::fprintf(stderr, "[window] task stack #%ld (%zu bytes), %s; created by:\n", i, bytes, (limit >= 0 && i >= limit) ? "OLD low mapping" : "window");
#ifndef __ANDROID__
    backtrace_symbols_fd(fr, n, 2);
#else
    (void)fr; (void)n;
#endif
  }
  static const long only = [] { const char* e = std::getenv("XD_WINDOW_TASK_OLD"); return e != nullptr ? std::strtol(e, nullptr, 0) : -1L; }();
  static const bool high = std::getenv("XD_WINDOW_TASKS_HIGH") != nullptr;
  static const long highOne = [] { const char* e = std::getenv("XD_WINDOW_TASK_HIGH_ONE"); return e != nullptr ? std::strtol(e, nullptr, 0) : -1L; }();
  if (high || i == highOne || (limit >= 0 && i >= limit) || i == only) {
    void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | ((high || i == highOne) ? 0 : MAP_32BIT) | MAP_STACK, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    if (log) std::fprintf(stderr, "[window] task stack #%ld at %p (native mapping)\n", i, p);
    std::lock_guard<std::mutex> g(sTaskInfoLock);
    bool found = false;
    for (TaskStackInfo& t : sTaskInfo) {
      if (t.start == reinterpret_cast<uintptr_t>(p)) { t.size = bytes; t.index = i; found = true; }
    }
    if (!found) sTaskInfo.push_back({reinterpret_cast<uintptr_t>(p), bytes, i});
    return p;
  }
  void* q = PortWin_StackMap(bytes);
  if (log) std::fprintf(stderr, "[window] task stack #%ld at %p (window)\n", i, q);
  if (q != nullptr) {
    std::lock_guard<std::mutex> g(sTaskInfoLock);
    bool found = false;
    for (TaskStackInfo& t : sTaskInfo) {
      if (t.start == reinterpret_cast<uintptr_t>(q)) { t.size = bytes; t.index = i; found = true; }
    }
    if (!found) sTaskInfo.push_back({reinterpret_cast<uintptr_t>(q), bytes, i});
  }
  return q;
}

void PortWin_TaskStackUnmap(void* p, size_t bytes) {
  if (p != nullptr && !in_stack_range(p)) {
    munmap(p, bytes);
    return;
  }
  PortWin_StackUnmap(p, bytes);
}
#endif

void* PortWin_At(uint32_t offset) { return g_portWinBase == 0 ? nullptr : reinterpret_cast<void*>(g_portWinBase + offset); }

static int class_of(size_t bytes) {
  int c = 5;
  while ((static_cast<size_t>(1) << c) < bytes) {
    ++c;
  }
  return c;
}

#ifndef _WIN32
void* PortWin_Malloc(size_t size) {
  if (g_portWinBase == 0) {
    return std::malloc(size ? size : 1);
  }
  {
    std::lock_guard<std::mutex> g(sLock);
    if (!sReady) {
      PortLow_UseRange(g_portWinBase + kHeapOff, g_portWinBase + kHeapOff + kHeapSize);
      sReady = true;
    }
  }
  return PortLow_Malloc(size ? size : 1);
}

void* PortWin_Calloc(size_t count, size_t size) {
  if (g_portWinBase == 0) {
    return std::calloc(count ? count : 1, size ? size : 1);
  }
  void* p = PortWin_Malloc(count * size);
  std::memset(p, 0, count * size);
  return p;
}

int PortWin_Owns(const void* p) {
  return g_portWinBase != 0 && PortLow_Owns(p);
}

void PortWin_Free(void* p) {
  if (p == nullptr) {
    return;
  }
  if (!PortWin_Owns(p)) {
    std::free(p);
    return;
  }
  PortLow_Free(p);
}

void* PortWin_Realloc(void* p, size_t size) {
  if (p == nullptr) {
    return PortWin_Malloc(size);
  }
  if (!PortWin_Owns(p)) {
    return std::realloc(p, size);
  }
  return PortLow_Realloc(p, size ? size : 1);
}
#else
void* PortWin_Malloc(size_t size) {
  if (g_portWinBase == 0) {
    return std::malloc(size ? size : 1);
  }
  std::lock_guard<std::mutex> g(sLock);
  if (!sReady) {
#ifdef _WIN32
    if (VirtualAlloc(heap_base(), kHeapSize, MEM_RESERVE, PAGE_NOACCESS) == nullptr) {
      PortWin_Fail("cannot reserve the window heap", reinterpret_cast<uintptr_t>(heap_base()));
    }
#endif
    sReady = true;
  }
  const int c = class_of(size + sizeof(Hdr));
  if (c >= 40) {
    PortWin_Fail("window heap: request too big", size);
  }
  Hdr* h;
  if (sFree[c] != nullptr) {
    h = static_cast<Hdr*>(sFree[c]);
    sFree[c] = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(h) + sizeof(Hdr));
  } else {
    const size_t bytes = static_cast<size_t>(1) << c;
    const size_t start = (sBump + 15) & ~static_cast<size_t>(15);
    if (start + bytes > kHeapSize) {
      std::fprintf(stderr, "[window] OUT OF MEMORY IN THE WINDOW HEAP: a request of %zu bytes, %zu used\n", size, sBump);
      std::fflush(stderr);
      std::abort();
    }
    if (start + bytes > sCommitted) {
      const size_t to = std::min(kHeapSize, (start + bytes + kChunk - 1) & ~(kChunk - 1));
      if (!commit(sCommitted, to)) {
        PortWin_Fail("cannot commit window heap memory", to);
      }
      sCommitted = to;
    }
    h = reinterpret_cast<Hdr*>(heap_base() + start);
    h->cls = static_cast<uint64_t>(c);
    sBump = start + bytes;
  }
  static const bool zeroFill = std::getenv("XD_WINDOW_HEAP_ZERO") != nullptr;
  if (zeroFill) {
    std::memset(reinterpret_cast<uint8_t*>(h) + sizeof(Hdr), 0, (static_cast<size_t>(1) << c) - sizeof(Hdr));
  }
  return reinterpret_cast<uint8_t*>(h) + sizeof(Hdr);
}

void* PortWin_Calloc(size_t count, size_t size) {
  const size_t n = count * size;
  void* p = PortWin_Malloc(n);
  std::memset(p, 0, n);
  return p;
}

int PortWin_Owns(const void* p) {
  const uintptr_t a = reinterpret_cast<uintptr_t>(p);
  return g_portWinBase != 0 && a >= g_portWinBase + kHeapOff && a < g_portWinBase + kHeapOff + kHeapSize;
}

void PortWin_Free(void* p) {
  if (p == nullptr) {
    return;
  }
  if (!PortWin_Owns(p)) {
    std::free(p);
    return;
  }
  std::lock_guard<std::mutex> g(sLock);
  Hdr* h = reinterpret_cast<Hdr*>(static_cast<uint8_t*>(p) - sizeof(Hdr));
  const int c = static_cast<int>(h->cls);
  *reinterpret_cast<void**>(p) = sFree[c];
  sFree[c] = h;
}

void* PortWin_Realloc(void* p, size_t size) {
  if (p == nullptr) {
    return PortWin_Malloc(size);
  }
  if (!PortWin_Owns(p)) {
    return std::realloc(p, size);
  }
  Hdr* h = reinterpret_cast<Hdr*>(static_cast<uint8_t*>(p) - sizeof(Hdr));
  const size_t have = (static_cast<size_t>(1) << h->cls) - sizeof(Hdr);
  if (size <= have) {
    return p;
  }
  void* q = PortWin_Malloc(size);
  std::memcpy(q, p, have);
  PortWin_Free(p);
  return q;
}

#endif

}

#endif
