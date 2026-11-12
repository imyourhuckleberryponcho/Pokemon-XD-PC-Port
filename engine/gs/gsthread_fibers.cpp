#include "gsthread_port.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace {

struct Task {
  void* fiber = nullptr;
  PortGSthreadEntry entry = nullptr;
  PortGSthreadExit exitFunc = nullptr;
  uintptr_t args[PORT_GSTHREAD_MAX_ARGS] = {};
};

std::unordered_map<void*, Task> sTasks;
void* sSchedulerFiber = nullptr;
Task* sStarting = nullptr;

[[noreturn]] void fatal(const char* what) {
  std::fprintf(stderr, "[engine/gs] GSthread: %s\n", what);
  std::fflush(stderr);
  std::abort();
}

void WINAPI fiber_start(void*) {
  Task* t = sStarting;
  const uintptr_t* a = t->args;
  const uintptr_t result = t->entry(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
  t->exitFunc(static_cast<uint32_t>(result));
  fatal("a task ran on after its exit function");
}

SIZE_T pc_stack_size(uint32_t gcSize) {
  const SIZE_T size = static_cast<SIZE_T>(gcSize) * 8;
  return size < 256 * 1024 ? 256 * 1024 : size;
}

}

extern "C" {

void PortGSthread_Create(void* thread, void* entry, PortGSthreadExit exitFunc, uint32_t stackSize) {
  PortGSthread_Destroy(thread);
  Task& t = sTasks[thread];
  t.entry = reinterpret_cast<PortGSthreadEntry>(entry);
  t.exitFunc = exitFunc;
  t.fiber = CreateFiber(pc_stack_size(stackSize), fiber_start, nullptr);
  if (t.fiber == nullptr) {
    fatal("CreateFiber failed");
  }
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
  if (sSchedulerFiber == nullptr) {
    sSchedulerFiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
  }
  sStarting = &it->second;
  SwitchToFiber(it->second.fiber);
}

void (*PortGSthread_SwitchOutHook)(void) = nullptr;

void PortGSthread_SwitchToScheduler(void) {
  if (sSchedulerFiber == nullptr) {
    fatal("switch with no scheduler running");
  }
  if (PortGSthread_SwitchOutHook != nullptr) {
    PortGSthread_SwitchOutHook();
  }
  SwitchToFiber(sSchedulerFiber);
}

void PortGSthread_Destroy(void* thread) {
  auto it = sTasks.find(thread);
  if (it == sTasks.end()) {
    return;
  }
  if (it->second.fiber != nullptr) {
    if (it->second.fiber == GetCurrentFiber()) {
      fatal("a task destroyed itself while running");
    }
    DeleteFiber(it->second.fiber);
  }
  sTasks.erase(it);
}

}
