#include "pad_windows.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#endif

namespace engine::controls {
namespace {
std::string Upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}
}

bool ParseHardwareId(const std::string& id, unsigned short* vid, unsigned short* pid) {
  const std::string u = Upper(id);
  const size_t v = u.find("VID_");
  const size_t p = u.find("PID_");
  if (v == std::string::npos || p == std::string::npos || u.size() < v + 8 || u.size() < p + 8) {
    return false;
  }
  const auto hex = [&](size_t at, unsigned short* out) {
    unsigned value = 0;
    for (size_t i = 0; i < 4; i++) {
      const char c = u[at + i];
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        return false;
      }
      value = value * 16 + static_cast<unsigned>(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    *out = static_cast<unsigned short>(value);
    return true;
  };
  unsigned short a = 0, b = 0;
  if (!hex(v + 4, &a) || !hex(p + 4, &b)) {
    return false;
  }
  if (vid != nullptr) {
    *vid = a;
  }
  if (pid != nullptr) {
    *pid = b;
  }
  return true;
}

bool LooksLikeGameController(const std::vector<std::string>& hardwareIds, const std::vector<std::string>& compatibleIds) {
  const auto has = [](const std::vector<std::string>& list, const char* what) {
    for (const std::string& s : list) {
      if (Upper(s).find(what) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  return has(compatibleIds, "HID_DEVICE_SYSTEM_GAME") || has(compatibleIds, "HID_DEVICE_UP:0001_U:0005") ||
         has(compatibleIds, "HID_DEVICE_UP:0001_U:0004") || has(hardwareIds, "HID_DEVICE_SYSTEM_GAME");
}

#ifdef _WIN32
namespace {
std::vector<std::string> MultiSz(HDEVINFO set, SP_DEVINFO_DATA* info, DWORD property) {
  std::vector<std::string> out;
  char buf[4096]{};
  DWORD type = 0, size = 0;
  if (!SetupDiGetDeviceRegistryPropertyA(set, info, property, &type, reinterpret_cast<PBYTE>(buf), sizeof(buf) - 2, &size)) {
    return out;
  }
  for (const char* p = buf; *p != 0 && p < buf + sizeof(buf); p += std::char_traits<char>::length(p) + 1) {
    out.push_back(p);
  }
  return out;
}
std::string OneString(HDEVINFO set, SP_DEVINFO_DATA* info, DWORD property) {
  char buf[512]{};
  DWORD type = 0, size = 0;
  if (SetupDiGetDeviceRegistryPropertyA(set, info, property, &type, reinterpret_cast<PBYTE>(buf), sizeof(buf) - 2, &size)) {
    return buf;
  }
  return {};
}
}

std::vector<WindowsPad> ListWindowsGamePads(std::string* error) {
  std::vector<WindowsPad> out;
  const HDEVINFO set = SetupDiGetClassDevsA(nullptr, "HID", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (set == INVALID_HANDLE_VALUE) {
    if (error != nullptr) {
      *error = "SetupDiGetClassDevs failed (" + std::to_string(GetLastError()) + ")";
    }
    return out;
  }
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); i++) {
    const std::vector<std::string> hardware = MultiSz(set, &info, SPDRP_HARDWAREID);
    const std::vector<std::string> compat = MultiSz(set, &info, SPDRP_COMPATIBLEIDS);
    if (!LooksLikeGameController(hardware, compat)) {
      continue;
    }
    WindowsPad pad;
    if (!hardware.empty()) {
      pad.hardwareId = hardware.front();
      ParseHardwareId(hardware.front(), &pad.vid, &pad.pid);
    }
    for (const std::string& h : hardware) {
      pad.xinput |= Upper(h).find("&IG_") != std::string::npos;
    }
    pad.name = OneString(set, &info, SPDRP_FRIENDLYNAME);
    if (pad.name.empty()) {
      pad.name = OneString(set, &info, SPDRP_DEVICEDESC);
    }
    pad.service = OneString(set, &info, SPDRP_SERVICE);
    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS) {
      pad.problem = static_cast<unsigned>(problem);
    }
    out.push_back(std::move(pad));
  }
  SetupDiDestroyDeviceInfoList(set);
  return out;
}
#else
std::vector<WindowsPad> ListWindowsGamePads(std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  return {};
}
#endif

std::string DescribeWindowsGamePads(const std::vector<WindowsPad>& pads, const std::string& error) {
  std::string out;
  if (!error.empty()) {
    return "[pads] Windows' game controller list could not be read: " + error;
  }
  if (pads.empty()) {
    return "[pads] Windows lists no game controller (HID gamepad / joystick) at all";
  }
  for (const WindowsPad& p : pads) {
    char ids[32];
    std::snprintf(ids, sizeof(ids), "%04X:%04X", p.vid, p.pid);
    if (!out.empty()) {
      out += "\n";
    }
    out += std::string("[pads] Windows lists: \"") + p.name + "\" " + ids + ", driver " +
           (p.service.empty() ? std::string("(none)") : p.service) + (p.xinput ? ", XInput" : "") +
           (p.problem != 0 ? ", Device Manager problem " + std::to_string(p.problem) : std::string()) + ", id " +
           p.hardwareId;
  }
  return out;
}

}
