namespace kit {

const uint8_t kText[4] = {0xF0, 0xF0, 0xF0, 0xFF};
const uint8_t kSoft[4] = {0xC0, 0xC0, 0xC8, 0xFF};
const uint8_t kDim[4] = {0x88, 0x88, 0x90, 0xFF};
const uint8_t kGold[4] = {0xFF, 0xD8, 0x68, 0xFF};
const uint8_t kGood[4] = {0x98, 0xEC, 0x98, 0xFF};
const uint8_t kBad[4] = {0xFF, 0x98, 0x88, 0xFF};
const uint8_t kShadow[4] = {0xC8, 0xA8, 0xFF, 0xFF};

constexpr s32 kMsgMenu = 0x33, kYesNoMenu = 0x35;
constexpr int kMsgFirst = 36, kMsgLast = 44, kYesNoFirst = 66, kYesNoLast = 74;

float Time() {
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
}
uint8_t Pulse() { return PulseAlpha(std::fmod(Time(), 1.f)); }

void WideSpan(const Screen& sc, float& left, float& right) {
  float W = 640.f;
  if (ImGui::GetCurrentContext() != nullptr) W = ImGui::GetIO().DisplaySize.x;
  left = std::floor(-sc.x / sc.s) - 1.f;
  right = std::ceil((W - sc.x) / sc.s) + 1.f;
}

bool HavePictures() { return ItemPicture(kOptMenu, kOptPanel).ok && ItemPicture(kOptMenu, kOptBgTop).ok; }

void Text(ImDrawList* dl, const Screen& sc, float x, float y, const std::string& t, const uint8_t* col, float scale = 1.f,
          float maxW = 0.f, uint16_t font = 0) {
  const UseFont f(font);
  float squeeze = 1.f;
  if (maxW > 0.f) {
    const float tw = static_cast<float>(TextWidth(t.c_str())) * scale;
    if (tw > maxW) squeeze = maxW / tw;
  }
  DrawText(dl, sc, x, y, t.c_str(), col, squeeze, scale);
}
float Width(const std::string& t, float scale = 1.f, uint16_t font = 0) {
  const UseFont f(font);
  return static_cast<float>(TextWidth(t.c_str())) * scale;
}
void TextC(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& t, const uint8_t* col, float scale = 1.f,
           uint16_t font = 0) {
  const float tw = std::min(Width(t, scale, font), w);
  Text(dl, sc, std::floor(x + (w - tw) / 2.f), y, t, col, scale, w, font);
}
int Para(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& t, const uint8_t* col, float scale, float pitch,
         int maxLines, uint16_t font = 0) {
  std::vector<std::string> lines;
  {
    const UseFont f(font);
    lines = Wrap(t, w / scale);
  }
  const int n = std::min<int>(maxLines, static_cast<int>(lines.size()));
  for (int i = 0; i < n; i++) Text(dl, sc, x, y + pitch * static_cast<float>(i), lines[static_cast<size_t>(i)], col, scale, w, font);
  return n;
}
int LinesOf(const std::string& t, float w, float scale, uint16_t font = 0) {
  const UseFont f(font);
  return static_cast<int>(Wrap(t, w / scale).size());
}

bool Nine(ImDrawList* dl, const Screen& sc, s32 menu, int first, int last, float x0, float y0, float x1, float y1, uint8_t alpha = 255,
          uint8_t midAlpha = 255) {
  std::vector<ItemPic> pics;
  std::vector<int> xs, ys;
  for (int i = first; i <= last; i++) {
    ItemPic p = ItemPicture(menu, i);
    if (!p.ok || p.shot.sp.b76 != 0) return false;
    xs.push_back(p.shot.sp.x), ys.push_back(p.shot.sp.y);
    pics.push_back(p);
  }
  std::sort(xs.begin(), xs.end()), xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  std::sort(ys.begin(), ys.end()), ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  if (xs.size() != 3 || ys.size() != 3) return false;
  float cw[3] = {0, 0, 0}, rh[3] = {0, 0, 0};
  for (const ItemPic& p : pics) {
    const int c = static_cast<int>(std::find(xs.begin(), xs.end(), p.shot.sp.x) - xs.begin());
    const int r = static_cast<int>(std::find(ys.begin(), ys.end(), p.shot.sp.y) - ys.begin());
    cw[c] = std::max(cw[c], static_cast<float>(std::abs(p.shot.sp.w)));
    rh[r] = std::max(rh[r], static_cast<float>(std::abs(p.shot.sp.h)));
  }
  const float kx = std::min(1.f, (x1 - x0) / std::max(1.f, cw[0] + cw[2])), ky = std::min(1.f, (y1 - y0) / std::max(1.f, rh[0] + rh[2]));
  cw[0] *= kx, cw[2] *= kx, rh[0] *= ky, rh[2] *= ky;
  for (ItemPic& p : pics) {
    const int c = static_cast<int>(std::find(xs.begin(), xs.end(), p.shot.sp.x) - xs.begin());
    const int r = static_cast<int>(std::find(ys.begin(), ys.end(), p.shot.sp.y) - ys.begin());
    const float px = c == 0 ? x0 : c == 1 ? x0 + cw[0] : x1 - cw[2];
    const float pw = c == 1 ? std::max(0.f, x1 - x0 - cw[0] - cw[2]) : cw[c];
    const float py = r == 0 ? y0 : r == 1 ? y0 + rh[0] : y1 - rh[2];
    const float ph = r == 1 ? std::max(0.f, y1 - y0 - rh[0] - rh[2]) : rh[r];
    if (pw <= 0.f || ph <= 0.f) continue;
    const SpriteWorkLayout& sp = p.shot.sp;
    const Picture& pic = p.shot.pic;
    float s0, s1, t0, t1;
    UVOf(sp, pic, s0, s1, t0, t1);
    s0 += 0.5f / pic.w, s1 -= 0.5f / pic.w, t0 += 0.5f / pic.h, t1 -= 0.5f / pic.h;
    if (sp.w < 0) std::swap(s0, s1);
    if (sp.h < 0) std::swap(t0, t1);
    dl->AddImage(pic.id, sc.At(px, py), sc.At(px + pw, py + ph), ImVec2(s0, t0), ImVec2(s1, t1),
                 IM_COL32(sp.color[0], sp.color[1], sp.color[2], sp.color[3] * alpha / 255 * (c == 1 && r == 1 ? midAlpha : 255) / 255));
  }
  return true;
}

bool Spr(ImDrawList* dl, const Screen& sc, uint32_t tex, int u, int v, int w, int h, float x, float y, float dw, float dh,
         uint8_t alpha = 255) {
  const Picture pic = PictureById(tex << 16 | 0x1200);
  if (!pic.ok || alpha == 0) return false;
  float s0 = (static_cast<float>(u) + 0.5f) / pic.w, s1 = (static_cast<float>(u + w) - 0.5f) / pic.w;
  float t0 = (static_cast<float>(v) + 0.5f) / pic.h, t1 = (static_cast<float>(v + h) - 0.5f) / pic.h;
  if (dw < 0.f) std::swap(s0, s1), dw = -dw;
  if (dh < 0.f) std::swap(t0, t1), dh = -dh;
  dl->AddImage(pic.id, sc.At(x, y), sc.At(x + dw, y + dh), ImVec2(s0, t0), ImVec2(s1, t1), IM_COL32(255, 255, 255, alpha));
  return true;
}

void Backdrop(ImDrawList* dl, const Screen& sc) {
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  float l, r;
  WideSpan(sc, l, r);
  const auto* top = static_cast<const MenuItemView*>(menuItemBiosGetPtr(kOptMenu, static_cast<u32>(kOptBgTop)));
  const auto* bot = static_cast<const MenuItemView*>(menuItemBiosGetPtr(kOptMenu, static_cast<u32>(kOptBgBottom)));
  if (HavePictures() && top != nullptr && bot != nullptr) {
    DrawItem(dl, sc, kOptBgTop, l, top->y, r - l, -1.f);
    DrawItem(dl, sc, kOptBgBottom, l, bot->y, r - l, -1.f);
    return;
  }
  for (int y = 0; y < 480; y += 4) {
    const float k = 0.55f + 0.45f * std::sin(static_cast<float>(y) / 480.f * 3.14159f);
    const int red = static_cast<int>(70.f * k);
    dl->AddRectFilled(sc.At(l, static_cast<float>(y)), sc.At(r, static_cast<float>(y) + 2.f), IM_COL32(red, 4, 6, 255));
    dl->AddRectFilled(sc.At(l, static_cast<float>(y) + 2.f), sc.At(r, static_cast<float>(y) + 4.f), IM_COL32(red / 2, 2, 4, 255));
  }
}

void Dim(ImDrawList* dl, uint8_t a = 150) { FillWindow(dl, IM_COL32(0, 0, 0, a)); }

void Band(ImDrawList* dl, const Screen& sc, float y, float h = -1.f) {
  float l, r;
  WideSpan(sc, l, r);
  if (ItemPicture(kOptMenu, kOptHeader).ok) {
    DrawItem(dl, sc, kOptHeader, l, y, r - l, h);
    return;
  }
  const float hh = h > 0.f ? h : 36.f;
  dl->AddRectFilled(sc.At(l, y), sc.At(r, y + hh), IM_COL32(0, 0, 0, 255));
  dl->AddLine(sc.At(l, y + 2.f), sc.At(r, y + 2.f), IM_COL32(0xD0, 0xD0, 0xD0, 255), 1.f * sc.s);
  dl->AddLine(sc.At(l, y + hh - 3.f), sc.At(r, y + hh - 3.f), IM_COL32(0xD0, 0xD0, 0xD0, 255), 1.f * sc.s);
}

void OptionsScreen(ImDrawList* dl, const Screen& sc) {
  Backdrop(dl, sc);
  DrawItemAsIs(dl, sc, kOptPanel);
  const auto* item = static_cast<const MenuItemView*>(menuItemBiosGetPtr(kOptMenu, static_cast<u32>(kOptHeader)));
  if (item != nullptr && ItemPicture(kOptMenu, kOptHeader).ok) Band(dl, sc, static_cast<float>(item->y));
}

void Header(ImDrawList* dl, const Screen& sc, const std::string& title, const std::string& right = "") {
  const auto* item = static_cast<const MenuItemView*>(menuItemBiosGetPtr(kOptMenu, static_cast<u32>(kOptHeader)));
  const float y = item != nullptr && ItemPicture(kOptMenu, kOptHeader).ok ? static_cast<float>(item->y) : 30.f;
  Band(dl, sc, y);
  float ty;
  {
    const UseFont f(1);
    ty = 38.f + std::floor((22.f - static_cast<float>(sFontW + 1)) / 2.f);
  }
  Text(dl, sc, 82.f, ty, title, kText, 1.f, right.empty() ? 480.f : 330.f, 1);
  if (!right.empty()) {
    const float w = std::min(Width(right, 0.8f), 150.f);
    Text(dl, sc, 588.f - w, ty + 3.f, right, kSoft, 0.8f, 150.f);
  }
}

constexpr float kInset = 22.f;
void Window(ImDrawList* dl, const Screen& sc, float x0, float y0, float x1, float y1, bool accent = false, uint8_t alpha = 255) {
  if (!Nine(dl, sc, kOptMenu, kOptDescFirst, kOptDescLast, x0, y0, x1, y1, alpha)) {
    dl->AddRectFilled(sc.At(x0, y0), sc.At(x1, y1), IM_COL32(0x80, 0x80, 0x84, alpha), 10.f * sc.s);
    dl->AddRectFilled(sc.At(x0 + 5.f, y0 + 5.f), sc.At(x1 - 5.f, y1 - 5.f), IM_COL32(0x14, 0x14, 0x16, alpha), 7.f * sc.s);
  }
  if (accent) {
    const float a = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(Time() * 3.f));
    dl->AddRect(sc.At(x0 + 7.f, y0 + 7.f), sc.At(x1 - 7.f, y1 - 7.f), IM_COL32(0xFF, 0xD8, 0x68, static_cast<int>(200.f * a)), 6.f * sc.s, 0,
                1.2f * sc.s);
  }
}
void MessageWindow(ImDrawList* dl, const Screen& sc, float x0 = 27.f, float y0 = 290.f, float x1 = 613.f, float y1 = 440.f) {
  if (!Nine(dl, sc, kMsgMenu, kMsgFirst, kMsgLast, x0, y0, x1, y1)) Window(dl, sc, x0, y0, x1, y1);
}
void TitledWindow(ImDrawList* dl, const Screen& sc, float x0, float y0, float x1, float y1, const std::string& title, bool accent = false) {
  Window(dl, sc, x0, y0, x1, y1, accent);
  Text(dl, sc, x0 + kInset + 4.f, y0 + 10.f, title, kGold, 0.85f, x1 - x0 - 2.f * kInset - 8.f, 1);
  dl->AddLine(sc.At(x0 + kInset, y0 + 32.f), sc.At(x1 - kInset, y0 + 32.f), IM_COL32(0x70, 0x70, 0x74, 255), 1.f * sc.s);
}

