#include "port_controls.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <aurora/input.hpp>
#include <aurora/pad.hpp>
#include <settings.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ec = engine::controls;

static_assert(static_cast<int>(ec::PadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH &&
              static_cast<int>(ec::PadButton::Start) == SDL_GAMEPAD_BUTTON_START &&
              static_cast<int>(ec::PadButton::LeftShoulder) == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER &&
              static_cast<int>(ec::PadButton::DpadRight) == SDL_GAMEPAD_BUTTON_DPAD_RIGHT &&
              static_cast<int>(ec::PadButton::Touchpad) == SDL_GAMEPAD_BUTTON_TOUCHPAD &&
              static_cast<int>(ec::PadButton::Misc3) == SDL_GAMEPAD_BUTTON_MISC3 &&
              static_cast<int>(ec::PadButton::Count) == SDL_GAMEPAD_BUTTON_COUNT);
static_assert(static_cast<int>(ec::PadAxis::LeftY) == SDL_GAMEPAD_AXIS_LEFTY &&
              static_cast<int>(ec::PadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER &&
              static_cast<int>(ec::PadAxis::Count) == SDL_GAMEPAD_AXIS_COUNT);
static_assert(SDL_SCANCODE_A == 4 && SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_RIGHT == 79 &&
              SDL_SCANCODE_UP == 82 && SDL_SCANCODE_KP_0 == 98 && SDL_SCANCODE_LSHIFT == 225);

namespace {

using aurora::binding::PhysicalInput;
using aurora::pad::ProfileTarget;
using Direction = PhysicalInput::GamepadAxis::Direction;

std::string ControlName(ec::Target target) { return std::string("aurora.pad.") + ec::TargetId(target); }

PhysicalInput ToAurora(const ec::Input& in) {
  PhysicalInput p;
  switch (in.kind) {
  case ec::Input::Kind::Key: p.control = PhysicalInput::Key{static_cast<SDL_Scancode>(in.code)}; break;
  case ec::Input::Kind::Button:
    p.control = PhysicalInput::GamepadButton{static_cast<SDL_GamepadButton>(in.code)};
    break;
  case ec::Input::Kind::Axis:
    p.control = PhysicalInput::GamepadAxis{static_cast<SDL_GamepadAxis>(in.code),
                                           in.sign > 0   ? Direction::Positive
                                           : in.sign < 0 ? Direction::Negative
                                                         : Direction::Full};
    break;
  default: break;
  }
  return p;
}

aurora::binding::Binding ToAurora(const ec::Binding& b) {
  aurora::binding::Binding out;
  out.input = ToAurora(b.input);
  out.target = aurora::binding::find_control(ControlName(b.target));
  out.scale = b.scale;
  out.threshold = b.threshold;
  out.deadZone = b.deadZone;
  if (b.held.kind != ec::Input::Kind::None) {
    out.held.push_back({ToAurora(b.held), 0.5f});
  }
  return out;
}

bool HasOverrides(ProfileTarget target) {
  const aurora::pad::Profile* p = aurora::pad::profile(target);
  if (p == nullptr) {
    return false;
  }
  for (size_t t = 0; t < ec::kTargetCount; t++) {
    if (p->bindings.find(ControlName(static_cast<ec::Target>(t))) != p->bindings.end()) {
      return true;
    }
  }
  return false;
}

void SetAll(ProfileTarget target, const std::vector<ec::Binding>& bindings) {
  (void)aurora::pad::controls();
  for (size_t t = 0; t < ec::kTargetCount; t++) {
    std::vector<aurora::binding::Binding> list;
    for (const ec::Binding& b : bindings) {
      if (static_cast<size_t>(b.target) == t) {
        list.push_back(ToAurora(b));
      }
    }
    aurora::pad::set_binding(target, ControlName(static_cast<ec::Target>(t)), std::move(list));
  }
}

SDL_Gamepad* GamepadOf(aurora::input::SourceId source) {
  return source != aurora::input::kInvalidSourceId ? SDL_GetGamepadFromID(aurora::input::gamepad_for_source(source))
                                                   : nullptr;
}

ec::Device Classify(SDL_Gamepad* gamepad) {
  if (gamepad == nullptr) {
    return ec::Device::Keyboard;
  }
  switch (SDL_GetGamepadType(gamepad)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
  case SDL_GAMEPAD_TYPE_XBOXONE: return ec::Device::Xbox;
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5: return ec::Device::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    return SDL_GetGamepadProduct(gamepad) == 0x2073 ? ec::Device::NsoGameCube : ec::Device::Switch;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return ec::Device::Switch;
  case SDL_GAMEPAD_TYPE_GAMECUBE: return ec::Device::GameCube;
  default: return ec::Device::Generic;
  }
}

struct Applied {
  bool done = false;
  ec::PortMap map;
  aurora::input::SourceId controller = aurora::input::kInvalidSourceId;
};
std::array<Applied, ec::kPorts> sApplied;
uint32_t sChanges = 0;
bool sStarted = false;
bool sLastWasKeyboard = false;
uint32_t sDeviceChanges = 0;

void ApplyKeyboard(int port, const ec::KeyboardMap& map) {
  const ProfileTarget target = ProfileTarget::keyboard(static_cast<uint32_t>(port));
  if (map == ec::DefaultKeyboard(port)) {
    if (HasOverrides(target)) {
      aurora::pad::reset_to_defaults(target);
    }
    return;
  }
  SetAll(target, ec::ResolveKeyboard(map));
}

void ApplyController(int port, const ec::GamepadMap& map, aurora::input::SourceId source) {
  if (source == aurora::input::kInvalidSourceId) {
    return;
  }
  const ProfileTarget target = ProfileTarget::controller(source);
  const aurora::pad::Profile* profile = aurora::pad::profile(target);
  if (profile == nullptr) {
    return;
  }
  aurora::pad::ProfileSettings settings = profile->settings;
  settings.leftStickDeadZone = map.leftStickDeadzone;
  settings.rightStickDeadZone = map.rightStickDeadzone;
  settings.leftTriggerThreshold = map.lClickThreshold;
  settings.rightTriggerThreshold = map.rClickThreshold;
  if (!map.rumble) {
    settings.rumbleIntensityLow = settings.rumbleIntensityHigh = 0.f;
  } else if (settings.rumbleIntensityLow == 0.f && settings.rumbleIntensityHigh == 0.f) {
    settings.rumbleIntensityLow = aurora::pad::ProfileSettings{}.rumbleIntensityLow;
    settings.rumbleIntensityHigh = aurora::pad::ProfileSettings{}.rumbleIntensityHigh;
  }
  if (!(settings == profile->settings) && !aurora::pad::set_settings(target, settings)) {
    std::fprintf(stderr, "[platform/input] controller %d: Aurora refused the deadzone / click settings\n", port + 1);
  }
  const bool emulate = settings.emulateTriggers;
  const ec::Device device = ec::EffectiveDevice(map, Classify(GamepadOf(source)));
  if (ec::UsesDefaultButtons(map, device)) {
    if (HasOverrides(target)) {
      aurora::pad::reset_to_defaults(target);
    }
    return;
  }
  SetAll(target, ec::ResolveGamepad(map, device, emulate));
}

struct Pads {
  std::vector<aurora::input::SourceId> sources;
  std::vector<ec::ConnectedPad> pads;
};
Pads ConnectedPads() {
  std::vector<std::pair<aurora::input::SourceId, ec::ConnectedPad>> found;
  for (const auto& source : aurora::input::sources()) {
    if (source.kind != aurora::input::InputSource::Kind::Controller) {
      continue;
    }
    if (const auto id = aurora::pad::controller_identity(source.id)) {
      found.push_back({source.id, {id->guid, id->name, id->vid, id->pid}});
    }
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  Pads out;
  for (auto& [source, pad] : found) {
    out.sources.push_back(source);
    out.pads.push_back(std::move(pad));
  }
  return out;
}

std::array<ec::ControllerChoice, ec::kPorts> sChoices;
bool sChoicesApplied = false;
std::vector<aurora::input::SourceId> sPadSources;
aurora::input::SourceId sLastPressedPad = aurora::input::kInvalidSourceId;
aurora::input::SourceId sLastPressedApplied = aurora::input::kInvalidSourceId;
aurora::input::SourceId sAutoPick = aurora::input::kInvalidSourceId;

aurora::input::SourceId AutoPick(const Pads& pads, bool followPresses) {
  int last = -1;
  for (size_t i = 0; i < pads.sources.size(); i++) {
    if (followPresses && pads.sources[i] == sLastPressedPad) {
      last = static_cast<int>(i);
    }
  }
  const int pick = ec::PreferredAutoPad(pads.pads, last);
  return pick >= 0 ? pads.sources[static_cast<size_t>(pick)] : aurora::input::kInvalidSourceId;
}

void ApplyChoices(const std::array<ec::ControllerChoice, ec::kPorts>& choices, const Pads& pads) {
  const aurora::input::SourceId autoPick =
      choices[0].mode == ec::ControllerChoice::Mode::Auto ? AutoPick(pads, ec::AutoFollowsPresses(choices)) : aurora::input::kInvalidSourceId;
  if (autoPick != sAutoPick && choices[0].mode == ec::ControllerChoice::Mode::Auto) {
    sAutoPick = autoPick;
    if (autoPick != aurora::input::kInvalidSourceId) {
      std::string name;
      bool builtIn = false;
      for (size_t i = 0; i < pads.sources.size(); i++) {
        if (pads.sources[i] == autoPick) {
          name = pads.pads[i].name;
          builtIn = ec::IsBuiltInPad(pads.pads[i]);
        }
      }
      std::fprintf(stderr, "[platform/input] controller 1: auto -> %s (%s)\n", name.c_str(),
                   autoPick == sLastPressedPad && ec::AutoFollowsPresses(choices) ? "pressed most recently"
                   : builtIn                  ? "built-in"
                                              : "outside controller preferred over built-in");
    } else {
      std::fprintf(stderr, "[platform/input] controller 1: auto -> the first free PC controller\n");
    }
  }
  if (choices[0].mode != ec::ControllerChoice::Mode::Auto) {
    sAutoPick = aurora::input::kInvalidSourceId;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int port = 0; port < ec::kPorts; port++) {
      const ec::ControllerChoice& c = choices[static_cast<size_t>(port)];
      const uint32_t p = static_cast<uint32_t>(port);
      const bool autoPinned = port == 0 && c.mode == ec::ControllerChoice::Mode::Auto && autoPick != aurora::input::kInvalidSourceId;
      const bool pinned = c.mode == ec::ControllerChoice::Mode::Pinned || autoPinned;
      if ((pass == 0) != pinned) {
        continue;
      }
      if (autoPinned) {
        if (aurora::pad::port_controller(p) != autoPick) {
          aurora::pad::set_port_controller(p, autoPick);
        }
        continue;
      }
      if (c.mode == ec::ControllerChoice::Mode::Auto) {
        if (aurora::pad::port_preference(p).state != aurora::pad::PortAssignment::Auto) {
          aurora::pad::set_port_auto(p);
        }
        continue;
      }
      if (c.mode == ec::ControllerChoice::Mode::None) {
        if (aurora::pad::port_preference(p).state != aurora::pad::PortAssignment::None) {
          aurora::pad::clear_port(p);
        }
        continue;
      }
      aurora::input::SourceId found = aurora::input::kInvalidSourceId;
      for (size_t i = 0; i < pads.pads.size(); i++) {
        if (pads.pads[i].guid == c.guid && ec::NthOfModel(pads.pads, i) == c.nth) {
          found = pads.sources[i];
          break;
        }
      }
      if (found != aurora::input::kInvalidSourceId) {
        if (aurora::pad::port_controller(p) != found) {
          aurora::pad::set_port_controller(p, found);
        }
      } else if (aurora::pad::port_preference(p).state != aurora::pad::PortAssignment::None) {
        aurora::pad::clear_port(p);
      }
    }
  }
}

void UpdateChoices(const engine::settings::Registry& registry, bool settingsChanged) {
  std::array<ec::ControllerChoice, ec::kPorts> choices = sChoices;
  if (settingsChanged || !sChoicesApplied) {
    for (int port = 0; port < ec::kPorts; port++) {
      choices[static_cast<size_t>(port)] = ec::ReadPort(registry, port).controller;
    }
  }
  Pads pads = ConnectedPads();
  const bool newChoices = !sChoicesApplied || choices != sChoices;
  if (!newChoices && pads.sources == sPadSources && sLastPressedPad == sLastPressedApplied) {
    return;
  }
  sLastPressedApplied = sLastPressedPad;
  ApplyChoices(choices, pads);
  if (newChoices) {
    for (int port = 0; port < ec::kPorts; port++) {
      const ec::ControllerChoice& c = choices[static_cast<size_t>(port)];
      const std::string what = c.mode == ec::ControllerChoice::Mode::Auto   ? "auto (the first free PC controller)"
                               : c.mode == ec::ControllerChoice::Mode::None ? "no PC controller"
                                                                            : "pinned: " + (c.name.empty() ? c.guid : c.name);
      std::fprintf(stderr, "[platform/input] controller %d: %s\n", port + 1, what.c_str());
    }
  }
  sChoices = choices;
  sPadSources = std::move(pads.sources);
  sChoicesApplied = true;
}

}

namespace port_controls {

ec::Device PortDevice(int port) {
  if (port < 0 || port >= ec::kPorts) {
    return ec::Device::Keyboard;
  }
  const aurora::input::SourceId source = aurora::pad::port_controller(static_cast<uint32_t>(port));
  if (source == aurora::input::kInvalidSourceId) {
    return ec::Device::Keyboard;
  }
  return ec::EffectiveDevice(ec::ReadPort(engine::settings::Global(), port).gamepad, Classify(GamepadOf(source)));
}

ec::Device LastUsedDevice(int port) {
  const ec::Device controller = PortDevice(port);
  if (port == 0 && sLastWasKeyboard) {
    return ec::Device::Keyboard;
  }
  return controller;
}

uint32_t DeviceChanges() { return sDeviceChanges; }

ec::ButtonLabel ButtonLabel(int port, ec::GcInput input) {
  const ec::PortMap map = ec::ReadPort(engine::settings::Global(), port);
  const ec::Device device = PortDevice(port);
  ec::ButtonLabel label = ec::GcInputLabel(map, input, device);
  if (device == ec::Device::Keyboard) {
    const auto& keys = map.keyboard.keys[static_cast<size_t>(input)];
    if (!keys.empty()) {
      const SDL_Keycode key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(keys.front().code), SDL_KMOD_NONE, false);
      if ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9')) {
        label.text = std::string(1, static_cast<char>(key >= 'a' && key <= 'z' ? key - 'a' + 'A' : key));
      }
    }
  }
  return label;
}

}

