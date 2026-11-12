#include <port_video.h>
#include <settings.h>

#include <game/winwork.h>
enum GSgfxLayerID {};
enum GSgfxLayerType {};
#include <game/gsgfxlayer.h>
struct GSgfxLayer : public GSgfxLayerData {};

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
struct Vec {
  float x, y, z;
};
struct GSbound;
int32_t GSboundIsInViewFast(GSbound* b);
void GSboundGetMinMax(GSbound* b, Vec* min, Vec* max, bool scale, bool rot, bool trans, bool ignoreRoot);
int32_t GSgfxProjectFastList(const Vec* points, Vec* screen, uint32_t count);
uint32_t GSgfxPerfbarCPUEntry(uint32_t id);
struct ModelHead {
  uint32_t flags;
  ModelHead* next;
};
extern ModelHead* _modelTopPtr;
}
void modelShadowUpdate();

namespace {

int32_t wide_screen_test(const Vec* screen, float left, float right) {
  uint8_t orCode = 0;
  uint8_t andCode = 0x1f;
  for (int i = 0; i < 8; ++i) {
    uint8_t code = screen[i].x < left ? 1 : screen[i].x > right ? 2 : 0;
    if (screen[i].y < 0.0f) {
      code |= 4;
    } else if (screen[i].y > 480.0f) {
      code |= 8;
    }
    if (screen[i].z == -1.0f) {
      code |= 0x10;
    }
    orCode |= code;
    andCode &= code;
  }
  return orCode == 0 ? 2 : andCode != 0 ? 0 : 1;
}

bool in_wide_view(GSbound* b, float wide) {
  Vec mm[2];
  GSboundGetMinMax(b, &mm[0], &mm[1], true, true, true, false);
  Vec points[8];
  for (int i = 0; i < 8; ++i) {
    points[i] = {mm[(i >> 2) & 1].x, mm[(i >> 1) & 1].y, mm[i & 1].z};
  }
  Vec screen[8];
  if (GSgfxProjectFastList(points, screen, 8) != 2) {
    return true;
  }
  const float half = 320.0f * wide;
  return wide_screen_test(screen, 320.0f - half, 320.0f + half) != 0;
}

bool wide_on(float* wide) {
  *wide = PortVideo_WideScale();
  return *wide > 1.001f;
}

std::vector<GSbound*> sWideOnly;
std::vector<GSbound*> sPending;
ptrdiff_t sBoundOffset = -1;

uint32_t* bound_owner_flags(GSbound* b) {
  const auto* bp = reinterpret_cast<const char*>(b);
  if (sBoundOffset >= 0) {
    for (ModelHead* m = _modelTopPtr; m != nullptr; m = m->next) {
      if (reinterpret_cast<const char*>(m) + sBoundOffset == bp) {
        return &m->flags;
      }
    }
    return nullptr;
  }
  ModelHead* best = nullptr;
  ptrdiff_t bestDiff = 0x400;
  for (ModelHead* m = _modelTopPtr; m != nullptr; m = m->next) {
    const ptrdiff_t d = bp - reinterpret_cast<const char*>(m);
    if (d >= static_cast<ptrdiff_t>(sizeof(ModelHead)) && d < bestDiff) {
      bestDiff = d;
      best = m;
    }
  }
  if (best == nullptr) {
    return nullptr;
  }
  sBoundOffset = bestDiff;
  std::fprintf(stderr, "[widescreen] a model's bound is at +0x%zx in its record\n", static_cast<size_t>(bestDiff));
  return &best->flags;
}

const bool sLog = [] {
  const char* v = std::getenv("XD_WIDE_CULL_LOG");
  return v != nullptr && v[0] == '1';
}();
uint64_t sTests = 0, sWideDrawn = 0;
uint32_t sHash = 2166136261u;

}

extern "C" int32_t PortXD_ModelBoundInView(GSbound* b) {
  const int32_t game = GSboundIsInViewFast(b);
  float wide = 1.f;
  if (game == 0 && wide_on(&wide) && in_wide_view(b, wide)) {
    sWideOnly.push_back(b);
  }
  if (sLog) {
    sHash = (sHash ^ static_cast<uint32_t>(game)) * 16777619u;
    if (++sTests % 2000 == 0) {
      std::fprintf(stderr, "[widescreen] cull: %llu model tests, game answers hash %08x, %llu drawn only for the wide picture\n",
                   static_cast<unsigned long long>(sTests), sHash, static_cast<unsigned long long>(sWideDrawn));
    }
  }
  return game;
}

