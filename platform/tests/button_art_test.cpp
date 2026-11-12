


#include <port_button_art.h>

#include <cstdio>
#include <cstdint>
#include <vector>

namespace ec = engine::controls;
namespace ba = port_button_art;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)


static int sGlyphCalls = 0;
static bool BlockGlyph(char32_t ch, std::vector<uint8_t>& alpha, int& w, int& h) {
  sGlyphCalls++;
  if (ch == ' ') return false;
  w = 6, h = 10;
  alpha.assign(static_cast<size_t>(w) * h, 255);
  return true;
}

struct Sheet {
  int w, h;
  std::vector<uint8_t> px;
  Sheet(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_ * 4) {
    for (size_t i = 0; i < px.size(); i++) px[i] = static_cast<uint8_t>(i * 7 + 3);
  }
  const uint8_t* At(int x, int y) const { return &px[(static_cast<size_t>(y) * w + x) * 4]; }
};


static Sheet DrawOne(const ec::ButtonLabel& label, const ba::Font* font, int rw = 24, int rh = 24) {
  Sheet s(40 + rw, 36 + rh);
  const Sheet before = s;
  ba::Draw(label, s.px.data(), s.w * 4, 8, 6, rw, rh, font);
  bool outsideSame = true, insideOpaque = false;
  for (int y = 0; y < s.h; y++) {
    for (int x = 0; x < s.w; x++) {
      const bool inside = x >= 8 && x < 8 + rw && y >= 6 && y < 6 + rh;
      for (int c = 0; c < 4; c++) {
        if (!inside && s.At(x, y)[c] != before.At(x, y)[c]) outsideSame = false;
      }
      if (inside && s.At(x, y)[3] == 255) insideOpaque = true;
    }
  }
  CHECK(outsideSame);
  CHECK(insideOpaque);

  CHECK(s.At(8, 6)[3] < 255);
  return s;
}

int main() {
  ba::Font font;
  font.glyph = BlockGlyph;


  CHECK(ba::KindOf({"A", "xbox_a"}) == ba::Kind::Face);
  CHECK(ba::KindOf({"B", "switch_b"}) == ba::Kind::Face);
  CHECK(ba::KindOf({"Cross", "ps_cross"}) == ba::Kind::Symbol);
  CHECK(ba::KindOf({"Triangle", "ps_triangle"}) == ba::Kind::Symbol);
  CHECK(ba::KindOf({"RT", "xbox_rt"}) == ba::Kind::Plate);
  CHECK(ba::KindOf({"Menu", "xbox_menu"}) == ba::Kind::Plate);
  CHECK(ba::KindOf({"D-pad Up", "xbox_d_pad_up"}) == ba::Kind::Dpad);
  CHECK(ba::KindOf({"Enter", "key_enter"}) == ba::Kind::Key);
  CHECK(ba::KindOf({"X", "key_x"}) == ba::Kind::Key);
  CHECK(ba::KindOf({}) == ba::Kind::None);


  const ec::PortMap map = ec::DefaultPort(0);
  for (ec::Device d : {ec::Device::Xbox, ec::Device::PlayStation, ec::Device::Switch, ec::Device::Generic,
                       ec::Device::Keyboard}) {
    for (ec::GcInput in : {ec::GcInput::A, ec::GcInput::B, ec::GcInput::X, ec::GcInput::Y, ec::GcInput::Z,
                           ec::GcInput::L, ec::GcInput::R, ec::GcInput::Start, ec::GcInput::DpadUp,
                           ec::GcInput::StickUp, ec::GcInput::CStickUp}) {
      const ec::ButtonLabel label = ec::GcInputLabel(map, in, d);
      CHECK(ba::KindOf(label) != ba::Kind::None);
      DrawOne(label, &font);
    }
  }


  sGlyphCalls = 0;
  const Sheet withFont = DrawOne({"A", "xbox_a"}, &font);
  CHECK(sGlyphCalls > 0);
  const Sheet noFont = DrawOne({"A", "xbox_a"}, nullptr);
  int green = 0, white = 0, whiteNoFont = 0;
  for (int y = 6; y < 30; y++) {
    for (int x = 8; x < 32; x++) {
      const uint8_t* p = withFont.At(x, y);
      const uint8_t* q = noFont.At(x, y);
      if (p[1] > 120 && p[0] < 140 && p[2] < 110) green++;
      if (p[0] > 220 && p[1] > 220 && p[2] > 220 && p[3] == 255) white++;
      if (q[0] > 220 && q[1] > 220 && q[2] > 220 && q[3] == 255) whiteNoFont++;
    }
  }
  CHECK(green > 40);
  CHECK(white > 10);
  CHECK(whiteNoFont == 0);


  CHECK(withFont.At(8 + 23, 6 + 12)[3] < 200);


  const Sheet key = DrawOne({"X", "key_x"}, &font);
  CHECK(key.At(8 + 4, 6 + 8)[0] > 180);

  DrawOne({"Up Arrow", "key_up"}, nullptr);
  DrawOne({"Enter", "key_enter"}, &font, 84, 20);
  DrawOne({"Menu", "xbox_menu"}, &font, 84, 20);


  const Sheet again = DrawOne({"A", "xbox_a"}, &font);
  CHECK(again.px == withFont.px);

  if (sFailures == 0) std::printf("button_art_test: all passed\n");
  return sFailures == 0 ? 0 : 1;
}
