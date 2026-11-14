#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#else
#include <png.h>
#endif

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/gx/GXAurora.h>

enum GSgfxLayerID {};
enum GSgfxLayerType {};
#include <game/gsgfxlayer.h>
struct GSgfxLayer : public GSgfxLayerData {};
#include <game/effects.h>

#include <port_disc.h>
#include <port_user_folder.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

extern "C" {
void (*PortInput_SetFrameHook(void (*hook)(uint32_t frame)))(uint32_t frame);

struct _GSmodel;
struct GScamera;
struct GSlight;
struct GSbound;
struct GSthread;
void GSmodelDrawModel(_GSmodel* model, GSgfxLayer* layer, int pass);
GSbound* GSmodelGetBound(_GSmodel* model);
void GSboundGetMinMax(GSbound* b, Vec* min, Vec* max, bool scale, bool rot, bool trans, bool ignoreRoot);
void GSmodelSetRenderCamera(_GSmodel* model, GScamera* camera);
void GSmodelSetRenderLayer(_GSmodel* model, s32 count, s32* layers);
void GSmodelSetFogState(_GSmodel* model, u8 opa, u8 xlu);
void GSmodelSetBoundCheck(_GSmodel* model, u8 enable);
u32 GSmodelCanAnimate(_GSmodel* model);
void GSmodelSetAnimIndex(_GSmodel* model, u32 index);
void GSmodelSetAnimFrame(_GSmodel* model, f32 frame);
void GSmodelSetAnimRate(_GSmodel* model, f32 rate);
f32 GSmodelGetAnimFrameCount(_GSmodel* model, u32 index);
void GSmodelStartAnimation(_GSmodel* model);
GScamera* GScameraCreate(void);
void GScameraSetPerspective(GScamera* camera, f32 fov, f32 aspect, f32 nearZ, f32 farZ);
void GScameraSetPosition(GScamera* camera, f32* position);
void GScameraLookAt(GScamera* camera, f32* up, f32* interest);
void GScameraSetViewport(GScamera* camera, u8 enable, f32 x0, f32 y0, f32 x1, f32 y1);
void GScameraSetScissor(GScamera* camera, u8 enable, s16 x, s16 y, s16 w, s16 h);
void GScameraLoadActiveCameraMatrix(int layerId);
GSlight* GSlightCreate(void);
void GSlightSetType(GSlight* light, s32 type);
void GSlightSetColor(GSlight* light, const GXColor* color);
void GSlightSetPosition(GSlight* light, f32* position);
void GSlightSetTarget(GSlight* light, f32* target);
void GSlightSetActive(GSlight* light, u8 active);
void* GSresGetResourceAllGroup(u32 id);
GSthread* GSthreadCreate(u8 affinity, u32 group, u32 stackSize, u8 usesFPU, u8 autoStart, void* entry);
s32 GSfloorGetCurrentFloorGroupID(void);
void* Sequence_LoadPokemon(u16 id, u32 variant, bool rare);
void* Sequence_LoadTrainer(u16 id);
_GSmodel* Sequence_GetGSmodel(void* seq);
void Sequence_SetVisibility(void* seq, bool visible);
void Sequence_DoAnimation(void* seq, int index);
}

namespace {

constexpr int kPassOpa = 0, kPassXlu = 1;
constexpr u32 kTitleModels = 0x22141000;
constexpr s32 kHiddenLayer = 0xFE;
constexpr int kLayerBackground = 0, kLayerFilter = 6, kLayerLast = 7;

enum class Kind { Logo, Title, Pokemon, Trainer, Scene, LugiaSky, DuelSky };
struct Job {
  Kind kind;
  int number = 0;
  bool shiny = false;
  int anim = -1;
  std::string name;
  void* sequence = nullptr;
  _GSmodel* model = nullptr;
  bool refined = false;
  bool failed = false;
  std::string note;
  std::vector<uint8_t> rgba;
  int width = 0, height = 0;
};

std::vector<Job> sJobs;
size_t sCurrent = 0;
std::string sIconDir, sBgDir;
int sSize = 1024;
void (*sPrevious)(uint32_t) = nullptr;
bool sStarted = false;
uint32_t sTitleSince = 0;
GSthread* sLoader = nullptr;
GScamera* sCamera = nullptr;
GSlight* sLights[3] = {};
AuroraReadbackStream sStreams[2] = {};
std::vector<uint8_t> sPixels[2];
int sMade = 0, sFailed = 0;
FILE* sReport = nullptr;

enum class Step { Boot, Load, Loading, Settle, Armed, Drawn, Next } sStep = Step::Boot;
uint32_t sStepFrame = 0;
uint32_t sFrame = 0;
bool sDrawnThisFrame = false;
int sScreenPhase = 0;
int sModelPhase = 0;

struct View {
  Vec target, dir;
  float dist, fov, r;
} sView;

void finish(int code) {
  std::fprintf(stderr, "[render icon] done: %d made, %d failed (exit %d)\n", sMade, sFailed, code);
  if (sReport != nullptr) std::fclose(sReport);
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
}

#if defined(_WIN32)
constexpr const char* kSep = "\\";
std::wstring Widen(const std::string& s) {
  std::wstring w(s.size() + 1, L'\0');
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), (int)w.size());
  w.resize(n > 0 ? n - 1 : 0);
  return w;
}
std::filesystem::path PathOf(const std::string& utf8) { return std::filesystem::path(Widen(utf8)); }
#else
constexpr const char* kSep = "/";
std::filesystem::path PathOf(const std::string& utf8) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
#endif

