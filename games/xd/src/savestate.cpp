#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "xd_savestate_ui.h"

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__) && defined(__x86_64__)) || (defined(__APPLE__) && (defined(__x86_64__) || defined(__aarch64__)))
#define XD_SAVESTATES_BUILT 1
#endif

#ifndef XD_SAVESTATES_BUILT

extern "C" void PortXD_SavestateInit(void) {}
extern "C" void PortXD_SavestateFrame(void) {}
namespace xd::savestate_ui {
SlotInfo Info(int) { return {}; }
void Request(int) {}
std::string Unavailable() { return "save states are not built for this system yet"; }
bool Offered() { return false; }
}
extern "C" void PortXD_RunGameMain(void (*fn)(int, char**), int argc, char** argv) { fn(argc, argv); }

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#endif
#include <any>
#include <atomic>
#include <filesystem>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef XD_HAVE_ZSTD
#include <zstd.h>
#endif
#include <algorithm>
#include <chrono>
#include <ctime>
#include <gsthread_coro.h>
#include <port_audio.h>
#include <port_menu_music.h>
#include <port_savestate.h>

#include "savestate_features.h"
#include "xd_savestate_ui.h"
#include "xd_save_picker.h"
#include "online_battle.h"
#include "xd_challenges.h"
#include "xd_speedrun.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <port_window_shot.h>
#include <save_thumbnail.h>
#include <i18n.h>
#include <port_user_folder.h>
#include <settings.h>

#ifdef _WIN32
extern "C" uintptr_t __security_cookie;
#endif
extern "C" {
const char* PortXD_SnapshotId(void);
const char* PortXD_ActiveMods(void);
const char* PortXD_SettingsSummary(void);
size_t EngineSettings_ReplayText(char* buffer, size_t size);
void PortAI_FileGet(uint64_t out[4]);
void PortAI_FileSet(const uint64_t in[4]);
}
bool PortDsp_StateToBytes(const PortDspState* s, portstate::Out& out);
bool PortDsp_StateLoadBytes(portstate::In& in);
bool PortXD_PcTradeBusy(void);
bool PortXD_HeapModelToBytes(const std::any& state, portstate::Out& out);
bool PortXD_HeapModelFromBytes(portstate::In& in, std::any& state);

extern void* MEM1Start;
void PortGX_DebugState(char* buf, size_t n);
extern "C" void PortGX_TraceBreakpoints(int on);
extern "C" void PortXD_GfxNote(char* out, unsigned size);
extern "C" void PortOS_DebugState(char* buf, size_t n);
extern "C" void PortGX_TraceOrigins(void (*describe)(const void*, char*, size_t));
extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize);
std::any PortXD_HeapModelSave();
void PortXD_HeapModelLoad(const std::any& state);

#include <dolphin/pad.h>
struct PortInputScriptState {
  bool virtualSet;
  PADStatus status;
};
extern "C" {
void PortInput_ScriptStateGet(PortInputScriptState* out);
void PortInput_ScriptStateSet(const PortInputScriptState* in);
void* ARGetStorageAddress(void);
uint32_t ARGetSize(void);
void* PortMem_LowAlloc(size_t size, size_t align);
void PortMem_LowRange(uint8_t** base, size_t* used);
bool PortMem_LowEnsureUsed(size_t used);
void PortXD_GameDataRange(unsigned char** begin, size_t* size);
void PortXD_GameDataRange2(unsigned char** begin, size_t* size);
void PortXD_FloorPlacesRange(unsigned char** begin, size_t* size);
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
void PortGX_DrainForState(void);
void PortTime_GameClockStateGet(int64_t out[4]);
void PortTime_GameClockStateSet(const int64_t in[4]);
void PortOS_ThreadReport(void);
void PortTime_SetSpeed(float speed);
bool PortXD_OpenSaveStatesMenu(void);
uint32_t PortSpeedups_SaveStatesPresses(void);
int32_t GSfloorGetCurrent(void);
int32_t fightIsFight(void);
uint32_t fightFloor_GetEncountFloorId(void* floor);
uint32_t floorGetPlaceName(uint32_t floorID);
uint16_t* GSmsgGetGSchar(uint32_t key);
extern void* floordata;
size_t PortAI_StateSize(void);
void PortAI_StateGet(void* out);
void PortAI_StateSet(const void* in);
bool PortDsp_StateSave(PortDspState* out);
void PortDsp_StateLoad(const PortDspState* in);
void PortDsp_StateFree(PortDspState* state);
void PortDsp_Join(void);
void* AuroraSaveGXState(void);
void AuroraRestoreGXState(void* saved);
}

namespace th = engine::memcard::thumbnail;

#ifdef _WIN32
#define XD_NOINLINE __declspec(noinline)
#else
#define XD_NOINLINE __attribute__((noinline))
#endif

namespace {

uintptr_t frame_guard() {
#ifdef _WIN32
  return __security_cookie;
#else
  return gscoro::StackGuardGet();
#endif
}

void sleep_ms(unsigned ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

std::FILE* open_path(const std::filesystem::path& path, bool write) {
#ifdef _WIN32
  return _wfopen(path.wstring().c_str(), write ? L"wb" : L"rb");
#else
  return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

bool region_at(const uint8_t* p, bool* writable, const uint8_t** regionEnd) {
  *writable = false;
  *regionEnd = p;
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
  *regionEnd = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
  *writable = mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) != 0 &&
              (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
  return true;
#elif defined(__APPLE__)
  mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(p);
  mach_vm_size_t size = 0;
  vm_region_basic_info_data_64_t info{};
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count,
                     &object) != KERN_SUCCESS) {
    return false;
  }
  if (address > reinterpret_cast<mach_vm_address_t>(p)) {
    *regionEnd = reinterpret_cast<const uint8_t*>(address);
    return true;
  }
  *regionEnd = reinterpret_cast<const uint8_t*>(address + size);
  *writable = (info.protection & VM_PROT_READ) != 0 && (info.protection & VM_PROT_WRITE) != 0;
  return true;
#else
  std::FILE* maps = std::fopen("/proc/self/maps", "r");
  if (maps == nullptr) return false;
  const uintptr_t at = reinterpret_cast<uintptr_t>(p);
  char line[512];
  bool ok = false;
  uintptr_t next = 0;
  while (std::fgets(line, sizeof line, maps) != nullptr) {
    unsigned long long lo = 0, hi = 0;
    char perms[8] = {};
    if (std::sscanf(line, "%llx-%llx %7s", &lo, &hi, perms) != 3) continue;
    if (at >= lo && at < hi) {
      *regionEnd = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(hi));
      *writable = perms[0] == 'r' && perms[1] == 'w';
      ok = true;
      break;
    }
    if (lo > at) {
      next = static_cast<uintptr_t>(lo);
      break;
    }
  }
  std::fclose(maps);
  if (!ok && next != 0) {
    *regionEnd = reinterpret_cast<const uint8_t*>(next);
    ok = true;
  }
  return ok;
#endif
}

using engine::i18n::Tr;
namespace es = engine::settings;

