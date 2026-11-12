#include <port_capture.h>

#include <aurora/aurora.h>
#include <aurora/time.hpp>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#define _wfopen(path, mode) std::fopen((path), (mode)[1] == L'b' ? "wb" : "w")
#endif

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include <utility>
#include <vector>

extern "C" void PortInput_StopRecording(void);

namespace {

bool sOn = false, sExit = false;
uint32_t sFrom = 0, sTo = UINT32_MAX;
std::filesystem::path sDir;
FILE* sTimeline = nullptr;
uint32_t sPics = 0, sShows = 0;
int sWaitMs = 40;
uint32_t (*sProgress)(void) = nullptr;
uint32_t (*sFieldCount)(void) = nullptr;
std::vector<uint8_t> sPixels, sPrevious;
uint32_t sPrevW = 0, sPrevH = 0;

void Line(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vfprintf(sTimeline, fmt, args);
  va_end(args);
  std::fflush(sTimeline);
}

bool WritePicture(uint32_t pic, const uint8_t* rgba, size_t bytes) {
  char name[32];
  std::snprintf(name, sizeof name, "p%06u.rgba", pic);
  const std::filesystem::path path = sDir / name;
  const std::filesystem::path part = sDir / (std::string(name) + ".part");
  FILE* f = _wfopen(part.c_str(), L"wb");
  if (f == nullptr) return false;
  std::setvbuf(f, nullptr, _IOFBF, 1 << 22);
  const bool ok = std::fwrite(rgba, 1, bytes, f) == bytes;
  std::fclose(f);
  std::error_code ec;
  std::filesystem::rename(part, path, ec);
  return ok && !ec;
}

#ifndef _WIN32
bool GrabWindow(uint32_t*, uint32_t*) { return false; }
#else
bool GrabWindow(uint32_t* wOut, uint32_t* hOut) {
  HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(aurora_get_window()),
                                                        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  RECT rect;
  if (hwnd == nullptr || !GetClientRect(hwnd, &rect) || rect.right <= 0 || rect.bottom <= 0) return false;
  const int w = rect.right, h = rect.bottom;
  HDC windowDc = GetDC(hwnd);
  HDC memDc = CreateCompatibleDC(windowDc);
  HBITMAP bitmap = CreateCompatibleBitmap(windowDc, w, h);
  HGDIOBJ old = SelectObject(memDc, bitmap);
  const bool printed = PrintWindow(hwnd, memDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != FALSE;
  SelectObject(memDc, old);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  sPixels.resize(static_cast<size_t>(w) * h * 4);
  const bool read = printed && GetDIBits(memDc, bitmap, 0, h, sPixels.data(), &bi, DIB_RGB_COLORS) == h;
  DeleteObject(bitmap);
  DeleteDC(memDc);
  ReleaseDC(hwnd, windowDc);
  for (size_t i = 0; i < sPixels.size(); i += 4) {
    std::swap(sPixels[i], sPixels[i + 2]);
    sPixels[i + 3] = 255;
  }
  *wOut = static_cast<uint32_t>(w);
  *hOut = static_cast<uint32_t>(h);
  return read;
}
#endif

void Finish() {
  Line("end pictures %u refreshes %u\n", sPics, sShows);
  std::fclose(sTimeline);
  sTimeline = nullptr;
  std::fprintf(stderr, "[capture] done: %u pictures for %u refreshes (%s)\n", sPics, sShows, sDir.u8string().c_str());
  sOn = false;
  if (sExit) {
    std::fprintf(stderr, "[capture] XD_CAPTURE_EXIT: the range is over\n");
    PortInput_StopRecording();
    std::fflush(stdout);
    std::fflush(stderr);
    _Exit(0);
  }
}

}

extern "C" {

bool PortCapture_WindowSize(int* width, int* height) {
  const char* size = std::getenv("XD_CAPTURE_WINDOW");
  int w = 0, h = 0;
  if (size == nullptr || std::sscanf(size, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return false;
  *width = w;
  *height = h;
  return true;
}

bool PortCapture_Init(uint32_t (*progress)(void), uint32_t (*field)(void)) {
  const char* dir = std::getenv("XD_CAPTURE_DIR");
  if (dir == nullptr || dir[0] == '\0') return false;
  sProgress = progress;
  sFieldCount = field;
  sDir = std::filesystem::u8path(dir);
  std::error_code ec;
  std::filesystem::create_directories(sDir, ec);
  if (const char* range = std::getenv("XD_CAPTURE_FRAMES"); range != nullptr && range[0] != '\0') {
    unsigned long a = 0, b = 0;
    if (std::sscanf(range, "%lu-%lu", &a, &b) == 2) {
      sFrom = static_cast<uint32_t>(a), sTo = static_cast<uint32_t>(b);
    } else if (std::sscanf(range, "%lu", &a) == 1) {
      sFrom = static_cast<uint32_t>(a);
    }
  }
  const char* exitAfter = std::getenv("XD_CAPTURE_EXIT");
  sExit = exitAfter != nullptr && exitAfter[0] == '1';
  if (const char* wait = std::getenv("XD_CAPTURE_WAIT"); wait != nullptr && wait[0] != '\0') {
    sWaitMs = std::atoi(wait);
  }
  sTimeline = _wfopen((sDir / "timeline.txt").c_str(), L"w");
  if (sTimeline == nullptr) {
    std::fprintf(stderr, "[capture] can't write into %s\n", dir);
    return false;
  }
  Line("capture frames %u-%u\n", sFrom, sTo);
  sOn = true;
  std::fprintf(stderr, "[capture] XD_CAPTURE_DIR: the window at every refresh of game frames %u-%u into %s\n", sFrom,
               sTo, dir);
  return true;
}

void PortCapture_Field(void) {
  if (!sOn) return;
  const uint32_t flip = sProgress();
  if (flip > sTo) {
    Finish();
    return;
  }
  if (flip < sFrom) return;
  const uint32_t field = sFieldCount();
  const float speed = aurora::time::scale();
  aurora::time::set_scale(0.0f);
  std::this_thread::sleep_for(std::chrono::milliseconds(sWaitMs));
  uint32_t w = 0, h = 0;
  if (GrabWindow(&w, &h)) {
    const bool same = w == sPrevW && h == sPrevH && sPics > 0 && sPixels == sPrevious;
    if (!same) {
      if (WritePicture(sPics + 1, sPixels.data(), sPixels.size())) {
        sPics++;
        Line("pic %u %u %u flip %u field %u\n", sPics, w, h, flip, field);
        sPrevious.swap(sPixels);
        sPrevW = w, sPrevH = h;
      } else {
        std::fprintf(stderr, "[capture] could not write a picture into %s\n", sDir.u8string().c_str());
      }
    }
  } else {
    std::fprintf(stderr, "[capture] the window could not be read (field %u)\n", field);
  }
  sShows++;
  Line("show %u %u %u\n", field, flip, sPics);
  aurora::time::set_scale(speed);
}

}
