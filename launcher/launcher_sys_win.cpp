#include "launcher_sys.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdint>
#include <cwchar>

namespace launcher::sys {

namespace fs = std::filesystem;

namespace {

HRESULT sCom = E_FAIL;

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::wstring Wide(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

}

void Start() {
  sCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
}

void End() {
  if (SUCCEEDED(sCom)) {
    CoUninitialize();
  }
  sCom = E_FAIL;
}

fs::path ProgramFile() {
  wchar_t self[MAX_PATH * 4];
  return fs::path(std::wstring(self, GetModuleFileNameW(nullptr, self, MAX_PATH * 4)));
}

fs::path ProgramFolder() { return ProgramFile().parent_path(); }

std::FILE* OpenFile(const fs::path& file, const char* mode) {
  return _wfopen(file.c_str(), Wide(mode).c_str());
}

fs::path EnvPath(const char* name) {
  const wchar_t* value = _wgetenv(Wide(name).c_str());
  return value != nullptr && value[0] != L'\0' ? fs::path(value) : fs::path();
}

std::string ProgramName(const std::string& stem) { return stem + ".exe"; }

std::vector<FontSet> SystemFonts() {
  const wchar_t* windir = _wgetenv(L"WINDIR");
  const fs::path windows = fs::path(windir != nullptr ? windir : L"C:\\Windows") / L"Fonts";
  return {{windows / L"segoeui.ttf", windows / L"seguisb.ttf", windows / L"segoeuib.ttf"}};
}

struct Process {
  HANDLE handle = nullptr;
  DWORD id = 0;
  HANDLE job = nullptr;
};

Process* StartProgram(const StartRequest& request, std::string& error) {
  const std::wstring program = request.program.wstring();
  std::wstring command = L"\"" + program + L"\"";
  for (const std::string& a : request.arguments) {
    command += L" \"" + Wide(a) + L"\"";
  }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  BOOL inherit = FALSE;
  if (request.shareOutput) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    const bool redirect = out != nullptr && out != INVALID_HANDLE_VALUE && err != nullptr &&
                          err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN;
    if (redirect) {
      SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      SetHandleInformation(err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = out;
      si.hStdError = err;
      si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      inherit = TRUE;
    }
  }
  HANDLE outFile = INVALID_HANDLE_VALUE;
  if (!request.outputFile.empty()) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    outFile = CreateFileW(request.outputFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (outFile == INVALID_HANDLE_VALUE) {
      error = "Windows error " + std::to_string(GetLastError()) + " (its output file)";
      return nullptr;
    }
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outFile;
    si.hStdError = outFile;
    si.hStdInput = nullptr;
    inherit = TRUE;
  }
  std::wstring env;
  DWORD flags = 0;
  if (request.ownEnvironment) {
    if (wchar_t* block = GetEnvironmentStringsW()) {
      for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
        bool keep = true;
        for (const std::string& without : request.environmentWithout) {
          const std::wstring w = Wide(without);
          if (_wcsnicmp(p, w.c_str(), w.size()) == 0) {
            keep = false;
          }
        }
        if (keep) {
          env.append(p).push_back(L'\0');
        }
      }
      FreeEnvironmentStringsW(block);
    }
    for (const std::string& e : request.environmentAdd) {
      env.append(Wide(e)).push_back(L'\0');
    }
    env.push_back(L'\0');
    flags |= CREATE_UNICODE_ENVIRONMENT;
  }
  if (request.hidden) {
    flags |= CREATE_NO_WINDOW;
  }
  if (request.ownEnvironment || request.hidden || request.lowPriority) {
    flags |= request.lowPriority ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
  }
  HANDLE job = nullptr;
  if (request.killTree) {
    job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
      flags |= CREATE_SUSPENDED;
    }
  }
  PROCESS_INFORMATION pi{};
  const std::wstring folder = request.folder.wstring();
  const BOOL started = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, inherit, flags,
                                      request.ownEnvironment ? env.data() : nullptr,
                                      folder.empty() ? nullptr : folder.c_str(), &si, &pi);
  const DWORD startError = GetLastError();
  if (outFile != INVALID_HANDLE_VALUE) {
    CloseHandle(outFile);
  }
  if (!started) {
    if (job != nullptr) CloseHandle(job);
    error = "Windows error " + std::to_string(startError);
    return nullptr;
  }
  if (job != nullptr) {
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
      CloseHandle(job);
      job = nullptr;
    }
    ResumeThread(pi.hThread);
  }
  if (request.toFront) {
    AllowSetForegroundWindow(pi.dwProcessId);
  }
  CloseHandle(pi.hThread);
  Process* p = new Process;
  p->handle = pi.hProcess;
  p->id = pi.dwProcessId;
  p->job = job;
  return p;
}

bool Ended(Process* process, int waitMs) {
  return process == nullptr || WaitForSingleObject(process->handle, static_cast<DWORD>(waitMs)) != WAIT_TIMEOUT;
}

