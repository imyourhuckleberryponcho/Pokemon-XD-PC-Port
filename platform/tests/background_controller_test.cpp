#include <SDL3/SDL.h>

#include <cstdio>

static bool PressAndSee(SDL_Joystick* joy, SDL_JoystickID id, bool down) {
  SDL_SetJoystickVirtualButton(joy, 0, down);
  SDL_UpdateJoysticks();
  bool seen = false;
  SDL_Event e;
  SDL_PumpEvents();
  while (SDL_PollEvent(&e)) {
    if ((e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_UP) && e.jbutton.which == id) seen = true;
  }
  return seen;
}

int main() {
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS)) {
    std::printf("background_controller_test: SKIPPED, SDL_Init failed: %s\n", SDL_GetError());
    return 0;
  }
  SDL_Window* win = SDL_CreateWindow("bg test", 64, 64, SDL_WINDOW_HIDDEN);
  if (win == nullptr) {
    std::printf("background_controller_test: SKIPPED, no window: %s\n", SDL_GetError());
    return 0;
  }
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.nbuttons = 4;
  desc.naxes = 0;
  desc.name = "virtual test pad";
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  if (id == 0) {
    std::printf("background_controller_test: SKIPPED, no virtual joystick: %s\n", SDL_GetError());
    return 0;
  }
  SDL_Joystick* joy = SDL_OpenJoystick(id);
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
  }
  int bad = 0;
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
  const bool off = PressAndSee(joy, id, true);
  std::printf("hint 0 (window not in front): the press %s (must not)\n", off ? "REACHED the program" : "did not reach the program");
  bad += off ? 1 : 0;
  PressAndSee(joy, id, false);
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  const bool on = PressAndSee(joy, id, true);
  std::printf("hint 1 (window not in front): the press %s (must)\n", on ? "reached the program" : "DID NOT REACH the program");
  bad += on ? 0 : 1;
  SDL_CloseJoystick(joy);
  SDL_DetachVirtualJoystick(id);
  SDL_DestroyWindow(win);
  SDL_Quit();
  std::printf(bad == 0 ? "background_controller_test: ok\n" : "background_controller_test: FAILED\n");
  return bad == 0 ? 0 : 1;
}