constexpr size_t kMem1Size = 0x01800000;
constexpr uintptr_t kLockedCache = 0xE0000000;
constexpr size_t kLockedCacheSize = 0x4000;

struct Area {
  const char* name;
  uint8_t* address;
  std::vector<uint8_t> bytes;
};

struct Slot {
  bool valid = false;
  uint32_t flip = 0, retrace = 0;
  std::vector<Area> areas;
  std::any os, vi, gxf, io, heap;
  std::vector<std::any> features;
  PortInputScriptState pad{};
  PortDspState dsp{};
  bool haveDsp = false;
  std::vector<uint8_t> ai;
  void* gx = nullptr;
  gscoro::Snapshot gs;
  int64_t gameClock[4] = {};
  uint64_t aiFile[4] = {};
  std::vector<uint8_t> dspBytes;
  bool fileMode = false;
  uintptr_t fileCookie = 0;
};

bool sOn = false;
bool sPlayer = false;
std::string sLastWhy;
bool sJustRestored = false;
int sLoadSlot = 0;
std::atomic<int> sKeyRequest{0};
std::map<int, Slot*> sSlots;
bool sLog = false;
uint32_t sLogEvery = 0;

struct ScriptEntry {
  uint32_t flip;
  std::string what;
  int slot;
  bool done;
};
std::vector<ScriptEntry> sScript;

uint64_t hash64(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = 0;
    std::memcpy(&v, p + i, i + 8 <= n ? 8 : n - i);
    h = (h ^ v) * 1099511628211ull;
  }
  return h;
}

std::vector<Area> collect_areas(bool withBytes) {
  std::vector<Area> v;
  auto add = [&](const char* name, void* address, size_t size) {
    if (address == nullptr || size == 0) return;
    uint8_t* p = static_cast<uint8_t*>(address);
    uint8_t* end = p + size;
    while (p < end) {
      bool ok = false;
      const uint8_t* regionLimit = nullptr;
      if (!region_at(p, &ok, &regionLimit) || regionLimit <= p) break;
      uint8_t* regionEnd = std::min(end, const_cast<uint8_t*>(regionLimit));
      if (ok) {
        Area a{name, p, {}};
        if (withBytes) a.bytes.assign(p, regionEnd);
        else a.bytes.resize(static_cast<size_t>(regionEnd - p));
        v.push_back(std::move(a));
      }
      p = regionEnd;
    }
  };
  add("mem1", MEM1Start, kMem1Size);
  add("aram", ARGetStorageAddress(), ARGetSize());
  add("scratch", reinterpret_cast<void*>(kLockedCache), kLockedCacheSize);
  uint8_t* lowBase = nullptr;
  size_t lowUsed = 0;
  PortMem_LowRange(&lowBase, &lowUsed);
  uint8_t* stackBegin = nullptr;
  size_t stackSize = 0;
  gscoro::StackBlock(&stackBegin, &stackSize);
  if (stackBegin != nullptr && lowBase != nullptr && stackBegin >= lowBase && stackBegin + stackSize <= lowBase + lowUsed) {
    add("low", lowBase, static_cast<size_t>(stackBegin - lowBase));
    add("low2", stackBegin + stackSize, static_cast<size_t>(lowBase + lowUsed - (stackBegin + stackSize)));
  } else {
    add("low", lowBase, lowUsed);
  }
  if (lowBase != nullptr) v.push_back(Area{"lowend", lowBase + lowUsed, {}});
  unsigned char* dataBegin = nullptr;
  size_t dataSize = 0;
  PortXD_GameDataRange(&dataBegin, &dataSize);
  add("gamedata", dataBegin, dataSize);
  PortXD_GameDataRange2(&dataBegin, &dataSize);
  add("gamedata2", dataBegin, dataSize);
  unsigned char* placesBegin = nullptr;
  size_t placesSize = 0;
  PortXD_FloorPlacesRange(&placesBegin, &placesSize);
  add("floorplaces", placesBegin, placesSize);
  PortMenuMusicRange ranges[16];
  const size_t n = PortMenuMusic_EngineRanges(ranges, 16);
  for (size_t i = 0; i < n; i++) add(i == 0 ? "musyx" : "musyx-extra", ranges[i].address, ranges[i].size);
  return v;
}

void log_hashes(const char* tag) {
  const std::vector<Area> areas = collect_areas(false);
  std::string line = std::string("[savestate-chk] ") + tag + " flip " + std::to_string(PortVI_GetFlipCount()) +
                     " retrace " + std::to_string(VIGetRetraceCount());
  for (const Area& a : areas) {
    char buf[64];
    std::snprintf(buf, sizeof buf, " %s %016llx", a.name, static_cast<unsigned long long>(hash64(a.address, a.bytes.size())));
    line += buf;
  }
  std::fprintf(stderr, "%s\n", line.c_str());
  if (const char* path = std::getenv("XD_SAVESTATE_BLOCKS"); path != nullptr && path[0] != '\0') {
    static FILE* f = std::fopen(path, "ab");
    if (f != nullptr) {
      std::fprintf(f, "F %u\n", PortVI_GetFlipCount());
      for (const Area& a : areas) {
        const size_t step = 0x1000;
        for (size_t o = 0; o < a.bytes.size(); o += step) {
          const size_t n = std::min(step, a.bytes.size() - o);
          std::fprintf(f, "%s %llx %016llx\n", a.name, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(a.address + o)),
                       static_cast<unsigned long long>(hash64(a.address + o, n)));
        }
      }
      std::fflush(f);
    }
  }
}

constexpr char kFileMagic[] = "XDSAVESTATE 1\n";

