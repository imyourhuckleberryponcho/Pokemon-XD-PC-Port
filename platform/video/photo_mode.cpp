#include <port_photo.h>
#include <port_freeze.h>
#include <port_menu_music.h>
#include <port_window_shot.h>
#include <port_video.h>
#include <port_user_folder.h>
#include <run_status.h>

#include <settings.h>

#include <aurora/aurora.h>
#include <aurora/time.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include <imgui.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
void PortInput_PhotoNote(const char* what);
}

namespace {

namespace fs = std::filesystem;
constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.f;

bool sEnabled = false;
SDL_Scancode sKey = SDL_SCANCODE_F8;
std::string sKeyName = "F8";
uint32_t (*sProgress)(void) = nullptr;
fs::path sFolder;

std::string U8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

struct Camera {
  float pos[3] = {0.f, 0.f, 0.f};
  float yaw = 0.f, pitch = 0.f, roll = 0.f;
  float fov = 0.f;
};
Camera sCam;
float sSpeed = 100.f;
float sLookSpeed = 0.15f;
bool sHide2D = false;
int sShotScale = 0;
bool sOverlay = true;

using Mat3 = float[3][3];

void Rotation(const Camera& c, Mat3 r) {
  const float cy = std::cos(c.yaw * kDeg), sy = std::sin(c.yaw * kDeg);
  const float cp = std::cos(c.pitch * kDeg), sp = std::sin(c.pitch * kDeg);
  const float cr = std::cos(c.roll * kDeg), sr = std::sin(c.roll * kDeg);
  const Mat3 ry = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
  const Mat3 rx = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}};
  const Mat3 rz = {{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}};
  Mat3 t;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) t[i][j] = ry[i][0] * rx[0][j] + ry[i][1] * rx[1][j] + ry[i][2] * rx[2][j];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) r[i][j] = t[i][0] * rz[0][j] + t[i][1] * rz[1][j] + t[i][2] * rz[2][j];
}

void ViewMatrix(const Camera& c, float out[3][4]) {
  Mat3 r;
  Rotation(c, r);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) out[i][j] = r[j][i];
    out[i][3] = -(r[0][i] * c.pos[0] + r[1][i] * c.pos[1] + r[2][i] * c.pos[2]);
  }
}

void Move(Camera& c, float right, float up, float back) {
  Mat3 r;
  Rotation(c, r);
  const float local[3] = {right, up, back};
  for (int i = 0; i < 3; i++) c.pos[i] += r[i][0] * local[0] + r[i][1] * local[1] + r[i][2] * local[2];
}

void Turn(Camera& c, float yaw, float pitch, float roll) {
  c.yaw = std::fmod(c.yaw + yaw, 360.f);
  c.pitch = std::clamp(c.pitch + pitch, -89.f, 89.f);
  c.roll = std::clamp(c.roll + roll, -180.f, 180.f);
}

float sMouseDx = 0.f, sMouseDy = 0.f, sWheel = 0.f;
bool sLooking = false;
bool sWantShot = false, sWantReset = false, sWantExit = false;
bool sRequested = false;
bool sActive = false;

float Axis(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
  const float v = SDL_GetGamepadAxis(pad, axis) / 32767.f;
  return std::fabs(v) < 0.15f ? 0.f : v;
}

bool GamepadHeld() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  bool held = false;
  for (int i = 0; ids != nullptr && i < count && !held; i++) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (pad == nullptr) continue;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !held; b++) {
      held = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b));
    }
    held = held || Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.f || Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.f;
  }
  SDL_free(ids);
  return held;
}

void ApplyInput(float dt) {
  const bool imguiKeys = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  float mult = 1.f;
  if (!imguiKeys && keys != nullptr) {
    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) mult = 4.f;
    if (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]) mult = 0.25f;
  }
  float right = 0, up = 0, back = 0, yaw = 0, pitch = 0, roll = 0;
  if (!imguiKeys && keys != nullptr) {
    right += (keys[SDL_SCANCODE_D] ? 1.f : 0.f) - (keys[SDL_SCANCODE_A] ? 1.f : 0.f);
    back += (keys[SDL_SCANCODE_S] ? 1.f : 0.f) - (keys[SDL_SCANCODE_W] ? 1.f : 0.f);
    up += (keys[SDL_SCANCODE_E] ? 1.f : 0.f) - (keys[SDL_SCANCODE_Q] ? 1.f : 0.f);
    roll += (keys[SDL_SCANCODE_Z] ? 1.f : 0.f) - (keys[SDL_SCANCODE_C] ? 1.f : 0.f);
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids != nullptr && i < count; i++) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (pad == nullptr) continue;
    right += Axis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    back += Axis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    up += Axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) - Axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    yaw -= Axis(pad, SDL_GAMEPAD_AXIS_RIGHTX) * 90.f * dt;
    pitch -= Axis(pad, SDL_GAMEPAD_AXIS_RIGHTY) * 90.f * dt;
    roll += (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? 1.f : 0.f) -
            (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ? 1.f : 0.f);
  }
  SDL_free(ids);
  const float step = sSpeed * mult * dt;
  Move(sCam, right * step, up * step, back * step);
  yaw -= sMouseDx * sLookSpeed;
  pitch -= sMouseDy * sLookSpeed;
  Turn(sCam, yaw, pitch, roll * 45.f * dt);
  sMouseDx = sMouseDy = 0.f;
  if (sWheel != 0.f) {
    sSpeed = std::clamp(sSpeed * std::pow(1.25f, sWheel), 1.f, 20000.f);
    sWheel = 0.f;
  }
}

