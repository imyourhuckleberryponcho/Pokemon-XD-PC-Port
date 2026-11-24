namespace sch {
struct Entry {
  int cand = -1;
  std::string head;
};
std::vector<Entry> list;
int cur = 0, firstRow = 0, jumpAt = 0, key = 0, yes = 0;
std::string query;
bool typing = false, confirm = false, two = false;
int stage = 0;
uint16_t pick1 = 0;
bool sFirstOpen = true;

constexpr int kRows = 14;
const char* const kKeys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";

std::string Lower(std::string t) {
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t;
}
std::string Upper(std::string t) {
  for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return t;
}

struct Pic {
  ImTextureID id{};
  float w = 0.f, h = 0.f;
  bool tried = false;
};
std::map<uint32_t, Pic> sPics;
Pic LoadPng(const std::filesystem::path& file) {
  Pic p;
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return p;
  SDL_Surface* loaded = SDL_LoadPNG(file.string().c_str());
  SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (rgba && rgba->pitch == rgba->w * 4) {
    p.id = aurora_imgui_add_texture(static_cast<uint32_t>(rgba->w), static_cast<uint32_t>(rgba->h), rgba->pixels);
    p.w = static_cast<float>(rgba->w), p.h = static_cast<float>(rgba->h);
  }
  if (rgba) SDL_DestroySurface(rgba);
  if (loaded) SDL_DestroySurface(loaded);
  return p;
}
const Pic& PicOf(const xd::starter::Candidate& c) {
  Pic& p = sPics[c.index];
  if (p.tried) return p;
  const std::filesystem::path base = std::filesystem::path(xd::UserFolder()) / "icons";
  if (c.national != 0) {
    for (const char* src : {"xd", "br", "usum"}) {
      p = LoadPng(base / "looks" / src / (std::to_string(c.national) + ".png"));
      if (p.id) break;
    }
  }
  if (!p.id) p = LoadPng(base / "pokedex" / ("pokemon_" + std::to_string(c.index) + ".png"));
  p.tried = true;
  return p;
}
void DrawPic(ImDrawList* dl, const Screen& sc, const Pic& p, float cx, float cy, float box) {
  if (!p.id || p.w <= 0.f) return;
  const float k = std::min(box / p.w, box / p.h), pw = p.w * k, ph = p.h * k;
  dl->AddImage(p.id, sc.At(cx - pw / 2.f, cy - ph / 2.f), sc.At(cx + pw / 2.f, cy + ph / 2.f));
}

const xd::starter::Candidate* CandAt(int row) {
  if (row < 0 || row >= static_cast<int>(list.size()) || list[static_cast<size_t>(row)].cand < 0) return nullptr;
  return &xd::starter::Candidates()[static_cast<size_t>(list[static_cast<size_t>(row)].cand)];
}

void Rebuild(int keepIndex = -1) {
  list.clear();
  const auto& all = xd::starter::Candidates();
  const std::string q = Lower(query);
  for (int pass = 0; pass < 2; pass++) {
    bool headed = false;
    for (size_t i = 0; i < all.size(); i++) {
      if (all[i].legendary != (pass == 1)) continue;
      if (!q.empty() && Lower(all[i].name).find(q) == std::string::npos) continue;
      if (pass == 1 && !headed) list.push_back({-1, Tr("LEGENDARY AND MYTHICAL")}), headed = true;
      list.push_back({static_cast<int>(i), {}});
    }
  }
  cur = 0;
  for (int r = 0; r < static_cast<int>(list.size()); r++) {
    if (list[static_cast<size_t>(r)].cand >= 0 && (keepIndex < 0 || all[static_cast<size_t>(list[static_cast<size_t>(r)].cand)].index == keepIndex)) {
      cur = r;
      break;
    }
  }
  firstRow = 0;
}

void Step(int d) {
  const int n = static_cast<int>(list.size());
  if (n == 0) return;
  int r = cur;
  for (int k = 0; k < n; k++) {
    r = ((r + d) % n + n) % n;
    if (list[static_cast<size_t>(r)].cand >= 0) break;
  }
  cur = r;
}
void Move(int d) {
  const int n = static_cast<int>(list.size());
  int r = std::clamp(cur + d, 0, std::max(0, n - 1));
  while (r > 0 && r < n - 1 && list[static_cast<size_t>(r)].cand < 0) r += d > 0 ? 1 : -1;
  if (n > 0 && list[static_cast<size_t>(r)].cand >= 0) cur = r;
}

std::vector<int> JumpRows() {
  std::vector<int> rows;
  std::set<int> gens;
  for (size_t r = 0; r < list.size(); r++) {
    const xd::starter::Candidate* c = CandAt(static_cast<int>(r));
    if (c != nullptr && !c->legendary && gens.insert(c->gen).second) rows.push_back(static_cast<int>(r));
  }
  for (size_t r = 0; r < list.size(); r++) {
    if (list[r].cand < 0) {
      for (size_t k = r + 1; k < list.size(); k++) {
        if (list[k].cand >= 0) { rows.push_back(static_cast<int>(k)); break; }
      }
      break;
    }
  }
  return rows;
}

void Open(void*) {
  stage = 0, pick1 = 0, confirm = false, typing = false, yes = 0, jumpAt = 0, key = 0;
  query.clear();
  sPics.clear();
  xd::starter::InvalidateCandidates();
  Rebuild();
}

void Finish(uint16_t first, uint16_t second) { xd::starter::Answer(first, second); }

bool Frame(void*, const PortMenuInput* in) {
  const auto& all = xd::starter::Candidates();
  if (all.empty()) {
    std::fprintf(stderr, "[starter] no species to choose from: the game's Eevee stays\n");
    return false;
  }
  const bool up = (in->repeat & PAD_BUTTON_UP) != 0, down = (in->repeat & PAD_BUTTON_DOWN) != 0;
  const bool left = (in->repeat & PAD_BUTTON_LEFT) != 0, right = (in->repeat & PAD_BUTTON_RIGHT) != 0;
  const bool a = (in->pressed & PAD_BUTTON_A) != 0, b = (in->pressed & PAD_BUTTON_B) != 0;
  const xd::starter::Candidate* now = CandAt(cur);
  if (confirm) {
    if (up || down || left || right) yes = 1 - yes, PlayMenuSound(kTitleMenu, kSoundMove);
    if (b) yes = 1;
    if (a || b) {
      PlayMenuSound(kTitleMenu, a && yes == 0 ? kSoundChoose : kSoundBack);
      confirm = false;
      if (a && yes == 0 && now != nullptr) {
        if (two && stage == 0) {
          pick1 = now->index, stage = 1;
          query.clear();
          Rebuild(now->index);
        } else {
          Finish(two ? pick1 : now->index, two ? now->index : 0);
          return false;
        }
      }
      yes = 0;
    }
  } else if (typing) {
    const int n = static_cast<int>(std::strlen(kKeys)), total = n + 2, per = 7;
    if (right) key = (key + 1) % total;
    if (left) key = (key + total - 1) % total;
    if (down) key = std::min(total - 1, key + per);
    if (up) key = std::max(0, key - per);
    if (left || right || up || down) PlayMenuSound(kTitleMenu, kSoundMove);
    if (a) {
      if (key < n) { if (query.size() < 10) query += kKeys[key]; }
      else if (key == n) { if (!query.empty()) query.pop_back(); }
      else typing = false;
      Rebuild();
    }
    if (in->pressed & PAD_BUTTON_X) { if (!query.empty()) query.pop_back(); Rebuild(); }
    if (b) typing = false;
  } else {
    if (up) Step(-1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (down) Step(1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (left) Move(-10), PlayMenuSound(kTitleMenu, kSoundMove);
    if (right) Move(10), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->pressed & PAD_TRIGGER_L) Move(-kRows), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->pressed & PAD_TRIGGER_R) Move(kRows), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->pressed & PAD_BUTTON_Y) {
      const std::vector<int> jumps = JumpRows();
      if (!jumps.empty()) cur = jumps[static_cast<size_t>(jumpAt++ % static_cast<int>(jumps.size()))], PlayMenuSound(kTitleMenu, kSoundMove);
    }
    if (in->pressed & PAD_BUTTON_X) typing = true, key = 0, PlayMenuSound(kTitleMenu, kSoundChoose);
    if (a && now != nullptr) confirm = true, yes = 0, PlayMenuSound(kTitleMenu, kSoundChoose);
    if (b && two && stage == 1) {
      stage = 0, pick1 = 0;
      Rebuild();
      PlayMenuSound(kTitleMenu, kSoundBack);
    }
  }
  now = CandAt(cur);

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("CHOOSE YOUR STARTER"), two ? Fmt(Tr("Starter %d of 2"), stage + 1) : std::string());
  const int n = static_cast<int>(list.size());
  kit::Window(dl, sc, 15.f, 75.f, 305.f, 420.f);
  firstRow = std::clamp(firstRow, std::max(0, cur - kRows + 1), cur);
  firstRow = std::clamp(firstRow, 0, std::max(0, n - kRows));
  if (n == 0) kit::Text(dl, sc, 40.f, 100.f, Tr("No Pokemon has that name."), kit::kDim, 0.7f, 240.f);
  for (int k = 0; k < kRows && firstRow + k < n; k++) {
    const int r = firstRow + k;
    const float y = 82.f + 24.f * static_cast<float>(k);
    if (list[static_cast<size_t>(r)].cand < 0) {
      kit::Text(dl, sc, 34.f, y + 5.f, list[static_cast<size_t>(r)].head, kit::kGold, 0.62f, 250.f);
      continue;
    }
    const xd::starter::Candidate& c = all[static_cast<size_t>(list[static_cast<size_t>(r)].cand)];
    const bool on = r == cur;
    if (on) { kit::RowSel(dl, sc, 21.f, 299.f, y, 22.f, true); }
    char num[16];
    std::snprintf(num, sizeof num, c.national != 0 ? "#%03d" : "   ", c.national);
    kit::Text(dl, sc, 38.f, y + 5.f, num, kit::kSoft, 0.55f);
    DrawPic(dl, sc, PicOf(c), 88.f, y + 11.f, 21.f);
    kit::Text(dl, sc, 104.f, y + 4.f, Upper(c.name), on ? kit::kText : kit::kSoft, 0.66f, 150.f, 1);
    kit::Text(dl, sc, 262.f, y + 6.f, Fmt("G%d", c.gen), kit::kDim, 0.5f);
  }
  if (firstRow > 0) kit::Arrow(dl, sc, 290.f, 82.f, true);
  if (firstRow + kRows < n) kit::Arrow(dl, sc, 290.f, 414.f, false);

  kit::Window(dl, sc, 315.f, 75.f, 625.f, 335.f);
  if (now != nullptr) {
    const Pic& pic = PicOf(*now);
    const bool hasPic = pic.id != ImTextureID{};
    DrawPic(dl, sc, pic, 400.f, 150.f, 112.f);
    const float tx = hasPic ? 468.f : 342.f, tw = hasPic ? 132.f : 256.f;
    kit::Text(dl, sc, tx, 90.f, Upper(now->name), kit::kText, 0.9f, tw, 1);
    char num[40];
    if (now->national != 0) std::snprintf(num, sizeof num, "#%03d  -  %s", now->national, std::string(Fmt(Tr("Generation %d"), now->gen)).c_str());
    else std::snprintf(num, sizeof num, "%s", Tr("Added by a mod"));
    kit::Text(dl, sc, tx, 120.f, num, kit::kSoft, 0.6f, tw);
    std::string types = now->typeNames[0];
    if (now->types[1] != now->types[0] && !now->typeNames[1].empty()) types += " / " + now->typeNames[1];
    kit::Text(dl, sc, tx, 144.f, types, kit::kGold, 0.64f, tw);
    if (now->legendary) kit::Text(dl, sc, tx, 168.f, Tr("Legendary"), kit::kShadow, 0.6f, tw);
    const char* const labels[6] = {Tr("HP"), Tr("Attack"), Tr("Defense"), Tr("Sp. Atk"), Tr("Sp. Def"), Tr("Speed")};
    for (int k = 0; k < 6; k++) {
      const float x = k < 3 ? 342.f : 474.f, y = 198.f + 20.f * static_cast<float>(k % 3);
      kit::Text(dl, sc, x, y, labels[k], kit::kSoft, 0.6f, 70.f);
      const std::string v = std::to_string(now->base[k]);
      kit::Text(dl, sc, x + 118.f - kit::Width(v, 0.6f), y, v, kit::kText, 0.6f, 40.f);
    }
    kit::Para(dl, sc, 342.f, 264.f, 256.f,
              Fmt(Tr("%s starts at level %d with the moves it would know by then, like Eevee."), now->name.c_str(), stage == 0 ? 10 : static_cast<int>(xd::starter::kSecondLevel)),
              kit::kSoft, 0.58f, 14.f, 3);
  }
  kit::Window(dl, sc, 315.f, 345.f, 625.f, 420.f);
  kit::Text(dl, sc, 335.f, 358.f, std::string(Tr("Search:")) + " " + (query.empty() ? std::string("----------") : Upper(query)), kit::kText, 0.7f, 270.f);
  kit::Text(dl, sc, 335.f, 384.f, Fmt(Tr("%d Pokemon"), static_cast<int>(std::count_if(list.begin(), list.end(), [](const Entry& e) { return e.cand >= 0; }))), kit::kDim, 0.6f, 270.f);
  kit::HintBar(dl, sc, std::string(Tr("Up / down: Pokemon   Left / right: 10 at a time   Y: next generation   X: search   A: choose")) + (two && stage == 1 ? Tr("   B: back") : std::string()));

  if (typing) {
    kit::Window(dl, sc, 315.f, 75.f, 625.f, 335.f, true);
    kit::Text(dl, sc, 335.f, 88.f, Tr("SEARCH BY NAME"), kit::kGold, 0.75f, 270.f);
    kit::Text(dl, sc, 335.f, 112.f, query.empty() ? std::string("----------") : Upper(query), kit::kText, 0.8f, 270.f);
    const int total = static_cast<int>(std::strlen(kKeys)) + 2, per = 7;
    for (int k = 0; k < total; k++) {
      const float x = 335.f + (k % per) * 39.f, y = 150.f + (k / per) * 36.f;
      if (k == key) kit::RowSel(dl, sc, x - 6.f, x + 33.f, y - 3.f, 28.f, false);
      const std::string t = k < total - 2 ? std::string(1, kKeys[k]) : (k == total - 2 ? Tr("DEL") : Tr("OK"));
      kit::Text(dl, sc, x, y + 2.f, t, k == key ? kit::kText : kit::kGold, 0.75f, 36.f);
    }
    kit::Text(dl, sc, 335.f, 300.f, Tr("A: letter   X: delete   B: close"), kit::kDim, 0.58f, 270.f);
  }
  if (confirm && now != nullptr) {
    kit::MessageWindow(dl, sc);
    const std::string q = two && stage == 0 ? Fmt(Tr("Choose %s as your first starter?"), now->name.c_str())
                          : two             ? Fmt(Tr("Choose %s as your second starter?"), now->name.c_str())
                                            : Fmt(Tr("Choose %s as your starter?"), now->name.c_str());
    kit::Para(dl, sc, 52.f, 307.f, 520.f, q, kit::kText, 1.f, 28.f, 3);
    kit::ChoiceBox(dl, sc, {Tr("Yes"), Tr("No")}, yes, 0);
  }
  return true;
}
const PortMenuPage kPage{"starter choose", Open, Frame, nullptr, nullptr};
}
