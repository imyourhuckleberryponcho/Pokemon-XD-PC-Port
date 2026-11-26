#ifndef ENGINE_CONTROLS_PAD_WINDOWS_H
#define ENGINE_CONTROLS_PAD_WINDOWS_H

#include <string>
#include <vector>

namespace engine::controls {

struct WindowsPad {
  unsigned short vid = 0;
  unsigned short pid = 0;
  std::string name;
  std::string hardwareId;
  std::string service;
  bool xinput = false;
  unsigned problem = 0;
};

bool ParseHardwareId(const std::string& id, unsigned short* vid, unsigned short* pid);

bool LooksLikeGameController(const std::vector<std::string>& hardwareIds, const std::vector<std::string>& compatibleIds);

std::vector<WindowsPad> ListWindowsGamePads(std::string* error = nullptr);

std::string DescribeWindowsGamePads(const std::vector<WindowsPad>& pads, const std::string& error);

}

#endif