std::string exe_hash() {
  static std::string cached;
  if (!cached.empty()) return cached;
  uint64_t h = 1469598103934665603ull;
  size_t total = 0;
#ifdef _WIN32
  wchar_t path[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
  const bool haveName = n != 0;
#elif defined(__APPLE__)
  char path[4096];
  uint32_t pathSize = sizeof path;
  const bool haveName = _NSGetExecutablePath(path, &pathSize) == 0;
#else
  const char* const path = "/proc/self/exe";
  const bool haveName = true;
#endif
  if (haveName) {
#ifdef _WIN32
    if (FILE* f = _wfopen(path, L"rb")) {
#else
    if (FILE* f = std::fopen(path, "rb")) {
#endif
      std::vector<uint8_t> buf(1 << 20);
      for (;;) {
        const size_t got = std::fread(buf.data(), 1, buf.size(), f);
        if (got == 0) break;
        total += got;
        for (size_t i = 0; i < got; i++) h = (h ^ buf[i]) * 1099511628211ull;
      }
      std::fclose(f);
    }
  }
  char text[64];
  std::snprintf(text, sizeof text, "%016llx/%zu", static_cast<unsigned long long>(h), total);
  cached = text;
  return cached;
}

std::string settings_text() {
  char replay[1024] = {};
  EngineSettings_ReplayText(replay, sizeof replay);
  std::string t = std::string(PortXD_SettingsSummary() != nullptr ? PortXD_SettingsSummary() : "") + " | " + replay;
  for (char& c : t) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return t;
}

std::string build_id_text(uint32_t flip) {
  std::string t = kFileMagic;
  t += std::string("snapshot ") + PortXD_SnapshotId() + "\n";
  t += "exe " + exe_hash() + "\n";
  t += std::string("mods ") + (PortXD_ActiveMods() != nullptr ? PortXD_ActiveMods() : "") + "\n";
  t += "settings " + settings_text() + "\n";
  t += "rules " + xd::challenges::ActiveIds() + "\n";
  t += "frame " + std::to_string(flip) + "\n";
  t += "end\n";
  return t;
}

std::string file_refusal() {
  const char* mods = PortXD_ActiveMods();
  if (mods != nullptr && mods[0] != '\0') {
    return std::string("mods are loaded (") + mods + "): their battle and script state is not saved yet";
  }
  if (PortXD_PcTradeBusy()) return "a trade is being finished";
  if (const char* rep = std::getenv("XD_REPLAY"); rep != nullptr && rep[0] != '\0') return "a recording is being replayed";
  if (const char* rep = std::getenv("XD_BATTLE_REPLAY"); rep != nullptr && rep[0] != '\0') return "a battle replay is running";
  for (const xd::savestate::FeatureEntry& f : xd::savestate::Features()) {
    if (!f.toBytes || !f.fromBytes) return std::string("the feature '") + f.name + "' cannot be written to a file yet";
  }
  return "";
}

bool write_file(const Slot* s, const char* path, std::string& why) {
  portstate::Out out;
  const std::string head = build_id_text(s->flip);
  out.raw(head.data(), head.size());
  auto section = [&](const char* tag, const portstate::Out& body) {
    out.raw(tag, 4);
    out.u64(body.b.size());
    out.b.insert(out.b.end(), body.b.begin(), body.b.end());
  };
  {
    portstate::Out b;
    b.pod(s->flip); b.pod(s->retrace);
    b.pod(s->gameClock);
    b.pod(s->pad);
    b.pod(s->aiFile);
    b.pod(s->haveDsp);
    section("META", b);
  }
  {
    portstate::Out b;
    if (!PortState_OsToBytes(s->os, b, why)) return false;
    section("OS__", b);
  }
  {
    portstate::Out b;
    PortState_ViToBytes(s->vi, b);
    section("VI__", b);
  }
  {
    portstate::Out b;
    PortState_IoToBytes(s->io, b);
    section("IO__", b);
  }
  {
    portstate::Out b;
    if (!PortXD_HeapModelToBytes(s->heap, b)) { why = "the heap model cannot be written"; return false; }
    section("HEAP", b);
  }
  {
    portstate::Out b;
    if (!PortState_GxFifoToBytes(s->gxf, b, why)) return false;
    section("GXF_", b);
  }
  {
    portstate::Out b;
    const auto& list = xd::savestate::Features();
    b.u64(list.size());
    for (size_t i = 0; i < list.size() && i < s->features.size(); i++) {
      b.str(list[i].name);
      portstate::Out f;
      list[i].toBytes(s->features[i], f);
      b.u64(f.b.size());
      b.b.insert(b.b.end(), f.b.begin(), f.b.end());
    }
    section("FEAT", b);
  }
  if (s->haveDsp) {
    portstate::Out b;
    if (!PortDsp_StateToBytes(&s->dsp, b)) { why = "the sound chip cannot be written"; return false; }
    section("DSP_", b);
  }
  {
    portstate::Out b;
    gscoro::ToBytes(s->gs, frame_guard(), b.b);
    section("TASK", b);
  }
  for (const Area& a : s->areas) {
    portstate::Out b;
    b.str(a.name);
    b.u64(a.name != nullptr && std::strcmp(a.name, "aram") == 0 ? static_cast<uint64_t>(a.address - static_cast<uint8_t*>(ARGetStorageAddress())) : reinterpret_cast<uintptr_t>(a.address));
    b.u64(a.bytes.size());
    b.raw(a.bytes.data(), a.bytes.size());
    section("AREA", b);
  }
  std::vector<uint8_t> packed;
  const std::vector<uint8_t>* toWrite = &out.b;
#ifdef XD_HAVE_ZSTD
  {
    const size_t headSize = head.size();
    const size_t rawSize = out.b.size() - headSize;
    packed.assign(out.b.begin(), out.b.begin() + static_cast<std::ptrdiff_t>(headSize));
    const char magic[4] = {'Z', 'S', 'T', '1'};
    packed.insert(packed.end(), magic, magic + 4);
    const uint64_t raw64 = rawSize;
    const uint8_t* rp = reinterpret_cast<const uint8_t*>(&raw64);
    packed.insert(packed.end(), rp, rp + 8);
    const size_t at = packed.size();
    packed.resize(at + ZSTD_compressBound(rawSize));
    const size_t n = ZSTD_compress(packed.data() + at, packed.size() - at, out.b.data() + headSize, rawSize, 1);
    if (!ZSTD_isError(n)) {
      packed.resize(at + n);
      toWrite = &packed;
      std::fprintf(stderr, "[savestate] packed %.1f MB -> %.1f MB (zstd 1)\n", out.b.size() / 1048576.0, packed.size() / 1048576.0);
    }
  }
#endif
  std::string tmp = std::string(path) + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) { why = std::string("cannot write ") + tmp; return false; }
  const bool ok = std::fwrite(toWrite->data(), 1, toWrite->size(), f) == toWrite->size();
  const bool closed = std::fclose(f) == 0;
  if (!ok || !closed) { why = "the file could not be written completely (disk full?)"; std::remove(tmp.c_str()); return false; }
  std::remove(path);
  if (std::rename(tmp.c_str(), path) != 0) { why = "cannot move the finished file into place"; return false; }
  std::fprintf(stderr, "[savestate] file written: %s (%.1f MB, frame %u)\n", path, out.b.size() / 1048576.0, s->flip);
  return true;
}

bool read_file(const char* path, Slot* s, std::string& why) {
  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) { why = std::string("cannot open ") + path; return false; }
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(size > 0 ? static_cast<size_t>(size) : 0);
  const bool got = std::fread(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  if (!got || data.empty()) { why = "the file cannot be read completely"; return false; }
  {
    const std::string probe(reinterpret_cast<const char*>(data.data()), data.size() < 4096 ? data.size() : 4096);
    const size_t headEnd = probe.find("\nend\n");
    if (headEnd != std::string::npos && data.size() >= headEnd + 5 + 12 && std::memcmp(data.data() + headEnd + 5, "ZST1", 4) == 0) {
#ifdef XD_HAVE_ZSTD
      uint64_t raw64 = 0;
      std::memcpy(&raw64, data.data() + headEnd + 9, 8);
      if (raw64 > (1ull << 31)) { why = "the file is damaged (size)"; return false; }
      std::vector<uint8_t> whole(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(headEnd + 5));
      whole.resize(headEnd + 5 + static_cast<size_t>(raw64));
      const size_t n = ZSTD_decompress(whole.data() + headEnd + 5, static_cast<size_t>(raw64), data.data() + headEnd + 17, data.size() - headEnd - 17);
      if (ZSTD_isError(n) || n != raw64) { why = "the file is damaged (it does not unpack)"; return false; }
      data.swap(whole);
#else
      why = "the file is packed and this program cannot unpack it";
      return false;
#endif
    }
  }
  const std::string text(reinterpret_cast<const char*>(data.data()), data.size() < 4096 ? data.size() : 4096);
  const size_t endAt = text.find("\nend\n");
  if (text.compare(0, sizeof(kFileMagic) - 1, kFileMagic) != 0 || endAt == std::string::npos) {
    why = "this is not a save state file of this program (or it is damaged)";
    return false;
  }
  const std::string mine = build_id_text(0);
  auto lines = [](const std::string& t) {
    std::vector<std::string> v;
    size_t pos = 0;
    while (pos < t.size()) {
      size_t e = t.find('\n', pos);
      if (e == std::string::npos) e = t.size();
      v.push_back(t.substr(pos, e - pos));
      pos = e + 1;
    }
    return v;
  };
  const std::vector<std::string> a = lines(text.substr(0, endAt + 5)), b = lines(mine);
  for (size_t i = 0; i < a.size() && i < b.size(); i++) {
    if (a[i].compare(0, 6, "frame ") == 0) continue;
    if (a[i] != b[i]) {
      std::string which = a[i].substr(0, a[i].find(' '));
      if (which == "exe") why = "this save state was made by another build of the program (the program file differs)";
      else if (which == "snapshot") why = "this save state was made with another version of the game code (" + a[i] + ", this: " + b[i] + ")";
      else if (which == "mods") why = "this save state was made with other mods (" + a[i] + "; this run: " + b[i] + ")";
      else if (which == "settings") why = "this save state was made with other settings; start with the same settings and try again";
      else why = "this save state does not fit this program (" + which + ")";
      return false;
    }
  }
  portstate::In in(data.data(), data.size());
  in.pos = endAt + 5;
  s->fileMode = true;
  bool haveMeta = false, haveOs = false, haveVi = false, haveIo = false, haveHeap = false, haveGxf = false, haveFeat = false, haveTask = false;
  while (in.pos < data.size() && in.ok()) {
    char tag[5] = {};
    in.raw(tag, 4);
    const uint64_t size64 = in.u64();
    if (!in.ok() || size64 > data.size() - in.pos) { why = "the file is cut short"; return false; }
    portstate::In body(data.data() + in.pos, static_cast<size_t>(size64));
    in.pos += static_cast<size_t>(size64);
    const std::string t = tag;
    if (t == "META") {
      s->flip = body.pod<uint32_t>(); s->retrace = body.pod<uint32_t>();
      for (int64_t& v : s->gameClock) v = body.pod<int64_t>();
      s->pad = body.pod<PortInputScriptState>();
      for (uint64_t& v : s->aiFile) v = body.pod<uint64_t>();
      s->haveDsp = body.pod<bool>();
      haveMeta = body.ok();
    } else if (t == "OS__") {
      if (!PortState_OsFromBytes(body, s->os, why)) return false;
      haveOs = true;
    } else if (t == "VI__") {
      haveVi = PortState_ViFromBytes(body, s->vi);
    } else if (t == "IO__") {
      haveIo = PortState_IoFromBytes(body, s->io);
    } else if (t == "HEAP") {
      haveHeap = PortXD_HeapModelFromBytes(body, s->heap);
    } else if (t == "GXF_") {
      haveGxf = PortState_GxFifoFromBytes(body, s->gxf);
    } else if (t == "FEAT") {
      const auto& list = xd::savestate::Features();
      const uint64_t n = body.u64();
      if (n != list.size()) { why = "the file's port features are not this program's"; return false; }
      s->features.clear();
      for (size_t i = 0; i < list.size() && body.ok(); i++) {
        if (body.str() != list[i].name) { why = "the file's port features are not this program's"; return false; }
        const uint64_t fs = body.u64();
        if (!body.ok() || fs > body.n - body.pos) { why = "the feature part of the file is damaged"; return false; }
        portstate::In fb(body.p + body.pos, static_cast<size_t>(fs));
        body.pos += static_cast<size_t>(fs);
        std::any value;
        if (!list[i].fromBytes(fb, value)) { why = std::string("the state of '") + list[i].name + "' in the file is damaged"; return false; }
        s->features.push_back(std::move(value));
      }
      haveFeat = body.ok();
    } else if (t == "DSP_") {
      s->dspBytes.assign(body.p, body.p + body.n);
    } else if (t == "TASK") {
      const char* gwhy = nullptr;
      size_t rewritten = 0;
      if (!gscoro::FromBytes(body.p, body.n, frame_guard(), s->gs, &rewritten, &s->fileCookie, &gwhy)) { why = gwhy != nullptr ? gwhy : "the task part is damaged"; return false; }
      std::fprintf(stderr, "[savestate] file's stacks: frame-guard words rewritten from cookie %llx to %llx: %zu\n", static_cast<unsigned long long>(s->fileCookie),
                   static_cast<unsigned long long>(frame_guard()), rewritten);
      haveTask = true;
    } else if (t == "AREA") {
      Area ar{nullptr, nullptr, {}};
      static std::deque<std::string> names;
      names.push_back(body.str());
      ar.name = names.back().c_str();
      ar.address = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(body.u64()));
      if (std::strcmp(ar.name, "aram") == 0) ar.address += reinterpret_cast<uintptr_t>(ARGetStorageAddress());
      const uint64_t n = body.u64();
      if (!body.ok() || n > body.n - body.pos) { why = "a memory part of the file is cut short"; return false; }
      ar.bytes.assign(body.p + body.pos, body.p + body.pos + n);
      s->areas.push_back(std::move(ar));
    } else {
      why = "the file has a part this program does not know (" + t + ")";
      return false;
    }
  }
  if (!in.ok() || !haveMeta || !haveOs || !haveVi || !haveIo || !haveHeap || !haveGxf || !haveFeat || !haveTask) {
    why = "the file is damaged or incomplete";
    return false;
  }
  return true;
}

