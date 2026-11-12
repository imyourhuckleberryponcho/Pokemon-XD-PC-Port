#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <dolphin/dvd.h>

extern "C" {
int64_t PortOS_BusyFor(int64_t ticks);
void PortCpuCost_Accounted(int64_t ticks);
void PortOS_InterruptCost(int64_t ticks);
void PortOS_BusyLater(int64_t ticks);
int PortOS_InHandler(void);
int64_t PortTime_Now(void);
BOOL PortDVD_Open(const char* fileName, DVDFileInfo* fileInfo);
extern int32_t pokecoloFrameCounter;
}

namespace {

struct Entry {
  std::string name;
  bool dir;
  int parent;
};

struct Switches {
  bool on = false;
  bool log = false;
  bool dump = false;
};

const Switches& switches() {
  static const Switches s = [] {
    Switches v;
    if (const char* e = std::getenv("XD_DVDOPEN_COST"); e != nullptr && e[0] != '\0') {
      v.dump = std::strcmp(e, "dump") == 0;
      v.on = v.dump || std::strcmp(e, "exact") == 0;
      std::fprintf(stderr, "[dvdopen cost] XD_DVDOPEN_COST=%s: opening a file by name %s\n", e,
                   v.on ? "takes the GameCube's directory-walk time" : "takes no time (unknown value)");
    }
    if (const char* e = std::getenv("XD_DVDOPEN_COST_LOG"); e != nullptr && e[0] != '\0' && e[0] != '0') {
      v.log = true;
    }
    return v;
  }();
  return s;
}

std::vector<Entry> gEntries;
std::once_flag gOnce;

std::string lower(const char* s, size_t n) {
  std::string out(s, n);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

int find_dir(const std::string& path) {
  if (path.empty()) return -1;
  int parent = -1;
  size_t at = 0;
  while (at < path.size()) {
    size_t end = path.find('/', at);
    if (end == std::string::npos) end = path.size();
    const std::string part = path.substr(at, end - at);
    int found = -2;
    for (size_t i = 0; i < gEntries.size(); ++i) {
      if (gEntries[i].dir && gEntries[i].parent == parent && gEntries[i].name == part) {
        found = static_cast<int>(i);
        break;
      }
    }
    if (found < 0) return -2;
    parent = found;
    at = end + 1;
  }
  return parent;
}

void read_table() {
  char path[512];
  int misses = 0;
  for (s32 num = 1; misses < 64; ++num) {
    if (!DVDConvertEntrynumToPath(num, path, sizeof(path))) {
      ++misses;
      continue;
    }
    misses = 0;
    size_t len = std::strlen(path);
    const bool dir = len > 1 && path[len - 1] == '/';
    if (dir) --len;
    const char* p = path[0] == '/' ? path + 1 : path;
    len -= static_cast<size_t>(p - path);
    const std::string whole = lower(p, len);
    const size_t slash = whole.rfind('/');
    Entry e;
    e.dir = dir;
    e.name = slash == std::string::npos ? whole : whole.substr(slash + 1);
    e.parent = slash == std::string::npos ? -1 : find_dir(whole.substr(0, slash));
    if (e.parent == -2) continue;
    gEntries.push_back(std::move(e));
  }
}

int64_t walk(int parent, const std::string& want, int* place, bool wantDir) {
  int64_t c = (wantDir ? 7 : 35) * static_cast<int64_t>(want.size()) - (wantDir ? 27 : 0);
  int order = 0;
  *place = -1;
  for (const Entry& e : gEntries) {
    if (e.parent != parent) continue;
    if (wantDir && !e.dir) {
      c += 26;
      ++order;
      continue;
    }
    if (e.name == want && e.dir == wantDir) {
      *place = order;
      if (wantDir) c += 28 * static_cast<int64_t>(want.size());
      break;
    }
    size_t k = 0;
    while (k < e.name.size() && k < want.size() && e.name[k] == want[k]) ++k;
    c += (e.dir ? 60 : 63) + 28 * static_cast<int64_t>(k);
    if (k == e.name.size() && k < want.size()) c -= 21;
    ++order;
  }
  return c;
}

int64_t instructions(const char* path, int* place) {
  std::call_once(gOnce, read_table);
  const char* p = path != nullptr && path[0] == '/' ? path + 1 : path;
  *place = -1;
  if (p == nullptr) return 0;
  const std::string whole = lower(p, std::strlen(p));
  int64_t c = 75;
  int parent = -1;
  size_t at = 0;
  for (;;) {
    size_t end = whole.find('/', at);
    const bool lastPart = end == std::string::npos;
    if (lastPart) end = whole.size();
    c += walk(parent, whole.substr(at, end - at), place, !lastPart);
    if (lastPart || *place < 0) break;
    parent = find_dir(whole.substr(0, end));
    if (parent < 0) break;
    at = end + 1;
  }
  return c;
}

void dump() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::call_once(gOnce, read_table);
    int order = 0;
    for (const Entry& e : gEntries) {
      if (e.parent != -1) continue;
      if (!e.dir) {
        int place = -1;
        const int64_t c = instructions(e.name.c_str(), &place);
        std::printf("[dvdopen cost] dump %d %s %lld\n", order, e.name.c_str(), static_cast<long long>(c));
      }
      ++order;
    }
  });
}

void spend(const char* what, const char* path) {
  const Switches& s = switches();
  if (!s.on && !s.log) return;
  if (s.dump) dump();
  int place = -1;
  const int64_t c = instructions(path, &place);
  const int64_t ticks = s.on ? c / 12 : 0;
  const int64_t t0 = PortTime_Now();
  int64_t spent = ticks > 0 ? PortOS_BusyFor(ticks) : 0;
  if (ticks > 0 && spent == 0) {
    if (PortOS_InHandler()) {
      PortOS_InterruptCost(ticks);
    } else {
      PortOS_BusyLater(ticks);
      spent = ticks;
    }
  }
  PortCpuCost_Accounted(spent);
  if (s.log) {
    std::printf("[dvdopen cost] t %lld pass %d %s %s place %d instructions %lld spent %lld\n",
                static_cast<long long>(t0), pokecoloFrameCounter, what, path != nullptr ? path : "(null)", place,
                static_cast<long long>(c), static_cast<long long>(spent));
  }
}

}

extern "C" BOOL PortXD_DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  spend("open", fileName);
  return PortDVD_Open(fileName, fileInfo);
}

extern "C" s32 PortXD_DVDConvertPathToEntrynum(const char* path) {
  spend("find", path);
  return DVDConvertPathToEntrynum(path);
}
