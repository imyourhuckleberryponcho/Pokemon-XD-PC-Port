#include <aurora/pad.hpp>
#include <port_controls.h>
#include <settings.h>

#include <cstdio>

extern "C" void PortInput_InstallKeyboardDefaults(void);

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
std::vector<aurora::binding::Binding> keyboard(aurora::binding::ControlId control) {
  return aurora::pad::bindings_for_control(aurora::pad::ProfileTarget::keyboard(0), control);
}
bool has_key(aurora::binding::ControlId control, SDL_Scancode key, float scale = 1.f, bool held = false) {
  for (const auto& b : keyboard(control)) {
    const auto* k = b.input.control.get_if<aurora::binding::PhysicalInput::Key>();
    if (k != nullptr && k->scancode == key && b.scale == scale && b.held.empty() != held) return true;
  }
  return false;
}
}

int main() {
  PortInput_InstallKeyboardDefaults();
  const auto& c = aurora::pad::controls();
  auto& settings = engine::settings::Global();

  PortInput_UpdateControls();
  check(settings.Find("keyboard1.a") != engine::settings::kNoSetting, "controls registered in the game's settings");
  check(aurora::pad::profile(aurora::pad::ProfileTarget::keyboard(0))->bindings.empty(),
        "default controls: no overrides in Aurora");
  check(has_key(c.a, SDL_SCANCODE_X) && has_key(c.start, SDL_SCANCODE_RETURN) && has_key(c.leftX, SDL_SCANCODE_LEFT, -1.f),
        "default controls: keyboard_defaults.cpp's layout");

  settings.SetText("keyboard1.a", "space");
  settings.SetText("keyboard1.walk", "lshift");
  PortInput_UpdateControls();
  check(has_key(c.a, SDL_SCANCODE_SPACE) && !has_key(c.a, SDL_SCANCODE_X), "remapped: Space is A, X no longer");
  check(has_key(c.start, SDL_SCANCODE_RETURN), "the other keys stay");
  check(has_key(c.leftX, SDL_SCANCODE_RIGHT, 1.f) && has_key(c.leftX, SDL_SCANCODE_RIGHT, -0.5f, true),
        "walk: a held-Shift binding takes half the tilt back");
  check(settings.IsOriginal(), "controls never leave Original");
  check(port_controls::ButtonLabel(0, engine::controls::GcInput::A).glyph == "key_space", "A's label: the Space key");

  settings.SetText("keyboard1.a", "x");
  settings.SetText("keyboard1.walk", "none");
  PortInput_UpdateControls();
  check(aurora::pad::profile(aurora::pad::ProfileTarget::keyboard(0))->bindings.empty() && has_key(c.a, SDL_SCANCODE_X),
        "back to the defaults: overrides removed");

  using aurora::pad::PortAssignment;
  check(aurora::pad::port_preference(0).state == PortAssignment::Auto &&
            aurora::pad::port_preference(1).state == PortAssignment::None,
        "default choices: controller 1 auto, 2 none (Aurora's own defaults)");
  settings.SetText("gamepad1.controller", "none");
  settings.SetText("gamepad2.controller", "auto");
  PortInput_UpdateControls();
  check(aurora::pad::port_preference(0).state == PortAssignment::None &&
            aurora::pad::port_preference(1).state == PortAssignment::Auto,
        "choices changed: controller 1 none, 2 auto");
  settings.SetText("gamepad1.controller", "030000005e040000e002000000007200|Xbox");
  PortInput_UpdateControls();
  check(aurora::pad::port_preference(0).state == PortAssignment::None,
        "a pinned controller that isn't plugged in: no controller in that port (until it is)");
  check(has_key(c.a, SDL_SCANCODE_X), "the keyboard plays on whatever the controller choice");
  settings.SetText("gamepad1.controller", "auto");
  settings.SetText("gamepad2.controller", "none");
  PortInput_UpdateControls();
  check(aurora::pad::port_preference(0).state == PortAssignment::Auto, "back to auto");
  return ok ? 0 : 1;
}
