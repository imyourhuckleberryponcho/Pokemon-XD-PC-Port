#include "gc_adapter.h"

#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#else
#include <dlfcn.h>
#endif

namespace engine::controls {

namespace {
std::string Lower(std::string s) {
  for (char& c : s) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return s;
}
}

AdapterState ClassifyOpenError(int err, bool windows, bool macOs) {
  switch (err) {
  case 0: return AdapterState::Reachable;
  case -3:
    return macOs || windows ? AdapterState::InUse : AdapterState::NoPermission;
  case -6:
    return AdapterState::InUse;
  case -5:
  case -12:
    return windows ? AdapterState::NoDriver : AdapterState::Unknown;
  default: return AdapterState::Unknown;
  }
}

DriverKind ClassifyWindowsDriver(const std::string& service) {
  const std::string s = Lower(service);
  if (s.empty()) {
    return DriverKind::None;
  }
  if (s == "winusb") {
    return DriverKind::WinUsb;
  }
  if (s == "libusbk") {
    return DriverKind::LibusbK;
  }
  if (s == "libusb0" || s == "libusb-win32") {
    return DriverKind::Libusb0;
  }
  if (s == "hidusb" || s == "hidclass" || s == "usbccgp") {
    return DriverKind::Hid;
  }
  return DriverKind::Other;
}

namespace {

struct DeviceDescriptor {
  uint8_t bLength, bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
  uint16_t idVendor, idProduct, bcdDevice;
  uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};
static_assert(sizeof(DeviceDescriptor) == 18, "libusb_device_descriptor is 18 bytes");

struct LibusbVersion {
  uint16_t major, minor, micro, nano;
  const char* rc;
  const char* describe;
};

#ifdef _WIN32
#define XD_USBCALL __stdcall
#else
#define XD_USBCALL
#endif
using InitFn = int(XD_USBCALL*)(void**);
using ExitFn = void(XD_USBCALL*)(void*);
using ListFn = long long(XD_USBCALL*)(void*, void***);
using FreeListFn = void(XD_USBCALL*)(void**, int);
using DescFn = int(XD_USBCALL*)(void*, DeviceDescriptor*);
using OpenFn = int(XD_USBCALL*)(void*, void**);
using CloseFn = void(XD_USBCALL*)(void*);
using ClaimFn = int(XD_USBCALL*)(void*, int);
using ReleaseFn = int(XD_USBCALL*)(void*, int);
using ErrNameFn = const char*(XD_USBCALL*)(int);
using VersionFn = const LibusbVersion*(XD_USBCALL*)();
using PortsFn = int(XD_USBCALL*)(void*, uint8_t*, int);
using BusFn = uint8_t(XD_USBCALL*)(void*);

#ifdef _WIN32
void* LoadUsb() { return reinterpret_cast<void*>(LoadLibraryA("libusb-1.0.dll")); }
void* Sym(void* lib, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
}
constexpr bool kWindows = true, kMac = false;
#else
void* LoadUsb() {
#ifdef __APPLE__
  return dlopen("libusb-1.0.0.dylib", RTLD_NOW | RTLD_LOCAL);
#else
  return dlopen("libusb-1.0.so.0", RTLD_NOW | RTLD_LOCAL);
#endif
}
void* Sym(void* lib, const char* name) { return dlsym(lib, name); }
constexpr bool kWindows = false;
#ifdef __APPLE__
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif
#endif

void* Lib() {
  static void* lib = LoadUsb();
  return lib;
}

struct WindowsDevice {
  bool found = false;
  std::string driver;
  std::string description;
  unsigned problem = 0;
  std::string error;
};

#ifdef _WIN32
WindowsDevice LookWindowsDevices() {
  WindowsDevice out;
  const HDEVINFO set = SetupDiGetClassDevsA(nullptr, "USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (set == INVALID_HANDLE_VALUE) {
    out.error = "SetupDiGetClassDevs failed (" + std::to_string(GetLastError()) + ")";
    return out;
  }
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof(info);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); i++) {
    char ids[1024]{};
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_HARDWAREID, &type, reinterpret_cast<PBYTE>(ids),
                                           sizeof(ids) - 2, &size)) {
      continue;
    }
    bool match = false;
    for (const char* p = ids; *p != 0; p += std::char_traits<char>::length(p) + 1) {
      if (Lower(p).find("vid_057e&pid_0337") != std::string::npos) {
        match = true;
        break;
      }
    }
    if (!match) {
      continue;
    }
    out.found = true;
    char text[256]{};
    if (SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_SERVICE, &type, reinterpret_cast<PBYTE>(text),
                                          sizeof(text) - 1, &size)) {
      out.driver = text;
    }
    char desc[256]{};
    if (SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_DEVICEDESC, &type, reinterpret_cast<PBYTE>(desc),
                                          sizeof(desc) - 1, &size)) {
      out.description = desc;
    }
    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS) {
      out.problem = static_cast<unsigned>(problem);
    }
    break;
  }
  SetupDiDestroyDeviceInfoList(set);
  return out;
}
#else
WindowsDevice LookWindowsDevices() { return {}; }
#endif

