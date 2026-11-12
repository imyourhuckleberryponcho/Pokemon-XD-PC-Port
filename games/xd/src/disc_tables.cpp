#include "disc_tables.h"
#include "dol_tables.h"

#include <sdk_port.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <port_win_compat.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#endif

extern "C" {
extern const XDDiscTableFile* const kXDDiscTableFiles[];
extern const int kXDDiscTableFileCount;
extern const int kXDDiscTablesCompiledIn;
}

namespace {

constexpr uint32_t kMem1Base = 0x80000000u;

std::vector<const XDDiscTable*> sTables;
std::vector<const XDDiscTable*> sTablesByPc;
std::unordered_map<uint32_t, void*> sFunctions;
std::unordered_map<const void*, uint32_t> sFunctionsBack;
struct Var {
  uint32_t gcAddress, gcSize;
  void* pc;
  uint32_t pcSize;
};
std::vector<Var> sVars;
uint32_t sImageEnd = 0, sBssStart = 0, sBssEnd = 0;

uint32_t step_gc(const XDDiscLeaf& l) { return l.kind >= XDDT_FUNC ? 4u : l.size; }
uint32_t step_pc(const XDDiscLeaf& l) { return l.kind >= XDDT_FUNC ? 8u : l.size; }

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

struct Failure {
  std::string text;
};

[[noreturn]] void fail(const XDDiscTable* t, uint32_t entry, uint32_t gcOffset, const char* what, uint32_t value) {
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s entry %u (main.dol %08X): %s (%08X)", t->name, entry,
                t->gcAddress + entry * t->gcStride + gcOffset, what, value);
  throw Failure{buf};
}

const XDDiscTable* table_at_gc(uint32_t gc) {
  auto it = std::upper_bound(sTables.begin(), sTables.end(), gc,
                             [](uint32_t a, const XDDiscTable* t) { return a < t->gcAddress; });
  if (it == sTables.begin()) return nullptr;
  const XDDiscTable* t = *(it - 1);
  return gc - t->gcAddress <= t->gcSize ? t : nullptr;
}

bool map_offset(const XDDiscTable* t, uint32_t off, uint32_t* pcOff) {
  if (off == t->gcSize) {
    *pcOff = t->count * t->pcStride;
    return true;
  }
  const uint32_t entry = off / t->gcStride, r = off % t->gcStride;
  if (entry >= t->count) return false;
  if (r == 0) {
    *pcOff = entry * t->pcStride;
    return true;
  }
  for (uint32_t i = 0; i < t->leafCount; i++) {
    const XDDiscLeaf& l = t->leaves[i];
    const uint32_t len = l.count * step_gc(l);
    if (r >= l.gcOffset && r < l.gcOffset + len) {
      const uint32_t k = (r - l.gcOffset) / step_gc(l), rem = (r - l.gcOffset) % step_gc(l);
      if (rem != 0 && !(l.kind == XDDT_RAW)) return false;
      *pcOff = entry * t->pcStride + l.pcOffset + k * step_pc(l) + rem;
      return true;
    }
  }
  return false;
}

bool map_offset_back(const XDDiscTable* t, uint32_t pcOff, uint32_t* gcOff) {
  if (pcOff == t->count * t->pcStride) {
    *gcOff = t->gcSize;
    return true;
  }
  const uint32_t entry = pcOff / t->pcStride, r = pcOff % t->pcStride;
  if (r == 0) {
    *gcOff = entry * t->gcStride;
    return true;
  }
  for (uint32_t i = 0; i < t->leafCount; i++) {
    const XDDiscLeaf& l = t->leaves[i];
    const uint32_t len = l.count * step_pc(l);
    if (r >= l.pcOffset && r < l.pcOffset + len) {
      const uint32_t k = (r - l.pcOffset) / step_pc(l), rem = (r - l.pcOffset) % step_pc(l);
      *gcOff = entry * t->gcStride + l.gcOffset + k * step_gc(l) + rem;
      return true;
    }
  }
  return false;
}