AuroraReadbackStream sStream = 0;
u64 sShotKey = 0;
enum class Shot { None, Resize, Capture, Wait } sShot = Shot::None;
int sShotWait = 0;
u32 sShotW = 0, sShotH = 0;
float sScaleBefore = 1.f;
std::string sShotName;
std::string sStatus;
int sPhotosTaken = 0;

bool SavePng(const fs::path& path, const uint8_t* rgba, u32 w, u32 h) {
  return PortShot_SavePng(path.wstring().c_str(), rgba, w, h);
}

void SaveWindow(const std::string& name) { PortShot_SaveWindow((sFolder / (name + ".png")).wstring().c_str()); }

fs::path NextPhotoPath() {
  std::error_code ec;
  fs::create_directories(sFolder, ec);
  if (!sShotName.empty()) {
    return sFolder / (sShotName + ".png");
  }
  const std::time_t now = std::time(nullptr);
  std::tm local;
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char name[64];
  std::strftime(name, sizeof name, "photo_%Y%m%d_%H%M%S", &local);
  fs::path path = sFolder / (std::string(name) + ".png");
  for (int n = 2; fs::exists(path, ec); n++) {
    path = sFolder / (std::string(name) + "_" + std::to_string(n) + ".png");
  }
  return path;
}

void StartShot() {
  if (sShot != Shot::None) return;
  sScaleBefore = PortVideo_ResolutionScale();
  if (sShotScale > 0 && static_cast<float>(sShotScale) != sScaleBefore) {
    VISetFrameBufferScale(static_cast<float>(sShotScale));
    sShot = Shot::Resize;
    sShotWait = 3;
  } else {
    sShot = Shot::Capture;
  }
}

void EndShot() {
  if (sShotScale > 0 && static_cast<float>(sShotScale) != sScaleBefore) {
    VISetFrameBufferScale(sScaleBefore);
  }
  sShot = Shot::None;
  sShotName.clear();
}

void CollectShot() {
  if (sShot == Shot::Resize) {
    if (--sShotWait <= 0) sShot = Shot::Capture;
    return;
  }
  if (sShot != Shot::Wait) return;
  std::vector<uint8_t> pixels(static_cast<size_t>(sShotW) * sShotH * 4);
  AuroraReadbackDest dest{sShotKey, pixels.data(), static_cast<u32>(pixels.size()), GX_FALSE};
  AuroraReadbackGet(sStream, &dest, 1);
  if (!dest.found) {
    if (--sShotWait <= 0) {
      sStatus = "The picture never came back from the graphics card.";
      std::fprintf(stderr, "[photo] %s\n", sStatus.c_str());
      EndShot();
    }
    return;
  }
  for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
  const fs::path path = NextPhotoPath();
  if (SavePng(path, pixels.data(), sShotW, sShotH)) {
    sPhotosTaken++;
    sStatus = "Saved " + U8(path.filename()) + " (" + std::to_string(sShotW) + "x" + std::to_string(sShotH) + ")";
    std::fprintf(stderr, "[photo] saved %s (%ux%u)\n", U8(path).c_str(), sShotW, sShotH);
  } else {
    sStatus = "Could not save " + U8(path);
    std::fprintf(stderr, "[photo] could not save %s\n", U8(path).c_str());
  }
  EndShot();
}

std::vector<uint8_t> sFrame;
bool sHaveScene = false;
f32 sSceneProj[4][4];

