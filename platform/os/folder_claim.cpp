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

}

extern "C" bool PortFolderClaim_Take(const char* folder) {
  if (folder == nullptr || folder[0] == '\0') return true;
  const std::wstring path = canonical(folder);
  if (path.empty()) return true;
  uint64_t hash = 0xCBF29CE484222325ull;
  for (const wchar_t c : path) {
    hash = (hash ^ static_cast<uint16_t>(c)) * 0x100000001B3ull;
  }
  wchar_t name[64];
  swprintf(name, 64, L"Local\\PortFolderClaim-%016llx", static_cast<unsigned long long>(hash));
  HANDLE lock = CreateMutexW(nullptr, FALSE, name);
  if (lock == nullptr) return true;
  if (GetLastError() != ERROR_ALREADY_EXISTS) return true;
  wchar_t parent[32];
  const DWORD got = GetEnvironmentVariableW(L"PORT_RESET_PARENT", parent, 32);
  if (got == 0 || got >= 32) return false;
  const DWORD pid = wcstoul(parent, nullptr, 10);
  if (pid == 0 || pid == GetCurrentProcessId()) return false;
  CloseHandle(lock);
  if (const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid); process != nullptr) {
    WaitForSingleObject(process, 20000);
    CloseHandle(process);
  }
  lock = CreateMutexW(nullptr, FALSE, name);
  if (lock == nullptr) return true;
  return GetLastError() != ERROR_ALREADY_EXISTS;
}
