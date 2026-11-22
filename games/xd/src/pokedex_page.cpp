#include "xd_test_run.h"
#include "xd_pokedex.h"

#include "xd_ui.h"
#include "xd_pc_trade.h"
#include "ui_kit2.h"
#include "memo_sprites.h"

#include <gx_texture_decode.h>
#include <i18n.h>
#include <port_menu.h>

#include <aurora/imgui.h>
#include <SDL3/SDL.h>
#include <port_user_folder.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <dolphin/types.h>
#include <game/faceData.h>
#include <game/gstexture.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/pokemon/pokemon.hpp>
#include <game/relglobal.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
uint32_t zokuseiBiosGetSmallSpriteID(uint16_t type);
uint32_t zokuseiBiosGetSpriteID(uint16_t type);
uint16_t zokuseiGetWazaJoutaiAsDefenseZokuseiAry(uint16_t attack, int16_t* types, int32_t count);
void fightMainWaitFrame(uint32_t frames);
void* GSresGetResourceAllGroup(u32 id);
void* Sequence_LoadPokemon(uint16_t id, uint32_t variant, bool rare);
void Sequence_SetVisibility(void* seq, bool visible);
void Sequence_DoAnimation(void* seq, int index);
void Sequence_Free(void* seq);
void GSfsysFreeCache(int32_t group);
struct IconPkxEntry { uint32_t a, b; };
extern IconPkxEntry pokemon_pkx_data[];
extern uint32_t pokemon_pkx_data_number;
s32 menuFaceLoadFromID(u16 id, void* fn, void* arg);
s8 menuFaceSearchEnable(u16 id);
void PortXD_RealMemoList(void);
}

namespace xd::pokedex {
namespace {

namespace ep = engine::pokedex;
namespace ui = xd::ui;
std::string Tr(const char* s) { return engine::i18n::Tr(s); }

struct Face {
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
  bool tried = false, ok = false, uploaded = false;
  ImTextureID tex{};
};
std::map<uint16_t, Face> sFaces;
bool sFacesBroken = false;

uint16_t FaceIdOf(uint16_t species) {
  if (pokemon_data == nullptr || species >= *pokemon_data_number) return 0;
  return pokemon_data[species].cgData[0].spriteId;
}

void LoadFace(uint16_t species) {
  Face& f = sFaces[species];
  if (f.tried) return;
  f.tried = true;
  if (sFacesBroken || poke_face == nullptr || poke_face_number == nullptr) return;
  const uint16_t faceId = FaceIdOf(species);
  if (faceId == 0 || faceId >= *poke_face_number) {
    std::fprintf(stderr, "[pokedex] face of species %u: no face id (%u)\n", species, faceId);
    return;
  }
  const u32 texID = static_cast<const MenuFaceData*>(poke_face)[faceId].texID;
  if (texID == 0) {
    std::fprintf(stderr, "[pokedex] face of species %u: no texture id\n", species);
    return;
  }
  menuFaceLoadFromID(faceId, nullptr, nullptr);
  int waited = 0;
  while (menuFaceSearchEnable(faceId) != 1 && waited < 400) {
    fightMainWaitFrame(1);
    waited++;
  }
  if (menuFaceSearchEnable(faceId) != 1) {
    std::fprintf(stderr, "[pokedex] the face of species %u did not load in %d frames: no pictures from now on\n", species, waited);
    sFacesBroken = true;
    return;
  }
  const auto* tex = static_cast<const GStexture*>(GSresGetResourceAllGroup(texID));
  if (tex == nullptr) {
    std::fprintf(stderr, "[pokedex] face of species %u: texture %u not in the resource table\n", species, texID);
    return;
  }
  const void* data = GXGetTexObjData(&tex->texObj);
  const u16 w = GXGetTexObjWidth(&tex->texObj), h = GXGetTexObjHeight(&tex->texObj);
  const u32 fmt = GXGetTexObjFmt(&tex->texObj);
  const u32 tlutFmt = tex->tlutFormat == 2 ? GX_TL_RGB565 : tex->tlutFormat == 3 ? GX_TL_RGB5A3 : GX_TL_IA8;
  f.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  if (data != nullptr && w > 0 && h > 0 && PortGX_DecodeTexture(data, w, h, fmt, tex->tlutData, tlutFmt, f.rgba.data())) {
    f.w = w, f.h = h, f.ok = true;
  } else {
    std::fprintf(stderr, "[pokedex] face of species %u: decode failed (%ux%u fmt %u)\n", species, w, h, fmt);
    f.rgba.clear();
  }
}

struct Picture {
  ImTextureID tex{};
  bool tried = false, ok = false;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  int bw = 1, bh = 1;
};
std::map<std::string, Picture> sPictures;

const Picture& PictureOf(const std::string& name) {
  Picture& p = sPictures[name];
  if (p.tried) return p;
  p.tried = true;
  const std::filesystem::path f = PortUser_Folder("Pokemon XD") / "icons" / "pokedex" / (name + ".png");
  std::error_code ec;
  if (!std::filesystem::exists(f, ec)) return p;
  SDL_Surface* loaded = SDL_LoadPNG(f.string().c_str());
  SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (rgba && rgba->pitch == rgba->w * 4) {
    const int w = rgba->w, h = rgba->h;
    const uint8_t* px = static_cast<const uint8_t*>(rgba->pixels);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        if (px[(static_cast<size_t>(y) * w + x) * 4 + 3] > 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 >= x0 && y1 >= y0) {
      p.u0 = static_cast<float>(x0) / w, p.u1 = static_cast<float>(x1 + 1) / w, p.v0 = static_cast<float>(y0) / h, p.v1 = static_cast<float>(y1 + 1) / h;
      p.bw = x1 - x0 + 1, p.bh = y1 - y0 + 1;
      p.tex = aurora_imgui_add_texture(static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgba->pixels);
      p.ok = true;
    }
  }
  if (rgba) SDL_DestroySurface(rgba);
  if (loaded) SDL_DestroySurface(loaded);
  return p;
}
ImTextureID PictureTexture(uint16_t species) { return PictureOf("pokemon_" + std::to_string(species)).tex; }
uint16_t PicSpecies(uint16_t species) {
  if (PictureTexture(species)) return species;
  const ep::Species* s = Find(species);
  return s != nullptr && s->formOf != 0 ? s->formOf : species;
}

bool DrawPicture(ImDrawList* dl, const std::string& name, float cx, float cy, float size) {
  const Picture& p = PictureOf(name);
  if (!p.ok) return false;
  const float k = size / static_cast<float>(std::max(p.bw, p.bh));
  const float w = p.bw * k, h = p.bh * k;
  dl->AddImage(p.tex, ui::At(cx - w / 2, cy - h / 2), ui::At(cx + w / 2, cy + h / 2), ImVec2(p.u0, p.v0), ImVec2(p.u1, p.v1));
  return true;
}
bool DrawStanding(ImDrawList* dl, const std::string& name, float cx, float baseY, float hPix) {
  const Picture& p = PictureOf(name);
  if (!p.ok) return false;
  const float k = hPix / static_cast<float>(p.bh);
  const float w = p.bw * k;
  dl->AddImage(p.tex, ui::At(cx - w / 2, baseY - hPix), ui::At(cx + w / 2, baseY), ImVec2(p.u0, p.v0), ImVec2(p.u1, p.v1));
  return true;
}

ImTextureID FaceTexture(uint16_t species) {
  const auto it = sFaces.find(species);
  if (it == sFaces.end() || !it->second.ok) return ImTextureID{};
  Face& f = it->second;
  if (!f.uploaded) {
    f.tex = aurora_imgui_add_texture(static_cast<uint32_t>(f.w), static_cast<uint32_t>(f.h), f.rgba.data());
    f.uploaded = true;
  }
  return f.tex;
}

enum class Mode { List, Show, Detail };
enum { kTabDetails, kTabStats, kTabMoves, kTabEvolution, kTabWhere, kTabView, kTabLooks, kTabSize, kTabMatchup, kTabs };

struct State {
  bool fresh = true;
  Mode mode = Mode::List;
  ep::Query query;
  std::vector<uint16_t> order;
  int cursor = 0;
  int tab = 0;
  int sub = 0;
  int rowCursor = 0;
  int showRow = 0;
  uint16_t species = 0;
  int form = 0;
  uint16_t formFor = 0;
  bool typing = false;
  int key = 0;
  uint16_t openGame = 0;
  bool needFace = false;
  uint16_t wantFace = 0;
  bool done = false;
  std::string note;
  bool yours = false;
  bool ivev = false;
  int owned = 0;
};
State S;
int sViewZoom = 0;
bool sPreloaded = false;
std::vector<ep::Species> sNone;

const uint8_t kTeal[4] = {0x20, 0xB4, 0xB0, 0xFF};
const uint8_t kYellow[4] = {0xF0, 0xB8, 0x20, 0xFF};
const uint8_t kWhite[4] = {0xF4, 0xF4, 0xF4, 0xFF};
const uint8_t kGreenTab[4] = {0x40, 0xE8, 0x40, 0xFF};
const uint8_t kGrey[4] = {0x9C, 0xA4, 0xA8, 0xFF};
const uint8_t kUp[4] = {0xFF, 0x70, 0x60, 0xFF};
const uint8_t kDown[4] = {0x78, 0xA8, 0xFF, 0xFF};
const uint8_t kBlack[4] = {0x08, 0x10, 0x18, 0xFF};
ImU32 C(const uint8_t* c, uint8_t a = 255) { return IM_COL32(c[0], c[1], c[2], a); }

void Backdrop(ImDrawList*) {}

void Panel(ImDrawList* dl, float x0, float y0, float x1, float y1) { xd::memo::Panel(dl, x0, y0, x1, y1); }

void Text(ImDrawList* dl, float x, float y, const std::string& t, const uint8_t* col, float scale = 0.8f, int font = 1, float maxW = 0.f) {
  ui::Text(dl, x, y, t, col, scale, maxW, font);
}
void TextR(ImDrawList* dl, float xr, float y, const std::string& t, const uint8_t* col, float scale = 0.8f, int font = 1) {
  ui::Text(dl, xr - ui::Width(t, scale, font), y, t, col, scale, 0.f, font);
}

void TypeSquare(ImDrawList* dl, float x, float y, const ep::Species& s, int slot = 0) {
  if (slot == 1 && (s.types[1] == s.types[0] || s.typeNames[1].empty())) return;
  xd::memo::Draw(dl, zokuseiBiosGetSmallSpriteID(static_cast<uint16_t>(s.types[slot])), x, y, 21, 18);
}

void Ball(ImDrawList* dl, float cx, float cy) {
  const float r = 8 * ui::Scale();
  const ImVec2 c = ui::At(cx, cy);
  dl->AddCircleFilled(c, r, IM_COL32(235, 235, 235, 255), 16);
  dl->PathClear();
  dl->PathArcTo(c, r, 3.14159f, 6.28318f, 12);
  dl->PathFillConvex(IM_COL32(220, 40, 40, 255));
  dl->AddCircle(c, r, IM_COL32(20, 20, 20, 255), 16, ui::Scale());
  dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), IM_COL32(20, 20, 20, 255), 2.f * ui::Scale());
  dl->AddCircleFilled(c, 2.6f * ui::Scale(), IM_COL32(245, 245, 245, 255), 10);
  dl->AddCircle(c, 2.6f * ui::Scale(), IM_COL32(20, 20, 20, 255), 10, ui::Scale());
}

