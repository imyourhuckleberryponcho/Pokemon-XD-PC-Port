#include "../src/report_scrub.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int sFails = 0;
void Check(bool ok, const std::string& what) {
  if (!ok) {
    sFails++;
    std::printf("FAIL %s\n", what.c_str());
  }
}
void Eq(const std::string& got, const std::string& want, const std::string& what) {
  if (got != want) {
    sFails++;
    std::printf("FAIL %s\n  got:  %s\n  want: %s\n", what.c_str(), got.c_str(), want.c_str());
  }
}
std::string Read(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
void Write(const std::filesystem::path& p, const std::string& t) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << t;
}
}

int main() {
  using xd::report::Home;
  using xd::report::Scrub;
  Home win;
  win.windows = true;
  win.folders = {"C:\\Users\\sample"};
  win.userName = "sample";
  Eq(Scrub("disc C:\\Users\\sample\\OneDrive\\Desktop\\xd.iso", win), "disc %USERPROFILE%\\OneDrive\\Desktop\\xd.iso", "OneDrive disc");
  Eq(Scrub("[user folder] C:\\Users\\sample\\Downloads\\Pokemon XD PC 0.7.1\\portable (portable)", win),
     "[user folder] %USERPROFILE%\\Downloads\\Pokemon XD PC 0.7.1\\portable (portable)", "portable install");
  Eq(Scrub("c:/users/SAMPLE/Downloads/x", win), "%USERPROFILE%/Downloads/x", "lower case, forward slashes");
  Eq(Scrub("{\"p\": \"C:\\\\Users\\\\sample\\\\x\"}", win), "{\"p\": \"%USERPROFILE%\\\\x\"}", "JSON doubled");
  Eq(Scrub("XD_REPLAY=C:\\Users\\sample\\Downloads\\a b\\recording.txt xd.exe", win),
     "XD_REPLAY=%USERPROFILE%\\Downloads\\a b\\recording.txt xd.exe", "replay line");
  Eq(Scrub("D:\\Users\\Other Person\\Games\\xd.iso", win), "%USERPROFILE%\\Games\\xd.iso", "another account, name with a space");
  Eq(Scrub("C:\\Users\\SAMPLE~1\\x", win), "%USERPROFILE%\\x", "8.3 short name");
  Home other;
  other.windows = true;
  other.folders = {"C:\\Users\\sam"};
  other.userName = "sam";
  Eq(Scrub("C:\\Users\\samplesmith\\x", other), "%USERPROFILE%\\x", "another name that starts the same: hidden by the Users rule, not cut in two");
  Eq(Scrub("E:\\roms\\sample\\x", win), "E:\\roms\\%USERNAME%\\x", "the bare name as a path component");
  Eq(Scrub("sample is not a path", win), "sample is not a path", "a bare word is left alone");
  Eq(Scrub("the Users folder", win), "the Users folder", "no path: unchanged");
  Home lin;
  lin.folders = {"/home/sam"};
  lin.userName = "sam";
  Eq(Scrub("disc /home/sam/Games/xd.iso and /home/sam", lin), "disc ~/Games/xd.iso and ~", "Linux home");
  Eq(Scrub("/home/other/x /Users/mac/y", lin), "~/x ~/y", "other homes");
  Home mac;
  mac.folders = {"/Users/sam"};
  mac.userName = "sam";
  Eq(Scrub("/Users/sam/Library/Application Support/Pokemon XD", mac), "~/Library/Application Support/Pokemon XD", "Mac home");

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "report_scrub_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
#if defined(_WIN32)
  _putenv_s("USERPROFILE", "C:\\Users\\sample");
  _putenv_s("USERNAME", "sample");
  const std::string home = "C:\\Users\\sample";
#else
  setenv("HOME", "/home/sample", 1);
  setenv("USER", "sample", 1);
  const std::string home = "/home/sample";
#endif
  const std::string sep = home[0] == '/' ? "/" : "\\";
  const std::string portable = home + sep + "Downloads" + sep + "Pokemon XD PC 0.7.1" + sep + "portable";
  Write(dir / "info.txt", "disc " + home + sep + "OneDrive" + sep + "Desktop" + sep + "xd.iso\r\nno folder " + home + sep + "Downloads\r\nset XD_REPLAY=" +
                              portable + sep + "reports" + sep + "r" + sep + "recording.txt\r\n");
  Write(dir / "log.txt", "[user folder] " + portable + "\r\n[settings] read " + portable + sep + "settings.ini\r\n[gen4] pack " + portable + sep + "extra_assets\r\n");
  Write(dir / "recording.txt", "# disc " + home + sep + "OneDrive" + sep + "xd.iso\n");
  Write(dir / "settings.ini", "disc_path = " + home + sep + "Desktop" + sep + "xd.iso\n");
  Write(dir / "mods.ini", "profile = default\n");
  xd::report::ScrubReportFolder(dir);
  for (const char* name : {"info.txt", "log.txt", "recording.txt", "settings.ini", "mods.ini"}) {
    Check(Read(dir / name).find("sample") == std::string::npos, std::string(name) + " holds no user name");
  }
  Check(Read(dir / "log.txt").find("Pokemon XD PC 0.7.1") != std::string::npos && Read(dir / "log.txt").find("settings.ini") != std::string::npos,
        "the paths after the home folder stay");
  Check(Read(dir / "info.txt").find("OneDrive") != std::string::npos, "info.txt keeps the folder names after the home folder");
  fs::remove_all(dir, ec);
  std::printf(sFails == 0 ? "ALL PASS\n" : "%d FAILED\n", sFails);
  return sFails == 0 ? 0 : 1;
}
