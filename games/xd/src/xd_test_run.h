#ifndef XD_TEST_RUN_H
#define XD_TEST_RUN_H

#include <cstdlib>
#include <cstring>

namespace xd {

struct TestRunSwitch {
  const char* name;
  bool one;
};
inline constexpr TestRunSwitch kTestRunSwitches[] = {
    {"XD_TEST_RUN", true},
    {"XD_INPUT_SCRIPT", false},
    {"XD_REPLAY", false},
    {"XD_MENU_SCRIPT", false},
    {"XD_DEV_ACTIONS", false},
    {"XD_PHOTO_SCRIPT", false},
    {"XD_WARP", false},
    {"XD_LAUNCHER_SCRIPT", false},
    {"XD_BUG_REPORT_AT", false},
    {"XD_TEST_CRASH", false},
    {"XD_FOCUS_TEST", false},
    {"XD_BUTTON_ICONS_AT", false},
    {"XD_RENDER_ICON", false},
    {"XD_PREPARE_SHADERS", false},
    {"XD_NO_MESSAGE_BOXES", true},
    {"XD_WINDOW_OFFSCREEN", true},
    {"XD_REQUIRE_ORIGINAL", true},
};

template <class GetEnv>
const char* TestRunSwitchSet(GetEnv get) {
  for (const TestRunSwitch& s : kTestRunSwitches) {
    const char* v = get(s.name);
    if (v == nullptr || v[0] == '\0') continue;
    if (s.one && std::strcmp(v, "1") != 0) continue;
    return s.name;
  }
  return nullptr;
}

template <class GetEnv>
bool PlayerFilesReadOnly(GetEnv get) {
  const char* own = get("XD_SETTINGS_DIR");
  if (own != nullptr && own[0] != '\0') return false;
  return TestRunSwitchSet(get) != nullptr;
}

inline bool TestRun() {
  return TestRunSwitchSet([](const char* n) { return static_cast<const char*>(std::getenv(n)); }) != nullptr;
}
inline bool PlayerFilesReadOnly() {
  return PlayerFilesReadOnly([](const char* n) { return static_cast<const char*>(std::getenv(n)); });
}

inline constexpr const char* kPlayerFilesReadOnlyNote =
    "test run: not saved to the player's settings (XD_SETTINGS_DIR not set)";

}

#endif