void PortXD_ModelShadowUpdate() {
  modelShadowUpdate();
  sPending.swap(sWideOnly);
  sWideOnly.clear();
}

extern "C" uint32_t PortXD_ModelPerfEntry(uint32_t id) {
  if (id == 0xFF) {
    for (GSbound* b : sPending) {
      if (uint32_t* flags = bound_owner_flags(b)) {
        *flags |= 0x4000000u;
        ++sWideDrawn;
      }
    }
    sPending.clear();
  }
  return GSgfxPerfbarCPUEntry(id);
}

extern "C" int32_t PortXD_DrawBoundInView(GSbound* b) {
  const int32_t game = GSboundIsInViewFast(b);
  float wide = 1.f;
  if (game != 0 || !wide_on(&wide)) {
    return game;
  }
  return in_wide_view(b, wide) ? 1 : 0;
}

extern "C" {
void PortXD_RealSpriteSetEnv(s32 layer);
void PortXD_RealSpriteSetScissor(s32 layer, s32 x, s32 y, s32 w, s32 h);
GSgfxLayer* GSgfxLayerFindByID(int32_t id);
void spriteGetOffset(s32 layer, s16* x, s16* y);
void spriteSetOffset(s32 layer, s16 x, s16 y);
uint32_t PortVI_GetFlipCount(void);
void menuDataBiosGetXY(s32 menuID, s16* x, s16* y);
}

