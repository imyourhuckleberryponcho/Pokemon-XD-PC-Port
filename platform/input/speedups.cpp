#include <port_speedups.h>

#include <controls.h>
#include <port_audio.h>
#include <port_controls.h>
#include <port_fps.h>
#include <port_video.h>
#include <settings.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <aurora/time.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
void PortTime_SetSpeed(float speed);
bool PortCARD_Busy(void);
void PortInput_EnhancedNote(const char* what);
uint32_t PortVI_GetFlipCount(void);
bool PortTime_IsDeterministic(void);
}

namespace {

namespace ec = engine::controls;
namespace es = engine::settings;

constexpr size_t kFastForward = static_cast<size_t>(ec::Action::FastForward);

std::vector<uint16_t> sKeysDown;
uint32_t sKeyPresses = 0;

std::array<ec::ActionMap, ec::kPorts> sActions;
uint32_t sActionsChanges = ~0u;

struct Handles {
  bool ready = false;
  es::BoolSetting on;
  es::IntSetting speed;
  es::EnumSetting mode;
  int32_t toggle = -1;
  es::FloatSetting base;
} sH;

bool sToggled = false;
bool sPadWasHeld = false;
uint32_t sKeyPressesSeen = 0;
float sAppliedSpeed = 0.f;
bool sFast = false;
bool sFastByKey = false;
bool sWaitingForCard = false;
constexpr auto kStallTime = std::chrono::milliseconds(1000);
std::atomic<bool> sFastNow{false};
std::atomic<bool> sStalled{false};
std::atomic<float> sBaseNow{1.0f};
uint32_t sLastFlip = 0;
std::chrono::steady_clock::time_point sLastFlipTime = std::chrono::steady_clock::now();
uint32_t sLastStallFlip = 0; float sLastStallSpeed = 0.f;
uint32_t sRecoverUntilFlip = 0, sCapSince = 0;
bool sRecovering = false;
uint32_t sStalls = 0;
bool sGaveUp = false;
float sSpeedCap = 16.0f;
std::mutex sMarksLock;
std::vector<std::string> sUsed;
uint32_t sFirstFrame = 0;
std::string sReport;

const ec::ActionMap& ActionsOf(int port) {
  const es::Registry& r = es::Global();
  if (sActionsChanges != r.Changes()) {
    sActionsChanges = r.Changes();
    for (int p = 0; p < ec::kPorts; p++) {
      sActions[static_cast<size_t>(p)] = ec::ReadPort(r, p).actions;
    }
  }
  return sActions[static_cast<size_t>(port)];
}

bool KeyHeld() {
  for (int port = 0; port < ec::kPorts; port++) {
    for (const ec::Input& in : ActionsOf(port).keys[kFastForward]) {
      if (in.kind == ec::Input::Kind::Key && std::find(sKeysDown.begin(), sKeysDown.end(), in.code) != sKeysDown.end()) {
        return true;
      }
    }
  }
  return false;
}

bool IsFastForwardKey(uint16_t scancode) {
  for (int port = 0; port < ec::kPorts; port++) {
    for (const ec::Input& in : ActionsOf(port).keys[kFastForward]) {
      if (in.kind == ec::Input::Kind::Key && in.code == scancode) {
        return true;
      }
    }
  }
  return false;
}

bool PadHeld() {
  for (int port = 0; port < ec::kPorts; port++) {
    const std::vector<ec::Input>& list = ActionsOf(port).pad[kFastForward];
    if (list.empty()) {
      continue;
    }
    const aurora::input::SourceId source = aurora::pad::port_controller(static_cast<uint32_t>(port));
    if (source == aurora::input::kInvalidSourceId) {
      continue;
    }
    SDL_Gamepad* pad = SDL_GetGamepadFromID(aurora::input::gamepad_for_source(source));
    if (pad == nullptr) {
      continue;
    }
    for (const ec::Input& in : list) {
      switch (in.kind) {
      case ec::Input::Kind::Button:
        if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(in.code))) return true;
        break;
      case ec::Input::Kind::Label:
        if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(ec::LabelPosition(
                                          static_cast<ec::FaceLabel>(in.code), port_controls::PortDevice(port))))) {
          return true;
        }
        break;
      case ec::Input::Kind::Axis: {
        const float v = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(in.code)) / 32767.f;
        if ((in.sign > 0 && v > 0.5f) || (in.sign < 0 && v < -0.5f)) return true;
        break;
      }
      default: break;
      }
    }
  }
  return false;
}

void Note(const std::string& what) { PortInput_EnhancedNote(what.c_str()); }

}

extern "C" {

void PortSpeedups_HandleEvent(const void* sdlEvent) {
  const auto* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type == SDL_EVENT_KEY_DOWN) {
    const auto code = static_cast<uint16_t>(e->key.scancode);
    if (std::find(sKeysDown.begin(), sKeysDown.end(), code) == sKeysDown.end()) {
      sKeysDown.push_back(code);
      if (!e->key.repeat && IsFastForwardKey(code)) {
        sKeyPresses++;
      }
    }
  } else if (e->type == SDL_EVENT_KEY_UP) {
    sKeysDown.erase(std::remove(sKeysDown.begin(), sKeysDown.end(), static_cast<uint16_t>(e->key.scancode)),
                    sKeysDown.end());
  } else if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    sKeysDown.clear();
  }
}