const char* TabName(int t);
void Banner(ImDrawList* dl, const std::string& title, const std::vector<std::pair<std::string, bool>>& tabs, const std::string& prompts, int firstTab) {
  xd::memo::Banner(dl);
  xd::memo::DrawNatural(dl, 0xAB, 491, 79);
  xd::memo::Draw(dl, 0x87, 515, 80, 51, 22);
  xd::memo::Draw(dl, 0x86, 566, 80, 15, 22);
  if (!title.empty()) Text(dl, 72, 80, title, kTeal, 1.0f, 0);
  float x = firstTab > 0 ? 88.f : 72.f;
  for (size_t i = static_cast<size_t>(firstTab); i < tabs.size() && x < 400; i++) {
    const char* en = TabName(static_cast<int>(i));
    xd::kit2::Piece pc;
    const bool on = tabs[i].second;
    pc.tex = 6163;
    const std::string w = en;
    if (w == "View") pc = {6163, 11, on ? 79 : 106, 73, 25};
    else if (w == "Size") pc = {6163, 103, on ? 79 : 106, 69, 25};
    else if (w == "Details") pc = {6163, 181, on ? 78 : 106, 96, 28};
    else if (w == "Matchup") pc = {6163, on ? 103 : 1, 138, 100, 22};
    else pc.tex = 0;
    if (pc.tex && xd::kit2::Texture(6163).ok) {
      const float hh = static_cast<float>(pc.h);
      xd::kit2::DrawPiece(dl, ui::At(x, 90 - hh / 2), ui::At(x + pc.w, 90 + hh / 2), pc);
      x += pc.w + 16;
    } else {
      Text(dl, x, 80, tabs[i].first, on ? kGreenTab : kGrey, 0.9f, 0);
      x += ui::Width(tabs[i].first, 0.9f, 0) + 22;
    }
  }
  if (prompts == "@list") {
    xd::memo::DrawNatural(dl, 0x88, 270, 78);
    Text(dl, 298, 82, Tr("SHOW"), kWhite, 0.7f, 1);
    xd::memo::DrawNatural(dl, 0x17F, 358, 79);
    xd::memo::DrawNatural(dl, 0x180, 382, 79);
    Text(dl, 412, 82, Tr("SORT"), kWhite, 0.7f, 1);
  } else if (!prompts.empty()) {
    Text(dl, 262, 82, prompts, kWhite, 0.7f, 1);
  }
  Text(dl, 517, 82, Tr("CANCEL"), kWhite, 0.75f, 1);
}

void BottomBar(ImDrawList* dl, const std::string& left, const std::string& right = "") {
  xd::memo::BottomBar(dl);
  Text(dl, 150, 382, left, kWhite, 0.8f, 1);
  if (!right.empty()) Text(dl, 340, 382, right, kWhite, 0.8f, 1);
}

void Face(ImDrawList* dl, uint16_t species, float cx, float cy, float size) {
  species = PicSpecies(species);
  if (DrawPicture(dl, "pokemon_" + std::to_string(species), cx, cy, size)) return;
  const ImTextureID tex = FaceTexture(species);
  if (tex) dl->AddImage(tex, ui::At(cx - size / 2, cy - size / 2), ui::At(cx + size / 2, cy + size / 2), ImVec2(0.f, 0.f), ImVec2(42.f / static_cast<float>(sFaces[species].w), 42.f / static_cast<float>(sFaces[species].h)));
  else Text(dl, cx - 6, cy - 8, "?", kGrey, 1.0f, 0);
}

const char* TabName(int t) {
  switch (t) {
    case kTabDetails: return "Details";
    case kTabStats: return "Stats";
    case kTabMoves: return "Moves";
    case kTabEvolution: return "Evolution";
    case kTabWhere: return "Where";
    case kTabView: return "View";
    case kTabLooks: return "Looks";
    case kTabSize: return "Size";
    default: return "Matchup";
  }
}
std::string TabText(int t) { return Tr(TabName(t)); }

void Reorder() {
  S.order = ep::Arrange(All(), Player(), DexSettings(), S.query, MetOrder());
  if (S.cursor >= static_cast<int>(S.order.size())) S.cursor = std::max(0, static_cast<int>(S.order.size()) - 1);
}

const char* OrderName(ep::Order o) {
  switch (o) {
    case ep::Order::National: return "National No.";
    case ep::Order::AToZ: return "A-Z";
    case ep::Order::Type: return "Type";
    case ep::Order::Height: return "Height";
    case ep::Order::Weight: return "Weight";
    case ep::Order::OrderMet: return "Order met";
    default: return "Caught first";
  }
}
const char* ShowName(ep::Show s) {
  switch (s) {
    case ep::Show::All: return "All";
    case ep::Show::AllSeen: return "All seen";
    case ep::Show::Caught: return "Caught";
    case ep::Show::NotCaught: return "Not caught yet";
    case ep::Show::Shadow: return "Shadow Pokemon";
    default: return "From mods";
  }
}

uint16_t CurrentSpecies() { return S.cursor >= 0 && S.cursor < static_cast<int>(S.order.size()) ? S.order[static_cast<size_t>(S.cursor)] : 0; }

