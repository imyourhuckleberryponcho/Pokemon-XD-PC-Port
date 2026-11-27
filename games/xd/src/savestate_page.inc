namespace ss {

struct Slot {
  xd::savestate_ui::SlotInfo info;
  sp::Thumb thumb;
};
struct State {
  Slot slots[xd::savestate_ui::kSlots];
  int row = 0, col = 0;
  int ask = 0;
  int askCursor = 1;
  int result = 0;
  float age = 0.f;
  ImVec2 lastMouse{-1.f, -1.f};
};
State S;
int sLastRow = 0;

constexpr float kTop = 74.f, kPitch = 84.f, kRowH = 82.f;
constexpr float kSaveX = 410.f, kSaveW = 90.f, kLoadX = 506.f, kLoadW = 80.f, kBtnDy = 25.f;

void Open(void*) {
  const int row = std::clamp(sLastRow, 0, xd::savestate_ui::kSlots - 1);
  S = State{};
  S.row = row;
  for (int i = 0; i < xd::savestate_ui::kSlots; i++) {
    S.slots[i].info = xd::savestate_ui::Info(i + 1);
    if (S.slots[i].info.used) S.slots[i].thumb = sp::ThumbOf(S.slots[i].info.picture);
  }
  if (S.slots[S.row].info.used) S.col = 1;
}

void Close(void*) {
  sLastRow = S.row;
  if (S.result != 0) xd::savestate_ui::Request(S.result);
}

struct Mouse {
  bool on = false;
  float x = 0, y = 0;
  bool moved = false, click = false, right = false;
};
Mouse ReadMouse(const Screen& sc) {
  Mouse m;
  if (ImGui::GetCurrentContext() == nullptr) return m;
  const ImGuiIO& io = ImGui::GetIO();
  if (io.MousePos.x < -1e30f) return m;
  m.on = true;
  m.x = (io.MousePos.x - sc.x) / sc.s, m.y = (io.MousePos.y - sc.y) / sc.s;
  m.moved = io.MousePos.x != S.lastMouse.x || io.MousePos.y != S.lastMouse.y;
  S.lastMouse = io.MousePos;
  m.click = io.MouseClicked[0], m.right = io.MouseClicked[1];
  return m;
}
bool At(const Mouse& m, int& row, int& col) {
  row = col = -1;
  if (!m.on || m.x < 46.f || m.x > 594.f) return false;
  const int r = static_cast<int>(std::floor((m.y - kTop) / kPitch));
  if (r < 0 || r >= xd::savestate_ui::kSlots || m.y < kTop) return false;
  row = r;
  const float by = kTop + kPitch * static_cast<float>(r) + kBtnDy;
  if (m.y >= by && m.y <= by + 32.f) {
    if (m.x >= kSaveX && m.x <= kSaveX + kSaveW) col = 0;
    else if (m.x >= kLoadX && m.x <= kLoadX + kLoadW) col = 1;
  }
  return true;
}
bool AskAt(const Mouse& m, int& which) {
  which = -1;
  if (!m.on || m.y < 390.f || m.y > 424.f) return false;
  if (m.x >= 190.f && m.x <= 310.f) which = 0;
  else if (m.x >= 330.f && m.x <= 450.f) which = 1;
  return which >= 0;
}

std::string Question() {
  return S.ask == 1 ? Fmt(Tr("Save over state %d?"), S.row + 1) : Fmt(Tr("Load state %d?"), S.row + 1);
}

bool sAudit = false;
void Draw(ImDrawList* dl, const Screen& sc) {
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("SAVE STATES"));
  kit::Window(dl, sc, 40.f, kTop - 6.f, 600.f, kTop + kPitch * static_cast<float>(xd::savestate_ui::kSlots) + 6.f);
  for (int i = 0; i < xd::savestate_ui::kSlots; i++) {
    const Slot& slot = S.slots[i];
    const float y = kTop + kPitch * static_cast<float>(i);
    if (sAudit && S.ask != 0 && y > 280.f) continue;
    const bool sel = i == S.row && S.ask == 0;
    if (i == S.row) kit::RowSel(dl, sc, 46.f, 594.f, y, kRowH, false);
    kit::PictureBox(dl, sc, 62.f, y + 5.f, 96.f, 72.f, slot.thumb.ok ? slot.thumb.id : ImTextureID{}, slot.thumb.w, slot.thumb.h,
                    slot.info.used ? Tr("no picture yet") : "", false);
    kit::Text(dl, sc, 178.f, y + 8.f, Fmt(Tr("State %d"), i + 1), sel ? kit::kText : kit::kSoft, 0.95f, 224.f, 1);
    if (slot.info.used) {
      kit::Text(dl, sc, 178.f, y + 34.f, slot.info.place.empty() ? std::string("-") : slot.info.place, kit::kGold, 0.75f, 224.f);
      kit::Text(dl, sc, 178.f, y + 56.f, slot.info.when, kit::kSoft, 0.72f, 224.f);
    } else {
      kit::Text(dl, sc, 178.f, y + 40.f, Tr("Empty"), kit::kDim, 0.8f, 224.f);
    }
    kit::Button(dl, sc, kSaveX, y + kBtnDy, kSaveW, Tr("Save here"), sel && S.col == 0);
    kit::Button(dl, sc, kLoadX, y + kBtnDy, kLoadW, Tr("Load"), sel && S.col == 1, slot.info.used);
  }
  kit::HintBar(dl, sc, Tr("A: choose   B: back   Up / Down: state   Left / Right: Save here or Load"));
  if (S.ask != 0) {
    kit::Dim(dl);
    kit::MessageWindow(dl, sc);
    kit::Para(dl, sc, 56.f, 316.f, 528.f, Question(), kit::kText, 0.9f, 24.f, 1);
    if (S.ask == 2) kit::Para(dl, sc, 56.f, 346.f, 528.f, Tr("Anything you did after saving it will be lost."), kit::kSoft, 0.8f, 22.f, 2);
    kit::Button(dl, sc, 190.f, 390.f, 120.f, S.ask == 1 ? Tr("Save") : Tr("Load"), S.askCursor == 0);
    kit::Button(dl, sc, 330.f, 390.f, 120.f, Tr("Cancel"), S.askCursor == 1);
  }
  kit::FadeIn(dl, S.age);
}

