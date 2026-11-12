#include "gc_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint16_t DSCR = 0xFFC9, DSBL = 0xFFCB, DSPA = 0xFFCD, DSMAH = 0xFFCE, DSMAL = 0xFFCF;
constexpr uint16_t DMBH = 0xFFFC, DMBL = 0xFFFD, CMBH = 0xFFFE, CMBL = 0xFFFF;

enum { BOOT_PHASE, BOOT_COMMAND, BOOT_IRAM_MMEM, BOOT_IRAM_ADDR, BOOT_IRAM_LENGTH, BOOT_DRAM_LENGTH };
enum { PHASE_GREET, PHASE_COMMAND, PHASE_VALUE };

GcDspRomResult boot_loader(GcDsp* dsp, void*) {
  uint32_t* s = GcDsp_PortRomState(dsp);
  if (s[BOOT_PHASE] == PHASE_GREET) {
    GcDsp_WriteDmem(dsp, DMBH, 0x8071);
    GcDsp_WriteDmem(dsp, DMBL, 0xFEED);
    s[BOOT_PHASE] = PHASE_COMMAND;
    return GCDSP_ROM_WAIT;
  }
  if (!(GcDsp_ReadDmem(dsp, CMBH) & 0x8000)) {
    return GCDSP_ROM_WAIT;
  }
  const uint32_t mail = static_cast<uint32_t>(GcDsp_ReadDmem(dsp, CMBH) & 0x7FFF) << 16 | GcDsp_ReadDmem(dsp, CMBL);
  if (s[BOOT_PHASE] == PHASE_COMMAND) {
    s[BOOT_COMMAND] = mail | 0x80000000u;
    s[BOOT_PHASE] = PHASE_VALUE;
    return GCDSP_ROM_WAIT;
  }
  s[BOOT_PHASE] = PHASE_COMMAND;
  char msg[96];
  switch (s[BOOT_COMMAND]) {
  case 0x80F3A001: s[BOOT_IRAM_MMEM] = mail; return GCDSP_ROM_WAIT;
  case 0x80F3C002: s[BOOT_IRAM_ADDR] = mail & 0xFFFF; return GCDSP_ROM_WAIT;
  case 0x80F3A002: s[BOOT_IRAM_LENGTH] = mail; return GCDSP_ROM_WAIT;
  case 0x80F3B002:
    s[BOOT_DRAM_LENGTH] = mail;
    if (mail != 0) {
      std::snprintf(msg, sizeof(msg), "boot loader: DRAM length %08X isn't supported (the SDK sends 0)", mail);
      GcDsp_Log(dsp, msg);
    }
    return GCDSP_ROM_WAIT;
  case 0x80F3D001:
    GcDsp_WriteDmem(dsp, DSMAH, static_cast<uint16_t>(s[BOOT_IRAM_MMEM] >> 16));
    GcDsp_WriteDmem(dsp, DSMAL, static_cast<uint16_t>(s[BOOT_IRAM_MMEM]));
    GcDsp_WriteDmem(dsp, DSPA, static_cast<uint16_t>(s[BOOT_IRAM_ADDR]));
    GcDsp_WriteDmem(dsp, DSCR, 0x0002);
    GcDsp_WriteDmem(dsp, DSBL, static_cast<uint16_t>(s[BOOT_IRAM_LENGTH]));
    s[BOOT_PHASE] = PHASE_GREET;
    GcDsp_WriteReg(dsp, 0x13, 0x2060);
    GcDsp_WriteReg(dsp, 0x12, 0x00FF);
    GcDsp_WriteReg(dsp, 0x00, static_cast<uint16_t>(mail));
    GcDsp_WriteReg(dsp, 0x04, static_cast<uint16_t>(0x8000 | s[BOOT_IRAM_MMEM] >> 16));
    GcDsp_WriteReg(dsp, 0x05, static_cast<uint16_t>(s[BOOT_IRAM_MMEM]));
    GcDsp_WriteReg(dsp, 0x07, static_cast<uint16_t>(s[BOOT_IRAM_LENGTH]));
    GcDsp_SetAcc(dsp, 0, static_cast<int64_t>(0x0002) << 16);
    GcDsp_SetAcc(dsp, 1, static_cast<int64_t>(s[BOOT_IRAM_LENGTH] & 0xFFFF) << 16);
    GcDsp_SetPc(dsp, static_cast<uint16_t>(mail));
    return GCDSP_ROM_JUMPED;
  default:
    std::snprintf(msg, sizeof(msg), "boot loader: unknown command mail %08X (value %08X)", s[BOOT_COMMAND], mail);
    GcDsp_Log(dsp, msg);
    return GCDSP_ROM_WAIT;
  }
}

enum {
  AR0 = 0x00, AR1 = 0x01, AR2 = 0x02, AR3 = 0x03, IX0 = 0x04, IX1 = 0x05, IX3 = 0x07, SR = 0x13,
  PRODL = 0x14, PRODM1 = 0x15, PRODH = 0x16, PRODM2 = 0x17, AX0L = 0x18, AX1L = 0x19, AX0H = 0x1A, AX1H = 0x1B,
};
constexpr int kMixSamples = 32;

