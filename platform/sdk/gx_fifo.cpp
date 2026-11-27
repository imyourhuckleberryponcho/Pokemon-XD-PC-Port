#include <dolphin/gx.h>
#include <gx/fifo.hpp>

#include "port_savestate.h"
#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long, unsigned long,
                                                                                  void**, unsigned long*);
#else
#include <execinfo.h>
#endif

extern "C" {
void PortOS_RaiseInterrupt(void (*handler)(void*), void* arg);
bool PortFrameCost_GpuDueField(int64_t* field);
uint32_t VIGetRetraceCount(void);
int OSDisableInterrupts(void);
int OSEnableInterrupts(void);
int OSRestoreInterrupts(int level);
void PortGXDump_Fifo(const void* data, uint32_t size);
void PortGXDump_DisplayList(const void* list, uint32_t size);
void PortGXDump_EndOfFrame(void);
}

namespace {
namespace af = aurora::gx::fifo;

struct Fifo {
  u8* base;
  u8* top;
  u32 size;
  u32 hiWatermark;
  u32 loWatermark;
  void* rdPtr;
  void* wrPtr;
  s32 count;
  u8 bind_cpu;
  u8 bind_gp;
};
static_assert(sizeof(Fifo) <= sizeof(GXFifoObj), "the SDK's FIFO object must fit in GXFifoObj");

Fifo* as_fifo(const GXFifoObj* f) { return reinterpret_cast<Fifo*>(const_cast<GXFifoObj*>(f)); }

constexpr u32 kGather = 32;
constexpr u32 kStagingSize = 8u << 20;
constexpr u32 kStagingReserve = 64u << 10;

[[noreturn]] void fatal(const char* what) {
  std::fprintf(stderr, "[platform/sdk] GX FIFO: %s\n", what);
  std::fflush(stderr);
  std::abort();
}

Fifo* sCpu = nullptr;
u8* sCpuWrite = nullptr;
bool sCpuWrap = false;

Fifo* sGp = nullptr;
u8* sGpBase = nullptr;
u32 sGpSize = 0;
u8* sGpRead = nullptr;
u8* sGpWrite = nullptr;
u8* sBreakPt = nullptr;
bool sBreakPtEnabled = false;
bool sAtBreakPt = false;

u16 sPeToken = 0;

void (*sBreakPtCallback)(void) = nullptr;
GXDrawSyncCallback sDrawSyncCallback = nullptr;
GXDrawDoneCallback sDrawDoneCallback = nullptr;

std::vector<u8> sStaging;
bool sRecording = false;
std::vector<std::pair<u32, u16>> sStagedTokens;
std::map<const u8*, u16> sRingTokens;

GXFifoObj* sInitFifo = nullptr;
bool linked() { return sCpu != nullptr && sCpu == sGp; }
}
extern "C" uint32_t PortVI_GetFlipCount(void);
namespace {
std::string describe_callers();
bool describe_on();
bool sFifoCallLog = std::getenv("XD_SAVESTATE_FIFO_CALLS") != nullptr;
void fifo_call_log(const char* what, const Fifo* f) {
  if (!sFifoCallLog) return;
  std::fprintf(stderr, "[fifocall] %s flip %u fifo %p wrPtr %ld rdPtr %ld size %u | cpu %p cpuWrite %ld\n", what, PortVI_GetFlipCount(), static_cast<const void*>(f),
               f ? static_cast<long>(static_cast<u8*>(f->wrPtr) - f->base) : -1L, f ? static_cast<long>(static_cast<u8*>(f->rdPtr) - f->base) : -1L, f ? f->size : 0u,
               static_cast<void*>(sCpu), sCpu ? static_cast<long>(sCpuWrite - sCpu->base) : -1L);
  if (describe_on()) std::fprintf(stderr, "[fifocall]   by %s\n", describe_callers().c_str());
}

void start_recording() {
  if (sStaging.empty()) {
    sStaging.resize(kStagingSize);
  }
  af::begin_display_list(sStaging.data(), kStagingSize);
  sRecording = true;
}

struct DirectToAurora {
  bool wasInDl = af::detail::sInDisplayList;
  DirectToAurora() { af::detail::sInDisplayList = false; }
  ~DirectToAurora() { af::detail::sInDisplayList = wasInDl; }
};

int (*sFrameEndProbe)(void) = nullptr;
struct HeldInterrupt {
  void (*handler)(void*);
  void* arg;
};
std::mutex sHeldLock;
std::vector<HeldInterrupt> sHeld;
int64_t sHeldUntil = 0;

void raise_gpu(void (*handler)(void*), void* arg) {
  {
    std::lock_guard<std::mutex> lock(sHeldLock);
    int64_t until = 0;
    if (!sHeld.empty() || (sFrameEndProbe != nullptr && sFrameEndProbe() != 0 && PortFrameCost_GpuDueField(&until))) {
      if (sHeld.empty()) sHeldUntil = until;
      sHeld.push_back({handler, arg});
      return;
    }
  }
  PortOS_RaiseInterrupt(handler, arg);
}

std::atomic<bool> sTraceOn{false};
void deliver_breakpoint(void*) {
  if (sTraceOn.load()) std::fprintf(stderr, "[gxtrace] breakpoint interrupt runs (callback %p)\n", reinterpret_cast<void*>(sBreakPtCallback));
  if (sBreakPtCallback != nullptr) {
    sBreakPtCallback();
  }
}
void deliver_token(void* arg) {
  if (sDrawSyncCallback != nullptr) {
    sDrawSyncCallback(static_cast<u16>(reinterpret_cast<uintptr_t>(arg)));
  }
}
void deliver_draw_done(void*) {
  if (sDrawDoneCallback != nullptr) {
    sDrawDoneCallback();
  }
}
void aurora_draw_done() { raise_gpu(deliver_draw_done, nullptr); }

void pass_token(u16 token) {
  af::drain();
  sPeToken = token;
  raise_gpu(deliver_token, reinterpret_cast<void*>(static_cast<uintptr_t>(token)));
}

void (*sDescribe)(const void*, char*, size_t) = nullptr;
std::map<const u8*, std::string> sOrigins;
std::string sLastOrigin;

bool describe_on() { return sDescribe != nullptr; }

std::string describe_callers() {
  void* frames[10];
#ifdef _WIN32
  const unsigned short n = RtlCaptureStackBackTrace(2, 10, frames, nullptr);
#else
  void* all[12];
  const int got = backtrace(all, 12);
  const unsigned short n = static_cast<unsigned short>(got > 2 ? got - 2 : 0);
  for (unsigned short i = 0; i < n; i++) frames[i] = all[i + 2];
#endif
  std::string s;
  char name[256];
  for (unsigned short i = 0; i < n; i++) {
    sDescribe(frames[i], name, sizeof(name));
    if (!s.empty()) s += " <- ";
    s += name;
  }
  return s;
}

void print_origin(const u8* from) {
  if (sDescribe == nullptr || sOrigins.empty()) return;
  auto it = sOrigins.upper_bound(from);
  if (it == sOrigins.begin()) return;
  --it;
  if (it->second != sLastOrigin) {
    sLastOrigin = it->second;
    std::fprintf(stderr, "[gx origin] commands at %p recorded by: %s\n", static_cast<const void*>(it->first),
                 it->second.c_str());
  }
}

bool sFeedLog = std::getenv("XD_GX_FEEDLOG") != nullptr;
void gp_feed(u8* from, u8* to) {
  print_origin(from);
  if (sFeedLog) {
    char hx[64] = {};
    for (int k = 0; k < 12 && from + k < to; k++) std::snprintf(hx + k * 3, 4, "%02x ", from[k]);
    std::fprintf(stderr, "[feed] flip %u ring %p..+%u from %ld size %ld | cpu %p cpuWr %ld gpRd %ld gpWr %ld bp %ld/%d | %s\n", PortVI_GetFlipCount(), static_cast<void*>(sGpBase), sGpSize,
                 static_cast<long>(from - sGpBase), static_cast<long>(to - from), static_cast<void*>(sCpu), sCpu ? static_cast<long>(sCpuWrite - sCpu->base) : -1L,
                 static_cast<long>(sGpRead - sGpBase), static_cast<long>(sGpWrite - sGpBase), sBreakPt ? static_cast<long>(sBreakPt - sGpBase) : -1L, sBreakPtEnabled ? 1 : 0, hx);
    if (const char* dump = std::getenv("XD_GX_FEEDDUMP"); dump != nullptr) {
      if (FILE* fp = std::fopen(dump, "wb")) {
        std::fwrite(from, 1, static_cast<size_t>(to - from), fp);
        std::fclose(fp);
      }
    }
  }
  DirectToAurora direct;
  auto it = sRingTokens.upper_bound(from);
  while (from < to) {
    u8* stop = to;
    const bool token = it != sRingTokens.end() && it->first <= to;
    if (token) {
      stop = const_cast<u8*>(it->first);
    }
    af::write_data(from, static_cast<u32>(stop - from));
    PortGXDump_Fifo(from, static_cast<u32>(stop - from));
    from = stop;
    if (token) {
      const u16 value = it->second;
      it = sRingTokens.erase(it);
      pass_token(value);
    }
  }
}

void gp_run() {
  if (sGp == nullptr || sGpBase == nullptr) {
    return;
  }
  u8* const end = sGpBase + sGpSize;
  bool fed = false;
  while (!sAtBreakPt && sGpRead != sGpWrite && !(sBreakPtEnabled && sGpRead == sBreakPt)) {
    u8* stop = sGpWrite > sGpRead ? sGpWrite : end;
    if (sBreakPtEnabled && sBreakPt > sGpRead && sBreakPt < stop) {
      stop = sBreakPt;
    }
    gp_feed(sGpRead, stop);
    sGpRead = stop == end ? sGpBase : stop;
    fed = true;
  }
  if (fed) {
    af::drain();
  }
  if (sBreakPtEnabled && !sAtBreakPt && sGpRead == sBreakPt) {
    sAtBreakPt = true;
    if (sTraceOn.load()) std::fprintf(stderr, "[gxtrace] chip reached the breakpoint at %ld: interrupt raised\n", static_cast<long>(sBreakPt - sGpBase));
    raise_gpu(deliver_breakpoint, nullptr);
  }
}

void ring_write(const u8* src, u32 n) {
  u8* const end = sCpu->base + sCpu->size;
  while (n > 0) {
    const u32 chunk = std::min<u32>(n, static_cast<u32>(end - sCpuWrite));
    sRingTokens.erase(sRingTokens.upper_bound(sCpuWrite), sRingTokens.upper_bound(sCpuWrite + chunk));
    std::memcpy(sCpuWrite, src, chunk);
    sCpuWrite += chunk;
    src += chunk;
    n -= chunk;
    if (sCpuWrite == end) {
      sCpuWrite = sCpu->base;
      sCpuWrap = true;
    }
  }
}

void syncStaged() {
  if (!sRecording) {
    return;
  }
  const u32 used = af::detail::sDlWritePos;
  if (used > kStagingSize - kStagingReserve) {
    fatal("more graphics commands between two sync points than the staging buffer holds");
  }
  const u32 n = af::end_display_list();
  start_recording();
  if (n == 0 || sCpu == nullptr) {
    return;
  }
  const u32 piece = linked() ? std::max<u32>(sCpu->size / 2 & ~(kGather - 1), kGather) : n;
  size_t token = 0;
  for (u32 done = 0; done < n;) {
    const u32 len = std::min(piece, n - done);
    u8* const at = sCpuWrite;
    if (sDescribe != nullptr) sOrigins[at] = describe_callers();
    ring_write(sStaging.data() + done, len);
    for (; token < sStagedTokens.size() && sStagedTokens[token].first <= done + len; ++token) {
      const u32 off = sStagedTokens[token].first - done;
      const u32 toEnd = static_cast<u32>(sCpu->base + sCpu->size - at);
      u8* addr = off <= toEnd ? at + off : sCpu->base + (off - toEnd);
      sRingTokens[addr] = sStagedTokens[token].second;
    }
    done += len;
    if (linked()) {
      sGpWrite = sCpuWrite;
      gp_run();
    }
  }
  sStagedTokens.clear();
}

u32 fifo_count(const Fifo* f) {
  s32 count = static_cast<s32>(static_cast<const u8*>(f->wrPtr) - static_cast<const u8*>(f->rdPtr));
  if (count < 0) {
    count += static_cast<s32>(f->size);
  }
  return static_cast<u32>(count);
}

void save_gp_state(Fifo* f) {
  f->rdPtr = sGpRead;
  f->wrPtr = sGpWrite;
  f->count = static_cast<s32>(fifo_count(f));
}

void write_bp(u32 value) {
  af::write_u8(0x61);
  af::write_u32(value);
}

}

