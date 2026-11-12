#include <dolphin/arq.h>
#include <dolphin/card.h>
#include <dolphin/dvd.h>

#include <settings_c.h>

#include "disc_drive.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

extern "C" void PortOS_RaiseInterrupt(void (*handler)(void*), void* arg);
extern "C" void PortCpuCost_Count(unsigned counter, uint64_t amount);
extern "C" void PortOS_SetAlarmAt(void* alarm, int64_t when, void (*handler)(void*, void*));
extern "C" int64_t PortTime_Now(void);
extern "C" uint32_t VIGetRetraceCount(void);
extern "C" uint32_t PortVI_GetFlipCount(void);
extern "C" s32 CARDFormatAsync(s32 chan, CARDCallback callback);
extern "C" bool PortTime_IsDeterministic(void);
extern "C" uint32_t PortTime_CardSeconds(void);
extern "C" void PortCARD_NewSerial(s32 chan);

namespace {

std::mutex sLock;

enum class DiscMode { GameCube, Fast, Drive, Buffered };
DiscMode disc_mode() {
  static const DiscMode mode = [] {
    const int32_t* timing = EngineSettings_IntSlot("system.disc_timing");
    if (timing == nullptr) return DiscMode::GameCube;
    if (*timing == EngineSettings_ChoiceIndex("system.disc_timing", "fast")) return DiscMode::Fast;
    if (*timing == EngineSettings_ChoiceIndex("system.disc_timing", "drive")) return DiscMode::Drive;
    if (*timing == EngineSettings_ChoiceIndex("system.disc_timing", "buffered")) return DiscMode::Buffered;
    return DiscMode::GameCube;
  }();
  return mode;
}
bool drive_model() { return disc_mode() == DiscMode::Drive || disc_mode() == DiscMode::Buffered; }

bool (*sFileLocator)(const char* path, uint32_t* offset) = nullptr;
std::unordered_map<const DVDFileInfo*, uint32_t> sFileStart;
uint32_t file_start(const DVDFileInfo* info) {
  auto it = sFileStart.find(info);
  return it != sFileStart.end() ? it->second : 0u;
}

bool disc_log() {
  static const bool on = std::getenv("XD_DISC_LOG") != nullptr;
  return on;
}
int64_t sOriginTb = INT64_MIN;
int64_t sFirstCommandUs = 0;
int64_t since_origin(int64_t tb) {
  if (sOriginTb == INT64_MIN) {
    static const int64_t firstCommand = [] {
      const char* e = std::getenv("XD_DISC_ORIGIN_US");
      return (e != nullptr ? static_cast<int64_t>(std::strtoll(e, nullptr, 10)) : sFirstCommandUs) * 405 / 10;
    }();
    sOriginTb = tb - firstCommand;
  }
  return tb - sOriginTb;
}

struct DvdOp {
  DVDCallback callback = nullptr;
  uint32_t due = 0;
  uint64_t doneTick = 0;
  bool chained = false;
  bool workerDone = false;
  s32 result = 0;
  uint64_t seq = 0;
};
uint64_t sDvdSeq = 0;
std::unordered_map<DVDFileInfo*, DvdOp> sDvdPending;
std::condition_variable sDvdWorkerCv;
constexpr uint64_t kTicksPerFrame = 2700000u / 60u;
uint64_t sDriveFreeTick = 0;
int sDiscFast = -1;

thread_local bool tInDiscCallback = false;
thread_local uint64_t tCallbackTick = 0;
thread_local uint32_t tCallbackFlip = 0;

struct DvdDone {
  DVDCallback callback;
  s32 result;
  DVDFileInfo* fileInfo;
  uint64_t doneTick;
  uint32_t flip;
  uint64_t seq;
};
void log_done(uint64_t seq);

DvdDone* take_chained(uint32_t flip) {
  std::unique_lock<std::mutex> guard(sLock);
  for (;;) {
    auto best = sDvdPending.end();
    for (auto it = sDvdPending.begin(); it != sDvdPending.end(); ++it) {
      if (it->second.chained && it->second.due <= flip &&
          (best == sDvdPending.end() || it->second.seq < best->second.seq)) {
        best = it;
      }
    }
    if (best == sDvdPending.end()) return nullptr;
    DvdOp& op = best->second;
    if (!sDvdWorkerCv.wait_for(guard, std::chrono::seconds(10), [&] { return op.workerDone; })) {
      std::fprintf(stderr, "[disc] a read due at frame %u didn't finish in 10 s; completing it anyway\n", op.due);
      op.result = DVD_RESULT_FATAL_ERROR;
    }
    DVDFileInfo* info = best->first;
    const DvdOp done = op;
    sDvdPending.erase(best);
    if (done.callback != nullptr) {
      return new DvdDone{done.callback, done.result, info, done.doneTick, flip, done.seq};
    }
  }
}

void dvd_deliver(void* arg) {
  auto* d = static_cast<DvdDone*>(arg);
  const uint32_t flip = d->flip;
  tInDiscCallback = true;
  tCallbackTick = d->doneTick;
  tCallbackFlip = flip;
  log_done(d->seq);
  if (d->result > 0) {
    PortCpuCost_Count(5u, static_cast<uint64_t>(d->result));
  }
  d->callback(d->result, d->fileInfo);
  tInDiscCallback = false;
  delete d;
  if (DvdDone* next = take_chained(flip)) {
    PortOS_RaiseInterrupt(dvd_deliver, next);
  }
}

void dvd_done(s32 result, DVDFileInfo* fileInfo) {
  std::lock_guard<std::mutex> guard(sLock);
  auto it = sDvdPending.find(fileInfo);
  if (it != sDvdPending.end()) {
    it->second.workerDone = true;
    it->second.result = result;
  }
  sDvdWorkerCv.notify_all();
}

uint64_t disc_ticks(s32 length) {
  if (sDiscFast < 0) {
    sDiscFast = disc_mode() == DiscMode::Fast ? 1 : 0;
  }
  static const uint64_t extra = [] {
    const char* e = std::getenv("XD_DISC_DELAY");
    return e != nullptr ? static_cast<uint64_t>(std::strtoul(e, nullptr, 10)) * kTicksPerFrame : 0u;
  }();
  return (sDiscFast == 1 ? 0 : static_cast<uint64_t>(length > 0 ? length : 0)) + extra;
}

DVDCallback dvd_track(DVDFileInfo* fileInfo, DVDCallback callback, s32 length, s32 offset, bool seek) {
  std::lock_guard<std::mutex> guard(sLock);
  const uint32_t flip = PortVI_GetFlipCount();
  const bool chained = tInDiscCallback;
  const uint64_t issued = chained ? tCallbackTick : static_cast<uint64_t>(flip) * kTicksPerFrame;
  const uint64_t done = std::max(issued, sDriveFreeTick) + disc_ticks(length);
  sDriveFreeTick = done;
  uint64_t due = (done + kTicksPerFrame - 1) / kTicksPerFrame;
  const uint64_t earliest = chained ? tCallbackFlip : static_cast<uint64_t>(flip) + 1;
  if (due < earliest) due = earliest;
  DvdOp op;
  op.callback = callback;
  op.due = static_cast<uint32_t>(due);
  op.doneTick = done;
  op.chained = chained;
  op.seq = ++sDvdSeq;
  sDvdPending[fileInfo] = op;
  if (disc_log()) {
    const int64_t now = since_origin(PortTime_Now());
    std::fprintf(stderr, "[disc cmd] seq=%llu kind=%s disc=0x%x len=0x%x issue=%lld done=-1 flip=%u field=%u due=%u%s\n",
                 static_cast<unsigned long long>(op.seq), seek ? "seek" : "read",
                 static_cast<unsigned>(file_start(fileInfo) + static_cast<uint32_t>(offset)),
                 static_cast<unsigned>(length), static_cast<long long>(now), flip, VIGetRetraceCount(), op.due,
                 chained ? " chained" : "");
  }
  return dvd_done;
}

void log_done(uint64_t seq) {
  if (disc_log()) {
    std::fprintf(stderr, "[disc done] seq=%llu flip=%u field=%u t=%lld\n", static_cast<unsigned long long>(seq),
                 PortVI_GetFlipCount(), VIGetRetraceCount(), static_cast<long long>(since_origin(PortTime_Now())));
  }
}

struct DriveCmd {
  DVDFileInfo* info = nullptr;
  DVDCallback callback = nullptr;
  bool seek = false;
  bool sync = false;
  bool finished = false;
  uint32_t disc = 0;
  uint32_t length = 0;
  uint32_t doneBytes = 0;
  uint32_t chunk = 0;
  int64_t doneTb = 0;
  s32 result = 0;
  uint64_t seq = 0;
};
port_disc::DriveModel sDrive;
DriveCmd* sExecuting = nullptr;
bool sCompleting = false;
std::deque<DriveCmd*> sDriveQueue;
alignas(8) unsigned char sDriveAlarm[64];
OSThreadQueue sSyncQueue;
constexpr uint32_t kMaxDriveCommand = 0x80000;
thread_local bool tInDriveInterrupt = false;
thread_local int64_t tDriveInterruptTb = 0;
thread_local bool tInArqInterrupt = false;
thread_local int64_t tArqInterruptTb = 0;

int64_t timed_now() {
  if (tInArqInterrupt) return tArqInterruptTb;
  if (tInDriveInterrupt) return tDriveInterruptTb;
  return PortTime_Now();
}

void drive_interrupt(void*, void*);

void drive_start(DriveCmd* cmd, int64_t tb) {
  static const int64_t extra = [] {
    const char* e = std::getenv("XD_DISC_DELAY");
    return e != nullptr ? static_cast<int64_t>(std::strtoul(e, nullptr, 10)) * 675675 : 0;
  }();
  sDrive.SetBuffered(disc_mode() == DiscMode::Buffered);
  const uint64_t cpuNow = static_cast<uint64_t>(since_origin(tb)) * 12u;
  uint64_t cpuTicks;
  if (cmd->seek) {
    cmd->chunk = 0;
    cpuTicks = port_disc::DriveModel::Seek();
  } else {
    cmd->chunk = std::min(cmd->length - cmd->doneBytes, kMaxDriveCommand);
    cpuTicks = sDrive.Read(cpuNow, cmd->disc + cmd->doneBytes, cmd->chunk);
  }
  cmd->doneTb = tb + static_cast<int64_t>((cpuTicks + 11u) / 12u) + extra;
  sExecuting = cmd;
  if (disc_log()) {
    std::fprintf(stderr,
                 "[disc cmd] seq=%llu kind=%s disc=0x%x len=0x%x issue=%lld done=%lld flip=%u field=%u buf=%u unbuf=%u "
                 "headmove=%d\n",
                 static_cast<unsigned long long>(cmd->seq), cmd->seek ? "seek" : cmd->sync ? "sync" : "read",
                 cmd->disc + cmd->doneBytes, cmd->chunk, static_cast<long long>(since_origin(tb)),
                 static_cast<long long>(since_origin(cmd->doneTb)), PortVI_GetFlipCount(), VIGetRetraceCount(),
                 cmd->seek ? 0u : sDrive.lastBuffered, cmd->seek ? 0u : sDrive.lastUnbuffered,
                 cmd->seek ? 0 : (sDrive.lastSeek ? 1 : 0));
  }
  PortOS_SetAlarmAt(sDriveAlarm, cmd->doneTb, drive_interrupt);
}

void drive_request(DriveCmd* cmd) {
  std::lock_guard<std::mutex> guard(sLock);
  cmd->seq = ++sDvdSeq;
  if (sExecuting == nullptr && !sCompleting) {
    drive_start(cmd, timed_now());
  } else {
    sDriveQueue.push_back(cmd);
  }
}

void drive_interrupt(void*, void*) {
  std::unique_lock<std::mutex> guard(sLock);
  DriveCmd* cmd = sExecuting;
  if (cmd == nullptr) return;
  const int64_t tb = cmd->doneTb;
  if (!cmd->seek) {
    cmd->doneBytes += cmd->chunk;
    if (cmd->doneBytes < cmd->length) {
      drive_start(cmd, tb);
      return;
    }
  }
  s32 result = cmd->result;
  auto it = sDvdPending.find(cmd->info);
  if (it != sDvdPending.end()) {
    DvdOp& op = it->second;
    if (!sDvdWorkerCv.wait_for(guard, std::chrono::seconds(10), [&] { return op.workerDone; })) {
      std::fprintf(stderr, "[disc] a read didn't finish in 10 s; completing it anyway\n");
      op.result = DVD_RESULT_FATAL_ERROR;
    }
    result = op.result;
    sDvdPending.erase(it);
  }
  sExecuting = nullptr;
  sCompleting = true;
  guard.unlock();
  if (disc_log()) {
    std::fprintf(stderr, "[disc done] seq=%llu flip=%u field=%u t=%lld\n", static_cast<unsigned long long>(cmd->seq),
                 PortVI_GetFlipCount(), VIGetRetraceCount(), static_cast<long long>(since_origin(tb)));
  }
  tInDriveInterrupt = true;
  tDriveInterruptTb = tb;
  if (result > 0) {
    PortCpuCost_Count(5u, static_cast<uint64_t>(result));
  }
  if (cmd->sync) {
    std::lock_guard<std::mutex> g(sLock);
    cmd->result = result;
    cmd->finished = true;
  } else if (cmd->callback != nullptr) {
    cmd->callback(result, cmd->info);
  }
  if (cmd->sync) {
    OSWakeupThread(&sSyncQueue);
  } else {
    delete cmd;
  }
  tInDriveInterrupt = false;
  guard.lock();
  sCompleting = false;
  if (sExecuting == nullptr && !sDriveQueue.empty()) {
    DriveCmd* next = sDriveQueue.front();
    sDriveQueue.pop_front();
    drive_start(next, tb);
  }
}

void drive_track(DVDFileInfo* info) {
  std::lock_guard<std::mutex> guard(sLock);
  DvdOp op;
  op.due = UINT32_MAX;
  sDvdPending[info] = op;
}

CARDCallback sCardPending[2];
CARDCallback sCardDetach[2];

struct CardDone {
  CARDCallback callback;
  s32 chan;
  s32 result;
};

void card_deliver(void* arg) {
  auto* d = static_cast<CardDone*>(arg);
  d->callback(d->chan, d->result);
  delete d;
}

void card_done(s32 chan, s32 result) {
  CARDCallback callback = nullptr;
  {
    std::lock_guard<std::mutex> guard(sLock);
    if (chan >= 0 && chan < 2) {
      callback = sCardPending[chan];
      sCardPending[chan] = nullptr;
    }
  }
  if (callback != nullptr) {
    PortOS_RaiseInterrupt(card_deliver, new CardDone{callback, chan, result});
  }
}

void card_detached(s32 chan, s32 result) {
  CARDCallback callback = nullptr;
  {
    std::lock_guard<std::mutex> guard(sLock);
    if (chan >= 0 && chan < 2) {
      callback = sCardDetach[chan];
    }
  }
  if (callback != nullptr) {
    PortOS_RaiseInterrupt(card_deliver, new CardDone{callback, chan, result});
  }
}

CARDCallback card_track(s32 chan, CARDCallback callback) {
  if (callback == nullptr || chan < 0 || chan >= 2) {
    return callback;
  }
  std::lock_guard<std::mutex> guard(sLock);
  sCardPending[chan] = callback;
  return card_done;
}

constexpr uint64_t kCardCpuHz = 486000000u;
constexpr uint64_t kCardFieldCycles = kCardCpuHz * 1001u / 60000u;
constexpr uint64_t kCardReadByte = kCardCpuHz / (512u * 1024u);
constexpr uint64_t kCardWriteByte = kCardCpuHz / 98432u;
constexpr uint64_t kCardCmdDone = 5000u;
constexpr uint64_t kCardSdkWork = 5000u;
constexpr uint64_t kCardPage = 128u * kCardWriteByte + kCardCmdDone + kCardSdkWork;
constexpr uint64_t kCardSegment = 512u * kCardReadByte + kCardSdkWork;
constexpr uint64_t kCardSector = kCardCmdDone + (0x2000u / 128u) * kCardPage;

struct CardOp {
  bool pending = false;
  uint32_t due = 0;
  s32 result = 0;
  CARDCallback callback = nullptr;
};
CardOp sCardOp[2];
bool sCardHaveResult[2] = {false, false};
s32 sCardResult[2] = {0, 0};
int sCardInstant = -1;
std::atomic<bool> sCardUnplugged{false};

bool card_instant() {
  if (sCardInstant < 0) {
    const int32_t* timing = EngineSettings_IntSlot("system.card_timing");
    sCardInstant = timing != nullptr && *timing == EngineSettings_ChoiceIndex("system.card_timing", "instant") ? 1 : 0;
  }
  return sCardInstant == 1;
}

template <typename Run>
s32 card_timed(s32 chan, uint64_t cycles, CARDCallback callback, const char* what, Run run) {
  if (sCardUnplugged) {
    return CARD_RESULT_NOCARD;
  }
  if (chan < 0 || chan >= 2 || card_instant()) {
    return run(card_track(chan, callback));
  }
  const s32 result = run(nullptr);
  static const bool sLog = std::getenv("XD_CARD_LOG") != nullptr;
  const uint32_t flip = PortVI_GetFlipCount();
  std::lock_guard<std::mutex> guard(sLock);
  if (result < 0) {
    sCardOp[chan].pending = false;
    sCardHaveResult[chan] = true;
    sCardResult[chan] = result;
    if (sLog) std::fprintf(stderr, "[card log] frame %u %s -> %d at once\n", flip, what, static_cast<int>(result));
    return result;
  }
  uint64_t frames = (cycles + kCardFieldCycles - 1) / kCardFieldCycles;
  if (frames < 1) frames = 1;
  CardOp& op = sCardOp[chan];
  op.pending = true;
  op.due = flip + static_cast<uint32_t>(frames);
  op.result = result;
  op.callback = callback;
  if (sLog) {
    std::fprintf(stderr, "[card log] frame %u %s: %.1f ms, due %u\n", flip, what,
                 static_cast<double>(cycles) * 1000.0 / static_cast<double>(kCardCpuHz), op.due);
  }
  return CARD_RESULT_READY;
}

uint64_t card_write_cycles(s32 length) {
  const uint64_t sectors = (static_cast<uint64_t>(length > 0 ? length : 0) + 0x1FFFu) / 0x2000u;
  return sectors * kCardSector + kCardSector;
}
uint64_t card_read_cycles(s32 length) {
  return (static_cast<uint64_t>(length > 0 ? length : 0) + 511u) / 512u * kCardSegment;
}

std::unordered_map<ARQRequest*, void (*)(u32)> sArqPending;

struct ArqDone {
  void (*callback)(u32);
  ARQRequest* request;
};

void arq_deliver(void* arg) {
  auto* d = static_cast<ArqDone*>(arg);
  d->callback(static_cast<u32>(reinterpret_cast<uintptr_t>(d->request)));
  delete d;
}

void arq_done(uintptr_t requestAddress) {
  auto* request = reinterpret_cast<ARQRequest*>(requestAddress);
  void (*callback)(u32) = nullptr;
  {
    std::lock_guard<std::mutex> guard(sLock);
    auto it = sArqPending.find(request);
    if (it == sArqPending.end()) return;
    callback = it->second;
    sArqPending.erase(it);
  }
  PortOS_RaiseInterrupt(arq_deliver, new ArqDone{callback, request});
}

struct ArqTimed {
  ARQRequest* request;
  void (*callback)(u32);
  uint32_t remaining;
};
std::deque<ArqTimed> sArqHi, sArqLo;
bool sArqHiPending = false, sArqLoPending = false;
ArqTimed sArqHiCur{}, sArqLoCur{};
bool sArqLoLast = false;
int64_t sArqDoneTb = 0;
alignas(8) unsigned char sArqAlarm[64];
uint32_t arq_chunk() {
  const uint32_t n = ARQGetChunkSize();
  return n != 0 ? n : 0x1000u;
}

void arq_interrupt(void*, void*);

void arq_dma(int64_t tb, uint32_t length) {
  sArqDoneTb = tb + static_cast<int64_t>((static_cast<uint64_t>(length / 32u) * 246u + 11u) / 12u);
  PortOS_SetAlarmAt(sArqAlarm, sArqDoneTb, arq_interrupt);
}
void arq_pop_hi(int64_t tb) {
  if (sArqHi.empty()) return;
  sArqHiCur = sArqHi.front();
  sArqHi.pop_front();
  sArqHiPending = true;
  arq_dma(tb, sArqHiCur.remaining);
}
void arq_service_lo(int64_t tb) {
  if (!sArqLoPending && !sArqLo.empty()) {
    sArqLoCur = sArqLo.front();
    sArqLo.pop_front();
    sArqLoPending = true;
  }
  if (sArqLoPending) {
    const uint32_t chunk = arq_chunk();
    const uint32_t n = std::min(sArqLoCur.remaining, chunk);
    sArqLoLast = sArqLoCur.remaining <= chunk;
    sArqLoCur.remaining -= n;
    arq_dma(tb, n);
  }
}

void arq_interrupt(void*, void*) {
  std::unique_lock<std::mutex> guard(sLock);
  const int64_t tb = sArqDoneTb;
  ArqTimed done{};
  int which = 0;
  if (sArqHiPending) {
    done = sArqHiCur;
    which = 1;
  } else if (sArqLoPending && sArqLoLast) {
    done = sArqLoCur;
    which = 2;
  }
  guard.unlock();
  if (disc_log() && which != 0) {
    std::fprintf(stderr, "[arq done] req=%p len=0x%x flip=%u field=%u t=%lld abs=%lld\n", static_cast<void*>(done.request),
                 done.request != nullptr ? static_cast<unsigned>(done.request->length) : 0u, PortVI_GetFlipCount(),
                 VIGetRetraceCount(), static_cast<long long>(since_origin(tb)), static_cast<long long>(tb));
  }
  if (which != 0 && done.callback != nullptr) {
    const bool wasIn = tInArqInterrupt;
    const int64_t wasTb = tArqInterruptTb;
    tInArqInterrupt = true;
    tArqInterruptTb = tb;
    done.callback(static_cast<u32>(reinterpret_cast<uintptr_t>(done.request)));
    tInArqInterrupt = wasIn;
    tArqInterruptTb = wasTb;
  }
  guard.lock();
  if (which == 1) sArqHiPending = false;
  if (which == 2) {
    sArqLoPending = false;
    sArqLoLast = false;
  }
  arq_pop_hi(tb);
  if (!sArqHiPending) arq_service_lo(tb);
}

}