void PickSceneProjection() {
  AuroraViewProjection list[16];
  const u32 n = AuroraGetViewOverrideProjections(list, 16);
  for (u32 i = 0; i < n && i < 16; i++) {
    const float fov = list[i].proj[1][1] > 0.f ? 2.f * std::atan(1.f / list[i].proj[1][1]) / kDeg : 0.f;
    std::fprintf(stderr, "[photo] perspective %u: %u draws from draw %u, field of view %.1f, z %.4g %.4g\n", i,
                 list[i].draws, list[i].firstDraw, fov, list[i].proj[2][2], list[i].proj[2][3]);
  }
  if (n > 0) {
    std::memcpy(sSceneProj, list[0].proj, sizeof sSceneProj);
  }
  sHaveScene = true;
}

bool EndsWithDisplayCopy(const std::vector<uint8_t>& f) {
  const size_t n = f.size();
  return n >= 5 && f[n - 5] == 0x61 && f[n - 4] == 0x52 && ((f[n - 2] >> 6) & 1) != 0;
}

void DrawFrame(bool gameCamera) {
  AuroraViewOverride o{};
  o.active = gameCamera ? GX_FALSE : GX_TRUE;
  o.hide2D = sHide2D ? GX_TRUE : GX_FALSE;
  ViewMatrix(sCam, o.view);
  const float gameFov = AuroraGetViewOverrideFovY();
  o.zoom = sCam.fov > 0.f && gameFov > 0.f ? std::tan(gameFov * 0.5f * kDeg) / std::tan(sCam.fov * 0.5f * kDeg) : 1.f;
  o.minViewportWidth = 600.f;
  o.sceneOnly = GX_TRUE;
  std::memcpy(o.sceneProj, sSceneProj, sizeof sSceneProj);
  if (!sHaveScene) {
    o.active = GX_TRUE;
    o.sceneOnly = GX_TRUE;
    std::memset(o.sceneProj, 0, sizeof o.sceneProj);
    o.hide2D = GX_FALSE;
  }
  AuroraSetViewOverride(&o);
  const bool capture = !gameCamera && sShot == Shot::Capture;
  if (capture && EndsWithDisplayCopy(sFrame)) {
    PortFreeze_Write(sFrame.data(), sFrame.size() - 5);
  } else {
    PortFreeze_Write(sFrame.data(), sFrame.size());
  }
  if (capture) {
    sShotKey++;
    if (AuroraReadbackCaptureColor(sStream, sShotKey, &sShotW, &sShotH)) {
      sShot = Shot::Wait;
      sShotWait = 120;
    } else {
      sStatus = "The picture could not be taken (no readback on this graphics card?).";
      std::fprintf(stderr, "[photo] %s\n", sStatus.c_str());
      EndShot();
    }
    if (EndsWithDisplayCopy(sFrame)) {
      PortFreeze_Write(sFrame.data() + sFrame.size() - 5, 5);
    }
  }
}

void DrawOverlay() {
  if (!sOverlay || ImGui::GetCurrentContext() == nullptr) return;
  ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);
  const std::string title = "Photo mode (" + sKeyName + ": back to the game)";
  if (!ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  ImGui::TextUnformatted("The game is paused.");
  ImGui::TextDisabled("Move: W A S D, up/down: E Q (Shift faster, Ctrl slower)");
  ImGui::TextDisabled("Look: hold the right mouse button; wheel: speed; tilt: Z C");
  ImGui::TextDisabled("Controller: sticks, triggers up/down, shoulders tilt");
  ImGui::TextDisabled("P: photo   R: reset   H: hide this window");
  ImGui::Separator();
  ImGui::SliderFloat("Speed", &sSpeed, 1.f, 20000.f, "%.0f", ImGuiSliderFlags_Logarithmic);
  const float gameFov = AuroraGetViewOverrideFovY();
  float fov = sCam.fov > 0.f ? sCam.fov : gameFov;
  if (ImGui::SliderFloat("Field of view", &fov, 5.f, 120.f, "%.0f degrees")) {
    sCam.fov = fov;
  }
  ImGui::SliderFloat("Tilt", &sCam.roll, -180.f, 180.f, "%.0f degrees");
  ImGui::Checkbox("Hide menus and text (2D)", &sHide2D);
  static const struct {
    int scale;
    const char* name;
  } kScales[] = {{0, "As the game draws"}, {1, "640x480"}, {2, "1280x960 (2x)"}, {3, "1920x1440 (3x)"},
                 {4, "2560x1920 (4x)"},   {6, "3840x2880 (6x)"}, {8, "5120x3840 (8x)"}};
  const char* current = kScales[0].name;
  for (const auto& s : kScales) {
    if (s.scale == sShotScale) current = s.name;
  }
  if (ImGui::BeginCombo("Photo size", current)) {
    for (const auto& s : kScales) {
      if (ImGui::Selectable(s.name, s.scale == sShotScale)) sShotScale = s.scale;
    }
    ImGui::EndCombo();
  }
  ImGui::Separator();
  ImGui::BeginDisabled(sShot != Shot::None);
  if (ImGui::Button("Take photo")) sWantShot = true;
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Reset camera")) sWantReset = true;
  ImGui::SameLine();
  if (ImGui::Button("Back to the game")) sWantExit = true;
  if (!sStatus.empty()) ImGui::TextWrapped("%s", sStatus.c_str());
  ImGui::TextDisabled("Photos: %s", U8(sFolder).c_str());
  ImGui::End();
}

