#include "port_folder_claim.h"

#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <windows.h>

namespace {

std::wstring widen(const char* utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
  w.resize(static_cast<size_t>(n - 1));
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring canonical(const char* folder) {
  std::wstring path = widen(folder);
  if (path.empty()) return {};
  wchar_t full[4096];
  const DWORD len = GetFullPathNameW(path.c_str(), 4096, full, nullptr);
  if (len > 0 && len < 4096) path.assign(full, len);
  for (wchar_t& c : path) {
    if (c == L'/') c = L'\\';
  }
  while (path.size() > 3 && path.back() == L'\\') path.pop_back();
  if (!path.empty()) {
    CharUpperBuffW(path.data(), static_cast<DWORD>(path.size()));
  }
  return path;
}

std::wstring nameOf(const std::wstring& path, const wchar_t* prefix) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const wchar_t c : path) {
    hash = (hash ^ static_cast<uint16_t>(c)) * 0x100000001B3ull;
  }
  wchar_t name[96];
  swprintf(name, 96, L"Local\\%ls-%016llx", prefix, static_cast<unsigned long long>(hash));
  return name;
}

struct Info {
  uint32_t magic;
  uint32_t pid;
  FILETIME started;
  wchar_t program[260];
};
constexpr uint32_t kInfoMagic = 0x43464C50;

std::wstring programOf(HANDLE process) {
  wchar_t buffer[1024];
  DWORD size = 1024;
  if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) return {};
  return std::wstring(buffer, size);
}

bool sameFile(const std::wstring& a, const std::wstring& b) {
  return !a.empty() && !b.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void publishInfo(const std::wstring& path) {
  const std::wstring name = nameOf(path, L"PortFolderClaimInfo");
  const HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Info), name.c_str());
  if (map == nullptr) return;
  void* view = MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, sizeof(Info));
  if (view == nullptr) return;
  Info* info = static_cast<Info*>(view);
  FILETIME created{}, x{}, y{}, z{};
  GetProcessTimes(GetCurrentProcess(), &created, &x, &y, &z);
  info->pid = GetCurrentProcessId();
  info->started = created;
  const std::wstring program = programOf(GetCurrentProcess());
  wcsncpy_s(info->program, program.c_str(), _TRUNCATE);
  info->magic = kInfoMagic;
}

long long unixOf(const FILETIME& t) {
  const unsigned long long ticks = (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  return ticks == 0 ? 0 : static_cast<long long>(ticks / 10000000ull) - 11644473600ll;
}

bool waitForResetParent(const char* folder) {
  wchar_t parent[32];
  const DWORD got = GetEnvironmentVariableW(L"PORT_RESET_PARENT", parent, 32);
  if (got == 0 || got >= 32) return false;
  const DWORD pid = wcstoul(parent, nullptr, 10);
  if (pid == 0 || pid == GetCurrentProcessId()) return false;
  const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
  if (process == nullptr) return true;
  if (WaitForSingleObject(process, 60000) == WAIT_TIMEOUT) {
    FILETIME theirs{}, mine{}, x{}, y{}, z{};
    GetProcessTimes(process, &theirs, &x, &y, &z);
    GetProcessTimes(GetCurrentProcess(), &mine, &x, &y, &z);
    PortFolderHolder holder;
    const bool holds = PortFolderClaim_Holder(folder, &holder) && holder.pid == pid;
    if (holds && CompareFileTime(&theirs, &mine) < 0 && sameFile(programOf(process), programOf(GetCurrentProcess()))) {
      std::fprintf(stderr, "[card] the copy that started this one (process %lu) is still closing after 60 s: ending it\n", pid);
      TerminateProcess(process, 3);
      WaitForSingleObject(process, 10000);
    }
  }
  CloseHandle(process);
  return true;
}

}

extern "C" bool PortFolderClaim_Take(const char* folder) {
  if (folder == nullptr || folder[0] == '\0') return true;
  const std::wstring path = canonical(folder);
  if (path.empty()) return true;
  const std::wstring name = nameOf(path, L"PortFolderClaim");
  HANDLE lock = CreateMutexW(nullptr, FALSE, name.c_str());
  if (lock == nullptr) return true;
  if (GetLastError() != ERROR_ALREADY_EXISTS) {
    publishInfo(path);
    return true;
  }
  CloseHandle(lock);
  if (!waitForResetParent(folder)) return false;
  lock = CreateMutexW(nullptr, FALSE, name.c_str());
  if (lock == nullptr) return true;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(lock);
    return false;
  }
  publishInfo(path);
  return true;
}

extern "C" bool PortFolderClaim_IsFree(const char* folder) {
  if (folder == nullptr || folder[0] == '\0') return true;
  const std::wstring path = canonical(folder);
  if (path.empty()) return true;
  const HANDLE lock = OpenMutexW(SYNCHRONIZE, FALSE, nameOf(path, L"PortFolderClaim").c_str());
  if (lock == nullptr) return true;
  CloseHandle(lock);
  return false;
}

extern "C" bool PortFolderClaim_Holder(const char* folder, PortFolderHolder* out) {
  if (out == nullptr) return false;
  *out = PortFolderHolder{};
  if (folder == nullptr || folder[0] == '\0' || PortFolderClaim_IsFree(folder)) return false;
  const std::wstring path = canonical(folder);
  const HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, nameOf(path, L"PortFolderClaimInfo").c_str());
  if (map == nullptr) return true;
  const void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(Info));
  if (view != nullptr) {
    Info info = *static_cast<const Info*>(view);
    UnmapViewOfFile(view);
    if (info.magic == kInfoMagic) {
      info.program[259] = L'\0';
      out->pid = info.pid;
      out->started = unixOf(info.started);
      std::snprintf(out->program, sizeof out->program, "%s", narrow(info.program).c_str());
      if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.pid)) {
        FILETIME created{}, x{}, y{}, z{};
        DWORD code = 0;
        if (GetProcessTimes(process, &created, &x, &y, &z) && GetExitCodeProcess(process, &code) && code == STILL_ACTIVE &&
            CompareFileTime(&created, &info.started) == 0) {
          out->alive = true;
          out->ours = sameFile(programOf(process), programOf(GetCurrentProcess()));
        }
        CloseHandle(process);
      }
    }
  }
  CloseHandle(map);
  return true;
}

extern "C" bool PortFolderClaim_EndHolder(const PortFolderHolder* holder) {
  if (holder == nullptr || holder->pid == 0 || !holder->alive || !holder->ours) return false;
  const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(holder->pid));
  if (process == nullptr) return false;
  TerminateProcess(process, 3);
  const bool ended = WaitForSingleObject(process, 10000) == WAIT_OBJECT_0;
  CloseHandle(process);
  return ended;
}