void Cursor(ImDrawList* dl, const Screen& sc, float cx, float cy, float size = 1.f) {
  const ImVec2 c = sc.At(cx + 1.5f * std::sin(Time() * 6.28f), cy);
  dl->AddCircleFilled(ImVec2(c.x + 1.f * sc.s, c.y + 1.f * sc.s), 8.5f * size * sc.s, IM_COL32(0, 0, 0, 120), 24);
  dl->AddCircleFilled(c, 8.5f * size * sc.s, IM_COL32(0xD8, 0x18, 0x20, 255), 24);
  dl->AddCircle(c, 8.5f * size * sc.s, IM_COL32(0xF8, 0xB0, 0xB0, 200), 24, 1.f * sc.s);
  const ImVec2 pts[3] = {ImVec2(c.x - 2.5f * size * sc.s, c.y - 5.f * size * sc.s), ImVec2(c.x + 3.f * size * sc.s, c.y),
                         ImVec2(c.x - 2.5f * size * sc.s, c.y + 5.f * size * sc.s)};
  dl->AddPolyline(pts, 3, IM_COL32(0x18, 0x18, 0x18, 255), 0, 2.6f * size * sc.s);
}

void RowSel(ImDrawList* dl, const Screen& sc, float x0, float x1, float y, float h, bool cursor = true) {
  if (ItemPicture(kOptMenu, kOptRowBar).ok) {
    DrawItem(dl, sc, kOptRowBar, x0, y, x1 - x0, h);
  } else {
    dl->AddRectFilledMultiColor(sc.At(x0, y), sc.At(x1, y + h), IM_COL32(150, 20, 24, 230), IM_COL32(90, 10, 14, 200),
                                IM_COL32(90, 10, 14, 200), IM_COL32(150, 20, 24, 230));
  }
  dl->AddLine(sc.At(x0, y), sc.At(x1, y), IM_COL32(0xFF, 0x80, 0x80, 140), 1.f * sc.s);
  dl->AddLine(sc.At(x0, y + h), sc.At(x1, y + h), IM_COL32(0xFF, 0x80, 0x80, 140), 1.f * sc.s);
  if (cursor) Cursor(dl, sc, x0 + 12.f, y + h / 2.f);
}