extern "C" {

void PortDVD_SetFileLocator(bool (*locator)(const char* path, uint32_t* offset)) { sFileLocator = locator; }
void PortDVD_SetFirstCommandTime(int64_t microseconds) { sFirstCommandUs = microseconds; }

BOOL PortDVD_Open(const char* fileName, DVDFileInfo* fileInfo) {
  const BOOL ok = DVDOpen(fileName, fileInfo);
  if (ok) {
    uint32_t start = 0;
    if (sFileLocator == nullptr || !sFileLocator(fileName, &start)) {
      static bool warned = false;
      if (!warned && sFileLocator != nullptr && drive_model()) {
        warned = true;
        std::fprintf(stderr, "[disc] %s isn't in the disc's file table (a mod's file?): timed as if at the start\n",
                     fileName);
      }
    }
    std::lock_guard<std::mutex> guard(sLock);
    sFileStart[fileInfo] = start;
  }
  return ok;
}

BOOL PortDVD_ReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  if (drive_model()) {
    drive_track(fileInfo);
    const BOOL ok = DVDReadAsyncPrio(fileInfo, addr, length, offset, dvd_done, prio);
    if (!ok) {
      std::lock_guard<std::mutex> guard(sLock);
      sDvdPending.erase(fileInfo);
      return ok;
    }
    auto* cmd = new DriveCmd;
    cmd->info = fileInfo;
    cmd->callback = callback;
    cmd->disc = file_start(fileInfo) + static_cast<uint32_t>(offset);
    cmd->length = static_cast<uint32_t>(length > 0 ? length : 0);
    drive_request(cmd);
    return ok;
  }
  const BOOL ok =
      DVDReadAsyncPrio(fileInfo, addr, length, offset, dvd_track(fileInfo, callback, length, offset, false), prio);
  if (!ok) {
    std::lock_guard<std::mutex> guard(sLock);
    sDvdPending.erase(fileInfo);
  }
  return ok;
}

