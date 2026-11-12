#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dbghelp.h>

extern "C" {
void PortCpuCost_Count(unsigned counter, uint64_t amount);
int PortFuncCost_Row(const char* name, char* kind, uint32_t* a, uint32_t* b);
void PortSdkCost_SkipConcat(void);
extern unsigned gPortSdkDirty, gPortSdkVatDirty;
extern int gPortSdkLastBp;
}

namespace {

enum : unsigned { kSdkInstr = 23, kSdkSpan = 24, kMemInstr = 26, kSdkCalls = 27 };

enum Side : int8_t { kNone = 0, kBp = 1, kXf = 2 };

const struct Known {
  const char* name;
  uint32_t instr;
  Side side;
} kKnown[] = {
    {"GXSetTevColorIn", 17, kBp},   {"GXSetTevAlphaIn", 17, kBp},     {"GXSetTevColorOp", 23, kBp},
    {"GXSetTevAlphaOp", 23, kBp},   {"GXSetTevColor", 24, kBp},       {"GXSetTevKColor", 25, kBp},
    {"GXSetArray", 16, kXf},        {"GXLoadPosMtxImm", 20, kXf},     {"GXLoadNrmMtxImm", 20, kXf},
    {"GXSetCurrentMtx", 33, kXf},   {"GXSetAlphaCompare", 17, kBp},   {"GXSetZMode", 13, kBp},
    {"GXSetBlendMode", 21, kBp},    {"GXSetTevSwapMode", 18, kBp},    {"GXSetTevKColorSel", 20, kBp},
    {"GXSetTevKAlphaSel", 20, kBp}, {"GXSetTevDirect", 45, kBp},      {"GXInvalidateVtxCache", 4, kNone},
    {"GXInitTexObjLOD", 77, kNone}, {"GXInitTexObjWrapMode", 7, kNone}, {"GXInitTlutObj", 14, kNone},
    {"GXSetTevOp", 80, kBp},
};
const struct {
  const char* name;
  Side side;
} kSides[] = {
    {"GXSetTevColorS10", kBp},  {"GXSetTevSwapModeTable", kBp}, {"GXSetZCompLoc", kBp},   {"GXSetZTexture", kBp},
    {"GXSetFog", kBp},          {"GXSetScissor", kBp},          {"GXSetColorUpdate", kBp}, {"GXSetAlphaUpdate", kBp},
    {"GXSetDither", kBp},       {"GXSetDstAlpha", kBp},         {"GXSetPixelFmt", kBp},    {"GXSetFieldMask", kBp},
    {"GXSetLineWidth", kBp},    {"GXSetPointSize", kBp},        {"GXSetIndTexCoordScale", kBp}, {"GXLoadTlut", kBp},
    {"GXSetTexCopySrc", kBp},   {"GXSetTexCopyDst", kBp},       {"GXSetViewport", kXf},    {"GXSetProjection", kXf},
    {"GXLoadLightObjImm", kXf}, {"GXSetClipMode", kXf},         {"GXSetCoPlanar", kNone},
};

struct Cost {
  bool resolved = false;
  uint32_t instr = 0, span = 0;
  Side side = kNone;
};

int sOn = -1;
thread_local unsigned tSkipConcat = 0;

bool on() {
  if (sOn < 0) {
    const char* e = std::getenv("XD_FUNC_COST");
    sOn = e != nullptr && e[0] != '\0' && e[0] != '0' ? 1 : 0;
    if (const char* s = std::getenv("XD_SDK_COST"); s != nullptr && s[0] == '0') {
      sOn = 0;
    }
  }
  return sOn == 1;
}

void resolve(const char* name, Cost& c) {
  c.resolved = true;
  for (const auto& s : kSides) {
    if (std::strcmp(s.name, name) == 0) {
      c.side = s.side;
    }
  }
  bool known = false;
  for (const auto& k : kKnown) {
    if (std::strcmp(k.name, name) == 0) {
      c.instr = k.instr;
      c.side = k.side;
      known = true;
    }
  }
  if (known && std::strcmp(name, "GXSetTevOp") != 0) {
    return;
  }
  char kind = 0;
  uint32_t a = 0, b = 0;
  if (PortFuncCost_Row(name, &kind, &a, &b) != 0) {
    if (kind == 'E') {
      c.instr += a;
    } else if (kind == 'M') {
      c.instr += a;
      c.span = b - a;
    } else if (kind == 'L') {
      c.instr += a;
      c.span = b;
    }
  }
}

inline void charge(const char* name, Cost& c) {
  if (!on()) {
    return;
  }
  if (!c.resolved) {
    resolve(name, c);
  }
  PortCpuCost_Count(kSdkInstr, c.instr);
  if (c.span != 0) {
    PortCpuCost_Count(kSdkSpan, c.span);
  }
  PortCpuCost_Count(kSdkCalls, 1);
  if (c.side != kNone) {
    gPortSdkLastBp = c.side == kBp ? 1 : 0;
  }
}

inline void fixed(uint64_t instr, Side side, unsigned dirty = 0) {
  if (!on()) {
    return;
  }
  PortCpuCost_Count(kSdkInstr, instr);
  PortCpuCost_Count(kSdkCalls, 1);
  if (side != kNone) {
    gPortSdkLastBp = side == kBp ? 1 : 0;
  }
  gPortSdkDirty |= dirty;
}

unsigned list_len_desc(const GXVtxDescList* l) {
  unsigned n = 0;
  for (; l != nullptr && n < 32 && l[n].attr != GX_VA_NULL; n++) {
  }
  return n;
}
unsigned list_len_fmt(const GXVtxAttrFmtList* l) {
  unsigned n = 0;
  for (; l != nullptr && n < 32 && l[n].attr != GX_VA_NULL; n++) {
  }
  return n;
}

}

