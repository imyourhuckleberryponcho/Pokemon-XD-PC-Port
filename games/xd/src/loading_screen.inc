namespace ls {

namespace fs = std::filesystem;

struct Pic {
  ImTextureID id{};
  float w = 0, h = 0;
  bool ok = false;
};
std::map<std::string, Pic> sPics;

Pic PicOf(const fs::path& file, bool shadow = false) {
  const std::u8string u8 = file.u8string();
  const std::string key = std::string(u8.begin(), u8.end()) + (shadow ? "|shadow" : "");
  auto it = sPics.find(key);
  if (it != sPics.end()) return it->second;
  Pic p;
  std::error_code ec;
  if (fs::exists(file, ec)) {
    xd::picker::Picture pic = xd::picker::LoadThumbnail(file, true);
    if (shadow) {
      for (size_t i = 0; i + 3 < pic.rgba.size(); i += 4) {
        const float l = (pic.rgba[i] + pic.rgba[i + 1] + pic.rgba[i + 2]) / (3.f * 255.f), k = 0.25f + 0.75f * l;
        pic.rgba[i] = static_cast<uint8_t>(0x30 + (0x9A - 0x30) * k), pic.rgba[i + 1] = static_cast<uint8_t>(0x12 + (0x6A - 0x12) * k);
        pic.rgba[i + 2] = static_cast<uint8_t>(0x66 + (0xFF - 0x66) * k);
      }
    }
    if (pic.width > 0 && pic.height > 0) {
      p.id = aurora_imgui_add_texture(pic.width, pic.height, pic.rgba.data());
      p.w = static_cast<float>(pic.width), p.h = static_cast<float>(pic.height), p.ok = true;
    }
  }
  sPics[key] = p;
  return p;
}
fs::path Rendered(const char* kind, const char* name) { return xd::UserFolder() / kind / "rendered" / (std::string(name) + ".png"); }

struct Info {
  bool newGame = true;
  std::string title;
  std::string sub;
  std::vector<std::pair<std::string, bool>> chips;
  fs::path face;
  fs::path thumb;
};
Info I;
kit::Age age;

Info InfoFor(bool newGame, const std::string& label) {
  Info in;
  in.newGame = newGame;
  if (in.newGame) {
    in.title = Tr("Starting a new journey");
    in.sub = Tr("Getting Orre ready with your chosen rules");
    in.face = Rendered("icons", "eevee");
    const std::string dot = "\xC2\xB7";
    size_t at = 0;
    for (;;) {
      const size_t next = label.find(dot, at);
      std::string item = label.substr(at, next == std::string::npos ? std::string::npos : next - at);
      while (!item.empty() && item.back() == ' ') item.pop_back();
      while (!item.empty() && item.front() == ' ') item.erase(item.begin());
      if (!item.empty()) in.chips.push_back({item, in.chips.empty()});
      if (next == std::string::npos) break;
      at = next + dot.size();
    }
    if (in.chips.empty()) {
      const bool original = es::Global().CurrentPreset() == es::Preset::Original;
      in.chips = {{original ? Tr("Faithful") : Tr("Enhanced"), true}, {Tr("Vanilla"), false}};
    }
  } else {
    xd::picker::Row row;
    const bool have = xd::picker::RowOf(xd::CardRoot(), row);
    in.title = Fmt(Tr("Loading %s"), have && !row.name.empty() ? row.name.c_str() : Tr("your save"));
    if (have) {
      in.sub = row.trainer + "   " + Fmt(Tr("Play time %s"), row.playTime.c_str());
      in.thumb = row.thumbnail;
      size_t at = 0;
      for (bool first = true; at <= row.rulesFull.size(); first = false) {
        const size_t nl = row.rulesFull.find('\n', at);
        const std::string item = row.rulesFull.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        if (!item.empty()) in.chips.push_back({item, first});
        if (nl == std::string::npos) break;
        at = nl + 1;
      }
    }
    in.face = Rendered("icons", "michael");
  }
  return in;
}

Info TestInfo() {
  const char* mode = std::getenv("XD_LOADSCREEN_TEST");
  Info in = InfoFor(mode == nullptr || std::strcmp(mode, "continue") != 0, "");
  if (const char* c = std::getenv("XD_LOADSCREEN_CHIPS")) {
    in.chips.clear();
    std::string all = c;
    size_t at = 0;
    for (;;) {
      const size_t bar = all.find('|', at);
      in.chips.push_back({all.substr(at, bar == std::string::npos ? std::string::npos : bar - at), in.chips.empty()});
      if (bar == std::string::npos) break;
      at = bar + 1;
    }
  }
  return in;
}

void Open(void*) {
  I = TestInfo();
  age.Reset();
  std::fprintf(stderr, "[loading screen] %s: \"%s\" (%zu chips)\n", I.newGame ? "new game" : "continue", I.title.c_str(), I.chips.size());
}

struct Spark {
  float x, y, r, v, w;
};
std::vector<Spark> sSparks;
void MakeSparks() {
  if (!sSparks.empty()) return;
  std::mt19937 rng(20261114u);
  std::uniform_real_distribution<float> u(0.f, 1.f);
  for (int i = 0; i < 90; i++) sSparks.push_back({u(rng), u(rng), u(rng) * 2.5f + 0.6f, u(rng) * 0.0012f + 0.0004f, u(rng) * 6.28f});
}

float sLogoDelay = 0.3f, sLogoFade = 1.2f, sPanelDelay = 0.6f, sPanelFade = 0.8f;

void Draw(ImDrawList* dl, float t) {
  float W = 640.f, H = 480.f;
  if (ImGui::GetCurrentContext() != nullptr) W = ImGui::GetIO().DisplaySize.x, H = ImGui::GetIO().DisplaySize.y;
  Screen sw;
  sw.s = H / 480.f;
  const float gw = W / sw.s;
  auto At = [&](float x, float y) { return ImVec2(x * sw.s, y * sw.s); };
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(7, 4, 15, 255));

  const Pic sky = PicOf(Rendered("backgrounds", "title_scene"));
  if (sky.ok) {
    const float k = 0.5f - 0.5f * std::cos(t * 6.2831853f / 80.f);
    const float scale = 1.05f + 0.07f * k, dx = (-0.03f + 0.06f * k) * gw, dy = -0.02f * k * 480.f;
    const float cover = std::max(gw / sky.w, 480.f / sky.h) * scale;
    const float sw2 = sky.w * cover, sh2 = sky.h * cover;
    const float x0 = (gw - sw2) / 2.f + dx, y0 = (480.f - sh2) / 2.f + dy;
    dl->AddImage(sky.id, At(x0, y0), At(x0 + sw2, y0 + sh2), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(150, 136, 172, 255));
  } else {
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0x22, 0x10, 0x48, 255), IM_COL32(0x22, 0x10, 0x48, 255),
                                IM_COL32(0x07, 0x04, 0x0F, 255), IM_COL32(0x07, 0x04, 0x0F, 255));
    for (int i = 0; i < 60; i++) {
      const float sx = std::fmod(static_cast<float>(i) * 97.31f, 1.f) * gw, sy = std::fmod(static_cast<float>(i) * 0.6180339f, 1.f) * 300.f;
      const float tw = 0.5f + 0.5f * std::sin(t * 1.3f + static_cast<float>(i));
      dl->AddCircleFilled(At(sx, sy), (0.6f + 0.6f * tw) * sw.s, IM_COL32(0xD8, 0xCC, 0xFF, static_cast<int>(60 + 120 * tw)), 8);
    }
  }

  const Pic lugia = PicOf(Rendered("icons", "shadow_lugia"), true);
  if (lugia.ok) {
    const float lw = std::min(gw * 0.27f, 235.f), lh = lw * lugia.h / lugia.w;
    const float f = std::fmod(t + 4.f, 14.f) / 14.f;
    const float lx = gw + 20.f - (gw + lw + 40.f) * f;
    const float ly = 294.f - lh / 2.f - 480.f * 0.016f * (0.5f - 0.5f * std::cos(t * 6.2831853f / 3.2f));
    for (int g = 4; g >= 1; g--) {
      const float e = 5.f * static_cast<float>(g);
      dl->AddImage(lugia.id, At(lx - e, ly - e * 0.8f), At(lx + lw + e, ly + lh + e * 0.8f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0xA0, 0x70, 0xFF, 40));
    }
    dl->AddImage(lugia.id, At(lx, ly), At(lx + lw, ly + lh), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 225));
  }

  MakeSparks();
  for (const Spark& p : sSparks) {
    float y = std::fmod(p.y - p.v * t * 60.f, 1.1f);
    if (y < -0.05f) y += 1.1f;
    const float x = (p.x + std::sin(t / 1.8f + p.w) * 0.01f) * gw, yy = y * 480.f, r = p.r * 0.9f;
    dl->AddCircleFilled(At(x, yy), r * 3.f * sw.s, IM_COL32(0x7B, 0x3C, 0xFF, 26), 12);
    dl->AddCircleFilled(At(x, yy), r * 1.4f * sw.s, IM_COL32(0xBE, 0x96, 0xFF, 70), 10);
    dl->AddCircleFilled(At(x, yy), r * 0.6f * sw.s, IM_COL32(0xE6, 0xD6, 0xFF, 150), 8);
  }

  const ImU32 clear = IM_COL32(5, 2, 15, 0), dark = IM_COL32(5, 2, 15, 210);
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H * 0.22f), dark, dark, clear, clear);
  dl->AddRectFilledMultiColor(ImVec2(0, H * 0.70f), ImVec2(W, H), clear, clear, dark, dark);
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W * 0.18f, H), dark, clear, clear, dark);
  dl->AddRectFilledMultiColor(ImVec2(W * 0.82f, 0), ImVec2(W, H), clear, dark, dark, clear);

  const Pic logo = PicOf(Rendered("icons", "logo"));
  if (logo.ok) {
    const float a = std::clamp((t - sLogoDelay) / sLogoFade, 0.f, 1.f), ease = 1.f - (1.f - a) * (1.f - a);
    const float lw = std::min(gw * 0.30f, 262.f) * (0.94f + 0.06f * ease), lh = lw * logo.h / logo.w;
    const float lx = (gw - lw) / 2.f, ly = 480.f * 0.05f;
    dl->AddImage(logo.id, At(lx + 3.f, ly + 5.f), At(lx + lw + 3.f, ly + lh + 5.f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, static_cast<int>(110 * ease)));
    dl->AddImage(logo.id, At(lx, ly), At(lx + lw, ly + lh), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * ease)));
  }

  const float pa = std::clamp((t - sPanelDelay) / sPanelFade, 0.f, 1.f), rise = (1.f - pa) * 14.f;
  if (pa <= 0.f) return;
  const float px0 = gw * 0.04f, px1 = gw * 0.96f, py1 = 480.f * 0.94f + rise, py0 = py1 - 92.f;
  const int A = static_cast<int>(255.f * pa);
  dl->AddRectFilledMultiColor(At(px0, py0), At(px0 + (px1 - px0) * 0.7f, py1), IM_COL32(20, 8, 45, 209 * A / 255), IM_COL32(20, 8, 45, 140 * A / 255),
                              IM_COL32(20, 8, 45, 140 * A / 255), IM_COL32(20, 8, 45, 209 * A / 255));
  dl->AddRectFilledMultiColor(At(px0 + (px1 - px0) * 0.7f, py0), At(px1, py1), IM_COL32(20, 8, 45, 140 * A / 255), IM_COL32(20, 8, 45, 0),
                              IM_COL32(20, 8, 45, 0), IM_COL32(20, 8, 45, 140 * A / 255));
  dl->AddRectFilled(At(px0, py0), At(px0 + 3.f, py1), IM_COL32(0x7B, 0x3C, 0xFF, A));
  float x = px0 + 18.f;
  const float cy = (py0 + py1) / 2.f;
  kit::Ring(dl, sw, x + 30.f, cy, 30.f);
  x += 74.f;
  if (!I.newGame) {
    const Pic th = PicOf(I.thumb);
    kit::PictureBox(dl, sw, x, cy - 30.f, 107.f, 60.f, th.ok ? th.id : ImTextureID{}, th.w, th.h, Tr("no picture yet"));
    x += 122.f;
  }
  const Pic face = PicOf(I.face);
  kit::Face(dl, sw, x + 28.f, cy, 28.f, face.ok ? face.id : ImTextureID{});
  x += 72.f;
  const int dots = static_cast<int>(std::fmod(t, 1.5f) / 0.375f);
  const float room = px1 - x - 10.f;
  const uint8_t ink[4] = {0xF3, 0xEE, 0xFF, static_cast<uint8_t>(A)}, inkDim[4] = {0xB9, 0xAC, 0xD9, static_cast<uint8_t>(A)};
  kit::Text(dl, sw, x, py0 + 12.f, I.title + std::string(static_cast<size_t>(dots), '.'), ink, 1.f, room, 1);
  if (!I.sub.empty()) kit::Text(dl, sw, x, py0 + 38.f, I.sub, inkDim, 0.72f, room);
  float chx = x;
  for (const auto& c : I.chips) {
    if (chx + kit::Width(c.first, 0.62f) + 18.f > px1) break;
    chx += kit::Chip(dl, sw, chx, py0 + 62.f, c.first, c.second) + 6.f;
  }
}