int PortDVD_SeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, DVDCallback callback, s32 prio) {
  if (drive_model()) {
    drive_track(fileInfo);
    const int ok = DVDSeekAsyncPrio(fileInfo, offset, dvd_done, prio);
    if (!ok) {
      std::lock_guard<std::mutex> guard(sLock);
      sDvdPending.erase(fileInfo);
      return ok;
    }
    auto* cmd = new DriveCmd;
    cmd->info = fileInfo;
    cmd->callback = callback;
    cmd->seek = true;
    cmd->disc = file_start(fileInfo) + static_cast<uint32_t>(offset);
    drive_request(cmd);
    return ok;
  }
  const int ok = DVDSeekAsyncPrio(fileInfo, offset, dvd_track(fileInfo, callback, 0, offset, true), prio);
  if (!ok) {
    std::lock_guard<std::mutex> guard(sLock);
    sDvdPending.erase(fileInfo);
  }
  return ok;
}

s32 PortDVD_ReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  if (!drive_model()) {
    const s32 r = DVDReadPrio(fileInfo, addr, length, offset, prio);
    if (disc_log()) {
      uint64_t seq;
      {
        std::lock_guard<std::mutex> guard(sLock);
        seq = ++sDvdSeq;
      }
      const long long now = static_cast<long long>(since_origin(PortTime_Now()));
      std::fprintf(stderr, "[disc cmd] seq=%llu kind=sync disc=0x%x len=0x%x issue=%lld done=%lld flip=%u field=%u\n",
                   static_cast<unsigned long long>(seq), file_start(fileInfo) + static_cast<uint32_t>(offset),
                   static_cast<unsigned>(length), now, now, PortVI_GetFlipCount(), VIGetRetraceCount());
      log_done(seq);
    }
    return r;
  }
  drive_track(fileInfo);
  if (!DVDReadAsyncPrio(fileInfo, addr, length, offset, dvd_done, prio)) {
    std::lock_guard<std::mutex> guard(sLock);
    sDvdPending.erase(fileInfo);
    return -1;
  }
  auto* cmd = new DriveCmd;
  cmd->info = fileInfo;
  cmd->sync = true;
  cmd->disc = file_start(fileInfo) + static_cast<uint32_t>(offset);
  cmd->length = static_cast<uint32_t>(length > 0 ? length : 0);
  drive_request(cmd);
  for (;;) {
    {
      std::lock_guard<std::mutex> guard(sLock);
      if (cmd->finished) break;
    }
    OSSleepThread(&sSyncQueue);
  }
  const s32 result = cmd->result;
  delete cmd;
  return result;
}

