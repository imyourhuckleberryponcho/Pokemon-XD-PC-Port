#include "launcher.h"
#include "launcher_data.h"
#include "launcher_extra.h"
#include "launcher_modtools.h"
#include "launcher_modupdate.h"
#include "launcher_pad.h"
#include "launcher_reports.h"
#include "launcher_sys.h"

#include <mods.h>
#include <mod_layout.h>
#include <save_backups.h>
#include <save_slots.h>
#include <i18n.h>
#include <port_user_folder.h>
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
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <ctime>
#include "presence.h"
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
using engine::i18n::Tr;

std::string Lbl(const char* translated, const char* id) { return std::string(translated) + "###" + id; }

std::string ProgramsName() {
  const std::string name = sys::ProgramsFolderName();
  return name == "Start menu" ? Tr("Start menu") : name == "applications menu" ? Tr("applications menu") : name;
}

std::string SysWord(const char* word) {
  const std::string w = word;
  if (w == "the recycle bin") return Tr("the recycle bin");
  if (w == "the trash") return Tr("the trash");
  if (w == "Windows user name") return Tr("Windows user name");
  if (w == "user name") return Tr("user name");
  return w;
}

const char* KindWord(reports::Report::Kind kind) {
  switch (kind) {
  case reports::Report::Kind::Crash: return Tr("Crash");
  case reports::Report::Kind::Stop: return Tr("Stopped");
  case reports::Report::Kind::Stuck: return Tr("Stuck");
  default: return Tr("F10 report");
  }
}
const char* KindText(reports::Report::Kind kind) {
  switch (kind) {
  case reports::Report::Kind::Crash: return Tr("The game crashed, and made this report by itself.");
  case reports::Report::Kind::Stop: return Tr("The game stopped itself because something went wrong, and made this report.");
  case reports::Report::Kind::Stuck: return Tr("The game stopped moving (it hung), and made this report by itself.");
  default: return Tr("You pressed F10 in the game.");
  }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
std::string Fmt(const char* fmt, ...) {
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  return buf;
}

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

bool Hot(ImGuiHoveredFlags flags = 0) {
  return ImGui::IsItemHovered(flags) || (ImGui::GetIO().NavVisible && ImGui::IsItemFocused());
}

std::string HexId(unsigned v) {
  char b[16];
  std::snprintf(b, sizeof b, "%08X", v);
  return b;
}

constexpr float kControllerScale = 1.2f;

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
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x2027, 0x20AC, 0x20AC, 0x2190, 0x2193, 0};
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  std::error_code ec;
  static ImVector<ImWchar> jpRanges;
  const fs::path jpFont = own / L"fonts" / L"NotoSansJP-Regular-subset.ttf";
  const bool haveJp = fs::exists(jpFont, ec);
  if (haveJp) {
    ImFontGlyphRangesBuilder builder;
    const std::string_view ja = engine::i18n::EmbeddedLocaleText("ja");
    builder.AddText(std::string(ja).c_str());
    static const ImWchar kKana[] = {0x3000, 0x30FF, 0xFF00, 0xFF5F, 0};
    builder.AddRanges(kKana);
    builder.AddText("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    builder.BuildRanges(&jpRanges);
  }
  const auto mergeJp = [&](float size) {
    if (!haveJp || jpRanges.Size == 0) return;
    ImFontConfig merge = config;
    merge.MergeMode = true;
    io.Fonts->AddFontFromFileTTF(Utf8(jpFont).c_str(), size * scale, &merge, jpRanges.Data);
  };
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
    mergeJp(17.0f);
    f.heading = add(strong, 20.0f);
    mergeJp(20.0f);
    f.nav = add(strong, 16.5f);
    mergeJp(16.5f);
    f.caption = add(c.regular, 15.0f);
    mergeJp(15.0f);
    f.play = add(display, 23.0f);
    mergeJp(23.0f);
    f.title = add(display, 27.0f);
    mergeJp(27.0f);
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
const char* PageName(int page) {
  switch (page) {
  case 0: return Tr("Disc");
  case 1: return Tr("Extra assets");
  case 2: return Tr("Preset");
  case 3: return Tr("Settings");
  case 4: return Tr("Controls");
  case 5: return Tr("Mods");
  case 6: return Tr("Save");
  case 7: return Tr("Reports");
  default: return "";
  }
}
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
  explicit App(const GameInfo& game) : game_(game), files_(game.settingsFolder / L"launcher_recent_folders.txt") {}
  ~App() {
    StopWorker();
    StopUpdate();
    StopOpenRead();
    StopImportScan();
    ExtraStop();
    StopDataWork();
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
  std::string wantExtraTab_;
  std::string curExtraTab_;
  std::vector<std::string> extraTabIds_;
  bool quit_ = false;

  Background bg_;
  std::mutex pickMutex_;
  std::string freshPicked_;
  std::string picked_;
  bool pickedReady_ = false;

  em::ModList mods_;
  std::vector<em::ModFolder> found_;
  std::vector<em::Message> modMessages_;
  std::string modError_;
  std::string newProfile_;
  modupd::Updater modUpd_;
  modtools::Tools modTools_;
  std::string modToolPicked_;
  bool modToolPickedReady_ = false;
  std::function<void(const std::string&)> modToolPickDone_;
  void InitModTools();
  em::Review review_;
  bool reviewDirty_ = true;
  std::map<std::string, std::string> offHints_;
  std::map<std::string, std::string> worksWith_;
  std::map<std::string, engine::modupdate::PreviousInfo> prevInfo_;
  std::vector<std::string> updaterLines_;
  std::string turnOnRequest_;
  std::string modPicked_;
  bool modPickedReady_ = false;
  std::function<void(const std::string&)> modPickDone_;
  void InitModUpdater();
  void RebuildReview();
  void DrawModUpdateWindows();
  void DrawModToolWindows();

  sys::Process* gameProcess_ = nullptr;
  bool gameRunning_ = false;
  std::unique_ptr<engine::presence::Client> presence_;
  std::chrono::steady_clock::time_point presenceAt_{};
  int64_t presenceStart_ = 0;
  void PresenceFrame();
  std::string playError_;
  bool askStartAnyway_ = false;
  bool playWhenDrawn_ = false;

  std::vector<std::string> script_;
  size_t step_ = 0;
  int framesSinceStep_ = 0;
  std::string waitFor_;
  std::string shotFile_;
  std::vector<SDL_Gamepad*> pads_;
  pad::Input padIn_;
  pad::Keyboard osk_;
  pad::FileBrowser files_;
  bool controller_ = false;
  bool controllerFixed_ = false;
  float baseScale_ = 1.0f;
  bool rescale_ = false;
  bool askMenu_ = false;
  bool focusPage_ = false;
  bool focusInNav_ = false;
  int enterDelay_ = 0;
  bool enterPage_ = false;
  std::string curTab_;
  std::vector<std::string> tabIds_;
  uint64_t listenStart_ = 0;
  std::string playWhy_;
  float promptH_ = 0;
  unsigned playId_ = 0;
  std::deque<pad::Btn> padQueue_;
  int padGap_ = 0;
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
  bool importOpen_ = false;
  struct ImportScan {
    std::thread thread;
    std::atomic<bool> busy{false};
    std::mutex mutex;
    std::vector<GameInfo::ImportOffer> offers;
    bool have = false;
    bool logged = true;
  } importScan_;
  GameInfo::ImportOffer importFile_;
  bool importHaveFile_ = false;
  int importSel_ = -1;
  std::string importError_;
  void ImportFind();
  void ImportChooseFile();
  void StopImportScan();
  bool ImportPick(const std::string& arg);
  bool ImportConfirm(int answer = -1);
  bool ImportCommit(const fs::path& file, const GameInfo::ImportCheck& check);
  void DrawImport(bool locked);
  std::string NamesDisc();
  std::string tradeFor_;
  fs::path tradeFile_;
  GameInfo::Saves::TradeView tradeView_;
  bool tradeHave_ = false;
  std::string tradeError_;
  int tradeMine_ = -1, tradeTheirs_ = -1;
  std::string tradeMessage_;
  bool tradeOk_ = false;
  bool TradePick(const std::string& file);
  bool TradeConfirm();
  void DrawTrade(const sl::Slot& slot, bool locked);
  std::string monsFor_;
  fs::path monsFile_;
  GameInfo::Saves::MonsView monsView_;
  bool monsHave_ = false;
  std::string monsError_;
  std::vector<char> monsPicked_;
  int monsKind_ = 0;
  bool MonsPick(const std::string& file);
  bool MonsConfirm();
  void DrawMons(const sl::Slot& slot, bool locked);
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
  bool askLooks_ = false;
  bool closeLooks_ = false;
  std::string pickLooks_;
  std::string looksMessage_;
  bool askSpoilers_ = false;
  bool closeSpoilers_ = false;
  UpdateWork up_;
  engine::update::Layout layout_;
  bool settingsFileFound_ = false;
  bool presetAsked_ = false;
  bool presetPending_ = false;
  bool askPreset_ = false;
  bool welcomeFromStart_ = false;

  struct DataWork {
    enum class Phase { None, Ask, Copying, Checking, Finished, Done };
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> busy{false}, stop{false};
    std::atomic<uint64_t> files{0}, bytes{0};
    std::atomic<Phase> phase{Phase::None};
    bool toPortable = false;
    bool ok = false;
    std::string error;
    fs::path from, to;
    uint64_t totalFiles = 0, totalBytes = 0;
  } dataWork_;
  bool dataAsked_ = false;
  bool askData_ = false;
  bool openDataMove_ = false, openDataRemove_ = false;
  int dataRemoveState_ = 0;
  int dataSavesToLose_ = 0;
  std::string dataMessage_;
  bool dataOk_ = false;
  std::string dataExportPicked_;
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
    extra::SecondStatus second;
    extra::SecondStatus third;
    bool workLeft = false;
    std::vector<std::string> contents;
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
  void ExtraMake(int index, bool confirmed = false);
  void ExtraCancel(int index);
  void ExtraRemove(int index);
  int extraAsk_ = -1;
  bool extraWarnClose_ = false;
  int extraWarn_ = -1;
  void ExtraPoll();
  bool ExtraBusy() const;
  bool ExtraRunning() const;
  std::string StageText(const std::string& id) const;
  void DrawExtraPage();
  void DrawExtraRow(int i);
  void DrawExtraHdTextures();
  bool languageChanged_ = false;
  std::string languageApplied_ = "\x01";
  void FollowLanguage();
  bool LanguageRowOn() const {
    return !game_.languages.setting.empty() && game_.languages.offered &&
           registry_.Find(game_.languages.setting) != es::kNoSetting;
  }
  fs::path ExtraFolder(const GameInfo::ExtraSource& s) const { return game_.settingsFolder / "extra_assets" / PathOf(s.id); }
  fs::path ExtraFolder3(const GameInfo::ExtraSource& s) const {
    return s.thirdFolder.empty() ? fs::path() : game_.settingsFolder / "extra_assets" / PathOf(s.thirdFolder);
  }
  fs::path ExtraFolder2(const GameInfo::ExtraSource& s) const {
    return s.secondFolder.empty() ? fs::path() : game_.settingsFolder / "extra_assets" / PathOf(s.secondFolder);
  }

  void LoadSettings();
  void SaveSettingsIfChanged();
  std::string DiscPath() const { return registry_.Text(kDiscSetting); }
  std::string RememberedPlainDisc() const {
    std::ifstream in(game_.settingsFolder / "plain_game_disc.txt", std::ios::binary);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
  }
  void RememberPlainDisc(const std::string& path) const {
    if (game_.playerFilesReadOnly || RememberedPlainDisc() == path) return;
    std::error_code ec;
    fs::create_directories(game_.settingsFolder, ec);
    std::ofstream out(game_.settingsFolder / "plain_game_disc.txt", std::ios::binary | std::ios::trunc);
    out << path << "\n";
  }

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
  void StartUpdateInstall(bool freshFolder = false);
  fs::path freshRoot_;
  void StopUpdate();
  void FollowUpdate();
  void SkipUpdate();
  bool GoBackVersion(std::string& error);
  void RestartLauncher();
  void DrawUpdateCard();
  void DrawUpdateWindows();
  void DrawWelcomeWindow();
  PortUserLocation DataLocation() const;
  static const char* DataKindName(PortUserKind kind);
  bool DataLooksFresh() const;
  void MaybeAskDataFolder();
  bool DataLocked(std::string& why) const;
  void AnswerDataFirst(bool portable);
  void OpenDataFolder();
  void RestartForData();
  void AskMoveData(bool toPortable);
  void StopDataWork();
  void StartMoveData();
  void FollowDataWork();
  void ExportAllSaves(const fs::path& folder);
  int CountSavesToLose() const;
  void AskRemoveData();
  void RemoveAllData();
  void DrawDataWindows();
  void DrawDataCard();
  void ShowFolderDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window, const char* start);
  void LoadLauncherState();
  void SaveLauncherState();
  void ChoosePreset(es::Preset preset);
  void OpenInSystem(const fs::path& what);
  void DrawNotOpenedWindow();
  std::string notOpened_;

  void SetController(bool on);
  void ApplyScale();
  void ShowOpenDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window, const SDL_DialogFileFilter* filters,
                      int count, const char* start, bool many);
  void ShowSaveDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window, const SDL_DialogFileFilter* filters,
                      int count, const char* start);
  void BrowseFor(pad::FileBrowser::Mode mode, SDL_DialogFileCallback callback, void* user,
                 const SDL_DialogFileFilter* filters, int count, const char* start);
  void PadShortcuts();
  void DrawPadUi();
  void DrawFileBrowser();
  std::vector<pad::Prompt> CurrentPrompts();
  bool BackPressed() const { return controller_ && padIn_.Pressed(pad::Btn::Cancel) && !osk_.Open(); }
  std::vector<pad::PadInfo> PadInfos() const;

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
  void LookAtAdapter();
  uint64_t adapterLookedAt_ = 0;
  engine::controls::AdapterState lastAdapterState_ = engine::controls::AdapterState::Unknown;
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
  fs::path SavesFolder() const {
    std::string edition;
    {
      std::lock_guard<std::mutex> lock(editionMutex_);
      edition = savesEdition_;
    }
    return game_.saves.dataFolderFor ? game_.saves.dataFolderFor(edition) : game_.saves.dataFolder;
  }
  mutable std::mutex editionMutex_;
  std::string savesEdition_;
  std::string slotsEdition_;
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
                                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
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
  const bool clicked = ImGui::InvisibleButton("##play", size, ImGuiButtonFlags_EnableNav);
  const bool hovered = Hot(), held = ImGui::IsItemActive();
  playId_ = ImGui::GetItemID();
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
  const bool loaded = registry_.Load(game_.settingsFolder);
  settingsFileFound_ = loaded;
  if (!game_.discOverride.empty() && registry_.SetText(kDiscSetting, game_.discOverride)) {
    Log("disc from the command line: " + game_.discOverride);
  }
  FollowLanguage();
  if (loaded) {
    Log("read " + Utf8(game_.settingsFolder / es::Registry::kFileName));
    for (const std::string& id : registry_.LastLoad().unknownIds) {
      settingsNotes_.push_back(Fmt(Tr("settings.ini has \"%s\", which this version doesn't know (kept as it is)"), id.c_str()));
    }
  } else {
    Log("no " + Utf8(game_.settingsFolder / es::Registry::kFileName) + " yet: the defaults (Original)");
  }
  savedChanges_ = registry_.Changes();
}

void App::FollowLanguage() {
  if (game_.languages.setting.empty() || registry_.Find(game_.languages.setting) == es::kNoSetting) {
    return;
  }
  const std::string value = registry_.ValueText(registry_.Find(game_.languages.setting));
  if (value == languageApplied_) {
    return;
  }
  languageApplied_ = value;
  const bool table = engine::i18n::SetLanguage(value);
  Log("language: " + value + (table ? " (texts of this port translated)" : " (English)"));
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
    saveError_ = Fmt(Tr("Your settings couldn't be saved to %s."), Utf8(game_.settingsFolder / es::Registry::kFileName).c_str());
  }
}

void App::SelectDisc(const std::string& path) {
  Log("disc: " + path);
  if (!registry_.SetText(kDiscSetting, path)) {
    playError_ = Fmt(Tr("This file name can't be used: %s"), path.c_str());
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
      if (verdict.kind == DiscVerdict::Kind::Good && verdict.edition.empty()) RememberPlainDisc(discPath);
      if (usable) {
        {
          std::lock_guard<std::mutex> lock(editionMutex_);
          savesEdition_ = verdict.edition;
        }
        if (game_.saves.setEdition) game_.saves.setEdition(verdict.edition);
      }
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
            bg_.iconNote = Tr("Drawing more icons and backgrounds from the game's own 3D models (about a minute; once "
                              "for this disc)...");
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
      note = error.empty() ? std::string(Tr("No icon could be made from this disc."))
                           : Fmt(Tr("No icon could be made from this disc: %s"), error.c_str());
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
        note = Fmt(Tr("The icons couldn't be saved in %s."), Utf8(game_.iconFolder).c_str());
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
                                                  Fmt(Tr("%s (rendered)"), Utf8(e.path().stem()).c_str()), std::move(img)});
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
      c.name = Fmt(Tr("%s (3D render)"), name.c_str());
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
    note = Fmt(Tr("Made from your disc, only on this PC: %s"), Utf8(game_.iconFolder).c_str());
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
    shortcutMessage_ = Fmt(Tr("The system didn't say where your %s is."), desktop ? Tr("desktop") : ProgramsName().c_str());
    return;
  }
  shortcutOk_ = sys::MakeShortcut(folder, game_.title, game_.title + " " + game_.subtitle, game_.shortcutTarget,
                             chosenIcon_, shortcutMessage_);
  Log(shortcutMessage_);
}

bool App::DiscPlayable(std::string& why) {
  const std::string path = DiscPath();
  if (path.empty()) {
    why = Tr("Choose your disc image first (Disc page).");
    return false;
  }
  std::lock_guard<std::mutex> lock(bg_.mutex);
  if (!bg_.haveVerdict || bg_.discPath != path) {
    why = Tr("The disc is being checked...");
    return false;
  }
  if (bg_.verdict.kind == DiscVerdict::Kind::Problem) {
    why = Tr("This disc image can't be used (see the Disc page).");
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
  reviewDirty_ = true;
  for (const std::string& id : fresh) {
    const bool pack = std::any_of(found_.begin(), found_.end(), [&](const em::ModFolder& f) { return f.texturePack && f.manifest.id == id; });
    modMessages_.push_back({em::Message::Level::Info,
                            pack ? Fmt(Tr("New texture pack found: %s (added, switched on: it only changes pictures)"), id.c_str())
                                 : Fmt(Tr("New mod found: %s (added, switched off)"), id.c_str())});
  }
  if (!fresh.empty() || (!exists && !found_.empty())) {
    SaveMods();
  }
}

void App::SaveMods() {
  reviewDirty_ = true;
  if (game_.playerFilesReadOnly) {
    Log("mods: " + game_.readOnlyNote);
    std::fprintf(stderr, "[mods] %s (mods.ini)\n", game_.readOnlyNote.c_str());
    modError_.clear();
    return;
  }
  if (em::WriteModListFile(game_.modListFile, mods_)) {
    modError_.clear();
  } else {
    modError_ = Fmt(Tr("The mod list couldn't be saved to %s."), Utf8(game_.modListFile).c_str());
  }
}

bool PrimaryButton(const char* label);

void App::InitModUpdater() {
  modupd::Host host;
  host.modsFolder = game_.modsFolder;
  host.stateFolder = game_.settingsFolder / L"mod_updates";
  host.rules = game_.modRules;
  host.testRun = game_.playerFilesReadOnly;
  host.mods = &mods_;
  host.found = &found_;
  host.discGameId = [this] {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    return bg_.haveVerdict ? em::MakeTarget(bg_.verdict.gameId, Lower(bg_.verdict.edition)) : std::string();
  };
  host.cardRoots = [this] {
    std::vector<std::pair<std::string, fs::path>> roots;
    if (SlotsOn()) {
      const sl::Index index = sl::Load(SavesFolder());
      for (const sl::Slot& slot : index.slots) roots.emplace_back(slot.name, sl::RootOf(SavesFolder(), slot.id));
    }
    return roots;
  };
  host.saveList = [this] { SaveMods(); };
  host.reload = [this] { ReloadMods(); };
  host.log = [](const std::string& text) { Log(text); };
  host.say = [this](const std::string& text) {
    updaterLines_.push_back(text);
    if (updaterLines_.size() > 6) updaterLines_.erase(updaterLines_.begin());
    Log("mods: " + text);
  };
  host.chooseFile = [this](const fs::path& name, bool save, std::function<void(const std::string&)> done) {
    modPickDone_ = std::move(done);
    static SDL_DialogFileFilter kJson[1];
    kJson[0] = {Tr("Mod lists (*.json)"), "json"};
    const auto callback = [](void* user, const char* const* files, int) {
      App* app = static_cast<App*>(user);
      if (files != nullptr && files[0] != nullptr) {
        std::lock_guard<std::mutex> lock(app->pickMutex_);
        app->modPicked_ = files[0];
        app->modPickedReady_ = true;
      }
    };
    if (save) {
      const std::string where = Utf8(sys::KnownFolder(sys::Folder::Documents, false) / name);
      ShowSaveDialog(callback, this, window_, kJson, 1, where.c_str());
    } else {
      ShowOpenDialog(callback, this, window_, kJson, 1, nullptr, false);
    }
  };
  modUpd_.Init(host);
}

void App::RebuildReview() {
  std::string game;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    if (bg_.haveVerdict) game = em::MakeTarget(bg_.verdict.gameId, Lower(bg_.verdict.edition));
  }
  review_ = em::ReviewList(mods_.Active(), found_, game);
  offHints_.clear();
  worksWith_.clear();
  for (const em::ModFolder& f : found_) {
    if (!f.texturePack) worksWith_[f.manifest.id] = em::SupportText(em::SupportOf(f.folder, f.manifest));
  }
  prevInfo_.clear();
  std::vector<std::string> onIds;
  for (const auto& [id, on] : mods_.Active().mods) {
    if (on) onIds.push_back(id);
  }
  for (const auto& [id, on] : mods_.Active().mods) {
    const em::ModFolder* mod = nullptr;
    for (const em::ModFolder& f : found_) {
      if (Lower(f.manifest.id) == Lower(id)) mod = &f;
    }
    if (mod == nullptr) continue;
    prevInfo_[id] = modUpd_.Previous(mod->manifest.id);
    if (on) continue;
    const em::Requirements req = em::ResolveRequirements(*mod, found_, onIds, game);
    std::string hint;
    for (const auto& m : req.missing) {
      hint += (hint.empty() ? "" : " ") + (m.have.empty() ? Fmt(Tr("Needs \"%s\", which isn't installed."), m.id.c_str())
                                                          : Fmt(Tr("Needs \"%s\" in version %s %s; you have %s."), m.id.c_str(), m.op.c_str(),
                                                                m.version.c_str(), m.have.c_str()));
    }
    for (const std::string& c : req.conflicts) hint += (hint.empty() ? "" : " ") + c;
    if (!game.empty() && !em::SupportsGame(mod->manifest, game)) {
      hint += (hint.empty() ? "" : " ") + em::NotForTargetText(mod->manifest, game);
    }
    if (!hint.empty()) offHints_[id] = hint;
  }
  reviewDirty_ = false;
}

void App::DrawModUpdateWindows() {
  modupd::Ui ui;
  ui.scale = scale_;
  ui.good = kGood;
  ui.warn = kWarn;
  ui.bad = kBad;
  ui.dim = kDim;
  ui.heading = [this](const char* t) { Heading(t); };
  ui.dimText = [this](const std::string& t) { Dim(t); };
  ui.primary = [](const char* label) { return PrimaryButton(label); };
  ui.back = [this] { return BackPressed(); };
  ui.closeNow = [this] { return closeWindows_; };
  modUpd_.Draw(ui);
}

void App::InitModTools() {
  modtools::Host host;
  host.modsFolder = game_.modsFolder;
  host.rules = game_.modRules;
  host.found = &found_;
  host.discPath = [this] { return DiscPath(); };
  host.discBusy = [this] { return bg_.busy.load(); };
  host.openDisc = game_.openModDisc;
  host.closeDisc = game_.closeModDisc;
  host.scratchFolder = game_.settingsFolder / L"mod_tools";
  host.openFolder = [this](const fs::path& folder) { OpenInSystem(folder); };
  host.turnOn = [this](const std::string& id) { modUpd_.BeginTurnOn(id); };
  host.say = [this](const std::string& text) {
    updaterLines_.push_back(text);
    if (updaterLines_.size() > 6) updaterLines_.erase(updaterLines_.begin());
    Log("mods: " + text);
  };
  host.log = [](const std::string& text) { Log(text); };
  host.chooseZip = [this](const std::string& name, std::function<void(const std::string&)> done) {
    modToolPickDone_ = std::move(done);
    static SDL_DialogFileFilter kZip[1];
    kZip[0] = {Tr("Mod zip files (*.zip)"), "zip"};
    const auto callback = [](void* user, const char* const* files, int) {
      App* app = static_cast<App*>(user);
      if (files != nullptr && files[0] != nullptr) {
        std::lock_guard<std::mutex> lock(app->pickMutex_);
        app->modToolPicked_ = files[0];
        app->modToolPickedReady_ = true;
      }
    };
    const std::string where = Utf8(sys::KnownFolder(sys::Folder::Documents, false) / PathOf(name));
    ShowSaveDialog(callback, this, window_, kZip, 1, where.c_str());
  };
  modTools_.Init(host);
}

void App::DrawModToolWindows() {
  modupd::Ui ui;
  ui.scale = scale_;
  ui.good = kGood;
  ui.warn = kWarn;
  ui.bad = kBad;
  ui.dim = kDim;
  ui.heading = [this](const char* t) { Heading(t); };
  ui.dimText = [this](const std::string& t) { Dim(t); };
  ui.primary = [](const char* label) { return PrimaryButton(label); };
  ui.back = [this] { return BackPressed(); };
  ui.closeNow = [this] { return closeWindows_; };
  modTools_.Draw(ui);
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
    error = Fmt(Tr("The game couldn't be started (%s, %s). Is it next to the launcher?"), Utf8(game_.program).c_str(), why.c_str());
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
  if (SlotsOn() && game_.saves.wrongEdition && game_.saves.saveFile) {
    const fs::path file = game_.saves.saveFile(sl::RootOf(SavesFolder(), slots_.Active().id));
    const std::string wrong = file.empty() ? std::string() : game_.saves.wrongEdition(file);
    if (!wrong.empty()) {
      playError_ = wrong;
      Log("play refused: " + wrong);
      return;
    }
  }
  SaveSettingsIfChanged();
  SaveMods();
  Log("play: " + registry_.Summary());
  playError_.clear();
  if (!StartGame(playError_)) {
    Log(playError_);
    return;
  }
  const bool stay = registry_.Bool(kStaySetting).Get() || ExtraRunning();
  if (!stay && step_ >= script_.size()) {
    quit_ = true;
  }
}

