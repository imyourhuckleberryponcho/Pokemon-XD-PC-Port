#include "gc_dsp.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {

constexpr int64_t kMask40 = 0xFFFFFFFFFFll;
constexpr uint16_t SR_C = 0x0001, SR_O = 0x0002, SR_Z = 0x0004, SR_S = 0x0008, SR_AS = 0x0010, SR_TB = 0x0020,
                   SR_LZ = 0x0040, SR_OS = 0x0080, SR_IE = 0x0200, SR_EIE = 0x0800, SR_AM = 0x2000,
                   SR_SXM = 0x4000, SR_SU = 0x8000;
constexpr uint16_t CTRL_RESET = 0x0001, CTRL_INT = 0x0002, CTRL_HALT = 0x0004, CTRL_INIT = 0x0800;

int64_t w40(int64_t v) { return (v << 24) >> 24; }
int64_t s16(uint16_t v) { return static_cast<int16_t>(v); }
int64_t s8(uint8_t v) { return static_cast<int8_t>(v); }

}

struct GcDsp {
  GcDspHost host{};
  uint16_t iram[0x1000]{};
  uint16_t irom[0x1000]{};
  uint16_t dram[0x1000]{};
  uint16_t coef[0x800]{};
  bool haveIrom = false, haveCoef = false;

  uint16_t ar[4]{}, ix[4]{}, wr[4]{};
  uint16_t st[4]{};
  uint16_t stack[4][32]{};
  uint8_t stackPtr[4]{};
  int64_t ac[2]{};
  uint16_t axl[2]{}, axh[2]{};
  uint16_t config = 0, sr = 0;
  uint16_t pl = 0, pm1 = 0, ph = 0, pm2 = 0;
  uint16_t pc = 0;

  uint16_t hw[256]{};
  uint32_t dspMail = 0, cpuMail = 0;
  uint16_t control = CTRL_INIT | CTRL_HALT;
  uint8_t pending = 0;
  uint32_t acsa = 0, acea = 0, acca = 0;
  bool accStopped = false;

  struct Queued {
    int reg;
    uint16_t value;
  };
  Queued queue[8];
  int queued = 0;

  std::unordered_map<uint16_t, std::pair<GcDspRomRoutine, void*>> romRoutines;
  uint32_t portRomState[16]{};
  bool waitingForCpu = false;
  bool loggedCoef = false;
};

namespace {

void log(GcDsp* d, const std::string& msg) {
  if (d->host.log != nullptr) {
    d->host.log(d->host.user, msg.c_str());
  }
}

void push(GcDsp* d, int s, uint16_t v) {
  d->stackPtr[s] = (d->stackPtr[s] + 1) & 31;
  d->stack[s][d->stackPtr[s]] = d->st[s];
  d->st[s] = v;
}
uint16_t pop(GcDsp* d, int s) {
  const uint16_t r = d->st[s];
  d->st[s] = d->stack[s][d->stackPtr[s]];
  d->stackPtr[s] = (d->stackPtr[s] - 1) & 31;
  return r;
}

uint16_t acc_l(const GcDsp* d, int n) { return static_cast<uint16_t>(d->ac[n]); }
uint16_t acc_m(const GcDsp* d, int n) { return static_cast<uint16_t>(d->ac[n] >> 16); }
uint16_t acc_h(const GcDsp* d, int n) { return static_cast<uint16_t>(static_cast<int16_t>(d->ac[n] >> 32)); }
void set_acc_l(GcDsp* d, int n, uint16_t v) { d->ac[n] = (d->ac[n] & ~0xFFFFll) | v; }
void set_acc_m(GcDsp* d, int n, uint16_t v) { d->ac[n] = (d->ac[n] & ~0xFFFF0000ll) | (static_cast<int64_t>(v) << 16); }
void set_acc_h(GcDsp* d, int n, uint16_t v) {
  d->ac[n] = (d->ac[n] & 0xFFFFFFFFll) | (s8(static_cast<uint8_t>(v)) << 32);
}
bool outside_s32(int64_t v) { return v < -2147483648ll || v > 2147483647ll; }
uint16_t acc_m_sat(const GcDsp* d, int n) {
  if ((d->sr & SR_SXM) && outside_s32(d->ac[n])) {
    return d->ac[n] > 0 ? 0x7FFF : 0x8000;
  }
  return acc_m(d, n);
}
int64_t ax(const GcDsp* d, int n) { return static_cast<int32_t>((static_cast<uint32_t>(d->axh[n]) << 16) | d->axl[n]); }

int64_t prod(const GcDsp* d) {
  return s8(static_cast<uint8_t>(d->ph)) * 0x100000000ll + ((static_cast<int64_t>(d->pm1) + d->pm2) << 16) + d->pl;
}
void set_prod(GcDsp* d, int64_t v) {
  const uint64_t u = static_cast<uint64_t>(v) & kMask40;
  d->pl = static_cast<uint16_t>(u);
  d->pm1 = static_cast<uint16_t>(u >> 16);
  d->ph = static_cast<uint16_t>((u >> 32) & 0xFF);
  d->pm2 = 0;
}
int64_t round_hm(int64_t p) {
  return (p & 0x10000) ? ((p + 0x8000) & ~0xFFFFll) : ((p + 0x7FFF) & ~0xFFFFll);
}
enum MulMode { MUL_SIGNED, MUL_UNSIGNED, MUL_MIXED };
int64_t multiply(const GcDsp* d, uint16_t a, uint16_t b, MulMode mode) {
  int64_t r;
  if ((d->sr & SR_SU) && mode == MUL_UNSIGNED) {
    r = static_cast<int64_t>(a) * b;
  } else if ((d->sr & SR_SU) && mode == MUL_MIXED) {
    r = static_cast<int64_t>(a) * s16(b);
  } else {
    r = s16(a) * s16(b);
  }
  return (d->sr & SR_AM) ? r : r * 2;
}

void flags40(GcDsp* d, int64_t r, bool carry, bool overflow) {
  d->sr &= ~0x3F;
  if (carry) d->sr |= SR_C;
  if (overflow) d->sr |= SR_O | SR_OS;
  if (r == 0) d->sr |= SR_Z;
  if (r < 0) d->sr |= SR_S;
  if (outside_s32(r)) d->sr |= SR_AS;
  const int top = static_cast<int>((r >> 30) & 3);
  if (top == 0 || top == 3) d->sr |= SR_TB;
}
void flags16(GcDsp* d, uint16_t v, bool as) {
  d->sr &= ~0x3F;
  if (v == 0) d->sr |= SR_Z;
  if (v & 0x8000) d->sr |= SR_S;
  if (as) d->sr |= SR_AS;
  const int top = (v >> 14) & 3;
  if (top == 0 || top == 3) d->sr |= SR_TB;
}
bool add_carry(int64_t a, int64_t r) {
  return static_cast<uint64_t>(a & kMask40) > static_cast<uint64_t>(r & kMask40);
}
bool add_overflow(int64_t a, int64_t b, int64_t r) { return (a < 0) == (b < 0) && (r < 0) != (a < 0); }
void do_add(GcDsp* d, int n, int64_t b) {
  const int64_t a = d->ac[n];
  const int64_t r = w40(a + b);
  d->ac[n] = r;
  flags40(d, r, add_carry(a, r), add_overflow(a, b, r));
}
int64_t sub_flags(GcDsp* d, int64_t a, int64_t b) {
  const int64_t r = w40(a - b);
  const bool carry = static_cast<uint64_t>(a & kMask40) >= static_cast<uint64_t>(r & kMask40);
  flags40(d, r, carry, add_overflow(a, -b, r));
  return r;
}
bool condition(const GcDsp* d, int cc) {
  const bool c = d->sr & SR_C, o = d->sr & SR_O, z = d->sr & SR_Z, s = d->sr & SR_S, as = d->sr & SR_AS,
             tb = d->sr & SR_TB, lz = d->sr & SR_LZ;
  switch (cc) {
  case 0x0: return o == s;
  case 0x1: return o != s;
  case 0x2: return o == s && !z;
  case 0x3: return o != s || z;
  case 0x4: return !z;
  case 0x5: return z;
  case 0x6: return !c;
  case 0x7: return c;
  case 0x8: return !as;
  case 0x9: return as;
  case 0xA: return !(!(as || tb) || z);
  case 0xB: return !(as || tb) || z;
  case 0xC: return !lz;
  case 0xD: return lz;
  case 0xE: return o;
  default: return true;
  }
}

uint16_t addr_inc(uint16_t ar, uint16_t wr) {
  const uint32_t m = 2u * (wr | 1u);
  uint32_t n = static_cast<uint32_t>(ar) + 1;
  if ((n ^ ar) > m) n -= static_cast<uint32_t>(wr) + 1;
  return static_cast<uint16_t>(n);
}
uint16_t addr_dec(uint16_t ar, uint16_t wr) {
  const uint32_t m = 2u * (wr | 1u);
  uint32_t n = static_cast<uint32_t>(ar) + wr;
  if (((n ^ ar) & m) > wr) n -= static_cast<uint32_t>(wr) + 1;
  return static_cast<uint16_t>(n);
}
uint16_t addr_add(uint16_t ar, uint16_t wr, uint16_t ixv) {
  const uint32_t m = 2u * (wr | 1u);
  const uint32_t ix = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(ixv)));
  uint32_t n = static_cast<uint32_t>(ar) + ix;
  const uint32_t carries = (n ^ ar ^ ix) & m;
  if (static_cast<int16_t>(ixv) >= 0) {
    if (carries > wr) n -= static_cast<uint32_t>(wr) + 1;
  } else if ((((n + wr + 1) ^ n) & carries) <= wr) {
    n += static_cast<uint32_t>(wr) + 1;
  }
  return static_cast<uint16_t>(n);
}
uint16_t addr_sub(uint16_t ar, uint16_t wr, uint16_t ixv) {
  const uint32_t m = 2u * (wr | 1u);
  const uint32_t ix = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(ixv)));
  uint32_t n = static_cast<uint32_t>(ar) - ix;
  const uint32_t carries = (n ^ ar ^ ~ix) & m;
  if (ix > 0xFFFF8000u) {
    if (carries > wr) n -= static_cast<uint32_t>(wr) + 1;
  } else if ((((n + wr + 1) ^ n) & carries) <= wr) {
    n += static_cast<uint32_t>(wr) + 1;
  }
  return static_cast<uint16_t>(n);
}
uint16_t ar_inc(const GcDsp* d, int r) { return addr_inc(d->ar[r], d->wr[r]); }
uint16_t ar_dec(const GcDsp* d, int r) { return addr_dec(d->ar[r], d->wr[r]); }
uint16_t ar_addix(const GcDsp* d, int r, int i) { return addr_add(d->ar[r], d->wr[r], d->ix[i]); }