s32 PortDVD_GetCommandBlockStatus(const DVDCommandBlock* block) {
  {
    std::lock_guard<std::mutex> guard(sLock);
    if (sDvdPending.count(reinterpret_cast<DVDFileInfo*>(const_cast<DVDCommandBlock*>(block))) != 0) {
      return DVD_STATE_BUSY;
    }
  }
  return DVDGetCommandBlockStatus(block);
}

s32 PortDVD_GetDriveStatus(void) {
  {
    std::lock_guard<std::mutex> guard(sLock);
    if (!sDvdPending.empty()) {
      return DVD_STATE_BUSY;
    }
  }
  return DVDGetDriveStatus();
}

void PortDVD_OnFlip(uint32_t frame) {
  std::vector<std::pair<DVDFileInfo*, DvdOp>> due;
  {
    std::unique_lock<std::mutex> guard(sLock);
    for (auto& [info, op] : sDvdPending) {
      if (op.due <= frame) {
        if (!sDvdWorkerCv.wait_for(guard, std::chrono::seconds(10), [&] { return op.workerDone; })) {
          std::fprintf(stderr, "[disc] a read due at frame %u didn't finish in 10 s; completing it anyway\n", op.due);
          op.result = DVD_RESULT_FATAL_ERROR;
        }
        due.emplace_back(info, op);
      }
    }
    for (auto& [info, op] : due) {
      sDvdPending.erase(info);
    }
  }
  std::sort(due.begin(), due.end(), [](const auto& a, const auto& b) {
    return a.second.due != b.second.due ? a.second.due < b.second.due : a.second.seq < b.second.seq;
  });
  for (auto& [info, op] : due) {
    if (op.callback != nullptr) {
      PortOS_RaiseInterrupt(dvd_deliver, new DvdDone{op.callback, op.result, info, op.doneTick, frame, op.seq});
    }
  }
}