inline int16_t s16(uint16_t v) { return static_cast<int16_t>(v); }

inline uint16_t ramp_step(uint16_t volume, uint16_t delta, int i) {
  const int v = s16(volume) + i * s16(delta);
  return static_cast<uint16_t>(v > 0x7FFF ? 0x7FFF : v < -0x8000 ? -0x8000 : v);
}

struct Dmem {
  GcDsp* dsp;
  uint16_t* dram;
  explicit Dmem(GcDsp* d) : dsp(d), dram(GcDsp_Dram(d)) {}
  uint16_t read(uint16_t a) const { return a < 0x1000 ? dram[a] : GcDsp_ReadDmem(dsp, a); }
  void write(uint16_t a, uint16_t v) const {
    if (a < 0x1000) {
      dram[a] = v;
    } else {
      GcDsp_WriteDmem(dsp, a, v);
    }
  }
};

struct Mixed {
  uint16_t lastTerm;
  uint16_t lastVolume;
  int64_t lastSum;
};

Mixed mix_channel(GcDsp* dsp, uint16_t src, uint16_t dst, uint16_t volAddr, bool ramp, uint16_t scratch) {
  const Dmem mem(dsp);
  const uint16_t volume = mem.read(volAddr);
  const uint16_t delta = ramp ? mem.read(static_cast<uint16_t>(volAddr + 1)) : 0;
  Mixed m{};
  int32_t last = 0;
  for (int i = 0; i < kMixSamples; i++) {
    const uint16_t v = ramp ? ramp_step(volume, delta, i) : volume;
    if (ramp) mem.write(static_cast<uint16_t>(scratch + i), v);
    const int64_t product = static_cast<int64_t>(s16(mem.read(static_cast<uint16_t>(src + i)))) * v * 2;
    const int32_t term = static_cast<int32_t>(product >> 16);
    const uint16_t at = static_cast<uint16_t>(dst + 2 * i);
    const int64_t old = static_cast<int64_t>(static_cast<int8_t>(mem.read(at))) * 0x10000 +
                        mem.read(static_cast<uint16_t>(at + 1));
    const int64_t sum = old + term;
    mem.write(at, static_cast<uint16_t>(static_cast<int8_t>(sum >> 16)));
    mem.write(static_cast<uint16_t>(at + 1), static_cast<uint16_t>(sum));
    last = term;
    m.lastVolume = v;
    m.lastSum = static_cast<int64_t>(static_cast<int8_t>(sum >> 16)) * 0x10000 + static_cast<uint16_t>(sum);
  }
  if (ramp) mem.write(volAddr, ramp_step(volume, delta, kMixSamples));
  m.lastTerm = last > 0x7FFF ? 0x7FFF : last < -0x8000 ? 0x8000 : static_cast<uint16_t>(last);
  return m;
}

void leave_loop_state(GcDsp* dsp, uint16_t src, const Mixed& m) {
  const uint16_t next = GcDsp_ReadDmem(dsp, static_cast<uint16_t>(src + kMixSamples));
  const int64_t product = static_cast<int64_t>(s16(next)) * m.lastVolume * 2;
  GcDsp_WriteReg(dsp, AR0, static_cast<uint16_t>(src + kMixSamples + 1));
  GcDsp_WriteReg(dsp, AX0H, next);
  GcDsp_WriteReg(dsp, AX1L, m.lastVolume);
  GcDsp_WriteReg(dsp, PRODL, static_cast<uint16_t>(product));
  GcDsp_WriteReg(dsp, PRODM1, static_cast<uint16_t>(product >> 16));
  GcDsp_WriteReg(dsp, PRODH, static_cast<uint16_t>((product >> 32) & 0xFF));
  GcDsp_WriteReg(dsp, PRODM2, 0);
  GcDsp_SetAcc(dsp, 0, 0);
  GcDsp_SetAcc(dsp, 1, m.lastSum);
  GcDsp_WriteReg(dsp, SR, static_cast<uint16_t>((GcDsp_Sr(dsp) & ~0x3F) | 0x24));
}

GcDspRomResult mix_mono(GcDsp* dsp, bool ramp) {
  const uint16_t src = GcDsp_ReadReg(dsp, AR0), vol = GcDsp_ReadReg(dsp, AR1), dst = GcDsp_ReadReg(dsp, AR2);
  const uint16_t scratch = GcDsp_ReadReg(dsp, AR3);
  const Mixed m = mix_channel(dsp, src, dst, vol, ramp, scratch);
  leave_loop_state(dsp, src, m);
  GcDsp_WriteReg(dsp, AR1, static_cast<uint16_t>(ramp ? scratch + kMixSamples : vol + 2));
  GcDsp_WriteReg(dsp, AR2, static_cast<uint16_t>(dst + 2 * kMixSamples));
  GcDsp_WriteReg(dsp, AR3, static_cast<uint16_t>(dst + 2 * kMixSamples));
  if (ramp) {
    GcDsp_WriteReg(dsp, IX3, scratch);
  } else {
    GcDsp_WriteReg(dsp, AX1H, static_cast<uint16_t>(dst + 2 * kMixSamples));
  }
  GcDsp_WriteReg(dsp, AX0L, m.lastTerm);
  return GCDSP_ROM_RETURN;
}

