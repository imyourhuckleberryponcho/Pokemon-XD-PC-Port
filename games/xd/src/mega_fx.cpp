#include "mega_fx.h"
#include "xd_gen57.h"

#include <aurora/imgui.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <imgui.h>
#include <settings.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
void PortVideo_SetFrameDrawer(void (*draw)());
void PortMenu_PictureRect(float* x, float* y, float* scale);
int64_t PortTime_Now(void);
uint32_t PortFreeze_PictureCount(size_t* partial);
void PortFreeze_Capture(void);
bool PortFreeze_Active(void);
bool PortPhoto_Active(void);
bool PortPhoto_LiveActive(void);
int PortAudio_OutRate(void);
void PortAudio_PlayOverlay(const int16_t* stereo, size_t frames, float gain);
void PortAudio_StopOverlay(void);
struct MegaFxVec {
  float x, y, z;
};
void* fightOutPokemon_GetSequencePtr(void* fop);
void* Sequence_GetGSmodel(void* seq);
void* GSmodelGetBound(void* model);
void GSboundGetMinMax(void* bound, MegaFxVec* min, MegaFxVec* max, bool scale, bool rot, bool trans, bool ignoreRoot);
void* GSgfxMatrixGetPtr(int which);
void* fightFloorGetFightOutPokemonPtrToFightTrainerPtr(void* floor, void* fop);
void* fightTrainer_GetSequencePtr(void* trainer);
}

namespace {
namespace es = engine::settings;

double GameNow() { return static_cast<double>(PortTime_Now()) / 40500000.0; }

constexpr float kPi = 3.14159265f;

struct V3 {
  float x = 0.f, y = 0.f, z = 0.f;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V3 a) { return std::sqrt(Dot(a, a)); }
V3 Norm(V3 a) {
  const float l = Len(a);
  return l > 1e-6f ? a * (1.f / l) : V3{0.f, 0.f, -1.f};
}
V3 Lerp(V3 a, V3 b, float k) { return a + (b - a) * k; }
using M34 = float[3][4];
V3 Apply(const M34& m, V3 p) {
  return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3], m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
          m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
}
void Identity(M34& m) {
  std::memset(m, 0, sizeof(M34));
  m[0][0] = m[1][1] = m[2][2] = 1.f;
}
float Clamp01(float x) { return std::clamp(x, 0.f, 1.f); }
float Smooth(float x) {
  x = Clamp01(x);
  return x * x * (3.f - 2.f * x);
}
float EaseOut(float x) {
  x = Clamp01(x);
  return 1.f - (1.f - x) * (1.f - x);
}
float EaseOutBack(float x) {
  x = Clamp01(x);
  const float c = 1.9f;
  return 1.f + (c + 1.f) * std::pow(x - 1.f, 3.f) + c * std::pow(x - 1.f, 2.f);
}

struct Rng {
  uint32_t s;
  float next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFF) / 16777216.f;
  }
};

ImU32 Hue(float h, float a, float sat = 0.75f, float val = 1.f) {
  h = h - std::floor(h);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, sat, val, r, g, b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, Clamp01(a)));
}
ImU32 Rgba(float r, float g, float b, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, Clamp01(a))); }
ImU32 White(float a) { return Rgba(1.f, 1.f, 1.f, a); }

enum Phase { kNone, kReact, kLink, kBurst };
struct Show {
  Phase phase = kNone;
  void* fop = nullptr;
  void* trainerSeq = nullptr;
  int primal = 0;
  double tReact = 0, tLink = 0, tBurst = 0;
  bool reacted = false;
  uint32_t serial = 0;
};
std::mutex sLock;
Show sShow;
uint32_t sSerials = 0;

bool Log() {
  static int on = -1;
  if (on < 0) { const char* e = std::getenv("XD_MEGA_FX_LOG"); on = e != nullptr && e[0] == '1'; }
  return on == 1;
}
bool CameraWanted() {
  static int on = -1;
  if (on < 0) { const char* e = std::getenv("XD_MEGA_FX_CAMERA"); on = !(e != nullptr && e[0] == '0'); }
  return on == 1;
}

struct Tex {
  ImTextureID id{};
  bool made = false;
};
Tex sGlow, sStar, sRing, sShell, sEmblem;

Tex Make(int n, const std::vector<uint32_t>& rgba) {
  Tex t;
  t.id = aurora_imgui_add_texture(static_cast<uint32_t>(n), static_cast<uint32_t>(n), rgba.data());
  t.made = true;
  return t;
}
uint32_t Px(float r, float g, float b, float a) {
  auto c = [](float v) { return static_cast<uint32_t>(std::lround(Clamp01(v) * 255.f)); };
  return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}

void MakeTextures() {
  if (sGlow.made) return;
  {
    const int n = 64;
    std::vector<uint32_t> p(n * n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f, r = std::sqrt(dx * dx + dy * dy);
        p[y * n + x] = Px(1, 1, 1, r >= 1.f ? 0.f : std::exp(-r * r * 5.f) * (1.f - r * r));
      }
    sGlow = Make(n, p);
  }
  {
    const int n = 128;
    std::vector<uint32_t> p(n * n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f, r = std::sqrt(dx * dx + dy * dy);
        const float u = (dx + dy) * 0.7071f, v = (dx - dy) * 0.7071f;
        float a = std::exp(-std::fabs(dx) * 38.f) * std::max(0.f, 1.f - std::fabs(dy)) + std::exp(-std::fabs(dy) * 38.f) * std::max(0.f, 1.f - std::fabs(dx));
        a += 0.45f * (std::exp(-std::fabs(u) * 45.f) * std::max(0.f, 1.f - 1.8f * std::fabs(v)) + std::exp(-std::fabs(v) * 45.f) * std::max(0.f, 1.f - 1.8f * std::fabs(u)));
        a += std::exp(-r * r * 60.f) + 0.35f * std::exp(-r * r * 9.f);
        p[y * n + x] = Px(1, 1, 1, r >= 1.f ? 0.f : a * (1.f - r));
      }
    sStar = Make(n, p);
  }
  {
    const int n = 128;
    std::vector<uint32_t> p(n * n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f, r = std::sqrt(dx * dx + dy * dy);
        const float a = std::exp(-std::pow((r - 0.84f) / 0.045f, 2.f)) + 0.35f * std::exp(-std::pow((r - 0.8f) / 0.13f, 2.f));
        p[y * n + x] = Px(1, 1, 1, r >= 1.f ? 0.f : a);
      }
    sRing = Make(n, p);
  }
  {
    const int n = 256;
    std::vector<uint32_t> p(n * n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f, r = std::sqrt(dx * dx + dy * dy);
        const float ang = std::atan2(dy, dx);
        float a = 0.16f + 0.84f * std::pow(Smooth((r - 0.35f) / 0.62f), 1.6f);
        a += 0.07f * std::sin(ang * 3.f + r * 11.f) + 0.05f * std::sin(ang * 5.f - r * 7.f);
        a *= 1.f - Smooth((r - 0.95f) / 0.05f);
        p[y * n + x] = Px(1, 1, 1, r >= 1.f ? 0.f : a);
      }
    sShell = Make(n, p);
  }
  {
    const int n = 256;
    std::vector<uint32_t> p(n * n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++) {
        const float dx = (x + 0.5f) / n * 2.f - 1.f, dy = -((y + 0.5f) / n * 2.f - 1.f), r = std::sqrt(dx * dx + dy * dy);
        const float ang = std::atan2(dy, dx);
        const float gapC = 0.95f, gapW = 0.32f;
        float gd = std::fabs(std::remainder(ang - gapC, 2.f * kPi));
        float ring = 1.f - Smooth((std::fabs(r - 0.8f) - 0.075f) / 0.02f);
        ring *= Smooth((gd - gapW) / 0.08f);
        float sd = 10.f;
        { const float d = std::sqrt(dx * dx + (dy - 0.3f) * (dy - 0.3f)); if (dx <= 0.02f || dy > 0.3f) sd = std::min(sd, std::fabs(d - 0.3f)); }
        { const float d = std::sqrt(dx * dx + (dy + 0.3f) * (dy + 0.3f)); if (dx >= -0.02f || dy < -0.3f) sd = std::min(sd, std::fabs(d - 0.3f)); }
        const float band = 1.f - Smooth((sd - 0.055f) / 0.02f);
        const float dot1 = 1.f - Smooth((std::sqrt((dx - 0.13f) * (dx - 0.13f) + (dy - 0.3f) * (dy - 0.3f)) - 0.07f) / 0.02f);
        const float dot2 = 1.f - Smooth((std::sqrt((dx + 0.13f) * (dx + 0.13f) + (dy + 0.3f) * (dy + 0.3f)) - 0.07f) / 0.02f);
        const float fill = std::max(std::max(ring, band), std::max(dot1, dot2));
        const float edgeRing = (1.f - Smooth((std::fabs(r - 0.8f) - 0.095f) / 0.02f)) * Smooth((gd - gapW + 0.03f) / 0.08f);
        const float edgeBand = 1.f - Smooth((sd - 0.075f) / 0.02f);
        const float edge = std::max(std::max(edgeRing, edgeBand), std::max(dot1, dot2)) - fill;
        float cr, cg, cb;
        ImGui::ColorConvertHSVtoRGB(std::fmod((ang + kPi) / (2.f * kPi) + r * 0.35f + 1.f, 1.f), 0.7f, 1.f, cr, cg, cb);
        const float a = Clamp01(fill + edge);
        const float k = a > 0.f ? fill / a : 0.f;
        p[y * n + x] = Px(cr * k + (1 - k), cg * k + (1 - k), cb * k + (1 - k), a);
      }
    sEmblem = Make(n, p);
  }
}

struct Scene {
  M34 view{};
  V3 c, feet;
  V3 up;
  float radius = 0.f;
  bool trainer = false;
  V3 key;
};

