#include "input_anchors.h"
#include "input_record_format.h"

#include <port_user_folder.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#define _ftelli64 ftello
#endif
#include <sstream>
#include <string>
#include <vector>

#include <dolphin/pad.h>
#include <dolphin/si.h>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
int64_t OSGetTime(void);
int64_t PortTime_Now(void);
bool PortTime_IsDeterministic(void);
bool PortFrameCost_Active(void);
bool PortCpuCost_Enabled(void);
void PortTime_SetGameReadHook(int64_t (*hook)(int64_t live, const void* caller));
void PortFrameCost_SetPacer(bool (*pacer)(uint32_t frame, int64_t* field));
void PortFrameCost_Rebase(void);
int64_t PortOS_BusyFor(int64_t ticks);
bool PortFrameCost_PaceBase(int64_t* base);
uint32_t VIGetRetraceCount(void);
void PortFrameCost_PacingStats(uint64_t* paced, uint64_t* pacedFields, uint64_t* behind, uint64_t* behindFields);
#ifdef _WIN32
extern char __ImageBase;
#elif defined(__APPLE__)
extern char port_mac_image_base __asm("__mh_execute_header");
#define __ImageBase port_mac_image_base
#else
extern char __executable_start;
#define __ImageBase __executable_start
#endif
struct PortIrqNote {
  uint64_t count;
  uint32_t site, seq;
  char kind;
};
struct PortIrqSiteDecl {
  uint64_t rva;
  int alarm;
};
size_t PortOS_TakeIrqNotes(PortIrqNote* out, size_t max);
int PortOS_IrqSiteInfo(uint32_t site, uint64_t* rva, int* alarm);
int PortOS_SetIrqSchedule(const PortIrqSiteDecl* sites, size_t siteCount, const PortIrqNote* notes, size_t count);
int PortOS_IrqScheduleActive(int* failed);
uint64_t PortOS_IrqScheduleDelivered(void);
}

