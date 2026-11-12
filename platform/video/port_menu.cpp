#include <port_menu.h>
#include <port_freeze.h>
#include <port_menu_music.h>
#include <port_user_folder.h>
#include <port_window_shot.h>

#include <aurora/aurora.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <settings.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
void PortInput_MenuNote(const char* what);
void PortInput_SettingsNote(const char* list);
void PortInput_LatchHeld(void);
}

namespace {

namespace fs = std::filesystem;

uint32_t (*sProgress)(void) = nullptr;
fs::path sShotDir;
PortMenuPage sRequested{};
bool sPending = false;
bool sActive = false;
PortMenuPage sScriptPage{};
bool sHaveScriptPage = false;
int sWaitHooks = 0;

struct Session {
  uint32_t frame;
  std::vector<std::string> steps;
};
std::vector<Session> sScript;
size_t sNextSession = 0;
const Session* sRunning = nullptr;
size_t sStep = 0;
int sWait = 0;
uint16_t sScriptHeld = 0;
int sScriptHoldLeft = 0;
int sHoldAtLeast = 0;

std::string Trim(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  return s;
}

void LoadScript(const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  const std::string all = text;
  size_t start = 0;
  while (start <= all.size()) {
    size_t end = all.find(',', start);
    if (end == std::string::npos) end = all.size();
    const std::string part = all.substr(start, end - start);
    const size_t colon = part.find(':');
    if (colon != std::string::npos) {
      Session s;
      s.frame = static_cast<uint32_t>(std::strtoul(part.c_str(), nullptr, 10));
      size_t a = colon + 1;
      while (a <= part.size()) {
        size_t b = part.find(';', a);
        if (b == std::string::npos) b = part.size();
        const std::string step = Trim(part.substr(a, b - a));
        if (!step.empty()) s.steps.push_back(step);
        a = b + 1;
      }
      sScript.push_back(s);
    }
    start = end + 1;
  }
  std::fprintf(stderr, "[port menu] XD_MENU_SCRIPT: %zu session(s)\n", sScript.size());
}

uint16_t ButtonBit(const std::string& name) {
  static const struct {
    const char* name;
    uint16_t bit;
  } kNames[] = {{"a", PAD_BUTTON_A},       {"b", PAD_BUTTON_B},         {"x", PAD_BUTTON_X},
                {"y", PAD_BUTTON_Y},       {"z", PAD_TRIGGER_Z},        {"start", PAD_BUTTON_START},
                {"up", PAD_BUTTON_UP},     {"down", PAD_BUTTON_DOWN},   {"left", PAD_BUTTON_LEFT},
                {"right", PAD_BUTTON_RIGHT}, {"l", PAD_TRIGGER_L},      {"r", PAD_TRIGGER_R}};
  for (const auto& n : kNames) {
    if (name == n.name) return n.bit;
  }
  return 0;
}

bool RunScriptStep(int refresh) {
  if (sScriptHoldLeft > 0 && --sScriptHoldLeft == 0) {
    sScriptHeld = 0;
    return true;
  }
  if (sScriptHeld != 0) return true;
  if (sWait > 0) {
    sWait--;
    return true;
  }
  if (sStep >= sRunning->steps.size()) {
    return refresh < sHoldAtLeast;
  }
  const std::string& step = sRunning->steps[sStep++];
  char word[32] = {}, arg[200] = {};
  int n = 0;
  std::sscanf(step.c_str(), "%31s %199s %d", word, arg, &n);
  const std::string w = word;
  std::fprintf(stderr, "[port menu] script: %s\n", step.c_str());
  if (w == "wait") {
    sWait = std::atoi(arg);
  } else if (w == "press") {
    sScriptHeld = ButtonBit(arg);
    sScriptHoldLeft = n > 0 ? n : 2;
    if (sScriptHeld == 0) std::fprintf(stderr, "[port menu] script: no button called \"%s\"\n", arg);
  } else if (w == "window") {
    PortShot_SaveWindow((sShotDir / (std::string(arg) + ".png")).wstring().c_str());
  } else if (w == "close") {
    return false;
  } else {
    std::fprintf(stderr, "[port menu] script: unknown step \"%s\"\n", step.c_str());
  }
  return true;
}

constexpr uint16_t kDirs = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;
uint16_t sLastHeld = 0;
int sRepeatTimer = 0;
bool sLatchPending = false;
uint16_t sLatched = 0;

uint16_t ReadHeld() {
  PADStatus pads[PAD_MAX_CONTROLLERS] = {};
  PADRead(pads);
  uint16_t held = 0;
  for (const PADStatus& p : pads) {
    if (p.err != PAD_ERR_NONE) continue;
    held |= p.button;
    const int x = p.stickX, y = p.stickY;
    if (std::abs(x) > 0x20 || std::abs(y) > 0x20) {
      if (std::abs(y) >= std::abs(x)) held |= y > 0 ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;
      else held |= x > 0 ? PAD_BUTTON_RIGHT : PAD_BUTTON_LEFT;
    }
  }
  return held;
}

PortMenuInput NextInput(bool live) {
  PortMenuInput in{};
  in.held = (live ? ReadHeld() : 0) | sScriptHeld;
  if (sLatchPending) {
    sLatchPending = false;
    sLatched = in.held;
  }
  sLatched &= in.held;
  in.held &= static_cast<uint16_t>(~sLatched);
  in.pressed = static_cast<uint16_t>(in.held & ~sLastHeld);
  in.repeat = in.pressed;
  const uint16_t dirs = in.held & kDirs;
  if ((in.pressed & kDirs) != 0) {
    sRepeatTimer = 15;
  } else if (dirs != 0 && --sRepeatTimer <= 0) {
    sRepeatTimer = 5;
    in.repeat |= dirs;
  }
  sLastHeld = in.held;
  return in;
}

bool TestPageFrame(void*, const PortMenuInput* in) {
  float x, y, s;
  PortMenu_PictureRect(&x, &y, &s);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(x + 160 * s, y + 200 * s), ImVec2(x + 480 * s, y + 280 * s), IM_COL32(0, 0, 0, 200));
  dl->AddText(ImVec2(x + 180 * s, y + 230 * s), IM_COL32(255, 255, 255, 255), "Port menu (test page): B closes");
  return (in->pressed & PAD_BUTTON_B) == 0;
}

