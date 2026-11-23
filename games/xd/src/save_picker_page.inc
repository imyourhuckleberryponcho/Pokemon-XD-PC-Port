namespace sp {

struct Thumb {
  ImTextureID id{};
  float w = 0, h = 0;
  bool ok = false;
  std::filesystem::file_time_type stamp{};
};

struct PageData {
  std::vector<xd::picker::Row> rows;
  std::vector<Thumb> thumbs;
  int cursor = 0, first = 0;
  int answer = -1;
  float age = 0.f;
};
PageData D;
std::map<std::string, Thumb> sCache;

Thumb ThumbOf(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return {};
  const auto stamp = std::filesystem::last_write_time(file, ec);
  const std::u8string u8 = file.u8string();
  const std::string key(u8.begin(), u8.end());
  auto it = sCache.find(key);
  if (it != sCache.end() && it->second.stamp == stamp) return it->second;
  Thumb t;
  const xd::picker::Picture p = xd::picker::LoadThumbnail(file);
  if (p.width > 0 && p.height > 0) {
    t.id = aurora_imgui_add_texture(p.width, p.height, p.rgba.data());
    t.w = static_cast<float>(p.width), t.h = static_cast<float>(p.height), t.ok = true, t.stamp = stamp;
  }
  sCache[key] = t;
  return t;
}

bool Open() {
  D = PageData{};
  D.rows = xd::picker::Rows();
  if (D.rows.size() < 2) return false;
  for (size_t i = 0; i < D.rows.size(); i++) {
    D.thumbs.push_back(ThumbOf(D.rows[i].thumbnail));
    if (D.rows[i].active) D.cursor = static_cast<int>(i);
  }
  std::fprintf(stderr, "[save picker] %zu saves offered\n", D.rows.size());
  return true;
}

void Close(void*) {
  if (D.answer >= 0 && D.answer < static_cast<int>(D.rows.size())) {
    ls::CoverContinue("the save picker's choice");
    xd::pt::PickerAnswer(&D.rows[static_cast<size_t>(D.answer)]);
  } else {
    xd::pt::PickerAnswer(nullptr);
  }
}

float CheatsBadge(ImDrawList* dl, const Screen& sc, float x1, float y) {
  const std::string t = Tr("Cheats used");
  const float s = 0.62f, w = kit::Width(t, s) + 18.f, h = 20.f, x = x1 - w;
  dl->AddRectFilled(sc.At(x, y), sc.At(x1, y + h), IM_COL32(0xF2, 0xC1, 0x4E, 50), h / 2.f * sc.s);
  dl->AddRect(sc.At(x, y), sc.At(x1, y + h), IM_COL32(0xF2, 0xC1, 0x4E, 150), h / 2.f * sc.s, 0, 1.f * sc.s);
  kit::Text(dl, sc, x + 9.f, y + 2.f, t, kit::kBad, s);
  return w;
}

constexpr int kShown = 3;
constexpr float kRowH = 84.f;

bool Frame(void*, const PortMenuInput* in) {
  const int n = static_cast<int>(D.rows.size());
  if (in->repeat & PAD_BUTTON_UP) D.cursor = (D.cursor + n - 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_DOWN) D.cursor = (D.cursor + 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    D.answer = -1;
    return false;
  }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    D.answer = D.cursor;
    return false;
  }
  D.first = std::clamp(D.first, std::max(0, D.cursor - kShown + 1), D.cursor);
  D.first = std::clamp(D.first, 0, std::max(0, n - kShown));
  D.age += 1.f / 60.f;

  const Screen sc = ScreenNow();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("CONTINUE: choose a save"));
  kit::Window(dl, sc, 40.f, 72.f, 600.f, 72.f + kRowH * kShown + 12.f);
  for (int r = 0; r < kShown && D.first + r < n; r++) {
    const int i = D.first + r;
    const xd::picker::Row& row = D.rows[static_cast<size_t>(i)];
    const float y = 78.f + kRowH * static_cast<float>(r);
    const bool sel = i == D.cursor;
    if (sel) kit::RowSel(dl, sc, 46.f, 594.f, y, kRowH - 2.f, false);
    const Thumb& t = D.thumbs[static_cast<size_t>(i)];
    kit::PictureBox(dl, sc, 62.f, y + 5.f, 96.f, 72.f, t.ok ? t.id : ImTextureID{}, t.w, t.h, Tr("no picture yet"), sel);
    if (sel) kit::Cursor(dl, sc, 182.f, y + 22.f);
    kit::Text(dl, sc, 198.f, y + 11.f, row.trainer, sel ? kit::kText : kit::kSoft, 0.95f, 230.f, 1);
    char played[96];
    std::snprintf(played, sizeof played, Tr("%s played"), row.playTime.c_str());
    kit::Text(dl, sc, 198.f, y + 40.f, row.name + (row.active ? std::string("  ") + Tr("(loaded now)") : std::string()), kit::kGold, 0.75f, 380.f);
    kit::Text(dl, sc, 198.f, y + 60.f, played, kit::kSoft, 0.72f, 230.f);
    const std::string engine = row.rules.substr(0, row.rules.find(','));
    const float cw = kit::Width(engine, 0.62f) + 18.f;
    kit::Chip(dl, sc, 584.f - cw, y + 12.f, engine, true);
    if (!row.cheats.empty()) CheatsBadge(dl, sc, 584.f - cw - 6.f, y + 12.f);
  }
  if (D.first > 0) kit::Arrow(dl, sc, 320.f, 70.f, true);
  if (D.first + kShown < n) kit::Arrow(dl, sc, 320.f, 72.f + kRowH * kShown + 12.f, false);
  const float ry = 72.f + kRowH * kShown + 18.f;
  kit::TitledWindow(dl, sc, 40.f, ry, 600.f, 438.f, Tr("Rules of this save"));
  const xd::picker::Row& chosen = D.rows[static_cast<size_t>(D.cursor)];
  const int ruleLines = kit::Para(dl, sc, 66.f, ry + 40.f, 508.f, chosen.rules, kit::kText, 0.68f, 16.f, chosen.cheats.empty() ? 3 : 2);
  if (!chosen.cheats.empty()) {
    std::string list;
    for (size_t i = 0; i < chosen.cheats.size(); i++) list += (i > 0 ? ", " : "") + chosen.cheats[i];
    kit::Para(dl, sc, 66.f, ry + 40.f + 16.f * static_cast<float>(ruleLines), 508.f, Fmt(Tr("Cheats used: %s"), list.c_str()), kit::kBad, 0.68f, 16.f,
              3 - ruleLines);
  }
  kit::HintBar(dl, sc, Tr("A: load this save   B: back to the title menu"));
  kit::FadeIn(dl, D.age);
  return true;
}

void OpenPage(void*) { }

const PortMenuPage kPage{"savepicker", OpenPage, Frame, Close, nullptr};

}
