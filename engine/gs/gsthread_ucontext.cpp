#include "gsthread_port.h"

#include <sys/mman.h>
#include <ucontext.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace {

struct Task {
  ucontext_t context{};
  void* stack = nullptr;
  size_t stackSize = 0;
  bool made = false;
  PortGSthreadEntry entry = nullptr;
  PortGSthreadExit exitFunc = nullptr;
  uintptr_t args[PORT_GSTHREAD_MAX_ARGS] = {};
};

std::unordered_map<void*, Task> sTasks;
ucontext_t sSchedulerContext;
bool sSchedulerRunning = false;
Task* sStarting = nullptr;
Task* sCurrent = nullptr;

[[noreturn]] void fatal(const char* what) {
  std::fprintf(stderr, "[engine/gs] GSthread: %s\n", what);
  std::fflush(stderr);
  std::abort();
}

void task_start() {
  Task* t = sStarting;
  const uintptr_t* a = t->args;
  const uintptr_t result = t->entry(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
  t->exitFunc(static_cast<uint32_t>(result));
  fatal("a task ran on after its exit function");
}

size_t pc_stack_size(uint32_t gcSize) {
  const size_t size = static_cast<size_t>(gcSize) * 8;
  return size < 256 * 1024 ? 256 * 1024 : size;
}

void free_stack(Task& t) {
  if (t.stack != nullptr) {
    munmap(t.stack, t.stackSize);
    t.stack = nullptr;
  }
}

}

extern "C" {

void PortGSthread_Create(void* thread, void* entry, PortGSthreadExit exitFunc, uint32_t stackSize) {
  PortGSthread_Destroy(thread);
  Task& t = sTasks[thread];
  t.entry = reinterpret_cast<PortGSthreadEntry>(entry);
  t.exitFunc = exitFunc;
  constexpr size_t kReserve = 0x100000, kGuard = 0x1000;
  size_t usable = (pc_stack_size(stackSize) + 0xFFF) & ~size_t{0xFFF};
  if (usable < kReserve) usable = kReserve;
  t.stackSize = usable + kGuard;
  void* stack = mmap(nullptr, t.stackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT | MAP_STACK,
                     -1, 0);
  if (stack == MAP_FAILED) {
    fatal("no memory below 2 GB for a task's stack");
  }
  mprotect(stack, kGuard, PROT_NONE);
  t.stack = stack;
  if (getcontext(&t.context) != 0) {
    fatal("getcontext failed");
  }
  t.context.uc_stack.ss_sp = static_cast<char*>(t.stack) + kGuard;
  t.context.uc_stack.ss_size = usable;
  t.context.uc_link = nullptr;
  makecontext(&t.context, task_start, 0);
  t.made = true;
}

void PortGSthread_SetArg(void* thread, int index, uintptr_t value) {
  auto it = sTasks.find(thread);
  if (it == sTasks.end() || index < 0 || index >= PORT_GSTHREAD_MAX_ARGS) {
    fatal("start-up value for an unknown task, or too many");
  }
  it->second.args[index] = value;
}

void PortGSthread_Run(void* thread) {
  auto it = sTasks.find(thread);
  if (it == sTasks.end()) {
    fatal("run of an unknown task");
  }
  sSchedulerRunning = true;
  sStarting = &it->second;
  ucontext_t* from = sCurrent != nullptr ? &sCurrent->context : &sSchedulerContext;
  sCurrent = &it->second;
  swapcontext(from, &it->second.context);
}

void (*PortGSthread_SwitchOutHook)(void) = nullptr;

void PortGSthread_SwitchToScheduler(void) {
  if (!sSchedulerRunning || sCurrent == nullptr) {
    fatal("switch with no scheduler running");
  }
  if (PortGSthread_SwitchOutHook != nullptr) {
    PortGSthread_SwitchOutHook();
  }
  Task* me = sCurrent;
  sCurrent = nullptr;
  swapcontext(&me->context, &sSchedulerContext);
}

void PortGSthread_Destroy(void* thread) {
  auto it = sTasks.find(thread);
  if (it == sTasks.end()) {
    return;
  }
  if (&it->second == sCurrent) {
    fatal("a task destroyed itself while running");
  }
  free_stack(it->second);
  sTasks.erase(it);
}

}