extern "C" {

void PortSdkCost_SkipConcat(void) { tSkipConcat++; }

#define W(ret, name, params, args)                                                                                    \
  ret PortSdk_##name params {                                                                                         \
    static Cost sdkCost_;                                                                                             \
    charge(#name, sdkCost_);                                                                                          \
    return name args;                                                                                                 \
  }
#include "sdk_cost_wrappers.inc"
#undef W

void PortSdk_PSMTXConcat(const Mtx a, const Mtx b, Mtx ab) {
  if (tSkipConcat != 0) {
    tSkipConcat--;
  } else {
    fixed(51, kNone);
  }
  PSMTXConcat(a, b, ab);
}

void PortSdk_GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {
  const unsigned m = static_cast<unsigned>(map);
  fixed(57 + (static_cast<unsigned>(stage) & 1u) + (static_cast<unsigned>(color) != 0xFFu ? 3 : 0) +
            (m != 0xFFu ? ((m & 0x100u) != 0 ? 1 : 2) : 0) + (static_cast<unsigned>(coord) == 0xFFu ? 2 : 0),
        kBp, 1u);
  GXSetTevOrder(stage, coord, map, color);
}

void PortSdk_GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask,
                           GXDiffuseFn diff_fn, GXAttnFn attn_fn) {
  const unsigned c = static_cast<unsigned>(chan);
  fixed((c <= 3 ? 34 : c == 4 ? 37 : 38) + (static_cast<unsigned>(attn_fn) == 0 ? 1 : 0), kXf);
  GXSetChanCtrl(chan, enable, amb_src, mat_src, light_mask, diff_fn, attn_fn);
}

static unsigned chan_color(GXChannelID chan) {
  static const uint8_t k[6] = {28, 24, 25, 21, 23, 21};
  return static_cast<unsigned>(chan) < 6 ? k[static_cast<unsigned>(chan)] : 21;
}
void PortSdk_GXSetChanAmbColor(GXChannelID chan, GXColor color) {
  fixed(chan_color(chan), kXf);
  GXSetChanAmbColor(chan, color);
}
void PortSdk_GXSetChanMatColor(GXChannelID chan, GXColor color) {
  fixed(chan_color(chan), kXf);
  GXSetChanMatColor(chan, color);
}

