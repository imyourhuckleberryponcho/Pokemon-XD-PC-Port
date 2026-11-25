#include <port_controls.h>

#include <gc_adapter.h>
#include <gc_adapter_sdl.h>

#include <SDL3/SDL.h>

#include <cstdio>

extern "C" void PortInput_GcAdapterHints(void) { engine::controls::SetGameCubeAdapterHints(); }

extern "C" void PortInput_GcAdapterLog(void) {
  const std::string block = engine::controls::DescribeGameCubeAdapter(engine::controls::DescribeGameCubeAdapterSdl());
  std::fprintf(stderr, "[gc adapter] --- game start ---\n%s\n", block.c_str());
  std::fflush(stderr);
}

extern "C" void PortInput_GcAdapterTick(void) {
  static Uint64 first = 0;
  static bool done = false;
  if (done) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (first == 0) {
    first = now == 0 ? 1 : now;
    return;
  }
  if (now - first < 2000 || !SDL_WasInit(SDL_INIT_JOYSTICK)) {
    return;
  }
  done = true;
  const std::string block = engine::controls::DescribeGameCubeAdapter(engine::controls::DescribeGameCubeAdapterSdl());
  std::fprintf(stderr, "[gc adapter] --- game, 2 seconds later ---\n%s\n", block.c_str());
  std::fflush(stderr);
}