std::string gfx_note() {
  char b[96];
  PortXD_GfxNote(b, sizeof b);
  char g[512] = {};
  PortGX_DebugState(g, sizeof g);
  return std::string(b) + " | gx " + g;
}

XD_NOINLINE int take_into(Slot* s) {
  volatile int again = gscoro::Take(s->gs);
  return again;
}

bool gather(int number, bool forFile, std::string& why) {
  PortGX_DrainForState();
  PortDsp_Join();
  portstate::Why w;
  if (!PortState_IoIdle(w)) {
    why = w;
    return false;
  }
  Slot*& slot = sSlots[number];
  if (slot == nullptr) slot = new Slot();
  Slot* s = slot;
  if (s->haveDsp) PortDsp_StateFree(&s->dsp);
  s->haveDsp = false;
  s->valid = false;
  s->fileMode = forFile;
  s->os = PortState_OsSave(w);
  if (!s->os.has_value()) {
    why = w;
    return false;
  }
  s->vi = PortState_ViSave(w);
  PortInput_ScriptStateGet(&s->pad);
  s->io = PortState_IoSave();
  s->heap = PortXD_HeapModelSave();
  s->features.clear();
  for (const xd::savestate::FeatureEntry& f : xd::savestate::Features()) s->features.push_back(f.save());
  s->gxf = PortState_GxFifoSave(w);
  if (!s->gxf.has_value()) {
    why = w;
    return false;
  }
  PortTime_GameClockStateGet(s->gameClock);
  s->flip = PortVI_GetFlipCount();
  s->retrace = VIGetRetraceCount();
  s->ai.resize(PortAI_StateSize());
  PortAI_StateGet(s->ai.data());
  PortAI_FileGet(s->aiFile);
  s->haveDsp = PortDsp_StateSave(&s->dsp);
  s->gx = forFile ? nullptr : AuroraSaveGXState();
  s->areas = collect_areas(true);
  return true;
}