uint16_t read_reg(GcDsp* d, int r) {
  switch (r & 31) {
  case 0: case 1: case 2: case 3: return d->ar[r & 3];
  case 4: case 5: case 6: case 7: return d->ix[r & 3];
  case 8: case 9: case 10: case 11: return d->wr[r & 3];
  case 12: case 13: case 14: case 15: return pop(d, r & 3);
  case 16: return acc_h(d, 0);
  case 17: return acc_h(d, 1);
  case 18: return d->config;
  case 19: return d->sr;
  case 20: return d->pl;
  case 21: return d->pm1;
  case 22: return d->ph;
  case 23: return d->pm2;
  case 24: return d->axl[0];
  case 25: return d->axl[1];
  case 26: return d->axh[0];
  case 27: return d->axh[1];
  case 28: return acc_l(d, 0);
  case 29: return acc_l(d, 1);
  case 30: return acc_m_sat(d, 0);
  default: return acc_m_sat(d, 1);
  }
}
void write_reg(GcDsp* d, int r, uint16_t v) {
  switch (r & 31) {
  case 0: case 1: case 2: case 3: d->ar[r & 3] = v; break;
  case 4: case 5: case 6: case 7: d->ix[r & 3] = v; break;
  case 8: case 9: case 10: case 11: d->wr[r & 3] = v; break;
  case 12: case 13: case 14: case 15: push(d, r & 3, v); break;
  case 16: set_acc_h(d, 0, v); break;
  case 17: set_acc_h(d, 1, v); break;
  case 18: d->config = v & 0xFF; break;
  case 19: d->sr = v & ~0x0100; break;
  case 20: d->pl = v; break;
  case 21: d->pm1 = v; break;
  case 22: d->ph = v & 0xFF; break;
  case 23: d->pm2 = v; break;
  case 24: d->axl[0] = v; break;
  case 25: d->axl[1] = v; break;
  case 26: d->axh[0] = v; break;
  case 27: d->axh[1] = v; break;
  case 28: set_acc_l(d, 0, v); break;
  case 29: set_acc_l(d, 1, v); break;
  case 30: set_acc_m(d, 0, v); break;
  default: set_acc_m(d, 1, v); break;
  }
}
void load_reg(GcDsp* d, int r, uint16_t v) {
  write_reg(d, r, v);
  if ((r == 30 || r == 31) && (d->sr & SR_SXM)) {
    d->ac[r - 30] = s16(v) << 16;
  }
}

uint8_t aram_byte(GcDsp* d, uint32_t byteAddress) {
  return byteAddress < d->host.aramSize && d->host.aram != nullptr ? d->host.aram[byteAddress] : 0;
}
uint16_t acc_raw_sample(GcDsp* d) {
  switch (d->hw[0xD1] & 3) {
  case 0: {
    const uint8_t b = aram_byte(d, (d->acca & 0x3FFFFFFF) >> 1);
    return (d->acca & 1) ? (b & 0xF) : (b >> 4);
  }
  case 1: return aram_byte(d, d->acca & 0x3FFFFFFF);
  case 2: {
    const uint32_t a = (d->acca & 0x3FFFFFFF) * 2;
    return static_cast<uint16_t>(aram_byte(d, a) << 8 | aram_byte(d, a + 1));
  }
  default: return 0;
  }
}
uint16_t acc_read_sample(GcDsp* d) {
  if (d->accStopped) {
    return 0;
  }
  const uint16_t format = d->hw[0xD1];
  const int mode = (format >> 2) & 3;
  const uint16_t raw = (mode == 1 || mode == 3) ? d->hw[0xDF] : acc_raw_sample(d);
  const int idx = (d->hw[0xDA] >> 4) & 7;
  const int64_t c1 = s16(d->hw[0xA0 + 2 * idx]), c2 = s16(d->hw[0xA1 + 2 * idx]);
  const int64_t yn1 = s16(d->hw[0xDB]), yn2 = s16(d->hw[0xDC]);
  int64_t v;
  uint32_t step = 2;
  if (mode == 0) {
    int n = raw & 0xF;
    if (n >= 8) n -= 16;
    const int32_t pred = static_cast<int32_t>(0x400 + c1 * yn1 + c2 * yn2) >> 11;
    v = static_cast<int64_t>(n) * (1 << (d->hw[0xDA] & 0xF)) + pred;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    d->hw[0xDC] = d->hw[0xDB];
    d->hw[0xDB] = static_cast<uint16_t>(v);
    d->acca++;
    if ((d->acea & 0xF) == 0 && d->acca == d->acea) {
      d->acca = d->acsa + 1;
    } else if ((d->acea & 0xF) == 1 && d->acca == d->acea - 1) {
      d->acca = d->acsa;
    } else if ((d->acca & 0xF) == 0) {
      d->hw[0xDA] = aram_byte(d, ((d->acca & 0x3FFFFFFF) & ~0xFu) >> 1);
      d->acca += 2;
      step = 4;
    }
  } else {
    const int shifts[4] = {11, 0, 16, 0};
    const int sh = shifts[(format >> 4) & 3];
    v = static_cast<int32_t>((static_cast<int32_t>(s16(d->hw[0xDE])) * static_cast<int32_t>(s16(raw))) >> sh) +
        (static_cast<int32_t>(c1 * yn1) >> sh) + (static_cast<int32_t>(c2 * yn2) >> sh);
    d->hw[0xDC] = d->hw[0xDB];
    d->hw[0xDB] = static_cast<uint16_t>(v);
    if (mode != 1) {
      d->acca++;
    }
  }
  if (d->acca == d->acea + step - 1) {
    d->acca = d->acsa;
    d->accStopped = true;
    d->pending |= 1 << 5;
  }
  d->acca &= 0xBFFFFFFF;
  return static_cast<uint16_t>(v);
}

