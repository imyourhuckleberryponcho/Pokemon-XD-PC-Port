#include <dsp_reference.h>
#include <gc_dsp.h>
#include <port_disc.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kDspSlave = 0x80409040;
constexpr uint32_t kDspSlaveSize = 0x1EC0;
constexpr uint32_t kMem1Size = 0x01800000;
constexpr uint32_t kAramSize = 0x01000000;
constexpr uint32_t kCmdList = 0x80600000;
constexpr uint32_t kSurround = 0x80601000;
constexpr uint32_t kDest = 0x80602000;
constexpr uint32_t kDestSize = 0x280;
constexpr uint32_t kSpb = 0x80610000;
constexpr uint32_t kMain = 0x80611000;
constexpr uint32_t kMainSize = 0x780;
constexpr uint32_t kPb = 0x80620000;
constexpr uint32_t kPbSize = 0xC4;
constexpr uint32_t kPatch = 0x80630000;
constexpr uint32_t kItd = 0x80640000;
constexpr uint32_t kAuxA = 0x80650000;
constexpr uint32_t kAuxB = 0x80652000;
constexpr int kVoices = 4, kFrames = 4;
constexpr int kBudget = 4'000'000;

bool ok = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  ok &= cond;
}

const char* const kRegNames[32] = {"ar0", "ar1", "ar2", "ar3", "ix0", "ix1", "ix2", "ix3", "wr0", "wr1", "wr2",
                                   "wr3", "st0", "st1", "st2", "st3", "ac0.h", "ac1.h", "config", "sr",
                                   "prod.l", "prod.m1", "prod.h", "prod.m2", "ax0.l", "ax1.l", "ax0.h", "ax1.h",
                                   "ac0.l", "ac1.l", "ac0.m", "ac1.m"};

struct RomCall {
  uint16_t address = 0, from = 0;
  uint16_t in[32]{}, out[32]{};
  std::vector<uint16_t> dramIn;
  std::vector<std::pair<uint16_t, uint16_t>> dramChanged;
};

struct Machine {
  std::vector<uint8_t> mem1 = std::vector<uint8_t>(kMem1Size);
  std::vector<uint8_t> aram = std::vector<uint8_t>(kAramSize);
  int dirqs = 0;
  GcDsp* dsp = nullptr;
  uint64_t steps = 0;
  std::map<uint16_t, int> romCalls;
  std::vector<RomCall> trace;
  bool tracing = false;

  uint8_t* at(uint32_t gcAddress) { return &mem1[gcAddress & 0x01FFFFFF]; }
  void put16(uint32_t gcAddress, uint16_t v) {
    at(gcAddress)[0] = static_cast<uint8_t>(v >> 8);
    at(gcAddress)[1] = static_cast<uint8_t>(v);
  }
  std::vector<uint8_t> bytes(uint32_t gcAddress, uint32_t size) { return {at(gcAddress), at(gcAddress) + size}; }
};

uint8_t* host_memory(void* user, uint32_t address, uint32_t size) {
  auto* m = static_cast<Machine*>(user);
  const uint32_t a = address & 0x01FFFFFF;
  return static_cast<uint64_t>(a) + size <= m->mem1.size() ? &m->mem1[a] : nullptr;
}
void host_interrupt(void* user) { static_cast<Machine*>(user)->dirqs++; }
void host_log(void*, const char* message) { std::printf("     dsp: %s\n", message); }

void read_regs(Machine& m, uint16_t* regs) {
  for (int reg = 0; reg < 32; reg++) {
    if (reg < 0x0C || reg > 0x0F) regs[reg] = GcDsp_ReadReg(m.dsp, reg);
  }
}

void step(Machine& m) {
  const uint16_t before = GcDsp_Pc(m.dsp);
  RomCall call;
  if (m.tracing) read_regs(m, call.in);
  const int ran = GcDsp_Run(m.dsp, 1);
  if (before < 0x8000) m.steps += ran;
  const uint16_t after = GcDsp_Pc(m.dsp);
  if (before < 0x8000 && after >= 0x8000 && after != 0x8000) {
    m.romCalls[after]++;
    if (m.tracing) {
      call.address = after;
      call.from = before;
      call.dramIn.assign(GcDsp_Dram(m.dsp), GcDsp_Dram(m.dsp) + 0x1000);
      m.trace.push_back(call);
    }
  } else if (m.tracing && before >= 0x8000 && after < 0x8000 && !m.trace.empty()) {
    RomCall& c = m.trace.back();
    read_regs(m, c.out);
    const uint16_t* dram = GcDsp_Dram(m.dsp);
    for (uint16_t a = 0; a < 0x1000; a++) {
      if (dram[a] != c.dramIn[a]) c.dramChanged.emplace_back(a, dram[a]);
    }
  }
}

template <typename F> bool run_until(Machine& m, F done) {
  for (int i = 0; i < kBudget; i++) {
    if (done()) return true;
    if (GcDsp_IsHalted(m.dsp)) return false;
    step(m);
  }
  return done();
}
uint32_t receive(Machine& m) {
  if (!run_until(m, [&] { return (GcDsp_CpuPeekMail(m.dsp) & 0x80000000u) != 0; })) {
    std::printf("     no mail from the chip (pc %04X, %s)\n", GcDsp_Pc(m.dsp), GcDsp_IsHalted(m.dsp) ? "halted" : "running");
    return 0;
  }
  return GcDsp_CpuTakeMail(m.dsp);
}
uint32_t interrupt_and_receive(Machine& m) {
  const int before = m.dirqs;
  if (!run_until(m, [&] { return m.dirqs > before; })) {
    std::printf("     no interrupt from the chip (pc %04X, %s)\n", GcDsp_Pc(m.dsp),
                GcDsp_IsHalted(m.dsp) ? "halted" : "running");
    return 0;
  }
  return receive(m);
}
bool send(Machine& m, uint32_t mail) {
  GcDsp_CpuSendMail(m.dsp, mail);
  return run_until(m, [&] { return (GcDsp_CpuMailToDsp(m.dsp) & 0x80000000u) == 0; });
}

int64_t run_list(Machine& m, const std::vector<uint16_t>& list) {
  for (size_t i = 0; i < list.size(); i++) m.put16(kCmdList + static_cast<uint32_t>(i * 2), list[i]);
  const uint16_t size = static_cast<uint16_t>((list.size() * 2 + 3) & ~3u);
  const uint64_t before = m.steps;
  send(m, 0xBABE0000u | size);
  send(m, kCmdList);
  const uint32_t done = interrupt_and_receive(m);
  if (done != 0xDCD10002) return -1;
  const int64_t took = static_cast<int64_t>(m.steps - before);
  send(m, 0xCDD10003);
  for (int i = 0; i < 64; i++) step(m);
  const uint16_t pc = GcDsp_Pc(m.dsp);
  return pc >= 0x004A && pc <= 0x0051 ? took : -1;
}

