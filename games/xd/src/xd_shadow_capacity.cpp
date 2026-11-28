#include "xd_shadow_capacity.h"

#include "xd_gen4.h"
#include "xd_gen57.h"
#include "xd_playthrough.h"

#include "data/xd_data_mods.h"

#include <data_mods.h>
#include <gs_formats.h>
#include <port_disc.h>
#ifdef __ANDROID__
#include <port_low_arena.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace em = engine::mods;
namespace xr = xd::randomizer;

namespace {

xr::MoreShadowsCapacity sCap[3];
bool sHave[3] = {false, false, false};
em::DataCatalog sCatalog;
bool sCatalogOk = false;

extern "C" const char* PortXD_DiscPath(void);

using DiscCache = std::map<std::pair<std::string, std::string>, std::pair<bool, std::vector<uint8_t>>>;
DiscCache sProgramCache;
int sScanDepth = 0;
PortDiscReader* sReader = nullptr;
std::string sHeldName;
uint8_t* sHeldFile = nullptr;
size_t sHeldSize = 0;
FsysArchive sHeld{};
bool sHeldOpen = false;
size_t sBiggestArchive = 0, sArchiveBytesRead = 0;
uint32_t sArchivesRead = 0;

void DropHeld() {
  if (sHeldFile != nullptr) PortDisc_Free(sHeldFile);
  sHeldFile = nullptr, sHeldSize = 0, sHeldOpen = false, sHeld = FsysArchive{};
  sHeldName.clear();
}

struct ScanScope {
  ScanScope() {
    if (sScanDepth++ == 0 && PortDisc_GameId()[0] == '\0') {
      const char* path = PortXD_DiscPath();
      if (path != nullptr && path[0] != '\0') sReader = PortDisc_ReaderOpen(path);
    }
  }
  ~ScanScope() {
    if (--sScanDepth != 0) return;
    DropHeld();
    if (sReader != nullptr) PortDisc_ReaderClose(sReader);
    sReader = nullptr;
  }
  ScanScope(const ScanScope&) = delete;
  ScanScope& operator=(const ScanScope&) = delete;
};

uint8_t* ReadArchiveFile(const std::string& archive, size_t* size) {
  return sReader != nullptr ? PortDisc_ReaderReadFile(sReader, archive.c_str(), size) : PortDisc_ReadOriginalFile(archive.c_str(), size);
}

bool ReadDisc(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
  const bool program = archive == em::kProgramArchive;
  if (program) {
    const auto it = sProgramCache.find({archive, entry});
    if (it != sProgramCache.end()) {
      out = it->second.second;
      return it->second.first;
    }
  }
  bool ok = false;
  std::vector<uint8_t> bytes;
  size_t size = 0;
  if (program) {
    const uint8_t* dol = PortDisc_Dol(&size);
    if (dol != nullptr && size != 0) {
      bytes.assign(dol, dol + size);
      ok = true;
    }
  } else {
    uint8_t* file = nullptr;
    FsysArchive local{};
    FsysArchive* f = nullptr;
    bool fileOwned = false;
    if (sScanDepth > 0 && sHeldFile != nullptr && sHeldName == archive) {
      f = sHeldOpen ? &sHeld : nullptr;
    } else {
      if (sScanDepth > 0) DropHeld();
      file = ReadArchiveFile(archive, &size);
      if (file != nullptr) {
        if (sScanDepth > 0) {
          sHeldFile = file, sHeldSize = size, sHeldName = archive;
          sArchivesRead++, sArchiveBytesRead += size, sBiggestArchive = std::max(sBiggestArchive, size);
          sHeldOpen = Fsys_Open(file, size, &sHeld);
          f = sHeldOpen ? &sHeld : nullptr;
        } else {
          fileOwned = true;
          f = Fsys_Open(file, size, &local) ? &local : nullptr;
        }
      }
    }
    if (f != nullptr) {
      const bool byNumber = entry.size() >= 2 && entry[0] == '#';
      const unsigned long number = byNumber ? std::strtoul(entry.c_str() + 1, nullptr, 10) : 0;
      for (uint32_t i = 0; i < f->count && !ok; i++) {
        FsysEntry e;
        if (byNumber ? i == number : Fsys_Entry(f, i, &e) && (std::strcmp(e.fullName, entry.c_str()) == 0 || std::strcmp(e.name, entry.c_str()) == 0)) {
          uint32_t n = 0;
          if (uint8_t* p = Fsys_Extract(f, i, &n)) {
            bytes.assign(p, p + n);
            std::free(p);
            ok = true;
          }
        }
      }
    }
    if (fileOwned && file != nullptr) PortDisc_Free(file);
  }
  if (!ok) {
    static int sReported = 0;
    if (sReported++ < 8) std::fprintf(stderr, "[shadow capacity] could not read %s / %s (scan depth %d)\n", archive.c_str(), entry.c_str(), sScanDepth);
  }
  if (program) sProgramCache[{archive, entry}] = {ok, bytes};
  out = bytes;
  return ok;
}

}