int SoundMode(const char* page) {
  if (page != nullptr && std::strcmp(page, "focus") == 0) return PORT_MENU_MUSIC_FADE;
  const engine::settings::Registry& reg = engine::settings::Global();
  const size_t i = reg.Find("audio.menu_music");
  if (i == engine::settings::kNoSetting) return PORT_MENU_MUSIC_STOP;
  return PortMenuMusic_ModeFromName(reg.ValueText(i).c_str(), PORT_MENU_MUSIC_FADE);
}

void Run(void (*pump)(void)) {
  PortMenuMusic_SetNext(SoundMode(sRequested.name));
  if (!PortFreeze_Begin()) {
    return;
  }
  PortMenuPage page = sRequested;
  sPending = false;
  sActive = true;
  const uint32_t frame = sProgress != nullptr ? sProgress() : 0;
  const std::string name = page.name != nullptr ? page.name : "menu";
  PortInput_MenuNote(("on " + name).c_str());
  const std::string settingsBefore = engine::settings::Global().ReplayText();
  std::fprintf(stderr, "[port menu] %s: the game is paused at game frame %u\n", name.c_str(), frame);
  if (page.open != nullptr) page.open(page.ctx);
  sLastHeld = ReadHeld();
  sLatched = 0;
  sLatchPending = false;
  sRepeatTimer = 0;
  const bool scripted = sRunning != nullptr;
  for (int refresh = 0;; refresh++) {
    const PortMenuInput in = NextInput(!scripted);
    bool keep = true;
    if (scripted && !RunScriptStep(refresh)) keep = false;
    PortFreeze_DrawPicture();
    if (keep && !page.frame(page.ctx, &in)) keep = false;
    PortFreeze_Present(pump);
    if (!keep) break;
  }
  if (page.close != nullptr) page.close(page.ctx);
  PortFreeze_DrawPicture();
  PortFreeze_Present(pump);
  PortFreeze_End();
  PortInput_LatchHeld();
  PortInput_MenuNote("off");
  if (const std::string settingsAfter = engine::settings::Global().ReplayText(); settingsAfter != settingsBefore) {
    PortInput_SettingsNote(settingsAfter.c_str());
  }
  std::fprintf(stderr, "[port menu] %s closed: the game carries on at game frame %u\n", name.c_str(), frame);
  sActive = false;
  sRunning = nullptr;
  sScriptHeld = 0;
  sScriptHoldLeft = 0;
}

}

