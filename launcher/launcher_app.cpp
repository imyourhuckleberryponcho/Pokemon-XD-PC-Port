#include "launcher.h"
#include "launcher_extra.h"
#include "launcher_reports.h"
#include "launcher_sys.h"

#include <mods.h>
#include <save_backups.h>
#include <save_slots.h>
#include <port_window_icon.h>
#include <settings_ui.h>
#include <update.h>

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

namespace {

namespace es = engine::settings;
namespace em = engine::mods;
namespace bk = engine::memcard::backups;
namespace sl = engine::memcard::slots;
namespace ui = engine::settings_ui;
namespace fs = std::filesystem;

constexpr const char* kDiscSetting = "system.disc_path";
constexpr const char* kStaySetting = "system.launcher_stays_open";
constexpr const char* kGroundSetting = "system.launcher_background";
constexpr const char* kIconSetting = "system.app_icon";
constexpr const char* kUpdateSetting = "system.update_check";

void Log(const std::string& text) {
  std::fprintf(stderr, "[launcher] %s\n", text.c_str());
  std::fflush(stderr);
}

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
fs::path PathOf(const std::string& utf8) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Lower(std::string s) {
  for (char& c : s) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return s;
}

ImVec4 Hex(uint32_t rgb, float a = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

const ImVec4 kGood = Hex(0x5FD69A), kWarn = Hex(0xF4C55C), kBad = Hex(0xFF7A70), kBusy = Hex(0x8FB8FF);
const ImVec4 kDim = Hex(0xACA4C4), kFaint = Hex(0x7B7296);
const ImVec4 kText = Hex(0xEFEAFA);
const ImVec4 kAccent = Hex(0x9B6BFF), kAccentHover = Hex(0xB089FF), kAccentActive = Hex(0x8452F0);
const ImVec4 kGround = Hex(0x110B1C), kPanel = Hex(0x1A1329), kPanelHigh = Hex(0x231A37), kLine = Hex(0x302549);

ImU32 U32(const ImVec4& c, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

void ApplyStyle(float scale) {
  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsDark(&s);
  s.WindowPadding = ImVec2(20, 18);
  s.FramePadding = ImVec2(12, 7);
  s.ItemSpacing = ImVec2(10, 9);
  s.ItemInnerSpacing = ImVec2(8, 6);
  s.CellPadding = ImVec2(10, 8);
  s.IndentSpacing = 22;
  s.ScrollbarSize = 12;
  s.GrabMinSize = 14;
  s.WindowRounding = 0;
  s.ChildRounding = 10;
  s.FrameRounding = 7;
  s.PopupRounding = 10;
  s.GrabRounding = 7;
  s.TabRounding = 7;
  s.ScrollbarRounding = 8;
  s.WindowBorderSize = 0;
  s.ChildBorderSize = 1;
  s.PopupBorderSize = 1;
  s.FrameBorderSize = 0;
  s.TabBarBorderSize = 1;
  s.TabBarOverlineSize = 2;
  s.SeparatorTextBorderSize = 1;
  s.ScaleAllSizes(scale);
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kFaint;
  c[ImGuiCol_WindowBg] = kGround;
  c[ImGuiCol_ChildBg] = kPanel;
  c[ImGuiCol_PopupBg] = Hex(0x201833);
  c[ImGuiCol_TitleBg] = Hex(0x2A1F45);
  c[ImGuiCol_TitleBgActive] = Hex(0x3A2A63);
  c[ImGuiCol_TitleBgCollapsed] = Hex(0x2A1F45);
  c[ImGuiCol_Border] = kLine;
  c[ImGuiCol_FrameBg] = Hex(0x271E3E);
  c[ImGuiCol_FrameBgHovered] = Hex(0x33284F);
  c[ImGuiCol_FrameBgActive] = Hex(0x3D3061);
  c[ImGuiCol_Button] = Hex(0x2F2549);
  c[ImGuiCol_ButtonHovered] = Hex(0x43346A);
  c[ImGuiCol_ButtonActive] = Hex(0x553F8C);
  c[ImGuiCol_Header] = Hex(0x30264B);
  c[ImGuiCol_HeaderHovered] = Hex(0x3B2E5C);
  c[ImGuiCol_HeaderActive] = Hex(0x4A3A75);
  c[ImGuiCol_CheckMark] = Hex(0xD2BEFF);
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHover;
  c[ImGuiCol_Tab] = Hex(0x211933);
  c[ImGuiCol_TabHovered] = Hex(0x43346A);
  c[ImGuiCol_TabSelected] = Hex(0x352A55);
  c[ImGuiCol_TabSelectedOverline] = kAccentHover;
  c[ImGuiCol_TabDimmed] = Hex(0x211933);
  c[ImGuiCol_TabDimmedSelected] = Hex(0x2C2346);
  c[ImGuiCol_TableHeaderBg] = Hex(0x241B39);
  c[ImGuiCol_TableRowBg] = Hex(0x1A1329, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF, 0.025f);
  c[ImGuiCol_TableBorderLight] = Hex(0x2A2140);
  c[ImGuiCol_TableBorderStrong] = kLine;
  c[ImGuiCol_Separator] = kLine;
  c[ImGuiCol_SeparatorHovered] = kAccent;
  c[ImGuiCol_ScrollbarBg] = Hex(0x000000, 0.0f);
  c[ImGuiCol_ScrollbarGrab] = Hex(0x3A2E58);
  c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x4B3B72);
  c[ImGuiCol_ScrollbarGrabActive] = Hex(0x5C4889);
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_NavCursor] = kAccentHover;
  c[ImGuiCol_TextSelectedBg] = Hex(0x9B6BFF, 0.35f);
  c[ImGuiCol_ModalWindowDimBg] = Hex(0x06030C, 0.65f);
}

struct Fonts {
  ImFont* body = nullptr;
  ImFont* heading = nullptr;
  ImFont* nav = nullptr;
  ImFont* title = nullptr;
  ImFont* play = nullptr;
  ImFont* caption = nullptr;
};

Fonts LoadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  Fonts f;
  const fs::path own = sys::ProgramFolder();
  struct Choice {
    fs::path regular, strong, display;
  };
  std::vector<Choice> choices = {{own / L"fonts" / L"Inter-Regular.ttf", own / L"fonts" / L"Inter-SemiBold.ttf",
                                  own / L"fonts" / L"InterDisplay-Bold.ttf"}};
  for (const sys::FontSet& set : sys::SystemFonts()) {
    choices.push_back({set.regular, set.strong, set.display});
  }
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2193, 0};
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  std::error_code ec;
  for (const Choice& c : choices) {
    if (!fs::exists(c.regular, ec)) {
      continue;
    }
    const fs::path strong = fs::exists(c.strong, ec) ? c.strong : c.regular;
    const fs::path display = fs::exists(c.display, ec) ? c.display : strong;
    const auto add = [&](const fs::path& file, float size) {
      return io.Fonts->AddFontFromFileTTF(Utf8(file).c_str(), size * scale, &config, kRanges);
    };
    f.body = add(c.regular, 17.0f);
    if (f.body == nullptr) {
      continue;
    }
    f.heading = add(strong, 20.0f);
    f.nav = add(strong, 16.5f);
    f.caption = add(c.regular, 15.0f);
    f.play = add(display, 23.0f);
    f.title = add(display, 27.0f);
    break;
  }
  if (f.body == nullptr) {
    ImFontConfig fallback;
    fallback.SizePixels = 15.0f * scale;
    f.body = io.Fonts->AddFontDefault(&fallback);
  }
  for (ImFont** p : {&f.heading, &f.nav, &f.caption, &f.play, &f.title}) {
    if (*p == nullptr) {
      *p = f.body;
    }
  }
  return f;
}

void DrawGlyph(ImDrawList* dl, int page, ImVec2 p, float s, ImU32 col) {
  const float t = std::max(1.5f, s * 0.09f);
  const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
  switch (page) {
  case 0:
    dl->AddCircle(c, s * 0.45f, col, 32, t);
    dl->AddCircle(c, s * 0.14f, col, 16, t);
    dl->PathArcTo(c, s * 0.30f, -2.6f, -1.6f, 8);
    dl->PathStroke(col, 0, t * 0.8f);
    break;
  case 1: {
    const float r = s * 0.48f, q = s * 0.13f;
    const ImVec2 pts[] = {{c.x, c.y - r}, {c.x + q, c.y - q}, {c.x + r, c.y}, {c.x + q, c.y + q},
                          {c.x, c.y + r}, {c.x - q, c.y + q}, {c.x - r, c.y}, {c.x - q, c.y - q}};
    dl->AddPolyline(pts, 8, col, ImDrawFlags_Closed, t);
    break;
  }
  case 2:
    for (int i = 0; i < 3; i++) {
      const float y = p.y + s * (0.2f + 0.3f * i);
      const float k = p.x + s * (i == 0 ? 0.3f : i == 1 ? 0.7f : 0.45f);
      dl->AddLine(ImVec2(p.x + s * 0.04f, y), ImVec2(p.x + s * 0.96f, y), col, t);
      dl->AddCircleFilled(ImVec2(k, y), s * 0.12f, col, 12);
    }
    break;
  case 3: {
    dl->AddRect(ImVec2(p.x + s * 0.02f, p.y + s * 0.22f), ImVec2(p.x + s * 0.98f, p.y + s * 0.80f), col, s * 0.26f, 0,
                t);
    const ImVec2 d(p.x + s * 0.30f, p.y + s * 0.51f);
    dl->AddLine(ImVec2(d.x - s * 0.12f, d.y), ImVec2(d.x + s * 0.12f, d.y), col, t);
    dl->AddLine(ImVec2(d.x, d.y - s * 0.12f), ImVec2(d.x, d.y + s * 0.12f), col, t);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.68f, p.y + s * 0.44f), s * 0.065f, col, 10);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.78f, p.y + s * 0.58f), s * 0.065f, col, 10);
    break;
  }
  case 4:
    dl->AddRect(ImVec2(p.x + s * 0.04f, p.y + s * 0.50f), ImVec2(p.x + s * 0.46f, p.y + s * 0.92f), col, s * 0.08f, 0,
                t);
    dl->AddRect(ImVec2(p.x + s * 0.54f, p.y + s * 0.50f), ImVec2(p.x + s * 0.96f, p.y + s * 0.92f), col, s * 0.08f, 0,
                t);
    dl->AddRectFilled(ImVec2(p.x + s * 0.29f, p.y + s * 0.06f), ImVec2(p.x + s * 0.71f, p.y + s * 0.42f), col,
                      s * 0.08f);
    break;
  case 5: {
    const float l = p.x + s * 0.16f, r = p.x + s * 0.84f, top = p.y + s * 0.04f, b = p.y + s * 0.96f;
    const ImVec2 pts[] = {{l, top}, {r - s * 0.22f, top}, {r, top + s * 0.22f}, {r, b}, {l, b}};
    dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, t);
    dl->AddRectFilled(ImVec2(l + s * 0.14f, b - s * 0.34f), ImVec2(r - s * 0.14f, b - s * 0.14f), col, s * 0.04f);
    break;
  }
  case 6: {
    const float l = p.x + s * 0.14f, r = p.x + s * 0.86f, top = p.y + s * 0.02f, b = p.y + s * 0.98f;
    const ImVec2 pts[] = {{l, top}, {r - s * 0.24f, top}, {r, top + s * 0.24f}, {r, b}, {l, b}};
    dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, t);
    for (int i = 0; i < 3; i++) {
      const float y = p.y + s * (0.44f + 0.18f * i);
      dl->AddLine(ImVec2(l + s * 0.14f, y), ImVec2(r - s * (i == 2 ? 0.30f : 0.14f), y), col, t * 0.85f);
    }
    break;
  }
  case 7: {
    dl->AddCircle(ImVec2(p.x + s * 0.34f, p.y + s * 0.34f), s * 0.30f, col, 24, t);
    dl->AddCircle(ImVec2(p.x + s * 0.34f, p.y + s * 0.34f), s * 0.08f, col, 12, t);
    dl->AddRect(ImVec2(p.x + s * 0.52f, p.y + s * 0.56f), ImVec2(p.x + s * 0.98f, p.y + s * 0.98f), col, s * 0.08f, 0, t);
    dl->AddLine(ImVec2(p.x + s * 0.58f, p.y + s * 0.77f), ImVec2(p.x + s * 0.92f, p.y + s * 0.77f), col, t);
    break;
  }
  default: break;
  }
}

SDL_Texture* MakeTexture(SDL_Renderer* renderer, const icon::Image& img) {
  if (img.Empty()) {
    return nullptr;
  }
  SDL_Texture* tex =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
  if (tex != nullptr) {
    SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
  }
  return tex;
}

ImTextureID TexId(SDL_Texture* tex) { return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tex)); }

std::string FileKey(const std::string& gameId) {
  std::string out;
  for (char ch : gameId) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
      out += ch;
    }
  }
  return out;
}

bool SavePng(const icon::Image& img, const fs::path& file) {
  SDL_Surface* s = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_RGBA32,
                                         const_cast<uint8_t*>(img.rgba.data()), img.width * 4);
  if (s == nullptr) {
    return false;
  }
  const bool ok = SDL_SavePNG(s, Utf8(file).c_str());
  SDL_DestroySurface(s);
  return ok;
}

icon::Image LoadPng(const fs::path& file) {
  icon::Image img;
  SDL_Surface* loaded = SDL_LoadPNG(Utf8(file).c_str());
  SDL_Surface* rgba = loaded != nullptr ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (rgba != nullptr) {
    img.width = rgba->w;
    img.height = rgba->h;
    img.rgba.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; y++) {
      std::memcpy(&img.rgba[static_cast<size_t>(y) * rgba->w * 4],
                  static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                  static_cast<size_t>(rgba->w) * 4);
    }
  }
  SDL_DestroySurface(rgba);
  SDL_DestroySurface(loaded);
  return img;
}

std::vector<std::pair<std::string, std::string>> ReadList(const fs::path& file) {
  std::vector<std::pair<std::string, std::string>> out;
  if (std::FILE* f = sys::OpenFile(file, "rb")) {
    char line[512];
    while (std::fgets(line, sizeof line, f) != nullptr) {
      std::string text(line);
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
      }
      const size_t tab = text.find('\t');
      if (tab != std::string::npos) {
        out.emplace_back(text.substr(0, tab), text.substr(tab + 1));
      }
    }
    std::fclose(f);
  }
  return out;
}

bool WriteText(const fs::path& file, const std::string& text) {
  std::FILE* f = sys::OpenFile(file, "wb");
  if (f == nullptr) {
    return false;
  }
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  return std::fclose(f) == 0 && ok;
}

using sys::ReadIcoSize;

engine::controls::Device DeviceOf(SDL_Gamepad* pad) {
  using D = engine::controls::Device;
  switch (SDL_GetGamepadType(pad)) {
  case SDL_GAMEPAD_TYPE_XBOX360:
  case SDL_GAMEPAD_TYPE_XBOXONE: return D::Xbox;
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5: return D::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    return SDL_GetGamepadProduct(pad) == 0x2073 ? D::NsoGameCube : D::Switch;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return D::Switch;
  case SDL_GAMEPAD_TYPE_GAMECUBE: return D::GameCube;
  default: return D::Generic;
  }
}

std::vector<std::string> Split(const std::string& text, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(sep, start);
    std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!part.empty() && (part.front() == ' ')) {
      part.erase(part.begin());
    }
    while (!part.empty() && (part.back() == ' ')) {
      part.pop_back();
    }
    if (!part.empty()) {
      out.push_back(part);
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}

enum class Page { Disc, Extra, Preset, Settings, Controls, Mods, Save, Reports, Count };
const char* const kPageNames[] = {"Disc", "Extra assets", "Preset", "Settings", "Controls", "Mods", "Save", "Reports"};
const char* const kPageIds[] = {"disc", "extra", "preset", "settings", "controls", "mods", "save", "reports"};
int GlyphOf(int page) { return page == static_cast<int>(Page::Extra) ? 7 : page > static_cast<int>(Page::Extra) ? page - 1 : page; }

struct Background {
  std::mutex mutex;
  std::thread thread;
  std::atomic<bool> busy{false};
  std::atomic<bool> stop{false};
  std::atomic<bool> drawing{false};
  std::atomic<bool> stopDrawing{false};
  std::atomic<uint64_t> done{0}, total{0};
  std::atomic<bool> preparing{false};
  std::atomic<uint32_t> shadersDone{0}, shadersTotal{0};
  std::atomic<bool> shadersRunning{false};
  std::string discPath;
  bool haveVerdict = false;
  DiscVerdict verdict;
  bool haveSave = false;
  SaveSummary save;
  bool haveIcon = false;
  std::atomic<uint32_t> iconVersion{0};
  std::string iconKey;
  struct IconChoice {
    std::string id, name;
    fs::path file;
    icon::Image preview;
  };
  std::vector<IconChoice> iconChoices;
  struct GroundChoice {
    std::string id, name;
    icon::Image image;
  };
  std::vector<GroundChoice> groundChoices;
  std::string iconNote;
};

struct UpdateWork {
  enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };
  std::mutex mutex;
  std::thread thread;
  std::atomic<bool> busy{false};
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> done{0}, total{0};
  Phase phase = Phase::Idle;
  bool manual = false;
  bool installing = false;
  engine::update::Manifest manifest;
  std::string message;
  std::string installed;
  bool offer = false;
  std::atomic<uint32_t> serial{0};
};

class App {
 public:
  explicit App(const GameInfo& game) : game_(game) {}
  ~App() {
    StopWorker();
    StopUpdate();
    StopOpenRead();
    ExtraStop();
  }
  int Run();

 private:
  const GameInfo& game_;
  es::Registry registry_;
  std::vector<std::string> settingsNotes_;
  uint32_t savedChanges_ = 0;
  std::string saveError_;
  ui::Options options_;
  ui::ControlsPanel controls_;

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  Fonts fonts_;
  float scale_ = 1.0f;
  Page page_ = Page::Disc;
  std::string wantTab_;
  bool quit_ = false;

  Background bg_;
  std::mutex pickMutex_;
  std::string picked_;
  bool pickedReady_ = false;

  em::ModList mods_;
  std::vector<em::ModFolder> found_;
  std::vector<em::Message> modMessages_;
  std::string modError_;
  std::string newProfile_;

  sys::Process* gameProcess_ = nullptr;
  bool gameRunning_ = false;
  std::string playError_;
  bool askStartAnyway_ = false;
  bool playWhenDrawn_ = false;

  std::vector<std::string> script_;
  size_t step_ = 0;
  int framesSinceStep_ = 0;
  std::string waitFor_;
  std::string shotFile_;
  std::vector<SDL_Gamepad*> pads_;
  std::vector<uint16_t> fakeKeys_;

  SDL_Texture* ownIcon_ = nullptr;
  std::vector<SDL_Texture*> ownIcons_;
  std::vector<SDL_Texture*> choiceIcons_;
  std::vector<std::pair<std::string, std::string>> grounds_;
  std::vector<SDL_Texture*> groundTex_;
  uint32_t iconShown_ = 0;
  std::string iconApplied_ = "\x01";
  fs::path chosenIcon_;
  std::string shortcutMessage_;
  bool shortcutOk_ = false;

  std::vector<reports::Report> reports_;
  uint64_t reportsLookedAt_ = 0;
  std::string reportSel_;
  SDL_Texture* reportShot_ = nullptr;
  std::string reportShotFor_ = "\x01";
  int reportShotW_ = 0, reportShotH_ = 0;
  reports::ExportOptions exportOptions_;
  std::string reportMessage_;
  bool reportOk_ = false;
  std::string exportPicked_;
  bool exportPickedReady_ = false;
  std::vector<reports::Session> sessions_;
  size_t sessionSel_ = 0;
  reports::SessionBundle sessionBundle_;
  std::string sessionBundleFor_ = "\x01";
  bool sessionOpen_ = false;
  std::string sessionMessage_;
  bool sessionOk_ = false;
  std::string sessionPicked_;
  bool sessionPickedReady_ = false;
  std::vector<bk::Backup> backups_;
  uint64_t backupsLookedAt_ = 0;
  std::string backupSel_;
  std::string backupMessage_;
  bool backupOk_ = false;
  sl::Index slots_;
  std::vector<GameInfo::SaveRow> slotRows_;
  uint64_t slotsLookedAt_ = 0;
  std::string slotNote_;
  std::string slotMessage_;
  bool slotOk_ = false;
  std::string slotName_;
  enum class SlotAsk { None, New, Rename, Delete } slotAsk_ = SlotAsk::None;
  std::string slotPicked_;
  int slotPickedKind_ = 0;
  std::string openSlot_;
  bool openSlotSet_ = false;
  bool backupsShown_ = false;
  struct OpenRead {
    std::thread thread;
    std::atomic<bool> busy{false};
    std::mutex mutex;
    std::string key;
    SaveSummary save;
    bool have = false;
  } openRead_;
  std::string openReadKey_;
  bool openReadAgain_ = false;
  std::string codeMessage_;
  bool askSpoilers_ = false;
  bool closeSpoilers_ = false;
  UpdateWork up_;
  engine::update::Layout layout_;
  bool askUpdates_ = false;
  bool updatesAsked_ = false;
  std::string skipVersion_;
  uint32_t upSeen_ = 0;
  bool openUpdates_ = false, openGoBack_ = false, closeWindows_ = false;
  std::string versionNote_;

  struct ExtraState {
    extra::DiscCheck check;
    extra::MakeJob job;
    std::mutex mutex;
    extra::Status status;
    bool converterFound = false;
    bool looked = false;
    std::thread looker;
    std::atomic<bool> looking{false};
    bool lookAgain = false;
    bool wasRunning = false;
    bool finishedShown = false;
    extra::MakeJob::Snapshot last;
    std::string note;
  };
  std::vector<std::unique_ptr<ExtraState>> extras_;
  std::string extraPicked_;
  int extraPickedFor_ = -1;
  bool extraPickedReady_ = false;
  void ExtraStart();
  void ExtraStop();
  void ExtraLook(int index);
  void ExtraSelectDisc(int index, const std::string& path);
  void ExtraMake(int index);
  void ExtraCancel(int index);
  void ExtraPoll();
  bool ExtraBusy() const;
  void DrawExtraPage();
  fs::path ExtraFolder(const GameInfo::ExtraSource& s) const { return game_.settingsFolder / "extra_assets" / PathOf(s.id); }

