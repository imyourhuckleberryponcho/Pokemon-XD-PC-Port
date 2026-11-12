#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "xd_user_folder.h"

#include <crash_report.h>
#include <port_log.h>
#include <port_user_folder.h>
#include <port_zip.h>

#include <aurora/gfx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern "C" {
void* PortXD_CaptureWindow(void* hwnd, int* width, int* height);
bool PortXD_SavePng(void* bitmap, const wchar_t* path);
const char* PortInput_RecordingPath(void);
uint32_t PortInput_MarkAndCopy(const wchar_t* dest);
const char* PortXD_DevMenuReport(void);
const char* PortXD_PlaythroughReport(void);
const char* PortSpeedups_Report(void);
int PortXD_ReportMap(char* out, int size);
uint32_t PortVI_GetFlipCount(void);
uint32_t VIGetRetraceCount(void);
}

namespace {

namespace fs = std::filesystem;

constexpr size_t kLogTail = 1 << 20;
constexpr ULONGLONG kNoticeMs = 6000;
constexpr DWORD kStepTimeoutMs = 5000;
constexpr size_t kKeepAutomatic = 10;

std::string sInfo;
HWND sGameWindow = nullptr;

std::atomic<int> sState{0};
std::wstring sDoneText;
HWND sWindow = nullptr;
std::wstring sTitle;
ULONGLONG sNoticeUntil = 0;
bool sNoticeShown = false;

std::string Utf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

struct Press {
  fs::path folder, zip;
  std::wstring name;
  void* bitmap = nullptr;
  int width = 0, height = 0;
  uint32_t field = 0, gameFrame = 0;
  std::string when;
  std::string map;
  const PortCrashReport* crash = nullptr;
};

bool CopyIfThere(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  return fs::exists(from, ec) && fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

fs::path ReportsFolder() { return xd::UserFolder() / L"reports"; }

bool MakeFolder(Press& p, const wchar_t* suffix) {
  const std::time_t now = std::time(nullptr);
  std::tm local;
  localtime_s(&local, &now);
  wchar_t stamp[40];
  wcsftime(stamp, 40, L"report_%Y-%m-%d_%H-%M-%S", &local);
  char when[32];
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
  p.when = when;
  const fs::path reports = ReportsFolder();
  std::error_code ec;
  fs::create_directories(reports, ec);
  const std::wstring base = std::wstring(stamp) + (suffix != nullptr ? suffix : L"");
  p.name = base;
  for (int n = 2; fs::exists(reports / p.name, ec) && n < 100; n++) {
    p.name = base + L"_" + std::to_wstring(n);
  }
  p.folder = reports / p.name;
  p.zip = reports / (p.name + L".zip");
  return fs::create_directory(p.folder, ec);
}

bool WithTimeout(std::function<void()> step, DWORD ms) {
  auto* job = new std::function<void()>(std::move(step));
  HANDLE thread = CreateThread(
      nullptr, 0,
      [](void* param) -> DWORD {
        auto* f = static_cast<std::function<void()>*>(param);
        (*f)();
        delete f;
        return 0;
      },
      job, 0, nullptr);
  if (thread == nullptr) {
    delete job;
    return false;
  }
  const bool done = WaitForSingleObject(thread, ms) == WAIT_OBJECT_0;
  CloseHandle(thread);
  return done;
}

void WriteLines(std::ofstream& info, const std::string& lines) {
  for (size_t at = 0; at < lines.size();) {
    const size_t end = lines.find('\n', at);
    info << lines.substr(at, end - at) << "\r\n";
    at = end == std::string::npos ? lines.size() : end + 1;
  }
}

bool WriteBundle(Press& p) {
  const bool automatic = p.crash != nullptr;
  const std::string kind = automatic ? p.crash->kind : "F10";

  bool dump = false;
  const std::wstring dumpName = automatic ? std::wstring(kind.begin(), kind.end()) + L".dmp" : L"";
  if (automatic) {
    dump = PortCrash_WriteMinidump((p.folder / dumpName).c_str(), p.crash) != 0;
  }
  uint32_t mark = 0;
  const char* shotNote = "(could not be taken)";
  const fs::path recording = p.folder / L"recording.txt";
  if (automatic) {
    auto result = std::make_shared<std::atomic<uint32_t>>(0);
    if (!WithTimeout([result, recording] { *result = PortInput_MarkAndCopy(recording.c_str()); }, kStepTimeoutMs)) {
      std::fprintf(stderr, "[bug report] the recording couldn't be copied in time\n");
    }
    mark = *result;
    auto shot = std::make_shared<std::atomic<void*>>(nullptr);
    auto size = std::make_shared<std::pair<int, int>>(0, 0);
    if (sGameWindow != nullptr &&
        WithTimeout([shot, size] { *shot = PortXD_CaptureWindow(sGameWindow, &size->first, &size->second); },
                    kStepTimeoutMs)) {
      p.bitmap = *shot;
      p.width = size->first;
      p.height = size->second;
    } else if (sGameWindow != nullptr) {
      std::fprintf(stderr, "[bug report] no screenshot: the window's thread doesn't answer (it crashed or is stuck)\n");
      shotNote = "(none: the window's thread had crashed or was stuck)";
    }
  } else {
    mark = PortInput_MarkAndCopy(recording.c_str());
  }
  const bool shot = PortXD_SavePng(p.bitmap, (p.folder / L"screenshot.png").c_str());
  std::fflush(stdout);
  std::fflush(stderr);
  PortLog_Flush();
  const bool log = PortLog_CopyTail((p.folder / L"log.txt").c_str(), kLogTail);
  const fs::path settingsFolder = xd::SettingsFolder();
  const bool settings = CopyIfThere(settingsFolder / L"settings.ini", p.folder / L"settings.ini");
  const bool mods = CopyIfThere(settingsFolder / L"mods.ini", p.folder / L"mods.ini");

  std::ofstream info(p.folder / L"info.txt", std::ios::binary);
  if (automatic) {
    info << "Pokemon XD PC port: automatic report (" << kind << ")\r\n"
         << "what happened: " << (p.crash->summary != nullptr ? p.crash->summary : "?") << "\r\n"
         << "time " << p.when << "\r\n"
         << "video field " << p.field << ", game frame " << p.gameFrame << " (when it happened)\r\n";
  } else {
    info << "Pokemon XD PC port: bug report (F10)\r\n"
         << "time " << p.when << "\r\n"
         << "video field " << p.field << ", game frame " << p.gameFrame << " (at the key press)\r\n";
  }
  if (!p.map.empty()) {
    info << "map " << p.map << "\r\n";
  }
  info << "window " << p.width << "x" << p.height << "\r\n";
  WriteLines(info, sInfo);
  WriteLines(info, PortXD_DevMenuReport());
  WriteLines(info, PortXD_PlaythroughReport());
  WriteLines(info, PortSpeedups_Report());
  info << "\r\n";
  if (automatic) {
    std::string name(dumpName.begin(), dumpName.end());
    name.resize(std::max<size_t>(name.size(), 16), ' ');
    info << name << (dump ? "minidump of the program at that moment (open in Visual Studio or WinDbg with\r\n"
                            "                the same build's xd.exe and xd.pdb)"
                          : "(the minidump could not be written)")
         << "\r\n";
  }
  info << "screenshot.png  " << (shot ? (automatic ? "the window then" : "the window at the key press")
                                       : shotNote)
       << "\r\n";
  if (mark != 0) {
    info << "recording.txt   the input recording up to mark #" << mark << " (" << (automatic ? kind : "this key press")
         << "), from\r\n"
         << "                " << PortInput_RecordingPath() << "\r\n";
  } else {
    info << "recording.txt   (none: this run isn't recorded, XD_RECORD=0, or the copy failed)\r\n";
  }
  if (log) {
    info << "log.txt         the end of the program's log (at most " << (kLogTail >> 10) << " KB), from\r\n"
         << "                " << PortLog_Path() << "\r\n";
  } else {
    info << "log.txt         (none: this run has no log file)\r\n";
  }
  info << "settings.ini    " << (settings ? "the player's settings now" : "(none)") << "\r\n"
       << "mods.ini        " << (mods ? "the player's mod list now (the mods themselves are not copied)" : "(none)")
       << "\r\n";
  if (mark != 0) {
    info << "\r\n"
         << "To see it again: same port build (port commit above), same disc, these settings.ini and mods.ini\r\n"
         << "in the data folder (normally %APPDATA%\\Pokemon XD; a portable copy keeps it next to xd.exe), then\r\n"
         << "  set XD_REPLAY=" << Utf8(recording) << "\r\n"
         << "  xd.exe <disc>\r\n"
         << "The replay stops at this moment (about game frame " << p.gameFrame << ") and hands the controls back.\r\n";
  }
  info.close();

  const bool zipped = PortZip_StoreFolder(p.folder.c_str(), p.zip.c_str());
  std::fprintf(stderr, "[bug report] saved %s%s%s\n", Utf8(p.folder).c_str(), zipped ? " and " : "",
               zipped ? Utf8(p.zip).c_str() : " (the zip file could not be written)");
  return zipped;
}

void Save(Press p) {
  const bool zipped = WriteBundle(p);
  sDoneText = L" - bug report saved: " + p.name + (zipped ? L".zip" : L"") + L" (in " + ReportsFolder().wstring() + L")";
  sState = 2;
}

void PruneAutomatic() {
  std::error_code ec;
  std::vector<fs::path> folders;
  for (const auto& entry : fs::directory_iterator(ReportsFolder(), ec)) {
    const std::wstring name = entry.path().filename().wstring();
    if (entry.is_directory(ec) && name.rfind(L"report_", 0) == 0 &&
        (name.find(L"_crash") != std::wstring::npos || name.find(L"_stop") != std::wstring::npos ||
         name.find(L"_stuck") != std::wstring::npos)) {
      folders.push_back(entry.path());
    }
  }
  std::sort(folders.begin(), folders.end());
  for (size_t i = 0; i + kKeepAutomatic < folders.size(); i++) {
    fs::remove_all(folders[i], ec);
    fs::remove(fs::path(folders[i]).concat(L".zip"), ec);
  }
}

void OnCrash(const PortCrashReport* report) {
  Press p;
  p.crash = report;
  p.field = VIGetRetraceCount();
  p.gameFrame = PortVI_GetFlipCount();
  auto map = std::make_shared<std::array<char, 160>>();
  if (WithTimeout([map] { PortXD_ReportMap(map->data(), static_cast<int>(map->size())); }, kStepTimeoutMs)) {
    p.map = map->data();
  }
  const std::string kind = report->kind != nullptr ? report->kind : "crash";
  const std::wstring suffix = L"_" + std::wstring(kind.begin(), kind.end());
  if (!MakeFolder(p, suffix.c_str())) {
    std::fprintf(stderr, "[bug report] could not create a %s report folder\n", kind.c_str());
    return;
  }
  std::fprintf(stderr, "[bug report] %s at field %u, game frame %u: saving a report to %s\n", kind.c_str(), p.field,
               p.gameFrame, Utf8(p.folder).c_str());
  WriteBundle(p);
  PruneAutomatic();
}

uint32_t sTestCrashFrame = 0;
std::string sTestCrashKind;

}

extern "C" {

void PortXD_BugReportSetInfo(const char* lines) { sInfo = lines != nullptr ? lines : ""; }

void PortXD_BugReportSetWindow(void* hwnd) { sGameWindow = static_cast<HWND>(hwnd); }

void PortXD_BugReportInstallAutomatic(int test) {
  const char* setting = std::getenv("XD_CRASH_REPORT");
  const bool on = setting != nullptr && setting[0] != '\0' ? std::strcmp(setting, "0") != 0 : test == 0;
  if (on) {
    PortCrash_SetReportHook(OnCrash);
  }
  const char* crash = std::getenv("XD_TEST_CRASH");
  if (crash != nullptr && crash[0] != '\0') {
    sTestCrashFrame = static_cast<uint32_t>(std::strtoul(crash, nullptr, 10));
    const char* colon = std::strchr(crash, ':');
    sTestCrashKind = colon != nullptr ? colon + 1 : "null";
    std::fprintf(stderr, "[bug report] XD_TEST_CRASH: a '%s' crash at game frame %u (automatic reports %s)\n",
                 sTestCrashKind.c_str(), sTestCrashFrame, on ? "on" : "off");
  }
}

void PortXD_TestCrashPoll(void) {
  if (sTestCrashFrame == 0 || PortVI_GetFlipCount() < sTestCrashFrame) {
    return;
  }
  sTestCrashFrame = 0;
  std::fprintf(stderr, "[bug report] XD_TEST_CRASH: '%s' now (game frame %u)\n", sTestCrashKind.c_str(),
               PortVI_GetFlipCount());
  if (sTestCrashKind == "abort") {
    std::abort();
  } else if (sTestCrashKind == "stuck") {
    Sleep(150000);
  } else if (sTestCrashKind == "gpu_lost") {
    aurora_debug_lose_device(false);
  } else if (sTestCrashKind == "gpu_removed") {
    aurora_debug_lose_device(true);
  } else if (sTestCrashKind == "gpu_stall") {
    aurora_debug_stall_render_worker(0);
  } else if (sTestCrashKind == "gpu_stall_removed") {
    aurora_debug_stall_render_worker(0);
    aurora_debug_lose_device(true);
  } else if (sTestCrashKind == "thread") {
    std::thread([] { *static_cast<volatile int*>(nullptr) = 1; }).detach();
  } else {
    *static_cast<volatile int*>(nullptr) = 1;
  }
}

void PortXD_BugReport(void* hwnd, uint32_t field, uint32_t gameFrame) {
  if (sState == 1) {
    std::fprintf(stderr, "[bug report] still saving the last one\n");
    return;
  }
  Press p;
  p.field = field;
  p.gameFrame = gameFrame;
  p.bitmap = PortXD_CaptureWindow(hwnd, &p.width, &p.height);
  char map[160];
  if (PortXD_ReportMap(map, static_cast<int>(sizeof map))) {
    p.map = map;
  }
  if (!MakeFolder(p, nullptr)) {
    std::fprintf(stderr, "[bug report] could not create %s\n", Utf8(p.folder).c_str());
    if (p.bitmap != nullptr) DeleteObject(static_cast<HBITMAP>(p.bitmap));
    return;
  }
  std::fprintf(stderr, "[bug report] F10 at field %u, game frame %u: saving to %s\n", field, gameFrame,
               Utf8(p.folder).c_str());

  HWND window = static_cast<HWND>(hwnd);
  if (window != nullptr) {
    if (!sNoticeShown) {
      wchar_t title[256] = L"";
      GetWindowTextW(window, title, 256);
      sTitle = title;
    }
    sWindow = window;
    SetWindowTextW(window, (sTitle + L" - saving bug report...").c_str());
    sNoticeShown = true;
    sNoticeUntil = 0;
  }
  sState = 1;
  std::thread(Save, std::move(p)).detach();
}

void PortXD_BugReportPoll(void) {
  if (!sNoticeShown) {
    return;
  }
  if (sState == 2 && sNoticeUntil == 0) {
    SetWindowTextW(sWindow, (sTitle + sDoneText).c_str());
    sNoticeUntil = GetTickCount64() + kNoticeMs;
  } else if (sNoticeUntil != 0 && GetTickCount64() >= sNoticeUntil) {
    SetWindowTextW(sWindow, sTitle.c_str());
    sNoticeShown = false;
    sNoticeUntil = 0;
    sState = 0;
  }
}

}