bool Bounds(void* seq, V3* mn, V3* mx) {
  if (seq == nullptr) return false;
  void* model = Sequence_GetGSmodel(seq);
  void* bound = model != nullptr ? GSmodelGetBound(model) : nullptr;
  if (bound == nullptr) return false;
  MegaFxVec a{}, b{};
  GSboundGetMinMax(bound, &a, &b, true, true, true, false);
  *mn = {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
  *mx = {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
  const V3 d = *mx - *mn;
  return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && d.x >= 0.f && d.y > 0.f && d.z >= 0.f && Len(d) < 1e4f;
}

bool Gather(void* fop, void* trainerSeq, Scene& sc) {
  const float* m = static_cast<const float*>(GSgfxMatrixGetPtr(2));
  if (m == nullptr) return false;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 4; j++) sc.view[i][j] = m[i * 4 + j];
  V3 mn, mx;
  if (!Bounds(fightOutPokemon_GetSequencePtr(fop), &mn, &mx)) return false;
  const V3 mid = (mn + mx) * 0.5f, size = mx - mn;
  sc.c = Apply(sc.view, mid);
  sc.feet = Apply(sc.view, V3{mid.x, mn.y, mid.z});
  sc.radius = 0.5f * std::max(size.y, std::max(size.x, size.z));
  sc.up = Norm(V3{sc.view[0][1], sc.view[1][1], sc.view[2][1]});
  V3 tn, tx;
  sc.trainer = Bounds(trainerSeq, &tn, &tx);
  if (sc.trainer) {
    const V3 tmid = (tn + tx) * 0.5f;
    V3 toward = mid - tmid;
    toward.y = 0.f;
    const float half = 0.5f * std::max(tx.x - tn.x, tx.z - tn.z);
    const V3 k = V3{tmid.x, tn.y + 0.6f * (tx.y - tn.y), tmid.z} + Norm(toward) * (0.35f * half);
    sc.key = Apply(sc.view, k);
  }
  return true;
}

struct CamMove {
  V3 target;
  float aim = 0.f;
  float dolly = 0.f;
  float orbit = 0.f;
  float shakeX = 0.f, shakeY = 0.f;
  float radius = 1.f;
  V3 up{0.f, 1.f, 0.f};
};

void CameraMatrix(const CamMove& cm, M34& o) {
  const V3 t = cm.target;
  V3 e = V3{} - t;
  if (cm.orbit != 0.f) {
    const float a = cm.orbit * kPi / 180.f, ca = std::cos(a), sa = std::sin(a);
    const V3 k = cm.up;
    e = e * ca + Cross(k, e) * sa + k * (Dot(k, e) * (1.f - ca));
  }
  const float dist = Len(e);
  float keep = 1.f - std::clamp(cm.dolly, 0.f, 0.6f);
  if (dist * keep < cm.radius * 2.2f) keep = std::min(1.f, cm.radius * 2.2f / std::max(dist, 1e-3f));
  const V3 c = t + e * keep;
  V3 ahead = c + V3{0.f, 0.f, -1.f} * std::max(1e-3f, -(t - c).z);
  if (cm.orbit != 0.f) ahead = t;
  V3 a = Lerp(ahead, t, Clamp01(cm.aim));
  const float d = Len(t - c);
  a = a + V3{cm.shakeX * d, cm.shakeY * d, 0.f};
  const V3 f = Norm(a - c);
  V3 r = Cross(f, V3{0.f, 1.f, 0.f});
  if (Len(r) < 1e-4f) r = {1.f, 0.f, 0.f};
  r = Norm(r);
  const V3 u = Cross(r, f), z = f * -1.f;
  const V3 rows[3] = {r, u, z};
  for (int i = 0; i < 3; i++) {
    o[i][0] = rows[i].x;
    o[i][1] = rows[i].y;
    o[i][2] = rows[i].z;
    o[i][3] = -Dot(rows[i], c);
  }
}

bool sOverrideOn = false;
M34 sCur{}, sPrev{};
uint32_t sPicturesAtSet = 0;
int sFieldsSinceSet = 0;
float sProj[4][4] = {};
bool sHaveProj = false;
uint32_t sDrawnSerial = ~0u;
uint32_t sJingleSerial = ~0u;
constexpr float kChargeSeconds = 2.2f;

void SetOverride(const M34& o, bool active) {
  AuroraInterpInfo info{};
  AuroraInterpGetInfo(&info);
  if (sOverrideOn) {
    AuroraViewProjection lp[4];
    const uint32_t n = AuroraGetViewOverrideProjections(lp, 4);
    if (n > 0) {
      std::memcpy(sProj, lp[0].proj, sizeof sProj);
      sHaveProj = true;
    }
  }
  AuroraViewOverride ov{};
  ov.active = active ? GX_TRUE : GX_FALSE;
  ov.hide2D = GX_FALSE;
  ov.sceneOnly = GX_FALSE;
  ov.sceneFirst = GX_FALSE;
  std::memcpy(ov.view, o, sizeof ov.view);
  ov.zoom = 1.f;
  ov.minViewportWidth = 600.f;
  AuroraSetViewOverride(&ov);
  std::memcpy(sPrev, sCur, sizeof sCur);
  std::memcpy(sCur, o, sizeof sCur);
  sOverrideOn = active;
  size_t partial = 0;
  sPicturesAtSet = PortFreeze_PictureCount(&partial);
  sFieldsSinceSet = 0;
}

const M34& ShownOverride() {
  size_t partial = 0;
  return PortFreeze_PictureCount(&partial) > sPicturesAtSet ? sCur : sPrev;
}

std::vector<int16_t> sJingle;
bool sJingleTried = false;
float sJingleClimax = 1.5f;

uint32_t Le32(const std::vector<uint8_t>& b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | static_cast<uint32_t>(b[o + 3]) << 24; }
uint16_t Le16(const std::vector<uint8_t>& b, size_t o) { return static_cast<uint16_t>(b[o] | b[o + 1] << 8); }

void LoadJingle() {
  if (sJingleTried) return;
  sJingleTried = true;
  const std::filesystem::path path = xd::gen57::PackFolder() / L"sound" / L"mega_evolution.wav";
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "[mega fx] no jingle (%s): the Gen 5-7 pack was made before it had one; the show plays without it\n", path.string().c_str());
    return;
  }
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) return;
  uint32_t rate = 0, channels = 0, bits = 0;
  size_t data = 0, dataSize = 0;
  for (size_t o = 12; o + 8 <= b.size();) {
    const uint32_t sz = Le32(b, o + 4);
    if (std::memcmp(b.data() + o, "fmt ", 4) == 0 && o + 24 <= b.size()) {
      channels = Le16(b, o + 10);
      rate = Le32(b, o + 12);
      bits = Le16(b, o + 22);
    } else if (std::memcmp(b.data() + o, "data", 4) == 0) {
      data = o + 8;
      dataSize = std::min<size_t>(sz, b.size() - data);
    }
    o += 8 + sz + (sz & 1);
  }
  if (data == 0 || bits != 16 || channels < 1 || channels > 2 || rate < 8000) return;
  const size_t frames = dataSize / (2 * channels);
  const int out = PortAudio_OutRate();
  const size_t outFrames = static_cast<size_t>(static_cast<double>(frames) * out / rate);
  sJingle.resize(outFrames * 2);
  auto at = [&](size_t i, uint32_t ch) {
    return static_cast<float>(static_cast<int16_t>(Le16(b, data + (i * channels + std::min(ch, channels - 1)) * 2)));
  };
  for (size_t i = 0; i < outFrames; i++) {
    const double src = static_cast<double>(i) * rate / out;
    const size_t i0 = std::min(static_cast<size_t>(src), frames - 1), i1 = std::min(i0 + 1, frames - 1);
    const float k = static_cast<float>(src - static_cast<double>(i0));
    for (uint32_t ch = 0; ch < 2; ch++) sJingle[i * 2 + ch] = static_cast<int16_t>(std::lround(at(i0, ch) * (1.f - k) + at(i1, ch) * k));
  }
  std::fprintf(stderr, "[mega fx] jingle %s: %.2f s\n", path.string().c_str(), static_cast<double>(outFrames) / out);
}

void PlayJingle() {
  LoadJingle();
  if (sJingle.empty()) return;
  const auto& reg = es::Global();
  float gain = 0.85f;
  if (reg.Find("audio.music_volume") != es::kNoSetting) gain *= static_cast<float>(reg.Int("audio.music_volume").Get()) / 100.f;
  PortAudio_PlayOverlay(sJingle.data(), sJingle.size() / 2, gain);
}