void push_addr(std::vector<uint16_t>& list, uint32_t a) {
  list.push_back(static_cast<uint16_t>(a >> 16));
  list.push_back(static_cast<uint16_t>(a));
}

constexpr int16_t kCoefs[8][2] = {{0x0800, 0},           {0x1000, -0x0800},     {0x0F00, -0x0700},
                                  {0x0E00, -0x0600},     {0x0C00, -0x0400},     {0x0A00, -0x0200},
                                  {0x1C00, -0x0C00},     {0x0400, 0x0200}};
constexpr uint32_t kZeroBuffer = 0x0000;
constexpr uint32_t kSampleFrames = 200;

struct VoiceSetup {
  uint16_t srcSelect, coefSelect;
  uint32_t pitch;
  uint16_t volL, volR, volS;
  uint16_t envVolume, envDelta;
  bool loop, lowPass;
};
constexpr VoiceSetup kVoiceSetups[kVoices] = {
    {0, 0, 0x00010000, 0x6000, 0x2000, 0, 0x7FFF, 0, false, false},
    {1, 1, 0x0000C000, 0x3000, 0x5000, 0, 0x4000, 0x0010, false, false},
    {2, 2, 0x00018000, 0x4000, 0x4000, 0, 0x7000, 0, false, true},
    {0, 2, 0x00024000, 0x2000, 0x1000, 0x3000, 0x6000, 0xFFF0, true, false},
};

uint32_t lcg(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s >> 8;
}

void setup_voices(Machine& m) {
  uint32_t seed = 12345;
  for (int v = 0; v < kVoices; v++) {
    const VoiceSetup& vs = kVoiceSetups[v];
    const uint32_t sampleByte = 0x1000 + v * 0x1000;
    for (uint32_t f = 0; f < kSampleFrames; f++) {
      uint8_t* frame = &m.aram[sampleByte + f * 8];
      frame[0] = static_cast<uint8_t>((lcg(seed) % 8) << 4 | (lcg(seed) % 12));
      for (int i = 1; i < 8; i++) frame[i] = static_cast<uint8_t>(lcg(seed));
    }
    const uint32_t pb = kPb + v * 0x100, patch = kPatch + v * 0x80, itd = kItd + v * 0x40;
    std::memset(m.at(pb), 0, kPbSize);
    auto set = [&](uint32_t offset, uint16_t value) { m.put16(pb + offset, value); };
    set(0x04, pb >> 16); set(0x06, pb & 0xFFFF);
    set(0x44 + 0xA, patch >> 16); set(0x44 + 0xC, patch & 0xFFFF);
    set(0x36 + 0x2, itd >> 16); set(0x36 + 0x4, itd & 0xFFFF);
    const uint32_t length = kSampleFrames * 14;
    const uint32_t base = sampleByte * 2, addr = base + 2;
    set(0x6E + 0x2, 0);
    for (int i = 0; i < 8; i++) {
      set(0x7E + i * 4, static_cast<uint16_t>(kCoefs[i][0]));
      set(0x7E + i * 4 + 2, static_cast<uint16_t>(kCoefs[i][1]));
    }
    set(0x7E + 0x20, 0);
    set(0x7E + 0x22, m.aram[sampleByte]);
    set(0x10, 0);
    set(0x6E + 0xC, addr >> 16); set(0x6E + 0xE, addr & 0xFFFF);
    uint32_t loopAddr, endAddr;
    if (vs.loop) {
      const uint32_t loopStart = 28, loopLength = 56;
      uint32_t bn = loopStart / 14, bo = loopStart - bn * 14;
      loopAddr = base + bn * 16 + 2 + bo;
      endAddr = loopStart + loopLength - 1;
      bn = endAddr / 14;
      bo = endAddr - bn * 14;
      endAddr = base + bn * 16 + 2 + bo;
      set(0x6E, 1);
      set(0xB4, m.aram[sampleByte + (loopStart / 14) * 8]);
    } else {
      const uint32_t bn = (length - 1) / 14, bo = (length - 1) - bn * 14;
      endAddr = base + bn * 16 + 2 + bo;
      loopAddr = kZeroBuffer * 2 + 2;
    }
    set(0x6E + 0x4, loopAddr >> 16); set(0x6E + 0x6, loopAddr & 0xFFFF);
    set(0x6E + 0x8, endAddr >> 16); set(0x6E + 0xA, endAddr & 0xFFFF);
    set(0x08, vs.srcSelect);
    set(0x0A, vs.coefSelect);
    set(0xBA, vs.lowPass ? 1 : 0);
    if (vs.lowPass) { set(0xBA + 4, 0x3000); set(0xBA + 6, 0x5000); }
    set(0x0E, 1);
    set(0x12 + 0x00, vs.volL);
    set(0x12 + 0x04, vs.volR);
    set(0x12 + 0x1C, vs.volS);
    constexpr int kRampSamples = 160 * kFrames;
    if (v == 1 || v == 3) {
      set(0x12 + 0x02, static_cast<uint16_t>((0x7000 - vs.volL) / kRampSamples));
      set(0x12 + 0x06, static_cast<uint16_t>(-static_cast<int>(vs.volR) / kRampSamples));
    }
    uint16_t mixerCtrl = 3;
    if (vs.volS != 0) mixerCtrl |= 4;
    if (v == 1 || v == 3) mixerCtrl |= 8;
    if (v == 2) {
      set(0x12 + 0x08, 0x2000); set(0x12 + 0x0C, 0x1800);
      set(0x12 + 0x10, 0x1000); set(0x12 + 0x14, 0x0800);
      mixerCtrl |= 0x30 | 0x600;
    }
    if (v == 3) {
      set(0x12 + 0x08, 0x3000); set(0x12 + 0x0A, static_cast<uint16_t>(-0x3000 / kRampSamples));
      set(0x12 + 0x0C, 0x1000); set(0x12 + 0x0E, static_cast<uint16_t>(0x2000 / kRampSamples));
      set(0x12 + 0x20, 0x0C00);
      mixerCtrl |= 0x30 | 0x40 | 0x80;
    }
    set(0x0C, mixerCtrl);
    set(0xA6, static_cast<uint16_t>(vs.pitch >> 16)); set(0xA8, vs.pitch & 0xFFFF);
    set(0x64, vs.envVolume); set(0x66, vs.envDelta);
    const uint32_t next = v + 1 < kVoices ? kPb + (v + 1) * 0x100 : 0;
    set(0x00, next >> 16); set(0x02, next & 0xFFFF);
  }
}

