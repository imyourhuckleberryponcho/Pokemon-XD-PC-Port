#include "gsthread_coro.h"
#include "gsthread_port.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr int kMaxTasks = 24;
constexpr size_t kSlotSize = 0x80000;
constexpr size_t kRestorerStack = 0x20000;
constexpr size_t kMainStack = 0x400000;
constexpr size_t kCtxBuf = 256;

struct Limits {
  void* base = nullptr;
  void* limit = nullptr;
  void* dealloc = nullptr;
};

struct CTask {
  void* key = nullptr;
  bool started = false;
  PortGSthreadEntry entry = nullptr;
  PortGSthreadExit exitFunc = nullptr;
  uintptr_t args[PORT_GSTHREAD_MAX_ARGS] = {};
  void* sp = nullptr;
};

struct Core {
  CTask tasks[kMaxTasks];
  void* schedSp = nullptr;
  Limits schedLim;
  bool schedKnown = false;
  int current = -1;
  int starting = -1;
};

bool sOn = false;
uint8_t* sArena = nullptr;
Core sCore;
void* sDummySp = nullptr;
const gscoro::Snapshot* sRestoring = nullptr;
uint8_t* sRestorer = nullptr;
uint8_t* sMainStack = nullptr;
void (*sMainFn)(void*) = nullptr;
void* sMainArg = nullptr;
void* sHostSp = nullptr;

[[noreturn]] void fatal(const char* what) {
  std::fprintf(stderr, "[engine/gs] GSthread (coroutines): %s\n", what);
  std::fflush(stderr);
  std::abort();
}

NT_TIB* tib() { return reinterpret_cast<NT_TIB*>(NtCurrentTeb()); }
constexpr size_t kDeallocOffset = 0x1478;

Limits get_limits() {
  Limits l;
  l.base = tib()->StackBase;
  l.limit = tib()->StackLimit;
  l.dealloc = *reinterpret_cast<void**>(reinterpret_cast<char*>(NtCurrentTeb()) + kDeallocOffset);
  return l;
}

void set_limits(const Limits& l) {
  tib()->StackBase = l.base;
  tib()->StackLimit = l.limit;
  *reinterpret_cast<void**>(reinterpret_cast<char*>(NtCurrentTeb()) + kDeallocOffset) = l.dealloc;
}

uint8_t* slot_lo(int i) { return sArena + static_cast<size_t>(i) * kSlotSize; }
uint8_t* slot_top(int i) { return slot_lo(i) + kSlotSize; }
Limits slot_limits(int i) {
  Limits l;
  l.base = slot_top(i);
  l.limit = slot_lo(i);
  l.dealloc = slot_lo(i);
  return l;
}

int find(void* key) {
  for (int i = 0; i < kMaxTasks; i++) {
    if (sCore.tasks[i].key == key) return i;
  }
  return -1;
}

typedef void(__fastcall* SwapFn)(void**, void*);
typedef int(__fastcall* SaveFn)(void*);
typedef void(__fastcall* ResumeFn)(void*);
SwapFn ctx_swap;
SaveFn ctx_save;
ResumeFn ctx_resume;

