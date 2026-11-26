#ifndef ENGINE_CONTROLS_PAD_SDL_H
#define ENGINE_CONTROLS_PAD_SDL_H

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "pad_setup.h"
#include "pad_windows.h"

namespace engine::controls::padsdl {

struct JoyInfo {
  SDL_JoystickID id = 0;
  std::string name;
  std::string guid;
  std::string path;
  std::string type;
  std::string gamepadType;
  std::string mapping;
  unsigned vid = 0, pid = 0, version = 0;
  int buttons = -1, axes = -1, hats = -1;
  bool gamepad = false;
  bool virt = false;
  int nth = 0;
};

inline std::string PathUtf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

inline const char* JoystickTypeName(SDL_JoystickType t) {
  switch (t) {
  case SDL_JOYSTICK_TYPE_GAMEPAD: return "gamepad";
  case SDL_JOYSTICK_TYPE_WHEEL: return "wheel";
  case SDL_JOYSTICK_TYPE_ARCADE_STICK: return "arcade stick";
  case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return "flight stick";
  case SDL_JOYSTICK_TYPE_DANCE_PAD: return "dance pad";
  case SDL_JOYSTICK_TYPE_GUITAR: return "guitar";
  case SDL_JOYSTICK_TYPE_DRUM_KIT: return "drum kit";
  case SDL_JOYSTICK_TYPE_ARCADE_PAD: return "arcade pad";
  case SDL_JOYSTICK_TYPE_THROTTLE: return "throttle";
  default: return "unknown";
  }
}

inline const char* GamepadTypeName(SDL_GamepadType t) {
  switch (t) {
  case SDL_GAMEPAD_TYPE_STANDARD: return "standard";
  case SDL_GAMEPAD_TYPE_XBOX360: return "Xbox 360";
  case SDL_GAMEPAD_TYPE_XBOXONE: return "Xbox One";
  case SDL_GAMEPAD_TYPE_PS3: return "PS3";
  case SDL_GAMEPAD_TYPE_PS4: return "PS4";
  case SDL_GAMEPAD_TYPE_PS5: return "PS5";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return "Switch Pro";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con (L)";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con (R)";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con pair";
  case SDL_GAMEPAD_TYPE_GAMECUBE: return "GameCube";
  default: return "other";
  }
}

inline std::vector<std::string>& ForgottenGuids() {
  static std::vector<std::string> list;
  return list;
}
inline bool IsForgotten(const std::string& guid) {
  const std::vector<std::string>& l = ForgottenGuids();
  return std::find(l.begin(), l.end(), padsetup::GuidOfLine(guid + ",")) != l.end();
}

inline std::vector<JoyInfo> ListJoysticks(bool count) {
  std::vector<JoyInfo> out;
  int n = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&n);
  if (ids == nullptr) {
    return out;
  }
  for (int i = 0; i < n; i++) {
    JoyInfo j;
    j.id = ids[i];
    const char* name = SDL_GetJoystickNameForID(j.id);
    j.name = name != nullptr ? name : "";
    char guid[33]{};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(j.id), guid, sizeof(guid));
    j.guid = guid;
    const char* path = SDL_GetJoystickPathForID(j.id);
    j.path = path != nullptr ? path : "";
    j.type = JoystickTypeName(SDL_GetJoystickTypeForID(j.id));
    j.vid = SDL_GetJoystickVendorForID(j.id);
    j.pid = SDL_GetJoystickProductForID(j.id);
    j.version = SDL_GetJoystickProductVersionForID(j.id);
    j.virt = SDL_IsJoystickVirtual(j.id);
    j.gamepad = SDL_IsGamepad(j.id) && !IsForgotten(j.guid);
    if (j.gamepad) {
      j.gamepadType = GamepadTypeName(SDL_GetGamepadTypeForID(j.id));
      if (char* m = SDL_GetGamepadMappingForID(j.id)) {
        j.mapping = m;
        SDL_free(m);
      }
    }
    if (count) {
      if (SDL_Joystick* joy = SDL_OpenJoystick(j.id)) {
        j.buttons = SDL_GetNumJoystickButtons(joy);
        j.axes = SDL_GetNumJoystickAxes(joy);
        j.hats = SDL_GetNumJoystickHats(joy);
        SDL_CloseJoystick(joy);
      }
    }
    for (const JoyInfo& before : out) {
      j.nth += before.guid == j.guid ? 1 : 0;
    }
    out.push_back(std::move(j));
  }
  SDL_free(ids);
  return out;
}

inline std::string Hex4(unsigned v) {
  char b[8];
  std::snprintf(b, sizeof(b), "%04X", v & 0xFFFF);
  return b;
}

inline std::vector<JoyInfo> Unrecognised(const std::vector<JoyInfo>& all) {
  std::vector<JoyInfo> out;
  for (const JoyInfo& j : all) {
    if (!j.gamepad) {
      out.push_back(j);
    }
  }
  return out;
}