bool in_loaded_data(uint32_t gc, uint32_t size = 1) { return PortXD_DolIsLoaded(gc, size); }

void* resolve_data(const XDDiscTable* t, uint32_t entry, uint32_t gcOff, uint32_t v, uint8_t kind) {
  if (v == 0) return nullptr;
  if (v < kMem1Base) return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
  const XDDiscTable* target = table_at_gc(v);
  if (target != nullptr && v - target->gcAddress == target->gcSize && kind == XDDT_STR && in_loaded_data(v)) {
    target = nullptr;
  }
  if (target != nullptr) {
    uint32_t pcOff;
    if (!map_offset(target, v - target->gcAddress, &pcOff)) {
      fail(t, entry, gcOff, "points into the middle of a field of another table", v);
    }
    return static_cast<uint8_t*>(target->pc) + pcOff;
  }
  for (const Var& var : sVars) {
    if (v >= var.gcAddress && v < var.gcAddress + var.gcSize) {
      if (v != var.gcAddress && var.gcSize != var.pcSize) {
        fail(t, entry, gcOff, "points into a variable whose PC layout differs", v);
      }
      return static_cast<uint8_t*>(var.pc) + (v - var.gcAddress);
    }
  }
  if (kind == XDDT_STR && in_loaded_data(v)) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
  }
  fail(t, entry, gcOff, "points at something the port doesn't know", v);
}

uint32_t unresolve_data(const void* p) {
  if (p == nullptr) return 0;
  auto it = std::upper_bound(sTablesByPc.begin(), sTablesByPc.end(), p,
                             [](const void* a, const XDDiscTable* t) { return a < t->pc; });
  if (it != sTablesByPc.begin()) {
    const XDDiscTable* t = *(it - 1);
    const auto off = static_cast<size_t>(static_cast<const uint8_t*>(p) - static_cast<const uint8_t*>(t->pc));
    uint32_t gcOff;
    if (off <= t->pcSize && map_offset_back(t, static_cast<uint32_t>(off), &gcOff)) return t->gcAddress + gcOff;
  }
  for (const Var& var : sVars) {
    const auto* b = static_cast<const uint8_t*>(var.pc);
    if (p == b || (p > b && p < b + var.pcSize)) {
      return var.gcAddress + static_cast<uint32_t>(static_cast<const uint8_t*>(p) - b);
    }
  }
  const auto u = reinterpret_cast<uintptr_t>(p);
  return u < 0x100000000ull ? static_cast<uint32_t>(u) : 0xFFFFFFFFu;
}

void convert(const XDDiscTable* t, const uint8_t* gc, uint8_t* dest) {
  for (uint32_t e = 0; e < t->count; e++) {
    const uint8_t* ge = gc + e * t->gcStride;
    uint8_t* pe = dest + e * t->pcStride;
    for (uint32_t i = 0; i < t->leafCount; i++) {
      const XDDiscLeaf& l = t->leaves[i];
      for (uint32_t k = 0; k < l.count; k++) {
        const uint32_t go = l.gcOffset + k * step_gc(l);
        const uint8_t* s = ge + go;
        uint8_t* d = pe + l.pcOffset + k * step_pc(l);
        switch (l.kind) {
        case XDDT_NUM:
          for (uint32_t b = 0; b < l.size; b++) d[b] = s[l.size - 1 - b];
          break;
        case XDDT_RAW:
          std::memcpy(d, s, l.size);
          break;
        case XDDT_FUNC: {
          const uint32_t v = be32(s);
          void* f = nullptr;
          if (v != 0 && v < kMem1Base) {
            f = reinterpret_cast<void*>(static_cast<uintptr_t>(v));
          } else if (v != 0) {
            auto it = sFunctions.find(v);
            if (it == sFunctions.end()) fail(t, e, go, "a function address the port doesn't know", v);
            f = it->second;
          }
          std::memcpy(d, &f, sizeof f);
          break;
        }
        default: {
          void* p = resolve_data(t, e, go, be32(s), l.kind);
          std::memcpy(d, &p, sizeof p);
          break;
        }
        }
      }
    }
  }
}

