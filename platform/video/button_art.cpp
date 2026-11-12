#include "port_button_art.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace port_button_art {

namespace {

constexpr int kScale = 4;

struct Color {
  float r, g, b, a;
};
constexpr Color Rgb(int r, int g, int b, int a = 255) {
  return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
}

constexpr Color kRim = Rgb(8, 8, 8);
constexpr Color kWhite = Rgb(244, 244, 244);
constexpr Color kKeyText = Rgb(28, 28, 30);

class Canvas {
 public:
  Canvas(int w, int h) : w_(w * kScale), h_(h * kScale), px_(static_cast<size_t>(w_) * h_ * 4, 0.f) {}
  int W() const { return w_; }
  int H() const { return h_; }

  template <typename F>
  void Fill(const Color& c, F&& coverage) {
    for (int y = 0; y < h_; y++) {
      const float fy = (static_cast<float>(y) + 0.5f) / kScale;
      for (int x = 0; x < w_; x++) {
        const float fx = (static_cast<float>(x) + 0.5f) / kScale;
        const float k = coverage(fx, fy);
        if (k <= 0.f) continue;
        Blend(x, y, c, std::min(k, 1.f));
      }
    }
  }

  template <typename F>
  void FillGradient(const Color& top, const Color& bottom, float y0, float y1, F&& coverage) {
    for (int y = 0; y < h_; y++) {
      const float fy = (static_cast<float>(y) + 0.5f) / kScale;
      const float t = std::clamp(y1 > y0 ? (fy - y0) / (y1 - y0) : 0.f, 0.f, 1.f);
      const Color c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t,
                    top.a + (bottom.a - top.a) * t};
      for (int x = 0; x < w_; x++) {
        const float fx = (static_cast<float>(x) + 0.5f) / kScale;
        const float k = coverage(fx, fy);
        if (k <= 0.f) continue;
        Blend(x, y, c, std::min(k, 1.f));
      }
    }
  }

  void Blend(int x, int y, const Color& c, float k) {
    float* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
    const float a = c.a * k;
    p[0] = c.r * a + p[0] * (1.f - a);
    p[1] = c.g * a + p[1] * (1.f - a);
    p[2] = c.b * a + p[2] * (1.f - a);
    p[3] = a + p[3] * (1.f - a);
  }

  void Resolve(uint8_t* rgba, int stride, int x0, int y0) const {
    const int w = w_ / kScale, h = h_ / kScale;
    for (int y = 0; y < h; y++) {
      uint8_t* row = rgba + static_cast<size_t>(y0 + y) * stride + static_cast<size_t>(x0) * 4;
      for (int x = 0; x < w; x++) {
        float s[4] = {0, 0, 0, 0};
        for (int sy = 0; sy < kScale; sy++) {
          for (int sx = 0; sx < kScale; sx++) {
            const float* p = &px_[(static_cast<size_t>(y * kScale + sy) * w_ + x * kScale + sx) * 4];
            for (int i = 0; i < 4; i++) s[i] += p[i];
          }
        }
        const float n = static_cast<float>(kScale * kScale);
        const float a = s[3] / n;
        uint8_t* o = row + static_cast<size_t>(x) * 4;
        for (int i = 0; i < 3; i++) {
          const float v = a > 0.f ? s[i] / n / a : 0.f;
          o[i] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
        }
        o[3] = static_cast<uint8_t>(std::lround(std::clamp(a, 0.f, 1.f) * 255.f));
      }
    }
  }

 private:
  int w_, h_;
  std::vector<float> px_;
};

