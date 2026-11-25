#ifndef ENGINE_CONTROLS_GC_ADAPTER_SDL_H
#define ENGINE_CONTROLS_GC_ADAPTER_SDL_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_hidapi.h>

#include <string>
#include <vector>

#include "gc_adapter.h"

namespace engine::controls {

inline void SetGameCubeAdapterHints() {
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
  SDL_SetHint(SDL_HINT_HIDAPI_LIBUSB, "1");
  SDL_SetHint(SDL_HINT_HIDAPI_LIBUSB_GAMECUBE, "1");
}

inline std::string DescribeGameCubeAdapterSdl() {
  std::string out;
  const auto line = [&out](const std::string& text) {
    if (!out.empty()) {
      out += '\n';
    }
    out += "[gc adapter] " + text;
  };
  const auto hint = [&line](const char* name) {
    const char* v = SDL_GetHint(name);
    line(std::string("SDL hint ") + name + " = " + (v != nullptr ? std::string("\"") + v + "\"" : std::string("(not set: SDL's default)")));
  };
  const int ver = SDL_GetVersion();
  line("SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(ver)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(ver)) + "." +
       std::to_string(SDL_VERSIONNUM_MICRO(ver)));
  hint(SDL_HINT_JOYSTICK_HIDAPI);
  hint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE);
  hint(SDL_HINT_HIDAPI_LIBUSB);
  hint(SDL_HINT_HIDAPI_LIBUSB_GAMECUBE);
  hint(SDL_HINT_HIDAPI_LIBUSB_WHITELIST);
  hint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE_RUMBLE_BRAKE);
  hint(SDL_HINT_HIDAPI_IGNORE_DEVICES);
  if (SDL_hid_device_info* devs = SDL_hid_enumerate(kGcAdapterVendor, kGcAdapterProduct)) {
    int n = 0;
    for (SDL_hid_device_info* d = devs; d != nullptr; d = d->next) {
      n++;
      line(std::string("SDL's libusb lists the adapter: path ") + (d->path != nullptr ? d->path : "(none)") +
           ", interface " + std::to_string(d->interface_number));
    }
    if (n == 0) {
      line("SDL's libusb does not list the adapter");
    }
    SDL_hid_free_enumeration(devs);
  } else {
    line("SDL's libusb does not list the adapter (SDL did not load libusb, or does not see the device)");
  }
  if (!SDL_WasInit(SDL_INIT_JOYSTICK)) {
    line("SDL joystick system not started yet");
    return out;
  }
  int count = 0;
  int gcPads = 0;
  if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
    for (int i = 0; i < count; i++) {
      const Uint16 vid = SDL_GetJoystickVendorForID(ids[i]);
      const Uint16 pid = SDL_GetJoystickProductForID(ids[i]);
      const char* name = SDL_GetJoystickNameForID(ids[i]);
      char id[16];
      SDL_snprintf(id, sizeof(id), "%04x:%04x", vid, pid);
      line(std::string("SDL joystick ") + std::to_string(i) + ": \"" + (name != nullptr ? name : "") + "\" " + id +
           (SDL_IsGamepad(ids[i]) ? " (gamepad)" : " (not a gamepad)"));
      if (vid == kGcAdapterVendor && pid == kGcAdapterProduct) {
        gcPads++;
      }
    }
    SDL_free(ids);
  }
  line("SDL joysticks: " + std::to_string(count) + ", of them from the GameCube adapter: " + std::to_string(gcPads));
  return out;
}

inline void NudgeGameCubeAdapter() {
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");
}

inline void HoldGameCubeAdapter(bool hold) { SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, hold ? "1" : "0"); }

}

#endif