GcDspRomResult mix_stereo(GcDsp* dsp, bool ramp) {
  const uint16_t srcL = GcDsp_ReadReg(dsp, AR0), srcR = GcDsp_ReadReg(dsp, IX0), vol = GcDsp_ReadReg(dsp, AR1);
  const uint16_t dstL = GcDsp_ReadReg(dsp, AR2), dstR = GcDsp_ReadReg(dsp, IX1), scratch = GcDsp_ReadReg(dsp, AR3);
  const Mixed left = mix_channel(dsp, srcL, dstL, vol, ramp, scratch);
  const Mixed right =
      mix_channel(dsp, srcR, dstR, static_cast<uint16_t>(vol + 2), ramp, static_cast<uint16_t>(scratch + kMixSamples));
  leave_loop_state(dsp, srcR, right);
  GcDsp_WriteReg(dsp, AR1, static_cast<uint16_t>(ramp ? scratch + 2 * kMixSamples : vol + 4));
  GcDsp_WriteReg(dsp, AR2, static_cast<uint16_t>(dstR + 2 * kMixSamples));
  GcDsp_WriteReg(dsp, AR3, static_cast<uint16_t>(dstR + 2 * kMixSamples));
  if (ramp) GcDsp_WriteReg(dsp, IX3, scratch);
  GcDsp_WriteReg(dsp, AX0L, left.lastTerm);
  GcDsp_WriteReg(dsp, AX1H, right.lastTerm);
  return GCDSP_ROM_RETURN;
}

GcDspRomResult mix_mono_steady(GcDsp* dsp, void*) { return mix_mono(dsp, false); }
GcDspRomResult mix_stereo_steady(GcDsp* dsp, void*) { return mix_stereo(dsp, false); }
GcDspRomResult mix_mono_ramp(GcDsp* dsp, void*) { return mix_mono(dsp, true); }
GcDspRomResult mix_stereo_ramp(GcDsp* dsp, void*) { return mix_stereo(dsp, true); }

void build_coef_table(uint16_t* table) {
  constexpr double kPi = 3.14159265358979323846;
  const double cutoffs[3] = {8000.0 / 16000.0, 12000.0 / 16000.0, 16000.0 / 16000.0};
  for (int set = 0; set < 3; set++) {
    for (int phase = 0; phase < 128; phase++) {
      const double t = phase / 128.0;
      double taps[4], sum = 0;
      for (int k = 0; k < 4; k++) {
        const double x = k - 1 - t;
        const double c = cutoffs[set];
        const double sinc = x == 0 ? 1.0 : std::sin(kPi * c * x) / (kPi * c * x);
        const double window = 0.5 * (1 + std::cos(kPi * x / 2));
        taps[k] = c * sinc * window;
        sum += taps[k];
      }
      for (int k = 0; k < 4; k++) {
        table[set * 0x200 + phase * 4 + k] = static_cast<uint16_t>(std::lround(taps[k] / sum * 32767.0));
      }
    }
  }
}

}

extern "C" void GcDsp_PortCoefTable(uint16_t* words) {
  std::fill(words, words + 0x800, static_cast<uint16_t>(0));
  build_coef_table(words);
}

static bool looks_like_coef_table(const uint16_t* words) {
  for (int phase = 0; phase < 3 * 128; phase++) {
    int sum = 0;
    for (int k = 0; k < 4; k++) sum += s16(words[phase * 4 + k]);
    if (sum < 0x7E00 || sum > 0x8200) return false;
  }
  return true;
}

extern "C" int GcDsp_ParseCoefDump(const uint8_t* bytes, size_t size, uint16_t* words) {
  if (bytes == nullptr || size != 0x1000) return 0;
  for (int order = 1; order <= 2; order++) {
    for (int i = 0; i < 0x800; i++) {
      const uint8_t a = bytes[i * 2], b = bytes[i * 2 + 1];
      words[i] = static_cast<uint16_t>(order == 1 ? a << 8 | b : b << 8 | a);
    }
    if (looks_like_coef_table(words)) return order;
  }
  return 0;
}

extern "C" void GcDsp_InstallPortRom(GcDsp* dsp) {
  uint16_t coef[0x800];
  GcDsp_PortCoefTable(coef);
  GcDsp_LoadCoef(dsp, coef);
  GcDsp_SetRomRoutine(dsp, 0x8000, boot_loader, nullptr);
  GcDsp_SetRomRoutine(dsp, 0x80E7, mix_stereo_steady, nullptr);
  GcDsp_SetRomRoutine(dsp, 0x81F9, mix_mono_steady, nullptr);
  GcDsp_SetRomRoutine(dsp, 0x8282, mix_stereo_ramp, nullptr);
  GcDsp_SetRomRoutine(dsp, 0x845D, mix_mono_ramp, nullptr);
}
