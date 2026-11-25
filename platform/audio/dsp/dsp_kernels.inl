// The native tier: hand-written kernels for the hot hardware loops of MusyX's program (included by dsp_core.cpp,
// inside its anonymous namespace, after the instruction code).
//
// Why: nearly all the time the sound chip's program takes goes into a dozen small hardware loops (copies, mixing,
// filters). The interpreter does their instructions one at a time, ~60 processor instructions each; a kernel is the
// loop's work written as plain C++ with the registers in local variables, a few processor instructions per chip
// instruction (docs/PERFORMANCE.md, "The sound chip's native tier").
//
// What a kernel is: the loop's meaning, from the chip's manual (what each instruction does to the registers, the
// accumulators, memory and the flags), written once for the whole loop. Nothing of the program's bytes is in here: a
// kernel is found by a 64-bit checksum of the loop's words (loop_hash), so another program, or a loop that differs
// in a single word, finds none and runs as it always did.
//
// Exact, always: a kernel is only run when every memory word it will touch, over all the iterations it is asked to
// do, is plain data memory and each address register it uses counts up without wrap-around ($wr = 0xFFFF), and it
// leaves the chip exactly as the interpreter would after the same instructions: registers, accumulators, flags (the
// last flag-setting instruction's, and the sticky overflow of all of them), memory, in the order the loop writes it
// (streams may overlap). Otherwise it says no, changes nothing, and the loop runs as before. Checked by
// `xd_dsp_run --kernels` (every kernel against the reference chip on made-up states) and --lockstep, --bench.

DSP_INLINE uint64_t loop_hash(const uint16_t* words, int count) {
  uint64_t h = 0xCBF29CE484222325ull; // FNV-1a over the words
  for (int i = 0; i < count; i++) h = (h ^ words[i]) * 0x100000001B3ull;
  return h;
}

// ── Pieces the kernels share (each is what the interpreter's own rule says, in plain integers) ──
// ac.m as a store or a move reads it (§4): saturated in 40-bit mode
DSP_INLINE uint16_t k_acc_m(int64_t ac, bool sxm) {
  if (sxm && outside_s32(ac)) return ac > 0 ? 0x7FFF : 0x8000;
  return static_cast<uint16_t>(ac >> 16);
}
DSP_INLINE int64_t k_ax(uint16_t h, uint16_t l) { return static_cast<int32_t>((static_cast<uint32_t>(h) << 16) | l); }
// The flags of the last flag-setting instruction, and whether any of them overflowed
struct KFlags {
  int64_t r = 0;
  bool carry = false, overflow = false, any = false;
  DSP_INLINE void set(int64_t result, bool c, bool o) {
    r = result;
    carry = c;
    overflow = o;
    any |= o;
  }
};
DSP_INLINE void k_finish_flags(GcDsp* d, const KFlags& fl) {
  flags40(d, fl.r, fl.carry, fl.overflow);
  if (fl.any) d->rf.sr |= SR_OS;
}
// acc += b (ADD...): the result and the flags
DSP_INLINE int64_t k_add(int64_t a, int64_t b, KFlags& fl) {
  const int64_t r = w40(a + b);
  fl.set(r, add_carry(a, r), add_overflow(a, b, r));
  return r;
}
// `count` words from an address register that counts up without wrap-around: all in data memory
DSP_INLINE bool k_range(const GcDsp* d, int ar, uint32_t count) {
  return d->rf.wr[ar] == 0xFFFF && static_cast<uint32_t>(d->rf.ar[ar]) + count <= 0x1000u;
}

// `count` words from address `a` up can be read without anything happening besides the read: data memory, or the
// coefficient ROM once it is loaded (it logs once when it is not)
DSP_INLINE bool k_read_ok(const GcDsp* d, uint16_t a, uint32_t count) {
  if (a < 0x1000) return a + count <= 0x1000u;
  return d->haveCoef && a + count <= 0x1800u;
}
DSP_INLINE uint16_t k_rd(const GcDsp* d, uint16_t a) { return a < 0x1000 ? d->dram[a] : d->coef[a & 0x7FF]; }

