#include "common_rel.h"

#include <gs_formats.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern "C" {
void* PortXD_GameAddressIfKnown(uint32_t gcAddress, uint32_t* size);
void* PortMem_LowAlloc(size_t size, size_t align);
}

extern "C" {
#define RELGLOBAL(addr, name) extern void* name;
#include "relglobal_vars.inc"
#undef RELGLOBAL
}

namespace {

struct Variable {
  const char* name;
  void** where;
};
const Variable kVariables[] = {
#define RELGLOBAL(addr, name) {#name, &name},
#include "relglobal_vars.inc"
#undef RELGLOBAL
};

constexpr uint32_t kCommonRelId = 88;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

[[noreturn]] void stop(const char* message) {
  std::fprintf(stderr, "[games/xd] %s\n", message);
  std::fflush(stderr);
  std::abort();
}

struct DataModule {
  const void* module = nullptr;
  std::vector<std::pair<void*, uint32_t>> stored;
};
std::vector<DataModule> sDataModules;

bool dead_store_target(uint32_t gcAddress) { return gcAddress >= 0x804B3974u && gcAddress < 0x804E1000u; }

void put_be32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

}

bool PortXD_LinkDataModule(const uint8_t* file, size_t size, std::vector<std::pair<void*, uint32_t>>* out,
                           std::string& problem) {
  RelModule rel{};
  if (!Rel_Parse(file, size, &rel)) {
    problem = "not a REL module";
    return false;
  }
  struct Freer {
    RelModule* r;
    ~Freer() { Rel_Free(r); }
  } freer{&rel};
  char msg[200];
  std::vector<uint8_t*> copies(rel.numSections, nullptr);
  std::vector<uint8_t> scratch;
  for (uint32_t s = 0; s < rel.numSections; s++) {
    const RelSection& sec = rel.sections[s];
    if (sec.code || sec.size == 0) continue;
    if (out == nullptr) {
      if (scratch.size() < sec.size) scratch.resize(sec.size);
      copies[s] = scratch.data();
      continue;
    }
    copies[s] = static_cast<uint8_t*>(PortMem_LowAlloc(sec.size, 32));
    if (sec.data != nullptr) std::memcpy(copies[s], sec.data, sec.size);
  }
  for (uint32_t r = 0; r < rel.numRelocs; r++) {
    const RelReloc& rr = rel.relocs[r];
    if (rr.section >= rel.numSections || copies[rr.section] == nullptr) continue;
    if (rr.type != REL_R_ADDR32) {
      std::snprintf(msg, sizeof(msg), "relocation type %u in data (section %u +0x%X)", rr.type, rr.section, rr.offset);
      problem = msg;
      return false;
    }
    uintptr_t target = 0;
    if (rr.module == 0) {
      uint32_t known = 0;
      target = reinterpret_cast<uintptr_t>(PortXD_GameAddressIfKnown(rr.addend, &known));
      if (target == 0 && rr.addend != 0) {
        std::snprintf(msg, sizeof(msg), "its data refers to main-program address %08X, which the port doesn't map",
                      rr.addend);
        problem = msg;
        return false;
      }
    } else if (rr.module == rel.id) {
      if (rr.targetSection >= rel.numSections || copies[rr.targetSection] == nullptr) {
        std::snprintf(msg, sizeof(msg), "its data refers to its own code (section %u +0x%X)", rr.targetSection,
                      rr.addend);
        problem = msg;
        return false;
      }
      target = reinterpret_cast<uintptr_t>(copies[rr.targetSection] + rr.addend);
    } else {
      std::snprintf(msg, sizeof(msg), "its data refers to another module (%u)", rr.module);
      problem = msg;
      return false;
    }
    if (target > 0xFFFFFFFFu) {
      problem = "an address in its data doesn't fit in 32 bits";
      return false;
    }
    if (out != nullptr) put_be32(copies[rr.section] + rr.offset, static_cast<uint32_t>(target));
  }
  RelPrologStore stores[512];
  const int n = Rel_ReadPrologStores(&rel, stores, 512);
  if (n < 0) {
    problem = "its _prolog does more than store addresses";
    return false;
  }
  for (int k = 0; k < n; k++) {
    const RelPrologStore& st = stores[k];
    uint32_t room = 0;
    void* var = PortXD_GameAddressIfKnown(st.dolAddress, &room);
    if (var == nullptr && dead_store_target(st.dolAddress)) {
      var = reinterpret_cast<void*>(static_cast<uintptr_t>(st.dolAddress));
      room = 4;
    }
    if (var == nullptr || room < 4) {
      std::snprintf(msg, sizeof(msg), "its _prolog stores to main-program address %08X, which the port doesn't map",
                    st.dolAddress);
      problem = msg;
      return false;
    }
    uintptr_t value = st.constant;
    if (st.isRelAddress) {
      if (st.section >= rel.numSections || copies[st.section] == nullptr) {
        problem = "its _prolog stores the address of its own code";
        return false;
      }
      value = reinterpret_cast<uintptr_t>(copies[st.section] + st.offset);
    }
    if (out != nullptr) {
      bool pointer = false;
      for (const Variable& v : kVariables) pointer = pointer || var == static_cast<void*>(v.where);
      if (pointer) {
        *static_cast<void**>(var) = reinterpret_cast<void*>(value);
        out->emplace_back(var, static_cast<uint32_t>(sizeof(void*)));
      } else {
        put_be32(static_cast<uint8_t*>(var), static_cast<uint32_t>(value));
        out->emplace_back(var, 4);
      }
    }
  }
  return true;
}