void PortARQ_PostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, u32 source, u32 dest, u32 length,
                         void (*callback)(u32)) {
  PortCpuCost_Count(6u, length);
  if (drive_model()) {
    ARQPostRequest(request, owner, type, priority, source, dest, length, nullptr);
    std::lock_guard<std::mutex> guard(sLock);
    if (disc_log()) {
      std::fprintf(stderr, "[arq post] req=%p len=0x%x prio=%u busy=%d flip=%u field=%u t=%lld abs=%lld\n",
                   static_cast<void*>(request), length, priority, (sArqHiPending || sArqLoPending) ? 1 : 0,
                   PortVI_GetFlipCount(), VIGetRetraceCount(), static_cast<long long>(since_origin(timed_now())),
                   static_cast<long long>(timed_now()));
    }
    const ArqTimed t{request, callback, length};
    (priority == ARQ_PRIORITY_HIGH ? sArqHi : sArqLo).push_back(t);
    if (!sArqHiPending && !sArqLoPending) {
      const int64_t tb = timed_now();
      arq_pop_hi(tb);
      if (!sArqHiPending) arq_service_lo(tb);
    }
    return;
  }
  if (callback != nullptr) {
    std::lock_guard<std::mutex> guard(sLock);
    sArqPending[request] = callback;
  }
  ARQPostRequest(request, owner, type, priority, source, dest, length, callback != nullptr ? arq_done : nullptr);
}