extern "C" {

void PortGX_InitFifoBase(GXFifoObj* fifo, void* base, u32 size);
void PortGX_InitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr);
void PortGX_SetCPUFifo(GXFifoObj* fifo);
void PortGX_SetGPFifo(GXFifoObj* fifo);

void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWatermark, u32 loWatermark) {
  as_fifo(fifo)->hiWatermark = hiWatermark;
  as_fifo(fifo)->loWatermark = loWatermark;
}

void PortGX_InitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
  Fifo* f = as_fifo(fifo);
  f->base = static_cast<u8*>(base);
  f->top = static_cast<u8*>(base) + size - 4;
  f->size = size;
  f->count = 0;
  GXInitFifoLimits(fifo, size - 0x4000, (size >> 1) & ~0x1Fu);
  PortGX_InitFifoPtrs(fifo, base, base);
}

void PortGX_InitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {
  Fifo* f = as_fifo(fifo);
  const int level = OSDisableInterrupts();
  f->rdPtr = readPtr;
  f->wrPtr = writePtr;
  f->count = static_cast<s32>(fifo_count(f));
  OSRestoreInterrupts(level);
}

void* GXGetFifoBase(const GXFifoObj* fifo) { return as_fifo(fifo)->base; }
u32 GXGetFifoSize(const GXFifoObj* fifo) { return as_fifo(fifo)->size; }
void GXGetFifoLimits(const GXFifoObj* fifo, u32* hi, u32* lo) {
  *hi = as_fifo(fifo)->hiWatermark;
  *lo = as_fifo(fifo)->loWatermark;
}