auto Disc(float cx, float cy, float r) {
  return [=](float x, float y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r ? 1.f : 0.f; };
}
auto RoundRect(float x0, float y0, float x1, float y1, float rad) {
  return [=](float x, float y) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return 0.f;
    const float cx = std::clamp(x, x0 + rad, x1 - rad), cy = std::clamp(y, y0 + rad, y1 - rad);
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad ? 1.f : 0.f;
  };
}
auto Line(float ax, float ay, float bx, float by, float half) {
  return [=](float x, float y) {
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.f ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float px = ax + t * dx - x, py = ay + t * dy - y;
    return px * px + py * py <= half * half ? 1.f : 0.f;
  };
}
auto Ring(float cx, float cy, float r, float half) {
  return [=](float x, float y) {
    const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
    return std::fabs(d - r) <= half ? 1.f : 0.f;
  };
}
auto Triangle(float ax, float ay, float bx, float by, float cx, float cy) {
  return [=](float x, float y) {
    const float d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
    const float d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy);
    const float d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos) ? 1.f : 0.f;
  };
}

struct Ink {
  std::vector<uint8_t> a;
  int w = 0, h = 0;
};

Ink Typeset(std::string_view text, const Font* font) {
  Ink ink;
  if (font == nullptr || !font->glyph || text.empty()) return ink;
  struct G {
    std::vector<uint8_t> a;
    int w = 0, h = 0;
  };
  std::vector<G> glyphs;
  int width = 0, height = 0;
  for (char ch : text) {
    G g;
    if (!font->glyph(static_cast<unsigned char>(ch), g.a, g.w, g.h) || g.w <= 0 || g.h <= 0) {
      g = {};
      g.w = ch == ' ' ? 6 : 0;
    }
    width += g.w + (glyphs.empty() ? 0 : 1);
    height = std::max(height, g.h);
    glyphs.push_back(std::move(g));
  }
  if (width <= 0 || height <= 0) return ink;
  std::vector<uint8_t> all(static_cast<size_t>(width) * height, 0);
  int pen = 0;
  for (const G& g : glyphs) {
    for (int y = 0; y < g.h; y++) {
      for (int x = 0; x < g.w; x++) {
        all[static_cast<size_t>(y) * width + pen + x] = g.a[static_cast<size_t>(y) * g.w + x];
      }
    }
    pen += g.w + 1;
  }
  int x0 = width, y0 = height, x1 = -1, y1 = -1;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (all[static_cast<size_t>(y) * width + x] > 24) {
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) return ink;
  ink.w = x1 - x0 + 1, ink.h = y1 - y0 + 1;
  ink.a.resize(static_cast<size_t>(ink.w) * ink.h);
  for (int y = 0; y < ink.h; y++) {
    for (int x = 0; x < ink.w; x++) {
      ink.a[static_cast<size_t>(y) * ink.w + x] = all[static_cast<size_t>(y + y0) * width + x + x0];
    }
  }
  return ink;
}

void Text(Canvas& cv, std::string_view text, const Font* font, float cx, float cy, float boxW, float maxH,
          const Color& color, float outline, const Color& outlineColor) {
  const Ink ink = Typeset(text, font);
  if (ink.w == 0) return;
  const float s = std::min(boxW / static_cast<float>(ink.w), maxH / static_cast<float>(ink.h));
  const float w = ink.w * s, h = ink.h * s, x0 = cx - w / 2.f, y0 = cy - h / 2.f;
  auto sample = [&](float x, float y) {
    const float u = (x - x0) / s, v = (y - y0) / s;
    if (u < -1.f || v < -1.f || u > ink.w || v > ink.h) return 0.f;
    const float fu = u - 0.5f, fv = v - 0.5f;
    const int iu = static_cast<int>(std::floor(fu)), iv = static_cast<int>(std::floor(fv));
    const float tu = fu - iu, tv = fv - iv;
    auto at = [&](int px, int py) {
      if (px < 0 || py < 0 || px >= ink.w || py >= ink.h) return 0.f;
      return ink.a[static_cast<size_t>(py) * ink.w + px] / 255.f;
    };
    const float a = at(iu, iv) * (1 - tu) + at(iu + 1, iv) * tu;
    const float b = at(iu, iv + 1) * (1 - tu) + at(iu + 1, iv + 1) * tu;
    return a * (1 - tv) + b * tv;
  };
  if (outline > 0.f) {
    cv.Fill(outlineColor, [&](float x, float y) {
      float m = 0.f;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          m = std::max(m, sample(x + dx * outline, y + dy * outline));
        }
      }
      return m;
    });
  }
  cv.Fill(color, sample);
}