void do_dma(GcDsp* d) {
  const uint16_t dscr = d->hw[0xC9];
  const uint32_t length = d->hw[0xCB];
  const uint32_t mainAddr = static_cast<uint32_t>(d->hw[0xCE]) << 16 | d->hw[0xCF];
  const uint16_t dspAddr = d->hw[0xCD];
  if (d->hw[0xEF] != 0) {
    return;
  }
  uint8_t* mem = d->host.mainMemory != nullptr ? d->host.mainMemory(d->host.user, mainAddr, length) : nullptr;
  if (mem == nullptr) {
    log(d, "DMA to/from unmapped main memory");
    return;
  }
  uint16_t* target = (dscr & 2) ? d->iram : d->dram;
  const uint32_t words = length / 2;
  const int hi = (!(dscr & 2) && d->host.dramWordsHostOrder) ? 1 : 0, lo = 1 - hi;
  for (uint32_t i = 0; i < words; i++) {
    const uint16_t a = static_cast<uint16_t>((dspAddr + i) & 0xFFF);
    if (dscr & 1) {
      if (dscr & 2) {
        log(d, "DMA from instruction memory to the CPU isn't supported");
        return;
      }
      mem[i * 2 + hi] = static_cast<uint8_t>(target[a] >> 8);
      mem[i * 2 + lo] = static_cast<uint8_t>(target[a]);
    } else {
      target[a] = static_cast<uint16_t>(mem[i * 2 + hi] << 8 | mem[i * 2 + lo]);
    }
  }
}

uint16_t hw_read(GcDsp* d, uint8_t r) {
  switch (r) {
  case 0xD3: {
    const uint16_t v = acc_raw_sample(d);
    const bool valid = (d->hw[0xD1] & 3) != 3;
    d->acca = valid ? d->acca + 1 : ((d->acca & ~3u) | ((d->acca + 1) & 3u));
    if (d->acca - 1 == d->acea) {
      d->acca = d->acsa;
      d->pending |= 1 << 3;
    }
    d->acca &= 0xBFFFFFFF;
    return v;
  }
  case 0xD4: return static_cast<uint16_t>(d->acsa >> 16);
  case 0xD5: return static_cast<uint16_t>(d->acsa);
  case 0xD6: return static_cast<uint16_t>(d->acea >> 16);
  case 0xD7: return static_cast<uint16_t>(d->acea);
  case 0xD8: return static_cast<uint16_t>(d->acca >> 16);
  case 0xD9: return static_cast<uint16_t>(d->acca);
  case 0xDD: return acc_read_sample(d);
  case 0xFC:
    if (d->dspMail & 0x80000000u) d->waitingForCpu = true;
    return static_cast<uint16_t>(d->dspMail >> 16);
  case 0xFD: return static_cast<uint16_t>(d->dspMail);
  case 0xFE:
    if (!(d->cpuMail & 0x80000000u)) d->waitingForCpu = true;
    return static_cast<uint16_t>(d->cpuMail >> 16);
  case 0xFF: {
    const uint16_t v = static_cast<uint16_t>(d->cpuMail);
    d->cpuMail &= 0x7FFFFFFF;
    return v;
  }
  default: return d->hw[r];
  }
}
void hw_write(GcDsp* d, uint8_t r, uint16_t v) {
  switch (r) {
  case 0xCB:
    d->hw[0xCB] = v;
    do_dma(d);
    d->hw[0xCB] = 0;
    return;
  case 0xD3:
    if (d->acca & 0x80000000u) {
      const uint32_t a = (d->acca & 0x3FFFFFFF) * 2;
      if (a + 1 < d->host.aramSize && d->host.aram != nullptr) {
        d->host.aram[a] = static_cast<uint8_t>(v >> 8);
        d->host.aram[a + 1] = static_cast<uint8_t>(v);
      }
      d->acca++;
      d->pending |= 1 << 4;
    }
    return;
  case 0xD4: d->acsa = ((static_cast<uint32_t>(v) << 16) | (d->acsa & 0xFFFF)) & 0x3FFFFFFF; return;
  case 0xD5: d->acsa = ((d->acsa & 0xFFFF0000) | v) & 0x3FFFFFFF; return;
  case 0xD6: d->acea = ((static_cast<uint32_t>(v) << 16) | (d->acea & 0xFFFF)) & 0x3FFFFFFF; return;
  case 0xD7: d->acea = ((d->acea & 0xFFFF0000) | v) & 0x3FFFFFFF; return;
  case 0xD8: d->acca = ((static_cast<uint32_t>(v) << 16) | (d->acca & 0xFFFF)) & 0xBFFFFFFF; return;
  case 0xD9: d->acca = ((d->acca & 0xFFFF0000) | v) & 0xBFFFFFFF; return;
  case 0xDA: d->hw[0xDA] = v & 0x7F; return;
  case 0xDC: d->hw[0xDC] = v; d->accStopped = false; return;
  case 0xFB:
    if ((v & 1) && d->host.interruptCpu != nullptr) {
      d->host.interruptCpu(d->host.user);
    }
    return;
  case 0xFC: d->dspMail = ((d->dspMail & 0xFFFF) | (static_cast<uint32_t>(v) << 16)) & 0x7FFFFFFF; return;
  case 0xFD: d->dspMail = (d->dspMail & 0xFFFF0000) | v | 0x80000000u; return;
  case 0xFE: d->cpuMail = ((d->cpuMail & 0xFFFF) | (static_cast<uint32_t>(v) << 16)) & 0x7FFFFFFF; return;
  case 0xFF: d->cpuMail = (d->cpuMail & 0xFFFF0000) | v | 0x80000000u; return;
  default: d->hw[r] = v; return;
  }
}
uint16_t dmem_read(GcDsp* d, uint16_t a) {
  switch (a >> 12) {
  case 0x0: return d->dram[a & 0xFFF];
  case 0x1:
    if (!d->haveCoef && !d->loggedCoef) {
      d->loggedCoef = true;
      char buf[96];
      std::snprintf(buf, sizeof(buf), "read from the coefficient ROM (%04X, at pc %04X), which isn't loaded", a, d->pc);
      log(d, buf);
    }
    return d->coef[a & 0x7FF];
  case 0xF: return hw_read(d, static_cast<uint8_t>(a));
  default: return 0;
  }
}
void dmem_write(GcDsp* d, uint16_t a, uint16_t v) {
  switch (a >> 12) {
  case 0x0: d->dram[a & 0xFFF] = v; break;
  case 0xF: hw_write(d, static_cast<uint8_t>(a), v); break;
  default: break;
  }
}
uint16_t imem_read(const GcDsp* d, uint16_t a) {
  switch (a >> 12) {
  case 0x0: return d->iram[a & 0xFFF];
  case 0x8: return d->irom[a & 0xFFF];
  default: return 0;
  }
}