void check_back(const XDDiscTable* t, const uint8_t* gc, const uint8_t* pcData) {
  for (uint32_t e = 0; e < t->count; e++) {
    for (uint32_t i = 0; i < t->leafCount; i++) {
      const XDDiscLeaf& l = t->leaves[i];
      for (uint32_t k = 0; k < l.count; k++) {
        const uint32_t go = l.gcOffset + k * step_gc(l);
        const uint8_t* s = gc + e * t->gcStride + go;
        const uint8_t* p = pcData + e * t->pcStride + l.pcOffset + k * step_pc(l);
        uint8_t back[8];
        uint32_t n = l.size;
        if (l.kind == XDDT_NUM) {
          for (uint32_t b = 0; b < l.size; b++) back[b] = p[l.size - 1 - b];
        } else if (l.kind == XDDT_RAW) {
          std::memcpy(back, p, l.size);
        } else {
          void* ptr;
          std::memcpy(&ptr, p, sizeof ptr);
          uint32_t v;
          if (l.kind == XDDT_FUNC) {
            auto it = ptr ? sFunctionsBack.find(ptr) : sFunctionsBack.end();
            v = ptr == nullptr ? 0 : (it == sFunctionsBack.end() ? unresolve_data(ptr) : it->second);
          } else {
            v = unresolve_data(ptr);
          }
          back[0] = (uint8_t)(v >> 24);
          back[1] = (uint8_t)(v >> 16);
          back[2] = (uint8_t)(v >> 8);
          back[3] = (uint8_t)v;
          n = 4;
        }
        if (std::memcmp(back, s, n) != 0) fail(t, e, go, "doesn't convert back to main.dol's bytes", be32(s));
      }
    }
  }
}

int compare_compiled(const XDDiscTable* t, uint8_t* conv) {
  const auto* comp = static_cast<const uint8_t*>(t->pc);
  int diffs = 0;
  for (uint32_t e = 0; e < t->count; e++) {
    for (uint32_t i = 0; i < t->leafCount; i++) {
      const XDDiscLeaf& l = t->leaves[i];
      if (l.kind != XDDT_STR) continue;
      for (uint32_t k = 0; k < l.count; k++) {
        const uint32_t o = e * t->pcStride + l.pcOffset + k * 8;
        const char *a, *b;
        std::memcpy(&a, conv + o, 8);
        std::memcpy(&b, comp + o, 8);
        if (a != b && a != nullptr && b != nullptr && std::strcmp(a, b) == 0) std::memcpy(conv + o, comp + o, 8);
      }
    }
  }
  for (uint32_t o = 0; o < t->pcSize; o++) {
    if (conv[o] != comp[o]) {
      if (diffs < 8) {
        std::fprintf(stderr, "[disc tables] VERIFY %s: PC byte 0x%X (entry %u + 0x%X): from main.dol %02X, compiled %02X\n",
                     t->name, o, o / t->pcStride, o % t->pcStride, conv[o], comp[o]);
      }
      diffs++;
    }
  }
  return diffs;
}

const char* known_difference(const char* name) {
  static const struct {
    const char* name;
    const char* why;
  } kKnownDifferences[] = {
      {"item_param", "positional initialisers on GC_BITS fields, reversed on PC (decomp bug)"},
      {"_matGSmatEnableEnvMapExt::tobjdesc", "plain numbers initialising gc_be32/gc_bef32 fill single bytes on PC "
                                             "(decomp bug)"},
  };
  for (const auto& k : kKnownDifferences) {
    if (std::strcmp(k.name, name) == 0) return k.why;
  }
  return nullptr;
}