std::vector<uint16_t> voices_list(int frame) {
  const uint32_t main = kMain + (frame & 1) * kMainSize;
  std::vector<uint16_t> l;
  l.push_back(0); push_addr(l, kSpb);
  l.push_back(2); push_addr(l, kPb);
  l.push_back(3);
  l.push_back(4); push_addr(l, kAuxA + (frame % 3) * kMainSize); push_addr(l, kAuxA + ((frame + 1) % 3) * kMainSize);
  l.push_back(5); push_addr(l, kAuxB + (frame % 3) * kMainSize); push_addr(l, kAuxB + ((frame + 1) % 3) * kMainSize);
  l.push_back(6); push_addr(l, main);
  l.push_back(17); push_addr(l, kSurround);
  l.push_back(9); push_addr(l, main);
  l.push_back(14); push_addr(l, kSurround); push_addr(l, kDest);
  l.push_back(15);
  return l;
}

struct Run {
  bool booted = false;
  uint16_t bootRegs[32]{};
  std::vector<uint8_t> silentDest, silentSurround;
  std::vector<uint16_t> silentDram;
  int64_t silentSteps = -1;
  std::vector<uint8_t> voiceOutput, voiceMain, voicePbs;
  std::vector<int64_t> voiceSteps;
  std::map<uint16_t, int> romCalls;
  std::vector<RomCall> trace;
};

bool boot(Machine& m, const std::vector<uint8_t>& program, Run& r) {
  GcDsp_WriteControl(m.dsp, 0x0004);
  GcDsp_WriteControl(m.dsp, 0x0001);
  const uint32_t greet = receive(m);
  check(greet == 0x8071FEED, "boot loader says it's ready (0x8071FEED)");
  const uint32_t bootMails[] = {0x80F3A001, kDspSlave, 0x80F3C002, 0,          0x80F3A002,
                                kDspSlaveSize, 0x80F3B002, 0,     0x80F3D001, 0x10};
  bool sent = true;
  for (uint32_t mail : bootMails) sent &= send(m, mail);
  check(sent, "boot loader takes the program's address, length and start");
  check(run_until(m, [&] { return GcDsp_Pc(m.dsp) == 0x0010; }), "the program starts at its init vector (0x10)");
  read_regs(m, r.bootRegs);
  bool loaded = true;
  for (size_t i = 0; i < program.size() / 2; i++) {
    loaded &= GcDsp_Iram(m.dsp)[i] == (program[i * 2] << 8 | program[i * 2 + 1]);
  }
  check(loaded, "IRAM holds the program");
  const uint32_t init = interrupt_and_receive(m);
  check(init == 0xDCD10000, "program interrupts the CPU to say it's initialised (0xDCD10000)");
  return init == 0xDCD10000;
}

Run run_program(const std::vector<uint8_t>& program, const std::vector<uint16_t>* irom,
                const std::vector<uint16_t>* coef, const char* label, bool tracing) {
  std::printf("-- %s\n", label);
  Machine m;
  m.tracing = tracing;
  std::memcpy(m.at(kDspSlave), program.data(), program.size());
  GcDspHost host{};
  host.user = &m;
  host.mainMemory = host_memory;
  host.aram = m.aram.data();
  host.aramSize = kAramSize;
  host.interruptCpu = host_interrupt;
  host.log = host_log;
  m.dsp = GcDsp_Create(&host);
  if (irom != nullptr) {
    GcDsp_LoadIrom(m.dsp, irom->data());
  } else {
    GcDsp_InstallPortRom(m.dsp);
  }
  if (coef != nullptr) GcDsp_LoadCoef(m.dsp, coef->data());
  Run r;
  r.booted = boot(m, program, r);

  std::memset(m.at(kDest), 0xAA, kDestSize);
  std::vector<uint16_t> silent = {17};
  push_addr(silent, kSurround);
  silent.push_back(14);
  push_addr(silent, kSurround);
  push_addr(silent, kDest);
  silent.push_back(15);
  r.silentSteps = r.booted ? run_list(m, silent) : -1;
  check(r.silentSteps >= 0, "silent list: finished (0xDCD10002), and the program waits for the next one");
  r.silentDest = m.bytes(kDest, kDestSize);
  r.silentSurround = m.bytes(kSurround, 160 * 4);
  r.silentDram.assign(GcDsp_Dram(m.dsp), GcDsp_Dram(m.dsp) + 0x1000);
  bool zero = true;
  for (uint8_t b : r.silentDest) zero &= b == 0;
  check(zero, "silent list: the output buffer is written, silent");

  setup_voices(m);
  bool allDone = r.silentSteps >= 0;
  for (int frame = 0; frame < kFrames && allDone; frame++) {
    std::memset(m.at(kDest), 0xAA, kDestSize);
    const int64_t took = run_list(m, voices_list(frame));
    r.voiceSteps.push_back(took);
    allDone &= took >= 0;
    const auto out = m.bytes(kDest, kDestSize);
    r.voiceOutput.insert(r.voiceOutput.end(), out.begin(), out.end());
  }
  check(allDone, "voices: " + std::to_string(kFrames) + " lists finished");
  bool sound = false;
  for (uint8_t b : r.voiceOutput) sound |= b != 0 && b != 0xAA;
  check(allDone && sound, "voices: the output buffer has sound in it");
  r.voiceMain = m.bytes(kMain, kMainSize * 2);
  for (uint32_t aux : {kAuxA, kAuxB}) {
    const auto b = m.bytes(aux, kMainSize * 3);
    r.voiceMain.insert(r.voiceMain.end(), b.begin(), b.end());
  }
  r.voicePbs = m.bytes(kPb, kVoices * 0x100);
  r.romCalls = m.romCalls;
  r.trace = m.trace;
  std::printf("     instructions: silent list %lld; voice lists", static_cast<long long>(r.silentSteps));
  for (int64_t s : r.voiceSteps) std::printf(" %lld", static_cast<long long>(s));
  std::printf("\n     calls into the ROM:");
  for (const auto& [address, count] : r.romCalls) std::printf(" %04X x%d", address, count);
  std::printf("\n");
  GcDsp_Destroy(m.dsp);
  return r;
}

bool read_words(const std::string& path, size_t count, std::vector<uint16_t>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::vector<uint8_t> bytes(count * 2);
  const bool full = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  out.resize(count);
  for (size_t i = 0; i < count; i++) out[i] = static_cast<uint16_t>(bytes[i * 2] << 8 | bytes[i * 2 + 1]);
  return full;
}

int count_diffs(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size()) return -1;
  int n = 0;
  for (size_t i = 0; i < a.size(); i++) n += a[i] != b[i];
  return n;
}

void write_trace(const char* path, const std::vector<RomCall>& trace) {
  FILE* f = std::fopen(path, "w");
  if (f == nullptr) return;
  for (const RomCall& c : trace) {
    std::fprintf(f, "call %04X from %04X\n", c.address, c.from);
    for (int reg = 0; reg < 32; reg++) {
      if (reg >= 0x0C && reg <= 0x0F) continue;
      std::fprintf(f, "  %-7s %04X -> %04X%s\n", kRegNames[reg], c.in[reg], c.out[reg], c.in[reg] != c.out[reg] ? "  *" : "");
    }
    for (const auto& [a, v] : c.dramChanged) std::fprintf(f, "  DRAM %04X: %04X -> %04X\n", a, c.dramIn[a], v);
  }
  std::fclose(f);
  f = std::fopen((std::string(path) + ".dram").c_str(), "wb");
  if (f == nullptr) return;
  for (const RomCall& c : trace) std::fwrite(c.dramIn.data(), 2, c.dramIn.size(), f);
  std::fclose(f);
}

