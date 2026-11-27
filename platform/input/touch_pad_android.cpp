#ifdef __ANDROID__

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_touch.h>

#include <dolphin/gx/GXAurora.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <aurora/aurora.h>
#include <controls.h>
#include <i18n.h>
#include <port_menu.h>
#include <port_speedups.h>
#include <settings.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" uint32_t PortVI_GetFlipCount(void);
extern "C" bool PortXD_OpenCheatMenu(void);
extern "C" int PortXD_CheatsEnabled(void);
extern "C" bool PortXD_OpenSettingsPageForTouch(void);

namespace {

namespace es = engine::settings;
namespace ec = engine::controls;
using engine::i18n::Tr;

enum Ctl { kStick, kDpad, kA, kB, kX, kY, kZ, kL, kR, kStart, kC, kPause, kFf, kMore, kCount };
const char* const kName[kCount] = {"stick", "dpad", "a", "b", "x", "y", "z", "l", "r", "start", "c", "pause", "ff", "more"};

struct Def {
  bool present;
  int anchor;
  float x, y, hw, hh;
};
enum Layout { kClassic, kSideBars, kFew, kLayouts };
const char* const kLayoutId[kLayouts] = {"classic", "sidebars", "few"};

const Def kTable[kLayouts][kCount] = {
    {{true, 0, 0.330f, 0.540f, 0.145f, 0.145f},
     {true, 0, 0.195f, 0.837f, 0.100f, 0.100f},
     {true, 1, 0.310f, 0.590f, 0.100f, 0.100f},
     {true, 1, 0.455f, 0.720f, 0.060f, 0.060f},
     {true, 1, 0.153f, 0.590f, 0.035f, 0.110f},
     {true, 1, 0.330f, 0.460f, 0.110f, 0.040f},
     {true, 1, 0.190f, 0.180f, 0.080f, 0.030f},
     {true, 0, 0.150f, 0.080f, 0.115f, 0.050f},
     {true, 1, 0.150f, 0.080f, 0.115f, 0.050f},
     {true, 2, 0.000f, 0.940f, 0.095f, 0.028f},
     {true, 1, 0.157f, 0.880f, 0.050f, 0.050f},
     {true, 2, -0.050f, 0.055f, 0.040f, 0.040f},
     {true, 2, 0.047f, 0.055f, 0.040f, 0.040f},
     {false, 0, 0, 0, 0, 0}},
    {{true, 0, 0.220f, 0.520f, 0.145f, 0.145f},
     {true, 0, 0.220f, 0.830f, 0.100f, 0.100f},
     {true, 1, 0.194f, 0.520f, 0.090f, 0.090f},
     {true, 1, 0.340f, 0.640f, 0.057f, 0.057f},
     {true, 1, 0.048f, 0.510f, 0.033f, 0.100f},
     {true, 1, 0.250f, 0.380f, 0.100f, 0.035f},
     {true, 1, 0.190f, 0.170f, 0.080f, 0.030f},
     {true, 0, 0.135f, 0.074f, 0.110f, 0.048f},
     {true, 1, 0.135f, 0.074f, 0.110f, 0.048f},
     {true, 1, 0.317f, 0.936f, 0.085f, 0.028f},
     {true, 1, 0.090f, 0.860f, 0.050f, 0.050f},
     {true, 0, 0.370f, 0.068f, 0.040f, 0.040f},
     {true, 0, 0.370f, 0.163f, 0.040f, 0.040f},
     {false, 0, 0, 0, 0, 0}},
    {{true, 0, 0.230f, 0.750f, 0.130f, 0.130f},
     {true, 0, 0.560f, 0.850f, 0.090f, 0.090f},
     {true, 1, 0.317f, 0.670f, 0.105f, 0.105f},
     {true, 1, 0.480f, 0.810f, 0.065f, 0.065f},
     {true, 1, 0.500f, 0.640f, 0.035f, 0.075f},
     {true, 1, 0.320f, 0.505f, 0.110f, 0.040f},
     {true, 1, 0.190f, 0.180f, 0.080f, 0.030f},
     {true, 0, 0.150f, 0.080f, 0.115f, 0.050f},
     {true, 1, 0.150f, 0.080f, 0.115f, 0.050f},
     {true, 1, 0.130f, 0.945f, 0.085f, 0.026f},
     {true, 1, 0.100f, 0.700f, 0.050f, 0.050f},
     {true, 2, -0.050f, 0.055f, 0.040f, 0.040f},
     {true, 2, 0.047f, 0.055f, 0.040f, 0.040f},
     {true, 1, 0.100f, 0.815f, 0.040f, 0.040f}},
};
bool IsExtra(int c) { return c == kL || c == kR || c == kZ || c == kX || c == kDpad || c == kC; }

struct Move {
  float dx = 0, dy = 0, scale = 1;
};
Move sMoves[kLayouts][kCount];

struct Rect {
  float cx, cy, hw, hh;
};

constexpr int kFingers = 10;
struct Finger {
  bool used = false;
  SDL_FingerID id = 0;
  float x = 0, y = 0;
  float sx = 0, sy = 0;
  int role = 0;
  int ctl = -1;
  Uint64 t0 = 0;
  float startScale = 1;
};
Finger sFinger[kFingers];

uint8_t sDown[kCount] = {};
uint32_t sHold[kCount] = {};
uint32_t sPump = 0;
float sStickX = 0, sStickY = 0, sStickCx = 0, sStickCy = 0;
bool sStickLive = false;
float sCX = 0, sCY = 0, sCCx = 0, sCCy = 0;
uint16_t sDpad = 0;

bool sShown = true;
bool sShownInit = false;
bool sInside = false;
uint64_t sMoreUntil = 0;
uint32_t (*sContext)(void) = nullptr;
void (*sSave)(void) = nullptr;
bool sBack = false;
int sMine = 0;
int sEditSel = -1;

constexpr uint32_t kSyntheticKeyboard = 0xD0C0FFEEu;
constexpr SDL_TouchID kTestTouch = 0x7E57;

enum Action { kNone, kOpenPause, kOpenSettings, kOpenButtons, kOpenEdit, kOpenCheats, kOpenGameSettings, kPhoto, kBugReport, kQuit };
Action sAction = kNone;
Action sAfterPage = kNone;

struct Tap {
  float x, y;
};
std::vector<Tap> sTaps;
struct RowRect {
  float x0, y0, x1, y1;
  int id;
};
std::vector<RowRect> sRows, sRowsNow;
bool sQuitAsk = false;
uint64_t sToastUntil = 0;
std::string sToast;

struct Cfg {
  bool ready = false;
  int layout = kSideBars;
  int show = 0;
  float size = 1, see = 0.3f;
  bool floating = false, ffButton = true;
  uint32_t changes = ~0u;
};
Cfg sCfg;

void ParseMoves(int layout, const std::string& text) {
  for (Move& m : sMoves[layout]) m = Move();
  size_t i = 0;
  while (i < text.size()) {
    size_t e = text.find(';', i);
    if (e == std::string::npos) e = text.size();
    const std::string item = text.substr(i, e - i);
    i = e + 1;
    const size_t eq = item.find('=');
    if (eq == std::string::npos) continue;
    for (int c = 0; c < kCount; c++) {
      if (item.compare(0, eq, kName[c]) == 0 && std::strlen(kName[c]) == eq) {
        float a = 0, b = 0, s = 1;
        if (std::sscanf(item.c_str() + eq + 1, "%f,%f,%f", &a, &b, &s) == 3) {
          sMoves[layout][c] = {std::clamp(a, -2.f, 2.f), std::clamp(b, -1.f, 1.f), std::clamp(s, 0.4f, 2.5f)};
        }
      }
    }
  }
}

std::string MovesText(int layout) {
  std::string out;
  for (int c = 0; c < kCount; c++) {
    const Move& m = sMoves[layout][c];
    if (m.dx == 0 && m.dy == 0 && m.scale == 1) continue;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s%s=%.4f,%.4f,%.3f", out.empty() ? "" : ";", kName[c], m.dx, m.dy, m.scale);
    out += buf;
  }
  return out;
}

void Refresh() {
  es::Registry& r = es::Global();
  if (sCfg.changes == r.Changes() && sCfg.ready) return;
  sCfg.ready = true;
  sCfg.changes = r.Changes();
  const std::string lay = r.ValueText(r.Find("touch.layout"));
  sCfg.layout = lay == "classic" ? kClassic : lay == "few" ? kFew : kSideBars;
  const std::string show = r.ValueText(r.Find("touch.show"));
  sCfg.show = show == "always" ? 1 : show == "never" ? 2 : 0;
  sCfg.size = static_cast<float>(r.Int("touch.size").Get()) / 100.f;
  sCfg.see = static_cast<float>(r.Int("touch.opacity").Get()) / 100.f;
  sCfg.floating = r.ValueText(r.Find("touch.stick")) == "floating";
  sCfg.ffButton = r.Bool("touch.ff_button").Get();
  for (int l = 0; l < kLayouts; l++) ParseMoves(l, r.Text(std::string("touch.positions_") + kLayoutId[l]));
}

void SetEnum(const char* id, const char* choice) {
  es::Global().SetEnum(id, choice);
  if (sSave != nullptr) sSave();
}
void SetInt(const char* id, int v) {
  es::Global().SetInt(id, v);
  if (sSave != nullptr) sSave();
}
void SaveMoves() {
  es::Registry& r = es::Global();
  r.SetText(std::string("touch.positions_") + kLayoutId[sCfg.layout], MovesText(sCfg.layout));
  if (sSave != nullptr) sSave();
}

ImVec2 Screen() {
  if (ImGui::GetCurrentContext() == nullptr) return ImVec2(1920, 1080);
  return ImGui::GetIO().DisplaySize;
}

bool WideRender() {
  uint32_t w = 0, h = 0;
  AuroraGetRenderSize(&w, &h);
  return w > 0 && h > 0 && static_cast<float>(w) / static_cast<float>(h) > 1.4f;
}

int TableOf() { return sCfg.layout == kSideBars && WideRender() ? kClassic : sCfg.layout; }

bool Wanted(int c) {
  const Def& d = kTable[TableOf()][c];
  if (!d.present) return false;
  if (c == kFf) {
    return sCfg.ffButton && es::Global().Bool("gameplay.fast_forward").Get();
  }
  if (sCfg.layout == kFew && IsExtra(c) && c != kDpad && c != kC) {
    if (sMoreUntil > SDL_GetTicks()) return true;
    return sContext != nullptr && (sContext() & (1u << c)) != 0;
  }
  if (sCfg.layout == kFew && (c == kDpad || c == kC)) {
    return sMoreUntil > SDL_GetTicks() || (sContext != nullptr && (sContext() & (1u << c)) != 0);
  }
  if (c == kMore) return sCfg.layout == kFew;
  return true;
}

Rect Place(int c) {
  const ImVec2 sc = Screen();
  const float W = sc.x, H = sc.y;
  const Def& d = kTable[TableOf()][c];
  const Move& m = sMoves[sCfg.layout][c];
  float bar = 1.f;
  if (TableOf() == kSideBars) bar = std::clamp((W - H * 4.f / 3.f) * 0.5f / (0.444f * H), 0.5f, 1.f);
  const float s = sCfg.size * bar * m.scale;
  float x = d.anchor == 0 ? d.x * H * bar : d.anchor == 1 ? W - d.x * H * bar : W * 0.5f + d.x * H;
  x += m.dx * H;
  const float y = d.y * H + m.dy * H * 1.f;
  return {x, y, d.hw * H * s, d.hh * H * s};
}

bool Inside(const Rect& r, float x, float y, float grow) {
  const float dx = (x - r.cx) / (r.hw * grow), dy = (y - r.cy) / (r.hh * grow);
  return dx * dx + dy * dy <= 1.f;
}
bool InsideBox(const Rect& r, float x, float y, float grow) {
  return std::fabs(x - r.cx) <= r.hw * grow && std::fabs(y - r.cy) <= r.hh * grow;
}

bool HitControl(int c, float x, float y) {
  if (!Wanted(c)) return false;
  const Rect r = Place(c);
  switch (c) {
  case kStick: {
    if (sCfg.floating) {
      const float zone = std::max(r.hw * 1.25f, Screen().y * 0.30f);
      return std::fabs(x - r.cx) <= zone * 1.25f && std::fabs(y - r.cy) <= zone;
    }
    return Inside(r, x, y, 1.3f);
  }
  case kDpad: return InsideBox(r, x, y, 1.15f);
  case kL: case kR: case kZ: case kStart: return InsideBox(r, x, y, 1.12f);
  case kA: case kB: case kC: case kPause: case kFf: case kMore: return Inside(r, x, y, 1.22f);
  default: return Inside(r, x, y, 1.12f);
  }
}

constexpr int kHitOrder[] = {kPause, kFf, kMore, kA, kB, kX, kY, kZ, kL, kR, kStart, kC, kDpad, kStick};

int ControlAt(float x, float y) {
  for (int c : kHitOrder) {
    if (HitControl(c, x, y)) return c;
  }
  return -1;
}

float Alpha() { return 1.f - sCfg.see; }
ImU32 Col(int r, int g, int b, float a = 1.f) {
  return IM_COL32(r, g, b, static_cast<int>(std::clamp(a * Alpha(), 0.f, 1.f) * 255.f));
}
ImU32 Lighter(int r, int g, int b, float a, float k) {
  return Col(std::min(255, static_cast<int>(r * k + 40 * (k - 1.f) * 2)), std::min(255, static_cast<int>(g * k + 40 * (k - 1.f) * 2)),
             std::min(255, static_cast<int>(b * k + 40 * (k - 1.f) * 2)), a);
}

void Letter(ImDrawList* dl, ImVec2 c, float size, const char* text, ImU32 col) {
  const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(size, 1000.f, 0.f, text);
  dl->AddText(ImGui::GetFont(), size, ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), col, text);
}

