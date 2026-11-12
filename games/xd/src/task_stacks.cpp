#include "gsthread_port.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <port_win_compat.h>
static void* GetCurrentFiber() { return nullptr; }
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

extern "C" uint32_t PortVI_GetFlipCount(void);
extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize);
extern "C" void* GSthreadGetCurrentThread(void);

namespace {

std::vector<uint32_t> sFrames;
size_t sNext = 0;
std::set<void*> sDone;

void on_switch_out() {
  const uint32_t frame = PortVI_GetFlipCount();
  while (sNext < sFrames.size() && sNext + 1 < sFrames.size() && frame >= sFrames[sNext + 1]) {
    sNext++;
    sDone.clear();
  }
  if (sNext >= sFrames.size() || frame < sFrames[sNext]) {
    return;
  }
  void* task = GSthreadGetCurrentThread();
  if (!sDone.insert(task).second) {
    return;
  }
  void* calls[48];
  const USHORT n = RtlCaptureStackBackTrace(1, 48, calls, nullptr);
  std::fprintf(stderr, "[task stacks] frame %u, task %p (fiber %p):\n", frame, task, GetCurrentFiber());
  for (USHORT i = 0; i < n; i++) {
    char where[512];
    PortCrash_DescribeAddress(calls[i], where, sizeof where);
    std::fprintf(stderr, "    #%-2u %s\n", static_cast<unsigned>(i), where);
  }
  std::fflush(stderr);
}

}

extern "C" void PortXD_TaskStacksInit(void) {
  const char* list = std::getenv("XD_TASK_STACKS");
  if (list == nullptr || list[0] == '\0') {
    return;
  }
  for (const char* p = list; *p != '\0';) {
    char* end = nullptr;
    const unsigned long frame = std::strtoul(p, &end, 10);
    if (end == p) {
      break;
    }
    sFrames.push_back(static_cast<uint32_t>(frame));
    p = *end == ',' ? end + 1 : end;
  }
  std::sort(sFrames.begin(), sFrames.end());
  if (!sFrames.empty()) {
    PortGSthread_SwitchOutHook = on_switch_out;
    std::fprintf(stderr, "[task stacks] XD_TASK_STACKS: %zu frames\n", sFrames.size());
  }
}
