#include "gc_adapter.h"

#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace engine::controls {

AdapterState ClassifyOpenError(int err, bool windows, bool macOs) {
  switch (err) {
  case 0: return AdapterState::Reachable;
  case -3:
    return macOs ? AdapterState::InUse : AdapterState::NoPermission;
  case -6:
    return AdapterState::InUse;
  case -5:
  case -12:
    return windows ? AdapterState::NoDriver : AdapterState::Unknown;
  default: return AdapterState::Unknown;
  }
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

}

AdapterProbe ProbeGameCubeAdapter() {
  static void* lib = LoadUsb();
  AdapterProbe out;
  if (lib == nullptr) {
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
  for (long long i = 0; i < count; i++) {
    DeviceDescriptor d{};
    if (desc(devices[i], &d) != 0 || d.idVendor != kGcAdapterVendor || d.idProduct != kGcAdapterProduct) {
      continue;
    }
    void* handle = nullptr;
    const int err = open(devices[i], &handle);
    out.libusbError = err;
    out.state = ClassifyOpenError(err, kWindows, kMac);
    if (err == 0 && handle != nullptr) {
      close(handle);
    }
    break;
  }
  if (devices != nullptr) {
    freeList(devices, 1);
  }
  exitCtx(ctx);
  return out;
}

}