void Glyph(ImDrawList* dl, ImVec2 c, float size, char ch, ImU32 col) {
  const float s = size * 0.5f;
  const float t = std::max(2.f, size * 0.13f);
  auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
  auto line = [&](std::initializer_list<ImVec2> pts) {
    for (const ImVec2& p : pts) dl->PathLineTo(p);
    dl->PathStroke(col, 0, t);
  };
  switch (ch) {
  case 'A': line({P(-0.55f, 0.55f), P(0, -0.6f), P(0.55f, 0.55f)}); line({P(-0.3f, 0.18f), P(0.3f, 0.18f)}); break;
  case 'B':
    line({P(-0.35f, -0.55f), P(-0.35f, 0.55f)});
    line({P(-0.35f, -0.55f), P(0.12f, -0.55f), P(0.4f, -0.38f), P(0.4f, -0.17f), P(0.12f, 0.0f), P(-0.35f, 0.0f)});
    line({P(0.12f, 0.0f), P(0.2f, 0.0f), P(0.48f, 0.17f), P(0.48f, 0.38f), P(0.2f, 0.55f), P(-0.35f, 0.55f)});
    break;
  case 'X': line({P(-0.45f, -0.55f), P(0.45f, 0.55f)}); line({P(0.45f, -0.55f), P(-0.45f, 0.55f)}); break;
  case 'Y': line({P(-0.48f, -0.55f), P(0, 0.0f), P(0.48f, -0.55f)}); line({P(0, 0.0f), P(0, 0.55f)}); break;
  case 'Z': line({P(-0.42f, -0.55f), P(0.42f, -0.55f), P(-0.42f, 0.55f), P(0.42f, 0.55f)}); break;
  case 'L': line({P(-0.32f, -0.55f), P(-0.32f, 0.55f), P(0.38f, 0.55f)}); break;
  case 'R':
    line({P(-0.35f, 0.55f), P(-0.35f, -0.55f), P(0.12f, -0.55f), P(0.4f, -0.38f), P(0.4f, -0.17f), P(0.12f, 0.0f), P(-0.35f, 0.0f)});
    line({P(0.05f, 0.0f), P(0.42f, 0.55f)});
    break;
  case 'C':
    dl->PathArcTo(c, s * 0.55f, 0.6f, 5.68f, 16);
    dl->PathStroke(col, 0, t);
    break;
  default: break;
  }
}
void Glyph(ImDrawList* dl, ImVec2 c, float size, const char* text, ImU32 col) {
  if (text[1] == 0) Glyph(dl, c, size, text[0], col);
  else Letter(dl, c, size, text, col);
}