#ifndef _WIN32
constexpr int PAGE_READWRITE = 0;
bool VirtualProtect(void*, size_t, DWORD, DWORD*) { return true; }
DWORD make_writable(void* p, size_t n) {
  const uintptr_t page = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
  const uintptr_t from = reinterpret_cast<uintptr_t>(p) & ~(page - 1);
  const uintptr_t to = (reinterpret_cast<uintptr_t>(p) + n + page - 1) & ~(page - 1);
#ifdef __APPLE__
  if (mprotect(reinterpret_cast<void*>(from), to - from, PROT_READ | PROT_WRITE) != 0 &&
      vm_protect(mach_task_self(), static_cast<vm_address_t>(from), static_cast<vm_size_t>(to - from), FALSE,
                 VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
    std::fprintf(stderr, "[disc tables] mac: could not make the table at %p writable\n", p);
  }
  return 0;
#endif
  mprotect(reinterpret_cast<void*>(from), to - from, PROT_READ | PROT_WRITE);
  return 0;
}
#else
DWORD make_writable(void* p, size_t n) {
  MEMORY_BASIC_INFORMATION mbi{};
  VirtualQuery(p, &mbi, sizeof mbi);
  const DWORD prot = mbi.Protect & 0xFF;
  if (prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY) return 0;
  DWORD old = 0;
  VirtualProtect(p, n, PAGE_READWRITE, &old);
  return old;
}
#endif

}