#if !defined(_WIN32)
bool WritePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
  const std::filesystem::path final = PathOf(path);
  std::filesystem::path temp = final;
  temp += ".part";
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = static_cast<png_uint_32>(width);
  image.height = static_cast<png_uint_32>(height);
  image.format = PNG_FORMAT_RGBA;
  bool ok = png_image_write_to_file(&image, temp.c_str(), 0, rgba.data(), width * 4, nullptr) != 0;
  std::error_code ec;
  if (ok) {
    std::filesystem::rename(temp, final, ec);
    ok = !ec;
  }
  if (!ok) std::filesystem::remove(temp, ec);
  return ok;
}
#else
bool WritePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> bgra(rgba.size());
  for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
    bgra[i] = rgba[i + 2], bgra[i + 1] = rgba[i + 1], bgra[i + 2] = rgba[i], bgra[i + 3] = rgba[i + 3];
  }
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IWICImagingFactory* factory = nullptr;
  IWICStream* stream = nullptr;
  IWICBitmapEncoder* encoder = nullptr;
  IWICBitmapFrameEncode* frame = nullptr;
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  const std::wstring final = Widen(path), temp = final + L".part";
  bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
            IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
            SUCCEEDED(frame->WritePixels(height, width * 4, (UINT)bgra.size(), bgra.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
  if (frame) frame->Release();
  if (encoder) encoder->Release();
  if (stream) stream->Release();
  if (factory) factory->Release();
  if (SUCCEEDED(com)) CoUninitialize();
  ok = ok && MoveFileExW(temp.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
  if (!ok) DeleteFileW(temp.c_str());
  return ok;
}
#endif

std::vector<float> Unblend(const std::vector<uint8_t>& black, const std::vector<uint8_t>& white, int n) {
  std::vector<float> out(static_cast<size_t>(n) * 4);
  for (int i = 0; i < n; i++) {
    const uint8_t* b = &black[static_cast<size_t>(i) * 4];
    const uint8_t* w = &white[static_cast<size_t>(i) * 4];
    float a = 0.f;
    for (int c = 0; c < 3; c++) a += 1.f - (w[c] - b[c]) / 255.f;
    a = std::clamp(a / 3.f, 0.f, 1.f);
    float* o = &out[static_cast<size_t>(i) * 4];
    o[0] = std::min(b[0] / 255.f, a), o[1] = std::min(b[1] / 255.f, a), o[2] = std::min(b[2] / 255.f, a), o[3] = a;
  }
  return out;
}

struct Rect {
  int x0, y0, x1, y1;
};

Rect Covered(const std::vector<float>& premul, int width, int rows) {
  Rect r{width, rows, -1, -1};
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < width; x++) {
      if (premul[(static_cast<size_t>(y) * width + x) * 4 + 3] > 1.5f / 255.f) {
        r.x0 = std::min(r.x0, x), r.y0 = std::min(r.y0, y), r.x1 = std::max(r.x1, x), r.y1 = std::max(r.y1, y);
      }
    }
  }
  return r;
}