void q(GcDsp* d, int reg, uint16_t v) {
  if (d->queued < 8) d->queue[d->queued++] = {reg, v};
}
uint16_t ext_src(GcDsp* d, int ss) {
  return ss < 2 ? acc_l(d, ss) : acc_m_sat(d, ss - 2);
}
void ext_execute(GcDsp* d, uint16_t opc, int ext) {
  int extCount = 0;
  const DspOpInfo* table = GcDsp_ExtOps(&extCount);
  const char* name = table[ext].name;
  const uint8_t e = static_cast<uint8_t>(opc);
  auto is = [&](const char* n) { return std::strcmp(name, n) == 0; };
  if (is("'NOP")) {
    return;
  }
  if (is("'DR")) { q(d, e & 3, ar_dec(d, e & 3)); return; }
  if (is("'IR")) { q(d, e & 3, ar_inc(d, e & 3)); return; }
  if (is("'NR")) { q(d, e & 3, ar_addix(d, e & 3, e & 3)); return; }
  if (is("'MV")) { q(d, 0x18 + ((e >> 2) & 3), ext_src(d, e & 3)); return; }
  if (is("'S") || is("'SN")) {
    const int dr = e & 3, ss = (e >> 3) & 3;
    dmem_write(d, d->ar[dr], ext_src(d, ss));
    q(d, dr, is("'S") ? ar_inc(d, dr) : ar_addix(d, dr, dr));
    return;
  }
  if (is("'L") || is("'LN")) {
    const int sr = e & 3, dd = (e >> 3) & 7;
    const uint16_t v = dmem_read(d, d->ar[sr]);
    const int reg = 0x18 + dd;
    if ((reg == 30 || reg == 31) && (d->sr & SR_SXM)) {
      const int n = reg - 30;
      q(d, 16 + n, (v & 0x8000) ? 0xFFFF : 0);
      q(d, reg, v);
      q(d, 28 + n, 0);
    } else {
      q(d, reg, v);
    }
    q(d, sr, is("'L") ? ar_inc(d, sr) : ar_addix(d, sr, sr));
    return;
  }
  if (name[1] == 'L' && name[2] == 'S' || name[1] == 'S' && name[2] == 'L') {
    const int s = e & 1, dd = (e >> 4) & 3;
    const bool load0 = name[1] == 'L';
    const bool n0 = (e >> 2) & 1, m3 = (e >> 3) & 1;
    dmem_write(d, d->ar[load0 ? 3 : 0], acc_m_sat(d, s));
    q(d, 0x18 + dd, dmem_read(d, d->ar[load0 ? 0 : 3]));
    q(d, 0, n0 ? ar_addix(d, 0, 0) : ar_inc(d, 0));
    q(d, 3, m3 ? ar_addix(d, 3, 3) : ar_inc(d, 3));
    return;
  }
  if (std::strncmp(name, "'LDAX", 5) == 0) {
    const int s = (e >> 5) & 1, r = (e >> 4) & 1;
    const bool n = (e >> 2) & 1, m = (e >> 3) & 1;
    const uint16_t a = d->ar[s], b = d->ar[3];
    const uint16_t v1 = dmem_read(d, a);
    const uint16_t v2 = (a >> 10) == (b >> 10) ? v1 : dmem_read(d, b);
    q(d, 26 + r, v1);
    q(d, 24 + r, v2);
    q(d, s, n ? ar_addix(d, s, s) : ar_inc(d, s));
    q(d, 3, m ? ar_addix(d, 3, 3) : ar_inc(d, 3));
    return;
  }
  if (std::strncmp(name, "'LD", 3) == 0) {
    const int s = e & 3, dsel = (e >> 5) & 1, rsel = (e >> 4) & 1;
    const bool n = (e >> 2) & 1, m = (e >> 3) & 1;
    const uint16_t a = d->ar[s], b = d->ar[3];
    const uint16_t v1 = dmem_read(d, a);
    const uint16_t v2 = (a >> 10) == (b >> 10) ? v1 : dmem_read(d, b);
    q(d, dsel ? 26 : 24, v1);
    q(d, rsel ? 27 : 25, v2);
    q(d, s, n ? ar_addix(d, s, s) : ar_inc(d, s));
    q(d, 3, m ? ar_addix(d, 3, 3) : ar_inc(d, 3));
    return;
  }
}

using Handler = void (*)(GcDsp*, uint16_t);
Handler sHandlers[256];

uint16_t fetch(GcDsp* d) { return imem_read(d, d->pc++); }
void skip_next(GcDsp* d) { d->pc += GcDsp_Size(imem_read(d, d->pc)); }
int64_t sel_ax_part(const GcDsp* d, int sel) {
  const uint16_t v = sel == 0 ? d->axl[0] : sel == 1 ? d->axl[1] : sel == 2 ? d->axh[0] : d->axh[1];
  return s16(v);
}
void store_mul(GcDsp* d, int r, int64_t newProd, int kind) {
  const int64_t old = prod(d);
  int64_t acc;
  if (kind == 0) {
    acc = w40(d->ac[r] + old);
  } else if (kind == 1) {
    acc = w40(old);
  } else {
    acc = w40(round_hm(old));
  }
  d->ac[r] = acc;
  set_prod(d, newProd);
  flags40(d, acc, false, false);
}
int64_t shift_logical(int64_t acc, int amount, bool left) {
  uint64_t u = static_cast<uint64_t>(acc) & kMask40;
  if (left) {
    u = amount >= 40 ? 0 : (u << amount);
  } else {
    u = amount >= 40 ? 0 : (u >> amount);
  }
  return w40(static_cast<int64_t>(u));
}
int64_t shift_arith(int64_t acc, int amount, bool left) {
  if (left) {
    return amount >= 40 ? 0 : w40(static_cast<int64_t>(static_cast<uint64_t>(acc) << amount));
  }
  return acc >> (amount > 63 ? 63 : amount);
}
int shift_k(uint16_t src) {
  const int v = src & 0x7F;
  if ((v & 0x3F) == 0) return 0;
  return (v & 0x40) ? (v & 0x3F) - 64 : (v & 0x3F);
}

void op_unknown(GcDsp* d, uint16_t opc) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "unknown instruction %04X at %04X", opc, static_cast<unsigned>(d->pc - 1));
  log(d, buf);
}

void register_handlers();

}