inline std::string DescribeJoysticks(const std::string& when, bool withWindows = true) {
  std::string out = "[pads] --- " + when + " ---";
  const auto line = [&out](const std::string& text) { out += "\n[pads] " + text; };
  const int ver = SDL_GetVersion();
  line("SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(ver)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(ver)) + "." +
       std::to_string(SDL_VERSIONNUM_MICRO(ver)) + ", platform " + SDL_GetPlatform());
  const auto hint = [&line](const char* name) {
    const char* v = SDL_GetHint(name);
    line(std::string("SDL hint ") + name + " = " + (v != nullptr ? std::string("\"") + v + "\"" : std::string("(not set: SDL's default)")));
  };
  hint(SDL_HINT_JOYSTICK_HIDAPI);
  hint(SDL_HINT_JOYSTICK_RAWINPUT);
  hint(SDL_HINT_JOYSTICK_WGI);
  hint(SDL_HINT_XINPUT_ENABLED);
  const bool running = SDL_WasInit(SDL_INIT_JOYSTICK) != 0;
  const std::vector<JoyInfo> all = running ? ListJoysticks(true) : std::vector<JoyInfo>();
  line(running ? std::to_string(all.size()) + " joystick(s) seen by SDL" : std::string("SDL's joystick system is not running yet (a later block lists them)"));
  int index = 0;
  for (const JoyInfo& j : all) {
    index++;
    std::string driver = j.virt ? "virtual" : j.path.empty() ? "(no path)" : j.path;
    std::string text = "device " + std::to_string(index) + ": \"" + j.name + "\" VID/PID " + Hex4(j.vid) + ":" + Hex4(j.pid) +
                       " version " + Hex4(j.version) + ", GUID " + j.guid + ", " + std::to_string(j.buttons) + " buttons, " +
                       std::to_string(j.axes) + " axes, " + std::to_string(j.hats) + " hats, type " + j.type + ", " +
                       (j.gamepad ? "RECOGNISED as a gamepad (" + j.gamepadType + ")" : std::string("NOT recognised (no mapping)")) +
                       ", path " + driver;
    line(text);
    if (j.gamepad && !j.mapping.empty()) {
      line("device " + std::to_string(index) + " mapping: " + j.mapping);
    }
  }
  if (withWindows) {
    std::string error;
    const std::vector<WindowsPad> win = ListWindowsGamePads(&error);
#ifdef _WIN32
    out += "\n" + DescribeWindowsGamePads(win, error);
#else
    (void)win;
#endif
  }
  return out;
}

inline void LogJoysticks(const std::string& when) {
  const std::string block = DescribeJoysticks(when);
  std::fprintf(stderr, "%s\n", block.c_str());
  std::fflush(stderr);
}

inline std::string Fingerprint() {
  std::string s;
  if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0) {
    return s;
  }
  for (const JoyInfo& j : ListJoysticks(false)) {
    s += std::to_string(j.id) + "/" + j.guid + "/" + (j.gamepad ? "g" : "j") + ";";
  }
  return s;
}

inline padsetup::Snapshot ReadSnapshot(SDL_Joystick* joy) {
  padsetup::Snapshot s;
  if (joy == nullptr) {
    return s;
  }
  const int nb = SDL_GetNumJoystickButtons(joy), na = SDL_GetNumJoystickAxes(joy), nh = SDL_GetNumJoystickHats(joy);
  for (int i = 0; i < nb; i++) {
    s.buttons.push_back(SDL_GetJoystickButton(joy, i));
  }
  for (int i = 0; i < na; i++) {
    const float v = static_cast<float>(SDL_GetJoystickAxis(joy, i)) / 32767.0f;
    s.axes.push_back(std::clamp(v, -1.0f, 1.0f));
  }
  for (int i = 0; i < nh; i++) {
    s.hats.push_back(SDL_GetJoystickHat(joy, i));
  }
  return s;
}

inline void SetMappingHints(const std::filesystem::path& dataFolder) {
  const std::vector<std::string> lines = padsetup::LoadMappings(dataFolder);
  if (!lines.empty()) {
    std::string joined;
    for (const std::string& l : lines) {
      joined += l + "\n";
    }
    SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, joined.c_str());
  }
  if (padsetup::HasDatabase(dataFolder)) {
    SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, PathUtf8(padsetup::DatabasePath(dataFolder)).c_str());
  }
}

struct LoadResult {
  int database = 0;
  int own = 0;
  int rejected = 0;
  std::string text;
};

inline LoadResult AddMappings(const std::filesystem::path& dataFolder) {
  LoadResult r;
  r.database = -1;
  if (padsetup::HasDatabase(dataFolder)) {
    r.database = SDL_AddGamepadMappingsFromFile(PathUtf8(padsetup::DatabasePath(dataFolder)).c_str());
    r.text += "gamecontrollerdb.txt: " + (r.database < 0 ? std::string("could not be read (") + SDL_GetError() + ")" : std::to_string(r.database) + " mapping(s)");
  }
  for (const std::string& line : padsetup::LoadMappings(dataFolder)) {
    if (SDL_AddGamepadMapping(line.c_str()) >= 0) {
      r.own++;
    } else {
      r.rejected++;
      r.text += std::string(r.text.empty() ? "" : "; ") + "SDL refused \"" + line.substr(0, 60) + "\": " + SDL_GetError();
    }
  }
  r.text += std::string(r.text.empty() ? "" : "; ") + "pad_mappings.txt: " + std::to_string(r.own) + " set-up(s) loaded";
  return r;
}

inline bool ApplyAndStore(const std::filesystem::path& dataFolder, const std::string& line, std::string* error) {
  if (SDL_AddGamepadMapping(line.c_str()) < 0) {
    if (error != nullptr) {
      *error = SDL_GetError();
    }
    return false;
  }
  std::vector<std::string>& forgotten = ForgottenGuids();
  forgotten.erase(std::remove(forgotten.begin(), forgotten.end(), padsetup::GuidOfLine(line)), forgotten.end());
  return padsetup::StoreMapping(dataFolder, line, error);
}

inline bool ForgetAndRemove(const std::filesystem::path& dataFolder, const std::string& guid, std::string* error) {
  const bool had = padsetup::ForgetMapping(dataFolder, guid, error);
  if (!IsForgotten(guid)) {
    ForgottenGuids().push_back(padsetup::GuidOfLine(guid + ","));
  }
  return had;
}

}

#endif
