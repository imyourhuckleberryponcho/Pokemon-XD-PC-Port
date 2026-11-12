#include <dsp_reference.h>
#include <gc_dsp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kMemSize = 0x20000, kAramSize = 0x40000;

struct Rng {
  uint64_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return static_cast<uint32_t>(s >> 16);
  }
  uint32_t below(uint32_t n) { return next() % n; }
  bool one_in(uint32_t n) { return below(n) == 0; }
};

struct Outside {
  std::vector<uint8_t> mem = std::vector<uint8_t>(kMemSize);
  std::vector<uint8_t> aram = std::vector<uint8_t>(kAramSize);
  int interrupts = 0, messages = 0;
};
uint8_t* host_memory(void* user, uint32_t address, uint32_t size) {
  auto* o = static_cast<Outside*>(user);
  const uint32_t a = address & (kMemSize / 2 - 1);
  return a + size <= kMemSize ? &o->mem[a] : nullptr;
}
void host_interrupt(void* user) { static_cast<Outside*>(user)->interrupts++; }
void host_log(void* user, const char*) { static_cast<Outside*>(user)->messages++; }

#define CHIP(NAME, TYPE, P)                                                                                            \
  struct NAME {                                                                                                        \
    using Chip = TYPE;                                                                                                 \
    static Chip* Create(const GcDspHost* h) { return P##Create(h); }                                                   \
    static void Destroy(Chip* d) { P##Destroy(d); }                                                                    \
    static Chip* Clone(const Chip* d) { return P##Clone(d); }                                                          \
    static void LoadIrom(Chip* d, const uint16_t* w) { P##LoadIrom(d, w); }                                            \
    static void InstallPortRom(Chip* d) { P##InstallPortRom(d); }                                                      \
    static int Run(Chip* d, int steps) { return P##Run(d, steps); }                                                    \
    static int RunUntilIdle(Chip* d, int steps) { return P##RunUntilIdle(d, steps); }                                  \
    static void WriteControl(Chip* d, uint16_t v) { P##WriteControl(d, v); }                                           \
    static void CpuSendMail(Chip* d, uint32_t m) { P##CpuSendMail(d, m); }                                             \
    static uint32_t CpuTakeMail(Chip* d) { return P##CpuTakeMail(d); }                                                 \
    static bool IsHalted(const Chip* d) { return P##IsHalted(d); }                                                     \
    static void WriteReg(Chip* d, int r, uint16_t v) { P##WriteReg(d, r, v); }                                         \
    static void WriteDmem(Chip* d, uint16_t a, uint16_t v) { P##WriteDmem(d, a, v); }                                  \
    static uint16_t Pc(const Chip* d) { return P##Pc(d); }                                                             \
    static void SetPc(Chip* d, uint16_t pc) { P##SetPc(d, pc); }                                                       \
    static void SetAcc(Chip* d, int n, int64_t v) { P##SetAcc(d, n, v); }                                              \
    static uint16_t* Iram(Chip* d) { return P##Iram(d); }                                                              \
    static uint16_t* Dram(Chip* d) { return P##Dram(d); }                                                              \
    static size_t SaveState(const Chip* d, uint8_t* out, size_t size, bool memories) {                                 \
      return P##SaveState(d, out, size, memories);                                                                     \
    }                                                                                                                  \
  }
CHIP(Real, GcDsp, GcDsp_);
CHIP(Ref, RefDsp, RefDsp_);

template <class C> struct Side {
  Outside out;
  typename C::Chip* dsp = nullptr;
  void create(uint64_t seed, bool portRom) {
    Rng r{seed};
    for (auto& b : out.mem) b = static_cast<uint8_t>(r.next());
    for (auto& b : out.aram) b = static_cast<uint8_t>(r.next());
    GcDspHost host{};
    host.user = &out;
    host.mainMemory = host_memory;
    host.aram = out.aram.data();
    host.aramSize = kAramSize;
    host.interruptCpu = host_interrupt;
    host.log = host_log;
    host.dramWordsHostOrder = (seed & 1) != 0;
    dsp = C::Create(&host);
    if (portRom) C::InstallPortRom(dsp);
    for (int i = 0; i < 0x1000; i++) C::Dram(dsp)[i] = static_cast<uint16_t>(r.next());
    for (int i = 0; i < 0x1000; i++) C::Iram(dsp)[i] = static_cast<uint16_t>(r.next());
    C::WriteControl(dsp, 0);
  }
};

uint16_t pick_address(Rng& r) {
  switch (r.below(8)) {
  case 0: case 1: case 2: case 3: return static_cast<uint16_t>(r.below(0x1000));
  case 4: return static_cast<uint16_t>(0x1000 + r.below(0x800));
  case 5: return static_cast<uint16_t>(0xFF00 + r.below(0x100));
  case 6: return static_cast<uint16_t>(0xFFA0 + r.below(0x60));
  default: return static_cast<uint16_t>(r.next());
  }
}

template <class C> void disturb(typename C::Chip* d, uint64_t seed) {
  Rng r{seed};
  const int changes = static_cast<int>(r.below(4));
  for (int i = 0; i < changes; i++) {
    const int reg = static_cast<int>(r.below(32));
    uint16_t v = static_cast<uint16_t>(r.next());
    if (reg < 4) v = pick_address(r);
    if (reg >= 4 && reg < 8 && r.one_in(2)) v = static_cast<uint16_t>(static_cast<int16_t>(r.below(9)) - 4);
    if (reg >= 8 && reg < 12 && !r.one_in(4)) v = r.one_in(2) ? 0xFFFF : static_cast<uint16_t>((1u << r.below(12)) - 1);
    if (reg == 18 && r.one_in(2)) v = 0xFF;
    if (reg == 19 && r.one_in(2)) v &= 0xE0FF;
    C::WriteReg(d, reg, v);
  }
  if (r.one_in(4)) {
    int64_t acc = static_cast<int64_t>(r.next()) << 32 | r.next();
    if (r.one_in(3)) acc = static_cast<int32_t>(r.next());
    if (r.one_in(8)) acc = r.one_in(2) ? 0 : (r.one_in(2) ? 0x7FFFFFFFFFll : -0x8000000000ll);
    C::SetAcc(d, static_cast<int>(r.below(2)), acc);
  }
  if (r.one_in(8)) {
    static const uint16_t formats[] = {0x00, 0x0A, 0x19, 0x05, 0x06, 0x03, 0x0C, 0x1A, 0x2A, 0x3F};
    C::WriteDmem(d, 0xFFD1, r.one_in(4) ? static_cast<uint16_t>(r.next()) : formats[r.below(10)]);
    const uint16_t start = static_cast<uint16_t>(r.below(0x8000));
    C::WriteDmem(d, 0xFFD4, static_cast<uint16_t>(r.one_in(16) ? 0x8000 : 0));
    C::WriteDmem(d, 0xFFD5, start);
    C::WriteDmem(d, 0xFFD6, 0);
    C::WriteDmem(d, 0xFFD7, static_cast<uint16_t>(start + r.below(64)));
    C::WriteDmem(d, 0xFFD8, static_cast<uint16_t>(r.one_in(16) ? 0x8000 : 0));
    C::WriteDmem(d, 0xFFD9, static_cast<uint16_t>(start + r.below(48)));
    C::WriteDmem(d, 0xFFDA, static_cast<uint16_t>(r.next()));
    C::WriteDmem(d, 0xFFDB, static_cast<uint16_t>(r.next()));
    C::WriteDmem(d, 0xFFDC, static_cast<uint16_t>(r.next()));
    C::WriteDmem(d, 0xFFDE, static_cast<uint16_t>(r.next()));
    for (int i = 0; i < 16; i++) C::WriteDmem(d, static_cast<uint16_t>(0xFFA0 + i), static_cast<uint16_t>(r.next()));
  }
  if (r.one_in(16)) {
    C::WriteDmem(d, 0xFFC9, static_cast<uint16_t>(r.below(4)));
    C::WriteDmem(d, 0xFFCD, static_cast<uint16_t>(r.below(0x1000)));
    C::WriteDmem(d, 0xFFCE, 0);
    C::WriteDmem(d, 0xFFCF, static_cast<uint16_t>(r.below(0x8000) & ~1u));
    C::WriteDmem(d, 0xFFEF, static_cast<uint16_t>(r.one_in(8) ? 1 : 0));
    if (r.one_in(2)) C::WriteDmem(d, 0xFFCB, static_cast<uint16_t>(r.below(96)));
  }
  if (r.one_in(32)) C::CpuSendMail(d, r.next());
  if (r.one_in(32)) C::CpuTakeMail(d);
  if (r.one_in(64)) C::WriteControl(d, 0x0002);
  if (C::IsHalted(d)) C::WriteControl(d, 0);
}

template <class C> void place(typename C::Chip* d, uint64_t seed, uint16_t word) {
  Rng r{seed};
  const uint16_t pc = static_cast<uint16_t>(r.below(0xFFC));
  uint16_t* iram = C::Iram(d);
  iram[pc] = word;
  iram[pc + 1] = r.one_in(2) ? pick_address(r) : static_cast<uint16_t>(r.next());
  iram[pc + 2] = static_cast<uint16_t>(r.next());
  C::SetPc(d, pc);
  if (r.one_in(6)) {
    C::WriteReg(d, 12, static_cast<uint16_t>(r.below(0x1000)));
    C::WriteReg(d, 14, static_cast<uint16_t>(pc + r.below(2)));
    C::WriteReg(d, 15, static_cast<uint16_t>(r.below(3)));
  }
}

int failures = 0;
long long compared = 0;

template <class A, class B> bool same(Side<A>& a, Side<B>& b, bool memories, const char* what, uint32_t detail) {
  static std::vector<uint8_t> sa(0x8000), sb(0x8000);
  const size_t na = A::SaveState(a.dsp, sa.data(), sa.size(), memories);
  const size_t nb = B::SaveState(b.dsp, sb.data(), sb.size(), memories);
  compared++;
  bool ok = na == nb && std::memcmp(sa.data(), sb.data(), na) == 0;
  size_t at = 0;
  if (!ok && na == nb) {
    while (sa[at] == sb[at]) at++;
  }
  const bool outsideOk = !memories || (a.out.mem == b.out.mem && a.out.aram == b.out.aram);
  const bool countsOk = a.out.interrupts == b.out.interrupts && a.out.messages == b.out.messages;
  if (ok && outsideOk && countsOk) return true;
  failures++;
  if (failures <= 20) {
    char text[64] = "";
    const uint16_t words[2] = {static_cast<uint16_t>(detail), 0};
    GcDsp_Disassemble(words, 2, 0, text, sizeof(text));
    std::printf("FAIL %s (%04X %s): %s\n", what, detail, text,
                !ok ? "chip state differs" : !outsideOk ? "main memory / ARAM differ" : "interrupts / messages differ");
    if (!ok) std::printf("     first different byte of the saved state: %zu (sizes %zu, %zu)\n", at, na, nb);
  }
  return false;
}

void every_word(int rounds, bool portRom) {
  Side<Real> a;
  Side<Ref> b;
  a.create(0x1234567 + portRom, portRom);
  b.create(0x1234567 + portRom, portRom);
  Rng r{0x9E3779B97F4A7C15ull + static_cast<uint64_t>(portRom)};
  long long n = 0;
  for (int round = 0; round < rounds; round++) {
    for (uint32_t word = 0; word < 0x10000; word++, n++) {
      const uint64_t seed = (static_cast<uint64_t>(r.next()) << 32 | r.next()) | 1;
      disturb<Real>(a.dsp, seed);
      disturb<Ref>(b.dsp, seed);
      place<Real>(a.dsp, seed ^ 0x5555, static_cast<uint16_t>(word));
      place<Ref>(b.dsp, seed ^ 0x5555, static_cast<uint16_t>(word));
      const int ra = Real::Run(a.dsp, 1), rb = Ref::Run(b.dsp, 1);
      if (ra != rb || !same(a, b, n % 251 == 0, "one instruction", word)) {
        if (ra != rb) { failures++; std::printf("FAIL one instruction %04X: ran %d, reference %d\n", word, ra, rb); }
        if (failures > 20) return;
        Real::Destroy(a.dsp);
        Ref::Destroy(b.dsp);
        a.create(seed, portRom);
        b.create(seed, portRom);
      }
    }
    same(a, b, true, "end of a round", 0);
  }
  Real::Destroy(a.dsp);
  Ref::Destroy(b.dsp);
}

uint16_t program_word(Rng& r) {
  int count = 0;
  const DspOpInfo* ops = GcDsp_MainOps(&count);
  if (r.one_in(8)) return static_cast<uint16_t>(r.next());
  for (;;) {
    const DspOpInfo& op = ops[r.below(static_cast<uint32_t>(count))];
    const uint16_t w = static_cast<uint16_t>(op.value | (r.next() & ~op.mask));
    if (GcDsp_DecodeMain(w) < 0) continue;
    if (w == 0x0021 && !r.one_in(16)) continue;
    if ((w & 0xFFE0) == 0x02C0 || (w & 0xFFF0) == 0x02D0) {
      if (!r.one_in(3)) continue;
    }
    return w;
  }
}

void programs(int count, bool irom, bool untilIdle) {
  Rng r{0xC0FFEE123ull + irom * 2 + untilIdle};
  for (int p = 0; p < count && failures <= 20; p++) {
    Side<Real> a;
    Side<Ref> b;
    const uint64_t seed = (static_cast<uint64_t>(r.next()) << 32 | r.next()) | 1;
    a.create(seed, !irom);
    b.create(seed, !irom);
    std::vector<uint16_t> words(0x1000);
    for (auto& w : words) w = program_word(r);
    for (int i = 0; i < 0x1000 - 8; i += 16 + static_cast<int>(r.below(48))) {
      switch (r.below(4)) {
      case 0: words[i] = static_cast<uint16_t>(0x1100 | (1 + r.below(6))); words[i + 1] = static_cast<uint16_t>(i + 2 + r.below(6)); break;
      case 1: words[i] = static_cast<uint16_t>(0x1000 | r.below(5)); break;
      case 2: words[i] = static_cast<uint16_t>(0x0290 | r.below(16)); words[i + 1] = static_cast<uint16_t>(r.below(0x1000)); break;
      default: words[i] = static_cast<uint16_t>(0x02B0 | r.below(16)); words[i + 1] = static_cast<uint16_t>(r.below(0x1000)); break;
      }
    }
    std::memcpy(Real::Iram(a.dsp), words.data(), 0x2000);
    std::memcpy(Ref::Iram(b.dsp), words.data(), 0x2000);
    if (irom) {
      std::vector<uint16_t> rom(0x1000);
      for (auto& w : rom) w = program_word(r);
      Real::LoadIrom(a.dsp, rom.data());
      Ref::LoadIrom(b.dsp, rom.data());
    }
    if (p % 7 == 3) {
      auto* ca = Real::Clone(a.dsp);
      auto* cb = Ref::Clone(b.dsp);
      Real::Destroy(a.dsp);
      Ref::Destroy(b.dsp);
      a.dsp = ca;
      b.dsp = cb;
    }
    for (int chunkNo = 0; chunkNo < 40; chunkNo++) {
      const uint64_t s = (static_cast<uint64_t>(r.next()) << 32 | r.next()) | 1;
      disturb<Real>(a.dsp, s);
      disturb<Ref>(b.dsp, s);
      if (r.one_in(4)) {
        const uint16_t pc = static_cast<uint16_t>(r.below(0x1000));
        Real::SetPc(a.dsp, pc);
        Ref::SetPc(b.dsp, pc);
      }
      const int chunk = 1 + static_cast<int>(r.below(400));
      int ra, rb = 0;
      if (untilIdle) {
        ra = Real::RunUntilIdle(a.dsp, chunk);
        rb = Ref::RunUntilIdle(b.dsp, chunk);
      } else {
        ra = Real::Run(a.dsp, chunk);
        for (int i = 0; i < chunk; i++) rb += Ref::Run(b.dsp, 1);
      }
      if (ra != rb) {
        failures++;
        std::printf("FAIL program %d: ran %d instructions, reference %d\n", p, ra, rb);
        break;
      }
      if (!same(a, b, true, irom ? "program with a ROM" : "program", static_cast<uint32_t>(p))) break;
    }
    Real::Destroy(a.dsp);
    Ref::Destroy(b.dsp);
  }
}

}

int main(int argc, char** argv) {
  const int rounds = argc >= 2 ? std::atoi(argv[1]) : 12;
  every_word(rounds, true);
  every_word(rounds / 3 + 1, false);
  std::printf("%s every instruction word, %d + %d times each, one instruction at a time\n", failures ? "FAIL" : "ok  ",
              rounds, rounds / 3 + 1);
  const int before = failures;
  programs(rounds * 50, false, false);
  programs(rounds * 25, false, true);
  programs(rounds * 25, true, false);
  std::printf("%s random programs run in one go (port's ROM routines; run until idle; with a made-up ROM)\n",
              failures > before ? "FAIL" : "ok  ");
  std::printf("%lld comparisons of the two chips' state, %d failures\n", compared, failures);
  return failures == 0 ? 0 : 1;
}