// ── Kernel: the stereo mixing step (8 instructions an iteration) ────────────────────────────────
// Two streams of 32-bit values (two words each, from $ar0 and from $ar3 by the double load) go through the
// accumulators and out to a third stream ($ar2), four words an iteration, each word the accumulator's value from
// before the instruction that stores it.
bool kernel_stream_mix(GcDsp* d, int iters) {
  const uint32_t n = 4u * static_cast<uint32_t>(iters);
  if (!k_range(d, 0, n) || !k_range(d, 3, n) || !k_range(d, 2, n)) return false;
  uint16_t* const m = d->dram;
  const bool sxm = (d->rf.sr & SR_SXM) != 0;
  uint32_t a0 = d->rf.ar[0], a2 = d->rf.ar[2], a3 = d->rf.ar[3];
  uint16_t axh0 = d->rf.axh[0], axh1 = d->rf.axh[1], axl0 = d->rf.axl[0], axl1 = d->rf.axl[1];
  int64_t c0 = d->ac[0], c1 = d->ac[1];
  KFlags fl;
  // The double load: the first register from $ar0's word, the second from $ar3's (the same word when the two
  // addresses are in the same 1024-word block: the chip reads once)
  auto load2 = [&](uint16_t& x, uint16_t& y) {
    const uint16_t v0 = m[a0];
    const uint16_t v1 = ((a0 ^ a3) & 0xFC00) == 0 ? v0 : m[a3];
    a0++;
    a3++;
    x = v0;
    y = v1;
  };
  for (int i = 0; i < iters; i++) {
    load2(axh0, axh1);
    load2(axl0, axl1);
    m[a2++] = k_acc_m(c0, sxm); // MOVAX'S: store ac0.m, ac1 = ax1
    c1 = k_ax(axh1, axl1);
    fl.set(c1, false, false);
    m[a2++] = static_cast<uint16_t>(c0); // ADDAX'S: store ac0.l, ac1 += ax0
    c1 = k_add(c1, k_ax(axh0, axl0), fl);
    load2(axh0, axh1);
    load2(axl0, axl1);
    m[a2++] = k_acc_m(c1, sxm); // MOVAX'S: store ac1.m, ac0 = ax1
    c0 = k_ax(axh1, axl1);
    fl.set(c0, false, false);
    m[a2++] = static_cast<uint16_t>(c1); // ADDAX'S: store ac1.l, ac0 += ax0
    c0 = k_add(c0, k_ax(axh0, axl0), fl);
  }
  d->rf.ar[0] = static_cast<uint16_t>(a0);
  d->rf.ar[2] = static_cast<uint16_t>(a2);
  d->rf.ar[3] = static_cast<uint16_t>(a3);
  d->rf.axh[0] = axh0;
  d->rf.axh[1] = axh1;
  d->rf.axl[0] = axl0;
  d->rf.axl[1] = axl1;
  d->ac[0] = c0;
  d->ac[1] = c1;
  k_finish_flags(d, fl);
  return true;
}

