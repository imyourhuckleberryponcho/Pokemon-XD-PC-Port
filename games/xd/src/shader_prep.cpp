#include <dolphin/gx/GXAurora.h>

#include <SDL3/SDL.h>
#include <run_status.h>
#ifdef __ANDROID__
#include <imgui.h>
#include <port_menu.h>
#include <settings.h>
#include <sys/system_properties.h>
#endif

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

#ifdef __ANDROID__
std::string AndroidProp(const char* name) {
  char v[PROP_VALUE_MAX] = {0};
  __system_property_get(name, v);
  return v;
}

std::string AndroidListHash() {
  SDL_IOStream* io = SDL_IOFromFile("initial_pipeline_cache.db", "rb");
  if (io == nullptr) return "";
  uint64_t h = 1469598103934665603ull;
  char buf[65536];
  for (;;) {
    const size_t n = SDL_ReadIO(io, buf, sizeof buf);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<unsigned char>(buf[i])) * 1099511628211ull;
  }
  SDL_CloseIO(io);
  char out[20];
  std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
  return out;
}

bool AndroidPrepareKey(std::string& key, std::string& cacheFolder) {
  const std::string list = AndroidListHash();
  if (list.empty()) return false;
  std::string cache;
  if (const char* dir = std::getenv("XD_CACHE_DIR"); dir != nullptr && dir[0] != '\0') {
    cache = dir;
  } else if (char* pref = SDL_GetPrefPath(nullptr, "Pokemon XD"); pref != nullptr) {
    cache = pref;
    SDL_free(pref);
  }
  if (cache.empty()) return false;
  while (cache.size() > 1 && (cache.back() == '/' || cache.back() == '\\')) cache.pop_back();
  key = "1|" XD_PORT_VERSION "|list " + list + "|android " + AndroidProp("ro.build.fingerprint") + "; vulkan " +
        AndroidProp("ro.hardware.vulkan") + "; soc " + AndroidProp("ro.board.platform") + "; ";
  for (char& c : key) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) c = '?';
  }
  cacheFolder = cache;
  return true;
}

std::string ReadFirstLine(const std::string& path) {
  std::ifstream in(path);
  std::string have;
  std::getline(in, have);
  while (!have.empty() && (have.back() == '\r' || have.back() == '\n')) have.pop_back();
  return have;
}

struct AndroidPrep {
  std::string key, stampPath;
  std::chrono::steady_clock::time_point start;
  uint32_t base = 0, total = 0;
  bool skipped = false, done = false;
  double lastLog = 0;
};
AndroidPrep sAndroid;
bool sAndroidWanted = false;
bool sAndroidOpened = false;

bool AndroidPrepFrame(void*, const PortMenuInput* in) {
  AuroraPipelineStats s{};
  AuroraGetPipelineStats(&s);
  AndroidPrep& a = sAndroid;
  const uint32_t built = s.created - a.base;
  a.total = std::max(a.total, built + s.queued);
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.start).count();
  if (in != nullptr && (in->pressed & 0x0200) != 0) { a.skipped = true; return false; }
  if (s.queued == 0) { a.done = true; return false; }
  if (secs > 45 * 60) { a.skipped = true; return false; }
  if (secs - a.lastLog >= 15) {
    a.lastLog = secs;
    std::fprintf(stderr, "[shaders] preparing the graphics: %u of %u after %.0f s\n", built, a.total, secs);
  }
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size = io.DisplaySize;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(8, 10, 24, 255));
  const float unit = std::max(14.f, std::min(size.x, size.y) / 22.f);
  ImFont* font = ImGui::GetFont();
  const float cx = size.x / 2, cy = size.y / 2;
  auto centered = [&](const char* text, float y, float px, ImU32 col) {
    const float wrap = size.x * 0.58f;
    const ImVec2 t = font->CalcTextSizeA(px, 1e9f, wrap, text, nullptr, nullptr);
    dl->AddText(font, px, ImVec2(cx - t.x / 2, y), col, text, nullptr, wrap);
    return t.y;
  };
  float y = cy - unit * 6.0f;
  y += centered("Getting the graphics ready", y, unit * 1.5f, IM_COL32(255, 255, 255, 255)) + unit * 0.5f;
  y += centered("This happens once on this phone (and again after a system update).", y, unit * 0.95f, IM_COL32(200, 205, 225, 255)) + unit * 0.2f;
  y += centered("It avoids short freezes and sound gaps later. The game starts when it is done.", y, unit * 0.95f, IM_COL32(200, 205, 225, 255)) + unit * 1.0f;
  const float barW = std::min(size.x - 4 * unit, unit * 28), barH = unit * 1.1f, bx = cx - barW / 2;
  const float frac = a.total > 0 ? std::min(1.f, static_cast<float>(built) / static_cast<float>(a.total)) : 0.f;
  dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + barW, y + barH), IM_COL32(40, 46, 80, 255), barH / 2);
  dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + barW * frac, y + barH), IM_COL32(90, 160, 255, 255), barH / 2);
  y += barH + unit * 0.6f;
  char line[160];
  const double left = built > 20 ? secs * (a.total - built) / built : 0;
  if (left > 0) {
    std::snprintf(line, sizeof line, "%u of %u   |   about %d min %02d s left", built, a.total, static_cast<int>(left) / 60,
                  static_cast<int>(left) % 60);
  } else {
    std::snprintf(line, sizeof line, "%u of %u", built, a.total);
  }
  y += centered(line, y, unit * 1.0f, IM_COL32(255, 255, 255, 255)) + unit * 1.2f;
  centered("Keep the phone plugged in if you can: it works hard for a while. Press B (or Back) to skip this time.", y, unit * 0.8f,
           IM_COL32(150, 156, 185, 255));
  SDL_Delay(30);
  return true;
}