struct Session {
  uint32_t frame;
  std::vector<std::string> steps;
};
std::vector<Session> sScript;
size_t sNextSession = 0;
const Session* sRunning = nullptr;
size_t sStep = 0;
int sWaitSteps = 0;

std::string Trim(std::string s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  return s;
}

void LoadScript(const char* text) {
  if (text == nullptr || text[0] == '\0') return;
  std::string all = text;
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
  std::fprintf(stderr, "[photo] XD_PHOTO_SCRIPT: %zu session(s)\n", sScript.size());
}

void RunScriptStep() {
  if (sRunning == nullptr || sShot != Shot::None) return;
  if (sWaitSteps > 0) {
    sWaitSteps--;
    return;
  }
  if (sStep >= sRunning->steps.size()) {
    sWantExit = true;
    return;
  }
  const std::string& step = sRunning->steps[sStep++];
  char word[32] = {};
  float a = 0, b = 0, c = 0;
  char name[200] = {};
  std::sscanf(step.c_str(), "%31s", word);
  const std::string w = word;
  std::fprintf(stderr, "[photo] script: %s\n", step.c_str());
  if (w == "wait") {
    std::sscanf(step.c_str(), "%*s %f", &a);
    sWaitSteps = static_cast<int>(a);
  } else if (w == "move") {
    std::sscanf(step.c_str(), "%*s %f %f %f", &a, &b, &c);
    Move(sCam, a, b, c);
  } else if (w == "turn") {
    std::sscanf(step.c_str(), "%*s %f %f %f", &a, &b, &c);
    Turn(sCam, a, b, c);
  } else if (w == "fov") {
    std::sscanf(step.c_str(), "%*s %f", &a);
    sCam.fov = a;
  } else if (w == "hide2d") {
    sHide2D = step.find("off") == std::string::npos;
  } else if (w == "scale") {
    std::sscanf(step.c_str(), "%*s %f", &a);
    sShotScale = static_cast<int>(a);
  } else if (w == "shot") {
    if (std::sscanf(step.c_str(), "%*s %199s", name) == 1) sShotName = name;
    sWantShot = true;
  } else if (w == "window") {
    if (std::sscanf(step.c_str(), "%*s %199s", name) == 1) SaveWindow(name);
  } else if (w == "reset") {
    sWantReset = true;
  } else if (w == "exit") {
    sWantExit = true;
  } else {
    std::fprintf(stderr, "[photo] script: unknown step \"%s\"\n", step.c_str());
  }
}

void SetLooking(bool on) {
  if (sLooking == on) return;
  sLooking = on;
  SDL_SetWindowRelativeMouseMode(aurora_get_window(), on);
}

void Run(void (*pump)(void)) {
  PortMenuMusic_SetNext(PORT_MENU_MUSIC_FADE);
  if (!PortFreeze_Begin()) {
    return;
  }
  {
    size_t size = 0;
    const uint8_t* picture = PortFreeze_Picture(&size);
    sFrame.assign(picture, picture + size);
  }
  sActive = true;
  const uint32_t frame = sProgress != nullptr ? sProgress() : 0;
  PortInput_PhotoNote("on");
  std::fprintf(stderr, "[photo] paused at game frame %u (picture of %zu bytes of commands)\n", frame, sFrame.size());
  sCam = Camera{};
  sHaveScene = false;
  std::memset(sSceneProj, 0, sizeof sSceneProj);
  sWantShot = sWantReset = sWantExit = false;
  sStatus.clear();
  const int photosBefore = sPhotosTaken;
  uint64_t last = SDL_GetTicksNS();
  bool leaving = false;
  int releaseWait = 0;
  for (;;) {
    const uint64_t now = SDL_GetTicksNS();
    const float dt = std::min(static_cast<float>(now - last) * 1e-9f, 0.1f);
    last = now;
    RunScriptStep();
    if (sWantReset) {
      sCam = Camera{};
      sWantReset = false;
    }
    if (sWantShot) {
      StartShot();
      sWantShot = false;
    }
    if (sWantExit && sShot == Shot::None && !leaving) {
      leaving = true;
      SetLooking(false);
    }
    if (!leaving) {
      ApplyInput(dt);
    }
    const bool picking = !sHaveScene;
    DrawFrame(false);
    DrawOverlay();
    aurora_end_frame();
    if (picking) {
      PickSceneProjection();
    }
    CollectShot();
    pump();
    while (!aurora_begin_frame()) {
      pump();
      SDL_Delay(10);
    }
    if (leaving && (!GamepadHeld() || ++releaseWait > 240)) {
      break;
    }
  }
  DrawFrame(true);
  PortFreeze_Present(pump);
  AuroraSetViewOverride(nullptr);
  PortFreeze_End();
  char note[64];
  std::snprintf(note, sizeof note, "off (%d photo%s)", sPhotosTaken - photosBefore,
                sPhotosTaken - photosBefore == 1 ? "" : "s");
  PortInput_PhotoNote(note);
  std::fprintf(stderr, "[photo] the game carries on at game frame %u\n", frame);
  sActive = false;
  sRequested = false;
  sRunning = nullptr;
}

