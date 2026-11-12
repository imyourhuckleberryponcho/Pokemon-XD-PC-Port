

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <port_folder_claim.h>

#include <cstdio>
#include <cstring>
#include <string>

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)


static DWORD RunChild(const std::string& folder) {
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string cmd = std::string("\"") + exe + "\" child \"" + folder + "\"";
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 99;
  WaitForSingleObject(pi.hProcess, 10000);
  DWORD code = 99;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code;
}

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "child") == 0) {
    return PortFolderClaim_Take(argv[2]) ? 0 : 1;
  }
  char temp[MAX_PATH];
  GetTempPathA(MAX_PATH, temp);
  char unique[64];
  std::snprintf(unique, sizeof unique, "folder_claim_test_%lu", GetCurrentProcessId());
  const std::string a = std::string(temp) + unique + "\\Card";
  const std::string b = std::string(temp) + unique + "\\Other";

  CHECK(RunChild(a) == 0);
  CHECK(RunChild(a) == 0);
  CHECK(PortFolderClaim_Take(a.c_str()));
  CHECK(RunChild(a) == 1);
  std::string other = a + "\\";
  for (char& c : other) {
    if (c == '\\') c = '/';
    else c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  CHECK(RunChild(other) == 1);
  CHECK(RunChild(b) == 0);
  CHECK(!PortFolderClaim_Take(a.c_str()));
  CHECK(PortFolderClaim_Take(nullptr) && PortFolderClaim_Take(""));

  if (sFailures == 0) std::printf("folder_claim_test: all passed\n");
  return sFailures == 0 ? 0 : 1;
}