void AndroidPrepClose(void*) {
  AndroidPrep& a = sAndroid;
  AuroraPipelineStats s{};
  AuroraGetPipelineStats(&s);
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.start).count();
  if (!a.done) {
    std::fprintf(stderr, "[shaders] preparing the graphics: SKIPPED after %.1f s (%u compiled, %u left); asked again at the next start\n", secs,
                 s.created - a.base, s.queued);
    return;
  }
  for (int waited = 0; AuroraCacheWritesPending() != 0 && waited < 15000; waited += 50) SDL_Delay(50);
  std::fprintf(stderr, "[shaders] prepared: %u pipelines compiled in %.1f s (Android, in the game)\n", s.created - a.base, secs);
  if (FILE* f = std::fopen(a.stampPath.c_str(), "wb")) {
    std::fputs((a.key + "\n").c_str(), f);
    std::fclose(f);
  }
}

const PortMenuPage kAndroidPrepPage{"shader preparation", nullptr, AndroidPrepFrame, AndroidPrepClose, nullptr};

void AndroidPrepStart(std::string& report) {
  std::string key, cache;
  if (!AndroidPrepareKey(key, cache)) {
    report = "prepared on this phone: not needed (no shader list in this APK)";
    return;
  }
  const bool test = std::getenv("XD_INPUT_SCRIPT") != nullptr || std::getenv("XD_REPLAY") != nullptr ||
                    std::getenv("XD_MENU_SCRIPT") != nullptr || std::getenv("XD_RENDER_ICON") != nullptr;
  const char* force = std::getenv("XD_ANDROID_PREPARE_SHADERS");
  const bool forced = force != nullptr && force[0] == '1';
  const std::string stamp = cache + "/shaders_prepared.txt";
  const std::string have = ReadFirstLine(stamp);
  if (have == key && !forced) {
    report = "prepared on this phone: yes (this version and system)";
    return;
  }
  if (have != key) std::fprintf(stderr, "[shaders] preparation stamp: wanted \"%s\", found \"%s\"\n", key.c_str(), have.c_str());
  if (test && !forced) {
    report = "prepared on this phone: NO (a test run: not asked)";
    return;
  }
  sAndroid.key = key;
  sAndroid.stampPath = stamp;
  sAndroidWanted = true;
  report = "prepared on this phone: NO (the game prepares the graphics first, with a progress screen)";
}
#endif

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
#ifdef __ANDROID__
  {
    const engine::settings::Registry& reg = engine::settings::Global();
    const size_t i = reg.Find("video.performance");
    std::fprintf(stderr, "[android] performance mode: %s\n",
                 i != engine::settings::kNoSetting && reg.ValueOf(i).i != 0 ? "on" : "off");
  }
#endif

  const char* stamp = std::getenv("XD_PREPARE_SHADERS");
  if (stamp == nullptr || stamp[0] == '\0') {
#ifdef __ANDROID__
    std::string text;
    AndroidPrepStart(text);
#else
    const std::string text = PreparedReport();
#endif
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
#ifdef __ANDROID__
  if (sAndroidWanted && !sAndroidOpened) {
    AuroraPipelineStats p{};
    AuroraGetPipelineStats(&p);
    sAndroidOpened = true;
    if (p.queued > 0) {
      sAndroid.start = std::chrono::steady_clock::now();
      sAndroid.base = p.created;
      sAndroid.total = p.created + p.queued;
      std::fprintf(stderr, "[shaders] preparing the graphics: %u known pipelines, %u to compile\n", p.knownPipelines, p.queued);
      PortMenu_Open(&kAndroidPrepPage);
    } else if (p.knownPipelines == 0) {
      std::fprintf(stderr, "[shaders] preparing the graphics: no pipelines known (the list or the cache database could not be read), not stamped\n");
    } else {
      std::fprintf(stderr, "[shaders] preparing the graphics: nothing queued, stamp written\n");
      if (FILE* f = std::fopen(sAndroid.stampPath.c_str(), "wb")) {
        std::fputs((sAndroid.key + "\n").c_str(), f);
        std::fclose(f);
      }
    }
  }
#endif
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