void DrawList(ImDrawList* dl, const PortMenuInput* in, bool& close) {
  const auto& all = All();
  const ep::Settings set = DexSettings();
  const ep::Counts counts = ep::Count(all, Player());
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridList, 11, 123, 161, 128);
  Banner(dl, Tr("Pokédex"), {}, "@list", 0);
  Panel(dl, 299, 131, 595, 369);
  const int rows = 8, n = static_cast<int>(S.order.size());
  int top = std::max(0, std::min(S.cursor - 4, n - rows));
  for (int r = 0; r < rows && top + r < n; r++) {
    const ep::Species* sp = Find(S.order[static_cast<size_t>(top + r)]);
    if (sp == nullptr) continue;
    const float y = 142.f + r * 27.f;
    const bool sel = top + r == S.cursor;
    const ep::Seen st = ep::Status(Player(), sp->index);
    if (sel) {
      xd::memo::Draw(dl, xd::memo::kRowGreen, 312, y, 256, 28);
      ui::Cursor(dl, 325.f, y + 14.f);
    }
    xd::memo::Draw(dl, xd::memo::kLine, 312, y + 27, 258, 1);
    Text(dl, 340, y + 3, ep::NumberText(*sp) + " " + ep::RowName(*sp, st, set), kWhite, 0.8f, 3);
    if (st != ep::Seen::Unseen || set.showAll) {
      const bool two = sp->types[1] != sp->types[0] && !sp->typeNames[1].empty();
      if (two) TypeSquare(dl, 485, y + 5, *sp, 0);
      TypeSquare(dl, 507, y + 5, *sp, two ? 1 : 0);
    }
    if (sp->mod && (st != ep::Seen::Unseen || set.showAll)) Text(dl, 430, y + 7, "MOD", kYellow, 0.6f, 1);
    if (st == ep::Seen::Caught) Ball(dl, 553, y + 15);
  }
  if (n > rows) {
    const float th = 213.f * rows / n, ty = 144.f + 213.f * top / n;
    dl->AddRectFilled(ui::At(574, ty), ui::At(580, ty + th), IM_COL32(24, 120, 130, 255));
  }
  const uint16_t cur = CurrentSpecies();
  if (cur != 0 && ep::CanOpen(Player(), cur, set)) {
    Face(dl, cur, 171, 250, 128);
    if (!PictureTexture(cur) && !FaceTexture(cur) && !sFacesBroken && !sFaces[cur].tried) {
      S.needFace = true;
      S.wantFace = cur;
    }
  }
  BottomBar(dl, Tr("Seen:") + " " + std::to_string(counts.seen) + "/" + std::to_string(counts.total),
            Tr("Caught:") + " " + std::to_string(counts.caught) + "/" + std::to_string(counts.total));

  if (in->repeat & PAD_BUTTON_DOWN) S.cursor = std::min(n - 1, S.cursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.cursor = std::max(0, S.cursor - 1);
  if (in->repeat & PAD_BUTTON_RIGHT) S.cursor = std::min(n - 1, S.cursor + 8);
  if (in->repeat & PAD_BUTTON_LEFT) S.cursor = std::max(0, S.cursor - 8);
  const auto step = [&](int d) {
    int o = (static_cast<int>(S.query.order) + d + 7) % 7;
    S.query.order = static_cast<ep::Order>(o);
    Reorder();
  };
  if (in->pressed & PAD_TRIGGER_L) step(-1);
  if (in->pressed & PAD_TRIGGER_R) step(1);
  if (in->pressed & PAD_BUTTON_X) S.mode = Mode::Show;
  if ((in->pressed & PAD_BUTTON_A) && cur != 0 && ep::CanOpen(Player(), cur, set)) {
    S.mode = Mode::Detail;
    S.species = cur;
    S.tab = kTabDetails;
    S.rowCursor = 0;
    S.sub = 0;
  }
  if (in->pressed & PAD_BUTTON_B) close = true;
}

std::string EntryText(const ep::Species& s) {
  if (!s.entry.empty()) return s.entry;
  return EntriesAvailable() ? Tr("No entry for this Pokemon in your Ultra Sun/Moon") : Tr("Add your Ultra Sun/Moon to see entries");
}

void DrawTabs(ImDrawList* dl) {
  std::vector<std::pair<std::string, bool>> tabs;
  for (int t = 0; t < kTabs; t++) tabs.push_back({TabText(t), t == S.tab});
  int first = std::max(0, std::min(S.tab - 2, kTabs - 3));
  Banner(dl, "", tabs, "", first);
  if (first > 0) Text(dl, 56, 80, "<<", kTeal, 0.9f, 0);
  if (first + 3 < kTabs) Text(dl, 468, 80, ">>", kTeal, 0.9f, 0);
}

void Notch(ImDrawList* dl, float x, float y, const std::string& label, float textX = 0.f) {
  xd::memo::Draw(dl, 0x171, x, y, 97, 15);
  Text(dl, textX > 0.f ? textX : x + 6.f, y - 3.f, label, kWhite, 0.62f, 1);
}

float NotchTextX(const std::string& label) { return std::max(160.f, 66.f + ui::Width(label, 0.62f, 1) + 12.f); }

void Divider(ImDrawList* dl, float x, float y, float w) { xd::memo::Draw(dl, xd::memo::kLine, x, y, w, 3); }

void BigType(ImDrawList* dl, float x, float y, uint8_t type) { xd::memo::Draw(dl, zokuseiBiosGetSpriteID(type), x, y, 72, 18); }

void MatchupFrame(ImDrawList* dl, const ep::Species& s) {
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridMatchup, 54, 123, 143, 69);
  Face(dl, s.index, 196, 188, 140);
  Panel(dl, 45, 258, 347, 411);
  Panel(dl, 348, 131, 595, 411);
  const ep::Species* nameOf = s.formOf != 0 ? Find(s.formOf) : &s;
  Text(dl, 375, 140, nameOf != nullptr ? nameOf->name : s.name, kWhite, 1.0f, 0);
  Text(dl, 375, 168, Imperial() && !s.category.empty() && s.category.find("POK") == std::string::npos ? s.category + " POKéMON" : s.category, kWhite, 0.75f, 3);
  const bool two = s.types[1] != s.types[0] && !s.typeNames[1].empty();
  BigType(dl, 366, 192, s.types[0]);
  if (two) BigType(dl, 444, 192, s.types[1]);
  Divider(dl, 361, 216, 221);
}

std::string ShownName(const ep::Species& s) {
  const ep::Species* b = s.formOf != 0 ? Find(s.formOf) : nullptr;
  return b != nullptr ? b->name + " (" + s.formName + ")" : s.name;
}

void BottomBarNo(ImDrawList* dl, const ep::Species& s, const std::string& extra = "") {
  xd::memo::BottomBar(dl);
  Text(dl, 76, 382, Tr("No."), kYellow, 0.8f, 1);
  Text(dl, 121, 382, s.national ? std::to_string(s.national) : "---", kWhite, 0.8f, 1);
  Text(dl, 190, 382, ShownName(s), kWhite, 0.8f, 1);
  if (!extra.empty()) TextR(dl, 560, 382, extra, kWhite, 0.8f, 1);
}

constexpr float kEntryWidth = 262.f, kEntryScale = 0.78f;
constexpr int kEntryLines = 5;

