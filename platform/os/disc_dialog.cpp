#include "port_disc_dialog.h"
#include "run_status.h"

#include <borealis/file_select.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#ifdef __ANDROID__
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_system.h>

#include <atomic>

namespace {
struct AndroidPick {
  std::atomic<int> state{0};
  std::string uri;
};
void SDLCALL AndroidPickDone(void* userdata, const char* const* fileList, int) {
  AndroidPick* pick = static_cast<AndroidPick*>(userdata);
  if (fileList == nullptr) {
    std::fprintf(stderr, "[disc dialog] the document picker failed: %s\n", SDL_GetError());
    pick->state = 3;
  } else if (fileList[0] == nullptr) {
    pick->state = 2;
  } else {
    pick->uri = fileList[0];
    pick->state = 1;
  }
}
}

extern "C" PortDiscDialogResult PortDiscDialog_Open(const char*, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return PORT_DISC_DIALOG_FAILED;
  }
  out[0] = '\0';
  static const SDL_DialogFileFilter filters[] = {{"GameCube disc images", "iso;gcm;rvz;wia;gcz;ciso;wbfs"},
                                                 {"All files", "*"}};
  AndroidPick pick;
  PortRunStatus_SetWaitingForPlayer(true);
  SDL_ShowOpenFileDialog(&AndroidPickDone, &pick, nullptr, filters, 2, nullptr, false);
  while (pick.state == 0) {
    SDL_Delay(50);
  }
  PortRunStatus_SetWaitingForPlayer(false);
  if (pick.state == 2) {
    return PORT_DISC_DIALOG_CANCELED;
  }
  if (pick.state != 1 || pick.uri.size() + 1 > outSize) {
    return PORT_DISC_DIALOG_FAILED;
  }
  std::memcpy(out, pick.uri.c_str(), pick.uri.size() + 1);
  std::fprintf(stderr, "[disc dialog] picked: %s\n", out);
  return PORT_DISC_DIALOG_CHOSEN;
}
#else
extern "C" PortDiscDialogResult PortDiscDialog_Open(const char* startFolder, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return PORT_DISC_DIALOG_FAILED;
  }
  out[0] = '\0';
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "[disc dialog] SDL couldn't start: %s\n", SDL_GetError());
    return PORT_DISC_DIALOG_FAILED;
  }

  std::optional<borealis::file_select::Result> answer;
  borealis::file_select::FileOptions options;
  options.filters = {
      {"GameCube disc images", "iso;gcm;rvz;wia;gcz;ciso;wbfs"},
      {"All files", "*"},
  };
  if (startFolder != nullptr) {
    options.defaultLocation = startFolder;
  }
  PortRunStatus_SetWaitingForPlayer(true);
  borealis::file_select::open_file(std::move(options),
                                   [&answer](borealis::file_select::Result result) { answer = std::move(result); });
  while (!answer) {
    SDL_PumpEvents();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
    }
    SDL_Delay(10);
  }
  PortRunStatus_SetWaitingForPlayer(false);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);

  switch (answer->status) {
  case borealis::file_select::Status::Selected:
    if (answer->locations.empty() || answer->locations.front().size() + 1 > outSize) {
      std::fprintf(stderr, "[disc dialog] the chosen file's path is too long\n");
      return PORT_DISC_DIALOG_FAILED;
    }
    std::memcpy(out, answer->locations.front().c_str(), answer->locations.front().size() + 1);
    return PORT_DISC_DIALOG_CHOSEN;
  case borealis::file_select::Status::Canceled:
    return PORT_DISC_DIALOG_CANCELED;
  default:
    std::fprintf(stderr, "[disc dialog] the file window couldn't be shown: %s\n", answer->message.c_str());
    return PORT_DISC_DIALOG_FAILED;
  }
}
#endif
