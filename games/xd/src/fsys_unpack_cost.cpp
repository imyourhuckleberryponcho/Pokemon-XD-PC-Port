
#include <dolphin/types.h>
#include <game/pxdvs/GSAPI/GSfsys/fsys.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

extern "C" int64_t PortOS_BusyFor(int64_t ticks);
extern "C" void PortCpuCost_Accounted(int64_t ticks);
extern "C" void _fsysFreeBlock(FsysBlockSlot*, s32);
extern "C" u32 _fsysBlockByteSize;
extern "C" int32_t pokecoloFrameCounter;
extern "C" int64_t PortTime_Now(void);
extern "C" uint32_t VIGetRetraceCount(void);

namespace {

struct Rates {
  bool on = false;
  bool flat = false;
  double flatCycles = 0;
  double in = 0, out = 0, base = 0, copy = 0, copyBase = 0;
  bool log = false;
  bool exact = false;
  double scale = 1.0;
};

constexpr int64_t kFlagByte = 8;
constexpr int64_t kLiteral = 20;
constexpr int64_t kMtspr = 1;
inline int64_t match_cycles(uint32_t n) {
  if (n <= 8) return 37 + 11 * static_cast<int64_t>(n) + kMtspr;
  if (n <= 16) return 20 + 11 * static_cast<int64_t>(n) + 2 * kMtspr;
  return 11 * static_cast<int64_t>(n) - 2 + 2 * kMtspr;
}
constexpr int64_t kCallEntry = 8 + 10 + 17 + 3 + 4 + 4 + 12 + 6;
constexpr int64_t kBlockEnd = 7 + 3 + 6 + 43 + 5 + 17 + 4 + 2 + 6;
constexpr int64_t kFileStart = 23 + 1317 + 73 + 100;
constexpr int64_t kCallExit = 7 + 5 + 10;
inline int64_t flush_cycles(uint32_t addr, uint32_t n) {
  return 7 + 3 * static_cast<int64_t>((n + (addr & 31u) + 31u) >> 5) + 2 + 1;
}

struct LzState {
  uint32_t pos = 0;
  uint32_t limit = 0;
  uint32_t flags = 0;
  int state = 0;
  bool done = false;
  uint64_t literals = 0, matches = 0, out = 0;
};

int64_t lz_block_cycles(LzState& s, const uint8_t* src, uint32_t blockSize, bool first) {
  int64_t cycles = 0;
  uint32_t in = 0;
  if (first) {
    const uint32_t inSize = (uint32_t{src[8]} << 24) | (uint32_t{src[9]} << 16) | (uint32_t{src[10]} << 8) | src[11];
    s = LzState{};
    s.limit = inSize - 0x10;
    in = 0x10;
  }
  const auto read = [&](int pending, uint8_t& b) {
    s.pos++;
    b = in < blockSize ? src[in] : 0;
    in++;
    if (s.pos > s.limit) {
      s.done = true;
      return false;
    }
    if (in > blockSize) {
      s.state = pending;
      s.pos--;
      return false;
    }
    return true;
  };
  int state = s.state;
  s.state = 0;
  uint8_t b = 0;
  for (;;) {
    if (state == 0) {
      s.flags >>= 1;
      if ((s.flags & 0x100) == 0) state = 1;
    }
    if (state == 1) {
      if (!read(1, b)) break;
      s.flags = b | 0xFF00u;
      cycles += kFlagByte;
      state = 0;
    }
    if (state == 0) state = (s.flags & 1) != 0 ? 2 : 3;
    if (state == 2) {
      if (!read(2, b)) break;
      cycles += kLiteral;
      s.literals++, s.out++;
      state = 0;
      continue;
    }
    if (state == 3) {
      if (!read(3, b)) break;
      state = 4;
    }
    if (!read(4, b)) break;
    const uint32_t n = (b & 0xFu) + 3;
    cycles += match_cycles(n);
    s.matches++, s.out += n;
    state = 0;
  }
  return cycles;
}

const Rates& rates() {
  static const Rates r = [] {
    Rates v;
    const char* lg = std::getenv("XD_FSYS_UNPACK_LOG");
    v.log = lg != nullptr && std::strcmp(lg, "1") == 0;
    if (const char* c = std::getenv("XD_FSYS_UNPACK_COST"); c != nullptr && std::strncmp(c, "exact", 5) == 0) {
      v.on = v.exact = true;
      if (c[5] == ':') v.scale = std::strtod(c + 6, nullptr);
      if (!(v.scale > 0)) v.scale = 1.0;
      std::printf("[fsys unpack] cost model on: exact (the original's instructions per block), scale %.3f\n", v.scale);
      return v;
    } else if (c != nullptr && *c != '\0') {
      double a[5] = {0, 0, 0, 0, 0};
      const int n = std::sscanf(c, "%lf,%lf,%lf,%lf,%lf", &a[0], &a[1], &a[2], &a[3], &a[4]);
      if (n >= 1 && !(n == 1 && a[0] == 0)) {
        v.on = true;
        v.in = a[0], v.out = a[1], v.base = a[2], v.copy = a[3], v.copyBase = a[4];
      }
    } else if (const char* e = std::getenv("XD_FSYS_UNPACK_CYCLES"); e != nullptr && *e != '\0') {
      v.flatCycles = std::strtod(e, nullptr);
      v.on = v.flat = v.flatCycles > 0;
    }
    if (v.on) {
      std::printf("[fsys unpack] cost model on: %s in %.2f out %.2f base %.0f copy %.3f copy_base %.0f (cycles)\n",
                  v.flat ? "flat" : "per byte", v.in, v.out, v.flat ? v.flatCycles : v.base, v.copy, v.copyBase);
    }
    return v;
  }();
  return r;
}

struct Progress {
  u32 data;
  u32 done;
  LzState lz;
};
std::unordered_map<const FsysResHeader*, Progress> gProgress;

}