void Row(ImDrawList* dl, const Screen& sc, float x0, float x1, float y, float h, const std::string& label, bool sel,
         const std::string& value = "", const uint8_t* labelCol = nullptr, const uint8_t* valueCol = nullptr, float scale = 0.85f) {
  if (sel) RowSel(dl, sc, x0, x1, y, h);
  const float ty = y + std::floor((h - 23.f * scale) / 2.f) + 1.f;
  const float lx = x0 + 28.f;
  float room = x1 - lx - 10.f;
  if (!value.empty()) {
    const float vw = std::min(Width(value, scale * 0.95f), (x1 - lx) * 0.55f);
    Text(dl, sc, x1 - 12.f - vw, ty, value, valueCol != nullptr ? valueCol : kGold, scale * 0.95f, vw);
    room = x1 - 12.f - vw - lx - 12.f;
  }
  Text(dl, sc, lx, ty, label, labelCol != nullptr ? labelCol : sel ? kText : kSoft, scale, room, 1);
}

void Arrow(ImDrawList* dl, const Screen& sc, float x, float y, bool up) {
  const float bob = 2.f * std::sin(Time() * 6.28f);
  DrawTriangle(dl, sc, x, y + (up ? -bob : bob), 14.f, up ? 0 : 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
}

void Button(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& label, bool sel, bool enabled = true) {
  const uint8_t a = enabled ? 255 : 120;
  if (ItemPicture(kOptMenu, kOptExit).ok) {
    DrawItem(dl, sc, kOptExit, x, y, w, -1.f, a, 16.f);
    if (sel) DrawItem(dl, sc, kOptExitCursor, x - 3.f, y - 3.f, w + 6.f, -1.f, Pulse(), 16.f);
  } else {
    dl->AddRectFilled(sc.At(x, y), sc.At(x + w, y + 32.f), IM_COL32(0x10, 0x10, 0x10, a), 16.f * sc.s);
    dl->AddRect(sc.At(x, y), sc.At(x + w, y + 32.f), IM_COL32(0xC0, 0xC0, 0xC0, a), 16.f * sc.s, 0, 2.f * sc.s);
    if (sel) dl->AddRect(sc.At(x - 3.f, y - 3.f), sc.At(x + w + 3.f, y + 35.f), IM_COL32(0xF0, 0xF0, 0xF0, Pulse()), 18.f * sc.s, 0, 2.f * sc.s);
  }
  uint8_t col[4] = {kText[0], kText[1], kText[2], static_cast<uint8_t>(enabled ? 0xFF : 0x80)};
  TextC(dl, sc, x + 12.f, y + 5.f, w - 24.f, label, col, 0.95f, 1);
}
constexpr float kButtonH = 32.f;

void Oval(ImDrawList* dl, const Screen& sc, float x, float y, float w, bool pulse, uint8_t alpha = 255) {
  if (ItemPicture(kOptMenu, kOptChoice).ok) {
    DrawItem(dl, sc, kOptChoice, x, y, w, -1.f, pulse ? static_cast<uint8_t>(Pulse() * alpha / 255) : alpha, 14.f);
    return;
  }
  dl->AddRect(sc.At(x, y), sc.At(x + w, y + 24.f), IM_COL32(0xF0, 0xF0, 0xF0, pulse ? Pulse() : alpha), 12.f * sc.s, 0, 2.f * sc.s);
}
void Value(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& v, bool sel, const uint8_t* col = kText) {
  Oval(dl, sc, x, y, w, sel);
  TextC(dl, sc, x + 22.f, y + 1.f, w - 44.f, v, col, 0.9f, 1);
  if (sel) {
    DrawTriangle(dl, sc, x + 14.f, y + 12.f, 11.f, 2, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
    DrawTriangle(dl, sc, x + w - 14.f, y + 12.f, 11.f, 3, IM_COL32(0xF0, 0xF0, 0xF0, 0xFF));
  }
}

float Key(ImDrawList* dl, const Screen& sc, float cx, float cy, const std::string& k, float size = 1.f) {
  {
    static const char* const kNames[] = {"A", "B", "X", "Y", "Z", "L", "R", "START"};
    int input = -1;
    for (int i = 0; i < 8; i++) if (k == kNames[i]) input = i;
    if (input >= 0) {
      struct Tex { uint32_t style = ~0u; ImTextureID id{}; bool ok = false; };
      static std::map<int, Tex> sTex;
      const bool wide = input >= 4;
      const int pw = wide ? 96 : 64, ph = 64;
      const uint32_t now = PortXD_HintButtonStyle();
      Tex& t = sTex[input];
      if (now != 0 && t.style != now) {
        std::vector<uint8_t> rgba;
        uint32_t style = 0;
        t.ok = PortXD_HintButtonPicture(input, pw, ph, rgba, style);
        if (t.ok) t.id = aurora_imgui_add_texture(static_cast<uint32_t>(pw), static_cast<uint32_t>(ph), rgba.data());
        t.style = now;
      }
      if (now != 0 && t.ok) {
        const float h = 22.f * size, w = h * static_cast<float>(pw) / ph;
        dl->AddImage(t.id, sc.At(cx - w / 2.f, cy - h / 2.f), sc.At(cx + w / 2.f, cy + h / 2.f));
        return w;
      }
    }
  }
  auto letter = [&](float x, const char* t, float scale) {
    const float w = Width(t, scale, 1);
    Text(dl, sc, std::floor(x - w / 2.f), cy - 11.f * scale - 1.f, t, kText, scale, 0.f, 1);
  };
  const float r = 9.f * size;
  if (k == "A") {
    dl->AddCircleFilled(sc.At(cx, cy), (r + 1.5f) * sc.s, IM_COL32(0x10, 0x60, 0x38, 255), 24);
    dl->AddCircleFilled(sc.At(cx, cy), r * sc.s, IM_COL32(0x2E, 0xB8, 0x6E, 255), 24);
    letter(cx, "A", 0.62f * size);
    return 2.f * r + 3.f;
  }
  if (k == "B") {
    dl->AddCircleFilled(sc.At(cx, cy), (r - 0.5f) * sc.s, IM_COL32(0x70, 0x10, 0x10, 255), 24);
    dl->AddCircleFilled(sc.At(cx, cy), (r - 2.f) * sc.s, IM_COL32(0xE0, 0x30, 0x30, 255), 24);
    letter(cx, "B", 0.55f * size);
    return 2.f * r;
  }
  if (k == "X" || k == "Y") {
    dl->AddRectFilled(sc.At(cx - r, cy - r * 0.75f), sc.At(cx + r, cy + r * 0.75f), IM_COL32(0xD8, 0xD8, 0xDC, 255), r * 0.75f * sc.s);
    const float w = Width(k, 0.55f * size, 1);
    const uint8_t dark[4] = {0x20, 0x20, 0x20, 0xFF};
    Text(dl, sc, std::floor(cx - w / 2.f), cy - 7.f * size, k, dark, 0.55f * size, 0.f, 1);
    return 2.f * r;
  }
  if (k == "Z" || k == "L" || k == "R" || k == "START") {
    const float w = std::max(2.f * r, Width(k, 0.5f * size, 1) + 10.f * size);
    const ImU32 fill = k == "Z" ? IM_COL32(0x70, 0x48, 0xC8, 255) : IM_COL32(0x60, 0x60, 0x68, 255);
    dl->AddRectFilled(sc.At(cx - w / 2.f, cy - r * 0.7f), sc.At(cx + w / 2.f, cy + r * 0.7f), fill, r * 0.7f * sc.s);
    letter(cx, k.c_str(), 0.5f * size);
    return w;
  }
  return 0.f;
}

void Hints(ImDrawList* dl, const Screen& sc, float x, float y, const std::string& text, float maxW, const uint8_t* col = kSoft) {
  std::vector<std::string> segs;
  {
    std::string cur;
    int spaces = 0;
    for (char c : text) {
      if (c == ' ') { spaces++; continue; }
      if (spaces >= 2 && !cur.empty()) segs.push_back(cur), cur.clear();
      else if (spaces == 1) cur += ' ';
      spaces = 0;
      cur += c;
    }
    if (!cur.empty()) segs.push_back(cur);
  }
  const float scale = 0.72f;
  struct Part { float keyW; std::string key, words; };
  std::vector<Part> parts;
  float total = 0.f;
  for (const std::string& s : segs) {
    Part p{0.f, "", s};
    const size_t colon = s.find(':');
    if (colon != std::string::npos) {
      const std::string k = s.substr(0, colon);
      if (k == "A" || k == "B" || k == "X" || k == "Y" || k == "Z" || k == "L" || k == "R" || k == "START") {
        p.key = k;
        p.words = s.substr(colon + 1);
        while (!p.words.empty() && p.words.front() == ' ') p.words.erase(0, 1);
        p.keyW = k == "START" || k == "Z" || k == "L" || k == "R" ? 36.f : 22.f;
      }
    }
    total += (p.key.empty() ? 0.f : p.keyW + 5.f) + Width(p.words, scale) + 18.f;
    parts.push_back(p);
  }
  const float k = total > maxW && total > 0.f ? maxW / total : 1.f;
  float cx = x;
  for (const Part& p : parts) {
    if (!p.key.empty()) {
      const float w = Key(dl, sc, cx + p.keyW * k / 2.f, y + 8.f, p.key, std::max(0.75f, k));
      cx += std::max(w, p.keyW) * k + 5.f * k;
    }
    const float ww = Width(p.words, scale) * k;
    Text(dl, sc, cx, y, p.words, col, scale, ww + 0.5f);
    cx += ww + 18.f * k;
  }
}
float Prompt(ImDrawList* dl, const Screen& sc, float x, float cy, std::initializer_list<const char*> keys, const std::string& word) {
  float cx = x;
  for (const char* k : keys) cx += Key(dl, sc, cx + 11.f, cy, k, 1.f) + 2.f;
  Text(dl, sc, cx + 3.f, cy - 11.f, word, kText, 0.85f, 0.f, 1);
  return cx + 6.f + Width(word, 0.85f, 1);
}
void HintBar(ImDrawList* dl, const Screen& sc, const std::string& text) {
  Band(dl, sc, 444.f, 32.f);
  Hints(dl, sc, 40.f, 452.f, text, 560.f);
}

void PictureBox(ImDrawList* dl, const Screen& sc, float x, float y, float w, float h, ImTextureID tex, float tw, float th,
                const std::string& empty = "", bool sel = false) {
  Window(dl, sc, x - 4.f, y - 4.f, x + w + 4.f, y + h + 4.f);
  dl->AddRectFilled(sc.At(x, y), sc.At(x + w, y + h), IM_COL32(0, 0, 0, 255));
  if (tex && tw > 0.f && th > 0.f) {
    const float k = std::min(w / tw, h / th), pw = tw * k, ph = th * k;
    dl->AddImage(tex, sc.At(x + (w - pw) / 2.f, y + (h - ph) / 2.f), sc.At(x + (w + pw) / 2.f, y + (h + ph) / 2.f));
  } else if (!empty.empty()) {
    Para(dl, sc, x + 6.f, y + h / 2.f - 9.f, w - 12.f, empty, kDim, 0.6f, 13.f, 2);
  }
  if (sel) dl->AddRect(sc.At(x - 1.f, y - 1.f), sc.At(x + w + 1.f, y + h + 1.f), IM_COL32(0xFF, 0xD8, 0x68, Pulse()), 2.f * sc.s, 0, 2.f * sc.s);
}
void Face(ImDrawList* dl, const Screen& sc, float cx, float cy, float r, ImTextureID tex) {
  for (int i = 4; i >= 1; i--) dl->AddCircleFilled(sc.At(cx, cy), (r + 2.f * static_cast<float>(i)) * sc.s, IM_COL32(0x7B, 0x3C, 0xFF, 28), 40);
  dl->AddCircleFilled(sc.At(cx, cy), r * sc.s, IM_COL32(0x8C, 0x7C, 0xB8, 255), 40);
  dl->AddCircleFilled(sc.At(cx - r * 0.2f, cy - r * 0.25f), r * 0.7f * sc.s, IM_COL32(0xC4, 0xB8, 0xE6, 120), 40);
  if (tex) dl->AddImageRounded(tex, sc.At(cx - r, cy - r), sc.At(cx + r, cy + r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, r * sc.s);
  dl->AddCircle(sc.At(cx, cy), r * sc.s, IM_COL32(0x7B, 0x3C, 0xFF, 255), 40, 2.f * sc.s);
}

void Ring(ImDrawList* dl, const Screen& sc, float cx, float cy, float r) {
  const float t = Time();
  auto arc = [&](float radius, float turn, float width, float alpha) {
    constexpr int kSeg = 48;
    for (int i = 0; i < kSeg; i++) {
      const float f = static_cast<float>(i) / kSeg;
      if (f < 0.45f) continue;
      const float a0 = turn + f * 6.2831853f, a1 = turn + (f + 1.f / kSeg) * 6.2831853f;
      const float k = (f - 0.45f) / 0.55f;
      const int rr = static_cast<int>(0x7B + (0xE6 - 0x7B) * k * k), gg = static_cast<int>(0x3C + (0xD6 - 0x3C) * k * k);
      dl->AddLine(sc.At(cx + radius * std::cos(a0), cy + radius * std::sin(a0)), sc.At(cx + radius * std::cos(a1), cy + radius * std::sin(a1)),
                  IM_COL32(rr, gg, 0xFF, static_cast<int>(255.f * alpha * k)), width * sc.s);
    }
  };
  for (int i = 3; i >= 1; i--) dl->AddCircle(sc.At(cx, cy), r * sc.s, IM_COL32(0x7B, 0x3C, 0xFF, 22), 48, (2.f + 3.f * i) * sc.s);
  arc(r, t * 6.2831853f / 1.1f, 4.f, 1.f);
  arc(r * 0.62f, -t * 6.2831853f / 2.4f, 2.5f, 0.6f);
  const float p = 0.85f + 0.2f * (0.5f + 0.5f * std::sin(t * 6.2831853f / 1.4f));
  const float cr = r * 0.28f * p;
  dl->AddCircleFilled(sc.At(cx, cy), cr * 1.6f * sc.s, IM_COL32(0x7B, 0x3C, 0xFF, 50), 32);
  dl->AddCircleFilled(sc.At(cx, cy), cr * sc.s, IM_COL32(0x3A, 0x15, 0x80, 255), 32);
  dl->AddCircleFilled(sc.At(cx - cr * 0.25f, cy - cr * 0.25f), cr * 0.65f * sc.s, IM_COL32(0xB4, 0x8C, 0xFF, 255), 32);
  dl->AddCircleFilled(sc.At(cx - cr * 0.35f, cy - cr * 0.35f), cr * 0.25f * sc.s, IM_COL32(0xFF, 0xFF, 0xFF, 230), 16);
}

float Chip(ImDrawList* dl, const Screen& sc, float x, float y, const std::string& t, bool gold = false) {
  const float s = 0.62f, w = Width(t, s) + 18.f, h = 20.f;
  dl->AddRectFilled(sc.At(x, y), sc.At(x + w, y + h), gold ? IM_COL32(0xF2, 0xC1, 0x4E, 50) : IM_COL32(0x7B, 0x3C, 0xFF, 70), h / 2.f * sc.s);
  dl->AddRect(sc.At(x, y), sc.At(x + w, y + h), gold ? IM_COL32(0xF2, 0xC1, 0x4E, 150) : IM_COL32(0xA0, 0x6E, 0xFF, 140), h / 2.f * sc.s, 0,
              1.f * sc.s);
  const uint8_t gc[4] = {0xFF, 0xE7, 0xA8, 0xFF}, pc[4] = {0xF3, 0xEE, 0xFF, 0xFF};
  Text(dl, sc, x + 9.f, y + 2.f, t, gold ? gc : pc, s);
  return w;
}

void FadeIn(ImDrawList* dl, float age, float seconds = 0.2f) {
  if (age < seconds) FillWindow(dl, IM_COL32(0, 0, 0, std::clamp(static_cast<int>(255.f * (1.f - age / seconds)), 0, 255)));
}

bool Notice(const char* text) {
  if (ImGui::GetCurrentContext() == nullptr || text == nullptr) return false;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  std::vector<std::string> lines;
  {
    const UseFont f(0);
    lines = Wrap(text, 540.f);
  }
  if (lines.size() > 5) lines.resize(5);
  const float h = 22.f + 28.f * static_cast<float>(lines.size());
  MessageWindow(dl, sc, 28.f, 20.f, 612.f, 20.f + h);
  for (size_t i = 0; i < lines.size(); i++) Text(dl, sc, 52.f, 32.f + 28.f * static_cast<float>(i), lines[i], kText, 1.f, 0.f, 0);
  return true;
}

struct Age {
  std::chrono::steady_clock::time_point last{};
  float age = 0.f;
  void Reset() { last = {}, age = 0.f; }
  float Tick() {
    const auto now = std::chrono::steady_clock::now();
    if (last.time_since_epoch().count() != 0) age += std::clamp(std::chrono::duration<float>(now - last).count(), 0.f, 0.1f);
    last = now;
    return age;
  }
};

void ListPill(ImDrawList* dl, const Screen& sc, float x0, float x1, float y, bool big = false) {
  const int u = big ? 20 : 47, um = big ? 37 : 62, h = big ? 32 : 28, cap = big ? 16 : 14;
  Spr(dl, sc, 0x16C1, u, 2, cap, h, x0, y, static_cast<float>(cap), static_cast<float>(h));
  Spr(dl, sc, 0x16C1, um, 2, 8, h, x0 + static_cast<float>(cap), y, x1 - x0 - 2.f * static_cast<float>(cap), static_cast<float>(h));
  Spr(dl, sc, 0x16C1, u, 2, cap, h, x1 - static_cast<float>(cap), y, -static_cast<float>(cap), static_cast<float>(h));
  const float by = y + (static_cast<float>(h) - 18.f) / 2.f;
  Spr(dl, sc, 0x16C1, 1, 1, 18, 18, x0 + 3.f, by, 18, 18);
  Spr(dl, sc, 0x16C1, 101, 1, 18, 18, x0 + 3.f, by, 18, 18);
}

constexpr float kNpcX0 = 27.f, kNpcY0 = 290.f, kNpcX1 = 613.f, kNpcY1 = 440.f;
void NpcWindow(ImDrawList* dl, const Screen& sc) {
  if (!Nine(dl, sc, kMsgMenu, kMsgFirst, kMsgLast, kNpcX0, kNpcY0, kNpcX1, kNpcY1, 255, 171)) Window(dl, sc, kNpcX0, kNpcY0, kNpcX1, kNpcY1);
}
std::vector<std::string> NpcLines(const std::string& title, const std::string& text) {
  const UseFont f(0);
  std::vector<std::string> lines;
  if (!title.empty()) lines.push_back(title);
  for (const std::string& l : Wrap(text, 506.f)) lines.push_back(l);
  return lines;
}
int NpcPages(const std::string& title, const std::string& text) {
  const int n = static_cast<int>(NpcLines(title, text).size());
  return std::max(1, (n + 1) / 2);
}
bool NpcMessage(ImDrawList* dl, const Screen& sc, const std::string& title, const std::string& text, int page) {
  NpcWindow(dl, sc);
  const std::vector<std::string> lines = NpcLines(title, text);
  const int pages = std::max(1, (static_cast<int>(lines.size()) + 1) / 2);
  page = std::clamp(page, 0, pages - 1);
  for (int k = 0; k < 2; k++) {
    const size_t li = static_cast<size_t>(page * 2 + k);
    if (li < lines.size()) Text(dl, sc, 59.f, 312.f + 33.f * static_cast<float>(k), lines[li], kText, 1.f, 0.f, 0);
  }
  if (page + 1 < pages) {
    const float bob = 2.f * std::sin(Time() * 6.28f);
    DrawTriangle(dl, sc, 580.f, 420.f + bob, 12.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  }
  return page + 1 >= pages;
}
int ChoiceRowsFit() { return 6; }
void ChoiceBox(ImDrawList* dl, const Screen& sc, const std::vector<std::string>& rows, int cursor, int top) {
  float w = 0.f;
  for (const std::string& r : rows) w = std::max(w, Width(r, 1.f, 1));
  const int shown = std::min<int>(static_cast<int>(rows.size()), ChoiceRowsFit());
  const float bw = std::clamp(w + 100.f, 150.f, 400.f), bh = 23.f + 34.f * static_cast<float>(shown);
  const float x1 = 604.f, x0 = x1 - bw, y1 = kNpcY0 - 9.f, y0 = y1 - bh;
  if (!Nine(dl, sc, kYesNoMenu, kYesNoFirst, kYesNoLast, x0, y0, x1, y1)) Window(dl, sc, x0, y0, x1, y1);
  for (int i = top; i < top + shown && i < static_cast<int>(rows.size()); i++) {
    const float y = y0 + 12.f + 34.f * static_cast<float>(i - top);
    if (i == cursor) ListPill(dl, sc, x0 + 22.f, x1 - 22.f, y, true);
    Text(dl, sc, x0 + 22.f + 34.f, y + 4.f, rows[static_cast<size_t>(i)], kText, 1.f, 0.f, 1);
  }
  if (top > 0) Arrow(dl, sc, x1 - 40.f, y0 + 8.f, true);
  if (top + shown < static_cast<int>(rows.size())) Arrow(dl, sc, x1 - 40.f, y1 - 8.f, false);
}

struct Hero {
  ImTextureID id{};
  float w = 0.f, h = 0.f;
  bool tried = false;
};
const Hero& HeroPicture() {
  static Hero hero;
  if (hero.tried) return hero;
  hero.tried = true;
  std::string path;
  if (const char* e = std::getenv("XD_HERO_PNG")) path = e;
  if (path.empty()) path = std::string(PortUser_FolderUtf8("Pokemon XD")) + "/icons/rendered/michael.png";
  if (SDL_Surface* raw = SDL_LoadPNG(path.c_str())) {
    if (SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32)) {
      hero.id = aurora_imgui_add_texture(static_cast<uint32_t>(rgba->w), static_cast<uint32_t>(rgba->h), rgba->pixels);
      hero.w = static_cast<float>(rgba->w), hero.h = static_cast<float>(rgba->h);
      SDL_DestroySurface(rgba);
    }
    SDL_DestroySurface(raw);
  }
  return hero;
}
void Stage(ImDrawList* dl, const Screen& sc) {
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  Backdrop(dl, sc);
  dl->AddEllipseFilled(sc.At(150.f, 330.f), ImVec2(215.f * sc.s, 175.f * sc.s), IM_COL32(0, 0, 0, 255));
  const Hero& h = HeroPicture();
  if (h.id) dl->AddImage(h.id, sc.At(-55.f, 105.f), sc.At(305.f, 465.f));
}

void BallMark(ImDrawList* dl, const Screen& sc, float cx, float cy, float size = 16.f, uint8_t alpha = 255) {
  const xd::kit2::Image& b = xd::kit2::Texture(3772);
  if (b.ok) {
    dl->AddImage(b.id, sc.At(cx - size / 2.f, cy - size / 2.f), sc.At(cx + size / 2.f, cy + size / 2.f), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, alpha));
  } else {
    dl->AddCircleFilled(sc.At(cx, cy), size / 2.f * sc.s, IM_COL32(0xD8, 0x20, 0x28, alpha), 20);
  }
}

struct NRow {
  std::string label, value, shortValue;
  bool grey = false;
  bool mark = false;
  bool arrows = false;
  bool action = false;
  float indent = 0.f;
};

constexpr float kStageX0 = 232.f, kStageX1 = 628.f, kStageRowH = 38.f;
void StageRow(ImDrawList* dl, const Screen& sc, const NRow& r, float y, bool sel, bool marks) {
  const uint8_t* ink = r.grey ? kDim : kText;
  if (sel) ListPill(dl, sc, kStageX0 + 22.f, kStageX1 - 22.f, y + 3.f, true);
  const float ty = y + 7.f;
  float lx = kStageX0 + (marks ? 84.f : 60.f) + r.indent;
  if (r.mark) BallMark(dl, sc, kStageX0 + 68.f, y + 19.f);
  if (r.action) {
    TextC(dl, sc, kStageX0, ty, kStageX1 - kStageX0, r.label, ink, 1.f, 1);
    return;
  }
  const float vr = kStageX1 - 44.f - (sel && r.arrows ? 22.f : 0.f);
  UseFont rowFont(1);
  const RowLay L = FitRow(r.label.c_str(), r.value, r.shortValue, kStageX1 - 44.f - (r.arrows ? 22.f : 0.f) - lx, r.arrows ? 26.f : 10.f);
  const float vw = L.valueCut ? L.valueRoom : static_cast<float>(TextWidth(L.value.c_str())) * L.valueS * L.scale;
  const float ty2 = ty + (1.f - L.scale) * static_cast<float>(sFontW) / 2.f;
  DrawFitText(dl, sc, lx, ty2, L.labelRoom, L.label, ink, L.labelS, L.scale, L.labelCut, sel, false);
  if (!L.value.empty()) DrawFitText(dl, sc, vr - L.valueRoom, ty2, L.valueRoom, L.value, ink, L.valueS, L.scale, L.valueCut, sel, true);
  if (sel && r.arrows && !L.value.empty()) {
    fit::Art(vr - vw - 16.f, y + 10.f, 10, 17, "left arrow");
    fit::Art(vr + 6.f, y + 10.f, 10, 17, "right arrow");
    Spr(dl, sc, 0x16BE, 2, 15, 10, 17, vr - vw - 16.f, y + 10.f, 10, 17);
    Spr(dl, sc, 0x16BE, 2, 15, 10, 17, vr + 6.f, y + 10.f, -10, 17);
  }
}

void StageDesc(ImDrawList* dl, const Screen& sc, const std::string& desc, float descAge, int forcePage = -1) {
  MessageWindow(dl, sc, 27.f, 372.f, 613.f, 462.f);
  std::vector<std::string> lines;
  {
    const UseFont f(0);
    lines = WrapPaged(desc, 530.f, 500.f, 2);
  }
  fit::Region(52.f, 380.f, 584.f, 440.f, "description box");
  const int pages = std::max(1, (static_cast<int>(lines.size()) + 1) / 2);
  const int page = forcePage >= 0 ? std::min(forcePage, pages - 1) : pages > 1 ? static_cast<int>(descAge / 3.4f) % pages : 0;
  for (int k = 0; k < 2; k++) {
    const size_t li = static_cast<size_t>(page * 2 + k);
    if (li < lines.size()) Text(dl, sc, 52.f, 384.f + 33.f * static_cast<float>(k), lines[li], kText, 1.f, 0.f, 0);
  }
  if (pages > 1) {
    fit::Art(580.f, 440.f, 12.f, 12.f, "more arrow");
    DrawTriangle(dl, sc, 586.f, 446.f + 2.f * std::sin(Time() * 6.28f), 12.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  }
}

void StageList(ImDrawList* dl, const Screen& sc, const std::string& title, const std::vector<NRow>& rows, int cursor, int& first,
               const std::string& desc, float& descAge, float dt, int maxRows = 6) {
  Stage(dl, sc);
  Window(dl, sc, kStageX0, 18.f, kStageX1, 66.f);
  TextC(dl, sc, kStageX0, 28.f, kStageX1 - kStageX0, title, kText, 1.f, 1);
  const int n = static_cast<int>(rows.size()), shown = std::min(n, maxRows);
  first = std::clamp(first, std::max(0, cursor - shown + 1), cursor);
  first = std::clamp(first, 0, std::max(0, n - shown));
  const float top = 76.f, h = 22.f + kStageRowH * static_cast<float>(shown);
  Window(dl, sc, kStageX0, top, kStageX1, top + h);
  bool marks = false;
  for (const NRow& r : rows) marks = marks || r.mark;
  for (int i = first; i < first + shown; i++) {
    StageRow(dl, sc, rows[static_cast<size_t>(i)], top + 11.f + kStageRowH * static_cast<float>(i - first), i == cursor, marks);
  }
  if (first > 0) Arrow(dl, sc, kStageX1 - 36.f, top + 8.f, true);
  if (first + shown < n) Arrow(dl, sc, kStageX1 - 36.f, top + h - 8.f, false);
  descAge += dt;
  StageDesc(dl, sc, desc, descAge);
}

}