void DrawDetails(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  MatchupFrame(dl, s);
  Notch(dl, 58, 263, Tr("ENTRY:"));
  const auto lines = ep::Wrap(EntryText(s), kEntryWidth, [](const std::string& t) { return ui::Width(t, kEntryScale, 1); });
  const int total = static_cast<int>(lines.size()), maxScroll = std::max(0, total - kEntryLines);
  S.rowCursor = std::max(0, std::min(S.rowCursor, maxScroll));
  for (int i = 0; i < kEntryLines && S.rowCursor + i < total; i++) Text(dl, 62, 280.f + 24.f * static_cast<float>(i), lines[static_cast<size_t>(S.rowCursor + i)], kWhite, kEntryScale, 1);
  if (S.rowCursor > 0) xd::memo::Draw(dl, xd::memo::kArrowUp, 320, 262, 19, 13);
  if (S.rowCursor < maxScroll) xd::memo::Draw(dl, xd::memo::kArrowUp, 320, 396, 19, -13);
  if (in->repeat & PAD_BUTTON_DOWN) S.rowCursor = std::min(maxScroll, S.rowCursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.rowCursor = std::max(0, S.rowCursor - 1);
  const ep::Seen st = ep::Status(Player(), s.formOf ? s.formOf : s.index);
  const ep::Species* base = Find(s.formOf ? s.formOf : s.index);
  const int formCount = base != nullptr ? static_cast<int>(base->forms.size()) : 0;
  const bool hasForms = formCount > 0;
  const float yNo = hasForms ? 219.f : 224.f, yForm = 241.f, yAb = hasForms ? 263.f : 247.f, yHt = hasForms ? 305.f : 298.f,
              yWt = hasForms ? 327.f : 321.f, yGen = hasForms ? 350.f : 346.f, yCaught = hasForms ? 381.f : 378.f;
  Text(dl, 375, yNo, Tr("No."), kYellow, 0.78f, 1);
  TextR(dl, 575, yNo, s.national ? std::to_string(s.national) : "---", kWhite, 0.78f, 1);
  if (hasForms) {
    Text(dl, 375, yForm, Tr("FORM"), kYellow, 0.78f, 1);
    const std::string word = s.formOf ? s.formName : Tr("Normal");
    xd::memo::DrawNatural(dl, 0x88, 561, yForm + 1);
    TextR(dl, 556, yForm, word + "  " + std::to_string(S.form + 1) + "/" + std::to_string(formCount + 1), kWhite, 0.7f, 1);
  }
  Text(dl, 375, yAb, Tr("ABILITY:"), kYellow, 0.78f, 1);
  Text(dl, 375, yAb + 24, s.abilities[0] + (!s.abilities[1].empty() && s.abilities[1] != s.abilities[0] ? " / " + s.abilities[1] : ""), kWhite, 0.62f, 1, 196.f);
  Text(dl, 375, yHt, Tr("HT:"), kYellow, 0.78f, 1);
  TextR(dl, 575, yHt, ep::HeightText(s.heightDm, Imperial()), kWhite, 0.78f, 1);
  Text(dl, 375, yWt, Tr("WT:"), kYellow, 0.78f, 1);
  TextR(dl, 575, yWt, ep::WeightText(s.weightHg, Imperial()), kWhite, 0.78f, 1);
  if (s.genderRatio == 255) {
    Text(dl, 375, yGen, Tr("Genderless"), kWhite, 0.62f, 1);
  } else {
    const double f = std::floor(s.genderRatio / 32.0 + 0.5) * 12.5, m = 100.0 - f;
    char a[16], b[16];
    std::snprintf(a, sizeof a, "%g%%", m);
    std::snprintf(b, sizeof b, "%g%%", f);
    static const uint8_t kBlue[4] = {0x60, 0xA8, 0xFF, 0xFF}, kPink[4] = {0xFF, 0x80, 0xB8, 0xFF};
    float x = 375;
    if (m > 0) {
      Text(dl, x, yGen - 2, "♂", kBlue, 0.78f, 1);
      Text(dl, x + 21, yGen, a, kWhite, 0.66f, 1);
      x += 25 + ui::Width(a, 0.66f, 1) + 12;
    }
    if (f > 0) {
      Text(dl, x, yGen - 2, "♀", kPink, 0.78f, 1);
      Text(dl, x + 21, yGen, b, kWhite, 0.66f, 1);
    }
  }
  if (st == ep::Seen::Caught) Ball(dl, 383, yCaught + 11);
  Text(dl, st == ep::Seen::Caught ? 396.f : 375.f, yCaught, st == ep::Seen::Caught ? Tr("CAUGHT") : Tr("SEEN"), kWhite, 0.78f, 1);
}

const std::vector<PortXD_Owned>& OwnedNow(uint16_t species) {
  static std::vector<PortXD_Owned> list;
  static uint16_t of = 0;
  static int age = 1000;
  if (of != species || ++age > 30) {
    list = PortXD_OwnedOf(species);
    of = species, age = 0;
  }
  return list;
}

std::string NatureName(int n) {
  switch (n) {
  case 0: return Tr("Hardy");
  case 1: return Tr("Lonely");
  case 2: return Tr("Brave");
  case 3: return Tr("Adamant");
  case 4: return Tr("Naughty");
  case 5: return Tr("Bold");
  case 6: return Tr("Docile");
  case 7: return Tr("Relaxed");
  case 8: return Tr("Impish");
  case 9: return Tr("Lax");
  case 10: return Tr("Timid");
  case 11: return Tr("Hasty");
  case 12: return Tr("Serious");
  case 13: return Tr("Jolly");
  case 14: return Tr("Naive");
  case 15: return Tr("Modest");
  case 16: return Tr("Mild");
  case 17: return Tr("Quiet");
  case 18: return Tr("Bashful");
  case 19: return Tr("Rash");
  case 20: return Tr("Calm");
  case 21: return Tr("Gentle");
  case 22: return Tr("Sassy");
  case 23: return Tr("Careful");
  case 24: return Tr("Quirky");
  default: return std::string();
  }
}
int NatureStat(int nature, bool up) {
  const int map[5] = {1, 2, 5, 3, 4};
  const int u = nature / 5, d = nature % 5;
  if (u == d) return -1;
  return map[up ? u : d];
}
std::string WhereText(const PortXD_Owned& o) {
  char buf[48];
  if (o.box < 0) return Tr("PARTY");
  const std::string f = Tr("BOX %d");
  std::snprintf(buf, sizeof buf, f.c_str(), o.box + 1);
  return buf;
}

void DrawStats(ImDrawList* dl, const ep::Species& s) {
  MatchupFrame(dl, s);
  const std::vector<PortXD_Owned>& mine = OwnedNow(s.index);
  const bool yours = S.yours && !mine.empty();
  const PortXD_Owned* o = yours ? &mine[static_cast<size_t>(std::clamp(S.owned, 0, static_cast<int>(mine.size()) - 1))] : nullptr;
  Notch(dl, 58, 263, Tr("ABILITY:"));
  Text(dl, NotchTextX(Tr("ABILITY:")), 262, s.abilities[0], kWhite, 0.7f, 1);
  const auto lines = ep::Wrap(s.abilityText[0], 262.f, [](const std::string& t) { return ui::Width(t, 0.8f, 1); });
  for (size_t i = 0; i < lines.size() && i < 2; i++) Text(dl, 62, 288.f + 26.f * static_cast<float>(i), lines[i], kWhite, 0.8f, 1);
  Divider(dl, 58, 333, 276);
  Notch(dl, 57, 336, "");
  if (o == nullptr) {
    Text(dl, 62, 348, Tr("CATCH RATE"), kYellow, 0.68f, 1);
    Text(dl, 186, 348, std::to_string(s.catchRate), kWhite, 0.68f, 1);
    Text(dl, 62, 366, Tr("GROWTH"), kYellow, 0.68f, 1);
    Text(dl, 186, 366, s.growth, kWhite, 0.68f, 1);
  } else {
    Text(dl, 62, 348, Tr("NATURE"), kYellow, 0.68f, 1);
    Text(dl, 150, 348, NatureName(o->nature), kWhite, 0.68f, 1, 100.f);
    const std::string idx = std::to_string(std::clamp(S.owned, 0, static_cast<int>(mine.size()) - 1) + 1) + "/" + std::to_string(mine.size());
    char lv[24];
    const std::string lvf = Tr("Lv%d");
    std::snprintf(lv, sizeof lv, lvf.c_str(), o->level);
    Text(dl, 62, 366, idx, kYellow, 0.68f, 1);
    Text(dl, 96, 366, o->nickname, kWhite, 0.68f, 1, 104.f);
    Text(dl, 206, 366, lv, kWhite, 0.68f, 1, 42.f);
    const std::string where = WhereText(*o);
    TextR(dl, 330, 366, where, kYellow, std::min(0.68f, 72.f / std::max(1.f, ui::Width(where, 0.68f, 1))), 1);
  }
  const std::string names[6] = {Tr("HP"), Tr("ATTACK"), Tr("DEFENSE"), Tr("SP. ATK"), Tr("SP. DEF"), Tr("SPEED##stat")};
  const bool ivev = o != nullptr && S.ivev;
  if (ivev) {
    TextR(dl, 524, 208, "IV", kYellow, 0.5f, 1);
    TextR(dl, 569, 208, "EV", kYellow, 0.5f, 1);
  }
  const int up = o != nullptr ? NatureStat(o->nature, true) : -1, down = o != nullptr ? NatureStat(o->nature, false) : -1;
  int total = 0;
  for (int i = 0; i < 6; i++) {
    const float y = 224.f + 22.f * static_cast<float>(i);
    const int v = o != nullptr ? o->stats[static_cast<size_t>(i)] : s.base[i];
    total += v;
    const uint8_t* ink = i == up ? kUp : i == down ? kDown : kWhite;
    Text(dl, 374, y, names[i], kYellow, 0.68f, 1, 70.f);
    TextR(dl, 479, y, std::to_string(v), ink, 0.68f, 1);
    if (i == up || i == down) {
      const float mx = 479.f - ui::Width(std::to_string(v), 0.68f, 1) - 9.f;
      if (i == up) dl->AddTriangleFilled(ui::At(mx, y + 11), ui::At(mx + 6, y + 11), ui::At(mx + 3, y + 4), C(kUp));
      else dl->AddTriangleFilled(ui::At(mx, y + 5), ui::At(mx + 6, y + 5), ui::At(mx + 3, y + 12), C(kDown));
    }
    if (ivev) {
      TextR(dl, 524, y + 2, std::to_string(o->ivs[static_cast<size_t>(i)]), kWhite, 0.6f, 1);
      TextR(dl, 569, y + 2, std::to_string(o->evs[static_cast<size_t>(i)]), kWhite, 0.6f, 1);
      continue;
    }
    const float w = std::min(1.f, static_cast<float>(v) / (o != nullptr ? 400.f : 255.f)) * 80.f;
    dl->AddRectFilled(ui::At(489, y + 4), ui::At(569, y + 14), IM_COL32(60, 60, 60, 255));
    dl->AddRectFilled(ui::At(489, y + 4), ui::At(489 + w, y + 14), IM_COL32(24, 200, 40, 255));
  }
  Divider(dl, 370, 358, 205);
  Text(dl, 374, 364, Tr("TOTAL"), kYellow, 0.68f, 1);
  TextR(dl, 479, 364, std::to_string(total), kWhite, 0.68f, 1);
  if (!mine.empty()) {
    ui::Prompt(dl, 62, 398, {"Z"}, yours ? Tr("BASE") : Tr("YOURS"));
    if (yours) ui::Prompt(dl, 374, 398, {"Y"}, S.ivev ? Tr("HIDE IV / EV") : Tr("IV / EV"));
  }
}

void DrawMoves(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  const ep::MoveSource kinds[3] = {ep::MoveSource::Level, ep::MoveSource::Machine, ep::MoveSource::Tutor};
  const auto list = ep::MovesOf(s, kinds[S.sub]);
  MatchupFrame(dl, s);
  const char* heads[3] = {"LEVEL", "TM", "TUTOR"};
  const float hx[3] = {400.f, 468.f, 512.f};
  for (int i = 0; i < 3; i++) Text(dl, hx[i], 224, Tr(heads[i]), i == S.sub ? kYellow : kWhite, 0.7f, 1);
  xd::memo::DrawNatural(dl, 0x88, 360, 220);
  {
    const ImVec2 a = ui::At(hx[S.sub] - 14.f, 228.f), b = ui::At(hx[S.sub] - 9.f, 233.f), c = ui::At(hx[S.sub] - 4.f, 228.f);
    dl->AddLine(a, b, IM_COL32(240, 184, 32, 255), 2.5f * ui::Scale());
    dl->AddLine(b, c, IM_COL32(240, 184, 32, 255), 2.5f * ui::Scale());
  }
  const int rows = 5, n = static_cast<int>(list.size());
  S.rowCursor = std::max(0, std::min(S.rowCursor, std::max(0, n - 1)));
  const int top = std::max(0, std::min(S.rowCursor - 2, n - rows));
  for (int r = 0; r < rows && top + r < n; r++) {
    const ep::LearnedMove& m = list[static_cast<size_t>(top + r)];
    const float y = 246.f + 27.f * static_cast<float>(r);
    if (top + r == S.rowCursor) {
      xd::memo::Draw(dl, xd::memo::kRowGreen, 361, y, 221, 28);
      ui::Cursor(dl, 372.f, y + 14.f);
    }
    xd::memo::Draw(dl, xd::memo::kLine, 361, y + 27, 221, 1);
    std::string head;
    if (m.source == ep::MoveSource::Level) head = std::to_string(m.level);
    else if (m.source == ep::MoveSource::Machine) head = (m.number > 100 ? "HM" + std::to_string(m.number - 100) : "TM" + std::to_string(m.number));
    TextR(dl, 430, y + 3, head, kWhite, 0.75f, 3);
    Text(dl, 442, y + 3, m.name, kWhite, 0.75f, 3);
  }
  if (n == 0) Text(dl, 420, 300, Tr("None"), kWhite, 0.8f, 1);
  Notch(dl, 58, 263, Tr("MOVE:"));
  if (n > 0) {
    const ep::LearnedMove& m = list[static_cast<size_t>(S.rowCursor)];
    const MoveInfo mi = MoveInfoOf(m.move);
    Text(dl, NotchTextX(Tr("MOVE:")), 262, m.name, kWhite, 0.78f, 1);
    if (mi.ok) {
      float x = 62;
      const std::string lp = Tr("POWER"), vp = mi.power > 0 ? std::to_string(mi.power) : "---", la = Tr("ACCURACY"), va = mi.accuracy > 0 ? std::to_string(mi.accuracy) : "---";
      Text(dl, x, 290, lp, kYellow, 0.66f, 1);
      x += ui::Width(lp, 0.66f, 1) + 8;
      Text(dl, x, 290, vp, kWhite, 0.66f, 1);
      x += ui::Width(vp, 0.66f, 1) + 16;
      Text(dl, x, 290, la, kYellow, 0.66f, 1);
      x += ui::Width(la, 0.66f, 1) + 8;
      Text(dl, x, 290, va, kWhite, 0.66f, 1);
      Text(dl, 62, 312, Tr("TYPE"), kYellow, 0.66f, 1);
      BigType(dl, 66 + ui::Width(Tr("TYPE"), 0.66f, 1), 311, mi.type);
      const auto lines = ep::Wrap(mi.text, 262.f, [](const std::string& t) { return ui::Width(t, 0.7f, 1); });
      for (size_t i = 0; i < lines.size() && i < 3; i++) Text(dl, 62, 338.f + 22.f * static_cast<float>(i), lines[i], kWhite, 0.7f, 1);
    }
  }
  if (in->repeat & PAD_BUTTON_DOWN) S.rowCursor = std::min(n - 1, S.rowCursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.rowCursor = std::max(0, S.rowCursor - 1);
  if (in->pressed & PAD_BUTTON_X) { S.sub = (S.sub + 1) % 3; S.rowCursor = 0; }
}

void FamilyOf(uint16_t index, std::vector<uint16_t>& out) {
  uint16_t root = index;
  for (int g = 0; g < 8; g++) {
    const ep::Species* r = Find(root);
    if (r == nullptr || r->evolvesFrom == 0) break;
    root = r->evolvesFrom;
  }
  std::vector<uint16_t> todo{root};
  while (!todo.empty() && out.size() < 24) {
    const uint16_t id = todo.back();
    todo.pop_back();
    if (std::find(out.begin(), out.end(), id) != out.end()) continue;
    out.push_back(id);
    if (const ep::Species* s = Find(id)) {
      for (auto it = s->evolutions.rbegin(); it != s->evolutions.rend(); ++it) todo.push_back(it->to);
    }
  }
}

void ListFrame(ImDrawList* dl, uint16_t species) {
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridList, 11, 123, 161, 128);
  Panel(dl, 299, 131, 595, 369);
  Face(dl, species, 171, 250, 190);
}

void DrawEvolution(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  std::vector<uint16_t> fam;
  FamilyOf(s.formOf != 0 && s.evolvesFrom == 0 && s.evolutions.empty() ? s.formOf : s.index, fam);
  const ep::Settings set = DexSettings();
  const int n = static_cast<int>(fam.size());
  S.rowCursor = std::max(0, std::min(S.rowCursor, std::max(0, n - 1)));
  const uint16_t shown = n > 0 ? fam[static_cast<size_t>(S.rowCursor)] : s.index;
  ListFrame(dl, ep::CanOpen(Player(), shown, set) ? shown : s.index);
  const int rows = 8, top = std::max(0, std::min(S.rowCursor - 3, n - rows));
  for (int r = 0; r < rows && top + r < n; r++) {
    const ep::Species* m = Find(fam[static_cast<size_t>(top + r)]);
    if (m == nullptr) continue;
    const float y = 142.f + 27.f * static_cast<float>(r);
    const bool sel = top + r == S.rowCursor;
    if (sel) {
      xd::memo::Draw(dl, xd::memo::kRowGreen, 312, y, 256, 28);
      ui::Cursor(dl, 325.f, y + 14.f);
    }
    xd::memo::Draw(dl, xd::memo::kLine, 312, y + 27, 258, 1);
    const ep::Seen st = ep::Status(Player(), m->index);
    const std::string nm = ep::EvolutionTargetName(*m, st, set);
    Text(dl, 340, y + 3, nm, kWhite, 0.8f, 3);
    if (st == ep::Seen::Caught) Ball(dl, 356.f + ui::Width(nm, 0.8f, 3) * 1.25f, y + 15);
    if (m->evolvesFrom != 0) {
      if (const ep::Species* from = Find(m->evolvesFrom)) {
        for (const ep::Evolution& e : from->evolutions) {
          if (e.to == m->index) {
            TextR(dl, 574, y + 5, e.text, kYellow, 0.66f, 3);
            break;
          }
        }
      }
    }
  }
  BottomBarNo(dl, s);
  if (in->repeat & PAD_BUTTON_DOWN) S.rowCursor = std::min(n - 1, S.rowCursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.rowCursor = std::max(0, S.rowCursor - 1);
  if ((in->pressed & PAD_BUTTON_A) && n > 0 && S.rowCursor < n) {
    const uint16_t target = fam[static_cast<size_t>(S.rowCursor)];
    if (ep::CanOpen(Player(), target, set)) { S.species = target; S.rowCursor = 0; }
  }
}

void DrawWhere(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  Backdrop(dl);
  Panel(dl, 47, 131, 595, 369);
  const ep::Where& w = WhereOf(s.index);
  const ep::Settings set = DexSettings();
  struct Row {
    int kind = 0;
    const ep::WhereLine* l = nullptr;
    std::string text;
  };
  std::vector<Row> rows;
  auto section = [&](const std::string& head, const std::vector<ep::WhereLine>& lines, int kind) {
    std::vector<const ep::WhereLine*> shown;
    for (const ep::WhereLine& l : lines)
      if (kind == 2 || ep::TrainerLineVisible(Player(), l, set)) shown.push_back(&l);
    if (shown.empty()) return;
    rows.push_back({0, nullptr, head});
    for (const ep::WhereLine* l : shown) rows.push_back({kind, l, ""});
  };
  section(Tr("POKE SPOT"), w.spots, 1);
  section(Tr("SHADOW POKEMON"), w.shadow, 1);
  section(Tr("SEEN WITH TRAINERS"), w.trainers, 1);
  section(Tr("YOURS"), w.yours, 2);
  const int n = static_cast<int>(rows.size()), visible = 9;
  S.rowCursor = std::max(0, std::min(S.rowCursor, std::max(0, n - visible)));
  for (int i = 0; i < visible && S.rowCursor + i < n; i++) {
    const Row& r = rows[static_cast<size_t>(S.rowCursor + i)];
    const float y = 139.f + 25.f * static_cast<float>(i);
    if (r.kind == 0) {
      Notch(dl, 66, y + 3, "", 0);
      Text(dl, 72, y, r.text, kWhite, 0.62f, 1);
      continue;
    }
    const ep::WhereLine& l = *r.l;
    if (r.kind == 1) {
      Text(dl, 74, y + 2, l.level ? "Lv" + std::to_string(l.level) : "", kYellow, 0.62f, 1);
      Text(dl, 126, y + 2, l.who, kWhite, 0.66f, 1, 190.f);
      Text(dl, 330, y + 2, l.place, kWhite, 0.66f, 1, 190.f);
      if (!l.note.empty()) TextR(dl, 574, y + 2, l.note, kYellow, 0.62f, 1);
    } else {
      Ball(dl, 80, y + 11);
      Text(dl, 96, y + 2, "Lv" + std::to_string(l.level), kYellow, 0.62f, 1);
      if (l.sex) Text(dl, 150, y + 2, l.sex == 'M' ? "M" : "F", l.sex == 'M' ? kGreenTab : kYellow, 0.62f, 1);
      Text(dl, 200, y + 2, l.place, kWhite, 0.66f, 1);
      if (!l.note.empty()) TextR(dl, 574, y + 2, l.note, kYellow, 0.62f, 1);
    }
    Divider(dl, 62, y + 22, 508);
  }
  if (n == 0) Text(dl, 74, 150, Tr("Nowhere in the wild: it is given, traded or evolved."), kWhite, 0.7f, 1);
  if (S.rowCursor > 0) xd::memo::Draw(dl, xd::memo::kArrowUp, 576, 134, 19, 13);
  if (S.rowCursor + visible < n) xd::memo::Draw(dl, xd::memo::kArrowUp, 576, 361, 19, -13);
  const ep::Seen st = ep::Status(Player(), s.formOf ? s.formOf : s.index);
  BottomBarNo(dl, s, st == ep::Seen::Caught ? Tr("CAUGHT") : "");
  if (in->repeat & PAD_BUTTON_DOWN) S.rowCursor = std::min(std::max(0, n - visible), S.rowCursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.rowCursor = std::max(0, S.rowCursor - 1);
}

void DrawMatchup(ImDrawList* dl, const ep::Species& s) {
  MatchupFrame(dl, s);
  static const uint16_t kAttack[17] = {0, 1, 3, 4, 2, 6, 5, 7, 8, 0xA, 0xB, 0xD, 0xC, 0xF, 0xE, 0x10, 0x11};
  static const uint16_t kJoutai[4] = {0x43, 0x42, 0x3F, 0x41};
  static const uint16_t kMark[4] = {0x16A, 0x16B, 0x16C, 0x16D};
  int16_t types[2] = {static_cast<int16_t>(s.types[0]), static_cast<int16_t>(s.types[1])};
  const int count = (s.types[1] != s.types[0] && !s.typeNames[1].empty()) ? 2 : 1;
  for (int i = 0; i < 17; i++) {
    const float x = i < 9 ? 366.f : 481.f;
    const float y = 222.f + 20.f * static_cast<float>(i < 9 ? i : i - 9);
    BigType(dl, x, y, static_cast<uint8_t>(kAttack[i]));
    const uint16_t j = zokuseiGetWazaJoutaiAsDefenseZokuseiAry(kAttack[i], types, count);
    for (int k = 0; k < 4; k++)
      if (j == kJoutai[k]) xd::memo::Draw(dl, kMark[k], x + 78.f, y + 1.f, 15, 16);
  }
  Notch(dl, 58, 263, Tr("ABILITY:"));
  Text(dl, NotchTextX(Tr("ABILITY:")), 262, s.abilities[0], kWhite, 0.7f, 1);
  const auto lines = ep::Wrap(s.abilityText[0], 262.f, [](const std::string& t) { return ui::Width(t, 0.8f, 1); });
  for (size_t i = 0; i < lines.size() && i < 3; i++) Text(dl, 62, 288.f + 26.f * static_cast<float>(i), lines[i], kWhite, 0.8f, 1);
}

void DrawSize(ImDrawList* dl, const ep::Species& s) {
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridView, 46, 124, 277, 129);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 14, 243, 30, 16);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 596, 243, -30, 16);
  float k = 6.15f;
  const float michael = 13.5f * k, mon = std::max(1.f, static_cast<float>(s.heightDm)) * k;
  const float biggest = std::max(michael, mon);
  if (biggest > 200.f) k *= 200.f / biggest;
  const float base = 318.f;
  const float cx = 320.f;
  const bool hasM = DrawStanding(dl, "michael", cx + 40.f, base, 13.5f * k);
  const bool hasP = DrawStanding(dl, "pokemon_" + std::to_string(PicSpecies(s.index)), cx - 55.f, base, std::max(1.f, static_cast<float>(s.heightDm)) * k);
  if (!hasM) Text(dl, cx + 40.f, base - 60.f, "?", kWhite, 1.0f, 0);
  if (!hasP) Face(dl, s.index, cx - 55.f, base - 40.f, 60.f);
  xd::memo::BottomBar(dl);
  Text(dl, 140, 382, Tr("HT:"), kYellow, 0.8f, 1);
  TextR(dl, 310, 382, ep::HeightText(s.heightDm, Imperial()), kWhite, 0.8f, 1);
  Text(dl, 336, 382, Tr("WT:"), kYellow, 0.8f, 1);
  TextR(dl, 510, 382, ep::WeightText(s.weightHg, Imperial()), kWhite, 0.8f, 1);
}