extern "C" void PortInput_ControlsHandleEvent(const SDL_Event* event) {
  if (event == nullptr) {
    return;
  }
  bool keyboard;
  if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    sLastPressedPad = aurora::input::source_for_gamepad(event->gbutton.which);
  }
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
    const ec::PortMap& map = sApplied[0].map;
    bool bound = map.keyboard.walk.kind == ec::Input::Kind::Key && map.keyboard.walk.code == event->key.scancode;
    for (const auto& keys : map.keyboard.keys) {
      for (const ec::Input& k : keys) {
        bound = bound || (k.kind == ec::Input::Kind::Key && k.code == event->key.scancode);
      }
    }
    for (const auto& keys : map.actions.keys) {
      for (const ec::Input& k : keys) {
        bound = bound || (k.kind == ec::Input::Kind::Key && k.code == event->key.scancode);
      }
    }
    if (!bound || !sApplied[0].done) {
      return;
    }
    keyboard = true;
  } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
             (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (event->gaxis.value > 16000 || event->gaxis.value < -16000))) {
    aurora::input::SourceId source = aurora::pad::port_controller(0);
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && sChoices[0].mode == ec::ControllerChoice::Mode::Auto && ec::AutoFollowsPresses(sChoices) &&
        sLastPressedPad != aurora::input::kInvalidSourceId &&
        std::find(sPadSources.begin(), sPadSources.end(), sLastPressedPad) != sPadSources.end()) {
      source = sLastPressedPad;
    }
    if (source == aurora::input::kInvalidSourceId) {
      return;
    }
    const SDL_JoystickID which =
        event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event->gbutton.which : event->gaxis.which;
    if (aurora::input::gamepad_for_source(source) != which) {
      return;
    }
    keyboard = false;
  } else {
    return;
  }
  if (keyboard != sLastWasKeyboard) {
    sLastWasKeyboard = keyboard;
    sDeviceChanges++;
  }
}