int ExitCode(Process* process) {
  DWORD code = 1;
  if (process != nullptr) {
    GetExitCodeProcess(process->handle, &code);
  }
  return static_cast<int>(code);
}

unsigned long IdOf(Process* process) { return process != nullptr ? process->id : 0; }

void Stop(Process* process) {
  if (process != nullptr) {
    if (process->job != nullptr) {
      TerminateJobObject(process->job, 1);
    }
    TerminateProcess(process->handle, 1);
    WaitForSingleObject(process->handle, 5000);
  }
}

void Close(Process* process) {
  if (process != nullptr) {
    if (process->job != nullptr) {
      CloseHandle(process->job);
    }
    CloseHandle(process->handle);
    delete process;
  }
}

bool OpenInSystem(const fs::path& what) {
  return reinterpret_cast<INT_PTR>(
             ShellExecuteW(nullptr, L"open", what.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

fs::path KnownFolder(Folder which, bool create) {
  const KNOWNFOLDERID& id =
      which == Folder::Documents ? FOLDERID_Documents : which == Folder::Desktop ? FOLDERID_Desktop : FOLDERID_Programs;
  fs::path folder;
  PWSTR known = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(id, create ? KF_FLAG_CREATE : 0, nullptr, &known))) {
    folder = known;
  }
  CoTaskMemFree(known);
  return folder;
}

bool ToRecycleBin(const fs::path& folder, std::string& error) {
  std::error_code ec;
  std::wstring from = folder.wstring();
  from.push_back(L'\0');
  from.push_back(L'\0');
  SHFILEOPSTRUCTW op{};
  op.hwnd = nullptr;
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_WANTNUKEWARNING;
  const int result = SHFileOperationW(&op);
  if (result != 0 || op.fAnyOperationsAborted || fs::exists(folder, ec)) {
    error = "Windows couldn't move it to the recycle bin (" + std::to_string(result) + "): " + Utf8(folder);
    return false;
  }
  return true;
}

const char* RecycleBinName() { return "the recycle bin"; }
const char* UserNameWords() { return "Windows user name"; }
const char* HomeFolderExample() { return "C:\\Users\\<you>\\..."; }

bool MakeShortcut(const fs::path& folder, const std::string& name, const std::string& description,
                  const fs::path& target, const fs::path& iconFile, std::string& message) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path link = folder / (Wide(name) + L".lnk");
  IShellLinkW* shell = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell));
  if (SUCCEEDED(hr)) {
    shell->SetPath(target.c_str());
    shell->SetWorkingDirectory(target.parent_path().c_str());
    shell->SetDescription(Wide(description).c_str());
    if (!iconFile.empty() && fs::exists(iconFile, ec)) {
      shell->SetIconLocation(iconFile.c_str(), 0);
    } else {
      shell->SetIconLocation(target.c_str(), 0);
    }
    IPersistFile* file = nullptr;
    hr = shell->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr)) {
      hr = file->Save(link.c_str(), TRUE);
      file->Release();
    }
    shell->Release();
  }
  if (FAILED(hr)) {
    char code[16];
    std::snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(hr));
    message = "Windows didn't let the launcher put a shortcut in " + Utf8(folder) + " (error " + code +
              "). If that folder is kept in OneDrive or protected by Windows Security, make the shortcut by hand: "
              "right-click " + Utf8(target.filename()) + ", \"Show more options\", \"Create shortcut\".";
    return false;
  }
  SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, link.c_str(), nullptr);
  message = "Shortcut made: " + Utf8(link);
  return true;
}

const char* ProgramsFolderName() { return "Start menu"; }
bool ShortcutsHere() { return true; }
bool HasIcons() { return true; }

icon::Image ReadIcoSize(const fs::path& file, int size) {
  icon::Image img;
  HICON h = static_cast<HICON>(LoadImageW(nullptr, file.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE));
  if (h == nullptr) {
    return img;
  }
  ICONINFO info{};
  if (GetIconInfo(h, &info) && info.hbmColor != nullptr) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> bgra(static_cast<size_t>(size) * size * 4);
    HDC dc = GetDC(nullptr);
    if (GetDIBits(dc, info.hbmColor, 0, size, bgra.data(), &bmi, DIB_RGB_COLORS) == size) {
      img.width = img.height = size;
      img.rgba.resize(bgra.size());
      for (size_t i = 0; i < bgra.size(); i += 4) {
        img.rgba[i] = bgra[i + 2];
        img.rgba[i + 1] = bgra[i + 1];
        img.rgba[i + 2] = bgra[i];
        img.rgba[i + 3] = bgra[i + 3];
      }
    }
    ReleaseDC(nullptr, dc);
  }
  if (info.hbmColor != nullptr) {
    DeleteObject(info.hbmColor);
  }
  if (info.hbmMask != nullptr) {
    DeleteObject(info.hbmMask);
  }
  DestroyIcon(h);
  return img;
}

}