  void LoadSettings();
  void SaveSettingsIfChanged();
  std::string DiscPath() const { return registry_.Text(kDiscSetting); }

  void SelectDisc(const std::string& path);
  void StartWorker(const std::string& discPath);
  void StopWorker();
  bool DiscPlayable(std::string& why);
  void MakeDiscIcon(const std::string& discPath, const std::string& gameId);
  void FollowIcon();
  void ApplyIconChoice();
  std::string ResolvedIconId();
  int GroundIndex();
  void DrawGround();
  void Shortcut(bool desktop);

  void ReloadMods();
  void SaveMods();

  void Play(bool confirmed);
  bool StartGame(std::string& error);
  void PollGame();

  void LoadUpdaterState();
  void SaveUpdaterState();
  void FindVersions();
  std::string FolderVersionOf(const std::string& folder);
  std::string NewestInstalled();
  void StartUpdateCheck(bool manual);
  void StartUpdateInstall();
  void StopUpdate();
  void FollowUpdate();
  void SkipUpdate();
  bool GoBackVersion(std::string& error);
  void RestartLauncher();
  void DrawUpdateCard();
  void DrawUpdateWindows();
  void OpenInSystem(const fs::path& what);
  void DrawNotOpenedWindow();
  std::string notOpened_;

  void RunScript();
  bool DoStep(const std::string& step);

  void Frame();
  void DrawHeader();
  void DrawNav(float height);
  void DrawBottomBar();
  void DrawDiscPage();
  void DrawPresetPage();
  void DrawSettingsPage();
  void DrawControlsPage();
  void UpdatePads();
  void FeedLiveInputs();
  void DrawModsPage();
  void DrawSavePage();
  void DrawSetup(const SaveSummary& save);
  void CopyCode(const std::string& code);
  void DrawSaveSummary();
  void DrawSaveContents(const SaveSummary& save);
  void DrawBackups(bool inBox);
  void LookAtBackups(bool now);
  void RestoreBackup();
  fs::path BackupsFolder() const;
  bool SlotsOn() const { return !game_.saves.dataFolder.empty(); }
  void LookAtSlots(bool now);
  bool SlotsLocked(std::string& why) const;
  bool SlotDone(bool ok, const std::string& message);
  void ActiveSaveChanged();
  void RereadSave();
  bool ChooseSlot(const std::string& id);
  bool NewSlot(const std::string& name);
  bool RenameSlot(const std::string& name);
  bool DuplicateSlot();
  bool DeleteSlot();
  bool ImportSlot(const std::string& file);
  bool ExportSlot(const std::string& file);
  bool BinFolder(const fs::path& folder, std::string& error);
  void DrawSlots();
  const sl::Slot* OpenSlot() const { return openSlot_.empty() ? nullptr : slots_.Find(openSlot_); }
  const sl::Slot* SlotByArg(const std::string& arg) const;
  void OpenSlotIs(const std::string& id);
  bool OpenSummary(SaveSummary& save);
  void StopOpenRead();
  void DrawSlotBox(const sl::Slot& slot, bool locked, bool& duplicate);
  void DrawIconSection();
  void DrawReportsPage();
  void LookAtReports(bool now);
  const reports::Report* SelectedReport() const;
  void ExportReport(const std::string& file);
  void DeleteReport();
  void LookAtSessions();
  void SaveSession(const std::string& file);
  void DrawSessionWindow();
  void Heading(const char* text);
  void PageTitle(const char* title, const std::string& intro);
  void Dim(const std::string& text);
  bool PlayButton(const char* label, ImVec2 size, bool enabled);
  bool BeginCard(const char* id);
  void EndCard();
  void Screenshot(const std::string& file);
};

void App::Heading(const char* text) {
  ImGui::PushFont(fonts_.heading);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

void App::PageTitle(const char* title, const std::string& intro) {
  ImGui::PushFont(fonts_.title);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (!intro.empty()) {
    Dim(intro);
  }
  ImGui::Dummy(ImVec2(0, 6 * scale_));
}

void App::Dim(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

bool App::BeginCard(const char* id) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelHigh);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * scale_, 14 * scale_));
  const bool open = ImGui::BeginChild(id, ImVec2(0, 0),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                          ImGuiChildFlags_AlwaysUseWindowPadding);
  return open;
}

void App::EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

bool App::PlayButton(const char* label, ImVec2 size, bool enabled) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  if (!enabled) {
    ImGui::BeginDisabled();
  }
  const bool clicked = ImGui::InvisibleButton("##play", size);
  const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
  if (!enabled) {
    ImGui::EndDisabled();
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 q(p.x + size.x, p.y + size.y);
  const float r = 12 * scale_;
  if (enabled) {
    for (int i = 3; i >= 1; i--) {
      const float g = i * 3 * scale_;
      dl->AddRectFilled(ImVec2(p.x - g, p.y - g + 2 * scale_), ImVec2(q.x + g, q.y + g + 2 * scale_),
                        U32(kAccent, 0.07f), r + g);
    }
  }
  const ImVec4 fill = !enabled ? Hex(0x2B2342) : held ? kAccentActive : hovered ? kAccentHover : kAccent;
  dl->AddRectFilled(p, q, U32(fill), r);
  if (enabled) {
    dl->AddRectFilled(p, ImVec2(q.x, p.y + size.y * 0.5f), U32(Hex(0xFFFFFF), 0.08f), r,
                      ImDrawFlags_RoundCornersTop);
  }
  ImGui::PushFont(fonts_.play);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  const float tri = fonts_.play->FontSize * 0.62f, gap = 12 * scale_;
  const float total = tri + gap + ts.x;
  const float x = p.x + (size.x - total) * 0.5f, cy = p.y + size.y * 0.5f;
  const ImU32 ink = enabled ? U32(Hex(0xFFFFFF)) : U32(kFaint);
  dl->AddTriangleFilled(ImVec2(x, cy - tri * 0.55f), ImVec2(x, cy + tri * 0.55f), ImVec2(x + tri, cy), ink);
  dl->AddText(ImVec2(x + tri + gap, cy - ts.y * 0.5f), ink, label);
  ImGui::PopFont();
  return clicked && enabled;
}

void App::LoadSettings() {
  registry_.SetWarningSink([this](const std::string& w) {
    settingsNotes_.push_back(w);
    Log("settings: " + w);
  });
  game_.registerSettings(registry_);
  if (registry_.Load(game_.settingsFolder)) {
    Log("read " + Utf8(game_.settingsFolder / es::Registry::kFileName));
    for (const std::string& id : registry_.LastLoad().unknownIds) {
      settingsNotes_.push_back("settings.ini has \"" + id + "\", which this version doesn't know (kept as it is)");
    }
  } else {
    Log("no " + Utf8(game_.settingsFolder / es::Registry::kFileName) + " yet: the defaults (Original)");
  }
  savedChanges_ = registry_.Changes();
}

void App::SaveSettingsIfChanged() {
  if (registry_.Changes() == savedChanges_) {
    return;
  }
  savedChanges_ = registry_.Changes();
  if (game_.playerFilesReadOnly) {
    Log("settings: " + game_.readOnlyNote);
    std::fprintf(stderr, "[settings] %s\n", game_.readOnlyNote.c_str());
    saveError_.clear();
    return;
  }
  if (registry_.Save(game_.settingsFolder)) {
    saveError_.clear();
  } else {
    saveError_ = "Your settings couldn't be saved to " + Utf8(game_.settingsFolder / es::Registry::kFileName) + ".";
  }
}

void App::SelectDisc(const std::string& path) {
  Log("disc: " + path);
  if (!registry_.SetText(kDiscSetting, path)) {
    playError_ = "This file name can't be used: " + path;
    return;
  }
  SaveSettingsIfChanged();
  StartWorker(path);
}

void App::StopWorker() {
  if (bg_.thread.joinable()) {
    bg_.stop = true;
    bg_.thread.join();
  }
  bg_.stop = false;
}

void App::StartWorker(const std::string& discPath) {
  StopWorker();
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    bg_.discPath = discPath;
    bg_.haveVerdict = false;
    bg_.haveSave = false;
  }
  bg_.done = 0;
  bg_.total = 0;
  bg_.busy = true;
  bg_.drawing = static_cast<bool>(game_.renderPictures) || static_cast<bool>(game_.prepareShaders);
  bg_.preparing = static_cast<bool>(game_.prepareShaders);
  bg_.shadersDone = 0;
  bg_.shadersTotal = 0;
  bg_.shadersRunning = false;
  bg_.stopDrawing = false;
  bg_.thread = std::thread([this, discPath] {
    DiscVerdict verdict;
    bool usable = false;
    if (!discPath.empty()) {
      verdict = game_.checkDisc(discPath, [this](uint64_t done, uint64_t total) {
        bg_.done = done;
        bg_.total = total;
        return !bg_.stop.load();
      });
      usable = verdict.kind != DiscVerdict::Kind::Problem;
      std::lock_guard<std::mutex> lock(bg_.mutex);
      bg_.verdict = verdict;
      bg_.haveVerdict = true;
    }
    if (bg_.stop) {
      bg_.busy = false;
      bg_.drawing = false;
      bg_.preparing = false;
      return;
    }
    SaveSummary save = game_.readSave(usable ? discPath : std::string());
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      bg_.save = std::move(save);
      bg_.haveSave = true;
    }
    if (usable && !bg_.stop) {
      MakeDiscIcon(discPath, verdict.gameId);
    }
    bg_.busy = false;
    if (usable && !bg_.stop && game_.prepareShaders) {
      std::string error;
      const auto started = std::chrono::steady_clock::now();
      bool counted = false;
      const bool ok = game_.prepareShaders(
          discPath,
          [this, &counted](uint32_t done, uint32_t total) {
            bg_.shadersDone = done;
            bg_.shadersTotal = total;
            bg_.shadersRunning = true;
            counted = true;
          },
          [this] { return bg_.stop.load(); }, error);
      if (!ok) {
        Log("shaders: " + error);
      } else if (counted) {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        char line[160];
        std::snprintf(line, sizeof line, "shaders: %u prepared for this PC in %.1f s", bg_.shadersTotal.load(), secs);
        Log(line);
      }
    }
    bg_.preparing = false;
    if (usable && !bg_.stop && !bg_.stopDrawing && game_.renderPictures) {
      std::string error;
      const bool ok = game_.renderPictures(
          discPath, verdict.gameId,
          [this] {
            std::lock_guard<std::mutex> lock(bg_.mutex);
            bg_.iconNote = "Drawing more icons and backgrounds from the game's own 3D models (about a minute; once "
                           "for this disc)...";
          },
          [this] { return bg_.stop.load() || bg_.stopDrawing.load(); }, error);
      if (!ok) {
        Log("pictures from the game: " + error);
      }
      if (!bg_.stop) {
        MakeDiscIcon(discPath, verdict.gameId);
      }
    }
    bg_.drawing = false;
  });
}

void App::MakeDiscIcon(const std::string& discPath, const std::string& gameId) {
  const std::string key = FileKey(gameId);
  if (!game_.readDiscPictures || game_.iconFolder.empty() || key.empty()) {
    return;
  }
  const fs::path list = game_.iconFolder / (key + "_icons_v2.txt");
  const fs::path groundList = game_.backgroundFolder / (key + "_backgrounds.txt");
  std::vector<Background::IconChoice> choices;
  std::vector<Background::GroundChoice> grounds;
  std::string note;
  std::error_code ec;
  for (const auto& [id, name] : ReadList(list)) {
    Background::IconChoice c;
    c.id = id;
    c.name = name;
    c.file = game_.iconFolder / (key + "_" + id + ".ico");
    choices.push_back(std::move(c));
  }
  for (const auto& [id, name] : game_.backgroundFolder.empty() ? decltype(ReadList(list))() : ReadList(groundList)) {
    grounds.push_back({id, name, LoadPng(game_.backgroundFolder / (key + "_" + id + ".png"))});
  }
  const bool complete =
      !choices.empty() && fs::exists(groundList, ec) &&
      std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return !fs::exists(c.file, ec); }) &&
      std::none_of(grounds.begin(), grounds.end(), [](const auto& g) { return g.image.Empty(); });
  if (!complete) {
    choices.clear();
    grounds.clear();
    std::vector<GameInfo::DiscPicture> pictures;
    icon::Image banner;
    std::string error;
    if (!game_.readDiscPictures(discPath, pictures, banner, error)) {
      note = "No icon could be made from this disc" + (error.empty() ? std::string(".") : ": " + error);
    }
    std::string listText, groundText;
    for (GameInfo::DiscPicture& p : pictures) {
      if (p.background) {
        const std::string id = FileKey(p.id);
        if (!game_.backgroundFolder.empty() && !id.empty()) {
          fs::create_directories(game_.backgroundFolder, ec);
          if (SavePng(p.image, game_.backgroundFolder / (key + "_" + id + ".png"))) {
            groundText += id + "\t" + p.name + "\n";
            grounds.push_back({id, p.name, std::move(p.image)});
          }
        }
        continue;
      }
      const icon::Image smallTile = p.forSmall.Empty() ? icon::Image() : icon::Framed(p.forSmall, 250);
      const std::vector<icon::Image> sizes =
          icon::IconSizes(icon::Framed(p.image), smallTile.Empty() ? nullptr : &smallTile);
      Background::IconChoice c;
      c.id = FileKey(p.id);
      c.name = p.name;
      c.file = game_.iconFolder / (key + "_" + c.id + ".ico");
      if (c.id.empty() || !icon::WriteIco(c.file, sizes)) {
        note = "The icons couldn't be saved in " + Utf8(game_.iconFolder) + ".";
        continue;
      }
      SavePng(sizes.back(), game_.iconFolder / (key + "_" + c.id + ".png"));
      listText += c.id + "\t" + c.name + "\n";
      choices.push_back(std::move(c));
    }
    if (!banner.Empty()) {
      SavePng(banner, game_.iconFolder / (key + "_disc_banner.png"));
    }
    if (!choices.empty()) {
      WriteText(list, listText);
      Log("icons made from the disc: " + std::to_string(choices.size()) + " in " + Utf8(game_.iconFolder));
    }
    if (!game_.backgroundFolder.empty() && !grounds.empty()) {
      WriteText(groundList, groundText);
      Log("backgrounds from the disc: " + std::to_string(grounds.size()) + " in " + Utf8(game_.backgroundFolder));
    }
  }
  if (!game_.backgroundFolder.empty()) {
    for (const auto& e : fs::directory_iterator(game_.backgroundFolder / L"rendered", ec)) {
      if (e.path().extension() == L".png" && e.is_regular_file(ec)) {
        icon::Image img = LoadPng(e.path());
        if (!img.Empty()) {
          grounds.insert(grounds.begin(),
                         Background::GroundChoice{"rendered_" + FileKey(Utf8(e.path().stem())),
                                                  Utf8(e.path().stem()) + " (rendered)", std::move(img)});
        }
      }
    }
  }
  for (const auto& e : fs::directory_iterator(game_.iconFolder / L"rendered", ec)) {
    if (e.path().extension() != L".png" || !e.is_regular_file(ec)) {
      continue;
    }
    Background::IconChoice c;
    c.id = "rendered_" + FileKey(Utf8(e.path().stem()));
    {
      std::string name = Utf8(e.path().stem());
      std::replace(name.begin(), name.end(), '_', ' ');
      if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') {
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
      }
      c.name = name + " (3D render)";
    }
    c.file = game_.iconFolder / (key + "_" + c.id + ".ico");
    if (!fs::exists(c.file, ec) || fs::last_write_time(c.file, ec) < fs::last_write_time(e.path(), ec)) {
      const icon::Image img = LoadPng(e.path());
      const icon::Image smallTile = img.Empty() ? icon::Image() : icon::Framed(icon::FocusTop(img), 250);
      if (img.Empty() ||
          !icon::WriteIco(c.file, icon::IconSizes(icon::Framed(icon::Square(img)), smallTile.Empty() ? nullptr : &smallTile))) {
        Log("rendered picture not usable: " + Utf8(e.path()));
        continue;
      }
    }
    choices.push_back(std::move(c));
  }
  for (Background::IconChoice& c : choices) {
    c.preview = ReadIcoSize(c.file, 64);
  }
  if (note.empty() && !choices.empty()) {
    note = "Made from your disc, only on this PC: " + Utf8(game_.iconFolder);
  }
  std::lock_guard<std::mutex> lock(bg_.mutex);
  bg_.haveIcon = true;
  bg_.iconKey = key;
  bg_.iconChoices = std::move(choices);
  bg_.groundChoices = std::move(grounds);
  bg_.iconNote = note;
  bg_.iconVersion++;
}

void App::FollowIcon() {
  if (bg_.iconVersion != iconShown_) {
    std::vector<icon::Image> previews;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      iconShown_ = bg_.iconVersion;
      for (const auto& c : bg_.iconChoices) {
        previews.push_back(c.preview);
      }
    }
    for (SDL_Texture* tex : choiceIcons_) {
      if (tex != nullptr) {
        SDL_DestroyTexture(tex);
      }
    }
    choiceIcons_.clear();
    for (const icon::Image& img : previews) {
      choiceIcons_.push_back(MakeTexture(renderer_, img));
    }
    std::vector<icon::Image> grounds;
    grounds_.clear();
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      for (const auto& g : bg_.groundChoices) {
        grounds_.emplace_back(g.id, g.name);
        grounds.push_back(g.image);
      }
    }
    for (SDL_Texture* tex : groundTex_) {
      if (tex != nullptr) {
        SDL_DestroyTexture(tex);
      }
    }
    groundTex_.clear();
    for (const icon::Image& img : grounds) {
      groundTex_.push_back(MakeTexture(renderer_, img));
    }
    iconApplied_ = "\x01";
  }
  if (registry_.Text(kIconSetting) != iconApplied_) {
    ApplyIconChoice();
  }
}

int App::GroundIndex() {
  const std::string value = registry_.Text(kGroundSetting);
  if (value == "none" || grounds_.empty()) {
    return -1;
  }
  for (size_t i = 0; i < grounds_.size(); i++) {
    if (grounds_[i].first == value) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void App::DrawGround() {
  const int index = GroundIndex();
  SDL_Texture* tex = index >= 0 && index < static_cast<int>(groundTex_.size()) ? groundTex_[index] : nullptr;
  if (tex == nullptr) {
    return;
  }
  float tw = 0, th = 0;
  SDL_GetTextureSize(tex, &tw, &th);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
  const float scale = std::max(vp->Size.x / tw, vp->Size.y / th);
  const float uw = vp->Size.x / scale / tw, vh = vp->Size.y / scale / th;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddImage(TexId(tex), a, b, ImVec2((1 - uw) / 2, (1 - vh) / 2), ImVec2((1 + uw) / 2, (1 + vh) / 2));
  dl->AddRectFilled(a, b, U32(kGround, 0.4f));
  dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + vp->Size.y * 0.5f), b, U32(kGround, 0.0f), U32(kGround, 0.0f),
                              U32(kGround, 0.35f), U32(kGround, 0.35f));
}

std::string App::ResolvedIconId() {
  const std::string value = registry_.Text(kIconSetting);
  for (const GameInfo::OwnIcon& o : game_.ownIcons) {
    if (o.id == value) {
      return value;
    }
  }
  std::lock_guard<std::mutex> lock(bg_.mutex);
  for (const auto& c : bg_.iconChoices) {
    if (c.id == value) {
      return value;
    }
  }
  if (!bg_.iconChoices.empty()) {
    return bg_.iconChoices.front().id;
  }
  return game_.ownIcons.empty() ? std::string() : game_.ownIcons.front().id;
}

void App::ApplyIconChoice() {
  iconApplied_ = registry_.Text(kIconSetting);
  const std::string id = ResolvedIconId();
  std::error_code ec;
  fs::path file;
  for (const GameInfo::OwnIcon& o : game_.ownIcons) {
    if (o.id == id && o.resource != 1 && !game_.iconFolder.empty()) {
      file = game_.iconFolder / (FileKey(o.id) + ".ico");
      fs::create_directories(game_.iconFolder, ec);
      if (!PortWindowIcon_SaveResource(o.resource, Utf8(file).c_str())) {
        file.clear();
      }
    }
  }
  std::string key;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    key = bg_.iconKey;
    for (const auto& c : bg_.iconChoices) {
      if (c.id == id) {
        file = c.file;
      }
    }
  }
  chosenIcon_ = file;
  const bool own = std::any_of(game_.ownIcons.begin(), game_.ownIcons.end(),
                               [&](const GameInfo::OwnIcon& o) { return o.id == iconApplied_; });
  if (key.empty() && file.empty() && !own && !game_.iconFolder.empty()) {
    for (const auto& e : fs::directory_iterator(game_.iconFolder, ec)) {
      const std::wstring stem = e.path().stem().wstring();
      if (e.path().extension() == L".ico" && stem.size() == 6 &&
          std::all_of(stem.begin(), stem.end(), [](wchar_t c) { return iswalnum(c) != 0; })) {
        chosenIcon_ = e.path();
      }
    }
  }
  if (!key.empty() && !game_.iconFolder.empty()) {
    const fs::path current = game_.iconFolder / (key + ".ico");
    if (file.empty()) {
      fs::remove(current, ec);
    } else {
      fs::copy_file(file, current, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        Log("the icon couldn't be copied to " + Utf8(current) + ": " + ec.message());
      } else {
        chosenIcon_ = current;
      }
    }
  }
  PortWindowIcon_SetAppId(window_, game_.appId.c_str());
  const bool fromFile = PortWindowIcon_Apply(window_, chosenIcon_.empty() ? nullptr : Utf8(chosenIcon_).c_str());
  Log("window icon: " + (!fromFile ? std::string("the program's own")
                         : (key.empty() && file.empty() ? std::string("last time's") : id) + " (" + Utf8(chosenIcon_) +
                               ")"));
}