void compare(const Run& a, const Run& b, const std::string& who, bool console) {
  std::printf("-- comparison: %s vs the folder's ROMs\n", who.c_str());
  std::string bootDiffers;
  for (int reg = 0; reg < 32; reg++) {
    if ((reg < 0x0C || reg > 0x0F) && a.bootRegs[reg] != b.bootRegs[reg]) {
      char text[48];
      std::snprintf(text, sizeof(text), " %s (ROM %04X, port %04X)", kRegNames[reg], b.bootRegs[reg], a.bootRegs[reg]);
      bootDiffers += text;
    }
  }
  if (console) {
    check(bootDiffers.empty(), "boot loader: the registers at the program's start are the ROM's" +
                                   (bootDiffers.empty() ? std::string() : " (differ:" + bootDiffers + ")"));
  } else {
    std::printf("     registers at the program's start that differ from this ROM's boot loader:%s\n",
                bootDiffers.empty() ? " none" : bootDiffers.c_str());
  }
  check(a.silentDest == b.silentDest && a.silentSurround == b.silentSurround,
        "silent list: same output and surround buffers");
  int dramDiffs = 0;
  for (size_t i = 0; i < a.silentDram.size(); i++) dramDiffs += a.silentDram[i] != b.silentDram[i];
  check(dramDiffs == 0, "silent list: same DRAM after it (" + std::to_string(dramDiffs) + " words differ)");
  check(a.silentSteps == b.silentSteps, "silent list: same number of program instructions");
  check(count_diffs(a.voiceOutput, b.voiceOutput) == 0,
        "voices: same output (" + std::to_string(count_diffs(a.voiceOutput, b.voiceOutput)) + " bytes differ)");
  check(count_diffs(a.voiceMain, b.voiceMain) == 0,
        "voices: same studio mix (" + std::to_string(count_diffs(a.voiceMain, b.voiceMain)) + " bytes differ)");
  check(count_diffs(a.voicePbs, b.voicePbs) == 0,
        "voices: same voice state afterwards (" + std::to_string(count_diffs(a.voicePbs, b.voicePbs)) + " bytes differ)");
  check(a.voiceSteps == b.voiceSteps, "voices: same number of program instructions per list");
}

void compare_table(const Run& a, const Run& b) {
  std::printf("-- comparison: port's own ROM routines and table vs the folder's ROMs\n");
  check(a.voiceOutput.size() == b.voiceOutput.size(), "voices: all lists finished on both");
  if (a.voiceOutput.size() != b.voiceOutput.size()) return;
  double signal = 0, noise = 0;
  int maxDiff = 0;
  for (size_t i = 0; i + 1 < a.voiceOutput.size(); i += 2) {
    const int x = static_cast<int16_t>(a.voiceOutput[i] << 8 | a.voiceOutput[i + 1]);
    const int y = static_cast<int16_t>(b.voiceOutput[i] << 8 | b.voiceOutput[i + 1]);
    signal += static_cast<double>(y) * y;
    noise += static_cast<double>(x - y) * (x - y);
    if (std::abs(x - y) > maxDiff) maxDiff = std::abs(x - y);
  }
  const double snr = noise > 0 ? 10 * std::log10(signal / noise) : 999;
  std::printf("     output differs by at most %d (of 32767); signal-to-difference %.1f dB\n", maxDiff, snr);
}

constexpr uint16_t kProbeReturn = 0x0F00;
constexpr int kProbeRegs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11,
                              0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

struct ProbeCase {
  uint16_t address = 0;
  uint16_t regs[32]{};
  std::vector<uint16_t> dram;
  std::string what;
};
struct ProbeResult {
  bool returned = false;
  uint16_t regs[32]{};
  int64_t acc[2]{};
  std::vector<uint16_t> dram;
};

ProbeResult run_probe(Machine& m, const ProbeCase& c) {
  std::memcpy(GcDsp_Dram(m.dsp), c.dram.data(), 0x1000 * sizeof(uint16_t));
  GcDsp_WriteReg(m.dsp, 0x13, c.regs[0x13]);
  for (int reg : kProbeRegs) {
    if (reg != 0x13) GcDsp_WriteReg(m.dsp, reg, c.regs[reg]);
  }
  GcDsp_WriteReg(m.dsp, 0x0C, kProbeReturn);
  GcDsp_SetPc(m.dsp, c.address);
  ProbeResult r;
  for (int i = 0; i < 100000 && !r.returned; i++) {
    GcDsp_Run(m.dsp, 1);
    r.returned = GcDsp_Pc(m.dsp) == kProbeReturn;
  }
  for (int reg : kProbeRegs) r.regs[reg] = GcDsp_ReadReg(m.dsp, reg);
  for (int n = 0; n < 2; n++) r.acc[n] = GcDsp_Acc(m.dsp, n);
  r.dram.assign(GcDsp_Dram(m.dsp), GcDsp_Dram(m.dsp) + 0x1000);
  return r;
}

