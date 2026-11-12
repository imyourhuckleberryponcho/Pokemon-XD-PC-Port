#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
void* OSGetCurrentThread(void);
int OSCreateThread(void* thread, void* (*func)(void*), void* param, void* stack, uint32_t stackSize, int32_t priority,
                   uint16_t attr);
int32_t OSResumeThread(void* thread);
void OSInitThreadQueue(void* queue);
void OSSleepThread(void* queue);
void OSWakeupThread(void* queue);
int OSJoinThread(void* thread, void** value);
void* OSSetIdleFunction(void (*func)(void*), void* param, void* stack, uint32_t stackSize);
void OSCreateAlarm(void* alarm);
void OSSetAlarm(void* alarm, int64_t ticks, void (*handler)(void*, void*));
int OSDisableInterrupts(void);
int OSRestoreInterrupts(int level);
void VIWaitForRetrace(void);
void (*VISetPreRetraceCallback(void (*cb)(uint32_t)))(uint32_t);
void (*VISetPostRetraceCallback(void (*cb)(uint32_t)))(uint32_t);
uint32_t VIGetRetraceCount(void);
int64_t OSGetTime(void);
void SISetSamplingRate(uint32_t msec);
void (*PADSetSamplingCallback(void (*callback)(void)))(void);
}

#include <algorithm>
#include <vector>

namespace {
std::vector<int64_t> sPolls;
std::vector<uint32_t> sPollRetrace;
void on_poll(void) {
  sPolls.push_back(OSGetTime());
  sPollRetrace.push_back(VIGetRetraceCount());
}
unsigned char threadA[0x318], threadB[0x318];
unsigned char queueQ[8], queueAlarm[8], alarm[0x28];
std::string sLog;
bool ok = true;

void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

void* thread_a(void*) {
  sLog += "A1 ";
  OSSleepThread(queueQ);
  sLog += "A2 ";
  return reinterpret_cast<void*>(42);
}

void* thread_b(void*) {
  sLog += "B1 ";
  OSWakeupThread(queueQ);
  sLog += "B2 ";
  return nullptr;
}

void alarm_handler(void*, void*) { OSWakeupThread(queueAlarm); }

volatile int sPre = 0, sPost = 0;
void pre_retrace(uint32_t) { sPre++; }
void post_retrace(uint32_t) { sPost++; }

volatile bool sMainActive = false;
volatile long sIdleCount = 0;
volatile bool sIdleOverlap = false;
void idle_loop(void*) {
  for (;;) {
    const int level = OSDisableInterrupts();
    sIdleCount++;
    if (sMainActive) {
      sIdleOverlap = true;
    }
    OSRestoreInterrupts(level);
  }
}
}

int main() {
  OSGetCurrentThread();
  OSInitThreadQueue(queueQ);
  OSInitThreadQueue(queueAlarm);

  OSCreateThread(threadA, thread_a, nullptr, nullptr, 0x1000, 10, 0);
  OSCreateThread(threadB, thread_b, nullptr, nullptr, 0x1000, 20, 0);
  OSResumeThread(threadB);
  OSResumeThread(threadA);
  sLog += "M1 ";
  void* resultA = nullptr;
  OSJoinThread(threadB, nullptr);
  OSJoinThread(threadA, &resultA);
  sLog += "M2";
  std::printf("run order: %s\n", sLog.c_str());
  check(sLog == "A1 M1 B1 A2 B2 M2", "threads run in GameCube priority order, waking preempts");
  check(resultA == reinterpret_cast<void*>(42), "a thread's result reaches OSJoinThread");

  auto t0 = std::chrono::steady_clock::now();
  OSCreateAlarm(alarm);
  OSSetAlarm(alarm, 40500000 / 1000 * 20, alarm_handler);
  OSSleepThread(queueAlarm);
  const double alarmMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("alarm woke the thread after %.1f ms (set for 20)\n", alarmMs);
  check(alarmMs >= 19.5 && alarmMs < 60, "an alarm fires on time and its handler wakes a thread");

  VISetPreRetraceCallback(pre_retrace);
  VISetPostRetraceCallback(post_retrace);
  OSSetIdleFunction(idle_loop, nullptr, nullptr, 0);
  VIWaitForRetrace();
  const int pre0 = sPre, post0 = sPost;
  t0 = std::chrono::steady_clock::now();
  auto last = t0;
  double worst = 0;
  for (int i = 0; i < 60; i++) {
    sMainActive = true;
    volatile double x = 0;
    for (int k = 0; k < 200000; k++) {
      x = x + k * 0.5;
    }
    sMainActive = false;
    VIWaitForRetrace();
    const auto now = std::chrono::steady_clock::now();
    const double interval = std::chrono::duration<double, std::milli>(now - last).count();
    if (i > 0 && std::abs(interval - 16.683) > worst) {
      worst = std::abs(interval - 16.683);
    }
    last = now;
  }
  const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("60 frames took %.1f ms (GameCube NTSC: 1001.0), worst single frame off by %.2f ms; retrace callbacks "
              "%d/%d; idle ran %ld times\n",
              frameMs, worst, sPre - pre0, sPost - post0, sIdleCount);
  check(frameMs > 995 && frameMs < 1007, "frames run at 59.94 per second");
  check(worst < 1.0, "every frame is within 1 ms of 16.68 ms (no stutter)");
  check(sPre - pre0 == 60 && sPost - post0 == 60, "pre- and post-retrace callbacks run once per frame");
  check(sIdleCount > 0, "the idle function runs while the main thread waits");
  check(!sIdleOverlap, "the idle function never runs at the same time as the main thread");

  SISetSamplingRate(11);
  PADSetSamplingCallback(on_poll);
  VIWaitForRetrace();
  sPolls.clear();
  sPollRetrace.clear();
  const uint32_t field0 = VIGetRetraceCount();
  for (int i = 0; i < 60; i++) {
    VIWaitForRetrace();
  }
  PADSetSamplingCallback(nullptr);
  int perField[3] = {0, 0, 0};
  bool orderOk = true;
  for (size_t i = 0; i < sPolls.size(); i++) {
    const uint32_t field = sPollRetrace[i] - field0;
    if (field < 60) {
      perField[std::min<uint32_t>(2, i == 0 || sPollRetrace[i - 1] != sPollRetrace[i] ? 0 : 1)]++;
    }
    if (i > 0 && sPollRetrace[i] == sPollRetrace[i - 1]) {
      const double gapMs = (sPolls[i] - sPolls[i - 1]) / 40500.0;
      orderOk &= std::abs(gapMs - 131 * 2574 / 40500.0) < 1.5;
    }
  }
  std::printf("controller polls in 60 fields: %zu (first in field %d, second %d)\n", sPolls.size(), perField[0],
              perField[1]);
  check(sPolls.size() >= 118 && sPolls.size() <= 122, "SISetSamplingRate(11): the sampling callback runs twice per field");
  check(orderOk, "the two polls of a field are 131 lines (8.3 ms) apart");

  std::fflush(stdout);
  _Exit(ok ? 0 : 1);
}