std::string Upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

bool StartsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

std::string KeyCapText(const engine::controls::ButtonLabel& label) {
  const std::string_view token = std::string_view(label.glyph).substr(4);
  struct Short {
    std::string_view token, text;
  };
  static constexpr Short kShort[] = {
      {"enter", "ENT"},      {"escape", "ESC"},     {"backspace", "BS"}, {"space", "SPC"},  {"capslock", "CAPS"},
      {"pageup", "PGUP"},    {"pagedown", "PGDN"},  {"insert", "INS"},   {"delete", "DEL"}, {"home", "HOME"},
      {"end", "END"},        {"lctrl", "CTRL"},     {"rctrl", "CTRL"},   {"lshift", "SHFT"}, {"rshift", "SHFT"},
      {"lalt", "ALT"},       {"ralt", "ALT"},       {"tab", "TAB"},      {"kp_enter", "ENT"},
  };
  for (const Short& s : kShort) {
    if (token == s.token) return std::string(s.text);
  }
  if (StartsWith(token, "kp_")) return Upper(label.text.size() > 4 ? label.text.substr(4) : label.text);
  return Upper(label.text);
}

int KeyArrow(const engine::controls::ButtonLabel& label) {
  const std::string_view g = label.glyph;
  if (g == "key_up") return 1;
  if (g == "key_down") return 2;
  if (g == "key_left") return 3;
  if (g == "key_right") return 4;
  return 0;
}

void DrawSymbolOnce(Canvas& cv, std::string_view glyph, float cx, float cy, float r, float t, const Color* only);
void DrawSymbol(Canvas& cv, std::string_view glyph, float cx, float cy, float r) {
  const float t = std::max(r * 0.13f, 0.6f);
  const Color dark = kRim;
  DrawSymbolOnce(cv, glyph, cx, cy, r, t + std::max(r * 0.07f, 0.5f), &dark);
  DrawSymbolOnce(cv, glyph, cx, cy, r, t, nullptr);
}

void DrawSymbolOnce(Canvas& cv, std::string_view glyph, float cx, float cy, float r, float t, const Color* only) {
  if (glyph == "ps_cross") {
    const float d = r * 0.42f;
    const Color c = only ? *only : Rgb(124, 166, 232);
    cv.Fill(c, Line(cx - d, cy - d, cx + d, cy + d, t));
    cv.Fill(c, Line(cx - d, cy + d, cx + d, cy - d, t));
  } else if (glyph == "ps_circle") {
    cv.Fill(only ? *only : Rgb(234, 96, 104), Ring(cx, cy, r * 0.44f, t));
  } else if (glyph == "ps_square") {
    const float d = r * 0.4f;
    const Color c = only ? *only : Rgb(222, 140, 204);
    cv.Fill(c, Line(cx - d, cy - d, cx + d, cy - d, t));
    cv.Fill(c, Line(cx + d, cy - d, cx + d, cy + d, t));
    cv.Fill(c, Line(cx + d, cy + d, cx - d, cy + d, t));
    cv.Fill(c, Line(cx - d, cy + d, cx - d, cy - d, t));
  } else if (glyph == "ps_triangle") {
    const float d = r * 0.48f;
    const Color c = only ? *only : Rgb(84, 204, 172);
    const float ax = cx, ay = cy - d * 0.95f, bx = cx + d * 0.87f, by = cy + d * 0.55f, qx = cx - d * 0.87f,
                qy = cy + d * 0.55f;
    cv.Fill(c, Line(ax, ay, bx, by, t));
    cv.Fill(c, Line(bx, by, qx, qy, t));
    cv.Fill(c, Line(qx, qy, ax, ay, t));
  }
}