namespace {

using namespace port_input_record;
namespace fs = std::filesystem;

constexpr size_t kKeepRecordings = 20;
constexpr uint32_t kReplayReportEvery = 600;

std::mutex sLock;
FILE* sFile = nullptr;
std::string sPath;
ChangeFilter sFilter;
uint32_t sMarks = 0;

uint32_t sPollFrame = UINT32_MAX, sPoll = 0;

Player* sPlayer = nullptr;
uint32_t sReplayUntil = UINT32_MAX;
std::string sReplayName;
uint32_t sNextReport = 0;

ClockWriter sClockWriter;
ClockPlayer* sClock = nullptr;
uintptr_t sImageBase = 0;

FieldTimeline sTimeline;
std::atomic<bool> sPacing{false};
uint64_t sOrderWaits = 0, sOrderWaitFields = 0;

std::atomic<bool> sScheduled{false};
bool ScheduleActive() { return sScheduled.load(std::memory_order_acquire) && PortOS_IrqScheduleActive(nullptr) != 0; }
IrqWriter sIrqWriter;
uint32_t sIrqSitesWritten = 0;

bool PaceField(uint32_t, int64_t* field) {
  if (!sPacing.load(std::memory_order_acquire)) return false;
  if (ScheduleActive()) return false;
  std::lock_guard<std::mutex> lock(sLock);
  size_t index = 0;
  if (sClock == nullptr || !sClock->NextRead(sTimeline.site, &index) || index >= sTimeline.fields.size()) return false;
  *field = sTimeline.fields[index];
  return true;
}

void StopPacing() {
  if (!sPacing.exchange(false)) return;
  PortFrameCost_SetPacer(nullptr);
  uint64_t paced = 0, pacedFields = 0, behind = 0, behindFields = 0;
  PortFrameCost_PacingStats(&paced, &pacedFields, &behind, &behindFields);
  std::fprintf(stderr,
               "[replay] pacing: %llu frames given %llu extra fields to match the recorded run; %llu frames behind it "
               "(%llu fields); %llu clock reads waited for their frame to be drawn (%llu fields)\n",
               (unsigned long long)paced, (unsigned long long)pacedFields, (unsigned long long)behind,
               (unsigned long long)behindFields, (unsigned long long)sOrderWaits, (unsigned long long)sOrderWaitFields);
}

void StartPacing(const Recording& rec) {
  const char* env = std::getenv("XD_REPLAY_PACING");
  if (env == nullptr || std::strcmp(env, "recorded") != 0) {
    return;
  }
  std::string why;
  if (!BuildFieldTimeline(rec, sTimeline, &why)) {
    std::fprintf(stderr, "[replay]   not paced by the recorded fields: %s\n", why.c_str());
    return;
  }
  if (!PortTime_IsDeterministic()) {
    std::fprintf(stderr, "[replay]   not paced by the recorded fields: needs the virtual clock (XD_DETERMINISTIC)\n");
    return;
  }
  sPacing = true;
  PortFrameCost_SetPacer(PaceField);
  const char* irq = std::getenv("XD_REPLAY_IRQ");
  if (!rec.irq.empty() && !(irq != nullptr && irq[0] == '0')) {
    std::vector<PortIrqSiteDecl> sites;
    for (const IrqSiteLine& l : rec.irqSites) sites.push_back({l.address, l.alarm ? 1 : 0});
    std::vector<PortIrqNote> notes;
    notes.reserve(rec.irq.size());
    for (const IrqEntry& e : rec.irq) notes.push_back({e.count, e.site, e.seq, e.kind});
    if (PortOS_SetIrqSchedule(sites.data(), sites.size(), notes.data(), notes.size()) != 0) {
      sScheduled = true;
      std::fprintf(stderr,
                   "[replay]   exact: %zu recorded interrupts are delivered at the places the recorded run took them "
                   "(the pacing by fields only takes over if that can't be followed)\n",
                   notes.size());
      return;
    }
  }
  std::fprintf(stderr,
               "[replay]   paced by the recorded run's video fields (clock site %u, %zu frames, %d fields): "
               "lag on this PC can't change the game\n",
               sTimeline.site, sTimeline.fields.size(), sTimeline.fields.empty() ? 0 : sTimeline.fields.back());
}

const char* sCheckName = nullptr;
uint32_t (*sCheckValue)(void) = nullptr;
uint32_t sCheckEvery = 1;
void (*sFrameHook)(uint32_t frame) = nullptr;

std::atomic<void (*)(PADStatus* status)> sPadFilter{nullptr};
std::atomic<bool> sLatchPending{false};
struct Latch {
  u16 buttons = 0;
  bool stick = false, substick = false, triggerL = false, triggerR = false;
};
Latch sLatch[kPorts];
constexpr int kStickRest = 0x20;

bool Out(int v, int rest = kStickRest) { return v > rest || v < -rest; }

void ApplyLatch(PADStatus* status) {
  for (int i = 0; i < kPorts; i++) {
    PADStatus& s = status[i];
    Latch& l = sLatch[i];
    if (sLatchPending.load()) {
      l.buttons = s.err == PAD_ERR_NONE ? s.button : 0;
      l.stick = Out(s.stickX) || Out(s.stickY);
      l.substick = Out(s.substickX) || Out(s.substickY);
      l.triggerL = s.triggerLeft > kStickRest;
      l.triggerR = s.triggerRight > kStickRest;
    }
    if (s.err != PAD_ERR_NONE) continue;
    l.buttons &= s.button;
    s.button &= static_cast<u16>(~l.buttons);
    if (l.stick && !(Out(s.stickX) || Out(s.stickY))) l.stick = false;
    if (l.stick) s.stickX = s.stickY = 0;
    if (l.substick && !(Out(s.substickX) || Out(s.substickY))) l.substick = false;
    if (l.substick) s.substickX = s.substickY = 0;
    if (l.triggerL && s.triggerLeft <= kStickRest) l.triggerL = false;
    if (l.triggerL) s.triggerLeft = 0;
    if (l.triggerR && s.triggerRight <= kStickRest) l.triggerR = false;
    if (l.triggerR) s.triggerRight = 0;
  }
  sLatchPending.store(false);
}

std::atomic<uint32_t> sReads{0};
TickWriter sTickWriter;
uint32_t sAnchorCount = 0;
PadState sDelivered[kPorts];
bool sTicking = false;
TickPlayer* sTicks = nullptr;
Recording* sTickRec = nullptr;
bool sReplayExit = false;
uint32_t sSegmentTicks = 0;
constexpr uint32_t kStallTicks = 60 * 120;
uint32_t sMismatches = 0, sMatched = 0;
std::string sFirstMismatch;
std::string sReportPath;
std::string sReplayResult;
std::vector<std::string> sSaveLines;
int sScheduleEnded = 0;
uint32_t sScheduleFailedFrame = 0;
uint64_t sScheduleDelivered = 0;
fs::path sCardRoot;
fs::path sCardsOut;
fs::path sSnapshotsOut;
uint32_t sSaves = 0;

PadState FromStatus(const PADStatus& s) {
  PadState p;
  p.button = s.button;
  p.stickX = s.stickX, p.stickY = s.stickY, p.substickX = s.substickX, p.substickY = s.substickY;
  p.triggerL = s.triggerLeft, p.triggerR = s.triggerRight, p.analogA = s.analogA, p.analogB = s.analogB;
  p.err = s.err;
#ifdef TARGET_PC
  p.extButton = s.extButton;
#endif
  return p;
}

void ToStatus(const PadState& p, PADStatus& s) {
  s.button = p.button;
  s.stickX = p.stickX, s.stickY = p.stickY, s.substickX = p.substickX, s.substickY = p.substickY;
  s.triggerLeft = p.triggerL, s.triggerRight = p.triggerR, s.analogA = p.analogA, s.analogB = p.analogB;
  s.err = p.err;
#ifdef TARGET_PC
  s.extButton = p.extButton;
#endif
}

void WriteLine(const std::string& line) {
  if (sFile != nullptr) {
    std::fputs(line.c_str(), sFile);
    std::fputc('\n', sFile);
    std::fflush(sFile);
  }
}

void WriteIrqNotes() {
  PortIrqNote notes[512];
  IrqEntry entries[512];
  for (;;) {
    const size_t n = PortOS_TakeIrqNotes(notes, 512);
    if (n == 0 || sFile == nullptr) break;
    for (size_t i = 0; i < n; i++) {
      entries[i].count = notes[i].count, entries[i].site = notes[i].site, entries[i].seq = notes[i].seq;
      entries[i].kind = notes[i].kind;
      while (sIrqSitesWritten < notes[i].site) {
        IrqSiteLine site;
        int alarm = 0;
        site.number = ++sIrqSitesWritten;
        PortOS_IrqSiteInfo(site.number, &site.address, &alarm);
        site.alarm = alarm != 0;
        WriteLine(FormatIrqSite(site));
      }
    }
    for (size_t at = 0; at < n; at += 64) {
      WriteLine(sIrqWriter.Line(entries + at, std::min<size_t>(64, n - at)));
    }
    if (n < 512) break;
  }
}

void Prune(const fs::path& folder, const std::string& prefix, size_t keep) {
  std::error_code ec;
  std::vector<fs::path> files;
  const std::wstring wprefix(prefix.begin(), prefix.end());
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const std::wstring name = entry.path().filename().wstring();
    if (entry.is_regular_file(ec) && name.rfind(wprefix, 0) == 0 && entry.path().extension() == L".txt") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (size_t i = 0; i + keep < files.size(); i++) {
    fs::remove(files[i], ec);
    fs::path cards = files[i];
    cards.replace_extension(L".cards");
    fs::remove_all(cards, ec);
  }
}

int CopyCard(const fs::path& dest) {
  std::error_code ec;
  if (sCardRoot.empty()) return -1;
  fs::create_directories(dest, ec);
  int files = 0;
  for (const auto& region : fs::directory_iterator(sCardRoot, ec)) {
    if (!region.is_directory(ec)) continue;
    if (region.path().filename() == L"save_backups") continue;
    for (const wchar_t* slot : {L"Card A", L"Card B"}) {
      const fs::path from = region.path() / slot;
      if (!fs::is_directory(from, ec)) continue;
      const fs::path to = dest / region.path().filename() / slot;
      fs::create_directories(to, ec);
      for (const auto& f : fs::directory_iterator(from, ec)) {
        if (!f.is_regular_file(ec)) continue;
        fs::copy_file(f.path(), to / f.path().filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) return -1;
        files++;
      }
    }
  }
  return files;
}

std::string Utf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

void PrintClockNotes() {
  if (sClock != nullptr) {
    std::istringstream lines(sClock->TakeNote());
    for (std::string line; std::getline(lines, line);) {
      std::fprintf(stderr, "[replay] %s\n", line.c_str());
    }
  }
}

int64_t GameClockRead(int64_t live, const void* caller) {
  const uint64_t address = reinterpret_cast<uintptr_t>(caller) - sImageBase;
  if (sPacing.load(std::memory_order_acquire) && !ScheduleActive()) {
    constexpr int kMaxFields = 600;
    int waited = 0;
    size_t need = 0, have = 0;
    for (; waited < kMaxFields; waited++) {
      {
        std::lock_guard<std::mutex> lock(sLock);
        if (sClock == nullptr || !sClock->OrderWait(address, sTimeline.site, &need, &have)) break;
      }
      int64_t base = 0;
      if (need > 0 && need <= sTimeline.fields.size() && PortFrameCost_PaceBase(&base) &&
          static_cast<int64_t>(VIGetRetraceCount()) > base + sTimeline.fields[need - 1] + 2) {
        break;
      }
      if (PortOS_BusyFor(kFieldTicks) == 0) break;
    }
    if (waited > 0) {
      sOrderWaits++;
      sOrderWaitFields += static_cast<uint64_t>(waited);
    }
    if (waited >= kMaxFields) {
      std::fprintf(stderr, "[replay] pacing: a clock read waited %d fields for frame-drawn read %zu (at %zu); going on\n",
                   waited, need, have);
    }
  }
  const uint32_t frame = PortVI_GetFlipCount();
  std::lock_guard<std::mutex> lock(sLock);
  int64_t value = live;
  if (sClock != nullptr) {
    value = sClock->Read(address, frame, live);
    PrintClockNotes();
  }
  if (sFile != nullptr) {
    std::string declaration;
    const uint32_t site = sClockWriter.Site(address, &declaration);
    if (!declaration.empty()) {
      WriteLine(declaration);
    }
    const std::string done = sClockWriter.Add(frame, site, value);
    if (!done.empty()) {
      WriteLine(done);
    }
  }
  return value;
}

void InstallClockHook() {
  sImageBase = reinterpret_cast<uintptr_t>(&__ImageBase);
  PortTime_SetGameReadHook(GameClockRead);
}

void StopRecording() {
  std::lock_guard<std::mutex> lock(sLock);
  if (sFile != nullptr) {
    WriteIrqNotes();
    const std::string time = sClockWriter.Flush();
    if (!time.empty()) {
      WriteLine(time);
    }
    if (sTicking) {
      const std::string run = sTickWriter.Flush();
      if (!run.empty()) {
        WriteLine(run);
      }
      WriteLine(FormatTickCount(sTickWriter.ticks()));
    }
    WriteLine(FormatEnd(PortVI_GetFlipCount(), VIGetRetraceCount()));
    std::fclose(sFile);
    sFile = nullptr;
  }
}

}

