#include <dolphin/gx.h>
#include <aurora/texture.hpp>
#include <controls.h>
#include <port_button_art.h>
#include <port_controls.h>
#include <settings.h>

#include <dolphin/gx/GXEnum.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

extern "C" uint8_t* PortXD_GetDolTable(const char* name);
extern "C" uint32_t PortVI_GetFlipCount(void);

namespace {

namespace ec = engine::controls;
namespace at = aurora::texture;
using ec::GcInput;

struct Piece {
  uint16_t x, y, w, h;
  GcInput input;
  bool over = false;
};
struct Atlas {
  const char* name;
  uint32_t width, height, format;
  uint64_t textureHash, tlutHash;
  std::vector<Piece> pieces;
};
const std::vector<Atlas>& Atlases() {
  static const std::vector<Atlas> kAtlases = {
      {"menu help sheet (summary, bag)", 81, 55, 9, 0xce9c1876be60b66full, 0x8541bb6cb2db0abbull,
       {{2, 2, 24, 24, GcInput::X}, {55, 2, 24, 24, GcInput::R}, {2, 28, 24, 24, GcInput::A},
        {29, 28, 24, 24, GcInput::B}}},
      {"button sheet", 242, 54, 9, 0xc7bbd6ae01cc8a7dull, 0xc2951e6750f14ac8ull,
       {{2, 2, 84, 20, GcInput::Start}, {88, 2, 24, 19, GcInput::L}, {115, 2, 24, 19, GcInput::R},
        {141, 2, 28, 28, GcInput::DpadUp}, {171, 2, 28, 28, GcInput::DpadLeft}, {3, 25, 24, 24, GcInput::A},
        {30, 26, 22, 22, GcInput::B}, {54, 26, 23, 22, GcInput::X}, {79, 26, 23, 22, GcInput::Y},
        {106, 25, 24, 24, GcInput::StickUp}, {175, 35, 34, 16, GcInput::Z}}},
      {"P*DA sheet", 512, 230, 9, 0xf8bf80969f7d7848ull, 0x10305c58136db808ull,
       {{278, 152, 22, 18, GcInput::L}, {278, 174, 22, 18, GcInput::R}, {363, 147, 23, 22, GcInput::CStickUp},
        {363, 169, 23, 22, GcInput::A}}},
      {"PC box sheet", 516, 162, 9, 0x196c5c4983995e96ull, 0x1c033dfe76a765afull,
       {{467, 2, 45, 29, GcInput::L, true}, {466, 34, 45, 29, GcInput::R, true}}},
      {"Purify Chamber sheet", 450, 207, 9, 0xc96e96cab44e06ebull, 0x0deb33024a9fef3dull,
       {{269, 37, 24, 19, GcInput::L, true}, {334, 37, 24, 19, GcInput::R, true}}},
      {"battle command sheet", 78, 55, 9, 0x8641347219b4738cull, 0xe49796ec497f397bull,
       {{1, 1, 72, 17, GcInput::Start}, {0, 20, 24, 17, GcInput::R}, {36, 20, 18, 17, GcInput::CStickUp},
        {56, 20, 21, 17, GcInput::L}, {38, 38, 16, 17, GcInput::X}}},
      {"name entry sheet", 371, 240, 6, 0x391062ebb460d03dull, 0,
       {{110, 133, 31, 39, GcInput::X}, {4, 174, 34, 33, GcInput::B}, {55, 187, 20, 20, GcInput::Start}}},
  };
  return kAtlases;
}

const Atlas* AtlasOf(const at::TextureSourceKey& key) {
  for (const Atlas& a : Atlases()) {
    if (a.width == key.width && a.height == key.height && a.format == key.format && a.textureHash == key.textureHash &&
        (!key.hasTlut || a.tlutHash == key.tlutHash)) {
      return &a;
    }
  }
  return nullptr;
}

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool GameGlyph(char32_t ch, std::vector<uint8_t>& alpha, int& w, int& h) {
  for (const char* table : {"resident_fnt", "common_fnt"}) {
    const uint8_t* blk = PortXD_GetDolTable(table);
    for (int guard = 0; blk != nullptr && guard < 64; guard++) {
      if (Be16(blk) == 0xFFFF) break;
      if (Be16(blk) == 0) {
        const uint8_t* info = blk + 8;
        const uint16_t count = Be16(info);
        const uint32_t dataOff = Be32(info + 4);
        uint32_t lo = 0, hi = count;
        while (lo < hi) {
          const uint32_t mid = (lo + hi) / 2;
          const uint8_t* e = info + 16 + mid * 8;
          const uint16_t c = Be16(e);
          if (c == ch) {
            w = e[2], h = e[3];
            const uint8_t* src = info + dataOff + (Be32(e + 4) & 0xFFFFFF);
            const int rowBytes = ((w + 1) & ~1) / 2;
            alpha.assign(static_cast<size_t>(w) * h, 0);
            for (int y = 0; y < h; y++) {
              for (int x = 0; x < w; x++) {
                const uint8_t b = src[y * rowBytes + x / 2];
                alpha[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(((x & 1) ? (b & 15) : (b >> 4)) * 17);
              }
            }
            return w > 0 && h > 0;
          }
          if (c < ch) lo = mid + 1;
          else hi = mid;
        }
      }
      const uint32_t next = Be32(blk + 4);
      if (next == 0) break;
      blk += next;
    }
  }
  return false;
}

struct Sheet {
  const Atlas* atlas = nullptr;
  at::TextureSourceKey key;
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> original;
  std::vector<uint8_t> painted;
  at::ReplacementRegistration registration;
  uint32_t style = 0;
};

std::mutex sMutex;
std::vector<Sheet> sSheets;
std::array<ec::ButtonLabel, ec::kGcInputCount> sLabels;
uint32_t sStyle = 0;
bool sActive = false;
port_button_art::Font sFont;

void Paint(Sheet& s) {
  if (s.registration.id != 0) {
    at::unregister_replacement(s.registration);
    s.registration = {};
  }
  s.painted = s.original;
  for (const Piece& p : s.atlas->pieces) {
    if (p.x + p.w > s.w || p.y + p.h > s.h) continue;
    const ec::ButtonLabel& label = sLabels[static_cast<size_t>(p.input)];
    if (port_button_art::KindOf(label) == port_button_art::Kind::None) continue;
    if (!p.over) {
      auto at = [&](int x, int y) { return &s.painted[(static_cast<size_t>(p.y + y) * s.w + p.x + x) * 4]; };
      auto lum = [](const uint8_t* c) { return (c[0] * 3 + c[1] * 6 + c[2]) / 10; };
      int x0 = p.w, y0 = p.h, x1 = -1, y1 = -1;
      int dark[3] = {0, 0, 0}, darkN = 0;
      for (int y = 0; y < p.h; y++) {
        for (int x = 0; x < p.w; x++) {
          const uint8_t* c = at(x, y);
          if (c[3] < 128) continue;
          if (lum(c) > 56) {
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
          } else if (std::max({c[0], c[1], c[2]}) < 40) {
            for (int k = 0; k < 3; k++) dark[k] += c[k];
            darkN++;
          }
        }
      }
      int outside = 0;
      for (int y = 0; y < p.h && x1 >= 0; y++) {
        for (int x = 0; x < p.w; x++) {
          const uint8_t* c = at(x, y);
          if (c[3] > 40 && lum(c) <= 56 && (x < x0 - 1 || x > x1 + 1 || y < y0 - 1 || y > y1 + 1)) outside++;
        }
      }
      const bool backing = x1 >= 0 && darkN > 0 && outside >= 12;
      if (std::getenv("XD_BUTTON_ICONS_LOG") != nullptr) {
        std::fprintf(stderr, "[button icons] %s piece %d,%d: body %d,%d-%d,%d, backing outside %d -> %s\n",
                     s.atlas->name, p.x, p.y, x0, y0, x1, y1, outside, backing ? "backing kept" : "replaced");
      }
      if (!backing) {
        port_button_art::Draw(label, s.painted.data(), static_cast<int>(s.w) * 4, p.x, p.y, p.w, p.h, &sFont);
        continue;
      }
      for (int y = 0; y < p.h; y++) {
        for (int x = 0; x < p.w; x++) {
          uint8_t* c = at(x, y);
          for (int k = 0; k < 3; k++) c[k] = static_cast<uint8_t>(dark[k] / darkN);
        }
      }
      const port_button_art::Kind kind = port_button_art::KindOf(label);
      if (kind != port_button_art::Kind::Plate) {
        const int pw = p.w, ph = p.h;
        const int side = std::min(std::max(x1 - x0, y1 - y0) + 1, std::min(pw, ph));
        const int cx = (x0 + x1 + 1) / 2, cy = (y0 + y1 + 1) / 2;
        x0 = std::clamp(cx - side / 2, 0, pw - side), y0 = std::clamp(cy - side / 2, 0, ph - side);
        x1 = x0 + side - 1, y1 = y0 + side - 1;
      }
      const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
      std::vector<uint8_t> art(static_cast<size_t>(bw) * bh * 4);
      port_button_art::Draw(label, art.data(), bw * 4, 0, 0, bw, bh, &sFont, false);
      for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
          const uint8_t* src = &art[(static_cast<size_t>(y) * bw + x) * 4];
          uint8_t* dst = at(x0 + x, y0 + y);
          const int a2 = src[3], da = dst[3];
          const int outA = a2 + da * (255 - a2) / 255;
          for (int k = 0; k < 3; k++) {
            dst[k] = static_cast<uint8_t>(outA > 0 ? (src[k] * a2 + dst[k] * da * (255 - a2) / 255) / outA : 0);
          }
          dst[3] = static_cast<uint8_t>(outA);
        }
      }
      continue;
    }
    auto px = [&](int x, int y) { return &s.painted[(static_cast<size_t>(p.y + y) * s.w + p.x + x) * 4]; };
    auto colourless = [](const uint8_t* c) {
      return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) <= 28;
    };
    int x0 = p.w, y0 = p.h, x1 = -1, y1 = -1;
    for (int y = 0; y < p.h; y++) {
      for (int x = 0; x < p.w; x++) {
        const uint8_t* c = px(x, y);
        if (c[3] > 128 && colourless(c) && c[0] + c[1] + c[2] > 3 * 90) {
          x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
      }
    }
    if (x1 < 0) continue;
    x0 = std::max(x0 - 1, 0), y0 = std::max(y0 - 1, 0), x1 = std::min(x1 + 1, p.w - 1), y1 = std::min(y1 + 1, p.h - 1);
    for (int y = y0; y <= y1; y++) {
      for (int x = x0; x <= x1; x++) {
        uint8_t* c = px(x, y);
        if (colourless(c)) c[0] = c[1] = c[2] = c[3] = 0;
      }
    }
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    std::vector<uint8_t> piece(static_cast<size_t>(bw) * bh * 4);
    port_button_art::Draw(label, piece.data(), bw * 4, 0, 0, bw, bh, &sFont);
    for (int y = 0; y < bh; y++) {
      for (int x = 0; x < bw; x++) {
        const uint8_t* src = &piece[(static_cast<size_t>(y) * bw + x) * 4];
        uint8_t* dst = px(x0 + x, y0 + y);
        const int a = src[3], da = dst[3];
        const int outA = a + da * (255 - a) / 255;
        for (int c = 0; c < 3; c++) {
          dst[c] = static_cast<uint8_t>(outA > 0 ? (src[c] * a + dst[c] * da * (255 - a) / 255) / outA : 0);
        }
        dst[3] = static_cast<uint8_t>(outA);
      }
    }
  }
  at::RawTextureReplacement raw;
  raw.bytes = s.painted;
  raw.width = s.w;
  raw.height = s.h;
  raw.mipCount = 1;
  raw.gxFormat = GX_TF_RGBA8_PC;
  raw.label = "XD button pictures";
  s.registration = at::register_replacement(s.key, raw);
  s.style = sStyle;
}

