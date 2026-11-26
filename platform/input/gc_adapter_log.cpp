#include <port_controls.h>

#include <gc_adapter.h>
#include <gc_adapter_sdl.h>
#include <pad_sdl.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <string>

extern "C" void PortInput_GcAdapterHints(void) { engine::controls::SetGameCubeAdapterHints(); }

extern "C" void PortInput_GcAdapterLog(void) {
  const std::string block = engine::controls::DescribeGameCubeAdapter(engine::controls::DescribeGameCubeAdapterSdl());
  std::fprintf(stderr, "[gc adapter] --- game start ---\n%s\n", block.c_str());
  std::fflush(stderr);
}

static std::filesystem::path g_padFolder;

extern "C" void PortInput_PadSetupHints(const char* dataFolderUtf8) {
  g_padFolder = std::filesystem::path(reinterpret_cast<const char8_t*>(dataFolderUtf8 != nullptr ? dataFolderUtf8 : ""));
  engine::controls::padsdl::SetMappingHints(g_padFolder);
}

extern "C" void PortInput_PadSetupLog(void) {
  std::fprintf(stderr, "[pads] mappings: %s%s\n",
               engine::controls::padsetup::LoadMappings(g_padFolder).empty() ? "no set-ups by the player" : "set-ups by the player in pad_mappings.txt",
               engine::controls::padsetup::HasDatabase(g_padFolder) ? ", gamecontrollerdb.txt present" : "");
  engine::controls::padsdl::LogJoysticks("game start");
}

static void PadSetupTick(Uint64 now) {
  static Uint64 lastLook = 0;
  static bool mappingsAdded = false;
  static std::string lastPrint;
  static bool printedOnce = false;
  if (now - lastLook < 1000 && lastLook != 0) {
    return;
  }
  lastLook = now == 0 ? 1 : now;
  if (!SDL_WasInit(SDL_INIT_JOYSTICK)) {
    return;
  }
  if (!mappingsAdded) {
    mappingsAdded = true;
    const engine::controls::padsdl::LoadResult r = engine::controls::padsdl::AddMappings(g_padFolder);
    std::fprintf(stderr, "[pads] %s\n", r.text.c_str());
    lastPrint.clear();
  }
  const std::string now_print = engine::controls::padsdl::Fingerprint();
  if (now_print != lastPrint || !printedOnce) {
    const bool first = !printedOnce;
    printedOnce = true;
    lastPrint = now_print;
    engine::controls::padsdl::LogJoysticks(first ? "game, once SDL's joysticks run" : "a controller was plugged in or out");
  }
}

extern "C" void PortInput_GcAdapterTick(void) {
  static Uint64 first = 0;
  static bool done = false;
  PadSetupTick(SDL_GetTicks());
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
