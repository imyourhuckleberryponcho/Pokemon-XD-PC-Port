#include <dolphin/pad.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <port_seh.h>
#else
#include <port_win_compat.h>
#include <unwind.h>
#ifdef __APPLE__
#include <dlfcn.h>
#endif
#define _ReturnAddress() __builtin_return_address(0)
extern "C" void PortCrash_DescribeAddress(const void* address, char* out, size_t outSize);
#endif

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>
#ifdef _WIN32
#pragma comment(lib, "dbghelp.lib")
#endif

extern "C" {
struct PortAnchorEvent {
  uint32_t kind, id;
};
struct PortAnchorState {
  uint32_t seed, floor;
  uint64_t party, progress;
};
void PortInput_NoteRead(void);
int PortInput_Tick(const PortAnchorEvent* events, int count, const PortAnchorState* state, PADStatus force[4],
                   int* restoreSeed, uint32_t* seed);
bool PortTime_IsDeterministic(void);
const void* PortXD_FindVariable(const char* path);
uint32_t PortXD_SaveWriteCount(void);

u32 GSinputButtonsPressed(s32 pad);
u32 GSinputButtonsChanged(s32 pad);
u32 GSinputButtonsRepeat(s32 pad);
u32 GSinputGetLeftStickXData(s32 pad, s32 mode);
u32 GSinputGetLeftStickYData(s32 pad, s32 mode);
u32 GSinputGetRightStickXData(s32 pad, s32 mode);
u32 GSinputGetRightStickYData(s32 pad, s32 mode);
void* windowGetKeyInfo(void);
void* windowGetKeyInfo2(void);
u32 windowGetKeyRepeat(void);
u16 windowGetKeyOn(void);
u32 windowGetKeyTrg(void);
void* windowGetPortKeyInfo(u8 ports);
void* windowGetPortKeyInfo2(u8 ports);
void menuGetKeyInfo(void* out, s32 port);
void GSinputReadStart(void);
u32 msgctrlKeyWait(void* ctrl);
s32 menuFightOpenTop(void* trainer, u8* work, s32* cursor, s32 d);

s32 floorGetCurrent(void);
int floorGetFloorState(void);
s8 fadeCheck(bool wait);
u32 fadeGetType(void);
s32 windowGetActiveID(void);
u8 fightIsFight(void);
void* savedataGetStatus(void* save, s32 kind);
void* heroBiosGetPokemonPtr(void* hero, u16 index);
uintptr_t gamedatasaveGetStatus(void* gamedata, u32 kind);
extern u32* __hsd_rand_seed_hold;
}