extern "C" void PortInput_UpdateControls(void) {
  engine::settings::Registry& registry = engine::settings::Global();
  if (!sStarted) {
    sStarted = true;
    if (registry.Find("keyboard1.a") == engine::settings::kNoSetting) {
      ec::RegisterSettings(registry);
    }
    for (const std::string& message : ec::UnknownEntries(registry)) {
      std::fprintf(stderr, "[platform/input] %s\n", message.c_str());
    }
    sChanges = registry.Changes() - 1;
  }

  const bool settingsChanged = registry.Changes() != sChanges;
  sChanges = registry.Changes();
  UpdateChoices(registry, settingsChanged);
  for (int port = 0; port < ec::kPorts; port++) {
    Applied& a = sApplied[static_cast<size_t>(port)];
    const aurora::input::SourceId controller = aurora::pad::port_controller(static_cast<uint32_t>(port));
    if (a.done && !settingsChanged && controller == a.controller) {
      continue;
    }
    const ec::PortMap map = settingsChanged || !a.done ? ec::ReadPort(registry, port) : a.map;
    if (!a.done || !(map.keyboard == a.map.keyboard)) {
      ApplyKeyboard(port, map.keyboard);
    }
    if (!a.done || !(map.gamepad == a.map.gamepad) || controller != a.controller) {
      ApplyController(port, map.gamepad, controller);
    }
    a = Applied{true, map, controller};
  }
}
