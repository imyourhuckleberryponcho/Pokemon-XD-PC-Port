#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::wstring OwnFolder() {
  std::wstring path(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  path.resize(n);
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

bool IsFile(const std::wstring& path) {
  const DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring Arguments() {
  const wchar_t* p = GetCommandLineW();
  bool quoted = false;
  while (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
    if (*p == L'"') {
      quoted = !quoted;
    }
    p++;
  }
  while (*p == L' ' || *p == L'\t') {
    p++;
  }
  return p;
}

bool IsGameFolderName(const std::string& name) {
  if (name == "game") {
    return true;
  }
  if (name.size() <= 5 || name.compare(0, 5, "game-") != 0) {
    return false;
  }
  int parts = 0;
  bool digit = false;
  for (size_t i = 5; i <= name.size(); i++) {
    if (i == name.size() || name[i] == '.') {
      if (!digit) {
        return false;
      }
      parts++;
      digit = false;
    } else if (name[i] >= '0' && name[i] <= '9') {
      digit = true;
    } else {
      return false;
    }
  }
  return parts <= 4;
}

std::vector<std::string> PointedFolders(const std::wstring& own) {
  std::vector<std::string> out;
  std::FILE* f = _wfopen((own + L"\\game_folder.txt").c_str(), L"rb");
  if (f == nullptr) {
    return out;
  }
  std::string current, previous;
  char line[512];
  while (std::fgets(line, sizeof line, f) != nullptr) {
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
      text.pop_back();
    }
    const size_t eq = text.find('=');
    if (eq == std::string::npos || text[0] == '#' || !IsGameFolderName(text.substr(eq + 1))) {
      continue;
    }
    const std::string key = text.substr(0, eq), value = text.substr(eq + 1);
    if (key == "current") {
      current = value;
    } else if (key == "previous") {
      previous = value;
    }
  }
  std::fclose(f);
  if (!current.empty()) {
    out.push_back(current);
  }
  if (!previous.empty()) {
    out.push_back(previous);
  }
  return out;
}

std::wstring Widen(const std::string& ascii) { return std::wstring(ascii.begin(), ascii.end()); }

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const std::wstring own = OwnFolder();
  std::vector<std::wstring> folders;
  for (const std::string& name : PointedFolders(own)) {
    folders.push_back(own + L"\\" + Widen(name));
  }
  folders.push_back(own + L"\\game");
  folders.push_back(own);
  const char* waitVar = std::getenv("XD_STARTER_WAIT");
  const bool wait = waitVar != nullptr && waitVar[0] == '1';
  const std::wstring args = Arguments();
  DWORD lastError = 0;
  std::wstring lastProgram;
  for (const std::wstring& folder : folders) {
    const std::wstring program = folder + L"\\xd_launcher.exe";
    if (!IsFile(program)) {
      continue;
    }
    std::wstring command = L"\"" + program + L"\"";
    if (!args.empty()) {
      command += L" " + args;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (wait) {
      AttachConsole(ATTACH_PARENT_PROCESS);
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
      si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, wait ? TRUE : FALSE, 0, nullptr,
                        folder.c_str(), &si, &pi)) {
      lastError = GetLastError();
      lastProgram = program;
      continue;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    DWORD code = 0;
    if (wait) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
  }
  if (!lastProgram.empty()) {
    MessageBoxW(nullptr, (L"The launcher couldn't be started (Windows error " + std::to_wstring(lastError) +
                          L"):\n" + lastProgram)
                             .c_str(),
                L"Pokemon XD", MB_OK | MB_ICONERROR);
  } else {
    MessageBoxW(nullptr,
                (L"The launcher is missing: " + own +
                 L"\\game\\xd_launcher.exe\n\nPlease unpack the whole folder again, keeping the \"game\" folder "
                 L"next to this program.")
                    .c_str(),
                L"Pokemon XD", MB_OK | MB_ICONERROR);
  }
  return 1;
}