std::vector<ProbeCase> probe_cases(int perRoutine) {
  std::vector<ProbeCase> cases;
  uint32_t seed = 20261111;
  auto rnd = [&] { return static_cast<uint16_t>(lcg(seed)); };
  for (uint16_t address : {0x80E7, 0x81F9, 0x8282, 0x845D}) {
    for (int n = 0; n < perRoutine; n++) {
      ProbeCase c;
      c.address = address;
      c.dram.resize(0x1000);
      const int kind = n % 8;
      for (auto& w : c.dram) w = rnd();
      const bool musyx = (n & 1) == 0;
      const uint16_t srcL = musyx ? 0x0CE0 : 0x0C00, srcR = musyx ? 0x0CE0 : 0x0C80;
      const uint16_t dstL = musyx ? static_cast<uint16_t>(0x0040 * (n % 5)) : 0x0200;
      const uint16_t dstR = musyx ? static_cast<uint16_t>(dstL + 0x0140) : 0x0400;
      const uint16_t vol = musyx ? 0x0B89 : 0x0A00, scratch = musyx ? 0x0E48 : 0x0900;
      for (int i = 0; i < 0x40; i++) {
        uint16_t s = rnd();
        if (kind == 1) s = static_cast<uint16_t>(static_cast<int16_t>(s) >> 6);
        if (kind == 2) s = (i & 1) ? 0x7FFF : 0x8000;
        if (kind == 3) s = (i % 3 == 0) ? 0x8000 : (i % 3 == 1) ? 0x7FFF : 0;
        c.dram[srcL + i] = s;
        if (!musyx) c.dram[srcR + i] = rnd();
      }
      for (uint16_t dst : {dstL, dstR}) {
        for (int i = 0; i < 32; i++) {
          uint32_t sum = static_cast<uint32_t>(rnd()) << 16 | rnd();
          if (kind == 0 || kind == 1) sum = static_cast<uint32_t>(static_cast<int32_t>(sum) >> 8);
          if (kind == 4) sum = 0x7FFF0000u | rnd();
          if (kind == 5) sum = 0x80000000u | rnd();
          if (kind == 6) sum = ((i & 1) ? 0x007F0000u : 0xFF800000u) | rnd();
          c.dram[dst + 2 * i] = static_cast<uint16_t>(sum >> 16);
          c.dram[dst + 2 * i + 1] = static_cast<uint16_t>(sum);
        }
      }
      for (int ch = 0; ch < 2; ch++) {
        uint16_t v = rnd(), d = rnd();
        switch ((n / 8) % 6) {
        case 0: v &= 0x7FFF; d = static_cast<uint16_t>(static_cast<int16_t>(d) >> 9); break;
        case 1: v = static_cast<uint16_t>(0x7F00 + (v & 0xFF)); d = d & 0x7F; break;
        case 2: v = v & 0xFF; d = static_cast<uint16_t>(-(d & 0x7F)); break;
        case 3: v = static_cast<uint16_t>(0x8000 + (v & 0xFF)); d = static_cast<uint16_t>(-(d & 0x7F)); break;
        case 4: v = static_cast<uint16_t>(0xFF00 + (v & 0xFF)); d = d & 0x7F; break;
        default: break;
        }
        c.dram[vol + ch * 2] = v;
        c.dram[vol + ch * 2 + 1] = d;
      }
      for (int reg : kProbeRegs) c.regs[reg] = rnd();
      c.regs[0x00] = srcL; c.regs[0x04] = srcR;
      c.regs[0x01] = vol;
      c.regs[0x02] = dstL; c.regs[0x05] = dstR;
      c.regs[0x03] = address == 0x8282 || address == 0x845D ? scratch : dstL;
      for (int reg = 0x08; reg <= 0x0B; reg++) c.regs[reg] = 0xFFFF;
      c.regs[0x12] = 0x00FF;
      static const uint16_t kSr[] = {0xDB64, 0xDB68, 0xDB40, 0xDB60, 0xDB48};
      c.regs[0x13] = kSr[(n / 3) % 5];
      c.regs[0x10] = static_cast<uint16_t>(static_cast<int8_t>(c.regs[0x10]));
      c.regs[0x11] = static_cast<uint16_t>(static_cast<int8_t>(c.regs[0x11]));
      c.regs[0x16] &= 0x00FF;
      char what[64];
      std::snprintf(what, sizeof(what), "%04X case %d (%s layout)", address, n, musyx ? "MusyX's" : "other");
      c.what = what;
      cases.push_back(c);
    }
  }
  return cases;
}

constexpr int kProbeCasesPerRoutine = 480;
constexpr uint32_t kConsoleProbeAnswers = 0xF747C09C;
constexpr uint32_t kConsoleIrom = 0x0757D772;

void fnv(uint32_t& h, const void* data, size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 0x01000193u;
}
void fnv_result(uint32_t& h, const ProbeResult& r) {
  for (uint16_t w : r.dram) fnv(h, &w, 2);
  for (int reg : kProbeRegs) fnv(h, &r.regs[reg], 2);
  for (int64_t acc : r.acc) fnv(h, &acc, 8);
}

void start_probe_machine(Machine& m, const std::vector<uint8_t>& program, const std::vector<uint16_t>* irom) {
  std::memcpy(m.at(kDspSlave), program.data(), program.size());
  GcDspHost host{};
  host.user = &m;
  host.mainMemory = host_memory;
  host.aram = m.aram.data();
  host.aramSize = kAramSize;
  host.interruptCpu = host_interrupt;
  host.log = host_log;
  m.dsp = GcDsp_Create(&host);
  if (irom != nullptr) {
    GcDsp_LoadIrom(m.dsp, irom->data());
  } else {
    GcDsp_InstallPortRom(m.dsp);
  }
  GcDsp_WriteControl(m.dsp, 0x0004);
  GcDsp_WriteControl(m.dsp, 0x0001);
  (void)receive(m);
}

void probe_port_routines(const std::vector<uint8_t>& program) {
  std::printf("-- the four mixing routines alone, on made-up inputs: the port's routines vs the console's recorded answers\n");
  Machine port;
  start_probe_machine(port, program, nullptr);
  uint32_t h = 0x811C9DC5u;
  bool returned = true;
  int cases = 0;
  for (const ProbeCase& c : probe_cases(kProbeCasesPerRoutine)) {
    const ProbeResult r = run_probe(port, c);
    returned &= r.returned;
    fnv_result(h, r);
    cases++;
  }
  char line[160];
  std::snprintf(line, sizeof(line), "%d cases: memory and registers afterwards as the console's ROM left them (checksum %08X, console %08X)",
                cases, h, kConsoleProbeAnswers);
  check(returned && h == kConsoleProbeAnswers, line);
  GcDsp_Destroy(port.dsp);
}