void Ball(ImDrawList* dl, ImVec2 c, float r, int cr, int cg, int cb, bool down, const char* letter, float letterK = 1.15f) {
  const float k = down ? 1.35f : 1.f;
  dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.06f), r * 1.04f, Col(0, 0, 0, 0.45f), 40);
  dl->AddCircleFilled(c, r, Lighter(cr, cg, cb, 1.f, k), 40);
  dl->AddCircle(c, r, Col(cr / 2, cg / 2, cb / 2, 1.f), 40, std::max(2.f, r * 0.07f));
  dl->AddCircleFilled(ImVec2(c.x - r * 0.1f, c.y - r * 0.4f), r * 0.5f, Col(255, 255, 255, down ? 0.1f : 0.22f), 24);
  if (letter != nullptr) Glyph(dl, c, r * letterK, letter, Col(255, 255, 255, 0.97f));
}

void Arc(ImDrawList* dl, ImVec2 c, float rad, float a0, float a1, float thick, ImU32 col) {
  dl->PathClear();
  dl->PathArcTo(c, rad, a0, a1, 24);
  dl->PathStroke(col, 0, thick);
}

void Pill(ImDrawList* dl, const Rect& r, ImU32 fill, ImU32 edge, float rounding) {
  dl->AddRectFilled(ImVec2(r.cx - r.hw, r.cy - r.hh), ImVec2(r.cx + r.hw, r.cy + r.hh), fill, rounding);
  dl->AddRect(ImVec2(r.cx - r.hw, r.cy - r.hh), ImVec2(r.cx + r.hw, r.cy + r.hh), edge, rounding, 0, std::max(1.5f, r.hh * 0.08f));
}

void Octagon(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float thick) {
  for (int i = 0; i < 8; i++) {
    const float a = (static_cast<float>(i) + 0.5f) * 3.14159265f / 4.f;
    dl->PathLineTo(ImVec2(c.x + std::cos(a) * r * 1.082f, c.y + std::sin(a) * r * 1.082f));
  }
  dl->PathStroke(col, ImDrawFlags_Closed, thick);
}

bool DownNow(int c) { return sDown[c] != 0 || sHold[c] > sPump; }

void DrawControl(ImDrawList* dl, int c, bool edit) {
  if (!Wanted(c) && !edit) return;
  if (!Wanted(c) && edit && !kTable[TableOf()][c].present) return;
  const Rect r = Place(c);
  const ImVec2 ctr(r.cx, r.cy);
  const bool down = DownNow(c);
  switch (c) {
  case kStick: {
    const float gate = r.hw;
    float cx = r.cx, cy = r.cy;
    if (sStickLive) { cx = sStickCx; cy = sStickCy; }
    dl->AddCircleFilled(ImVec2(cx, cy), gate * 1.0f, Col(0, 0, 0, 0.22f), 8);
    Octagon(dl, ImVec2(cx, cy), gate, Col(200, 200, 205, 0.9f), std::max(2.f, gate * 0.025f));
    const float kr = gate * 0.56f;
    const ImVec2 k(cx + sStickX * gate * 0.8f, cy - sStickY * gate * 0.8f);
    dl->AddCircleFilled(ImVec2(k.x, k.y + kr * 0.05f), kr * 1.05f, Col(0, 0, 0, 0.45f), 32);
    dl->AddCircleFilled(k, kr, Col(150, 150, 156, 1.f), 32);
    dl->AddCircle(k, kr, Col(90, 90, 95, 1.f), 32, std::max(2.f, kr * 0.06f));
    dl->AddCircle(k, kr * 0.62f, Col(200, 200, 205, 0.55f), 32, std::max(1.5f, kr * 0.05f));
    dl->AddCircle(k, kr * 0.25f, Col(200, 200, 205, 0.5f), 24, std::max(1.5f, kr * 0.05f));
    break;
  }
  case kDpad: {
    const float a = r.hw, w = a * 0.36f;
    const ImU32 fill = Col(120, 120, 126, 1.f), edge = Col(60, 60, 64, 1.f);
    dl->AddRectFilled(ImVec2(ctr.x - w, ctr.y - a), ImVec2(ctr.x + w, ctr.y + a), fill, w * 0.35f);
    dl->AddRectFilled(ImVec2(ctr.x - a, ctr.y - w), ImVec2(ctr.x + a, ctr.y + w), fill, w * 0.35f);
    dl->AddRect(ImVec2(ctr.x - w, ctr.y - a), ImVec2(ctr.x + w, ctr.y + a), edge, w * 0.35f, 0, std::max(2.f, a * 0.04f));
    dl->AddRect(ImVec2(ctr.x - a, ctr.y - w), ImVec2(ctr.x + a, ctr.y + w), edge, w * 0.35f, 0, std::max(2.f, a * 0.04f));
    dl->AddRectFilled(ImVec2(ctr.x - w * 0.9f, ctr.y - w * 0.9f), ImVec2(ctr.x + w * 0.9f, ctr.y + w * 0.9f), fill);
    const struct { uint16_t bit; float dx, dy; } arms[4] = {{PAD_BUTTON_UP, 0, -1}, {PAD_BUTTON_DOWN, 0, 1}, {PAD_BUTTON_LEFT, -1, 0}, {PAD_BUTTON_RIGHT, 1, 0}};
    for (const auto& arm : arms) {
      const ImVec2 p(ctr.x + arm.dx * a * 0.68f, ctr.y + arm.dy * a * 0.68f);
      const float t = a * 0.2f;
      const bool on = (sDpad & arm.bit) != 0;
      const ImU32 tc = on ? Col(255, 255, 255, 1.f) : Col(60, 60, 64, 1.f);
      if (arm.dx == 0) dl->AddTriangleFilled(ImVec2(p.x, p.y + arm.dy * t), ImVec2(p.x - t, p.y - arm.dy * t), ImVec2(p.x + t, p.y - arm.dy * t), tc);
      else dl->AddTriangleFilled(ImVec2(p.x + arm.dx * t, p.y), ImVec2(p.x - arm.dx * t, p.y - t), ImVec2(p.x - arm.dx * t, p.y + t), tc);
    }
    dl->AddCircleFilled(ctr, a * 0.2f, Col(90, 90, 95, 1.f), 16);
    break;
  }
  case kA: Ball(dl, ctr, r.hw, 40, 168, 140, down, "A", 1.2f); break;
  case kB: Ball(dl, ctr, r.hw, 200, 32, 36, down, "B"); break;
  case kC: {
    const float gate = r.hw;
    Ball(dl, sCX != 0 || sCY != 0 ? ImVec2(ctr.x + sCX * gate * 0.5f, ctr.y - sCY * gate * 0.5f) : ctr, gate, 224, 190, 32, down || sCX != 0 || sCY != 0, "C", 1.0f);
    break;
  }
  case kX: case kY: {
    const float len = c == kX ? r.hh : r.hw;
    const float thick = (c == kX ? r.hw : r.hh) * 1.25f;
    const ImU32 fill = down ? Col(190, 190, 196, 1.f) : Col(130, 130, 136, 1.f);
    const float rad = len * 1.5f;
    if (c == kX) {
      const ImVec2 mid(ctr.x - rad + r.hw, ctr.y);
      dl->AddRect(ImVec2(0, 0), ImVec2(0, 0), 0);
      Arc(dl, mid, rad, -0.72f, 0.72f, thick, Col(0, 0, 0, 0.45f));
      Arc(dl, mid, rad, -0.72f, 0.72f, thick * 0.86f, fill);
      Glyph(dl, ImVec2(ctr.x + r.hw * 0.3f, ctr.y), thick * 0.9f, "X", Col(255, 255, 255, 0.97f));
    } else {
      const ImVec2 mid(ctr.x, ctr.y + rad - r.hh);
      Arc(dl, mid, rad, -2.42f, -0.72f, thick, Col(0, 0, 0, 0.45f));
      Arc(dl, mid, rad, -2.42f, -0.72f, thick * 0.86f, fill);
      Glyph(dl, ImVec2(ctr.x, ctr.y - r.hh * 0.1f), thick * 0.9f, "Y", Col(255, 255, 255, 0.97f));
    }
    break;
  }
  case kZ: {
    Pill(dl, r, down ? Col(160, 140, 235, 1.f) : Col(118, 98, 200, 1.f), Col(70, 55, 130, 1.f), r.hh);
    Glyph(dl, ctr, r.hh * 1.5f, "Z", Col(255, 255, 255, 0.97f));
    break;
  }
  case kL: case kR: {
    const ImU32 fill = down ? Col(190, 190, 196, 1.f) : Col(128, 128, 134, 1.f);
    Pill(dl, r, fill, Col(70, 70, 75, 1.f), r.hh * 0.6f);
    dl->AddRectFilled(ImVec2(r.cx - r.hw * 0.94f, r.cy + r.hh * 0.05f), ImVec2(r.cx + r.hw * 0.94f, r.cy + r.hh * 0.85f), Col(0, 0, 0, 0.18f), r.hh * 0.4f);
    Glyph(dl, ImVec2(ctr.x, ctr.y - r.hh * 0.1f), r.hh * 1.3f, c == kL ? "L" : "R", Col(235, 235, 240, 0.97f));
    break;
  }
  case kStart: {
    Pill(dl, r, down ? Col(120, 120, 128, 1.f) : Col(64, 64, 70, 1.f), Col(150, 150, 156, 1.f), r.hh);
    dl->AddCircleFilled(ImVec2(r.cx - r.hw * 0.72f, r.cy), r.hh * 0.42f, Col(200, 200, 205, 1.f), 16);
    Letter(dl, ImVec2(r.cx + r.hw * 0.12f, r.cy), r.hh * 1.25f, "START", Col(235, 235, 240, 0.95f));
    break;
  }
  case kPause: case kFf: case kMore: {
    const bool lit = c == kFf && (PortSpeedups_FastForwarding() || down);
    dl->AddCircleFilled(ctr, r.hw, lit ? Col(90, 90, 100, 1.f) : Col(24, 24, 30, 0.8f), 32);
    dl->AddCircle(ctr, r.hw, Col(235, 235, 240, 1.f), 32, std::max(2.f, r.hw * 0.07f));
    const ImU32 w = Col(235, 235, 240, 1.f);
    if (c == kPause) {
      dl->AddRectFilled(ImVec2(ctr.x - r.hw * 0.33f, ctr.y - r.hw * 0.38f), ImVec2(ctr.x - r.hw * 0.1f, ctr.y + r.hw * 0.38f), w);
      dl->AddRectFilled(ImVec2(ctr.x + r.hw * 0.1f, ctr.y - r.hw * 0.38f), ImVec2(ctr.x + r.hw * 0.33f, ctr.y + r.hw * 0.38f), w);
    } else if (c == kFf) {
      const float t = r.hw * 0.36f;
      dl->AddTriangleFilled(ImVec2(ctr.x - r.hw * 0.5f, ctr.y - t), ImVec2(ctr.x - r.hw * 0.5f, ctr.y + t), ImVec2(ctr.x, ctr.y), w);
      dl->AddTriangleFilled(ImVec2(ctr.x, ctr.y - t), ImVec2(ctr.x, ctr.y + t), ImVec2(ctr.x + r.hw * 0.5f, ctr.y), w);
    } else {
      for (int i = -1; i <= 1; i++) dl->AddCircleFilled(ImVec2(ctr.x + i * r.hw * 0.4f, ctr.y), r.hw * 0.1f, w, 12);
    }
    break;
  }
  default: break;
  }
}