extern "C" {

bool PortInput_StartRecording(const char* appFolder, const char* kind, const char* header) {
  fs::path folder = PortUser_Folder(appFolder) / L"recordings";
  std::error_code ec;
  fs::create_directories(folder, ec);
  const std::string prefix = std::string(kind) + "_";
  Prune(folder, prefix, kKeepRecordings - 1);

  const std::time_t now = std::time(nullptr);
  std::tm local;
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char stamp[32], when[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &local);
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
  const std::string base = prefix + stamp + "_p" + std::to_string(_getpid());
  fs::path path = folder / (base + ".txt");

  std::lock_guard<std::mutex> lock(sLock);
#ifdef _WIN32
  const wchar_t* to = _wgetenv(L"XD_RECORD_TO");
  if (to != nullptr && to[0] != L'\0') {
    path = fs::path(to);
    fs::create_directories(path.parent_path(), ec);
    sFile = _wfopen(path.c_str(), L"w");
  } else {
    sFile = _wfopen(path.c_str(), L"wx");
  }
  for (int n = 2; sFile == nullptr && n < 100 && fs::exists(path, ec) && (to == nullptr || to[0] == L'\0');
       n++) {
    path = folder / (base + "_" + std::to_string(n) + ".txt");
    sFile = _wfopen(path.c_str(), L"wx");
  }
#else
  const char* to = std::getenv("XD_RECORD_TO");
  if (to != nullptr && to[0] != '\0') {
    path = fs::path(to);
    fs::create_directories(path.parent_path(), ec);
    sFile = std::fopen(path.c_str(), "w");
  } else {
    sFile = std::fopen(path.c_str(), "wx");
  }
  for (int n = 2; sFile == nullptr && n < 100 && fs::exists(path, ec) && (to == nullptr || to[0] == '\0'); n++) {
    path = folder / (base + "_" + std::to_string(n) + ".txt");
    sFile = std::fopen(path.c_str(), "wx");
  }
#endif
  if (sFile == nullptr) {
    std::fprintf(stderr, "[input record] could not create %s; this run is not recorded\n", Utf8(path).c_str());
    return false;
  }
  sPath = Utf8(path);
  WriteLine("# Input recording (platform/input/input_record.cpp): what the game read from its controllers,");
  WriteLine("# one line per change, by game frame and controller poll. Replay: set XD_REPLAY=<this file>.");
  WriteLine(std::string("# started ") + when);
  std::istringstream lines(header != nullptr ? header : "");
  for (std::string line; std::getline(lines, line);) {
    WriteLine("# " + line);
  }
  std::atexit(StopRecording);
  InstallClockHook();
  std::fprintf(stderr, "[input record] recording to %s\n", sPath.c_str());
  if (!sCardRoot.empty()) {
    sCardsOut = path;
    sCardsOut.replace_extension(L".cards");
    fs::remove_all(sCardsOut, ec);
    const int files = CopyCard(sCardsOut / L"start-card");
    if (files < 0) {
      std::fprintf(stderr, "[input record] could not copy the memory card at the start\n");
    }
  }
  return true;
}

void PortInput_SetCardRoot(const char* folder) {
  std::lock_guard<std::mutex> lock(sLock);
  sCardRoot = folder != nullptr ? fs::u8path(folder) : fs::path();
}

void PortInput_StopRecording(void) { StopRecording(); }

const char* PortInput_RecordingPath(void) { return sPath.c_str(); }

void PortInput_Mark(void) {
  std::lock_guard<std::mutex> lock(sLock);
  Mark m{PortVI_GetFlipCount(), VIGetRetraceCount(), ++sMarks};
  WriteLine(FormatMark(m));
  std::fprintf(stderr, "[input record] %s%s%s\n", FormatMark(m).c_str(), sFile != nullptr ? " in " : " (not recording)",
               sFile != nullptr ? sPath.c_str() : "");
}

void PortInput_DevMenuNote(const char* action) {
  std::lock_guard<std::mutex> lock(sLock);
  const DevMenuLine d{PortVI_GetFlipCount(), VIGetRetraceCount(), action != nullptr ? action : ""};
  WriteLine(FormatDevMenu(d));
  std::fprintf(stderr, "[input record] %s%s\n", FormatDevMenu(d).c_str(), sFile != nullptr ? "" : " (not recording)");
}

void PortInput_PhotoNote(const char* what) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::string line = FormatPhoto(PortVI_GetFlipCount(), VIGetRetraceCount(), what != nullptr ? what : "");
  WriteLine(line);
  std::fprintf(stderr, "[input record] %s%s\n", line.c_str(), sFile != nullptr ? "" : " (not recording)");
}