namespace {

enum Kind : uint32_t { kFloor = 1, kMessage = 2, kBattle = 3, kMenu = 4, kFade = 5, kMovie = 6, kSave = 7 };

volatile bool sMessageWaiting = false;
volatile uint32_t sBattleMenus = 0;

int SafeFloorState() {
  PORT_SEH_TRY {
    return floorGetFloorState();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
int32_t SafeFloor() {
  PORT_SEH_TRY {
    return floorGetCurrent();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
int64_t SafeSaveCount() {
  PORT_SEH_TRY {
    void* game = savedataGetStatus(nullptr, 1);
    return game != nullptr ? static_cast<int64_t>(static_cast<uint32_t>(gamedatasaveGetStatus(game, 4))) : -1;
  } PORT_SEH_EXCEPT {
    return -1;
  }
}

uint64_t Fnv(uint64_t h, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull;

void DumpBytes(const char* what, const void* data, size_t n) {
  static const char* dir = std::getenv("XD_ANCHOR_DUMP");
  if (dir == nullptr || dir[0] == '\0') return;
  static unsigned counter[2] = {};
  unsigned& c = counter[what[0] == 'p' ? 0 : 1];
  char path[512];
  std::snprintf(path, sizeof path, "%s\\%s_%03u.bin", dir, what, c++);
  if (FILE* f = std::fopen(path, "wb")) {
    std::fwrite(data, 1, n, f);
    std::fclose(f);
  }
}

uint64_t SafeParty() {
  PORT_SEH_TRY {
    void* hero = savedataGetStatus(nullptr, 2);
    if (hero == nullptr) return 0;
    const auto* p0 = static_cast<const uint8_t*>(heroBiosGetPokemonPtr(hero, 0));
    const auto* p1 = static_cast<const uint8_t*>(heroBiosGetPokemonPtr(hero, 1));
    if (p0 == nullptr || p1 <= p0 || p1 - p0 > 0x1000) return 0;
    static uint8_t copy[0x6000];
    const size_t stride = static_cast<size_t>(p1 - p0);
    std::memcpy(copy, p0, stride * 6);
    for (size_t i = 0; i < 6 && stride >= 0x34; i++) std::memset(copy + i * stride + 0x30, 0, 4);
    DumpBytes("party", copy, stride * 6);
    return Fnv(kFnvBasis, copy, stride * 6);
  } PORT_SEH_EXCEPT {
    return 0;
  }
}

uint64_t SafeProgress() {
  PORT_SEH_TRY {
    uint64_t h = kFnvBasis;
    for (s32 list = 4; list <= 6; list++) {
      const void* data = savedataGetStatus(nullptr, list);
      const uintptr_t words = reinterpret_cast<uintptr_t>(savedataGetStatus(nullptr, list + 3));
      if (data != nullptr && words > 0 && words <= 0x1000) {
        DumpBytes("flags", data, words * 4);
        h = Fnv(h, data, words * 4);
      }
    }
    return h;
  } PORT_SEH_EXCEPT {
    return 0;
  }
}

s32 SafeActiveWindow() {
  PORT_SEH_TRY {
    return windowGetActiveID();
  } PORT_SEH_EXCEPT {
    return 0;
  }
}
int SafeFade(uint32_t* type) {
  PORT_SEH_TRY {
    const int pending = fadeCheck(false);
    *type = fadeGetType();
    return pending;
  } PORT_SEH_EXCEPT {
    return -1;
  }
}
int SafeFight() {
  PORT_SEH_TRY {
    return fightIsFight();
  } PORT_SEH_EXCEPT {
    return -1;
  }
}

uint32_t sTickNumber = 0;
struct ReaderTally {
  std::mutex lock;
  std::map<uintptr_t, std::pair<uint32_t, uint32_t>> ticks;
};
ReaderTally* sTally = nullptr;
void PrintTally() {
#ifdef _WIN32
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  SymInitialize(process, nullptr, TRUE);
#endif
  std::vector<std::pair<uint32_t, uintptr_t>> rows;
  for (const auto& [at, t] : sTally->ticks) rows.push_back({t.first, at});
  std::sort(rows.rbegin(), rows.rend());
  std::fprintf(stderr, "[input ticks] readers of the controls (ticks read in, of %u):\n", sTickNumber);
  for (const auto& [n, at] : rows) {
#ifndef _WIN32
    char where[512] = "?";
    PortCrash_DescribeAddress(reinterpret_cast<const void*>(at), where, sizeof where);
    std::fprintf(stderr, "[input ticks]   %7u  %s\n", n, where);
    continue;
#else
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 off = 0;
    const bool ok = SymFromAddr(process, at, &off, sym) != FALSE;
    std::fprintf(stderr, "[input ticks]   %7u  %s+0x%llx\n", n, ok ? sym->Name : "?", (unsigned long long)off);
#endif
  }
}
void NoteRead(void* caller) {
  PortInput_NoteRead();
  static const bool tally = [] {
    const char* e = std::getenv("XD_TICK_READERS");
    return e != nullptr && e[0] == '1';
  }();
  if (!tally) return;
  if (sTally == nullptr) {
    sTally = new ReaderTally;
    std::atexit(PrintTally);
  }
  std::lock_guard<std::mutex> guard(sTally->lock);
  auto& t = sTally->ticks[reinterpret_cast<uintptr_t>(caller)];
  if (t.second != sTickNumber + 1) {
    t.first++;
    t.second = sTickNumber + 1;
  }
}

struct Seen {
  int floorState = -2;
  int32_t floor = -1;
  bool message = false;
  uint32_t messageGap = 0;
  s32 menu = 0;
  int fade = 0;
  int fight = 0;
  int64_t saves = -1;
  uint32_t saveWrites = 0;
} sSeen;

int CollectEvents(PortAnchorEvent* out) {
  int n = 0;
  const int state = SafeFloorState();
  const int32_t floor = SafeFloor();
  if (state == 5 && floor >= 0 && (sSeen.floorState != 5 || floor != sSeen.floor)) {
    out[n++] = {kFloor, static_cast<uint32_t>(floor)};
  }
  sSeen.floorState = state;
  sSeen.floor = floor;

  constexpr uint32_t kMessageGap = 8;
  const bool waiting = sMessageWaiting;
  sMessageWaiting = false;
  static const bool messages = [] {
    const char* e = std::getenv("XD_ANCHOR_MESSAGES");
    return e != nullptr && e[0] == '1';
  }();
  if (waiting) {
    if (!sSeen.message && messages) out[n++] = {kMessage, 0};
    sSeen.message = true;
    sSeen.messageGap = 0;
  } else if (sSeen.message && ++sSeen.messageGap >= kMessageGap) {
    sSeen.message = false;
  }

  const s32 menu = SafeActiveWindow();
  if (menu != sSeen.menu && menu != 0) {
    out[n++] = {kMenu, static_cast<uint32_t>(menu)};
  }
  sSeen.menu = menu;

  uint32_t fadeType = 0;
  const int fade = SafeFade(&fadeType);
  if (sSeen.fade == 1 && fade == 0) {
    out[n++] = {kFade, fadeType};
  }
  sSeen.fade = fade;

  for (; sBattleMenus > 0 && n < 12; sBattleMenus--) out[n++] = {kBattle, 0x100};

  const int fight = SafeFight();
  if (fight >= 0 && sSeen.fight >= 0 && fight != sSeen.fight) {
    out[n++] = {kBattle, static_cast<uint32_t>(fight)};
  }
  sSeen.fight = fight;

  const int64_t saves = SafeSaveCount();
  const uint32_t writes = PortXD_SaveWriteCount();
  if (saves != sSeen.saves) {
    if (sSeen.saves >= 0 && saves >= 0 && writes != sSeen.saveWrites) {
      out[n++] = {kSave, static_cast<uint32_t>(saves)};
    }
    sSeen.saveWrites = writes;
  }
  sSeen.saves = saves;
  return n;
}

struct InputLayer {
  bool tried = false, found = false;
  uint8_t* base = nullptr;
  size_t stride = 0, raw = 0;
} sLayer;

void FindInputLayer() {
  sLayer.tried = true;
  const auto* d0 = static_cast<const uint8_t*>(PortXD_FindVariable("_inputData[0]"));
  const auto* d1 = static_cast<const uint8_t*>(PortXD_FindVariable("_inputData[1]"));
  const auto* r0 = static_cast<const uint8_t*>(PortXD_FindVariable("_inputData[0].raw"));
  if (d0 == nullptr || d1 <= d0 || r0 < d0 || r0 >= d1) {
    std::fprintf(stderr, "[input ticks] the game's input layer (_inputData) was not found in xd.pdb: a replay feeds "
                         "the controls one tick late\n");
    return;
  }
  sLayer.base = const_cast<uint8_t*>(d0);
  sLayer.stride = static_cast<size_t>(d1 - d0);
  sLayer.raw = static_cast<size_t>(r0 - d0);
  sLayer.found = true;
}

void ForceRaw(const PADStatus force[4]) {
  if (!sLayer.tried) FindInputLayer();
  if (!sLayer.found) return;
  for (int slot = 0; slot < 4; slot++) {
    uint8_t* device = sLayer.base + slot * sLayer.stride;
    s32 id = 0;
    std::memcpy(&id, device, sizeof id);
    if (id < 1 || id > 4) continue;
    PADStatus s = force[id - 1];
    if (s.err == PAD_ERR_TRANSFER) continue;
    s.stickY = static_cast<s8>(-s.stickY);
    s.substickY = static_cast<s8>(-s.substickY);
    std::memcpy(device + sLayer.raw, &s, sizeof s);
  }
}

}

extern "C" {

void PortTick_GSinputReadStart(void) {
  PortAnchorEvent events[16];
  sTickNumber++;
  if (sTally != nullptr && sTickNumber % 3000 == 0) PrintTally();
  const int count = CollectEvents(events);
  PortAnchorState state{};
  if (count > 0) {
    state.seed = __hsd_rand_seed_hold != nullptr ? *__hsd_rand_seed_hold : 0;
    state.floor = static_cast<uint32_t>(SafeFloor());
    state.party = SafeParty();
    state.progress = SafeProgress();
  }
  PADStatus force[4] = {};
  int restore = 0;
  uint32_t seed = 0;
  if (PortInput_Tick(events, count, &state, force, &restore, &seed)) {
    ForceRaw(force);
  }
  if (restore && __hsd_rand_seed_hold != nullptr) {
    *__hsd_rand_seed_hold = seed;
  }
  GSinputReadStart();
}

s32 PortTick_menuFightOpenTop(void* trainer, u8* work, s32* cursor, s32 d) {
  sBattleMenus = sBattleMenus + 1;
  return menuFightOpenTop(trainer, work, cursor, d);
}

u32 PortTick_msgctrlKeyWait(void* ctrl) {
  sMessageWaiting = true;
  return msgctrlKeyWait(ctrl);
}

u32 PortTick_GSinputButtonsPressed(s32 pad) {
  NoteRead(_ReturnAddress());
  return GSinputButtonsPressed(pad);
}
u32 PortTick_GSinputButtonsChanged(s32 pad) {
  NoteRead(_ReturnAddress());
  return GSinputButtonsChanged(pad);
}
u32 PortTick_GSinputButtonsRepeat(s32 pad) {
  NoteRead(_ReturnAddress());
  return GSinputButtonsRepeat(pad);
}
u32 PortTick_GSinputGetLeftStickXData(s32 pad, s32 mode) {
  NoteRead(_ReturnAddress());
  return GSinputGetLeftStickXData(pad, mode);
}
u32 PortTick_GSinputGetLeftStickYData(s32 pad, s32 mode) {
  NoteRead(_ReturnAddress());
  return GSinputGetLeftStickYData(pad, mode);
}
u32 PortTick_GSinputGetRightStickXData(s32 pad, s32 mode) {
  NoteRead(_ReturnAddress());
  return GSinputGetRightStickXData(pad, mode);
}
u32 PortTick_GSinputGetRightStickYData(s32 pad, s32 mode) {
  NoteRead(_ReturnAddress());
  return GSinputGetRightStickYData(pad, mode);
}
void* PortTick_windowGetKeyInfo(void) {
  NoteRead(_ReturnAddress());
  return windowGetKeyInfo();
}
void menuDaemon(void);
void* PortTick_menu_windowGetKeyInfo(void) {
  void* const caller = _ReturnAddress();
#ifdef _WIN32
  DWORD64 image = 0;
  const RUNTIME_FUNCTION* fn = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(caller), &image, nullptr);
  const bool daemon = fn != nullptr && image + fn->BeginAddress == reinterpret_cast<DWORD64>(&menuDaemon);
#elif defined(__APPLE__)
  void* const inside = static_cast<char*>(caller) - 1;
  void* start = _Unwind_FindEnclosingFunction(inside);
  const bool fromUnwind = start != nullptr;
  if (!fromUnwind) {
    Dl_info info{};
    if (dladdr(inside, &info) != 0) start = info.dli_saddr;
  }
  static bool said = false;
  if (!said) {
    said = true;
    std::fprintf(stderr, "[input ticks] mac: the caller's function start comes from %s (%p for a call at %p)\n",
                 fromUnwind ? "the unwind tables" : start != nullptr ? "the symbol table (dladdr)" : "NOWHERE",
                 start, caller);
  }
  const bool daemon = start == reinterpret_cast<void*>(&menuDaemon);
#else
  const bool daemon = _Unwind_FindEnclosingFunction(static_cast<char*>(caller) - 1) == reinterpret_cast<void*>(&menuDaemon);
#endif
  if (!daemon) NoteRead(caller);
  return windowGetKeyInfo();
}
void* PortTick_windowGetKeyInfo2(void) {
  NoteRead(_ReturnAddress());
  return windowGetKeyInfo2();
}
u32 PortTick_windowGetKeyRepeat(void) {
  NoteRead(_ReturnAddress());
  return windowGetKeyRepeat();
}
u32 PortTick_windowGetKeyOn(void) {
  NoteRead(_ReturnAddress());
  return windowGetKeyOn();
}
u32 PortTick_windowGetKeyTrg(void) {
  NoteRead(_ReturnAddress());
  return windowGetKeyTrg();
}
void* PortTick_windowGetPortKeyInfo(u8 ports) {
  NoteRead(_ReturnAddress());
  return windowGetPortKeyInfo(ports);
}
void* PortTick_windowGetPortKeyInfo2(u8 ports) {
  NoteRead(_ReturnAddress());
  return windowGetPortKeyInfo2(ports);
}
void PortTick_menuGetKeyInfo(void* out, s32 port) {
  NoteRead(_ReturnAddress());
  menuGetKeyInfo(out, port);
}

}