bool TakeTap(float* x, float* y) {
  if (sTaps.empty()) return false;
  *x = sTaps.front().x;
  *y = sTaps.front().y;
  sTaps.erase(sTaps.begin());
  return true;
}

int RowAt(float x, float y) {
  for (const RowRect& r : sRows) {
    if (x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return r.id;
  }
  return -1;
}

void DrawRow(ImDrawList* dl, float x0, float y0, float x1, float y1, const std::string& label, const std::string& value, bool enabled,
             bool hot, int id) {
  const float h = y1 - y0;
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), hot ? IM_COL32(150, 28, 36, 245) : IM_COL32(10, 40, 40, enabled ? 235 : 150), h * 0.5f);
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(200, 200, 205, enabled ? 235 : 110), h * 0.5f, 0, std::max(2.f, h * 0.06f));
  const ImU32 tc = enabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(170, 170, 175, 255);
  const float fs = h * 0.5f;
  if (value.empty()) {
    Letter(dl, ImVec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), fs, label.c_str(), tc);
  } else {
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x0 + h * 0.6f, (y0 + y1) * 0.5f - fs * 0.5f), tc, label.c_str());
    const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(fs, 1000.f, 0.f, value.c_str());
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x1 - h * 0.6f - t.x, (y0 + y1) * 0.5f - fs * 0.5f), tc, value.c_str());
  }
  if (enabled) sRowsNow.push_back({x0, y0, x1, y1, id});
}

void DrawOverlayOnly(ImDrawList* dl, bool edit);

void PauseTitle(ImDrawList* dl, const char* text, float x, float y, float h) {
  dl->AddRectFilled(ImVec2(x, y), ImVec2(x + h * 5.2f, y + h), IM_COL32(8, 8, 8, 235), h * 0.5f);
  dl->AddRect(ImVec2(x, y), ImVec2(x + h * 5.2f, y + h), IM_COL32(200, 200, 205, 235), h * 0.5f, 0, std::max(2.f, h * 0.06f));
  Letter(dl, ImVec2(x + h * 2.6f, y + h * 0.5f), h * 0.5f, text, IM_COL32(255, 255, 255, 255));
}

bool PausePage(void*, const PortMenuInput* in) {
  sMine = 1;
  sRowsNow.clear();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 sc = Screen();
  const float H = sc.y;
  dl->AddRectFilled(ImVec2(0, 0), sc, IM_COL32(0, 0, 0, 90));
  const float rowH = H * 0.095f, gap = H * 0.012f;
  const float x1 = sc.x * 0.5f + H * 0.55f + (WideRender() ? H * 0.0f : 0.f);
  const float rowW = std::min(sc.x * 0.4f, H * 0.78f);
  const float x0 = std::min(x1, sc.x - H * 0.06f) - rowW;
  PauseTitle(dl, Tr("PAUSED"), sc.x * 0.5f - H * 0.9f, H * 0.04f, H * 0.075f);
  struct Item { int id; std::string text; bool on; };
  const es::Registry& r = es::Global();
  std::vector<Item> items = {{0, Tr("RESUME"), true}, {1, Tr("SETTINGS"), true}, {2, Tr("BUTTONS"), true},
                             {3, Tr("PHOTO MODE"), r.Bool("video.photo_mode").Get()}};
  if (PortXD_CheatsEnabled() != 0) items.push_back({4, Tr("CHEATS"), true});
  items.push_back({5, Tr("BUG REPORT"), true});
  items.push_back({6, std::string(sQuitAsk ? Tr("QUIT? TAP AGAIN") : Tr("QUIT")), true});
  float y = H * 0.17f;
  float tx, ty;
  const int sel = TakeTap(&tx, &ty) ? RowAt(tx, ty) : -1;
  for (const Item& it : items) {
    DrawRow(dl, x0, y, x0 + rowW, y + rowH, it.text, "", it.on, it.id == 0, it.id);
    y += rowH + gap;
  }
  const std::string noteText = Tr("The game is paused. Nothing moves until you go back to it.");
  dl->AddRectFilled(ImVec2(sc.x * 0.5f - H * 0.9f, H * 0.82f), ImVec2(sc.x * 0.5f + H * 0.55f, H * 0.95f), IM_COL32(10, 10, 12, 235), H * 0.012f);
  dl->AddText(ImGui::GetFont(), H * 0.036f, ImVec2(sc.x * 0.5f - H * 0.87f, H * 0.845f), IM_COL32(255, 255, 255, 255), noteText.c_str(), nullptr, H * 1.4f);
  sRows = sRowsNow;
  bool close = false;
  if (sBack || (in != nullptr && (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_START)))) {
    sBack = false;
    close = true;
  }
  if (sel == 0) close = true;
  else if (sel == 1) { sAfterPage = kOpenGameSettings; close = true; }
  else if (sel == 2) { sAfterPage = kOpenButtons; close = true; }
  else if (sel == 3) { sAfterPage = kPhoto; close = true; }
  else if (sel == 4) { sAfterPage = kOpenCheats; close = true; }
  else if (sel == 5) { sAfterPage = kBugReport; close = true; }
  else if (sel == 6) {
    if (sQuitAsk) { sAfterPage = kQuit; close = true; }
    else sQuitAsk = true;
  } else if (sel < 0 && !sRows.empty() && tx > 0 && (tx < x0 - H * 0.05f || tx > x0 + rowW + H * 0.05f) && ty > H * 0.12f && ty < H * 0.8f) {
    close = true;
  }
  if (close) sQuitAsk = sel == 6 ? sQuitAsk : false;
  return !close;
}