bool take(int number, const char* file = nullptr) {
  std::string why;
  if (file != nullptr) {
    why = file_refusal();
    if (!why.empty()) {
      sLastWhy = why; std::fprintf(stderr, "[savestate] not saved to a file: %s\n", why.c_str());
      return false;
    }
  }
  if (!gather(number, file != nullptr, why)) {
    sLastWhy = why; std::fprintf(stderr, "[savestate] not saved: %s\n", why.c_str());
    return false;
  }
  Slot* s = sSlots[number];
  size_t total = 0;
  for (const Area& a : s->areas) total += a.bytes.size();
  const int r = take_into(s);
  if (r == 0) {
    s->valid = true;
    size_t stacks = 0;
    for (const auto& p : s->gs.stacks) stacks += p.bytes.size();
    std::fprintf(stderr,
                 "[savestate] saved slot %d at game frame %u (retrace %u): %.1f MB of memory, %zu tasks, %.1f KB of stacks%s\n",
                 number, s->flip, s->retrace, total / 1048576.0, s->gs.tasks, stacks / 1024.0,
                 s->gs.inTask ? " (inside a task)" : "");
    std::fprintf(stderr, "[savestate] gfx at the save: %s (a copy is in flight when xfb is not 0)\n", gfx_note().c_str());
    if (file != nullptr && !write_file(s, file, why)) {
      sLastWhy = why; std::fprintf(stderr, "[savestate] not saved to a file: %s\n", why.c_str());
      return false;
    }
    if (file != nullptr) {
      s->areas.clear();
      s->areas.shrink_to_fit();
      s->valid = false;
    }
    return true;
  }
  sJustRestored = true;
  std::fprintf(stderr, "[savestate] continuing from slot %d (game frame %u, retrace %u)\n", number, PortVI_GetFlipCount(),
               VIGetRetraceCount());
  return true;
}

void apply(Slot* s, int number) {
  PortGX_DrainForState();
  PortDsp_Join();
  portstate::Why why;
  if (!PortState_IoIdle(why) || !PortState_OsCheck(s->os, why) || !PortState_ViCheck(s->vi, why)) {
    sLastWhy = why; std::fprintf(stderr, "[savestate] not loaded: %s\n", why.c_str());
    return;
  }
  if (s->fileMode) {
    {
      uint8_t* lowBase = nullptr;
      size_t lowUsed = 0;
      PortMem_LowRange(&lowBase, &lowUsed);
      uint8_t* hi = nullptr;
      for (const Area& a : s->areas)
        if (a.name != nullptr && (std::strcmp(a.name, "low") == 0 || std::strcmp(a.name, "low2") == 0 || std::strcmp(a.name, "lowend") == 0)) hi = std::max(hi, a.address + a.bytes.size());
      if (hi != nullptr && lowBase != nullptr && !PortMem_LowEnsureUsed(static_cast<size_t>(hi - lowBase))) {
        sLastWhy = "this process already uses more of the game's memory than the saved one did";
        std::fprintf(stderr, "[savestate] not loaded: this process already uses more of the port's game memory than the saved one did (saved %zu bytes, here %zu)\n",
                     static_cast<size_t>(hi - lowBase), lowUsed);
        for (const Area& a : s->areas) std::fprintf(stderr, "[savestate]   saved area %s at %p size %zu\n", a.name, static_cast<void*>(a.address), a.bytes.size());
        return;
      }
    }
    for (const Area& a : s->areas) {
      uint8_t* p = a.address;
      uint8_t* end = a.address + a.bytes.size();
      while (p < end) {
        bool writable = false;
        const uint8_t* regionLimit = nullptr;
        if (!region_at(p, &writable, &regionLimit) || !writable) {
          sLastWhy = "the game is not at the same stage of start-up as when the state was saved";
          std::fprintf(stderr, "[savestate] not loaded: the game's memory part '%s' at %p is not writable in this process (the game is not at the same stage of start-up)\n",
                       a.name, static_cast<void*>(p));
          return;
        }
        p = std::min(end, const_cast<uint8_t*>(regionLimit));
      }
    }
  }
  std::fprintf(stderr, "[savestate] loading slot %d (game frame %u) at game frame %u\n", number, s->flip, PortVI_GetFlipCount());
  for (const Area& a : s->areas) {
    if (a.bytes.empty()) continue;
    bool writable = false;
    const uint8_t* regionLimit = nullptr;
    if (!region_at(a.address, &writable, &regionLimit) || !writable) {
      continue;
    }
    std::memcpy(a.address, a.bytes.data(), a.bytes.size());
  }
  std::fprintf(stderr, "[savestate] gfx after the memory is put back: %s\n", gfx_note().c_str());
  PortState_OsLoad(s->os);
  PortTime_GameClockStateSet(s->gameClock);
  PortState_ViLoad(s->vi);
  PortState_GxFifoLoad(s->gxf);
  PortInput_ScriptStateSet(&s->pad);
  PortState_IoLoad(s->io);
  PortXD_HeapModelLoad(s->heap);
  {
    const auto& list = xd::savestate::Features();
    for (size_t i = 0; i < list.size() && i < s->features.size(); i++) list[i].load(s->features[i]);
  }
  if (s->fileMode) {
    PortAI_FileSet(s->aiFile);
    if (s->haveDsp) {
      portstate::In din(s->dspBytes.data(), s->dspBytes.size());
      if (!PortDsp_StateLoadBytes(din)) std::fprintf(stderr, "[savestate] the sound chip's state could not be put back\n");
    }
  } else {
    PortAI_StateSet(s->ai.data());
    if (s->haveDsp) PortDsp_StateLoad(&s->dsp);
    if (s->gx != nullptr) {
      AuroraRestoreGXState(s->gx);
      s->gx = AuroraSaveGXState();
    }
  }
  if (std::getenv("XD_SAVESTATE_WATCH") != nullptr) PortGX_TraceBreakpoints(1);
  if (std::getenv("XD_SAVESTATE_WATCH") != nullptr) {
    std::thread([] {
      for (int i = 0; i < 10; i++) {
        sleep_ms(3000);
        char gx[512] = {}, os[1024] = {};
        PortGX_DebugState(gx, sizeof gx);
        PortOS_DebugState(os, sizeof os);
        std::fprintf(stderr, "[savestate] watch %d: gx %s | os %s | %s (flip %u)\n", i, gx, os, gfx_note().c_str(), PortVI_GetFlipCount());
      }
    }).detach();
  }
#ifndef _WIN32
  if (s->fileMode && s->fileCookie != 0) gscoro::StackGuardSet(s->fileCookie);
#endif
  gscoro::Restore(s->gs);
}