namespace {

int link_module(void* module, void* bss) {
  (void)bss;
#if XD_RELGLOBAL_IS_U32
  stop("relglobal.c still declares the table variables as u32: needs DECOMP_FIXES.md section 14 before "
       "common_rel can be linked on PC.");
#endif
  const auto* file = static_cast<const uint8_t*>(module);
  const uint32_t id = be32(file);
  if (id != kCommonRelId) {
    DataModule m;
    m.module = module;
    std::string problem;
    if (!PortXD_LinkDataModule(file, Rel_FileExtent(file), &m.stored, problem)) {
      char message[300];
      std::snprintf(message, sizeof(message), "The game linked REL module %u, which the port can't link yet: %s.", id,
                    problem.c_str());
      stop(message);
    }
    bool fillsTables = false;
    for (const auto& [var, bytes] : m.stored)
      for (const Variable& v : kVariables) fillsTables = fillsTables || var == static_cast<void*>(v.where);
    if (fillsTables) {
      auto* tables = new XDCommonTables;
      char error[512];
      if (!PortXD_ConvertCommonRel(file, Rel_FileExtent(file), tables, error, sizeof(error))) stop(error);
      for (uint32_t g = 0; g < tables->numGlobals; g++)
        for (const Variable& v : kVariables)
          if (std::strcmp(v.name, tables->globals[g].name) == 0) *v.where = tables->globals[g].value;
    }
    sDataModules.push_back(std::move(m));
    return 1;
  }
  const size_t size = Rel_FileExtent(file);
  static XDCommonTables tables;
  char error[512];
  if (size == 0 || !PortXD_ConvertCommonRel(file, size, &tables, error, sizeof(error))) {
    stop(size == 0 ? "common_rel: the loaded module doesn't look like a REL" : error);
  }
  for (uint32_t g = 0; g < tables.numGlobals; g++) {
    bool found = false;
    for (const Variable& v : kVariables) {
      if (std::strcmp(v.name, tables.globals[g].name) == 0) {
        *v.where = tables.globals[g].value;
        found = true;
      }
    }
    if (!found) {
      stop("common_rel fills a variable missing from relglobal_vars.inc");
    }
  }
  return 1;
}

int unlink_module(void* module, void*) {
  for (auto it = sDataModules.begin(); it != sDataModules.end(); ++it) {
    if (it->module == module) {
      for (const auto& [var, bytes] : it->stored) std::memset(var, 0, bytes);
      sDataModules.erase(it);
      return 1;
    }
  }
  if (be32(static_cast<const uint8_t*>(module)) == kCommonRelId) {
    for (const Variable& v : kVariables) {
      *v.where = nullptr;
    }
  }
  return 1;
}

}

extern "C" {
typedef int (*PortOSModuleHandler)(void* module, void* bss);
void PortOS_SetModuleHandlers(PortOSModuleHandler link, PortOSModuleHandler unlink);

void PortXD_RegisterModuleHandlers(void) { PortOS_SetModuleHandlers(link_module, unlink_module); }
}
