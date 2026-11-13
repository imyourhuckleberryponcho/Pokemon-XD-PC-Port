#if defined(__linux__) || defined(__APPLE__)
#include "xd_user_folder.h"

#include <crash_report.h>
#include <port_log.h>
#include <port_menu.h>
#include <port_user_folder.h>
#include <port_window_shot.h>
#include <port_zip.h>

#include <aurora/aurora.h>
#include <aurora/imgui.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

extern "C" {
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
constexpr uint64_t kNoticeMs = 6000;
constexpr unsigned kStepTimeoutMs = 5000;
constexpr size_t kKeepAutomatic = 10;

struct Picture {
  std::vector<uint8_t> rgba;
  uint32_t width = 0, height = 0;
  bool overlay = false;
};
using PictureDone = std::function<void(std::shared_ptr<Picture>)>;

void PictureArrived(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h, bool overlay) {
  std::unique_ptr<PictureDone> done(static_cast<PictureDone*>(ctx));
  std::shared_ptr<Picture> picture;
  if (rgba != nullptr) {
    picture = std::make_shared<Picture>();
    picture->rgba.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
    picture->width = w, picture->height = h, picture->overlay = overlay;
  }
  (*done)(std::move(picture));
}

void RequestPicture(PictureDone done, bool window) {
  PortShot_LinuxRequest(window, PictureArrived, new PictureDone(std::move(done)));
}

bool SavePicture(const Picture& picture, const fs::path& path) {
  return PortShot_SavePng(path.wstring().c_str(), picture.rgba.data(), picture.width, picture.height);
}

std::tm LocalNow() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  return local;
}

std::string sInfo;

std::atomic<int> sState{0};
std::string sDoneText;
std::string sTitle;
uint64_t sNoticeUntil = 0;
bool sNoticeShown = false;

struct PictureSlot {
  std::mutex lock;
  std::condition_variable arrived;
  bool answered = false;
  std::shared_ptr<Picture> picture;
};

struct Press {
  fs::path folder, zip;
  std::string name;
  std::shared_ptr<PictureSlot> slot;
  uint32_t field = 0, gameFrame = 0;
  std::string when;
  std::string map;
  const PortCrashReport* crash = nullptr;
};