bool sShinyView = false;
void DrawView(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridView, 46, 124, 277, 129);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 14, 243, 30, 16);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 596, 243, -30, 16);
  const bool shinyOk = ep::ShinyAllowed(Player(), s.index, DexSettings());
  const std::string base = "pokemon_" + std::to_string(PicSpecies(s.index));
  static const float kZoom[3] = {1.0f, 1.3f, 1.6f};
  if (in->pressed & PAD_TRIGGER_R) sViewZoom = std::min(2, sViewZoom + 1);
  if (in->pressed & PAD_TRIGGER_L) sViewZoom = std::max(0, sViewZoom - 1);
  const float zs = kZoom[sViewZoom];
  dl->PushClipRect(ui::At(48, 126), ui::At(592, 350), true);
  const bool shown = sShinyView && shinyOk && DrawPicture(dl, base + "_shiny", 320, 250, 150 * zs);
  if (!shown && !DrawPicture(dl, base, 320, 250, 150 * zs)) Face(dl, s.index, 320, 250, 120 * zs);
  dl->PopClipRect();
  xd::memo::BottomBar(dl);
  {
    const float shinyAt = shinyOk ? 574.f - ui::Width(Tr("Shiny"), 0.8f, 1) - 40.f : 574.f;
    const float zx = shinyAt - ui::Width(Tr("Zoom"), 0.7f, 1);
    xd::memo::DrawNatural(dl, 0x17F, zx - 54.f, 380);
    xd::memo::DrawNatural(dl, 0x180, zx - 30.f, 380);
    Text(dl, zx, 382, Tr("Zoom"), kWhite, 0.7f, 1);
  }
  Text(dl, 76, 382, Tr("No."), kYellow, 0.8f, 1);
  Text(dl, 121, 382, s.national ? std::to_string(s.national) : "---", kWhite, 0.8f, 1);
  Text(dl, 170, 382, ShownName(s), kWhite, 0.68f, 1, 215.f);
  if (shinyOk) {
    TextR(dl, 574, 382, Tr("Shiny"), sShinyView ? kGreenTab : kWhite, 0.8f, 1);
    xd::memo::DrawNatural(dl, 0x88, 574.f - ui::Width(Tr("Shiny"), 0.8f, 1) - 32.f, 379);
  }
  if ((in->pressed & PAD_BUTTON_X) && shinyOk) sShinyView = !sShinyView;
}