AdapterState StateForDriver(const WindowsDevice& w, AdapterState otherwise) {
  switch (ClassifyWindowsDriver(w.driver)) {
  case DriverKind::None: return AdapterState::NoDriver;
  case DriverKind::WinUsb: return otherwise;
  default: return AdapterState::WrongDriver;
  }
}

std::string ModulePath(void* lib) {
#ifdef _WIN32
  char path[1024]{};
  if (GetModuleFileNameA(reinterpret_cast<HMODULE>(lib), path, sizeof(path) - 1) != 0) {
    return path;
  }
#else
  (void)lib;
#endif
  return {};
}

const char* StateName(AdapterState s) {
  switch (s) {
  case AdapterState::NoLibrary: return "NoLibrary";
  case AdapterState::NotPlugged: return "NotPlugged";
  case AdapterState::Reachable: return "Reachable";
  case AdapterState::NoDriver: return "NoDriver";
  case AdapterState::WrongDriver: return "WrongDriver";
  case AdapterState::NotListed: return "NotListed";
  case AdapterState::LibraryMissing: return "LibraryMissing";
  case AdapterState::NoPermission: return "NoPermission";
  case AdapterState::InUse: return "InUse";
  case AdapterState::Unknown: return "Unknown";
  }
  return "?";
}

}

AdapterProbe ProbeGameCubeAdapter() {
  void* lib = Lib();
  AdapterProbe out;
  if (kWindows) {
    const WindowsDevice w = LookWindowsDevices();
    out.windowsSeen = w.found;
    out.driver = w.driver;
  }
  if (lib == nullptr) {
    if (out.windowsSeen) {
      out.state = AdapterState::LibraryMissing;
    }
    return out;
  }
  const auto init = reinterpret_cast<InitFn>(Sym(lib, "libusb_init"));
  const auto exitCtx = reinterpret_cast<ExitFn>(Sym(lib, "libusb_exit"));
  const auto list = reinterpret_cast<ListFn>(Sym(lib, "libusb_get_device_list"));
  const auto freeList = reinterpret_cast<FreeListFn>(Sym(lib, "libusb_free_device_list"));
  const auto desc = reinterpret_cast<DescFn>(Sym(lib, "libusb_get_device_descriptor"));
  const auto open = reinterpret_cast<OpenFn>(Sym(lib, "libusb_open"));
  const auto close = reinterpret_cast<CloseFn>(Sym(lib, "libusb_close"));
  if (!init || !exitCtx || !list || !freeList || !desc || !open || !close) {
    return out;
  }
  out.state = AdapterState::NotPlugged;
  void* ctx = nullptr;
  if (init(&ctx) != 0) {
    out.state = AdapterState::Unknown;
    return out;
  }
  void** devices = nullptr;
  const long long count = list(ctx, &devices);
  bool listed = false;
  for (long long i = 0; i < count; i++) {
    DeviceDescriptor d{};
    if (desc(devices[i], &d) != 0 || d.idVendor != kGcAdapterVendor || d.idProduct != kGcAdapterProduct) {
      continue;
    }
    listed = true;
    void* handle = nullptr;
    const int err = open(devices[i], &handle);
    out.libusbError = err;
    out.state = ClassifyOpenError(err, kWindows, kMac);
    if (kWindows && out.windowsSeen && err != 0 &&
        (out.state == AdapterState::NoDriver || out.state == AdapterState::Unknown)) {
      out.state = StateForDriver(WindowsDevice{true, out.driver, {}, 0, {}}, AdapterState::Unknown);
    }
    if (err == 0 && handle != nullptr) {
      close(handle);
    }
    break;
  }
  if (devices != nullptr) {
    freeList(devices, 1);
  }
  exitCtx(ctx);
  if (!listed && out.windowsSeen) {
    out.state = StateForDriver(WindowsDevice{true, out.driver, {}, 0, {}}, AdapterState::NotListed);
  }
  return out;
}