void probe_rom_routines(const std::vector<uint8_t>& program, const std::vector<uint16_t>& irom, bool console,
                        const char* detailPath) {
  std::printf("-- the four mixing routines alone, on made-up inputs: the folder's ROM vs the port's routines\n");
  if (!console) {
    std::printf("     (not a console's ROM: differences are reported, not counted as failures)\n");
  }
  Machine rom, port;
  Machine* machines[2] = {&rom, &port};
  start_probe_machine(rom, program, &irom);
  start_probe_machine(port, program, nullptr);
  uint32_t romAnswers = 0x811C9DC5u;
  FILE* detail = detailPath != nullptr ? std::fopen(detailPath, "w") : nullptr;
  struct Tally {
    int cases = 0, noReturn = 0, memory = 0, memoryWords = 0, acc[2] = {0, 0};
    std::map<int, int> regs;
  };
  std::map<uint16_t, Tally> tallies;
  auto report = [&](bool same, const std::string& what) {
    if (console) {
      check(same, what);
    } else {
      std::printf("%s %s\n", same ? "same" : "differs", what.c_str());
    }
  };
  for (const ProbeCase& c : probe_cases(kProbeCasesPerRoutine)) {
    const ProbeResult a = run_probe(rom, c), b = run_probe(port, c);
    fnv_result(romAnswers, a);
    Tally& t = tallies[c.address];
    t.cases++;
    if (!a.returned || !b.returned) {
      t.noReturn++;
      continue;
    }
    int words = 0;
    for (int i = 0; i < 0x1000; i++) words += a.dram[i] != b.dram[i];
    t.memory += words != 0;
    t.memoryWords += words;
    bool regsDiffer = false;
    for (int reg : kProbeRegs) {
      if (a.regs[reg] != b.regs[reg]) {
        t.regs[reg]++;
        regsDiffer = true;
      }
    }
    for (int n = 0; n < 2; n++) {
      if (a.acc[n] != b.acc[n]) {
        t.acc[n]++;
        regsDiffer = true;
      }
    }
    if (detail != nullptr && (words != 0 || regsDiffer)) {
      std::fprintf(detail, "%s: sr %04X, volumes %04X %04X %04X %04X\n", c.what.c_str(), c.regs[0x13],
                   c.dram[c.regs[1]], c.dram[c.regs[1] + 1], c.dram[c.regs[1] + 2], c.dram[c.regs[1] + 3]);
      for (int reg : kProbeRegs) {
        if (a.regs[reg] != b.regs[reg]) {
          std::fprintf(detail, "  %-7s in %04X  rom %04X  port %04X\n", kRegNames[reg], c.regs[reg], a.regs[reg], b.regs[reg]);
        }
      }
      for (int n = 0; n < 2; n++) {
        if (a.acc[n] != b.acc[n]) {
          std::fprintf(detail, "  ac%d whole: rom %010llX  port %010llX\n", n,
                       static_cast<unsigned long long>(a.acc[n]) & 0xFFFFFFFFFFull,
                       static_cast<unsigned long long>(b.acc[n]) & 0xFFFFFFFFFFull);
        }
      }
      int shown = 0;
      for (int i = 0; i < 0x1000 && shown < 40; i++) {
        if (a.dram[i] != b.dram[i]) {
          std::fprintf(detail, "  DRAM %04X: in %04X  rom %04X  port %04X\n", i, c.dram[i], a.dram[i], b.dram[i]);
          shown++;
        }
      }
      if (words > shown) std::fprintf(detail, "  ... %d words in all\n", words);
    }
  }
  if (detail != nullptr) std::fclose(detail);
  for (const auto& [address, t] : tallies) {
    char line[160];
    std::snprintf(line, sizeof(line), "routine %04X: %d cases, same memory afterwards (%d cases differ, %d words)",
                  address, t.cases, t.memory, t.memoryWords);
    report(t.noReturn == 0 && t.memory == 0, line);
    std::string regs;
    for (const auto& [reg, count] : t.regs) regs += std::string(" ") + kRegNames[reg] + " x" + std::to_string(count);
    for (int n = 0; n < 2; n++) {
      if (t.acc[n] != 0) regs += " ac" + std::to_string(n) + " whole x" + std::to_string(t.acc[n]);
    }
    std::snprintf(line, sizeof(line), "routine %04X: same registers afterwards", address);
    report(regs.empty(), std::string(line) + (regs.empty() ? "" : " (differ:" + regs + ")"));
  }
  if (console) {
    char line[120];
    std::snprintf(line, sizeof(line), "the ROM's answers are the recorded ones (checksum %08X, recorded %08X)", romAnswers,
                  kConsoleProbeAnswers);
    check(romAnswers == kConsoleProbeAnswers, line);
  }
  for (Machine* m : machines) GcDsp_Destroy(m->dsp);
}

bool bench_wait(Machine& m, bool (*done)(Machine&, int), int arg, uint64_t& ran) {
  for (int i = 0; i < 1'000'000; i++) {
    if (done(m, arg)) return true;
    if (GcDsp_IsHalted(m.dsp)) return false;
    ran += static_cast<uint64_t>(GcDsp_RunUntilIdle(m.dsp, kBudget));
  }
  return false;
}
bool bench_mail_taken(Machine& m, int) { return (GcDsp_CpuMailToDsp(m.dsp) & 0x80000000u) == 0; }
bool bench_mail_there(Machine& m, int) { return (GcDsp_CpuPeekMail(m.dsp) & 0x80000000u) != 0; }
bool bench_interrupted(Machine& m, int before) { return m.dirqs > before; }
bool bench_send(Machine& m, uint32_t mail, uint64_t& ran) {
  GcDsp_CpuSendMail(m.dsp, mail);
  return bench_wait(m, bench_mail_taken, 0, ran);
}

