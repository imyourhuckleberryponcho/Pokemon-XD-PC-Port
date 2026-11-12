#include <dolphin/gx/GXAurora.h>

#include <SDL3/SDL.h>
#include <run_status.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <port_win_compat.h>
#include <cwchar>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

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

}

extern "C" bool PortXD_ShadersPrepareMode(void) {
  const char* stamp = std::getenv("XD_PREPARE_SHADERS");
  if (stamp == nullptr || stamp[0] == '\0') return false;
  SetDefault("XD_NO_MESSAGE_BOXES", "1");
  SetDefault("XD_WINDOW_OFFSCREEN", "1");
  SetDefault("SDL_WINDOW_ACTIVATE_WHEN_SHOWN", "0");
  SetDefault("XD_RECORD", "0");
  SetDefault("SDL_AUDIO_DRIVER", "dummy");
  SetDefault("XD_RUN_LABEL", "prepare shaders");
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
  if (stamp == nullptr || stamp[0] == '\0') return;
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