struct Painter {
  ImDrawList* dl;
  float x0, y0, s;
  const M34* o;
  float p00, p02, p11, p12;
  bool At(V3 v, ImVec2* out, float* invDepth = nullptr) const {
    if (o != nullptr) v = Apply(*o, v);
    if (v.z > -1e-3f) return false;
    const float w = -v.z;
    const float nx = (v.x * p00 + v.z * p02) / w, ny = (v.y * p11 + v.z * p12) / w;
    *out = ImVec2(x0 + (320.f + 320.f * nx) * s, y0 + (240.f - 240.f * ny) * s);
    if (invDepth) *invDepth = 1.f / w;
    return true;
  }
  float Size(V3 v, float len) const {
    if (o != nullptr) v = Apply(*o, v);
    return v.z < -1e-3f ? len * p11 * 240.f * s / -v.z : 0.f;
  }
  void Sprite(const Tex& t, ImVec2 c, float hw, float hh, ImU32 col, float rot = 0.f) const {
    if (!t.made || hw <= 0.f || hh <= 0.f) return;
    const float cs = std::cos(rot), sn = std::sin(rot);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
    dl->AddImageQuad(t.id, P(-hw, -hh), P(hw, -hh), P(hw, hh), P(-hw, hh), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
  }
  void Sprite(const Tex& t, ImVec2 c, float r, ImU32 col, float rot = 0.f) const { Sprite(t, c, r, r, col, rot); }
};

void DarkenPicture(const Painter& p, float alpha, float bottom = 480.f) {
  if (alpha <= 0.f) return;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  p.dl->AddRectFilled(ImVec2(0, 0), ImVec2(size.x, p.y0 + bottom * p.s), IM_COL32(0, 0, 0, static_cast<int>(255.f * Clamp01(alpha))));
}

void Motes(const Painter& p, ImVec2 from, ImVec2 dir, ImVec2 to, float t, float size, uint32_t seed) {
  Rng rng{seed};
  const ImVec2 perp(-dir.y, dir.x);
  for (int i = 0; i < 18; i++) {
    const float delay = i * 0.06f + rng.next() * 0.05f, spread = (rng.next() - 0.5f) * 2.f, hue = rng.next();
    const float u = (t - delay) / 0.55f;
    if (u <= 0.f || u >= 1.f) continue;
    for (int k = 0; k < 5; k++) {
      const float uk = std::max(0.f, u - k * 0.035f);
      const float e = uk * uk;
      const ImVec2 start(from.x + perp.x * spread * size * 9.f, from.y + perp.y * spread * size * 9.f);
      const ImVec2 q(start.x + (to.x - start.x) * e + perp.x * std::sin(uk * kPi) * spread * size * 4.f,
                     start.y + (to.y - start.y) * e + perp.y * std::sin(uk * kPi) * spread * size * 4.f);
      p.Sprite(sGlow, q, size * (1.4f - k * 0.22f), Hue(hue + t * 0.4f, (1.f - k * 0.18f) * Smooth(u * 4.f)));
      if (k == 0) p.Sprite(sGlow, q, size * 0.45f, White(0.9f));
    }
  }
}

void Glint(const Painter& p, ImVec2 c, float size, float t, float alpha) {
  p.Sprite(sGlow, c, size * 1.3f, Hue(t * 0.3f, 0.55f * alpha, 0.45f));
  p.Sprite(sStar, c, size, White(alpha), t * 0.9f);
  p.Sprite(sStar, c, size * 0.6f, Hue(t * 0.5f + 0.1f, 0.8f * alpha, 0.4f), -t * 1.4f + 0.4f);
}

void Strands(const Painter& p, const Scene& sc, V3 from, float head, float t, float alpha, float shellR) {
  if (head <= 0.f || alpha <= 0.f) return;
  const V3 axis = sc.c - from;
  const float L = Len(axis);
  if (L < 1e-3f) return;
  const V3 n1 = Norm(Cross(axis, sc.up)), n2 = Norm(Cross(axis, n1));
  const int steps = 220;
  for (int strand = 0; strand < 2; strand++) {
    ImVec2 prev{};
    bool havePrev = false;
    for (int i = 0; i <= steps; i++) {
      const float sPos = head * static_cast<float>(i) / steps;
      const float wind = Smooth((sPos - 0.72f) / 0.28f);
      const float amp = std::min(sc.radius * 0.45f, L * 0.06f) * std::sqrt(std::max(0.f, std::sin(kPi * std::min(sPos, 0.72f) / 0.72f * 0.5f + 0.001f))) * (1.f - wind) +
                        shellR * 1.05f * wind;
      const float phi = sPos * 4.5f * 2.f * kPi - t * 7.f + strand * kPi + wind * 3.f;
      V3 q = Lerp(from, sc.c, std::min(sPos, 0.72f) / 0.72f * (1.f - wind) + wind) + (n1 * std::cos(phi) + n2 * std::sin(phi)) * amp;
      q = q + sc.up * (wind * shellR * 0.9f * std::sin(phi * 0.25f));
      ImVec2 sp;
      if (!p.At(q, &sp)) { havePrev = false; continue; }
      const float frontness = 0.6f + 0.4f * std::cos(phi);
      const ImU32 col = Hue(sPos * 0.9f + strand * 0.5f + t * 0.35f, alpha * (0.55f + 0.45f * frontness));
      if (havePrev) {
        p.dl->AddLine(prev, sp, col, std::max(1.5f, 4.5f * p.s * frontness));
        p.dl->AddLine(prev, sp, White(alpha * 0.55f * frontness), std::max(1.f, 1.6f * p.s));
      }
      if (i % 14 == 0) p.Sprite(sGlow, sp, 11.f * p.s * frontness, Hue(sPos * 0.9f + strand * 0.5f + t * 0.35f, alpha * 0.6f));
      prev = sp;
      havePrev = true;
    }
    if (havePrev && head < 1.f) p.Sprite(sGlow, prev, 20.f * p.s, White(alpha));
  }
}

void Shell(const Painter& p, ImVec2 c, float rx, float t, float alpha, const float tint[3], bool primal) {
  if (rx <= 0.f || alpha <= 0.f) return;
  const float ry = rx * 1.2f;
  const float pulse = 0.5f + 0.5f * std::sin(t * (5.f + 3.f * t));
  p.Sprite(sGlow, c, rx * 2.0f, ry * 2.0f, primal ? Rgba(tint[0], tint[1], tint[2], 0.35f * alpha) : White(0.3f * alpha));
  p.Sprite(sShell, c, rx, ry, Rgba(0.75f + 0.25f * tint[0], 0.75f + 0.25f * tint[1], 0.75f + 0.25f * tint[2], (0.62f + 0.18f * pulse) * alpha));
  for (int k = 0; k < 3; k++) {
    const float sc = 1.0f + k * 0.035f;
    const ImU32 col = primal ? Rgba(tint[0], tint[1], tint[2], (0.55f - k * 0.12f) * alpha) : Hue(t * 0.35f + k * 0.29f, (0.55f - k * 0.12f) * alpha, 0.85f);
    p.Sprite(sRing, c, rx * sc * 1.1f, ry * sc * 1.1f, col, t * (0.6f + k * 0.4f));
  }
  p.Sprite(sGlow, c, rx * 0.9f, ry * 0.9f, White((0.35f + 0.35f * pulse) * alpha));
}

void Sparkles(const Painter& p, ImVec2 c, float rx, float t, float alpha, uint32_t seed) {
  Rng rng{seed};
  for (int i = 0; i < 20; i++) {
    const float a0 = rng.next() * 2.f * kPi, rr = 1.15f + 0.7f * rng.next(), sp = 0.35f + 0.4f * rng.next(), ph = rng.next(), hue = rng.next();
    const float life = std::fmod(t * sp + ph, 1.f);
    const float ang = a0 + t * 0.5f;
    const ImVec2 q(c.x + std::cos(ang) * rx * rr, c.y + std::sin(ang) * rx * rr * 0.45f + rx * (0.9f - 2.2f * life));
    const float tw = std::sin(life * kPi);
    p.Sprite(sStar, q, rx * 0.16f * (0.5f + tw), Hue(hue, alpha * tw, 0.35f), t * 2.f + ph * 6.f);
  }
}

void DrawBeta(const Painter& p, const Scene& sc, const Show& sh, float t, ImVec2 C, float Rpx, float Rs, const float tint[3], bool primal) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (sh.phase == kReact) {
    DarkenPicture(p, 0.22f * Smooth(t / 0.8f), 372.f);
    ImVec2 S;
    if (!p.At(sc.c + sc.up * (sc.radius * 0.35f), &S)) S = C;
    ImVec2 dir(-0.8f, 0.6f);
    ImVec2 K;
    if (sc.trainer && p.At(sc.key, &K)) {
      const float dx = K.x - S.x, dy = K.y - S.y, l = std::sqrt(dx * dx + dy * dy);
      if (l > 1.f) dir = ImVec2(dx / l, dy / l);
    } else if (sc.trainer && sc.key.z >= 0.f) {
      dir = ImVec2(sc.key.x - sc.c.x >= 0.f ? 0.85f : -0.85f, 0.5f);
    }
    const float far = 640.f * p.s;
    Motes(p, ImVec2(S.x + dir.x * far, S.y + dir.y * far), dir, S, t - 0.2f, 13.f * p.s, 0x51A7u + sh.serial);
    const float aura = Smooth(t / 1.0f);
    p.Sprite(sRing, C, Rs * 1.25f, Rs * 1.4f, Hue(t * 0.4f, 0.45f * aura, 0.8f), t * 0.7f);
    p.Sprite(sRing, C, Rs * 1.32f, Rs * 1.48f, Hue(t * 0.4f + 0.4f, 0.3f * aura, 0.8f), -t * 0.5f);
    p.Sprite(sGlow, C, Rs * 1.6f, White(0.18f * aura));
    const float pop = t < 0.3f ? EaseOutBack(t / 0.3f) * 1.25f : 1.f + 0.12f * std::sin(t * 9.f);
    const float ringU = std::fmod(t, 0.6f) / 0.6f;
    p.Sprite(sRing, S, Rs * (0.2f + 0.9f * ringU), Hue(t * 0.6f, 0.7f * (1.f - ringU), 0.6f));
    Glint(p, S, Rs * 0.55f * pop, t, Smooth(t / 0.12f));
  } else if (sh.phase == kLink) {
    DarkenPicture(p, 0.3f * Smooth(t / 0.6f));
    const float formed = Smooth((t - 0.55f) / 0.55f);
    const float shellR = sc.radius * 1.15f * (Rs / std::max(Rpx, 1.f));
    float rx = Rs * 1.15f * EaseOutBack(Clamp01((t - 0.55f) / 0.65f));
    if (t > 1.7f) rx *= 1.f + 0.035f * std::sin((t - 1.7f) * 32.f) * Smooth((t - 1.7f) / 0.3f);
    if (!primal) {
      ImVec2 K;
      const bool kv = sc.trainer && p.At(sc.key, &K);
      V3 from = sc.trainer ? sc.key : sc.c + V3{-sc.radius * 6.f, sc.radius * 0.5f, sc.radius * 2.f};
      if (kv) {
        const float flare = t < 0.3f ? EaseOutBack(t / 0.3f) * 1.5f : 1.f + 0.1f * std::sin(t * 8.f);
        const float fade = 1.f - 0.6f * Smooth((t - 1.2f) / 0.6f);
        const float ringU = Clamp01((t - 0.05f) / 0.5f);
        p.Sprite(sRing, K, 70.f * p.s * EaseOut(ringU), Hue(0.12f, 0.8f * (1.f - ringU), 0.5f));
        Glint(p, K, 38.f * p.s * flare, t, fade);
      }
      Strands(p, sc, from, Smooth((t - 0.15f) / 0.55f), t, 1.f - 0.45f * formed, shellR);
    } else {
      Rng rng{0x9A11u + sh.serial};
      for (int i = 0; i < 24; i++) {
        const float a0 = rng.next() * 2.f * kPi, ph = rng.next(), sp = 0.6f + 0.6f * rng.next();
        const float life = std::fmod(t * sp + ph, 1.f);
        const ImVec2 q(C.x + std::cos(a0 + t) * Rpx * 1.6f * (1.f - life), C.y + Rpx * 1.8f * (1.f - life) - Rpx * 0.2f);
        p.Sprite(sGlow, q, 12.f * p.s, Rgba(tint[0], tint[1], tint[2], 0.9f * std::sin(life * kPi)));
      }
    }
    Shell(p, C, rx, t, formed, tint, primal);
    if (sJingleSerial != sh.serial && t >= std::max(0.f, kChargeSeconds - sJingleClimax)) {
      sJingleSerial = sh.serial;
      PlayJingle();
    }
    Sparkles(p, C, std::max(rx, Rs), t, formed, 0x5EEDu + sh.serial);
  } else {
    if (sJingleSerial != sh.serial) {
      sJingleSerial = sh.serial;
      PlayJingle();
    }
    const float flash = t < 0.05f ? t / 0.05f : (t < 0.13f ? 1.f : 1.f - Smooth((t - 0.13f) / 0.4f));
    Rng rng{0xB0057u + sh.serial};
    const float su = std::max(0.f, t - 0.04f);
    for (int i = 0; i < 28; i++) {
      const float az = rng.next() * 2.f * kPi, el = (rng.next() - 0.3f) * 1.4f, spd = sc.radius * (2.2f + 3.2f * rng.next()), sz = 0.6f + 0.8f * rng.next(),
                  spin = rng.next() * 6.f, hue = rng.next();
      const float lk = su / 1.0f;
      if (lk >= 1.f) continue;
      const V3 dirW = V3{std::cos(az) * std::cos(el), std::sin(el), std::sin(az) * std::cos(el)};
      const V3 dirV = Norm(V3{Dot(V3{sc.view[0][0], sc.view[0][1], sc.view[0][2]}, dirW), Dot(V3{sc.view[1][0], sc.view[1][1], sc.view[1][2]}, dirW),
                              Dot(V3{sc.view[2][0], sc.view[2][1], sc.view[2][2]}, dirW)});
      const float d = spd * EaseOut(std::min(1.f, su / 0.9f));
      const V3 q = sc.c + dirV * d - sc.up * (sc.radius * 1.5f * su * su);
      ImVec2 sp;
      if (!p.At(q, &sp)) continue;
      const float r = Rs * 0.2f * sz, th = spin + su * 7.f, a = (1.f - lk) * (1.f - lk);
      const ImVec2 q1(sp.x + std::cos(th) * r, sp.y + std::sin(th) * r), q2(sp.x + std::cos(th + 2.4f) * r * 0.7f, sp.y + std::sin(th + 2.4f) * r * 0.7f),
          q3(sp.x + std::cos(th + 4.2f) * r * 0.55f, sp.y + std::sin(th + 4.2f) * r * 0.55f);
      p.dl->AddTriangleFilled(q1, q2, q3, Hue(hue, 0.75f * a, 0.25f));
      p.dl->AddTriangle(q1, q2, q3, White(0.9f * a), std::max(1.f, 1.2f * p.s));
    }
    for (int k = 0; k < 2; k++) {
      const float ku = (t - 0.05f - 0.16f * k) / 0.8f;
      if (ku <= 0.f || ku >= 1.f) continue;
      const float rad = sc.radius * (1.f + 4.5f * EaseOut(ku)), a = std::pow(1.f - ku, 1.5f);
      ImVec2 prev{};
      bool hp = false;
      const V3 ax = Norm(V3{sc.view[0][0], sc.view[1][0], sc.view[2][0]}), az = Norm(V3{sc.view[0][2], sc.view[1][2], sc.view[2][2]});
      for (int i = 0; i <= 64; i++) {
        const float ang = i * 2.f * kPi / 64.f;
        const V3 q = sc.feet + ax * (std::cos(ang) * rad) + az * (std::sin(ang) * rad);
        ImVec2 sp;
        if (!p.At(q, &sp)) { hp = false; continue; }
        if (hp) {
          const ImU32 col = primal ? Rgba(tint[0], tint[1], tint[2], a) : Hue(static_cast<float>(i) / 64.f + k * 0.3f + t * 0.3f, a, 0.8f);
          p.dl->AddLine(prev, sp, col, std::max(1.5f, 7.f * p.s * (1.f - ku)));
          p.dl->AddLine(prev, sp, White(a * 0.8f), std::max(1.f, 2.f * p.s * (1.f - ku)));
        }
        prev = sp;
        hp = true;
      }
    }
    p.Sprite(sGlow, C, Rs * 2.2f, primal ? Rgba(tint[0], tint[1], tint[2], 0.6f * (1.f - Smooth((t - 0.1f) / 1.0f))) : White(0.55f * (1.f - Smooth((t - 0.1f) / 1.0f))));
    for (int i = 0; i < 36; i++) {
      const float a0 = rng.next() * 2.f * kPi, far = 0.6f + 2.6f * rng.next(), hue = rng.next();
      const float u = Clamp01(t / 1.1f);
      const ImVec2 q(C.x + std::cos(a0) * Rs * (0.3f + far * EaseOut(u)), C.y + std::sin(a0) * Rs * 0.8f * (0.3f + far * EaseOut(u)));
      p.Sprite(sStar, q, Rs * 0.14f * (1.f - u * 0.5f), Hue(hue, (1.f - u) * 0.95f, 0.4f), a0 + t * 3.f);
    }
    const float ue = (t - 0.08f) / 0.3f;
    if (ue > 0.f && t < 1.1f) {
      const float grow = EaseOutBack(ue), fade = 0.8f * (1.f - Smooth((t - 0.55f) / 0.45f));
      const float er = Rs * 0.95f * (0.6f + 0.4f * grow + 0.12f * Clamp01(t - 0.4f));
      p.Sprite(sGlow, C, er * 1.5f, primal ? Rgba(tint[0], tint[1], tint[2], 0.45f * fade) : Hue(t * 0.3f, 0.35f * fade, 0.3f));
      p.Sprite(sEmblem, C, er, primal ? Rgba(0.6f + 0.4f * tint[0], 0.6f + 0.4f * tint[1], 0.6f + 0.4f * tint[2], fade) : White(fade), 0.25f * (1.f - grow));
    }
    if (flash > 0.f) p.dl->AddRectFilled(ImVec2(0, 0), size, White(0.88f * flash));
  }
}