// ── Kernel: sample-pair copy with negation (8 instructions an iteration) ────────────────────────
// A 32-bit value is read from $ar3 (high word, low word), its high word of ax0 written twice to $ar0, the value
// written to $ar2 and negated, and the negated value written to $ar1.
bool kernel_copy_negate(GcDsp* d, int iters) {
  const uint32_t n = 2u * static_cast<uint32_t>(iters);
  if (!k_range(d, 0, n) || !k_range(d, 1, n) || !k_range(d, 2, n) || !k_range(d, 3, n)) return false;
  uint16_t* const m = d->dram;
  const bool sxm = (d->rf.sr & SR_SXM) != 0;
  uint32_t a0 = d->rf.ar[0], a1 = d->rf.ar[1], a2 = d->rf.ar[2], a3 = d->rf.ar[3];
  const uint16_t axh0 = d->rf.axh[0];
  int64_t c0 = d->ac[0];
  KFlags fl;
  for (int i = 0; i < iters; i++) {
    { // load ac0.m
      const uint16_t v = m[a3++];
      c0 = (c0 & ~0xFFFF0000ll) | (static_cast<int64_t>(v) << 16);
      if (sxm) c0 = s16(v) << 16;
    }
    m[a0++] = axh0;
    { // load ac0.l
      const uint16_t v = m[a3++];
      c0 = (c0 & ~0xFFFFll) | v;
    }
    m[a0++] = axh0;
    m[a2++] = k_acc_m(c0, sxm);
    m[a2++] = static_cast<uint16_t>(c0); // NEG'S: store ac0.l, ac0 = -ac0
    {
      const int64_t a = c0;
      c0 = w40(-a);
      fl.set(c0, a == 0, a == -0x8000000000ll);
    }
    m[a1++] = k_acc_m(c0, sxm);
    m[a1++] = static_cast<uint16_t>(c0);
  }
  d->rf.ar[0] = static_cast<uint16_t>(a0);
  d->rf.ar[1] = static_cast<uint16_t>(a1);
  d->rf.ar[2] = static_cast<uint16_t>(a2);
  d->rf.ar[3] = static_cast<uint16_t>(a3);
  d->ac[0] = c0;
  k_finish_flags(d, fl);
  return true;
}


// ── Kernel: the filter's multiply-accumulate rounds (7 instructions an iteration) ────────────────
// Four double loads and three multiply-accumulates an iteration; the two data pointers ($ar0, $ar3) are fetched
// from a table ($ar2, two words an iteration) in the middle of it, the result word goes to $ar1. The table's words
// are looked at before the loop starts (they must be pointers into data memory and the table must not be where the
// loop writes), so that the kernel can say no before it has changed anything.
DSP_INLINE int64_t k_mul(bool su, bool am, uint16_t a, uint16_t b, MulMode mode) { // as multiply()
  int64_t r;
  if (su && mode == MUL_UNSIGNED) {
    r = static_cast<int64_t>(a) * b;
  } else if (su && mode == MUL_MIXED) {
    r = static_cast<int64_t>(a) * s16(b);
  } else {
    r = s16(a) * s16(b);
  }
  return am ? r : r * 2;
}
bool kernel_mac_rounds(GcDsp* d, int iters) {
  const uint32_t n = static_cast<uint32_t>(iters);
  if (!k_range(d, 1, n) || !k_range(d, 2, 2 * n) || d->rf.wr[0] != 0xFFFF || d->rf.wr[3] != 0xFFFF) return false;
  uint16_t* const m = d->dram;
  uint32_t a1 = d->rf.ar[1], a2 = d->rf.ar[2];
  if (a1 < a2 + 2 * n && a2 < a1 + n) return false; // the loop would write into its own table
  uint32_t p0 = d->rf.ar[0], p3 = d->rf.ar[3];
  if (!k_read_ok(d, p0, 2) || !k_read_ok(d, p3, 2)) return false; // two words are read from each
  for (uint32_t i = 0; i < 2 * n; i++) {
    if (!k_read_ok(d, m[a2 + i], 2)) return false;
  }
  const bool sxm = (d->rf.sr & SR_SXM) != 0, su = (d->rf.sr & SR_SU) != 0, am = (d->rf.sr & SR_AM) != 0;
  uint16_t axh0 = d->rf.axh[0], axh1 = d->rf.axh[1], axl0 = d->rf.axl[0], axl1 = d->rf.axl[1];
  int64_t c0 = d->ac[0], P = prod(d);
  KFlags fl;
  // the double load of $ar0's and $ar3's words: the second register reads the first's word when both addresses are
  // in the same 1024-word block
  auto load2 = [&](uint16_t& x, uint16_t& y) {
    const uint16_t v0 = k_rd(d, p0);
    const uint16_t v1 = ((p0 ^ p3) & 0xFC00) == 0 ? v0 : k_rd(d, p3);
    p0++;
    p3++;
    x = v0;
    y = v1;
  };
  for (int i = 0; i < iters; i++) {
    { // MULXMV'LD: ac0 = prod, prod = axl0 * axl1 (unsigned when the chip is in that mode), then axh <- two words
      uint16_t v0, v1;
      load2(v0, v1);
      const int64_t np = k_mul(su, am, axl0, axl1, MUL_UNSIGNED);
      c0 = w40(P);
      fl.set(c0, false, false);
      P = w40(np);
      axh0 = v0;
      axh1 = v1;
    }
    { // MULXAC'LD: ac0 += prod, prod = axh0 * axh1
      uint16_t v0, v1;
      load2(v0, v1);
      const int64_t np = k_mul(su, am, axh0, axh1, MUL_SIGNED);
      c0 = w40(c0 + P);
      fl.set(c0, false, false);
      P = w40(np);
      axh0 = v0;
      axh1 = v1;
    }
    p0 = m[a2++]; // LRRI $ar0, @$ar2
    p3 = m[a2++]; // LRRI $ar3, @$ar2
    { // MULXAC'LD
      uint16_t v0, v1;
      load2(v0, v1);
      const int64_t np = k_mul(su, am, axh0, axh1, MUL_SIGNED);
      c0 = w40(c0 + P);
      fl.set(c0, false, false);
      P = w40(np);
      axh0 = v0;
      axh1 = v1;
    }
    { // ADDP'LD: ac0 += prod (flags of a real addition), axl <- two words
      uint16_t v0, v1;
      load2(v0, v1);
      c0 = k_add(c0, P, fl);
      axl0 = v0;
      axl1 = v1;
    }
    // MULX'S: store ac0.m, prod = axh0 * axh1
    m[a1++] = k_acc_m(c0, sxm);
    P = w40(k_mul(su, am, axh0, axh1, MUL_SIGNED));
  }
  d->rf.ar[0] = static_cast<uint16_t>(p0);
  d->rf.ar[3] = static_cast<uint16_t>(p3);
  d->rf.ar[1] = static_cast<uint16_t>(a1);
  d->rf.ar[2] = static_cast<uint16_t>(a2);
  d->rf.axh[0] = axh0;
  d->rf.axh[1] = axh1;
  d->rf.axl[0] = axl0;
  d->rf.axl[1] = axl1;
  d->ac[0] = c0;
  set_prod(d, P);
  k_finish_flags(d, fl);
  return true;
}