struct Fill2 {
  Color top, bottom;
};
Fill2 FaceFill(std::string_view glyph) {
  if (glyph == "xbox_a") return {Rgb(104, 192, 70), Rgb(46, 126, 34)};
  if (glyph == "xbox_b") return {Rgb(226, 74, 62), Rgb(150, 28, 28)};
  if (glyph == "xbox_x") return {Rgb(76, 136, 228), Rgb(30, 72, 164)};
  if (glyph == "xbox_y") return {Rgb(236, 196, 58), Rgb(178, 128, 18)};
  return {Rgb(178, 178, 184), Rgb(104, 104, 110)};
}
constexpr Fill2 kSymbolFill{Rgb(92, 92, 100), Rgb(38, 38, 44)};
constexpr Fill2 kGreyFill{Rgb(178, 178, 184), Rgb(104, 104, 110)};
constexpr Fill2 kPlateFill{Rgb(136, 136, 142), Rgb(76, 76, 82)};
constexpr Fill2 kBarFill{Rgb(104, 104, 108), Rgb(58, 58, 62)};
constexpr Fill2 kKeyFill{Rgb(240, 240, 236), Rgb(196, 196, 190)};

}

Kind KindOf(const engine::controls::ButtonLabel& label) {
  const std::string_view g = label.glyph;
  if (g.empty()) return Kind::None;
  if (StartsWith(g, "key_")) return Kind::Key;
  if (g == "ps_cross" || g == "ps_circle" || g == "ps_square" || g == "ps_triangle") return Kind::Symbol;
  if (StartsWith(label.text, "D-pad")) return Kind::Dpad;
  if (label.text.size() == 1 && (label.text[0] == 'A' || label.text[0] == 'B' || label.text[0] == 'X' ||
                                 label.text[0] == 'Y')) {
    return Kind::Face;
  }
  return Kind::Plate;
}

namespace {

struct Shape {
  bool round = true;
  float cx = 0, cy = 0, r = 0;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  float rad = 0;
  float Top() const { return round ? cy - r : y0; }
  float Bottom() const { return round ? cy + r : y1; }
  bool Inside(float x, float y, float inset, float dx = 0.f, float dy = 0.f) const {
    x -= dx, y -= dy;
    if (round) {
      const float rr = r - inset;
      return rr > 0.f && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr;
    }
    const float ax = x0 + inset, ay = y0 + inset, bx = x1 - inset, by = y1 - inset;
    if (x < ax || x > bx || y < ay || y > by) return false;
    const float rr = std::max(rad - inset, 0.f);
    const float px = std::clamp(x, ax + rr, bx - rr), py = std::clamp(y, ay + rr, by - rr);
    return (x - px) * (x - px) + (y - py) * (y - py) <= rr * rr;
  }
};

void Body(Canvas& cv, const Shape& s, float shadow, float rim, const Fill2& fill) {
  cv.Fill(Rgb(0, 0, 0, 120), [&](float x, float y) { return s.Inside(x, y, 0.f, shadow * 0.8f, shadow) ? 1.f : 0.f; });
  cv.Fill(kRim, [&](float x, float y) { return s.Inside(x, y, 0.f) ? 1.f : 0.f; });
  cv.FillGradient(fill.top, fill.bottom, s.Top() + rim, s.Bottom() - rim,
                  [&](float x, float y) { return s.Inside(x, y, rim) ? 1.f : 0.f; });
  const float mid = (s.Top() + s.Bottom()) / 2.f;
  cv.Fill(Rgb(255, 255, 255, 90), [&](float x, float y) {
    return y < mid && s.Inside(x, y, rim) && !s.Inside(x, y, rim + std::max(rim * 0.6f, 0.6f), 0.f, 0.6f) ? 1.f : 0.f;
  });
}

}