void DrawLooks(ImDrawList* dl, const PortMenuInput* in, const ep::Species& s) {
  Backdrop(dl);
  xd::memo::Grid(dl, xd::memo::kGridView, 46, 124, 277, 129);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 14, 243, 30, 16);
  xd::memo::Draw(dl, xd::memo::kSideArrow, 596, 243, -30, 16);
  std::vector<LookSource> srcs;
  for (const LookSource& l : LookSources())
    if (l.onPc) srcs.push_back(l);
  const int n = static_cast<int>(srcs.size());
  S.rowCursor = std::max(0, std::min(S.rowCursor, std::max(0, n - 1)));
  const std::string pinned = LookPin(s.national);
  std::vector<std::pair<float, int>> named;
  const int shown = std::min(n, 3);
  const int first = std::max(0, std::min(S.rowCursor - 1, n - shown));
  for (int i = 0; i < shown; i++) {
    const LookSource& l = srcs[static_cast<size_t>(first + i)];
    const float cx = 320.f + (static_cast<float>(i) - (shown - 1) / 2.f) * 180.f;
    const std::string name = (l.id == "xd" ? std::string() : l.id + "/") + "pokemon_" + std::to_string(PicSpecies(s.index));
    if (!DrawPicture(dl, name, cx, 250.f, 180.f)) Face(dl, s.index, cx, 250.f, 100.f);
    named.push_back({cx, first + i});
  }
  if (first > 0) xd::memo::Draw(dl, xd::memo::kSideArrow, 44, 243, 30, 16);
  xd::memo::BottomBar(dl);
  for (const auto& nm : named) {
    const LookSource& l = srcs[static_cast<size_t>(nm.second)];
    const float w = ui::Width(l.name, 0.72f, 1), x = nm.first - w / 2.f;
    Text(dl, x, 382, l.name, kWhite, 0.72f, 1);
    if (nm.second == S.rowCursor) ui::Cursor(dl, x - 12.f, 391.f);
    if (pinned == l.id) Ball(dl, x + w + 10.f, 391.f);
  }
  TextR(dl, 592, 382, "A " + (pinned == (n > 0 ? srcs[static_cast<size_t>(S.rowCursor)].id : "") ? Tr("Unpin") : Tr("Pin")), kWhite, 0.8f, 1);
  if (in->repeat & PAD_BUTTON_DOWN) S.rowCursor = std::min(n - 1, S.rowCursor + 1);
  if (in->repeat & PAD_BUTTON_UP) S.rowCursor = std::max(0, S.rowCursor - 1);
  if ((in->pressed & PAD_BUTTON_A) && n > 0 && s.national != 0) {
    const std::string& id = srcs[static_cast<size_t>(S.rowCursor)].id;
    SetLookPin(s.national, pinned == id ? std::string() : id);
  }
}

