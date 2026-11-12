


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <port_user_folder.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static fs::path RealAppData() {
  PWSTR known = nullptr;
  fs::path real;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &known))) {
    real = known;
  }
  CoTaskMemFree(known);
  return real;
}

static fs::path FromUtf8(const char* text) { return fs::path(std::u8string(text, text + std::strlen(text))); }

static bool Same(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  return fs::equivalent(a, b, ec);
}


static void CheckUtf8(const char* app, const fs::path& folder) {
  const char* utf8 = PortUser_FolderUtf8(app);
  CHECK(utf8 != nullptr && utf8[0] != '\0');
  const size_t n = std::strlen(utf8);
  CHECK(n > 0 && utf8[n - 1] != '\\' && utf8[n - 1] != '/');
  CHECK(Same(FromUtf8(utf8), folder));
  CHECK(PortUser_FolderUtf8(app) == utf8);
}

int main() {
  const fs::path real = RealAppData();
  CHECK(!real.empty());
  std::error_code ec;





  const fs::path temp = fs::temp_directory_path() / L"xd_user_folder_test";
  fs::remove_all(temp, ec);
  fs::create_directories(temp);
  _wputenv_s(L"APPDATA", temp.c_str());
  const fs::path a = PortUser_Folder("XD Folder Test A");
  CHECK(Same(a, temp / L"XD Folder Test A"));
  CHECK(fs::is_directory(a, ec));
  CheckUtf8("XD Folder Test A", a);
  CHECK(PortUser_Folder("XD Folder Test A") == a);



  const char* umlaut = "XD Ordner Pr\xC3\xBC" "fung";
  const std::wstring umlautWide = std::wstring(L"XD Ordner Pr") + static_cast<wchar_t>(0x00FC) + L"fung";
  const fs::path u = PortUser_Folder(umlaut);
  CHECK(Same(u, temp / umlautWide));
  CHECK(u.filename().wstring() == umlautWide);
  CHECK(fs::is_directory(u, ec));
  CheckUtf8(umlaut, u);


  _wputenv_s(L"APPDATA", real.c_str());
  const fs::path b = PortUser_Folder("XD Folder Test B");
  CHECK(std::strcmp(PortUser_FolderNote("XD Folder Test B"), "default") == 0);
  CHECK(Same(b, real / L"XD Folder Test B"));
  CHECK(fs::is_directory(b, ec));
  CheckUtf8("XD Folder Test B", b);


  _wputenv_s(L"APPDATA", L"");
  const fs::path c = PortUser_Folder("XD Folder Test C");
  CHECK(Same(c, real / L"XD Folder Test C"));
  CheckUtf8("XD Folder Test C", c);

  fs::remove_all(temp, ec);
  fs::remove_all(real / L"XD Folder Test B", ec);
  fs::remove_all(real / L"XD Folder Test C", ec);
  _wputenv_s(L"APPDATA", real.c_str());

  std::printf("user_folder_test: %s\n", sFailures == 0 ? "all passed" : "FAILED");
  return sFailures == 0 ? 0 : 1;
}