namespace {

constexpr int kEdge = 44;
constexpr int kMaxWidth = 440;
constexpr int kTouch = 2;
constexpr int kPairMax = 64;

const int sMenuLog = [] {
  const char* v = std::getenv("XD_WIDE_MENU_LOG");
  return v != nullptr ? std::atoi(v) : 0;
}();

engine::settings::BoolSetting sEdgeMenus;
bool sEdgeMenusReady = false;

struct Box {
  int x0, y0, x1, y1;
  bool Has(int x, int y) const { return x >= x0 - kTouch && x <= x1 + kTouch && y >= y0 - kTouch && y <= y1 + kTouch; }
};

struct WindowPlan {
  const WinWorkLayout* w = nullptr;
  int shift = 0;
  bool pieces = false;
  int left = 0, right = 0;
  int dx = 0, dy = 0;
  std::vector<std::pair<Box, int>> pieceShift;
  std::vector<std::pair<const void*, int>> spriteShift;
};

struct FramePlan {
  uint32_t flip = 0xFFFFFFFFu;
  bool screen = false;
  std::vector<WindowPlan> windows;
} sPlan;

const WindowPlan* sWindow = nullptr;
int sShift = 0;
int sBar = 0;

template <class F> void for_each_sprite(const void* first, F&& f) {
  int n = 0;
  for (auto* sp = static_cast<const SpriteWorkLayout*>(first); sp != nullptr && n < 512;
       sp = static_cast<const SpriteWorkLayout*>(sp->next), ++n) {
    f(sp);
  }
}

Box box_of(const WinWorkLayout* w, const SpriteWorkLayout* sp) {
  const int x = w->offX + sp->x, y = w->offY + sp->y;
  return Box{x, y, x + std::abs(static_cast<int>(sp->w)), y + std::abs(static_cast<int>(sp->h))};
}
bool shown(const SpriteWorkLayout* sp) { return (sp->kind & 2) != 0; }
bool hook(const SpriteWorkLayout* sp) { return (sp->flags & 8) != 0; }
bool picture(const SpriteWorkLayout* sp) { return shown(sp) && (sp->flags & 3) != 0 && sp->w != 0 && sp->h != 0; }

int side_shift(int x0, int x1) {
  if (x1 - x0 > kMaxWidth) return 0;
  const bool atLeft = x0 <= kEdge, atRight = x1 >= 640 - kEdge;
  return atLeft == atRight ? 0 : atLeft ? -sBar : sBar;
}

void plan_window(WindowPlan& p) {
  const WinWorkLayout* w = p.w;
  auto box_at = [&](const SpriteWorkLayout* sp) {
    Box b = box_of(w, sp);
    return Box{b.x0 + p.dx, b.y0 + p.dy, b.x1 + p.dx, b.y1 + p.dy};
  };
  int left[2] = {1 << 30, 1 << 30}, right[2] = {-(1 << 30), -(1 << 30)};
  std::vector<Box> pics;
  std::vector<const SpriteWorkLayout*> picSprites;
  for_each_sprite(w->itemSprites, [&](const SpriteWorkLayout* sp) {
    if (sp->w == 0 || sp->h == 0 || (!shown(sp) && !hook(sp))) return;
    if (!hook(sp) && !picture(sp)) return;
    const Box b = box_at(sp);
    const int k = picture(sp) ? 0 : 1;
    left[k] = std::min(left[k], b.x0);
    right[k] = std::max(right[k], b.x1);
    if (k == 0 || b.x1 - b.x0 <= kMaxWidth || (sp->flags & 3) != 0) {
      pics.push_back(b);
      picSprites.push_back(sp);
    }
  });
  const int use = left[0] <= right[0] ? 0 : 1;
  p.left = left[use];
  p.right = right[use];
  if (p.left > p.right) return;
  p.shift = side_shift(p.left, p.right);
  if (p.shift != 0 || p.right - p.left <= kMaxWidth) return;
  std::vector<int> group(pics.size());
  for (size_t i = 0; i < pics.size(); ++i) group[i] = static_cast<int>(i);
  auto find = [&](int i) {
    while (group[i] != i) i = group[i] = group[group[i]];
    return i;
  };
  for (size_t i = 0; i < pics.size(); ++i) {
    for (size_t j = i + 1; j < pics.size(); ++j) {
      const Box &a = pics[i], &b = pics[j];
      if (a.x0 <= b.x1 + kTouch && b.x0 <= a.x1 + kTouch && a.y0 <= b.y1 + kTouch && b.y0 <= a.y1 + kTouch) {
        group[find(static_cast<int>(i))] = find(static_cast<int>(j));
      }
    }
  }
  std::vector<Box> box(pics.size());
  std::vector<bool> used(pics.size(), false);
  for (size_t i = 0; i < pics.size(); ++i) {
    const int g = find(static_cast<int>(i));
    Box& b = box[g];
    b = used[g] ? Box{std::min(b.x0, pics[i].x0), std::min(b.y0, pics[i].y0), std::max(b.x1, pics[i].x1),
                      std::max(b.y1, pics[i].y1)}
                : pics[i];
    used[g] = true;
  }
  std::vector<int> shiftOf(pics.size(), 0);
  for (size_t g = 0; g < pics.size(); ++g) {
    if (used[g]) shiftOf[g] = side_shift(box[g].x0, box[g].x1);
  }
  for (size_t g = 0; g < pics.size(); ++g) {
    if (!used[g] || shiftOf[g] >= 0 || box[g].x1 - box[g].x0 > kPairMax) continue;
    for (size_t h = 0; h < pics.size(); ++h) {
      const Box &a = box[g], &b = box[h];
      if (used[h] && shiftOf[h] > 0 && std::abs(a.x0 - (640 - b.x1)) <= kTouch && std::abs(a.x1 - (640 - b.x0)) <= kTouch &&
          std::abs(a.y0 - b.y0) <= kTouch && std::abs(a.y1 - b.y1) <= kTouch) {
        shiftOf[g] = shiftOf[h] = 0;
      }
    }
  }
  bool any = false;
  for (size_t g = 0; g < pics.size(); ++g) {
    if (!used[g]) continue;
    p.pieceShift.push_back({box[g], shiftOf[g]});
    any = any || shiftOf[g] != 0;
  }
  if (!any) {
    p.pieceShift.clear();
    return;
  }
  p.pieces = true;
  for (size_t i = 0; i < pics.size(); ++i) {
    p.spriteShift.push_back({picSprites[i], shiftOf[find(static_cast<int>(i))]});
  }
}

int piece_shift_at(const WindowPlan& p, int x, int y) {
  for (const auto& [b, s] : p.pieceShift) {
    if (b.Has(x, y)) return s;
  }
  return 0;
}

int sprite_shift(const WindowPlan& p, const SpriteWorkLayout* sp, bool cursor) {
  if (!p.pieces) return p.shift;
  for (const auto& [s, v] : p.spriteShift) {
    if (s == sp) return v;
  }
  Box b = box_of(p.w, sp);
  if (b.x1 - b.x0 > kMaxWidth) return 0;
  b = Box{b.x0 + p.dx, b.y0 + p.dy, b.x1 + p.dx, b.y1 + p.dy};
  return cursor ? piece_shift_at(p, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2) : piece_shift_at(p, b.x0, b.y0);
}

void log_plan() {
  struct Seen {
    const void* w;
    int32_t menuID;
    s16 x, y;
    int left, right, shift;
    size_t pieces;
  };
  static std::vector<Seen> seen;
  static bool screen = false;
  if (sPlan.screen != screen) {
    screen = sPlan.screen;
    std::fprintf(stderr, "[widescreen] frame %u full-screen menu: %s\n", sPlan.flip, screen ? "nothing moves" : "ended");
  }
  for (const WindowPlan& p : sPlan.windows) {
    const WinWorkLayout* w = p.w;
    size_t moved = 0;
    for (const auto& ps : p.pieceShift) moved += ps.second != 0 ? 1 : 0;
    const Seen now{w, w->menuID, w->offX, w->offY, p.left, p.right, p.shift, moved};
    bool fresh = true;
    for (Seen& s : seen) {
      if (s.w != w) continue;
      fresh = false;
      if (s.menuID == now.menuID && s.x == now.x && s.y == now.y && s.left == now.left && s.right == now.right &&
          s.shift == now.shift && s.pieces == now.pieces) {
        goto next;
      }
      s = now;
      break;
    }
    if (fresh) {
      if (seen.size() > 256) seen.clear();
      seen.push_back(now);
    }
    std::fprintf(stderr, "[widescreen] frame %u menu 0x%X at %d,%d extent x %d..%d shift %d%s", sPlan.flip,
                 static_cast<unsigned>(w->menuID), w->offX, w->offY, p.left, p.right, p.shift, fresh ? " (new)" : "");
    for (const auto& [b, s] : p.pieceShift) {
      if (s != 0) std::fprintf(stderr, " | piece %d..%d,%d..%d shift %d", b.x0, b.x1, b.y0, b.y1, s);
    }
    std::fprintf(stderr, "\n");
    if (sMenuLog >= 2) {
      for_each_sprite(w->itemSprites, [](const SpriteWorkLayout* sp) {
        std::fprintf(stderr, "[widescreen]   sprite %d kind %02X flags %02X x %d y %d w %d h %d\n", sp->id,
                     static_cast<unsigned>(sp->kind) & 0xFF, sp->flags, sp->x, sp->y, sp->w, sp->h);
      });
    }
  next:;
  }
}

struct Rest {
  const void* w;
  int32_t menuID;
  uint32_t flip;
  s16 x, y;
  bool still;
  s16 stillX, stillY;
};
std::vector<Rest> sRest;

void decision_pos(const WinWorkLayout* w, uint32_t flip, int* x, int* y) {
  Rest* r = nullptr;
  for (Rest& e : sRest) {
    if (e.w == w) r = &e;
  }
  if (r == nullptr || r->menuID != w->menuID || flip - r->flip > 2) {
    if (r == nullptr) {
      if (sRest.size() > 256) sRest.clear();
      sRest.push_back({});
      r = &sRest.back();
    }
    s16 hx = w->offX, hy = w->offY;
    menuDataBiosGetXY(w->menuID, &hx, &hy);
    *r = Rest{w, w->menuID, flip, w->offX, w->offY, false, hx, hy};
    *x = hx;
    *y = hy;
    return;
  }
  if (r->flip != flip && r->x == w->offX && r->y == w->offY) {
    r->still = true;
    r->stillX = w->offX;
    r->stillY = w->offY;
  }
  r->x = w->offX;
  r->y = w->offY;
  r->flip = flip;
  *x = r->stillX;
  *y = r->stillY;
}

bool on_backdrop(int32_t menuID) {
  switch (menuID) {
  case 0x118:
  case 0xA4:
  case 0xA5:
  case 0xB4:
    return true;
  default:
    return false;
  }
}

std::vector<std::pair<const void*, int32_t>> sScreenWindows;

void plan_frame(const WinWorkLayout* any, uint32_t flip) {
  sPlan.flip = flip;
  sPlan.screen = false;
  sPlan.windows.clear();
  std::vector<std::pair<const void*, int32_t>> screenNow;
  const WinWorkLayout* head = any;
  for (int n = 0; head->prev != nullptr && n < 256; ++n) head = static_cast<const WinWorkLayout*>(head->prev);
  int n = 0;
  for (auto* w = head; w != nullptr && n < 256; w = static_cast<const WinWorkLayout*>(w->next), ++n) {
    if ((w->used & 2) == 0) continue;
    static bool cell[24][32];
    std::memset(cell, 0, sizeof cell);
    int covered = 0;
    for_each_sprite(w->itemSprites, [&](const SpriteWorkLayout* sp) {
      if (!picture(sp) || hook(sp)) return;
      const Box b = box_of(w, sp);
      for (int cy = std::max(0, (b.y0 + 9) / 20); cy < 24 && cy * 20 + 10 < b.y1; ++cy) {
        for (int cx = std::max(0, (b.x0 + 9) / 20); cx < 32 && cx * 20 + 10 < b.x1; ++cx) {
          if (cx * 20 + 10 < b.x0 || cy * 20 + 10 < b.y0 || cell[cy][cx]) continue;
          cell[cy][cx] = true;
          ++covered;
        }
      }
    });
    bool wasScreen = false;
    for (const auto& s : sScreenWindows) wasScreen = wasScreen || (s.first == w && s.second == w->menuID);
    if (covered >= 24 * 32 * 6 / 10 || wasScreen || on_backdrop(w->menuID)) {
      sPlan.screen = true;
      screenNow.push_back({w, w->menuID});
    }
    WindowPlan p;
    p.w = w;
    int x = w->offX, y = w->offY;
    decision_pos(w, flip, &x, &y);
    p.dx = x - w->offX;
    p.dy = y - w->offY;
    sPlan.windows.push_back(std::move(p));
  }
  sScreenWindows.swap(screenNow);
  for (WindowPlan& p : sPlan.windows) {
    if (!sPlan.screen) plan_window(p);
  }
  if (sMenuLog != 0) log_plan();
}

const WindowPlan* plan_of(const void* window) {
  const uint32_t flip = PortVI_GetFlipCount();
  for (int pass = 0; pass < 2; ++pass) {
    if (sPlan.flip == flip) {
      for (const WindowPlan& p : sPlan.windows) {
        if (p.w == window) return &p;
      }
      if (pass == 1) return nullptr;
    }
    plan_frame(static_cast<const WinWorkLayout*>(window), flip);
  }
  return nullptr;
}

bool edge_menus_on() {
  float wide = 1.f;
  if (!wide_on(&wide)) return false;
  if (!sEdgeMenusReady) {
    sEdgeMenusReady = true;
    sEdgeMenus = engine::settings::Global().Bool("video.widescreen_edge_menus");
  }
  if (!sEdgeMenus.Valid() || !sEdgeMenus.Get()) return false;
  sBar = static_cast<int>(320.0f * (wide - 1.0f) + 0.5f);
  return true;
}

}