namespace dspops {
#define OP(name) static void name(GcDsp* d, uint16_t o)
OP(NOP) { (void)d, (void)o; }
OP(DAR) { d->ar[o & 3] = ar_dec(d, o & 3); }
OP(IAR) { d->ar[o & 3] = ar_inc(d, o & 3); }
OP(SUBARN) { d->ar[o & 3] = addr_sub(d->ar[o & 3], d->wr[o & 3], d->ix[o & 3]); }
OP(ADDARN) { d->ar[o & 3] = ar_addix(d, o & 3, (o >> 2) & 3); }
OP(HALT) { d->control |= CTRL_HALT; d->pc--; }
OP(LOOP) {
  const uint16_t count = read_reg(d, o & 31);
  if (count != 0) { push(d, 0, d->pc); push(d, 2, d->pc); push(d, 3, count); } else { skip_next(d); }
}
OP(LOOPI) {
  const uint16_t count = o & 0xFF;
  if (count != 0) { push(d, 0, d->pc); push(d, 2, d->pc); push(d, 3, count); } else { skip_next(d); }
}
static void bloop(GcDsp* d, uint16_t count) {
  const uint16_t end = fetch(d);
  if (count != 0) { push(d, 0, d->pc); push(d, 2, end); push(d, 3, count); } else { d->pc = end; skip_next(d); }
}
OP(BLOOP) { bloop(d, read_reg(d, o & 31)); }
OP(BLOOPI) { bloop(d, o & 0xFF); }
OP(LRI) { load_reg(d, o & 31, fetch(d)); }
OP(LR) { const uint16_t a = fetch(d); load_reg(d, o & 31, dmem_read(d, a)); }
OP(SR) { const uint16_t a = fetch(d); dmem_write(d, a, read_reg(d, o & 31)); }
OP(IF) { if (!condition(d, o & 15)) skip_next(d); }
OP(J) { const uint16_t a = fetch(d); if (condition(d, o & 15)) d->pc = a; }
OP(CALL) { const uint16_t a = fetch(d); if (condition(d, o & 15)) { push(d, 0, d->pc); d->pc = a; } }
OP(RET) { if (condition(d, o & 15)) d->pc = pop(d, 0); }
OP(RTI) { if (condition(d, o & 15)) { d->sr = pop(d, 1); d->pc = pop(d, 0); } }
OP(JR) { if (condition(d, o & 15)) d->pc = read_reg(d, (o >> 5) & 7); }
OP(CALLR) {
  if (condition(d, o & 15)) { const uint16_t t = read_reg(d, (o >> 5) & 7); push(d, 0, d->pc); d->pc = t; }
}
OP(LSRN) { const int k = shift_k(acc_m(d, 1)); d->ac[0] = k > 0 ? shift_logical(d->ac[0], k, false) : shift_logical(d->ac[0], -k, true); flags40(d, d->ac[0], false, false); }
OP(ASRN) { const int k = shift_k(acc_m(d, 1)); d->ac[0] = k > 0 ? shift_arith(d->ac[0], k, false) : shift_arith(d->ac[0], -k, true); flags40(d, d->ac[0], false, false); }
OP(ADDI) { do_add(d, (o >> 8) & 1, s16(fetch(d)) << 16); }
static void logic_imm(GcDsp* d, uint16_t o, int kind) {
  const int n = (o >> 8) & 1;
  const uint16_t imm = fetch(d);
  uint16_t m = acc_m(d, n);
  m = kind == 0 ? (m ^ imm) : kind == 1 ? (m & imm) : (m | imm);
  set_acc_m(d, n, m);
  flags16(d, m, outside_s32(d->ac[n]));
}
OP(XORI) { logic_imm(d, o, 0); }
OP(ANDI) { logic_imm(d, o, 1); }
OP(ORI) { logic_imm(d, o, 2); }
OP(CMPI) { const int n = (o >> 8) & 1; sub_flags(d, d->ac[n], s16(fetch(d)) << 16); }
OP(ANDF) { const int n = (o >> 8) & 1; const uint16_t imm = fetch(d); d->sr = ((acc_m(d, n) & imm) == 0) ? (d->sr | SR_LZ) : (d->sr & ~SR_LZ); }
OP(ANDCF) { const int n = (o >> 8) & 1; const uint16_t imm = fetch(d); d->sr = ((acc_m(d, n) & imm) == imm) ? (d->sr | SR_LZ) : (d->sr & ~SR_LZ); }
static void ilrr(GcDsp* d, uint16_t o, int update) {
  const int s = o & 3, n = (o >> 8) & 1;
  load_reg(d, 30 + n, imem_read(d, d->ar[s]));
  if (update == 1) d->ar[s] = ar_dec(d, s);
  if (update == 2) d->ar[s] = ar_inc(d, s);
  if (update == 3) d->ar[s] = ar_addix(d, s, s);
}
OP(ILRR) { ilrr(d, o, 0); }
OP(ILRRD) { ilrr(d, o, 1); }
OP(ILRRI) { ilrr(d, o, 2); }
OP(ILRRN) { ilrr(d, o, 3); }
OP(ADDIS) { do_add(d, (o >> 8) & 1, s8(static_cast<uint8_t>(o)) << 16); }
OP(CMPIS) { const int n = (o >> 8) & 1; sub_flags(d, d->ac[n], s8(static_cast<uint8_t>(o)) << 16); }
OP(LRIS) { load_reg(d, 0x18 + ((o >> 8) & 7), static_cast<uint16_t>(s8(static_cast<uint8_t>(o)))); }
OP(SBCLR) { d->sr &= ~(1 << ((o & 7) + 6)); }
OP(SBSET) { d->sr |= 1 << ((o & 7) + 6); }
OP(LSL) { const int n = (o >> 8) & 1; d->ac[n] = shift_arith(d->ac[n], o & 0x3F, true); flags40(d, d->ac[n], false, false); }
OP(ASL) { const int n = (o >> 8) & 1; d->ac[n] = shift_arith(d->ac[n], o & 0x3F, true); flags40(d, d->ac[n], false, false); }
OP(LSR) { const int n = (o >> 8) & 1; const int i = o & 0x3F; d->ac[n] = shift_logical(d->ac[n], i ? 64 - i : 0, false); flags40(d, d->ac[n], false, false); }
OP(ASR) { const int n = (o >> 8) & 1; const int i = o & 0x3F; d->ac[n] = shift_arith(d->ac[n], i ? 64 - i : 0, false); flags40(d, d->ac[n], false, false); }
OP(SI) { const uint16_t v = fetch(d); dmem_write(d, static_cast<uint16_t>(s8(static_cast<uint8_t>(o))), v); }
static void lrr(GcDsp* d, uint16_t o, int update) {
  const int s = (o >> 5) & 3;
  load_reg(d, o & 31, dmem_read(d, d->ar[s]));
  if (update == 1) d->ar[s] = ar_dec(d, s);
  if (update == 2) d->ar[s] = ar_inc(d, s);
  if (update == 3) d->ar[s] = ar_addix(d, s, s);
}
OP(LRR) { lrr(d, o, 0); }
OP(LRRD) { lrr(d, o, 1); }
OP(LRRI) { lrr(d, o, 2); }
OP(LRRN) { lrr(d, o, 3); }
static void srr(GcDsp* d, uint16_t o, int update) {
  const int dr = (o >> 5) & 3;
  dmem_write(d, d->ar[dr], read_reg(d, o & 31));
  if (update == 1) d->ar[dr] = ar_dec(d, dr);
  if (update == 2) d->ar[dr] = ar_inc(d, dr);
  if (update == 3) d->ar[dr] = ar_addix(d, dr, dr);
}
OP(SRR) { srr(d, o, 0); }
OP(SRRD) { srr(d, o, 1); }
OP(SRRI) { srr(d, o, 2); }
OP(SRRN) { srr(d, o, 3); }
OP(MRR) { load_reg(d, (o >> 5) & 31, read_reg(d, o & 31)); }
OP(LRS) { load_reg(d, 0x18 + ((o >> 8) & 7), dmem_read(d, static_cast<uint16_t>((d->config << 8) | (o & 0xFF)))); }
OP(SRSH) { dmem_write(d, static_cast<uint16_t>((d->config << 8) | (o & 0xFF)), acc_h(d, (o >> 8) & 1)); }
OP(SRS) { dmem_write(d, static_cast<uint16_t>((d->config << 8) | (o & 0xFF)), ext_src(d, (o >> 8) & 3)); }
static void logic_reg(GcDsp* d, int n, uint16_t operand, int kind) {
  uint16_t m = acc_m(d, n);
  m = kind == 0 ? (m ^ operand) : kind == 1 ? (m & operand) : (m | operand);
  set_acc_m(d, n, m);
  flags16(d, m, outside_s32(d->ac[n]));
}
OP(XORR) { logic_reg(d, (o >> 8) & 1, d->axh[(o >> 9) & 1], 0); }
OP(ANDR) { logic_reg(d, (o >> 8) & 1, d->axh[(o >> 9) & 1], 1); }
OP(ORR) { logic_reg(d, (o >> 8) & 1, d->axh[(o >> 9) & 1], 2); }
OP(XORC) { const int n = (o >> 8) & 1; logic_reg(d, n, acc_m(d, 1 - n), 0); }
OP(ANDC) { const int n = (o >> 8) & 1; logic_reg(d, n, acc_m(d, 1 - n), 1); }
OP(ORC) { const int n = (o >> 8) & 1; logic_reg(d, n, acc_m(d, 1 - n), 2); }
OP(NOT) { const int n = (o >> 8) & 1; logic_reg(d, n, 0xFFFF, 0); }
static void shift_var(GcDsp* d, int n, uint16_t src, bool logical) {
  const int k = shift_k(src);
  if (logical) {
    d->ac[n] = k > 0 ? shift_logical(d->ac[n], k, true) : shift_logical(d->ac[n], -k, false);
  } else {
    d->ac[n] = k > 0 ? shift_arith(d->ac[n], k, true) : shift_arith(d->ac[n], -k, false);
  }
  flags40(d, d->ac[n], false, false);
}
OP(LSRNRX) { shift_var(d, (o >> 8) & 1, d->axh[(o >> 9) & 1], true); }
OP(ASRNRX) { shift_var(d, (o >> 8) & 1, d->axh[(o >> 9) & 1], false); }
OP(LSRNR) { const int n = (o >> 8) & 1; shift_var(d, n, acc_m(d, 1 - n), true); }
OP(ASRNR) { const int n = (o >> 8) & 1; shift_var(d, n, acc_m(d, 1 - n), false); }
OP(ADDR) { do_add(d, (o >> 8) & 1, sel_ax_part(d, (o >> 9) & 3) << 16); }
OP(ADDAX) { do_add(d, (o >> 8) & 1, ax(d, (o >> 9) & 1)); }
OP(ADD) { const int n = (o >> 8) & 1; do_add(d, n, d->ac[1 - n]); }
OP(ADDP) { do_add(d, (o >> 8) & 1, prod(d)); }
static void do_sub(GcDsp* d, int n, int64_t b) { d->ac[n] = sub_flags(d, d->ac[n], b); }
OP(SUBR) { do_sub(d, (o >> 8) & 1, sel_ax_part(d, (o >> 9) & 3) << 16); }
OP(SUBAX) { do_sub(d, (o >> 8) & 1, ax(d, (o >> 9) & 1)); }
OP(SUB) { const int n = (o >> 8) & 1; do_sub(d, n, d->ac[1 - n]); }
OP(SUBP) { do_sub(d, (o >> 8) & 1, prod(d)); }
OP(MOVR) { const int n = (o >> 8) & 1; d->ac[n] = sel_ax_part(d, (o >> 9) & 3) << 16; flags40(d, d->ac[n], false, false); }
OP(MOVAX) { const int n = (o >> 8) & 1; d->ac[n] = ax(d, (o >> 9) & 1); flags40(d, d->ac[n], false, false); }
OP(MOV) { const int n = (o >> 8) & 1; d->ac[n] = d->ac[1 - n]; flags40(d, d->ac[n], false, false); }
OP(MOVP) { const int n = (o >> 8) & 1; const int64_t p = prod(d); d->ac[n] = w40(p); flags40(d, p, false, false); }
OP(ADDAXL) { do_add(d, (o >> 8) & 1, d->axl[(o >> 9) & 1]); }
OP(INCM) { do_add(d, (o >> 8) & 1, 0x10000); }
OP(INC) { do_add(d, (o >> 8) & 1, 1); }
OP(DECM) { do_sub(d, (o >> 8) & 1, 0x10000); }
OP(DEC) { do_sub(d, (o >> 8) & 1, 1); }
OP(NEG) {
  const int n = (o >> 8) & 1;
  const int64_t a = d->ac[n];
  const int64_t r = w40(-a);
  d->ac[n] = r;
  flags40(d, r, a == 0, a == -0x8000000000ll);
}
OP(MOVNP) { const int n = (o >> 8) & 1; const int64_t p = -prod(d); d->ac[n] = w40(p); flags40(d, p, false, false); }
OP(NX) { (void)d, (void)o; }
OP(CLR) { const int n = (o >> 11) & 1; d->ac[n] = 0; flags40(d, 0, false, false); }
OP(CMP) { (void)o; sub_flags(d, d->ac[0], d->ac[1]); }
OP(MULAXH) { (void)o; set_prod(d, multiply(d, d->axh[0], d->axh[0], MUL_SIGNED)); }
OP(CLRP) { (void)o; d->pl = 0; d->pm1 = 0xFFF0; d->ph = 0x00FF; d->pm2 = 0x0010; }
OP(TSTPROD) { (void)o; flags40(d, prod(d), false, false); }
OP(TSTAXH) { flags16(d, d->axh[(o >> 8) & 1], false); }
static void mode_bits(GcDsp* d, uint16_t o) {
  switch ((o >> 8) & 7) {
  case 2: d->sr &= ~SR_AM; break;
  case 3: d->sr |= SR_AM; break;
  case 4: d->sr &= ~SR_SU; break;
  case 5: d->sr |= SR_SU; break;
  case 6: d->sr &= ~SR_SXM; break;
  case 7: d->sr |= SR_SXM; break;
  default: break;
  }
}
OP(M2) { mode_bits(d, o); }
OP(M0) { mode_bits(d, o); }
OP(CLR15) { mode_bits(d, o); }
OP(SET15) { mode_bits(d, o); }
OP(SET16) { mode_bits(d, o); }
OP(SET40) { mode_bits(d, o); }
OP(MUL) { const int s = (o >> 11) & 1; set_prod(d, multiply(d, d->axl[s], d->axh[s], MUL_SIGNED)); }
OP(ASR16) { const int n = (o >> 11) & 1; d->ac[n] = d->ac[n] >> 16; flags40(d, d->ac[n], false, false); }
OP(MULMVZ) { const int s = (o >> 11) & 1; store_mul(d, (o >> 8) & 1, multiply(d, d->axl[s], d->axh[s], MUL_SIGNED), 2); }
OP(MULAC) { const int s = (o >> 11) & 1; store_mul(d, (o >> 8) & 1, multiply(d, d->axl[s], d->axh[s], MUL_SIGNED), 0); }
OP(MULMV) { const int s = (o >> 11) & 1; store_mul(d, (o >> 8) & 1, multiply(d, d->axl[s], d->axh[s], MUL_SIGNED), 1); }
static int64_t mulx_product(const GcDsp* d, uint16_t o) {
  const int s = (o >> 12) & 1, t = (o >> 11) & 1;
  if (s == 0 && t == 0) return multiply(d, d->axl[0], d->axl[1], MUL_UNSIGNED);
  if (s == 0 && t == 1) return multiply(d, d->axl[0], d->axh[1], MUL_MIXED);
  if (s == 1 && t == 0) return multiply(d, d->axl[1], d->axh[0], MUL_MIXED);
  return multiply(d, d->axh[0], d->axh[1], MUL_SIGNED);
}
OP(MULX) { set_prod(d, mulx_product(d, o)); }
OP(ABS) {
  const int n = (o >> 11) & 1;
  if (d->ac[n] < 0) d->ac[n] = w40(-d->ac[n]);
  flags40(d, d->ac[n], false, false);
}
OP(TST) { flags40(d, d->ac[(o >> 11) & 1], false, false); }
OP(MULXMVZ) { store_mul(d, (o >> 8) & 1, mulx_product(d, o), 2); }
OP(MULXAC) { store_mul(d, (o >> 8) & 1, mulx_product(d, o), 0); }
OP(MULXMV) { store_mul(d, (o >> 8) & 1, mulx_product(d, o), 1); }
static int64_t mulc_product(const GcDsp* d, uint16_t o) {
  const int s = (o >> 12) & 1, t = (o >> 11) & 1;
  return multiply(d, acc_m(d, s), d->axh[t], MUL_SIGNED);
}
OP(MULC) { set_prod(d, mulc_product(d, o)); }
OP(CMPAXH) { const int r = (o >> 12) & 1, s = (o >> 11) & 1; sub_flags(d, d->ac[s], s16(d->axh[r]) << 16); }
OP(MULCMVZ) { store_mul(d, (o >> 8) & 1, mulc_product(d, o), 2); }
OP(MULCAC) { store_mul(d, (o >> 8) & 1, mulc_product(d, o), 0); }
OP(MULCMV) { store_mul(d, (o >> 8) & 1, mulc_product(d, o), 1); }
static uint16_t reg18(const GcDsp* d, int idx) {
  return idx == 0 ? d->axl[0] : idx == 1 ? d->axl[1] : idx == 2 ? d->axh[0] : d->axh[1];
}
OP(MADDX) { const int s = (o >> 9) & 1, t = (o >> 8) & 1; set_prod(d, prod(d) + multiply(d, reg18(d, s * 2), reg18(d, 1 + t * 2), MUL_SIGNED)); }
OP(MSUBX) { const int s = (o >> 9) & 1, t = (o >> 8) & 1; set_prod(d, prod(d) - multiply(d, reg18(d, s * 2), reg18(d, 1 + t * 2), MUL_SIGNED)); }
OP(MADDC) { const int s = (o >> 9) & 1, t = (o >> 8) & 1; set_prod(d, prod(d) + multiply(d, acc_m(d, s), d->axh[t], MUL_SIGNED)); }
OP(MSUBC) { const int s = (o >> 9) & 1, t = (o >> 8) & 1; set_prod(d, prod(d) - multiply(d, acc_m(d, s), d->axh[t], MUL_SIGNED)); }
OP(LSL16) { const int n = (o >> 8) & 1; d->ac[n] = shift_arith(d->ac[n], 16, true); flags40(d, d->ac[n], false, false); }
OP(MADD) { const int s = (o >> 8) & 1; set_prod(d, prod(d) + multiply(d, d->axl[s], d->axh[s], MUL_SIGNED)); }
OP(LSR16) { const int n = (o >> 8) & 1; d->ac[n] = shift_logical(d->ac[n], 16, false); flags40(d, d->ac[n], false, false); }
OP(MSUB) { const int s = (o >> 8) & 1; set_prod(d, prod(d) - multiply(d, d->axl[s], d->axh[s], MUL_SIGNED)); }
OP(ADDPAXZ) {
  const int n = (o >> 8) & 1, s = (o >> 9) & 1;
  const int64_t p = prod(d);
  const int64_t r = w40(round_hm(p) + (ax(d, s) & ~0xFFFFll));
  d->ac[n] = r;
  flags40(d, r, static_cast<uint64_t>(p & kMask40) > static_cast<uint64_t>(r & kMask40), false);
}
OP(CLRL) { const int n = (o >> 8) & 1; const int64_t r = round_hm(d->ac[n]); d->ac[n] = w40(r); flags40(d, r, false, false); }
OP(MOVPZ) { const int n = (o >> 8) & 1; const int64_t r = round_hm(prod(d)); d->ac[n] = w40(r); flags40(d, r, false, false); }
#undef OP

struct Named {
  const char* name;
  Handler fn;
};
#define N(x) {#x, x}
const Named kNamed[] = {
    N(NOP), N(DAR), N(IAR), N(SUBARN), N(ADDARN), N(HALT), N(LOOP), N(BLOOP), N(LRI), N(LR), N(SR), N(IF), N(J),
    N(CALL), N(RET), N(RTI), N(LSRN), N(ASRN), N(ADDI), N(XORI), N(ANDI), N(ORI), N(CMPI), N(ANDF), N(ANDCF),
    N(ILRR), N(ILRRD), N(ILRRI), N(ILRRN), N(ADDIS), N(CMPIS), N(LRIS), N(LOOPI), N(BLOOPI), N(SBCLR), N(SBSET),
    N(LSL), N(LSR), N(ASL), N(ASR), N(SI), N(JR), N(CALLR), N(LRR), N(LRRD), N(LRRI), N(LRRN), N(SRR), N(SRRD),
    N(SRRI), N(SRRN), N(MRR), N(LRS), N(SRSH), N(SRS), N(XORR), N(ANDR), N(ORR), N(ANDC), N(ORC), N(XORC), N(NOT),
    N(LSRNRX), N(ASRNRX), N(LSRNR), N(ASRNR), N(ADDR), N(ADDAX), N(ADD), N(ADDP), N(SUBR), N(SUBAX), N(SUB),
    N(SUBP), N(MOVR), N(MOVAX), N(MOV), N(MOVP), N(ADDAXL), N(INCM), N(INC), N(DECM), N(DEC), N(NEG), N(MOVNP),
    N(NX), N(CLR), N(CMP), N(MULAXH), N(CLRP), N(TSTPROD), N(TSTAXH), N(M2), N(M0), N(CLR15), N(SET15), N(SET16),
    N(SET40), N(MUL), N(ASR16), N(MULMVZ), N(MULAC), N(MULMV), N(MULX), N(ABS), N(TST), N(MULXMVZ), N(MULXAC),
    N(MULXMV), N(MULC), N(CMPAXH), N(MULCMVZ), N(MULCAC), N(MULCMV), N(MADDX), N(MSUBX), N(MADDC), N(MSUBC),
    N(LSL16), N(MADD), N(LSR16), N(MSUB), N(ADDPAXZ), N(CLRL), N(MOVPZ),
};
#undef N
}