const char* Choice(int v, const char* a, const char* b, const char* c = nullptr) { return v == 0 ? a : v == 1 ? b : c; }

bool SettingsPage(void*, const PortMenuInput* in) {
  sMine = 2;
  sRowsNow.clear();
  Refresh();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 sc = Screen();
  const float H = sc.y;
  DrawOverlayOnly(dl, false);
  const float panelW = std::min(sc.x * 0.62f, H * 1.25f);
  const float px0 = sc.x * 0.5f - panelW * 0.5f, px1 = px0 + panelW;
  dl->AddRectFilled(ImVec2(px0 - H * 0.02f, H * 0.03f), ImVec2(px1 + H * 0.02f, H * 0.985f), IM_COL32(8, 10, 12, 215), H * 0.02f);
  dl->AddText(ImGui::GetFont(), H * 0.05f, ImVec2(px0, H * 0.045f), IM_COL32(255, 255, 255, 255), Tr("TOUCH BUTTONS"));
  const float rowH = H * 0.078f, gap = H * 0.0105f;
  float y = H * 0.115f;
  struct Item { int id; std::string label, value; bool arrows; };
  std::vector<Item> items = {
      {0, Tr("Layout"), Tr(Choice(sCfg.layout, "Classic pad", "Side bars", "Few buttons")), true},
      {1, Tr("Show buttons"), Tr(Choice(sCfg.show, "Auto", "Always", "Never")), true},
      {2, Tr("Button size"), std::to_string(static_cast<int>(sCfg.size * 100.f + 0.5f)) + "%", true},
      {3, Tr("See-through"), std::to_string(static_cast<int>(sCfg.see * 100.f + 0.5f)) + "%", true},
      {4, Tr("Stick"), Tr(sCfg.floating ? "Follows your thumb" : "Stays put"), true},
      {5, Tr("Fast-forward button"), Tr(sCfg.ffButton ? "On" : "Off"), true},
      {6, Tr("Move the buttons..."), "", false},
      {7, Tr("Back to the defaults"), "", false},
      {8, Tr("DONE"), "", false},
  };
  float tx, ty;
  const bool tapped = TakeTap(&tx, &ty);
  const int sel = tapped ? RowAt(tx, ty) : -1;
  const bool rightHalf = tapped && tx > (px0 + px1) * 0.5f;
  for (const Item& it : items) {
    DrawRow(dl, px0, y, px1, y + rowH, it.label, it.arrows ? "< " + it.value + " >" : it.value, true, it.id == 8, it.id);
    y += rowH + gap;
  }
  sRows = sRowsNow;
  const int dirStep = rightHalf ? 1 : -1;
  switch (sel) {
  case 0: SetEnum("touch.layout", kLayoutId[(sCfg.layout + (dirStep > 0 ? 1 : kLayouts - 1)) % kLayouts]); break;
  case 1: SetEnum("touch.show", Choice((sCfg.show + (dirStep > 0 ? 1 : 2)) % 3, "auto", "always", "never")); break;
  case 2: SetInt("touch.size", std::clamp(static_cast<int>(sCfg.size * 100.f + 0.5f) + dirStep * 10, 50, 150)); break;
  case 3: SetInt("touch.opacity", std::clamp(static_cast<int>(sCfg.see * 100.f + 0.5f) + dirStep * 10, 0, 90)); break;
  case 4: SetEnum("touch.stick", sCfg.floating ? "fixed" : "floating"); break;
  case 5: es::Global().SetBool("touch.ff_button", !sCfg.ffButton); if (sSave != nullptr) sSave(); break;
  case 6: sAfterPage = kOpenEdit; return false;
  case 7:
    for (const char* id : {"touch.layout", "touch.show", "touch.size", "touch.opacity", "touch.stick", "touch.ff_button", "touch.positions_classic",
                           "touch.positions_sidebars", "touch.positions_few"}) {
      const size_t i = es::Global().Find(id);
      if (i != es::kNoSetting) es::Global().Set(i, es::Global().DefaultOf(i, es::Preset::Original));
    }
    if (sSave != nullptr) sSave();
    break;
  case 8: return false;
  default: break;
  }
  if (sBack || (in != nullptr && (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_START)))) {
    sBack = false;
    return false;
  }
  return true;
}

bool EditPage(void*, const PortMenuInput* in) {
  sMine = 3;
  sRowsNow.clear();
  Refresh();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 sc = Screen();
  const float H = sc.y;
  dl->AddRectFilled(ImVec2(0, 0), sc, IM_COL32(0, 0, 0, 120));
  DrawOverlayOnly(dl, true);
  const const std::string hint = Tr("Drag a button to move it. Pull the dot at its corner to make it bigger or smaller.");
  dl->AddText(ImGui::GetFont(), H * 0.036f, ImVec2(sc.x * 0.5f - H * 0.62f, H * 0.2f), IM_COL32(255, 255, 255, 255), hint.c_str(), nullptr, H * 1.25f);
  const float bh = H * 0.075f, bw = H * 0.36f;
  DrawRow(dl, sc.x * 0.5f - bw - H * 0.01f, H * 0.36f, sc.x * 0.5f - H * 0.01f, H * 0.36f + bh, Tr("DEFAULTS"), "", true, false, 1);
  DrawRow(dl, sc.x * 0.5f + H * 0.01f, H * 0.36f, sc.x * 0.5f + bw + H * 0.01f, H * 0.36f + bh, Tr("DONE"), "", true, true, 0);
  float tx, ty;
  int sel = -1;
  while (TakeTap(&tx, &ty)) {
    const int r = RowAt(tx, ty);
    if (r >= 0) sel = r;
  }
  sRows = sRowsNow;
  if (sel == 1) {
    for (Move& m : sMoves[sCfg.layout]) m = Move();
    SaveMoves();
  }
  if (sel == 0 || sBack || (in != nullptr && (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_START)))) {
    sBack = false;
    SaveMoves();
    sAfterPage = kOpenButtons;
    sEditSel = -1;
    return false;
  }
  return true;
}

void PageClosed(void*) {
  sMine = 0;
  sRows.clear();
  sTaps.clear();
  sQuitAsk = false;
  sAction = sAfterPage;
  sAfterPage = kNone;
}

const PortMenuPage kPausePage = {"touch_pause", nullptr, PausePage, PageClosed, nullptr};
const PortMenuPage kSettingsPage = {"touch_settings", nullptr, SettingsPage, PageClosed, nullptr};
const PortMenuPage kEditPage = {"touch_move", nullptr, EditPage, PageClosed, nullptr};

bool Live() {
  if (ImGui::GetCurrentContext() == nullptr) return false;
  ImGuiContext* context = ImGui::GetCurrentContext();
  return context->WithinFrameScope && context->Viewports.Size != 0;
}