std::vector<uint8_t> Resample(const std::vector<float>& premul, int width, int rows, float cx, float cy, float sideX,
                              float sideY, int outW, int outH) {
  std::vector<uint8_t> out(static_cast<size_t>(outW) * outH * 4, 0);
  const float stepX = sideX / outW, stepY = sideY / outH;
  for (int oy = 0; oy < outH; oy++) {
    const float sy0 = cy - sideY * 0.5f + oy * stepY, sy1 = sy0 + stepY;
    for (int ox = 0; ox < outW; ox++) {
      const float sx0 = cx - sideX * 0.5f + ox * stepX, sx1 = sx0 + stepX;
      float acc[4] = {}, wsum = 0.f;
      for (int y = (int)std::floor(sy0); y < (int)std::ceil(sy1); y++) {
        const float wy = std::min(sy1, y + 1.f) - std::max(sy0, (float)y);
        for (int x = (int)std::floor(sx0); x < (int)std::ceil(sx1); x++) {
          const float wgt = (std::min(sx1, x + 1.f) - std::max(sx0, (float)x)) * wy;
          wsum += wgt;
          if (x < 0 || y < 0 || x >= width || y >= rows) continue;
          const float* p = &premul[(static_cast<size_t>(y) * width + x) * 4];
          for (int c = 0; c < 4; c++) acc[c] += p[c] * wgt;
        }
      }
      uint8_t* o = &out[(static_cast<size_t>(oy) * outW + ox) * 4];
      if (wsum <= 0.f) continue;
      const float a = acc[3] / wsum;
      if (a <= 0.f) continue;
      for (int c = 0; c < 3; c++) o[c] = (uint8_t)std::lround(std::clamp(acc[c] / wsum / a, 0.f, 1.f) * 255.f);
      o[3] = (uint8_t)std::lround(std::clamp(a, 0.f, 1.f) * 255.f);
    }
  }
  return out;
}

bool ComposeIcon(const std::vector<float>& premul, int width, int rows, std::vector<uint8_t>& out, std::string& note) {
  const Rect r = Covered(premul, width, rows);
  if (r.x1 < 0) {
    note = "nothing was drawn";
    return false;
  }
  char buf[160];
  std::snprintf(buf, sizeof buf, "drawn area %dx%d of %dx%d%s", r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, width, rows,
                (r.x0 == 0 || r.y0 == 0 || r.x1 == width - 1 || r.y1 == rows - 1) ? " (touches the edge: may be cut off)" : "");
  note = buf;
  const float side = std::max(r.x1 - r.x0 + 1, r.y1 - r.y0 + 1) * 1.06f;
  out = Resample(premul, width, rows, (r.x0 + r.x1 + 1) * 0.5f, (r.y0 + r.y1 + 1) * 0.5f, side, side, sSize, sSize);
  return true;
}