const char* const kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";
void Arrow(ImDrawList* dl, float x, float y, bool right) {
  const float s = ui::Scale();
  const ImVec2 a = ui::At(x, y - 6), b = ui::At(x + (right ? 6.f : -6.f), y), c = ui::At(x, y + 6);
  dl->AddLine(a, b, IM_COL32(240, 240, 240, 255), 2.5f * s);
  dl->AddLine(b, c, IM_COL32(240, 240, 240, 255), 2.5f * s);
}

void DrawShow(ImDrawList* dl, const PortMenuInput* in) {
  const auto& all = All();
  const ep::Counts counts = ep::Count(all, Player());
  const uint16_t cur = CurrentSpecies();
  ListFrame(dl, cur);
  Banner(dl, Tr("Pokédex"), {}, "", 0);
  Notch(dl, 308, 138, "", 0);
  Text(dl, 314, 135, Tr("SHOW"), kWhite, 0.62f, 1);
  const std::string label[5] = {Tr("ORDER"), Tr("SHOW"), Tr("TYPE"), Tr("GEN."), Tr("NAME")};
  const std::string value[5] = {Tr(OrderName(S.query.order)), Tr(ShowName(S.query.show)),
                                S.query.typeId < 0 ? Tr("All") : TypeNameOf(static_cast<uint8_t>(S.query.typeId)),
                                S.query.generation == 0 ? Tr("All") : std::to_string(S.query.generation),
                                S.query.nameContains.empty() ? std::string("----------") : S.query.nameContains};
  for (int i = 0; i < 5; i++) {
    const float y = 160.f + i * 27.f;
    const bool sel = i == S.showRow;
    if (sel) {
      xd::memo::Draw(dl, xd::memo::kRowGreen, 312, y, 256, 28);
      ui::Cursor(dl, 325.f, y + 14.f);
    }
    xd::memo::Draw(dl, xd::memo::kLine, 312, y + 27, 258, 1);
    Text(dl, 340, y + 4, label[i], kYellow, 0.8f, 1);
    ui::TextC(dl, 410, y + 4, 140, value[i], kWhite, 0.8f, 1);
    if (sel && i < 4) {
      Arrow(dl, 428, y + 14, false);
      Arrow(dl, 550, y + 14, true);
    }
  }
  xd::memo::BottomBar(dl);
  Text(dl, 150, 382, Tr("Seen:") + " " + std::to_string(counts.seen) + "/" + std::to_string(counts.total), kWhite, 0.8f, 1);
  Text(dl, 340, 382, Tr("Caught:") + " " + std::to_string(counts.caught) + "/" + std::to_string(counts.total), kWhite, 0.8f, 1);
  if (S.typing) {
    Panel(dl, 299, 131, 595, 369);
    Text(dl, 330, 140, Tr("NAME"), kYellow, 0.8f, 1);
    ui::TextC(dl, 330, 140, 240, S.query.nameContains.empty() ? std::string("----------") : S.query.nameContains, kWhite, 0.8f, 1);
    const int per = 7, n = static_cast<int>(std::strlen(kKeys));
    for (int k = 0; k < n + 2; k++) {
      const float x = 330.f + (k % per) * 34.f, y = 176.f + (k / per) * 34.f;
      const bool sel = k == S.key;
      if (sel) xd::memo::Draw(dl, xd::memo::kRowGreen, x - 4, y - 2, 30, 28);
      const std::string t = k < n ? std::string(1, kKeys[k]) : (k == n ? Tr("DEL") : Tr("OK"));
      Text(dl, x, y, t, sel ? kWhite : kYellow, 0.75f, 1);
    }
    const int total = n + 2;
    if (in->repeat & PAD_BUTTON_RIGHT) S.key = (S.key + 1) % total;
    if (in->repeat & PAD_BUTTON_LEFT) S.key = (S.key + total - 1) % total;
    if (in->repeat & PAD_BUTTON_DOWN) S.key = std::min(total - 1, S.key + per);
    if (in->repeat & PAD_BUTTON_UP) S.key = std::max(0, S.key - per);
    if (in->pressed & PAD_BUTTON_A) {
      if (S.key < n) { if (S.query.nameContains.size() < 10) S.query.nameContains += kKeys[S.key]; }
      else if (S.key == n) { if (!S.query.nameContains.empty()) S.query.nameContains.pop_back(); }
      else S.typing = false;
      Reorder();
    }
    if (in->pressed & PAD_BUTTON_X) { if (!S.query.nameContains.empty()) S.query.nameContains.pop_back(); Reorder(); }
    if (in->pressed & PAD_BUTTON_B) S.typing = false;
    return;
  }
  if (in->repeat & PAD_BUTTON_DOWN) S.showRow = (S.showRow + 1) % 5;
  if (in->repeat & PAD_BUTTON_UP) S.showRow = (S.showRow + 4) % 5;
  const int d = (in->repeat & PAD_BUTTON_RIGHT) ? 1 : (in->repeat & PAD_BUTTON_LEFT) ? -1 : 0;
  if (d != 0) {
    if (S.showRow == 0) S.query.order = static_cast<ep::Order>((static_cast<int>(S.query.order) + d + 7) % 7);
    else if (S.showRow == 1) S.query.show = static_cast<ep::Show>((static_cast<int>(S.query.show) + d + 6) % 6);
    else if (S.showRow == 2) S.query.typeId = std::max(-1, std::min(17, S.query.typeId + d));
    else if (S.showRow == 3) S.query.generation = std::max(0, std::min(7, S.query.generation + d));
    Reorder();
  }
  if ((in->pressed & PAD_BUTTON_A) && S.showRow == 4) { S.typing = true; S.key = 0; return; }
  if (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_X) || ((in->pressed & PAD_BUTTON_A) && S.showRow != 4)) S.mode = Mode::List;
}

