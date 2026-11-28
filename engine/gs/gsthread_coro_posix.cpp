#include "gsthread_coro.h"
#include "gsthread_port.h"
#include "port_ctx.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__APPLE__)
extern "C" uintptr_t __stack_chk_guard;
#endif

extern "C" int PortCoro_Save(void* buffer) __attribute__((returns_twice));
extern "C" void PortCoro_Resume(void* buffer) __attribute__((noreturn));

namespace {

constexpr int kMaxTasks = 24;
constexpr size_t kSlotSize = 0x80000;
constexpr size_t kRestorerStack = 0x20000;
constexpr size_t kMainStack = 0x400000;
constexpr size_t kCtxBuf = 256;

#if defined(__x86_64__)
constexpr size_t kSpOffset = 56;
#elif defined(__aarch64__)
constexpr size_t kSpOffset = 96;
#else
#error "gsthread_coro_posix: only x86_64 and arm64 are written"
#endif

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
  void* schedBase = nullptr;
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

uint8_t* slot_lo(int i) { return sArena + static_cast<size_t>(i) * kSlotSize; }
uint8_t* slot_top(int i) { return slot_lo(i) + kSlotSize; }

int find(void* key) {
  for (int i = 0; i < kMaxTasks; i++) {
    if (sCore.tasks[i].key == key) return i;
  }
  return -1;
}

#ifdef __APPLE__
#define CORO_SAVE "_PortCoro_Save"
#define CORO_RESUME "_PortCoro_Resume"
#define CORO_TYPE(name, kind)
#else
#define CORO_SAVE "PortCoro_Save"
#define CORO_RESUME "PortCoro_Resume"
#define CORO_TYPE(name, kind) ".type " name "," kind "\n"
#endif

#if defined(__x86_64__)
__asm__(".text\n"
        ".globl " CORO_SAVE "\n" CORO_TYPE(CORO_SAVE, "@function") CORO_SAVE ":\n"
        "  movq %rbx, 0(%rdi)\n"
        "  movq %rbp, 8(%rdi)\n"
        "  movq %r12, 16(%rdi)\n"
        "  movq %r13, 24(%rdi)\n"
        "  movq %r14, 32(%rdi)\n"
        "  movq %r15, 40(%rdi)\n"
        "  movq (%rsp), %rax\n"
        "  movq %rax, 48(%rdi)\n"
        "  leaq 8(%rsp), %rax\n"
        "  movq %rax, 56(%rdi)\n"
        "  xorl %eax, %eax\n"
        "  ret\n"
        ".globl " CORO_RESUME "\n" CORO_TYPE(CORO_RESUME, "@function") CORO_RESUME ":\n"
        "  movq 0(%rdi), %rbx\n"
        "  movq 8(%rdi), %rbp\n"
        "  movq 16(%rdi), %r12\n"
        "  movq 24(%rdi), %r13\n"
        "  movq 32(%rdi), %r14\n"
        "  movq 40(%rdi), %r15\n"
        "  movq 56(%rdi), %rsp\n"
        "  movq 48(%rdi), %rdx\n"
        "  movl $1, %eax\n"
        "  jmp *%rdx\n");
#elif defined(__aarch64__)
__asm__(".text\n"
        ".globl " CORO_SAVE "\n" CORO_TYPE(CORO_SAVE, "%function") CORO_SAVE ":\n"
        "  stp x19, x20, [x0, #0]\n"
        "  stp x21, x22, [x0, #16]\n"
        "  stp x23, x24, [x0, #32]\n"
        "  stp x25, x26, [x0, #48]\n"
        "  stp x27, x28, [x0, #64]\n"
        "  stp x29, x30, [x0, #80]\n"
        "  mov x2, sp\n"
        "  str x2, [x0, #96]\n"
        "  stp d8, d9, [x0, #104]\n"
        "  stp d10, d11, [x0, #120]\n"
        "  stp d12, d13, [x0, #136]\n"
        "  stp d14, d15, [x0, #152]\n"
        "  mov w0, #0\n"
        "  ret\n"
        ".globl " CORO_RESUME "\n" CORO_TYPE(CORO_RESUME, "%function") CORO_RESUME ":\n"
        "  ldp x19, x20, [x0, #0]\n"
        "  ldp x21, x22, [x0, #16]\n"
        "  ldp x23, x24, [x0, #32]\n"
        "  ldp x25, x26, [x0, #48]\n"
        "  ldp x27, x28, [x0, #64]\n"
        "  ldp x29, x30, [x0, #80]\n"
        "  ldr x2, [x0, #96]\n"
        "  mov sp, x2\n"
        "  ldp d8, d9, [x0, #104]\n"
        "  ldp d10, d11, [x0, #120]\n"
        "  ldp d12, d13, [x0, #136]\n"
        "  ldp d14, d15, [x0, #152]\n"
        "  mov w0, #1\n"
        "  ret\n");
#endif

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

void* current_stack_top() {
  const uint8_t* sp = static_cast<const uint8_t*>(__builtin_frame_address(0));
  if (sp >= sMainStack && sp < sMainStack + kMainStack) return sMainStack + kMainStack;
  for (int i = 0; i < kMaxTasks; i++) {
    if (sp >= slot_lo(i) && sp < slot_top(i)) return slot_top(i);
  }
  if (sp >= sRestorer && sp < sRestorer + kRestorerStack) return sRestorer + kRestorerStack;
  return nullptr;
}

__attribute__((noinline)) void switch_to(void** from, void* to) { PortCtx_Swap(from, to); }

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
  sOn = true;
  return true;
}

bool On() { return sOn; }

size_t LiveTasks() {
  size_t n = 0;
  for (const CTask& t : sCore.tasks) n += t.key != nullptr ? 1 : 0;
  return n;
}

