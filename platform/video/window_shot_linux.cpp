#if defined(__linux__) || defined(__APPLE__)
#include <port_window_shot.h>

#include <png.h>

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

extern "C" {

bool PortShot_SavePng(const wchar_t* path, const uint8_t* rgba, uint32_t w, uint32_t h) {
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
  for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; i++) {
    rgb[i * 3 + 0] = rgba[i * 4 + 0];
    rgb[i * 3 + 1] = rgba[i * 4 + 1];
    rgb[i * 3 + 2] = rgba[i * 4 + 2];
  }
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = w;
  image.height = h;
  image.format = PNG_FORMAT_RGB;
  const std::filesystem::path file(path);
  return png_image_write_to_file(&image, file.c_str(), 0, rgb.data(), static_cast<png_int_32>(w * 3), nullptr) != 0;
}

}

#include <port_freeze.h>

namespace {
struct RgbaAsk {
  PortShotRgbaDone done;
  void* ctx;
};
void RgbaArrived(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h, bool) {
  const RgbaAsk ask = *static_cast<RgbaAsk*>(ctx);
  delete static_cast<RgbaAsk*>(ctx);
  ask.done(ask.ctx, rgba, w, h);
}
}

extern "C" void PortShot_GamePicture(PortShotRgbaDone done, void* ctx) {
  PortShot_LinuxRequest(false, RgbaArrived, new RgbaAsk{done, ctx});
}

#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <imgui.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

struct Texture {
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
};
struct OverlayCmd {
  float clip[4];
  std::shared_ptr<const Texture> tex;
  unsigned idxOffset, vtxOffset, count;
};
struct OverlayList {
  std::vector<ImDrawVert> vtx;
  std::vector<ImDrawIdx> idx;
  std::vector<OverlayCmd> cmds;
};
struct Overlay {
  bool taken = false;
  float posX = 0, posY = 0, sizeX = 0, sizeY = 0, scaleX = 1, scaleY = 1;
  std::vector<OverlayList> lists;
  size_t triangles = 0;
  size_t unknownTextures = 0;
};

std::unordered_map<uint64_t, std::shared_ptr<const Texture>> sTextures;

std::shared_ptr<const Texture> sFontSheet;
uint64_t sFontSheetId = 0;
const void* sFontSheetPixels = nullptr;

std::shared_ptr<const Texture> FontSheet(uint64_t id) {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  if (atlas == nullptr || static_cast<uint64_t>(atlas->TexID) != id) return nullptr;
  if (atlas->TexPixelsRGBA32 == nullptr && atlas->TexPixelsAlpha8 == nullptr) return nullptr;
  unsigned char* pixels = nullptr;
  int w = 0, h = 0;
  atlas->GetTexDataAsRGBA32(&pixels, &w, &h);
  if (pixels == nullptr || w <= 0 || h <= 0) return nullptr;
  if (sFontSheet == nullptr || sFontSheetId != id || sFontSheetPixels != pixels ||
      sFontSheet->w != static_cast<uint32_t>(w) || sFontSheet->h != static_cast<uint32_t>(h)) {
    auto t = std::make_shared<Texture>();
    t->w = static_cast<uint32_t>(w);
    t->h = static_cast<uint32_t>(h);
    t->rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    sFontSheet = std::move(t);
    sFontSheetId = id;
    sFontSheetPixels = pixels;
  }
  return sFontSheet;
}