namespace {

void register_handlers() {
  static bool done = false;
  if (done) return;
  done = true;
  int count = 0;
  const DspOpInfo* ops = GcDsp_MainOps(&count);
  for (int i = 0; i < count && i < 256; i++) {
    sHandlers[i] = op_unknown;
    for (const auto& n : dspops::kNamed) {
      if (std::strcmp(n.name, ops[i].name) == 0) {
        sHandlers[i] = n.fn;
      }
    }
  }
}

void check_exceptions(GcDsp* d) {
  if (d->control & CTRL_INT) {
    if (d->sr & SR_EIE) {
      d->pending |= 1 << 7;
      d->control &= ~CTRL_INT;
    }
  }
  for (int i = 7; i >= 1; i--) {
    if ((d->pending & (1 << i)) && ((d->sr & SR_IE) || i == 7)) {
      push(d, 0, d->pc);
      push(d, 1, d->sr);
      d->pc = static_cast<uint16_t>(2 * i);
      d->pending &= ~(1 << i);
      d->sr &= i == 7 ? ~SR_EIE : ~SR_IE;
      return;
    }
  }
}

void step(GcDsp* d) {
  check_exceptions(d);
  if (d->pc >= 0x8000 && !d->haveIrom) {
    auto it = d->romRoutines.find(d->pc);
    if (it == d->romRoutines.end()) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "the program called ROM address %04X, which the port doesn't implement", d->pc);
      log(d, buf);
      d->control |= CTRL_HALT;
      return;
    }
    if (it->second.first(d, it->second.second) == GCDSP_ROM_RETURN) {
      d->pc = pop(d, 0);
    }
    return;
  }
  const uint16_t opc = fetch(d);
  const int op = GcDsp_DecodeMain(opc);
  if (op < 0) {
    op_unknown(d, opc);
    return;
  }
  const int ext = GcDsp_DecodeExt(opc);
  d->queued = 0;
  if (ext >= 0) {
    ext_execute(d, opc, ext);
  }
  sHandlers[op](d, opc);
  for (int i = 0; i < d->queued; i++) {
    write_reg(d, d->queue[i].reg, d->queue[i].value);
  }
  d->queued = 0;
  const uint16_t p = d->pc;
  if (d->st[2] != 0 && d->st[3] != 0 && static_cast<uint16_t>(p - 1) == d->st[2]) {
    d->st[3]--;
    if (d->st[3] > 0) {
      d->pc = d->st[0];
    } else {
      pop(d, 0);
      pop(d, 2);
      pop(d, 3);
    }
  }
}