void load(int number) {
  auto it = sSlots.find(number);
  if (it == sSlots.end() || !it->second->valid) {
    std::fprintf(stderr, "[savestate] slot %d is empty\n", number);
    return;
  }
  apply(it->second, number);
}

void load_file(const char* path) {
  Slot*& slot = sSlots[-2];
  if (slot == nullptr) slot = new Slot();
  *slot = Slot();
  std::string why;
  if (!read_file(path, slot, why)) {
    sLastWhy = why; std::fprintf(stderr, "[savestate] not loaded: %s\n", why.c_str());
    return;
  }
  slot->valid = true;
  std::fprintf(stderr, "[savestate] file read: %s (frame %u)\n", path, slot->flip);
  apply(slot, -2);
}

extern "C" size_t PortXD_PlaythroughStemCopy(char* out, size_t size);
extern "C" void PortXD_CheatsNoteUsed(const char* name);
extern "C" void PortVideo_Notice(const char* text, double seconds);

std::string player_refusal() {
  if (xd::speedrun::Active()) return Tr("a speedrun timer is running");
  if (xd::online::Active()) return Tr("online play is on");
  return "";
}

std::filesystem::path player_file(int slot, bool createFolder) {
  char stem[256] = {};
  PortXD_PlaythroughStemCopy(stem, sizeof stem);
  if (stem[0] == '\0') return {};
  std::filesystem::path dir = PortUser_Folder("Pokemon XD") / "savestates" / stem;
  if (createFolder) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
  }
  return dir / ("slot" + std::to_string(slot) + ".xds");
}

void say(const std::string& text) {
  std::fprintf(stderr, "[savestate] notice: %s\n", text.c_str());
  PortVideo_Notice(text.c_str(), 3.5);
}

std::string with_reason(const char* head, const std::string& why) { return std::string(head) + ": " + why; }

char sPathBuf[1024];
bool prepare_request(int request) {
  const int slot = request < 0 ? -request : request;
  const bool saving = request > 0;
  std::string why = player_refusal();
  std::filesystem::path file;
  if (why.empty()) file = player_file(slot, saving);
  if (why.empty() && file.empty()) why = Tr("no playthrough is chosen yet");
  if (why.empty() && !saving && !std::filesystem::exists(file)) why = Tr("there is nothing in this slot");
  if (why.empty() && !saving) why = file_refusal();
  if (!why.empty()) {
    say(with_reason(saving ? Tr("State not saved") : Tr("State not loaded"), why));
    return false;
  }
  std::snprintf(sPathBuf, sizeof sPathBuf, "%s", file.string().c_str());
  return true;
}

void after_saved(int slot);

void player_failed(bool saving) {
  say(with_reason(saving ? Tr("State not saved") : Tr("State not loaded"), sLastWhy));
}

void player_request() {
  const int request = sKeyRequest.exchange(0);
  if (request == 0) return;
  if (!prepare_request(request)) return;
  sLastWhy.clear();
  sJustRestored = false;
  sLoadSlot = request < 0 ? -request : request;
  PortXD_CheatsNoteUsed("save states");
  if (request > 0) {
    const bool ok = take(-1, sPathBuf);
    if (sJustRestored) {
      sJustRestored = false;
      char text[96];
      std::snprintf(text, sizeof text, Tr("State %d loaded"), sLoadSlot);
      say(text);
      return;
    }
    if (ok) {
      after_saved(sLoadSlot);
      char text[96];
      std::snprintf(text, sizeof text, Tr("State %d saved"), sLoadSlot);
      say(text);
    } else {
      {
        const std::string leftover = std::string(sPathBuf) + ".tmp";
        std::remove(leftover.c_str());
      }
      player_failed(true);
    }
    return;
  }
  load_file(sPathBuf);
  player_failed(false);
}

std::filesystem::path sidecar(int slot, const char* ext) {
  std::filesystem::path f = player_file(slot, false);
  if (f.empty()) return {};
  f.replace_extension(ext);
  return f;
}

std::string game_text(uint32_t key) {
  const uint16_t* t = key != 0 ? GSmsgGetGSchar(key) : nullptr;
  std::string out;
  for (int n = 0; t != nullptr && n < 40 && t[n] != 0 && t[n] != 0xFFFF; n++) {
    const uint32_t c = t[n];
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
  }
  return out;
}

std::string place_text() {
  if (floordata == nullptr || GSfloorGetCurrent() == 0) return "";
  const bool fight = fightIsFight() != 0;
  const uint32_t map = fight ? (fightFloor_GetEncountFloorId(nullptr) & 0xFFFFu) : static_cast<uint32_t>(GSfloorGetCurrent());
  const std::string place = game_text(floorGetPlaceName(map));
  if (!fight) return place;
  return place.empty() ? std::string(Tr("Battle")) : std::string(Tr("Battle")) + ": " + place;
}

void write_meta(int slot) {
  const std::filesystem::path f = sidecar(slot, ".txt");
  if (f.empty()) return;
  const std::string place = place_text();
  if (FILE* w = open_path(f, true)) {
    std::fprintf(w, "time=%lld\nplace=%s\nframe=%u\n", static_cast<long long>(std::time(nullptr)), place.c_str(), PortVI_GetFlipCount());
    std::fclose(w);
  }
}

int sThumbSlot = 0;
void thumb_arrived(void*, const uint8_t* rgba, uint32_t w, uint32_t h) {
  if (rgba == nullptr || w == 0 || h == 0 || sThumbSlot == 0) return;
  const std::filesystem::path f = sidecar(sThumbSlot, ".png");
  if (f.empty()) return;
  std::string error;
  if (!th::WriteFile(f, th::Shrink(rgba, w, h), error)) std::fprintf(stderr, "[savestate] the slot's picture was not kept: %s\n", error.c_str());
}

void after_saved(int slot) {
  write_meta(slot);
  sThumbSlot = slot;
  xd::picker::Picture kept;
  if (xd::picker::NewestFieldPicture(kept)) {
    const std::filesystem::path f = sidecar(slot, ".png");
    std::string error;
    th::Picture p;
    p.width = kept.width, p.height = kept.height, p.rgba = std::move(kept.rgba);
    if (!f.empty() && th::WriteFile(f, p, error)) return;
  }
  PortShot_GamePicture(thumb_arrived, nullptr);
}

void open_menu() {
  const std::string why = player_refusal();
  if (!why.empty()) {
    say(with_reason(Tr("Save states are not available"), why));
    return;
  }
  if (!PortXD_OpenSaveStatesMenu()) std::fprintf(stderr, "[savestate] the menu could not open now (another page is open)\n");
}

