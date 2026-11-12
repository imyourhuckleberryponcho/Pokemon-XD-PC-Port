#include <dolphin/gx.h>
#include <gx/fifo.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

bool linked() { return sCpu != nullptr && sCpu == sGp; }

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

void deliver_breakpoint(void*) {
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

void gp_feed(u8* from, u8* to) {
  print_origin(from);
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

void sync() {
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
  const int level = OSDisableInterrupts();
  if (sCpu != nullptr) {
    sync();
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
  sync();
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
  GXFlush();
  const int level = OSDisableInterrupts();
  sync();
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
    sync();
    f->wrPtr = sCpuWrite;
  }
  if (f == sGp) {
    save_gp_state(f);
  } else {
    f->count = static_cast<s32>(fifo_count(f));
  }
  *readPtr = f->rdPtr;
  *writePtr = f->wrPtr;
}

void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underflow, u32* fifoCount, GXBool* cpuWrite,
                     GXBool* gpRead, GXBool* fifowrap) {
  Fifo* f = as_fifo(fifo);
  *fifowrap = GX_FALSE;
  if (f == sGp) {
    save_gp_state(f);
  }
  if (f == sCpu) {
    sync();
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
  sync();
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
    sync();
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
    sync();
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
    sync();
  }
  OSRestoreInterrupts(level);
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
  sync();
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
    sync();
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
    sync();
  }
}

void PortGX_CopyTex(void* dest, GXBool clear) {
  GXCopyTex(dest, clear);
  if (linked()) {
    sync();
  }
}

GXFifoObj* PortGX_Init(void* base, u32 size) {
  GXFifoObj* fifo = GXInit(base, size);
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
    sync();
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
