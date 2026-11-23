#include <dolphin/gx/GXAurora.h>

#include <SDL3/SDL.h>
#include <run_status.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#else
#include <port_win_compat.h>
#include <cwchar>
#endif

#ifndef _WIN32
#include <csignal>
#include <cerrno>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifndef XD_PORT_VERSION
#define XD_PORT_VERSION "unknown"
#endif

extern "C" uint32_t PortVI_GetFlipCount(void);

namespace {

bool sStats = false;
AuroraPipelineStats sLast{};

void SetDefault(const char* name, const char* value) {
  const char* now = std::getenv(name);
#ifdef _WIN32
  if (now == nullptr || now[0] == '\0') _putenv_s(name, value);
#else
  if (now == nullptr || now[0] == '\0') setenv(name, value, 1);
#endif
}

#ifdef _WIN32
std::wstring Widen(const char* s) {
  std::wstring w(std::strlen(s) + 1, L'\0');
  w.resize(std::max(0, MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), (int)w.size()) - 1));
  return w;
}

void WriteFileAtomic(const std::wstring& path, const std::string& text) {
  const std::wstring tmp = path + L".tmp";
  if (FILE* f = _wfopen(tmp.c_str(), L"wb")) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
  }
}
#else
std::wstring Widen(const char* s) {
  std::wstring w;
  for (; *s != '\0'; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
  return w;
}
std::string Narrow(const std::wstring& w) {
  std::string s;
  for (const wchar_t c : w) s.push_back(static_cast<char>(c));
  return s;
}
const wchar_t* _wgetenv(const wchar_t*) { return nullptr; }

void WriteFileAtomic(const std::wstring& path, const std::string& text) {
  const std::string final = Narrow(path), tmp = final + ".tmp";
  if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), final.c_str());
  }
}
#endif

void PrintTotals() {
  AuroraPipelineStats s{};
  AuroraGetPipelineStats(&s);
  std::fprintf(stderr,
               "[shaders] totals: %u pipelines known, %u compiled this run; %llu draws waited for a shader (%.1f ms in "
               "all, longest %.1f ms), %llu draws skipped\n",
               s.knownPipelines, s.created, (unsigned long long)s.waits, s.waitMicros / 1000.0,
               s.maxWaitMicros / 1000.0, (unsigned long long)s.skippedDraws);
}

std::string Fnv64File(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  uint64_t h = 1469598103934665603ull;
  char buf[65536];
  while (in.read(buf, sizeof buf) || in.gcount() > 0) {
    for (std::streamsize i = 0; i < in.gcount(); ++i) {
      h = (h ^ static_cast<unsigned char>(buf[i])) * 1099511628211ull;
    }
  }
  char out[20];
  std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
  return out;
}

std::string GraphicsAdapters() {
#if !defined(_WIN32)
  return "adapters not listed on this system";
#else
  std::string out;
  HMODULE dll = LoadLibraryW(L"dxgi.dll");
  if (dll == nullptr) return "unknown";
  using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
  auto create = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dll, "CreateDXGIFactory1")));
  IDXGIFactory1* factory = nullptr;
  if (create == nullptr || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    return "unknown";
  }
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    LARGE_INTEGER driver{};
    adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
    for (const wchar_t* c = desc.Description; *c != L'\0'; ++c) {
      out += (*c >= 0x20 && *c < 0x7F) ? static_cast<char>(*c) : '?';
    }
    char line[96];
    std::snprintf(line, sizeof line, " %04x:%04x driver %u.%u.%u.%u; ", desc.VendorId, desc.DeviceId,
                  HIWORD(driver.HighPart), LOWORD(driver.HighPart), HIWORD(driver.LowPart), LOWORD(driver.LowPart));
    out += line;
    adapter->Release();
  }
  factory->Release();
  return out;
#endif
}

std::string sPreparedReport;

std::string PreparedReport() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const char* basePath = SDL_GetBasePath();
  if (basePath == nullptr) return "prepared on this PC: unknown (program folder not found)";
  const std::string baseUtf8 = basePath;
  const fs::path list = fs::path(std::u8string(baseUtf8.begin(), baseUtf8.end())) / L"initial_pipeline_cache.db";
  if (!fs::exists(list, ec)) return "prepared on this PC: not needed (no shader list is shipped with this program)";
  fs::path cache;
  if (const char* dir = std::getenv("XD_CACHE_DIR"); dir != nullptr && dir[0] != '\0') {
    cache = fs::path(std::u8string(reinterpret_cast<const char8_t*>(dir), reinterpret_cast<const char8_t*>(dir) + std::strlen(dir)));
  } else if (char* pref = SDL_GetPrefPath(nullptr, "Pokemon XD"); pref != nullptr) {
    const std::string utf8 = pref;
    SDL_free(pref);
    cache = fs::path(std::u8string(utf8.begin(), utf8.end()));
  }
  if (cache.empty()) return "prepared on this PC: unknown (no cache folder)";
  std::string key = "1|" XD_PORT_VERSION "|list " + Fnv64File(list) + "|" + GraphicsAdapters();
  for (char& c : key) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) c = '?';
  }
  std::string have;
  {
    std::ifstream in(cache / L"shaders_prepared.txt");
    std::getline(in, have);
    while (!have.empty() && (have.back() == '\r' || have.back() == '\n')) have.pop_back();
  }
  if (have == key) return "prepared on this PC: yes (this version and graphics driver)";
  std::fprintf(stderr, "[shaders] preparation stamp: wanted \"%s\", found \"%s\"\n", key.c_str(), have.c_str());
  return "prepared on this PC: NO (start the game from the launcher once, so it can prepare the graphics; "
         "until then the first battles and places may stutter)";
}

}