bool Frame(void*, const PortMenuInput* in) {
  (void)in;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  Draw(dl, age.Tick());
  kit::FadeIn(dl, age.age, 0.3f);
  return true;
}

const PortMenuPage kPage{"loading", Open, Frame, nullptr, nullptr};

enum class Cover { None, NewGame, Continue, Battle, Room };
struct Landing {
  Cover kind = Cover::None;
  bool ending = false;
  float endAge = 0.f;
  long polls = 0;
  uint16_t hold = 0;
  uint32_t holdUntil = 0, gapUntil = 0;
  int lastMenu = -1;
  bool yesDone = false;
  int32_t floorAtYes = -1;
  int settled = 0;
  const char* why = "";
  long blocked = 0;
  uint16_t prevHeld = 0;
};
Landing C;
kit::Age coverAge;

bool LogOn() {
  static int on = -1;
  if (on < 0) on = std::getenv("XD_LANDING_LOG") != nullptr ? 1 : 0;
  return on == 1;
}

void CoverStart(Cover kind, const std::string& label, const char* why) {
  if (kind == Cover::None) return;
  C = Landing();
  C.kind = kind;
  C.why = why;
  I = InfoFor(kind == Cover::NewGame, label);
  coverAge.Reset();
  sLogoDelay = 0.f, sLogoFade = 0.35f, sPanelDelay = 0.05f, sPanelFade = 0.25f;
  std::fprintf(stderr, "[landing] %s: the loading screen covers the way in (%s)\n", kind == Cover::NewGame ? "new game" : "continue", why);
}

