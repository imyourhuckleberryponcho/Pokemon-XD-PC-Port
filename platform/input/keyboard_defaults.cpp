#include <aurora/pad.hpp>

#include <vector>

namespace {

using aurora::binding::Binding;
using aurora::binding::ControlId;
using aurora::binding::PhysicalInput;

Binding key(SDL_Scancode scancode, ControlId target, float scale = 1.f) {
  Binding b;
  b.input.control = PhysicalInput::Key{scancode};
  b.target = target;
  b.scale = scale;
  return b;
}

}

extern "C" void PortInput_InstallKeyboardDefaults(void) {
  const aurora::pad::Controls& c = aurora::pad::controls();
  std::vector<Binding> b = {
      key(SDL_SCANCODE_X, c.a),         key(SDL_SCANCODE_Z, c.b),     key(SDL_SCANCODE_C, c.x),
      key(SDL_SCANCODE_S, c.y),         key(SDL_SCANCODE_D, c.z),     key(SDL_SCANCODE_RETURN, c.start),
      key(SDL_SCANCODE_Q, c.l),         key(SDL_SCANCODE_W, c.r),     key(SDL_SCANCODE_Q, c.triggerL),
      key(SDL_SCANCODE_W, c.triggerR),  key(SDL_SCANCODE_T, c.up),    key(SDL_SCANCODE_G, c.down),
      key(SDL_SCANCODE_F, c.left),      key(SDL_SCANCODE_H, c.right),
      key(SDL_SCANCODE_RIGHT, c.leftX, 1.f), key(SDL_SCANCODE_LEFT, c.leftX, -1.f),
      key(SDL_SCANCODE_UP, c.leftY, 1.f),    key(SDL_SCANCODE_DOWN, c.leftY, -1.f),
      key(SDL_SCANCODE_L, c.rightX, 1.f),    key(SDL_SCANCODE_J, c.rightX, -1.f),
      key(SDL_SCANCODE_I, c.rightY, 1.f),    key(SDL_SCANCODE_K, c.rightY, -1.f),
  };
  aurora::pad::set_default_keyboard_bindings(0, std::move(b));
}