void PortInput_EnhancedNote(const char* what) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::string line = FormatEnhanced(PortVI_GetFlipCount(), VIGetRetraceCount(), what != nullptr ? what : "");
  WriteLine(line);
  std::fprintf(stderr, "[input record] %s%s\n", line.c_str(), sFile != nullptr ? "" : " (not recording)");
}

void PortInput_MenuNote(const char* what) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::string line = FormatMenu(PortVI_GetFlipCount(), VIGetRetraceCount(), what != nullptr ? what : "");
  WriteLine(line);
  std::fprintf(stderr, "[input record] %s%s\n", line.c_str(), sFile != nullptr ? "" : " (not recording)");
}

void PortInput_SettingsNote(const char* list) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::string line = FormatSettings(PortVI_GetFlipCount(), VIGetRetraceCount(), list != nullptr ? list : "");
  WriteLine(line);
  std::fprintf(stderr, "[input record] %s%s\n", line.c_str(), sFile != nullptr ? "" : " (not recording)");
}

void PortInput_PlaythroughNote(const char* what) {
  std::lock_guard<std::mutex> lock(sLock);
  const std::string line = FormatPlaythrough(PortVI_GetFlipCount(), VIGetRetraceCount(), what != nullptr ? what : "");
  WriteLine(line);
  std::fprintf(stderr, "[input record] %s%s\n", line.c_str(), sFile != nullptr ? "" : " (not recording)");
}

const char* PortInput_ReplaySettingsAt(size_t index, uint32_t* frame) {
  std::lock_guard<std::mutex> lock(sLock);
  const Recording* rec = sPlayer != nullptr ? &sPlayer->recording() : sTicks != nullptr ? sTickRec : nullptr;
  if (rec == nullptr || index >= rec->settings.size()) {
    return nullptr;
  }
  if (frame != nullptr) {
    *frame = rec->settings[index].frame;
  }
  return rec->settings[index].action.c_str();
}

uint32_t PortInput_ReplayRecordedFrame(void) {
  std::lock_guard<std::mutex> lock(sLock);
  if (sTicks != nullptr) {
    const uint32_t pos = sTicks->position();
    const std::vector<Anchor>& anchors = sTicks->recordedAnchors();
    const uint32_t passed = std::min<uint32_t>(sTicks->segment(), static_cast<uint32_t>(anchors.size()));
    if (passed == 0) return pos > 0 ? pos - 1 : 0;
    const Anchor& a = anchors[passed - 1];
    return a.frame + (pos > a.tick ? pos - a.tick : 0);
  }
  return sPlayer != nullptr ? PortVI_GetFlipCount() : UINT32_MAX;
}