void power_on(GcDsp* d) {
  std::memset(d->ar, 0, sizeof(d->ar));
  std::memset(d->ix, 0, sizeof(d->ix));
  for (auto& w : d->wr) w = 0xFFFF;
  std::memset(d->st, 0, sizeof(d->st));
  std::memset(d->stack, 0, sizeof(d->stack));
  std::memset(d->stackPtr, 0, sizeof(d->stackPtr));
  d->ac[0] = d->ac[1] = 0;
  std::memset(d->axl, 0, sizeof(d->axl));
  std::memset(d->axh, 0, sizeof(d->axh));
  d->config = 0;
  d->sr = SR_IE | SR_EIE;
  d->pl = d->pm1 = d->ph = d->pm2 = 0;
  d->pc = 0;
  for (auto& w : d->iram) w = 0x0021;
  std::memset(d->dram, 0, sizeof(d->dram));
  std::memset(d->hw, 0, sizeof(d->hw));
  d->dspMail = d->cpuMail = 0;
  d->control = CTRL_INIT | CTRL_HALT;
  d->pending = 0;
  d->acsa = d->acea = d->acca = 0;
  d->accStopped = false;
  std::memset(d->portRomState, 0, sizeof(d->portRomState));
}

}

extern "C" {

GcDsp* GcDsp_Create(const GcDspHost* host) {
  register_handlers();
  auto* d = new GcDsp();
  if (host != nullptr) d->host = *host;
  power_on(d);
  return d;
}
void GcDsp_Destroy(GcDsp* dsp) { delete dsp; }
GcDsp* GcDsp_Clone(const GcDsp* dsp) { return new GcDsp(*dsp); }
void GcDsp_SetHost(GcDsp* dsp, const GcDspHost* host) { dsp->host = *host; }
void GcDsp_LoadIrom(GcDsp* dsp, const uint16_t* words) {
  std::memcpy(dsp->irom, words, sizeof(dsp->irom));
  dsp->haveIrom = true;
}
void GcDsp_LoadCoef(GcDsp* dsp, const uint16_t* words) {
  std::memcpy(dsp->coef, words, sizeof(dsp->coef));
  dsp->haveCoef = true;
}
void GcDsp_SetRomRoutine(GcDsp* dsp, uint16_t address, GcDspRomRoutine routine, void* user) {
  dsp->romRoutines[address] = {routine, user};
}
int GcDsp_Run(GcDsp* dsp, int steps) {
  int n = 0;
  while (n < steps && !(dsp->control & CTRL_HALT)) {
    step(dsp);
    n++;
  }
  return n;
}
void GcDsp_WriteControl(GcDsp* dsp, uint16_t value) {
  const bool wasInit = dsp->control & CTRL_INIT;
  if (value & CTRL_RESET) {
    dsp->pc = 0x8000;
    for (auto& w : dsp->wr) w = 0xFFFF;
  }
  dsp->control = static_cast<uint16_t>((dsp->control & ~(CTRL_INT | CTRL_HALT | CTRL_INIT)) |
                                       (value & (CTRL_INT | CTRL_HALT | CTRL_INIT)));
  if (wasInit && !(value & CTRL_INIT)) {
    dsp->pc = 0;
  }
}
uint16_t GcDsp_ReadControl(const GcDsp* dsp) { return dsp->control; }
void GcDsp_CpuSendMail(GcDsp* dsp, uint32_t mail) { dsp->cpuMail = mail | 0x80000000u; }
uint32_t GcDsp_CpuMailToDsp(const GcDsp* dsp) { return dsp->cpuMail; }
uint32_t GcDsp_CpuPeekMail(const GcDsp* dsp) { return dsp->dspMail; }
uint32_t GcDsp_CpuTakeMail(GcDsp* dsp) {
  const uint32_t m = dsp->dspMail;
  dsp->dspMail &= 0x7FFFFFFF;
  return m;
}
bool GcDsp_IsHalted(const GcDsp* dsp) { return dsp->control & CTRL_HALT; }
uint16_t GcDsp_ReadReg(GcDsp* dsp, int reg) { return read_reg(dsp, reg); }
void GcDsp_WriteReg(GcDsp* dsp, int reg, uint16_t value) { write_reg(dsp, reg, value); }
uint16_t GcDsp_ReadDmem(GcDsp* dsp, uint16_t address) { return dmem_read(dsp, address); }
void GcDsp_WriteDmem(GcDsp* dsp, uint16_t address, uint16_t value) { dmem_write(dsp, address, value); }
uint16_t GcDsp_Pc(const GcDsp* dsp) { return dsp->pc; }
void GcDsp_SetPc(GcDsp* dsp, uint16_t pc) { dsp->pc = pc; }
int64_t GcDsp_Acc(const GcDsp* dsp, int n) { return dsp->ac[n & 1]; }
void GcDsp_SetAcc(GcDsp* dsp, int n, int64_t value) { dsp->ac[n & 1] = w40(value); }
uint16_t GcDsp_Sr(const GcDsp* dsp) { return dsp->sr; }
uint16_t* GcDsp_Iram(GcDsp* dsp) { return dsp->iram; }
uint16_t* GcDsp_Dram(GcDsp* dsp) { return dsp->dram; }
uint32_t* GcDsp_PortRomState(GcDsp* dsp) { return dsp->portRomState; }
int GcDsp_RunUntilIdle(GcDsp* dsp, int maxSteps) {
  int n = 0;
  dsp->waitingForCpu = false;
  while (n < maxSteps && !(dsp->control & CTRL_HALT) && !dsp->waitingForCpu) {
    step(dsp);
    n++;
  }
  return n;
}
void GcDsp_Log(GcDsp* dsp, const char* message) { log(dsp, message); }
size_t GcDsp_SaveState(const GcDsp* d, uint8_t* out, size_t size, bool memories) {
  size_t n = 0;
  auto put = [&](const void* p, size_t bytes) {
    if (out != nullptr && n + bytes <= size) std::memcpy(out + n, p, bytes);
    n += bytes;
  };
  put(d->ar, sizeof(d->ar));
  put(d->ix, sizeof(d->ix));
  put(d->wr, sizeof(d->wr));
  put(d->st, sizeof(d->st));
  put(d->stack, sizeof(d->stack));
  put(d->stackPtr, sizeof(d->stackPtr));
  put(d->ac, sizeof(d->ac));
  put(d->axl, sizeof(d->axl));
  put(d->axh, sizeof(d->axh));
  const uint16_t words[] = {d->config, d->sr, d->pl, d->pm1, d->ph, d->pm2, d->pc, d->control};
  put(words, sizeof(words));
  put(d->hw, sizeof(d->hw));
  const uint32_t longs[] = {d->dspMail, d->cpuMail, d->acsa, d->acea, d->acca};
  put(longs, sizeof(longs));
  const uint8_t bytes[] = {d->pending, static_cast<uint8_t>(d->accStopped), static_cast<uint8_t>(d->waitingForCpu),
                           static_cast<uint8_t>(d->queued)};
  put(bytes, sizeof(bytes));
  put(d->portRomState, sizeof(d->portRomState));
  if (memories) {
    put(d->iram, sizeof(d->iram));
    put(d->dram, sizeof(d->dram));
  }
  return n;
}

}
