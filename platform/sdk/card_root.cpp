#include <aurora/card.h>
#include <dolphin/card.h>

#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::string sRoot;
std::filesystem::path sRootFolder;

#ifdef _WIN32
bool Narrow(const std::wstring& wide, std::string& out) {
  const UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
  const bool utf8 = cp == CP_ACP && GetACP() == CP_UTF8;
  BOOL lossy = FALSE;
  const int n = WideCharToMultiByte(cp, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, nullptr, 0, nullptr,
                                    utf8 ? nullptr : &lossy);
  if (n <= 0 || lossy) return false;
  out.assign(static_cast<size_t>(n), ' ');
  WideCharToMultiByte(cp, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, out.data(), n, nullptr,
                      utf8 ? nullptr : &lossy);
  out.resize(static_cast<size_t>(n) - 1);
  return !lossy;
}
#endif
}

extern "C" {

bool PortCARD_SetRoot(const char* utf8Folder) {
  if (utf8Folder == nullptr || utf8Folder[0] == 0) return false;
  const std::filesystem::path folder = std::filesystem::u8path(utf8Folder);
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (!std::filesystem::is_directory(folder, ec)) return false;
#ifdef _WIN32
  std::string text;
  if (!Narrow(folder.wstring(), text)) {
    wchar_t shortName[1024];
    const DWORD n = GetShortPathNameW(folder.c_str(), shortName, 1024);
    if (n == 0 || n >= 1024 || !Narrow(shortName, text)) return false;
  }
  sRoot = text;
#else
  sRoot = utf8Folder;
#endif
  sRootFolder = folder;
  return true;
}

void PortCARD_ApplyRoot(void) {
  static bool applied = false;
  if (!sRoot.empty() && !applied) {
    CARDSetBasePath(sRoot.c_str(), -1);
  }
  applied = true;
}

bool PortCARD_RootMounted(const char* game) {
  if (sRootFolder.empty()) return true;
  char path[1024] = {};
  std::error_code ec;
  if (aurora_card_get_path(game, AURORA_CARD_GCI_DIRECTORY, 0, path, sizeof path) != 0 && path[0] != 0) {
    const std::filesystem::path card = std::filesystem::u8path(path);
    const std::filesystem::path got = std::filesystem::weakly_canonical(card.parent_path().parent_path(), ec);
    const std::filesystem::path want = std::filesystem::weakly_canonical(sRootFolder, ec);
    if (!got.empty() && got == want) return true;
  }
  std::fprintf(stderr, "[card] the memory card is NOT in this save's folder (%s); Aurora has \"%s\"\n",
               sRoot.c_str(), path);
  return false;
}

}
