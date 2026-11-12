#include "port_log.h"
#include "port_user_folder.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <process.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr DWORD kPipeSize = 1 << 20;
constexpr long long kMaxLogBytes = 256LL << 20;
constexpr DWORD kFlushWaitMs = 1000;

struct Stream {
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  HANDLE original = nullptr;
  volatile LONG sent = 0;
  volatile LONG seen = 0;
};

Stream sStreams[2];
bool sStarted = false;
CRITICAL_SECTION sFileLock;
HANDLE sFile = INVALID_HANDLE_VALUE;
long long sWritten = 0;
std::string* sPath = new std::string();

void WriteAll(HANDLE h, const char* data, DWORD size) {
  while (size > 0) {
    DWORD done = 0;
    if (!WriteFile(h, data, size, &done, nullptr) || done == 0) {
      return;
    }
    data += done;
    size -= done;
  }
}

void AppendToLog(const char* data, DWORD size) {
  EnterCriticalSection(&sFileLock);
  if (sWritten < kMaxLogBytes) {
    WriteAll(sFile, data, size);
    sWritten += size;
    if (sWritten >= kMaxLogBytes) {
      static const char kFull[] = "\r\n(log too long: nothing more is written to this file)\r\n";
      WriteAll(sFile, kFull, sizeof kFull - 1);
    }
  }
  LeaveCriticalSection(&sFileLock);
}

DWORD WINAPI Pump(void* param) {
  Stream& s = *static_cast<Stream*>(param);
  static thread_local char buffer[64 * 1024];
  for (;;) {
    DWORD n = 0;
    if (!ReadFile(s.read, buffer, sizeof buffer, &n, nullptr)) {
      return 0;
    }
    DWORD kept = 0;
    LONG markers = 0;
    for (DWORD i = 0; i < n; i++) {
      if (buffer[i] == '\0') {
        markers++;
      } else {
        buffer[kept++] = buffer[i];
      }
    }
    if (kept > 0) {
      if (s.original != nullptr) {
        WriteAll(s.original, buffer, kept);
      }
      AppendToLog(buffer, kept);
    }
    if (markers > 0) {
      InterlockedAdd(&s.seen, markers);
    }
  }
}

void Prune(const fs::path& folder, const std::wstring& prefix, size_t keep) {
  std::error_code ec;
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const std::wstring name = entry.path().filename().wstring();
    if (entry.is_regular_file(ec) && name.rfind(prefix, 0) == 0 && entry.path().extension() == L".log") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  for (size_t i = 0; i + keep < files.size(); i++) {
    fs::remove(files[i], ec);
  }
}

bool Redirect(int fd, Stream& s) {
  HANDLE read = nullptr, write = nullptr;
  if (!CreatePipe(&read, &write, nullptr, kPipeSize)) {
    return false;
  }
  const intptr_t before = _get_osfhandle(fd);
  HANDLE original = nullptr;
  if (before != -1 && before != -2 &&
      !DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(before), GetCurrentProcess(), &original, 0,
                       FALSE, DUPLICATE_SAME_ACCESS)) {
    original = nullptr;
  }
  int mode = _setmode(fd, _O_BINARY);
  if (mode == -1) {
    mode = _O_TEXT;
  } else {
    _setmode(fd, mode);
  }
  const int pipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(write), _O_TEXT);
  if (pipeFd == -1) {
    CloseHandle(read);
    CloseHandle(write);
    if (original != nullptr) CloseHandle(original);
    return false;
  }
  _setmode(pipeFd, mode);
  if (_dup2(pipeFd, fd) != 0) {
    _close(pipeFd);
    CloseHandle(read);
    if (original != nullptr) CloseHandle(original);
    return false;
  }
  _close(pipeFd);
  s.read = read;
  s.write = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  s.original = original;
  SetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, s.write);
  CloseHandle(CreateThread(nullptr, 0, Pump, &s, 0, nullptr));
  return true;
}

bool SameTarget() {
  const intptr_t a = _get_osfhandle(1), b = _get_osfhandle(2);
  if (a == -1 || a == -2 || b == -1 || b == -2) {
    return false;
  }
  HANDLE ha = reinterpret_cast<HANDLE>(a), hb = reinterpret_cast<HANDLE>(b);
  using Compare = BOOL(WINAPI*)(HANDLE, HANDLE);
  static const Compare compare = reinterpret_cast<Compare>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles")));
  if (ha == hb || (compare != nullptr && compare(ha, hb))) {
    return true;
  }
  DWORD mode = 0;
  if (GetConsoleMode(ha, &mode) && GetConsoleMode(hb, &mode)) {
    return true;
  }
  BY_HANDLE_FILE_INFORMATION ia{}, ib{};
  return GetFileType(ha) == FILE_TYPE_DISK && GetFileType(hb) == FILE_TYPE_DISK && GetFileInformationByHandle(ha, &ia) &&
         GetFileInformationByHandle(hb, &ib) && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
         ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
}