void PortGX_TraceOrigins(void (*describe)(const void*, char*, size_t)) { sDescribe = describe; }

GXFifoObj* PortGX_GetCPUFifo(void) { return reinterpret_cast<GXFifoObj*>(sCpu); }
GXFifoObj* PortGX_GetGPFifo(void) { return reinterpret_cast<GXFifoObj*>(sGp); }

void PortGX_SetCPUFifo(GXFifoObj* fifo) {
  fifo_call_log("SetCPUFifo-in", as_fifo(fifo));
  const int level = OSDisableInterrupts();
  if (sCpu != nullptr) {
    syncStaged();
  }
  sCpu = as_fifo(fifo);
  sCpuWrite = static_cast<u8*>(sCpu->wrPtr);
  sCpuWrap = false;
  if (!sRecording && !af::in_display_list()) {
    start_recording();
  }
  if (linked()) {
    sGpWrite = sCpuWrite;
  }
  OSRestoreInterrupts(level);
}

void PortGX_SetGPFifo(GXFifoObj* fifo) {
  const int level = OSDisableInterrupts();
  syncStaged();
  sGp = as_fifo(fifo);
  sGpBase = sGp->base;
  sGpSize = sGp->size;
  sGpRead = static_cast<u8*>(sGp->rdPtr);
  sGpWrite = linked() ? sCpuWrite : static_cast<u8*>(sGp->wrPtr);
  sAtBreakPt = false;
  gp_run();
  OSRestoreInterrupts(level);
}