// ── Kernel: the filter's interpolation rounds (11 instructions an iteration) ────────────────────
// A pointer ($ar0) is fetched from a table ($ar2, two words an iteration, the second is a coefficient); two
// neighbouring words are read, their difference is multiplied by the coefficient and added to the second, the
// result goes to $ar1 (the table must not be where the loop writes, and the pointers must be inside data memory:
// looked at first, as in kernel_mac_rounds).
bool kernel_interpolate(GcDsp* d, int iters) {
  const uint32_t n = static_cast<uint32_t>(iters);
  if (!k_range(d, 1, n) || !k_range(d, 2, 2 * n) || d->rf.wr[0] != 0xFFFF) return false;
  uint16_t* const m = d->dram;
  uint32_t a1 = d->rf.ar[1], a2 = d->rf.ar[2];
  if (a1 < a2 + 2 * n && a2 < a1 + n) return false;
  for (uint32_t i = 0; i < n; i++) {
    const uint16_t t = m[a2 + 2 * i];
    if (!k_read_ok(d, static_cast<uint16_t>(t - 1), 2)) return false; // words are read at t and t-1
  }
  const bool sxm = (d->rf.sr & SR_SXM) != 0, su = (d->rf.sr & SR_SU) != 0, am = (d->rf.sr & SR_AM) != 0;
  const uint16_t ix0 = d->rf.ix[0];
  uint16_t ar0 = d->rf.ar[0];
  uint16_t axh0 = d->rf.axh[0], axh1 = d->rf.axh[1], axl1 = d->rf.axl[1];
  int64_t c0 = d->ac[0], c1 = d->ac[1], P = prod(d);
  KFlags fl;
  for (int i = 0; i < iters; i++) {
    c1 = 0; // CLR ac1
    fl.set(0, false, false);
    ar0 = m[a2++]; // LRRI $ar0, @$ar2
    { // LRRD $ac0.m, @$ar0: ac0.m = word (all of ac0 in 40-bit mode), $ar0 counts down
      const uint16_t v = k_rd(d, ar0);
      c0 = (c0 & ~0xFFFF0000ll) | (static_cast<int64_t>(v) << 16);
      if (sxm) c0 = s16(v) << 16;
      ar0 = static_cast<uint16_t>(ar0 - 1);
    }
    axh1 = k_rd(d, ar0);                    // LRR $ax1.h, @$ar0
    axh0 = k_rd(d, ar0);                    // LRRN $ax0.h, @$ar0
    ar0 = static_cast<uint16_t>(ar0 + ix0); //  ... then $ar0 += $ix0
    { // SUBR: ac0 -= ax0.h << 16
      const int64_t a = c0, b = s16(axh0) << 16;
      const int64_t r = w40(a - b);
      fl.set(r, static_cast<uint64_t>(a & kMask40) >= static_cast<uint64_t>(r & kMask40), add_overflow(a, -b, r));
      c0 = r;
    }
    axh0 = k_acc_m(c0, sxm); // MRR $ax0.h, $ac0.m
    axl1 = m[a2++];          // LRRI $ax1.l, @$ar2
    P = w40(k_mul(su, am, axl1, axh0, MUL_MIXED)); // MULX
    { // ADDPAXZ (ac1): ac1 = round(prod) + ax1 without its low word
      const int64_t p = P;
      const int64_t r = w40(round_hm(p) + (k_ax(axh1, axl1) & ~0xFFFFll));
      c1 = r;
      fl.set(r, static_cast<uint64_t>(p & kMask40) > static_cast<uint64_t>(r & kMask40), false);
    }
    m[a1++] = k_acc_m(c1, sxm); // CLR'S: store ac1.m, ac0 = 0
    c0 = 0;
    fl.set(0, false, false);
  }
  d->rf.ar[0] = ar0;
  d->rf.ar[1] = static_cast<uint16_t>(a1);
  d->rf.ar[2] = static_cast<uint16_t>(a2);
  d->rf.axh[0] = axh0;
  d->rf.axh[1] = axh1;
  d->rf.axl[1] = axl1;
  d->ac[0] = c0;
  d->ac[1] = c1;
  set_prod(d, P);
  k_finish_flags(d, fl);
  return true;
}


