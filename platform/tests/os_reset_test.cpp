#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
void OSResetSystem(int reset, uint32_t resetCode, int forceMenu);
uint32_t OSGetResetCode(void);
}

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "restart") {
    if (std::getenv("PORT_RESET_CODE") == nullptr) {
      OSResetSystem(0, 0x12, 0);
      return 3;
    }
    FILE* f = std::fopen(argv[2], "w");
    if (f != nullptr) {
      std::fprintf(f, "%08X", OSGetResetCode());
      std::fclose(f);
    }
    return 0;
  }
  bool ok = true;
  auto check = [&](bool cond, const char* what) {
    std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
    ok &= cond;
  };
  check(OSGetResetCode() == 0, "switched on: reset code 0");

  char tmp[MAX_PATH];
  GetTempPathA(MAX_PATH, tmp);
  const std::string out = std::string(tmp) + "os_reset_test.txt";
  DeleteFileA(out.c_str());
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string cmd = std::string("\"") + exe + "\" restart \"" + out + "\"";
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  check(CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) != 0,
        "the first copy starts");
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::string got;
  for (int i = 0; i < 100 && got.empty(); i++) {
    Sleep(100);
    if (FILE* f = std::fopen(out.c_str(), "r")) {
      char buf[16] = {};
      if (std::fgets(buf, sizeof(buf), f) != nullptr) got = buf;
      std::fclose(f);
    }
  }
  check(got == "80000012", ("after a restart with code 0x12 the new copy reads " + got).c_str());
  DeleteFileA(out.c_str());
  return ok ? 0 : 1;
}