void PortSdk_GXSetVtxDesc(GXAttr attr, GXAttrType type) {
  const unsigned a = static_cast<unsigned>(attr);
  fixed(a == 10 ? 31 : a == 25 ? 32 : a == 20 ? 28 : 29, kNone, 8u);
  GXSetVtxDesc(attr, type);
}
void PortSdk_GXSetVtxDescv(GXVtxDescList* list) {
  static Cost c;
  if (on()) {
    if (!c.resolved) {
      resolve("GXSetVtxDescv", c);
    }
    PortCpuCost_Count(kSdkInstr, c.instr + static_cast<uint64_t>(c.span) * list_len_desc(list));
    PortCpuCost_Count(kSdkCalls, 1);
    gPortSdkDirty |= 8u;
  }
  GXSetVtxDescv(list);
}
void PortSdk_GXClearVtxDesc(void) {
  fixed(14, kNone, 8u);
  GXClearVtxDesc();
}
void PortSdk_GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
  const unsigned a = static_cast<unsigned>(attr);
  fixed(a == 10 || a == 25 ? 40 : a == 11 || a == 12 ? 34 : a == 20 ? 36 : 37, kNone, 0x10u);
  if (on()) {
    gPortSdkVatDirty |= 1u << (static_cast<unsigned>(vtxfmt) & 7u);
  }
  GXSetVtxAttrFmt(vtxfmt, attr, cnt, type, frac);
}
void PortSdk_GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list) {
  static Cost c;
  if (on()) {
    if (!c.resolved) {
      resolve("GXSetVtxAttrFmtv", c);
    }
    PortCpuCost_Count(kSdkInstr, c.instr + static_cast<uint64_t>(c.span) * list_len_fmt(list));
    PortCpuCost_Count(kSdkCalls, 1);
    gPortSdkDirty |= 0x10u;
    gPortSdkVatDirty |= 1u << (static_cast<unsigned>(vtxfmt) & 7u);
  }
  GXSetVtxAttrFmtv(vtxfmt, list);
}

void PortSdk_GXLoadTexMtxImm(const void* mtx, u32 id, GXTexMtxType type) {
  fixed((type == GX_MTX3x4 ? 29 : 26) + (id >= 64 ? 3 : 0), kXf);
  GXLoadTexMtxImm(mtx, id, type);
}

void PortSdk_GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx,
                               GXBool normalize, u32 postmtx) {
  const unsigned f = static_cast<unsigned>(func);
  unsigned n = f == 0 ? 82 : f == 1 ? 80 : f >= 2 && f <= 9 ? 88 : 84;
  const unsigned s = static_cast<unsigned>(src_param);
  if (s >= 4 && s <= 11) {
    n -= 2;
  }
  if (static_cast<unsigned>(dst_coord) == 7) {
    n -= 8;
  }
  fixed(n, kXf);
  GXSetTexCoordGen2(dst_coord, func, src_param, mtx, normalize, postmtx);
}

void PortSdk_GXSetNumChans(u8 n) {
  fixed(15, kXf, 4u);
  GXSetNumChans(n);
}
void PortSdk_GXSetNumTexGens(u8 n) {
  fixed(15, kXf, 4u);
  GXSetNumTexGens(n);
}
void PortSdk_GXSetNumTevStages(u8 n) {
  fixed(10, kNone, 4u);
  GXSetNumTevStages(n);
}
void PortSdk_GXSetNumIndStages(u8 n) {
  fixed(9, kNone, 6u);
  GXSetNumIndStages(n);
}
void PortSdk_GXSetCullMode(GXCullMode mode) {
  fixed(10, kNone, 4u);
  GXSetCullMode(mode);
}