void Draw(const engine::controls::ButtonLabel& label, uint8_t* rgba, int stride, int x, int y, int w, int h,
          const Font* font, bool withShadow) {
  if (w <= 0 || h <= 0) return;
  for (int row = 0; row < h; row++) {
    std::memset(rgba + static_cast<size_t>(y + row) * stride + static_cast<size_t>(x) * 4, 0,
                static_cast<size_t>(w) * 4);
  }
  Canvas cv(w, h);
  const float fw = static_cast<float>(w), fh = static_cast<float>(h);
  const float shadow = withShadow ? std::clamp(std::min(fw, fh) * 0.08f, 1.f, 2.f) : 0.f;
  const float bx0 = 0.4f, by0 = 0.4f, bx1 = fw - 0.4f - shadow, by1 = fh - 0.4f - shadow;
  const float bw = bx1 - bx0, bh = by1 - by0, bcx = (bx0 + bx1) / 2.f, bcy = (by0 + by1) / 2.f;
  const float rim = std::clamp(std::min(bw, bh) * 0.07f, 0.8f, 1.6f);
  const float outline = std::max(std::min(bw, bh) * 0.045f, 0.55f);
  const std::string_view glyph = label.glyph;
  Shape disc;
  disc.round = true, disc.cx = bcx, disc.cy = bcy, disc.r = std::min(bw, bh) / 2.f;

  switch (KindOf(label)) {
  case Kind::None: break;
  case Kind::Face: {
    Body(cv, disc, shadow, rim, FaceFill(glyph));
    const float inner = disc.r - rim;
    Text(cv, label.text, font, bcx, bcy + 0.1f, inner * 1.35f, inner * 1.38f, kWhite, outline, kRim);
    break;
  }
  case Kind::Symbol: {
    Body(cv, disc, shadow, rim, kSymbolFill);
    DrawSymbol(cv, glyph, bcx, bcy, disc.r - rim);
    break;
  }
  case Kind::Dpad: {
    const float arm = std::min(bw, bh) / 2.f, half = arm * 0.36f;
    Shape hbar, vbar;
    hbar.round = vbar.round = false;
    hbar.x0 = bcx - arm, hbar.x1 = bcx + arm, hbar.y0 = bcy - half, hbar.y1 = bcy + half, hbar.rad = half * 0.35f;
    vbar.x0 = bcx - half, vbar.x1 = bcx + half, vbar.y0 = bcy - arm, vbar.y1 = bcy + arm, vbar.rad = half * 0.35f;
    auto cross = [&](float px, float py, float inset, float dx, float dy) {
      return hbar.Inside(px, py, inset, dx, dy) || vbar.Inside(px, py, inset, dx, dy);
    };
    cv.Fill(Rgb(0, 0, 0, 120), [&](float px, float py) { return cross(px, py, 0.f, shadow * 0.8f, shadow) ? 1.f : 0.f; });
    cv.Fill(kRim, [&](float px, float py) { return cross(px, py, 0.f, 0.f, 0.f) ? 1.f : 0.f; });
    cv.FillGradient(kGreyFill.top, kGreyFill.bottom, bcy - arm, bcy + arm,
                    [&](float px, float py) { return cross(px, py, rim, 0.f, 0.f) ? 1.f : 0.f; });
    const std::string_view t = label.text;
    const bool left = t.find("Left") != std::string_view::npos, right = t.find("Right") != std::string_view::npos;
    const bool up = t.find("Up") != std::string_view::npos, down = t.find("Down") != std::string_view::npos;
    const float lx0 = left ? bcx - arm : right ? bcx + half : bcx - half;
    const float lx1 = left ? bcx - half : right ? bcx + arm : bcx + half;
    const float ly0 = up ? bcy - arm : down ? bcy + half : bcy - half;
    const float ly1 = up ? bcy - half : down ? bcy + arm : bcy + half;
    cv.FillGradient(Rgb(214, 70, 64), Rgb(150, 34, 34), ly0, ly1, [&](float px, float py) {
      return px >= lx0 && px <= lx1 && py >= ly0 && py <= ly1 && cross(px, py, rim, 0.f, 0.f) ? 1.f : 0.f;
    });
    break;
  }
  case Kind::Plate: {
    const bool wide = fw >= 2.5f * fh;
    Shape plate;
    plate.round = false;
    const float ph = fw >= 1.6f * fh ? bh : bh * 0.8f;
    plate.x0 = bx0, plate.x1 = bx1, plate.y0 = bcy - ph / 2.f, plate.y1 = bcy + ph / 2.f, plate.rad = ph * 0.48f;
    Body(cv, plate, shadow, rim, wide ? kBarFill : kPlateFill);
    std::string text = label.text;
    if (text == "Menu" || text == "Options" || text == "View" || text == "Share") {
      text = wide ? Upper(text) : Upper(text).substr(0, 4);
    }
    if (text == "Plus") text = "+";
    if (text == "Minus") text = "-";
    if (text.find("stick") != std::string::npos) text = text[0] == 'L' ? "LS" : "RS";
    const float innerH = ph - 2.f * rim;
    float tx0 = plate.x0 + rim + plate.rad * 0.25f, tx1 = plate.x1 - rim - plate.rad * 0.25f;
    if (wide) {
      const float dotR = innerH * 0.32f, dotX = plate.x0 + rim + plate.rad * 0.55f + dotR * 0.2f;
      cv.Fill(Rgb(30, 30, 32), Disc(dotX, bcy, dotR + 0.6f));
      cv.FillGradient(Rgb(176, 176, 180), Rgb(120, 120, 124), bcy - dotR, bcy + dotR, Disc(dotX, bcy, dotR));
      tx0 = dotX + dotR + innerH * 0.25f;
    }
    const float textH = innerH * 0.8f;
    Text(cv, text, font, (tx0 + tx1) / 2.f, bcy, tx1 - tx0, textH, kWhite, wide || textH < 9.f ? 0.f : outline * 0.7f,
         kRim);
    break;
  }
  case Kind::Key: {
    Shape cap;
    cap.round = false;
    cap.x0 = bx0, cap.x1 = bx1, cap.y0 = by0, cap.y1 = by1, cap.rad = std::min(bw, bh) * 0.2f;
    const std::string capText = fw >= 2.5f * fh ? Upper(label.text) : KeyCapText(label);
    if ((capText.size() <= 1 || KeyArrow(label) != 0) && bw > bh * 1.2f) {
      cap.x0 = bcx - bh * 0.6f, cap.x1 = bcx + bh * 0.6f;
    }
    Body(cv, cap, shadow, rim, {Rgb(150, 150, 146), Rgb(120, 120, 116)});
    const float edge = std::max(bh * 0.13f, 1.f);
    Shape face = cap;
    face.x0 += rim, face.y0 += rim, face.x1 -= rim, face.y1 -= rim + edge, face.rad = std::max(cap.rad - rim, 0.5f);
    cv.FillGradient(kKeyFill.top, kKeyFill.bottom, face.y0, face.y1,
                    [&](float px, float py) { return face.Inside(px, py, 0.f) ? 1.f : 0.f; });
    const float faceCx = (face.x0 + face.x1) / 2.f, faceCy = (face.y0 + face.y1) / 2.f;
    const float faceW = face.x1 - face.x0, faceH = face.y1 - face.y0;
    if (const int arrow = KeyArrow(label)) {
      const float d = std::min(faceW, faceH) * 0.3f;
      float ax = faceCx, ay = faceCy, bx = faceCx, by = faceCy, qx = faceCx, qy = faceCy;
      switch (arrow) {
      case 1: ay -= d, bx -= d, by += d * 0.7f, qx += d, qy += d * 0.7f; break;
      case 2: ay += d, bx -= d, by -= d * 0.7f, qx += d, qy -= d * 0.7f; break;
      case 3: ax -= d, by -= d, bx += d * 0.7f, qy += d, qx += d * 0.7f; break;
      default: ax += d, by -= d, bx -= d * 0.7f, qy += d, qx -= d * 0.7f; break;
      }
      cv.Fill(kKeyText, Triangle(ax, ay, bx, by, qx, qy));
    } else {
      const float maxH = faceH * (capText.size() == 1 ? 0.8f : 0.56f);
      Text(cv, capText, font, faceCx, faceCy, faceW * 0.84f, maxH, kKeyText, 0.f, kRim);
    }
    break;
  }
  }
  cv.Resolve(rgba, stride, x, y);
}

}