std::string Utf8(const fs::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}

extern "C" bool PortLog_Start(const char* appFolder, const char* kind, size_t keep) {
  if (sStarted) {
    return true;
  }
  const fs::path folder = PortUser_Folder(appFolder) / L"logs";
  std::error_code ec;
  fs::create_directories(folder, ec);
  const std::string prefixUtf8 = std::string(kind) + "_";
  const std::wstring prefix(prefixUtf8.begin(), prefixUtf8.end());
  Prune(folder, prefix, keep > 0 ? keep - 1 : 0);

  const std::time_t now = std::time(nullptr);
  std::tm local;
  localtime_s(&local, &now);
  wchar_t stamp[32];
  wcsftime(stamp, 32, L"%Y-%m-%d_%H-%M-%S", &local);
  const fs::path path = folder / (prefix + stamp + L"_p" + std::to_wstring(_getpid()) + L".log");
  sFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                      CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (sFile == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "[log] could not create %s; this run has no log file\n", Utf8(path).c_str());
    return false;
  }
  InitializeCriticalSection(&sFileLock);
  std::fflush(stdout);
  std::fflush(stderr);
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  bool redirected = false;
  if (SameTarget()) {
    redirected = Redirect(1, sStreams[0]) && _dup2(1, 2) == 0;
    if (redirected) {
      SetStdHandle(STD_ERROR_HANDLE, sStreams[0].write);
    }
  } else {
    redirected = Redirect(1, sStreams[0]) && Redirect(2, sStreams[1]);
  }
  if (!redirected) {
    std::fprintf(stderr, "[log] could not redirect the program's output; the log stays incomplete\n");
  }
  *sPath = Utf8(path);
  sStarted = true;
  std::atexit(PortLog_Flush);
  return true;
}

extern "C" void PortLog_Flush(void) {
  if (!sStarted) {
    return;
  }
  LONG target[2] = {0, 0};
  for (int i = 0; i < 2; i++) {
    Stream& s = sStreams[i];
    if (s.write != nullptr) {
      target[i] = InterlockedIncrement(&s.sent);
      DWORD done = 0;
      WriteFile(s.write, "", 1, &done, nullptr);
    }
  }
  const ULONGLONG deadline = GetTickCount64() + kFlushWaitMs;
  for (int i = 0; i < 2; i++) {
    while (sStreams[i].write != nullptr && sStreams[i].seen < target[i] && GetTickCount64() < deadline) {
      Sleep(1);
    }
  }
  EnterCriticalSection(&sFileLock);
  FlushFileBuffers(sFile);
  LeaveCriticalSection(&sFileLock);
}

extern "C" const char* PortLog_Path(void) { return sPath->c_str(); }

extern "C" bool PortLog_CopyTail(const wchar_t* dest, size_t maxBytes) {
  if (!sStarted) {
    return false;
  }
  PortLog_Flush();
  const fs::path path = fs::u8path(*sPath);
  HANDLE in = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (in == INVALID_HANDLE_VALUE) {
    return false;
  }
  LARGE_INTEGER size{};
  GetFileSizeEx(in, &size);
  const long long start = size.QuadPart > static_cast<long long>(maxBytes) ? size.QuadPart - maxBytes : 0;
  std::string text(static_cast<size_t>(size.QuadPart - start), '\0');
  LARGE_INTEGER at{};
  at.QuadPart = start;
  SetFilePointerEx(in, at, nullptr, FILE_BEGIN);
  DWORD got = 0;
  const bool read = text.empty() || ReadFile(in, text.data(), static_cast<DWORD>(text.size()), &got, nullptr);
  CloseHandle(in);
  if (!read) {
    return false;
  }
  text.resize(got);
  std::string head;
  if (start > 0) {
    const size_t newline = text.find('\n');
    text.erase(0, newline == std::string::npos ? 0 : newline + 1);
    head = "(the start of this log is left out: the whole log is " + *sPath + ")\r\n";
  }
  HANDLE out = CreateFileW(dest, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) {
    return false;
  }
  WriteAll(out, head.data(), static_cast<DWORD>(head.size()));
  WriteAll(out, text.data(), static_cast<DWORD>(text.size()));
  CloseHandle(out);
  return true;
}

#else
extern "C" bool PortLog_Start(const char*, const char*, size_t) { return false; }
extern "C" void PortLog_Flush(void) {}
extern "C" const char* PortLog_Path(void) { return ""; }
extern "C" bool PortLog_CopyTail(const wchar_t*, size_t) { return false; }
#endif