void run_script() {
  const uint32_t flip = PortVI_GetFlipCount();
  for (ScriptEntry& e : sScript) {
    if (e.done || flip < e.flip) continue;
    e.done = true;
    if (e.what == "save") take(e.slot);
    else if (e.what == "load") load(e.slot);
    else if (e.what == "savefile" || e.what == "loadfile") {
      const char* path = std::getenv("XD_SAVESTATE_FILE");
      if (path == nullptr || path[0] == '\0') {
        std::fprintf(stderr, "[savestate] %s: XD_SAVESTATE_FILE is not set\n", e.what.c_str());
      } else if (e.what == "savefile") {
        take(-1, path);
      } else {
        load_file(path);
      }
    }
    else if (e.what == "keysave") sKeyRequest = e.slot;
    else if (e.what == "keyload") sKeyRequest = -e.slot;
    else if (e.what == "menu") open_menu();
    else if (e.what == "speed") PortTime_SetSpeed(static_cast<float>(e.slot));
    else if (e.what == "report") PortOS_ThreadReport();
    return;
  }
}

}

namespace xd::savestate_ui {
SlotInfo Info(int slot) {
  SlotInfo info;
  const std::filesystem::path state = player_file(slot, false);
  std::error_code ec;
  if (state.empty() || !std::filesystem::exists(state, ec)) return info;
  info.used = true;
  info.picture = sidecar(slot, ".png");
  long long when = 0;
  if (std::FILE* r = open_path(sidecar(slot, ".txt"), false)) {
    char line[600];
    while (std::fgets(line, sizeof line, r) != nullptr) {
      std::string l = line;
      while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
      if (l.compare(0, 5, "time=") == 0) when = std::atoll(l.c_str() + 5);
      else if (l.compare(0, 6, "place=") == 0) info.place = l.substr(6);
    }
    std::fclose(r);
  }
  if (when == 0) {
#ifdef _WIN32
    const auto t = std::filesystem::last_write_time(state, ec);
    if (!ec) when = std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count() - 11644473600ll;
#else
    struct stat st{};
    if (::stat(state.c_str(), &st) == 0) when = static_cast<long long>(st.st_mtime);
#endif
  }
  if (when > 0) {
    const std::time_t tt = static_cast<std::time_t>(when);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char text[64];
    std::strftime(text, sizeof text, "%Y-%m-%d  %H:%M", &tmv);
    info.when = text;
  }
  return info;
}

void Request(int signedSlot) { sKeyRequest = signedSlot; }

std::string Unavailable() { return player_refusal(); }
bool Offered() { return sPlayer && !xd::speedrun::Active() && !xd::online::Active(); }
}

extern "C" void PortXD_SavestateInit(void) {
  const char* on = std::getenv("XD_SAVESTATES");
  bool player = false;
  if (on == nullptr || on[0] != '1') {
    const char* orig = std::getenv("XD_REQUIRE_ORIGINAL");
    const char* rep = std::getenv("XD_REPLAY");
    const char* brep = std::getenv("XD_BATTLE_REPLAY");
    const char* pre = std::getenv("XD_PRESET");
    if ((pre != nullptr && std::strcmp(pre, "original") == 0) || (orig != nullptr && orig[0] == '1') || (rep != nullptr && rep[0] != '\0') || (brep != nullptr && brep[0] != '\0')) return;
    es::Registry& r = es::Global();
    if (r.Find("system.save_states") == es::kNoSetting || !r.Bool("system.save_states").Get()) return;
    player = true;
  }
  if (!gscoro::UseCoroutines(PortMem_LowAlloc)) {
    std::fprintf(stderr, "[savestate] no memory for the task stacks: save states are off\n");
    return;
  }
  sOn = true;
  sPlayer = player;
  if (const char* log = std::getenv("XD_SAVESTATE_LOG"); log != nullptr && log[0] != '\0') {
    sLog = true;
    sLogEvery = static_cast<uint32_t>(std::strtoul(log, nullptr, 10));
    if (sLogEvery == 0) sLogEvery = 30;
  }
  if (const char* script = std::getenv("XD_SAVESTATE_SCRIPT"); script != nullptr) {
    std::string text = script;
    size_t pos = 0;
    while (pos < text.size()) {
      size_t end = text.find(',', pos);
      if (end == std::string::npos) end = text.size();
      const std::string item = text.substr(pos, end - pos);
      pos = end + 1;
      const size_t c1 = item.find(':');
      if (c1 == std::string::npos) continue;
      ScriptEntry e{static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10)), item.substr(c1 + 1), 1, false};
      const size_t c2 = e.what.find(':');
      if (c2 != std::string::npos) {
        e.slot = std::atoi(e.what.c_str() + c2 + 1);
        e.what = e.what.substr(0, c2);
      }
      sScript.push_back(e);
    }
  }
  std::fprintf(stderr, "[savestate] on: game tasks run as coroutines (%zu script entries)\n", sScript.size());
}

extern "C" void PortXD_RunGameMain(void (*fn)(int, char**), int argc, char** argv) {
  if (!sOn) {
    fn(argc, argv);
    return;
  }
  static struct {
    void (*fn)(int, char**);
    int argc;
    char** argv;
  } call;
  call = {fn, argc, argv};
  gscoro::RunOnMainStack([](void*) { call.fn(call.argc, call.argv); }, nullptr);
}