void CoverBattle(const std::vector<std::string>& chips, bool rematch) {
  C = Landing();
  C.kind = Cover::Battle;
  C.why = rematch ? "the rematch" : "the online battle";
  I = Info();
  I.newGame = true;
  I.title = Tr("Starting the battle");
  I.sub = Tr("Both sides are ready, the battle begins in a moment");
  I.face = Rendered("icons", "eevee");
  for (const std::string& c : chips) I.chips.push_back({c, I.chips.empty()});
  coverAge.Reset();
  sLogoDelay = 0.f, sLogoFade = 0.35f, sPanelDelay = 0.05f, sPanelFade = 0.25f;
  std::fprintf(stderr, "[landing] battle: the loading screen covers the menu presses (%s)\n", C.why);
}

void CoverRoomResume() {
  C = Landing();
  C.kind = Cover::Room;
  C.why = "started again into the room";
  I = Info();
  I.newGame = true;
  I.title = Tr("Getting back into the room");
  I.sub = Tr("The game restarts with the room's rules");
  I.face = Rendered("icons", "eevee");
  coverAge.Reset();
  sLogoDelay = 0.f, sLogoFade = 0.35f, sPanelDelay = 0.05f, sPanelFade = 0.25f;
  std::fprintf(stderr, "[landing] room: the loading screen covers the way back into the room\n");
}