bool Wants(void*, const at::TextureSourceKey& key) {
  if (AtlasOf(key) == nullptr) return false;
  std::lock_guard lk(sMutex);
  if (!sActive) return false;
  for (const Sheet& s : sSheets) {
    if (s.key == key) return s.style != sStyle || s.registration.id == 0;
  }
  return true;
}

void Decoded(void*, const at::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h) {
  const Atlas* atlas = AtlasOf(key);
  if (atlas == nullptr) return;
  std::lock_guard lk(sMutex);
  if (!sActive) return;
  Sheet* sheet = nullptr;
  for (Sheet& s : sSheets) {
    if (s.key == key) sheet = &s;
  }
  if (sheet == nullptr) {
    sSheets.emplace_back();
    sheet = &sSheets.back();
    sheet->atlas = atlas;
    sheet->key = key;
    sheet->w = w, sheet->h = h;
    sheet->original.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
    std::fprintf(stderr, "[button icons] %s (%ux%u): PC button pictures\n", atlas->name, w, h);
  }
  Paint(*sheet);
}

std::optional<ec::Device> ChosenDevice(engine::settings::Registry& reg) {
  const int32_t v = reg.Enum("controls.button_icons").Get();
  ec::Device d;
  if (v == reg.ChoiceIndex("controls.button_icons", "auto")) {
    d = port_controls::LastUsedDevice(0);
  } else if (v == reg.ChoiceIndex("controls.button_icons", "xbox")) {
    d = ec::Device::Xbox;
  } else if (v == reg.ChoiceIndex("controls.button_icons", "playstation")) {
    d = ec::Device::PlayStation;
  } else if (v == reg.ChoiceIndex("controls.button_icons", "nintendo")) {
    d = ec::Device::Switch;
  } else if (v == reg.ChoiceIndex("controls.button_icons", "keyboard")) {
    d = ec::Device::Keyboard;
  } else {
    return std::nullopt;
  }
  if (d == ec::Device::GameCube || d == ec::Device::NsoGameCube) return std::nullopt;
  return d;
}