bool TakeOverlay(Overlay& o, const char* where) {
  if (ImGui::GetCurrentContext() == nullptr) return false;
  const ImDrawData* d = ImGui::GetDrawData();
  if (d == nullptr || !d->Valid) return false;
  o = Overlay{};
  o.taken = true;
  o.posX = d->DisplayPos.x, o.posY = d->DisplayPos.y;
  o.sizeX = d->DisplaySize.x, o.sizeY = d->DisplaySize.y;
  const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
  o.scaleX = scale.x > 0 ? scale.x : 1.f, o.scaleY = scale.y > 0 ? scale.y : 1.f;
  for (int n = 0; n < d->CmdListsCount; n++) {
    const ImDrawList* list = d->CmdLists[n];
    OverlayList copy;
    copy.vtx.assign(list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
    copy.idx.assign(list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr || cmd.ElemCount == 0) continue;
      OverlayCmd c{};
      c.clip[0] = cmd.ClipRect.x, c.clip[1] = cmd.ClipRect.y, c.clip[2] = cmd.ClipRect.z, c.clip[3] = cmd.ClipRect.w;
      c.idxOffset = cmd.IdxOffset, c.vtxOffset = cmd.VtxOffset, c.count = cmd.ElemCount;
      const uint64_t id = static_cast<uint64_t>(cmd.GetTexID());
      if (id != 0) {
        if (auto it = sTextures.find(id); it != sTextures.end()) {
          c.tex = it->second;
        } else if ((c.tex = FontSheet(id)) == nullptr) {
          o.unknownTextures++;
        }
      }
      o.triangles += c.count / 3;
      copy.cmds.push_back(std::move(c));
    }
    if (!copy.cmds.empty()) o.lists.push_back(std::move(copy));
  }
  if (std::getenv("XD_WINDOW_SHOT_LOG") != nullptr) {
    std::fprintf(stderr, "[window shot] ImGui's lists copied (%s): %d lists, %d vertices, %zu triangles, ImGui frame %d\n",
                 where, d->CmdListsCount, d->TotalVtxCount, o.triangles, ImGui::GetFrameCount());
  }
  return true;
}

struct Canvas {
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> rgba;
};

struct Vert {
  double x, y, u, v;
  double c[4];
};

inline double Edge(const Vert& a, const Vert& b, double px, double py) {
  const bool swap = b.x < a.x || (b.x == a.x && b.y < a.y);
  const Vert& p = swap ? b : a;
  const Vert& q = swap ? a : b;
  const double e = (q.x - p.x) * (py - p.y) - (q.y - p.y) * (px - p.x);
  return swap ? -e : e;
}
inline bool Owns(const Vert& a, const Vert& b) {
  const double dy = b.y - a.y, dx = b.x - a.x;
  return dy > 0 || (dy == 0 && dx > 0);
}

inline void Sample(const Texture& t, double u, double v, double out[4]) {
  double x = u * t.w - 0.5, y = v * t.h - 0.5;
  x = std::clamp(x, 0.0, static_cast<double>(t.w - 1));
  y = std::clamp(y, 0.0, static_cast<double>(t.h - 1));
  const uint32_t x0 = static_cast<uint32_t>(x), y0 = static_cast<uint32_t>(y);
  const uint32_t x1 = std::min(x0 + 1, t.w - 1), y1 = std::min(y0 + 1, t.h - 1);
  const double fx = x - x0, fy = y - y0;
  const uint8_t* p00 = &t.rgba[(static_cast<size_t>(y0) * t.w + x0) * 4];
  const uint8_t* p10 = &t.rgba[(static_cast<size_t>(y0) * t.w + x1) * 4];
  const uint8_t* p01 = &t.rgba[(static_cast<size_t>(y1) * t.w + x0) * 4];
  const uint8_t* p11 = &t.rgba[(static_cast<size_t>(y1) * t.w + x1) * 4];
  for (int k = 0; k < 4; k++) {
    out[k] = ((p00[k] * (1 - fx) + p10[k] * fx) * (1 - fy) + (p01[k] * (1 - fx) + p11[k] * fx) * fy) / 255.0;
  }
}