void CoverNewGame(const std::string& label, const char* why) { CoverStart(Cover::NewGame, label, why); }
void CoverContinue(const char* why) { CoverStart(Cover::Continue, "", why); }

long GiveUpPolls() {
  static const long n = [] { const char* v = std::getenv("XD_LANDING_GIVEUP_POLLS"); return v != nullptr ? std::strtol(v, nullptr, 0) : 60L * 90; }();
  return n > 0 ? n : 60L * 90;
}

void CoverEnd(const char* why) {
  if (C.kind == Cover::None || C.ending) return;
  C.ending = true;
  C.endAge = coverAge.age;
  std::fprintf(stderr, "[landing] handed over at game frame %u: %s (floor 0x%X)\n", PortVI_GetFlipCount(), why,
               static_cast<unsigned>(GSfloorGetCurrent()));
}

bool CoverActive() { return C.kind != Cover::None; }

void CoverMute(PADStatus* status) {
  if (C.kind == Cover::None) return;
  uint16_t held = 0;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
    PADStatus& s = status[i];
    if (s.err != PAD_ERR_NONE) continue;
    held |= s.button;
    s.button = 0;
    s.stickX = s.stickY = s.substickX = s.substickY = 0;
    s.triggerLeft = s.triggerRight = 0;
    s.analogA = s.analogB = 0;
  }
  if (const uint16_t fresh = held & static_cast<uint16_t>(~C.prevHeld)) {
    C.blocked++;
    if (C.kind == Cover::Battle) std::fprintf(stderr, "[landing] a pressed button (0x%X) kept out of the game (frame %u, %ld so far)\n", static_cast<unsigned>(fresh), PortVI_GetFlipCount(), C.blocked);
  }
  C.prevHeld = held;
}

