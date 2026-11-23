#ifndef ENGINE_CONTROLS_GC_ADAPTER_H
#define ENGINE_CONTROLS_GC_ADAPTER_H

namespace engine::controls {

inline constexpr unsigned short kGcAdapterVendor = 0x057e;
inline constexpr unsigned short kGcAdapterProduct = 0x0337;

enum class AdapterState {
  NoLibrary,
  NotPlugged,
  Reachable,
  NoDriver,
  NoPermission,
  InUse,
  Unknown,
};

struct AdapterProbe {
  AdapterState state = AdapterState::NoLibrary;
  int libusbError = 0;
};

AdapterState ClassifyOpenError(int libusbError, bool windows, bool macOs);

AdapterProbe ProbeGameCubeAdapter();

}

#endif