void PortSpeedups_Frame(bool gameWants) {
  es::Registry& r = es::Global();
  if (!sH.ready) {
    sH.ready = true;
    sH.on = r.Bool("gameplay.fast_forward");
    sH.speed = r.Int("gameplay.fast_forward_speed");
    sH.mode = r.Enum("gameplay.fast_forward_mode");
    sH.toggle = r.ChoiceIndex("gameplay.fast_forward_mode", "toggle");
    sH.base = r.Float("system.game_speed");
    sAppliedSpeed = aurora::time::scale();
  }
  bool byKey = false;
  if (sH.on.Get()) {
    const bool padHeld = PadHeld();
    const bool held = KeyHeld() || padHeld;
    if (sH.mode.Valid() && sH.mode.Get() == sH.toggle) {
      const bool pressed = sKeyPresses != sKeyPressesSeen || (padHeld && !sPadWasHeld);
      if (pressed) {
        sToggled = !sToggled;
      }
      byKey = sToggled;
    } else {
      sToggled = false;
      byKey = held;
    }
    sPadWasHeld = padHeld;
  } else {
    sToggled = false;
    sPadWasHeld = false;
  }
  sKeyPressesSeen = sKeyPresses;

  bool want = (byKey || gameWants) && !sGaveUp;
  const bool card = want && PortCARD_Busy();
  if (card != sWaitingForCard) {
    sWaitingForCard = card;
    if (card) {
      Note("fast-forward waits: memory card at work");
    }
  }
  want = want && !card;

  const uint32_t flips = PortVI_GetFlipCount();
  if (sStalled.exchange(false)) {
    sRecoverUntilFlip = flips + 60;
    if (sLastStallSpeed == sAppliedSpeed && flips - sLastStallFlip < 600) {
      sCapSince = flips;
      sSpeedCap = std::max(2.0f, std::floor(sAppliedSpeed / 2.0f));
    }
    sStalls++;
    static const bool mayGiveUp = [] {
      const char* e = std::getenv("XD_FAST_FORWARD_GIVE_UP");
      return !(e != nullptr && e[0] == '0');
    }();
    const bool atLowest = sLastStallSpeed == sAppliedSpeed && flips - sLastStallFlip < 600 &&
                          sAppliedSpeed <= 2.0f * sBaseNow.load();
    if (mayGiveUp && !sGaveUp && (atLowest || sStalls >= 4)) {
      sGaveUp = true;
      Note("fast-forward off until the game is started again: the PC can't keep up with it");
      PortVideo_Notice("Fast-forward switched itself off: this PC can't keep up with it.", 6.0);
    }
    sLastStallFlip = flips;
    sLastStallSpeed = sAppliedSpeed;
    if (!sRecovering && !sGaveUp) {
      sRecovering = true;
      char line[160];
      std::snprintf(line, sizeof line, "fast-forward waits: the PC can't keep up at %gx (no new frame for 1 s); "
                    "then at most %gx", static_cast<double>(sAppliedSpeed), static_cast<double>(sSpeedCap));
      Note(line);
    }
    sAppliedSpeed = aurora::time::scale();
  }
  if (sRecovering && static_cast<int32_t>(flips - sRecoverUntilFlip) >= 0) {
    sRecovering = false;
  }
  if (sSpeedCap < 16.0f && flips - sCapSince > 1800) {
    sSpeedCap = 16.0f;
  }
  want = want && !sRecovering && !sGaveUp;
  const float base = sH.base.Valid() && sH.base.Get() > 1.0f ? sH.base.Get() : 1.0f;
  sBaseNow.store(base);
  const float fast = static_cast<float>(std::clamp(sH.speed.Valid() ? sH.speed.Get() : 4, 2, 16));
  const float target = want ? std::max(base, std::min(fast, sSpeedCap)) : base;
  if (want != sFast || (want && byKey != sFastByKey)) {
    if (want) {
      PortSpeedups_Used(byKey ? "fast-forward" : "fast battle animations");
      char line[96];
      std::snprintf(line, sizeof line, "fast-forward on %gx (%s)", static_cast<double>(target),
                    byKey ? "key" : "battle animation");
      Note(line);
    } else {
      Note("fast-forward off");
    }
    sFast = want;
    sFastNow.store(want);
    sFastByKey = byKey;
    PortVideo_SetFastForward(want);
    PortAudio_SetFastForward(want ? static_cast<int>(target / base + 0.5f) : 1);
  }
  if (target != sAppliedSpeed) {
    PortTime_SetSpeed(target);
    sAppliedSpeed = target;
  }
}

bool PortSpeedups_FastForwarding(void) { return sFast; }

void PortSpeedups_Field(uint32_t field) {
  (void)field;
  const uint32_t flips = PortVI_GetFlipCount();
  const auto now = std::chrono::steady_clock::now();
  if (flips != sLastFlip) {
    sLastFlip = flips;
    sLastFlipTime = now;
    return;
  }
  if (sFastNow.load() && now - sLastFlipTime > kStallTime && !sStalled.load()) {
    static bool deterministic = PortTime_IsDeterministic();
    if (deterministic) return;
    aurora::time::set_scale(sBaseNow.load());
    sFastNow.store(false);
    sStalled.store(true);
  }
}

void PortSpeedups_Used(const char* what) {
  if (what == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(sMarksLock);
    for (const std::string& u : sUsed) {
      if (u.rfind(what, 0) == 0) return;
    }
    const uint32_t frame = PortVI_GetFlipCount();
    if (sUsed.empty()) sFirstFrame = frame;
    sUsed.push_back(std::string(what) + " (frame " + std::to_string(frame) + ")");
  }
  Note(std::string("first use: ") + what);
}

const char* PortSpeedups_Report(void) {
  std::lock_guard<std::mutex> lock(sMarksLock);
  if (sUsed.empty()) {
    sReport = "speed-ups: none used";
    return sReport.c_str();
  }
  sReport = "speed-ups: Enhanced from game frame " + std::to_string(sFirstFrame) + ":";
  for (size_t i = 0; i < sUsed.size(); i++) {
    sReport += (i ? ", " : " ") + sUsed[i];
  }
  return sReport.c_str();
}

}
