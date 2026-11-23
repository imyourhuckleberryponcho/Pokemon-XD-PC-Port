#include "i18n.h"
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
uint32_t VIGetRetraceCount(void);
bool PortTime_IsDeterministic(void);
}

namespace {

namespace ec = engine::controls;
namespace es = engine::settings;
using engine::i18n::Tr;

constexpr size_t kFastForward = static_cast<size_t>(ec::Action::FastForward);
constexpr size_t kBreakAction = static_cast<size_t>(ec::Action::SpeedrunBreak);
constexpr size_t kBarAction = static_cast<size_t>(ec::Action::ReplayBar);
constexpr size_t kCheatAction = static_cast<size_t>(ec::Action::CheatMenu);
std::atomic<bool> sCheatModsHeld{false};
uint32_t sCheatKeyPresses = 0;
uint32_t sBarKeyPresses = 0;
uint32_t sBreakKeyPresses = 0;

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
  es::EnumSetting ffMusic, ffEffects;
  es::IntSetting ffFade;
  int32_t ffRealtime = -1, ffMuted = -1, ffTape = -1;
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
std::atomic<float> sReplayFactor{1.0f};
uint32_t sLastFlip = 0;
std::chrono::steady_clock::time_point sLastFlipTime = std::chrono::steady_clock::now();
uint32_t sLastStallFlip = 0; float sLastStallSpeed = 0.f;
uint32_t sRecoverUntilFlip = 0, sCapSince = 0;
bool sRecovering = false;
uint32_t sStalls = 0;
bool sGaveUp = false;
float sSpeedCap = 16.0f;
std::vector<uint32_t> sGaps;
uint32_t sGapField = 0;
std::chrono::steady_clock::time_point sGapWindow = std::chrono::steady_clock::now();
int sBehindWindows = 0;
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

bool IsActionKey(uint16_t scancode, size_t action) {
  for (int port = 0; port < ec::kPorts; port++) {
    for (const ec::Input& in : ActionsOf(port).keys[action]) {
      if (in.kind == ec::Input::Kind::Key && in.code == scancode) {
        return true;
      }
    }
  }
  return false;
}

bool IsFastForwardKey(uint16_t scancode) { return IsActionKey(scancode, kFastForward); }

bool PadInputHeld(SDL_Gamepad* pad, int port, const ec::Input& in) {
  switch (in.kind) {
  case ec::Input::Kind::Button: return SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(in.code));
  case ec::Input::Kind::Label:
    return SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(ec::LabelPosition(
                                         static_cast<ec::FaceLabel>(in.code), port_controls::PortDevice(port))));
  case ec::Input::Kind::Axis: {
    const float v = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(in.code)) / 32767.f;
    return (in.sign > 0 && v > 0.5f) || (in.sign < 0 && v < -0.5f);
  }
  default: return false;
  }
}

SDL_Gamepad* PadOfPort(int port) {
  const aurora::input::SourceId source = aurora::pad::port_controller(static_cast<uint32_t>(port));
  if (source == aurora::input::kInvalidSourceId) {
    return nullptr;
  }
  return SDL_GetGamepadFromID(aurora::input::gamepad_for_source(source));
}

bool PadHeldFor(size_t action) {
  for (int port = 0; port < ec::kPorts; port++) {
    const std::vector<ec::Input>& list = ActionsOf(port).pad[action];
    if (list.empty()) {
      continue;
    }
    SDL_Gamepad* pad = PadOfPort(port);
    if (pad == nullptr) {
      continue;
    }
    for (const ec::Input& in : list) {
      if (PadInputHeld(pad, port, in)) return true;
    }
  }
  return false;
}

bool PadChordFor(size_t action, bool last) {
  for (int port = 0; port < ec::kPorts; port++) {
    const std::vector<ec::Input>& list = ActionsOf(port).pad[action];
    if (list.empty()) {
      continue;
    }
    SDL_Gamepad* pad = PadOfPort(port);
    if (pad == nullptr) {
      continue;
    }
    bool all = true;
    for (size_t i = 0; i + 1 < list.size(); i++) {
      all = all && PadInputHeld(pad, port, list[i]);
    }
    if (all && (!last || PadInputHeld(pad, port, list.back()))) return true;
  }
  return false;
}

bool PadHeld() { return PadHeldFor(kFastForward); }

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
      if (!e->key.repeat && IsActionKey(code, kBreakAction)) sBreakKeyPresses++;
      if (!e->key.repeat && IsActionKey(code, kBarAction)) sBarKeyPresses++;
      if (!e->key.repeat && IsActionKey(code, kCheatAction)) sCheatKeyPresses++;
    }
  } else if (e->type == SDL_EVENT_KEY_UP) {
    sKeysDown.erase(std::remove(sKeysDown.begin(), sKeysDown.end(), static_cast<uint16_t>(e->key.scancode)),
                    sKeysDown.end());
  } else if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    sKeysDown.clear();
  }
}

uint32_t PortSpeedups_ReplayBarPresses(void) {
  static bool padWas = false;
  uint32_t n = sBarKeyPresses;
  sBarKeyPresses = 0;
  const bool held = PadHeldFor(kBarAction);
  if (held && !padWas) n++;
  padWas = held;
  return n;
}