s32 PortCARD_MountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
  {
    std::lock_guard<std::mutex> guard(sLock);
    if (chan >= 0 && chan < 2) {
      sCardDetach[chan] = detachCallback;
    }
  }
  return card_timed(chan, 5u * (0x2000u / 512u) * kCardSegment, attachCallback, "mount", [&](CARDCallback cb) {
    return CARDMountAsync(chan, workArea, detachCallback ? card_detached : nullptr, cb);
  });
}
s32 PortCARD_CheckAsync(s32 chan, CARDCallback cb) {
  if (sCardUnplugged) return CARD_RESULT_NOCARD;
  const s32 result = CARDCheckAsync(chan, card_track(chan, cb));
  if (chan >= 0 && chan < 2 && !card_instant()) {
    std::lock_guard<std::mutex> guard(sLock);
    sCardHaveResult[chan] = true;
    sCardResult[chan] = result;
  }
  return result;
}
s32 PortCARD_FormatAsync(s32 chan, CARDCallback cb) {
  const s32 result = card_timed(chan, 5u * kCardSector, cb, "format",
                                [&](CARDCallback c) { return CARDFormatAsync(chan, c); });
  if (result >= 0) PortCARD_NewSerial(chan);
  return result;
}
s32 PortCARD_CreateAsync(s32 chan, const char* name, u32 size, CARDFileInfo* fi, CARDCallback cb) {
  return card_timed(chan, 2u * kCardSector, cb, "create",
                    [&](CARDCallback c) { return CARDCreateAsync(chan, name, size, fi, c); });
}
s32 PortCARD_DeleteAsync(s32 chan, const char* name, CARDCallback cb) {
  return card_timed(chan, 2u * kCardSector, cb, "delete",
                    [&](CARDCallback c) { return CARDDeleteAsync(chan, name, c); });
}
s32 PortCARD_SetAttributesAsync(s32 chan, s32 fileNo, u8 attr, CARDCallback cb) {
  return card_timed(chan, kCardSector, cb, "set attributes",
                    [&](CARDCallback c) { return CARDSetAttributesAsync(chan, fileNo, attr, c); });
}
typedef int (*PortCardReadFilter)(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, s32* result);
typedef int (*PortCardWriteFilter)(const CARDFileInfo* fi, const void* addr, s32 length, s32 offset, s32* result);
static PortCardReadFilter sCardReadFilter = nullptr;
static PortCardWriteFilter sCardWriteFilter = nullptr;
void PortCARD_SetFilters(PortCardReadFilter read, PortCardWriteFilter write) {
  sCardReadFilter = read;
  sCardWriteFilter = write;
}

