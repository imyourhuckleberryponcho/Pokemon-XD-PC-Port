#include <dolphin/gx.h>
#include <dolphin/os.h>

#include <cstdint>

extern "C" {
void PortCpuCost_Count(unsigned counter, uint64_t amount);

enum : unsigned {
  kVertices = 0, kDlBytes = 1, kTexLoads = 2, kCacheBytes = 4, kBegins = 7,
  kSdkInstr = 23, kSdkDirty = 25, kMemInstr = 26, kSdkCalls = 27
};

unsigned gPortSdkDirty = 0;
unsigned gPortSdkVatDirty = 0;
int gPortSdkLastBp = 0;

static void sdk_draw(unsigned own) {
  uint64_t n = own;
  if (gPortSdkDirty != 0) {
    const unsigned d = gPortSdkDirty;
    uint64_t s = 26;
    if (d & 1u) {
      s += 1 + 52;
      gPortSdkLastBp = 1;
    }
    if (d & 2u) {
      s += 1 + 1;
    }
    if (d & 4u) {
      s += 1 + 9;
      gPortSdkLastBp = 1;
    }
    if (d & 8u) {
      s += 1 + 42;
      gPortSdkLastBp = 0;
    }
    if (d & 0x10u) {
      unsigned formats = 0;
      for (unsigned m = gPortSdkVatDirty & 0xFFu; m != 0; m >>= 1) {
        formats += m & 1u;
      }
      s += 1 + 15 + 24ull * formats;
      gPortSdkLastBp = 0;
    }
    if (d & 0x18u) {
      s += 4;
    }
    gPortSdkDirty = 0;
    gPortSdkVatDirty = 0;
    PortCpuCost_Count(kSdkDirty, s);
  }
  if (gPortSdkLastBp != 0) {
    n += 18;
    gPortSdkLastBp = 0;
  }
  PortCpuCost_Count(kSdkInstr, n);
  PortCpuCost_Count(kSdkCalls, 1);
}

static void sdk_cache(uintptr_t addr, uint32_t n, unsigned own = 9) {
  PortCpuCost_Count(kMemInstr, n == 0 ? 2 : own + 7ull * ((n + (addr & 31u) + 31u) / 32u));
}

void PortXD_GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
  PortCpuCost_Count(kBegins, 1);
  PortCpuCost_Count(kVertices, nverts == 0xFFFF ? 0 : nverts);
  sdk_draw(30);
  GXBegin(type, vtxfmt, nverts);
}

void PortXD_GXCallDisplayList(const void* list, u32 nbytes) {
  PortCpuCost_Count(kDlBytes, nbytes);
  sdk_draw(26);
  GXCallDisplayList(list, nbytes);
}

void PortXD_GXLoadTexObj(GXTexObj* obj, GXTexMapID id) {
  PortCpuCost_Count(kTexLoads, 1);
  const u32 fmt = static_cast<u32>(GXGetTexObjFmt(obj));
  PortCpuCost_Count(kSdkInstr, fmt >= 8 && fmt <= 10 ? 167 : 148);
  PortCpuCost_Count(kSdkCalls, 1);
  gPortSdkDirty |= 1u;
  gPortSdkLastBp = 1;
  GXLoadTexObj(obj, id);
}

void PortXD_DCFlushRange(void* addr, u32 n) {
  PortCpuCost_Count(kCacheBytes, n);
  sdk_cache(reinterpret_cast<uintptr_t>(addr), n, 11);
  DCFlushRange(addr, n);
}
void PortXD_DCStoreRange(void* addr, u32 n) {
  PortCpuCost_Count(kCacheBytes, n);
  sdk_cache(reinterpret_cast<uintptr_t>(addr), n, 11);
  DCStoreRange(addr, n);
}
void PortXD_DCFlushRangeNoSync(void* addr, u32 n) {
  PortCpuCost_Count(kCacheBytes, n);
  sdk_cache(reinterpret_cast<uintptr_t>(addr), n);
  DCFlushRangeNoSync(addr, n);
}
void PortXD_DCStoreRangeNoSync(void* addr, u32 n) {
  PortCpuCost_Count(kCacheBytes, n);
  sdk_cache(reinterpret_cast<uintptr_t>(addr), n);
  DCStoreRangeNoSync(addr, n);
}
void PortXD_DCInvalidateRange(void* addr, u32 n) {
  PortCpuCost_Count(kCacheBytes, n);
  sdk_cache(reinterpret_cast<uintptr_t>(addr), n);
  DCInvalidateRange(addr, n);
}
}