void App::PresenceFrame() {
  const auto now = std::chrono::steady_clock::now();
  if (presenceAt_ != std::chrono::steady_clock::time_point{} && now - presenceAt_ < std::chrono::seconds(1)) {
    return;
  }
  presenceAt_ = now;
  const GameInfo::Presence& p = game_.presence;
  const bool want = !p.clientId.empty() && p.allowed && registry_.Bool("system.discord_presence").Get();
  if (want && !presence_) {
    engine::presence::Config cfg;
    cfg.clientId = p.clientId;
    cfg.endpointOverride = p.endpoint;
    const char* l = std::getenv("XD_DISCORD_LOG");
    cfg.log = l != nullptr && l[0] == '1';
    if (!p.endpoint.empty()) {
      cfg.minIntervalMs = 1000;
      cfg.retryIntervalMs = 1000;
    }
    cfg.stopWaitMs = 1500;
    presence_ = std::make_unique<engine::presence::Client>();
    presence_->Start(cfg);
    if (presenceStart_ == 0) {
      presenceStart_ = static_cast<int64_t>(std::time(nullptr));
    }
  } else if (!want && presence_) {
    presence_->Stop();
    presence_.reset();
  }
  if (!presence_) {
    return;
  }
  if (gameRunning_ || playWhenDrawn_) {
    presence_->Clear();
    return;
  }
  engine::presence::Activity a;
  a.details = p.details ? p.details() : std::string();
  a.state = p.state;
  a.largeImage = p.largeImage;
  a.largeText = p.largeText;
  a.startSeconds = presenceStart_;
  presence_->Set(a);
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
  return eu::FolderVersion(eu::GameFolderPath(layout_, folder));
}