void PortGX_SaveCPUFifo(GXFifoObj* fifo) {
  fifo_call_log("SaveCPUFifo-in", as_fifo(fifo));
  GXFlush();
  const int level = OSDisableInterrupts();
  syncStaged();
  Fifo* f = as_fifo(fifo);
  if (f == sCpu) {
    f->wrPtr = sCpuWrite;
    if (linked()) {
      f->rdPtr = sGpRead;
    }
  }
  f->count = static_cast<s32>(fifo_count(f));
  OSRestoreInterrupts(level);
}

void GXSaveGPFifo(void* fifo) {
  const int level = OSDisableInterrupts();
  Fifo* f = static_cast<Fifo*>(fifo);
  if (f == sGp) {
    save_gp_state(f);
  }
  OSRestoreInterrupts(level);
}

void PortGX_GetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {
  Fifo* f = as_fifo(fifo);
  if (f == sCpu) {
    syncStaged();
    f->wrPtr = sCpuWrite;
  }
  if (f == sGp) {
    save_gp_state(f);
  } else {
    f->count = static_cast<s32>(fifo_count(f));
  }
  *readPtr = f->rdPtr;
  *writePtr = f->wrPtr;
  fifo_call_log("GetFifoPtrs-out", f);
}

void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underflow, u32* fifoCount, GXBool* cpuWrite,
                     GXBool* gpRead, GXBool* fifowrap) {
  Fifo* f = as_fifo(fifo);
  *fifowrap = GX_FALSE;
  if (f == sGp) {
    save_gp_state(f);
  }
  if (f == sCpu) {
    syncStaged();
    f->wrPtr = sCpuWrite;
    f->count = static_cast<s32>(fifo_count(f));
    *fifowrap = sCpuWrap ? GX_TRUE : GX_FALSE;
  }
  *overhi = static_cast<u32>(f->count) > f->hiWatermark;
  *underflow = static_cast<u32>(f->count) < f->loWatermark;
  *fifoCount = static_cast<u32>(f->count);
  *cpuWrite = f == sCpu;
  *gpRead = f == sGp;
}

