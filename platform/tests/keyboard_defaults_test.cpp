#include <aurora/pad.hpp>

#include <cstdio>

extern "C" void PortInput_InstallKeyboardDefaults(void);

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
bool has_key(aurora::binding::ControlId control, SDL_Scancode key, float scale) {
  for (const auto& b : aurora::pad::bindings_for_control(aurora::pad::ProfileTarget::keyboard(0), control)) {
    const auto* k = b.input.control.get_if<aurora::binding::PhysicalInput::Key>();
    if (k != nullptr && k->scancode == key && b.scale == scale) return true;
  }
  return false;
}
}

int main() {
  PortInput_InstallKeyboardDefaults();
  const auto& c = aurora::pad::controls();
  check(has_key(c.start, SDL_SCANCODE_RETURN, 1.f), "Enter is Start");
  check(has_key(c.a, SDL_SCANCODE_X, 1.f), "X is A");
  check(has_key(c.b, SDL_SCANCODE_Z, 1.f), "Z is B");
  check(has_key(c.leftX, SDL_SCANCODE_RIGHT, 1.f) && has_key(c.leftX, SDL_SCANCODE_LEFT, -1.f),
        "left/right arrows move the main stick");
  check(has_key(c.leftY, SDL_SCANCODE_UP, 1.f) && has_key(c.leftY, SDL_SCANCODE_DOWN, -1.f),
        "up/down arrows move the main stick");
  check(has_key(c.l, SDL_SCANCODE_Q, 1.f) && has_key(c.triggerL, SDL_SCANCODE_Q, 1.f), "Q is L (button and trigger)");
  return ok ? 0 : 1;
}