extern "C" const char* PortXD_ShadersPreparedReport(void) { return sPreparedReport.c_str(); }

extern "C" void PortXD_BackgroundGuardStart(void) {
  const char* text = std::getenv("XD_PARENT_PID");
  if (text == nullptr || text[0] == 0) return;
  const long parent = std::strtol(text, nullptr, 10);
  if (parent <= 0) return;
  std::thread([parent] {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parent));
    if (h == nullptr) _Exit(3);
    WaitForSingleObject(h, INFINITE);
#else
    while (kill(static_cast<pid_t>(parent), 0) == 0 || errno == EPERM) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif
    std::fprintf(stderr, "[background] the launcher that started this run has ended: stopping\n");
    std::fflush(stderr);
    _Exit(3);
  }).detach();
}

extern "C" bool PortXD_ShadersPrepareMode(void) {
  const char* stamp = std::getenv("XD_PREPARE_SHADERS");
  if (stamp == nullptr || stamp[0] == '\0') return false;
  SetDefault("XD_NO_MESSAGE_BOXES", "1");
  SetDefault("XD_WINDOW_OFFSCREEN", "1");
#ifndef __APPLE__
  SetDefault("XD_WINDOW_HIDDEN", "1");
#endif
  SetDefault("SDL_WINDOW_ACTIVATE_WHEN_SHOWN", "0");
  SetDefault("XD_RECORD", "0");
  SetDefault("SDL_AUDIO_DRIVER", "dummy");
  SetDefault("XD_RUN_LABEL", "prepare shaders");
  PortXD_BackgroundGuardStart();
  return true;
}

extern "C" void PortXD_ShadersStart(void) {
  const char* wait = std::getenv("XD_SHADER_WAIT");
  const bool blocking = wait == nullptr || std::strcmp(wait, "0") != 0;
  AuroraSetDrawsBlocking(blocking);
  if (!blocking) std::fprintf(stderr, "[shaders] XD_SHADER_WAIT=0: draws are SKIPPED while their shader compiles\n");
  const char* stats = std::getenv("XD_SHADER_STATS");
  sStats = stats != nullptr && stats[0] == '1';
  std::atexit(PrintTotals);

  const char* stamp = std::getenv("XD_PREPARE_SHADERS");
  if (stamp == nullptr || stamp[0] == '\0') {
    const std::string text = PreparedReport();
    sPreparedReport = "shaders " + text;
    std::fprintf(stderr, "[shaders] %s\n", text.c_str());
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  const wchar_t* wideStamp = _wgetenv(L"XD_PREPARE_SHADERS");
  const std::wstring stampPath = wideStamp != nullptr ? std::wstring(wideStamp) : Widen(stamp);
  const std::wstring progressPath = stampPath + L".progress";
  AuroraPipelineStats s{};
  AuroraGetPipelineStats(&s);
  const uint32_t base = s.created;
  std::fprintf(stderr, "[shaders] preparing %u known pipelines (%u to compile)\n", s.knownPipelines, s.queued);
  PortRunStatus_SetNote("preparing shaders");
  PortRunStatus_SetWaitingForPlayer(true);
  uint32_t lastBuilt = UINT32_MAX;
  for (;;) {
    SDL_PumpEvents();
    AuroraGetPipelineStats(&s);
    const uint32_t built = s.created - base;
    const uint32_t total = built + s.queued;
    if (built != lastBuilt) {
      WriteFileAtomic(progressPath, std::to_string(built) + " " + std::to_string(total) + "\n");
      lastBuilt = built;
    }
    if (s.queued == 0) break;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (secs > 20 * 60) {
      std::fprintf(stderr, "[shaders] preparation took too long (%u of %u)\n", built, total);
      std::fflush(stderr);
      _Exit(2);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::fprintf(stderr, "[shaders] prepared: %u pipelines compiled in %.1f s\n", s.created - base, secs);
  const char* key = std::getenv("XD_PREPARE_SHADERS_KEY");
  for (int waited = 0; AuroraCacheWritesPending() != 0 && waited < 15000; waited += 50) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (const uint32_t left = AuroraCacheWritesPending(); left != 0) {
    std::fprintf(stderr, "[shaders] %u cache entries not stored (the cache database stayed busy)\n", left);
  }
  WriteFileAtomic(stampPath, std::string(key != nullptr ? key : "") + "\n");
  std::fflush(stdout);
  std::fflush(stderr);
  _Exit(0);
}

extern "C" void PortXD_ShadersFrame(void) {
  if (!sStats) return;
  AuroraPipelineStats s{};
  AuroraGetPipelineStats(&s);
  if (s.waits != sLast.waits || s.skippedDraws != sLast.skippedDraws) {
    std::fprintf(stderr,
                 "[shaders] frame %u: %llu draws waited %.1f ms, %llu skipped (known %u, queued %u) | so far %llu "
                 "waited %.1f ms, longest %.1f ms, %llu skipped\n",
                 PortVI_GetFlipCount(), (unsigned long long)(s.waits - sLast.waits),
                 (s.waitMicros - sLast.waitMicros) / 1000.0, (unsigned long long)(s.skippedDraws - sLast.skippedDraws),
                 s.knownPipelines, s.queued, (unsigned long long)s.waits, s.waitMicros / 1000.0,
                 s.maxWaitMicros / 1000.0, (unsigned long long)s.skippedDraws);
  }
  sLast = s;
}