void PortInput_FsysNote(uint32_t pass, uint32_t blocks) {
  std::lock_guard<std::mutex> lock(sLock);
  if (sFile == nullptr) return;
  char line[64];
  std::snprintf(line, sizeof line, "FSYS pass %u blocks %u", pass, blocks);
  WriteLine(line);
}

int PortInput_ReplayFsysBlocks(uint32_t pass) {
  std::lock_guard<std::mutex> lock(sLock);
  const Recording* rec = sPlayer != nullptr ? &sPlayer->recording() : sTicks != nullptr ? sTickRec : nullptr;
  if (rec == nullptr || rec->fsys.empty()) return -1;
  if (ScheduleActive()) return -1;
  for (const auto& [p, n] : rec->fsys) {
    if (p == pass) return static_cast<int>(n);
    if (p > pass) break;
  }
  return 0;
}

bool PortInput_Replaying(void) {
  std::lock_guard<std::mutex> lock(sLock);
  return sPlayer != nullptr || sTicks != nullptr;
}

void PortInput_SetPadFilter(void (*filter)(PADStatus* status)) { sPadFilter.store(filter); }

void PortInput_LatchHeld(void) { sLatchPending.store(true); }

const char* PortInput_ReplayDevMenuAt(size_t index, uint32_t* frame) {
  std::lock_guard<std::mutex> lock(sLock);
  const Recording* rec = sPlayer != nullptr ? &sPlayer->recording() : sTicks != nullptr ? sTickRec : nullptr;
  if (rec == nullptr || index >= rec->devMenu.size()) {
    return nullptr;
  }
  const DevMenuLine& d = rec->devMenu[index];
  if (frame != nullptr) {
    *frame = d.frame;
  }
  return d.action.c_str();
}

uint32_t PortInput_MarkAndCopy(const wchar_t* dest) {
  long long length = 0;
  std::string tail;
  uint32_t number = 0;
  {
    std::lock_guard<std::mutex> lock(sLock);
    if (sFile == nullptr) {
      std::fprintf(stderr, "[input record] not recording: no recording for this report\n");
      return 0;
    }
    const uint32_t frame = PortVI_GetFlipCount(), field = VIGetRetraceCount();
    Mark m{frame, field, ++sMarks};
    WriteLine(FormatMark(m));
    length = _ftelli64(sFile);
    number = m.number;
    if (!sClockWriter.Pending().empty()) {
      tail = sClockWriter.Pending() + "\n";
    }
    if (sTicking) {
      const std::string run = sTickWriter.PendingRun();
      tail += run.empty() ? "" : run + "\n";
      tail += FormatTickCount(sTickWriter.ticks()) + "\n";
    }
    tail += FormatEnd(frame, field) + "\n";
    std::fprintf(stderr, "[input record] %s in %s (bug report)\n", FormatMark(m).c_str(), sPath.c_str());
  }
  if (length <= 0) {
    return 0;
  }
  std::error_code ec;
  const fs::path from = fs::u8path(sPath);
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(fs::path(dest), std::ios::binary | std::ios::trunc);
  std::vector<char> buffer(1 << 16);
  long long left = length;
  while (in && out && left > 0) {
    const std::streamsize chunk = static_cast<std::streamsize>(std::min<long long>(left, buffer.size()));
    in.read(buffer.data(), chunk);
    out.write(buffer.data(), in.gcount());
    left -= in.gcount();
    if (in.gcount() == 0) break;
  }
  out << "# copied for a bug report: the recording up to mark #" << number << ", ending there\n" << tail;
  out.close();
  return left == 0 && !out.fail() ? number : 0;
}