void Triangle(Canvas& cv, const int clip[4], Vert v0, Vert v1, Vert v2, const Texture* tex) {
  double area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
  if (area == 0) return;
  if (area < 0) {
    std::swap(v1, v2);
    area = -area;
  }
  const int x0 = std::max(clip[0], static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
  const int y0 = std::max(clip[1], static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
  const int x1 = std::min(clip[2], static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
  const int y1 = std::min(clip[3], static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
  if (x0 >= x1 || y0 >= y1) return;
  const bool own0 = Owns(v1, v2), own1 = Owns(v2, v0), own2 = Owns(v0, v1);
  for (int y = y0; y < y1; y++) {
    const double py = y + 0.5;
    uint8_t* row = &cv.rgba[(static_cast<size_t>(y) * cv.w + x0) * 4];
    for (int x = x0; x < x1; x++, row += 4) {
      const double px = x + 0.5;
      const double e0 = Edge(v1, v2, px, py), e1 = Edge(v2, v0, px, py), e2 = Edge(v0, v1, px, py);
      if (e0 < 0 || e1 < 0 || e2 < 0) continue;
      if ((e0 == 0 && !own0) || (e1 == 0 && !own1) || (e2 == 0 && !own2)) continue;
      const double b0 = e0 / area, b1 = e1 / area, b2 = e2 / area;
      double c[4];
      for (int k = 0; k < 4; k++) c[k] = v0.c[k] * b0 + v1.c[k] * b1 + v2.c[k] * b2;
      if (tex != nullptr) {
        double t[4];
        Sample(*tex, v0.u * b0 + v1.u * b1 + v2.u * b2, v0.v * b0 + v1.v * b1 + v2.v * b2, t);
        for (int k = 0; k < 4; k++) c[k] *= t[k];
      }
      const double a = std::clamp(c[3], 0.0, 1.0);
      if (a <= 0) continue;
      for (int k = 0; k < 3; k++) {
        const double out = std::clamp(c[k], 0.0, 1.0) * 255.0 * a + row[k] * (1 - a);
        row[k] = static_cast<uint8_t>(std::lround(out));
      }
      row[3] = 255;
    }
  }
}

void DrawOverlay(Canvas& cv, const Overlay& o) {
  for (const OverlayList& list : o.lists) {
    for (const OverlayCmd& cmd : list.cmds) {
      const int clip[4] = {
          std::max(0, static_cast<int>(std::floor((cmd.clip[0] - o.posX) * o.scaleX))),
          std::max(0, static_cast<int>(std::floor((cmd.clip[1] - o.posY) * o.scaleY))),
          std::min(static_cast<int>(cv.w), static_cast<int>(std::ceil((cmd.clip[2] - o.posX) * o.scaleX))),
          std::min(static_cast<int>(cv.h), static_cast<int>(std::ceil((cmd.clip[3] - o.posY) * o.scaleY)))};
      if (clip[0] >= clip[2] || clip[1] >= clip[3]) continue;
      for (unsigned i = 0; i + 2 < cmd.count; i += 3) {
        Vert v[3];
        bool ok = true;
        for (int k = 0; k < 3; k++) {
          const size_t at = cmd.idxOffset + i + k;
          if (at >= list.idx.size()) {
            ok = false;
            break;
          }
          const size_t index = static_cast<size_t>(list.idx[at]) + cmd.vtxOffset;
          if (index >= list.vtx.size()) {
            ok = false;
            break;
          }
          const ImDrawVert& s = list.vtx[index];
          v[k].x = (s.pos.x - o.posX) * o.scaleX;
          v[k].y = (s.pos.y - o.posY) * o.scaleY;
          v[k].u = s.uv.x, v[k].v = s.uv.y;
          v[k].c[0] = ((s.col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0;
          v[k].c[1] = ((s.col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0;
          v[k].c[2] = ((s.col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0;
          v[k].c[3] = ((s.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0;
        }
        if (ok) Triangle(cv, clip, v[0], v[1], v[2], cmd.tex.get());
      }
    }
  }
}

void PlacePicture(Canvas& cv, const uint8_t* rgba, uint32_t pw, uint32_t ph) {
  const double aspect = static_cast<double>(pw) / ph;
  double cw = cv.w, ch = cv.w / aspect;
  if (ch > cv.h) {
    ch = cv.h;
    cw = cv.h * aspect;
  }
  const int dw = std::max(1, static_cast<int>(std::lround(cw))), dh = std::max(1, static_cast<int>(std::lround(ch)));
  const int ox = (static_cast<int>(cv.w) - dw) / 2, oy = (static_cast<int>(cv.h) - dh) / 2;
  Texture src;
  src.w = pw, src.h = ph;
  src.rgba.assign(rgba, rgba + static_cast<size_t>(pw) * ph * 4);
  for (int y = 0; y < dh; y++) {
    if (oy + y < 0 || oy + y >= static_cast<int>(cv.h)) continue;
    uint8_t* row = &cv.rgba[(static_cast<size_t>(oy + y) * cv.w) * 4];
    for (int x = 0; x < dw; x++) {
      if (ox + x < 0 || ox + x >= static_cast<int>(cv.w)) continue;
      double c[4];
      Sample(src, (x + 0.5) / dw, (y + 0.5) / dh, c);
      for (int k = 0; k < 3; k++) row[(ox + x) * 4 + k] = static_cast<uint8_t>(std::lround(c[k] * 255.0));
    }
  }
}

struct Request {
  bool window;
  PortShotLinuxDone done;
  void* ctx;
  uint32_t freeze;
  Overlay overlay;
};
std::deque<Request> sRequests;
enum class Grab { None, WaitPicture, WaitColour } sGrab = Grab::None;
int sGrabTries = 0;
uint64_t sColourSince = 0;
AuroraReadbackStream sStream = 0;
u64 sKey = 0;
u32 sGrabW = 0, sGrabH = 0;
constexpr uint64_t kColourTimeoutMs = 5000;

bool sWasFrozen = false;
uint32_t sFreezeCount = 0;
uint32_t sColourFreeze = 0;
uint32_t FreezeNow() {
  const bool frozen = PortFreeze_Active();
  if (frozen && !sWasFrozen) sFreezeCount++;
  sWasFrozen = frozen;
  return frozen ? sFreezeCount : 0;
}

void FinishGrab(const std::vector<uint8_t>* picture, const char* why) {
  Request request = std::move(sRequests.front());
  sRequests.pop_front();
  sGrab = Grab::None;
  if (picture == nullptr) {
    std::fprintf(stderr, "[window shot] no picture: %s\n", why);
    request.done(request.ctx, nullptr, 0, 0, false);
    return;
  }
  const Overlay& o = request.overlay;
  const uint32_t cw = static_cast<uint32_t>(std::lround(o.sizeX * o.scaleX));
  const uint32_t ch = static_cast<uint32_t>(std::lround(o.sizeY * o.scaleY));
  if (!request.window || !o.taken || o.triangles == 0 || cw == 0 || ch == 0 || cw > 16384 || ch > 16384) {
    if (request.window && std::getenv("XD_WINDOW_SHOT_LOG") != nullptr) {
      std::fprintf(stderr, "[window shot] no overlay: lists %s, %zu triangles, window %ux%u\n",
                   o.taken ? "taken" : "NOT taken", o.triangles, cw, ch);
    }
    request.done(request.ctx, picture->data(), sGrabW, sGrabH, false);
    return;
  }
  const uint64_t t0 = SDL_GetTicks();
  Canvas cv;
  cv.w = cw, cv.h = ch;
  cv.rgba.assign(static_cast<size_t>(cw) * ch * 4, 0);
  for (size_t i = 3; i < cv.rgba.size(); i += 4) cv.rgba[i] = 255;
  PlacePicture(cv, picture->data(), sGrabW, sGrabH);
  DrawOverlay(cv, o);
  std::fprintf(stderr,
               "[window shot] the window's picture put together: game picture %ux%u in %ux%u, %zu triangles of the "
               "port's screens over it%s (%u ms)\n",
               sGrabW, sGrabH, cw, ch, o.triangles,
               o.unknownTextures != 0 ? "; SOME PICTURES UNKNOWN (drawn as plain colour)" : "",
               static_cast<unsigned>(SDL_GetTicks() - t0));
  request.done(request.ctx, cv.rgba.data(), cw, ch, true);
}

bool EndsWithDisplayCopy(const uint8_t* f, size_t n) {
  return n >= 5 && f[n - 5] == 0x61 && f[n - 4] == 0x52 && ((f[n - 2] >> 6) & 1) != 0;
}

bool WriteAndCapture(const uint8_t* frame, size_t size) {
  const bool copyLast = EndsWithDisplayCopy(frame, size);
  PortFreeze_Write(frame, copyLast ? size - 5 : size);
  if (sStream == 0) {
    sStream = AuroraReadbackCreateStream();
  }
  sKey++;
  const bool accepted = AuroraReadbackCaptureColor(sStream, sKey, &sGrabW, &sGrabH) != GX_FALSE;
  if (copyLast) {
    PortFreeze_Write(frame + size - 5, 5);
  }
  return accepted;
}

void ColourAsked(bool accepted) {
  if (!accepted) {
    FinishGrab(nullptr, "the graphics card's picture can't be read back here (OpenGL ES fall-back, or MSAA)");
    return;
  }
  sGrab = Grab::WaitColour;
  sColourSince = SDL_GetTicks();
}

void CollectColour() {
  std::vector<uint8_t> rgba(static_cast<size_t>(sGrabW) * sGrabH * 4);
  AuroraReadbackDest dest{sKey, rgba.data(), static_cast<u32>(rgba.size()), GX_FALSE};
  AuroraReadbackGet(sStream, &dest, 1);
  if (!dest.found) {
    if (SDL_GetTicks() - sColourSince >= kColourTimeoutMs) {
      FinishGrab(nullptr, "the picture never came back from the graphics card");
    }
    return;
  }
  FinishGrab(&rgba, "");
  while (!sRequests.empty() && sColourFreeze != 0 && sRequests.front().freeze == sColourFreeze) {
    FinishGrab(&rgba, "");
  }
}

struct SaveTo {
  std::filesystem::path path;
};
void SaveWindowDone(void* ctx, const uint8_t* rgba, uint32_t w, uint32_t h, bool overlay) {
  std::unique_ptr<SaveTo> to(static_cast<SaveTo*>(ctx));
  std::error_code ec;
  std::filesystem::create_directories(to->path.parent_path(), ec);
  const bool saved = rgba != nullptr && PortShot_SavePng(to->path.wstring().c_str(), rgba, w, h);
  std::fprintf(stderr, "[window shot] %s: %s (%ux%u, %s)\n", saved ? "saved" : "FAILED", to->path.string().c_str(), w,
               h, overlay ? "with the port's screens" : "the game's picture alone");
}

}

extern "C" {

void PortShot_LinuxNoteTexture(uint64_t id, uint32_t w, uint32_t h, const void* rgba) {
  if (id == 0 || rgba == nullptr || w == 0 || h == 0) return;
  auto t = std::make_shared<Texture>();
  t->w = w, t->h = h;
  const auto* bytes = static_cast<const uint8_t*>(rgba);
  t->rgba.assign(bytes, bytes + static_cast<size_t>(w) * h * 4);
  sTextures[id] = std::move(t);
}

void PortShot_LinuxRequest(bool window, PortShotLinuxDone done, void* ctx) {
  PortFreeze_Capture();
  Request r{window, done, ctx, FreezeNow(), {}};
  if (window) {
    TakeOverlay(r.overlay, "when asked");
  }
  sRequests.push_back(std::move(r));
}

void PortShot_LinuxPoll(void) {
  FreezeNow();
  if (sRequests.empty()) return;
  for (Request& r : sRequests) {
    if (r.window && !r.overlay.taken) TakeOverlay(r.overlay, "after the refresh");
  }
  if (sGrab == Grab::WaitColour) CollectColour();
}

bool PortShot_LinuxFrozenDraw(const uint8_t* frame, size_t size) {
  if (sRequests.empty() || sGrab == Grab::WaitColour || frame == nullptr || size == 0) return false;
  sGrab = Grab::WaitPicture;
  sColourFreeze = FreezeNow();
  ColourAsked(WriteAndCapture(frame, size));
  return true;
}

void PortShot_LinuxFrame(void) {
  if (sRequests.empty()) {
    return;
  }
  if (sGrab == Grab::None) {
    sGrab = Grab::WaitPicture;
    sGrabTries = 0;
  }
  if (sGrab == Grab::WaitColour) {
    CollectColour();
    return;
  }
  AuroraInterpInfo info{};
  AuroraInterpGetInfo(&info);
  size_t partial = 0;
  const uint32_t pictures = PortFreeze_PictureCount(&partial);
  if ((pictures == 0 || partial != 0) && ++sGrabTries < 60) {
    return;
  }
  if (pictures == 0) {
    FinishGrab(nullptr, "the game has not drawn a whole picture yet");
    return;
  }
  if (!PortFreeze_RedrawBegin()) {
    if (++sGrabTries < 600) return;
    FinishGrab(nullptr, "a port screen stayed open");
    return;
  }
  size_t size = 0;
  const uint8_t* frame = PortFreeze_Picture(&size);
  sColourFreeze = 0;
  const bool accepted = WriteAndCapture(frame, size);
  PortFreeze_RedrawEnd();
  ColourAsked(accepted);
}

bool PortShot_SaveWindow(const wchar_t* path) {
  PortShot_LinuxRequest(true, SaveWindowDone, new SaveTo{std::filesystem::path(path)});
  return true;
}

}
#endif