uint32_t PortSpeedups_CheatMenuPresses(void) {
  static bool padWas = false;
  uint32_t n = sCheatKeyPresses;
  sCheatKeyPresses = 0;
  const bool held = PadChordFor(kCheatAction, true);
  if (held && !padWas) n++;
  padWas = held;
  bool mods = false;
  for (int port = 0; port < ec::kPorts && !mods; port++) {
    mods = ActionsOf(port).pad[kCheatAction].size() >= 2 && PadChordFor(kCheatAction, false);
  }
  sCheatModsHeld.store(mods, std::memory_order_relaxed);
  return n;
}

bool PortSpeedups_CheatChordModifiersHeld(void) { return sCheatModsHeld.load(std::memory_order_relaxed); }

uint32_t PortSpeedups_BreakPresses(void) {
  static bool padWas = false;
  uint32_t n = sBreakKeyPresses;
  sBreakKeyPresses = 0;
  const bool held = PadHeldFor(kBreakAction);
  if (held && !padWas) n++;
  padWas = held;
  return n;
}

extern "C" void PortFfMusic_Frame(int factor, int musicMode, int effectsMode, int fadeMs);

void PortSpeedups_Frame(bool gameWants) {
  es::Registry& r = es::Global();
  if (!sH.ready) {
    sH.ready = true;
    sH.on = r.Bool("gameplay.fast_forward");
    sH.speed = r.Int("gameplay.fast_forward_speed");
    sH.mode = r.Enum("gameplay.fast_forward_mode");
    sH.toggle = r.ChoiceIndex("gameplay.fast_forward_mode", "toggle");
    sH.base = r.Float("system.game_speed");
    sH.ffMusic = r.Enum("audio.ff_music");
    sH.ffEffects = r.Enum("audio.ff_effects");
    sH.ffFade = r.Int("audio.ff_music_fade");
    sH.ffRealtime = r.ChoiceIndex("audio.ff_music", "realtime");
    sH.ffMuted = r.ChoiceIndex("audio.ff_effects", "muted");
    sH.ffTape = r.ChoiceIndex("audio.ff_effects", "tape");
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
      PortVideo_Notice(sFastByKey ? Tr("Fast-forward switched itself off: this PC can't keep up with it.")
                                  : Tr("Fast battle animations switched themselves off: this PC can't keep up with them."),
                       6.0);
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
  {
    static uint32_t lastFlip = 0;
    static const bool deterministic = PortTime_IsDeterministic();
    const uint32_t field = VIGetRetraceCount();
    if (flips != lastFlip) {
      if (sFast && !deterministic && field > sGapField) sGaps.push_back(field - sGapField);
      lastFlip = flips;
      sGapField = field;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!sFast) {
      sGaps.clear();
      sBehindWindows = 0;
      sGapWindow = now;
    } else if (now - sGapWindow >= std::chrono::milliseconds(500)) {
      sGapWindow = now;
      if (sGaps.size() >= 3) {
        std::nth_element(sGaps.begin(), sGaps.begin() + static_cast<std::ptrdiff_t>(sGaps.size() / 2), sGaps.end());
        const float median = static_cast<float>(sGaps[sGaps.size() / 2]);
        sBehindWindows = median >= 3.0f ? sBehindWindows + 1 : 0;
        if (sBehindWindows >= 2) {
          const float manages = std::max(2.0f * sBaseNow.load(), std::floor(sAppliedSpeed * 2.0f / median));
          if (manages < sAppliedSpeed) {
            sSpeedCap = manages;
            sCapSince = flips;
            char line[160];
            std::snprintf(line, sizeof line, "%s eases off: the PC manages about %gx, not %gx (pictures %g fields apart)",
                          sFastByKey ? "fast-forward" : "fast battle animations", static_cast<double>(manages),
                          static_cast<double>(sAppliedSpeed), static_cast<double>(median));
            Note(line);
          }
          sBehindWindows = 0;
        }
      }
      sGaps.clear();
    }
  }
  want = want && !sRecovering && !sGaveUp;
  const float replayFactor = sReplayFactor.load();
  const float base = sH.base.Valid() && sH.base.Get() > 1.0f ? sH.base.Get() : 1.0f;
  sBaseNow.store(base);
  const float fast = static_cast<float>(std::clamp(sH.speed.Valid() ? sH.speed.Get() : 4, 2, 16));
  float target = want ? std::max(base, std::min(fast, sSpeedCap)) : base;
  PortAudio_SetSlowMotion(replayFactor < 1.0f ? replayFactor : 1.0f);
  if (replayFactor != 1.0f) {
    target = std::min(16.0f, base * replayFactor);
    if (replayFactor > 1.0f && !want) {
      want = true;
      byKey = true;
    }
  }
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
    if (sFast) PortAudio_SetFastForward(static_cast<int>(target / base + 0.5f));
  }
  {
    const int music = sH.ffMusic.Valid() && sH.ffMusic.Get() == sH.ffRealtime ? 1 : 0;
    const int fx = !sH.ffEffects.Valid() ? 0 : sH.ffEffects.Get() == sH.ffMuted ? 1 : sH.ffEffects.Get() == sH.ffTape ? 2 : 0;
    PortFfMusic_Frame(sFast ? static_cast<int>(target / base + 0.5f) : 1, music, fx, sH.ffFade.Valid() ? sH.ffFade.Get() : 400);
  }
}

bool PortSpeedups_FastForwarding(void) { return sFast; }

void PortSpeedups_SetReplayFactor(float factor) { sReplayFactor.store(std::clamp(factor, 0.25f, 16.0f)); }

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