u32 PortGX_ReadCPUFifoWriteReg(void) {
  syncStaged();
  const u32 phys = static_cast<u32>(reinterpret_cast<uintptr_t>(sCpuWrite)) & 0x03FFFFE0u;
  return phys | (sCpuWrap ? 1u << 26 : 0u);
}

void (*GXSetBreakPtCallback(void (*cb)(void)))(void) {
  const int level = OSDisableInterrupts();
  auto old = sBreakPtCallback;
  sBreakPtCallback = cb;
  OSRestoreInterrupts(level);
  return old;
}

void PortGX_EnableBreakPt(void* breakPt) {
  const int level = OSDisableInterrupts();
  sBreakPt = static_cast<u8*>(breakPt);
  sBreakPtEnabled = true;
  sAtBreakPt = false;
  OSRestoreInterrupts(level);
}

void PortGX_DisableBreakPt(void) {
  const int level = OSDisableInterrupts();
  sBreakPtEnabled = false;
  sBreakPt = nullptr;
  sAtBreakPt = false;
  gp_run();
  OSRestoreInterrupts(level);
}

void PortGX_GetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
  *overhi = GX_FALSE;
  *underlow = GX_FALSE;
  *readIdle = GX_TRUE;
  *cmdIdle = GX_TRUE;
  *brkpt = sAtBreakPt ? GX_TRUE : GX_FALSE;
}

void PortGX_Flush(void) {
  GXFlush();
  if (linked()) {
    syncStaged();
  }
}