s32 PortCARD_ReadAsync(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, CARDCallback cb) {
  return card_timed(fi->chan, card_read_cycles(length), cb, "read", [&](CARDCallback c) {
    s32 result = 0;
    if (sCardReadFilter != nullptr && sCardReadFilter(fi, addr, length, offset, &result)) {
      if (c != nullptr) c(fi->chan, result);
      return result;
    }
    return CARDReadAsync(fi, addr, length, offset, c);
  });
}
s32 PortCARD_SetStatusAsync(s32 chan, s32 fileNo, const CARDStat* stat, CARDCallback cb) {
  return card_timed(chan, kCardSector, cb, "set status",
                    [&](CARDCallback c) { return CARDSetStatusAsync(chan, fileNo, stat, c); });
}
void PortCardBackup_BeforeWrite(s32 chan, s32 fileNo);
s32 PortCARD_WriteAsync(const CARDFileInfo* fi, const void* addr, s32 length, s32 offset, CARDCallback cb) {
  if (fi != nullptr) PortCardBackup_BeforeWrite(fi->chan, fi->fileNo);
  return card_timed(fi->chan, card_write_cycles(length), cb, "write", [&](CARDCallback c) {
    s32 result = 0;
    if (sCardWriteFilter != nullptr && sCardWriteFilter(fi, addr, length, offset, &result)) {
      if (c != nullptr) c(fi->chan, result);
      return result;
    }
    return CARDWriteAsync(fi, addr, length, offset, c);
  });
}