void DrawDetail(ImDrawList* dl, const PortMenuInput* in) {
  const ep::Species* species = Find(S.species);
  if (species == nullptr) { S.mode = Mode::List; return; }
  if (S.formFor != S.species) { S.formFor = S.species; S.form = 0; }
  const int formCount = static_cast<int>(species->forms.size());
  if (S.form > formCount) S.form = 0;
  if (formCount > 0 && S.tab != kTabMoves && S.tab != kTabView && (in->pressed & PAD_BUTTON_X)) S.form = (S.form + 1) % (formCount + 1);
  const ep::Species* sp = S.form > 0 ? Find(species->forms[static_cast<size_t>(S.form - 1)]) : species;
  if (sp == nullptr) sp = species;
  if (S.tab == kTabStats) {
    const std::vector<PortXD_Owned>& mine = OwnedNow(sp->index);
    if (mine.empty()) S.yours = S.ivev = false;
    if (!mine.empty() && (in->pressed & PAD_TRIGGER_Z)) S.yours = !S.yours, S.ivev = false, S.owned = 0;
    if (S.yours && !mine.empty()) {
      const int n = static_cast<int>(mine.size());
      if (in->repeat & PAD_BUTTON_DOWN) S.owned = (S.owned + 1) % n;
      if (in->repeat & PAD_BUTTON_UP) S.owned = (S.owned + n - 1) % n;
      if (in->pressed & PAD_BUTTON_Y) S.ivev = !S.ivev;
    }
  } else {
    S.yours = S.ivev = false;
  }
  Backdrop(dl);
  DrawTabs(dl);
  if (!PictureTexture(S.species) && !sFaces[S.species].tried && !sFacesBroken) { S.needFace = true; S.wantFace = S.species; }
  switch (S.tab) {
    case kTabDetails: DrawDetails(dl, in, *sp); break;
    case kTabStats: DrawStats(dl, *sp); break;
    case kTabMoves: DrawMoves(dl, in, *sp); break;
    case kTabEvolution: DrawEvolution(dl, in, *sp); break;
    case kTabWhere: DrawWhere(dl, in, *sp); break;
    case kTabView: DrawView(dl, in, *sp); break;
    case kTabLooks: DrawLooks(dl, in, *sp); break;
    case kTabSize: DrawSize(dl, *sp); break;
    default: DrawMatchup(dl, *sp); break;
  }
  if (in->repeat & PAD_BUTTON_RIGHT) { S.tab = (S.tab + 1) % kTabs; S.rowCursor = 0; }
  if (in->repeat & PAD_BUTTON_LEFT) { S.tab = (S.tab + kTabs - 1) % kTabs; S.rowCursor = 0; }
  const auto next = [&](int d) {
    const int n = static_cast<int>(S.order.size());
    for (int k = 1; k <= n; k++) {
      const int i = ((S.cursor + d * k) % n + n) % n;
      if (ep::CanOpen(Player(), S.order[static_cast<size_t>(i)], DexSettings())) { S.cursor = i; S.species = S.order[static_cast<size_t>(i)]; return; }
    }
  };
  if (S.tab != kTabView && S.tab != kTabSize && S.tab != kTabMatchup) {
    if (in->pressed & PAD_TRIGGER_L) next(-1);
    if (in->pressed & PAD_TRIGGER_R) next(1);
  }
  if (in->pressed & PAD_BUTTON_B) S.mode = Mode::List;
}

void WrapCheck() {
  const char* dir = std::getenv("XD_POKEDEX_WRAPCHECK");
  static bool done = false;
  if (dir == nullptr || done) return;
  done = true;
  for (const char* lang : {"en", "fr", "de", "it", "es", "ja"}) {
    for (const char* game : {"sun", "moon"}) {
      const std::filesystem::path f = std::filesystem::path(dir) / (std::string("entries_") + lang + "_" + game + ".txt");
      FILE* fp = std::fopen(f.string().c_str(), "rb");
      if (fp == nullptr) continue;
      std::string text;
      char buf[4096];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, n);
      std::fclose(fp);
      int count = 0, over = 0, most = 0;
      std::map<int, int> hist;
      for (size_t at = 0; at < text.size();) {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(at, e - at);
        at = e + 1;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const int lines = static_cast<int>(ep::Wrap(line.substr(tab + 1), kEntryWidth, [](const std::string& t2) { return ui::Width(t2, kEntryScale, 1); }).size());
        count++;
        hist[lines]++;
        most = std::max(most, lines);
        if (lines > kEntryLines) over++;
      }
      std::string h;
      for (const auto& kv : hist) h += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
      std::fprintf(stderr, "[wrapcheck] %s %s: %d texts, most lines %d, over %d lines: %d  (lines:count%s)\n", lang, game, count, most, kEntryLines, over, h.c_str());
    }
  }
}

extern "C" uint32_t* __hsd_rand_seed_hold;
void ModelProbe() {
  const char* v = std::getenv("XD_POKEDEX_MODELPROBE");
  static bool done = false;
  if (v == nullptr || done || !xd::TestRun()) return;
  done = true;
  const bool load = std::strncmp(v, "load:", 5) == 0;
  const char* list = std::strchr(v, ':');
  if (list == nullptr) return;
  list++;
  int frames = 90;
  if (const char* c = std::strchr(list, ':')) frames = std::atoi(c + 1);
  const auto rng = []() -> unsigned { return __hsd_rand_seed_hold != nullptr ? static_cast<unsigned>(*__hsd_rand_seed_hold) : 0u; };
  for (const char* q = list; *q != 0 && *q != ':';) {
    char* end = nullptr;
    const int row = static_cast<int>(std::strtol(q, &end, 10));
    if (end == q) break;
    q = *end == ',' ? end + 1 : end;
    std::fprintf(stderr, "[model probe] %s row %d: start rng %08x\n", load ? "load" : "noload", row, rng());
    void* seq = nullptr;
    uint32_t group = 0, group2 = 0;
    if (load) {
      const uint16_t model = static_cast<uint16_t>(static_cast<uint32_t>(pokemonGetStatus(nullptr, static_cast<uint32_t>(row), 0x66, 0)));
      if (model != 0) {
        if (model < pokemon_pkx_data_number) group = pokemon_pkx_data[model].a, group2 = pokemon_pkx_data[model].b;
        seq = Sequence_LoadPokemon(model, 0, false);
      }
      if (seq != nullptr) {
        Sequence_SetVisibility(seq, false);
        Sequence_DoAnimation(seq, 0);
      }
    }
    for (int i = 0; i < frames; i++) fightMainWaitFrame(1);
    std::fprintf(stderr, "[model probe] row %d: loaded %s, after %d frames rng %08x\n", row, seq != nullptr ? "yes" : "no", frames, rng());
    if (seq != nullptr) {
      Sequence_SetVisibility(seq, false);
      Sequence_Free(seq);
      if (group != 0) GSfsysFreeCache(static_cast<int32_t>(group));
      if (group2 != 0 && std::getenv("XD_POKEDEX_MODELPROBE_B") != nullptr) GSfsysFreeCache(static_cast<int32_t>(group2));
      std::fprintf(stderr, "[model probe] row %d: file groups %u / %u\n", row, group, group2);
    }
    const char* after = std::getenv("XD_POKEDEX_MODELPROBE_AFTER");
    for (int i = 0, n = after != nullptr ? std::atoi(after) : 30; i < n; i++) fightMainWaitFrame(1);
    std::fprintf(stderr, "[model probe] row %d: freed, rng %08x\n", row, rng());
  }
}

void PageOpen(void*) {
  S.needFace = false;
  WrapCheck();
  S.done = false;
  RefreshPlayer();
}

bool PageFrame(void*, const PortMenuInput* in) {
  float px, py, ps;
  PortMenu_PictureRect(&px, &py, &ps);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  bool close = false;
  switch (S.mode) {
    case Mode::List: DrawList(dl, in, close); break;
    case Mode::Show: DrawShow(dl, in); break;
    case Mode::Detail: DrawDetail(dl, in); break;
  }
  if (S.done || close) return false;
  if (S.needFace && !sPreloaded) return false;
  return true;
}

void PageClose(void*) {}

}

bool RunPage() {
  ModelProbe();
  S = State();
  Reorder();
  S.fresh = false;
  if (const char* start = std::getenv("XD_POKEDEX_START")) {
    const bool national = start[std::strlen(start) - 1] == 'n';
    const int want = std::atoi(start);
    for (size_t i = 0; i < S.order.size(); i++) {
      const ep::Species* sp = Find(S.order[i]);
      if (sp != nullptr && (national ? sp->national == want : sp->index == want)) S.cursor = static_cast<int>(i);
    }
  }
  sPreloaded = std::getenv("XD_POKEDEX_PRELOAD") != nullptr;
  if (sPreloaded) {
    RefreshPlayer();
    for (const ep::Species& sp : All()) {
      if (ep::CanOpen(Player(), sp.index, DexSettings())) LoadFace(sp.index);
    }
  }
  for (int rounds = 0; rounds < 2000; rounds++) {
    S.needFace = false;
    if (S.wantFace != 0) LoadFace(S.wantFace);
    for (int r = -2; r < 10 && S.mode == Mode::List; r++) {
      const int i = S.cursor + r;
      if (i >= 0 && i < static_cast<int>(S.order.size()) && ep::CanOpen(Player(), S.order[static_cast<size_t>(i)], DexSettings())) LoadFace(S.order[static_cast<size_t>(i)]);
    }
    PortMenuPage pg{"pokedex", PageOpen, PageFrame, PageClose, nullptr};
    int guard = 0;
    while (!PortMenu_Open(&pg) && guard++ < 600) fightMainWaitFrame(1);
    while (PortMenu_Active() || PortMenu_Pending()) fightMainWaitFrame(1);
    if (S.done || !S.needFace) break;
  }
  const uint16_t open = S.openGame;
  S.openGame = 0;
  if (open != 0) PortXD_RealMemoList();
  return true;
}

}

extern "C" void PortXD_PokedexOpen(void) { xd::pokedex::RunPage(); }

extern "C" void menuPdaMemoList(void) {
  if (xd::pokedex::Enabled()) PortXD_PokedexOpen();
  else PortXD_RealMemoList();
}