void PortGX_SetDrawSync(u16 token) {
  if (!sRecording && af::in_display_list()) {
    fatal("GXSetDrawSync inside a display list is not supported yet (its token would pass at the wrong time)");
  }
  const int level = OSDisableInterrupts();
  write_bp(0x48000000u | token);
  write_bp(0x47000000u | token);
  if (sRecording) {
    sStagedTokens.emplace_back(af::detail::sDlWritePos, token);
  }
  GXFlush();
  if (!sRecording) {
    pass_token(token);
  } else if (linked()) {
    syncStaged();
  }
  OSRestoreInterrupts(level);
}

u16 PortGX_ReadDrawSync(void) { return sPeToken; }

GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb) {
  const int level = OSDisableInterrupts();
  GXDrawSyncCallback old = sDrawSyncCallback;
  sDrawSyncCallback = cb;
  OSRestoreInterrupts(level);
  return old;
}

GXDrawDoneCallback PortGX_SetDrawDoneCallback(GXDrawDoneCallback cb) {
  const int level = OSDisableInterrupts();
  GXDrawDoneCallback old = sDrawDoneCallback;
  sDrawDoneCallback = cb;
  GXSetDrawDoneCallback(aurora_draw_done);
  OSRestoreInterrupts(level);
  return old;
}

void PortGX_SetDrawDone(void) {
  const int level = OSDisableInterrupts();
  GXSetDrawDoneCallback(aurora_draw_done);
  GXSetDrawDone();
  if (linked() || !sRecording) {
    syncStaged();
  }
  OSRestoreInterrupts(level);
}

void PortGX_DrainForState(void) { af::drain(); }

