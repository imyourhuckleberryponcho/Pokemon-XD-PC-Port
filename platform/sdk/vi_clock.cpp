#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <settings_c.h>

extern "C" {
void OSInitThreadQueue(void* queue);
void OSSleepThread(void* queue);
void OSWakeupThread(void* queue);
int OSDisableInterrupts(void);
int OSRestoreInterrupts(int level);
int64_t OSGetTime(void);
int64_t PortTime_Now(void);
void OSSetPeriodicAlarm(void* alarm, int64_t start, int64_t period, void (*handler)(void*, void*));
void OSCancelAlarm(void* alarm);
bool PortGX_InDisplayList(void);
void PortFrameCost_FrameWork(void);
void PortGX_OnRetrace(uint32_t count);
void PortTime_SetGameClockFilter(int64_t (*filter)(int64_t pcClock));
bool PortTime_IsDeterministic(void);
}

namespace {

constexpr int64_t kTimerClock = 40500000;
constexpr int64_t kFieldTicks = kTimerClock * 1001 / 60000;
constexpr int64_t kLineTicks = kFieldTicks * 2 / 525;

struct PollXY {
  uint16_t line;
  uint8_t count;
};
constexpr PollXY kPollNtsc[12] = {{0x00F6, 0x02}, {0x000E, 0x13}, {0x001E, 0x09}, {0x002C, 0x06},
                                  {0x0034, 0x05}, {0x0041, 0x04}, {0x0057, 0x03}, {0x0057, 0x03},
                                  {0x0057, 0x03}, {0x0083, 0x02}, {0x0083, 0x02}, {0x0083, 0x02}};
constexpr int kMaxPolls = 0x13;
unsigned char sPollAlarms[kMaxPolls][0x28];
int sPollsArmed = 0;
uint32_t sSamplingRate = 0;
void (*sSamplingCallback)(void) = nullptr;
int64_t sFieldBase = 0;

using RetraceCallback = void (*)(uint32_t);

unsigned char sAlarm[0x28];
unsigned char sRetraceQueue[8];
volatile uint32_t sRetraceCount = 0;
RetraceCallback sPreRetrace = nullptr;
RetraceCallback sPostRetrace = nullptr;
void (*sFrameHook)(void) = nullptr;
uint32_t sHookRetrace = 0;
std::thread::id sHookThread;
bool sStarted = false;

void (*sFieldHook)(uint32_t) = nullptr;
void (*sWorkHook)(void) = nullptr;

constexpr int64_t kMaxLateTicks = kTimerClock / 10;
std::atomic<int64_t> sHiddenTicks{0};
constexpr int64_t kFloorLagTicks = 2 * kFieldTicks;

int64_t gamecube_clock(int64_t pcClock) {
  const int64_t hidden = sHiddenTicks.load(std::memory_order_relaxed);
  const int64_t field = sFieldBase + static_cast<int64_t>(sRetraceCount) * kFieldTicks - hidden;
  int64_t floor = pcClock - hidden - kFloorLagTicks;
  if (floor > field + kMaxLateTicks) {
    floor = field + kMaxLateTicks;
  }
  return field > floor ? field : floor;
}

void vertical_blank(void*, void*) {
  sRetraceCount++;
  if (PortTime_Now() - (sFieldBase + static_cast<int64_t>(sRetraceCount) * kFieldTicks) > kMaxLateTicks) {
    sHiddenTicks.fetch_add(kFieldTicks, std::memory_order_relaxed);
  }
  if (sFieldHook != nullptr) {
    sFieldHook(sRetraceCount);
  }
  if (sPreRetrace != nullptr) {
    sPreRetrace(sRetraceCount);
  }
  if (sPostRetrace != nullptr) {
    sPostRetrace(sRetraceCount);
  }
  PortGX_OnRetrace(sRetraceCount);
  OSWakeupThread(sRetraceQueue);
}

void ensure_started() {
  if (!sStarted) {
    sStarted = true;
    OSInitThreadQueue(sRetraceQueue);
    sFieldBase = PortTime_Now();
    OSSetPeriodicAlarm(sAlarm, sFieldBase + kFieldTicks, kFieldTicks, vertical_blank);
    const int32_t* clock = EngineSettings_IntSlot("system.game_clock");
    const bool pc = clock != nullptr && *clock == EngineSettings_ChoiceIndex("system.game_clock", "pc");
    if (!PortTime_IsDeterministic()) {
      PortTime_SetGameClockFilter(pc ? nullptr : gamecube_clock);
      std::fprintf(stderr, "[platform/sdk] game clock: %s\n",
                   pc ? "the PC's (system.game_clock=pc: frame times as this PC measures them)"
                      : "the GameCube's (the last vertical blank: frame times in whole fields)");
    }
  }
}

void controller_poll(void*, void*) {
  if (sSamplingCallback != nullptr) {
    sSamplingCallback();
  }
}

void arm_polls() {
  ensure_started();
  for (int i = 0; i < sPollsArmed; i++) {
    OSCancelAlarm(sPollAlarms[i]);
  }
  const PollXY xy = kPollNtsc[sSamplingRate];
  const int64_t now = PortTime_Now();
  const int64_t nextField = sFieldBase + ((now - sFieldBase) / kFieldTicks + 1) * kFieldTicks;
  sPollsArmed = xy.count < kMaxPolls ? xy.count : kMaxPolls;
  for (int k = 0; k < sPollsArmed; k++) {
    const int64_t offset = static_cast<int64_t>(k + 1) * xy.line * kLineTicks;
    OSSetPeriodicAlarm(sPollAlarms[k], nextField + offset, kFieldTicks, controller_poll);
  }
}

}