bool Frame(void*, const PortMenuInput* in) {
  const Screen sc = ScreenNow();
  const Mouse m = ReadMouse(sc);
  int sound = 0;
  struct Sound {
    int& event;
    ~Sound() {
      if (event != 0) PlayMenuSound(0x57, event);
    }
  } play{sound};
  S.age += 1.f / 60.f;
  if (PortSpeedups_SaveStatesPresses() > 0) {
    sound = kSoundBack;
    return false;
  }
  int hoverRow = -1, hoverCol = -1;
  At(m, hoverRow, hoverCol);
  if (S.ask != 0) {
    int which = -1;
    AskAt(m, which);
    if (in->repeat & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) S.askCursor = 1 - S.askCursor, sound = kSoundMove;
    if (which >= 0 && m.moved && which != S.askCursor) S.askCursor = which, sound = kSoundMove;
    if ((in->pressed & PAD_BUTTON_B) || m.right || (m.click && which == 1)) {
      S.ask = 0;
      sound = kSoundBack;
    } else if ((in->pressed & PAD_BUTTON_A) || (m.click && which == 0)) {
      if (S.askCursor == 0) {
        S.result = (S.ask == 1 ? 1 : -1) * (S.row + 1);
        sound = kSoundChoose;
        return false;
      }
      S.ask = 0;
      sound = kSoundBack;
    }
    Draw(ImGui::GetForegroundDrawList(), sc);
    return true;
  }
  if ((in->pressed & PAD_BUTTON_B) || m.right || (in->pressed & PAD_BUTTON_START)) {
    sound = kSoundBack;
    return false;
  }
  const int n = xd::savestate_ui::kSlots;
  int row = S.row, col = S.col;
  if (in->repeat & PAD_BUTTON_UP) row = (row + n - 1) % n;
  if (in->repeat & PAD_BUTTON_DOWN) row = (row + 1) % n;
  if (in->repeat & PAD_BUTTON_LEFT) col = 0;
  if (in->repeat & PAD_BUTTON_RIGHT) col = 1;
  if (m.moved && hoverRow >= 0) {
    row = hoverRow;
    if (hoverCol >= 0) col = hoverCol;
  }
  if (row != S.row || col != S.col) {
    S.row = row, S.col = col;
    sound = kSoundMove;
  }
  const bool click = m.click && hoverRow >= 0 && hoverCol >= 0;
  if (click) S.row = hoverRow, S.col = hoverCol;
  if ((in->pressed & PAD_BUTTON_A) || click) {
    const bool used = S.slots[S.row].info.used;
    if (S.col == 0) {
      if (used) S.ask = 1, S.askCursor = 1, sound = kSoundChoose;
      else S.result = S.row + 1, sound = kSoundChoose;
      if (S.result != 0) return false;
    } else if (used) {
      S.ask = 2, S.askCursor = 1, sound = kSoundChoose;
    } else {
      sound = kSoundBack;
    }
  }
  Draw(ImGui::GetForegroundDrawList(), sc);
  return true;
}

void Audit(const std::function<void(const std::string&, const std::function<void()>&)>& run, ImDrawList* dl, const Screen& sc) {
  (void)dl;
  State saved = S;
  S = State{};
  sAudit = true;
  for (int i = 0; i < xd::savestate_ui::kSlots; i++) {
    S.slots[i].info.used = i != 3;
    S.slots[i].info.place = "POK\xC3\xA9MON HQ LAB - GREAT HALL OF THE ORRE PHENAC CITY";
    S.slots[i].info.when = "2026-11-27  23:59";
  }
  for (int ask = 0; ask < 3; ask++) {
    for (int col = 0; col < 2; col++) {
      S.ask = ask, S.col = col, S.row = ask;
      run(std::string("save states page ask") + std::to_string(ask) + " col" + std::to_string(col), [&] { Draw(ImGui::GetForegroundDrawList(), sc); });
    }
  }
  run("save states entry", [&] {
    const uint8_t white[4] = {0xF0, 0xF0, 0xF0, 0xFF};
    DrawTextCentred(ImGui::GetForegroundDrawList(), sc, 20.f, 100.f, 104.f, 24.f, EntryNameOf(2), white);
  });
  sAudit = false;
  S = saved;
}

const PortMenuPage kPage{"savestates", Open, Frame, Close, nullptr};

}