bool CopyIfThere(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  return fs::exists(from, ec) && fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

fs::path ReportsFolder() { return xd::UserFolder() / "reports"; }

bool MakeFolder(Press& p, const char* suffix) {
  const std::tm local = LocalNow();
  char stamp[40], when[32];
  std::strftime(stamp, sizeof stamp, "report_%Y-%m-%d_%H-%M-%S", &local);
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
  p.when = when;
  const fs::path reports = ReportsFolder();
  std::error_code ec;
  fs::create_directories(reports, ec);
  const std::string base = std::string(stamp) + (suffix != nullptr ? suffix : "");
  p.name = base;
  for (int n = 2; fs::exists(reports / p.name, ec) && n < 100; n++) {
    p.name = base + "_" + std::to_string(n);
  }
  p.folder = reports / p.name;
  p.zip = reports / (p.name + ".zip");
  return fs::create_directory(p.folder, ec);
}

bool WithTimeout(std::function<void()> step, unsigned ms) {
  struct Shared {
    std::mutex lock;
    std::condition_variable done;
    bool finished = false;
  };
  auto shared = std::make_shared<Shared>();
  std::thread([shared, step = std::move(step)] {
    step();
    std::lock_guard<std::mutex> guard(shared->lock);
    shared->finished = true;
    shared->done.notify_all();
  }).detach();
  std::unique_lock<std::mutex> guard(shared->lock);
  return shared->done.wait_for(guard, std::chrono::milliseconds(ms), [&] { return shared->finished; });
}

void WriteLines(std::ofstream& info, const std::string& lines) {
  for (size_t at = 0; at < lines.size();) {
    const size_t end = lines.find('\n', at);
    info << lines.substr(at, end - at) << "\n";
    at = end == std::string::npos ? lines.size() : end + 1;
  }
}

bool WriteWhatHappened(const fs::path& log, const fs::path& out, const std::string& kind) {
  std::ifstream in(log, std::ios::binary);
  if (!in) {
    return false;
  }
  std::stringstream all;
  all << in.rdbuf();
  const std::string text = all.str();
  const char* marker = kind == "stuck" ? "[watchdog] STUCK" : kind == "stop" ? "*** Stopped" : "*** Crash";
  size_t at = text.rfind(marker);
  if (at == std::string::npos) {
    at = text.rfind("*** ");
  }
  if (at == std::string::npos) {
    return false;
  }
  std::ofstream f(out, std::ios::binary);
  f << text.substr(at);
  return static_cast<bool>(f);
}

bool WriteBundle(Press& p) {
  const bool automatic = p.crash != nullptr;
  const std::string kind = automatic ? p.crash->kind : "F10";

  uint32_t mark = 0;
  const char* shotNote = "(could not be taken)";
  const fs::path recording = p.folder / "recording.txt";
  std::shared_ptr<Picture> picture;
  if (automatic) {
    auto result = std::make_shared<std::atomic<uint32_t>>(0);
    if (!WithTimeout([result, recording] { *result = PortInput_MarkAndCopy(recording.wstring().c_str()); },
                     kStepTimeoutMs)) {
      std::fprintf(stderr, "[bug report] the recording couldn't be copied in time\n");
    }
    mark = *result;
    shotNote = "(none: automatic reports have no picture on Linux; the game's main thread had crashed or was stuck)";
  } else {
    mark = PortInput_MarkAndCopy(recording.wstring().c_str());
    std::unique_lock<std::mutex> guard(p.slot->lock);
    if (p.slot->arrived.wait_for(guard, std::chrono::milliseconds(kStepTimeoutMs), [&] { return p.slot->answered; })) {
      picture = p.slot->picture;
      if (picture == nullptr) shotNote = "(none: the graphics card's picture could not be read back)";
    } else {
      shotNote = "(none: the picture did not come back from the graphics card within 5 s)";
    }
  }
  const bool shot = picture != nullptr && SavePicture(*picture, p.folder / "screenshot.png");
  std::fflush(stdout);
  std::fflush(stderr);
  PortLog_Flush();
  const bool log = PortLog_CopyTail((p.folder / "log.txt").wstring().c_str(), kLogTail);
  const std::string whatName = kind + ".txt";
  const bool what = automatic && log && WriteWhatHappened(p.folder / "log.txt", p.folder / whatName, kind);
  const fs::path settingsFolder = xd::SettingsFolder();
  const bool settings = CopyIfThere(settingsFolder / "settings.ini", p.folder / "settings.ini");
  const bool mods = CopyIfThere(settingsFolder / "mods.ini", p.folder / "mods.ini");

  std::ofstream info(p.folder / "info.txt", std::ios::binary);
  if (automatic) {
    info << "Pokemon XD PC port: automatic report (" << kind << ")\n"
         << "what happened: " << (p.crash->summary != nullptr ? p.crash->summary : "?") << "\n"
         << "time " << p.when << "\n"
         << "video field " << p.field << ", game frame " << p.gameFrame << " (when it happened)\n";
  } else {
    info << "Pokemon XD PC port: bug report (F10)\n"
         << "time " << p.when << "\n"
         << "video field " << p.field << ", game frame " << p.gameFrame << " (at the key press)\n";
  }
  if (!p.map.empty()) {
    info << "map " << p.map << "\n";
  }
  if (picture != nullptr) {
    info << (picture->overlay ? "window picture " : "game picture ") << picture->width << "x" << picture->height
         << "\n";
  }
  info << "system Linux\n";
  WriteLines(info, sInfo);
  WriteLines(info, PortXD_DevMenuReport());
  WriteLines(info, PortXD_PlaythroughReport());
  WriteLines(info, PortSpeedups_Report());
  info << "\n";
  if (automatic) {
    std::string name = whatName;
    name.resize(std::max<size_t>(name.size(), 16), ' ');
    info << name
         << (what ? "what happened and the chain of calls (function+offset; the same text is in log.txt)"
                  : "(could not be written; look at the end of log.txt)")
         << "\n";
  }
  info << "screenshot.png  "
       << (!shot ? shotNote
           : picture->overlay
               ? "the window at the key press, put together by the program: the game's picture read back from\n"
                 "                the graphics card, and the port's screen that was open drawn over it again (the\n"
                 "                window's size; single pixels can differ from what the screen showed)"
               : "the game's picture at the key press (read back from the graphics card: the game's own size;\n"
                 "                nothing of the port was drawn over it)")
       << "\n";
  if (mark != 0) {
    info << "recording.txt   the input recording up to mark #" << mark << " (" << (automatic ? kind : "this key press")
         << "), from\n"
         << "                " << PortInput_RecordingPath() << "\n";
  } else {
    info << "recording.txt   (none: this run isn't recorded, XD_RECORD=0, or the copy failed)\n";
  }
  if (log) {
    info << "log.txt         the end of the program's log (at most " << (kLogTail >> 10) << " KB), from\n"
         << "                " << PortLog_Path() << "\n";
  } else {
    info << "log.txt         (none: this run has no log file)\n";
  }
  info << "settings.ini    " << (settings ? "the player's settings now" : "(none)") << "\n"
       << "mods.ini        " << (mods ? "the player's mod list now (the mods themselves are not copied)" : "(none)")
       << "\n";
  if (mark != 0) {
    info << "\n"
         << "To see it again: same port build (port commit above), same disc, these settings.ini and mods.ini\n"
         << "in the data folder (normally ~/.local/share/Pokemon XD), then\n"
         << "  XD_REPLAY=\"" << recording.string() << "\" ./xd <disc>\n"
         << "The replay stops at this moment (about game frame " << p.gameFrame << ") and hands the controls back.\n";
  }
  info.close();

  const bool zipped = PortZip_StoreFolder(p.folder.wstring().c_str(), p.zip.wstring().c_str());
  std::fprintf(stderr, "[bug report] saved %s%s%s\n", p.folder.string().c_str(), zipped ? " and " : "",
               zipped ? p.zip.string().c_str() : " (the zip file could not be written)");
  return zipped;
}

void Save(Press p) {
  const bool zipped = WriteBundle(p);
  sDoneText = " - bug report saved: " + p.name + (zipped ? ".zip" : "") + " (in " + ReportsFolder().string() + ")";
  sState = 2;
}

void PruneAutomatic() {
  std::error_code ec;
  std::vector<fs::path> folders;
  for (const auto& entry : fs::directory_iterator(ReportsFolder(), ec)) {
    const std::string name = entry.path().filename().string();
    if (entry.is_directory(ec) && name.rfind("report_", 0) == 0 &&
        (name.find("_crash") != std::string::npos || name.find("_stop") != std::string::npos ||
         name.find("_stuck") != std::string::npos)) {
      folders.push_back(entry.path());
    }
  }
  std::sort(folders.begin(), folders.end());
  for (size_t i = 0; i + kKeepAutomatic < folders.size(); i++) {
    fs::remove_all(folders[i], ec);
    fs::remove(fs::path(folders[i]).concat(".zip"), ec);
  }
}

void OnCrash(const PortCrashReport* report) {
  Press p;
  p.crash = report;
  p.field = VIGetRetraceCount();
  p.gameFrame = PortVI_GetFlipCount();
  auto map = std::make_shared<std::array<char, 160>>();
  (*map)[0] = '\0';
  if (WithTimeout([map] { PortXD_ReportMap(map->data(), static_cast<int>(map->size())); }, kStepTimeoutMs)) {
    p.map = map->data();
  }
  const std::string kind = report->kind != nullptr ? report->kind : "crash";
  if (!MakeFolder(p, ("_" + kind).c_str())) {
    std::fprintf(stderr, "[bug report] could not create a %s report folder\n", kind.c_str());
    return;
  }
  std::fprintf(stderr, "[bug report] %s at field %u, game frame %u: saving a report to %s\n", kind.c_str(), p.field,
               p.gameFrame, p.folder.string().c_str());
  WriteBundle(p);
  PruneAutomatic();
}

void TestF10InMenu() {
  static int sAfter = -1, sOpenFor = 0;
  if (sAfter == -1) {
    const char* text = std::getenv("XD_TEST_F10_IN_MENU");
    sAfter = text != nullptr && text[0] != '\0' ? std::max(1, std::atoi(text)) : 0;
  }
  if (sAfter == 0) return;
  sOpenFor = PortMenu_Active() ? sOpenFor + 1 : 0;
  if (sOpenFor < sAfter) return;
  sAfter = 0;
  std::fprintf(stderr, "[bug report] XD_TEST_F10_IN_MENU: an F10 key press now (a port screen open for %d refreshes)\n",
               sOpenFor);
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_F10;
  event.key.key = SDLK_F10;
  event.key.down = true;
  if (SDL_Window* window = aurora_get_window(); window != nullptr) event.key.windowID = SDL_GetWindowID(window);
  SDL_PushEvent(&event);
}

uint32_t sTestCrashFrame = 0;
std::string sTestCrashKind;

}