extern "C++" {
void PortGX_DebugState(char* buf, size_t n) {
  auto off = [](const u8* p, const Fifo* f) -> long { return (p == nullptr || f == nullptr) ? -1L : static_cast<long>(p - f->base); };
  std::snprintf(buf, n, "cpu %p wr %ld wrap %d | gp %p rd %ld wr %ld | token %u rec %d staged %zu ring %zu held %zu dl %u bp %ld en %d at %d", static_cast<void*>(sCpu), off(sCpuWrite, sCpu), sCpuWrap ? 1 : 0,
                static_cast<void*>(sGp), off(sGpRead, sGp), off(sGpWrite, sGp), sPeToken, sRecording ? 1 : 0, sStagedTokens.size(), sRingTokens.size(), sHeld.size(), static_cast<unsigned>(af::detail::sDlWritePos),
                sBreakPt != nullptr && sGp != nullptr ? static_cast<long>(sBreakPt - sGp->base) : -1L, sBreakPtEnabled ? 1 : 0, sAtBreakPt ? 1 : 0);
}

namespace {
struct GxFifoState {
  Fifo* cpu; u8* cpuWrite; bool cpuWrap;
  Fifo* gp; u8* gpBase; u32 gpSize; u8* gpRead; u8* gpWrite; u8* breakPt; bool breakPtEnabled, atBreakPt;
  u16 peToken;
  bool recording;
  std::vector<u8> staging;
  std::vector<std::pair<u32, u16>> stagedTokens;
  std::map<const u8*, u16> ringTokens;
  std::vector<HeldInterrupt> held;
  int64_t heldUntil;
  GXFifoObj initObj;
};
}
std::any PortState_GxFifoSave(portstate::Why& why) {
  if (sRecording && (af::detail::sDlWritePos != 0 || !sStagedTokens.empty())) { why = "the graphics FIFO holds unsent commands"; return {}; }
  GxFifoState s{sCpu, sCpuWrite, sCpuWrap, sGp, sGpBase, sGpSize, sGpRead, sGpWrite, sBreakPt, sBreakPtEnabled, sAtBreakPt,
                sPeToken, sRecording, {}, sStagedTokens, sRingTokens, {}, 0};
  std::lock_guard<std::mutex> lock(sHeldLock);
  s.held = sHeld;
  s.heldUntil = sHeldUntil;
  if (sInitFifo != nullptr) std::memcpy(&s.initObj, sInitFifo, sizeof(GXFifoObj));
  return s;
}
void PortState_GxFifoLoad(const std::any& a) {
  const GxFifoState& s = *std::any_cast<GxFifoState>(&a);
  if (sRecording) { af::end_display_list(); sRecording = false; }
  if (sInitFifo != nullptr) std::memcpy(sInitFifo, &s.initObj, sizeof(GXFifoObj));
  sCpu = s.cpu; sCpuWrite = s.cpuWrite; sCpuWrap = s.cpuWrap;
  sGp = s.gp; sGpBase = s.gpBase; sGpSize = s.gpSize; sGpRead = s.gpRead; sGpWrite = s.gpWrite;
  sBreakPt = s.breakPt; sBreakPtEnabled = s.breakPtEnabled; sAtBreakPt = s.atBreakPt;
  sPeToken = s.peToken;
  sStagedTokens = s.stagedTokens; sRingTokens = s.ringTokens;
  if (s.recording) start_recording();
  std::lock_guard<std::mutex> lock(sHeldLock);
  sHeld = s.held; sHeldUntil = s.heldUntil;
}
bool PortState_GxFifoToBytes(const std::any& a, portstate::Out& out, portstate::Why& why) {
  const GxFifoState* s = std::any_cast<GxFifoState>(&a);
  if (s == nullptr) { why = "no state"; return false; }
  if (!s->staging.empty()) { why = "the graphics FIFO holds unsent commands"; return false; }
  out.pod(uint32_t{0x47463031});
  out.pod(s->cpu); out.pod(s->cpuWrite); out.pod(s->cpuWrap);
  out.pod(s->gp); out.pod(s->gpBase); out.pod(s->gpSize); out.pod(s->gpRead); out.pod(s->gpWrite);
  out.pod(s->breakPt); out.pod(s->breakPtEnabled); out.pod(s->atBreakPt);
  out.pod(s->peToken); out.pod(s->recording);
  out.u64(s->stagedTokens.size());
  for (const auto& t : s->stagedTokens) { out.pod(t.first); out.pod(t.second); }
  out.u64(s->ringTokens.size());
  for (const auto& t : s->ringTokens) { out.pod(t.first); out.pod(t.second); }
  out.u64(s->held.size());
  for (const HeldInterrupt& h : s->held) out.pod(h);
  out.pod(s->heldUntil);
  out.pod(s->initObj);
  return true;
}
bool PortState_GxFifoFromBytes(portstate::In& in, std::any& a) {
  if (in.pod<uint32_t>() != 0x47463031) return false;
  GxFifoState s{};
  s.cpu = in.pod<Fifo*>(); s.cpuWrite = in.pod<u8*>(); s.cpuWrap = in.pod<bool>();
  s.gp = in.pod<Fifo*>(); s.gpBase = in.pod<u8*>(); s.gpSize = in.pod<u32>(); s.gpRead = in.pod<u8*>(); s.gpWrite = in.pod<u8*>();
  s.breakPt = in.pod<u8*>(); s.breakPtEnabled = in.pod<bool>(); s.atBreakPt = in.pod<bool>();
  s.peToken = in.pod<u16>(); s.recording = in.pod<bool>();
  const uint64_t nStaged = in.u64();
  if (nStaged > (1u << 24)) return false;
  for (uint64_t i = 0; i < nStaged && in.ok(); i++) { const u32 o = in.pod<u32>(); const u16 t = in.pod<u16>(); s.stagedTokens.push_back({o, t}); }
  const uint64_t nRing = in.u64();
  if (nRing > (1u << 24)) return false;
  for (uint64_t i = 0; i < nRing && in.ok(); i++) { const u8* p = in.pod<const u8*>(); const u16 t = in.pod<u16>(); s.ringTokens[p] = t; }
  const uint64_t nHeld = in.u64();
  if (nHeld > (1u << 20)) return false;
  for (uint64_t i = 0; i < nHeld && in.ok(); i++) s.held.push_back(in.pod<HeldInterrupt>());
  s.heldUntil = in.pod<int64_t>();
  s.initObj = in.pod<GXFifoObj>();
  if (!in.ok()) return false;
  a = std::move(s);
  return true;
}
}

