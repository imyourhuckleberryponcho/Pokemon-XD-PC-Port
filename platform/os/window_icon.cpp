#include "port_window_icon.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propvarutil.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kIconResource = 1;

HICON sBig = nullptr, sSmall = nullptr;

std::wstring Wide(const char* utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  std::wstring out(n > 0 ? n : 0, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
    out.resize(n - 1);
  }
  return out;
}

HICON Load(const std::wstring& file, int size) {
  if (file.empty()) {
    return static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource), IMAGE_ICON, size, size, 0));
  }
  return static_cast<HICON>(LoadImageW(nullptr, file.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
}

}

extern "C" bool PortWindowIcon_Apply(SDL_Window* window, const char* icoPathUtf8) {
  if (window == nullptr) {
    return false;
  }
  HWND hwnd = static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  if (hwnd == nullptr) {
    return false;
  }
  const UINT dpi = GetDpiForWindow(hwnd) != 0 ? GetDpiForWindow(hwnd) : 96;
  const int smallSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi), big = GetSystemMetricsForDpi(SM_CXICON, dpi);
  std::wstring file = icoPathUtf8 != nullptr && icoPathUtf8[0] != '\0' ? Wide(icoPathUtf8) : std::wstring();
  bool fromFile = !file.empty() && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
  HICON b = fromFile ? Load(file, big) : nullptr, s = fromFile ? Load(file, smallSize) : nullptr;
  if (b == nullptr || s == nullptr) {
    if (b != nullptr) {
      DestroyIcon(b);
    }
    if (s != nullptr) {
      DestroyIcon(s);
    }
    fromFile = false;
    b = Load(L"", big);
    s = Load(L"", smallSize);
  }
  if (b == nullptr || s == nullptr) {
    return false;
  }
  SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(b));
  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(s));
  if (sBig != nullptr) {
    DestroyIcon(sBig);
  }
  if (sSmall != nullptr) {
    DestroyIcon(sSmall);
  }
  sBig = b;
  sSmall = s;
  return fromFile;
}

extern "C" bool PortWindowIcon_OwnRGBA(int size, uint8_t* rgba) { return PortWindowIcon_ResourceRGBA(kIconResource, size, rgba); }

extern "C" bool PortWindowIcon_ResourceRGBA(int resource, int size, uint8_t* rgba) {
  HICON icon = static_cast<HICON>(
      LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource), IMAGE_ICON, size, size, 0));
  if (icon == nullptr) {
    return false;
  }
  ICONINFO info{};
  bool ok = false;
  if (GetIconInfo(icon, &info) && info.hbmColor != nullptr) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> bgra(static_cast<size_t>(size) * size * 4);
    HDC dc = GetDC(nullptr);
    ok = GetDIBits(dc, info.hbmColor, 0, size, bgra.data(), &bmi, DIB_RGB_COLORS) == size;
    ReleaseDC(nullptr, dc);
    for (size_t i = 0; ok && i < bgra.size(); i += 4) {
      rgba[i + 0] = bgra[i + 2];
      rgba[i + 1] = bgra[i + 1];
      rgba[i + 2] = bgra[i + 0];
      rgba[i + 3] = bgra[i + 3];
    }
  }
  if (info.hbmColor != nullptr) {
    DeleteObject(info.hbmColor);
  }
  if (info.hbmMask != nullptr) {
    DeleteObject(info.hbmMask);
  }
  DestroyIcon(icon);
  return ok;
}

extern "C" bool PortWindowIcon_SaveResource(int resource, const char* icoPathUtf8) {
  HMODULE self = GetModuleHandleW(nullptr);
  HRSRC group = FindResourceW(self, MAKEINTRESOURCEW(resource), MAKEINTRESOURCEW(14));
  HGLOBAL groupData = group != nullptr ? LoadResource(self, group) : nullptr;
  const auto* dir = static_cast<const uint8_t*>(groupData != nullptr ? LockResource(groupData) : nullptr);
  if (dir == nullptr || SizeofResource(self, group) < 6) {
    return false;
  }
  const uint16_t count = static_cast<uint16_t>(dir[4] | dir[5] << 8);
  if (SizeofResource(self, group) < 6u + 14u * count) {
    return false;
  }
  std::vector<uint8_t> head(dir, dir + 6), body;
  uint32_t offset = 6 + 16 * count;
  for (uint16_t i = 0; i < count; i++) {
    const uint8_t* e = dir + 6 + 14 * i;
    const uint16_t id = static_cast<uint16_t>(e[12] | e[13] << 8);
    HRSRC image = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(3));
    HGLOBAL imageData = image != nullptr ? LoadResource(self, image) : nullptr;
    const auto* bytes = static_cast<const uint8_t*>(imageData != nullptr ? LockResource(imageData) : nullptr);
    if (bytes == nullptr) {
      return false;
    }
    const DWORD size = SizeofResource(self, image);
    head.insert(head.end(), e, e + 12);
    for (int b = 0; b < 4; b++) {
      head.push_back(static_cast<uint8_t>(offset >> (8 * b)));
    }
    body.insert(body.end(), bytes, bytes + size);
    offset += size;
  }
  std::FILE* f = _wfopen(Wide(icoPathUtf8).c_str(), L"wb");
  if (f == nullptr) {
    return false;
  }
  const bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size() &&
                  std::fwrite(body.data(), 1, body.size(), f) == body.size();
  return std::fclose(f) == 0 && ok;
}

extern "C" bool PortWindowIcon_SetAppId(SDL_Window* window, const char* appId) {
  if (window == nullptr || appId == nullptr || appId[0] == '\0') {
    return false;
  }
  HWND hwnd = static_cast<HWND>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  if (hwnd == nullptr) {
    return false;
  }
  const std::wstring id = Wide(appId);
  SetCurrentProcessExplicitAppUserModelID(id.c_str());
  IPropertyStore* store = nullptr;
  if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) {
    return false;
  }
  PROPVARIANT v;
  bool ok = false;
  if (SUCCEEDED(InitPropVariantFromString(id.c_str(), &v))) {
    ok = SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, v)) && SUCCEEDED(store->Commit());
    PropVariantClear(&v);
  }
  store->Release();
  return ok;
}