extern "C" int PortXD_FillDiscTables(char* error, unsigned errorSize) {
  uint8_t* mem1 = PortMem_MEM1();
  PortXD_DolImage(&sBssStart, &sBssEnd, &sImageEnd);
  if (mem1 == nullptr || sImageEnd == 0) {
    std::snprintf(error, errorSize, "The game's tables need main.dol loaded into GameCube memory");
    return 0;
  }
  sTables.clear();
  sFunctions.clear();
  sFunctionsBack.clear();
  sVars.clear();
  size_t pcBytes = 0;
  try {
    for (int f = 0; f < kXDDiscTableFileCount; f++) {
      const XDDiscTableFile* file = kXDDiscTableFiles[f];
      for (uint32_t i = 0; i < file->tableCount; i++) {
        const XDDiscTable* t = &file->tables[i];
        if (t->gcStride * t->count != t->gcSize || t->pcStride * t->count != t->pcSize || t->gcAddress == 0 ||
            !in_loaded_data(t->gcAddress, t->gcSize)) {
          char buf[256];
          std::snprintf(buf, sizeof buf, "%s (%s): not inside main.dol's loaded data (%08X, 0x%X bytes)", t->name,
                        file->source, t->gcAddress, t->gcSize);
          throw Failure{buf};
        }
        sTables.push_back(t);
        pcBytes += t->pcSize;
      }
      for (uint32_t i = 0; i < file->refCount; i++) {
        const XDDiscRef& r = file->refs[i];
        if (r.gcSize == 0) {
          auto [it, added] = sFunctions.emplace(r.gcAddress, r.pc);
          if (!added && it->second != r.pc) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "two PC functions for GameCube function %08X (%s)", r.gcAddress, file->source);
            throw Failure{buf};
          }
          sFunctionsBack.emplace(r.pc, r.gcAddress);
        } else {
          sVars.push_back({r.gcAddress, r.gcSize, r.pc, r.pcSize});
        }
      }
    }
    std::sort(sTables.begin(), sTables.end(),
              [](const XDDiscTable* a, const XDDiscTable* b) { return a->gcAddress < b->gcAddress; });
    for (size_t i = 1; i < sTables.size(); i++) {
      if (sTables[i]->gcAddress < sTables[i - 1]->gcAddress + sTables[i - 1]->gcSize) {
        throw Failure{std::string(sTables[i - 1]->name) + " and " + sTables[i]->name + " overlap in main.dol"};
      }
    }
    sTablesByPc = sTables;
    std::sort(sTablesByPc.begin(), sTablesByPc.end(),
              [](const XDDiscTable* a, const XDDiscTable* b) { return a->pc < b->pc; });

    int verifyDiffs = 0, verifyTables = 0;
    std::string firstFailure;
    int failures = 0;
    std::unordered_map<const XDDiscTable*, std::vector<uint8_t>> scratch;
    auto dest = [&](const XDDiscTable* t) {
      return kXDDiscTablesCompiledIn ? scratch[t].data() : static_cast<uint8_t*>(t->pc);
    };
    for (const XDDiscTable* t : sTables) {
      const uint8_t* gc = mem1 + (t->gcAddress - kMem1Base);
      try {
        DWORD old = 0;
        if (kXDDiscTablesCompiledIn) {
          scratch[t].assign(t->pcSize, 0);
        } else {
          old = make_writable(t->pc, t->pcSize);
          std::memset(t->pc, 0, t->pcSize);
        }
        convert(t, gc, dest(t));
        check_back(t, gc, dest(t));
        if (old != 0) {
          DWORD ignored;
          VirtualProtect(t->pc, t->pcSize, old, &ignored);
        }
      } catch (const Failure& f) {
        std::fprintf(stderr, "[disc tables] PROBLEM %s\n", f.text.c_str());
        if (failures++ == 0) firstFailure = f.text;
      }
    }
    if (failures) {
      throw Failure{firstFailure + (failures > 1 ? " (and " + std::to_string(failures - 1) + " more)" : "")};
    }
    size_t fixups = 0;
    for (int f = 0; f < kXDDiscTableFileCount; f++) {
      const XDDiscTableFile* file = kXDDiscTableFiles[f];
      for (uint32_t i = 0; i < file->fixupCount; i++) {
        const XDDiscFixup& x = file->fixups[i];
        const XDDiscTable* t = &file->tables[x.table];
        if (x.pcOffset + x.size > t->pcSize) throw Failure{std::string(t->name) + ": a start-up field outside the table"};
        const DWORD old = kXDDiscTablesCompiledIn ? 0 : make_writable(t->pc, t->pcSize);
        std::memcpy(dest(t) + x.pcOffset, x.source, x.size);
        if (old != 0) {
          DWORD ignored;
          VirtualProtect(t->pc, t->pcSize, old, &ignored);
        }
        fixups++;
      }
    }
    if (kXDDiscTablesCompiledIn) {
      for (const XDDiscTable* t : sTables) {
        const int d = compare_compiled(t, dest(t));
        if (const char* why = known_difference(t->name)) {
          std::fprintf(stderr, "[disc tables] VERIFY %s: %s (known: %s)\n", t->name,
                       d ? "differs" : "NO LONGER DIFFERS, remove it from kKnownDifferences", why);
        } else if (d) {
          verifyTables++;
          verifyDiffs += d;
        }
      }
    }
    if (fixups) std::fprintf(stderr, "[disc tables] %zu fields filled at start-up as CodeWarrior's __sinit does\n", fixups);
    std::fprintf(stderr, "[disc tables] %zu tables (%zu bytes) from main.dol, %zu functions%s\n", sTables.size(), pcBytes,
                 sFunctions.size(), kXDDiscTablesCompiledIn ? " (verify build: compared with the compiled tables)" : "");
    if (kXDDiscTablesCompiledIn) {
      std::fprintf(stderr, "[disc tables] VERIFY %s: %d tables differ, %d bytes\n", verifyDiffs ? "FAILED" : "OK",
                   verifyTables, verifyDiffs);
      if (verifyDiffs) {
        std::snprintf(error, errorSize, "%d of the game's tables differ from the compiled ones", verifyTables);
        return 0;
      }
    }
  } catch (const Failure& f) {
    std::snprintf(error, errorSize, "The game's table %s", f.text.c_str());
    return 0;
  }
  return 1;
}

extern "C" int PortXD_DiscTableCount(void) { return static_cast<int>(sTables.size()); }
extern "C" const XDDiscTable* PortXD_DiscTable(int index) { return sTables[index]; }
extern "C" const XDDiscTable* PortXD_DiscTableAt(const void* pc) {
  for (const XDDiscTable* t : sTables) {
    const auto* b = static_cast<const uint8_t*>(t->pc);
    if (pc >= b && pc < b + t->pcSize) return t;
  }
  return nullptr;
}