void PortGX_DrawDone(void) {
  if (sRecording && !linked()) {
    fatal("GXDrawDone with the CPU and GP FIFOs unlinked would wait forever");
  }
  PortGX_SetDrawDone();
  af::drain();
  OSRestoreInterrupts(OSDisableInterrupts());
}

void PortGX_BeginDisplayList(void* list, u32 size) {
  GXFlush();
  syncStaged();
  if (sRecording) {
    af::end_display_list();
    sRecording = false;
  }
  GXBeginDisplayList(list, size);
}

bool PortGX_InDisplayList(void) { return !sRecording && af::in_display_list(); }

u32 PortGX_EndDisplayList(void) {
  const u32 n = GXEndDisplayList();
  if (sCpu != nullptr) {
    start_recording();
  }
  return n;
}

void PortGX_CallDisplayList(const void* list, u32 nbytes) {
  if (sRecording && af::detail::sDlWritePos + nbytes + kStagingReserve > kStagingSize) {
    syncStaged();
  }
  if (sRecording) {
    PortGXDump_DisplayList(list, nbytes);
    GXCallDisplayListAt(list, nbytes);
  } else {
    GXCallDisplayList(list, nbytes);
  }
}

void PortGX_CopyDisp(void* dest, GXBool clear) {
  GXCopyDisp(dest, clear);
  if (linked()) {
    syncStaged();
  }
}

void PortGX_CopyTex(void* dest, GXBool clear) {
  GXCopyTex(dest, clear);
  if (linked()) {
    syncStaged();
  }
}

GXFifoObj* PortGX_Init(void* base, u32 size) {
  GXFifoObj* fifo = GXInit(base, size);
  sInitFifo = fifo;
  std::memset(fifo, 0, sizeof(*fifo));
  sCpu = sGp = nullptr;
  PortGX_InitFifoBase(fifo, base, size);
  PortGX_SetCPUFifo(fifo);
  PortGX_SetGPFifo(fifo);
  GXSetDrawDoneCallback(aurora_draw_done);
  return fifo;
}

void PortGX_EndOfFrame(void) {
  if (linked()) {
    syncStaged();
  }
  PortGXDump_EndOfFrame();
}

void PortGX_ReplayBegin(void) {
  af::drain();
  GXSetDrawDoneCallback(nullptr);
}

void PortGX_ReplayWrite(const void* data, u32 size) {
  DirectToAurora direct;
  af::write_data(data, size);
}

void PortGX_ReplayEnd(void) {
  af::drain();
  GXSetDrawDoneCallback(aurora_draw_done);
}

void PortGX_SetArrayLE(u32 attr, const void* data, u32 size, u8 stride, u8 le) {
  GXSetArray(static_cast<GXAttr>(attr), data, size != 0 ? size : GX_ARRAY_SIZE_UNKNOWN, stride, le != 0);
}

}

extern "C" void PortGX_OnRetrace(uint32_t count) {
  std::vector<HeldInterrupt> due;
  {
    std::lock_guard<std::mutex> lock(sHeldLock);
    if (sHeld.empty() || static_cast<int64_t>(count) < sHeldUntil) return;
    due.swap(sHeld);
  }
  for (const HeldInterrupt& h : due) PortOS_RaiseInterrupt(h.handler, h.arg);
}

extern "C" void PortGX_SetFrameEndProbe(int (*probe)(void)) { sFrameEndProbe = probe; }

extern "C" void PortGX_TraceBreakpoints(int on) { sTraceOn.store(on != 0); }