bool PortInput_StartReplay(const char* path, uint32_t untilFrame) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "[replay] could not open %s\n", path);
    return false;
  }
  std::stringstream text;
  text << in.rdbuf();
  std::string problems;
  Recording rec = ParseRecording(text.str(), &problems);
  if (!problems.empty()) {
    std::fprintf(stderr, "[replay] %s", problems.c_str());
  }
  if (rec.changes.empty()) {
    std::fprintf(stderr, "[replay] %s has no controller lines; nothing to replay\n", path);
    return false;
  }
  const uint32_t last = rec.hasEnd ? rec.endFrame
                                   : std::max(rec.changes.back().when.frame,
                                              rec.clockReads.empty() ? 0u : rec.clockReads.back().frame);
  std::fprintf(stderr, "[replay] %s: %zu changes, %zu marks, up to game frame %u (%s)\n", path, rec.changes.size(),
               rec.marks.size(), last,
               rec.hasEnd ? "END line" : "last line; no END line, the run crashed or was killed");
  for (const std::string& line : rec.header) {
    if (line.rfind("Input recording", 0) != 0 && line.rfind("one line per change", 0) != 0) {
      std::fprintf(stderr, "[replay]   recorded: %s\n", line.c_str());
    }
  }
  for (const Mark& m : rec.marks) {
    std::fprintf(stderr, "[replay]   mark #%u at game frame %u (field %u)\n", m.number, m.frame, m.field);
  }
  if (untilFrame != UINT32_MAX) {
    std::fprintf(stderr, "[replay]   XD_REPLAY_UNTIL: live controls from game frame %u\n", untilFrame);
  }
  if (rec.clockReads.empty()) {
    std::fprintf(stderr, "[replay]   no clock reads in this recording (made before they were recorded): the game "
                         "gets the live clock, so its random numbers will differ from the recorded run\n");
  } else {
    std::fprintf(stderr, "[replay]   %zu clock reads at %zu places: the game gets the recorded values (first %lld)\n",
                 rec.clockReads.size(), rec.clockSites.size(), static_cast<long long>(rec.clockReads[0].ticks));
  }
  std::lock_guard<std::mutex> lock(sLock);
  sReplayName = path;
  sReplayUntil = untilFrame;
  sNextReport = kReplayReportEvery;
  sClock = new ClockPlayer(rec);
  if (PortTime_IsDeterministic() && (PortFrameCost_Active() || PortCpuCost_Enabled())) {
    sClock->GoLive(PortTime_Now(), "XD_FRAME_COST / system.cpu_timing=gamecube: the game gets the deterministic clock, not the recorded one");
    PrintClockNotes();
  } else {
    StartPacing(rec);
  }
  const char* modeEnv = std::getenv("XD_REPLAY_MODE");
  const std::string mode = modeEnv != nullptr && modeEnv[0] != '\0' ? modeEnv : "resync";
  const char* exitEnv = std::getenv("XD_REPLAY_EXIT");
  sReplayExit = exitEnv != nullptr && exitEnv[0] == '1';
  const char* report = std::getenv("XD_ANCHOR_REPORT");
  sReportPath = report != nullptr ? report : "";
  const char* snaps = std::getenv("XD_SAVE_SNAPSHOTS");
  sSnapshotsOut = snaps != nullptr && snaps[0] != '\0' ? fs::u8path(snaps) : fs::path();
  if (mode != "frames") {
    auto* ticks = new TickPlayer(rec, mode == "strict" ? TickPlayer::Mode::Strict : TickPlayer::Mode::Resync);
    std::string why;
    if (ticks->Usable(&why)) {
      std::fprintf(stderr, "[replay]   anchored replay (%s): %u anchors, by the game's input ticks\n",
                   mode == "strict" ? "strict" : "resync", ticks->anchors());
      sTicks = ticks;
      sTickRec = new Recording(std::move(rec));
      InstallClockHook();
      return true;
    }
    delete ticks;
    if (mode == "strict" || (modeEnv != nullptr && mode == "resync")) {
      std::fprintf(stderr, "[replay]   XD_REPLAY_MODE=%s asked, but %s\n", mode.c_str(), why.c_str());
    }
  }
  sPlayer = new Player(std::move(rec));
  InstallClockHook();
  return true;
}

void PortInput_NoteRead(void) { sReads.fetch_add(1, std::memory_order_relaxed); }

struct PortAnchorEvent {
  uint32_t kind;
  uint32_t id;
};
struct PortAnchorState {
  uint32_t seed, floor;
  uint64_t party, progress;
};

}

namespace {

void WriteReport() {
  if (sReportPath.empty()) return;
  FILE* f = std::fopen(sReportPath.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "result %s\n", sReplayResult.empty() ? "running" : sReplayResult.c_str());
  if (sTicks != nullptr) {
    const TickPlayer::Stats& s = sTicks->stats();
    std::fprintf(f, "mode %s\nanchors_recorded %u\nanchors_reached %u\nanchors_matched %u\nmismatches %u\n",
                 sTicks->mode() == TickPlayer::Mode::Strict ? "strict" : "resync", sTicks->anchors(), sTicks->segment(),
                 sMatched, sMismatches);
    std::fprintf(f, "ticks %u\nearly %llu\nlate %llu\nphantom %llu\noverflow %llu\nunused %llu\nresyncs %llu\n",
                 sTicks->ticks(), (unsigned long long)s.early, (unsigned long long)s.late,
                 (unsigned long long)s.phantom, (unsigned long long)s.overflow, (unsigned long long)s.unused,
                 (unsigned long long)s.resyncs);
    std::fprintf(f, "skipped %llu\nextra %llu\n", (unsigned long long)s.skipped, (unsigned long long)s.extra);
  }
  std::fprintf(f, "frame %u\n", PortVI_GetFlipCount());
  if (ScheduleActive() || sScheduleEnded == 1) {
    std::fprintf(f, "exact yes interrupts %llu\n",
                 (unsigned long long)(ScheduleActive() ? PortOS_IrqScheduleDelivered() : sScheduleDelivered));
  } else if (sScheduleEnded == 2) {
    std::fprintf(f, "exact no frame %u interrupts %llu\n", sScheduleFailedFrame, (unsigned long long)sScheduleDelivered);
  } else {
    std::fprintf(f, "exact none\n");
  }
  if (!sFirstMismatch.empty()) std::fprintf(f, "first_mismatch %s\n", sFirstMismatch.c_str());
  for (const std::string& line : sSaveLines) std::fprintf(f, "%s\n", line.c_str());
  std::fclose(f);
}

bool EndTickReplay(const std::string& why, bool failed) {
  const uint32_t frame = PortVI_GetFlipCount();
  if (failed || sMismatches > 0) {
    sReplayResult = "fail " + (sFirstMismatch.empty() ? why : sFirstMismatch);
  } else {
    sReplayResult = "pass";
  }
  const TickPlayer::Stats& s = sTicks->stats();
  std::fprintf(stderr,
               "[replay] %s at game frame %u: %u of %u anchors reached, %u matched, %u differed; ticks %u (early %llu, "
               "late %llu, phantom %llu, unused %llu, resyncs %llu; soft events skipped %llu, extra %llu) -> %s\n",
               why.c_str(), frame, sTicks->segment(), sTicks->anchors(), sMatched, sMismatches, sTicks->ticks(),
               (unsigned long long)s.early, (unsigned long long)s.late, (unsigned long long)s.phantom,
               (unsigned long long)s.unused, (unsigned long long)s.resyncs, (unsigned long long)s.skipped,
               (unsigned long long)s.extra, sReplayResult.c_str());
  StopPacing();
  WriteReport();
  delete sTicks;
  sTicks = nullptr;
  const int64_t live = PortTime_IsDeterministic() ? PortTime_Now() : OSGetTime();
  sClock->GoLive(live, why + " at game frame " + std::to_string(frame));
  PrintClockNotes();
  return sReplayExit;
}

}