uint32_t sSeenChanges = ~0u, sSeenDeviceChanges = ~0u;

struct Switch {
  uint32_t frame;
  std::string choice;
};
std::vector<Switch> sSwitches;
size_t sNextSwitch = 0;
bool sSwitchesRead = false;

void RunSwitches(engine::settings::Registry& reg) {
  if (!sSwitchesRead) {
    sSwitchesRead = true;
    const char* env = std::getenv("XD_BUTTON_ICONS_AT");
    for (const char* p = env; p != nullptr && *p != '\0';) {
      char* end = nullptr;
      const unsigned long frame = std::strtoul(p, &end, 10);
      if (end == p || *end != ':') break;
      const char* c = end + 1;
      const char* stop = std::strchr(c, ',');
      sSwitches.push_back({static_cast<uint32_t>(frame), std::string(c, stop ? stop - c : std::strlen(c))});
      p = stop ? stop + 1 : c + std::strlen(c);
    }
  }
  while (sNextSwitch < sSwitches.size() && PortVI_GetFlipCount() >= sSwitches[sNextSwitch].frame) {
    const Switch& sw = sSwitches[sNextSwitch++];
    std::fprintf(stderr, "[button icons] game frame %u: controls.button_icons=%s (XD_BUTTON_ICONS_AT)\n",
                 PortVI_GetFlipCount(), sw.choice.c_str());
    reg.Override("controls.button_icons", sw.choice);
  }
}

}