Vec Add(Vec a, Vec b, float s = 1.f) { return Vec{a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
Vec Cross(Vec a, Vec b) { return Vec{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec Normal(Vec v) {
  const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return n > 0.f ? Vec{v.x / n, v.y / n, v.z / n} : Vec{0.f, 0.f, 1.f};
}

void ApplyView() {
  const View& v = sView;
  const Vec eye = Add(v.target, v.dir, v.dist);
  f32 e[3] = {eye.x, eye.y, eye.z};
  f32 up[3] = {0.f, 1.f, 0.f};
  f32 at[3] = {v.target.x, v.target.y, v.target.z};
  GScameraSetPerspective(sCamera, v.fov, 1.0f, std::max(v.dist - 2.f * v.r, v.dist * 0.01f), v.dist + 2.f * v.r);
  GScameraSetPosition(sCamera, e);
  GScameraLookAt(sCamera, up, at);
  const Vec right = Normal(Cross(Vec{0.f, 1.f, 0.f}, v.dir));
  const Vec key = Add(Add(Add(v.target, v.dir, v.dist), right, -0.8f * v.dist), Vec{0.f, 1.f, 0.f}, 0.9f * v.dist);
  const Vec rim = Add(Add(Add(v.target, v.dir, -0.6f * v.dist), right, 0.9f * v.dist), Vec{0.f, 1.f, 0.f}, 0.4f * v.dist);
  f32 k[3] = {key.x, key.y, key.z}, m[3] = {rim.x, rim.y, rim.z};
  GSlightSetPosition(sLights[1], k);
  GSlightSetTarget(sLights[1], at);
  GSlightSetPosition(sLights[2], m);
  GSlightSetTarget(sLights[2], at);
}

bool AimAtBounds(_GSmodel* model, Vec dir) {
  Vec mn, mx;
  GSboundGetMinMax(GSmodelGetBound(model), &mn, &mx, true, true, true, false);
  const float dx = mx.x - mn.x, dy = mx.y - mn.y, dz = mx.z - mn.z;
  if (!(mx.x >= mn.x) || dx + dy + dz < 1e-4f) return false;
  sView.target = Vec{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
  sView.dir = dir;
  sView.r = std::max(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), 0.01f);
  sView.fov = 30.f;
  sView.dist = sView.r / std::sin(sView.fov * 0.5f * 3.14159265f / 180.f) * 1.02f;
  ApplyView();
  return true;
}

void AimTighter(const Rect& r, int s) {
  View& v = sView;
  const float half = std::tan(v.fov * 0.5f * 3.14159265f / 180.f);
  const float perPixel = 2.f * v.dist * half / s;
  const Vec right = Normal(Cross(Vec{0.f, 1.f, 0.f}, v.dir));
  const Vec up = Cross(v.dir, right);
  const float px = (r.x0 + r.x1 + 1) * 0.5f - s * 0.5f, py = (r.y0 + r.y1 + 1) * 0.5f - s * 0.5f;
  v.target = Add(Add(v.target, right, px * perPixel), up, -py * perPixel);
  const float side = (float)std::max(r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
  v.fov = 2.f * std::atan(half * side / (0.85f * s)) * 180.f / 3.14159265f;
  std::fprintf(stderr, "[render icon] first picture: area %d,%d-%d,%d of %d; view %.2f degrees, dist %.2f, r %.2f\n", r.x0, r.y0, r.x1, r.y1, s, v.fov, v.dist, v.r);
  ApplyView();
}

void UseOwnLights(bool on) {
  for (GSlight* l : sLights) {
    if (l != nullptr) GSlightSetActive(l, on ? 1 : 0);
  }
}

s32 LoadThread() {
  Job& job = sJobs[sCurrent];
  job.sequence = job.kind == Kind::Pokemon ? Sequence_LoadPokemon((u16)job.number, 0, job.shiny)
                                           : Sequence_LoadTrainer((u16)job.number);
  if (job.sequence != nullptr) job.model = Sequence_GetGSmodel(job.sequence);
  sLoader = nullptr;
  return 1;
}

_GSmodel* TitleModel(int n) { return static_cast<_GSmodel*>(GSresGetResourceAllGroup(kTitleModels + n)); }

void StopPose(_GSmodel* m) {
  if (m != nullptr && GSmodelCanAnimate(m) != 0) GSmodelSetAnimRate(m, 0.f);
}

Job* FindJob(const char* name) {
  for (Job& j : sJobs) {
    if (j.name == name && !j.failed && !j.rgba.empty()) return &j;
  }
  return nullptr;
}

struct Figure {
  const char* job;
  float x, bottom, height;
  bool mirror;
};
void MakeSky(Job& job, std::initializer_list<Figure> figures) {
  Job* sky = FindJob("title_scene");
  if (sky == nullptr) {
    job.failed = true, job.note = "needs title_scene in the same run";
    return;
  }
  job.width = sky->width, job.height = sky->height, job.rgba = sky->rgba;
  for (const Figure& f : figures) {
    Job* fig = FindJob(f.job);
    if (fig == nullptr) {
      job.failed = true, job.note = std::string("needs ") + f.job + " in the same run";
      return;
    }
    const int side = (int)(job.height * f.height);
    const int ox = (int)(job.width * f.x) - side / 2, oy = (int)(job.height * f.bottom) - side;
    for (int y = 0; y < side; y++) {
      if (oy + y < 0 || oy + y >= job.height) continue;
      for (int x = 0; x < side; x++) {
        if (ox + x < 0 || ox + x >= job.width) continue;
        const float fx0 = (float)(f.mirror ? side - 1 - x : x) * fig->width / side, fy0 = (float)y * fig->height / side;
        const float step = (float)fig->width / side;
        float acc[4] = {}, n = 0.f;
        for (int sy = (int)fy0; sy < std::max((int)fy0 + 1, (int)(fy0 + step)); sy++) {
          for (int sx = (int)fx0; sx < std::max((int)fx0 + 1, (int)(fx0 + step)); sx++) {
            const uint8_t* p = &fig->rgba[(static_cast<size_t>(std::min(sy, fig->height - 1)) * fig->width +
                                           std::min(sx, fig->width - 1)) * 4];
            const float a = p[3] / 255.f;
            acc[0] += p[0] * a, acc[1] += p[1] * a, acc[2] += p[2] * a, acc[3] += a, n += 1.f;
          }
        }
        const float a = acc[3] / n;
        uint8_t* d = &job.rgba[(static_cast<size_t>(oy + y) * job.width + ox + x) * 4];
        for (int c = 0; c < 3; c++) d[c] = (uint8_t)std::lround(acc[c] / n + d[c] * (1.f - a));
      }
    }
  }
  const std::string file = sBgDir + kSep + job.name + ".png";
  if (!WritePng(file, job.width, job.height, job.rgba)) {
    job.failed = true, job.note = "could not write " + file;
  } else {
    job.note = "written";
    std::fprintf(stderr, "[render icon] wrote %s\n", file.c_str());
  }
}
void StartJob(uint32_t frame) {
  Job& job = sJobs[sCurrent];
  if ((job.kind != Kind::Logo && job.kind != Kind::Scene) || frame - sTitleSince >= 240) {
    std::fprintf(stderr, "[render icon] game frame %u: %s\n", frame, job.name.c_str());
  }
  sStepFrame = frame;
  switch (job.kind) {
  case Kind::Logo:
  case Kind::Scene:
    if (frame - sTitleSince < 240) {
      sStep = Step::Load;
      return;
    }
    sStep = Step::Armed;
    break;
  case Kind::LugiaSky:
    MakeSky(job, {{"shadow_lugia", 0.36f, 0.93f, 0.78f, false}});
    sStep = Step::Next;
    break;
  case Kind::DuelSky:
    MakeSky(job, {{"shadow_lugia", 0.30f, 0.90f, 0.80f, true}, {"eevee", 0.70f, 0.86f, 0.42f, false}});
    sStep = Step::Next;
    break;
  case Kind::Title:
    job.model = job.number >= 0 && job.number < 4 ? TitleModel(job.number) : nullptr;
    if (job.model == nullptr) {
      job.failed = true, job.note = "that title model isn't loaded";
      sStep = Step::Next;
      return;
    }
    sStep = Step::Settle;
    break;
  case Kind::Pokemon:
  case Kind::Trainer:
    sLoader = GSthreadCreate(1, (u32)GSfloorGetCurrentFloorGroupID(), 0x4000, 1, 1, (void*)&LoadThread);
    if (sLoader == nullptr) {
      job.failed = true, job.note = "no game thread for loading";
      sStep = Step::Next;
      return;
    }
    sStep = Step::Loading;
    break;
  }
}

u64 Key() { return sCurrent * 2 + (sJobs[sCurrent].refined ? 2 : 1); }

void DrawModel(GSgfxLayer* layer, _GSmodel* model, GXColor background, int stream) {
  const u32 s = static_cast<u32>(sSize) * 2;
  GXCreateFrameBuffer(s, s);
  GXClear(GX_TRUE, GX_TRUE, GX_TRUE, background, 0xFFFFFF);
  GSmodelDrawModel(model, layer, kPassOpa);
  GSmodelDrawModel(model, layer, kPassXlu);
  AuroraReadbackCaptureColorInStream(sStreams[stream], Key());
  GXRestoreFrameBuffer();
}

void OnRenderScreen(GSgfxLayer* layer, int pass, bool logo) {
  const int id = (int)layer->id;
  if (logo && id == kLayerBackground && pass == kPassOpa) {
    const GXColor bg = sScreenPhase == 0 ? GXColor{0, 0, 0, 0} : GXColor{255, 255, 255, 255};
    GXClear(GX_TRUE, GX_TRUE, GX_TRUE, bg, 0xFFFFFF);
  } else if (id == (logo ? kLayerLast : kLayerFilter) && pass == kPassXlu) {
    AuroraReadbackCaptureColorInStream(sStreams[sScreenPhase], Key());
    sDrawnThisFrame = true;
    if (!logo || ++sScreenPhase == 2) {
      sScreenPhase = 0;
      sStep = Step::Drawn;
      sStepFrame = sFrame;
    }
  }
}

void OnRender(GSgfxLayer* layer, int pass) {
  if (sStep != Step::Armed || sDrawnThisFrame || layer == nullptr) return;
  Job& job = sJobs[sCurrent];
  if (job.kind == Kind::Logo || job.kind == Kind::Scene) {
    OnRenderScreen(layer, pass, job.kind == Kind::Logo);
    return;
  }
  if (pass != kPassXlu || (int)layer->type != 0) return;
  const bool own = job.kind == Kind::Pokemon || job.kind == Kind::Trainer;
  GSmodelSetRenderCamera(job.model, sCamera);
  if (own) UseOwnLights(true);
  const bool black = sModelPhase == 0;
  DrawModel(layer, job.model, GXColor{0, 0, 0, 0}, 0);
  DrawModel(layer, job.model, black ? GXColor{0, 0, 0, 0} : GXColor{255, 255, 255, 255}, black ? 0 : 1);
  if (own) UseOwnLights(false);
  GSmodelSetRenderCamera(job.model, nullptr);
  GScameraLoadActiveCameraMatrix((int)layer->id);
  sDrawnThisFrame = true;
  if (std::getenv("XD_RENDER_ICON_LOG") != nullptr) {
    const auto& pass = layer->passes[kPassXlu];
    std::fprintf(stderr, "[render icon] layer fifo (xlu pass): %u of %u bytes used so far, highest %u\n", pass.unkB4, pass.size, pass.unkB8);
  }
  if (black) {
    sModelPhase = 1;
    return;
  }
  sModelPhase = 0;
  sStep = Step::Drawn;
  sStepFrame = sFrame;
}

class IconEffect : public GSeffect {
public:
  bool _update(f32) override {
    sDrawnThisFrame = false;
    return true;
  }
  void _render(GSgfxLayer* layer, GSgfxLayerPass pass) override { OnRender(layer, (int)pass); }
};

void Collect(uint32_t frame) {
  Job& job = sJobs[sCurrent];
  const bool screen = job.kind == Kind::Logo || job.kind == Kind::Scene;
  const u32 s = static_cast<u32>(sSize) * 2;
  std::vector<std::pair<u32, u32>> sizes{{s, s}};
  if (screen) {
    u32 w = 0, h = 0;
    AuroraGetRenderSize(&w, &h);
    sizes = {{w, h}, {w, w * 3 / 4}, {w, h * 480 / 528}};
  }
  const int pictures = job.kind == Kind::Scene ? 1 : 2;
  u32 width = 0, height = 0;
  for (const auto& [w, h] : sizes) {
    bool all = true;
    for (int i = 0; i < pictures; i++) {
      sPixels[i].resize(static_cast<size_t>(w) * h * 4);
      AuroraReadbackDest dest{Key(), sPixels[i].data(), w * h * 4, GX_FALSE};
      AuroraReadbackGet(sStreams[i], &dest, 1);
      all = all && dest.found != GX_FALSE;
    }
    if (all) {
      width = w, height = h;
      break;
    }
  }
  if (width == 0) {
    if (frame - sStepFrame > 120) {
      job.failed = true, job.note = "the picture never came back from the graphics card";
      sStep = Step::Next;
    }
    return;
  }
  sStep = Step::Next;
  if (job.kind == Kind::Scene) {
    const int outH = (int)width * 9 / 16, top = ((int)height - outH) / 2;
    job.width = (int)width, job.height = outH;
    job.rgba.assign(sPixels[0].begin() + static_cast<size_t>(top) * width * 4,
                    sPixels[0].begin() + static_cast<size_t>(top + outH) * width * 4);
    for (size_t i = 3; i < job.rgba.size(); i += 4) job.rgba[i] = 255;
    const std::string file = sBgDir + kSep + job.name + ".png";
    if (!WritePng(file, job.width, job.height, job.rgba)) {
      job.failed = true, job.note = "could not write " + file;
    } else {
      job.note = "game picture " + std::to_string(width) + "x" + std::to_string(height);
      std::fprintf(stderr, "[render icon] wrote %s\n", file.c_str());
    }
    return;
  }
  if (std::getenv("XD_RENDER_ICON_RAW") != nullptr) {
    WritePng(sIconDir + kSep + job.name + (job.refined ? "_raw2" : "_raw1") + "_black.png", (int)width, (int)height, sPixels[0]);
    WritePng(sIconDir + kSep + job.name + (job.refined ? "_raw2" : "_raw1") + "_white.png", (int)width, (int)height, sPixels[1]);
  }
  const auto premul = Unblend(sPixels[0], sPixels[1], (int)(width * height));
  // The logo: only the top of the title picture (PRESS START and the copyright lines are below it).
  const int rows = job.kind == Kind::Logo ? (int)(height * 0.53f) : (int)height;
  if (!screen && !job.refined) {
    const Rect r = Covered(premul, (int)width, rows);
    if (r.x1 < 0) {
      job.failed = true, job.note = "nothing was drawn";
      return;
    }
    job.refined = true;
    AimTighter(r, (int)s);
    sStep = Step::Armed;
    sStepFrame = frame;
    return;
  }
  std::string note;
  if (!ComposeIcon(premul, (int)width, rows, job.rgba, note)) {
    job.failed = true, job.note = note;
    return;
  }
  job.width = job.height = sSize;
  const std::string file = sIconDir + kSep + job.name + ".png";
  if (WritePng(file, sSize, sSize, job.rgba)) {
    job.note = note;
    std::fprintf(stderr, "[render icon] wrote %s (%s)\n", file.c_str(), note.c_str());
  } else {
    job.failed = true, job.note = "could not write " + file;
  }
}

void OnFrame(uint32_t frame) {
  if (sPrevious != nullptr) sPrevious(frame);
  sFrame = frame;
  switch (sStep) {
  case Step::Boot:
    if (TitleModel(0) != nullptr) {
      if (sTitleSince == 0) {
        sTitleSince = frame;
        std::fprintf(stderr, "[render icon] game frame %u: title screen loaded\n", frame);
      }
      static const uint32_t wait = std::getenv("XD_RENDER_ICON_WAIT") ? (uint32_t)std::atoi(std::getenv("XD_RENDER_ICON_WAIT")) : 60;
      if (frame - sTitleSince >= wait) {
        static IconEffect* effect = new IconEffect();
        effect->Start();
        sCamera = GScameraCreate();
        if (sCamera != nullptr) {
          const float s = sSize * 2.f;
          GScameraSetViewport(sCamera, 1, 0.f, 0.f, s * 0.5f, s * 0.5f);
          GScameraSetScissor(sCamera, 1, 0, 0, (s16)(sSize * 2), (s16)(sSize * 2));
        }
        const GXColor ambient{0x38, 0x36, 0x40, 0xFF}, key{0xFF, 0xF6, 0xE6, 0xFF}, rim{0xB0, 0xA0, 0xE0, 0xFF};
        const GXColor* colors[3] = {&ambient, &key, &rim};
        for (int i = 0; i < 3; i++) {
          sLights[i] = GSlightCreate();
          if (sLights[i] == nullptr) continue;
          GSlightSetType(sLights[i], i == 0 ? 0 : 2);
          GSlightSetColor(sLights[i], colors[i]);
          GSlightSetActive(sLights[i], 0);
        }
        sStreams[0] = AuroraReadbackCreateStream();
        sStreams[1] = AuroraReadbackCreateStream();
        if (sCamera == nullptr || sLights[1] == nullptr || sLights[2] == nullptr) {
          std::fprintf(stderr, "[render icon] the game has no free camera or lights\n");
          finish(3);
        }
        sStep = Step::Load;
      }
    } else if (frame > 60 * 240) {
      std::fprintf(stderr, "[render icon] the title screen never came (game frame %u)\n", frame);
      finish(3);
    }
    break;
  case Step::Load:
    StartJob(frame);
    break;
  case Step::Loading: {
    Job& job = sJobs[sCurrent];
    if (sLoader == nullptr) {
      if (job.model == nullptr) {
        job.failed = true, job.note = "the game could not load this model";
        sStep = Step::Next;
        break;
      }
      Sequence_SetVisibility(job.sequence, true);
      s32 hidden = kHiddenLayer;
      GSmodelSetRenderLayer(job.model, 1, &hidden);
      GSmodelSetBoundCheck(job.model, 0);
      GSmodelSetFogState(job.model, 0, 0);
      Sequence_DoAnimation(job.sequence, 0);
      if (job.anim >= 0 && GSmodelCanAnimate(job.model) != 0) {
        GSmodelSetAnimIndex(job.model, (u32)job.anim);
        GSmodelSetAnimFrame(job.model, 0.f);
        GSmodelSetAnimRate(job.model, 0.5f);
        GSmodelStartAnimation(job.model);
      }
      sStep = Step::Settle;
      sStepFrame = frame;
    } else if (frame - sStepFrame > 60 * 30) {
      job.failed = true, job.note = "loading took too long";
      sStep = Step::Next;
    }
    break;
  }
  case Step::Settle:
    if (frame - sStepFrame >= 20) {
      Job& job = sJobs[sCurrent];
      const float yaw = 32.f * 3.14159265f / 180.f, pitch = 12.f * 3.14159265f / 180.f;
      const Vec dir{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
      StopPose(job.model);
      if (!AimAtBounds(job.model, dir)) {
        job.failed = true, job.note = "the model has no size";
        sStep = Step::Next;
        break;
      }
      sStep = Step::Armed;
      sStepFrame = frame;
    }
    break;
  case Step::Armed:
    if (frame - sStepFrame > 120) {
      sJobs[sCurrent].failed = true, sJobs[sCurrent].note = "the layer was never drawn";
      sStep = Step::Next;
    }
    break;
  case Step::Drawn:
    Collect(frame);
    break;
  case Step::Next: {
    Job& job = sJobs[sCurrent];
    if (job.failed) {
      sFailed++;
      std::fprintf(stderr, "[render icon] %s FAILED: %s\n", job.name.c_str(), job.note.c_str());
    } else {
      sMade++;
    }
    if (sReport != nullptr) {
      std::fprintf(sReport, "%s %s %s\n", job.name.c_str(), job.failed ? "failed" : "ok", job.note.c_str());
      std::fflush(sReport);
    }
    if (job.sequence != nullptr) Sequence_SetVisibility(job.sequence, false);
    if (++sCurrent >= sJobs.size()) finish(sFailed == 0 ? 0 : 2);
    sStep = Step::Load;
    break;
  }
  }
}

bool AddItem(const std::string& item) {
  auto add = [](Kind kind, int number, bool shiny, const std::string& name) {
    Job job;
    job.kind = kind, job.number = number, job.shiny = shiny, job.name = name;
    sJobs.push_back(job);
    return true;
  };
  if (item == "all") {
    return AddItem("shadow_lugia") && AddItem("eevee") && AddItem("michael") && AddItem("logo") &&
           AddItem("title_scene") && AddItem("lugia_sky") && AddItem("duel_sky");
  }
  if (item == "logo") return add(Kind::Logo, 0, false, "logo");
  if (item == "title_scene") return add(Kind::Scene, 0, false, "title_scene");
  if (item == "lugia_sky") return add(Kind::LugiaSky, 0, false, "lugia_sky");
  if (item == "duel_sky") return add(Kind::DuelSky, 0, false, "duel_sky");
  if (item == "shadow_lugia") return add(Kind::Pokemon, 417, false, "shadow_lugia");
  if (item == "eevee") return add(Kind::Pokemon, 133, false, "eevee");
  if (item == "michael") {
    add(Kind::Trainer, 1, false, "michael");
    sJobs.back().anim = 2;
    return true;
  }
  std::vector<std::string> parts;
  for (size_t start = 0;;) {
    const size_t colon = item.find(':', start);
    parts.push_back(item.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
    if (colon == std::string::npos) break;
    start = colon + 1;
  }
  if (parts.size() < 2) return false;
  const int number = (int)std::strtol(parts[1].c_str(), nullptr, 0);
  bool shiny = false;
  int anim = -1;
  std::string name;
  for (size_t i = 2; i < parts.size(); i++) {
    if (parts[i] == "shiny") shiny = true;
    else if (parts[i].rfind("anim", 0) == 0) anim = std::atoi(parts[i].c_str() + 4);
    else name = parts[i];
  }
  if (name.empty()) name = parts[0] + "_" + parts[1] + (shiny ? "_shiny" : "") + (anim >= 0 ? "_anim" + std::to_string(anim) : "");
  if (parts[0] == "pokemon" || parts[0] == "trainer") {
    add(parts[0] == "pokemon" ? Kind::Pokemon : Kind::Trainer, number, shiny, name);
    sJobs.back().anim = anim;
    return true;
  }
  if (parts[0] == "title") return add(Kind::Title, number, false, name);
  return false;
}

std::string Folder(const char* env, const char* fallback) {
  const char* dir = std::getenv(env);
  std::string path = dir != nullptr && dir[0] != '\0' ? dir : std::string(PortUser_FolderUtf8("Pokemon XD")) + fallback;
  std::error_code error;
  std::filesystem::create_directories(PathOf(path), error);
  return path;
}

}

extern "C" bool PortXD_RenderIconPrepare(void) {
  const char* spec = std::getenv("XD_RENDER_ICON");
  if (spec == nullptr || spec[0] == '\0') return false;
  auto setDefault = [](const char* name, const char* value) {
    const char* now = std::getenv(name);
#if defined(_WIN32)
    if (now == nullptr || now[0] == '\0') _putenv_s(name, value);
#else
    if (now == nullptr || now[0] == '\0') setenv(name, value, 1);
#endif
  };
  setDefault("XD_NO_MESSAGE_BOXES", "1");
  setDefault("XD_WINDOW_OFFSCREEN", "1");
  setDefault("SDL_WINDOW_ACTIVATE_WHEN_SHOWN", "0");
  setDefault("XD_RECORD", "0");
  setDefault("XD_DISC_TIMING", "fast");
  setDefault("XD_TURBO", "4");
  setDefault("SDL_AUDIO_DRIVER", "dummy");
  setDefault("XD_RUN_LABEL", "render icon");
  return true;
}

extern "C" void PortXD_RenderIconStart(void) {
  const char* spec = std::getenv("XD_RENDER_ICON");
  if (spec == nullptr || spec[0] == '\0') return;
  const std::string list = spec;
  for (size_t start = 0;;) {
    const size_t comma = list.find(',', start);
    const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!item.empty() && !AddItem(item)) std::fprintf(stderr, "[render icon] unknown item \"%s\"\n", item.c_str());
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (sJobs.empty()) {
    std::fprintf(stderr, "[render icon] nothing to render\n");
    finish(2);
  }
  const char* size = std::getenv("XD_RENDER_ICON_SIZE");
  if (size != nullptr && size[0] != '\0') sSize = std::clamp(std::atoi(size), 16, 2048);
  sIconDir = Folder("XD_RENDER_ICON_DIR", (std::string(kSep) + "icons" + kSep + "rendered").c_str());
  sBgDir = Folder("XD_RENDER_BG_DIR", (std::string(kSep) + "backgrounds" + kSep + "rendered").c_str());
#if defined(_WIN32)
  sReport = _wfopen(Widen(sIconDir + kSep + "render_icon.txt").c_str(), L"w");
#else
  sReport = std::fopen((sIconDir + kSep + "render_icon.txt").c_str(), "w");
#endif
  if (sReport != nullptr) {
    std::fprintf(sReport, "# Pictures rendered from the game on this PC (xd.exe, XD_RENDER_ICON).\n"
                          "game %s\nsize %d (models drawn at %d)\n", PortDisc_GameId(), sSize, sSize * 2);
  }
  std::fprintf(stderr, "[render icon] %zu pictures, icons %dx%d into %s, backgrounds into %s\n", sJobs.size(), sSize,
               sSize, sIconDir.c_str(), sBgDir.c_str());
  sPrevious = PortInput_SetFrameHook(OnFrame);
}