extern "C" void PortXD_WideMenuSprite(const void* window, const void* sprite) {
  sWindow = nullptr;
  sShift = 0;
  if (!edge_menus_on()) return;
  sWindow = plan_of(window);
  if (sWindow == nullptr) return;
  bool cursor = true;
  for_each_sprite(sWindow->w->itemSprites, [&](const SpriteWorkLayout* sp) { cursor = cursor && sp != sprite; });
  sShift = sprite_shift(*sWindow, static_cast<const SpriteWorkLayout*>(sprite), cursor);
}

extern "C" int PortXD_WideMenuShiftNow(void) { return sShift; }

extern "C" void PortXD_WideMenuSpriteDone(void) { sShift = sWindow != nullptr && !sWindow->pieces ? sWindow->shift : 0; }

extern "C" void PortXD_MenuSpriteSetOffset(s32 layer, s16 x, s16 y) {
  sWindow = nullptr;
  sShift = 0;
  spriteSetOffset(layer, x, y);
}

extern "C" void spriteSetScissor(s32 layer, s32 x, s32 y, s32 w, s32 h) {
  PortXD_RealSpriteSetScissor(layer, x, y, w, h);
  if (sShift == 0) return;
  GSgfxLayer* l = GSgfxLayerFindByID(layer);
  if (l == nullptr) return;
  s16 ox = 0, oy = 0;
  spriteGetOffset(layer, &ox, &oy);
  const int left = std::clamp(ox + x + sShift, -sBar, 640 + sBar);
  l->hw.scissor[0] = static_cast<u16>(static_cast<s16>(left));
  if (sMenuLog != 0) {
    std::fprintf(stderr, "[widescreen] scissor layer %d x %d w %d moved by %d -> %d\n", static_cast<int>(layer),
                 ox + x, static_cast<int>(w), sShift, left);
  }
}

extern "C" void spriteSetEnv(s32 layer) {
  if (sShift == 0) {
    PortXD_RealSpriteSetEnv(layer);
    return;
  }
  s16 x = 0, y = 0;
  spriteGetOffset(layer, &x, &y);
  spriteSetOffset(layer, static_cast<s16>(x + sShift), y);
  PortXD_RealSpriteSetEnv(layer);
  spriteSetOffset(layer, x, y);
}