extern "C" void PortXD_ButtonIconsUpdate(void) {
  engine::settings::Registry& reg = engine::settings::Global();
  if (reg.Find("controls.button_icons") == engine::settings::kNoSetting) return;
  RunSwitches(reg);
  if (reg.Changes() == sSeenChanges && port_controls::DeviceChanges() == sSeenDeviceChanges) return;
  sSeenChanges = reg.Changes();
  sSeenDeviceChanges = port_controls::DeviceChanges();

  const std::optional<ec::Device> device = ChosenDevice(reg);
  std::array<ec::ButtonLabel, ec::kGcInputCount> labels{};
  if (device) {
    const ec::PortMap map = ec::ReadPort(reg, 0);
    for (size_t i = 0; i < ec::kGcInputCount; i++) {
      labels[i] = ec::GcInputLabel(map, static_cast<GcInput>(i), *device);
    }
  }
  bool setObserver = false, clearObserver = false;
  {
    std::lock_guard lk(sMutex);
    if (!sFont.glyph) sFont.glyph = GameGlyph;
    if (device.has_value() == sActive && labels == sLabels) return;
    sLabels = labels;
    sStyle++;
    if (device) {
      setObserver = !sActive;
      sActive = true;
      for (Sheet& s : sSheets) Paint(s);
    } else {
      clearObserver = sActive;
      sActive = false;
      for (Sheet& s : sSheets) {
        if (s.registration.id != 0) at::unregister_replacement(s.registration);
        s.registration = {};
      }
    }
  }
  std::fprintf(stderr, "[button icons] %s\n",
               device ? (std::string("pictures of: ") + ec::DeviceId(*device) + " (A = " + labels[0].text + ")").c_str()
                      : "GameCube pictures");
  if (setObserver) at::set_source_observer({Wants, Decoded, nullptr});
  if (clearObserver) at::set_source_observer({});
}