extern "C" void PortXD_SavestateFrame(void) {
  static const char* const stall = std::getenv("XD_TEST_STALL");
  if (stall != nullptr) {
    static const uint32_t every = std::max(1u, static_cast<uint32_t>(std::strtoul(stall, nullptr, 10)));
    static const char* colon = std::strchr(stall, ':');
    static const uint32_t ms = colon != nullptr ? static_cast<uint32_t>(std::strtoul(colon + 1, nullptr, 10)) : 67;
    static uint32_t lastStalled = UINT32_MAX;
    const uint32_t flip = PortVI_GetFlipCount();
    if (flip % every == 0 && flip != lastStalled) {
      lastStalled = flip;
      sleep_ms(ms);
    }
  }
  if (!sOn) return;
  static uint32_t lastLogged = UINT32_MAX;
  if (sLog) {
    const uint32_t flip = PortVI_GetFlipCount();
    static const uint32_t rangeLo = [] {
      const char* r = std::getenv("XD_SAVESTATE_LOG_RANGE");
      return r != nullptr ? static_cast<uint32_t>(std::strtoul(r, nullptr, 10)) : 0u;
    }();
    static const uint32_t rangeHi = [] {
      const char* r = std::getenv("XD_SAVESTATE_LOG_RANGE");
      const char* dash = r != nullptr ? std::strchr(r, '-') : nullptr;
      return dash != nullptr ? static_cast<uint32_t>(std::strtoul(dash + 1, nullptr, 10)) : UINT32_MAX;
    }();
    if (flip >= rangeLo && flip <= rangeHi && flip % sLogEvery == 0 && flip != lastLogged) {
      log_hashes("frame");
    }
    lastLogged = flip;
  }
  if (const char* at = std::getenv("XD_SAVESTATE_DUMP_AT"); at != nullptr) {
    const uint32_t flip = PortVI_GetFlipCount();
    static std::map<uint32_t, int> seen;
    static uint32_t lastDumped = UINT32_MAX;
    std::string list = std::string(",") + at + ",";
    const bool newFrame = flip != lastDumped;
    lastDumped = flip;
    if (newFrame && list.find("," + std::to_string(flip) + ",") != std::string::npos) {
      const char* dir = std::getenv("XD_SAVESTATE_DUMP_DIR");
      int n = 1;
      for (;; n++) {
        std::string probe = std::string(dir != nullptr ? dir : ".") + "\\pass_" + std::to_string(flip) + "_" + std::to_string(n);
        FILE* t = std::fopen(probe.c_str(), "rb");
        if (t != nullptr) { std::fclose(t); continue; }
        if (FILE* w = std::fopen(probe.c_str(), "wb")) std::fclose(w);
        break;
      }
      (void)seen;
      for (const Area& a : collect_areas(true)) {
        std::string name = std::string(dir != nullptr ? dir : ".") + "\\" + a.name + "_" + std::to_string(flip) + "_" + std::to_string(n) + "_" +
                           std::to_string(reinterpret_cast<uintptr_t>(a.address) & 0xFFFFFFFFu) + ".bin";
        if (FILE* f = std::fopen(name.c_str(), "wb")) {
          std::fwrite(a.bytes.data(), 1, a.bytes.size(), f);
          std::fclose(f);
        }
      }
    }
  }
  if (static const bool fifoLog = std::getenv("XD_SAVESTATE_FIFO_LOG") != nullptr; fifoLog) {
    char buf[400];
    PortGX_DebugState(buf, sizeof buf);
    std::fprintf(stderr, "[fifo] frame %u retrace %u %s\n", PortVI_GetFlipCount(), VIGetRetraceCount(), buf);
    char os[1500];
    PortOS_DebugState(os, sizeof os);
    std::fprintf(stderr, "[osstate] frame %u %s\n", PortVI_GetFlipCount(), os);
  }
  if (static const char* dsc = std::getenv("XD_SAVESTATE_FIFO_DESCRIBE"); dsc != nullptr) {
    static const uint32_t lo = static_cast<uint32_t>(std::strtoul(dsc, nullptr, 10));
    static const uint32_t hi = std::strchr(dsc, '-') ? static_cast<uint32_t>(std::strtoul(std::strchr(dsc, '-') + 1, nullptr, 10)) : lo;
    const uint32_t f = PortVI_GetFlipCount();
    PortGX_TraceOrigins(f >= lo && f <= hi ? PortCrash_DescribeAddress : nullptr);
  }
#ifdef _WIN32
  if (static const char* hs = std::getenv("XD_SAVESTATE_HOOK_STACK"); hs != nullptr) {
    static const uint32_t lo = static_cast<uint32_t>(std::strtoul(hs, nullptr, 10));
    static const uint32_t hi = std::strchr(hs, '-') ? static_cast<uint32_t>(std::strtoul(std::strchr(hs, '-') + 1, nullptr, 10)) : lo;
    const uint32_t f = PortVI_GetFlipCount();
    if (f >= lo && f <= hi) {
      void* frames[14];
      const USHORT n = RtlCaptureStackBackTrace(0, 14, frames, nullptr);
      std::string text;
      for (USHORT i = 0; i < n; i++) {
        char where[256];
        PortCrash_DescribeAddress(frames[i], where, sizeof where);
        text += std::string(i ? " <- " : "") + where;
      }
      std::fprintf(stderr, "[hookstack] frame %u: %s\n", f, text.c_str());
    }
  }
  if (static const char* find = std::getenv("XD_SAVESTATE_FIND"); find != nullptr && PortVI_GetFlipCount() >= 7050) {
    static bool done = false;
    if (!done) {
      done = true;
      HANDLE process = GetCurrentProcess();
      SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
      SymInitialize(process, nullptr, TRUE);
      std::string list = find;
      size_t pos = 0;
      while (pos < list.size()) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        const std::string name = list.substr(pos, e - pos);
        pos = e + 1;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        if (SymFromName(process, name.c_str(), sym)) {
          unsigned long long v = 0;
          std::memcpy(&v, reinterpret_cast<void*>(static_cast<uintptr_t>(sym->Address)), 8);
          std::fprintf(stderr, "[savestate-find] %s at %llx = %llx\n", name.c_str(), static_cast<unsigned long long>(sym->Address), v);
        } else {
          std::fprintf(stderr, "[savestate-find] %s not found\n", name.c_str());
        }
      }
    }
  }
  if (static const bool fm = std::getenv("XD_SAVESTATE_FLOORLOG") != nullptr; fm) {
    static const void* const* pfm = [] {
      HANDLE process = GetCurrentProcess();
      SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
      SymInitialize(process, nullptr, TRUE);
      alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      return SymFromName(process, "_floorManager", sym) ? reinterpret_cast<const void* const*>(static_cast<uintptr_t>(sym->Address)) : nullptr;
    }();
    if (pfm != nullptr && *pfm != nullptr) {
      const uint8_t* m = static_cast<const uint8_t*>(*pfm);
      uint64_t list = 0;
      std::memcpy(&list, m + 16, 8);
      const uint8_t* l = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(list));
      if (l != nullptr) {
        uint64_t base = 0;
        std::memcpy(&base, l, 8);
        int32_t depth = 0;
        std::memcpy(&depth, l + 8, 4);
        std::string states;
        for (int i = 0; i <= depth && i < 4; i++) {
          const uint8_t* st = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(base)) + static_cast<size_t>(i) * 568;
          uint32_t us = 0;
          std::memcpy(&us, st + 560, 4);
          states += " s" + std::to_string(i) + "=" + std::to_string(us) + "/push" + std::to_string(st[20]) + "pop" + std::to_string(st[21]);
        }
        std::fprintf(stderr, "[floor] frame %u base %llx task %u next %u changing %u depth %d%s\n", PortVI_GetFlipCount(), static_cast<unsigned long long>(base), *reinterpret_cast<const uint32_t*>(m + 24),
                     *reinterpret_cast<const uint32_t*>(m + 28), static_cast<unsigned>(m[36]), depth, states.c_str());
      }
    }
  }
  if (static bool named = false; !named) {
    named = true;
    if (const char* list = std::getenv("XD_SAVESTATE_NAME"); list != nullptr && list[0] != '\0') {
      HANDLE process = GetCurrentProcess();
      SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
      SymInitialize(process, nullptr, TRUE);
      for (const char* q = list; *q != '\0';) {
        char* end = nullptr;
        const unsigned long long addr = std::strtoull(q, &end, 16);
        if (end == q) break;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 offset = 0;
        const bool found = SymFromAddr(process, addr, &offset, sym) != FALSE;
        std::fprintf(stderr, "[savestate-name] %llx = %s+0x%llx (size %lu)\n", addr, found ? sym->Name : "?",
                     static_cast<unsigned long long>(offset), found ? sym->Size : 0ul);
        q = *end == ',' ? end + 1 : end;
      }
    }
  }
#endif
  if (sPlayer) {
    if (PortSpeedups_SaveStatesPresses() > 0) open_menu();
    player_request();
  }
  run_script();
}

#endif