void DrawUsum(const Painter& p, const Scene& sc, const Show& sh, float t, ImVec2 C, float Rpx, float Rs, const float tint[3], bool primal);
bool UsumWanted();

void Draw() {
  Show sh;
  { std::lock_guard<std::mutex> l(sLock); sh = sShow; }
  const bool other = PortPhoto_Active() || PortPhoto_LiveActive() || PortFreeze_Active();
  if (sh.phase == kNone) {
    if (sOverrideOn && !other) { M34 id; Identity(id); SetOverride(id, false); }
    return;
  }
  const double now = GameNow();
  MakeTextures();
  PortFreeze_Capture();
  if (sDrawnSerial != sh.serial) {
    sDrawnSerial = sh.serial;
    sHaveProj = false;
    M34 id;
    Identity(id);
    std::memcpy(sCur, id, sizeof sCur);
    std::memcpy(sPrev, id, sizeof sPrev);
  }
  Scene sc;
  const bool have = Gather(sh.fop, sh.trainerSeq, sc);
  float t = 0.f;
  bool end = false;
  if (sh.phase == kReact) { t = static_cast<float>(now - sh.tReact); end = t > 9.f; }
  if (sh.phase == kLink) { t = static_cast<float>(now - sh.tLink); end = t > 9.f; }
  if (sh.phase == kBurst) { t = static_cast<float>(now - sh.tBurst); end = t > 2.8f; }
  if (!have && Log()) {
    V3 mn, mx;
    void* seq = fightOutPokemon_GetSequencePtr(sh.fop);
    void* model = seq != nullptr ? Sequence_GetGSmodel(seq) : nullptr;
    const bool ok = Bounds(seq, &mn, &mx);
    MegaFxVec a{}, b{};
    if (model != nullptr && GSmodelGetBound(model) != nullptr) GSboundGetMinMax(GSmodelGetBound(model), &a, &b, true, true, true, false);
    std::fprintf(stderr, "[mega fx] nothing to show: camera %p sequence %p model %p ok %d bound %.2f %.2f %.2f .. %.2f %.2f %.2f\n", GSgfxMatrixGetPtr(2), seq, model,
                 ok ? 1 : 0, a.x, a.y, a.z, b.x, b.y, b.z);
  }
  if (end || !have) {
    if (end) {
      std::lock_guard<std::mutex> l(sLock);
      if (sShow.serial == sh.serial) sShow.phase = kNone;
    }
    if (sOverrideOn && !other) { M34 id; Identity(id); SetOverride(id, false); }
    return;
  }
  float tint[3] = {1.f, 1.f, 1.f};
  if (sh.primal == 1) { tint[0] = 0.35f; tint[1] = 0.6f; tint[2] = 1.f; }
  if (sh.primal == 2) { tint[0] = 1.f; tint[1] = 0.4f; tint[2] = 0.25f; }
  if (sh.primal == 3) { tint[0] = 0.8f; tint[1] = 0.5f; tint[2] = 1.f; }
  const bool primal = sh.primal != 0;
  CamMove cm;
  cm.target = sc.c;
  cm.radius = sc.radius;
  cm.up = sc.up;
  const bool keyInView = sc.trainer && sc.key.z < -1e-3f;
  if (sh.phase == kReact) {
    cm.aim = 0.55f * Smooth(t / 1.2f);
    cm.dolly = 0.16f * Smooth(t / 1.6f);
  } else if (sh.phase == kLink) {
    const float toMon = Smooth((t - 0.5f) / 1.0f);
    cm.target = keyInView ? Lerp((sc.key + sc.c) * 0.5f, sc.c, toMon) : sc.c;
    cm.aim = 0.4f + 0.5f * Smooth(t / 1.4f);
    cm.dolly = 0.3f * Smooth((t - 0.5f) / 1.7f);
    cm.orbit = (sh.fop != nullptr ? 1.f : 0.f) * 8.f * Smooth((t - 0.6f) / 1.8f);
  } else {
    const float back = Smooth((t - 0.7f) / 1.6f);
    cm.aim = 0.6f * (1.f - back);
    cm.dolly = 0.2f * (1.f - back);
    const float sk = t < 0.45f ? (1.f - t / 0.45f) : 0.f;
    cm.shakeX = 0.018f * sk * std::sin(t * 93.f);
    cm.shakeY = 0.014f * sk * std::cos(t * 71.f);
  }
  if (!other && CameraWanted()) {
    size_t partial = 0;
    PortFreeze_PictureCount(&partial);
    sFieldsSinceSet++;
    if (partial <= 256 || sFieldsSinceSet >= 3 || !sOverrideOn) {
      M34 o;
      CameraMatrix(cm, o);
      SetOverride(o, true);
    }
  } else if (!other && !sOverrideOn) {
    M34 id;
    Identity(id);
    SetOverride(id, true);
  }
  Painter p{};
  p.dl = ImGui::GetBackgroundDrawList();
  PortMenu_PictureRect(&p.x0, &p.y0, &p.s);
  p.o = other ? nullptr : &ShownOverride();
  if (sHaveProj) {
    p.p11 = sProj[1][1];
    p.p12 = sProj[1][2];
    p.p00 = p.p11 * 0.75f;
    p.p02 = sProj[0][2];
  } else {
    p.p11 = 2.414f;
    p.p00 = p.p11 * 0.75f;
    p.p02 = p.p12 = 0.f;
  }
  ImVec2 C;
  if (!p.At(sc.c, &C)) {
    if (Log()) std::fprintf(stderr, "[mega fx] phase %d t %.2f: the Pokemon is behind the camera (view %.1f %.1f %.1f, radius %.1f)\n", sh.phase, t,
                            sc.c.x, sc.c.y, sc.c.z, sc.radius);
    return;
  }
  const float Rpx = std::max(14.f * p.s, p.Size(sc.c, sc.radius));
  const float Rs = std::clamp(Rpx * 0.8f, 30.f * p.s, 125.f * p.s);
  if (Log()) {
    ImVec2 K(0, 0);
    const bool k = sc.trainer && p.At(sc.key, &K);
    std::fprintf(stderr, "[mega fx] phase %d t %.2f mon (%.0f %.0f) r %.0f px key %d (%.0f %.0f) proj %d p11 %.3f p00/p11 %.3f\n", sh.phase, t, (C.x - p.x0) / p.s,
                 (C.y - p.y0) / p.s, Rpx / p.s, k ? 1 : 0, (K.x - p.x0) / p.s, (K.y - p.y0) / p.s, sHaveProj ? 1 : 0, sProj[1][1],
                 sProj[1][1] != 0.f ? sProj[0][0] / sProj[1][1] : 0.f);
  }
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  p.dl->PushClipRect(ImVec2(0, 0), size, false);
  if (std::getenv("XD_MEGA_FX_MARKS") != nullptr) {
    p.dl->AddCircle(C, Rpx, IM_COL32(255, 0, 0, 255), 32, 2.f);
    ImVec2 K;
    if (sc.trainer && p.At(sc.key, &K)) p.dl->AddCircle(K, 8.f * p.s, IM_COL32(0, 255, 0, 255), 16, 2.f);
  }

  if (UsumWanted()) DrawUsum(p, sc, sh, t, C, Rpx, Rs, tint, primal);
  else DrawBeta(p, sc, sh, t, C, Rpx, Rs, tint, primal);
  p.dl->PopClipRect();
}