// ── Kernel: the noise generator's step (5 instructions an iteration) ────────────────────────────
// Two accumulators feed each other through an addition of ax1, a shift and a mask with ax0.h and an addition of ax0.l;
// two words an iteration go out through $ar3 (the accumulators' values from before the instructions that store them).
bool kernel_noise_step(GcDsp* d, int iters) {
  if (!k_range(d, 3, 2u * static_cast<uint32_t>(iters))) return false;
  uint16_t* const m = d->dram;
  const bool sxm = (d->rf.sr & SR_SXM) != 0;
  uint32_t a3 = d->rf.ar[3];
  const int64_t ax1 = k_ax(d->rf.axh[1], d->rf.axl[1]);
  const uint16_t axh0 = d->rf.axh[0];
  const int64_t axl0Part = s16(d->rf.axl[0]) << 16;
  int64_t c0 = d->ac[0], c1 = d->ac[1];
  KFlags fl;
  for (int i = 0; i < iters; i++) {
    m[a3++] = k_acc_m(c1, sxm); // ADDAX'S: store ac1.m, ac0 += ax1
    c0 = k_add(c0, ax1, fl);
    { // MRR $ac1.m, $ac0.l
      const uint16_t v = static_cast<uint16_t>(c0);
      c1 = (c1 & ~0xFFFF0000ll) | (static_cast<int64_t>(v) << 16);
      if (sxm) c1 = s16(v) << 16;
    }
    { // LSR ac1, 7: logical shift right of the 40 bits
      c1 = w40(static_cast<int64_t>((static_cast<uint64_t>(c1) & kMask40) >> 7));
      fl.set(c1, false, false);
    }
    m[a3++] = k_acc_m(c0, sxm); // ANDR'S: store ac0.m, ac1.m &= ax0.h (its flags are the next instruction's to overwrite)
    {
      const uint16_t v = static_cast<uint16_t>(static_cast<uint16_t>(c1 >> 16) & axh0);
      c1 = (c1 & ~0xFFFF0000ll) | (static_cast<int64_t>(v) << 16);
    }
    c1 = k_add(c1, axl0Part, fl); // ADDR: ac1 += ax0.l << 16
  }
  d->rf.ar[3] = static_cast<uint16_t>(a3);
  d->ac[0] = c0;
  d->ac[1] = c1;
  k_finish_flags(d, fl);
  return true;
}

