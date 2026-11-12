#include <aurora/aurora.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/os.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

constexpr u32 kSize = 0x10000;
alignas(32) u8 sImmedBuf[kSize];
alignas(32) u8 sBufA[kSize];
alignas(32) u8 sBufB[kSize];
alignas(32) u8 sBufC[kSize];
alignas(32) u8 sDisplayList[256];
GXFifoObj sFifoA, sFifoB, sFifoC, sSpare[2];

std::string sEvents;

struct Block {
  GXFifoObj* fifo;
  void* start;
  void* end;
};
std::vector<Block> sBlocks;
size_t sNextBlock = 0;

void process_block(const Block& b) {
  const int level = OSDisableInterrupts();
  GXFifoObj* spare = &sSpare[sNextBlock % 2];
  GXInitFifoBase(spare, GXGetFifoBase(b.fifo), GXGetFifoSize(b.fifo));
  GXInitFifoPtrs(spare, b.start, b.end);
  GXEnableBreakPt(b.end);
  GXSetGPFifo(spare);
  OSRestoreInterrupts(level);
}

void on_breakpoint() {
  GXBool a, b, readIdle, d, brk;
  GXGetGPStatus(&a, &b, &readIdle, &d, &brk);
  sEvents += brk ? "B" : "b?";
  if (++sNextBlock < sBlocks.size()) {
    process_block(sBlocks[sNextBlock]);
  }
}

void on_token(u16 token) { sEvents += "T" + std::to_string(token); }
void on_draw_done() { sEvents += "D"; }

Block record(GXFifoObj* fifo, u16 token) {
  void* rd;
  Block b{fifo, nullptr, nullptr};
  GXSetCPUFifo(fifo);
  GXGetFifoPtrs(fifo, &rd, &b.start);
  GXSetCullMode(GX_CULL_BACK);
  GXSetDrawSync(token);
  GXSaveCPUFifo(fifo);
  GXGetFifoPtrs(fifo, &rd, &b.end);
  return b;
}

bool contains(const void* from, const void* to, const u8* pattern, size_t n) {
  for (auto* p = static_cast<const u8*>(from); p + n <= to; ++p) {
    if (std::memcmp(p, pattern, n) == 0) {
      return true;
    }
  }
  return false;
}
}

extern "C" bool PortMem_ReserveMEM1(void);
extern "C" void PortMem_InitOS(uint32_t arenaLo);