void App::Shortcut(bool desktop) {
  fs::path folder = game_.shortcutFolder;
  if (folder.empty()) {
    folder = sys::KnownFolder(desktop ? sys::Folder::Desktop : sys::Folder::Programs, true);
  }
  if (folder.empty()) {
    shortcutOk_ = false;
    shortcutMessage_ = std::string("The system didn't say where your ") +
                       (desktop ? "desktop" : sys::ProgramsFolderName()) + " is.";
    return;
  }
  shortcutOk_ = sys::MakeShortcut(folder, game_.title, game_.title + " " + game_.subtitle, game_.shortcutTarget,
                             chosenIcon_, shortcutMessage_);
  Log(shortcutMessage_);
}

bool App::DiscPlayable(std::string& why) {
  const std::string path = DiscPath();
  if (path.empty()) {
    why = "Choose your disc image first (Disc page).";
    return false;
  }
  std::lock_guard<std::mutex> lock(bg_.mutex);
  if (!bg_.haveVerdict || bg_.discPath != path) {
    why = "The disc is being checked...";
    return false;
  }
  if (bg_.verdict.kind == DiscVerdict::Kind::Problem) {
    why = "This disc image can't be used (see the Disc page).";
    return false;
  }
  return true;
}

void App::ReloadMods() {
  modMessages_.clear();
  found_ = em::FindMods(game_.modsFolder, modMessages_);
  std::vector<std::string> warnings;
  const bool exists = em::ReadModListFile(game_.modListFile, mods_, warnings);
  for (const std::string& w : warnings) {
    modMessages_.push_back({em::Message::Level::Warning, w});
  }
  const std::vector<std::string> fresh = em::AddNewMods(mods_, found_);
  for (const std::string& id : fresh) {
    modMessages_.push_back({em::Message::Level::Info, "New mod found: " + id + " (added, switched off)"});
  }
  if (!fresh.empty() || (!exists && !found_.empty())) {
    SaveMods();
  }
}

void App::SaveMods() {
  if (game_.playerFilesReadOnly) {
    Log("mods: " + game_.readOnlyNote);
    std::fprintf(stderr, "[mods] %s (mods.ini)\n", game_.readOnlyNote.c_str());
    modError_.clear();
    return;
  }
  if (em::WriteModListFile(game_.modListFile, mods_)) {
    modError_.clear();
  } else {
    modError_ = "The mod list couldn't be saved to " + Utf8(game_.modListFile) + ".";
  }
}

bool App::StartGame(std::string& error) {
  sys::StartRequest request;
  request.program = game_.program;
  request.arguments = {DiscPath()};
  request.folder = game_.program.parent_path();
  request.shareOutput = true;
  std::string why;
  gameProcess_ = sys::StartProgram(request, why);
  if (gameProcess_ == nullptr) {
    error = "The game couldn't be started (" + Utf8(game_.program) + ", " + why + "). Is it next to the launcher?";
    return false;
  }
  gameRunning_ = true;
  Log("started " + Utf8(game_.program) + " (process " + std::to_string(sys::IdOf(gameProcess_)) + ")");
  return true;
}

void App::Play(bool confirmed) {
  std::string why;
  if (gameRunning_ || !DiscPlayable(why)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    if (!confirmed && bg_.verdict.kind == DiscVerdict::Kind::Warning) {
      askStartAnyway_ = true;
      return;
    }
  }
  if (bg_.drawing) {
    Log(bg_.preparing ? "play: starts once the shaders are prepared" : "play: stopping the game's picture drawing first");
    bg_.stopDrawing = true;
    playWhenDrawn_ = true;
    return;
  }
  SaveSettingsIfChanged();
  SaveMods();
  Log("play: " + registry_.Summary());
  playError_.clear();
  if (!StartGame(playError_)) {
    Log(playError_);
    return;
  }
  const bool stay = registry_.Bool(kStaySetting).Get();
  if (!stay && step_ >= script_.size()) {
    quit_ = true;
  }
}

void App::PollGame() {
  if (!gameRunning_ || !sys::Ended(gameProcess_, 0)) {
    return;
  }
  const int code = sys::ExitCode(gameProcess_);
  sys::Close(gameProcess_);
  gameProcess_ = nullptr;
  gameRunning_ = false;
  Log("the game ended (exit code " + std::to_string(code) + ")");
  ReloadMods();
  LookAtSlots(true);
  StartWorker(DiscPath());
}

namespace eu = engine::update;

fs::path OwnProgramFolder() { return sys::ProgramFolder(); }

bool PrimaryButton(const char* label) {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(3);
  return clicked;
}

std::string MegaBytes(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / 1e6);
  return text;
}

void App::LoadUpdaterState() {
  updatesAsked_ = false;
  skipVersion_.clear();
  if (std::FILE* f = sys::OpenFile(game_.updates.stagingFolder / L"updater.ini", "rb")) {
    char line[256];
    while (std::fgets(line, sizeof line, f) != nullptr) {
      std::string text(line);
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
      }
      if (text == "asked=yes") {
        updatesAsked_ = true;
      } else if (text.rfind("skip=", 0) == 0) {
        skipVersion_ = text.substr(5);
      }
    }
    std::fclose(f);
  }
}

void App::SaveUpdaterState() {
  std::error_code ec;
  fs::create_directories(game_.updates.stagingFolder, ec);
  std::string text = "# The launcher's updater (docs/UPDATER.md). Delete this file to be asked again.\r\n";
  text += updatesAsked_ ? "asked=yes\r\n" : "";
  text += skipVersion_.empty() ? "" : "skip=" + skipVersion_ + "\r\n";
  if (!WriteText(game_.updates.stagingFolder / L"updater.ini", text)) {
    Log("updates: updater.ini couldn't be saved in " + Utf8(game_.updates.stagingFolder));
  }
}

void App::FindVersions() {
  layout_ = eu::FindLayout(OwnProgramFolder(), game_.updates.starterName);
}

std::string App::FolderVersionOf(const std::string& folder) {
  if (!layout_.valid || folder.empty()) {
    return {};
  }
  if (folder == layout_.running) {
    return game_.updates.version;
  }
  return eu::FolderVersion(layout_.root / PathOf(folder));
}

std::string App::NewestInstalled() {
  const std::string next = layout_.valid && layout_.current != layout_.running &&
                                   eu::FolderUsable(layout_.root / PathOf(layout_.current), game_.updates.requiredFiles)
                               ? FolderVersionOf(layout_.current)
                               : std::string();
  return eu::IsNewer(next, game_.updates.version) ? next : game_.updates.version;
}

void App::StopUpdate() {
  if (up_.thread.joinable()) {
    up_.stop = true;
    up_.thread.join();
  }
  up_.stop = false;
}

void App::StartUpdateCheck(bool manual) {
  if (!game_.updates.Active() || up_.busy) {
    return;
  }
  StopUpdate();
  {
    std::lock_guard<std::mutex> lock(up_.mutex);
    up_.phase = UpdateWork::Phase::Checking;
    up_.manual = manual;
    up_.installing = false;
    up_.message.clear();
  }
  const std::string have = NewestInstalled(), skip = skipVersion_;
  Log(std::string("updates: checking ") + game_.updates.feedUrl + (manual ? " (Check now)" : " (at start)"));
  up_.busy = true;
  up_.thread = std::thread([this, manual, have, skip] {
    const GameInfo::Updates& u = game_.updates;
    eu::FetchOptions options;
    options.maxBytes = eu::kMaxManifestBytes;
    options.userAgent = u.product + "/" + u.version;
    options.progress = [this](uint64_t, uint64_t) { return !up_.stop.load(); };
    std::string text, error;
    eu::Manifest m;
    UpdateWork::Phase phase = UpdateWork::Phase::Failed;
    std::string message;
    if (!eu::FetchToString(u.feedUrl, options, text, error)) {
      message = "Couldn't check for updates: " + error + ".";
    } else if (!eu::ParseSignedManifest(text, u.publicKey, u.product, m, error)) {
      message = "The update information was refused: " + error + ".";
    } else if (eu::IsNewer(m.version, have)) {
      phase = UpdateWork::Phase::Available;
      message = "Version " + m.version + " is available (you have " + have + ").";
    } else {
      phase = UpdateWork::Phase::UpToDate;
      message = "You have the newest version (" + have + ").";
    }
    Log("updates: " + message);
    std::lock_guard<std::mutex> lock(up_.mutex);
    up_.phase = phase;
    up_.manifest = m;
    up_.message = message;
    up_.offer = phase == UpdateWork::Phase::Available && (manual || m.version != skip);
    if (phase == UpdateWork::Phase::Available && !up_.offer) {
      Log("updates: version " + m.version + " was skipped earlier: not asked");
    }
    up_.serial++;
    up_.busy = false;
  });
}

void App::StartUpdateInstall() {
  eu::Manifest m;
  {
    std::lock_guard<std::mutex> lock(up_.mutex);
    if (up_.busy || up_.phase != UpdateWork::Phase::Available) {
      return;
    }
    m = up_.manifest;
    up_.phase = UpdateWork::Phase::Downloading;
    up_.installing = true;
    up_.message = "Downloading version " + m.version + "...";
  }
  StopUpdate();
  FindVersions();
  const eu::Layout layout = layout_;
  Log("updates: Update now: downloading " + m.url + " (" + std::to_string(m.size) + " bytes)");
  up_.done = 0;
  up_.total = m.size;
  up_.busy = true;
  up_.thread = std::thread([this, m, layout] {
    const GameInfo::Updates& u = game_.updates;
    const fs::path zip = u.stagingFolder / PathOf("update-" + m.version + ".zip");
    eu::FetchOptions options;
    options.maxBytes = m.size;
    options.userAgent = u.product + "/" + u.version;
    options.timeoutMs = 60000;
    options.progress = [this](uint64_t done, uint64_t) {
      up_.done = done;
      return !up_.stop.load();
    };
    std::string error, installed;
    bool ok = layout.valid;
    if (!ok) {
      error = layout.why;
    }
    ok = ok && eu::FetchToFile(m.url, zip, options, error);
    if (ok) {
      std::lock_guard<std::mutex> lock(up_.mutex);
      up_.message = "Checking and unpacking version " + m.version + "...";
    }
    eu::InstallRequest request;
    request.layout = layout;
    request.manifest = m;
    request.package = zip;
    request.requiredFiles = u.requiredFiles;
    ok = ok && eu::Install(request, installed, error, [this] { return up_.stop.load(); });
    std::error_code ec;
    fs::remove(zip, ec);
    std::lock_guard<std::mutex> lock(up_.mutex);
    if (ok) {
      up_.phase = UpdateWork::Phase::Installed;
      up_.installed = installed;
      up_.message = "Version " + m.version + " is installed. It starts the next time you open " + game_.title + ".";
    } else {
      up_.phase = UpdateWork::Phase::Failed;
      up_.message = (error == "stopped" ? std::string("The update was cancelled") :
                                          "The update couldn't be installed: " + error) +
                    ". Nothing was changed: your version (" + u.version + ") stays as it is.";
    }
    Log("updates: " + up_.message + (ok ? " (" + installed + ")" : ""));
    up_.serial++;
    up_.busy = false;
  });
}

void App::FollowUpdate() {
  std::lock_guard<std::mutex> lock(up_.mutex);
  if (up_.serial == upSeen_) {
    return;
  }
  upSeen_ = up_.serial;
  if (up_.offer) {
    up_.offer = false;
    openUpdates_ = true;
  }
  if (up_.phase == UpdateWork::Phase::Installed) {
    FindVersions();
    versionNote_.clear();
  }
}

void App::SkipUpdate() {
  std::lock_guard<std::mutex> lock(up_.mutex);
  if (up_.phase == UpdateWork::Phase::Available) {
    skipVersion_ = up_.manifest.version;
    Log("updates: skip version " + skipVersion_);
    SaveUpdaterState();
  }
}

bool App::GoBackVersion(std::string& error) {
  FindVersions();
  const std::string leaving = FolderVersionOf(layout_.current), back = FolderVersionOf(layout_.previous);
  if (!eu::GoBack(layout_, game_.updates.requiredFiles, error)) {
    Log("updates: go back refused: " + error);
    return false;
  }
  if (eu::IsNewer(leaving, back)) {
    skipVersion_ = leaving;
    SaveUpdaterState();
  } else if (skipVersion_ == back) {
    skipVersion_.clear();
    SaveUpdaterState();
  }
  FindVersions();
  versionNote_ = "Version " + back + " starts the next time you open " + game_.title + " (version " + leaving +
                 " stays installed).";
  Log("updates: " + versionNote_);
  return true;
}

void App::RestartLauncher() {
  if (!layout_.valid || gameRunning_) {
    return;
  }
  const fs::path starter = layout_.root / PathOf(game_.updates.starterName);
  sys::StartRequest request;
  request.program = starter;
  request.folder = layout_.root;
  request.toFront = true;
  std::string why;
  if (sys::Process* started = sys::StartProgram(request, why)) {
    sys::Close(started);
    Log("updates: restarted through " + Utf8(starter));
    quit_ = true;
  } else {
    versionNote_ = "Please close this window and open " + game_.title + " again (the restart didn't work: " + why +
                   ").";
  }
}

void App::DrawUpdateCard() {
  Heading("Version and updates");
  BeginCard("##updates");
  const GameInfo::Updates& u = game_.updates;
  ImGui::Text("Version %s", u.version.c_str());
  if (layout_.valid && layout_.running != eu::kFirstFolder) {
    ImGui::SameLine();
    ImGui::TextColored(kFaint, "(in the folder %s)", layout_.running.c_str());
  }
  if (!u.Active()) {
    EndCard();
    return;
  }
  bool check = registry_.Bool(kUpdateSetting).Get();
  if (ImGui::Checkbox("Check for updates when the launcher starts", &check)) {
    registry_.SetBool(kUpdateSetting, check);
    updatesAsked_ = true;
    SaveUpdaterState();
  }
  Dim("It only looks. If there is a newer version, the launcher shows what's new and asks you first: nothing is ever "
      "downloaded or installed without your yes. Your saves, settings and mods are never touched by an update.");
  UpdateWork::Phase phase;
  std::string message, available;
  {
    std::lock_guard<std::mutex> lock(up_.mutex);
    phase = up_.phase;
    message = up_.message;
    available = up_.manifest.version;
  }
  const bool busy = up_.busy;
  if (busy) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(phase == UpdateWork::Phase::Checking ? "Checking..." : "Check now")) {
    StartUpdateCheck(true);
  }
  if (busy) {
    ImGui::EndDisabled();
  }
  if (phase == UpdateWork::Phase::Available && !busy) {
    ImGui::SameLine();
    if (ImGui::Button(("See version " + available + "...").c_str())) {
      openUpdates_ = true;
    }
  }
  if (!message.empty() && phase != UpdateWork::Phase::Installed) {
    const ImVec4 tone = phase == UpdateWork::Phase::Failed      ? kWarn
                        : phase == UpdateWork::Phase::Available ? kAccentHover
                        : phase == UpdateWork::Phase::UpToDate || phase == UpdateWork::Phase::Installed ? kGood
                                                                                                         : kBusy;
    ImGui::PushStyleColor(ImGuiCol_Text, tone);
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::PopStyleColor();
  }
  if (phase == UpdateWork::Phase::Downloading && up_.total > 0) {
    ImGui::ProgressBar(static_cast<float>(static_cast<double>(up_.done) / static_cast<double>(up_.total)),
                       ImVec2(-1, 0));
  }
  if (layout_.valid) {
    const bool another = layout_.current != layout_.running;
    if (another) {
      const std::string next = FolderVersionOf(layout_.current);
      const std::string line = !versionNote_.empty() ? versionNote_
                                                     : "Version " + (next.empty() ? layout_.current : next) +
                                                           " starts the next time you open " + game_.title + ".";
      ImGui::PushStyleColor(ImGuiCol_Text, kGood);
      ImGui::TextWrapped("%s", line.c_str());
      ImGui::PopStyleColor();
      if (!gameRunning_ && ImGui::Button("Restart now")) {
        RestartLauncher();
      }
    } else if (!versionNote_.empty()) {
      Dim(versionNote_);
    }
    if (!layout_.previous.empty() && eu::FolderUsable(layout_.root / PathOf(layout_.previous), u.requiredFiles)) {
      const std::string v = FolderVersionOf(layout_.previous);
      const std::string label = eu::IsNewer(v, FolderVersionOf(layout_.current))
                                    ? "Use version " + v + " again..."
                                    : "Go back to the previous version (" + v + ")...";
      if (busy) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button(label.c_str())) {
        openGoBack_ = true;
      }
      if (busy) {
        ImGui::EndDisabled();
      }
    }
  } else if (!layout_.why.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kFaint);
    ImGui::TextWrapped("%s", layout_.why.c_str());
    ImGui::PopStyleColor();
  }
  EndCard();
}

