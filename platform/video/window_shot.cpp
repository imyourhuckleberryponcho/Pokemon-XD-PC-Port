#include <port_window_shot.h>

#include <aurora/aurora.h>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

extern "C" {

bool PortShot_SavePng(const wchar_t* path, const uint8_t* rgba, uint32_t w, uint32_t h) {
  std::vector<uint8_t> bgr(static_cast<size_t>(w) * h * 3);
  for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; i++) {
    bgr[i * 3 + 0] = rgba[i * 4 + 2];
    bgr[i * 3 + 1] = rgba[i * 4 + 1];
    bgr[i * 3 + 2] = rgba[i * 4 + 0];
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
            IsEqualGUID(format, GUID_WICPixelFormat24bppBGR) &&
            SUCCEEDED(frame->WritePixels(h, w * 3, static_cast<UINT>(bgr.size()), bgr.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  if (frame) frame->Release();
  if (encoder) encoder->Release();
  if (stream) stream->Release();
  if (factory) factory->Release();
  if (SUCCEEDED(com)) CoUninitialize();
  return ok;
}

bool PortShot_SaveWindow(const wchar_t* path) {
  HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()),
                                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  RECT rect;
  if (hwnd == nullptr || !GetClientRect(hwnd, &rect) || rect.right <= 0 || rect.bottom <= 0) return false;
  const int w = rect.right, h = rect.bottom;
  HDC windowDc = GetDC(hwnd);
  HDC memDc = CreateCompatibleDC(windowDc);
  HBITMAP bitmap = CreateCompatibleBitmap(windowDc, w, h);
  HGDIOBJ old = SelectObject(memDc, bitmap);
  PrintWindow(hwnd, memDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
  SelectObject(memDc, old);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
  GetDIBits(memDc, bitmap, 0, h, bgra.data(), &bi, DIB_RGB_COLORS);
  DeleteObject(bitmap);
  DeleteDC(memDc);
  ReleaseDC(hwnd, windowDc);
  for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  return PortShot_SavePng(path, bgra.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

}
