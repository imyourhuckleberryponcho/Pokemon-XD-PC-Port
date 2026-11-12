#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <aurora/time.hpp>
#include <port_user_folder.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

extern "C" const char* PortInput_RecordingPath(void);

namespace {

bool SavePng(HBITMAP bitmap, const wchar_t* path) {
  IWICImagingFactory* factory = nullptr;
  IWICBitmap* source = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source)) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->WriteSource(source, nullptr)) && SUCCEEDED(frame->Commit()) &&
            SUCCEEDED(encoder->Commit());
  if (frame) frame->Release();
  if (encoder) encoder->Release();
  if (stream) stream->Release();
  if (source) source->Release();
  if (factory) factory->Release();
  return ok;
}

}

extern "C" void* PortXD_CaptureWindow(void* hwnd, int* width, int* height) {
  HWND window = static_cast<HWND>(hwnd);
  RECT rect;
  if (window == nullptr || !GetClientRect(window, &rect) || rect.right <= 0 || rect.bottom <= 0) {
    return nullptr;
  }
  *width = rect.right;
  *height = rect.bottom;
  HDC windowDc = GetDC(window);
  HDC memDc = CreateCompatibleDC(windowDc);
  HBITMAP bitmap = CreateCompatibleBitmap(windowDc, rect.right, rect.bottom);
  HGDIOBJ old = SelectObject(memDc, bitmap);
  const bool captured = PrintWindow(window, memDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE;
  SelectObject(memDc, old);
  DeleteDC(memDc);
  ReleaseDC(window, windowDc);
  if (!captured) {
    DeleteObject(bitmap);
    return nullptr;
  }
  return bitmap;
}

extern "C" bool PortXD_SavePng(void* bitmap, const wchar_t* path) {
  if (bitmap == nullptr) {
    return false;
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool saved = SavePng(static_cast<HBITMAP>(bitmap), path);
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
  DeleteObject(static_cast<HBITMAP>(bitmap));
  return saved;
}

extern "C" void PortXD_ScreenshotTo(void* hwnd, const char* folder, uint32_t field, uint32_t gameFrame) {
  const float speed = aurora::time::scale();
  aurora::time::set_scale(0.0f);
  Sleep(300);
  int width = 0, height = 0;
  void* bitmap = PortXD_CaptureWindow(hwnd, &width, &height);
  wchar_t png[MAX_PATH];
  swprintf(png, MAX_PATH, L"%hs\\f%u.png", folder, gameFrame);
  const bool saved = PortXD_SavePng(bitmap, png);
  char txt[MAX_PATH];
  std::snprintf(txt, sizeof txt, "%s\\f%u.txt", folder, gameFrame);
  if (FILE* f = saved ? std::fopen(txt, "w") : nullptr) {
    std::fprintf(f, "video field %u\ngame frame %u\nwindow %dx%d\n", field, gameFrame, width, height);
    std::fclose(f);
  }
  std::fprintf(stderr, "[screenshot] %s: %ls (field %u, game frame %u)\n", saved ? "saved" : "FAILED", png, field,
               gameFrame);
  aurora::time::set_scale(speed);
}

extern "C" void PortXD_Screenshot(void* hwnd, uint32_t field, uint32_t gameFrame) {
  int width = 0, height = 0;
  void* bitmap = PortXD_CaptureWindow(hwnd, &width, &height);
  if (bitmap == nullptr) {
    std::fprintf(stderr, "[screenshot] no window to capture\n");
    return;
  }

  wchar_t folder[MAX_PATH];
  swprintf(folder, MAX_PATH, L"%ls\\screenshots", PortUser_Folder("Pokemon XD").c_str());
  CreateDirectoryW(folder, nullptr);

  const std::time_t now = std::time(nullptr);
  std::tm local;
  localtime_s(&local, &now);
  wchar_t name[64];
  wcsftime(name, 64, L"shot_%Y%m%d_%H%M%S", &local);
  wchar_t png[MAX_PATH], txt[MAX_PATH], latest[MAX_PATH];
  swprintf(png, MAX_PATH, L"%ls\\%ls_f%u.png", folder, name, field);
  swprintf(txt, MAX_PATH, L"%ls\\%ls_f%u.txt", folder, name, field);
  swprintf(latest, MAX_PATH, L"%ls\\latest.png", folder);

  if (!PortXD_SavePng(bitmap, png)) {
    std::fprintf(stderr, "[screenshot] could not save the picture\n");
    return;
  }
  CopyFileW(png, latest, FALSE);
  if (FILE* f = _wfopen(txt, L"w")) {
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(f, "time %s\nvideo field %u\ngame frame %u\nwindow %dx%d\n", when, field, gameFrame, width, height);
    if (PortInput_RecordingPath()[0] != '\0') {
      std::fprintf(f, "input recording %s\n", PortInput_RecordingPath());
    }
    std::fclose(f);
  }
  std::fprintf(stderr, "[screenshot] %ls (field %u, game frame %u)\n", png, field, gameFrame);
}
