#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>
#include <controls.h>
#include <dolphin/pad.h>

#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <port_log.h>

extern "C" void PortInput_StopRecording(void);

namespace {

struct Step {
  uint32_t field;
  uint32_t held;
  uint16_t buttons;
  bool exit;
  std::vector<uint16_t> keys;
  int8_t cx = 0, cy = 0;
};

std::vector<Step> sSteps;
bool sVirtual = false;
std::vector<uint16_t> sKeysDown;
bool (*sStickFn)(uint32_t field, int8_t* x, int8_t* y) = nullptr;

void send_key(uint16_t scancode, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.timestamp = SDL_GetTicksNS();
  e.key.scancode = static_cast<SDL_Scancode>(scancode);
  e.key.key = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
  e.key.down = down;
  SDL_PushEvent(&e);
}

uint16_t button_named(const std::string& name) {
  static const struct {
    const char* name;
    uint16_t bit;
  } kButtons[] = {
      {"a", PAD_BUTTON_A},       {"b", PAD_BUTTON_B},         {"x", PAD_BUTTON_X},       {"y", PAD_BUTTON_Y},
      {"z", PAD_TRIGGER_Z},      {"l", PAD_TRIGGER_L},        {"r", PAD_TRIGGER_R},      {"start", PAD_BUTTON_START},
      {"up", PAD_BUTTON_UP},     {"down", PAD_BUTTON_DOWN},   {"left", PAD_BUTTON_LEFT}, {"right", PAD_BUTTON_RIGHT},
  };
  for (const auto& b : kButtons) {
    if (name == b.name) {
      return b.bit;
    }
  }
  std::fprintf(stderr, "[input script] unknown button '%s'\n", name.c_str());
  return 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t end = s.find(sep, start);
    out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) {
      return out;
    }
    start = end + 1;
  }
}

}

extern "C" {

bool PortInput_LoadScript(const char* script) {
  if (script == nullptr || script[0] == '\0') {
    return false;
  }
  for (const std::string& item : split(script, ',')) {
    const std::vector<std::string> parts = split(item, ':');
    if (parts.size() < 2 || parts[0].empty()) {
      std::fprintf(stderr, "[input script] skipped '%s' (FIELD:BUTTONS[:HELD])\n", item.c_str());
      continue;
    }
    Step step{static_cast<uint32_t>(std::strtoul(parts[0].c_str(), nullptr, 10)), 10, 0, false};
    if (parts[1] == "exit") {
      step.exit = true;
    } else {
      for (const std::string& name : split(parts[1], '+')) {
        if (name.rfind("key_", 0) == 0) {
          const auto key = engine::controls::ParseKeyToken(name.substr(4));
          if (key) {
            step.keys.push_back(key->code);
          } else {
            std::fprintf(stderr, "[input script] unknown key '%s'\n", name.c_str());
          }
          continue;
        }
        if (name == "cup" || name == "cdown") {
          step.cy = name == "cup" ? 127 : -128;
        } else if (name == "cleft" || name == "cright") {
          step.cx = name == "cright" ? 127 : -128;
        } else {
          step.buttons |= button_named(name);
        }
      }
      if (parts.size() >= 3) {
        step.held = static_cast<uint32_t>(std::strtoul(parts[2].c_str(), nullptr, 10));
      }
    }
    sSteps.push_back(step);
  }
  std::fprintf(stderr, "[input script] %zu steps\n", sSteps.size());
  return !sSteps.empty();
}

void PortInput_SetScriptStick(bool (*fn)(uint32_t field, int8_t* x, int8_t* y)) { sStickFn = fn; }

void PortInput_ScriptField(uint32_t field) {
  uint16_t buttons = 0;
  std::vector<uint16_t> keys;
  int8_t cx = 0, cy = 0;
  for (const Step& step : sSteps) {
    if (step.exit && field >= step.field) {
      std::fprintf(stderr, "[input script] exit at field %u\n", field);
      PortInput_StopRecording();
      std::fflush(stdout);
      std::fflush(stderr);
      PortLog_Flush();
      std::_Exit(0);
    }
    if (!step.exit && field >= step.field && field < step.field + step.held) {
      buttons |= step.buttons;
      keys.insert(keys.end(), step.keys.begin(), step.keys.end());
      cx = step.cx != 0 ? step.cx : cx;
      cy = step.cy != 0 ? step.cy : cy;
    }
  }
  for (uint16_t k : keys) {
    if (std::find(sKeysDown.begin(), sKeysDown.end(), k) == sKeysDown.end()) {
      std::fprintf(stderr, "[input script] field %u: key %s down\n", field,
                   engine::controls::InputToken(engine::controls::Input::Key(k)).c_str());
      send_key(k, true);
      sKeysDown.push_back(k);
    }
  }
  for (auto it = sKeysDown.begin(); it != sKeysDown.end();) {
    if (std::find(keys.begin(), keys.end(), *it) == keys.end()) {
      std::fprintf(stderr, "[input script] field %u: key %s up\n", field,
                   engine::controls::InputToken(engine::controls::Input::Key(*it)).c_str());
      send_key(*it, false);
      it = sKeysDown.erase(it);
    } else {
      ++it;
    }
  }
  int8_t sx = 0, sy = 0;
  const bool stick = sStickFn != nullptr && sStickFn(field, &sx, &sy);
  if (buttons != 0 || cx != 0 || cy != 0 || stick) {
    if (!sVirtual && !stick) {
      std::fprintf(stderr, "[input script] field %u: buttons 0x%04x c stick %d,%d\n", field, buttons, cx, cy);
    }
    PADStatus status{};
    status.button = buttons;
    status.stickX = sx;
    status.stickY = sy;
    status.substickX = cx;
    status.substickY = cy;
    status.err = PAD_ERR_NONE;
    PADSetVirtualStatus(0, &status);
    sVirtual = true;
  } else if (sVirtual) {
    PADClearVirtualStatus(0);
    sVirtual = false;
  }
}

}