extern "C" {

RetraceCallback VISetPreRetraceCallback(RetraceCallback cb) {
  const int level = OSDisableInterrupts();
  RetraceCallback old = sPreRetrace;
  sPreRetrace = cb;
  OSRestoreInterrupts(level);
  return old;
}

RetraceCallback VISetPostRetraceCallback(RetraceCallback cb) {
  const int level = OSDisableInterrupts();
  RetraceCallback old = sPostRetrace;
  sPostRetrace = cb;
  OSRestoreInterrupts(level);
  return old;
}

void VIWaitForRetrace(void) {
  ensure_started();
  const int level = OSDisableInterrupts();
  const uint32_t count = sRetraceCount;
  do {
    OSSleepThread(sRetraceQueue);
  } while (count == sRetraceCount);
  OSRestoreInterrupts(level);
  if (sFrameHook != nullptr) {
    sHookRetrace = sRetraceCount;
    sFrameHook();
  }
}

void PortVI_FrameHookIfDue(uint32_t fields) {
  if (sFrameHook != nullptr && sRetraceCount - sHookRetrace >= fields &&
      std::this_thread::get_id() == sHookThread && !PortGX_InDisplayList()) {
    sHookRetrace = sRetraceCount;
    sFrameHook();
  }
}

uint32_t VIGetRetraceCount(void) { return sRetraceCount; }

uint32_t VIGetNextField(void) {
  const uint32_t next = (sRetraceCount + 1) & 1;
  if (std::this_thread::get_id() == sHookThread) {
    if (sWorkHook != nullptr) {
      sWorkHook();
    }
    PortFrameCost_FrameWork();
  }
  return next;
}

void SISetSamplingRate(uint32_t msec) {
  if (msec > 11) {
    msec = 11;
  }
  const int level = OSDisableInterrupts();
  sSamplingRate = msec;
  arm_polls();
  OSRestoreInterrupts(level);
}

void SIRefreshSamplingRate(void) { SISetSamplingRate(sSamplingRate); }

void (*PADSetSamplingCallback(void (*callback)(void)))(void) {
  const int level = OSDisableInterrupts();
  auto old = sSamplingCallback;
  sSamplingCallback = callback;
  OSRestoreInterrupts(level);
  return old;
}

void PortVI_SetFieldHook(void (*hook)(uint32_t field)) { sFieldHook = hook; }

void PortVI_SetFrameWorkHook(void (*hook)(void)) { sWorkHook = hook; }

void PortVI_SetFrameHook(void (*hook)(void)) {
  sFrameHook = hook;
  sHookThread = std::this_thread::get_id();
}

}