extern "C" {

#if !defined(__APPLE__)
ImTextureID __real_aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8);
ImTextureID __wrap_aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8) {
  const ImTextureID id = __real_aurora_imgui_add_texture(width, height, rgba8);
  PortShot_LinuxNoteTexture(static_cast<uint64_t>(id), width, height, rgba8);
  return id;
}
#endif

void PortXD_GamePictureFrame(void) { PortShot_LinuxFrame(); }

void* PortXD_CaptureWindow(void*, int*, int*) { return nullptr; }
bool PortXD_SavePng(void*, const wchar_t*) { return false; }

void PortXD_ScreenshotTo(void*, const char* folder, uint32_t field, uint32_t gameFrame) {
  const fs::path dir(folder);
  RequestPicture([dir, field, gameFrame](std::shared_ptr<Picture> picture) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path png = dir / ("f" + std::to_string(gameFrame) + ".png");
    const bool saved = picture != nullptr && SavePicture(*picture, png);
    if (saved) {
      std::ofstream f(dir / ("f" + std::to_string(gameFrame) + ".txt"));
      f << "video field " << field << "\ngame frame " << gameFrame << "\ngame picture " << picture->width << "x"
        << picture->height << "\n";
    }
    std::fprintf(stderr, "[screenshot] %s: %s (field %u, game frame %u)\n", saved ? "saved" : "FAILED",
                 png.string().c_str(), field, gameFrame);
  }, false);
}