std::string App::NewestInstalled() {
  const std::string next = layout_.valid && layout_.current != layout_.running &&
                                   eu::FolderUsable(eu::GameFolderPath(layout_, layout_.current), game_.updates.requiredFiles)
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
      message = Fmt(Tr("Couldn't check for updates: %s."), error.c_str());
    } else if (!eu::ParseSignedManifest(text, u.publicKey, u.product, m, error)) {
      message = Fmt(Tr("The update information was refused: %s."), error.c_str());
    } else if (!m.hasPackage) {
      phase = UpdateWork::Phase::UpToDate;
      message = Fmt(Tr("You have the newest version (%s)."), have.c_str());
      Log("updates: the information names version " + m.version + " but has no package for " +
          std::string(eu::CurrentSystem()) + " yet");
    } else if (eu::IsNewer(m.version, have)) {
      phase = UpdateWork::Phase::Available;
      message = Fmt(Tr("Version %s is available (you have %s)."), m.version.c_str(), have.c_str());
    } else {
      phase = UpdateWork::Phase::UpToDate;
      message = Fmt(Tr("You have the newest version (%s)."), have.c_str());
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

void App::StartUpdateInstall(bool freshFolder) {
  freshFolder = freshFolder || (!layout_.valid && !layout_.macApp && !layout_.suggestedRoot.empty());
  eu::Manifest m;
  {
    std::lock_guard<std::mutex> lock(up_.mutex);
    if (up_.busy || up_.phase != UpdateWork::Phase::Available) {
      return;
    }
    m = up_.manifest;
    up_.phase = UpdateWork::Phase::Downloading;
    up_.installing = true;
    up_.message = Fmt(Tr("Downloading version %s..."), m.version.c_str());
  }
  StopUpdate();
  FindVersions();
  const eu::Layout layout = layout_;
  const fs::path freshRoot = freshFolder ? (freshRoot_.empty() ? layout_.suggestedRoot : freshRoot_) : fs::path();
  Log("updates: Update now: downloading " + m.url + " (" + std::to_string(m.size) + " bytes)");
  up_.done = 0;
  up_.total = m.size;
  up_.busy = true;
  up_.thread = std::thread([this, m, layout, freshFolder, freshRoot] {
    const GameInfo::Updates& u = game_.updates;
    const fs::path zip = u.stagingFolder / PathOf("update-" + m.version + (eu::PackageIsTarGz() ? ".tar.gz" : ".zip"));
    eu::FetchOptions options;
    options.maxBytes = m.size;
    options.userAgent = u.product + "/" + u.version;
    options.timeoutMs = 60000;
    options.progress = [this](uint64_t done, uint64_t) {
      up_.done = done;
      return !up_.stop.load();
    };
    std::string error, installed;
    bool ok = layout.valid || (freshFolder && !layout.macApp && !freshRoot.empty());
    if (!ok) {
      error = layout.why;
    }
    ok = ok && eu::FetchToFile(m.url, zip, options, error);
    if (ok) {
      std::lock_guard<std::mutex> lock(up_.mutex);
      up_.message = Fmt(Tr("Checking and unpacking version %s..."), m.version.c_str());
    }
    eu::InstallRequest request;
    request.layout = layout;
    request.manifest = m;
    request.package = zip;
    request.requiredFiles = u.requiredFiles;
    if (freshFolder) {
      ok = ok && eu::SetUpPlayerFolder(request, OwnProgramFolder(), freshRoot, error, [this] { return up_.stop.load(); });
      installed = Utf8(freshRoot);
    } else {
      ok = ok && eu::Install(request, installed, error, [this] { return up_.stop.load(); });
    }
    std::error_code ec;
    fs::remove(zip, ec);
    std::lock_guard<std::mutex> lock(up_.mutex);
    if (ok) {
      up_.phase = UpdateWork::Phase::Installed;
      up_.installed = installed;
      up_.message = freshFolder
                        ? Fmt(Tr("Version %s is set up in the new folder %s. Open the launcher in that folder from now on (the old folder can be deleted when you like: your saves and settings are not in it)."),
                              m.version.c_str(), installed.c_str())
                        : layout.macApp
                        ? Fmt(Tr("Version %s is ready. It is put in place when you restart %s (the app is replaced after "
                                 "this window closes, then opens again)."),
                              m.version.c_str(), game_.title.c_str())
                        : Fmt(Tr("Version %s is installed. It starts the next time you open %s."), m.version.c_str(),
                              game_.title.c_str());
    } else {
      up_.phase = UpdateWork::Phase::Failed;
      up_.message = error == "stopped"
                        ? Fmt(Tr("The update was cancelled. Nothing was changed: your version (%s) stays as it is."),
                              u.version.c_str())
                        : Fmt(Tr("The update couldn't be installed: %s. Nothing was changed: your version (%s) stays as it "
                                 "is."),
                              error.c_str(), u.version.c_str());
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
  const bool setAside = layout_.macApp && layout_.current == "pending";
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
  versionNote_ = setAside ? Fmt(Tr("The waiting update (version %s) was set aside: version %s stays."), leaving.c_str(),
                                back.c_str())
                 : layout_.macApp ? Fmt(Tr("Version %s is put in place when you restart %s (version %s is kept)."),
                                        back.c_str(), game_.title.c_str(), leaving.c_str())
                                  : Fmt(Tr("Version %s starts the next time you open %s (version %s stays installed)."),
                                        back.c_str(), game_.title.c_str(), leaving.c_str());
  Log("updates: " + versionNote_);
  return true;
}

void App::RestartLauncher() {
  if (!layout_.valid || gameRunning_) {
    return;
  }
  if (layout_.macApp) {
    std::string why;
    if (eu::MacApplyPending(layout_, why)) {
      Log("updates: restarting through the update helper");
      quit_ = true;
    } else if (!why.empty()) {
      versionNote_ = Fmt(Tr("The restart didn't work: %s. Close this window and open %s again."), why.c_str(),
                         game_.title.c_str());
    } else {
      versionNote_ = Tr("Nothing is waiting to be put in place.");
    }
    return;
  }
  const fs::path starter =
      layout_.root / PathOf(!layout_.starter.empty() ? layout_.starter
                            : !layout_.starterNames.empty() ? layout_.starterNames.front() : game_.updates.starterName);
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
    versionNote_ = Fmt(Tr("Please close this window and open %s again (the restart didn't work: %s)."),
                       game_.title.c_str(), why.c_str());
  }
}

void App::DrawUpdateCard() {
  Heading(Tr("Version and updates"));
  BeginCard("##updates");
  const GameInfo::Updates& u = game_.updates;
  ImGui::Text(Tr("Version %s"), u.version.c_str());
  if (layout_.valid && layout_.running != eu::kFirstFolder) {
    ImGui::SameLine();
    ImGui::TextColored(kFaint, Tr("(in the folder %s)"), layout_.running.c_str());
  }
  if (!u.Active()) {
    EndCard();
    return;
  }
  bool check = registry_.Bool(kUpdateSetting).Get();
  if (ImGui::Checkbox(Lbl(Tr("Check for updates when the launcher starts"), "Check for updates when the launcher starts").c_str(), &check)) {
    registry_.SetBool(kUpdateSetting, check);
    updatesAsked_ = true;
    SaveUpdaterState();
  }
  Dim(Tr("It only looks. If there is a newer version, the launcher shows what's new and asks you first: nothing is ever "
         "downloaded or installed without your yes. Your saves, settings and mods are never touched by an update."));
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
  if (ImGui::Button(phase == UpdateWork::Phase::Checking ? Lbl(Tr("Checking..."), "Check now").c_str()
                                                         : Lbl(Tr("Check now"), "Check now").c_str())) {
    StartUpdateCheck(true);
  }
  if (busy) {
    ImGui::EndDisabled();
  }
  if (phase == UpdateWork::Phase::Available && !busy) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Fmt(Tr("See version %s..."), available.c_str()).c_str(), "seeversion").c_str())) {
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
      const std::string line = !versionNote_.empty()
                                   ? versionNote_
                                   : Fmt(Tr("Version %s starts the next time you open %s."),
                                         (next.empty() ? layout_.current : next).c_str(), game_.title.c_str());
      ImGui::PushStyleColor(ImGuiCol_Text, kGood);
      ImGui::TextWrapped("%s", line.c_str());
      ImGui::PopStyleColor();
      if (!gameRunning_ && ImGui::Button(Lbl(Tr("Restart now"), "Restart now").c_str())) {
        RestartLauncher();
      }
    } else if (!versionNote_.empty()) {
      Dim(versionNote_);
    }
    if (!layout_.previous.empty() && eu::FolderUsable(eu::GameFolderPath(layout_, layout_.previous), u.requiredFiles)) {
      const std::string v = FolderVersionOf(layout_.previous);
      const std::string label = eu::IsNewer(v, FolderVersionOf(layout_.current))
                                    ? Fmt(Tr("Use version %s again..."), v.c_str())
                                    : Fmt(Tr("Go back to the previous version (%s)..."), v.c_str());
      if (busy) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button(Lbl(label.c_str(), "goback").c_str())) {
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

void App::LoadLauncherState() {
  presetAsked_ = false;
  presetPending_ = false;
  if (std::FILE* f = sys::OpenFile(game_.settingsFolder / L"launcher.ini", "rb")) {
    char line[256];
    while (std::fgets(line, sizeof line, f) != nullptr) {
      std::string text(line);
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
      }
      presetAsked_ = presetAsked_ || text == "preset_asked=yes";
      presetPending_ = presetPending_ || text == "preset_pending=yes";
      dataAsked_ = dataAsked_ || text == "data_asked=yes";
    }
    std::fclose(f);
  }
}

void App::SaveLauncherState() {
  if (game_.playerFilesReadOnly) {
    return;
  }
  std::error_code ec;
  fs::create_directories(game_.settingsFolder, ec);
  const std::string text = std::string("# The launcher's own memory. Delete this file to be asked \"How do you want to play?\" again.\r\n") +
                           (presetAsked_ ? "preset_asked=yes\r\n" : presetPending_ ? "preset_pending=yes\r\n" : "") +
                           (dataAsked_ ? "data_asked=yes\r\n" : "");
  if (!WriteText(game_.settingsFolder / L"launcher.ini", text)) {
    Log("launcher.ini couldn't be saved in " + Utf8(game_.settingsFolder));
  }
}

void App::ChoosePreset(es::Preset preset) {
  registry_.ApplyPreset(preset);
  SaveSettingsIfChanged();
  if (!settingsFileFound_ && !game_.playerFilesReadOnly) {
    if (registry_.Save(game_.settingsFolder)) {
      settingsFileFound_ = true;
    }
  }
  presetAsked_ = true;
  presetPending_ = false;
  SaveLauncherState();
  Log(std::string("preset chosen in the first window: ") + ui::PresetName(preset));
}

void App::DrawWelcomeWindow() {
  if (askPreset_ && !askData_ && !ImGui::IsPopupOpen("###datafirst")) {
    ImGui::OpenPopup("###welcome");
    askPreset_ = false;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float width = std::min(960 * scale_, vp->WorkSize.x - 40 * scale_);
  const float height = std::min(680 * scale_, vp->WorkSize.y - 30 * scale_);
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  if (!ImGui::BeginPopupModal(Lbl(Tr("How do you want to play?"), "welcome").c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }
  if (closeWindows_) {
    ImGui::CloseCurrentPopup();
  }
  const auto bullets = [&](std::initializer_list<std::string> lines) {
    for (const std::string& line : lines) {
      ImGui::Bullet();
      ImGui::TextWrapped("%s", line.c_str());
    }
  };
  if (ImGui::BeginTable("##presets", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV |
                                            ImGuiTableFlags_PadOuterX)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    Heading(Tr("Original"));
    ImGui::TextWrapped("%s", Tr("Exactly the GameCube game: same speed, same rules, same random numbers."));
    ImGui::Spacing();
    ImGui::TextColored(kWarn, "%s", Tr("Not available in Original:"));
    bullets({Tr("The New Game setup screen"),
             Tr("Randomizer"),
             Tr("Challenge runs: Nuzlocke, level caps, no items in battle"),
             Tr("Gen 4 Pokemon, Gen 4 rules, Fairy type, shiny odds"),
             Tr("Speed-ups: fast-forward, faster text, faster loading and saving"),
             Tr("Widescreen, a sharper picture, photo mode, HD texture packs")});
    ImGui::Spacing();
    Dim(Tr("Allowed in Original too: button remapping, volumes, full screen and the speedrun timer."));
    Dim(Tr("Online battles and online trading work in Original too; trading in a Faithful save only receives Pokemon the "
           "GameCube game could have."));
    ImGui::TableNextColumn();
    Heading(Tr("Enhanced (recommended for most players)"));
    ImGui::TextWrapped("%s", Tr("The GameCube game plus the PC port's improvements:"));
    ImGui::Spacing();
    bullets({Tr("A sharper picture that fills your window, widescreen, even frame pacing"),
             Tr("Shorter waits: faster text, loading and saving, and a fast-forward key"),
             Tr("TMs listed with their move, your controller's own button pictures, photo mode, a pause when the window is in "
                "the background")});
    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr("The New Game setup lets you choose randomizer, challenge runs, Gen 4 Pokemon, Gen 4 rules, "
                                "Fairy type, shiny odds and more for each new save."));
    ImGui::Spacing();
    Dim(Tr("The game's own rules stay as they were until you choose otherwise."));
    Dim(Tr("Online battles and online trading are available here too."));
    ImGui::EndTable();
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  Dim(Tr("You can change this any time on the Preset page. Each save remembers how it was started: when it differs "
         "from your choice, the game asks at Continue whether to keep it or switch."));
  Dim(Tr("If you close this window without choosing, Enhanced is used and you are asked again next time."));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const ImVec2 big((ImGui::GetContentRegionAvail().x - gap) * 0.5f, 52 * scale_);
  if (ImGui::Button(Lbl(Tr("Play Original"), "welcome-original").c_str(), big)) {
    ChoosePreset(es::Preset::Original);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
  const bool enhanced = ImGui::Button(Lbl(Tr("Play Enhanced"), "welcome-enhanced").c_str(), big);
  ImGui::PopStyleColor(3);
  ImGui::SetItemDefaultFocus();
  if (enhanced) {
    ChoosePreset(es::Preset::Enhanced);
    ImGui::CloseCurrentPopup();
  }
  if (BackPressed() || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::DrawUpdateWindows() {
  const GameInfo::Updates& u = game_.updates;
  if (!u.Active()) {
    return;
  }
  const float wrap = 560 * scale_;
  if (askUpdates_ && !askPreset_ && !askData_ && !ImGui::IsPopupOpen("###welcome") && !ImGui::IsPopupOpen("###datafirst")) {
    ImGui::OpenPopup("###ask");
    askUpdates_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Updates"), "ask").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    Heading(Tr("Check for updates when the launcher starts?"));
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(Fmt(Tr("If you say yes, the launcher looks for a newer version of %s each time it starts. It "
                                  "only looks: when there is one, it shows what's new and asks you before downloading "
                                  "anything. If you say no, it never goes online by itself."),
                               game_.title.c_str())
                               .c_str());
    Dim(Tr("You can change this any time on the Disc page (\"Version and updates\"), where \"Check now\" also works."));
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
    if (PrimaryButton(Lbl(Tr("Yes, check at start"), "Yes, check at start").c_str())) {
      answer(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("No"), "No").c_str())) {
      answer(false);
    }
    ImGui::EndPopup();
  }

  if (openUpdates_) {
    ImGui::OpenPopup("###offer");
    openUpdates_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Update"), "offer").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
      Heading(Fmt(Tr("Version %s is available"), m.version.c_str()).c_str());
      Dim(m.date.empty() ? Fmt(Tr("You have version %s. Download: %s."), NewestInstalled().c_str(), MegaBytes(m.size).c_str())
                         : Fmt(Tr("You have version %s. Published %s. Download: %s."), NewestInstalled().c_str(),
                               m.date.c_str(), MegaBytes(m.size).c_str()));
      if (!m.notes.empty() || !m.history.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(Tr("What's new:"));
        ImGui::BeginChild("##notes", ImVec2(wrap, std::min(220 * scale_, ImGui::GetTextLineHeightWithSpacing() * 12)),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(eu::NotesSince(m, NewestInstalled()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
      }
      if (m.version == skipVersion_) {
        Dim(Tr("You chose to skip this version earlier."));
      }
      if (layout_.valid || layout_.macApp || layout_.suggestedRoot.empty())
        Dim(layout_.macApp ? Tr("The new app waits beside the one you have and is put in place when you restart; the "
                              "version you have is kept for \"Go back\". Your saves, settings and mods are not touched.")
                         : Tr("Installed next to the version you have, which stays for \"Go back\". Your saves, settings "
                              "and mods are not touched."));
      const bool fresh = !layout_.valid && !layout_.macApp && !layout_.suggestedRoot.empty();
      if (!layout_.valid && !fresh) {
        ImGui::TextColored(kWarn, "%s", layout_.why.c_str());
      }
      if (fresh) {
        const fs::path target = freshRoot_.empty() ? layout_.suggestedRoot : freshRoot_;
        ImGui::TextColored(kWarn, "%s", Tr("This copy isn't in the player's folder layout, so it can't update itself in place."));
        ImGui::TextWrapped("%s", Fmt(Tr("Update now downloads version %s and sets it up in a NEW folder: %s. Nothing is copied from this folder and nothing in it is changed. "
                                        "Your saves, settings and mods stay where they are (they are not kept in the game folder). Afterwards open the launcher in the new folder; this old folder can be deleted."),
                                     m.version.c_str(), Utf8(target).c_str()).c_str());
        if (ImGui::Button(Lbl(Tr("Choose another folder..."), "Choose another folder").c_str())) {
          SDL_ShowOpenFolderDialog(
              [](void* user, const char* const* files, int) {
                App* app = static_cast<App*>(user);
                if (files != nullptr && files[0] != nullptr) {
                  std::lock_guard<std::mutex> lock(app->pickMutex_);
                  app->freshPicked_ = files[0];
                }
              },
              this, window_, nullptr, false);
        }
        {
          std::lock_guard<std::mutex> lock(pickMutex_);
          if (!freshPicked_.empty()) {
            freshRoot_ = eu::SuggestPlayerFolder(PathOf(freshPicked_) / L"x");
            freshPicked_.clear();
          }
        }
      }
      {
        const std::string synced = eu::SyncedFolderName(fresh ? (freshRoot_.empty() ? layout_.suggestedRoot : freshRoot_) : OwnProgramFolder());
        if (!synced.empty()) {
          ImGui::TextColored(kWarn, "%s", Fmt(Tr("Tip: this folder is inside %s. Keep the game outside synced folders (for example in C:\Games): a sync can lock or move files while the game runs."), synced.c_str()).c_str());
        }
      }
      ImGui::Spacing();
      if ((!layout_.valid && !fresh) || gameRunning_) {
        ImGui::BeginDisabled();
      }
      if (PrimaryButton(Lbl(Tr("Update now"), "Update now").c_str())) {
        StartUpdateInstall(fresh);
      }
      if ((!layout_.valid && !fresh) || gameRunning_) {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Not now"), "Not now").c_str()) || BackPressed()) {
        Log("updates: not now");
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Skip this version"), "Skip this version").c_str())) {
        SkipUpdate();
        ImGui::CloseCurrentPopup();
      }
    } else if (phase == UpdateWork::Phase::Downloading) {
      Heading(Fmt(Tr("Updating to version %s"), m.version.c_str()).c_str());
      ImGui::TextUnformatted(message.c_str());
      const uint64_t total = up_.total, done = up_.done;
      ImGui::ProgressBar(total > 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f,
                         ImVec2(wrap, 0), Fmt(Tr("%s of %s"), MegaBytes(done).c_str(), MegaBytes(total).c_str()).c_str());
      if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str())) {
        up_.stop = true;
      }
    } else {
      Heading(phase == UpdateWork::Phase::Installed ? Tr("Update installed") : Tr("The update was not installed"));
      ImGui::PushStyleColor(ImGuiCol_Text, phase == UpdateWork::Phase::Failed ? kWarn : kText);
      ImGui::TextWrapped("%s", message.c_str());
      ImGui::PopStyleColor();
      ImGui::Spacing();
      if (phase == UpdateWork::Phase::Installed && layout_.valid && !gameRunning_) {
        if (PrimaryButton(Lbl(Tr("Restart now"), "Restart now").c_str())) {
          ImGui::CloseCurrentPopup();
          RestartLauncher();
        }
        ImGui::SameLine();
      }
      if (ImGui::Button(phase == UpdateWork::Phase::Installed ? Lbl(Tr("Later"), "Later").c_str() : Lbl(Tr("OK"), "OK").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openGoBack_) {
    ImGui::OpenPopup("###back");
    openGoBack_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Version"), "back").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    const std::string v = FolderVersionOf(layout_.previous);
    Heading(Fmt(Tr("Start version %s next time?"), v.c_str()).c_str());
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(Fmt(Tr("Version %s starts the next time you open %s. Version %s stays installed, so you can "
                                  "switch back the same way. Your saves, settings and mods stay as they are."),
                               v.c_str(), game_.title.c_str(), FolderVersionOf(layout_.current).c_str())
                               .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(Lbl(Fmt(Tr("Use version %s"), v.c_str()).c_str(), "usever").c_str())) {
      std::string error;
      if (!GoBackVersion(error)) {
        versionNote_ = Fmt(Tr("That didn't work: %s."), error.c_str());
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str()) || BackPressed()) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  closeWindows_ = false;
}

PortUserLocation App::DataLocation() const { return PortUser_Location(game_.title.c_str()); }

const char* App::DataKindName(PortUserKind kind) {
  switch (kind) {
  case PortUserKind::Portable: return Tr("next to the game (portable)");
  case PortUserKind::Custom: return Tr("in a folder set by hand (data_location.json)");
  case PortUserKind::ThisRun: return Tr("in a folder given for this run (--data-folder)");
  case PortUserKind::TestAppData: return Tr("in a folder set for testing");
  case PortUserKind::User:
  default: return Tr("in your user folder");
  }
}

bool App::DataLooksFresh() const {
  std::error_code ec;
  const fs::path folder = DataLocation().current;
  return !settingsFileFound_ && !fs::exists(folder / L"USA", ec) && !fs::exists(folder / L"saves", ec) &&
         !fs::exists(folder / L"mods.ini", ec);
}

void App::MaybeAskDataFolder() {
  const char* ask = std::getenv("XD_LAUNCHER_ASK_DATA");
  const bool allowed = ask != nullptr ? std::string(ask) == "1" : script_.empty();
  if (dataAsked_ || !allowed || game_.playerFilesReadOnly || !DataLooksFresh()) {
    return;
  }
  const PortUserLocation where = DataLocation();
  if (!where.canSwitch || !where.portableProblem.empty() ||
      (where.kind != PortUserKind::User && where.kind != PortUserKind::TestAppData)) {
    if (!where.portableProblem.empty() && where.canSwitch) {
      Log("first start: the data folder question is not asked (portable is not possible: " + where.portableProblem + ")");
    }
    return;
  }
  askData_ = true;
  Log("first start: asking where the data is kept");
}

bool App::DataLocked(std::string& why) const {
  if (gameRunning_ || playWhenDrawn_) {
    why = Tr("Close the game first: it has the memory card open.");
    return true;
  }
  if (game_.playerFilesReadOnly) {
    why = Tr("Test run on the player's own files: the data is not changed.");
    return true;
  }
  if (dataWork_.busy) {
    why = Tr("Your data is being copied.");
    return true;
  }
  if (bg_.drawing || bg_.preparing) {
    why = Tr("The launcher is still preparing pictures for your disc; try again in a minute.");
    return true;
  }
  return false;
}

void App::AnswerDataFirst(bool portable) {
  std::string error;
  if (portable) {
    if (!PortUser_SwitchLocation(game_.title.c_str(), true, error)) {
      dataMessage_ = Fmt(Tr("That didn't work: %s."), error.c_str());
      dataOk_ = false;
      Log("data folder: " + dataMessage_);
      return;
    }
    const fs::path folder = DataLocation().portable;
    std::error_code ec;
    fs::create_directories(folder, ec);
    WriteText(folder / L"launcher.ini", "# The launcher's own memory.\r\ndata_asked=yes\r\n");
    Log("data folder: portable, " + Utf8(folder));
    dataAsked_ = true;
    RestartForData();
    return;
  }
  dataAsked_ = true;
  SaveLauncherState();
  Log("data folder: your user folder");
}

void App::OpenDataFolder() {
  const fs::path folder = DataLocation().current;
  std::error_code ec;
  fs::create_directories(folder, ec);
  OpenInSystem(folder);
}

void App::RestartForData() {
  quit_ = true;
  if (!script_.empty()) {
    Log("data folder: a script is running, not starting again");
    return;
  }
  sys::StartRequest request;
  request.program = sys::ProgramFile();
  request.arguments = game_.restartArguments;
  request.folder = sys::ProgramFolder();
  request.shareOutput = true;
  request.toFront = true;
  request.ownEnvironment = true;
  request.environmentWithout = {"XD_CACHE_DIR="};
  std::string why;
  if (sys::Process* started = sys::StartProgram(request, why)) {
    sys::Close(started);
    Log("data folder: the launcher started again");
  } else {
    Log("data folder: couldn't start the launcher again: " + why);
  }
}

void App::AskMoveData(bool toPortable) {
  std::string why;
  const PortUserLocation where = DataLocation();
  if (DataLocked(why) || !where.canSwitch) {
    dataMessage_ = why.empty() ? std::string(Tr("This run keeps its data in a folder it was given (--data-folder).")) : why;
    dataOk_ = false;
    return;
  }
  if (toPortable && !where.portableProblem.empty()) {
    dataMessage_ = Fmt(Tr("Portable isn't possible here: %s."), where.portableProblem.c_str());
    dataOk_ = false;
    return;
  }
  dataWork_.toPortable = toPortable;
  dataWork_.from = where.current;
  dataWork_.to = toPortable ? where.portable : where.userDefault;
  dataWork_.phase = DataWork::Phase::Ask;
  const datafolder::Tally t = datafolder::Measure(dataWork_.from);
  dataWork_.totalFiles = t.files;
  dataWork_.totalBytes = t.bytes;
  dataMessage_.clear();
  openDataMove_ = true;
}

void App::StopDataWork() {
  if (dataWork_.thread.joinable()) {
    dataWork_.stop = true;
    dataWork_.thread.join();
  }
  dataWork_.stop = false;
}

void App::StartMoveData() {
  StopDataWork();
  StopWorker();
  dataWork_.files = 0;
  dataWork_.bytes = 0;
  dataWork_.phase = DataWork::Phase::Copying;
  dataWork_.busy = true;
  Log(std::string("data folder: moving ") + Utf8(dataWork_.from) + " to " + Utf8(dataWork_.to));
  dataWork_.thread = std::thread([this] {
    std::string error;
    bool ok = datafolder::Copy(dataWork_.from, dataWork_.to, dataWork_.stop,
                               [this](uint64_t files, uint64_t bytes) {
                                 dataWork_.files = files;
                                 dataWork_.bytes = bytes;
                               },
                               error);
    if (ok) {
      dataWork_.phase = DataWork::Phase::Checking;
      ok = datafolder::Verify(dataWork_.from, dataWork_.to, error);
      if (!ok) {
        std::error_code ec;
        fs::remove_all(dataWork_.to, ec);
      }
    }
    if (ok && !PortUser_SwitchLocation(game_.title.c_str(), dataWork_.toPortable, error)) {
      ok = false;
      std::error_code ec;
      fs::remove_all(dataWork_.to, ec);
    }
    {
      std::lock_guard<std::mutex> lock(dataWork_.mutex);
      dataWork_.error = error;
      dataWork_.ok = ok;
    }
    dataWork_.phase = DataWork::Phase::Finished;
    dataWork_.busy = false;
  });
}

void App::FollowDataWork() {
  if (dataWork_.phase != DataWork::Phase::Finished || dataWork_.busy) {
    return;
  }
  StopDataWork();
  dataWork_.phase = DataWork::Phase::Done;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(dataWork_.mutex);
    error = dataWork_.error;
  }
  if (!dataWork_.ok) {
    dataMessage_ = error;
    dataOk_ = false;
    Log("data folder: not moved: " + error);
    return;
  }
  std::string binError;
  if (BinFolder(dataWork_.from, binError)) {
    dataMessage_ = Fmt(Tr("Your data is now in %s. The old folder went to %s, so you can still take it back."),
                       Utf8(dataWork_.to).c_str(),
                       (game_.saves.binFolder.empty() ? SysWord(sys::RecycleBinName()) : Utf8(game_.saves.binFolder)).c_str());
  } else {
    dataMessage_ = Fmt(Tr("Your data is now in %s. The old folder is still in %s; delete it yourself once you have checked that the game works."),
                       Utf8(dataWork_.to).c_str(), Utf8(dataWork_.from).c_str());
    Log("data folder: the old folder stays: " + binError);
  }
  dataOk_ = true;
  Log("data folder: moved to " + Utf8(dataWork_.to));
}

void App::ExportAllSaves(const fs::path& folder) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  int copied = 0, missing = 0;
  std::vector<std::pair<std::string, fs::path>> lists;
  lists.emplace_back("", game_.saves.dataFolder);
  if (game_.saves.dataFolderFor) {
    const fs::path other = game_.saves.dataFolderFor("XG");
    if (!other.empty() && other != game_.saves.dataFolder && fs::exists(sl::IndexFile(other), ec)) {
      lists.emplace_back("XG - ", other);
    }
  }
  for (const auto& [prefix, dataFolder] : lists) {
    if (dataFolder.empty()) {
      continue;
    }
    const sl::Index index = sl::Load(dataFolder);
    for (const sl::Slot& slot : index.slots) {
      const fs::path file = game_.saves.saveFile ? game_.saves.saveFile(sl::RootOf(dataFolder, slot.id)) : fs::path();
      if (file.empty() || !fs::exists(file, ec)) {
        missing++;
        continue;
      }
      std::string name = prefix + slot.name;
      for (char& c : name) {
        if (std::strchr("<>:\"/\\|?*", c) != nullptr || static_cast<unsigned char>(c) < 0x20) c = '_';
      }
      fs::path target = folder / PathOf(name + ".gci");
      for (int n = 2; fs::exists(target, ec); n++) {
        target = folder / PathOf(name + " (" + std::to_string(n) + ").gci");
      }
      std::string error;
      if (sl::Export(file, target, error)) {
        copied++;
      } else {
        Log("data folder: export: " + error);
      }
    }
  }
  dataOk_ = copied > 0;
  dataMessage_ = copied > 0 ? Fmt(Tr("Saved %d copies of your saves in %s."), copied, Utf8(folder).c_str())
                            : std::string(Tr("There was no save to copy."));
  (void)missing;
  Log("data folder: " + dataMessage_);
}

int App::CountSavesToLose() const {
  int n = 0;
  std::error_code ec;
  std::vector<fs::path> lists{game_.saves.dataFolder};
  if (game_.saves.dataFolderFor) {
    const fs::path other = game_.saves.dataFolderFor("XG");
    if (!other.empty() && other != game_.saves.dataFolder) lists.push_back(other);
  }
  for (const fs::path& dataFolder : lists) {
    if (dataFolder.empty()) continue;
    const sl::Index index = sl::Load(dataFolder);
    for (const sl::Slot& slot : index.slots) {
      const fs::path file = game_.saves.saveFile ? game_.saves.saveFile(sl::RootOf(dataFolder, slot.id)) : fs::path();
      if (!file.empty() && fs::exists(file, ec)) n++;
    }
  }
  return n;
}

void App::AskRemoveData() {
  std::string why;
  const PortUserLocation where = DataLocation();
  if (DataLocked(why)) {
    dataMessage_ = why;
    dataOk_ = false;
    return;
  }
  if (where.kind == PortUserKind::ThisRun || where.kind == PortUserKind::Custom) {
    dataMessage_ = Tr("This folder was chosen by hand or for this run, so the launcher doesn't empty it: it could hold more than the game's data. Delete it yourself (Open data folder).");
    dataOk_ = false;
    return;
  }
  dataRemoveState_ = 0;
  dataSavesToLose_ = CountSavesToLose();
  dataMessage_.clear();
  openDataRemove_ = true;
}

void App::RemoveAllData() {
  const fs::path folder = DataLocation().current;
  StopWorker();
  StopOpenRead();
  std::vector<fs::path> left;
  std::string error;
  const bool ok = datafolder::RemoveContents(folder, left, error);
  dataRemoveState_ = ok ? 1 : 2;
  dataOk_ = ok;
  dataMessage_ = ok ? Fmt(Tr("All game data was removed from %s."), Utf8(folder).c_str())
                    : Fmt(Tr("Some files could not be removed (in use?): %s"), error.c_str());
  Log("data folder: remove all: " + std::string(ok ? "done" : error));
}

void App::DrawDataWindows() {
  FollowDataWork();
  const float wrap = 600 * scale_;
  if (askData_ && !ImGui::IsPopupOpen("###datafirst")) {
    ImGui::OpenPopup("###datafirst");
    askData_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Where should your data be kept?"), "datafirst").c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    const PortUserLocation where = DataLocation();
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(Tr("Your saves, settings and mods need a home. Choose once; you can change it later on the Save page (\"Move my data\")."));
    ImGui::Spacing();
    const ImVec2 big(wrap, 48 * scale_);
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
    const bool user = ImGui::Button(Lbl(Tr("Keep saves and settings in your user folder (recommended)"), "datafirst-user").c_str(), big);
    ImGui::PopStyleColor(3);
    ImGui::SetItemDefaultFocus();
    Dim(Tr("It survives deleting or updating the game folder."));
    Dim(Fmt(Tr("Folder: %s"), Utf8(where.userDefault).c_str()));
    ImGui::Spacing();
    const bool portable = ImGui::Button(Lbl(Tr("Keep everything next to the game (portable)"), "datafirst-portable").c_str(), big);
    Dim(Tr("For a USB stick or a single folder: everything stays beside the game."));
    Dim(Fmt(Tr("Folder: %s"), Utf8(where.portable).c_str()));
    if (!dataMessage_.empty() && !dataOk_) {
      ImGui::TextColored(kBad, "%s", dataMessage_.c_str());
    }
    ImGui::Spacing();
    Dim(Tr("If you close this window without choosing, your user folder is used and you are asked again next time."));
    ImGui::PopTextWrapPos();
    if (user) {
      AnswerDataFirst(false);
      ImGui::CloseCurrentPopup();
    } else if (portable) {
      AnswerDataFirst(true);
      ImGui::CloseCurrentPopup();
    }
    if (BackPressed() || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (openDataMove_) {
    ImGui::OpenPopup("###datamove");
    openDataMove_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Move my data"), "datamove").c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    if (closeWindows_ && dataWork_.phase != DataWork::Phase::Copying && dataWork_.phase != DataWork::Phase::Checking) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::PushTextWrapPos(wrap);
    const DataWork::Phase phase = dataWork_.phase;
    if (phase == DataWork::Phase::Ask) {
      Heading(dataWork_.toPortable ? Tr("Move your data next to the game?") : Tr("Move your data to your user folder?"));
      ImGui::TextUnformatted(Fmt(Tr("From: %s"), Utf8(dataWork_.from).c_str()).c_str());
      ImGui::TextUnformatted(Fmt(Tr("To: %s"), Utf8(dataWork_.to).c_str()).c_str());
      Dim(Fmt(Tr("%llu files, %s"), static_cast<unsigned long long>(dataWork_.totalFiles), MegaBytes(dataWork_.totalBytes).c_str()));
      ImGui::Spacing();
      ImGui::BulletText("%s", Tr("Everything is copied and the copy is checked before anything is switched. Your saves are never touched until the copy is right."));
      ImGui::BulletText("%s", Fmt(Tr("Then the old folder goes to %s, so you can still take it back."),
                                  (game_.saves.binFolder.empty() ? SysWord(sys::RecycleBinName()) : Utf8(game_.saves.binFolder)).c_str()).c_str());
      ImGui::BulletText("%s", Tr("The launcher then starts again to use the new place."));
      ImGui::Spacing();
      if (ImGui::Button(Lbl(Tr("Move my data"), "datamove-go").c_str())) {
        StartMoveData();
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Cancel"), "datamove-cancel").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
    } else if (phase == DataWork::Phase::Copying || phase == DataWork::Phase::Checking) {
      Heading(phase == DataWork::Phase::Copying ? Tr("Copying your data...") : Tr("Checking the copy..."));
      const float done = dataWork_.totalFiles > 0 ? static_cast<float>(static_cast<double>(dataWork_.files.load()) /
                                                                     static_cast<double>(dataWork_.totalFiles))
                                                  : 1.0f;
      ImGui::ProgressBar(phase == DataWork::Phase::Checking ? 1.0f : std::min(done, 1.0f), ImVec2(wrap, 0),
                         Fmt(Tr("%llu of %llu files"), static_cast<unsigned long long>(dataWork_.files.load()),
                             static_cast<unsigned long long>(dataWork_.totalFiles))
                             .c_str());
      if (ImGui::Button(Lbl(Tr("Cancel"), "datamove-stop").c_str())) {
        dataWork_.stop = true;
      }
    } else if (phase == DataWork::Phase::Finished) {
      ImGui::TextUnformatted(Tr("Finishing..."));
    } else {
      Heading(dataOk_ ? Tr("Your data has moved") : Tr("Your data was not moved"));
      ImGui::PushStyleColor(ImGuiCol_Text, dataOk_ ? kText : kWarn);
      ImGui::TextUnformatted(dataMessage_.c_str());
      ImGui::PopStyleColor();
      if (!dataOk_) {
        Dim(Fmt(Tr("Nothing was lost: your data is still in %s."), Utf8(dataWork_.from).c_str()));
      }
      ImGui::Spacing();
      if (dataOk_) {
        if (PrimaryButton(Lbl(Tr("Restart the launcher"), "datamove-restart").c_str()) || BackPressed()) {
          RestartForData();
          ImGui::CloseCurrentPopup();
        }
      } else if (ImGui::Button(Lbl(Tr("Close"), "datamove-close").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }

  if (openDataRemove_) {
    ImGui::OpenPopup("###dataremove");
    openDataRemove_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Remove all game data?"), "dataremove").c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    if (closeWindows_) {
      ImGui::CloseCurrentPopup();
    }
    const PortUserLocation where = DataLocation();
    ImGui::PushTextWrapPos(wrap);
    if (dataRemoveState_ == 0) {
      Heading(Tr("Remove all game data?"));
      ImGui::TextUnformatted(Fmt(Tr("This deletes everything the game keeps in %s: your saves, settings, mods, logs and bug reports."),
                                 Utf8(where.current).c_str()).c_str());
      ImGui::TextColored(kBad, "%s", Fmt(Tr("Saves that will be deleted: %d"), dataSavesToLose_).c_str());
      ImGui::TextUnformatted(Tr("This cannot be undone. The game and the launcher themselves are not removed."));
      if (!dataMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, dataOk_ ? kGood : kBad);
        ImGui::TextUnformatted(dataMessage_.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::Spacing();
      if (ImGui::Button(Lbl(Tr("Export my saves first..."), "dataremove-export").c_str())) {
        ShowFolderDialog(
            [](void* user, const char* const* files, int) {
              App* app = static_cast<App*>(user);
              if (files != nullptr && files[0] != nullptr) {
                std::lock_guard<std::mutex> lock(app->pickMutex_);
                app->dataExportPicked_ = files[0];
              }
            },
            this, window_, nullptr);
      }
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, kBad);
      if (ImGui::Button(Lbl(Tr("Delete everything"), "dataremove-go").c_str())) {
        RemoveAllData();
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Keep my data"), "dataremove-cancel").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
    } else {
      Heading(dataRemoveState_ == 1 ? Tr("Game data removed") : Tr("Not everything could be removed"));
      ImGui::PushStyleColor(ImGuiCol_Text, dataOk_ ? kText : kWarn);
      ImGui::TextUnformatted(dataMessage_.c_str());
      ImGui::PopStyleColor();
      ImGui::Spacing();
      if (ImGui::Button(Lbl(Tr("Close the launcher"), "dataremove-quit").c_str()) || BackPressed()) {
        quit_ = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
  }
}

void App::DrawDataCard() {
  ImGui::Dummy(ImVec2(0, 8 * scale_));
  Heading(Tr("Where your data is kept"));
  const PortUserLocation where = DataLocation();
  BeginCard("##datafolder");
  ImGui::TextWrapped("%s", Fmt(Tr("Saves, settings, mods, logs and reports are kept %s: %s."), DataKindName(where.kind),
                               Utf8(where.current).c_str())
                               .c_str());
  Dim(Tr("The user folder survives deleting or updating the game folder. Portable keeps everything next to the game, for a USB stick or a single folder."));
  ImGui::Spacing();
  std::string why;
  const bool locked = DataLocked(why);
  if (ImGui::Button(Lbl(Tr("Open data folder"), "Open data folder").c_str())) {
    OpenDataFolder();
  }
  const bool toPortable = where.kind != PortUserKind::Portable;
  ImGui::SameLine();
  if (locked || !where.canSwitch || (toPortable && !where.portableProblem.empty())) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(Lbl(toPortable ? Tr("Move my data next to the game...") : Tr("Move my data to my user folder..."),
                        toPortable ? "Move my data next to the game..." : "Move my data to my user folder...").c_str())) {
    AskMoveData(toPortable);
  }
  if (locked || !where.canSwitch || (toPortable && !where.portableProblem.empty())) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(Lbl(Tr("Remove all game data..."), "Remove all game data...").c_str())) {
    AskRemoveData();
  }
  if (locked) {
    ImGui::EndDisabled();
    Dim(why);
  } else if (!where.canSwitch) {
    Dim(Tr("This run keeps its data in a folder it was given (--data-folder); that can't be changed here."));
  } else if (toPortable && !where.portableProblem.empty()) {
    Dim(Fmt(Tr("Portable isn't possible here: %s."), where.portableProblem.c_str()));
  }
  if (!dataMessage_.empty() && !openDataMove_ && !ImGui::IsPopupOpen("###datamove") && !ImGui::IsPopupOpen("###dataremove")) {
    ImGui::PushStyleColor(ImGuiCol_Text, dataOk_ ? kGood : kBad);
    ImGui::TextWrapped("%s", dataMessage_.c_str());
    ImGui::PopStyleColor();
  }
  EndCard();
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
  } else if (verb == "extradisc" || verb == "extramake" || verb == "extraconfirm" || verb == "extracancel" || verb == "extraask" ||
             verb == "extraremove") {
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
    } else if (verb == "extramake" || verb == "extraconfirm") {
      ExtraMake(index, verb == "extraconfirm");
      return extras_[index]->note.empty();
    } else if (verb == "extraask") {
      extraAsk_ = index;
    } else if (verb == "extraremove") {
      ExtraRemove(index);
    } else {
      ExtraCancel(index);
    }
  } else if (verb == "extratab") {
    page_ = Page::Extra;
    wantExtraTab_ = Lower(arg);
  } else if (verb == "tab") {
    page_ = Page::Settings;
    wantTab_ = Lower(arg);
  } else if (verb == "welcome") {
    const std::string what = Lower(arg);
    if (what.empty()) {
      askPreset_ = true;
    } else if (what == "close") {
      closeWindows_ = true;
    } else if (what == "original" || what == "enhanced") {
      ChoosePreset(what == "original" ? es::Preset::Original : es::Preset::Enhanced);
      closeWindows_ = true;
    } else {
      Log("expected welcome|welcome:original|enhanced|close");
      return false;
    }
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
  } else if (verb == "padmode") {
    SetController(Lower(arg) == "on");
  } else if (verb == "padtype") {
    using D = engine::controls::Device;
    const std::string t = Lower(arg);
    padIn_.SetDevice(t == "ps" || t == "playstation" ? D::PlayStation : t == "switch" ? D::Switch : t == "generic" ? D::Generic : D::Xbox);
  } else if (verb == "pad") {
    using B = pad::Btn;
    SetController(true);
    std::string names = arg;
    int times = 1;
    if (const size_t c = arg.rfind(':'); c != std::string::npos) {
      names = arg.substr(0, c);
      times = std::max(1, std::atoi(arg.c_str() + c + 1));
    }
    static const std::pair<const char*, B> kNames[] = {
        {"up", B::Up}, {"down", B::Down}, {"left", B::Left}, {"right", B::Right}, {"a", B::Confirm}, {"b", B::Cancel},
        {"x", B::X},   {"y", B::Y},       {"lb", B::LB},     {"rb", B::RB},       {"lt", B::LT},     {"rt", B::RT},
        {"start", B::Start}, {"back", B::Back}};
    for (int i = 0; i < times; i++) {
      for (const std::string& name : Split(Lower(names), '+')) {
        bool found = false;
        for (const auto& [n, b] : kNames) {
          if (name == n) {
            padQueue_.push_back(b);
            found = true;
          }
        }
        if (!found) {
          Log("no such controller button: " + name);
          return false;
        }
      }
    }
  } else if (verb == "type") {
    std::string text;
    for (size_t i = 0; i < arg.size(); i++) {
      if (arg[i] == '\\' && i + 1 < arg.size() && arg[i + 1] == 'n') {
        text += '\n';
        i++;
      } else {
        text += arg[i];
      }
    }
    osk_.Type(text);
  } else if (verb == "padwhere") {
    Log("pad focus: " + pad::FocusInfo() + (files_.IsOpen() ? "; file browser in " + files_.Where() : std::string()) +
        (osk_.Open() ? "; keyboard open" : "") + "; play button 0x" + HexId(playId_) + "; controller mode " + (controller_ ? "on" : "off"));
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
    SDL_SetWindowSize(window_, static_cast<int>(w * baseScale_), static_cast<int>(h * baseScale_));
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
             verb == "exportslot" || verb == "importfind" || verb == "importpick" || verb == "importconfirm" ||
             verb == "importcancel" || verb == "tradefile" || verb == "tradepick" || verb == "tradeconfirm" ||
             verb == "monsfile" || verb == "modsfile" || verb == "monspick" || verb == "monsconfirm") {
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
    if (verb == "monsfile" || verb == "modsfile") {
      monsKind_ = verb == "modsfile" ? 1 : 0;
      LookAtSlots(true);
      if (OpenSlot() == nullptr) {
        Log("mons: no save is open (openslot:<n> first)");
        return false;
      }
      return MonsPick(arg);
    }
    if (verb == "monspick") {
      if (!monsHave_) {
        Log("mons: monspick after monsfile");
        return false;
      }
      std::fill(monsPicked_.begin(), monsPicked_.end(), 0);
      if (Lower(arg) == "all") {
        for (size_t i = 0; i < monsView_.rows.size(); i++) {
          monsPicked_[i] = monsView_.rows[i].problem.empty() ? 1 : 0;
        }
        return true;
      }
      size_t at = 0;
      while (at < arg.size()) {
        const size_t comma = arg.find(',', at);
        const int n = std::atoi(arg.substr(at, comma == std::string::npos ? std::string::npos : comma - at).c_str());
        if (n >= 1 && n <= static_cast<int>(monsPicked_.size())) {
          monsPicked_[static_cast<size_t>(n - 1)] = 1;
        }
        if (comma == std::string::npos) {
          break;
        }
        at = comma + 1;
      }
      return true;
    }
    if (verb == "monsconfirm") {
      return MonsConfirm();
    }
    if (verb == "tradefile") {
      LookAtSlots(true);
      if (OpenSlot() == nullptr) {
        Log("trade: no save is open (openslot:<n> first)");
        return false;
      }
      tradeFor_ = OpenSlot()->id;
      return TradePick(arg);
    }
    if (verb == "tradepick") {
      const size_t comma = arg.find(',');
      if (comma == std::string::npos || !tradeHave_) {
        Log("trade: tradepick:<yours>,<theirs> after tradefile");
        return false;
      }
      tradeMine_ = std::atoi(arg.substr(0, comma).c_str()) - 1;
      tradeTheirs_ = std::atoi(arg.substr(comma + 1).c_str()) - 1;
      return true;
    }
    if (verb == "tradeconfirm") {
      return TradeConfirm();
    }
    if (verb == "importfind") {
      ImportFind();
      return true;
    }
    if (verb == "importpick") {
      return ImportPick(arg);
    }
    if (verb == "importconfirm") {
      return ImportConfirm(arg.empty() ? -1 : std::atoi(arg.c_str()) - 1);
    }
    if (verb == "importcancel") {
      importOpen_ = false;
      importSel_ = -1;
      importHaveFile_ = false;
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
  } else if (verb == "savelooks") {
    page_ = Page::Save;
    if (Lower(arg) == "close") {
      closeLooks_ = true;
    } else {
      askLooks_ = true;
      pickLooks_ = Lower(arg);
    }
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
  } else if (verb == "modupdate" || verb == "modneeds" || verb == "modlist" || verb == "modsort") {
    page_ = Page::Mods;
    bool ok = true;
    modUpd_.DoStep(verb, arg, ok);
    if (!ok) Log("expected modupdate:check|yes|always|no|get|install|keep|skip|close|rollback=<id>|rollbackyes, modneeds:<id|yes|download|no>, modlist:export=<f>|import=<f>|apply|download|close, modsort");
    return ok;
  } else if (verb == "modcheck" || verb == "modpack" || verb == "modon" || verb == "modtools") {
    page_ = Page::Mods;
    bool ok = true;
    modTools_.DoStep(verb, arg, ok);
    if (!ok) Log("expected modcheck:<id>|close, modpack:<id>=<zip file>, modon:<id>, modtools:on|close");
    return ok;
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
  } else if (verb == "data") {
    page_ = Page::Save;
    const size_t eq = arg.find('=');
    const std::string what = Lower(eq == std::string::npos ? arg : arg.substr(0, eq));
    const std::string value = eq == std::string::npos ? std::string() : arg.substr(eq + 1);
    if (what == "first") {
      if (value == "user" || value == "portable") {
        AnswerDataFirst(value == "portable");
      } else {
        askData_ = true;
      }
    } else if (what == "move") {
      AskMoveData(DataLocation().kind != PortUserKind::Portable);
      if (!openDataMove_) {
        Log("data: " + dataMessage_);
        return false;
      }
      if (Lower(value) == "yes") {
        StartMoveData();
      }
    } else if (what == "remove") {
      AskRemoveData();
      if (!openDataRemove_) {
        Log("data: " + dataMessage_);
        return false;
      }
      if (Lower(value) == "yes") {
        RemoveAllData();
      }
    } else if (what == "export") {
      ExportAllSaves(PathOf(value));
      return dataOk_;
    } else if (what == "open") {
      OpenDataFolder();
    } else if (what == "restart") {
      RestartForData();
    } else if (what == "close") {
      closeWindows_ = true;
    } else {
      Log("expected data:first|move|remove|export|open|restart|close");
      return false;
    }
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
  if (!padQueue_.empty() || padIn_.AnyDown()) {
    framesSinceStep_ = 0;
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
    if (waitFor_ == "import" && importScan_.busy) {
      return;
    }
    if (waitFor_ == "pictures" && (bg_.drawing || iconShown_ != bg_.iconVersion)) {
      return;
    }
    if (waitFor_ == "shaders" && (bg_.busy || bg_.preparing)) {
      return;
    }
    if (waitFor_ == "data" && (dataWork_.busy || dataWork_.phase == DataWork::Phase::Finished)) {
      return;
    }
    if (waitFor_ == "extra" && ExtraBusy()) {
      return;
    }
    if (waitFor_ == "extraprogress" &&
        std::none_of(extras_.begin(), extras_.end(), [](const auto& e) { return e->job.Running() && e->job.Get().done >= 3; }) &&
        std::any_of(extras_.begin(), extras_.end(), [](const auto& e) { return e->job.Running(); })) {
      return;
    }
    if (waitFor_.rfind("extrastage=", 0) == 0 &&
        std::any_of(extras_.begin(), extras_.end(), [&](const auto& e) { return e->job.Running() && e->job.Get().stage != waitFor_.substr(11); })) {
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
    if (waitFor_ == "modupdate" && modUpd_.WaitPending()) {
      return;
    }
    if (waitFor_ == "modtools" && modTools_.WaitPending()) {
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

std::vector<pad::PadInfo> App::PadInfos() const {
  std::vector<pad::PadInfo> out;
  for (SDL_Gamepad* p : pads_) {
    out.push_back({p, DeviceOf(p)});
  }
  return out;
}

void App::SetController(bool on) {
  if (on == controller_) {
    return;
  }
  controller_ = on;
  rescale_ = true;
  focusPage_ = on;
  Log(std::string("controller mode ") + (on ? "on" : "off"));
}

void App::ApplyScale() {
  rescale_ = false;
  scale_ = baseScale_ * (controller_ ? kControllerScale : 1.0f);
  ImGuiIO& io = ImGui::GetIO();
  ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
  io.Fonts->Clear();
  fonts_ = LoadFonts(scale_);
  options_.headingFont = fonts_.heading;
  ApplyStyle(scale_);
}

void App::BrowseFor(pad::FileBrowser::Mode mode, SDL_DialogFileCallback callback, void* user,
                    const SDL_DialogFileFilter* filters, int count, const char* start) {
  pad::FileBrowser::Request req;
  req.mode = mode;
  req.title = mode == pad::FileBrowser::Mode::SaveFile ? "Save as" : "Choose a file";
  for (int i = 0; i < count; i++) {
    pad::FileFilter f;
    f.name = filters[i].name != nullptr ? filters[i].name : "";
    for (const std::string& ext : Split(filters[i].pattern != nullptr ? filters[i].pattern : "*", ';')) {
      if (ext == "*") {
        f.exts.clear();
        break;
      }
      if (!ext.empty()) {
        f.exts.push_back(Lower(ext));
      }
    }
    req.filters.push_back(f);
  }
  if (start != nullptr && *start != 0) {
    req.start = PathOf(start);
  }
  Log(std::string("file browser: ") + req.title + (start != nullptr ? std::string(" from ") + start : std::string()));
  files_.Open(req, [callback, user](const std::string& result) {
    if (result.empty()) {
      const char* none[1] = {nullptr};
      callback(user, none, -1);
    } else {
      const char* one[2] = {result.c_str(), nullptr};
      callback(user, one, 0);
    }
  });
}

void App::ShowFolderDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window, const char* start) {
  if (!controller_) {
    SDL_ShowOpenFolderDialog(callback, user, window, start, false);
    return;
  }
  BrowseFor(pad::FileBrowser::Mode::OpenFolder, callback, user, nullptr, 0, start);
}

void App::ShowOpenDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window,
                         const SDL_DialogFileFilter* filters, int count, const char* start, bool many) {
  if (!controller_) {
    SDL_ShowOpenFileDialog(callback, user, window, filters, count, start, many);
    return;
  }
  BrowseFor(pad::FileBrowser::Mode::OpenFile, callback, user, filters, count, start);
}

void App::ShowSaveDialog(SDL_DialogFileCallback callback, void* user, SDL_Window* window,
                         const SDL_DialogFileFilter* filters, int count, const char* start) {
  if (!controller_) {
    SDL_ShowSaveFileDialog(callback, user, window, filters, count, start);
    return;
  }
  BrowseFor(pad::FileBrowser::Mode::SaveFile, callback, user, filters, count, start);
}

void App::PadShortcuts() {
  using pad::Btn;
  if (!controller_ || osk_.Open() || files_.IsOpen() || controls_.Listening() || ImGui::IsAnyItemActive() ||
      ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    return;
  }
  int delta = padIn_.Pressed(Btn::RB) ? 1 : padIn_.Pressed(Btn::LB) ? -1 : 0;
  if (delta != 0) {
    std::vector<Page> visible;
    for (int p = 0; p < static_cast<int>(Page::Count); p++) {
      if ((static_cast<Page>(p) == Page::Reports && game_.reportsFolder.empty()) ||
          (static_cast<Page>(p) == Page::Extra && game_.extra.sources.empty())) {
        continue;
      }
      visible.push_back(static_cast<Page>(p));
    }
    const int n = static_cast<int>(visible.size());
    int at = 0;
    for (int i = 0; i < n; i++) {
      if (visible[static_cast<size_t>(i)] == page_) {
        at = i;
      }
    }
    page_ = visible[static_cast<size_t>((at + delta + n) % n)];
    controls_.Cancel();
    focusPage_ = true;
    Log(std::string("pad: page ") + kPageIds[static_cast<int>(page_)]);
    return;
  }
  delta = padIn_.Pressed(Btn::RT) ? 1 : padIn_.Pressed(Btn::LT) ? -1 : 0;
  if (delta != 0) {
    if (page_ == Page::Settings && !tabIds_.empty()) {
      size_t at = 0;
      for (size_t i = 0; i < tabIds_.size(); i++) {
        if (tabIds_[i] == curTab_) {
          at = i;
        }
      }
      const int n = static_cast<int>(tabIds_.size());
      wantTab_ = tabIds_[static_cast<size_t>((static_cast<int>(at) + delta + n) % n)];
      Log("pad: tab " + wantTab_);
    } else if (page_ == Page::Extra && !extraTabIds_.empty()) {
      size_t at = 0;
      for (size_t i = 0; i < extraTabIds_.size(); i++) {
        if (extraTabIds_[i] == curExtraTab_) at = i;
      }
      const int n = static_cast<int>(extraTabIds_.size());
      wantExtraTab_ = extraTabIds_[static_cast<size_t>((static_cast<int>(at) + delta + n) % n)];
      Log("pad: extra tab " + wantExtraTab_);
    } else if (page_ == Page::Controls) {
      controls_.SetPort((controls_.Port() + delta + 4) % 4);
      Log("pad: controller tab " + std::to_string(controls_.Port() + 1));
    }
  }
  if (padIn_.Pressed(Btn::Start)) {
    askMenu_ = true;
  }
}

std::vector<pad::Prompt> App::CurrentPrompts() {
  using pad::Btn;
  if (files_.IsOpen()) {
    return files_.Prompts();
  }
  std::vector<pad::Prompt> p;
  if (controls_.Listening()) {
    return p;
  }
  const bool popup = ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  if (ImGui::IsAnyItemActive()) {
    p.push_back({Btn::Left, "Change"});
    p.push_back({Btn::Confirm, "Done"});
    return p;
  }
  p.push_back({Btn::Up, "Move"});
  p.push_back({Btn::Confirm, "Select"});
  if (popup) {
    p.push_back({Btn::Cancel, "Back"});
    return p;
  }
  p.push_back({Btn::Cancel, "To the pages"});
  p.push_back({Btn::LB, ""});
  p.push_back({Btn::RB, "Page"});
  if (page_ == Page::Settings || page_ == Page::Controls) {
    p.push_back({Btn::LT, ""});
    p.push_back({Btn::RT, "Tab"});
  }
  p.push_back({Btn::Start, "Menu"});
  return p;
}

void App::DrawFileBrowser() {
  files_.Draw(scale_, fonts_.heading, fonts_.caption, padIn_, controller_);
}

void App::DrawPadUi() {
  if (!controller_) {
    return;
  }
  if (askMenu_) {
    ImGui::OpenPopup("Menu##launcher");
    askMenu_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Menu##launcher", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
    Heading("Launcher");
    ImGui::Spacing();
    const ImVec2 big(320 * scale_, 0);
    if (ImGui::Button("Back to the launcher", big) || BackPressed()) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    const bool canPlay = playWhy_.empty() && !gameRunning_ && !playWhenDrawn_;
    if (!canPlay) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Play", big)) {
      ImGui::CloseCurrentPopup();
      Play(false);
    }
    if (!canPlay) {
      ImGui::EndDisabled();
    }
    if (ImGui::Button("Quit the launcher", big)) {
      ImGui::CloseCurrentPopup();
      quit_ = true;
    }
    ImGui::EndPopup();
  }
  if (!sessionOpen_) {
    DrawFileBrowser();
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (osk_.Open()) {
    osk_.Draw(scale_, fonts_.nav, fonts_.caption, padIn_.Device());
    return;
  }
  const float h = promptH_;
  const ImVec2 a(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h), b(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  dl->AddRectFilled(a, b, U32(Hex(0x0C0714), 0.96f));
  dl->AddLine(a, ImVec2(b.x, a.y), U32(kLine));
  ImFont* font = fonts_.caption;
  if (controls_.Listening()) {
    const char* text = "Press the new button now (a key needs a keyboard). It gives up after a few seconds.";
    const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text);
    dl->AddText(font, font->FontSize, ImVec2(a.x + 30 * scale_, a.y + (h - ts.y) * 0.5f), U32(kBusy), text);
    return;
  }
  const std::vector<pad::Prompt> prompts = CurrentPrompts();
  const float size = font->FontSize * 1.45f;
  pad::DrawPrompts(dl, ImVec2(a.x + 30 * scale_, a.y + (h - size) * 0.5f), vp->WorkSize.x, prompts, padIn_.Device(), font,
                   scale_, U32(kText), U32(kDim));
  const char* tag = "Controller mode";
  const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, tag);
  dl->AddText(font, font->FontSize, ImVec2(b.x - 30 * scale_ - ts.x, a.y + (h - ts.y) * 0.5f), U32(kFaint), tag);
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

  const std::string pill = Fmt(Tr("%s preset"), Tr(ui::PresetName(registry_.CurrentPreset())));
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
  float navWidth = 186 * scale_;
  ImGui::PushFont(fonts_.nav);
  for (int p = 0; p < static_cast<int>(Page::Count); p++) {
    navWidth = std::max(navWidth, ImGui::CalcTextSize(PageName(p)).x + (14 + 18 + 13 + 14 + 20) * scale_ +
                                      ImGui::GetStyle().ScrollbarSize);
  }
  ImGui::PopFont();
  ImGui::BeginChild("##nav", ImVec2(navWidth, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rowH = (controller_ ? 38 : 44) * scale_, glyph = 18 * scale_;
  bool inNav = false;
  for (int p = 0; p < static_cast<int>(Page::Count); p++) {
    if (static_cast<Page>(p) == Page::Reports && game_.reportsFolder.empty()) {
      continue;
    }
    if (static_cast<Page>(p) == Page::Extra && game_.extra.sources.empty()) {
      continue;
    }
    ImGui::PushID(p);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !focusInNav_ && !focusPage_);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool selected = page_ == static_cast<Page>(p);
    if (ImGui::InvisibleButton("##page", ImVec2(w, rowH), ImGuiButtonFlags_EnableNav)) {
      page_ = static_cast<Page>(p);
      controls_.Cancel();
      enterPage_ = controller_;
    }
    if (focusPage_ && selected && !ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
      pad::FocusLastItem();
      ImGui::SetScrollHereY(0.5f);
      focusPage_ = false;
    }
    const bool hovered = Hot();
    inNav |= ImGui::IsItemFocused();
    if (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()) {
      dl->AddRect(pos, ImVec2(pos.x + w, pos.y + rowH), U32(kAccentHover), 8 * scale_, 0, 2 * scale_);
    }
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
                PageName(p));
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::PopItemFlag();
    ImGui::PopID();
  }
  if (controller_) {
    ImGui::PushID("quit");
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, !focusInNav_ && !focusPage_);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("##quit", ImVec2(w, rowH), ImGuiButtonFlags_EnableNav)) {
      askMenu_ = true;
    }
    inNav |= ImGui::IsItemFocused();
    const bool hot = Hot();
    if (hot) {
      dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + rowH), U32(Hex(0x241B39)), 8 * scale_);
    }
    if (ImGui::GetIO().NavVisible && ImGui::IsItemFocused()) {
      dl->AddRect(pos, ImVec2(pos.x + w, pos.y + rowH), U32(kAccentHover), 8 * scale_, 0, 2 * scale_);
    }
    dl->AddText(fonts_.nav, fonts_.nav->FontSize,
                ImVec2(pos.x + 14 * scale_ + glyph + 13 * scale_, pos.y + (rowH - fonts_.nav->FontSize) * 0.5f),
                U32(hot ? kText : kDim), "Quit...");
    ImGui::PopItemFlag();
    ImGui::PopID();
  }
  focusInNav_ = inNav;
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
    if (!disc.empty() && (game_.extra.identifyDisc || sources[i].identify)) {
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
        found ? extra::QueryVersion(converter, game_.settingsFolder / "extra_assets" / PathOf(src.id + "_version.txt"), 5000, src.versionArgument)
              : std::string();
    const extra::Status status = extra::ComputeStatus(ExtraFolder(src), src.manifestId, found, version, src.modelsFolder);
    const std::vector<std::string> contents =
        src.contentsFile.empty() ? std::vector<std::string>() : extra::ListNames(ExtraFolder(src) / PathOf(src.contentsFile));
    extra::SecondStatus second;
    if (!src.secondFolder.empty()) {
      second = extra::ComputeSecondStatus(ExtraFolder2(src), src.secondManifestFile, src.secondManifestId, src.secondVersion,
                                          src.secondProvides, status.kind == extra::Status::Kind::OutOfDate);
    }
    extra::SecondStatus third;
    bool workLeft = false;
    if (!src.thirdFolder.empty()) {
      third = extra::ComputeSecondStatus(ExtraFolder3(src), src.thirdManifestFile, src.thirdManifestId, src.thirdVersion,
                                         src.thirdProvides, status.kind == extra::Status::Kind::OutOfDate);
      workLeft = third.kind != extra::SecondStatus::Kind::Ready && extra::HasUnfinishedWork(ExtraFolder3(src));
      if (workLeft) third.detail = Tr("It was started and stopped. Press Make: it continues where it stopped.");
      if (src.thirdCheck && third.kind == extra::SecondStatus::Kind::Ready) {
        const auto verdict = src.thirdCheck(game_.settingsFolder / "extra_assets");
        if (verdict.first != 0) {
          third.kind = extra::SecondStatus::Kind::OutOfDate;
          third.text = Fmt(Tr("%s: out of date, make again"), Tr(src.thirdProvides).c_str());
          third.detail = Tr(verdict.second);
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.status = status;
      state.second = second;
      state.third = third;
      state.workLeft = workLeft;
      state.contents = contents;
      state.converterFound = found;
      state.looked = true;
    }
    Log("extra: " + src.id + ": " + status.text + (src.secondFolder.empty() ? std::string() : "; " + second.text) +
        (src.thirdFolder.empty() ? std::string() : "; " + third.text + (workLeft ? " (unfinished work kept)" : "")) +
        (found ? "" : " (no converter at " + Utf8(converter) + ")") +
        (version.empty() ? std::string() : " (converter " + version + ")"));
    state.looking = false;
  });
}

void App::ExtraSelectDisc(int index, const std::string& path) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  const GameInfo::ExtraSource& src = game_.extra.sources[index];
  Log("extra disc (" + src.id + "): " + path);
  if (!registry_.SetText(src.discSetting, path)) {
    playError_ = Fmt(Tr("This file name can't be used: %s"), path.c_str());
    return;
  }
  SaveSettingsIfChanged();
  extras_[index]->note.clear();
  extras_[index]->check.Start(src, path, game_.extra);
}

void App::ExtraMake(int index, bool confirmed) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  ExtraState& st = *extras_[index];
  const GameInfo::ExtraSource& src = game_.extra.sources[index];
  if (st.job.Running()) return;
  std::string gameDisc = DiscPath();
  const std::string disc = registry_.Text(src.discSetting);
  std::string why;
  if (src.needsPlainGameDisc && !gameDisc.empty()) {
    std::string edition;
    bool known = false;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      known = bg_.haveVerdict && bg_.discPath == gameDisc;
      if (known) edition = bg_.verdict.edition;
    }
    if (!known) {
      why = Tr("still looking at your game disc");
    } else {
      const extra::GameDiscChoice choice = extra::ChooseGameDisc(
          src, gameDisc, edition, RememberedPlainDisc(), [](const std::string& p) {
            std::error_code ec;
            return fs::is_regular_file(PathOf(p), ec);
          });
      if (!choice.message.empty()) why = choice.message;
      else gameDisc = choice.disc;
    }
  }
  fs::path converter;
  {
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.looked) why = Tr("still looking at what is installed");
    else if (!st.converterFound) why = Tr("the converter isn't next to the game");
    converter = extra::ConverterPath(src, game_.program.parent_path());
  }
  if (why.empty() && src.needsGameDisc && gameDisc.empty()) why = Tr("choose your own game disc on the Disc page first");
  if (why.empty() && disc.empty()) why = src.dump ? Fmt(Tr("choose the %s dump first"), src.name.c_str()) : Fmt(Tr("choose the %s disc first"), src.name.c_str());
  if (why.empty() && st.check.state() != extra::DiscCheck::State::Good) why = src.dump ? Tr("that file isn't the right game dump (or is still being looked at)") : Tr("that disc isn't the right game (or is still being looked at)");
  if (why.empty() && game_.playerFilesReadOnly) why = Tr("this is a test run on the player's own files, which it never changes");
  if (!why.empty()) {
    st.note = Fmt(Tr("Can't start: %s."), why.c_str());
    Log("extra: " + st.note);
    return;
  }
  if (confirmed) extraWarnClose_ = true;
  if (!src.thirdWarning.empty() && !confirmed) {
    bool needed;
    {
      std::lock_guard<std::mutex> lock(st.mutex);
      needed = st.third.kind != extra::SecondStatus::Kind::Ready;
    }
    if (needed) {
      extraWarn_ = index;
      st.note.clear();
      return;
    }
  }
  extra::MakeRequest request;
  request.converter = converter;
  request.outFolder = ExtraFolder(src);
  request.outFolder2 = ExtraFolder2(src);
  request.outFolder3 = ExtraFolder3(src);
  request.logFile = game_.settingsFolder / "extra_assets" / PathOf(src.id + ".log");
  request.arguments = extra::ConverterArguments(src, disc, gameDisc, request.outFolder, request.outFolder2, request.outFolder3,
                                                Utf8(game_.program), game_.settingsFolder);
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

void App::ExtraRemove(int index) {
  if (index < 0 || index >= static_cast<int>(extras_.size())) return;
  const GameInfo::ExtraSource& src = game_.extra.sources[index];
  ExtraState& st = *extras_[index];
  if (st.job.Running() || game_.playerFilesReadOnly) return;
  std::error_code ec;
  for (const fs::path& folder : {ExtraFolder(src), ExtraFolder2(src), ExtraFolder3(src)}) {
    if (folder.empty()) continue;
    fs::remove_all(folder, ec);
    fs::remove_all(fs::path(folder.native() + fs::path(".tmp").native()), ec);
    fs::remove_all(fs::path(folder.native() + fs::path(".old").native()), ec);
    fs::remove_all(fs::path(folder.native() + fs::path(".work").native()), ec);
  }
  Log("extra: " + src.id + ": removed " + Utf8(ExtraFolder(src)) + (ec ? " (" + ec.message() + ")" : ""));
  st.finishedShown = false;
  st.note = Tr("Removed. Make it again here whenever you like.");
  ReloadMods();
  ExtraLook(index);
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

bool App::ExtraRunning() const {
  for (const auto& st : extras_) {
    if (st->job.Running()) return true;
  }
  return false;
}

std::string App::StageText(const std::string& id) const {
  if (id == "base") return Tr("Unpacking the dump, converting Gen 1-4 models");
  if (id == "tables") return Tr("Reading the game's tables");
  if (id == "models") return Tr("Converting models");
  if (id == "data") return Tr("Writing Pokemon, moves and abilities");
  if (id == "forms") return Tr("Converting forms and Mega Evolutions");
  if (id == "faces") return Tr("Rendering faces");
  if (id == "entries") return Tr("Writing Pokedex entries");
  if (id == "pictures") return Tr("Drawing Pokedex pictures");
  if (id == "check") return Tr("Checking the pack");
  return id;
}

bool App::ExtraBusy() const {
  for (const auto& st : extras_) {
    if (st->job.Running() || st->wasRunning || st->looking || st->lookAgain) return true;
  }
  return false;
}

void App::DrawExtraRow(int i) {
  const auto& sources = game_.extra.sources;
  {
    ExtraState& st = *extras_[i];
    const GameInfo::ExtraSource& src = sources[i];
    ImGui::PushID(i);
    BeginCard("##extra");
    extra::Status status;
    extra::SecondStatus second, third;
    std::vector<std::string> contents;
    bool looked, converterFound, workLeft;
    {
      std::lock_guard<std::mutex> lock(st.mutex);
      status = st.status;
      second = st.second;
      third = st.third;
      workLeft = st.workLeft;
      contents = st.contents;
      looked = st.looked;
      converterFound = st.converterFound;
    }
    Heading(Tr(src.provides).c_str());
    if (!looked) {
      ImGui::TextColored(kBusy, "%s", Tr("Looking..."));
    } else {
      const ImVec4 colour = status.kind == extra::Status::Kind::Ready       ? kGood
                            : status.kind == extra::Status::Kind::OutOfDate ? kWarn
                            : status.kind == extra::Status::Kind::ConverterMissing ? kBad
                                                                                   : kDim;
      ImGui::TextColored(colour, "%s", status.text.c_str());
      if (!status.detail.empty()) Dim(status.detail);
      if (!contents.empty() && status.kind != extra::Status::Kind::NotSetUp && status.kind != extra::Status::Kind::ConverterMissing) {
        std::string made;
        for (const std::string& n : contents) made += (made.empty() ? "" : ", ") + n;
        Dim("Made: " + made);
      }
      if (!src.secondFolder.empty()) {
        ImGui::TextColored(second.kind == extra::SecondStatus::Kind::Ready       ? kGood
                           : second.kind == extra::SecondStatus::Kind::OutOfDate ? kWarn
                                                                                 : kDim,
                           "%s", second.text.c_str());
        if (!second.detail.empty()) Dim(second.detail);
      }
      if (!src.thirdFolder.empty()) {
        ImGui::TextColored(third.kind == extra::SecondStatus::Kind::Ready       ? kGood
                           : third.kind == extra::SecondStatus::Kind::OutOfDate ? kWarn
                                                                                 : kDim,
                           "%s", third.text.c_str());
        if (!third.detail.empty()) Dim(third.detail);
      }
    }
    Dim(src.dump ? Fmt(Tr("From a %s dump. %s"), src.name.c_str(), Tr(src.how).c_str())
                 : Fmt(Tr("From a %s disc. %s"), src.name.c_str(), Tr(src.how).c_str()));
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    std::string disc = registry_.Text(src.discSetting);
    if (src.dump) ImGui::TextColored(kDim, Tr("%s dump file (.3ds / .cci / .cia)"), src.name.c_str());
    else ImGui::TextColored(kDim, Tr("%s disc image"), src.name.c_str());
    const std::string chooseDisc = src.dump ? Lbl(Tr("Choose dump..."), "Choose dump...") : Lbl(Tr("Choose disc..."), "Choose disc...");
    ImGui::SetNextItemWidth(-std::max(250 * scale_, ImGui::CalcTextSize(src.dump ? Tr("Choose dump...") : Tr("Choose disc...")).x +
                                                        ImGui::GetStyle().FramePadding.x * 2 +
                                                        ImGui::GetStyle().ItemSpacing.x + 12 * scale_));
    ImGui::InputText("##disc", &disc, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button(chooseDisc.c_str())) {
      static SDL_DialogFileFilter kFilters[2];
      kFilters[0] = {Tr("Disc images (ISO, RVZ, WBFS, CISO, GCZ...)"), "iso;gcm;rvz;wia;gcz;ciso;wbfs"};
      if (src.dump) kFilters[0] = {Tr("3DS game dumps (3DS, CCI, CIA)"), "3ds;cci;cia"};
      kFilters[1] = {Tr("All files"), "*"};
      const std::string start = disc.empty() ? std::string() : Utf8(PathOf(disc).parent_path());
      extraPickedFor_ = i;
      ShowOpenDialog(
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
    case extra::DiscCheck::State::Checking: ImGui::TextColored(kBusy, "%s", src.dump ? Tr("Looking at the dump file...") : Tr("Looking at the disc image...")); break;
    case extra::DiscCheck::State::Good:
    case extra::DiscCheck::State::WrongGame:
    case extra::DiscCheck::State::Unreadable:
      ImGui::PushStyleColor(ImGuiCol_Text, st.check.state() == extra::DiscCheck::State::Good ? kGood : kBad);
      ImGui::TextWrapped("%s", st.check.message().c_str());
      ImGui::PopStyleColor();
      break;
    case extra::DiscCheck::State::None:
      if (disc.empty()) Dim(src.dump ? Tr("No dump chosen yet.") : Tr("No disc chosen yet."));
      break;
    }
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    const bool running = st.job.Running();
    if (!running) {
      const bool resume = workLeft && !src.thirdFolder.empty();
      if (ImGui::Button(resume ? Lbl(Tr("Continue"), "Continue").c_str() : Lbl(Tr(src.makeLabel).c_str(), src.makeLabel.c_str()).c_str())) ExtraMake(i);
      if (Hot()) ImGui::SetTooltip("%s", src.dump ? Tr("Needs your own game disc (Disc page) and this dump file.") : Tr("Needs your own game disc (Disc page) and this disc."));
    } else {
      if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str())) ExtraCancel(i);
    }
    if (running) {
      const extra::MakeJob::Snapshot snap = st.job.Get();
      const float fraction = snap.total > 0 ? static_cast<float>(static_cast<double>(snap.done) / static_cast<double>(snap.total)) : 0.0f;
      char label[200];
      float shown = fraction;
      if (!snap.stage.empty()) {
        const double part = snap.total > 0 ? static_cast<double>(snap.done) / static_cast<double>(snap.total) : 0.0;
        shown = static_cast<float>((static_cast<double>(snap.stageFrom) + (static_cast<double>(snap.stageTo) - static_cast<double>(snap.stageFrom)) * part) / 100.0);
        const std::string words = StageText(snap.stage);
        if (snap.total > 1) std::snprintf(label, sizeof label, "%s %llu/%llu  (%d%%)", words.c_str(), static_cast<unsigned long long>(snap.done), static_cast<unsigned long long>(snap.total), static_cast<int>(shown * 100.0f));
        else std::snprintf(label, sizeof label, "%s  (%d%%)", words.c_str(), static_cast<int>(shown * 100.0f));
      } else if (snap.total > 0) {
        std::snprintf(label, sizeof label, "%llu / %llu  %s", static_cast<unsigned long long>(snap.done),
                      static_cast<unsigned long long>(snap.total), snap.current.c_str());
      } else {
        std::snprintf(label, sizeof label, "%s", Tr("starting..."));
      }
      ImGui::ProgressBar(shown, ImVec2(-1, 0), label);
      if (!src.thirdFolder.empty()) {
        Dim(Tr("This takes a while. You can keep using the launcher or play meanwhile (the launcher stays open until it is done). Cancel pauses it: what is made so far is kept, and Make continues later."));
      } else {
        Dim(Tr("This can take a while. The window stays usable; Cancel stops it and leaves nothing half-made."));
      }
    } else if (st.finishedShown) {
      const extra::MakeJob::Snapshot& r = st.last;
      if (r.phase == extra::MakeJob::Phase::Done && !src.doneText.empty()) {
        ImGui::TextColored(kGood, Tr("Done (%s). %s"), HumanBytes(r.totalBytes).c_str(), Tr(src.doneText).c_str());
      } else if (r.phase == extra::MakeJob::Phase::Done && src.switchSetting.empty()) {
        ImGui::TextColored(kGood, Tr("Done: %llu made (%s)."), static_cast<unsigned long long>(r.converted),
                           HumanBytes(r.totalBytes).c_str());
        if (r.keptOriginal > 0) Dim(Fmt(Tr("%d could not be converted (see conversion_log.txt in the folder)."), static_cast<int>(r.keptOriginal)));
      } else if (r.phase == extra::MakeJob::Phase::Done) {
        ImGui::TextColored(kGood, Tr("Done: %llu made, %llu kept as the game's own (%s). Turn it on with the setting below."),
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
      if (ImGui::Checkbox(Lbl(Fmt(Tr("Use them in the game (setting: %s)"), Tr(def.name).c_str()).c_str(),
                              ("Use them in the game (setting: " + def.name + ")").c_str())
                              .c_str(),
                          &on)) {
        registry_.SetBool(src.switchSetting, on);
        SaveSettingsIfChanged();
      }
      Dim(Tr("The same switch is in Settings and in the game's SETTINGS page. It is an improvement, so it is not part of "
             "Original: turning it on here makes the preset Custom. The game reads it when it starts."));
    }
    if (ImGui::SmallButton(Lbl(Tr("Look again"), "Look again").c_str())) {
      ExtraLook(i);
      if (!disc.empty()) st.check.Start(src, disc, game_.extra);
    }
    if (looked && !running && !game_.playerFilesReadOnly &&
        (status.kind == extra::Status::Kind::Ready || status.kind == extra::Status::Kind::OutOfDate)) {
      ImGui::SameLine();
      if (ImGui::SmallButton(Lbl(Tr("Remove what was made"), "Remove what was made").c_str())) extraAsk_ = i;
    }
    if (extraWarn_ == i) {
      ImGui::OpenPopup("###extrawarn");
      extraWarn_ = -1;
      extraWarnClose_ = false;
    }
    if (ImGui::BeginPopupModal(Lbl(Tr("This takes a while"), "extrawarn").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (extraWarnClose_) {
        extraWarnClose_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
      ImGui::TextWrapped("%s", Tr(src.thirdWarning).c_str());
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
      if (ImGui::Button(Lbl(Tr("Start making"), "Start making").c_str())) {
        ImGui::CloseCurrentPopup();
        ExtraMake(i, true);
        extraWarnClose_ = false;
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Not now"), "Not now").c_str()) || BackPressed()) ImGui::CloseCurrentPopup();
      ImGui::SetItemDefaultFocus();
      ImGui::EndPopup();
    }
    if (extraAsk_ == i) {
      ImGui::OpenPopup("###removeextra");
      extraAsk_ = -1;
    }
    if (ImGui::BeginPopupModal(Lbl(Tr("Remove this?"), "removeextra").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text(Tr("Remove \"%s\"?"), Tr(src.provides).c_str());
      Dim(Fmt(Tr("The folder \"%s\" in extra_assets (in your data folder) is deleted. Your own discs and dumps are not touched; you can make it again."),
              src.id.c_str()));
      if (ImGui::Button(Lbl(Tr("Remove it"), "Remove it").c_str())) {
        ImGui::CloseCurrentPopup();
        ExtraRemove(i);
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Keep it"), "Keep it").c_str()) || BackPressed()) ImGui::CloseCurrentPopup();
      ImGui::SetItemDefaultFocus();
      ImGui::EndPopup();
    }
    (void)converterFound;
    EndCard();
    ImGui::PopID();
    ImGui::Spacing();
  }
}

void App::DrawExtraHdTextures() {
  if (!game_.textureGameId.empty()) {
    const fs::path folder = game_.settingsFolder / "textures" / game_.textureGameId;
    BeginCard("##hdtextures");
    Heading(Tr("HD texture packs"));
    Dim(Fmt(Tr("Texture packs made for Dolphin work here with the same file names. Each pack in %s is listed on the Mods page like a "
               "mod: on from the start (it only changes pictures), and the order there says which pack wins when two replace the "
               "same picture. A pack that lies in a folder of its own inside that folder is one entry; a mod can also carry a "
               "\"textures\" folder. The plain GameCube game (the Original preset) never uses a pack. No pictures from the game "
               "are shipped with the port."),
            Utf8(folder).c_str()));
    if (ImGui::SmallButton(Lbl(Tr("Open the textures folder"), "Open the textures folder").c_str())) {
      std::error_code ec;
      fs::create_directories(folder, ec);
      OpenInSystem(folder);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(Lbl(Tr("Go to the Mods page"), "Go to the Mods page").c_str())) page_ = Page::Mods;
    EndCard();
    ImGui::Spacing();
  }
}

void App::DrawExtraPage() {
  PageTitle(Tr("Extra assets"),
            Tr("Discs of other games that this port can borrow assets from. Your discs are only read; what is made from "
               "them is made on this PC, kept in your own data folder (extra_assets) and never sent anywhere."));
  const auto& sources = game_.extra.sources;
  const int count = std::min(static_cast<int>(sources.size()), static_cast<int>(extras_.size()));
  {
    std::string have;
    for (int i = 0; i < count; i++) {
      std::lock_guard<std::mutex> lock(extras_[i]->mutex);
      if (extras_[i]->looked && (extras_[i]->status.kind == extra::Status::Kind::Ready || extras_[i]->status.kind == extra::Status::Kind::OutOfDate)) {
        have += (have.empty() ? "" : ", ") + std::string(Tr(sources[i].provides));
      }
    }
    if (have.empty()) Dim(Tr("Installed: nothing yet."));
    else Dim(Fmt(Tr("Installed: %s."), have.c_str()));
  }
  struct Tab { const char* id; const char* english; std::vector<int> rows; bool hd = false; };
  std::vector<Tab> tabs = {{"models", "Models", {}}, {"languages", "Languages", {}}, {"other", "Other", {}}};
  for (int i = 0; i < count; i++) {
    Tab& t = sources[i].tab == "models" ? tabs[0] : sources[i].tab == "languages" ? tabs[1] : tabs[2];
    t.rows.push_back(i);
  }
  for (Tab& t : tabs) {
    std::stable_sort(t.rows.begin(), t.rows.end(), [&](int a, int b) { return sources[a].tabOrder < sources[b].tabOrder; });
  }
  tabs[2].hd = !game_.textureGameId.empty();
  extraTabIds_.clear();
  if (ImGui::BeginTabBar("##extratabs")) {
    for (const Tab& t : tabs) {
      if (t.rows.empty() && !t.hd) continue;
      extraTabIds_.push_back(t.id);
      const bool select = !wantExtraTab_.empty() && wantExtraTab_ == t.id;
      if (ImGui::BeginTabItem(Lbl(Tr(t.english), t.english).c_str(), nullptr, select ? ImGuiTabItemFlags_SetSelected : 0)) {
        curExtraTab_ = t.id;
        ImGui::BeginChild("##extrascroll", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        for (int i : t.rows) {
          DrawExtraRow(i);
        }
        if (t.hd) DrawExtraHdTextures();
        Dim(Tr("More discs can be added to this page later."));
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    wantExtraTab_.clear();
    ImGui::EndTabBar();
  }
}

void App::DrawDiscPage() {
  PageTitle(Tr("Your disc"), Tr(game_.discHelp));
  std::string path = DiscPath();
  ImGui::TextColored(kDim, "%s", Tr("Disc image file"));
  const float checkW = ImGui::CalcTextSize(Tr("Check again")).x + ImGui::GetStyle().FramePadding.x * 2;
  const float chooseW = ImGui::CalcTextSize(Tr("Choose...")).x + ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(-std::max(250 * scale_, checkW + chooseW + ImGui::GetStyle().ItemSpacing.x * 3 + 12 * scale_));
  ImGui::InputText("##disc", &path, ImGuiInputTextFlags_ReadOnly);
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Choose..."), "Choose...").c_str())) {
    static SDL_DialogFileFilter kFilters[2];
    kFilters[0] = {Tr("Disc images (ISO, RVZ, GCM, WIA, GCZ, CISO, WBFS)"), "iso;gcm;rvz;wia;gcz;ciso;wbfs"};
    kFilters[1] = {Tr("All files"), "*"};
    const std::string current = DiscPath();
    const std::string start = current.empty() ? std::string() : Utf8(PathOf(current).parent_path());
    ShowOpenDialog(
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
  if (ImGui::Button(Lbl(Tr("Check again"), "Check again").c_str())) {
    StartWorker(path);
  }
  if (path.empty()) {
    ImGui::EndDisabled();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));

  BeginCard("##status");
  if (path.empty()) {
    ImGui::TextColored(kWarn, "%s", Tr("No disc chosen yet"));
    Dim(Tr("Click \"Choose...\" and pick your disc image file. The launcher remembers it for next time."));
  } else {
    bool have;
    DiscVerdict v;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      have = bg_.haveVerdict && bg_.discPath == path;
      v = bg_.verdict;
    }
    if (!have) {
      ImGui::TextColored(kBusy, "%s", Tr("Checking your disc image..."));
      const uint64_t total = bg_.total, done = bg_.done;
      if (total > 0) {
        Dim(Tr("The first time a disc image is used, all of it is read to make sure it is complete and "
               "unchanged. This takes a few seconds; after that it is remembered."));
        ImGui::ProgressBar(static_cast<float>(static_cast<double>(done) / static_cast<double>(total)), ImVec2(-1, 0));
      }
    } else {
      ImGui::PushFont(fonts_.heading);
      switch (v.kind) {
      case DiscVerdict::Kind::Good: ImGui::TextColored(kGood, "%s", Tr("Ready to play")); break;
      case DiscVerdict::Kind::Usable: ImGui::TextColored(kGood, "%s", Tr("Ready to play (content not compared)")); break;
      case DiscVerdict::Kind::Warning: ImGui::TextColored(kWarn, "%s", Tr("Can be played, but read this first")); break;
      case DiscVerdict::Kind::Problem: ImGui::TextColored(kBad, "%s", Tr("This disc image can't be used")); break;
      }
      ImGui::PopFont();
      if (!v.edition.empty()) {
        ImGui::TextColored(kGood, "%s", (v.edition + " mode").c_str());
      }
      ImGui::TextWrapped("%s", Tr(v.text).c_str());
      if (v.kind == DiscVerdict::Kind::Problem && game_.extra.otherVersion && game_.extra.otherVersion(v.gameId) &&
          !game_.extra.sources.empty()) {
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        if (ImGui::Button(Lbl(Tr("Go to the Extra assets page"), "Go to the Extra assets page").c_str())) page_ = Page::Extra;
      }
      const uint32_t shadersTotal = bg_.shadersTotal;
      if (bg_.preparing && bg_.shadersRunning && v.kind != DiscVerdict::Kind::Problem) {
        ImGui::Dummy(ImVec2(0, 4 * scale_));
        ImGui::TextColored(kBusy, "%s", Tr("Getting the game ready for your PC..."));
        Dim(Tr("This happens once (and again after a graphics driver or game update) and takes about a minute. It runs "
               "quietly in the background: no game window opens, and there is nothing to close. You can look around "
               "meanwhile."));
        if (playWhenDrawn_) {
          Dim(Tr("The game starts as soon as this is done."));
        }
        const uint32_t done = std::min(bg_.shadersDone.load(), shadersTotal);
        char label[64];
        if (shadersTotal > 0) {
          std::snprintf(label, sizeof label, "%u / %u", done, shadersTotal);
        } else {
          std::snprintf(label, sizeof label, "%s", Tr("starting..."));
        }
        ImGui::ProgressBar(shadersTotal > 0 ? static_cast<float>(done) / static_cast<float>(shadersTotal) : 0.0f,
                           ImVec2(-1, 0), label);
      }
    }
  }
  EndCard();
  ImGui::Spacing();
  Dim(Tr("The game never changes your disc image: mods and settings are kept separately."));
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
  Heading(Tr("Icon, background and shortcuts"));
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
  std::string chosenName = Tr("the program's own icon");
  const bool icons = sys::HasIcons();
  if (icons) {
    ImGui::TextUnformatted(Tr("Icon of the windows, the taskbar and the shortcuts:"));
  } else {
    Dim(Tr("Choosing another icon for the windows and shortcuts isn't there on this system yet: they show the "
           "program's own."));
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
    if (ImGui::InvisibleButton("##icon", ImVec2(side, side), ImGuiButtonFlags_EnableNav)) {
      registry_.SetText(kIconSetting, id);
    }
    const bool hovered = Hot();
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
    Tile(i < ownIcons_.size() ? ownIcons_[i] : nullptr, game_.ownIcons[i].id, Tr(game_.ownIcons[i].name));
  }
  for (size_t i = 0; icons && i < choices.size(); i++) {
    Tile(i < choiceIcons_.size() ? choiceIcons_[i] : nullptr, choices[i].first, choices[i].second);
  }
  if (icons) {
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    ImGui::Text(Tr("In use: %s"), chosenName.c_str());
  }
  if (!grounds_.empty()) {
    ImGui::Dummy(ImVec2(0, 6 * scale_));
    ImGui::TextUnformatted(Tr("Launcher background:"));
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
      if (ImGui::InvisibleButton("##ground", ImVec2(gw, gh), ImGuiButtonFlags_EnableNav)) {
        registry_.SetText(kGroundSetting, i < 0 ? std::string("none") : grounds_[i].first);
      }
      const bool hovered = Hot();
      if (hovered) {
        ImGui::SetTooltip("%s", i < 0 ? Tr("None (plain)") : grounds_[i].second.c_str());
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 q(p.x + gw, p.y + gh);
      dl->AddRectFilled(p, q, U32(kGround), 7 * scale_);
      if (i >= 0 && i < static_cast<int>(groundTex_.size()) && groundTex_[i] != nullptr) {
        dl->AddImageRounded(TexId(groundTex_[i]), p, q, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 7 * scale_);
      } else if (i < 0) {
        const ImVec2 ts = ImGui::CalcTextSize(Tr("None"));
        dl->AddText(ImVec2(p.x + std::max(0.0f, (gw - ts.x) / 2), p.y + (gh - ts.y) / 2), U32(kDim), Tr("None"));
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
                              : std::string(Tr("Once your disc has been checked, more icons are made from its pictures "
                                               "(only on this PC, in your data folder).")));
  } else if (grounds_.empty()) {
    Dim(Tr("Once your disc has been checked, launcher backgrounds are made from its pictures (only on this PC, in "
           "your data folder)."));
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));
  if (!sys::ShortcutsHere()) {
    Dim(Tr("There is no shortcut to make on this system: drag the Pokemon XD app to the Dock, or keep it in "
           "Applications."));
  } else {
    if (ImGui::Button(Lbl(Fmt(Tr("Create %s shortcut"), ProgramsName().c_str()).c_str(),
                          (std::string("Create ") + sys::ProgramsFolderName() + " shortcut").c_str())
                          .c_str())) {
      Shortcut(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Create desktop shortcut"), "Create desktop shortcut").c_str())) {
      Shortcut(true);
    }
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
  if (!ImGui::IsPopupOpen("###notopened")) {
    ImGui::OpenPopup("###notopened");
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(Lbl(Tr("Couldn't open it"), "notopened").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(Tr("Your system didn't open this by itself. It is here:"));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560 * scale_);
    ImGui::TextColored(kText, "%s", notOpened_.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button(Lbl(Tr("Copy the path"), "Copy the path").c_str())) {
      SDL_SetClipboardText(notOpened_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("OK"), "OK").c_str()) || closeWindows_ || BackPressed()) {
      notOpened_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::DrawPresetPage() {
  PageTitle(Tr("How do you want to play?"), Tr("Pick a preset. You can change single settings later on the Settings page."));
  if (ImGui::Button(Lbl(Tr("What's the difference?"), "whatsdiff").c_str())) {
    askPreset_ = true;
  }
  ImGui::Spacing();
  ui::DrawPresetChooser(registry_, options_);
  std::vector<size_t> startRows;
  for (const char* id : {"playthrough.new_rule_set", "playthrough.new_pokemon", "playthrough.new_looks"}) {
    const size_t at = registry_.Find(id);
    if (at != es::kNoSetting) startRows.push_back(at);
  }
  if (!startRows.empty()) {
    ImGui::Spacing();
    Heading(Tr("New playthroughs start with"));
    Dim(Tr("The New Game screen starts with these. Each playthrough keeps its own choices; only pictures change with Looks."));
    if (ImGui::BeginTable("##newplay", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
      ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 3.0f);
      ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
      for (size_t at : startRows) ui::DrawSettingRow(registry_, at, options_);
      ImGui::EndTable();
    }
  }
}

void App::DrawSettingsPage() {
  ImGui::PushFont(fonts_.title);
  ImGui::TextUnformatted(Tr("Settings"));
  ImGui::PopFont();
  const es::Preset preset = registry_.CurrentPreset();
  const std::string line =
      Fmt(preset == es::Preset::Original
              ? Tr("Preset: %s. Changing a game setting switches to Custom; volumes, window and controls never do.")
              : Tr("Preset: %s. Settings marked \"Changed\" are not as on the GameCube."),
          Tr(ui::PresetName(preset)));
  Dim(line);
  ImGui::Spacing();
  tabIds_.clear();
  if (ImGui::BeginTabBar("##cats")) {
    for (es::Category c : {es::Category::Video, es::Category::Audio, es::Category::Gameplay,
                           es::Category::Accessibility, es::Category::System}) {
      if (ui::VisibleSettings(registry_, c, options_).empty()) {
        continue;
      }
      tabIds_.push_back(es::CategoryId(c));
      const bool select = !wantTab_.empty() && wantTab_ == es::CategoryId(c);
      if (ImGui::BeginTabItem(Lbl(Tr(es::CategoryName(c)), es::CategoryName(c)).c_str(), nullptr,
                              select ? ImGuiTabItemFlags_SetSelected : 0)) {
        curTab_ = es::CategoryId(c);
        ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        ui::DrawCategory(registry_, c, options_);
        ImGui::Spacing();
        const char* baseName = ui::PresetName(preset == es::Preset::Custom ? registry_.BasePreset() : preset);
        const std::string reset = Lbl(Fmt(Tr("Back to %s on this page"), Tr(baseName)).c_str(),
                                      (std::string("Back to ") + baseName + " on this page").c_str());
        if (ImGui::Button(reset.c_str())) {
          ui::ResetCategory(registry_, c, options_);
        }
        if (!settingsNotes_.empty() && c == es::Category::System) {
          ImGui::Spacing();
          Heading(Tr("Notes about your settings file"));
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

void App::LookAtAdapter() {
  const uint64_t tick = SDL_GetTicks();
  if (adapterLookedAt_ != 0 && tick - adapterLookedAt_ < 3000) {
    return;
  }
  adapterLookedAt_ = tick == 0 ? 1 : tick;
  namespace ec = engine::controls;
  if (const char* fake = std::getenv("XD_GC_ADAPTER_FAKE"); fake != nullptr && *fake != 0) {
    const std::string f = Lower(fake);
    ec::AdapterState s = ec::AdapterState::NotPlugged;
    s = f == "nodriver" ? ec::AdapterState::NoDriver : f == "nopermission" ? ec::AdapterState::NoPermission
        : f == "inuse" ? ec::AdapterState::InUse : f == "reachable" ? ec::AdapterState::Reachable
        : f == "unknown" ? ec::AdapterState::Unknown : s;
    controls_.SetAdapterState(s, -99);
    return;
  }
  if (controls_.AdapterPadShown()) {
    controls_.SetAdapterState(ec::AdapterState::Reachable, 0);
    return;
  }
  const ec::AdapterProbe probe = ec::ProbeGameCubeAdapter();
  if (probe.state != lastAdapterState_) {
    lastAdapterState_ = probe.state;
    Log("adapter: GameCube adapter state " + std::to_string(static_cast<int>(probe.state)) + " (libusb error " +
        std::to_string(probe.libusbError) + ")");
  }
  controls_.SetAdapterState(probe.state, probe.libusbError);
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
  PageTitle(Tr("Controls"), "");
  Dim(Tr("Which PC controller and which keys play each of the four GameCube controllers. Controls are yours in every "
         "preset, Original too: they change how you press the buttons, never what the game does with them."));
  ImGui::Spacing();
  LookAtAdapter();
  FeedLiveInputs();
  controls_.DrawPortTabs(registry_, options_);
  ImGui::BeginChild("##scroll", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
  const std::string n = std::to_string(controls_.Port() + 1);
  if (BeginCard("##ctl_pc")) {
    Heading(Fmt(Tr("Controller %s"), n.c_str()).c_str());
    controls_.DrawController(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_test")) {
    Heading(Tr("Try it"));
    controls_.DrawLiveTest(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_map")) {
    Heading(Tr("Buttons"));
    controls_.DrawMapping(registry_, options_);
  }
  EndCard();
  ImGui::Spacing();
  if (BeginCard("##ctl_sticks")) {
    Heading(Tr("Sticks and triggers"));
    controls_.DrawSticks(registry_, options_);
  }
  EndCard();
  ui::Options generic = options_;
  generic.hiddenPrefixes = {"keyboard", "gamepad"};
  if (!ui::VisibleSettings(registry_, es::Category::Controls, generic).empty()) {
    ImGui::Spacing();
    if (BeginCard("##ctl_all")) {
      Heading(Tr("For every controller"));
      ui::DrawCategory(registry_, es::Category::Controls, generic);
    }
    EndCard();
  }
  ImGui::EndChild();
}

void App::DrawModsPage() {
  if (reviewDirty_) RebuildReview();
  PageTitle(Tr("Mods"), "");
  Dim(Tr("Mods change the game's files (on top of your disc; the disc itself is never changed). A mod is only used "
         "when it is switched on here. Further down the list wins when two mods change the same thing."));
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr("Mod list:"));
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
  if (ImGui::Button(Lbl(Tr("New list..."), "New list...").c_str())) {
    newProfile_.clear();
    ImGui::OpenPopup("###newmodlist");
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Open the mods folder"), "Open the mods folder").c_str())) {
    std::error_code ec;
    fs::create_directories(game_.modsFolder, ec);
    OpenInSystem(game_.modsFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Look again"), "Look again").c_str())) {
    ReloadMods();
  }
  if (ImGui::Button(Lbl(Tr("Check for mod updates"), "Check for mod updates").c_str())) {
    modUpd_.RequestCheck(true);
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Put in working order"), "Put in working order").c_str())) {
    modUpd_.SortList(true);
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Save this list to a file..."), "Save this list to a file...").c_str())) {
    modUpd_.StartExport();
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Use a list from a file..."), "Use a list from a file...").c_str())) {
    modUpd_.StartImport();
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("New mod list"), "newmodlist").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(Tr("A name for the new list (for example \"randomizer run\"):"));
    ImGui::SetNextItemWidth(320 * scale_);
    pad::InputText("##name", &newProfile_);
    bool usable = !newProfile_.empty() && newProfile_.find_first_of("[]=;#") == std::string::npos &&
                  newProfile_.front() != ' ' && newProfile_.back() != ' ';
    for (const em::ModList::Profile& p : mods_.profiles) {
      usable &= p.name != newProfile_;
    }
    if (!usable) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(Lbl(Tr("Make it"), "Make it").c_str())) {
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
    if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str()) || BackPressed()) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::Spacing();

  std::string discGame;
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    if (bg_.haveVerdict) {
      discGame = em::MakeTarget(bg_.verdict.gameId, Lower(bg_.verdict.edition));
    }
  }
  auto& list = mods_.Active().mods;
  if (list.empty()) {
    ImGui::BeginChild("##none", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
    ImGui::TextUnformatted(Tr("No mods yet."));
    Dim(Fmt(Tr("To add a mod, unpack it into its own folder inside the mods folder (\"Open the mods folder\"), then "
               "click \"Look again\". Mods: %s"),
            Utf8(game_.modsFolder).c_str()));
    ImGui::EndChild();
  } else if (ImGui::BeginTable("##mods", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                               ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn(Lbl(Tr("On"), "On").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Lbl(Tr("Mod"), "Mod").c_str(), ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn(Lbl(Tr("Version, made by"), "Version, made by").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn(Lbl(Tr("Order"), "Order").c_str(), ImGuiTableColumnFlags_WidthFixed);
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
      bool wantOn = on;
      if (ImGui::Checkbox("##on", &wantOn)) {
        if (wantOn && mod != nullptr) {
          turnOnRequest_ = id;
        } else {
          on = wantOn;
          SaveMods();
        }
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(mod != nullptr && !mod->manifest.name.empty() ? mod->manifest.name.c_str() : id.c_str());
      if (mod != nullptr && !mod->texturePack) {
        const std::string tag = em::GameTag(mod->manifest);
        if (!tag.empty()) {
          ImGui::SameLine();
          ImGui::TextColored(kDim, "[%s]", tag.c_str());
        }
      }
      if (mod == nullptr) {
        ImGui::TextColored(kWarn, "%s", Tr("Not in the mods folder any more (left in the list; it isn't used)."));
      } else {
        if (!mod->manifest.description.empty()) {
          Dim(mod->manifest.description);
        }
        if (mod->manifest.scripts) {
          std::set<std::string>& scriptsOff = mods_.Active().scriptsOff;
          bool allow = scriptsOff.count(id) == 0;
          ImGui::TextColored(allow ? kWarn : kDim, "%s", Tr("Runs scripts (Luau)"));
          ImGui::SameLine();
          if (ImGui::Checkbox(Lbl(Tr("Allow"), "Allow").c_str(), &allow)) {
            if (allow) scriptsOff.erase(id); else scriptsOff.insert(id);
            SaveMods();
          }
          Dim(allow ? Tr("Its scripts run in a locked box: they cannot touch your files, but they can change how the game plays. "
                         "Untick Allow to skip them; the rest of the mod stays.")
                    : Tr("Its scripts are skipped. The rest of the mod (files, data) is still used."));
        }
        {
          const em::AppliesVerdict v = em::CheckApplies(*mod, game_.modLooks);
          std::string when;
          if (v.Downgraded()) {
            when = Fmt(Tr("Applies: %s (it says %s, but %s)"), Tr(em::AppliesName(v.effective)),
                       Tr(em::AppliesName(v.claimed)), v.reason.c_str());
          } else if (v.effective == em::Applies::NewPlaythrough) {
            when = Fmt(Tr("Applies: %s (chosen at New Game)"), Tr(em::AppliesName(v.effective)));
          } else {
            when = Fmt(Tr("Applies: %s"), Tr(em::AppliesName(v.effective)));
          }
          Dim(when);
        }
        if (!discGame.empty()) {
          if (!em::SupportsGame(mod->manifest, discGame)) {
            ImGui::TextColored(kWarn, "%s", (em::NotForTargetText(mod->manifest, discGame) + " " + Tr("It won't be used with this disc.")).c_str());
          }
        }
        if (const auto works = worksWith_.find(mod->manifest.id); works != worksWith_.end() && !works->second.empty()) {
          Dim(Fmt(Tr("Works with: %s"), works->second.c_str()));
        }
        for (const em::ReviewRow& row : review_.rows) {
          if (Lower(row.id) != Lower(id)) continue;
          if (on) {
            for (const std::string& p : row.problems) {
              if (p.rfind("Made for ", 0) == 0) continue;
              ImGui::PushStyleColor(ImGuiCol_Text, kBad);
              ImGui::TextWrapped("%s", (std::string(Tr("Not used: ")) + p).c_str());
              ImGui::PopStyleColor();
            }
          }
          for (const std::string& w : row.warnings) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
            ImGui::TextWrapped("%s", w.c_str());
            ImGui::PopStyleColor();
          }
          if (on) {
            for (const std::string& w : row.suggestions) Dim(w);
          }
        }
        if (!mod->texturePack) {
          const bool busy = modTools_.Busy();
          if (busy) ImGui::BeginDisabled();
          if (ImGui::SmallButton(Lbl(Tr("Check this mod"), "Check this mod").c_str())) modTools_.StartCheck(mod->manifest.id);
          ImGui::SameLine();
          if (ImGui::SmallButton(Lbl(Tr("Pack for sharing..."), "Pack for sharing...").c_str())) modTools_.StartPack(mod->manifest.id);
          if (busy) ImGui::EndDisabled();
        }
        if (!on) {
          const auto hint = offHints_.find(id);
          if (hint != offHints_.end()) Dim(std::string(Tr("Can't be used yet. ")) + hint->second);
        }
      }
      ImGui::TableSetColumnIndex(2);
      if (mod != nullptr) {
        ImGui::TextUnformatted(mod->manifest.version.c_str());
        if (!mod->manifest.author.empty()) {
          Dim(mod->manifest.author);
        }
        std::string newer, why;
        if (modUpd_.Offers(mod->manifest.id, newer, &why)) {
          ImGui::TextColored(kGood, "%s", Fmt(Tr("Update %s found"), newer.c_str()).c_str());
          if (!why.empty()) Dim(Tr("It can't be installed from here (see \"Check for mod updates\")."));
        }
        const auto prev = prevInfo_.find(id);
        if (prev != prevInfo_.end() && prev->second.exists &&
            ImGui::SmallButton((Fmt(Tr("Roll back to %s"), prev->second.version.c_str()) + "##rb").c_str())) {
          modUpd_.AskRollBack(mod->manifest.id);
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
  if (!turnOnRequest_.empty()) {
    const std::string want = turnOnRequest_;
    turnOnRequest_.clear();
    modTools_.BeforeOn(want);
  }
  if (!modError_.empty()) {
    ImGui::TextColored(kBad, "%s", modError_.c_str());
  }
  for (const std::string& line : updaterLines_) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGood);
    ImGui::TextWrapped("%s", line.c_str());
    ImGui::PopStyleColor();
  }
  for (const em::Message& m : modMessages_) {
    ImGui::PushStyleColor(ImGuiCol_Text, m.level == em::Message::Level::Info ? kDim : kWarn);
    ImGui::TextWrapped("%s", m.text.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Spacing();
  Dim(Tr("The game checks the mods again when it starts (what they need, what they clash with) and tells you "
         "about any problem."));
}

void App::DrawSavePage() {
  PageTitle(SlotsOn() ? Tr("Your saves") : Tr("Your save"),
            SlotsOn() ? Tr("Your saves: which one the game loads, what is in each, and the backups the game keeps of it.")
            : game_.saveBackupsFolder.empty()
                ? Tr("What is saved on the game's memory card. Only shown here, never changed.")
                : Tr("What is saved on the game's memory card, and the backups the game keeps of it."));
  if (SlotsOn()) {
    DrawSlots();
    DrawDataCard();
    return;
  }
  DrawSaveSummary();
  if (!game_.saveBackupsFolder.empty()) {
    ImGui::Dummy(ImVec2(0, 8 * scale_));
    DrawBackups(false);
  }
  DrawDataCard();
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
        backupMessage_ = Tr("Close the game first: it has the memory card open.");
        return;
      }
      std::string error;
      const sl::Slot* open = SlotsOn() ? OpenSlot() : nullptr;
      const bool other = open != nullptr && open->id != slots_.Active().id;
      const fs::path card = other ? (game_.saves.cardFolder
                                         ? game_.saves.cardFolder(sl::RootOf(SavesFolder(), open->id), b.card)
                                         : fs::path())
                                  : game_.cardFolder ? game_.cardFolder(b.card) : fs::path();
      if (game_.saves.wrongEdition) {
        const std::string wrong = game_.saves.wrongEdition(b.folder / f.name);
        if (!wrong.empty()) {
          backupOk_ = false;
          backupMessage_ = wrong;
          Log("restore backup refused: " + wrong);
          return;
        }
      }
      const fs::path companions = card.empty() ? fs::path() : card.parent_path().parent_path() / L"playthroughs" / fs::path(b.card);
      backupOk_ = !card.empty() && bk::Restore(b, f.name, card, BackupsFolder(), bk::kKeep, error, companions);
      backupMessage_ = backupOk_ ? Fmt(Tr("Restored the backup of %s. The save you had before is in the list too."),
                                       b.when.c_str())
                                 : (error.empty() ? std::string(Tr("This card's folder isn't known.")) : error);
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
  backupMessage_ = Tr("Choose a backup first.");
}

void App::DrawBackups(bool inBox) {
  LookAtBackups(false);
  Heading(Tr("Backups"));
  if (inBox) {
    size_t count = 0;
    for (const bk::Backup& b : backups_) {
      count += b.files.size();
    }
    ImGui::AlignTextToFramePadding();
    if (count == 0) {
      Dim(Tr("None yet: the first one is made the next time the game saves over this save."));
    } else {
      ImGui::Text(Tr("%d kept, newest %s"), static_cast<int>(count), backups_.front().when.c_str());
      ImGui::SameLine();
      if (ImGui::Button(backupsShown_ ? Lbl(Tr("Hide backups"), "Hide backups").c_str()
                                      : Lbl(Tr("Show backups"), "Show backups").c_str())) {
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
  Dim(Fmt(Tr("Each time the game saves, it first keeps a copy of the save as it was (only when it changed). The newest "
             "%d are kept. Restore puts one back on the memory card."),
          static_cast<int>(bk::kKeep)));
  if (ImGui::Button(Lbl(Tr("Open folder"), "Open folder").c_str())) {
    std::error_code ec;
    fs::create_directories(BackupsFolder(), ec);
    OpenInSystem(BackupsFolder());
  }
  if (!inBox) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Look again"), "Look again").c_str())) {
      LookAtBackups(true);
    }
  }
  bool restore = false;
  if (backups_.empty()) {
    Dim(Tr("No backups yet: the first one is made the next time the game saves over an existing save."));
  } else if (ImGui::BeginTable("##backups", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn(Lbl(Tr("Backed up"), "Backed up").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Lbl(Tr("Save"), "Save").c_str(), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(Lbl(Tr("Card"), "Card").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Lbl(Tr("Size"), "Size").c_str(), ImGuiTableColumnFlags_WidthFixed);
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
          ImGui::TextColored(kDim, Tr("(saved %s)"), saved.c_str());
        }
        if (Hot()) {
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
        if (ImGui::SmallButton(Lbl(Tr("Restore..."), "Restore...").c_str())) {
          backupSel_ = id;
          backupMessage_.clear();
          ImGui::OpenPopup("###restorebackup");
        }
        if (gameRunning_) {
          ImGui::EndDisabled();
        }
        if (ImGui::BeginPopupModal(Lbl(Tr("Restore this backup?"), "restorebackup").c_str(), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text(Tr("Restore the backup of %s?"), b.when.c_str());
          Dim(Tr("This replaces your current save; the current one is backed up first."));
          if (ImGui::Button(Lbl(Tr("Restore it"), "Restore it").c_str())) {
            ImGui::CloseCurrentPopup();
            restore = true;
          }
          ImGui::SameLine();
          if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str()) || BackPressed()) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::SetItemDefaultFocus();
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
    Dim(Tr("Close the game to restore a backup (it has the memory card open)."));
  }
  if (!backupMessage_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, backupOk_ ? kGood : kBad);
    ImGui::TextWrapped("%s", backupMessage_.c_str());
    ImGui::PopStyleColor();
  }
  Dim(Fmt(Tr("Backups folder: %s"), Utf8(BackupsFolder()).c_str()));
}

fs::path App::BackupsFolder() const {
  if (!SlotsOn()) {
    return game_.saveBackupsFolder;
  }
  const sl::Slot* open = OpenSlot();
  return bk::FolderFor(sl::RootOf(SavesFolder(), open != nullptr ? open->id : slots_.Active().id));
}

void App::LookAtSlots(bool now) {
  if (!SlotsOn()) {
    return;
  }
  const uint64_t tick = SDL_GetTicks();
  if (!now && slotsLookedAt_ != 0 && tick - slotsLookedAt_ < 2000) {
    return;
  }
  {
    std::string edition;
    {
      std::lock_guard<std::mutex> lock(editionMutex_);
      edition = savesEdition_;
    }
    if (edition != slotsEdition_) {
      slotsEdition_ = edition;
      slots_ = sl::Index{};
      slotRows_.clear();
      openSlotSet_ = false;
      openSlot_.clear();
      now = true;
    }
  }
  const bool first = slots_.slots.empty();
  slotsLookedAt_ = tick == 0 ? 1 : tick;
  std::string note;
  sl::Index index = sl::Load(SavesFolder(), &note);
  if (!note.empty() && note != slotNote_) {
    Log("saves: " + note);
  }
  slotNote_ = note;
  const bool otherActive = !first && index.Active().id != slots_.Active().id;
  slots_ = std::move(index);
  slotRows_.clear();
  for (const sl::Slot& s : slots_.slots) {
    slotRows_.push_back(game_.saves.row ? game_.saves.row(sl::RootOf(SavesFolder(), s.id)) : GameInfo::SaveRow());
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
    openRead_.thread = std::thread([this, key, disc, root = sl::RootOf(SavesFolder(), open->id)] {
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
    why = Tr("Close the game first: it has the memory card open.");
    return true;
  }
  if (game_.playerFilesReadOnly) {
    why = Tr("Test run on the player's own files: the saves are not changed.");
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
    return SlotDone(false, Tr("There is no such save."));
  }
  if (slots_.Active().id == id) {
    return true;
  }
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  const std::string name = slot->name;
  if (!sl::SetActive(SavesFolder(), id, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  return SlotDone(true, Fmt(Tr("\"%s\" is the save the game loads now."), name.c_str()));
}

bool App::NewSlot(const std::string& name) {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const std::string wanted = name.empty() ? sl::NextName(slots_) : name;
  if (!sl::Create(SavesFolder(), wanted, id, error) || !sl::SetActive(SavesFolder(), id, error)) {
    LookAtSlots(true);
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  OpenSlotIs(slots_.Active().id);
  return SlotDone(true, Fmt(Tr("Made \"%s\": an empty save. The game offers New Game when it starts."),
                            slots_.Active().name.c_str()));
}

static const char* OpenFirst() { return Tr("Open a save first (click its row)."); }

bool App::RenameSlot(const std::string& name) {
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, OpenFirst());
  }
  const std::string id = open->id, before = open->name;
  if (!sl::Rename(SavesFolder(), id, name, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  const sl::Slot* after = slots_.Find(id);
  return SlotDone(true, Fmt(Tr("\"%s\" is called \"%s\" now."), before.c_str(), (after != nullptr ? after->name : name).c_str()));
}

bool App::DuplicateSlot() {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, OpenFirst());
  }
  const std::string from = open->name;
  if (!sl::Duplicate(SavesFolder(), open->id, id, error)) {
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  const sl::Slot* made = slots_.Find(id);
  return SlotDone(true, Fmt(Tr("Made \"%s\": a copy of \"%s\" (without its backups). \"%s\" is still the one the game "
                               "loads."),
                            (made != nullptr ? made->name : id).c_str(), from.c_str(), slots_.Active().name.c_str()));
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
      error = Fmt(Tr("Can't move %s away (%s)."), Utf8(folder).c_str(), ec.message().c_str());
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
    return SlotDone(false, OpenFirst());
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
  const bool ok = sl::Delete(SavesFolder(), gone.id,
                             [this](const fs::path& folder, std::string& e) { return BinFolder(folder, e); }, error);
  LookAtSlots(true);
  if (chosen && slots_.Active().id == gone.id) {
    ActiveSaveChanged();
  }
  backupsLookedAt_ = 0;
  if (!ok) {
    return SlotDone(false, error);
  }
  const std::string where = game_.saves.binFolder.empty() ? SysWord(sys::RecycleBinName()) : Utf8(game_.saves.binFolder);
  return SlotDone(true, first ? Fmt(Tr("\"%s\" is empty now: its save, setup and backups went to %s."), gone.name.c_str(),
                                    where.c_str())
                              : Fmt(chosen ? Tr("Deleted \"%s\" (it is in %s). \"%s\" is the save the game loads now.")
                                           : Tr("Deleted \"%s\" (it is in %s). \"%s\" is the save the game loads, as before."),
                                    gone.name.c_str(), where.c_str(), slots_.Active().name.c_str()));
}

std::string App::NamesDisc() {
  std::string disc = DiscPath();
  std::lock_guard<std::mutex> lock(bg_.mutex);
  if (!bg_.haveVerdict || bg_.discPath != disc || bg_.verdict.kind == DiscVerdict::Kind::Problem) {
    disc.clear();
  }
  return disc;
}

bool App::ImportSlot(const std::string& file) {
  std::string why, error;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  GameInfo::ImportCheck check;
  if (!game_.saves.checkImport || !game_.saves.checkImport(PathOf(file), NamesDisc(), check, error)) {
    return SlotDone(false, error.empty() ? std::string(Tr("This file can't be imported.")) : error);
  }
  if (check.editionUnknown) {
    return SlotDone(false, game_.saves.editionQuestion);
  }
  return ImportCommit(PathOf(file), check);
}

bool App::ImportCommit(const fs::path& file, const GameInfo::ImportCheck& check) {
  std::string why, error, id;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  LookAtSlots(true);
  const std::string name = sl::CleanName(check.name).empty() ? sl::NextName(slots_) : check.name;
  const bool made = check.gci.empty()
                        ? sl::Import(SavesFolder(), file, name, check.cardFolder, check.cardFileName, id, error)
                        : sl::ImportBytes(SavesFolder(), check.gci, name, check.cardFolder,
                                          check.cardFileName, id, error);
  if (!made || !sl::SetActive(SavesFolder(), id, error)) {
    LookAtSlots(true);
    return SlotDone(false, error);
  }
  LookAtSlots(true);
  OpenSlotIs(slots_.Active().id);
  return SlotDone(true, Fmt(Tr("Imported as \"%s\", the save the game loads now. The file you chose was only copied: your other "
                            "saves were not touched."),
                            slots_.Active().name.c_str()));
}

void App::ImportFind() {
  importOpen_ = true;
  slotMessage_.clear();
  if (!game_.saves.findImportable || importScan_.busy) {
    return;
  }
  StopImportScan();
  importScan_.busy = true;
  importScan_.logged = false;
  importScan_.thread = std::thread([this, disc = NamesDisc()] {
    std::vector<GameInfo::ImportOffer> offers = game_.saves.findImportable(disc);
    {
      std::lock_guard<std::mutex> lock(importScan_.mutex);
      importScan_.offers = std::move(offers);
      importScan_.have = true;
    }
    importScan_.busy = false;
  });
}

void App::StopImportScan() {
  if (importScan_.thread.joinable()) {
    importScan_.thread.join();
  }
}

bool App::ImportPick(const std::string& arg) {
  importOpen_ = true;
  importError_.clear();
  const bool number = !arg.empty() && std::all_of(arg.begin(), arg.end(), [](char c) { return c >= '0' && c <= '9'; });
  if (number) {
    std::lock_guard<std::mutex> lock(importScan_.mutex);
    const int n = std::atoi(arg.c_str());
    if (n < 1 || n > static_cast<int>(importScan_.offers.size())) {
      Log("import: no such found save: " + arg);
      return false;
    }
    importSel_ = n - 1;
    const GameInfo::ImportOffer& o = importScan_.offers[static_cast<size_t>(n - 1)];
    Log("import: chose found save " + arg + ": " + o.check.name + " from " + o.where);
    return true;
  }
  GameInfo::ImportOffer offer;
  offer.path = PathOf(arg);
  offer.where = "the file you chose";
  std::string error;
  importSel_ = -1;
  importHaveFile_ = false;
  if (!game_.saves.checkImport || !game_.saves.checkImport(offer.path, NamesDisc(), offer.check, error)) {
    importError_ = error.empty() ? std::string("This file can't be imported.") : error;
    Log("import: " + importError_);
    return false;
  }
  importFile_ = std::move(offer);
  importHaveFile_ = true;
  importSel_ = -2;
  Log("import: the chosen file holds a save of " + importFile_.check.name);
  return true;
}

bool App::ImportConfirm(int answer) {
  GameInfo::ImportOffer chosen;
  if (importSel_ == -2 && importHaveFile_) {
    chosen = importFile_;
  } else if (importSel_ >= 0) {
    std::lock_guard<std::mutex> lock(importScan_.mutex);
    if (importSel_ >= static_cast<int>(importScan_.offers.size())) {
      return SlotDone(false, "Choose a save first.");
    }
    chosen = importScan_.offers[static_cast<size_t>(importSel_)];
  } else {
    return SlotDone(false, "Choose a save first.");
  }
  if (chosen.check.editionUnknown) {
    if (answer < 0) {
      return SlotDone(false, game_.saves.editionQuestion);
    }
    const std::string refusal = game_.saves.editionRefusal ? game_.saves.editionRefusal(answer) : std::string();
    if (!refusal.empty()) {
      importError_ = refusal;
      Log("import refused: " + refusal);
      return false;
    }
  }
  const bool ok = ImportCommit(chosen.path, chosen.check);
  if (ok) {
    importOpen_ = false;
    importSel_ = -1;
    importHaveFile_ = false;
  }
  return ok;
}

void App::ImportChooseFile() {
  static const SDL_DialogFileFilter kCard[] = {{"GameCube saves and memory cards", "gci;raw;gcp;mcd;sav;gcs"},
                                               {"All files", "*"}};
  ShowOpenDialog(
      [](void* user, const char* const* files, int) {
        App* app = static_cast<App*>(user);
        if (files != nullptr && files[0] != nullptr) {
          std::lock_guard<std::mutex> lock(app->pickMutex_);
          app->slotPicked_ = files[0];
          app->slotPickedKind_ = 1;
        }
      },
      this, window_, kCard, 2, nullptr, false);
}

void App::DrawImport(bool locked) {
  if (!importOpen_) {
    return;
  }
  ImGui::PushID("import");
  ImGui::Spacing();
  Heading("Import from Dolphin / GameCube");
  Dim(Tr("Pokemon XD saves that this PC already has: in Dolphin's folders, found by themselves below, or in a file you "
         "choose (a .gci, a memory card image, a GameCube card dump). The USA game's saves are added as they are; a Japanese "
         "save is converted to the USA game's layout (names stay in Japanese characters), and a European save is added as "
         "it is. Look at what is in one, then add it. It is added as a NEW save: your other saves are never replaced, and the "
         "file is only copied."));
  if (locked) {
    ImGui::BeginDisabled();
  }
  const bool scanning = importScan_.busy;
  if (game_.saves.findImportable) {
    if (ImGui::Button("Look again")) {
      ImportFind();
    }
    ImGui::SameLine();
  }
  if (ImGui::Button(Lbl(Tr("Choose a file..."), "Choose a file...").c_str())) {
    ImportChooseFile();
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    importOpen_ = false;
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  if (!game_.saves.findImportable) {
  } else if (scanning) {
    ImGui::TextColored(kBusy, "Looking in Dolphin's folders...");
  } else {
    std::lock_guard<std::mutex> lock(importScan_.mutex);
    if (importScan_.have && importScan_.offers.empty()) {
      Dim("No Pokemon XD save found in Dolphin's folders (this PC has no Dolphin, or none of its cards holds one). "
          "If Dolphin keeps its files somewhere else, use Choose a file....");
    }
    for (size_t i = 0; i < importScan_.offers.size(); i++) {
      const GameInfo::ImportOffer& o = importScan_.offers[i];
      std::string who;
      std::string time;
      for (const auto& [label, value] : o.check.preview.facts) {
        if (label == "Trainer") who = value;
        if (label == "Play time") time = value;
      }
      const std::string label = who + "  -  " + time + "  -  " + o.where;
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(label.c_str(), importSel_ == static_cast<int>(i))) {
        importSel_ = static_cast<int>(i);
        importError_.clear();
      }
      ImGui::PopID();
    }
  }
  if (!importError_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    ImGui::TextWrapped("%s", importError_.c_str());
    ImGui::PopStyleColor();
  }
  if (importHaveFile_) {
    if (ImGui::Selectable(("The file you chose: " + importFile_.check.name).c_str(), importSel_ == -2)) {
      importSel_ = -2;
    }
  }
  const GameInfo::ImportCheck* shown = nullptr;
  GameInfo::ImportCheck copy;
  if (importSel_ == -2 && importHaveFile_) {
    copy = importFile_.check;
    shown = &copy;
  } else if (importSel_ >= 0) {
    std::lock_guard<std::mutex> lock(importScan_.mutex);
    if (importSel_ < static_cast<int>(importScan_.offers.size())) {
      copy = importScan_.offers[static_cast<size_t>(importSel_)].check;
      shown = &copy;
    }
  }
  if (shown != nullptr) {
    ImGui::Spacing();
    ImGui::Separator();
    Heading("What is in it");
    DrawSaveContents(shown->preview);
    ImGui::Spacing();
    if (locked) {
      ImGui::BeginDisabled();
    }
    if (shown->editionUnknown) {
      ImGui::TextWrapped("%s", game_.saves.editionQuestion.c_str());
      for (size_t a = 0; a < game_.saves.editionAnswers.size(); a++) {
        if (a > 0) {
          ImGui::SameLine();
        }
        if (ImGui::Button((game_.saves.editionAnswers[a] + "##ed").c_str())) {
          ImportConfirm(static_cast<int>(a));
        }
      }
    } else if (ImGui::Button("Add as a new save")) {
      ImportConfirm();
    }
    if (locked) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    Dim("It will be called \"" + (sl::CleanName(shown->name).empty() ? sl::NextName(slots_) : shown->name) + "\" (rename it afterwards if you like).");
    ImGui::Separator();
  }
  ImGui::PopID();
}

bool App::ExportSlot(const std::string& file) {
  LookAtSlots(true);
  const sl::Slot* open = OpenSlot();
  if (open == nullptr) {
    return SlotDone(false, OpenFirst());
  }
  std::string error;
  const fs::path from = game_.saves.saveFile ? game_.saves.saveFile(sl::RootOf(SavesFolder(), open->id))
                                             : fs::path();
  if (!sl::Export(from, PathOf(file), error)) {
    return SlotDone(false, error);
  }
  return SlotDone(true, Fmt(Tr("Saved a copy of \"%s\" as %s."), open->name.c_str(), file.c_str()));
}

void App::DrawSlots() {
  LookAtSlots(false);
  Heading(Tr("Saves"));
  Dim(Tr("Keep several saves side by side. The circle marks the one the game loads (CONTINUE, or New Game when it is "
         "empty). Click a save to see what is in it."));
  std::string why;
  const bool locked = SlotsLocked(why);
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(Lbl(Tr("New..."), "New...").c_str())) {
    slotName_ = sl::NextName(slots_);
    slotAsk_ = SlotAsk::New;
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Import from Dolphin / GameCube..."), "Import from Dolphin / GameCube...").c_str())) {
    if (importOpen_) {
      importOpen_ = false;
    } else {
      ImportFind();
    }
  }
  if (game_.saves.checkImport) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Import a Japanese save..."), "Import a Japanese save...").c_str())) {
      ImportFind();
      ImportChooseFile();
    }
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
  DrawImport(locked);

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
        ImGui::TableSetupColumn(Lbl(Tr("Save"), "Save").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(Lbl(Tr("Trainer"), "Trainer").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(Lbl(Tr("Play time"), "Play time").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                std::max(132 * scale_, ImGui::CalcTextSize(Tr("Play time")).x +
                                                           ImGui::GetStyle().CellPadding.x * 2 + 8 * scale_));
        ImGui::TableSetupColumn(Lbl(Tr("Playthrough"), "Playthrough").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.9f);
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
      if (Hot(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", loads ? Tr("The game loads this save.") : locked ? why.c_str() : Tr("Have the game load this save."));
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
      if (clipped && Hot()) {
        ImGui::SetTooltip("%s", s.name.c_str());
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::AlignTextToFramePadding();
      if (row.found) {
        ImGui::TextUnformatted(row.who.c_str());
      } else {
        ImGui::TextColored(kDim, "%s", row.who.empty() ? Tr("empty") : Tr(row.who).c_str());
      }
      if (!row.edition.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(row.warn.empty() ? kGood : kBad, "[%s]", row.edition.c_str());
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(row.time.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::AlignTextToFramePadding();
      ImGui::TextWrapped("%s", row.setup.c_str());
      if (!row.warn.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", row.warn.c_str());
        ImGui::PopStyleColor();
      }
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
    ImGui::OpenPopup("###newsave");
  } else if (slotAsk_ == SlotAsk::Rename) {
    ImGui::OpenPopup("###renamesave");
  } else if (slotAsk_ == SlotAsk::Delete) {
    ImGui::OpenPopup("###deletesave");
  }
  slotAsk_ = SlotAsk::None;
  for (const bool isNew : {true, false}) {
    if (ImGui::BeginPopupModal(Lbl(isNew ? Tr("New save") : Tr("Rename this save"), isNew ? "newsave" : "renamesave").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(isNew ? Tr("A new, empty save. What should it be called?") : Tr("The save's new name:"));
      ImGui::SetNextItemWidth(360 * scale_);
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }
      const bool enter = pad::InputText("##slotname", &slotName_, ImGuiInputTextFlags_EnterReturnsTrue);
      if (isNew) {
        Dim(Tr("The game then starts with New Game. Your other saves stay as they are."));
      }
      const bool usable = !sl::CleanName(slotName_).empty();
      if (!usable) {
        ImGui::BeginDisabled();
      }
      if ((ImGui::Button(isNew ? Lbl(Tr("Make it"), "Make it").c_str() : Lbl(Tr("Rename"), "Rename").c_str()) || enter) && usable) {
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
      if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Delete this save?"), "deletesave").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const sl::Slot* open = OpenSlot();
    if (open == nullptr) {
      ImGui::CloseCurrentPopup();
    } else {
      ImGui::Text(Tr("Delete \"%s\"?"), open->name.c_str());
      Dim(Fmt(Tr("Its save, its playthrough setup and its backups go to %s (you can take them back out of it)."),
              SysWord(sys::RecycleBinName()).c_str()));
      if (open->id == sl::kMainId) {
        Dim(Tr("This is the first save: its place in the list stays, empty."));
      }
      if (open->id == slots_.Active().id && open->id != sl::kMainId) {
        Dim(Tr("It is the save the game loads: after this the game loads the first save."));
      }
      if (ImGui::Button(Lbl(Tr("Delete it"), "Delete it").c_str())) {
        ImGui::CloseCurrentPopup();
        DeleteSlot();
      }
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Keep it"), "Keep it").c_str()) || BackPressed()) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
    }
    ImGui::EndPopup();
  }
}

bool App::TradePick(const std::string& file) {
  std::string why;
  const sl::Slot* open = OpenSlot();
  if (open == nullptr || !game_.saves.tradeOpen) {
    Log("trade: no save is open (openslot:<n> first)");
    return false;
  }
  tradeFor_ = open->id;
  tradeFile_ = PathOf(file);
  tradeError_.clear();
  tradeHave_ = false;
  tradeMine_ = tradeTheirs_ = -1;
  if (!game_.saves.tradeOpen(sl::RootOf(SavesFolder(), open->id), tradeFile_, NamesDisc(), tradeView_, tradeError_)) {
    Log("trade: " + tradeError_);
    return false;
  }
  tradeHave_ = true;
  Log("trade: " + std::to_string(tradeView_.mine.size()) + " Pokemon on your side, " + std::to_string(tradeView_.theirs.size()) +
      " on the " + tradeView_.theirsWho + "'s");
  for (size_t i = 0; i < tradeView_.mine.size(); i++) {
    const auto& m = tradeView_.mine[i];
    Log("trade:   yours #" + std::to_string(i + 1) + " " + m.species + " L" + m.level + (m.problem.empty() ? ": can go" : ": " + m.problem));
  }
  for (size_t i = 0; i < tradeView_.theirs.size(); i++) {
    const auto& m = tradeView_.theirs[i];
    Log("trade:   theirs #" + std::to_string(i + 1) + " " + m.species + " L" + m.level + (m.problem.empty() ? ": can go" : ": " + m.problem));
  }
  return true;
}

bool App::TradeConfirm() {
  std::string why;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  const sl::Slot* open = OpenSlot();
  if (open == nullptr || !tradeHave_ || tradeFor_ != open->id || !game_.saves.tradeDo) {
    return SlotDone(false, "Choose a GBA save first.");
  }
  if (tradeMine_ < 0 || tradeTheirs_ < 0) {
    return SlotDone(false, "Choose one Pokemon on each side.");
  }
  const std::string id = open->id;
  const GameInfo::Saves::TradeResult r = game_.saves.tradeDo(sl::RootOf(SavesFolder(), id), tradeFile_, NamesDisc(), tradeMine_, tradeTheirs_);
  tradeOk_ = r.ok;
  tradeMessage_ = r.message;
  SlotDone(r.ok, r.message);
  if (r.ok) {
    LookAtSlots(true);
    backupsLookedAt_ = 0;
    if (slots_.Active().id == id) {
      ActiveSaveChanged();
    } else {
      openReadAgain_ = true;
    }
    TradePick(Utf8(tradeFile_));
    tradeMine_ = tradeTheirs_ = -1;
  }
  return r.ok;
}

void App::DrawTrade(const sl::Slot& slot, bool locked) {
  if (tradeFor_ != slot.id) {
    return;
  }
  ImGui::PushID("trade");
  ImGui::Spacing();
  ImGui::Separator();
  Heading(game_.saves.tradeName.c_str());
  Dim("Trade one Pokemon of this save for one of a Game Boy Advance save (Ruby, Sapphire, Emerald, FireRed, LeafGreen), as "
      "the GameCube's link cable does. Every Pokemon is checked first: the game's own rules and \"could it exist\". Your GBA "
      "save is never changed: a new file is made next to it. Backups of both saves are made before anything is written.");
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Choose a GBA save...")) {
    static const SDL_DialogFileFilter kGba[] = {{"Game Boy Advance saves", "sav;srm;sa1"}, {"All files", "*"}};
    ShowOpenDialog(
        [](void* user, const char* const* files, int) {
          App* app = static_cast<App*>(user);
          if (files != nullptr && files[0] != nullptr) {
            std::lock_guard<std::mutex> lock(app->pickMutex_);
            app->slotPicked_ = files[0];
            app->slotPickedKind_ = 3;
          }
        },
        this, window_, kGba, 2, nullptr, false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Close##trade")) {
    tradeFor_.clear();
    tradeHave_ = false;
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  if (!tradeError_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    ImGui::TextWrapped("%s", tradeError_.c_str());
    ImGui::PopStyleColor();
  }
  if (tradeHave_) {
    Dim("GBA save: " + Utf8(tradeFile_));
    const auto drawSide = [&](const char* title, const std::string& who, const std::vector<GameInfo::Saves::TradeMon>& mons, int& sel) {
      ImGui::TextColored(kDim, "%s: %s", title, who.c_str());
      for (size_t i = 0; i < mons.size(); i++) {
        const auto& m = mons[i];
        if (m.problem == "empty") {
          continue;
        }
        const bool can = m.problem.empty();
        std::string label = "#" + std::to_string(i + 1) + "  " + m.species + "  Lv " + m.level + "  " + m.nickname +
                            (m.item == "-" ? "" : "  holds " + m.item);
        ImGui::PushID(static_cast<int>(i) + (title[0] == 'Y' ? 0 : 100));
        if (!can) {
          ImGui::BeginDisabled();
        }
        if (ImGui::Selectable(label.c_str(), sel == static_cast<int>(i))) {
          sel = static_cast<int>(i);
        }
        if (!can) {
          ImGui::EndDisabled();
          ImGui::Indent(24 * scale_);
          ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
          ImGui::TextWrapped("%s", m.problem.c_str());
          ImGui::PopStyleColor();
          ImGui::Unindent(24 * scale_);
        }
        ImGui::PopID();
      }
    };
    drawSide("Your save", tradeView_.mineWho, tradeView_.mine, tradeMine_);
    ImGui::Spacing();
    drawSide("GBA party", tradeView_.theirsWho, tradeView_.theirs, tradeTheirs_);
    for (const std::string& n : tradeView_.notes) {
      Dim(n);
    }
    if (locked) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Trade these two")) {
      TradeConfirm();
    }
    if (locked) {
      ImGui::EndDisabled();
    }
  }
  ImGui::PopID();
}

bool App::MonsPick(const std::string& file) {
  const sl::Slot* open = OpenSlot();
  const auto& openFn = monsKind_ == 1 ? game_.saves.monsOpen2 : game_.saves.monsOpen;
  if (open == nullptr || !openFn) {
    Log("import: no save is open (openslot:<n> first)");
    return false;
  }
  monsFor_ = open->id;
  monsFile_ = PathOf(file);
  monsError_.clear();
  monsHave_ = false;
  monsPicked_.clear();
  if (!openFn(sl::RootOf(SavesFolder(), open->id), monsFile_, NamesDisc(), monsView_, monsError_)) {
    Log("mons: " + monsError_);
    return false;
  }
  monsHave_ = true;
  monsPicked_.assign(monsView_.rows.size(), 0);
  Log("mons: " + std::to_string(monsView_.rows.size()) + " Pokemon in the " + (monsKind_ == 1 ? std::string("modded") : game_.saves.monsName) + " save of " + monsView_.who + "; room for " +
      std::to_string(monsView_.room) + " in this save's PC boxes");
  for (size_t i = 0; i < monsView_.rows.size(); i++) {
    const auto& r = monsView_.rows[i];
    Log("mons:   #" + std::to_string(i + 1) + " " + r.where + ": " + r.species + " L" + r.level + (r.problem.empty() ? ": can come" : ": " + r.problem));
    for (const std::string& l : r.lost) {
      Log("mons:       lost: " + l);
    }
  }
  return true;
}

bool App::MonsConfirm() {
  std::string why;
  if (SlotsLocked(why)) {
    return SlotDone(false, why);
  }
  const sl::Slot* open = OpenSlot();
  const auto& doFn = monsKind_ == 1 ? game_.saves.monsDo2 : game_.saves.monsDo;
  if (open == nullptr || !monsHave_ || monsFor_ != open->id || !doFn) {
    return SlotDone(false, "Choose a save to take Pokemon from first.");
  }
  std::vector<int> rows;
  for (size_t i = 0; i < monsPicked_.size(); i++) {
    if (monsPicked_[i] != 0) {
      rows.push_back(static_cast<int>(i));
    }
  }
  if (rows.empty()) {
    return SlotDone(false, "Tick at least one Pokemon.");
  }
  const std::string id = open->id;
  const GameInfo::Saves::TradeResult r = doFn(sl::RootOf(SavesFolder(), id), monsFile_, NamesDisc(), rows);
  SlotDone(r.ok, r.message);
  if (r.ok) {
    LookAtSlots(true);
    backupsLookedAt_ = 0;
    if (slots_.Active().id == id) {
      ActiveSaveChanged();
    } else {
      openReadAgain_ = true;
    }
    MonsPick(Utf8(monsFile_));
  }
  return r.ok;
}

void App::DrawMons(const sl::Slot& slot, bool locked) {
  if (monsFor_ != slot.id) {
    return;
  }
  ImGui::PushID("mons");
  ImGui::Spacing();
  ImGui::Separator();
  const bool modded = monsKind_ == 1;
  Heading(modded ? game_.saves.monsName2.c_str() : game_.saves.monsName.c_str());
  Dim(modded ? game_.saves.monsHelp2
             : std::string("Copy Pokemon out of a Pokemon Colosseum (USA) save into the PC boxes of this save. The Colosseum save is only read. "
                           "Look at what each Pokemon loses on the way before you confirm; Pokemon that can't come say why."));
  if (locked) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(modded ? game_.saves.monsChoose2.c_str() : "Choose a Colosseum save...")) {
    static const SDL_DialogFileFilter kColo[] = {{"GameCube saves and memory cards", "gci;raw;gcp;mcd"}, {"All files", "*"}};
    ShowOpenDialog(
        [](void* user, const char* const* files, int) {
          App* app = static_cast<App*>(user);
          if (files != nullptr && files[0] != nullptr) {
            std::lock_guard<std::mutex> lock(app->pickMutex_);
            app->slotPicked_ = files[0];
            app->slotPickedKind_ = 4;
          }
        },
        this, window_, kColo, 2, nullptr, false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Close##mons")) {
    monsFor_.clear();
    monsHave_ = false;
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  if (!monsError_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kBad);
    ImGui::TextWrapped("%s", monsError_.c_str());
    ImGui::PopStyleColor();
  }
  if (monsHave_) {
    Dim((modded ? game_.saves.monsSaveLabel2 : std::string("Colosseum save of")) + " " + monsView_.who + ": " + Utf8(monsFile_));
    int ticked = 0;
    for (char c : monsPicked_) {
      ticked += c != 0 ? 1 : 0;
    }
    if (monsView_.rows.empty()) {
      Dim("There are no Pokemon in that save.");
    }
    for (size_t i = 0; i < monsView_.rows.size(); i++) {
      const auto& r = monsView_.rows[i];
      ImGui::PushID(static_cast<int>(i));
      const bool can = r.problem.empty();
      if (!can) {
        ImGui::BeginDisabled();
      }
      bool on = monsPicked_[i] != 0;
      const std::string label = r.where + "   " + r.species + "  Lv " + r.level + "  " + r.nickname;
      if (ImGui::Checkbox(label.c_str(), &on)) {
        monsPicked_[i] = on ? 1 : 0;
      }
      if (!can) {
        ImGui::EndDisabled();
      }
      ImGui::Indent(28 * scale_);
      if (!can) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s %s", modded ? game_.saves.monsStays2.c_str() : "Stays in the Colosseum save:", r.problem.c_str());
        ImGui::PopStyleColor();
      }
      for (const std::string& l : r.lost) {
        Dim("Loses: " + l);
      }
      ImGui::Unindent(28 * scale_);
      ImGui::PopID();
    }
    for (const std::string& n : monsView_.notes) {
      Dim(n);
    }
    ImGui::Text("Room in this save's PC boxes: %d.  Ticked: %d.", monsView_.room, ticked);
    if (locked) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button(modded ? game_.saves.monsCopy2.c_str() : "Copy the ticked Pokemon into the PC boxes")) {
      MonsConfirm();
    }
    if (locked) {
      ImGui::EndDisabled();
    }
  }
  ImGui::PopID();
}

void App::DrawSlotBox(const sl::Slot& slot, bool locked, bool& duplicate) {
  ImGui::PushID("openbox");
  BeginCard("##box");
  Heading(slot.name.c_str());
  SaveSummary save;
  const bool have = OpenSummary(save);
  const bool found = have && save.state == SaveSummary::State::Found;
  const bool loads = slot.id == slots_.Active().id;
  Dim(loads ? Tr("The game loads this save.") : Tr("The game does not load this save (its circle is not ticked)."));
  if (!have) {
    ImGui::TextColored(kBusy, "%s", Tr("Reading the memory card..."));
  } else if (save.state == SaveSummary::State::None) {
    ImGui::TextUnformatted(Tr("No save yet: the game offers New Game."));
    Dim(Fmt(Tr("Folder: %s"), Utf8(sl::RootOf(SavesFolder(), slot.id)).c_str()));
  } else if (!found) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", Tr(save.message).c_str());
    ImGui::PopStyleColor();
    if (!save.where.empty()) {
      Dim(Fmt(Tr("Memory card: %s"), save.where.c_str()));
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
  if (ImGui::Button(Lbl(Tr("Rename..."), "Rename...").c_str())) {
    slotName_ = slot.name;
    slotAsk_ = SlotAsk::Rename;
  }
  if (found) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Duplicate"), "Duplicate").c_str())) {
      duplicate = true;
    }
  }
  if (found && game_.saves.monsOpen) {
    ImGui::SameLine();
    if (ImGui::Button((game_.saves.monsName + "...").c_str())) {
      monsKind_ = 0;
      monsFor_ = slot.id;
      monsHave_ = false;
      monsError_.clear();
    }
  }
  if (found && game_.saves.monsOpen2) {
    ImGui::SameLine();
    if (ImGui::Button((game_.saves.monsName2 + "...").c_str())) {
      monsKind_ = 1;
      monsFor_ = slot.id;
      monsHave_ = false;
      monsError_.clear();
    }
  }
  if (found && game_.saves.tradeOpen) {
    ImGui::SameLine();
    if (ImGui::Button((game_.saves.tradeName + "...").c_str())) {
      tradeFor_ = slot.id;
      tradeHave_ = false;
      tradeError_.clear();
      tradeMine_ = tradeTheirs_ = -1;
    }
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  if (found) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Export..."), "Export...").c_str())) {
      static SDL_DialogFileFilter kGci[1];
      kGci[0] = {Tr("GameCube save files"), "gci"};
      const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
      const std::string where = Utf8(start / PathOf(game_.saves.exportName));
      slotMessage_.clear();
      ShowSaveDialog(
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
  if (ImGui::Button(Lbl(Tr("Delete..."), "Delete...").c_str())) {
    slotAsk_ = SlotAsk::Delete;
  }
  if (locked) {
    ImGui::EndDisabled();
  }
  DrawTrade(slot, locked);
  DrawMons(slot, locked);
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
    ImGui::TextColored(kBusy, "%s", Tr("Reading the memory card..."));
    return;
  }
  if (save.state != SaveSummary::State::Found) {
    ImGui::TextColored(save.state == SaveSummary::State::None ? kDim : kWarn, "%s", Tr(save.message).c_str());
    if (!save.where.empty()) {
      Dim(Fmt(Tr("Memory card: %s"), save.where.c_str()));
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
      ImGui::TextColored(kDim, "%s", Tr(label).c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(value.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::Spacing();
  Heading(Tr("Party"));
  const int columns = static_cast<int>(save.partyColumns.size());
  if (save.party.empty()) {
    Dim(Tr("No Pokemon in the party yet."));
  } else if (columns > 0 && ImGui::BeginTable("##party", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                                       ImGuiTableFlags_PadOuterX)) {
    for (const std::string& c : save.partyColumns) {
      ImGui::TableSetupColumn(Tr(c).c_str());
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
    Dim(Tr(n));
  }
  DrawSetup(save);
  ImGui::Spacing();
  Dim(Fmt(Tr("Memory card: %s"), save.where.c_str()));
}

void App::DrawSetup(const SaveSummary& save) {
  if (save.setup.empty() && save.setupNotes.empty()) {
    return;
  }
  ImGui::Spacing();
  Heading(Tr("Playthrough"));
  if (ImGui::BeginTable("##setup", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& [label, value] : save.setup) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(kDim, "%s", Tr(label).c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextWrapped("%s", value.c_str());
    }
    ImGui::EndTable();
  }
  for (const std::string& n : save.setupNotes) {
    Dim(Tr(n));
  }
  if (!save.looksStyles.empty() && game_.changeLooks) {
    if (ImGui::Button(Lbl(Tr("Change looks..."), "Change looks...").c_str())) {
      askLooks_ = true;
    }
    Dim(looksMessage_.empty() ? std::string(Tr("Looks only change the pictures of this playthrough, never how battles work. A change shows the next "
                                               "time the game starts."))
                              : looksMessage_);
  }
  if (askLooks_) {
    askLooks_ = false;
    ImGui::OpenPopup("###looks");
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Change looks"), "looks").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(Tr("Which models does this playthrough show?"));
    Dim(Tr("Pictures only: how a Pokemon looks, never how it battles. Pokemon you pinned in the game keep their pin."));
    bool done = false;
    for (const auto& [id, name] : save.looksStyles) {
      const bool now = id == save.looksStyle;
      const std::string label = now ? Fmt(Tr("%s (now)"), name.c_str()) : name;
      if (ImGui::Button(Lbl(label.c_str(), ("looks_" + id).c_str()).c_str()) || (!pickLooks_.empty() && pickLooks_ == id)) {
        std::string error;
        if (game_.changeLooks(save.setupFile, id, error)) {
          looksMessage_ = Fmt(Tr("Looks changed to %s. They show the next time the game starts."), name.c_str());
          Log("looks: changed to " + id);
        } else {
          looksMessage_ = Fmt(Tr("The looks could not be changed: %s"), error.c_str());
          Log("looks: not changed: " + error);
        }
        pickLooks_.clear();
        RereadSave();
        done = true;
      }
    }
    if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel looks").c_str()) || closeLooks_ || BackPressed() || done) {
      closeLooks_ = false;
      pickLooks_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!save.shareCode.empty()) {
    if (ImGui::Button(Lbl(Tr("Copy code"), "Copy code").c_str())) {
      CopyCode(save.shareCode);
    }
    if (!save.spoilerFile.empty()) {
      ImGui::SameLine();
      if (ImGui::Button(Lbl(Tr("Show spoilers..."), "Show spoilers...").c_str())) {
        askSpoilers_ = true;
      }
    }
    Dim(codeMessage_.empty() ? std::string(Tr("Give the code to a friend: on the New Game setup screen they choose \"Use a "
                                              "friend's code\" and get the same changes."))
                             : codeMessage_);
  }
  if (askSpoilers_) {
    askSpoilers_ = false;
    ImGui::OpenPopup("###spoilers");
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Show spoilers?"), "spoilers").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(Tr("This shows everything the randomizer changed in this playthrough."));
    Dim(Tr("Which Pokemon, trainers and items are where: it spoils the surprises."));
    if (ImGui::Button(Lbl(Tr("Show them"), "Show them").c_str())) {
      ImGui::CloseCurrentPopup();
      Log("spoilers: " + Utf8(save.spoilerFile));
      OpenInSystem(save.spoilerFile);
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Cancel"), "Cancel").c_str()) || closeSpoilers_ || BackPressed()) {
      closeSpoilers_ = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void App::CopyCode(const std::string& code) {
  if (SDL_SetClipboardText(code.c_str())) {
    codeMessage_ = Fmt(Tr("Copied %s: paste it to your friend."), code.c_str());
  } else {
    codeMessage_ = Fmt(Tr("The code could not be copied: %s"), SDL_GetError());
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
    reportMessage_ = Tr("Choose a report first.");
    return;
  }
  fs::path to = PathOf(file);
  if (!to.has_extension()) {
    to += L".zip";
  }
  std::string error;
  reportOk_ = reports::Export(*r, to, exportOptions_, reports::CurrentIdentity(), error);
  reportMessage_ = reportOk_ ? Fmt(Tr("Exported to %s. You can send that file to whoever asked for it."), Utf8(to).c_str()) : error;
}

void App::DeleteReport() {
  const reports::Report* r = SelectedReport();
  if (r == nullptr) {
    return;
  }
  std::string error;
  reportOk_ = reports::Delete(*r, error);
  reportMessage_ = reportOk_ ? Fmt(Tr("Deleted the report of %s."), r->when.c_str()) : error;
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
    sessionMessage_ = Tr("There is no play session to save.");
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
  sessionMessage_ = sessionOk_ ? Fmt(Tr("Saved %s. You can send that file to whoever asked for it."), Utf8(to).c_str()) : error;
}

void App::DrawSessionWindow() {
  const std::string title = Lbl(Tr("Save a play session"), "session");
  if (sessionOpen_ && !ImGui::IsPopupOpen("###session")) {
    LookAtSessions();
    sessionMessage_.clear();
    ImGui::OpenPopup("###session");
  }
  const ImVec2 view = ImGui::GetMainViewport()->WorkSize;
  ImGui::SetNextWindowSize(ImVec2(std::min(view.x - 40 * scale_, 900 * scale_), view.y - 60 * scale_), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  bool open = true;
  if (!ImGui::BeginPopupModal(title.c_str(), &open)) {
    sessionOpen_ = false;
    return;
  }
  Dim(Tr("For when the game stopped, froze or was closed and no report was made. This makes one .zip file of a play "
         "session: what the game program wrote about itself (its log), your button presses, your memory card at the "
         "start and after each save, and your settings and mod list. Nothing is sent: you save the file and give it to "
         "the developer yourself."));
  if (ImGui::Button(Lbl(Tr("Look again"), "Look again").c_str())) {
    LookAtSessions();
  }
  ImGui::Spacing();
  if (sessions_.empty()) {
    ImGui::TextUnformatted(Tr("No play sessions yet."));
    Dim(Tr("A session is kept each time you play the game (the newest few are kept), so the time you play next can be "
           "saved here."));
  } else {
    const float listH = std::min(static_cast<float>(sessions_.size()) + 1.6f, 6.6f) * ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTable("##sessions", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_PadOuterX,
                          ImVec2(0, listH))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn(Lbl(Tr("Started"), "Started").c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(Lbl(Tr("Played"), "Played").c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(Lbl(Tr("Size"), "Size").c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(Lbl(Tr("How it ended"), "How it ended").c_str(), ImGuiTableColumnFlags_WidthStretch);
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
        std::string how = s.recording.empty() ? Tr("the game didn't start playing")
                          : s.ended           ? Tr("closed normally")
                                              : Tr("not closed normally (stopped, crashed or killed; or still running)");
        if (s.shortRun && !s.recording.empty()) {
          how = Fmt(Tr("very short; %s"), how.c_str());
        }
        if (!s.reports.empty()) {
          how = Fmt(s.reports.size() == 1 ? Tr("%s, %d report") : Tr("%s, %d reports"), how.c_str(),
                    static_cast<int>(s.reports.size()));
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
    ImGui::Checkbox(Lbl(Fmt(Tr("Hide my %s"), SysWord(sys::UserNameWords()).c_str()).c_str(),
                        (std::string("Hide my ") + sys::UserNameWords()).c_str())
                        .c_str(),
                    &exportOptions_.hideUserName);
    Dim(Fmt(Tr("Your %s appears in folder names in the log, the recording and the settings (%s). With this on, the "
               "saved copy says %%USERNAME%% instead."),
            SysWord(sys::UserNameWords()).c_str(), sys::HomeFolderExample()));
    if (!sessionBundle_.choice.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
      ImGui::TextWrapped("%s", sessionBundle_.choice.c_str());
      ImGui::PopStyleColor();
    }
    if (!s.reports.empty()) {
      ImGui::TextWrapped("%s", Tr("A report was made during this session: it is included (without a crash dump; to add "
                                  "that, use Export on the report in the list)."));
    }
    if (ImGui::Button(Lbl(Tr("Save..."), "Save...").c_str())) {
      static SDL_DialogFileFilter kZip[1];
      kZip[0] = {Tr("Zip files"), "zip"};
      const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
      const std::string where = Utf8(start / PathOf(reports::SessionExportName(s)));
      sessionMessage_.clear();
      ShowSaveDialog(
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
    if (ImGui::Button(Lbl(Tr("Close"), "Close").c_str())) {
      open = false;
    }
    if (!sessionMessage_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, sessionOk_ ? kGood : kBad);
      ImGui::TextWrapped("%s", sessionMessage_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    Heading(Fmt(Tr("What's inside (%s)"), reports::SizeText(sessionBundle_.size).c_str()).c_str());
    if (ImGui::BeginTable("##sessionfiles", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, 120 * scale_)))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn(Lbl(Tr("File"), "File").c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(Lbl(Tr("Size"), "Size").c_str(), ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn(Lbl(Tr("What it is"), "What it is").c_str(), ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      for (const reports::BundleFile& f : sessionBundle_.files) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(f.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(f.from.empty() ? Tr("small") : reports::SizeText(f.size).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("%s", Tr(f.description).c_str());
        ImGui::PopStyleColor();
      }
      ImGui::EndTable();
    }
  }
  if (sessions_.empty() && ImGui::Button(Lbl(Tr("Close"), "Close").c_str())) {
    open = false;
  }
  if (!open) {
    ImGui::CloseCurrentPopup();
    sessionOpen_ = false;
  }
  if (controller_) {
    DrawFileBrowser();
  }
  ImGui::EndPopup();
}

void App::DrawReportsPage() {
  PageTitle(Tr("Reports"), Tr("Reports help find problems in the game. They stay on this PC, in their folder: nothing is "
                              "ever sent anywhere. To share one, export it and send the file yourself."));
  LookAtReports(false);
  if (ImGui::Button(Lbl(Tr("Open folder"), "Open folder").c_str())) {
    std::error_code ec;
    fs::create_directories(game_.reportsFolder, ec);
    OpenInSystem(game_.reportsFolder);
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Look again"), "Look again").c_str())) {
    LookAtReports(true);
  }
  if (!game_.logsFolder.empty() || !game_.recordingsFolder.empty()) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Save my last play session..."), "Save my last play session...").c_str())) {
      sessionOpen_ = true;
      sessionSel_ = 0;
      sessions_.clear();
    }
    if (Hot()) {
      ImGui::SetTooltip("%s", Tr("The game stopped or froze and made no report? This saves the last time you played as "
                                 "one file to give to the developer."));
    }
    DrawSessionWindow();
  }
  ImGui::Dummy(ImVec2(0, 4 * scale_));

  if (reports_.empty()) {
    BeginCard("##noreports");
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted(Tr("No reports yet"));
    ImGui::PopFont();
    Dim(Tr("If something looks wrong while you play, press F10 in the game window. The game then saves a report: a "
           "picture of the game, your recent button presses and what the game program wrote about itself, so a "
           "developer can see and replay that moment."));
    Dim(Tr("If the game crashes or stops responding, it makes a report by itself. Reports appear here."));
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
    if (Hot() && !r.moment.empty()) {
      ImGui::SetTooltip("%s", r.moment.c_str());
    }
    if (ImGui::IsItemFocused() && !selected && ImGui::GetIO().NavVisible) {
      reportSel_ = r.name;
      reportMessage_.clear();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = pos.x + 6 * scale_;
    dl->AddText(ImVec2(x, pos.y + 2 * scale_), U32(kText), r.when.c_str());
    const char* kind = KindWord(r.kind);
    const float kw = ImGui::CalcTextSize(kind).x;
    const float right = pos.x + ImGui::GetContentRegionAvail().x - 6 * scale_;
    dl->AddText(ImVec2(right - kw, pos.y + 2 * scale_), U32(kindColour(r.kind)), kind);
    const std::string second = (r.moment.empty() ? std::string(Tr("moment unknown")) : r.moment) + "  -  " +
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
    Dim(Tr("Choose a report on the left."));
    ImGui::EndChild();
    return;
  }
  ImGui::PushFont(fonts_.heading);
  ImGui::TextColored(kindColour(r->kind), "%s", KindWord(r->kind));
  ImGui::SameLine();
  ImGui::TextUnformatted(r->when.c_str());
  ImGui::PopFont();
  Dim(KindText(r->kind));
  if (!r->happened.empty()) {
    ImGui::TextWrapped(Tr("What happened: %s"), r->happened.c_str());
  }
  if (!r->moment.empty()) {
    ImGui::TextWrapped(Tr("Where in the game: %s"), r->moment.c_str());
  }
  ImGui::Spacing();
  bool hasShot = false, hasDump = false;
  for (const reports::File& f : r->files) {
    hasShot |= Lower(f.name) == "screenshot.png";
    hasDump |= f.dump;
  }

  Heading(Tr("Export"));
  BeginCard("##export");
  Dim(Tr("Makes one .zip file of this report, with a name and in a place you choose, to send to a developer "
         "yourself. The report here stays as it is."));
  ImGui::Checkbox(Lbl(Fmt(Tr("Hide my %s"), SysWord(sys::UserNameWords()).c_str()).c_str(),
                      (std::string("Hide my ") + sys::UserNameWords()).c_str())
                      .c_str(),
                  &exportOptions_.hideUserName);
  Dim(Fmt(Tr("Your %s appears in folder names in the log, the settings and the report's info (%s). With this on, the "
             "exported copy says %%USERNAME%% instead."),
          SysWord(sys::UserNameWords()).c_str(), sys::HomeFolderExample()));
  if (!hasDump) {
    ImGui::BeginDisabled();
  }
  bool dump = exportOptions_.includeDump && hasDump;
  if (ImGui::Checkbox(Lbl(Tr("Include the crash dump"), "Include the crash dump").c_str(), &dump)) {
    exportOptions_.includeDump = dump;
  }
  if (!hasDump) {
    ImGui::EndDisabled();
  }
  Dim(hasDump ? Tr("A crash dump is a snapshot of the game program's memory when it crashed. It helps a lot in finding "
                   "the cause, but it may contain bits of whatever was in memory (your user name too), so it is left "
                   "out unless you tick this.")
              : Tr("This report has no crash dump (only crashes, stops and hangs make one)."));
  Dim(Tr("Always included: the picture of the game (the screenshot shows only the game window), the recording of "
         "your button presses, the log, your settings and mod list."));
  ImGui::Spacing();
  if (ImGui::Button(Lbl(Tr("Export..."), "Export...").c_str())) {
    static SDL_DialogFileFilter kZip[1];
    kZip[0] = {Tr("Zip files"), "zip"};
    const fs::path start = sys::KnownFolder(sys::Folder::Documents, false);
    const std::string where = Utf8(start / PathOf(reports::ExportName(*r)));
    reportMessage_.clear();
    ShowSaveDialog(
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
  if (ImGui::Button(Lbl(Tr("Delete..."), "Delete...").c_str())) {
    ImGui::OpenPopup("###deletereport");
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Delete this report?"), "deletereport").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text(Tr("Delete the report of %s (%s)?"), r->when.c_str(), KindWord(r->kind));
    Dim(Tr("Its folder and the zip file next to it are deleted for good. Copies you exported stay."));
    if (ImGui::Button(Lbl(Tr("Delete it"), "Delete it").c_str())) {
      ImGui::CloseCurrentPopup();
      DeleteReport();
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Keep it"), "Keep it").c_str()) || BackPressed()) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
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

  Heading(Tr("What's inside"));
  if (ImGui::BeginTable("##files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn(Lbl(Tr("File"), "File").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Lbl(Tr("Size"), "Size").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(Lbl(Tr("What it is"), "What it is").c_str(), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const reports::File& f : r->files) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(f.name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(reports::SizeText(f.size).c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::PushStyleColor(ImGuiCol_Text, kDim);
      ImGui::TextWrapped("%s", Tr(f.description).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  if (!r->info.empty() && ImGui::TreeNode(Lbl(Tr("The report's info.txt"), "The report's info.txt").c_str())) {
    std::string info = r->info;
    ImGui::InputTextMultiline("##info", &info, ImVec2(-1, 220 * scale_), ImGuiInputTextFlags_ReadOnly);
    ImGui::TreePop();
  }

  ImGui::EndChild();
}

void App::DrawBottomBar() {
  const float playHeight = 56 * scale_;
  float playWidth = 220 * scale_;
  ImGui::PushFont(fonts_.play);
  for (const char* t : {Tr("Play"), Tr("Running..."), Tr("Starting..."), Tr("Preparing...")}) {
    playWidth = std::max(playWidth, ImGui::CalcTextSize(t).x + fonts_.play->FontSize * 0.62f + 12 * scale_ + 56 * scale_);
  }
  ImGui::PopFont();
  float label = 62 * scale_;
  ImGui::PushFont(fonts_.caption);
  for (const char* t : {Tr("DISC"), Tr("PRESET"), Tr("MODS"), Tr("LANGUAGE"), Tr("SAVE")}) {
    label = std::max(label, ImGui::CalcTextSize(t).x + 14 * scale_);
  }
  ImGui::PopFont();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale_, 12 * scale_));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * scale_, 5 * scale_));
  ImGui::BeginChild("##bottom", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  const float top = ImGui::GetCursorPosY();
  const auto Label = [&](const char* text) {
    const float x = ImGui::GetCursorPosX();
    ImGui::PushFont(fonts_.caption);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kFaint, "%s", text);
    ImGui::PopFont();
    ImGui::SameLine(x + label);
  };
  const float noteRight = right - playWidth - 28 * scale_ -
                          (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                           ImGui::CalcTextSize(Tr("Keep this window open")).x) -
                          14 * scale_;
  const auto Note = [&](const char* text, float reserve) {
    const float room = std::max(60 * scale_, noteRight - ImGui::GetCursorPosX() - reserve);
    std::string shownText = text;
    if (ImGui::CalcTextSize(text).x > room) {
      while (shownText.size() > 1 && ImGui::CalcTextSize((shownText + "...").c_str()).x > room) {
        shownText.pop_back();
        while (shownText.size() > 1 && (static_cast<unsigned char>(shownText.back()) & 0xC0) == 0x80) {
          shownText.pop_back();
        }
      }
      shownText += "...";
    }
    ImGui::TextColored(kDim, "%s", shownText.c_str());
    if (shownText != text && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", text);
    }
  };
  ImGui::BeginGroup();
  const std::string path = DiscPath();
  {
    std::lock_guard<std::mutex> lock(bg_.mutex);
    const bool have = bg_.haveVerdict && bg_.discPath == path;
    Label(Tr("DISC"));
    if (path.empty()) {
      ImGui::TextColored(kWarn, "%s", Tr("none chosen"));
    } else {
      const std::string name = Utf8(PathOf(path).filename());
      const ImVec4 color = !have ? kBusy
                           : bg_.verdict.kind == DiscVerdict::Kind::Problem ? kBad
                           : bg_.verdict.kind == DiscVerdict::Kind::Warning ? kWarn
                                                                           : kGood;
      const char* state = !have ? Tr("checking...")
                          : bg_.verdict.kind == DiscVerdict::Kind::Problem ? Tr("can't be used")
                          : bg_.verdict.kind == DiscVerdict::Kind::Warning ? Tr("changed or damaged")
                                                                          : Tr("OK");
      ImGui::TextColored(color, "%s (%s%s%s)", name.c_str(), have && !bg_.verdict.edition.empty() ? bg_.verdict.edition.c_str() : "",
                         have && !bg_.verdict.edition.empty() ? ", " : "", state);
    }
  }
  if (!controller_) {
    Label(Tr("PRESET"));
    if (registry_.IsOriginal()) {
      ImGui::TextUnformatted(Tr("Original: the game exactly as on the GameCube"));
    } else {
      const size_t n = registry_.NotOriginal().size();
      ImGui::Text(n == 1 ? Tr("%s: %zu setting different from the GameCube game")
                         : Tr("%s: %zu settings different from the GameCube game"),
                  Tr(ui::PresetName(registry_.CurrentPreset())), n);
    }
  }
  if (reviewDirty_) RebuildReview();
  int on = 0, notUsed = 0;
  for (const auto& row : review_.rows) {
    if (row.on) (row.Usable() ? on : notUsed)++;
  }
  Label(Tr("MODS"));
  if (on == 0 && notUsed == 0) {
    ImGui::TextUnformatted(Tr("none on"));
  } else if (notUsed == 0) {
    ImGui::Text(Tr("%d on (list \"%s\")"), on, mods_.active.c_str());
  } else if (on == 0) {
    ImGui::Text(Tr("none on, %d not used (list \"%s\")"), notUsed, mods_.active.c_str());
  } else {
    ImGui::Text(Tr("%d on, %d not used (list \"%s\")"), on, notUsed, mods_.active.c_str());
  }
  if (LanguageRowOn()) {
    Label(Tr("LANGUAGE"));
    const auto offered = game_.languages.offered();
    const std::string current = registry_.ValueText(registry_.Find(game_.languages.setting));
    static const char* const kLanguageIds[] = {"us", "ge", "fr", "it", "sp", "jp"};
    const auto inPack = [&](const std::string& id) {
      return std::any_of(offered.begin(), offered.end(), [&](const auto& o) { return o.first == id; });
    };
    const std::string ownId = offered.empty() ? std::string("us") : offered.front().first;
    const bool known = std::any_of(std::begin(kLanguageIds), std::end(kLanguageIds), [&](const char* id) { return current == id; });
    std::string shown = known ? std::string(engine::i18n::LanguageName(current)) : current;
    float comboWidth = 170 * scale_;
    for (const char* id : kLanguageIds) {
      comboWidth = std::max(comboWidth, ImGui::CalcTextSize(engine::i18n::LanguageName(id)).x +
                                            ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight() + 8 * scale_);
    }
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("##language", shown.c_str())) {
      for (const char* id : kLanguageIds) {
        if (ImGui::Selectable(Lbl(engine::i18n::LanguageName(id), id).c_str(), current == id)) {
          registry_.SetEnum(game_.languages.setting, id);
          SaveSettingsIfChanged();
          languageChanged_ = true;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    const bool hasText = current == ownId || inPack(current);
    if (languageChanged_) {
      ImGui::TextColored(kDim, "%s", Tr("Use it:"));
      ImGui::SameLine();
      ImGui::BeginDisabled(gameRunning_ || playWhenDrawn_);
      if (ImGui::SmallButton(Lbl(Tr("Start the game now"), "Start the game now").c_str())) {
        languageChanged_ = false;
        Play(false);
      }
      ImGui::EndDisabled();
      if (gameRunning_ && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", Tr("The game is running: close it (or use Restart now in its SETTINGS) and start it again."));
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(Lbl(Tr("Next start"), "Next start").c_str())) languageChanged_ = false;
    } else if (hasText) {
      Note(Tr("Takes effect the next time the game starts."), 0);
    } else {
      const bool noPack = offered.size() <= 1 && !game_.extra.sources.empty();
      Note(Tr("Game text needs the European disc pack; menus added by this port are translated."),
           noPack ? ImGui::CalcTextSize(Tr("Extra assets")).x + ImGui::GetStyle().FramePadding.x * 2 +
                        ImGui::GetStyle().ItemSpacing.x
                  : 0.0f);
      if (noPack) {
        ImGui::SameLine();
        if (ImGui::SmallButton(Lbl(Tr("Extra assets"), "Extra assets").c_str())) page_ = Page::Extra;
      }
    }
  }
  if (SlotsOn()) {
    Label(Tr("SAVE"));
    ImGui::TextUnformatted(slots_.Active().name.c_str());
    for (size_t i = 0; i < slots_.slots.size() && i < slotRows_.size(); i++) {
      if (slots_.slots[i].id == slots_.Active().id) {
        const GameInfo::SaveRow& row = slotRows_[i];
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%s", (row.found ? row.who + (row.time.empty() ? "" : ", " + row.time)
                                                  : row.who.empty() ? std::string(Tr("empty")) : Tr(row.who)).c_str());
      }
    }
  }
  ImGui::EndGroup();

  const float groupBottom = ImGui::GetCursorPosY();

  const float rowsHeight = groupBottom - top;
  const float playTop = top + std::max(0.0f, (rowsHeight - playHeight) * 0.5f);
  bool stay = registry_.Bool(kStaySetting).Get();
  const float checkWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                           ImGui::CalcTextSize(Tr("Keep this window open")).x;
  ImGui::SetCursorPos(ImVec2(right - playWidth - 28 * scale_ - checkWidth,
                             playTop + (playHeight - ImGui::GetFrameHeight()) * 0.5f));
  if (ImGui::Checkbox(Lbl(Tr("Keep this window open"), "Keep this window open").c_str(), &stay)) {
    registry_.SetBool(kStaySetting, stay);
  }
  ImGui::SetCursorPos(ImVec2(right - playWidth, playTop));
  std::string why;
  const bool shadersPreparing = bg_.preparing && bg_.shadersRunning;
  const bool picturesDrawing = bg_.drawing && !bg_.preparing && !bg_.busy && !playWhenDrawn_;
  bool playable = !gameRunning_ && DiscPlayable(why);
  if (PlayButton(gameRunning_ ? Tr("Running...") : playWhenDrawn_ ? Tr("Starting...") : Tr("Play"),
                 ImVec2(playWidth, playHeight), playable && !playWhenDrawn_)) {
    Play(false);
  }
  playWhy_ = !playable && !gameRunning_ ? why : std::string();
  if (!playable && !gameRunning_ && Hot(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", why.c_str());
  }
  ImGui::SetCursorPosY(std::max(groupBottom, playTop + playHeight + 4 * scale_));
  if (!playError_.empty()) {
    ImGui::TextColored(kBad, "%s", playError_.c_str());
  } else if (!saveError_.empty()) {
    ImGui::TextColored(kBad, "%s", saveError_.c_str());
  } else if (shadersPreparing) {
    const uint32_t total = bg_.shadersTotal;
    const int percent = total > 0 ? static_cast<int>(std::min<uint32_t>(bg_.shadersDone, total) * 100ull / total) : 0;
    char line[320];
    std::snprintf(line, sizeof line, Tr("Getting the game ready for your PC (once, about a minute): %d%%"), percent);
    ImGui::TextColored(kBusy, "%s", line);
  } else if (picturesDrawing) {
    ImGui::TextColored(kBusy, "%s", Tr("Drawing pictures from your disc (once, about a minute). Play stops this and "
                                         "finishes it another time."));
  } else if (gameRunning_) {
    ImGui::TextColored(kGood, "%s", Tr("The game is running. Settings changed now are used the next time it starts."));
  } else if (controller_ && !playWhy_.empty()) {
    ImGui::TextColored(kWarn, "%s", playWhy_.c_str());
  }
  ImGui::EndChild();
  ImGui::PopStyleVar(2);

  if (askStartAnyway_) {
    ImGui::OpenPopup("###startanyway");
    askStartAnyway_ = false;
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Start anyway?"), "startanyway").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    std::string text;
    {
      std::lock_guard<std::mutex> lock(bg_.mutex);
      text = bg_.verdict.text;
    }
    ImGui::PushTextWrapPos(560 * scale_);
    ImGui::TextUnformatted(Tr(text).c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(Lbl(Tr("Start anyway"), "Start anyway").c_str())) {
      ImGui::CloseCurrentPopup();
      Play(true);
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Don't start"), "Don't start").c_str()) || BackPressed()) {
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
  promptH_ = controller_ ? 46 * scale_ : 0.0f;
  const bool message = !playError_.empty() || !saveError_.empty() || gameRunning_ || (bg_.preparing && bg_.shadersRunning) || (bg_.drawing && !bg_.preparing && !bg_.busy && !playWhenDrawn_) || (controller_ && !playWhy_.empty());
  const int rows = (SlotsOn() ? 4 : 3) - (controller_ ? 1 : 0) + (LanguageRowOn() ? 1 : 0);
  const float bottom = rows * ImGui::GetFrameHeight() + ((rows - 1) * 5 + 2 * 12 + 4 + 6) * scale_ +
                       ImGui::GetStyle().ItemSpacing.y + (message ? ImGui::GetTextLineHeightWithSpacing() : 0.0f) +
                       std::max(0.0f, promptH_ - ImGui::GetStyle().WindowPadding.y);
  const float height = ImGui::GetContentRegionAvail().y - bottom;
  PadShortcuts();
  if (BackPressed() && !ImGui::IsAnyItemActive() &&
      !ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && !files_.IsOpen()) {
    focusPage_ = true;
  }
  if (controller_ && focusInNav_ && padIn_.Pressed(pad::Btn::Right) && !ImGui::IsAnyItemActive() &&
      !ImGui::IsPopupOpen(0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    enterDelay_ = 3;
  }
  if (enterDelay_ > 0 && --enterDelay_ == 0) {
    enterPage_ = true;
  }
  DrawNav(height);
  ImGui::SameLine(0, 14 * scale_);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26 * scale_, 22 * scale_));
  ImGui::BeginChild("##page", ImVec2(0, height),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, 0);
  ImGui::PopStyleVar();
  if (enterPage_) {
    pad::EnterCurrentWindow();
    enterPage_ = false;
  }
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
  DrawDataWindows();
  DrawWelcomeWindow();
  DrawUpdateWindows();
  modUpd_.Poll();
  DrawModUpdateWindows();
  modTools_.Poll();
  DrawModToolWindows();
  DrawPadUi();
  if (controller_) {
    pad::WarnAboutChildWindows();
  }
  ImGui::End();
  if (ground) {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
  }
}

int App::Run() {
  LoadSettings();
  options_.gameId = game_.gameId;
  options_.hidden = {kDiscSetting, kStaySetting, kIconSetting, kGroundSetting, kUpdateSetting,
                     "gameplay.physical_special_split",
                     "audio.ff_music_fade"};
  options_.hiddenPrefixes.push_back("system.extra_disc.");
  options_.hiddenPrefixes.push_back("playthrough.new_");
  script_ = Split(game_.script, '|');
  ReloadMods();
  InitModUpdater();
  InitModTools();
  modUpd_.OnStart();
  ExtraStart();
  LookAtSlots(true);
  if (SlotsOn()) {
    Log("save: \"" + slots_.Active().name + "\" of " + std::to_string(slots_.slots.size()) + " (" +
        Utf8(sl::RootOf(SavesFolder(), slots_.Active().id)) + ")");
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
  baseScale_ = scale_;
  {
    const char* env = std::getenv("XD_LAUNCHER_PAD");
    const std::string mode = env != nullptr ? Lower(env) : std::string();
    if (mode == "on" || mode == "1") {
      controller_ = controllerFixed_ = true;
    } else if (mode == "off" || mode == "0") {
      controllerFixed_ = true;
    } else {
      const char* deck = std::getenv("SteamDeck");
      const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
      controller_ = (deck != nullptr && std::string(deck) == "1") || std::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr ||
                    (desktop != nullptr && Lower(desktop).find("gamescope") != std::string::npos);
    }
    scale_ = baseScale_ * (controller_ ? kControllerScale : 1.0f);
    focusPage_ = controller_;
  }
  const std::string title = (game_.windowTitle.empty() ? game_.title : game_.windowTitle) +
                            (game_.updates.version.empty() ? std::string() : " " + game_.updates.version) +
                            " - launcher";
  window_ = SDL_CreateWindow(title.c_str(), static_cast<int>(1100 * baseScale_), static_cast<int>(760 * baseScale_),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window_ == nullptr) {
    Log("no window: " + std::string(SDL_GetError()));
    return 1;
  }
  SDL_SetWindowMinimumSize(window_, static_cast<int>(820 * baseScale_), static_cast<int>(560 * baseScale_));
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
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ApplyStyle(scale_);
  fonts_ = LoadFonts(scale_);
  options_.headingFont = fonts_.heading;
  options_.textInput = [](const char* label, std::string* text) { return pad::InputText(label, text); };
  ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
  ImGui_ImplSDLRenderer3_Init(renderer_);
  ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

  if (!DiscPath().empty()) {
    StartWorker(DiscPath());
  } else {
    StartWorker("");
  }
  FindVersions();
  if (layout_.macApp && layout_.valid) {
    std::string why;
    if (eu::MacApplyPending(layout_, why)) {
      Log("updates: an update was waiting: the update helper puts it in place, then opens the app");
      quit_ = true;
    } else if (!why.empty()) {
      Log("updates: " + why);
    }
  }
  Log("version " + (game_.updates.version.empty() ? std::string("?") : game_.updates.version) + " (" +
      (layout_.valid ? "player's folder, " + layout_.running : std::string("not in a player's folder")) + "); updates " +
      (game_.updates.Active() ? "available" : "not set up in this build"));
  LoadLauncherState();
  MaybeAskDataFolder();
  {
    const char* ask = std::getenv("XD_LAUNCHER_ASK_PRESET");
    const bool allowed = ask != nullptr ? std::string(ask) == "1" : script_.empty();
    if (!presetAsked_ && (!settingsFileFound_ || presetPending_) && allowed) {
      if (!settingsFileFound_) {
        registry_.ApplyPreset(es::Preset::Enhanced);
        Log("first start: a new install, Enhanced is in use until a choice is made");
      }
      presetPending_ = true;
      SaveLauncherState();
      askPreset_ = true;
      welcomeFromStart_ = true;
      Log("first start: asking which preset");
    }
  }
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
    const bool busy = bg_.busy || up_.busy || !script_.empty() || ImGui::IsAnyItemActive() || page_ == Page::Controls ||
                      padIn_.AnyDown() || osk_.Open() || !padQueue_.empty();
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
        case SDL_EVENT_MOUSE_MOTION:
          if (!controllerFixed_ && controller_ && std::fabs(e.motion.xrel) + std::fabs(e.motion.yrel) >= 3.0f) {
            SetController(false);
          }
          break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
          if (!controllerFixed_) {
            SetController(false);
          }
          break;
        case SDL_EVENT_KEY_DOWN:
          if (!e.key.repeat && !controllerFixed_ && !controls_.Listening() && !controls_.Testing()) {
            SetController(false);
          }
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
    if (rescale_) {
      ApplyScale();
    }
    PresenceFrame();
    padIn_.Update(PadInfos(), (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0 && !gameRunning_);
    if (padIn_.Activity() && !controllerFixed_) {
      SetController(true);
    }
    if (!padQueue_.empty() && !padIn_.AnyDown() && ++padGap_ >= 3) {
      padIn_.Virtual(padQueue_.front(), 2);
      padQueue_.pop_front();
      padGap_ = 0;
    }
    if (controls_.Listening()) {
      if (listenStart_ == 0) {
        listenStart_ = SDL_GetTicks();
      } else if (controller_ && SDL_GetTicks() - listenStart_ > 8000) {
        controls_.Cancel();
      }
    } else {
      listenStart_ = 0;
    }
    files_.Poll();
    ExtraPoll();
    {
      std::lock_guard<std::mutex> lock(pickMutex_);
      if (pickedReady_) {
        pickedReady_ = false;
        SelectDisc(picked_);
      }
      if (modToolPickedReady_) {
        modToolPickedReady_ = false;
        auto done = std::move(modToolPickDone_);
        modToolPickDone_ = nullptr;
        if (done) done(modToolPicked_);
      }
      if (modPickedReady_) {
        modPickedReady_ = false;
        auto done = std::move(modPickDone_);
        modPickDone_ = nullptr;
        if (done) done(modPicked_);
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
      {
        std::string dataFolder;
        {
          std::lock_guard<std::mutex> lock(pickMutex_);
          std::swap(dataFolder, dataExportPicked_);
        }
        if (!dataFolder.empty()) {
          ExportAllSaves(PathOf(dataFolder));
        }
      }
      if (kind == 4) {
        MonsPick(file);
      } else if (kind == 3) {
        TradePick(file);
      } else if (kind == 1) {
        ImportPick(file);
      } else if (kind == 2) {
        ExportSlot(file);
      }
    }
    if (!importScan_.busy && !importScan_.logged) {
      importScan_.logged = true;
      std::lock_guard<std::mutex> lock(importScan_.mutex);
      Log("import: found " + std::to_string(importScan_.offers.size()) + " Pokemon XD save(s) outside the port");
      for (const GameInfo::ImportOffer& o : importScan_.offers) {
        std::string time;
        for (const auto& [label, value] : o.check.preview.facts) {
          if (label == "Play time") time = value;
        }
        Log("import:   " + o.check.name + ", " + time + ", " + o.where);
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
    FollowLanguage();

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    osk_.Update(padIn_, io, controller_ && io.WantTextInput && !controls_.Listening(), SDL_GetTicks());
    padIn_.Feed(io, osk_.Open() || controls_.Listening(), ImGui::IsAnyItemActive() && !io.WantTextInput);
    if (controller_ && (padIn_.Activity() || padIn_.AnyDown())) {
      ImGui::SetNavCursorVisible(true);
    }
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
  if (presence_) {
    presence_->Stop();
    presence_.reset();
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