uintptr_t StackGuardGet() {
#if defined(__APPLE__)
  return __stack_chk_guard;
#elif defined(__x86_64__) && defined(__linux__)
  uintptr_t v;
  __asm__ volatile("movq %%fs:0x28, %0" : "=r"(v));
  return v;
#else
  return 0;
#endif
}

void StackGuardSet(uintptr_t value) {
#if defined(__APPLE__)
  (void)value;
#elif defined(__x86_64__) && defined(__linux__)
  __asm__ volatile("movq %0, %%fs:0x28" : : "r"(value));
#else
  (void)value;
#endif
}

namespace {
struct Tail {
  void* base;
  alignas(16) uint8_t ctx[kCtxBuf];
};

__attribute__((noinline)) void gather(Snapshot& s, const uint8_t* ctx) {
  void* curSp = *reinterpret_cast<void* const*>(ctx + kSpOffset);
  void* const base = current_stack_top();
  if (base == nullptr) fatal("Take on a stack the port does not own (the game's main thread must run on RunOnMainStack)");
  s.core.resize(sizeof(Core) + sizeof(Tail));
  std::memcpy(s.core.data(), &sCore, sizeof(Core));
  Tail tail{};
  tail.base = base;
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
  keep(static_cast<uint8_t*>(curSp), static_cast<uint8_t*>(base));
  if (sCore.current >= 0) keep(static_cast<uint8_t*>(sCore.schedSp), static_cast<uint8_t*>(sCore.schedBase));
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
  PortCoro_Resume(tail.ctx);
}

void main_start() {
  sMainFn(sMainArg);
  void* dummy = nullptr;
  PortCtx_Swap(&dummy, sHostSp);
  fatal("the main stack ran on after its host took it back");
}
}

void RunOnMainStack(void (*fn)(void*), void* arg) {
  if (!sOn) fatal("RunOnMainStack before UseCoroutines");
  sMainFn = fn;
  sMainArg = arg;
  uint64_t* p = reinterpret_cast<uint64_t*>(sMainStack);
  for (size_t k = 0; k < kCanaryBytes / 8; k++) p[k] = kCanary;
  PortCtx sp = PortCtx_Make(sMainStack, kMainStack, main_start);
  PortCtx_Swap(&sHostSp, sp);
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
  if (coreSize != sizeof(Core) + sizeof(Tail) || coreSize > size - pos) return bad("the task part of the file ends too early");
  s.core.assign(data + pos, data + pos + coreSize);
  pos += static_cast<size_t>(coreSize);
  s.tasks = static_cast<size_t>(tasks);
  s.inTask = inTask != 0;
  if (!get64(pieces) || pieces > 4096) return bad("the task part of the file is damaged");
  s.stacks.clear();
  for (uint64_t i = 0; i < pieces; i++) {
    uint64_t address, n;
    if (!get64(address) || !get64(n) || n > size - pos) return bad("the task part of the file ends too early");
    if (address < arena || address + n > arena + arenaSize) return bad("a stack piece of the file lies outside the stacks");
    Piece p;
    p.address = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
    p.bytes.assign(data + pos, data + pos + n);
    pos += static_cast<size_t>(n);
    s.stacks.push_back(std::move(p));
  }
  size_t changed = 0;
#if defined(__APPLE__)
  if (static_cast<uintptr_t>(cookie) != currentCookie && cookie != 0) {
    auto fix = [&](uint8_t* base, size_t n, uintptr_t address) {
      size_t off = static_cast<size_t>((8 - (address & 7)) & 7);
      for (; off + 8 <= n; off += 8) {
        uint64_t v;
        std::memcpy(&v, base + off, 8);
        if (v == cookie) {
          const uint64_t now = currentCookie;
          std::memcpy(base + off, &now, 8);
          changed++;
        }
      }
    };
    for (Piece& p : s.stacks) fix(p.bytes.data(), p.bytes.size(), reinterpret_cast<uintptr_t>(p.address));
    fix(s.core.data() + sizeof(Core) + offsetof(Tail, ctx), kCtxBuf, 0);
  }
#endif
  if (rewritten != nullptr) *rewritten = changed;
  if (fileCookie != nullptr) *fileCookie = static_cast<uintptr_t>(cookie);
  return true;
}

__attribute__((noinline)) int Take(Snapshot& s) {
  alignas(16) static uint8_t buf[kCtxBuf];
  if (PortCoro_Save(buf) == 0) {
    gather(s, buf);
    return 0;
  }
  return 1;
}

[[noreturn]] void Restore(const Snapshot& s) {
  sRestoring = &s;
  PortCtx sp = PortCtx_Make(sRestorer, kRestorerStack, restorer_main);
  PortCtx_Swap(&sDummySp, sp);
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
  void* const base = current_stack_top();
  if (base == nullptr) fatal("the scheduler runs on a stack the port does not own (the game's main thread must run on RunOnMainStack)");
  sCore.schedBase = base;
  sCore.schedKnown = true;
  if (!t.started) {
    t.started = true;
    canary_set(i);
    t.sp = PortCtx_Make(slot_lo(i), kSlotSize, start_function);
    sCore.starting = i;
  }
  sCore.current = i;
  switch_to(&sCore.schedSp, t.sp);
  sCore.current = -1;
  canary_check(i, "task switched out");
}

void SwitchToScheduler(void (*hook)()) {
  if (sCore.current < 0) fatal("switch with no task running");
  if (hook != nullptr) hook();
  CTask& t = sCore.tasks[sCore.current];
  switch_to(&t.sp, sCore.schedSp);
}

void Destroy(void* thread) {
  const int i = find(thread);
  if (i < 0) return;
  if (i == sCore.current) fatal("a task destroyed itself while running");
  sCore.tasks[i] = CTask{};
}

}