extern "C" void PortInput_FsysNote(uint32_t pass, uint32_t blocks);
extern "C" int PortInput_ReplayFsysBlocks(uint32_t pass);

namespace {
uint32_t gNotePass = UINT32_MAX;
uint32_t gNoteCount = 0;
uint32_t gReplayPass = UINT32_MAX;
int gReplayWant = -1;
uint32_t gReplayDone = 0;

void count_block() {
  const uint32_t pass = static_cast<uint32_t>(pokecoloFrameCounter);
  if (pass != gNotePass) {
    if (gNoteCount != 0) PortInput_FsysNote(gNotePass, gNoteCount);
    gNotePass = pass;
    gNoteCount = 0;
  }
  gNoteCount++;
}

void replay_wait(FsysBlockSlot* slot, bool last) {
  const uint32_t pass = static_cast<uint32_t>(pokecoloFrameCounter);
  if (pass != gReplayPass) {
    gReplayPass = pass;
    gReplayWant = PortInput_ReplayFsysBlocks(pass);
    gReplayDone = 0;
  }
  gReplayDone++;
  if (gReplayWant < 0 || last || gReplayDone >= static_cast<uint32_t>(gReplayWant)) return;
  constexpr int64_t kStep = 675675 / 16;
  for (int i = 0; i < 16 * 8 && slot->gc() == 0; i++) {
    if (PortOS_BusyFor(kStep) == 0) break;
  }
}
}

extern "C" void PortXD_fsysFreeBlock(FsysBlockSlot* slot, s32 flag) {
  const Rates& r = rates();
  count_block();
  if (!r.on && !r.log) {
    _fsysFreeBlock(slot, flag);
    const auto* hdr = reinterpret_cast<const FsysResHeader*>(reinterpret_cast<const u8*>(slot) -
                                                             offsetof(FsysResHeader, blockB));
    const u32 blockSize = _fsysBlockByteSize != 0 ? _fsysBlockByteSize : 0x8000;
    Progress& p = gProgress[hdr];
    if (p.data != hdr->data.gc()) p = Progress{hdr->data.gc(), 0};
    p.done += blockSize;
    const bool last = p.done >= hdr->size14;
    if (last) gProgress.erase(hdr);
    replay_wait(slot, last);
    return;
  }
  const auto* res = reinterpret_cast<const FsysResHeader*>(reinterpret_cast<const u8*>(slot) -
                                                           offsetof(FsysResHeader, blockB));
  const u32 flags = res->flags;
  const u32 stored = res->size14;
  const u32 unpacked = res->size8;
  const u32 data = res->data.gc();
  Progress& p = gProgress[res];
  if (p.data != data) {
    p = Progress{data, 0};
  }
  const u32 blockSize = _fsysBlockByteSize != 0 ? _fsysBlockByteSize : 0x8000;
  const u32 bytes = p.done < stored ? (stored - p.done < blockSize ? stored - p.done : blockSize) : 0;
  const bool first = p.done == 0;
  p.done += blockSize;
  const bool last = p.done >= stored;
  const bool packed = (flags & 0x80000000u) != 0 && data != 0;
  double cycles;
  if (r.exact) {
    const FsysBlock* block = *slot;
    int64_t c = 0;
    if (packed && block != nullptr && block->data != nullptr && !p.lz.done) {
      c = kCallEntry + kBlockEnd + (first ? kFileStart : 0);
      c += lz_block_cycles(p.lz, block->data, blockSize, first);
      if (p.lz.done || last) c += flush_cycles(data, unpacked) + kCallExit;
    } else if (!packed) {
      const u32 n = data != 0 ? ((bytes + 31u) & ~31u) : 0;
      c = (data != 0 ? 48 + 9 + 4 * static_cast<int64_t>(n) : 39) + 43;
      if (last) c += flush_cycles(data, stored);
    }
    cycles = static_cast<double>(c) * r.scale;
    if (last) gProgress.erase(res);
  } else if (last) {
    gProgress.erase(res);
  }
  if (r.exact) {
  } else if (r.flat) {
    cycles = r.flatCycles;
  } else if (packed) {
    const double ratio = stored != 0 ? static_cast<double>(unpacked) / stored : 1.0;
    cycles = r.base + r.in * bytes + r.out * bytes * ratio;
  } else {
    cycles = r.copyBase + (data != 0 ? r.copy * bytes : 0.0);
  }
  if (!r.on) {
    cycles = 0;
  }
  const int64_t ticks = static_cast<int64_t>(cycles / 12.0);
  const int64_t t0 = PortTime_Now();
  const uint32_t f0 = VIGetRetraceCount();
  const int64_t spent = ticks > 0 ? PortOS_BusyFor(ticks) : 0;
  PortCpuCost_Accounted(spent);
  _fsysFreeBlock(slot, flag);
  replay_wait(slot, last);
  if (r.log) {
    std::printf("[fsys unpack] t %lld field %u pass %d res %08x %s bytes %u (stored %u unpacked %u)%s cycles %.0f "
                "spent %lld\n",
                static_cast<long long>(t0), f0, pokecoloFrameCounter, static_cast<u32>(res->id),
                packed ? "packed" : (data != 0 ? "copy" : "skip"), bytes, stored, unpacked, last ? " last" : "", cycles,
                static_cast<long long>(spent));
  }
}