s32 PortCARD_GetResultCode(s32 chan) {
  if (sCardUnplugged) return CARD_RESULT_NOCARD;
  if (chan >= 0 && chan < 2) {
    std::lock_guard<std::mutex> guard(sLock);
    if (sCardOp[chan].pending) return CARD_RESULT_BUSY;
    if (sCardHaveResult[chan]) return sCardResult[chan];
  }
  return CARDGetResultCode(chan);
}

bool PortCARD_Busy(void) {
  std::lock_guard<std::mutex> guard(sLock);
  return sCardOp[0].pending || sCardOp[1].pending;
}

void PortCARD_OnFlip(uint32_t frame) {
  for (s32 chan = 0; chan < 2; chan++) {
    CARDCallback callback = nullptr;
    s32 result = 0;
    {
      std::lock_guard<std::mutex> guard(sLock);
      CardOp& op = sCardOp[chan];
      if (!op.pending || static_cast<int32_t>(frame - op.due) < 0) continue;
      op.pending = false;
      sCardHaveResult[chan] = true;
      sCardResult[chan] = op.result;
      callback = op.callback;
      result = op.result;
      op.callback = nullptr;
    }
    if (callback != nullptr) {
      PortOS_RaiseInterrupt(card_deliver, new CardDone{callback, chan, result});
    }
  }
}

void PortCARD_SetUnplugged(bool unplugged) { sCardUnplugged = unplugged; }

s32 PortCARD_ProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
  if (sCardUnplugged) return CARD_RESULT_NOCARD;
  return CARDProbeEx(chan, memSize, sectorSize);
}
BOOL PortCARD_Probe(s32 chan) { return sCardUnplugged ? FALSE : CARDProbe(chan); }

extern "C" void PortCARD_ApplyRoot(void);
extern "C" bool PortCARD_RootMounted(const char* game);

void PortCARD_Init(void) {
  const DVDDiskID* id = DVDGetCurrentDiskID();
  char game[5] = {}, maker[3] = {};
  if (id != nullptr) {
    std::memcpy(game, id->gameName, 4);
    std::memcpy(maker, id->company, 2);
  }
  if (PortTime_IsDeterministic()) {
    CARDSetTimeSource(PortTime_CardSeconds);
  }
  PortCARD_ApplyRoot();
  CARDInit(game, maker);
  if (!PortCARD_RootMounted(game)) {
    std::fprintf(stderr, "[card] playing without the memory card\n");
    sCardUnplugged = true;
  }
}

}