void PortSdk_GXInitTexObj(GXTexObj* obj, const void* data, u16 width, u16 height, GXTexFmt format,
                          GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
  fixed(mipmap ? 153 : 125, kNone);
  GXInitTexObj(obj, data, width, height, format, wrapS, wrapT, mipmap);
}
void PortSdk_GXInitTexObjCI(GXTexObj* obj, const void* data, u16 width, u16 height, GXCITexFmt format,
                            GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlut) {
  fixed(mipmap ? 171 : 143, kNone);
  GXInitTexObjCI(obj, data, width, height, format, wrapS, wrapT, mipmap, tlut);
}

struct MemUse {
  uint64_t calls = 0, bytes = 0, instr = 0;
};
static std::map<uintptr_t, MemUse>* sMemUse[2] = {nullptr, nullptr};
static const char* sMemLogPath = nullptr;
static void mem_log_write() {
  FILE* f = std::fopen(sMemLogPath, "w");
  if (f == nullptr) {
    return;
  }
  for (int k = 0; k < 2; k++) {
    std::map<std::string, MemUse> byName;
    for (const auto& [addr, u] : *sMemUse[k]) {
      char buf[sizeof(SYMBOL_INFO) + 256] = {};
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      DWORD64 disp = 0;
      MemUse& m = byName[SymFromAddr(GetCurrentProcess(), addr, &disp, sym) ? sym->Name : "?"];
      m.calls += u.calls;
      m.bytes += u.bytes;
      m.instr += u.instr;
    }
    std::multimap<uint64_t, std::string> order;
    for (const auto& [name, u] : byName) {
      char line[512];
      std::snprintf(line, sizeof(line), "%s\t%s\t%llu\t%llu\t%llu", k == 0 ? "memcpy" : "memset", name.c_str(),
                    static_cast<unsigned long long>(u.calls), static_cast<unsigned long long>(u.bytes),
                    static_cast<unsigned long long>(u.instr));
      order.emplace(~u.instr, line);
    }
    for (const auto& [key, line] : order) {
      std::fprintf(f, "%s\n", line.c_str());
    }
  }
  std::fclose(f);
}
static void mem_log(int kind, uintptr_t caller, uint64_t bytes, uint64_t instr) {
  static int state = -1;
  if (state < 0) {
    sMemLogPath = std::getenv("XD_SDK_MEM_LOG");
    state = sMemLogPath != nullptr && sMemLogPath[0] != '\0' ? 1 : 0;
    if (state == 1) {
      sMemUse[0] = new std::map<uintptr_t, MemUse>;
      sMemUse[1] = new std::map<uintptr_t, MemUse>;
      std::atexit(mem_log_write);
    }
  }
  if (state == 1) {
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    MemUse& u = (*sMemUse[kind])[caller];
    u.calls++;
    u.bytes += bytes;
    u.instr += instr;
    static uint64_t total = 0;
    if (++total % 50000 == 0) {
      mem_log_write();
    }
    ReleaseSRWLockExclusive(&lock);
  }
}

void* PortSdk_memcpy(void* dst, const void* src, size_t n) {
  if (on()) {
    PortCpuCost_Count(kMemInstr, 9 + 4ull * n);
    PortCpuCost_Count(kSdkCalls, 1);
    mem_log(0, reinterpret_cast<uintptr_t>(_ReturnAddress()), n, 9 + 4ull * n);
  }
  return std::memcpy(dst, src, n);
}

void* PortSdk_memset(void* dst, int value, size_t n) {
  if (on()) {
    uint64_t c;
    if (n == 0) {
      c = 19;
    } else if (n < 32) {
      c = 20 + 3ull * n;
    } else {
      c = 31 + 10ull * (n / 32) + 3ull * ((n % 32) / 4) + ((n % 4) != 0 ? 4 : 0);
      if ((reinterpret_cast<uintptr_t>(dst) & 3u) != 0) {
        c += 25;
      }
      if ((value & 0xFF) != 0) {
        c += 6;
      }
    }
    c += 1;
    PortCpuCost_Count(kMemInstr, c);
    PortCpuCost_Count(kSdkCalls, 1);
    mem_log(1, reinterpret_cast<uintptr_t>(_ReturnAddress()), n, c);
  }
  return std::memset(dst, value, n);
}

}
