#include <port_focus_pause.h>
#include <port_freeze.h>
#include <port_menu.h>

#include <settings.h>

#include <aurora/aurora.h>
#include <imgui.h>

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace es = engine::settings;

uint32_t (*sProgress)(void) = nullptr;
bool sAllowed = false;
bool sHadFocus = false;

struct Loss {
  uint32_t frame;
  int refreshes;
};
std::vector<Loss> sTests;
size_t sNextTest = 0;
int sTestLeft = 0;

bool SettingOn() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("system.pause_on_focus_loss");
  return i != es::kNoSetting && reg.ValueOf(i).i != 0;
}

bool InFront() {
  SDL_Window* w = aurora_get_window();
  if (w == nullptr) return true;
  const SDL_WindowFlags f = SDL_GetWindowFlags(w);
  return (f & SDL_WINDOW_INPUT_FOCUS) != 0 && (f & SDL_WINDOW_MINIMIZED) == 0;
}

bool PageFrame(void*, const PortMenuInput*) {
  if (sTestLeft > 0) {
    if (--sTestLeft == 0) return false;
  } else if (InFront() || !SettingOn()) {
    return false;
  }
  float x, y, s;
  PortMenu_PictureRect(&x, &y, &s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const char* text = "Paused: the game carries on when its window is in front again";
  const ImVec2 size = ImGui::CalcTextSize(text);
  const float cx = x + 320.f * s, by = y + 440.f * s;
  dl->AddRectFilled(ImVec2(cx - size.x / 2 - 12, by - size.y - 8), ImVec2(cx + size.x / 2 + 12, by + 8),
                    IM_COL32(0, 0, 0, 170), 6.f);
  dl->AddText(ImVec2(cx - size.x / 2, by - size.y), IM_COL32(240, 240, 240, 255), text);
  if (sTestLeft == 0) {
    SDL_Delay(30);
  }
  return true;
}

void PageOpen(void*) {
  std::fprintf(stderr, "[focus pause] %s: the game is paused\n",
               sTestLeft > 0 ? "XD_FOCUS_TEST" : "the window is in the background");
}

void PageClose(void*) {
  std::fprintf(stderr, "[focus pause] %s: the game carries on\n",
               sTestLeft > 0 || !InFront() ? "end of the pause" : "the window is in front again");
  sTestLeft = 0;
}

const PortMenuPage kPage{"focus", PageOpen, PageFrame, PageClose, nullptr};

bool EnvIs(const char* name, const char* value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::strcmp(v, value) == 0;
}

void LoadTests(const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  const std::string all = text;
  size_t start = 0;
  while (start < all.size()) {
    size_t end = all.find(',', start);
    if (end == std::string::npos) end = all.size();
    const std::string part = all.substr(start, end - start);
    unsigned frame = 0;
    int refreshes = 0;
    if (std::sscanf(part.c_str(), "%u:%d", &frame, &refreshes) == 2 && refreshes > 0) {
      sTests.push_back({frame, refreshes});
    }
    start = end + 1;
  }
  std::fprintf(stderr, "[focus pause] XD_FOCUS_TEST: %zu simulated focus loss(es)\n", sTests.size());
}

}

extern "C" {

void PortFocusPause_Init(uint32_t (*progress)(void), bool testRun) {
  sProgress = progress;
  if (EnvIs("XD_PAUSE_ON_FOCUS", "0")) {
    sAllowed = false;
  } else {
    sAllowed = !testRun || EnvIs("XD_PAUSE_ON_FOCUS", "1");
  }
  LoadTests(std::getenv("XD_FOCUS_TEST"));
  std::fprintf(stderr, "[focus pause] %s\n",
               !sAllowed ? "never this run (a test run, or XD_PAUSE_ON_FOCUS=0)"
               : SettingOn() ? "on: the game pauses while its window is in the background"
                             : "off (system.pause_on_focus_loss): the game keeps running in the background");
}

void PortFocusPause_Frame(void) {
  if (PortMenu_Active() || PortMenu_Pending() || PortFreeze_Active()) return;
  if (sNextTest < sTests.size() && sProgress != nullptr && sProgress() >= sTests[sNextTest].frame) {
    const Loss t = sTests[sNextTest++];
    if (!SettingOn()) {
      std::fprintf(stderr, "[focus pause] XD_FOCUS_TEST at game frame %u: the setting is off, no pause\n", t.frame);
    } else if (PortMenu_Open(&kPage)) {
      sTestLeft = t.refreshes;
    }
    return;
  }
  if (!sAllowed) return;
  const bool front = InFront();
  if (front) {
    sHadFocus = true;
    return;
  }
  if (!sHadFocus || !SettingOn()) return;
  PortMenu_Open(&kPage);
}

}