void make_code() {
  std::vector<uint8_t> push = {0x55, 0x53, 0x57, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                               0x48, 0x81, 0xEC, 0xA0, 0, 0, 0};
  for (int n = 6; n <= 15; n++) {
    if (n >= 8) push.push_back(0x44);
    push.insert(push.end(), {0x0F, 0x11, static_cast<uint8_t>(0x84 | ((n & 7) << 3)), 0x24, static_cast<uint8_t>((n - 6) * 16), 0, 0, 0});
  }
  std::vector<uint8_t> pop;
  for (int n = 6; n <= 15; n++) {
    if (n >= 8) pop.push_back(0x44);
    pop.insert(pop.end(), {0x0F, 0x10, static_cast<uint8_t>(0x84 | ((n & 7) << 3)), 0x24, static_cast<uint8_t>((n - 6) * 16), 0, 0, 0});
  }
  pop.insert(pop.end(), {0x48, 0x81, 0xC4, 0xA0, 0, 0, 0,
                         0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5E, 0x5F, 0x5B, 0x5D});
  std::vector<uint8_t> code;
  auto add = [&](const std::vector<uint8_t>& v) { code.insert(code.end(), v.begin(), v.end()); };
  const size_t swapAt = code.size();
  add(push);
  add({0x48, 0x89, 0x21});
  add({0x48, 0x89, 0xD4});
  add(pop);
  add({0xC3});
  const size_t saveAt = code.size();
  add({0x48, 0x89, 0x69, 0x00, 0x48, 0x89, 0x59, 0x08, 0x48, 0x89, 0x79, 0x10, 0x48, 0x89, 0x71, 0x18,
       0x4C, 0x89, 0x61, 0x20, 0x4C, 0x89, 0x69, 0x28, 0x4C, 0x89, 0x71, 0x30, 0x4C, 0x89, 0x79, 0x38});
  add({0x48, 0x8B, 0x04, 0x24, 0x48, 0x89, 0x41, 0x40});
  add({0x48, 0x8D, 0x44, 0x24, 0x08, 0x48, 0x89, 0x41, 0x48});
  for (int n = 6; n <= 15; n++) {
    if (n >= 8) code.push_back(0x44);
    code.insert(code.end(), {0x0F, 0x11, static_cast<uint8_t>(0x81 | ((n & 7) << 3)), static_cast<uint8_t>(80 + (n - 6) * 16), 0, 0, 0});
  }
  add({0x31, 0xC0, 0xC3});
  const size_t resumeAt = code.size();
  add({0x48, 0x8B, 0x69, 0x00, 0x48, 0x8B, 0x59, 0x08, 0x48, 0x8B, 0x79, 0x10, 0x48, 0x8B, 0x71, 0x18,
       0x4C, 0x8B, 0x61, 0x20, 0x4C, 0x8B, 0x69, 0x28, 0x4C, 0x8B, 0x71, 0x30, 0x4C, 0x8B, 0x79, 0x38});
  for (int n = 6; n <= 15; n++) {
    if (n >= 8) code.push_back(0x44);
    code.insert(code.end(), {0x0F, 0x10, static_cast<uint8_t>(0x81 | ((n & 7) << 3)), static_cast<uint8_t>(80 + (n - 6) * 16), 0, 0, 0});
  }
  add({0x48, 0x8B, 0x61, 0x48, 0x48, 0x8B, 0x51, 0x40});
  add({0xB8, 1, 0, 0, 0, 0xFF, 0xE2});
  uint8_t* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (mem == nullptr) fatal("no executable memory for the switch");
  std::memcpy(mem, code.data(), code.size());
  FlushInstructionCache(GetCurrentProcess(), mem, code.size());
  ctx_swap = reinterpret_cast<SwapFn>(mem + swapAt);
  ctx_save = reinterpret_cast<SaveFn>(mem + saveAt);
  ctx_resume = reinterpret_cast<ResumeFn>(mem + resumeAt);
}

void* prepare_stack(uint8_t* top, void (*fn)()) {
  uintptr_t top16 = reinterpret_cast<uintptr_t>(top) & ~uintptr_t{15};
  uint64_t* ret = reinterpret_cast<uint64_t*>(top16 - 0x30);
  *ret = reinterpret_cast<uint64_t>(fn);
  *(ret + 1) = 0;
  uint8_t* sp = reinterpret_cast<uint8_t*>(ret) - 8 * 8 - 0xA0;
  std::memset(sp, 0, 8 * 8 + 0xA0);
  return sp;
}