struct UTex {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  std::map<int, ImTextureID> ids;
};
struct UCoord {
  int tex = -1, wu = 0, wv = 0, set = 0;
};
struct USub {
  std::string mesh, mat;
  int blend = 0;
  std::vector<UCoord> coords;
  int nv = 0, nt = 0;
  std::vector<uint8_t> col;
  std::vector<float> uv[2];
  std::vector<uint16_t> tri;
  std::vector<uint8_t> vis;
  std::vector<float> pos;
  std::vector<float> xf;
};
struct UModel {
  std::string name;
  int frames = 1;
  std::vector<USub> subs;
};
struct UEmitter {
  std::string name;
  std::vector<int> tex;
  float col[3][4] = {};
};
struct USet {
  std::string name;
  std::vector<UEmitter> em;
};
struct Usum {
  bool tried = false, ok = false;
  std::vector<UTex> tex;
  std::vector<UModel> models;
  std::vector<USet> sets;
  const UModel* Model(const char* part) const {
    for (auto& m : models) if (m.name.find(part) != std::string::npos) return &m;
    return nullptr;
  }
  const USet* Set(const char* part) const {
    for (auto& s : sets) if (s.name.find(part) != std::string::npos) return &s;
    return nullptr;
  }
};
Usum sUsum;

struct Reader {
  const std::vector<uint8_t>& b;
  size_t p = 0;
  bool bad = false;
  bool Need(size_t n) { if (p + n > b.size()) bad = true; return !bad; }
  uint32_t U8() { return Need(1) ? b[p++] : 0; }
  uint32_t U16() { if (!Need(2)) return 0; const uint32_t v = b[p] | b[p + 1] << 8; p += 2; return v; }
  uint32_t U32() { if (!Need(4)) return 0; const uint32_t v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | static_cast<uint32_t>(b[p + 3]) << 24; p += 4; return v; }
  int I16() { return static_cast<int16_t>(U16()); }
  float F32() { const uint32_t v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
  std::string Str() { const uint32_t n = U8(); if (!Need(n)) return {}; std::string s(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + n)); p += n; return s; }
  template <class T> void Bytes(std::vector<T>& out, size_t count) {
    if (!Need(count * sizeof(T))) return;
    out.resize(count);
    std::memcpy(out.data(), b.data() + p, count * sizeof(T));
    p += count * sizeof(T);
  }
};

void LoadUsum() {
  if (sUsum.tried) return;
  sUsum.tried = true;
  const std::filesystem::path path = xd::gen57::PackFolder() / L"mega" / L"usum_mega.bin";
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "[mega fx] no %s: the Gen 5-7 pack was made before it had the Ultra Sun / Ultra Moon effect; the show uses its own drawings\n", path.string().c_str());
    return;
  }
  const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  Reader r{b};
  if (!r.Need(8) || std::memcmp(b.data(), "XDMEGA01", 8) != 0) return;
  r.p = 8;
  if (r.U32() != 1) return;
  const uint32_t nt = r.U32(), nm = r.U32(), ns = r.U32();
  if (nt > 512 || nm > 64 || ns > 256) return;
  sUsum.tex.resize(nt);
  for (auto& t : sUsum.tex) {
    r.Str();
    t.w = static_cast<int>(r.U16());
    t.h = static_cast<int>(r.U16());
    if (t.w <= 0 || t.h <= 0 || t.w > 1024 || t.h > 1024) return;
    r.Bytes(t.px, static_cast<size_t>(t.w) * t.h * 4);
  }
  sUsum.models.resize(nm);
  for (auto& m : sUsum.models) {
    m.name = r.Str();
    m.frames = static_cast<int>(r.U32());
    const uint32_t nsub = r.U32();
    if (m.frames < 1 || m.frames > 1000 || nsub > 64) return;
    m.subs.resize(nsub);
    for (auto& s : m.subs) {
      s.mesh = r.Str();
      s.mat = r.Str();
      s.blend = static_cast<int>(r.U8());
      const uint32_t nc = r.U8();
      s.coords.resize(nc);
      for (auto& c : s.coords) {
        c.tex = r.I16();
        c.wu = static_cast<int>(r.U8());
        c.wv = static_cast<int>(r.U8());
        c.set = static_cast<int>(r.U8());
        if (c.tex >= static_cast<int>(nt)) c.tex = -1;
      }
      s.nv = static_cast<int>(r.U32());
      s.nt = static_cast<int>(r.U32());
      if (s.nv < 0 || s.nv > 65535 || s.nt < 0 || s.nt > 65535) return;
      s.col.resize(static_cast<size_t>(s.nv) * 4);
      s.uv[0].resize(static_cast<size_t>(s.nv) * 2);
      s.uv[1].resize(static_cast<size_t>(s.nv) * 2);
      for (int k = 0; k < s.nv; k++) {
        for (int e = 0; e < 4; e++) s.col[static_cast<size_t>(k) * 4 + e] = static_cast<uint8_t>(r.U8());
        for (int set = 0; set < 2; set++) {
          s.uv[set][static_cast<size_t>(k) * 2] = r.F32();
          s.uv[set][static_cast<size_t>(k) * 2 + 1] = r.F32();
        }
      }
      r.Bytes(s.tri, static_cast<size_t>(s.nt) * 3);
      for (uint16_t i : s.tri) if (i >= s.nv) return;
      s.vis.resize(static_cast<size_t>(m.frames));
      s.pos.resize(static_cast<size_t>(m.frames) * s.nv * 3);
      s.xf.resize(static_cast<size_t>(m.frames) * nc * 5);
      for (int fr = 0; fr < m.frames; fr++) {
        s.vis[static_cast<size_t>(fr)] = static_cast<uint8_t>(r.U8());
        for (size_t k = 0; k < static_cast<size_t>(s.nv) * 3; k++) s.pos[static_cast<size_t>(fr) * s.nv * 3 + k] = r.F32();
        for (size_t k = 0; k < nc * 5; k++) s.xf[static_cast<size_t>(fr) * nc * 5 + k] = r.F32();
      }
      if (r.bad) return;
    }
  }
  sUsum.sets.resize(ns);
  for (auto& s : sUsum.sets) {
    s.name = r.Str();
    const uint32_t ne = r.U32();
    if (ne > 64) return;
    s.em.resize(ne);
    for (auto& e : s.em) {
      e.name = r.Str();
      const uint32_t n = r.U8();
      for (uint32_t k = 0; k < n; k++) { const int t = r.I16(); if (t >= 0 && t < static_cast<int>(nt)) e.tex.push_back(t); }
      for (auto& c : e.col) for (float& x : c) x = r.F32();
    }
  }
  if (r.bad) return;
  sUsum.ok = true;
  std::fprintf(stderr, "[mega fx] Ultra Sun / Ultra Moon effect %s: %u textures, %u models, %u particle sets\n", path.string().c_str(), nt, nm, ns);
}