void CoverDrive(PADStatus* status, const WinWorkLayout* w) {
  if (C.kind == Cover::None) return;
  C.polls++;
  PADStatus* pad = nullptr;
  for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) {
    if (status[i].err == PAD_ERR_NONE) {
      pad = &status[i];
      break;
    }
  }
  const int menu = w != nullptr ? w->menuID : -1;
  const int32_t floor = GSfloorGetCurrent();
  if (LogOn() && (menu != C.lastMenu || C.polls % 120 == 0)) {
    std::fprintf(stderr, "[landing] poll %ld frame %u: window 0x%X state %d cursor %d, floor 0x%X state %d\n", C.polls, PortVI_GetFlipCount(), menu,
                 w != nullptr ? w->state : -1, w != nullptr ? w->cursor : -1, static_cast<unsigned>(floor), floorGetFloorState());
  }
  C.lastMenu = menu;
  if (C.ending) {
    if (coverAge.age - C.endAge > 0.35f) {
      std::fprintf(stderr, "[landing] cover over at game frame %u: %ld button presses were kept out; what is still held (0x%X) counts once released\n",
                   PortVI_GetFlipCount(), C.blocked, static_cast<unsigned>(C.prevHeld));
      C.kind = Cover::None;
      PortInput_LatchHeld();
    }
    return;
  }
  if (C.kind == Cover::Room) {
    if (PortMenu_Active() || PortMenu_Pending() || C.polls > GiveUpPolls()) {
      std::fprintf(stderr, "[landing] room cover over at game frame %u: %ld button presses kept out\n", PortVI_GetFlipCount(), C.blocked);
      C.kind = Cover::None;
      PortInput_LatchHeld();
    }
    return;
  }
  if (C.kind == Cover::Battle) {
    if (!ol::D.on && coverAge.age >= 1.2f) CoverEnd("the battle has started");
    else if (C.polls > GiveUpPolls()) CoverEnd("gave up after 90 seconds (the room's driver did not finish)");
    return;
  }
  if (C.polls > GiveUpPolls()) {
    CoverEnd("gave up after 90 seconds (the way in looked different)");
    return;
  }
  const bool shownLongEnough = coverAge.age >= 1.2f;
  if (C.kind == Cover::NewGame && (menu == 0x65 || menu == 0x66 || menu == 0x68) && shownLongEnough) {
    CoverEnd("the name entry is up");
    return;
  }
  if (C.kind == Cover::Continue && C.yesDone && floor != C.floorAtYes && floor > 0 && floorGetFloorState() == 5) {
    if (C.settled == 0) C.settled = static_cast<int>(PortVI_GetFlipCount());
    if (PortVI_GetFlipCount() - static_cast<uint32_t>(C.settled) > 30 && shownLongEnough) CoverEnd("the save's map is up");
  } else {
    C.settled = 0;
  }
  if (pad == nullptr) return;
  const uint32_t now = PortVI_GetFlipCount();
  if (static_cast<int32_t>(now - C.holdUntil) < 0) {
    pad->button |= C.hold;
    return;
  }
  if (static_cast<int32_t>(now - C.gapUntil) < 0) return;
  if (menu == 0x35 && w->state == 2 && w->pad_a4 == 0) {
    if (w->cursor != 0) {
      C.hold = PAD_BUTTON_UP, C.holdUntil = now + 3, C.gapUntil = now + 8;
    } else {
      C.hold = PAD_BUTTON_A, C.holdUntil = now + 3, C.gapUntil = now + 40;
      C.yesDone = true;
      C.floorAtYes = floor;
      std::fprintf(stderr, "[landing] YES to the game's question (frame %u)\n", now);
    }
    pad->button |= C.hold;
  }
}

void CoverDraw() {
  if (C.kind == Cover::None || ImGui::GetCurrentContext() == nullptr) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float t = coverAge.Tick();
  static bool labelled = false;
  if (!labelled && C.kind == Cover::NewGame && !xd::pt::RestartLabel().empty()) {
    labelled = true;
    I = InfoFor(true, xd::pt::RestartLabel());
  }
  Draw(dl, t);
  if (C.ending) FillWindow(dl, IM_COL32(0, 0, 0, std::clamp(static_cast<int>(255.f * (t - C.endAge) / 0.3f), 0, 255)));
}

}
