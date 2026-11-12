#include <port_freeze.h>

#include <port_capture.h>

#include <port_audio.h>
#include <port_menu_music.h>
#include <run_status.h>

#include <aurora/aurora.h>
#include <aurora/time.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <SDL3/SDL_timer.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

extern "C" {
void PortGX_ReplayBegin(void);
void PortGX_ReplayWrite(const void* data, uint32_t size);
void PortGX_ReplayEnd(void);
}

namespace {

std::mutex sTapLock;
std::vector<uint8_t> sCurrent;
std::vector<uint8_t> sLast;
uint32_t sPictures = 0;
std::atomic<bool> sReplaying{false};
bool sTapOn = false;

std::vector<uint8_t> sFrame;
bool sActive = false;
bool sRedrawing = false;
float sSpeed = 1.f;
void* sSavedState = nullptr;

void Tap(const void* data, u32 size, GXBool displayCopy) {
  if (sReplaying.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard<std::mutex> lock(sTapLock);
  const auto* bytes = static_cast<const uint8_t*>(data);
  sCurrent.insert(sCurrent.end(), bytes, bytes + size);
  if (displayCopy) {
    sLast.swap(sCurrent);
    sCurrent.clear();
    sPictures++;
  }
}

}

extern "C" {

void PortFreeze_Capture(void) {
  if (sTapOn) return;
  sTapOn = true;
  AuroraSetCommandTap(Tap);
}

bool PortFreeze_Ready(int* waitHooks) {
  size_t partial = 0;
  uint32_t pictures = 0;
  {
    std::lock_guard<std::mutex> lock(sTapLock);
    partial = sCurrent.size();
    pictures = sPictures;
  }
  if (pictures == 0) {
    return false;
  }
  if (partial > 0 && ++*waitHooks < 30) {
    return false;
  }
  if (partial > 0) {
    std::fprintf(stderr, "[freeze] pausing in the middle of a picture (%zu bytes of it drawn)\n", partial);
  }
  *waitHooks = 0;
  return true;
}

bool PortFreeze_Begin(void) {
  if (sActive || sRedrawing) return false;
  {
    std::lock_guard<std::mutex> lock(sTapLock);
    sFrame = sLast;
  }
  if (sFrame.empty()) {
    return false;
  }
  sActive = true;
  sSpeed = aurora::time::scale();
  aurora::time::set_scale(0.0f);
  PortMenuMusic_Begin(PortMenuMusic_TakeNext());
  PortRunStatus_SetWaitingForPlayer(true);
  sSavedState = AuroraSaveGXState();
  sReplaying.store(true, std::memory_order_release);
  PortGX_ReplayBegin();
  return true;
}

const uint8_t* PortFreeze_Picture(size_t* size) {
  if (size != nullptr) *size = sFrame.size();
  return sFrame.data();
}

void PortFreeze_Write(const void* data, size_t size) {
  if (size > 0) PortGX_ReplayWrite(data, static_cast<uint32_t>(size));
}

#if defined(__linux__)
extern "C" bool PortShot_LinuxFrozenDraw(const uint8_t* frame, size_t size);
void PortFreeze_DrawPicture(void) {
  if (sActive && PortShot_LinuxFrozenDraw(sFrame.data(), sFrame.size())) return;
  PortFreeze_Write(sFrame.data(), sFrame.size());
}
#else
void PortFreeze_DrawPicture(void) { PortFreeze_Write(sFrame.data(), sFrame.size()); }
#endif

void PortFreeze_Present(void (*pump)(void)) {
  aurora_end_frame();
  PortCapture_Field();
  pump();
  while (!aurora_begin_frame()) {
    pump();
    SDL_Delay(10);
  }
}

void PortFreeze_End(void) {
  if (!sActive) return;
  PortGX_ReplayEnd();
  sReplaying.store(false, std::memory_order_release);
  AuroraRestoreGXState(sSavedState);
  sSavedState = nullptr;
  PortMenuMusic_End();
  aurora::time::set_scale(sSpeed);
  PortRunStatus_SetWaitingForPlayer(false);
  sActive = false;
}

bool PortFreeze_Active(void) { return sActive; }

uint32_t PortFreeze_PictureCount(size_t* partial) {
  std::lock_guard<std::mutex> lock(sTapLock);
  if (partial != nullptr) *partial = sCurrent.size();
  return sPictures;
}

bool PortFreeze_RedrawBegin(void) {
  if (sActive || sRedrawing) return false;
  {
    std::lock_guard<std::mutex> lock(sTapLock);
    sFrame = sLast;
  }
  if (sFrame.empty()) return false;
  sRedrawing = true;
  sSavedState = AuroraSaveGXState();
  sReplaying.store(true, std::memory_order_release);
  PortGX_ReplayBegin();
  return true;
}

void PortFreeze_RedrawEnd(void) {
  if (!sRedrawing) return;
  PortGX_ReplayEnd();
  sReplaying.store(false, std::memory_order_release);
  AuroraRestoreGXState(sSavedState);
  sSavedState = nullptr;
  sRedrawing = false;
}

}