bool UsumWanted() {
  static int beta = -1;
  if (beta < 0) { const char* e = std::getenv("XD_MEGA_FX_STYLE"); beta = e != nullptr && std::strcmp(e, "beta") == 0; }
  if (beta == 1) return false;
  LoadUsum();
  return sUsum.ok;
}

constexpr int kTile = 4;

int QuarterCorner(const UTex& t) {
  if (t.w != t.h) return -1;
  auto lum = [&](int x, int y) {
    const uint8_t* s = &t.px[(static_cast<size_t>(y) * t.w + x) * 4];
    return (s[0] + s[1] + s[2]) / 765.f * s[3] / 255.f;
  };
  const float c[4] = {lum(0, 0), lum(t.w - 1, 0), lum(0, t.h - 1), lum(t.w - 1, t.h - 1)};
  for (int k = 0; k < 4; k++)
    if (c[k] > 0.6f && c[3 - k] < 0.08f) return k;
  return -1;
}

ImTextureID TexId(int ti, bool add, int wu, int wv, bool full = false) {
  UTex& t = sUsum.tex[static_cast<size_t>(ti)];
  const int corner = full ? QuarterCorner(t) : -1;
  if (corner >= 0) wu = wv = 0;
  const bool tu = wu == 2 || wu == 3, tv = wv == 2 || wv == 3;
  const int key = (add ? 1 : 0) | (tu ? wu : 0) << 1 | (tv ? wv : 0) << 3 | (corner >= 0 ? 1 : 0) << 5;
  auto it = t.ids.find(key);
  if (it != t.ids.end()) return it->second;
  const int nx = corner >= 0 ? 2 : tu ? kTile : 1, ny = corner >= 0 ? 2 : tv ? kTile : 1, W = t.w * nx, H = t.h * ny;
  std::vector<uint8_t> o(static_cast<size_t>(W) * H * 4);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int sx = x % t.w, sy = y % t.h;
      if (wu == 3 && (x / t.w) % 2 == 1) sx = t.w - 1 - sx;
      if (wv == 3 && (y / t.h) % 2 == 1) sy = t.h - 1 - sy;
      if (corner >= 0) {
        const bool right = x >= t.w, bottom = y >= t.h;
        const bool cr = (corner & 1) != 0, cb = (corner & 2) != 0;
        if (right && cr) sx = t.w - 1 - (x - t.w);
        if (right && !cr) sx = x - t.w;
        if (!right && cr) sx = x;
        if (!right && !cr) sx = t.w - 1 - x;
        if (bottom && cb) sy = t.h - 1 - (y - t.h);
        if (bottom && !cb) sy = y - t.h;
        if (!bottom && cb) sy = y;
        if (!bottom && !cb) sy = t.h - 1 - y;
      }
      const uint8_t* s = &t.px[(static_cast<size_t>(sy) * t.w + sx) * 4];
      uint8_t* d = &o[(static_cast<size_t>(y) * W + x) * 4];
      if (add) {
        const int m = std::max({s[0], s[1], s[2], static_cast<uint8_t>(1)});
        d[0] = static_cast<uint8_t>(s[0] * 255 / m);
        d[1] = static_cast<uint8_t>(s[1] * 255 / m);
        d[2] = static_cast<uint8_t>(s[2] * 255 / m);
        d[3] = static_cast<uint8_t>(s[3] * m / 255);
      } else {
        std::memcpy(d, s, 4);
      }
    }
  const ImTextureID id = aurora_imgui_add_texture(static_cast<uint32_t>(W), static_cast<uint32_t>(H), o.data());
  t.ids[key] = id;
  return id;
}

float WrapF(float x, int mode) {
  if (mode == 2) return x - std::floor(x);
  if (mode == 3) { float f = x - 2.f * std::floor(x * 0.5f); return f > 1.f ? 2.f - f : f; }
  return Clamp01(x);
}
void Sample(int ti, float u, float v, int wu, int wv, float out[4]) {
  const UTex& t = sUsum.tex[static_cast<size_t>(ti)];
  const float x = WrapF(u, wu), y = 1.f - WrapF(v, wv);
  const int ix = std::clamp(static_cast<int>(x * t.w), 0, t.w - 1), iy = std::clamp(static_cast<int>(y * t.h), 0, t.h - 1);
  const uint8_t* s = &t.px[(static_cast<size_t>(iy) * t.w + ix) * 4];
  for (int e = 0; e < 4; e++) out[e] = s[e] / 255.f;
}
void Slide(const float* x5, float u, float v, float* ou, float* ov) {
  const float su = u * x5[0], sv = v * x5[1], c = std::cos(x5[2]), s = std::sin(x5[2]);
  *ou = su * c - sv * s + x5[3];
  *ov = su * s + sv * c + x5[4];
}

struct Place {
  V3 o, ex, ey, ez;
  V3 At(float x, float y, float z) const { return o + ex * x + ey * y + ez * z; }
};
Place PlaceAt(const Scene& sc, float unitsPerRadius, float lift = 0.f) {
  Place pl;
  const float k = sc.radius / unitsPerRadius;
  V3 toCam = V3{} - sc.c;
  toCam = toCam - sc.up * Dot(toCam, sc.up);
  toCam = Norm(toCam);
  const V3 right = Norm(Cross(sc.up, toCam));
  pl.o = sc.feet + sc.up * (sc.radius * lift);
  pl.ex = right * k;
  pl.ey = sc.up * k;
  pl.ez = toCam * k;
  return pl;
}

void DrawModel(const Painter& p, const UModel* m, float frame, const Place& pl, float alpha, const float tint[3] = nullptr) {
  if (m == nullptr || alpha <= 0.f || frame < 0.f) return;
  if (frame > static_cast<float>(m->frames - 1)) return;
  const int f0 = static_cast<int>(frame), f1 = std::min(f0 + 1, m->frames - 1);
  const float fk = frame - static_cast<float>(f0);
  std::vector<ImVec2> sp;
  std::vector<uint8_t> ok;
  std::vector<float> vc;
  std::vector<float> uvm;
  for (const USub& s : m->subs) {
    if (!s.vis[static_cast<size_t>(f0)] || s.coords.empty() || s.coords[0].tex < 0) continue;
    const size_t nc = s.coords.size();
    const float* x5a = &s.xf[static_cast<size_t>(f0) * nc * 5];
    const float* x5b = &s.xf[static_cast<size_t>(f1) * nc * 5];
    float x5[3][5];
    for (size_t c = 0; c < nc && c < 3; c++)
      for (int e = 0; e < 5; e++) x5[c][e] = x5a[c * 5 + e] + (x5b[c * 5 + e] - x5a[c * 5 + e]) * fk;
    sp.resize(static_cast<size_t>(s.nv));
    ok.assign(static_cast<size_t>(s.nv), 0);
    vc.resize(static_cast<size_t>(s.nv) * 4);
    uvm.resize(static_cast<size_t>(s.nv) * 2);
    const float* pa = &s.pos[static_cast<size_t>(f0) * s.nv * 3];
    const float* pb = &s.pos[static_cast<size_t>(f1) * s.nv * 3];
    for (int k = 0; k < s.nv; k++) {
      const float x = pa[k * 3] + (pb[k * 3] - pa[k * 3]) * fk, y = pa[k * 3 + 1] + (pb[k * 3 + 1] - pa[k * 3 + 1]) * fk,
                  z = pa[k * 3 + 2] + (pb[k * 3 + 2] - pa[k * 3 + 2]) * fk;
      ok[static_cast<size_t>(k)] = p.At(pl.At(x, y, z), &sp[static_cast<size_t>(k)]) ? 1 : 0;
      float c4[4];
      for (int e = 0; e < 4; e++) c4[e] = s.col[static_cast<size_t>(k) * 4 + e] / 255.f;
      for (size_t c = 0; c < nc && c < 3; c++) {
        const UCoord& cc = s.coords[c];
        const float* uv = &s.uv[cc.set][static_cast<size_t>(k) * 2];
        float u, v;
        Slide(x5[c], uv[0], uv[1], &u, &v);
        if (c == 0) { uvm[static_cast<size_t>(k) * 2] = u; uvm[static_cast<size_t>(k) * 2 + 1] = v; continue; }
        if (cc.tex < 0) continue;
        float t4[4];
        Sample(cc.tex, u, v, cc.wu, cc.wv, t4);
        for (int e = 0; e < 4; e++) c4[e] *= t4[e];
      }
      if (tint != nullptr) for (int e = 0; e < 3; e++) c4[e] *= tint[e];
      c4[3] *= alpha;
      for (int e = 0; e < 4; e++) vc[static_cast<size_t>(k) * 4 + e] = c4[e];
    }
    const UCoord& mc = s.coords[0];
    const bool add = s.blend == 1 || s.blend == 3;
    p.dl->PushTextureID(TexId(mc.tex, add, mc.wu, mc.wv));
    const bool tu = mc.wu == 2 || mc.wu == 3, tv = mc.wv == 2 || mc.wv == 3;
    for (int t = 0; t < s.nt; t++) {
      const uint16_t* ix = &s.tri[static_cast<size_t>(t) * 3];
      if (!ok[ix[0]] || !ok[ix[1]] || !ok[ix[2]]) continue;
      float us[3], ws[3];
      for (int e = 0; e < 3; e++) { us[e] = uvm[ix[e] * 2u]; ws[e] = 1.f - uvm[ix[e] * 2u + 1]; }
      auto fit = [](float* a, bool tiled, int mode) {
        if (!tiled) { for (int e = 0; e < 3; e++) a[e] = Clamp01(a[e]); return; }
        const float mn = std::min({a[0], a[1], a[2]});
        const float step = mode == 3 ? 2.f : 1.f;
        const float sh = step * std::floor(mn / step);
        for (int e = 0; e < 3; e++) a[e] = std::clamp(a[e] - sh, 0.f, static_cast<float>(kTile)) / kTile;
      };
      fit(us, tu, mc.wu);
      fit(ws, tv, mc.wv);
      p.dl->PrimReserve(3, 3);
      const ImDrawIdx base = static_cast<ImDrawIdx>(p.dl->_VtxCurrentIdx);
      for (int e = 0; e < 3; e++) {
        const float* c4 = &vc[ix[e] * 4u];
        const ImU32 col = s.blend == 3 ? Rgba(0.f, 0.f, 0.f, c4[3]) : Rgba(std::min(1.f, c4[0]), std::min(1.f, c4[1]), std::min(1.f, c4[2]), c4[3]);
        p.dl->PrimWriteVtx(sp[ix[e]], ImVec2(us[e], ws[e]), col);
      }
      p.dl->PrimWriteIdx(base);
      p.dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
      p.dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
    }
    p.dl->PopTextureID();
  }
}