void App::DrawUpdateWindows() {
  const GameInfo::Updates& u = game_.updates;
  if (!u.Active()) {
    return;
  }
  const float wrap = 560 * scale_;
  if (askUpdates_) {
    ImGui::OpenPopup("Updates##ask");
    askUpdates_ = false;
  }
  if (ImGui::BeginPopupModal("Updates##ask", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    Heading("Check for updates when the launcher starts?");
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(("If you say yes, the launcher looks for a newer version of " + game_.title +
                            " each time it starts. It only looks: when there is one, it shows what's new and asks you "
                            "before downloading anything. If you say no, it never goes online by itself.")
                               .c_str());
    Dim("You can change this any time on the Disc page (\"Version and updates\"), where \"Check now\" also works.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const auto answer = [&](bool yes) {
      registry_.SetBool(kUpdateSetting, yes);
      SaveSettingsIfChanged();
      updatesAsked_ = true;
      SaveUpdaterState();
      Log(std::string("updates: check at start: ") + (yes ? "yes" : "no"));
      ImGui::CloseCurrentPopup();
      if (yes) {
        StartUpdateCheck(false);
      }
    };
    if (PrimaryButton("Yes, check at start")) {
      answer(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) {
      answer(false);
    }
    ImGui::EndPopup();
  }

  if (openUpdates_) {
    ImGui::OpenPopup("Update##offer");
    openUpdates_ = false;
  }
  if (ImGui::BeginPopupModal("Update##offer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    UpdateWork::Phase phase;
    eu::Manifest m;
    std::string message;
    {
      std::lock_guard<std::mutex> lock(up_.mutex);
      phase = up_.phase;
      m = up_.manifest;
      message = up_.message;
    }
    ImGui::PushTextWrapPos(wrap);
    if (phase == UpdateWork::Phase::Available) {
      Heading(("Version " + m.version + " is available").c_str());
      Dim("You have version " + NewestInstalled() + (m.date.empty() ? "" : ". Published " + m.date) + ". Download: " +
          MegaBytes(m.size) + ".");
      if (!m.notes.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("What's new:");
        ImGui::BeginChild("##notes", ImVec2(wrap, std::min(220 * scale_, ImGui::GetTextLineHeightWithSpacing() * 12)),
                          ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(m.notes.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
      }
      if (m.version == skipVersion_) {
        Dim("You chose to skip this version earlier.");
      }
      Dim("Installed next to the version you have, which stays for \"Go back\". Your saves, settings and mods are "
          "not touched.");
      if (!layout_.valid) {
        ImGui::TextColored(kWarn, "%s", layout_.why.c_str());
      }
      ImGui::Spacing();
      if (!layout_.valid || gameRunning_) {
        ImGui::BeginDisabled();
      }
      if (PrimaryButton("Update now")) {
        StartUpdateInstall();
      }
      if (!layout_.valid || gameRunning_) {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (ImGui::Button("Not now")) {
        Log("updates: not now");
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Skip this version")) {
        SkipUpdate();
        ImGui::CloseCurrentPopup();
      }
    } else if (phase == UpdateWork::Phase::Downloading) {
      Heading(("Updating to version " + m.version).c_str());
      ImGui::TextUnformatted(message.c_str());
      const uint64_t total = up_.total, done = up_.done;
      ImGui::ProgressBar(total > 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f,
                         ImVec2(wrap, 0), (MegaBytes(done) + " of " + MegaBytes(total)).c_str());
      if (ImGui::Button("Cancel")) {
        up_.stop = true;
      }
    } else {
      Heading(phase == UpdateWork::Phase::Installed ? "Update installed" : "The update was not installed");
      ImGui::PushStyleColor(ImGuiCol_Text, phase == UpdateWork::Phase::Failed ? kWarn : kText);
      ImGui::TextWrapped("%s", message.c_str());
      ImGui::PopStyleColor();
      ImGui::Spacing();
      if (phase == UpdateWork::Phase::Installed && layout_.valid && !gameRunning_) {
        if (PrimaryButton("Restart now")) {
          ImGui::CloseCurrentPopup();
          RestartLauncher();
        }
        ImGui::SameLine();
      }
      if (ImGui::Button(phase == UpdateWork::Phase::Installed ? "Later" : "OK")) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openGoBack_) {
    ImGui::OpenPopup("Version##back");
    openGoBack_ = false;
  }
  if (ImGui::BeginPopupModal("Version##back", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    const std::string v = FolderVersionOf(layout_.previous);
    Heading(("Start version " + v + " next time?").c_str());
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(("Version " + v + " starts the next time you open " + game_.title + ". Version " +
                            FolderVersionOf(layout_.current) + " stays installed, so you can switch back the same "
                            "way. Your saves, settings and mods stay as they are.")
                               .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(("Use version " + v).c_str())) {
      std::string error;
      if (!GoBackVersion(error)) {
        versionNote_ = "That didn't work: " + error + ".";
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  closeWindows_ = false;
}

bool App::DoStep(const std::string& step) {
  const size_t colon = step.find(':');
  const std::string verb = Lower(step.substr(0, colon));
  const std::string arg = colon == std::string::npos ? std::string() : step.substr(colon + 1);
  Log("step: " + step);
  if (verb == "disc") {
    SelectDisc(arg);
  } else if (verb == "wait") {
    waitFor_ = Lower(arg);
  } else if (verb == "page") {
    for (int p = 0; p < static_cast<int>(Page::Count); p++) {
      if (Lower(arg) == kPageIds[p]) {
        page_ = static_cast<Page>(p);
        return true;
      }
    }
    Log("no such page: " + arg);
    return false;
  } else if (verb == "extradisc" || verb == "extramake" || verb == "extracancel") {
    page_ = Page::Extra;
    int index = 0;
    std::string rest = arg;
    const size_t eq = arg.find('=');
    const std::string name = verb == "extradisc" ? (eq == std::string::npos ? std::string() : arg.substr(0, eq)) : arg;
    if (!name.empty()) {
      const auto* found = extra::FindSource(game_.extra.sources, name);
      if (found != nullptr) {
        index = static_cast<int>(found - game_.extra.sources.data());
        if (verb == "extradisc") rest = arg.substr(eq + 1);
      } else if (verb != "extradisc") {
        Log("no such extra source: " + name);
        return false;
      }
    }
    if (extras_.empty()) {
      Log("no extra assets in this launcher");
      return false;
    }
    if (verb == "extradisc") {
      ExtraSelectDisc(index, rest);
    } else if (verb == "extramake") {
      ExtraMake(index);
      return extras_[index]->note.empty();
    } else {
      ExtraCancel(index);
    }
  } else if (verb == "tab") {
    page_ = Page::Settings;
    wantTab_ = Lower(arg);
  } else if (verb == "preset") {
    es::Preset preset;
    if (!es::ParsePreset(arg, preset)) {
      Log("no such preset: " + arg);
      return false;
    }
    registry_.ApplyPreset(preset);
  } else if (verb == "set") {
    const size_t eq = arg.find('=');
    if (eq == std::string::npos || !registry_.SetFromText(arg.substr(0, eq), arg.substr(eq + 1))) {
      Log("refused: " + arg);
      return false;
    }
  } else if (verb == "mod") {
    const size_t eq = arg.find('=');
    const std::string id = arg.substr(0, eq);
    for (auto& [modId, on] : mods_.Active().mods) {
      if (Lower(modId) == Lower(id)) {
        on = eq != std::string::npos && Lower(arg.substr(eq + 1)) == "on";
        SaveMods();
        return true;
      }
    }
    Log("no such mod in the list: " + id);
    return false;
  } else if (verb == "key") {
    const size_t eq = arg.find('=');
    const auto input = engine::controls::GcInputFromId(arg.substr(0, eq));
    const auto action = engine::controls::ActionFromId(arg.substr(0, eq));
    if (eq == std::string::npos || (!input && !action)) {
      Log("expected key:<button>=<scancode>");
      return false;
    }
    page_ = Page::Controls;
    if (action) {
      controls_.StartListeningAction(controls_.Port(), static_cast<size_t>(*action), true, false);
    } else {
      controls_.StartListening(controls_.Port(), static_cast<size_t>(*input), true, false);
    }
    controls_.FeedKey(registry_, static_cast<uint16_t>(std::atoi(arg.substr(eq + 1).c_str())));
  } else if (verb == "padbutton") {
    const size_t eq = arg.find('=');
    const auto input = engine::controls::GcInputFromId(arg.substr(0, eq));
    const auto action = engine::controls::ActionFromId(arg.substr(0, eq));
    const auto button = eq == std::string::npos ? std::nullopt : engine::controls::ParsePadToken(arg.substr(eq + 1));
    if ((!input && !action) || !button || button->kind != engine::controls::Input::Kind::Button) {
      Log("expected padbutton:<button>=<controller button>");
      return false;
    }
    page_ = Page::Controls;
    if (action) {
      controls_.StartListeningAction(controls_.Port(), static_cast<size_t>(*action), false, false);
    } else {
      controls_.StartListening(controls_.Port(), static_cast<size_t>(*input), false, false);
    }
    controls_.FeedPadButton(registry_, button->code);
  } else if (verb == "port") {
    const int port = std::atoi(arg.c_str());
    if (port < 1 || port > 4) {
      Log("expected port:1..4");
      return false;
    }
    page_ = Page::Controls;
    controls_.SetPort(port - 1);
  } else if (verb == "testkeys") {
    fakeKeys_.clear();
    for (const std::string& name : Split(arg, '+')) {
      if (name.empty()) {
        continue;
      }
      const auto key = engine::controls::ParseKeyToken(name);
      if (!key) {
        Log("no such key: " + name);
        return false;
      }
      fakeKeys_.push_back(key->code);
    }
    page_ = Page::Controls;
    controls_.SetTesting(!fakeKeys_.empty());
  } else if (verb == "size") {
    const size_t x = arg.find('x');
    const int w = std::atoi(arg.c_str()), h = x == std::string::npos ? 0 : std::atoi(arg.c_str() + x + 1);
    if (w < 400 || h < 300) {
      Log("expected size:<width>x<height>");
      return false;
    }
    SDL_SetWindowSize(window_, static_cast<int>(w * scale_), static_cast<int>(h * scale_));
  } else if (verb == "report") {
    page_ = Page::Reports;
    LookAtReports(true);
    const int n = std::atoi(arg.c_str());
    for (size_t i = 0; i < reports_.size(); i++) {
      if ((n > 0 && static_cast<size_t>(n) == i + 1) || Lower(reports_[i].name) == Lower(arg)) {
        reportSel_ = reports_[i].name;
        return true;
      }
    }
    Log("no such report: " + arg);
    return false;
  } else if (verb == "exportoption") {
    const size_t eq = arg.find('=');
    const std::string what = Lower(arg.substr(0, eq));
    const bool on = eq != std::string::npos && Lower(arg.substr(eq + 1)) == "on";
    if (what == "name") {
      exportOptions_.hideUserName = on;
    } else if (what == "dump") {
      exportOptions_.includeDump = on;
    } else {
      Log("expected exportoption:name=on|off or exportoption:dump=on|off");
      return false;
    }
  } else if (verb == "export") {
    ExportReport(arg);
    Log(reportMessage_);
  } else if (verb == "deletereport") {
    DeleteReport();
    Log(reportMessage_);
  } else if (verb == "session") {
    page_ = Page::Reports;
    LookAtSessions();
    const int n = arg.empty() ? 1 : std::atoi(arg.c_str());
    if (n < 1 || static_cast<size_t>(n) > sessions_.size()) {
      Log("no such session: " + arg + " (" + std::to_string(sessions_.size()) + " found)");
      return false;
    }
    sessionSel_ = static_cast<size_t>(n - 1);
    sessionOpen_ = true;
    Log("session " + std::to_string(n) + ": " + sessions_[sessionSel_].id);
  } else if (verb == "savesession") {
    SaveSession(arg);
    Log(sessionMessage_);
  } else if (verb == "backup") {
    page_ = Page::Save;
    if (SlotsOn()) {
      LookAtSlots(true);
      if (OpenSlot() == nullptr) {
        Log("backup: no save is open (openslot:<n> first)");
        return false;
      }
      backupsShown_ = true;
    }
    LookAtBackups(true);
    const int n = std::atoi(arg.c_str());
    int i = 0;
    for (const bk::Backup& b : backups_) {
      for (const bk::File& f : b.files) {
        if (++i == n) {
          backupSel_ = b.card + "/" + b.name + "/" + f.name;
          Log("backup " + std::to_string(n) + ": " + backupSel_);
          return true;
        }
      }
    }
    Log("no such backup: " + arg);
    return false;
  } else if (verb == "slot" || verb == "openslot" || verb == "showbackups" || verb == "newslot" ||
             verb == "renameslot" || verb == "dupslot" || verb == "deleteslot" || verb == "importslot" ||
             verb == "exportslot") {
    page_ = Page::Save;
    if (!SlotsOn()) {
      Log("saves: this run has one memory card folder only (no list of saves)");
      return false;
    }
    if (verb == "slot" || verb == "openslot") {
      LookAtSlots(true);
      if (verb == "openslot" && Lower(arg) == "none") {
        OpenSlotIs("");
        return true;
      }
      const sl::Slot* slot = SlotByArg(arg);
      if (slot == nullptr) {
        Log("no such save: " + arg);
        return false;
      }
      const std::string id = slot->id;
      if (verb == "slot") {
        return ChooseSlot(id);
      }
      OpenSlotIs(openSlot_ == id ? std::string() : id);
      Log("saves: " + (OpenSlot() != nullptr ? "opened \"" + OpenSlot()->name + "\"" : std::string("closed the open save")) +
          "; the game loads \"" + slots_.Active().name + "\"");
      return true;
    }
    if (verb == "showbackups") {
      if (OpenSlot() == nullptr) {
        Log("showbackups: no save is open (openslot:<n> first)");
        return false;
      }
      backupsShown_ = Lower(arg) != "off";
      return true;
    }
    return verb == "newslot"      ? NewSlot(arg)
           : verb == "renameslot" ? RenameSlot(arg)
           : verb == "dupslot"    ? DuplicateSlot()
           : verb == "deleteslot" ? DeleteSlot()
           : verb == "importslot" ? ImportSlot(arg)
                                  : ExportSlot(arg);
  } else if (verb == "copycode") {
    page_ = Page::Save;
    std::string code;
    {
      SaveSummary shown;
      code = OpenSummary(shown) ? shown.shareCode : std::string();
    }
    if (code.empty()) {
      Log("copycode: this save has no code to copy");
      return false;
    }
    CopyCode(code);
    char* clip = SDL_GetClipboardText();
    Log(std::string("clipboard: ") + (clip != nullptr ? clip : ""));
    SDL_free(clip);
  } else if (verb == "spoilers") {
    page_ = Page::Save;
    if (Lower(arg) == "close") {
      closeSpoilers_ = true;
    } else {
      askSpoilers_ = true;
    }
  } else if (verb == "openfolder") {
    const std::string which = Lower(arg);
    if (which == "close") {
      closeWindows_ = true;
    } else {
      OpenInSystem(which == "mods" ? game_.modsFolder : which == "reports" ? game_.reportsFolder : BackupsFolder());
    }
  } else if (verb == "restorebackup") {
    page_ = Page::Save;
    RestoreBackup();
    Log(backupMessage_);
    return backupOk_;
  } else if (verb == "shortcut") {
    Shortcut(Lower(arg) == "desktop");
  } else if (verb == "update") {
    const std::string what = Lower(arg);
    if (what == "check") {
      StartUpdateCheck(true);
    } else if (what == "now") {
      StartUpdateInstall();
    } else if (what == "skip") {
      SkipUpdate();
      closeWindows_ = true;
    } else if (what == "notnow") {
      closeWindows_ = true;
    } else if (what == "back") {
      std::string error;
      GoBackVersion(error);
    } else if (what == "yes" || what == "no") {
      registry_.SetBool(kUpdateSetting, what == "yes");
      SaveSettingsIfChanged();
      updatesAsked_ = true;
      SaveUpdaterState();
      closeWindows_ = true;
      Log("updates: check at start: " + what);
      if (what == "yes") {
        StartUpdateCheck(false);
      }
    } else if (what == "restart") {
      RestartLauncher();
    } else {
      Log("expected update:check|now|notnow|skip|back|yes|no|restart");
      return false;
    }
  } else if (verb == "shot") {
    shotFile_ = arg;
  } else if (verb == "play") {
    Play(true);
  } else if (verb == "quit") {
    quit_ = true;
  } else {
    Log("unknown step: " + step);
    return false;
  }
  return true;
}

void App::RunScript() {
  if (step_ >= script_.size() && waitFor_.empty()) {
    return;
  }
  if (!waitFor_.empty()) {
    if (waitFor_ == "disc" && (bg_.busy || DiscPath().empty())) {
      return;
    }
    if (waitFor_ == "save" && (bg_.busy || !bg_.haveSave)) {
      return;
    }
    if (waitFor_ == "save") {
      SaveSummary shown;
      if (!OpenSummary(shown) || openRead_.busy || openReadAgain_) {
        return;
      }
    }
    if (waitFor_ == "pictures" && (bg_.drawing || iconShown_ != bg_.iconVersion)) {
      return;
    }
    if (waitFor_ == "shaders" && (bg_.busy || bg_.preparing)) {
      return;
    }
    if (waitFor_ == "extra" && ExtraBusy()) {
      return;
    }
    if (waitFor_ == "extradisc" &&
        std::any_of(extras_.begin(), extras_.end(), [](const auto& e) { return e->check.state() == extra::DiscCheck::State::Checking; })) {
      return;
    }
    if (waitFor_ == "game" && (gameRunning_ || playWhenDrawn_)) {
      return;
    }
    if (waitFor_ == "update" && (up_.busy || up_.serial != upSeen_)) {
      return;
    }
    if (waitFor_ == "icon" && (bg_.busy || iconShown_ != bg_.iconVersion || registry_.Text(kIconSetting) != iconApplied_)) {
      return;
    }
    Log("waited for " + waitFor_);
    waitFor_.clear();
    framesSinceStep_ = 0;
  }
  if (++framesSinceStep_ < 4 || !shotFile_.empty() || step_ >= script_.size()) {
    return;
  }
  framesSinceStep_ = 0;
  DoStep(script_[step_++]);
  if (step_ >= script_.size() && waitFor_.empty() && !gameRunning_ && shotFile_.empty()) {
    Log("script done");
  }
}

void App::Screenshot(const std::string& file) {
  SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
  if (shot == nullptr) {
    Log("screenshot failed: " + std::string(SDL_GetError()));
    return;
  }
  std::error_code ec;
  fs::create_directories(PathOf(file).parent_path(), ec);
  Log(SDL_SavePNG(shot, file.c_str()) ? "screenshot " + file : "screenshot failed: " + std::string(SDL_GetError()));
  SDL_DestroySurface(shot);
}

void App::DrawHeader() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float height = 84 * scale_;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 a = vp->WorkPos, b(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + height);
  const float band = GroundIndex() >= 0 && !groundTex_.empty() ? 0.35f : 1.0f;
  dl->AddRectFilledMultiColor(a, b, U32(Hex(0x2A1450), band), U32(kGround, band), U32(kGround, band),
                              U32(Hex(0x1E0F38), band));
  dl->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), U32(kLine));
  dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - 2 * scale_), ImVec2(a.x + vp->WorkSize.x * 0.5f, b.y),
                              U32(kAccent, 0.9f), U32(kAccent, 0.0f), U32(kAccent, 0.0f), U32(kAccent, 0.9f));

  const float pad = ImGui::GetStyle().WindowPadding.x;
  const float iconSize = 52 * scale_;
  float x = a.x + pad;
  if (ownIcon_ != nullptr) {
    const ImVec2 ip(x, a.y + (height - iconSize) * 0.5f);
    dl->AddImage(TexId(ownIcon_), ip, ImVec2(ip.x + iconSize, ip.y + iconSize));
    x += iconSize + 16 * scale_;
  }
  const float titleH = fonts_.title->FontSize, subH = fonts_.caption->FontSize;
  const float top = a.y + (height - titleH - subH - 2 * scale_) * 0.5f;
  dl->AddText(fonts_.title, titleH, ImVec2(x, top), U32(kText), game_.title.c_str());
  dl->AddText(fonts_.caption, subH, ImVec2(x + 1, top + titleH + 2 * scale_), U32(kDim), game_.subtitle.c_str());

  const std::string pill = std::string(ui::PresetName(registry_.CurrentPreset())) + " preset";
  const ImVec2 ts = fonts_.nav->CalcTextSizeA(fonts_.nav->FontSize, FLT_MAX, 0, pill.c_str());
  const float ph = ts.y + 10 * scale_, pw = ts.x + 26 * scale_;
  const ImVec2 pp(b.x - pad - pw, a.y + (height - ph) * 0.5f);
  const bool original = registry_.IsOriginal();
  const ImVec4 tone = original ? kGood : kAccentHover;
  dl->AddRectFilled(pp, ImVec2(pp.x + pw, pp.y + ph), U32(tone, 0.14f), ph * 0.5f);
  dl->AddRect(pp, ImVec2(pp.x + pw, pp.y + ph), U32(tone, 0.55f), ph * 0.5f, 0, 1.0f * scale_);
  dl->AddCircleFilled(ImVec2(pp.x + 12 * scale_, pp.y + ph * 0.5f), 3.5f * scale_, U32(tone));
  dl->AddText(fonts_.nav, fonts_.nav->FontSize, ImVec2(pp.x + 20 * scale_, pp.y + 5 * scale_), U32(tone),
              pill.c_str());

  ImGui::SetCursorScreenPos(ImVec2(a.x + pad, b.y + 14 * scale_));
}

void App::DrawNav(float height) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * scale_, 12 * scale_));
  ImGui::BeginChild("##nav", ImVec2(186 * scale_, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rowH = 44 * scale_, glyph = 18 * scale_;
  for (int p = 0; p < static_cast<int>(Page::Count); p++) {
    if (static_cast<Page>(p) == Page::Reports && game_.reportsFolder.empty()) {
      continue;
    }
    if (static_cast<Page>(p) == Page::Extra && game_.extra.sources.empty()) {
      continue;
    }
    ImGui::PushID(p);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool selected = page_ == static_cast<Page>(p);
    if (ImGui::InvisibleButton("##page", ImVec2(w, rowH))) {
      page_ = static_cast<Page>(p);
      controls_.Cancel();
    }
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 end(pos.x + w, pos.y + rowH);
    if (selected) {
      dl->AddRectFilled(pos, end, U32(Hex(0x2E2350)), 8 * scale_);
      dl->AddRectFilled(ImVec2(pos.x, pos.y + 9 * scale_), ImVec2(pos.x + 3.5f * scale_, end.y - 9 * scale_),
                        U32(kAccentHover), 2 * scale_);
    } else if (hovered) {
      dl->AddRectFilled(pos, end, U32(Hex(0x241B39)), 8 * scale_);
    }
    const ImU32 ink = U32(selected ? kText : hovered ? Hex(0xD9D2EC) : kDim);
    const ImU32 glyphInk = U32(selected ? kAccentHover : hovered ? Hex(0xC5B5EC) : kFaint);
    DrawGlyph(dl, GlyphOf(p), ImVec2(pos.x + 14 * scale_, pos.y + (rowH - glyph) * 0.5f), glyph, glyphInk);
    dl->AddText(fonts_.nav, fonts_.nav->FontSize,
                ImVec2(pos.x + 14 * scale_ + glyph + 13 * scale_, pos.y + (rowH - fonts_.nav->FontSize) * 0.5f), ink,
                kPageNames[p]);
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

namespace {
std::string HumanBytes(uint64_t bytes) {
  char text[48];
  if (bytes >= (1ull << 30)) {
    std::snprintf(text, sizeof text, "%.1f GB", static_cast<double>(bytes) / static_cast<double>(1ull << 30));
  } else if (bytes >= (1ull << 20)) {
    std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / static_cast<double>(1ull << 20));
  } else {
    std::snprintf(text, sizeof text, "%llu KB", static_cast<unsigned long long>((bytes + 1023) / 1024));
  }
  return text;
}
}

void App::ExtraStart() {
  const auto& sources = game_.extra.sources;
  for (int i = 0; i < static_cast<int>(sources.size()); i++) {
    extras_.push_back(std::make_unique<ExtraState>());
    {
      const fs::path oldMod = game_.modsFolder / PathOf(sources[i].manifestId);
      std::error_code ec;
      if (!game_.playerFilesReadOnly && !sources[i].manifestId.empty() && fs::is_regular_file(oldMod / "mod.json", ec) &&
          !fs::exists(ExtraFolder(sources[i]), ec)) {
        fs::create_directories(game_.settingsFolder / "extra_assets", ec);
        fs::rename(oldMod, ExtraFolder(sources[i]), ec);
        Log("extra: " + sources[i].id + ": moved the old mod folder " + Utf8(oldMod) + " to " + Utf8(ExtraFolder(sources[i])) +
            (ec ? " (failed: " + ec.message() + ")" : ""));
        if (!ec) {
          for (em::ModList::Profile& profile : mods_.profiles) {
            auto& list = profile.mods;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [&](const auto& m) { return Lower(m.first) == Lower(sources[i].manifestId); }),
                       list.end());
          }
          SaveMods();
        }
        ReloadMods();
      }
    }
    const std::string disc = registry_.Text(sources[i].discSetting);
    if (!disc.empty() && game_.extra.identifyDisc) {
      extras_[i]->check.Start(sources[i], disc, game_.extra);
    }
    ExtraLook(i);
  }
}

void App::ExtraStop() {
  for (auto& st : extras_) {
    st->job.Cancel();
    if (st->looker.joinable()) st->looker.join();
  }
  extras_.clear();
}

void App::ExtraLook(int index) {
  ExtraState& st = *extras_[index];
  if (st.looking) {
    st.lookAgain = true;
    return;
  }
  if (st.looker.joinable()) st.looker.join();
  st.looking = true;
  st.lookAgain = false;
  st.looker = std::thread([this, index] {
    ExtraState& state = *extras_[index];
    const GameInfo::ExtraSource& src = game_.extra.sources[index];
    const fs::path converter = extra::ConverterPath(src, game_.program.parent_path());
    std::error_code ec;
    const bool found = fs::is_regular_file(converter, ec);
    const std::string version =
        found ? extra::QueryVersion(converter, game_.settingsFolder / "extra_assets" / PathOf(src.id + "_version.txt"), 5000)
              : std::string();
    const extra::Status status = extra::ComputeStatus(ExtraFolder(src), src.manifestId, found, version);
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.status = status;
      state.converterFound = found;
      state.looked = true;
    }
    Log("extra: " + src.id + ": " + status.text + (found ? "" : " (no converter at " + Utf8(converter) + ")") +
        (version.empty() ? std::string() : " (converter " + version + ")"));
    state.looking = false;
  });
}

void App::ExtraSelectDisc(int index, const std::string& path) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  const GameInfo::ExtraSource& src = game_.extra.sources[index];
  Log("extra disc (" + src.id + "): " + path);
  if (!registry_.SetText(src.discSetting, path)) {
    playError_ = "This file name can't be used: " + path;
    return;
  }
  SaveSettingsIfChanged();
  extras_[index]->note.clear();
  extras_[index]->check.Start(src, path, game_.extra);
}

void App::ExtraMake(int index) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  ExtraState& st = *extras_[index];
  const GameInfo::ExtraSource& src = game_.extra.sources[index];
  if (st.job.Running()) return;
  const std::string gameDisc = DiscPath(), disc = registry_.Text(src.discSetting);
  std::string why;
  fs::path converter;
  {
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.looked) why = "still looking at what is installed";
    else if (!st.converterFound) why = "the converter isn't next to the game";
    converter = extra::ConverterPath(src, game_.program.parent_path());
  }
  if (why.empty() && gameDisc.empty()) why = "choose your own game disc on the Disc page first";
  if (why.empty() && disc.empty()) why = "choose the " + src.name + " disc first";
  if (why.empty() && st.check.state() != extra::DiscCheck::State::Good) why = "that disc isn't the right game (or is still being looked at)";
  if (why.empty() && game_.playerFilesReadOnly) why = "this is a test run on the player's own files, which it never changes";
  if (!why.empty()) {
    st.note = "Can't start: " + why + ".";
    Log("extra: " + st.note);
    return;
  }
  extra::MakeRequest request;
  request.converter = converter;
  request.outFolder = ExtraFolder(src);
  request.logFile = game_.settingsFolder / "extra_assets" / PathOf(src.id + ".log");
  request.arguments = extra::ConverterArguments(src, disc, gameDisc, request.outFolder);
  std::string error;
  st.note.clear();
  if (!st.job.Start(request, error)) {
    st.note = error;
    Log("extra: " + error);
    return;
  }
  st.wasRunning = true;
  st.finishedShown = false;
  Log("extra: " + src.id + ": started " + Utf8(converter));
}

