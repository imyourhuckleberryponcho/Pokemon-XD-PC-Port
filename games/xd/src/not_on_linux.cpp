#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void not_yet(const char* what) {
  std::fprintf(stderr, "[games/xd] %s: not on Linux yet\n", what);
  std::fflush(stderr);
}

bool asked(const char* variable) {
  const char* v = std::getenv(variable);
  return v != nullptr && v[0] != '\0';
}

}

extern "C" {

#ifdef __APPLE__
void PortXD_BugReportSetInfo(const char*) {}
void PortXD_BugReportSetWindow(void*) {}
void PortXD_BugReportInstallAutomatic(int) {
  if (asked("XD_TEST_CRASH")) not_yet("XD_TEST_CRASH (test crashes and their report bundles)");
  const char* setting = std::getenv("XD_CRASH_REPORT");
  if (setting != nullptr && setting[0] != '\0' && setting[0] != '0') not_yet("XD_CRASH_REPORT (crash report bundles)");
}
void PortXD_TestCrashPoll(void) {}
void PortXD_BugReport(void*, uint32_t, uint32_t) { not_yet("F10 bug reports"); }
void PortXD_BugReportPoll(void) {}

void* PortXD_CaptureWindow(void*, int*, int*) { return nullptr; }
bool PortXD_SavePng(void*, const wchar_t*) { return false; }
void PortXD_ScreenshotTo(void*, const char*, uint32_t, uint32_t) {
  static bool said = false;
  if (!said) not_yet("XD_SHOT_FRAMES (pictures of the window)");
  said = true;
}
void PortXD_Screenshot(void*, uint32_t, uint32_t) { not_yet("F12 screenshots"); }
#endif

#ifdef __APPLE__
void PortXD_StateDumpStart(void) {
  if (asked("XD_STATE_PROBES")) not_yet("XD_STATE_PROBES (game variables by name)");
  if (asked("XD_RNG_SEED")) not_yet("XD_RNG_SEED");
}
#endif

bool PortXD_RenderIconPrepare(void) {
  if (asked("XD_RENDER_ICON")) not_yet("XD_RENDER_ICON (pictures from the game's models)");
  return false;
}
void PortXD_RenderIconStart(void) {}

}