extern "C" {

int PortInput_Tick(const PortAnchorEvent* events, int count, const PortAnchorState* state, PADStatus force[4],
                   int* restoreSeed, uint32_t* seed) {
  *restoreSeed = 0;
  const uint32_t frame = PortVI_GetFlipCount();
  const bool used = sReads.exchange(0, std::memory_order_relaxed) > 0;
  int exitCode = -1;
  int forced = 0;
  {
    std::lock_guard<std::mutex> lock(sLock);
    std::vector<std::string> lines;
    if (!sTicking) {
      sTicking = true;
      WriteLine(FormatFormatLine(2));
    }
    WriteIrqNotes();
    if (sScheduled.load(std::memory_order_acquire)) {
      int failed = 0;
      if (PortOS_IrqScheduleActive(&failed) == 0) {
        sScheduled = false;
        sScheduleDelivered = PortOS_IrqScheduleDelivered();
        sScheduleFailedFrame = failed ? frame : 0;
        sScheduleEnded = failed ? 2 : 1;
        if (failed) {
          PortFrameCost_Rebase();
          std::fprintf(stderr, "[replay] NOT EXACT from about game frame %u on: paced by the recorded fields now\n", frame);
        }
      }
    }
    sTickWriter.End(used, lines);
    if (sTicks != nullptr) {
      sTicks->EndTick(used);
      sSegmentTicks++;
      AnchorMismatch missing;
      while (sTicks->TakeMissing(&missing)) {
        sMismatches++;
        if (sFirstMismatch.empty()) sFirstMismatch = missing.Describe();
        std::fprintf(stderr, "[replay] ANCHOR MISMATCH %s (game frame %u)\n", missing.Describe().c_str(), frame);
      }
    }
    AnchorState st;
    if (count > 0 && state != nullptr) {
      st.seed = state->seed, st.floor = state->floor, st.party = state->party, st.progress = state->progress;
    }
    for (int i = 0; i < count; i++) {
      Anchor a;
      a.number = ++sAnchorCount;
      a.kind = static_cast<AnchorKind>(events[i].kind);
      a.id = events[i].id;
      a.tick = sTickWriter.ticks();
      a.frame = frame;
      a.state = st;
      a.clock = sClockWriter.Counts();
      const std::string run = sTickWriter.Flush();
      if (!run.empty()) lines.push_back(run);
      lines.push_back(FormatAnchor(a));
      uint32_t recordedNumber = 0;
      if (sTicks != nullptr) {
        const TickPlayer::Check c = sTicks->OnAnchor(a.kind, a.id, st);
        if (c.ok && c.recorded != nullptr) recordedNumber = c.recorded->number;
        sSegmentTicks = 0;
        if (!c.ok) {
          sMismatches++;
          if (sFirstMismatch.empty()) sFirstMismatch = c.mismatch.Describe();
          std::fprintf(stderr, "[replay] ANCHOR MISMATCH %s (game frame %u)\n", c.mismatch.Describe().c_str(), frame);
        } else if (!c.beyond && !c.extra) {
          sMatched++;
        }
        if (c.restoreSeed && c.recorded != nullptr) {
          if (c.seed != st.seed) {
            *restoreSeed = 1;
            *seed = c.seed;
            st.seed = c.seed;
          }
        }
        static const bool realign = [] {
          const char* e = std::getenv("XD_RESYNC_CLOCK");
          return e != nullptr && e[0] == '1';
        }();
        if (sClock != nullptr && c.recorded != nullptr && !c.extra && !ScheduleActive() &&
            (sPacing || (realign && c.restoreSeed))) {
          sClock->Realign(c.recorded->clock, static_cast<int64_t>(frame) - c.recorded->frame);
          const std::vector<Anchor>& rec = sTicks->recordedAnchors();
          const size_t next = c.recorded->number;
          sClock->SetLimit(next < rec.size() ? rec[next].clock : std::vector<uint32_t>{});
          if (sPacing) PortFrameCost_Rebase();
        }
      }
      if (a.kind == AnchorKind::Save) {
        sSaves++;
        char name[48];
        std::snprintf(name, sizeof name, "%02u-anchor%06u", sSaves, a.number);
        if (!sCardsOut.empty()) CopyCard(sCardsOut / L"saves" / name);
        if (!sSnapshotsOut.empty()) CopyCard(sSnapshotsOut / name);
        sSaveLines.push_back("save " + std::to_string(sSaves) + " anchor " + std::to_string(a.number) + " frame " +
                             std::to_string(frame) + " recorded_anchor " + std::to_string(recordedNumber) + " folder " +
                             name);
        std::fprintf(stderr, "[input record] save %u done at anchor %u (game frame %u)\n", sSaves, a.number, frame);
      }
    }
    if (sTicks != nullptr) {
      bool over = false;
      if (frame >= sReplayUntil) {
        over = EndTickReplay("XD_REPLAY_UNTIL reached", false);
        exitCode = over ? (sMismatches > 0 ? 1 : 0) : -1;
      } else if (sTicks->Finished()) {
        over = EndTickReplay("recording finished", false);
        exitCode = over ? (sMismatches > 0 ? 1 : 0) : -1;
      } else if (sSegmentTicks > sTicks->segmentLength() + kStallTicks) {
        const std::string why = "stalled: no anchor " + std::to_string(sTicks->segment() + 1) + " after " +
                                std::to_string(sSegmentTicks) + " ticks";
        if (sFirstMismatch.empty()) sFirstMismatch = why;
        over = EndTickReplay(why, true);
        exitCode = over ? 1 : -1;
      } else {
        const PadState* pads = sTicks->Next();
        for (int i = 0; i < kPorts; i++) {
          ToStatus(pads[i], force[i]);
        }
        forced = 1;
        if (frame >= sNextReport) {
          std::fprintf(stderr, "[replay] game frame %u: tick %u, anchor %u of %u\n", frame, sTicks->ticks(),
                       sTicks->segment(), sTicks->anchors());
          sNextReport = frame + kReplayReportEvery;
        }
      }
    }
    PadState now[kPorts];
    for (int i = 0; i < kPorts; i++) {
      now[i] = forced ? FromStatus(force[i]) : sDelivered[i];
    }
    sTickWriter.Begin(now, lines);
    for (const std::string& line : lines) WriteLine(line);
  }
  if (exitCode >= 0) {
    std::fprintf(stderr, "[replay] XD_REPLAY_EXIT: the replay is over, exit %d\n", exitCode);
    StopRecording();
    std::fflush(stdout);
    std::fflush(stderr);
    _Exit(exitCode);
  }
  return forced;
}

u32 PortInput_PADRead(PADStatus* status) {
  const u32 result = PADRead(status);
  const uint32_t frame = PortVI_GetFlipCount();
  const uint32_t field = VIGetRetraceCount();
  std::lock_guard<std::mutex> lock(sLock);
  if (frame != sPollFrame) {
    sPollFrame = frame;
    sPoll = 0;
  } else {
    sPoll++;
  }
  const When now{frame, sPoll};

  if (sTicks != nullptr) {
    const PadState* pads = sTicks->Current();
    for (int i = 0; i < kPorts; i++) {
      ToStatus(pads[i], status[i]);
    }
  }
  if (sPlayer != nullptr) {
    for (const Mark& m : sPlayer->MarksReached(frame)) {
      std::fprintf(stderr, "[replay] mark #%u reached: game frame %u (recorded at field %u, now field %u)\n",
                   m.number, frame, m.field, field);
    }
    const char* stop = nullptr;
    if (frame >= sReplayUntil) {
      stop = "XD_REPLAY_UNTIL reached";
    } else if (sPlayer->Finished(now)) {
      stop = "recording finished";
    }
    if (stop != nullptr) {
      std::fprintf(stderr, "[replay] %s at game frame %u (field %u): live controls from here\n", stop, frame, field);
      delete sPlayer;
      sPlayer = nullptr;
      StopPacing();
      const int64_t live = PortTime_IsDeterministic() ? PortTime_Now() : OSGetTime();
      sClock->GoLive(live, std::string(stop) + " at game frame " + std::to_string(frame));
      PrintClockNotes();
    } else {
      PadState pads[kPorts];
      sPlayer->Apply(now, pads);
      for (int i = 0; i < kPorts; i++) {
        ToStatus(pads[i], status[i]);
      }
      if (frame >= sNextReport) {
        std::fprintf(stderr, "[replay] game frame %u (field %u): %zu of %zu changes\n", frame, field,
                     sPlayer->applied(), sPlayer->recording().changes.size());
        sNextReport = frame + kReplayReportEvery;
      }
    }
  }

  if (sTicks == nullptr && sPlayer == nullptr) {
    ApplyLatch(status);
    if (const auto filter = sPadFilter.load(); filter != nullptr) {
      filter(status);
    }
  }
  if (sCheckValue != nullptr && sPoll == 0 && frame % sCheckEvery == 0) {
    std::fprintf(stderr, "[check] game frame %u (field %u): %s 0x%08x\n", frame, field, sCheckName, sCheckValue());
  }
  if (sFrameHook != nullptr && sPoll == 0) {
    sFrameHook(frame);
  }

  for (int i = 0; i < kPorts; i++) {
    if (status[i].err != PAD_ERR_TRANSFER) {
      sDelivered[i] = FromStatus(status[i]);
    }
  }
  if (sFile != nullptr) {
    for (int i = 0; i < kPorts; i++) {
      const PadState state = FromStatus(status[i]);
      if (sFilter.Changed(i, state)) {
        WriteLine(FormatChange(Change{now, field, i, state}));
      }
    }
  }
  return result;
}

void PortInput_SetCheckValue(const char* name, uint32_t (*value)(void), uint32_t everyFrames) {
  std::lock_guard<std::mutex> lock(sLock);
  sCheckName = name;
  sCheckValue = value;
  sCheckEvery = everyFrames > 0 ? everyFrames : 1;
}

void (*PortInput_SetFrameHook(void (*hook)(uint32_t frame)))(uint32_t frame) {
  std::lock_guard<std::mutex> lock(sLock);
  void (*previous)(uint32_t) = sFrameHook;
  sFrameHook = hook;
  return previous;
}

u32 PortInput_SIProbe(s32 chan) {
  {
    std::lock_guard<std::mutex> lock(sLock);
    if (sPlayer != nullptr && chan >= 0 && chan < kPorts) {
      return sPlayer->Current(chan).err == kErrNoController ? SI_ERROR_NO_RESPONSE : SI_GC_CONTROLLER;
    }
    if (sTicks != nullptr && chan >= 0 && chan < kPorts) {
      return sTicks->Current()[chan].err == kErrNoController ? SI_ERROR_NO_RESPONSE : SI_GC_CONTROLLER;
    }
  }
  return SIProbe(chan);
}

}