void App::ExtraCancel(int index) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  if (extras_[index]->job.Running()) {
    Log("extra: " + game_.extra.sources[index].id + ": cancel");
    extras_[index]->job.Cancel();
  }
}

void App::ExtraPoll() {
  for (int i = 0; i < static_cast<int>(extras_.size()); i++) {
    ExtraState& st = *extras_[i];
    if (st.wasRunning && !st.job.Running()) {
      st.wasRunning = false;
      st.last = st.job.Get();
      st.finishedShown = true;
      static const char* const kPhase[] = {"idle", "running", "done", "failed", "wrong disc", "cancelled"};
      Log(std::string("extra: ") + game_.extra.sources[i].id + ": " + kPhase[static_cast<int>(st.last.phase)] +
          (st.last.message.empty() ? std::string() : " (" + st.last.message + ")") +
          (st.last.phase == extra::MakeJob::Phase::Done
               ? " (" + std::to_string(st.last.converted) + " made, " + std::to_string(st.last.keptOriginal) +
                     " kept, " + std::to_string(st.last.totalBytes) + " bytes)"
               : std::string()));
      ExtraLook(i);
    }
    if (st.lookAgain && !st.looking) ExtraLook(i);
  }
}

bool App::ExtraBusy() const {
  for (const auto& st : extras_) {
    if (st->job.Running() || st->wasRunning || st->looking || st->lookAgain) return true;
  }
  return false;
}

void App::DrawExtraPage() {
  PageTitle("Extra assets",
            "Discs of other games that this port can borrow assets from. Your discs are only read; what is made from "
            "them is made on this PC, kept in your own data folder (extra_assets) and never sent anywhere.");
  const auto& sources = game_.extra.sources;
  for (int i = 0; i < static_cast<int>(sources.size()) && i < static_cast<int>(extras_.size()); i++) {
    ExtraState& st = *extras_[i];
    const GameInfo::ExtraSource& src = sources[i];
    ImGui::PushID(i);
    BeginCard("##extra");
    extra::Status status;
    bool looked, converterFound;
    {
      std::lock_guard<std::mutex> lock(st.mutex);
      status = st.status;
      looked = st.looked;
      converterFound = st.converterFound;
    }
    Heading(src.provides.c_str());
    if (!looked) {
      ImGui::TextColored(kBusy, "Looking...");
    } else {
      const ImVec4 colour = status.kind == extra::Status::Kind::Ready       ? kGood
                            : status.kind == extra::Status::Kind::OutOfDate ? kWarn
                            : status.kind == extra::Status::Kind::ConverterMissing ? kBad
                                                                                   : kDim;
      ImGui::TextColored(colour, "%s", status.text.c_str());
      if (!status.detail.empty()) Dim(status.detail);
    }
    Dim("From a " + src.name + " disc. " + src.how);
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    std::string disc = registry_.Text(src.discSetting);
    ImGui::TextColored(kDim, "%s disc image", src.name.c_str());
    ImGui::SetNextItemWidth(-250 * scale_);
    ImGui::InputText("##disc", &disc, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Choose disc...")) {
      static const SDL_DialogFileFilter kFilters[] = {
          {"Disc images (ISO, RVZ, WBFS, CISO, GCZ...)", "iso;gcm;rvz;wia;gcz;ciso;wbfs"},
          {"All files", "*"},
      };
      const std::string start = disc.empty() ? std::string() : Utf8(PathOf(disc).parent_path());
      extraPickedFor_ = i;
      SDL_ShowOpenFileDialog(
          [](void* user, const char* const* files, int) {
            App* app = static_cast<App*>(user);
            if (files != nullptr && files[0] != nullptr) {
              std::lock_guard<std::mutex> lock(app->pickMutex_);
              app->extraPicked_ = files[0];
              app->extraPickedReady_ = true;
            }
          },
          this, window_, kFilters, 2, start.empty() ? nullptr : start.c_str(), false);
    }
    switch (st.check.state()) {
    case extra::DiscCheck::State::Checking: ImGui::TextColored(kBusy, "Looking at the disc image..."); break;
    case extra::DiscCheck::State::Good:
    case extra::DiscCheck::State::WrongGame:
    case extra::DiscCheck::State::Unreadable:
      ImGui::PushStyleColor(ImGuiCol_Text, st.check.state() == extra::DiscCheck::State::Good ? kGood : kBad);
      ImGui::TextWrapped("%s", st.check.message().c_str());
      ImGui::PopStyleColor();
      break;
    case extra::DiscCheck::State::None:
      if (disc.empty()) Dim("No disc chosen yet.");
      break;
    }
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    const bool running = st.job.Running();
    if (!running) {
      if (ImGui::Button(src.makeLabel.c_str())) ExtraMake(i);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Needs your own game disc (Disc page) and this disc.");
    } else {
      if (ImGui::Button("Cancel")) ExtraCancel(i);
    }
    if (running) {
      const extra::MakeJob::Snapshot snap = st.job.Get();
      const float fraction = snap.total > 0 ? static_cast<float>(static_cast<double>(snap.done) / static_cast<double>(snap.total)) : 0.0f;
      char label[160];
      std::snprintf(label, sizeof label, snap.total > 0 ? "%llu / %llu  %s" : "starting...",
                    static_cast<unsigned long long>(snap.done), static_cast<unsigned long long>(snap.total),
                    snap.current.c_str());
      ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
      Dim("This can take a while. The window stays usable; Cancel stops it and leaves nothing half-made.");
    } else if (st.finishedShown) {
      const extra::MakeJob::Snapshot& r = st.last;
      if (r.phase == extra::MakeJob::Phase::Done) {
        ImGui::TextColored(kGood, "Done: %llu made, %llu kept as the game's own (%s). Turn it on with the setting below.",
                           static_cast<unsigned long long>(r.converted), static_cast<unsigned long long>(r.keptOriginal),
                           HumanBytes(r.totalBytes).c_str());
      } else if (r.phase == extra::MakeJob::Phase::Cancelled) {
        ImGui::TextColored(kWarn, "%s", r.message.c_str());
      } else {
        ImGui::TextColored(kBad, "%s", r.message.c_str());
      }
    }
    if (!st.note.empty()) ImGui::TextColored(kWarn, "%s", st.note.c_str());

    if (looked && !src.switchSetting.empty() && registry_.Find(src.switchSetting) != es::kNoSetting &&
        (status.kind == extra::Status::Kind::Ready || status.kind == extra::Status::Kind::OutOfDate)) {
      ImGui::Dummy(ImVec2(0, 4 * scale_));
      bool on = registry_.Bool(src.switchSetting).Get();
      const es::Definition& def = registry_.Get(registry_.Find(src.switchSetting));
      if (ImGui::Checkbox(("Use them in the game (setting: " + def.name + ")").c_str(), &on)) {
        registry_.SetBool(src.switchSetting, on);
        SaveSettingsIfChanged();
      }
      Dim("The same switch is in Settings and in the game's SETTINGS page. It is an improvement, so it is not part of "
          "Original: turning it on here makes the preset Custom. The game reads it when it starts.");
    }
    if (ImGui::SmallButton("Look again")) {
      ExtraLook(i);
      if (!disc.empty()) st.check.Start(src, disc, game_.extra);
    }
    (void)converterFound;
    EndCard();
    ImGui::PopID();
    ImGui::Spacing();
  }
  Dim("More discs (for example Pokemon Colosseum) can be added to this page later.");
}

void App::DrawDiscPage() {
  PageTitle("Your disc", game_.discHelp);
  std::string path = DiscPath();
  ImGui::TextColored(kDim, "Disc image file");
  ImGui::SetNextItemWidth(-250 * scale_);
  ImGui::InputText("##disc", &path, ImGuiInputTextFlags_ReadOnly);
  ImGui::SameLine();
  if (ImGui::Button("Choose...")) {
    static const SDL_DialogFileFilter kFilters[] = {
        {"Disc images (ISO, RVZ, GCM, WIA, GCZ, CISO, WBFS)", "iso;gcm;rvz;wia;gcz;ciso;wbfs"},
        {"All files", "*"},
    };
    const std::string current = DiscPath();
    const std::string start = current.empty() ? std::string() : Utf8(PathOf(current).parent_path());
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
          App* app = static_cast<App*>(user);
          if (files != nullptr && files[0] != nullptr) {
            std::lock_guard<std::mutex> lock(app->pickMutex_);
            app->picked_ = files[0];
            app->pickedReady_ = true;
          }
        },
        this, window_, kFilters, 2, start.empty() ? nullptr : start.c_str(), false);
  }
  ImGui::SameLine();
  if (path.empty()) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Check again")) {
    StartWorker(path);
  }
  if (path.empty()) {
    ImGui::EndDisabled();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));

  BeginCard("##status");
  if (path.empty()) {
    ImGui::TextColored(kWarn, "No disc chosen yet");
    Dim("Click \"Choose...\" and pick your disc image file. The launcher remembers it for next time.");
  } else {
    bool have;
    DiscVerdict v;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      have = bg_.haveVerdict && bg_.discPath == path;
      v = bg_.verdict;
    }
    if (!have) {
      ImGui::TextColored(kBusy, "Checking your disc image...");
      const uint64_t total = bg_.total, done = bg_.done;
      if (total > 0) {
        Dim("The first time a disc image is used, all of it is read to make sure it is complete and "
            "unchanged. This takes a few seconds; after that it is remembered.");
        ImGui::ProgressBar(static_cast<float>(static_cast<double>(done) / static_cast<double>(total)), ImVec2(-1, 0));
      }
    } else {
      ImGui::PushFont(fonts_.heading);
      switch (v.kind) {
      case DiscVerdict::Kind::Good: ImGui::TextColored(kGood, "Ready to play"); break;
      case DiscVerdict::Kind::Usable: ImGui::TextColored(kGood, "Ready to play (content not compared)"); break;
      case DiscVerdict::Kind::Warning: ImGui::TextColored(kWarn, "Can be played, but read this first"); break;
      case DiscVerdict::Kind::Problem: ImGui::TextColored(kBad, "This disc image can't be used"); break;
      }
      ImGui::PopFont();
      ImGui::TextWrapped("%s", v.text.c_str());
      const uint32_t shadersTotal = bg_.shadersTotal;
      if (bg_.preparing && bg_.shadersRunning && v.kind != DiscVerdict::Kind::Problem) {
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        ImGui::TextColored(kBusy, "Preparing the game's shaders for this PC...");
        Dim(playWhenDrawn_ ? "The game starts as soon as this is done."
                           : "Done once (again after a graphics driver or game update), so places never wait for "
                             "their graphics the first time you see them. You can look around meanwhile.");
        const uint32_t done = std::min(bg_.shadersDone.load(), shadersTotal);
        char label[64];
        std::snprintf(label, sizeof label, shadersTotal > 0 ? "%u / %u" : "starting...", done, shadersTotal);
        ImGui::ProgressBar(shadersTotal > 0 ? static_cast<float>(done) / static_cast<float>(shadersTotal) : 0.0f,
                           ImVec2(-1, 0), label);
      }
    }
  }
  EndCard();
  ImGui::Spacing();
  Dim("The game never changes your disc image: mods and settings are kept separately.");
  if (game_.readDiscPictures || !game_.shortcutTarget.empty()) {
    ImGui::Dummy(ImVec2(0, 8 * scale_));
    DrawIconSection();
  }
  if (!game_.updates.version.empty()) {
    ImGui::Dummy(ImVec2(0, 8 * scale_));
    DrawUpdateCard();
  }
}

void App::DrawIconSection() {
  Heading("Icon, background and shortcuts");
  BeginCard("##icon");
  std::string note;
  std::vector<std::pair<std::string, std::string>> choices;
  bool have;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    have = bg_.haveIcon;
    note = bg_.iconNote;
    for (const auto& c : bg_.iconChoices) {
      choices.emplace_back(c.id, c.name);
    }
  }
  const std::string selected = ResolvedIconId();
  std::string chosenName = "the program's own icon";
  const bool icons = sys::HasIcons();
  if (icons) {
    ImGui::TextUnformatted("Icon of the windows, the taskbar and the shortcuts:");
  } else {
    Dim("Choosing another icon for the windows and shortcuts isn't there on this system yet: they show the "
        "program's own.");
  }
  const float side = 56 * scale_, gap = 10 * scale_;
  int index = 0;
  const auto Tile = [&](SDL_Texture* tex, const std::string& id, const std::string& name) {
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (index > 0 && ImGui::GetItemRectMax().x + gap + side <= right) {
      ImGui::SameLine(0, gap);
    } else if (index > 0) {
      ImGui::Dummy(ImVec2(0, 2 * scale_));
    }
    ImGui::PushID(index++);
    const bool on = id == selected;
    if (on) {
      chosenName = name;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##icon", ImVec2(side, side))) {
      registry_.SetText(kIconSetting, id);
    }
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
      ImGui::SetTooltip("%s", name.c_str());
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + side, p.y + side);
    dl->AddRectFilled(p, q, U32(on ? Hex(0x3A2D5C) : hovered ? Hex(0x2C2345) : kGround), 9 * scale_);
    if (tex != nullptr) {
      const float in = 6 * scale_;
      dl->AddImage(TexId(tex), ImVec2(p.x + in, p.y + in), ImVec2(q.x - in, q.y - in));
    }
    if (on) {
      dl->AddRect(p, q, U32(kAccentHover), 9 * scale_, 0, 2.0f * scale_);
    }
    ImGui::PopID();
  };
  for (size_t i = 0; icons && i < game_.ownIcons.size(); i++) {
    Tile(i < ownIcons_.size() ? ownIcons_[i] : nullptr, game_.ownIcons[i].id, game_.ownIcons[i].name);
  }
  for (size_t i = 0; icons && i < choices.size(); i++) {
    Tile(i < choiceIcons_.size() ? choiceIcons_[i] : nullptr, choices[i].first, choices[i].second);
  }
  if (icons) {
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::Text("In use: %s", chosenName.c_str());
  }
  if (!grounds_.empty()) {
    ImGui::Dummy(ImVec2(0, 6 * scale_));
    ImGui::TextUnformatted("Launcher background:");
    const int current = GroundIndex();
    const float gw = 92 * scale_, gh = 56 * scale_;
    for (int i = -1; i < static_cast<int>(grounds_.size()); i++) {
      if (i > -1) {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + gap + gw <= right) {
          ImGui::SameLine(0, gap);
        }
      }
      ImGui::PushID(1000 + i);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##ground", ImVec2(gw, gh))) {
        registry_.SetText(kGroundSetting, i < 0 ? std::string("none") : grounds_[i].first);
      }
      const bool hovered = ImGui::IsItemHovered();
      if (hovered) {
        ImGui::SetTooltip("%s", i < 0 ? "None (plain)" : grounds_[i].second.c_str());
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 q(p.x + gw, p.y + gh);
      dl->AddRectFilled(p, q, U32(kGround), 7 * scale_);
      if (i >= 0 && i < static_cast<int>(groundTex_.size()) && groundTex_[i] != nullptr) {
        dl->AddImageRounded(TexId(groundTex_[i]), p, q, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 7 * scale_);
      } else if (i < 0) {
        const ImVec2 ts = ImGui::CalcTextSize("None");
        dl->AddText(ImVec2(p.x + (gw - ts.x) / 2, p.y + (gh - ts.y) / 2), U32(kDim), "None");
      }
      if (i == current) {
        dl->AddRect(p, q, U32(kAccentHover), 7 * scale_, 0, 2.0f * scale_);
      } else if (hovered) {
        dl->AddRect(p, q, U32(kLine), 7 * scale_, 0, 1.0f * scale_);
      }
      ImGui::PopID();
    }
  }
  if (icons) {
    Dim(have && !note.empty() ? note
                              : "Once your disc has been checked, more icons are made from its pictures (only on this "
                                "PC, in your data folder).");
  } else if (grounds_.empty()) {
    Dim("Once your disc has been checked, launcher backgrounds are made from its pictures (only on this PC, in "
        "your data folder).");
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  if (ImGui::Button((std::string("Create ") + sys::ProgramsFolderName() + " shortcut").c_str())) {
    Shortcut(false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Create desktop shortcut")) {
    Shortcut(true);
  }
  if (!shortcutMessage_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, shortcutOk_ ? kGood : kWarn);
    ImGui::TextWrapped("%s", shortcutMessage_.c_str());
    ImGui::PopStyleColor();
  }
  EndCard();
}

void App::OpenInSystem(const fs::path& what) {
  if (!sys::OpenInSystem(what)) {
    notOpened_ = Utf8(what);
    Log("couldn't be opened by the system: " + notOpened_);
  }
}