std::atomic<uint64_t> gKernelUses[8]; // loops run by each kernel (tests: the kernel was really used)

struct KernelInfo {
  uint64_t hash;    // of the loop's words (loop_hash)
  int instructions; // in the loop's body
  const char* name;
  bool (*run)(GcDsp*, int iters);
};
const KernelInfo kKernels[] = {
    {0xB54E6837881C0285ull, 8, "stereo mixing step", kernel_stream_mix},
    {0xD24A92E364781079ull, 8, "sample copy with negation", kernel_copy_negate},
    {0x512F828E6267B127ull, 7, "filter multiply-accumulate rounds", kernel_mac_rounds},
    {0x4EF3B4EB3D1F3FB4ull, 11, "filter interpolation rounds", kernel_interpolate},
    {0x2F1227B4411F0092ull, 5, "noise generator step", kernel_noise_step},
};

// Called where run_body is: at the first instruction of a loop's body, nothing pending. Runs whole iterations (as many as
// the budget allows) of a loop it has a kernel for, and does the loop's bookkeeping as run_body does. 0 = not done,
// nothing changed.
DSP_INLINE int try_kernel(GcDsp* d, int budget) {
  const uint16_t start = d->pc, end = d->rf.st[2];
  if (start >= 0x1000 || end < start || end - start >= 40) return 0;
  GcDsp::KCache& c = d->kcache[start & 63];
  const int words = end - start + 1;
  if (c.start != start || c.end != end || std::memcmp(c.words, d->iram + start, static_cast<size_t>(words) * 2) != 0) {
    c.start = start;
    c.end = end;
    c.kernel = -1;
    c.len = 0;
    std::memcpy(c.words, d->iram + start, static_cast<size_t>(words) * 2);
    int instr = 0;
    unsigned a = start;
    while (a <= end && instr < 255) {
      const Decoded& dc = sDecoded[d->iram[a]];
      if (dc.fn == nullptr) break;
      a += dc.size;
      instr++;
    }
    if (a == static_cast<unsigned>(end) + 1) { // the words are whole instructions up to the loop's end
      c.len = static_cast<uint8_t>(instr);
      const uint64_t h = loop_hash(c.words, words);
      for (size_t k = 0; k < sizeof(kKernels) / sizeof(kKernels[0]); k++) {
        if (kKernels[k].hash == h && kKernels[k].instructions == instr) c.kernel = static_cast<int16_t>(k);
      }
      static const bool log = std::getenv("XD_DSP_KERNEL_LOG") != nullptr; // finding the checksum of a loop for a new kernel
      if (log) {
        std::fprintf(stderr, "[dsp native] loop %04X-%04X: %d instructions, checksum %016llX%s\n", start, end, instr,
                     static_cast<unsigned long long>(h), c.kernel >= 0 ? " (has a kernel)" : "");
      }
    }
  }
  if (c.kernel < 0) return 0;
  int iters = budget / c.len;
  if (iters > d->rf.st[3]) iters = d->rf.st[3];
  if (iters < 2) return 0;
  if (!kKernels[c.kernel].run(d, iters)) return 0;
  gKernelUses[c.kernel].fetch_add(1, std::memory_order_relaxed);
  const uint16_t left = static_cast<uint16_t>(d->rf.st[3] - iters);
  if (left > 0) {
    d->rf.st[3] = left;
    d->pc = d->rf.st[0];
  } else {
    d->pc = static_cast<uint16_t>(end + 1);
    d->rf.st[3] = 0;
    pop(d, 0);
    pop(d, 2);
    pop(d, 3);
  }
  return iters * c.len;
}