int bench(const std::vector<uint8_t>& program, const std::vector<uint16_t>* irom, const std::vector<uint16_t>* coef,
          int lists, int repeats) {
  double best = 0;
  uint32_t firstSum = 0;
  uint64_t firstRan = 0;
  for (int repeat = 0; repeat < repeats; repeat++) {
    Machine m;
    std::memcpy(m.at(kDspSlave), program.data(), program.size());
    GcDspHost host{};
    host.user = &m;
    host.mainMemory = host_memory;
    host.aram = m.aram.data();
    host.aramSize = kAramSize;
    host.interruptCpu = host_interrupt;
    host.log = host_log;
    m.dsp = GcDsp_Create(&host);
    if (irom != nullptr) {
      GcDsp_LoadIrom(m.dsp, irom->data());
    } else {
      GcDsp_InstallPortRom(m.dsp);
    }
    if (coef != nullptr) GcDsp_LoadCoef(m.dsp, coef->data());
    uint64_t ran = 0;
    uint32_t sum = 0x811C9DC5u;
    bool fine = true;
    GcDsp_WriteControl(m.dsp, 0x0004);
    GcDsp_WriteControl(m.dsp, 0x0001);
    fine &= bench_wait(m, bench_mail_there, 0, ran) && GcDsp_CpuTakeMail(m.dsp) == 0x8071FEED;
    for (uint32_t mail : {0x80F3A001u, kDspSlave, 0x80F3C002u, 0u, 0x80F3A002u, kDspSlaveSize, 0x80F3B002u, 0u,
                          0x80F3D001u, 0x10u}) {
      fine &= bench_send(m, mail, ran);
    }
    fine &= bench_wait(m, bench_interrupted, 0, ran) && bench_wait(m, bench_mail_there, 0, ran) &&
            GcDsp_CpuTakeMail(m.dsp) == 0xDCD10000;
    if (!fine) {
      std::printf("FAIL bench: the program didn't start\n");
      return 1;
    }
    const uint64_t bootRan = ran;
    const auto t0 = std::chrono::steady_clock::now();
    for (int n = 0; n < lists && fine; n++) {
      if (n % 16 == 0) setup_voices(m);
      std::memset(m.at(kDest), 0xAA, kDestSize);
      const std::vector<uint16_t> list = voices_list(n);
      for (size_t i = 0; i < list.size(); i++) m.put16(kCmdList + static_cast<uint32_t>(i * 2), list[i]);
      const uint16_t size = static_cast<uint16_t>((list.size() * 2 + 3) & ~3u);
      const uint64_t before = ran;
      const int dirqs = m.dirqs;
      fine &= bench_send(m, 0xBABE0000u | size, ran) && bench_send(m, kCmdList, ran);
      fine &= bench_wait(m, bench_interrupted, dirqs, ran) && bench_wait(m, bench_mail_there, 0, ran) &&
              GcDsp_CpuTakeMail(m.dsp) == 0xDCD10002;
      fine &= bench_send(m, 0xCDD10003, ran);
      const uint64_t took = ran - before;
      fnv(sum, &took, sizeof(took));
      fnv(sum, m.at(kDest), kDestSize);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!fine) {
      std::printf("FAIL bench: a list didn't finish (pc %04X)\n", GcDsp_Pc(m.dsp));
      return 1;
    }
    fnv(sum, GcDsp_Dram(m.dsp), 0x1000 * 2);
    fnv(sum, m.at(kMain), kMainSize * 2);
    fnv(sum, m.at(kAuxA), kMainSize * 3);
    fnv(sum, m.at(kAuxB), kMainSize * 3);
    fnv(sum, m.at(kPb), kVoices * 0x100);
    fnv(sum, m.at(kSurround), 160 * 4);
    uint16_t regs[32]{};
    read_regs(m, regs);
    fnv(sum, regs, sizeof(regs));
    for (int n = 0; n < 2; n++) {
      const int64_t acc = GcDsp_Acc(m.dsp, n);
      fnv(sum, &acc, sizeof(acc));
    }
    const uint16_t pc = GcDsp_Pc(m.dsp), sr = GcDsp_Sr(m.dsp);
    fnv(sum, &pc, 2);
    fnv(sum, &sr, 2);
    GcDsp_Destroy(m.dsp);
    const uint64_t listRan = ran - bootRan;
    const double mips = static_cast<double>(listRan) / seconds / 1e6;
    std::printf("bench %d: %d lists, %llu instructions (%.0f a list), %.3f s, %.1f million instructions a second, "
                "%.1f microseconds a list, checksum %08X\n",
                repeat + 1, lists, static_cast<unsigned long long>(listRan), static_cast<double>(listRan) / lists, seconds,
                mips, seconds / lists * 1e6, sum);
    if (repeat == 0) {
      firstSum = sum;
      firstRan = ran;
    } else if (sum != firstSum || ran != firstRan) {
      std::printf("FAIL bench: two runs of the same lists differ\n");
      return 1;
    }
    if (mips > best) best = mips;
  }
  std::printf("bench result: %s ROM, best %.1f million instructions a second, checksum %08X\n",
              irom != nullptr ? "the folder's" : "the port's", best, firstSum);
  return 0;
}
struct Lockstep {
  Machine a, b;
  RefDsp* ref = nullptr;
  bool perInstruction = true;
  uint64_t ran = 0, compared = 0;
  bool fine = true;
  std::vector<uint8_t> sa = std::vector<uint8_t>(0x8000), sb = std::vector<uint8_t>(0x8000);

  bool same(bool memories) {
    const size_t na = GcDsp_SaveState(a.dsp, sa.data(), sa.size(), memories);
    const size_t nb = RefDsp_SaveState(ref, sb.data(), sb.size(), memories);
    compared++;
    if (na != nb || std::memcmp(sa.data(), sb.data(), na) != 0 || a.dirqs != b.dirqs) {
      size_t at = 0;
      while (at < na && sa[at] == sb[at]) at++;
      std::printf("FAIL lockstep: the chips differ after %llu instructions (pc %04X / %04X, saved state byte %zu)\n",
                  static_cast<unsigned long long>(ran), GcDsp_Pc(a.dsp), RefDsp_Pc(ref), at);
      return fine = false;
    }
    if (memories && (a.mem1 != b.mem1 || a.aram != b.aram)) {
      std::printf("FAIL lockstep: main memory or ARAM differ after %llu instructions\n", static_cast<unsigned long long>(ran));
      return fine = false;
    }
    return true;
  }
  enum Wait { MAIL_TAKEN, MAIL_THERE, INTERRUPTED };
  bool done(Wait w, int before, bool real) {
    switch (w) {
    case MAIL_TAKEN: return ((real ? GcDsp_CpuMailToDsp(a.dsp) : RefDsp_CpuMailToDsp(ref)) & 0x80000000u) == 0;
    case MAIL_THERE: return ((real ? GcDsp_CpuPeekMail(a.dsp) : RefDsp_CpuPeekMail(ref)) & 0x80000000u) != 0;
    default: return (real ? a.dirqs : b.dirqs) > before;
    }
  }
  bool wait(Wait w, int before = 0) {
    for (int i = 0; i < kBudget && fine; i++) {
      const bool da = done(w, before, true), db = done(w, before, false);
      if (da != db) {
        std::printf("FAIL lockstep: one chip is ready and the other is not\n");
        return fine = false;
      }
      if (da) return true;
      if (GcDsp_IsHalted(a.dsp)) break;
      if (perInstruction) {
        const int ra = GcDsp_Run(a.dsp, 1), rb = RefDsp_Run(ref, 1);
        ran += static_cast<uint64_t>(ra);
        if (ra != rb || !same(false)) return fine = false;
      } else {
        const int ra = GcDsp_RunUntilIdle(a.dsp, kBudget), rb = RefDsp_RunUntilIdle(ref, kBudget);
        ran += static_cast<uint64_t>(ra);
        if (ra != rb) {
          std::printf("FAIL lockstep: ran %d instructions to the next wait, the reference %d\n", ra, rb);
          return fine = false;
        }
        if (!same(true)) return false;
      }
    }
    return fine = false;
  }
  bool send(uint32_t mail) {
    GcDsp_CpuSendMail(a.dsp, mail);
    RefDsp_CpuSendMail(ref, mail);
    return wait(MAIL_TAKEN);
  }
  bool take(uint32_t expected) {
    const uint32_t ma = GcDsp_CpuTakeMail(a.dsp), mb = RefDsp_CpuTakeMail(ref);
    return fine = fine && ma == mb && ma == expected;
  }
};

