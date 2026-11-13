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
#include <utility>
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

struct ListCopies {
  struct Entry {
    uint32_t address, length;
    size_t offset;
  };
  std::vector<uint8_t> bytes;
  std::vector<Entry> entries;
  void clear() {
    bytes.clear();
    entries.clear();
  }
};
ListCopies sCurrentLists, sLastLists, sFrameLists;
std::vector<AuroraCalledList> sReplayLists;

void ListTap(uint32_t address, const void* data, uint32_t length) {
  if (sReplaying.load(std::memory_order_acquire)) {
    return;
  }
  std::lock_guard<std::mutex> lock(sTapLock);
  const auto* bytes = static_cast<const uint8_t*>(data);
  sCurrentLists.entries.push_back({address, length, sCurrentLists.bytes.size()});
  sCurrentLists.bytes.insert(sCurrentLists.bytes.end(), bytes, bytes + length);
}

void BeginListReplay() {
  sReplayLists.clear();
  sReplayLists.reserve(sFrameLists.entries.size());
  for (const auto& e : sFrameLists.entries) {
    sReplayLists.push_back({e.address, e.length, sFrameLists.bytes.data() + e.offset});
  }
  AuroraSetCalledListReplay(sReplayLists.data(), static_cast<uint32_t>(sReplayLists.size()));
}

void EndListReplay() {
  AuroraSetCalledListReplay(nullptr, 0);
  sReplayLists.clear();
}

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
    sLastLists = std::move(sCurrentLists);
    sCurrentLists.clear();
    sPictures++;
  }
}

}

extern "C" {

void PortFreeze_Capture(void) {
  if (sTapOn) return;
  sTapOn = true;
  AuroraSetCommandTap(Tap);
  AuroraSetCalledListTap(ListTap);
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
    sFrameLists = sLastLists;
  }
  if (sFrame.empty()) {
    return false;
  }
  sActive = true;
  sSpeed = aurora::time::scale();
  aurora::time::set_scale(0.0f);
  PortAudio_SetFrozen(true);
  PortMenuMusic_Begin(PortMenuMusic_TakeNext());
  PortRunStatus_SetWaitingForPlayer(true);
  sSavedState = AuroraSaveGXState();
  sReplaying.store(true, std::memory_order_release);
  PortGX_ReplayBegin();
  BeginListReplay();
  return true;
}

const uint8_t* PortFreeze_Picture(size_t* size) {
  if (size != nullptr) *size = sFrame.size();
  return sFrame.data();
}

void PortFreeze_Write(const void* data, size_t size) {
  if (size > 0) PortGX_ReplayWrite(data, static_cast<uint32_t>(size));
}

#if defined(__linux__) || defined(__APPLE__)
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
  EndListReplay();
  sReplaying.store(false, std::memory_order_release);
  AuroraRestoreGXState(sSavedState);
  sSavedState = nullptr;
  PortMenuMusic_End();
  PortAudio_SetFrozen(false);
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
    sFrameLists = sLastLists;
  }
  if (sFrame.empty()) return false;
  sRedrawing = true;
  sSavedState = AuroraSaveGXState();
  sReplaying.store(true, std::memory_order_release);
  PortGX_ReplayBegin();
  BeginListReplay();
  return true;
}

void PortFreeze_RedrawEnd(void) {
  if (!sRedrawing) return;
  PortGX_ReplayEnd();
  EndListReplay();
  sReplaying.store(false, std::memory_order_release);
  AuroraRestoreGXState(sSavedState);
  sSavedState = nullptr;
  sRedrawing = false;
}

}