int sWaitHooks = 0;

}

extern "C" {

bool PortPhoto_Init(const char* appFolder, uint32_t (*progress)(void)) {
  const engine::settings::Registry& r = engine::settings::Global();
  sEnabled = r.Bool("video.photo_mode").Get();
  sProgress = progress;
  if (!sEnabled) {
    return false;
  }
  const std::string key = r.Text("video.photo_mode_key");
  if (!key.empty()) {
    const SDL_Scancode code = SDL_GetScancodeFromName(key.c_str());
    if (code != SDL_SCANCODE_UNKNOWN) {
      sKey = code;
      sKeyName = SDL_GetScancodeName(code);
    } else {
      std::fprintf(stderr, "[photo] video.photo_mode_key: no key called \"%s\", F8 instead\n", key.c_str());
    }
  }
  const char* dir = std::getenv("XD_PHOTO_DIR");
  sFolder = dir != nullptr && dir[0] != '\0' ? fs::u8path(dir) : PortUser_Folder(appFolder) / "photos";
  LoadScript(std::getenv("XD_PHOTO_SCRIPT"));
  sStream = AuroraReadbackCreateStream();
  PortFreeze_Capture();
  std::fprintf(stderr, "[photo] on: %s in the game window pauses the game for photos (%s)\n", sKeyName.c_str(),
               U8(sFolder).c_str());
  return true;
}

void PortPhoto_HandleEvent(const void* sdlEvent) {
  if (!sEnabled) return;
  const auto* e = static_cast<const SDL_Event*>(sdlEvent);
  const bool imguiMouse = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
  if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
    if (e->key.scancode == sKey) {
      if (sActive) {
        sWantExit = true;
      } else {
        sRequested = !sRequested;
      }
      return;
    }
    if (!sActive) return;
    switch (e->key.scancode) {
    case SDL_SCANCODE_ESCAPE: sWantExit = true; break;
    case SDL_SCANCODE_P: sWantShot = true; break;
    case SDL_SCANCODE_R: sWantReset = true; break;
    case SDL_SCANCODE_H: sOverlay = !sOverlay; break;
    default: break;
    }
    return;
  }
  if (!sActive) return;
  if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) sWantShot = true;
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) sWantReset = true;
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_WEST) sHide2D = !sHide2D;
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_START || e->gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
      sWantExit = true;
    }
  } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && e->button.button == SDL_BUTTON_RIGHT && !imguiMouse) {
    SetLooking(true);
  } else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP && e->button.button == SDL_BUTTON_RIGHT) {
    SetLooking(false);
  } else if (e->type == SDL_EVENT_MOUSE_MOTION && sLooking) {
    sMouseDx += e->motion.xrel;
    sMouseDy += e->motion.yrel;
  } else if (e->type == SDL_EVENT_MOUSE_WHEEL && !imguiMouse) {
    sWheel += e->wheel.y;
  }
}

void PortPhoto_Frame(void (*pump)(void)) {
  if (!sEnabled || sActive) return;
  if (sNextSession < sScript.size() && sProgress != nullptr && sProgress() >= sScript[sNextSession].frame) {
    sRunning = &sScript[sNextSession++];
    sStep = 0;
    sWaitSteps = 0;
    sRequested = true;
  }
  if (!sRequested) {
    sWaitHooks = 0;
    return;
  }
  if (!PortFreeze_Ready(&sWaitHooks)) {
    return;
  }
  Run(pump);
}

bool PortPhoto_Active(void) { return sActive; }

}