int lockstep(const std::vector<uint8_t>& program, const std::vector<uint16_t>* irom, const std::vector<uint16_t>* coef,
             int lists) {
  for (int pass = 0; pass < 2; pass++) {
    Lockstep l;
    l.perInstruction = pass == 0;
    for (Machine* m : {&l.a, &l.b}) {
      std::memcpy(m->at(kDspSlave), program.data(), program.size());
      GcDspHost host{};
      host.user = m;
      host.mainMemory = host_memory;
      host.aram = m->aram.data();
      host.aramSize = kAramSize;
      host.interruptCpu = host_interrupt;
      host.log = host_log;
      if (m == &l.a) {
        m->dsp = GcDsp_Create(&host);
        if (irom != nullptr) GcDsp_LoadIrom(m->dsp, irom->data()); else GcDsp_InstallPortRom(m->dsp);
        if (coef != nullptr) GcDsp_LoadCoef(m->dsp, coef->data());
        GcDsp_WriteControl(m->dsp, 0x0004);
        GcDsp_WriteControl(m->dsp, 0x0001);
      } else {
        l.ref = RefDsp_Create(&host);
        if (irom != nullptr) RefDsp_LoadIrom(l.ref, irom->data()); else RefDsp_InstallPortRom(l.ref);
        if (coef != nullptr) RefDsp_LoadCoef(l.ref, coef->data());
        RefDsp_WriteControl(l.ref, 0x0004);
        RefDsp_WriteControl(l.ref, 0x0001);
      }
    }
    if (l.wait(Lockstep::MAIL_THERE)) l.take(0x8071FEED);
    for (uint32_t mail : {0x80F3A001u, kDspSlave, 0x80F3C002u, 0u, 0x80F3A002u, kDspSlaveSize, 0x80F3B002u, 0u,
                          0x80F3D001u, 0x10u}) {
      if (l.fine) l.send(mail);
    }
    if (l.fine && l.wait(Lockstep::INTERRUPTED, 0) && l.wait(Lockstep::MAIL_THERE)) l.take(0xDCD10000);
    for (int n = 0; n < lists && l.fine; n++) {
      const std::vector<uint16_t> list = voices_list(n);
      for (Machine* m : {&l.a, &l.b}) {
        if (n % 16 == 0) setup_voices(*m);
        std::memset(m->at(kDest), 0xAA, kDestSize);
        for (size_t i = 0; i < list.size(); i++) m->put16(kCmdList + static_cast<uint32_t>(i * 2), list[i]);
      }
      const uint16_t size = static_cast<uint16_t>((list.size() * 2 + 3) & ~3u);
      const int dirqs = l.a.dirqs;
      if (l.send(0xBABE0000u | size) && l.send(kCmdList) && l.wait(Lockstep::INTERRUPTED, dirqs) &&
          l.wait(Lockstep::MAIL_THERE) && l.take(0xDCD10002) && l.send(0xCDD10003)) {
        l.same(true);
      }
    }
    char what[200];
    std::snprintf(what, sizeof(what),
                  "lockstep, %s ROM, %s: %d lists, %llu instructions, %llu comparisons with the reference chip",
                  irom != nullptr ? "the folder's" : "the port's",
                  l.perInstruction ? "every instruction" : "every wait for the CPU (as the game runs it)", lists,
                  static_cast<unsigned long long>(l.ran), static_cast<unsigned long long>(l.compared));
    check(l.fine, what);
    GcDsp_Destroy(l.a.dsp);
    RefDsp_Destroy(l.ref);
  }
  return ok ? 0 : 1;
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "Usage: %s <disc image> [folder with dsp_rom.bin and dsp_coef.bin [trace file]]\n"
                 "       %s <disc image> --bench|--lockstep <lists> [folder with dsp_rom.bin and dsp_coef.bin]\n",
                 argv[0], argv[0]);
    return 2;
  }
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  std::vector<uint8_t> program(kDspSlaveSize);
  if (!PortDol_Read(dol, dolSize, kDspSlave, program.data(), kDspSlaveSize)) {
    std::fprintf(stderr, "FAILED: MusyX's DSP program isn't in main.dol\n");
    return 1;
  }
  if (argc >= 4 && (std::strcmp(argv[2], "--bench") == 0 || std::strcmp(argv[2], "--lockstep") == 0)) {
    const int lists = std::atoi(argv[3]);
    const bool isBench = std::strcmp(argv[2], "--bench") == 0;
    if (argc >= 5) {
      std::vector<uint16_t> irom, coef;
      const std::string dir = argv[4];
      if (!read_words(dir + "/dsp_rom.bin", 0x1000, irom) || !read_words(dir + "/dsp_coef.bin", 0x800, coef)) {
        std::fprintf(stderr, "FAILED: no dsp_rom.bin / dsp_coef.bin in %s\n", argv[4]);
        return 1;
      }
      return isBench ? bench(program, &irom, &coef, lists, 3) : lockstep(program, &irom, &coef, lists);
    }
    return isBench ? bench(program, nullptr, nullptr, lists, 3) : lockstep(program, nullptr, nullptr, lists);
  }
  const Run port = run_program(program, nullptr, nullptr, "port's own ROM routines", false);
  probe_port_routines(program);
  if (argc >= 3) {
    std::vector<uint16_t> irom, coef;
    const std::string dir = argv[2];
    if (!read_words(dir + "/dsp_rom.bin", 0x1000, irom) || !read_words(dir + "/dsp_coef.bin", 0x800, coef)) {
      std::fprintf(stderr, "FAILED: no dsp_rom.bin / dsp_coef.bin in %s\n", argv[2]);
      return 1;
    }
    uint32_t iromSum = 0x811C9DC5u;
    for (uint16_t w : irom) {
      const uint8_t bytes[2] = {static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w)};
      fnv(iromSum, bytes, 2);
    }
    const bool console = iromSum == kConsoleIrom;
    std::printf("-- the folder's dsp_rom.bin: %s (checksum %08X)\n",
                console ? "a console's ROM" : "not the console's ROM known here (a substitute)", iromSum);
    {
      std::vector<uint8_t> bytes(0x1000), swapped(0x1000), other(0x1000, 0x55);
      for (int i = 0; i < 0x800; i++) {
        bytes[i * 2] = swapped[i * 2 + 1] = static_cast<uint8_t>(coef[i] >> 8);
        bytes[i * 2 + 1] = swapped[i * 2] = static_cast<uint8_t>(coef[i]);
      }
      std::vector<uint16_t> a(0x800), b(0x800), c(0x800);
      check(GcDsp_ParseCoefDump(bytes.data(), bytes.size(), a.data()) == 1 && a == coef,
            "the folder's dsp_coef.bin is taken as a coefficient table");
      check(GcDsp_ParseCoefDump(swapped.data(), swapped.size(), b.data()) == 2 && b == coef,
            "... also with its bytes swapped (the same table)");
      check(GcDsp_ParseCoefDump(other.data(), other.size(), c.data()) == 0 &&
                GcDsp_ParseCoefDump(bytes.data(), bytes.size() - 2, c.data()) == 0,
            "... and another file or a cut one is refused");
    }
    const Run sub = run_program(program, &irom, &coef, "the folder's ROMs (answer key)", argc >= 4);
    if (argc >= 4) {
      write_trace(argv[3], sub.trace);
      std::printf("     %zu ROM calls written to %s\n", sub.trace.size(), argv[3]);
    }
    const Run mixed = run_program(program, nullptr, &coef, "port's ROM routines + the folder's coefficient table", false);
    compare(mixed, sub, "port's routines", console);
    compare_table(port, sub);
    const std::string detail = argc >= 4 ? std::string(argv[3]) + ".routines.txt" : std::string();
    probe_rom_routines(program, irom, console, detail.empty() ? nullptr : detail.c_str());
  }
  return ok ? 0 : 1;
}
