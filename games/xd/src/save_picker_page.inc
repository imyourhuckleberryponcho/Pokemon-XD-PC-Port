namespace ot {

const uint8_t kWhite[4] = {0xF0, 0xF0, 0xF0, 0xFF};
const uint8_t kGrey[4] = {0x88, 0x88, 0x88, 0xFF};
const uint8_t kGold[4] = {0xFF, 0xE0, 0x70, 0xFF};
const uint8_t kGreen[4] = {0x90, 0xE8, 0x90, 0xFF};

void Say(ImDrawList* dl, const Screen& sc, float x, float y, const std::string& t, const uint8_t* col, float scale = 0.8f,
         float maxW = 0.f) {
  float squeeze = 1.f;
  if (maxW > 0.f) {
    const float tw = static_cast<float>(TextWidth(t.c_str())) * scale;
    if (tw > maxW) squeeze = maxW / tw;
  }
  DrawText(dl, sc, x, y, t.c_str(), col, squeeze, scale);
}

void Panel(ImDrawList* dl, const Screen& sc, float x0, float y0, float x1, float y1, bool bright = false) {
  dl->AddRectFilled(sc.At(x0, y0), sc.At(x1, y1), IM_COL32(18, 22, 40, 235), 8.f * sc.s);
  dl->AddRect(sc.At(x0, y0), sc.At(x1, y1), bright ? IM_COL32(255, 224, 112, 255) : IM_COL32(120, 140, 200, 255), 8.f * sc.s, 0,
              1.5f * sc.s);
}

void Wrapped(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& text, const uint8_t* col,
             float scale, float pitch, int maxLines) {
  const std::vector<std::string> lines = Wrap(text, w / scale);
  for (int i = 0; i < static_cast<int>(lines.size()) && i < maxLines; i++)
    Say(dl, sc, x, y + pitch * static_cast<float>(i), lines[static_cast<size_t>(i)], col, scale);
}

}

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
    xd::pt::PickerAnswer(&D.rows[static_cast<size_t>(D.answer)]);
  } else {
    xd::pt::PickerAnswer(nullptr);
  }
}

constexpr int kShown = 3;
constexpr float kRowH = 90.f;

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
  FillWindow(dl, IM_COL32(8, 10, 28, 245));
  ot::Panel(dl, sc, 40.f, 20.f, 600.f, 66.f);
  ot::Say(dl, sc, 62.f, 30.f, "CONTINUE: choose a save", ot::kGold, 1.f);
  ot::Panel(dl, sc, 40.f, 74.f, 600.f, 74.f + kRowH * kShown + 8.f);
  for (int r = 0; r < kShown && D.first + r < n; r++) {
    const int i = D.first + r;
    const xd::picker::Row& row = D.rows[static_cast<size_t>(i)];
    const float y = 80.f + kRowH * static_cast<float>(r);
    const bool sel = i == D.cursor;
    if (sel) dl->AddRectFilled(sc.At(48.f, y), sc.At(592.f, y + kRowH - 4.f), IM_COL32(70, 80, 140, 255), 4.f * sc.s);
    const float bx = 56.f, by = y + 4.f, bw = 104.f, bh = 78.f;
    dl->AddRectFilled(sc.At(bx, by), sc.At(bx + bw, by + bh), IM_COL32(0, 0, 0, 255));
    const Thumb& t = D.thumbs[static_cast<size_t>(i)];
    if (t.ok) {
      const float k = std::min(bw / t.w, bh / t.h), w = t.w * k, h = t.h * k;
      dl->AddImage(t.id, sc.At(bx + (bw - w) / 2.f, by + (bh - h) / 2.f), sc.At(bx + (bw + w) / 2.f, by + (bh + h) / 2.f));
    } else {
      ot::Say(dl, sc, bx + 8.f, by + 28.f, "no picture yet", ot::kGrey, 0.6f, bw - 12.f);
    }
    dl->AddRect(sc.At(bx, by), sc.At(bx + bw, by + bh), IM_COL32(120, 140, 200, 255));
    ot::Say(dl, sc, 176.f, y + 8.f, row.trainer, sel ? ot::kGold : ot::kWhite, 0.95f, 300.f);
    ot::Say(dl, sc, 176.f, y + 36.f, row.name + (row.active ? "  (loaded now)" : ""), ot::kGrey, 0.75f, 300.f);
    ot::Say(dl, sc, 176.f, y + 56.f, row.playTime + " played", ot::kWhite, 0.75f, 300.f);
    ot::Say(dl, sc, 440.f, y + 8.f, row.rules.substr(0, row.rules.find(',')), ot::kGreen, 0.7f, 140.f);
  }
  if (D.first > 0) DrawTriangle(dl, sc, 320.f, 69.f, 12.f, 0, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  if (D.first + kShown < n) DrawTriangle(dl, sc, 320.f, 74.f + kRowH * kShown + 8.f, 12.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
  const float ry = 74.f + kRowH * kShown + 18.f;
  ot::Panel(dl, sc, 40.f, ry, 600.f, 436.f);
  ot::Say(dl, sc, 56.f, ry + 6.f, "Rules of this save", ot::kGold, 0.8f);
  ot::Wrapped(dl, sc, 56.f, ry + 28.f, 530.f, D.rows[static_cast<size_t>(D.cursor)].rules, ot::kWhite, 0.7f, 17.f, 4);
  ot::Say(dl, sc, 48.f, 446.f, "A: load this save   B: back to the title menu", ot::kGrey, 0.75f);
  if (D.age < 0.2f) FillWindow(dl, IM_COL32(0, 0, 0, std::clamp(static_cast<int>(255.f * (1.f - D.age / 0.2f)), 0, 255)));
  return true;
}

void OpenPage(void*) { }

const PortMenuPage kPage{"savepicker", OpenPage, Frame, Close, nullptr};

}
