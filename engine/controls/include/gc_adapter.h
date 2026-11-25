#ifndef ENGINE_CONTROLS_GC_ADAPTER_H
#define ENGINE_CONTROLS_GC_ADAPTER_H

#include <string>

namespace engine::controls {

inline constexpr unsigned short kGcAdapterVendor = 0x057e;
inline constexpr unsigned short kGcAdapterProduct = 0x0337;

enum class AdapterState {
  NoLibrary,
  NotPlugged,
  Reachable,
  NoDriver,
  WrongDriver,
  NotListed,
  LibraryMissing,
  NoPermission,
  InUse,
  Unknown,
};

struct AdapterProbe {
  AdapterState state = AdapterState::NoLibrary;
  int libusbError = 0;
  std::string driver;
  bool windowsSeen = false;
};

enum class DriverKind { None, WinUsb, LibusbK, Libusb0, Hid, Other };
DriverKind ClassifyWindowsDriver(const std::string& service);

AdapterState ClassifyOpenError(int libusbError, bool windows, bool macOs);

std::string DescribeGameCubeAdapter(const std::string& sdlLines);

AdapterProbe ProbeGameCubeAdapter();

}

#endif