extern "C" {

void PortMenu_Init(const char* appFolder, uint32_t (*progress)(void)) {
  sProgress = progress;
  const char* dir = std::getenv("XD_MENU_SHOT_DIR");
  sShotDir = dir != nullptr && dir[0] != '\0' ? fs::u8path(dir) : PortUser_Folder(appFolder) / "menu_shots";
  const char* hold = std::getenv("XD_MENU_HOLD");
  sHoldAtLeast = hold != nullptr ? std::atoi(hold) : 0;
  LoadScript(std::getenv("XD_MENU_SCRIPT"));
  PortFreeze_Capture();
}

bool PortMenu_Open(const PortMenuPage* page) {
  if (page == nullptr || page->frame == nullptr || sActive || sPending) return false;
  sRequested = *page;
  sPending = true;
  if (sRunning == nullptr && sNextSession < sScript.size() && sScript[sNextSession].frame == 0) {
    sRunning = &sScript[sNextSession++];
    sStep = 0;
    sWait = 0;
  }
  return true;
}

void PortMenu_SetScriptPage(const PortMenuPage* page) {
  if (page == nullptr) {
    sHaveScriptPage = false;
    return;
  }
  sScriptPage = *page;
  sHaveScriptPage = true;
}

void PortMenu_Frame(void (*pump)(void)) {
  if (sActive || PortFreeze_Active()) return;
  if (!sPending && sNextSession < sScript.size() && sScript[sNextSession].frame != 0 && sProgress != nullptr &&
      sProgress() >= sScript[sNextSession].frame) {
    sRunning = &sScript[sNextSession++];
    sStep = 0;
    sWait = 0;
    static const PortMenuPage kTest{"test", nullptr, TestPageFrame, nullptr, nullptr};
    sRequested = sHaveScriptPage ? sScriptPage : kTest;
    sPending = true;
  }
  if (!sPending) {
    sWaitHooks = 0;
    return;
  }
  if (!PortFreeze_Ready(&sWaitHooks)) {
    return;
  }
  Run(pump);
}

void PortMenu_LatchHeld(void) {
  if (sActive) sLatchPending = true;
}

bool PortMenu_Active(void) { return sActive; }
bool PortMenu_Pending(void) { return sPending; }

void PortMenu_PictureRect(float* x, float* y, float* scale) {
  float W = 640.f, H = 480.f;
  if (ImGui::GetCurrentContext() != nullptr) {
    W = ImGui::GetIO().DisplaySize.x;
    H = ImGui::GetIO().DisplaySize.y;
  }
  u32 fw = 0, fh = 0;
  AuroraGetRenderSize(&fw, &fh);
  const float aspect = fw > 0 && fh > 0 ? static_cast<float>(fw) / static_cast<float>(fh) : 4.f / 3.f;
  float cw = W, ch = W / aspect;
  if (ch > H) {
    ch = H;
    cw = H * aspect;
  }
  const float ox = (W - cw) * 0.5f, oy = (H - ch) * 0.5f;
  const float w43 = std::min(cw, ch * 4.f / 3.f);
  *x = ox + (cw - w43) * 0.5f;
  *y = oy;
  *scale = w43 / 640.f;
}

}
