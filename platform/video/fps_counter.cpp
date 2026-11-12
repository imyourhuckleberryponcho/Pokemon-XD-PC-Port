#include <port_fps.h>

#include <port_frame_rate.h>

#include <settings.h>

#include <imgui.h>

#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

namespace es = engine::settings;

constexpr double kWindowSeconds = 0.5;
constexpr double kGapSeconds = 0.25;
constexpr double kFieldsPerSecond = 60.0 / 1.001;

bool sHave = false;
uint64_t sStart = 0;
uint64_t sLast = 0;
uint32_t sPictures0 = 0, sFields0 = 0, sBetween0 = 0;
bool sShown = false;
double sFps = 0.0, sSpeed = 0.0;
double sShownFps = 0.0;
int sLog = -1;

bool SettingOn() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("video.fps_counter");
  return i != es::kNoSetting && reg.ValueOf(i).i != 0;
}

}

namespace {

std::mutex sNoticeLock;
std::string sNotice;
uint64_t sNoticeUntil = 0;

void draw_notice() {
  std::lock_guard<std::mutex> lock(sNoticeLock);
  if (sNotice.empty() || ImGui::GetCurrentContext() == nullptr) return;
  if (SDL_GetPerformanceCounter() > sNoticeUntil) {
    sNotice.clear();
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  float px = ImGui::GetIO().DisplaySize.y / 36.f;
  if (px < 14.f) px = 14.f;
  const ImVec2 size = font->CalcTextSizeA(px, 100000.f, 0.f, sNotice.c_str());
  const float pad = px * 0.35f;
  const ImVec2 at(pad * 2.f, pad * 1.6f + px * 1.6f);
  dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad * 0.6f), ImVec2(at.x + size.x + pad, at.y + size.y + pad * 0.6f),
                    IM_COL32(0, 0, 0, 170), pad);
  dl->AddText(font, px, at, IM_COL32(240, 240, 240, 255), sNotice.c_str());
}

}

extern "C" void PortVideo_Notice(const char* text, double seconds) {
  std::lock_guard<std::mutex> lock(sNoticeLock);
  sNotice = text != nullptr ? text : "";
  sNoticeUntil = SDL_GetPerformanceCounter() + static_cast<uint64_t>(seconds * static_cast<double>(SDL_GetPerformanceFrequency()));
}

extern "C" void PortFps_Frame(uint32_t gamePictures, uint32_t fields) {
  draw_notice();
  if (!SettingOn()) {
    sHave = false;
    sShown = false;
    return;
  }
  const uint64_t now = SDL_GetPerformanceCounter();
  const double perSecond = static_cast<double>(SDL_GetPerformanceFrequency());
  if (sHave && static_cast<double>(now - sLast) / perSecond > kGapSeconds) {
    sHave = false;
  }
  sLast = now;
  if (!sHave) {
    sHave = true;
    sStart = now;
    sPictures0 = gamePictures;
    sFields0 = fields;
    sBetween0 = PortFrameRate_BetweenCount();
  } else {
    const double seconds = static_cast<double>(now - sStart) / perSecond;
    if (seconds >= kWindowSeconds) {
      sFps = static_cast<double>(gamePictures - sPictures0) / seconds;
      sSpeed = static_cast<double>(fields - sFields0) / seconds / kFieldsPerSecond * 100.0;
      const uint32_t between = PortFrameRate_BetweenCount();
      sShownFps = between != sBetween0 ? static_cast<double>(gamePictures - sPictures0 + between - sBetween0) / seconds : 0.0;
      sBetween0 = between;
      sShown = true;
      sStart = now;
      sPictures0 = gamePictures;
      sFields0 = fields;
      if (sLog < 0) {
        const char* v = std::getenv("XD_FPS_LOG");
        sLog = v != nullptr && v[0] == '1' ? 1 : 0;
      }
      if (sLog == 1 && sShownFps > 0.0) {
        std::fprintf(stderr, "[fps] %.1f fps (%.1f shown) %.0f%%\n", sFps, sShownFps, sSpeed);
      } else if (sLog == 1) {
        std::fprintf(stderr, "[fps] %.1f fps %.0f%%\n", sFps, sSpeed);
      }
    }
  }
  if (!sShown || ImGui::GetCurrentContext() == nullptr) return;
  char text[48];
  if (sShownFps > 0.0) {
    std::snprintf(text, sizeof text, "%.0f fps (%.0f shown)  %.0f%%", sFps, sShownFps, sSpeed);
  } else {
    std::snprintf(text, sizeof text, "%.0f fps  %.0f%%", sFps, sSpeed);
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = ImGui::GetFont();
  float px = ImGui::GetIO().DisplaySize.y / 36.f;
  if (px < 14.f) px = 14.f;
  const ImVec2 size = font->CalcTextSizeA(px, 100000.f, 0.f, text);
  const float pad = px * 0.35f;
  const ImVec2 at(pad * 2.f, pad * 1.6f);
  dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad * 0.6f), ImVec2(at.x + size.x + pad, at.y + size.y + pad * 0.6f),
                    IM_COL32(0, 0, 0, 150), pad);
  dl->AddText(font, px, at, IM_COL32(240, 240, 240, 255), text);
}