int Mode() {
  static int mode = -1;
  if (mode < 0) {
    const char* v = std::getenv("XD_TOUCHPAD");
    const bool script = std::getenv("XD_INPUT_SCRIPT") != nullptr || std::getenv("XD_REPLAY") != nullptr;
    mode = (v != nullptr && v[0] == '0') ? 0 : (v != nullptr && v[0] == '1') ? 1 : script ? 0 : 1;
  }
  return mode;
}

bool ControllerConnected() {
  int n = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&n);
  SDL_free(ids);
  return n > 0;
}

bool Visible() {
  if (Mode() == 0) return false;
  Refresh();
  if (!sShownInit) {
    sShownInit = true;
    sShown = !ControllerConnected() || sCfg.show == 1;
  }
  if (sCfg.show == 1) return true;
  if (sCfg.show == 2) return false;
  return sShown;
}

void DrawOverlayOnly(ImDrawList* dl, bool edit) {
  for (int c : {kStick, kDpad, kL, kR, kZ, kY, kX, kA, kB, kC, kStart, kMore, kPause, kFf}) {
    DrawControl(dl, c, edit);
    if (edit && sEditSel == c) {
      const Rect r = Place(c);
      const float x = r.cx + r.hw, y = r.cy + r.hh;
      dl->AddRect(ImVec2(r.cx - r.hw, r.cy - r.hh), ImVec2(r.cx + r.hw, r.cy + r.hh), IM_COL32(255, 220, 60, 255), 0.f, 0, 3.f);
      dl->AddCircleFilled(ImVec2(x, y), Screen().y * 0.022f, IM_COL32(255, 220, 60, 255), 16);
    }
  }
}

}

namespace {

Finger* FindFinger(SDL_FingerID id) {
  for (Finger& f : sFinger) {
    if (f.used && f.id == id) return &f;
  }
  return nullptr;
}

void PushKey(SDL_Scancode scancode, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.timestamp = SDL_GetTicksNS();
  e.key.scancode = scancode;
  e.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
  e.key.down = down;
  e.key.which = kSyntheticKeyboard;
  SDL_PushEvent(&e);
}

SDL_Scancode ActionKey(ec::Action action, SDL_Scancode fallback) {
  const ec::PortMap pc = ec::ReadPort(es::Global(), 0);
  for (const ec::Input& in : pc.actions.keys[static_cast<size_t>(action)]) {
    if (in.kind == ec::Input::Kind::Key) return static_cast<SDL_Scancode>(in.code);
  }
  return fallback;
}

void Release(int c) {
  sDown[c] = 0;
  if (c == kStick) {
    sStickX = sStickY = 0;
    sStickLive = false;
  } else if (c == kC) {
    sCX = sCY = 0;
  } else if (c == kDpad) {
    sDpad = 0;
  } else if (c == kFf) {
    PushKey(ActionKey(ec::Action::FastForward, SDL_SCANCODE_TAB), false);
  }
}

void StickTo(Finger& f) {
  const Rect r = Place(kStick);
  const float gate = r.hw;
  float dx = (f.x - sStickCx) / gate, dy = (f.y - sStickCy) / gate;
  if (sCfg.floating) {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1.f) {
      sStickCx += (dx - dx / len) * gate;
      sStickCy += (dy - dy / len) * gate;
      dx /= len;
      dy /= len;
    }
  }
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1.f) { dx /= len; dy /= len; }
  const float dead = 0.08f;
  if (len < dead) { dx = dy = 0; }
  sStickX = dx;
  sStickY = -dy;
}

void DpadTo(Finger& f) {
  const Rect r = Place(kDpad);
  const float dx = f.x - r.cx, dy = f.y - r.cy;
  uint16_t bits = 0;
  if (std::sqrt(dx * dx + dy * dy) > r.hw * 0.22f) {
    const float a = std::atan2(dy, dx);
    const float s = std::sin(a), c = std::cos(a);
    if (c > 0.38f) bits |= PAD_BUTTON_RIGHT;
    if (c < -0.38f) bits |= PAD_BUTTON_LEFT;
    if (s > 0.38f) bits |= PAD_BUTTON_DOWN;
    if (s < -0.38f) bits |= PAD_BUTTON_UP;
  }
  sDpad = bits;
}

void CStickTo(Finger& f) {
  const Rect r = Place(kC);
  float dx = (f.x - sCCx) / (r.hw * 1.6f), dy = (f.y - sCCy) / (r.hw * 1.6f);
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1.f) { dx /= len; dy /= len; }
  if (len < 0.15f) dx = dy = 0;
  sCX = dx;
  sCY = -dy;
}

void FingerDown(const SDL_TouchFingerEvent& t) {
  Finger* f = nullptr;
  for (Finger& x : sFinger) {
    if (!x.used) { f = &x; break; }
  }
  if (f == nullptr) return;
  const ImVec2 sc = Screen();
  *f = Finger();
  f->used = true;
  f->id = t.fingerID;
  f->x = f->sx = t.x * sc.x;
  f->y = f->sy = t.y * sc.y;
  f->t0 = SDL_GetTicks();
  Refresh();
  const bool wasHidden = !Visible() && sCfg.show == 0 && Mode() != 0;
  if (Mode() == 0) { f->role = 0; return; }
  if (wasHidden) {
    sShown = true;
    f->role = 5;
    std::fprintf(stderr, "[touch pad] buttons shown (the screen was touched)\n");
    return;
  }
  if (sCfg.show == 2) { f->role = 0; return; }
  if (sMine == 3) {
    if (sEditSel >= 0) {
      const Rect r = Place(sEditSel);
      const float hx = r.cx + r.hw, hy = r.cy + r.hh;
      if (std::fabs(f->x - hx) < sc.y * 0.05f && std::fabs(f->y - hy) < sc.y * 0.05f) {
        f->role = 4;
        f->ctl = sEditSel;
        f->startScale = sMoves[sCfg.layout][sEditSel].scale;
        return;
      }
    }
    const int c = ControlAt(f->x, f->y);
    if (c >= 0) {
      sEditSel = c;
      f->role = 3;
      f->ctl = c;
      return;
    }
    f->role = 2;
    return;
  }
  if (sMine != 0) { f->role = 2; return; }
  const int c = ControlAt(f->x, f->y);
  if (c < 0) {
    if (PortMenu_Active()) f->role = 0;
    return;
  }
  f->role = 1;
  f->ctl = c;
  sDown[c] = 1;
  sHold[c] = sPump + 3;
  if (c == kStick) {
    const Rect r = Place(kStick);
    sStickLive = true;
    sStickCx = sCfg.floating ? f->x : r.cx;
    sStickCy = sCfg.floating ? f->y : r.cy;
    StickTo(*f);
  } else if (c == kC) {
    const Rect r = Place(kC);
    sCCx = r.cx;
    sCCy = r.cy;
    CStickTo(*f);
  } else if (c == kDpad) {
    DpadTo(*f);
  } else if (c == kFf) {
    PushKey(ActionKey(ec::Action::FastForward, SDL_SCANCODE_TAB), true);
  } else if (c == kMore) {
    sMoreUntil = SDL_GetTicks() + 5000;
  }
}

void FingerMove(const SDL_TouchFingerEvent& t) {
  Finger* f = FindFinger(t.fingerID);
  if (f == nullptr) return;
  const ImVec2 sc = Screen();
  const float nx = t.x * sc.x, ny = t.y * sc.y;
  const float ddx = nx - f->x, ddy = ny - f->y;
  f->x = nx;
  f->y = ny;
  if (f->role == 1) {
    if (f->ctl == kStick) StickTo(*f);
    else if (f->ctl == kDpad) DpadTo(*f);
    else if (f->ctl == kC) CStickTo(*f);
  } else if (f->role == 3) {
    Move& m = sMoves[sCfg.layout][f->ctl];
    m.dx = std::clamp(m.dx + ddx / sc.y, -2.f, 2.f);
    m.dy = std::clamp(m.dy + ddy / sc.y, -1.f, 1.f);
  } else if (f->role == 4) {
    const Rect r = Place(f->ctl);
    const float d0 = std::sqrt((f->sx - r.cx) * (f->sx - r.cx) + (f->sy - r.cy) * (f->sy - r.cy));
    const float d1 = std::sqrt((f->x - r.cx) * (f->x - r.cx) + (f->y - r.cy) * (f->y - r.cy));
    if (d0 > 1.f) sMoves[sCfg.layout][f->ctl].scale = std::clamp(f->startScale * d1 / d0, 0.4f, 2.5f);
  }
}