void PortXD_Screenshot(void*, uint32_t field, uint32_t gameFrame) {
  const std::tm local = LocalNow();
  RequestPicture([local, field, gameFrame](std::shared_ptr<Picture> picture) {
    if (picture == nullptr) {
      std::fprintf(stderr, "[screenshot] could not take the picture\n");
      return;
    }
    const fs::path folder = xd::UserFolder() / "screenshots";
    std::error_code ec;
    fs::create_directories(folder, ec);
    char name[64], when[32];
    std::strftime(name, sizeof name, "shot_%Y%m%d_%H%M%S", &local);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
    const std::string base = std::string(name) + "_f" + std::to_string(field);
    const fs::path png = folder / (base + ".png");
    if (!SavePicture(*picture, png)) {
      std::fprintf(stderr, "[screenshot] could not save the picture %s\n", png.string().c_str());
      return;
    }
    fs::copy_file(png, folder / "latest.png", fs::copy_options::overwrite_existing, ec);
    std::ofstream f(folder / (base + ".txt"));
    f << "time " << when << "\nvideo field " << field << "\ngame frame " << gameFrame
      << (picture->overlay ? "\nwindow picture " : "\ngame picture ") << picture->width << "x" << picture->height
      << "\n";
    if (PortInput_RecordingPath()[0] != '\0') {
      f << "input recording " << PortInput_RecordingPath() << "\n";
    }
    std::fprintf(stderr, "[screenshot] %s (field %u, game frame %u)\n", png.string().c_str(), field, gameFrame);
  }, true);
}

void PortXD_BugReportSetInfo(const char* lines) { sInfo = lines != nullptr ? lines : ""; }

void PortXD_BugReportSetWindow(void*) {}

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
    std::this_thread::sleep_for(std::chrono::seconds(150));
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

void PortXD_BugReport(void*, uint32_t field, uint32_t gameFrame) {
  if (sState == 1) {
    std::fprintf(stderr, "[bug report] still saving the last one\n");
    return;
  }
  Press p;
  p.field = field;
  p.gameFrame = gameFrame;
  char map[160];
  if (PortXD_ReportMap(map, static_cast<int>(sizeof map))) {
    p.map = map;
  }
  if (!MakeFolder(p, nullptr)) {
    std::fprintf(stderr, "[bug report] could not create %s\n", p.folder.string().c_str());
    return;
  }
  std::fprintf(stderr, "[bug report] F10 at field %u, game frame %u: saving to %s\n", field, gameFrame,
               p.folder.string().c_str());
  p.slot = std::make_shared<PictureSlot>();
  RequestPicture([slot = p.slot](std::shared_ptr<Picture> picture) {
    std::lock_guard<std::mutex> guard(slot->lock);
    slot->picture = std::move(picture);
    slot->answered = true;
    slot->arrived.notify_all();
  }, true);

  if (SDL_Window* window = aurora_get_window(); window != nullptr) {
    if (!sNoticeShown) {
      const char* title = SDL_GetWindowTitle(window);
      sTitle = title != nullptr ? title : "";
    }
    SDL_SetWindowTitle(window, (sTitle + " - saving bug report...").c_str());
    sNoticeShown = true;
    sNoticeUntil = 0;
  }
  sState = 1;
  std::thread(Save, std::move(p)).detach();
}

void PortXD_BugReportPoll(void) {
  PortShot_LinuxPoll();
  TestF10InMenu();
  if (!sNoticeShown) {
    if (sState == 2) sState = 0;
    return;
  }
  SDL_Window* window = aurora_get_window();
  if (sState == 2 && sNoticeUntil == 0) {
    if (window != nullptr) SDL_SetWindowTitle(window, (sTitle + sDoneText).c_str());
    sNoticeUntil = SDL_GetTicks() + kNoticeMs;
  } else if (sNoticeUntil != 0 && SDL_GetTicks() >= sNoticeUntil) {
    if (window != nullptr) SDL_SetWindowTitle(window, sTitle.c_str());
    sNoticeShown = false;
    sNoticeUntil = 0;
    sState = 0;
  }
}

}
#endif
