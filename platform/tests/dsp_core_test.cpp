#include <gc_dsp.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
void on_log(void*, const char* message) { std::printf("     [dsp] %s\n", message); }

GcDsp* run(std::initializer_list<uint16_t> program, void (*setup)(GcDsp*) = nullptr) {
  GcDspHost host{};
  host.log = on_log;
  GcDsp* d = GcDsp_Create(&host);
  uint16_t* iram = GcDsp_Iram(d);
  int i = 0;
  for (uint16_t w : program) {
    iram[i++] = w;
  }
  if (setup != nullptr) {
    setup(d);
  }
  GcDsp_WriteControl(d, 0);
  GcDsp_Run(d, 10000);
  return d;
}

constexpr uint16_t HALT = 0x0021;
}

int main() {
  GcDsp* d = run({0x009E, 0x7FFF, 0x0200, 0x0001, HALT});
  check(GcDsp_Acc(d, 0) == 0x80000000ll, "ADDI adds to the middle of the 40-bit accumulator");
  check((GcDsp_Sr(d) & 0x10) && !(GcDsp_Sr(d) & 0x08) && !(GcDsp_Sr(d) & 0x02),
        "flags: above s32 set, not negative, no 40-bit overflow");
  GcDsp_Destroy(d);

  {
    GcDspHost host{};
    host.log = on_log;
    GcDsp* real = GcDsp_Create(&host);
    const uint16_t prog[] = {0x8100, 0x1005, 0x7600, HALT};
    std::memcpy(GcDsp_Iram(real), prog, sizeof(prog));
    GcDsp_WriteDmem(real, 0x10, 0xBEEF);
    GcDsp_WriteControl(real, 0);
    GcDsp* copy = GcDsp_Clone(real);
    GcDsp_WriteDmem(copy, 0x10, 0x1234);
    GcDsp_Run(copy, 10000);
    check(GcDsp_Acc(copy, 0) == 5 && GcDsp_ReadDmem(copy, 0x10) == 0x1234, "a clone runs its own program");
    check(GcDsp_Acc(real, 0) == 0 && GcDsp_Pc(real) == 0 && GcDsp_ReadDmem(real, 0x10) == 0xBEEF,
          "... and the original is untouched");
    GcDsp_Run(real, 10000);
    check(GcDsp_Acc(real, 0) == 5, "... and runs the same way afterwards");
    GcDsp_Destroy(copy);
    GcDsp_Destroy(real);
  }

  d = run({0x8100, 0x1103, 0x0003, 0x7600, HALT});
  check(GcDsp_Acc(d, 0) == 3, "BLOOPI runs its block 3 times");
  GcDsp_Destroy(d);

  d = run({0x8100, 0x1005, 0x7600, HALT});
  check(GcDsp_Acc(d, 0) == 5, "LOOPI repeats the next instruction 5 times");
  GcDsp_Destroy(d);

  d = run({0x0080, 0x0103, 0x0088, 0x0003, 0x0008, HALT});
  check(GcDsp_ReadReg(d, 0) == 0x0100, "IAR wraps inside a 4-word circular buffer (0x0103 -> 0x0100)");
  GcDsp_Destroy(d);
  d = run({0x0080, 0x0100, 0x0088, 0x0003, 0x0004, HALT});
  check(GcDsp_ReadReg(d, 0) == 0x0103, "DAR wraps the other way (0x0100 -> 0x0103)");
  GcDsp_Destroy(d);

  d = run({0x0802, 0x0AFD, 0x8A00, 0x9000, 0x6F00, HALT});
  check(GcDsp_Acc(d, 1) == -12, "MUL is signed and doubled in M2 mode (2 x -3 x 2 = -12)");
  GcDsp_Destroy(d);

  d = run({0x0080, 0x0100, 0x8048, HALT}, [](GcDsp* dsp) { GcDsp_Dram(dsp)[0x100] = 0x1234; });
  check(GcDsp_ReadReg(d, 25) == 0x1234 && GcDsp_ReadReg(d, 0) == 0x0101, "NX'L loads ax1.l from @ar0 and steps ar0");
  GcDsp_Destroy(d);

  d = run({0x16FC, 0x1234, 0x16FD, 0x5678, HALT});
  check(GcDsp_CpuPeekMail(d) == 0x92345678u, "mail written by the chip reaches the CPU with bit 31 set");
  check(GcDsp_CpuTakeMail(d) == 0x92345678u && GcDsp_CpuPeekMail(d) == 0x12345678u,
        "taking the mail clears the new bit");
  GcDsp_Destroy(d);

  d = run({0x8100, 0x0090, 0x00FF, 0x009E, 0xFFFF, 0x14FC, HALT});
  check(GcDsp_Acc(d, 0) == -0x1000, "ASR shifts right arithmetically (-0x10000 >> 4 = -0x1000)");
  GcDsp_Destroy(d);

  return ok ? 0 : 1;
}