namespace xd::shadowcap {

bool Get(int pool, xr::MoreShadowsCapacity& out) {
  if (pool < 0 || pool > 2 || !sHave[pool]) return false;
  out = sCap[pool];
  return true;
}

int RealCount(int pool, int percent) {
  if (pool < 0 || pool > 2 || !sHave[pool]) return -1;
  return RealCountOf(sCap[pool], percent);
}

int RealCountOf(const xr::MoreShadowsCapacity& c, int percent) {
  if (c.speciesMt != 0 || c.speciesStory != 0) {
    const auto share = [&](uint32_t possible) {
      uint32_t target = static_cast<uint32_t>((static_cast<uint64_t>(percent) * possible + 50) / 100);
      if (target == 0 && possible != 0) target = 1;
      return std::min(target, possible);
    };
    const uint32_t story = std::min(share(c.speciesStory), c.rows);
    return static_cast<int>(story + std::min(share(c.speciesMt), c.rows - story));
  }
  uint32_t target = static_cast<uint32_t>((static_cast<uint64_t>(percent) * c.species + 50) / 100);
  if (target == 0 && c.species != 0) target = 1;
  return static_cast<int>(std::min<uint32_t>({target, c.species, c.rows, c.slots}));
}

bool ForCode(const std::string& code, int pool, xr::MoreShadowsCapacity& out) {
  static std::map<std::pair<std::string, int>, std::pair<bool, xr::MoreShadowsCapacity>> cache;
  const auto it = cache.find({code, pool});
  if (it != cache.end()) {
    out = it->second.second;
    return it->second.first;
  }
  bool ok = false;
  xr::MoreShadowsCapacity cap;
  ScanScope scan;
  do {
    if (!sCatalogOk) break;
    std::string error;
    engine::randomizer::Settings s;
    xr::ApplyStyle(s, xr::Style::Classic);
    bool seedOnly = false;
    if (!engine::randomizer::DecodeCode(xr::GetSchema(), code, s, seedOnly, error)) break;
    const bool incl4 = xr::CodeIncludesGen4(code), incl57 = xr::CodeIncludesGen57(code);
    const bool gen4 = incl4 || incl57 || pool >= 1, gen57 = incl57 || pool == 2;
    if (gen4 && (!xd::gen4::PackPresent() || !xr::LoadGen4Species(xd::gen4::PackFolder() / L"randomizer.txt", error))) break;
    if (gen57 && (!xd::gen57::PackPresent() || !xr::LoadGen57Species(xd::gen57::PackFolder() / L"randomizer.txt", error))) break;
    xr::Result r;
    if (!xr::Generate(sCatalog, ReadDisc, s, r, error, nullptr)) break;
    xr::MoreShadowsPlan plan;
    plan.amount = 7, plan.percent = 100, plan.seed = 1, plan.capacityOnly = true;
    engine::randomizer::ModOutput mod;
    ok = xr::GenerateMoreShadows(sCatalog, ReadDisc, plan, &r, gen4, mod, error, gen57, &cap);
    if (!ok) std::fprintf(stderr, "[shadow capacity] randomizer %s: %s\n", code.c_str(), error.c_str());
    else std::fprintf(stderr, "[shadow capacity] randomizer %s: %u eligible Pokemon, %u possible Shadow Pokemon (free species), %u free rows\n", code.c_str(), cap.slots, cap.species, cap.rows);
  } while (false);
  cache[{code, pool}] = {ok, cap};
  out = cap;
  return ok;
}

}

static void ReadShadowCapacityScan();

extern "C" void PortXD_ReadShadowCapacity(void) {
  if (!xd::pt::ScreenEnabled()) return;
  ReadShadowCapacityScan();
  std::fprintf(stderr, "[shadow capacity] the scan read %u archives (%llu MB in all, the biggest %llu MB, at most one held at a time)\n", sArchivesRead,
               static_cast<unsigned long long>(sArchiveBytesRead >> 20), static_cast<unsigned long long>(sBiggestArchive >> 20));
#ifdef __ANDROID__
  PortLowStats st;
  PortLow_GetStats(&st);
  std::fprintf(stderr, "[shadow capacity] below-4-GB memory: %llu MB in use after the scan (the highest so far %llu MB)\n", static_cast<unsigned long long>(st.used >> 20),
               static_cast<unsigned long long>(st.usedPeak >> 20));
#endif
}

static void ReadShadowCapacityScan() {
  em::DataCatalog& catalog = sCatalog;
  std::string error;
  sCatalogOk = false;
  ScanScope scan;
  if (!xd::data::BuildDataCatalog(ReadDisc, catalog, error)) {
    std::fprintf(stderr, "[shadow capacity] the disc's tables could not be read (%s): the New Game screen shows no number\n", error.c_str());
    return;
  }
  sCatalogOk = true;
  for (int pool = 0; pool < 3; pool++) {
    const bool gen4 = pool >= 1, gen57 = pool == 2;
    if (gen4 && !xd::gen4::PackPresent()) break;
    if (gen4 && !xr::LoadGen4Species(xd::gen4::PackFolder() / L"randomizer.txt", error)) break;
    if (gen57 && (!xd::gen57::PackPresent() || !xr::LoadGen57Species(xd::gen57::PackFolder() / L"randomizer.txt", error))) break;
    xr::MoreShadowsPlan plan;
    plan.amount = 7, plan.percent = 100, plan.seed = 1, plan.capacityOnly = true;
    engine::randomizer::ModOutput out;
    xr::MoreShadowsCapacity cap;
    if (!xr::GenerateMoreShadows(catalog, ReadDisc, plan, nullptr, gen4, out, error, gen57, &cap)) {
      std::fprintf(stderr, "[shadow capacity] pool %d: %s\n", pool, error.c_str());
      break;
    }
    sCap[pool] = cap;
    sHave[pool] = true;
    std::fprintf(stderr, "[shadow capacity] pool %d (%s): %u eligible trainers, %u eligible Pokemon, %u free species, %u free rows\n", pool, pool == 0 ? "Gen 1-3" : pool == 1 ? "+ Gen 4" : "+ Gen 5-7",
                 cap.trainers, cap.slots, cap.species, cap.rows);
  }
}