const UEmitter* Em(const char* set, const char* emitter) {
  const USet* s = sUsum.Set(set);
  if (s == nullptr) return nullptr;
  for (auto& e : s->em) if (e.name.find(emitter) != std::string::npos) return &e;
  return s->em.empty() ? nullptr : &s->em[0];
}
ImU32 EmCol(const UEmitter* e, int which, float a, float boost = 1.f) {
  const float* c = e->col[std::clamp(which, 0, 2)];
  return Rgba(std::min(1.f, c[0] * boost), std::min(1.f, c[1] * boost), std::min(1.f, c[2] * boost), a);
}
void EmSprite(const Painter& p, const UEmitter* e, int texSlot, ImVec2 c, float hw, float hh, ImU32 col, float rot = 0.f, bool add = true, int cell = -1,
              int gridSide = 1) {
  if (e == nullptr || e->tex.empty() || hw <= 0.f || hh <= 0.f) return;
  const int ti = e->tex[static_cast<size_t>(std::min<int>(texSlot, static_cast<int>(e->tex.size()) - 1))];
  const UTex& t = sUsum.tex[static_cast<size_t>(ti)];
  int cols = 1, rows = 1;
  if (t.w > t.h) cols = t.w / t.h;
  else if (t.h > t.w) rows = t.h / t.w;
  else cols = rows = std::max(1, gridSide);
  ImVec2 a(0, 0), b(1, 1);
  if (cell >= 0 && cols * rows > 1) {
    const int k = cell % (cols * rows), cx = k % cols, cy = k / cols;
    a = ImVec2(static_cast<float>(cx) / cols, static_cast<float>(cy) / rows);
    b = ImVec2(static_cast<float>(cx + 1) / cols, static_cast<float>(cy + 1) / rows);
  }
  const bool whole = cell < 0 || cols * rows == 1;
  const float cs = std::cos(rot), sn = std::sin(rot);
  auto P = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
  p.dl->AddImageQuad(TexId(ti, add, 0, 0, whole), P(-hw, -hh), P(hw, -hh), P(hw, hh), P(-hw, hh), ImVec2(a.x, a.y), ImVec2(b.x, a.y), ImVec2(b.x, b.y), ImVec2(a.x, b.y), col);
}
void EmRing(const Painter& p, const UEmitter* e, V3 centre, V3 ax, V3 az, V3 up, float radius, float height, float turn, ImU32 col) {
  if (e == nullptr || e->tex.empty()) return;
  const ImTextureID id = TexId(e->tex[0], true, 0, 0);
  const int n = 40;
  p.dl->PushTextureID(id);
  for (int i = 0; i < n; i++) {
    const float a0 = turn + i * 2.f * kPi / n, a1 = turn + (i + 1) * 2.f * kPi / n;
    const V3 b0 = centre + ax * (std::cos(a0) * radius) + az * (std::sin(a0) * radius), b1 = centre + ax * (std::cos(a1) * radius) + az * (std::sin(a1) * radius);
    ImVec2 q[4];
    const V3 pts[4] = {b0, b1, b1 + up * height, b0 + up * height};
    bool okq = true;
    for (int k = 0; k < 4; k++) okq = okq && p.At(pts[k], &q[k]);
    if (!okq) continue;
    const float u0 = static_cast<float>(i) / n, u1 = static_cast<float>(i + 1) / n;
    p.dl->PrimReserve(6, 4);
    p.dl->PrimQuadUV(q[0], q[1], q[2], q[3], ImVec2(u0, 1), ImVec2(u1, 1), ImVec2(u1, 0), ImVec2(u0, 0), col);
  }
  p.dl->PopTextureID();
}