void FingerUp(const SDL_TouchFingerEvent& t, bool cancel) {
  Finger* f = FindFinger(t.fingerID);
  if (f == nullptr) return;
  const ImVec2 sc = Screen();
  const bool tap = !cancel && std::fabs(f->x - f->sx) < sc.y * 0.04f && std::fabs(f->y - f->sy) < sc.y * 0.04f;
  if (f->role == 1) {
    Release(f->ctl);
    if (f->ctl == kPause && tap && !PortMenu_Active()) sAction = kOpenPause;
    if (f->ctl == kPause && tap && PortMenu_Active() && sMine == 0) sAction = kOpenPause;
  } else if (f->role == 2 && tap) {
    sTaps.push_back({f->sx, f->sy});
  } else if (f->role == 3 || f->role == 4) {
    SaveMoves();
  }
  f->used = false;
}

void NoteOtherDevice(const char* what) {
  if (Mode() == 0 || sCfg.show != 0) return;
  if (sShown) {
    sShown = false;
    std::fprintf(stderr, "[touch pad] buttons hidden (%s in use)\n", what);
    for (int c = 0; c < kCount; c++) {
      if (sDown[c]) Release(c);
    }
    for (Finger& f : sFinger) f.used = false;
  }
}

}

extern "C" {

void PortTouchPad_HandleEvent(const void* sdlEvent) {
  const auto* e = static_cast<const SDL_Event*>(sdlEvent);
  if (Mode() == 0 || ImGui::GetCurrentContext() == nullptr) return;
  Refresh();
  switch (e->type) {
  case SDL_EVENT_FINGER_DOWN: FingerDown(e->tfinger); break;
  case SDL_EVENT_FINGER_MOTION: FingerMove(e->tfinger); break;
  case SDL_EVENT_FINGER_UP: FingerUp(e->tfinger, false); break;
  case SDL_EVENT_FINGER_CANCELED: FingerUp(e->tfinger, true); break;
  case SDL_EVENT_KEY_DOWN:
    if (e->key.which == kSyntheticKeyboard) break;
    if (e->key.scancode == SDL_SCANCODE_AC_BACK) {
      if (sMine != 0) sBack = true;
      else if (!PortMenu_Active() && sAction == kNone) sAction = kOpenPause;
      break;
    }
    NoteOtherDevice("a keyboard");
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN: NoteOtherDevice("a controller"); break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (std::abs(static_cast<int>(e->gaxis.value)) > 16000) NoteOtherDevice("a controller");
    break;
  default: break;
  }
}

bool PortTouchPad_IsOwnKey(const void* sdlEvent) {
  const auto* e = static_cast<const SDL_Event*>(sdlEvent);
  return (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && e->key.which == kSyntheticKeyboard;
}

void PortTouchPad_SetContext(uint32_t (*fn)(void)) { sContext = fn; }

void PortTouchPad_Init(void (*saveSettings)(void)) {
  sSave = saveSettings;
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
}

}

namespace {

struct Step {
  std::string op;
  std::string a;
  float x = 0, y = 0;
  int n = 0;
};
std::vector<Step> sScript;
size_t sScriptAt = 0;
int sScriptWait = 0;
bool sScriptLoaded = false;
struct TestFinger {
  SDL_FingerID id;
  int release;
  float x, y;
};
std::vector<TestFinger> sTestFingers;
SDL_FingerID sNextTestFinger = 1000;
SDL_FingerID sStickFinger = 0;

void PushFinger(SDL_EventType type, SDL_FingerID id, float px, float py) {
  const ImVec2 sc = Screen();
  SDL_Event e{};
  e.type = type;
  e.tfinger.timestamp = SDL_GetTicksNS();
  e.tfinger.touchID = kTestTouch;
  e.tfinger.fingerID = id;
  e.tfinger.x = px / sc.x;
  e.tfinger.y = py / sc.y;
  e.tfinger.pressure = 1.f;
  SDL_PushEvent(&e);
}

int CtlByName(const std::string& s) {
  for (int c = 0; c < kCount; c++) {
    if (s == kName[c]) return c;
  }
  return -1;
}

void LoadScript() {
  sScriptLoaded = true;
  const char* v = std::getenv("XD_TOUCH_SCRIPT");
  if (v == nullptr) return;
  std::string text = v;
  size_t i = 0;
  while (i <= text.size()) {
    size_t e = text.find(';', i);
    if (e == std::string::npos) e = text.size();
    std::string item = text.substr(i, e - i);
    i = e + 1;
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    if (item.empty()) continue;
    Step s;
    char op[32] = {}, a[64] = {};
    float x = 0, y = 0;
    const int got = std::sscanf(item.c_str(), "%31s %63s %f %f", op, a, &x, &y);
    s.op = op;
    if (got >= 2) {
      s.a = a;
      s.x = x;
      s.y = y;
      s.n = static_cast<int>(std::strtol(a, nullptr, 10));
    }
    sScript.push_back(s);
  }
  std::fprintf(stderr, "[touch pad] test script: %zu steps\n", sScript.size());
}

void Pulse(float px, float py, int pumps) {
  const SDL_FingerID id = sNextTestFinger++;
  PushFinger(SDL_EVENT_FINGER_DOWN, id, px, py);
  sTestFingers.push_back({id, pumps, px, py});
}

void RunScript() {
  if (!sScriptLoaded) LoadScript();
  for (auto it = sTestFingers.begin(); it != sTestFingers.end();) {
    if (--it->release <= 0 && it->id != sStickFinger) {
      PushFinger(SDL_EVENT_FINGER_UP, it->id, it->x, it->y);
      it = sTestFingers.erase(it);
    } else {
      ++it;
    }
  }
  if (sScriptAt >= sScript.size()) return;
  if (sScriptWait > 0) {
    sScriptWait--;
    return;
  }
  while (sScriptAt < sScript.size()) {
    const Step& s = sScript[sScriptAt];
    if (s.op == "at") {
      if (PortVI_GetFlipCount() < static_cast<uint32_t>(s.n)) return;
    } else if (s.op == "wait") {
      sScriptWait = s.n;
      sScriptAt++;
      return;
    } else if (s.op == "tap" || s.op == "down") {
      const int c = CtlByName(s.a);
      const int hold = s.op == "tap" ? (s.x > 0 ? static_cast<int>(s.x) : 4) : 1000000;
      if (c < 0) {
        std::fprintf(stderr, "[touch pad] test script: no control called %s\n", s.a.c_str());
      } else {
        const Rect r = Place(c);
        float px = r.cx, py = r.cy;
        if (c == kDpad) {
          const int dir = static_cast<int>(s.x);
          px += dir == 3 ? -r.hw * 0.6f : dir == 4 ? r.hw * 0.6f : 0.f;
          py += dir == 1 ? -r.hw * 0.6f : dir == 2 ? r.hw * 0.6f : 0.f;
          Pulse(px, py, 4);
        } else {
          Pulse(px, py, hold);
        }
        std::fprintf(stderr, "[touch pad] test script: %s %s at %.0f,%.0f (pump %u)\n", s.op.c_str(), s.a.c_str(), px, py, sPump);
      }
    } else if (s.op == "up") {
      for (auto it = sTestFingers.begin(); it != sTestFingers.end();) {
        if (it->release > 100000) {
          PushFinger(SDL_EVENT_FINGER_UP, it->id, it->x, it->y);
          it = sTestFingers.erase(it);
        } else {
          ++it;
        }
      }
    } else if (s.op == "stick") {
      const Rect r = Place(kStick);
      const float sx = std::strtof(s.a.c_str(), nullptr), sy = s.x;
      const bool zero = sx == 0.f && sy == 0.f;
      if (sStickFinger == 0 && !zero) {
        sStickFinger = sNextTestFinger++;
        PushFinger(SDL_EVENT_FINGER_DOWN, sStickFinger, r.cx, r.cy);
        sTestFingers.push_back({sStickFinger, 1000000, r.cx, r.cy});
        PushFinger(SDL_EVENT_FINGER_MOTION, sStickFinger, r.cx + sx * r.hw, r.cy - sy * r.hw);
      } else if (sStickFinger != 0) {
        if (zero) {
          PushFinger(SDL_EVENT_FINGER_UP, sStickFinger, r.cx, r.cy);
          for (auto it = sTestFingers.begin(); it != sTestFingers.end(); ++it) {
            if (it->id == sStickFinger) { sTestFingers.erase(it); break; }
          }
          sStickFinger = 0;
        } else {
          PushFinger(SDL_EVENT_FINGER_MOTION, sStickFinger, r.cx + sx * r.hw, r.cy - sy * r.hw);
        }
      }
    } else if (s.op == "tapxy") {
      const ImVec2 sc = Screen();
      Pulse(std::strtof(s.a.c_str(), nullptr) * sc.x, s.x * sc.y, 4);
    } else if (s.op == "row") {
      bool found = false;
      for (const RowRect& r : sRows) {
        if (r.id == s.n) {
          float px = (r.x0 + r.x1) * 0.5f;
          if (s.x > 0) px = r.x0 + (r.x1 - r.x0) * s.x;
          Pulse(px, (r.y0 + r.y1) * 0.5f, 4);
          found = true;
        }
      }
      if (!found) {
        std::fprintf(stderr, "[touch pad] test script: no row %d now\n", s.n);
        if (sScriptWait == 0) { sScriptWait = 30; return; }
      }
    } else if (s.op == "drag") {
      const int c = CtlByName(s.a);
      if (c >= 0) {
        const Rect r = Place(c);
        const SDL_FingerID id = sNextTestFinger++;
        const ImVec2 sc = Screen();
        PushFinger(SDL_EVENT_FINGER_DOWN, id, r.cx, r.cy);
        PushFinger(SDL_EVENT_FINGER_MOTION, id, r.cx + s.y * sc.y * 0.5f, r.cy + s.y * sc.y * 0.5f);
        PushFinger(SDL_EVENT_FINGER_MOTION, id, r.cx + s.x * sc.y, r.cy + (s.y) * sc.y);
        PushFinger(SDL_EVENT_FINGER_UP, id, r.cx + s.x * sc.y, r.cy + s.y * sc.y);
      }
    } else if (s.op == "back") {
      SDL_Event e{};
      e.type = SDL_EVENT_KEY_DOWN;
      e.key.scancode = SDL_SCANCODE_AC_BACK;
      e.key.down = true;
      SDL_PushEvent(&e);
    } else if (s.op == "exit") {
      std::fprintf(stderr, "[touch pad] test script: exit\n");
      std::fflush(stderr);
      std::_Exit(0);
    } else if (s.op == "mark") {
      std::fprintf(stderr, "[touch pad] test script: mark %s (pump %u, frame %u)\n", s.a.c_str(), sPump, PortVI_GetFlipCount());
    }
    sScriptAt++;
    if (s.op == "tap" || s.op == "tapxy" || s.op == "row" || s.op == "drag") {
      sScriptWait = 6;
      return;
    }
  }
}

void ToastDraw(ImDrawList* dl) {
  if (sToastUntil <= SDL_GetTicks()) return;
  const ImVec2 sc = Screen();
  const float h = sc.y * 0.06f;
  const ImVec2 t = ImGui::GetFont()->CalcTextSizeA(h * 0.55f, 1000.f, 0.f, sToast.c_str());
  dl->AddRectFilled(ImVec2(sc.x * 0.5f - t.x * 0.5f - h * 0.5f, sc.y * 0.88f), ImVec2(sc.x * 0.5f + t.x * 0.5f + h * 0.5f, sc.y * 0.88f + h), IM_COL32(0, 0, 0, 215), h * 0.4f);
  dl->AddText(ImGui::GetFont(), h * 0.55f, ImVec2(sc.x * 0.5f - t.x * 0.5f, sc.y * 0.88f + h * 0.22f), IM_COL32(255, 255, 255, 255), sToast.c_str());
}

void DoAction() {
  if (sAction == kNone || PortMenu_Active() || PortMenu_Pending()) return;
  const Action a = sAction;
  sAction = kNone;
  switch (a) {
  case kOpenPause: PortMenu_Open(&kPausePage); break;
  case kOpenButtons: PortMenu_Open(&kSettingsPage); break;
  case kOpenEdit: PortMenu_Open(&kEditPage); break;
  case kOpenGameSettings: PortXD_OpenSettingsPageForTouch(); break;
  case kOpenCheats: PortXD_OpenCheatMenu(); break;
  case kPhoto: {
    const std::string name = es::Global().Text("video.photo_mode_key");
    SDL_Scancode k = name.empty() ? SDL_SCANCODE_F8 : SDL_GetScancodeFromName(name.c_str());
    if (k == SDL_SCANCODE_UNKNOWN) k = SDL_SCANCODE_F8;
    PushKey(k, true);
    PushKey(k, false);
    break;
  }
  case kBugReport:
    PushKey(SDL_SCANCODE_F10, true);
    PushKey(SDL_SCANCODE_F10, false);
    sToast = Tr("Bug report saved");
    sToastUntil = SDL_GetTicks() + 3500;
    break;
  case kQuit: {
    std::fprintf(stderr, "[touch pad] quit from the pause list\n");
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
    break;
  }
  default: break;
  }
}

}

extern "C" {

void PortTouchPad_Pump(void) {
  if (Mode() == 0 || ImGui::GetCurrentContext() == nullptr) return;
  sPump++;
  Refresh();
  RunScript();
  DoAction();
  static bool was = false;
  PADStatus st{};
  st.err = PAD_ERR_NONE;
  const bool active = Visible() && sMine == 0 && sCfg.show != 2;
  bool any = false;
  if (active) {
    if (DownNow(kA)) st.button |= PAD_BUTTON_A, st.analogA = 255;
    if (DownNow(kB)) st.button |= PAD_BUTTON_B, st.analogB = 255;
    if (DownNow(kX)) st.button |= PAD_BUTTON_X;
    if (DownNow(kY)) st.button |= PAD_BUTTON_Y;
    if (DownNow(kZ)) st.button |= PAD_TRIGGER_Z;
    if (DownNow(kStart)) st.button |= PAD_BUTTON_START;
    if (DownNow(kL)) st.button |= PAD_TRIGGER_L, st.triggerLeft = 255;
    if (DownNow(kR)) st.button |= PAD_TRIGGER_R, st.triggerRight = 255;
    st.button |= sDpad;
    const float sx = sStickX * 127.f, sy = sStickY * 127.f;
    st.stickX = static_cast<s8>(std::clamp(std::lround(sx), -127l, 127l));
    st.stickY = static_cast<s8>(std::clamp(std::lround(sy), -127l, 127l));
    st.substickX = static_cast<s8>(std::clamp(std::lround(sCX * 127.f), -127l, 127l));
    st.substickY = static_cast<s8>(std::clamp(std::lround(sCY * 127.f), -127l, 127l));
    any = st.button != 0 || st.stickX != 0 || st.stickY != 0 || st.substickX != 0 || st.substickY != 0;
  }
  if (any) {
    PADSetVirtualStatus(0, &st);
    was = true;
  } else if (was) {
    PADClearVirtualStatus(0);
    was = false;
  }
}

void PortTouchPad_Frame(void) {
  if (Mode() == 0 || !Live()) {
    if (Mode() != 0) {
      for (int c = 0; c < kCount; c++) {
        if (sDown[c]) Release(c);
      }
      for (Finger& f : sFinger) f.used = false;
    }
    return;
  }
  static bool wasShown = true;
  const bool shown = Visible();
  if (shown != wasShown) {
    std::fprintf(stderr, "[touch pad] touch buttons %s (layout %s)\n", shown ? "shown" : "hidden", kLayoutId[sCfg.layout]);
    wasShown = shown;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (shown && sMine == 0) DrawOverlayOnly(dl, false);
  ToastDraw(dl);
}

}

#endif
