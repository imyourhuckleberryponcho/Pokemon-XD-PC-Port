#include "disc_verify.h"
#include "xd_user_folder.h"

#include <port_disc_dialog.h>
#include <settings.h>
#ifdef __ANDROID__
#include <SDL3/SDL_iostream.h>
#endif

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
namespace es = engine::settings;

namespace {

constexpr const char* kSetting = "system.disc_path";
std::string sChosen;
#ifdef __ANDROID__
std::string sChosenUri;
bool IsContentUri(const std::string& text) { return text.rfind("content://", 0) == 0; }
bool OpenContent(const std::string& uri) {
  SDL_IOStream* io = SDL_IOFromFile(uri.c_str(), "rb");
  if (io == nullptr) {
    return false;
  }
  SDL_CloseIO(io);
  sChosen = uri;
  sChosenUri = uri;
  return true;
}
#endif

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string Remembered() { return es::Global().Text(kSetting); }

void Store(const std::string& path) {
  es::Registry& registry = es::Global();
  if (registry.Text(kSetting) == path) {
    return;
  }
  if (!registry.SetText(kSetting, path)) {
    std::fprintf(stderr, "[disc] this disc image's path can't be stored in settings.ini: %s\n", path.c_str());
    return;
  }
  if (xd::SaveSettings(registry) == xd::SettingsSave::Failed) {
    std::fprintf(stderr, "[disc] couldn't write settings.ini (%s)\n", kSetting);
  }
}

}

extern "C" const char* PortXD_ChooseDisc(const char* given, bool interactive) {
  if (given != nullptr && given[0] != '\0') {
#ifdef __ANDROID__
    if (IsContentUri(given) && OpenContent(given)) {
      return sChosen.c_str();
    }
#endif
    return given;
  }
  if (!interactive) {
    return nullptr;
  }

  const std::string last = Remembered();
  std::error_code ec;
#ifdef __ANDROID__
  if (IsContentUri(last)) {
    if (OpenContent(last)) {
      std::fprintf(stderr, "[disc] the disc image remembered in settings.ini (a document the player picked): %s\n", last.c_str());
      return sChosen.c_str();
    }
    std::fprintf(stderr, "[disc] the remembered document can't be opened any more; asking for the disc image again\n");
    Store("");
  } else
#endif
  if (!last.empty() && fs::is_regular_file(FromUtf8(last), ec)) {
    std::fprintf(stderr, "[disc] the disc image in settings.ini (%s): %s\n", kSetting, last.c_str());
    sChosen = last;
    return sChosen.c_str();
  }
  if (!last.empty()) {
    std::fprintf(stderr, "[disc] the disc image in settings.ini isn't there any more (%s); asking for it\n",
                 last.c_str());
    Store("");
  }

  std::string start;
  if (!last.empty()) {
    const fs::path folder = FromUtf8(last).parent_path();
    if (!folder.empty() && fs::is_directory(folder, ec)) {
      const std::u8string text = folder.u8string();
      start = std::string(text.begin(), text.end());
    }
  }
  char chosen[4096];
  switch (PortDiscDialog_Open(start.empty() ? nullptr : start.c_str(), chosen, sizeof chosen)) {
  case PORT_DISC_DIALOG_CHOSEN:
#ifdef __linux__
    if (chosen[0] == '\0') {
      std::fprintf(stderr, "[disc] the file window gave no file (it may not have opened)\n");
      return nullptr;
    }
#endif
    std::fprintf(stderr, "[disc] chosen: %s\n", chosen);
#ifdef __ANDROID__
    if (IsContentUri(chosen)) {
      if (!OpenContent(chosen)) {
        std::fprintf(stderr, "[disc] the chosen document can't be opened\n");
        return nullptr;
      }
      return sChosen.c_str();
    }
#endif
    sChosen = chosen;
    return sChosen.c_str();
  case PORT_DISC_DIALOG_CANCELED:
    std::fprintf(stderr, "[disc] no disc image chosen\n");
    return "";
  default:
    return nullptr;
  }
}

static std::string sDiscPath;
extern "C" void PortXD_SetDiscPath(const char* path) { sDiscPath = path ? path : ""; }
extern "C" const char* PortXD_DiscPath(void) { return sDiscPath.c_str(); }

extern "C" void PortXD_RememberDisc(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }
#ifdef __ANDROID__
  if (!sChosenUri.empty() && sChosen == path) {
    if (sChosenUri != Remembered()) {
      std::fprintf(stderr, "[disc] remembered for the next start (settings.ini, %s): %s\n", kSetting, sChosenUri.c_str());
    }
    Store(sChosenUri);
    return;
  }
#endif
  std::error_code ec;
  const fs::path full = fs::absolute(FromUtf8(path), ec);
  const std::u8string text = (ec ? FromUtf8(path) : full.lexically_normal()).u8string();
  const std::string utf8(text.begin(), text.end());
  if (utf8 != Remembered()) {
    std::fprintf(stderr, "[disc] remembered for the next start (settings.ini, %s): %s\n", kSetting, utf8.c_str());
  }
  Store(utf8);
}

extern "C" void PortXD_DiscFailed(const char* path) {
#ifdef __ANDROID__
  if (!sChosenUri.empty() && path != nullptr && sChosen == path && Remembered() == sChosenUri) {
    std::fprintf(stderr, "[disc] forgetting the disc image in settings.ini, so the next start asks again\n");
    Store("");
    return;
  }
#endif
  if (path != nullptr && path[0] != '\0' && Remembered() == path) {
    std::fprintf(stderr, "[disc] forgetting the disc image in settings.ini, so the next start asks again\n");
    Store("");
  }
}