void DrawUsum(const Painter& p, const Scene& scene, const Show& sh, float t, ImVec2 C, float Rpx, float Rs, const float tint[3], bool primal) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  Scene sc = scene;
  sc.radius *= std::clamp(Rs / std::max(Rpx, 1.f), 0.15f, 1.f);
  const float fps = 30.f;
  const float* tn = primal ? tint : nullptr;
  ImVec2 S;
  if (!p.At(sc.c + sc.up * (sc.radius * 0.35f), &S)) S = C;
  if (sh.phase == kReact) {
    const UEmitter* st = Em("c00_at_start01", "base01_add");
    const UEmitter* stn = Em("c00_at_start01", "base01_nor");
    const float burst = Clamp01(t / 0.5f);
    if (st && t < 0.6f) {
      EmSprite(p, stn, 0, S, Rs * (0.3f + 1.4f * EaseOut(burst)), Rs * (0.3f + 1.4f * EaseOut(burst)), EmCol(stn, 0, 0.9f * (1.f - burst), 1.6f), t * 2.f);
      EmSprite(p, st, 0, S, Rs * 0.8f * (0.5f + burst), Rs * 0.8f * (0.5f + burst), EmCol(st, 0, 1.f - burst, 1.3f), -t * 3.f);
    }
    const UEmitter* stone = Em("c01_bg_stone01", "base01");
    if (stone) {
      Rng rng{0x5701u + sh.serial};
      for (int i = 0; i < 10; i++) {
        const float a0 = rng.next() * 2.f * kPi, sp = 0.25f + 0.3f * rng.next(), sz = 0.18f + 0.16f * rng.next(), spin = (rng.next() - 0.5f) * 3.f;
        const float u = Clamp01((t - 0.15f - 0.05f * i) * sp);
        const float rr = Rs * (0.6f + 2.4f * EaseOut(u));
        const ImVec2 q(C.x + std::cos(a0) * rr, C.y + std::sin(a0) * rr * 0.7f);
        const float a = Smooth(u * 6.f) * (1.f - Smooth((t - 1.6f) / 0.4f));
        EmSprite(p, stone, 0, q, Rs * sz, Rs * sz, EmCol(stone, 0, 0.9f * a, 1.1f), spin * t, false, i % 4, 2);
      }
    }
    DrawModel(p, sUsum.Model("c01_aurastart01"), (t - 0.15f) * fps, PlaceAt(sc, 30.f), 1.f, tn);
    const UEmitter* fire = Em("c01_cam_fire01", "fire01");
    if (fire) {
      Rng rng{0xF12Eu + sh.serial};
      for (int i = 0; i < 14; i++) {
        const float side = (i % 2) ? 1.f : -1.f, ph = rng.next(), sp = 0.7f + 0.5f * rng.next();
        const float life = std::fmod(t * sp + ph, 1.f);
        const ImVec2 q(p.x0 + (320.f + side * (250.f + 70.f * rng.next())) * p.s, p.y0 + (480.f - 520.f * life) * p.s);
        EmSprite(p, fire, i % 2, q, 70.f * p.s, 110.f * p.s, EmCol(fire, i % 3, 0.55f * std::sin(life * kPi) * Smooth(t / 0.5f), 1.2f), side * 0.2f);
      }
    }
    if (t < 0.35f) {
      const float a = t < 0.06f ? t / 0.06f : 1.f - Smooth((t - 0.06f) / 0.29f);
      p.dl->AddRectFilled(ImVec2(0, 0), size, White(0.8f * a));
    }
  } else if (sh.phase == kLink) {
    ImVec2 K;
    if (!primal && sc.trainer && p.At(sc.key, &K)) {
      const UEmitter* st = Em("c00_at_start01", "base01_add");
      const float u = Clamp01(t / 0.7f);
      if (st) {
        EmSprite(p, st, 0, K, 60.f * p.s * (0.4f + EaseOut(u)), 60.f * p.s * (0.4f + EaseOut(u)), EmCol(st, 0, 1.f - u, 1.4f), t * 3.f);
        EmSprite(p, st, 0, K, 26.f * p.s, 26.f * p.s, White(0.9f * (1.f - Smooth((t - 0.8f) / 0.6f))), -t * 2.f);
      }
    }
    const float speed = 90.f / 2.2f;
    const Place pl = PlaceAt(sc, 30.f);
    DrawModel(p, sUsum.Model("c02_bg_wind01_in"), t * speed, pl, 0.7f, tn);
    DrawModel(p, sUsum.Model("c02_lineroll01_in"), t * speed, pl, 1.f, tn);
    DrawModel(p, sUsum.Model("c02_bg_wind01_out"), (t - 1.1f) * fps * 1.5f, pl, 0.8f, tn);
    const UEmitter* en = Em("c02_at_enagy01_root", "srash_up01_nor");
    if (en) {
      Rng rng{0xE7E5u + sh.serial};
      for (int i = 0; i < 16; i++) {
        const float a0 = rng.next() * 2.f * kPi, ph = rng.next(), sp = 0.9f + 0.6f * rng.next();
        const float life = std::fmod(t * sp + ph, 1.f);
        const V3 at = sc.feet + V3{std::cos(a0) * sc.radius * 0.9f, sc.radius * (0.2f + 2.4f * life), std::sin(a0) * sc.radius * 0.9f};
        ImVec2 q;
        if (!p.At(at, &q)) continue;
        const float a = 0.8f * std::sin(life * kPi) * Smooth(t / 0.3f) * (1.f - Smooth((t - 1.9f) / 0.3f));
        EmSprite(p, en, 0, q, Rs * 0.25f, Rs * 0.4f, primal ? Rgba(tint[0], tint[1], tint[2], a) : Hue(rng.next(), a, 0.45f), 0.f, true, static_cast<int>(life * 16.f), 4);
      }
    }
    const UEmitter* ring = Em("c02_at_ringroll01", "ring01_add");
    if (ring) {
      const float u = std::fmod(t / 1.1f, 1.f);
      const V3 ax = Norm(V3{sc.view[0][0], sc.view[1][0], sc.view[2][0]}), az = Norm(V3{sc.view[0][2], sc.view[1][2], sc.view[2][2]});
      EmRing(p, ring, sc.feet + sc.up * (sc.radius * 2.2f * u), ax, az, sc.up, sc.radius * (1.3f - 0.3f * u), sc.radius * 0.35f, t * 3.f,
             primal ? Rgba(tint[0], tint[1], tint[2], 0.8f * std::sin(u * kPi)) : EmCol(ring, 0, 0.8f * std::sin(u * kPi) * (1.f - Smooth((t - 1.8f) / 0.3f)), 1.3f));
    }
    const float tb = t - 1.25f;
    const UEmitter* ap = Em("c03_at_ball_appea01", "ball01_add");
    const UEmitter* fl = Em("c03_at_ball_appea01", "flash01_add");
    if (tb > -0.2f && fl) {
      const float u = Clamp01((tb + 0.2f) / 0.35f);
      EmSprite(p, fl, 0, C, Rs * (0.5f + 1.8f * u), Rs * (0.5f + 1.8f * u), EmCol(fl, 0, 0.9f * std::sin(u * kPi), 1.2f));
    }
    {
      Place pb = PlaceAt(sc, 26.f);
      pb.o = sc.c;
      DrawModel(p, sUsum.Model("c03_ball_start01"), tb * fps * 1.6f, pb, 0.85f, tn);
    }
    if (tb > 0.f && ap) EmSprite(p, ap, 0, C, Rs * 1.25f, Rs * 1.25f, EmCol(ap, 0, 0.85f * Smooth(tb / 0.4f), 1.1f), t * 0.8f);
    if (sJingleSerial != sh.serial && t >= std::max(0.f, kChargeSeconds - sJingleClimax)) {
      sJingleSerial = sh.serial;
      PlayJingle();
    }
  } else {
    if (sJingleSerial != sh.serial) {
      sJingleSerial = sh.serial;
      PlayJingle();
    }
    const UEmitter* ap = Em("c03_at_ball_appea01", "ball01_add");
    if (ap && t < 0.25f) EmSprite(p, ap, 0, C, Rs * 1.25f, Rs * 1.25f, EmCol(ap, 0, 0.85f * (1.f - t / 0.25f), 1.1f));
    {
      Place pb = PlaceAt(sc, 30.f);
      pb.o = sc.c;
      DrawModel(p, sUsum.Model("c04_ball_break01"), t * fps, pb, 1.f - Smooth((t - 1.5f) / 0.5f), tn);
    }
    const UEmitter* bb = Em("c04_at_ballbreak01", "base01_add");
    if (bb) {
      Rng rng{0xBB01u + sh.serial};
      for (int i = 0; i < 26; i++) {
        const float a0 = rng.next() * 2.f * kPi, far = 0.8f + 2.4f * rng.next();
        const float u = Clamp01(t / 0.9f);
        const ImVec2 q(C.x + std::cos(a0) * Rs * far * EaseOut(u), C.y + std::sin(a0) * Rs * far * 0.8f * EaseOut(u));
        EmSprite(p, bb, 0, q, Rs * 0.12f * (1.f - 0.5f * u), Rs * 0.12f * (1.f - 0.5f * u), EmCol(bb, 0, 1.f - u, 1.2f), a0);
      }
    }
    const float tm = t - 0.35f;
    const UEmitter* ma = Em("c06_bg_markaura01", "bubble01");
    if (ma && tm > 0.f) {
      Rng rng{0xB0B1u + sh.serial};
      for (int i = 0; i < 12; i++) {
        const float a0 = rng.next() * 2.f * kPi, ph = rng.next(), sp = 0.6f + 0.6f * rng.next(), sz = 0.12f + 0.12f * rng.next();
        const float life = std::fmod(tm * sp + ph, 1.f);
        const ImVec2 q(C.x + std::cos(a0) * Rs * 1.3f, C.y + Rs * (0.9f - 2.f * life));
        const float a = 0.7f * std::sin(life * kPi) * Smooth(tm / 0.3f) * (1.f - Smooth((tm - 1.4f) / 0.5f));
        EmSprite(p, ma, 0, q, Rs * sz, Rs * sz, EmCol(ma, 0, a), 0.f, true, static_cast<int>(life * 8.f));
      }
    }
    {
      const float perUnit = p.Size(sc.c, 1.f);
      if (perUnit > 0.f) {
        const float k = (Rs * 1.7f / 50.f) / perUnit;
        Place pm;
        pm.o = sc.c;
        pm.ex = V3{k, 0.f, 0.f};
        pm.ey = V3{0.f, k, 0.f};
        pm.ez = V3{0.f, 0.f, k};
        const float fade = 1.f - Smooth((tm - 1.4f) / 0.4f);
        DrawModel(p, sUsum.Model("c06_megamark01"), tm * fps, pm, fade, tn);
      }
    }
    const UEmitter* sh1 = Em("c06_at_shine01_root", "base01_nor");
    if (sh1 && tm > 0.f) {
      const float u = Clamp01(tm / 0.6f);
      EmSprite(p, sh1, 0, C, Rs * (0.6f + 1.4f * u), Rs * (0.6f + 1.4f * u), EmCol(sh1, 0, 0.9f * (1.f - u)), tm);
    }
    const UEmitter* storm = Em("c06_cam_storm01", "base01_add");
    if (storm && tm > 0.f) {
      Rng rng{0x5702u + sh.serial};
      for (int i = 0; i < 30; i++) {
        const float a0 = rng.next() * 2.f * kPi, sp = 0.5f + rng.next(), ph = rng.next();
        const float life = std::fmod(tm * sp + ph, 1.f);
        const float rr = (60.f + 340.f * life) * p.s;
        const ImVec2 q(C.x + std::cos(a0 + tm) * rr, C.y + std::sin(a0 + tm) * rr * 0.75f);
        EmSprite(p, storm, 0, q, 9.f * p.s, 9.f * p.s, EmCol(storm, 0, 0.8f * std::sin(life * kPi) * (1.f - Smooth((tm - 1.3f) / 0.4f)), 1.2f));
      }
    }
    const float flash = t < 0.05f ? t / 0.05f : (t < 0.12f ? 1.f : 1.f - Smooth((t - 0.12f) / 0.35f));
    if (flash > 0.f) p.dl->AddRectFilled(ImVec2(0, 0), size, White(0.85f * flash));
  }
  (void)Rpx;
}

struct Register {
  Register() { PortVideo_SetFrameDrawer(&Draw); }
} sRegister;
}

namespace xd::megafx {

bool Enabled() { return es::Global().Bool("video.mega_effect").Get(); }

void Begin(void* fop) {
  if (!Enabled() || fop == nullptr) return;
  void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fop);
  std::lock_guard<std::mutex> l(sLock);
  sShow = Show{};
  sShow.serial = ++sSerials;
  sShow.fop = fop;
  sShow.trainerSeq = trainer != nullptr ? fightTrainer_GetSequencePtr(trainer) : nullptr;
  sShow.tReact = GameNow();
  sShow.reacted = true;
  sShow.phase = kReact;
  if (Log()) std::fprintf(stderr, "[mega fx] begin at %.3f (trainer model %s)\n", sShow.tReact, sShow.trainerSeq != nullptr ? "found" : "not found");
}

void Charge(void* fop, int primal) {
  if (!Enabled() || fop == nullptr) return;
  void* trainer = fightFloorGetFightOutPokemonPtrToFightTrainerPtr(nullptr, fop);
  std::lock_guard<std::mutex> l(sLock);
  if (sShow.fop != fop || sShow.phase != kReact) {
    sShow = Show{};
    sShow.serial = ++sSerials;
    sShow.fop = fop;
    sShow.trainerSeq = primal == 0 && trainer != nullptr ? fightTrainer_GetSequencePtr(trainer) : nullptr;
  }
  sShow.primal = primal;
  sShow.tLink = GameNow();
  sShow.phase = kLink;
  if (Log()) std::fprintf(stderr, "[mega fx] charge at %.3f (%.3f after the start)\n", sShow.tLink, sShow.reacted ? sShow.tLink - sShow.tReact : 0.0);
}

void Burst(void* fop) {
  std::lock_guard<std::mutex> l(sLock);
  if (sShow.phase != kLink || sShow.fop != fop) return;
  sShow.tBurst = GameNow();
  sShow.phase = kBurst;
  if (Log()) std::fprintf(stderr, "[mega fx] burst at %.3f (%.3f after the charge)\n", sShow.tBurst, sShow.tBurst - sShow.tLink);
}

void Reset() {
  std::lock_guard<std::mutex> l(sLock);
  sShow.phase = kNone;
  sShow.fop = nullptr;
  sShow.trainerSeq = nullptr;
}

}