std::string DescribeGameCubeAdapter(const std::string& sdlLines) {
  std::string out;
  const auto line = [&out](const std::string& text) {
    if (!out.empty()) {
      out += '\n';
    }
    out += "[gc adapter] " + text;
  };
  line(std::string("platform: ") + (kWindows ? "Windows" : kMac ? "macOS" : "Linux/other"));
  void* lib = Lib();
  const auto init = lib ? reinterpret_cast<InitFn>(Sym(lib, "libusb_init")) : nullptr;
  const auto exitCtx = lib ? reinterpret_cast<ExitFn>(Sym(lib, "libusb_exit")) : nullptr;
  const auto list = lib ? reinterpret_cast<ListFn>(Sym(lib, "libusb_get_device_list")) : nullptr;
  const auto freeList = lib ? reinterpret_cast<FreeListFn>(Sym(lib, "libusb_free_device_list")) : nullptr;
  const auto desc = lib ? reinterpret_cast<DescFn>(Sym(lib, "libusb_get_device_descriptor")) : nullptr;
  const auto open = lib ? reinterpret_cast<OpenFn>(Sym(lib, "libusb_open")) : nullptr;
  const auto close = lib ? reinterpret_cast<CloseFn>(Sym(lib, "libusb_close")) : nullptr;
  const auto claim = lib ? reinterpret_cast<ClaimFn>(Sym(lib, "libusb_claim_interface")) : nullptr;
  const auto release = lib ? reinterpret_cast<ReleaseFn>(Sym(lib, "libusb_release_interface")) : nullptr;
  const auto errName = lib ? reinterpret_cast<ErrNameFn>(Sym(lib, "libusb_error_name")) : nullptr;
  const auto version = lib ? reinterpret_cast<VersionFn>(Sym(lib, "libusb_get_version")) : nullptr;
  const auto ports = lib ? reinterpret_cast<PortsFn>(Sym(lib, "libusb_get_port_numbers")) : nullptr;
  const auto bus = lib ? reinterpret_cast<BusFn>(Sym(lib, "libusb_get_bus_number")) : nullptr;
  const auto errText = [&](int err) {
    const char* name = errName != nullptr ? errName(err) : nullptr;
    return std::to_string(err) + (name != nullptr ? std::string(" ") + name : std::string());
  };

  if (lib == nullptr) {
    line(kWindows ? "libusb: NOT loaded (libusb-1.0.dll not found next to the program or on the path)"
                  : "libusb: NOT loaded (libusb-1.0 shared library not found)");
  } else {
    std::string v;
    if (version != nullptr) {
      if (const LibusbVersion* ver = version()) {
        v = " version " + std::to_string(ver->major) + "." + std::to_string(ver->minor) + "." +
            std::to_string(ver->micro) + "." + std::to_string(ver->nano) +
            (ver->describe != nullptr && *ver->describe != 0 ? std::string(" (") + ver->describe + ")" : std::string());
      }
    }
    const std::string path = ModulePath(lib);
    line("libusb: loaded" + (path.empty() ? std::string() : " from " + path) + v);
  }

  WindowsDevice w;
  if (kWindows) {
    w = LookWindowsDevices();
    if (!w.error.empty()) {
      line("Windows device list: " + w.error);
    } else if (!w.found) {
      line("Windows device list: no USB device 057e:0337 (adapter not plugged in, or not seen by Windows)");
    } else {
      line("Windows device list: 057e:0337 \"" + w.description + "\", driver: " +
           (w.driver.empty() ? std::string("none") : w.driver) + ", Device Manager problem code " +
           std::to_string(w.problem) + (w.problem == 28 ? " (no driver installed)" : w.problem == 0 ? " (none)" : ""));
      const DriverKind k = ClassifyWindowsDriver(w.driver);
      if (k == DriverKind::WinUsb) {
        line("driver check: WinUSB = the driver this program needs");
      } else if (k == DriverKind::None) {
        line("driver check: no driver: install WinUSB with Zadig");
      } else if (k == DriverKind::LibusbK) {
        line("driver check: libusbK: libusb can use it only with libusbK.dll present; WinUSB is the safe choice");
      } else if (k == DriverKind::Libusb0) {
        line("driver check: libusb0 (libusb-win32): NOT supported by libusb 1.0; choose WinUSB in Zadig");
      } else {
        line("driver check: " + w.driver + " is not a driver libusb can use; choose WinUSB in Zadig");
      }
    }
  }

  if (lib != nullptr && init && exitCtx && list && freeList && desc && open && close) {
    void* ctx = nullptr;
    const int initErr = init(&ctx);
    if (initErr != 0) {
      line("libusb_init failed: " + errText(initErr));
    } else {
      void** devices = nullptr;
      const long long count = list(ctx, &devices);
      line("libusb lists " + std::to_string(count < 0 ? 0 : count) + " USB devices" +
           (count < 0 ? " (list error " + errText(static_cast<int>(count)) + ")" : std::string()));
      int nintendo = 0;
      for (long long i = 0; i < count; i++) {
        DeviceDescriptor d{};
        if (desc(devices[i], &d) != 0 || d.idVendor != kGcAdapterVendor) {
          continue;
        }
        nintendo++;
        char id[16];
        std::snprintf(id, sizeof(id), "%04x:%04x", d.idVendor, d.idProduct);
        std::string where;
        if (bus != nullptr) {
          where += " bus " + std::to_string(bus(devices[i]));
        }
        if (ports != nullptr) {
          uint8_t p[8]{};
          const int n = ports(devices[i], p, 8);
          if (n > 0) {
            where += " port";
            for (int k = 0; k < n; k++) {
              where += (k == 0 ? " " : ".") + std::to_string(p[k]);
            }
          }
        }
        line(std::string("libusb Nintendo device ") + id + where + (d.idProduct == kGcAdapterProduct ? " = the GameCube adapter" : ""));
        if (d.idProduct != kGcAdapterProduct) {
          continue;
        }
        void* handle = nullptr;
        const int err = open(devices[i], &handle);
        line("  libusb_open: " + (err == 0 ? std::string("ok") : errText(err)) + " -> " +
             StateName(ClassifyOpenError(err, kWindows, kMac)));
        if (err == 0 && handle != nullptr) {
          if (claim != nullptr) {
            const int cerr = claim(handle, 0);
            line("  claim interface 0: " + (cerr == 0 ? std::string("ok (nobody else holds it right now)") : errText(cerr)) +
                 (cerr == -3 || cerr == -6 ? "  (busy: another program, or this program's own SDL, holds the adapter)" : ""));
            if (cerr == 0 && release != nullptr) {
              release(handle, 0);
            }
          }
          close(handle);
        }
      }
      line("Nintendo (057e) devices seen by libusb: " + std::to_string(nintendo));
      if (devices != nullptr) {
        freeList(devices, 1);
      }
      exitCtx(ctx);
    }
  }
  const AdapterProbe probe = ProbeGameCubeAdapter();
  line(std::string("verdict: ") + StateName(probe.state) + " (libusb error " + std::to_string(probe.libusbError) +
       (probe.driver.empty() ? std::string() : ", driver " + probe.driver) + ")");
  if (!sdlLines.empty()) {
    out += '\n';
    out += sdlLines;
  }
  return out;
}

}