constexpr uint64_t kCanary = 0xC0FFEE5AFEC0DE01ull;
constexpr size_t kCanaryBytes = 256;
void canary_set(int i) {
  uint64_t* p = reinterpret_cast<uint64_t*>(slot_lo(i));
  for (size_t k = 0; k < kCanaryBytes / 8; k++) p[k] = kCanary;
}
void canary_check(int i, const char* when) {
  const uint64_t* p = reinterpret_cast<const uint64_t*>(slot_lo(i));
  for (size_t k = 0; k < kCanaryBytes / 8; k++) {
    if (p[k] != kCanary) {
      std::fprintf(stderr, "[engine/gs] GSthread (coroutines): the stack of task row %d overflowed (%s)\n", i, when);
      std::fflush(stderr);
      std::abort();
    }
  }
}

__declspec(noinline) void switch_to(void** from, void* to, const Limits& toLimits) {
  set_limits(toLimits);
  ctx_swap(from, to);
}

void start_function() {
  CTask& t = sCore.tasks[sCore.starting];
  const uintptr_t* a = t.args;
  const uintptr_t result = t.entry(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
  t.exitFunc(static_cast<uint32_t>(result));
  fatal("a task ran on after its exit function");
}

}

namespace gscoro {

bool UseCoroutines(void* (*alloc)(size_t size, size_t align)) {
  if (sOn) return true;
  if (alloc == nullptr) return false;
  sArena = static_cast<uint8_t*>(alloc(kSlotSize * kMaxTasks + kRestorerStack + kMainStack, 4096));
  if (sArena == nullptr) return false;
  sRestorer = sArena + kSlotSize * kMaxTasks;
  sMainStack = sRestorer + kRestorerStack;
  make_code();
  sOn = true;
  return true;
}

bool On() { return sOn; }

size_t LiveTasks() {
  size_t n = 0;
  for (const CTask& t : sCore.tasks) n += t.key != nullptr ? 1 : 0;
  return n;
}

namespace {
struct Tail {
  Limits cur;
  alignas(16) uint8_t ctx[kCtxBuf];
};

__declspec(noinline) void gather(Snapshot& s, const uint8_t* ctx) {
  void* curSp = *reinterpret_cast<void* const*>(ctx + 72);
  const Limits lim = get_limits();
  s.core.resize(sizeof(Core) + sizeof(Tail));
  std::memcpy(s.core.data(), &sCore, sizeof(Core));
  Tail tail{};
  tail.cur = lim;
  std::memcpy(tail.ctx, ctx, kCtxBuf);
  std::memcpy(s.core.data() + sizeof(Core), &tail, sizeof(Tail));
  s.stacks.clear();
  auto keep = [&](uint8_t* from, uint8_t* to) {
    if (to <= from) return;
    Piece p;
    p.address = from;
    p.bytes.assign(from, to);
    s.stacks.push_back(std::move(p));
  };
  keep(static_cast<uint8_t*>(curSp), static_cast<uint8_t*>(lim.base));
  if (sCore.current >= 0) keep(static_cast<uint8_t*>(sCore.schedSp), static_cast<uint8_t*>(sCore.schedLim.base));
  s.tasks = 0;
  for (int i = 0; i < kMaxTasks; i++) {
    const CTask& t = sCore.tasks[i];
    if (t.key == nullptr) continue;
    s.tasks++;
    if (i == sCore.current || !t.started) continue;
    keep(static_cast<uint8_t*>(t.sp), slot_top(i));
  }
  s.inTask = sCore.current >= 0;
}

void restorer_main() {
  const Snapshot& s = *sRestoring;
  for (const Piece& p : s.stacks) std::memcpy(p.address, p.bytes.data(), p.bytes.size());
  std::memcpy(&sCore, s.core.data(), sizeof(Core));
  for (int i = 0; i < kMaxTasks; i++)
    if (sCore.tasks[i].key != nullptr && sCore.tasks[i].started) canary_set(i);
  Tail tail;
  std::memcpy(&tail, s.core.data() + sizeof(Core), sizeof(Tail));
  set_limits(tail.cur);
  ctx_resume(tail.ctx);
  fatal("ctx_resume returned");
}
}

namespace {
Limits& host_limits() {
  static Limits l;
  return l;
}
void main_start() {
  sMainFn(sMainArg);
  set_limits(host_limits());
  void* dummy = nullptr;
  ctx_swap(&dummy, sHostSp);
  fatal("the main stack ran on after its host took it back");
}
}

void RunOnMainStack(void (*fn)(void*), void* arg) {
  if (!sOn) fatal("RunOnMainStack before UseCoroutines");
  sMainFn = fn;
  sMainArg = arg;
  host_limits() = get_limits();
  uint64_t* p = reinterpret_cast<uint64_t*>(sMainStack);
  for (size_t k = 0; k < kCanaryBytes / 8; k++) p[k] = kCanary;
  void* sp = prepare_stack(sMainStack + kMainStack, main_start);
  Limits l;
  l.base = sMainStack + kMainStack;
  l.limit = sMainStack;
  l.dealloc = sMainStack;
  set_limits(l);
  ctx_swap(&sHostSp, sp);
}

void StackBlock(uint8_t** begin, size_t* size) {
  *begin = sArena;
  *size = sArena == nullptr ? 0 : kSlotSize * kMaxTasks + kRestorerStack + kMainStack;
}

void ToBytes(const Snapshot& s, uintptr_t cookie, std::vector<uint8_t>& out) {
  auto put = [&](const void* p, size_t n) {
    const uint8_t* c = static_cast<const uint8_t*>(p);
    out.insert(out.end(), c, c + n);
  };
  auto put64 = [&](uint64_t v) { put(&v, 8); };
  put("GSN1", 4);
  put64(cookie);
  put64(reinterpret_cast<uintptr_t>(sArena));
  put64(kSlotSize * kMaxTasks + kRestorerStack + kMainStack);
  put64(reinterpret_cast<uintptr_t>(sMainStack));
  put64(s.tasks);
  put64(s.inTask ? 1 : 0);
  put64(s.core.size());
  put(s.core.data(), s.core.size());
  put64(s.stacks.size());
  for (const Piece& p : s.stacks) {
    put64(reinterpret_cast<uintptr_t>(p.address));
    put64(p.bytes.size());
    put(p.bytes.data(), p.bytes.size());
  }
}

bool FromBytes(const uint8_t* data, size_t size, uintptr_t currentCookie, Snapshot& s, size_t* rewritten, uintptr_t* fileCookie,
               const char** why) {
  size_t pos = 0;
  auto bad = [&](const char* text) {
    if (why != nullptr) *why = text;
    return false;
  };
  auto get64 = [&](uint64_t& v) {
    if (pos + 8 > size) return false;
    std::memcpy(&v, data + pos, 8);
    pos += 8;
    return true;
  };
  if (size < 4 || std::memcmp(data, "GSN1", 4) != 0) return bad("the task part of the file is not in this program's format");
  pos = 4;
  uint64_t cookie, arena, arenaSize, mainStack, tasks, inTask, coreSize, pieces;
  if (!get64(cookie) || !get64(arena) || !get64(arenaSize) || !get64(mainStack) || !get64(tasks) || !get64(inTask) ||
      !get64(coreSize))
    return bad("the task part of the file ends too early");
  if (arena != reinterpret_cast<uintptr_t>(sArena) || arenaSize != kSlotSize * kMaxTasks + kRestorerStack + kMainStack ||
      mainStack != reinterpret_cast<uintptr_t>(sMainStack))
    return bad("the game's task stacks lie at other addresses in this process than in the one that saved");
  if (coreSize > size - pos) return bad("the task part of the file ends too early");
  s.core.assign(data + pos, data + pos + coreSize);
  pos += static_cast<size_t>(coreSize);
  s.tasks = static_cast<size_t>(tasks);
  s.inTask = inTask != 0;
  if (!get64(pieces) || pieces > 4096) return bad("the task part of the file is damaged");
  s.stacks.clear();
  size_t fixed = 0;
  for (uint64_t i = 0; i < pieces; i++) {
    uint64_t address, n;
    if (!get64(address) || !get64(n) || n > size - pos) return bad("the task part of the file ends too early");
    if (address < arena || address + n > arena + arenaSize) return bad("a stack piece of the file lies outside the stacks");
    Piece p;
    p.address = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
    p.bytes.assign(data + pos, data + pos + n);
    pos += static_cast<size_t>(n);
    if (cookie != currentCookie) {
      for (size_t k = 0; k < p.bytes.size() / 8; k++) {
        uint64_t v;
        std::memcpy(&v, p.bytes.data() + k * 8, 8);
        const uint64_t x = v ^ cookie;
        if (x >= arena && x < arena + arenaSize) {
          const uint64_t r = x ^ currentCookie;
          std::memcpy(p.bytes.data() + k * 8, &r, 8);
          fixed++;
        }
      }
    }
    s.stacks.push_back(std::move(p));
  }
  if (rewritten != nullptr) *rewritten = fixed;
  if (fileCookie != nullptr) *fileCookie = static_cast<uintptr_t>(cookie);
  return true;
}

__declspec(noinline) int Take(Snapshot& s) {
  alignas(16) static uint8_t buf[kCtxBuf];
  if (ctx_save(buf) == 0) {
    gather(s, buf);
    return 0;
  }
  return 1;
}

[[noreturn]] void Restore(const Snapshot& s) {
  sRestoring = &s;
  void* sp = prepare_stack(sRestorer + kRestorerStack, restorer_main);
  Limits l;
  l.base = sRestorer + kRestorerStack;
  l.limit = sRestorer;
  l.dealloc = sRestorer;
  set_limits(l);
  ctx_swap(&sDummySp, sp);
  fatal("the restorer returned");
}

void Create(void* thread, void* entry, PortGSthreadExit exitFunc, uint32_t stackSize) {
  const size_t need = static_cast<size_t>(stackSize) * 8;
  if (need > kSlotSize) fatal("a task wants a bigger stack than the slots give");
  int i = find(thread);
  if (i >= 0) {
    sCore.tasks[i] = CTask{};
  }
  for (i = 0; i < kMaxTasks && sCore.tasks[i].key != nullptr; i++) {
  }
  if (i == kMaxTasks) fatal("more tasks than rows");
  CTask& t = sCore.tasks[i];
  t = CTask{};
  t.key = thread;
  t.entry = reinterpret_cast<PortGSthreadEntry>(entry);
  t.exitFunc = exitFunc;
}

void SetArg(void* thread, int index, uintptr_t value) {
  const int i = find(thread);
  if (i < 0 || index < 0 || index >= PORT_GSTHREAD_MAX_ARGS) fatal("start-up value for an unknown task, or too many");
  sCore.tasks[i].args[index] = value;
}

void Run(void* thread) {
  const int i = find(thread);
  if (i < 0) fatal("run of an unknown task");
  CTask& t = sCore.tasks[i];
  if (sCore.current >= 0) {
    static int sNested = 0;
    if (sNested++ < 20) std::fprintf(stderr, "[engine/gs] GSthread (coroutines): Run of row %d while row %d runs (nested scheduler)\n", i, sCore.current);
  }
  sCore.schedLim = get_limits();
  sCore.schedKnown = true;
  if (!t.started) {
    t.started = true;
    canary_set(i);
    t.sp = prepare_stack(slot_top(i), start_function);
    sCore.starting = i;
  }
  sCore.current = i;
  switch_to(&sCore.schedSp, t.sp, slot_limits(i));
  sCore.current = -1;
  canary_check(i, "task switched out");
}

void SwitchToScheduler(void (*hook)()) {
  if (sCore.current < 0) fatal("switch with no task running");
  if (hook != nullptr) hook();
  CTask& t = sCore.tasks[sCore.current];
  switch_to(&t.sp, sCore.schedSp, sCore.schedLim);
}

void Destroy(void* thread) {
  const int i = find(thread);
  if (i < 0) return;
  if (i == sCore.current) fatal("a task destroyed itself while running");
  sCore.tasks[i] = CTask{};
}

}