int main(int argc, char* argv[]) {
  if (!PortMem_ReserveMEM1()) {
    std::printf("FAIL could not reserve game memory\n");
    return 1;
  }
  AuroraConfig config{};
  config.appName = "XD port - GX FIFO test";
  aurora_initialize(argc, argv, &config);
  PortMem_InitOS(0);
  OSGetCurrentThread();
  while (!aurora_begin_frame()) {
  }

  GXFifoObj* immed = GXInit(sImmedBuf, kSize);
  check(GXGetCPUFifo() == immed && GXGetGPFifo() == immed, "after GXInit the CPU and the chip share one FIFO");
  GXSetBreakPtCallback(on_breakpoint);
  GXSetDrawSyncCallback(on_token);
  GXSetDrawDoneCallback(on_draw_done);

  GXInitFifoBase(&sFifoA, sBufA, kSize);
  GXInitFifoBase(&sFifoB, sBufB, kSize);
  const Block a = record(&sFifoA, 1);
  const Block b = record(&sFifoB, 2);
  check(a.start == sBufA && a.end > a.start, "layer A's commands land in its own buffer");
  check(((uintptr_t)a.end & 31) == 0 && ((uintptr_t)b.end & 31) == 0, "write pointers advance in 32-byte steps");
  const u8 tokenCmd[] = {0x61, 0x48, 0x00, 0x00, 0x01};
  check(contains(a.start, a.end, tokenCmd, sizeof(tokenCmd)), "the token is a real GameCube command in the buffer");
  check(GXReadDrawSync() == 0 && sEvents.empty(), "nothing reaches the chip while only recording");

  sBlocks = {a, b};
  sNextBlock = 0;
  {
    const int level = OSDisableInterrupts();
    process_block(sBlocks[0]);
    check(sEvents.empty(), "with interrupts disabled, the chip's interrupts wait");
    OSRestoreInterrupts(level);
  }
  std::printf("     events: %s\n", sEvents.c_str());
  check(sEvents == "T1BT2B", "tokens and breakpoints arrive in stream order, each block sending the next");
  check(GXReadDrawSync() == 2, "the token register holds the last token passed");

  GXInitFifoBase(&sFifoC, sBufC, kSize);
  GXSetCPUFifo(&sFifoC);
  for (u32 i = 0; i < kSize + 64; ++i) {
    GXWGFifo.u8 = 0;
  }
  GXSaveCPUFifo(&sFifoC);
  check((PortGX_ReadCPUFifoWriteReg() >> 26 & 1) == 1, "writing past the end wraps and sets the wrap flag");
  GXSetCPUFifo(immed);
  GXSetCPUFifo(&sFifoC);
  check((PortGX_ReadCPUFifoWriteReg() >> 26 & 1) == 0, "re-attaching the FIFO clears the flag (as on the GameCube)");

  void *rd, *before, *after;
  GXSetCPUFifo(&sFifoA);
  GXGetFifoPtrs(&sFifoA, &rd, &before);
  GXBeginDisplayList(sDisplayList, sizeof(sDisplayList));
  GXSetCullMode(GX_CULL_FRONT);
  const u32 dlSize = GXEndDisplayList();
  GXSetDrawSync(9);
  GXSaveCPUFifo(&sFifoA);
  GXGetFifoPtrs(&sFifoA, &rd, &after);
  check(dlSize > 0 && dlSize % 32 == 0, "the display list is recorded into its own memory");
  const u8 token9[] = {0x61, 0x48, 0x00, 0x00, 0x09};
  check(contains(before, after, token9, sizeof(token9)), "recording into the layer buffer resumes after the list");

  *reinterpret_cast<volatile u8*>(0x80100010) = 0x5A;
  check(*reinterpret_cast<volatile u8*>(0xC0100010) == 0x5A, "game memory is mirrored uncached at 0xC0000000");
  auto* gameList = reinterpret_cast<u8*>(0x80100000);
  std::memcpy(gameList, sDisplayList, dlSize);
  GXGetFifoPtrs(&sFifoA, &rd, &before);
  GXCallDisplayList(gameList, dlSize);
  GXSetDrawSync(10);
  GXSaveCPUFifo(&sFifoA);
  GXGetFifoPtrs(&sFifoA, &rd, &after);
  const u8 callCmd[] = {0x40, 0x80, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<u8>(dlSize)};
  check(contains(before, after, callCmd, sizeof(callCmd)), "a list in game memory is called by address");
  check(!contains(before, after, sDisplayList, 8), "... and its commands aren't copied into the layer buffer");
  GXGetFifoPtrs(&sFifoA, &rd, &before);
  GXCallDisplayList(sDisplayList, dlSize);
  GXSetDrawSync(11);
  GXSaveCPUFifo(&sFifoA);
  GXGetFifoPtrs(&sFifoA, &rd, &after);
  check(contains(before, after, sDisplayList, 8), "a list outside game memory is copied in");

  GXGetFifoPtrs(&sFifoA, &rd, &before);
  GXSetChanMatColor(GX_COLOR0, GXColor{0xFF, 0xFF, 0xFF, 0xFF});
  GXSetChanMatColor(GX_ALPHA0, GXColor{0, 0, 0, 0});
  GXSetDrawSync(12);
  GXSaveCPUFifo(&sFifoA);
  GXGetFifoPtrs(&sFifoA, &rd, &after);
  const u8 matWhiteAlpha0[] = {0x10, 0x00, 0x00, 0x10, 0x0C, 0xFF, 0xFF, 0xFF, 0x00};
  check(contains(before, after, matWhiteAlpha0, sizeof(matWhiteAlpha0)),
        "setting a channel's alpha keeps its material RGB (HSD sets them separately)");

  sEvents.clear();
  GXSetCPUFifo(immed);
  GXSetGPFifo(immed);
  GXSetDrawSync(7);
  check(GXReadDrawSync() == 7, "with linked FIFOs a token passes straight away");
  GXDrawDone();
  std::printf("     events: %s\n", sEvents.c_str());
  check(sEvents == "T7D", "GXDrawDone returns after the draw-done interrupt");

  PortGX_EndOfFrame();
  aurora_end_frame();
  aurora_shutdown();
  return ok ? 0 : 1;
}