void App::DrawNotOpenedWindow() {
  if (notOpened_.empty()) {
    return;
  }
  if (!ImGui::IsPopupOpen("Couldn't open it")) {
    ImGui::OpenPopup("Couldn't open it");
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Couldn't open it", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Your system didn't open this by itself. It is here:");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560 * scale_);
    ImGui::TextColored(kText, "%s", notOpened_.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Copy the path")) {
      SDL_SetClipboardText(notOpened_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("OK") || closeWindows_) {
      notOpened_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::DrawPresetPage() {
  PageTitle("How do you want to play?", "Pick a preset. You can change single settings later on the Settings page.");
  ui::DrawPresetChooser(registry_, options_);
}

void App::DrawSettingsPage() {
  ImGui::PushFont(fonts_.title);
  ImGui::TextUnformatted("Settings");
  ImGui::PopFont();
  const es::Preset preset = registry_.CurrentPreset();
  std::string line = std::string("Preset: ") + ui::PresetName(preset) + ". ";
  line += preset == es::Preset::Original ? "Changing a game setting switches to Custom; "
                                           "volumes, window and controls never do."
                                         : "Settings marked \"Changed\" are not as on the GameCube.";
  Dim(line);
  ImGui::Spacing();
  if (ImGui::BeginTabBar("##cats")) {
    for (es::Category c : {es::Category::Video, es::Category::Audio, es::Category::Gameplay,
                           es::Category::Accessibility, es::Category::System}) {
      if (ui::VisibleSettings(registry_, c, options_).empty()) {
        continue;
      }
      const bool select = !wantTab_.empty() && wantTab_ == es::CategoryId(c);
      if (ImGui::BeginTabItem(es::CategoryName(c), nullptr, select ? ImGuiTabItemFlags_SetSelected : 0)) {
        ImGui::BeginChild("##scroll", ImVec2(0, 0));
        ui::DrawCategory(registry_, c, options_);
        ImGui::Spacing();
        const std::string reset = std::string("Back to ") +
                                  ui::PresetName(preset == es::Preset::Custom ? registry_.BasePreset() : preset) +
                                  " on this page";
        if (ImGui::Button(reset.c_str())) {
          ui::ResetCategory(registry_, c, options_);
        }
        if (!settingsNotes_.empty() && c == es::Category::System) {
          ImGui::Spacing();
          Heading("Notes about your settings file");
          for (const std::string& n : settingsNotes_) {
            ImGui::BulletText("%s", n.c_str());
          }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    wantTab_.clear();
    ImGui::EndTabBar();
  }
}

void App::UpdatePads() {
  std::vector<ui::ControlsPanel::Pad> list;
  for (SDL_Gamepad* pad : pads_) {
    char guid[33]{};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(pad)), guid, sizeof(guid));
    const char* name = SDL_GetGamepadName(pad);
    list.push_back({guid, name != nullptr ? name : "", DeviceOf(pad)});
  }
  controls_.SetPads(std::move(list));
}

void App::FeedLiveInputs() {
  engine::controls::RawInputs raw;
  if (controls_.Testing()) {
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int k = 0; k < count && k < 512; k++) {
      if (keys[k]) {
        raw.keys.push_back(static_cast<uint16_t>(k));
      }
    }
    raw.keys.insert(raw.keys.end(), fakeKeys_.begin(), fakeKeys_.end());
  }
  const int pad = controls_.AssignedPad(registry_, controls_.Port());
  if (pad >= 0 && pad < static_cast<int>(pads_.size())) {
    SDL_Gamepad* g = pads_[static_cast<size_t>(pad)];
    for (int b = 0; b < static_cast<int>(raw.buttons.size()); b++) {
      raw.buttons[static_cast<size_t>(b)] = SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(b));
    }
    for (int a = 0; a < static_cast<int>(raw.axes.size()); a++) {
      raw.axes[static_cast<size_t>(a)] = SDL_GetGamepadAxis(g, static_cast<SDL_GamepadAxis>(a)) / 32767.0f;
    }
    if (controls_.TakeRumbleRequest()) {
      SDL_RumbleGamepad(g, 0xC000, 0xC000, 600);
    }
  }
  controls_.SetLiveInputs(raw);
}

void App::DrawControlsPage() {
  PageTitle("Controls", "");
  Dim("Which PC controller and which keys play each of the four GameCube controllers. Controls are yours in every "
      "preset, Original too: they change how you press the buttons, never what the game does with them.");
  ImGui::Spacing();
  FeedLiveInputs();
  controls_.DrawPortTabs(registry_, options_);
  ImGui::BeginChild("##scroll", ImVec2(0, 0));
  const std::string n = std::to_string(controls_.Port() + 1);
  if (BeginCard("##ctl_pc")) {
    Heading(("Controller " + n).c_str());
    controls_.DrawController(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_test")) {
    Heading("Try it");
    controls_.DrawLiveTest(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_map")) {
    Heading("Buttons");
    controls_.DrawMapping(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_sticks")) {
    Heading("Sticks and triggers");
    controls_.DrawSticks(registry_, options_);
  }
  EndCard();
  ui::Options generic = options_;
  generic.hiddenPrefixes = {"keyboard", "gamepad"};
  if (!ui::VisibleSettings(registry_, es::Category::Controls, generic).empty()) {
    ImGui::Spacing();
    if (BeginCard("##ctl_all")) {
      Heading("For every controller");
      ui::DrawCategory(registry_, es::Category::Controls, generic);
    }
    EndCard();
  }
  ImGui::EndChild();
}

void App::DrawModsPage() {
  PageTitle("Mods", "");
  Dim("Mods change the game's files (on top of your disc; the disc itself is never changed). A mod is only used "
      "when it is switched on here. Further down the list wins when two mods change the same thing.");
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Mod list:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220 * scale_);
  if (ImGui::BeginCombo("##profile", mods_.active.c_str())) {
    for (const em::ModList::Profile& p : mods_.profiles) {
      if (ImGui::Selectable(p.name.c_str(), p.name == mods_.active) && p.name != mods_.active) {
        mods_.active = p.name;
        em::AddNewMods(mods_, found_);
        SaveMods();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New list...")) {
    newProfile_.clear();
    ImGui::OpenPopup("New mod list");
  }
  ImGui::SameLine();
  if (ImGui::Button("Open the mods folder")) {
    std::error_code ec;
    fs::create_directories(game_.modsFolder, ec);
    OpenInSystem(game_.modsFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button("Look again")) {
    ReloadMods();
  }
  if (ImGui::BeginPopupModal("New mod list", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("A name for the new list (for example \"randomizer run\"):");
    ImGui::SetNextItemWidth(320 * scale_);
    ImGui::InputText("##name", &newProfile_);
    bool usable = !newProfile_.empty() && newProfile_.find_first_of("[]=;#") == std::string::npos &&
                  newProfile_.front() != ' ' && newProfile_.back() != ' ';
    for (const em::ModList::Profile& p : mods_.profiles) {
      usable &= p.name != newProfile_;
    }
    if (!usable) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Make it")) {
      mods_.active = newProfile_;
      mods_.Active();
      em::AddNewMods(mods_, found_);
      SaveMods();
      ImGui::CloseCurrentPopup();
    }
    if (!usable) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::Spacing();

  std::string discGame;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    if (bg_.haveVerdict) {
      discGame = bg_.verdict.gameId;
    }
  }
  auto& list = mods_.Active().mods;
  if (list.empty()) {
    ImGui::BeginChild("##none", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("No mods yet.");
    Dim("To add a mod, unpack it into its own folder inside the mods folder (\"Open the mods folder\"), then "
        "click \"Look again\". Mods: " + Utf8(game_.modsFolder));
    ImGui::EndChild();
  } else if (ImGui::BeginTable("##mods", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                               ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Version, made by", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    int moveFrom = -1, moveTo = -1;
    for (int i = 0; i < static_cast<int>(list.size()); i++) {
      auto& [id, on] = list[i];
      const em::ModFolder* mod = nullptr;
      for (const em::ModFolder& f : found_) {
        if (Lower(f.manifest.id) == Lower(id)) {
          mod = &f;
        }
      }
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (ImGui::Checkbox("##on", &on)) {
        SaveMods();
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(mod != nullptr && !mod->manifest.name.empty() ? mod->manifest.name.c_str() : id.c_str());
      if (mod == nullptr) {
        ImGui::TextColored(kWarn, "Not in the mods folder any more (left in the list; it isn't used).");
      } else {
        if (!mod->manifest.description.empty()) {
          Dim(mod->manifest.description);
        }
        {
          const em::AppliesVerdict v = em::CheckApplies(*mod, game_.modLooks);
          std::string when = std::string("Applies: ") + em::AppliesName(v.effective);
          if (v.Downgraded()) {
            when += std::string(" (it says ") + em::AppliesName(v.claimed) + ", but " + v.reason + ")";
          } else if (v.effective == em::Applies::NewPlaythrough) {
            when += " (chosen at New Game)";
          }
          Dim(when);
        }
        if (!discGame.empty()) {
          const auto& games = mod->manifest.games;
          if (std::none_of(games.begin(), games.end(), [&](const std::string& g) { return Lower(g) == Lower(discGame); })) {
            ImGui::TextColored(kWarn, "Made for another game or game version: it won't be used with this disc.");
          }
        }
      }
      ImGui::TableSetColumnIndex(2);
      if (mod != nullptr) {
        ImGui::TextUnformatted(mod->manifest.version.c_str());
        if (!mod->manifest.author.empty()) {
          Dim(mod->manifest.author);
        }
      }
      ImGui::TableSetColumnIndex(3);
      if (i == 0) {
        ImGui::BeginDisabled();
      }
      if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
        moveFrom = i;
        moveTo = i - 1;
      }
      if (i == 0) {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (i + 1 == static_cast<int>(list.size())) {
        ImGui::BeginDisabled();
      }
      if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
        moveFrom = i;
        moveTo = i + 1;
      }
      if (i + 1 == static_cast<int>(list.size())) {
        ImGui::EndDisabled();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
    if (moveFrom >= 0) {
      if (em::MoveMod(mods_.Active(), static_cast<size_t>(moveFrom), moveTo - moveFrom)) SaveMods();
    }
  }
  if (!modError_.empty()) {
    ImGui::TextColored(kBad, "%s", modError_.c_str());
  }
  for (const em::Message& m : modMessages_) {
    ImGui::PushStyleColor(ImGuiCol_Text, m.level == em::Message::Level::Info ? kDim : kWarn);
    ImGui::TextWrapped("%s", m.text.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Spacing();
  Dim("The game checks the mods again when it starts (what they need, what they clash with) and tells you "
      "about any problem.");
}

void App::DrawSavePage() {
  PageTitle(SlotsOn() ? "Your saves" : "Your save",
            SlotsOn() ? "Your saves: which one the game loads, what is in each, and the backups the game keeps of it."
            : game_.saveBackupsFolder.empty()
                ? "What is saved on the game's memory card. Only shown here, never changed."
                : "What is saved on the game's memory card, and the backups the game keeps of it.");
  if (SlotsOn()) {
    DrawSlots();
    return;
  }
  DrawSaveSummary();
  if (!game_.saveBackupsFolder.empty()) {
    ImGui::Dummy(ImVec2(0, 8 * scale_));
    DrawBackups(false);
  }
}

void App::LookAtBackups(bool now) {
  const uint64_t tick = SDL_GetTicks();
  if (!now && backupsLookedAt_ != 0 && tick - backupsLookedAt_ < 2000) {
    return;
  }
  backupsLookedAt_ = tick == 0 ? 1 : tick;
  backups_ = bk::List(BackupsFolder());
  bool found = false;
  for (const bk::Backup& b : backups_) {
    for (const bk::File& f : b.files) {
      found |= b.card + "/" + b.name + "/" + f.name == backupSel_;
    }
  }
  if (!found) {
    backupSel_ = backups_.empty() || backups_.front().files.empty()
                     ? std::string()
                     : backups_.front().card + "/" + backups_.front().name + "/" + backups_.front().files.front().name;
  }
}

void App::RestoreBackup() {
  for (const bk::Backup& b : backups_) {
    for (const bk::File& f : b.files) {
      if (b.card + "/" + b.name + "/" + f.name != backupSel_) {
        continue;
      }
      if (gameRunning_) {
        backupOk_ = false;
        backupMessage_ = "Close the game first: it has the memory card open.";
        return;
      }
      std::string error;
      const sl::Slot* open = SlotsOn() ? OpenSlot() : nullptr;
      const bool other = open != nullptr && open->id != slots_.Active().id;
      const fs::path card = other ? (game_.saves.cardFolder
                                         ? game_.saves.cardFolder(sl::RootOf(game_.saves.dataFolder, open->id), b.card)
                                         : fs::path())
                                  : game_.cardFolder ? game_.cardFolder(b.card) : fs::path();
      backupOk_ = !card.empty() && bk::Restore(b, f.name, card, BackupsFolder(), bk::kKeep, error);
      backupMessage_ = backupOk_ ? "Restored the backup of " + b.when + ". The save you had before is in the list too."
                                 : (error.empty() ? std::string("This card's folder isn't known.") : error);
      Log("restore backup " + backupSel_ + ": " + backupMessage_);
      LookAtBackups(true);
      slotsLookedAt_ = 0;
      if (backupOk_ && other) {
        openReadAgain_ = true;
      } else if (backupOk_) {
        StartWorker(DiscPath());
      }
      return;
    }
  }
  backupOk_ = false;
  backupMessage_ = "Choose a backup first.";
}

void App::DrawBackups(bool inBox) {
  LookAtBackups(false);
  Heading("Backups");
  if (inBox) {
    size_t count = 0;
    for (const bk::Backup& b : backups_) {
      count += b.files.size();
    }
    ImGui::AlignTextToFramePadding();
    if (count == 0) {
      Dim("None yet: the first one is made the next time the game saves over this save.");
    } else {
      ImGui::Text("%d kept, newest %s", static_cast<int>(count), backups_.front().when.c_str());
      ImGui::SameLine();
      if (ImGui::Button(backupsShown_ ? "Hide backups" : "Show backups")) {
        backupsShown_ = !backupsShown_;
      }
    }
    if (count == 0 || !backupsShown_) {
      if (!backupMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, backupOk_ ? kGood : kBad);
        ImGui::TextWrapped("%s", backupMessage_.c_str());
        ImGui::PopStyleColor();
      }
      return;
    }
  }
  Dim("Each time the game saves, it first keeps a copy of the save as it was (only when it changed). The newest " +
      std::to_string(bk::kKeep) + " are kept. Restore puts one back on the memory card.");
  if (ImGui::Button("Open folder")) {
    std::error_code ec;
    fs::create_directories(BackupsFolder(), ec);
    OpenInSystem(BackupsFolder());
  }
  if (!inBox) {
    ImGui::SameLine();
    if (ImGui::Button("Look again")) {
      LookAtBackups(true);
    }
  }
  bool restore = false;
  if (backups_.empty()) {
    Dim("No backups yet: the first one is made the next time the game saves over an existing save.");
  } else if (ImGui::BeginTable("##backups", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("Backed up", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Card", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (const bk::Backup& b : backups_) {
      for (const bk::File& f : b.files) {
        const std::string id = b.card + "/" + b.name + "/" + f.name;
        ImGui::PushID(id.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(b.when.c_str(), backupSel_ == id, ImGuiSelectableFlags_SpanAllColumns |
                                                                     ImGuiSelectableFlags_AllowOverlap)) {
          backupSel_ = id;
          backupMessage_.clear();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted((f.comment.empty() ? f.name : f.comment).c_str());
        const std::string saved = bk::CardTimeText(f.savedAt);
        if (!saved.empty()) {
          ImGui::SameLine();
          ImGui::TextColored(kDim, "(saved %s)", saved.c_str());
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", f.name.c_str());
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(b.card.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(bk::SizeText(f.size).c_str());
        ImGui::TableSetColumnIndex(4);
        if (gameRunning_) {
          ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("Restore...")) {
          backupSel_ = id;
          backupMessage_.clear();
          ImGui::OpenPopup("Restore this backup?");
        }
        if (gameRunning_) {
          ImGui::EndDisabled();
        }
        if (ImGui::BeginPopupModal("Restore this backup?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("Restore the backup of %s?", b.when.c_str());
          Dim("This replaces your current save; the current one is backed up first.");
          if (ImGui::Button("Restore it")) {
            ImGui::CloseCurrentPopup();
            restore = true;
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  if (restore) {
    RestoreBackup();
  }
  if (gameRunning_ && !backups_.empty()) {
    Dim("Close the game to restore a backup (it has the memory card open).");
  }
  if (!backupMessage_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, backupOk_ ? kGood : kBad);
    ImGui::TextWrapped("%s", backupMessage_.c_str());
    ImGui::PopStyleColor();
  }
  Dim("Backups folder: " + Utf8(BackupsFolder()));
}

fs::path App::BackupsFolder() const {
  if (!SlotsOn()) {
    return game_.saveBackupsFolder;
  }
  const sl::Slot* open = OpenSlot();
  return bk::FolderFor(sl::RootOf(game_.saves.dataFolder, open != nullptr ? open->id : slots_.Active().id));
}

void App::LookAtSlots(bool now) {
  if (!SlotsOn()) {
    return;
  }
  const uint64_t tick = SDL_GetTicks();
  if (!now && slotsLookedAt_ != 0 && tick - slotsLookedAt_ < 2000) {
    return;
  }
  const bool first = slots_.slots.empty();
  slotsLookedAt_ = tick == 0 ? 1 : tick;
  std::string note;
  sl::Index index = sl::Load(game_.saves.dataFolder, &note);
  if (!note.empty() && note != slotNote_) {
    Log("saves: " + note);
  }
  slotNote_ = note;
  const bool otherActive = !first && index.Active().id != slots_.Active().id;
  slots_ = std::move(index);
  slotRows_.clear();
  for (const sl::Slot& s : slots_.slots) {
    slotRows_.push_back(game_.saves.row ? game_.saves.row(sl::RootOf(game_.saves.dataFolder, s.id)) : GameInfo::SaveRow());
  }
  if (!openSlotSet_) {
    openSlotSet_ = true;
    OpenSlotIs(slots_.Active().id);
  } else if (!openSlot_.empty() && slots_.Find(openSlot_) == nullptr) {
    OpenSlotIs("");
  }
  if (now) {
    openReadAgain_ = true;
  }
  if (otherActive) {
    ActiveSaveChanged();
  }
}

const sl::Slot* App::SlotByArg(const std::string& arg) const {
  const int n = std::atoi(arg.c_str());
  for (size_t i = 0; i < slots_.slots.size(); i++) {
    if ((n > 0 && static_cast<size_t>(n) == i + 1) || (n <= 0 && Lower(slots_.slots[i].name) == Lower(arg))) {
      return &slots_.slots[i];
    }
  }
  return nullptr;
}

void App::OpenSlotIs(const std::string& id) {
  if (openSlot_ == id) {
    return;
  }
  openSlot_ = id;
  backupsShown_ = false;
  backupsLookedAt_ = 0;
  backupSel_.clear();
  backupMessage_.clear();
  codeMessage_.clear();
}

void App::StopOpenRead() {
  if (openRead_.thread.joinable()) {
    openRead_.thread.join();
  }
}

bool App::OpenSummary(SaveSummary& save) {
  const sl::Slot* open = SlotsOn() ? OpenSlot() : nullptr;
  if (open == nullptr || open->id == slots_.Active().id || !game_.saves.read) {
    openReadAgain_ = false;
    std::lock_guard<std::mutex> lock(bg_.mutex);
    save = bg_.save;
    return bg_.haveSave;
  }
  std::string disc = DiscPath();
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    if (!bg_.haveVerdict || bg_.discPath != disc || bg_.verdict.kind == DiscVerdict::Kind::Problem) {
      disc.clear();
    }
  }
  const std::string key = open->id + "\n" + disc;
  if (!openRead_.busy && (key != openReadKey_ || openReadAgain_)) {
    StopOpenRead();
    openReadKey_ = key;
    openReadAgain_ = false;
    openRead_.busy = true;
    openRead_.thread = std::thread([this, key, disc, root = sl::RootOf(game_.saves.dataFolder, open->id)] {
      SaveSummary read = game_.saves.read(root, disc);
      {
        std::lock_guard<std::mutex> lock(openRead_.mutex);
        openRead_.save = std::move(read);
        openRead_.key = key;
        openRead_.have = true;
      }
      openRead_.busy = false;
    });
  }
  std::lock_guard<std::mutex> lock(openRead_.mutex);
  if (!openRead_.have || openRead_.key.compare(0, open->id.size() + 1, open->id + "\n") != 0) {
    return false;
  }
  save = openRead_.save;
  return true;
}

bool App::SlotsLocked(std::string& why) const {
  if (gameRunning_ || playWhenDrawn_) {
    why = "Close the game first: it has the memory card open.";
    return true;
  }
  if (game_.playerFilesReadOnly) {
    why = "Test run on the player's own files: the saves are not changed.";
    return true;
  }
  return false;
}

bool App::SlotDone(bool ok, const std::string& message) {
  slotOk_ = ok;
  slotMessage_ = message;
  Log("saves: " + message);
  return ok;
}

void App::ActiveSaveChanged() {
  if (game_.saves.changed) {
    game_.saves.changed();
  }
  backupsLookedAt_ = 0;
  RereadSave();
}

void App::RereadSave() {
  if (bg_.busy || bg_.drawing || bg_.preparing) {
    StartWorker(DiscPath());
    return;
  }
  StopWorker();
  const std::string path = DiscPath();
  bool usable = false, whole = false;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    const bool known = bg_.haveVerdict && bg_.discPath == path;
    whole = !path.empty() && !known;
    usable = known && bg_.verdict.kind != DiscVerdict::Kind::Problem;
    if (!whole) {
      bg_.haveSave = false;
    }
  }
  if (whole) {
    StartWorker(path);
    return;
  }
  bg_.busy = true;
  bg_.thread = std::thread([this, disc = usable ? path : std::string()] {
    SaveSummary save = game_.readSave(disc);
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      bg_.save = std::move(save);
      bg_.haveSave = true;
    }
    bg_.busy = false;
  });
}

bool App::ChooseSlot(const std::string& id) {
  LookAtSlots(true);
  const sl::Slot* slot = slots_.Find(id);
  if (slot == nullptr) {
    return SlotDone(false, "There is no such save.");
  }
  if (slots_.Active().id == id) {
    return true;
  }
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  const std::string name = slot->name;
  if (!sl::SetActive(game_.saves.dataFolder, id, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  return SlotDone(true, "\"" + name + "\" is the save the game loads now.");
}

bool App::NewSlot(const std::string& name) {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const std::string wanted = name.empty() ? sl::NextName(slots_) : name;
  if (!sl::Create(game_.saves.dataFolder, wanted, id, error) || !sl::SetActive(game_.saves.dataFolder, id, error)) {
    LookAtSlots(true);
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  OpenSlotIs(slots_.Active().id);
  return SlotDone(true, "Made \"" + slots_.Active().name + "\": an empty save. The game offers New Game when it starts.");
}

static const char* const kOpenFirst = "Open a save first (click its row).";

bool App::RenameSlot(const std::string& name) {
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, kOpenFirst);
  }
  const std::string id = open->id, before = open->name;
  if (!sl::Rename(game_.saves.dataFolder, id, name, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  const sl::Slot* after = slots_.Find(id);
  return SlotDone(true, "\"" + before + "\" is called \"" + (after != nullptr ? after->name : name) + "\" now.");
}

bool App::DuplicateSlot() {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, kOpenFirst);
  }
  const std::string from = open->name;
  if (!sl::Duplicate(game_.saves.dataFolder, open->id, id, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  const sl::Slot* made = slots_.Find(id);
  return SlotDone(true, "Made \"" + (made != nullptr ? made->name : id) + "\": a copy of \"" + from +
                            "\" (without its backups). \"" + slots_.Active().name + "\" is still the one the game loads.");
}

bool App::BinFolder(const fs::path& folder, std::string& error) {
  std::error_code ec;
  if (!game_.saves.binFolder.empty()) {
    fs::create_directories(game_.saves.binFolder, ec);
    fs::path target;
    for (int n = 1; n < 1000; n++) {
      target = game_.saves.binFolder / PathOf(Utf8(folder.filename()) + "_" + std::to_string(n));
      if (!fs::exists(target, ec)) {
        break;
      }
    }
    fs::rename(folder, target, ec);
    if (ec) {
      error = "Can't move " + Utf8(folder) + " away (" + ec.message() + ").";
      return false;
    }
    Log("saves: moved " + Utf8(folder) + " to " + Utf8(target));
    return true;
  }
  if (!sys::ToRecycleBin(folder, error)) {
    return false;
  }
  Log(std::string("saves: to ") + sys::RecycleBinName() + ": " + Utf8(folder));
  return true;
}

bool App::DeleteSlot() {
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  if (OpenSlot() == nullptr) {
    return SlotDone(false, kOpenFirst);
  }
  const sl::Slot gone = *OpenSlot();
  const bool first = gone.id == sl::kMainId;
  const bool chosen = gone.id == slots_.Active().id;
  StopOpenRead();
  if (chosen) {
    const bool working = bg_.busy || bg_.drawing || bg_.preparing;
    StopWorker();
    if (working) {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      bg_.haveVerdict = false;
    }
  }
  const bool ok = sl::Delete(game_.saves.dataFolder, gone.id,
                             [this](const fs::path& folder, std::string& e) { return BinFolder(folder, e); }, error);
  LookAtSlots(true);
  if (chosen && slots_.Active().id == gone.id) {
    ActiveSaveChanged();
  }
  backupsLookedAt_ = 0;
  if (!ok) {
    return SlotDone(false, error);
  }
  const std::string where = game_.saves.binFolder.empty() ? sys::RecycleBinName() : Utf8(game_.saves.binFolder);
  return SlotDone(true, first ? "\"" + gone.name + "\" is empty now: its save, setup and backups went to " + where + "."
                              : "Deleted \"" + gone.name + "\" (it is in " + where + "). \"" + slots_.Active().name +
                                    "\" is the save the game loads" + (chosen ? " now." : ", as before."));
}

bool App::ImportSlot(const std::string& file) {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  GameInfo::ImportCheck check;
  if (!game_.saves.checkImport || !game_.saves.checkImport(PathOf(file), check, error)) {
    return SlotDone(false, error.empty() ? std::string("This file can't be imported.") : error);
  }
  LookAtSlots(true);
  const std::string name = sl::CleanName(check.name).empty() ? sl::NextName(slots_) : check.name;
  if (!sl::Import(game_.saves.dataFolder, PathOf(file), name, check.cardFolder, check.cardFileName, id, error) ||
      !sl::SetActive(game_.saves.dataFolder, id, error)) {
    LookAtSlots(true);
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  OpenSlotIs(slots_.Active().id);
  return SlotDone(true, "Imported as \"" + slots_.Active().name + "\", the save the game loads now. The file you chose "
                            "was only copied.");
}

bool App::ExportSlot(const std::string& file) {
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, kOpenFirst);
  }
  std::string error;
  const fs::path from = game_.saves.saveFile ? game_.saves.saveFile(sl::RootOf(game_.saves.dataFolder, open->id))
                                             : fs::path();
  if (!sl::Export(from, PathOf(file), error)) {
    return SlotDone(false, error);
  }
  return SlotDone(true, "Saved a copy of \"" + open->name + "\" as " + file + ".");
}

void App::DrawSlots() {
  LookAtSlots(false);
  Heading("Saves");
  Dim("Keep several saves side by side. The circle marks the one the game loads (CONTINUE, or New Game when it is "
      "empty). Click a save to see what is in it.");
  std::string why;
  const bool locked = SlotsLocked(why);
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("New...")) {
    slotName_ = sl::NextName(slots_);
    slotAsk_ = SlotAsk::New;
  }
  ImGui::SameLine();
  if (ImGui::Button("Import...")) {
    static const SDL_DialogFileFilter kGci[] = {{"GameCube save files", "gci"}, {"All files", "*"}};
    slotMessage_.clear();
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
          App* app = static_cast<App*>(user);
          if (files != nullptr && files[0] != nullptr) {
            std::lock_guard<std::mutex> lock(app->pickMutex_);
            app->slotPicked_ = files[0];
            app->slotPickedKind_ = 1;
          }
        },
        this, window_, kGci, 2, nullptr, false);
  }
  if (locked) {
    ImGui::EndDisabled();
    Dim(why);
  }
  if (!slotMessage_.empty() && !(locked && slotMessage_ == why)) {
    ImGui::PushStyleColor(ImGuiCol_Text, slotOk_ ? kGood : kBad);
    ImGui::TextWrapped("%s", slotMessage_.c_str());
    ImGui::PopStyleColor();
  }

  std::string choose, toggle;
  bool duplicate = false;
  bool inTable = false, headers = true;
  const float frame = ImGui::GetFrameHeight();
  for (size_t i = 0; i < slots_.slots.size(); i++) {
    const sl::Slot& s = slots_.slots[i];
    const GameInfo::SaveRow row = i < slotRows_.size() ? slotRows_[i] : GameInfo::SaveRow();
    const bool loads = slots_.Active().id == s.id, open = openSlot_ == s.id;
    if (!inTable) {
      inTable = ImGui::BeginTable("##saves", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX);
      if (inTable) {
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Trainer", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Play time", ImGuiTableColumnFlags_WidthFixed, 132 * scale_);
        ImGui::TableSetupColumn("Playthrough", ImGuiTableColumnFlags_WidthStretch, 1.9f);
        if (headers) {
          ImGui::TableHeadersRow();
        }
      }
      headers = false;
    }
    if (inTable) {
      ImGui::PushID(s.id.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (locked) {
        ImGui::BeginDisabled();
      }
      if (ImGui::RadioButton("##loads", loads) && !loads) {
        choose = s.id;
      }
      if (locked) {
        ImGui::EndDisabled();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", loads ? "The game loads this save." : locked ? why.c_str() : "Have the game load this save.");
      }
      ImGui::SameLine();
      {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = frame * 0.17f;
        const ImVec2 c(p.x + r, p.y + frame * 0.5f);
        const ImU32 colour = ImGui::GetColorU32(kDim);
        if (open) {
          ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f),
                                                        ImVec2(c.x, c.y + r * 0.8f), colour);
        } else {
          ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r),
                                                        ImVec2(c.x + r * 0.8f, c.y), colour);
        }
        ImGui::Dummy(ImVec2(r * 2, frame));
      }
      ImGui::SameLine();
      const bool clipped = ImGui::CalcTextSize(s.name.c_str()).x > ImGui::GetContentRegionAvail().x;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().FramePadding.y);
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
      if (open) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
      }
      if (ImGui::Selectable(s.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0, frame))) {
        toggle = s.id;
      }
      ImGui::PopStyleVar();
      if (clipped && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", s.name.c_str());
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      if (row.found) {
        ImGui::TextUnformatted(row.who.c_str());
      } else {
        ImGui::TextColored(kDim, "%s", row.who.empty() ? "empty" : row.who.c_str());
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(row.time.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::AlignTextToFramePadding();
      ImGui::TextWrapped("%s", row.setup.c_str());
      ImGui::PopID();
    }
    if (open) {
      if (inTable) {
        ImGui::EndTable();
        inTable = false;
      }
      DrawSlotBox(s, locked, duplicate);
    }
  }
  if (inTable) {
    ImGui::EndTable();
  }
  if (!choose.empty()) {
    ChooseSlot(choose);
  }
  if (!toggle.empty()) {
    OpenSlotIs(toggle == openSlot_ ? std::string() : toggle);
  }
  if (duplicate) {
    DuplicateSlot();
  }

  if (slotAsk_ == SlotAsk::New) {
    ImGui::OpenPopup("New save");
  } else if (slotAsk_ == SlotAsk::Rename) {
    ImGui::OpenPopup("Rename this save");
  } else if (slotAsk_ == SlotAsk::Delete) {
    ImGui::OpenPopup("Delete this save?");
  }
  slotAsk_ = SlotAsk::None;
  for (const bool isNew : {true, false}) {
    if (ImGui::BeginPopupModal(isNew ? "New save" : "Rename this save", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(isNew ? "A new, empty save. What should it be called?" : "The save's new name:");
      ImGui::SetNextItemWidth(360 * scale_);
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }
      const bool enter = ImGui::InputText("##slotname", &slotName_, ImGuiInputTextFlags_EnterReturnsTrue);
      if (isNew) {
        Dim("The game then starts with New Game. Your other saves stay as they are.");
      }
      const bool usable = !sl::CleanName(slotName_).empty();
      if (!usable) {
        ImGui::BeginDisabled();
      }
      if ((ImGui::Button(isNew ? "Make it" : "Rename") || enter) && usable) {
        ImGui::CloseCurrentPopup();
        if (isNew) {
          NewSlot(slotName_);
        } else {
          RenameSlot(slotName_);
        }
      }
      if (!usable) {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  if (ImGui::BeginPopupModal("Delete this save?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const sl::Slot* open = OpenSlot();
    if (open == nullptr) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::Text("Delete \"%s\"?", open->name.c_str());
      Dim("Its save, its playthrough setup and its backups go to " + std::string(sys::RecycleBinName()) + " (you can take them back out of it).");
      if (open->id == sl::kMainId) {
        Dim("This is the first save: its place in the list stays, empty.");
      }
      if (open->id == slots_.Active().id && open->id != sl::kMainId) {
        Dim("It is the save the game loads: after this the game loads the first save.");
      }
      if (ImGui::Button("Delete it")) {
        ImGui::CloseCurrentPopup();
        DeleteSlot();
      }
      ImGui::SameLine();
      if (ImGui::Button("Keep it")) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
}

void App::DrawSlotBox(const sl::Slot& slot, bool locked, bool& duplicate) {
  ImGui::PushID("openbox");
  BeginCard("##box");
  Heading(slot.name.c_str());
  SaveSummary save;
  const bool have = OpenSummary(save);
  const bool found = have && save.state == SaveSummary::State::Found;
  const bool loads = slot.id == slots_.Active().id;
  Dim(loads ? "The game loads this save." : "The game does not load this save (its circle is not ticked).");
  if (!have) {
    ImGui::TextColored(kBusy, "Reading the memory card...");
  } else if (save.state == SaveSummary::State::None) {
    ImGui::TextUnformatted("No save yet: the game offers New Game.");
    Dim("Folder: " + Utf8(sl::RootOf(game_.saves.dataFolder, slot.id)));
  } else if (!found) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", save.message.c_str());
    ImGui::PopStyleColor();
    if (!save.where.empty()) {
      Dim("Memory card: " + save.where);
    }
  } else {
    DrawSaveContents(save);
  }
  LookAtBackups(false);
  if (found || !backups_.empty()) {
    ImGui::Spacing();
    DrawBackups(true);
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Rename...")) {
    slotName_ = slot.name;
    slotAsk_ = SlotAsk::Rename;
  }
  if (found) {
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
      duplicate = true;
    }
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  if (found) {
    ImGui::SameLine();
    if (ImGui::Button("Export...")) {
      static const SDL_DialogFileFilter kGci[] = {{"GameCube save files", "gci"}};
      const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
      const std::string where = Utf8(start / PathOf(game_.saves.exportName));
      slotMessage_.clear();
      SDL_ShowSaveFileDialog(
          [](void* user, const char* const* files, int) {
            App* app = static_cast<App*>(user);
            if (files != nullptr && files[0] != nullptr) {
              std::lock_guard<std::mutex> lock(app->pickMutex_);
              app->slotPicked_ = files[0];
              app->slotPickedKind_ = 2;
            }
          },
          this, window_, kGci, 1, where.c_str());
    }
  }
  if (locked) {
    ImGui::BeginDisabled();
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete...")) {
    slotAsk_ = SlotAsk::Delete;
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  EndCard();
  ImGui::PopID();
}

void App::DrawSaveSummary() {
  bool have;
  SaveSummary save;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    have = bg_.haveSave;
    save = bg_.save;
  }
  if (!have) {
    ImGui::TextColored(kBusy, "Reading the memory card...");
    return;
  }
  if (save.state != SaveSummary::State::Found) {
    ImGui::TextColored(save.state == SaveSummary::State::None ? kDim : kWarn, "%s", save.message.c_str());
    if (!save.where.empty()) {
      Dim("Memory card: " + save.where);
    }
    return;
  }
  DrawSaveContents(save);
}

void App::DrawSaveContents(const SaveSummary& save) {
  if (ImGui::BeginTable("##facts", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    for (const auto& [label, value] : save.facts) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(kDim, "%s", label.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(value.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::Spacing();
  Heading("Party");
  const int columns = static_cast<int>(save.partyColumns.size());
  if (save.party.empty()) {
    Dim("No Pokemon in the party yet.");
  } else if (columns > 0 && ImGui::BeginTable("##party", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                                       ImGuiTableFlags_PadOuterX)) {
    for (const std::string& c : save.partyColumns) {
      ImGui::TableSetupColumn(c.c_str());
    }
    ImGui::TableHeadersRow();
    for (const auto& row : save.party) {
      ImGui::TableNextRow();
      for (int c = 0; c < columns && c < static_cast<int>(row.size()); c++) {
        ImGui::TableSetColumnIndex(c);
        ImGui::TextUnformatted(row[c].c_str());
      }
    }
    ImGui::EndTable();
  }
  for (const std::string& n : save.notes) {
    Dim(n);
  }
  DrawSetup(save);
  ImGui::Spacing();
  Dim("Memory card: " + save.where);
}

void App::DrawSetup(const SaveSummary& save) {
  if (save.setup.empty() && save.setupNotes.empty()) {
    return;
  }
  ImGui::Spacing();
  Heading("Playthrough");
  if (ImGui::BeginTable("##setup", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& [label, value] : save.setup) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(kDim, "%s", label.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextWrapped("%s", value.c_str());
    }
    ImGui::EndTable();
  }
  for (const std::string& n : save.setupNotes) {
    Dim(n);
  }
  if (!save.shareCode.empty()) {
    if (ImGui::Button("Copy code")) {
      CopyCode(save.shareCode);
    }
    if (!save.spoilerFile.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Show spoilers...")) {
        askSpoilers_ = true;
      }
    }
    Dim(codeMessage_.empty() ? "Give the code to a friend: on the New Game setup screen they choose \"Use a friend's "
                               "code\" and get the same changes."
                             : codeMessage_);
  }
  if (askSpoilers_) {
    askSpoilers_ = false;
    ImGui::OpenPopup("Show spoilers?");
  }
  if (ImGui::BeginPopupModal("Show spoilers?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("This shows everything the randomizer changed in this playthrough.");
    Dim("Which Pokemon, trainers and items are where: it spoils the surprises.");
    if (ImGui::Button("Show them")) {
      ImGui::CloseCurrentPopup();
      Log("spoilers: " + Utf8(save.spoilerFile));
      OpenInSystem(save.spoilerFile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || closeSpoilers_) {
      closeSpoilers_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::CopyCode(const std::string& code) {
  if (SDL_SetClipboardText(code.c_str())) {
    codeMessage_ = "Copied " + code + ": paste it to your friend.";
  } else {
    codeMessage_ = std::string("The code could not be copied: ") + SDL_GetError();
  }
}

void App::LookAtReports(bool now) {
  const uint64_t tick = SDL_GetTicks();
  if (!now && reportsLookedAt_ != 0 && tick - reportsLookedAt_ < 2000) {
    return;
  }
  reportsLookedAt_ = tick == 0 ? 1 : tick;
  reports_ = reports::List(game_.reportsFolder);
  if (SelectedReport() == nullptr) {
    reportSel_ = reports_.empty() ? std::string() : reports_.front().name;
  }
}

const reports::Report* App::SelectedReport() const {
  for (const reports::Report& r : reports_) {
    if (r.name == reportSel_) {
      return &r;
    }
  }
  return nullptr;
}

void App::ExportReport(const std::string& file) {
  const reports::Report* r = SelectedReport();
  if (r == nullptr) {
    reportOk_ = false;
    reportMessage_ = "Choose a report first.";
    return;
  }
  fs::path to = PathOf(file);
  if (!to.has_extension()) {
    to += L".zip";
  }
  std::string error;
  reportOk_ = reports::Export(*r, to, exportOptions_, reports::CurrentIdentity(), error);
  reportMessage_ = reportOk_ ? "Exported to " + Utf8(to) + ". You can send that file to whoever asked for it." : error;
}

void App::DeleteReport() {
  const reports::Report* r = SelectedReport();
  if (r == nullptr) {
    return;
  }
  std::string error;
  reportOk_ = reports::Delete(*r, error);
  reportMessage_ = reportOk_ ? "Deleted the report of " + r->when + "." : error;
  if (reportOk_) {
    reportSel_.clear();
  }
  LookAtReports(true);
}

void App::LookAtSessions() {
  const std::string was = sessionSel_ < sessions_.size() ? sessions_[sessionSel_].id : std::string();
  sessions_ = reports::ListSessions(game_.logsFolder, game_.recordingsFolder, game_.reportsFolder);
  sessionSel_ = 0;
  for (size_t i = 0; i < sessions_.size(); i++) {
    if (sessions_[i].id == was) {
      sessionSel_ = i;
    }
  }
  sessionBundleFor_ = "\x01";
}

void App::SaveSession(const std::string& file) {
  if (sessions_.empty()) {
    LookAtSessions();
  }
  if (sessionSel_ >= sessions_.size()) {
    sessionOk_ = false;
    sessionMessage_ = "There is no play session to save.";
    return;
  }
  fs::path to = PathOf(file);
  if (!to.has_extension()) {
    to += L".zip";
  }
  LookAtSessions();
  sessionBundle_ = reports::BundleFor(sessions_, sessionSel_, game_.settingsFolder / L"settings.ini", game_.modListFile);
  std::string error;
  sessionOk_ = reports::ExportSession(sessions_, sessionSel_, sessionBundle_, to, exportOptions_.hideUserName,
                                      game_.updates.version, reports::CurrentIdentity(), error);
  sessionMessage_ = sessionOk_ ? "Saved " + Utf8(to) + ". You can send that file to whoever asked for it." : error;
}

void App::DrawSessionWindow() {
  const char* kTitle = "Save a play session";
  if (sessionOpen_ && !ImGui::IsPopupOpen(kTitle)) {
    LookAtSessions();
    sessionMessage_.clear();
    ImGui::OpenPopup(kTitle);
  }
  const ImVec2 view = ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowSize(ImVec2(std::min(view.x - 40 * scale_, 900 * scale_), view.y - 60 * scale_), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  bool open = true;
  if (!ImGui::BeginPopupModal(kTitle, &open)) {
    sessionOpen_ = false;
    return;
  }
  Dim("For when the game stopped, froze or was closed and no report was made. This makes one .zip file of a play "
      "session: what the game program wrote about itself (its log), your button presses, your memory card at the "
      "start and after each save, and your settings and mod list. Nothing is sent: you save the file and give it to "
      "the developer yourself.");
  if (ImGui::Button("Look again")) {
    LookAtSessions();
  }
  ImGui::Spacing();
  if (sessions_.empty()) {
    ImGui::TextUnformatted("No play sessions yet.");
    Dim("A session is kept each time you play the game (the newest few are kept), so the time you play next can be "
        "saved here.");
  } else {
    const float listH = std::min(static_cast<float>(sessions_.size()) + 1.6f, 6.6f) * ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("##sessions", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_PadOuterX,
                          ImVec2(0, listH))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Started", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Played", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("How it ended", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < sessions_.size(); i++) {
        const reports::Session& s = sessions_[i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(s.when.c_str(), i == sessionSel_, ImGuiSelectableFlags_SpanAllColumns)) {
          sessionSel_ = i;
          sessionMessage_.clear();
        }
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(reports::DurationText(s.seconds).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(reports::SizeText(s.logSize + s.recordingSize + s.cardsSize).c_str());
        ImGui::TableSetColumnIndex(3);
        std::string how = s.recording.empty() ? "the game didn't start playing"
                          : s.ended           ? "closed normally"
                                              : "not closed normally (stopped, crashed or killed; or still running)";
        if (s.shortRun && !s.recording.empty()) {
          how = "very short; " + how;
        }
        if (!s.reports.empty()) {
          how += ", " + std::to_string(s.reports.size()) + (s.reports.size() == 1 ? " report" : " reports");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted(how.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::EndTable();
    }
    if (sessionSel_ >= sessions_.size()) {
      sessionSel_ = 0;
    }
    const reports::Session& s = sessions_[sessionSel_];
    const std::string key = s.id + (exportOptions_.hideUserName ? "+" : "-");
    if (sessionBundleFor_ != key) {
      sessionBundle_ = reports::BundleFor(sessions_, sessionSel_, game_.settingsFolder / L"settings.ini", game_.modListFile);
      sessionBundleFor_ = key;
    }
    ImGui::Spacing();
    ImGui::Checkbox((std::string("Hide my ") + sys::UserNameWords()).c_str(), &exportOptions_.hideUserName);
    Dim(std::string("Your ") + sys::UserNameWords() +
        " appears in folder names in the log, the recording and the settings (" + sys::HomeFolderExample() +
        "). With this on, the saved copy says %USERNAME% instead.");
    if (!sessionBundle_.choice.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
      ImGui::TextWrapped("%s", sessionBundle_.choice.c_str());
      ImGui::PopStyleColor();
    }
    if (!s.reports.empty()) {
      ImGui::TextWrapped("A report was made during this session: it is included (without a crash dump; to add that, "
                         "use Export on the report in the list).");
    }
    if (ImGui::Button("Save...")) {
      static const SDL_DialogFileFilter kZip[] = {{"Zip files", "zip"}};
      const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
      const std::string where = Utf8(start / PathOf(reports::SessionExportName(s)));
      sessionMessage_.clear();
      SDL_ShowSaveFileDialog(
          [](void* user, const char* const* files, int) {
            App* app = static_cast<App*>(user);
            if (files != nullptr && files[0] != nullptr) {
              std::lock_guard<std::mutex> lock(app->pickMutex_);
              app->sessionPicked_ = files[0];
              app->sessionPickedReady_ = true;
            }
          },
          this, window_, kZip, 1, where.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      open = false;
    }
    if (!sessionMessage_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, sessionOk_ ? kGood : kBad);
      ImGui::TextWrapped("%s", sessionMessage_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    Heading(("What's inside (" + reports::SizeText(sessionBundle_.size) + ")").c_str());
    if (ImGui::BeginTable("##sessionfiles", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, 120 * scale_)))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("What it is", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (const reports::BundleFile& f : sessionBundle_.files) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(f.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(f.from.empty() ? "small" : reports::SizeText(f.size).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("%s", f.description.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::EndTable();
    }
  }
  if (sessions_.empty() && ImGui::Button("Close")) {
    open = false;
  }
  if (!open) {
    ImGui::CloseCurrentPopup();
    sessionOpen_ = false;
  }
  ImGui::EndPopup();
}

void App::DrawReportsPage() {
  PageTitle("Reports", "Reports help find problems in the game. They stay on this PC, in their folder: nothing is "
                       "ever sent anywhere. To share one, export it and send the file yourself.");
  LookAtReports(false);
  if (ImGui::Button("Open folder")) {
    std::error_code ec;
    fs::create_directories(game_.reportsFolder, ec);
    OpenInSystem(game_.reportsFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button("Look again")) {
    LookAtReports(true);
  }
  if (!game_.logsFolder.empty() || !game_.recordingsFolder.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Save my last play session...")) {
      sessionOpen_ = true;
      sessionSel_ = 0;
      sessions_.clear();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("The game stopped or froze and made no report? This saves the last time you played as one "
                        "file to give to the developer.");
    }
    DrawSessionWindow();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));

  if (reports_.empty()) {
    BeginCard("##noreports");
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted("No reports yet");
    ImGui::PopFont();
    Dim("If something looks wrong while you play, press F10 in the game window. The game then saves a report: a "
        "picture of the game, your recent button presses and what the game program wrote about itself, so a "
        "developer can see and replay that moment.");
    Dim("If the game crashes or stops responding, it makes a report by itself. Reports appear here.");
    EndCard();
    if (!reportMessage_.empty()) {
      ImGui::TextColored(reportOk_ ? kGood : kBad, "%s", reportMessage_.c_str());
    }
    return;
  }

  const float height = ImGui::GetContentRegionAvail().y;
  const float listWidth = std::max(260 * scale_, ImGui::GetContentRegionAvail().x * 0.38f);
  const auto kindColour = [&](reports::Report::Kind k) {
    return k == reports::Report::Kind::Key ? kBusy : k == reports::Report::Kind::Stuck ? kWarn : kBad;
  };

  ImGui::BeginChild("##reportlist", ImVec2(listWidth, height), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
  for (const reports::Report& r : reports_) {
    ImGui::PushID(r.name.c_str());
    const bool selected = r.name == reportSel_;
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##r", selected, 0, ImVec2(0, lineH * 2 + 6 * scale_))) {
      reportSel_ = r.name;
      reportMessage_.clear();
    }
    if (ImGui::IsItemHovered() && !r.moment.empty()) {
      ImGui::SetTooltip("%s", r.moment.c_str());
    }
    if (ImGui::IsItemFocused() && !selected && ImGui::GetIO().NavVisible) {
      reportSel_ = r.name;
      reportMessage_.clear();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = pos.x + 6 * scale_;
    dl->AddText(ImVec2(x, pos.y + 2 * scale_), U32(kText), r.when.c_str());
    const char* kind = reports::KindName(r.kind);
    const float kw = ImGui::CalcTextSize(kind).x;
    const float right = pos.x + ImGui::GetContentRegionAvail().x - 6 * scale_;
    dl->AddText(ImVec2(right - kw, pos.y + 2 * scale_), U32(kindColour(r.kind)), kind);
    const std::string second = (r.moment.empty() ? std::string("moment unknown") : r.moment) + "  -  " +
                               reports::SizeText(r.size);
    dl->PushClipRect(ImVec2(x, pos.y), ImVec2(right, pos.y + lineH * 2 + 8 * scale_), true);
    dl->AddText(ImVec2(x, pos.y + 4 * scale_ + lineH), U32(kDim), second.c_str());
    dl->PopClipRect();
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::SameLine(0, 12 * scale_);

  ImGui::BeginChild("##report", ImVec2(0, height), ImGuiChildFlags_NavFlattened);
  const reports::Report* r = SelectedReport();
  if (r == nullptr) {
    Dim("Choose a report on the left.");
    ImGui::EndChild();
    return;
  }
  ImGui::PushFont(fonts_.heading);
  ImGui::TextColored(kindColour(r->kind), "%s", reports::KindName(r->kind));
  ImGui::SameLine();
  ImGui::TextUnformatted(r->when.c_str());
  ImGui::PopFont();
  Dim(reports::KindExplained(r->kind));
  if (!r->happened.empty()) {
    ImGui::TextWrapped("What happened: %s", r->happened.c_str());
  }
  if (!r->moment.empty()) {
    ImGui::TextWrapped("Where in the game: %s", r->moment.c_str());
  }
  ImGui::Spacing();
  bool hasShot = false, hasDump = false;
  for (const reports::File& f : r->files) {
    hasShot |= Lower(f.name) == "screenshot.png";
    hasDump |= f.dump;
  }

  Heading("Export");
  BeginCard("##export");
  Dim("Makes one .zip file of this report, with a name and in a place you choose, to send to a developer "
      "yourself. The report here stays as it is.");
  ImGui::Checkbox((std::string("Hide my ") + sys::UserNameWords()).c_str(), &exportOptions_.hideUserName);
  Dim(std::string("Your ") + sys::UserNameWords() +
      " appears in folder names in the log, the settings and the report's info (" + sys::HomeFolderExample() +
      "). With this on, the exported copy says %USERNAME% instead.");
  if (!hasDump) {
    ImGui::BeginDisabled();
  }
  bool dump = exportOptions_.includeDump && hasDump;
  if (ImGui::Checkbox("Include the crash dump", &dump)) {
    exportOptions_.includeDump = dump;
  }
  if (!hasDump) {
    ImGui::EndDisabled();
  }
  Dim(hasDump ? "A crash dump is a snapshot of the game program's memory when it crashed. It helps a lot in finding "
                "the cause, but it may contain bits of whatever was in memory (your user name too), so it is left "
                "out unless you tick this."
              : "This report has no crash dump (only crashes, stops and hangs make one).");
  Dim("Always included: the picture of the game (the screenshot shows only the game window), the recording of "
      "your button presses, the log, your settings and mod list.");
  ImGui::Spacing();
  if (ImGui::Button("Export...")) {
    static const SDL_DialogFileFilter kZip[] = {{"Zip files", "zip"}};
    const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
    const std::string where = Utf8(start / PathOf(reports::ExportName(*r)));
    reportMessage_.clear();
    SDL_ShowSaveFileDialog(
        [](void* user, const char* const* files, int) {
          App* app = static_cast<App*>(user);
          if (files != nullptr && files[0] != nullptr) {
            std::lock_guard<std::mutex> lock(app->pickMutex_);
            app->exportPicked_ = files[0];
            app->exportPickedReady_ = true;
          }
        },
        this, window_, kZip, 1, where.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete...")) {
    ImGui::OpenPopup("Delete this report?");
  }
  if (ImGui::BeginPopupModal("Delete this report?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete the report of %s (%s)?", r->when.c_str(), reports::KindName(r->kind));
    Dim("Its folder and the zip file next to it are deleted for good. Copies you exported stay.");
    if (ImGui::Button("Delete it")) {
      ImGui::CloseCurrentPopup();
      DeleteReport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep it")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!reportMessage_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, reportOk_ ? kGood : kBad);
    ImGui::TextWrapped("%s", reportMessage_.c_str());
    ImGui::PopStyleColor();
  }
  EndCard();
  r = SelectedReport();
  if (r == nullptr) {
    ImGui::EndChild();
    return;
  }
  ImGui::Dummy(ImVec2(0, 6 * scale_));

  if (reportShotFor_ != r->name) {
    if (reportShot_ != nullptr) {
      SDL_DestroyTexture(reportShot_);
      reportShot_ = nullptr;
    }
    reportShotFor_ = r->name;
    const icon::Image shot = hasShot ? LoadPng(r->folder / L"screenshot.png") : icon::Image();
    reportShot_ = MakeTexture(renderer_, shot);
    reportShotW_ = shot.width;
    reportShotH_ = shot.height;
  }
  if (reportShot_ != nullptr && reportShotW_ > 0 && reportShotH_ > 0) {
    const float w = std::min(ImGui::GetContentRegionAvail().x, 360 * scale_);
    const float h = w * static_cast<float>(reportShotH_) / static_cast<float>(reportShotW_);
    ImGui::Image(TexId(reportShot_), ImVec2(w, std::min(h, 300 * scale_)));
    ImGui::Spacing();
  }

  Heading("What's inside");
  if (ImGui::BeginTable("##files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("What it is", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const reports::File& f : r->files) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(f.name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(reports::SizeText(f.size).c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::PushStyleColor(ImGuiCol_Text, kDim);
      ImGui::TextWrapped("%s", f.description.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  if (!r->info.empty() && ImGui::TreeNode("The report's info.txt")) {
    std::string info = r->info;
    ImGui::InputTextMultiline("##info", &info, ImVec2(-1, 220 * scale_), ImGuiInputTextFlags_ReadOnly);
    ImGui::TreePop();
  }

  ImGui::EndChild();
}

void App::DrawBottomBar() {
  const float playWidth = 220 * scale_, playHeight = 56 * scale_;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale_, 12 * scale_));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale_, 5 * scale_));
  ImGui::BeginChild("##bottom", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  const float top = ImGui::GetCursorPosY();
  const float label = 62 * scale_;
  const auto Label = [&](const char* text) {
    const float x = ImGui::GetCursorPosX();
    ImGui::PushFont(fonts_.caption);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kFaint, "%s", text);
    ImGui::PopFont();
    ImGui::SameLine(x + label);
  };
  ImGui::BeginGroup();
  const std::string path = DiscPath();
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    const bool have = bg_.haveVerdict && bg_.discPath == path;
    Label("DISC");
    if (path.empty()) {
      ImGui::TextColored(kWarn, "none chosen");
    } else {
      const std::string name = Utf8(PathOf(path).filename());
      const ImVec4 color = !have ? kBusy
                           : bg_.verdict.kind == DiscVerdict::Kind::Problem ? kBad
                           : bg_.verdict.kind == DiscVerdict::Kind::Warning ? kWarn
                                                                           : kGood;
      const char* state = !have ? "checking..."
                          : bg_.verdict.kind == DiscVerdict::Kind::Problem ? "can't be used"
                          : bg_.verdict.kind == DiscVerdict::Kind::Warning ? "changed or damaged"
                                                                          : "OK";
      ImGui::TextColored(color, "%s (%s)", name.c_str(), state);
    }
  }
  Label("PRESET");
  if (registry_.IsOriginal()) {
    ImGui::TextUnformatted("Original: the game exactly as on the GameCube");
  } else {
    const size_t n = registry_.NotOriginal().size();
    ImGui::Text("%s: %zu setting%s different from the GameCube game", ui::PresetName(registry_.CurrentPreset()), n,
                n == 1 ? "" : "s");
  }
  int on = 0;
  for (const auto& m : mods_.Active().mods) {
    on += m.second ? 1 : 0;
  }
  Label("MODS");
  if (on == 0) {
    ImGui::TextUnformatted("none on");
  } else {
    ImGui::Text("%d on (list \"%s\")", on, mods_.active.c_str());
  }
  if (SlotsOn()) {
    Label("SAVE");
    ImGui::TextUnformatted(slots_.Active().name.c_str());
    for (size_t i = 0; i < slots_.slots.size() && i < slotRows_.size(); i++) {
      if (slots_.slots[i].id == slots_.Active().id) {
        const GameInfo::SaveRow& row = slotRows_[i];
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", (row.found ? row.who + (row.time.empty() ? "" : ", " + row.time)
                                                  : row.who.empty() ? std::string("empty") : row.who).c_str());
      }
    }
  }
  ImGui::EndGroup();

  const float groupBottom = ImGui::GetCursorPosY();

  const float rowsHeight = groupBottom - top;
  const float playTop = top + std::max(0.0f, (rowsHeight - playHeight) * 0.5f);
  bool stay = registry_.Bool(kStaySetting).Get();
  const float checkWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                           ImGui::CalcTextSize("Keep this window open").x;
  ImGui::SetCursorPos(ImVec2(right - playWidth - 28 * scale_ - checkWidth,
                             playTop + (playHeight - ImGui::GetFrameHeight()) * 0.5f));
  if (ImGui::Checkbox("Keep this window open", &stay)) {
    registry_.SetBool(kStaySetting, stay);
  }
  ImGui::SetCursorPos(ImVec2(right - playWidth, playTop));
  std::string why;
  const bool shadersPreparing = bg_.preparing && bg_.shadersRunning;
  bool playable = !gameRunning_ && DiscPlayable(why);
  if (playable && shadersPreparing) {
    playable = false;
    why = "Preparing the game's shaders for this PC (once; see the Disc page).";
  }
  if (PlayButton(gameRunning_ ? "Running..." : playWhenDrawn_ ? "Starting..." : shadersPreparing ? "Preparing..." : "Play",
                 ImVec2(playWidth, playHeight), playable && !playWhenDrawn_)) {
    Play(false);
  }
  if (!playable && !gameRunning_ && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", why.c_str());
  }
  ImGui::SetCursorPosY(std::max(groupBottom, playTop + playHeight + 4 * scale_));
  if (!playError_.empty()) {
    ImGui::TextColored(kBad, "%s", playError_.c_str());
  } else if (!saveError_.empty()) {
    ImGui::TextColored(kBad, "%s", saveError_.c_str());
  } else if (gameRunning_) {
    ImGui::TextColored(kGood, "The game is running. Settings changed now are used the next time it starts.");
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(2);

  if (askStartAnyway_) {
    ImGui::OpenPopup("Start anyway?");
    askStartAnyway_ = false;
  }
  if (ImGui::BeginPopupModal("Start anyway?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    std::string text;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      text = bg_.verdict.text;
    }
    ImGui::PushTextWrapPos(560 * scale_);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Start anyway")) {
      ImGui::CloseCurrentPopup();
      Play(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't start")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::Frame() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  DrawGround();
  const bool ground = GroundIndex() >= 0 && !groundTex_.empty();
  if (ground) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(kPanel.x, kPanel.y, kPanel.z, 0.76f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(34 * scale_, 26 * scale_));
  }
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  DrawHeader();
  const bool message = !playError_.empty() || !saveError_.empty() || gameRunning_;
  const int rows = SlotsOn() ? 4 : 3;
  const float bottom = rows * ImGui::GetFrameHeight() + ((rows - 1) * 5 + 2 * 12 + 4 + 6) * scale_ +
                       ImGui::GetStyle().ItemSpacing.y + (message ? ImGui::GetTextLineHeightWithSpacing() : 0.0f);
  const float height = ImGui::GetContentRegionAvail().y - bottom;
  DrawNav(height);
  ImGui::SameLine(0, 14 * scale_);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * scale_, 22 * scale_));
  ImGui::BeginChild("##page", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, 0);
  ImGui::PopStyleVar();
  switch (page_) {
  case Page::Disc: DrawDiscPage(); break;
  case Page::Extra: DrawExtraPage(); break;
  case Page::Preset: DrawPresetPage(); break;
  case Page::Settings: DrawSettingsPage(); break;
  case Page::Controls: DrawControlsPage(); break;
  case Page::Mods: DrawModsPage(); break;
  case Page::Save: DrawSavePage(); break;
  case Page::Reports: DrawReportsPage(); break;
  default: break;
  }
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  DrawBottomBar();
  DrawNotOpenedWindow();
  DrawUpdateWindows();
  ImGui::End();
  if (ground) {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
  }
}

int App::Run() {
  LoadSettings();
  options_.gameId = game_.gameId;
  options_.hidden = {kDiscSetting, kStaySetting, kIconSetting, kGroundSetting, kUpdateSetting};
  options_.hiddenPrefixes.push_back("system.extra_disc.");
  script_ = Split(game_.script, '|');
  ReloadMods();
  ExtraStart();
  LookAtSlots(true);
  if (SlotsOn()) {
    Log("save: \"" + slots_.Active().name + "\" of " + std::to_string(slots_.slots.size()) + " (" +
        Utf8(sl::RootOf(game_.saves.dataFolder, slots_.Active().id)) + ")");
  }

  SDL_SetHint(SDL_HINT_APP_NAME, game_.title.c_str());
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    Log("SDL_Init failed: " + std::string(SDL_GetError()));
    return 1;
  }
  scale_ = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale_ <= 0.0f) {
    scale_ = 1.0f;
  }
  const std::string title = (game_.windowTitle.empty() ? game_.title : game_.windowTitle) +
                            (game_.updates.version.empty() ? std::string() : " " + game_.updates.version) +
                            " - launcher";
  window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(1100 * scale_), static_cast<int>(760 * scale_),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window_ == nullptr) {
    Log("no window: " + std::string(SDL_GetError()));
    return 1;
  }
  SDL_SetWindowMinimumSize(window_, static_cast<int>(820 * scale_), static_cast<int>(560 * scale_));
  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    Log("no renderer: " + std::string(SDL_GetError()));
    return 1;
  }
  SDL_SetRenderVSync(renderer_, 1);
  ApplyIconChoice();
  iconApplied_ = "\x01";
  {
    icon::Image own;
    own.width = own.height = 128;
    own.rgba.resize(128 * 128 * 4);
    if (PortWindowIcon_OwnRGBA(128, own.rgba.data())) {
      ownIcon_ = MakeTexture(renderer_, own);
    }
    for (const GameInfo::OwnIcon& o : game_.ownIcons) {
      ownIcons_.push_back(PortWindowIcon_ResourceRGBA(o.resource, 128, own.rgba.data()) ? MakeTexture(renderer_, own)
                                                                                       : nullptr);
    }
  }
  if (!sys::HasIcons()) {
    const icon::Image own = LoadPng(sys::ProgramFolder() / L"icon.png");
    if (!own.Empty()) {
      ownIcon_ = MakeTexture(renderer_, own);
      if (SDL_Surface* surface = SDL_CreateSurfaceFrom(own.width, own.height, SDL_PIXELFORMAT_RGBA32,
                                                       const_cast<uint8_t*>(own.rgba.data()), own.width * 4)) {
        SDL_SetWindowIcon(window_, surface);
        SDL_DestroySurface(surface);
      }
    }
  }
  sys::Start();
  if (game_.offscreen) {
    SDL_SetWindowPosition(window_, -20000, -20000);
  } else {
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
  SDL_ShowWindow(window_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ApplyStyle(scale_);
  fonts_ = LoadFonts(scale_);
  options_.headingFont = fonts_.heading;
  ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer3_Init(renderer_);

  if (!DiscPath().empty()) {
    StartWorker(DiscPath());
  } else {
    StartWorker("");
  }
  FindVersions();
  Log("version " + (game_.updates.version.empty() ? std::string("?") : game_.updates.version) + " (" +
      (layout_.valid ? "player's folder, " + layout_.running : std::string("not in a player's folder")) + "); updates " +
      (game_.updates.Active() ? "available" : "not set up in this build"));
  if (game_.updates.Active()) {
    LoadUpdaterState();
    if (!updatesAsked_) {
      askUpdates_ = true;
    } else if (registry_.Bool(kUpdateSetting).Get()) {
      StartUpdateCheck(false);
    }
  }
  Log(script_.empty() ? "ready" : "ready; " + std::to_string(script_.size()) + " script steps");

  while (!quit_) {
    SDL_Event e;
    const bool busy =
        bg_.busy || up_.busy || !script_.empty() || ImGui::IsAnyItemActive() || page_ == Page::Controls;
    if (SDL_WaitEventTimeout(&e, busy ? 16 : 100)) {
      do {
        bool toImGui = true;
        switch (e.type) {
        case SDL_EVENT_QUIT: quit_ = true; break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
          if (e.window.windowID == SDL_GetWindowID(window_)) {
            quit_ = true;
          }
          break;
        case SDL_EVENT_GAMEPAD_ADDED:
          if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which); pad != nullptr) {
            pads_.push_back(pad);
            UpdatePads();
          }
          break;
        case SDL_EVENT_GAMEPAD_REMOVED:
          for (auto it = pads_.begin(); it != pads_.end(); ++it) {
            if (SDL_GetGamepadID(*it) == e.gdevice.which) {
              SDL_CloseGamepad(*it);
              pads_.erase(it);
              UpdatePads();
              break;
            }
          }
          break;
        case SDL_EVENT_KEY_DOWN:
          if (controls_.Listening()) {
            if (!e.key.repeat) {
              controls_.FeedKey(registry_, static_cast<uint16_t>(e.key.scancode));
            }
            toImGui = false;
          } else if (controls_.Testing()) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
              controls_.SetTesting(false);
            }
            toImGui = false;
          }
          break;
        case SDL_EVENT_KEY_UP:
          toImGui = !controls_.Listening() && !controls_.Testing();
          break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
          if (controls_.Listening()) {
            controls_.FeedPadButton(registry_, e.gbutton.button);
          }
          break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
          if (controls_.Listening()) {
            controls_.FeedPadAxis(registry_, e.gaxis.axis, e.gaxis.value / 32767.0f);
          }
          break;
        default: break;
        }
        if (toImGui) {
          ImGui_ImplSDL3_ProcessEvent(&e);
        }
      } while (SDL_PollEvent(&e));
    }
    ExtraPoll();
    {
      std::lock_guard<std::mutex> lock(pickMutex_);
      if (pickedReady_) {
        pickedReady_ = false;
        SelectDisc(picked_);
      }
      if (extraPickedReady_) {
        extraPickedReady_ = false;
        ExtraSelectDisc(extraPickedFor_, extraPicked_);
      }
      if (exportPickedReady_) {
        exportPickedReady_ = false;
        ExportReport(exportPicked_);
        Log(reportMessage_);
      }
      if (sessionPickedReady_) {
        sessionPickedReady_ = false;
        SaveSession(sessionPicked_);
        Log(sessionMessage_);
      }
    }
    {
      std::string file;
      int kind = 0;
      {
        std::lock_guard<std::mutex> lock(pickMutex_);
        std::swap(file, slotPicked_);
        std::swap(kind, slotPickedKind_);
      }
      if (kind == 1) {
        ImportSlot(file);
      } else if (kind == 2) {
        ExportSlot(file);
      }
    }
    PollGame();
    if (playWhenDrawn_ && !bg_.drawing) {
      playWhenDrawn_ = false;
      Play(true);
    }
    FollowIcon();
    FollowUpdate();
    RunScript();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    Frame();
    ImGui::Render();
    SDL_SetRenderScale(renderer_, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer_, 0x11, 0x0B, 0x1C, 0xFF);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    if (!shotFile_.empty()) {
      Screenshot(shotFile_);
      shotFile_.clear();
    }
    SDL_RenderPresent(renderer_);
    SaveSettingsIfChanged();
  }

  SaveSettingsIfChanged();
  StopWorker();
  StopUpdate();
  if (gameProcess_ != nullptr) {
    sys::Close(gameProcess_);
  }
  choiceIcons_.push_back(ownIcon_);
  choiceIcons_.push_back(reportShot_);
  choiceIcons_.insert(choiceIcons_.end(), ownIcons_.begin(), ownIcons_.end());
  choiceIcons_.insert(choiceIcons_.end(), groundTex_.begin(), groundTex_.end());
  for (SDL_Texture* tex : choiceIcons_) {
    if (tex != nullptr) {
      SDL_DestroyTexture(tex);
    }
  }
  sys::End();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  for (SDL_Gamepad* pad : pads_) {
    SDL_CloseGamepad(pad);
  }
  SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
  SDL_Quit();
  Log("closed");
  return 0;
}

}

int Run(const GameInfo& game) {
  App app(game);
  return app.Run();
}

}
