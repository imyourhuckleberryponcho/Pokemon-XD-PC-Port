namespace cm {

using bag::kArrows;
const uint8_t kWhite[4] = {0xF0, 0xF0, 0xF0, 0xFF};
const uint8_t kGrey[4] = {0x80, 0x80, 0x84, 0xFF};

enum class Kind { Toggle, Choice, Picker, Candy, Purify };
struct CRow {
  const char* setting;
  const char* (*label)();
  Kind kind;
  int group;
};
constexpr int kGroups = 4;
const char* GroupName(int g) {
  switch (g) {
  case 0: return Tr("CATCHING");
  case 1: return Tr("EXP");
  case 2: return Tr("ITEMS");
  default: return Tr("BATTLE");
  }
}
const CRow kCheatRows[] = {
    {"cheats.catch_always", [] { return Tr("Every ball catches"); }, Kind::Toggle, 0},
    {"cheats.catch_trainers", [] { return Tr("Snag any POK\xC3\xA9MON"); }, Kind::Toggle, 0},
    {"cheats.balls_infinite", [] { return Tr("Balls never run out"); }, Kind::Toggle, 0},
    {"cheats.exp", [] { return Tr("EXP multiplier"); }, Kind::Choice, 1},
    {"cheats.level_cap_off", [] { return Tr("Ignore level cap"); }, Kind::Toggle, 1},
    {"cheats.money_infinite", [] { return Tr("Endless money"); }, Kind::Toggle, 2},
    {"cheats.coupons_infinite", [] { return Tr("Endless COUPONS"); }, Kind::Toggle, 2},
    {nullptr, [] { return Tr("Get any item"); }, Kind::Picker, 2},
    {nullptr, nullptr, Kind::Candy, 2},
    {"cheats.shiny", [] { return Tr("Shiny chance"); }, Kind::Choice, 3},
    {"cheats.perfect_ivs", [] { return Tr("Perfect IVs"); }, Kind::Toggle, 3},
    {"cheats.max_evs", [] { return Tr("Perfect EVs"); }, Kind::Toggle, 3},
    {"cheats.no_damage", [] { return Tr("No damage taken"); }, Kind::Toggle, 3},
    {"cheats.win_battles", [] { return Tr("Win every battle"); }, Kind::Toggle, 3},
    {nullptr, [] { return Tr("Purify at once"); }, Kind::Purify, 3},
};
constexpr int kRowCount = static_cast<int>(sizeof kCheatRows / sizeof kCheatRows[0]);

std::map<std::string, int> sLocal;
bool HaveSetting(const char* id) { return id != nullptr && es::Global().Find(id) != es::kNoSetting; }
int ChoiceCount(const CRow& r) {
  if (r.kind == Kind::Toggle) return 2;
  if (HaveSetting(r.setting)) return std::max(1, static_cast<int>(es::Global().Get(es::Global().Find(r.setting)).choices.size()));
  return std::strcmp(r.setting, "cheats.exp") == 0 ? 4 : 5;
}
int ValueOf(const CRow& r) {
  if (r.setting == nullptr) return 0;
  if (HaveSetting(r.setting)) return es::Global().ValueOf(es::Global().Find(r.setting)).i;
  return sLocal[r.setting];
}
bool sChanged = false;
void SetValue(const CRow& r, int v) {
  if (r.setting == nullptr) return;
  if (HaveSetting(r.setting)) {
    es::Registry& reg = es::Global();
    es::Value val = reg.ValueOf(reg.Find(r.setting));
    val.i = v;
    if (reg.Set(reg.Find(r.setting), val)) {
      sChanged = true;
      std::fprintf(stderr, "[cheat menu] %s = %s\n", r.setting, reg.ValueText(reg.Find(r.setting)).c_str());
    }
    return;
  }
  static std::map<std::string, bool> said;
  if (!said[r.setting]) {
    said[r.setting] = true;
    std::fprintf(stderr, "[cheat menu] %s is not a setting of this build yet: the row keeps its value and changes nothing\n", r.setting);
  }
  sLocal[r.setting] = v;
}
std::string ChoiceId(const CRow& r, int v) {
  if (HaveSetting(r.setting)) {
    const es::Definition& d = es::Global().Get(es::Global().Find(r.setting));
    if (v >= 0 && v < static_cast<int>(d.choices.size())) return d.choices[static_cast<size_t>(v)].id;
  }
  static const char* const kExp[] = {"off", "x2", "x5", "x10"};
  static const char* const kShiny[] = {"off", "1_in_512", "1_in_64", "1_in_8", "always"};
  if (std::strcmp(r.setting, "cheats.exp") == 0) return v >= 0 && v < 4 ? kExp[v] : "off";
  return v >= 0 && v < 5 ? kShiny[v] : "off";
}
int ExpFactorOf(const std::string& id) { return id.size() > 1 && id[0] == 'x' ? std::atoi(id.c_str() + 1) : 1; }
int ShinyOf(const std::string& id) { return id == "always" ? 1 : id.rfind("1_in_", 0) == 0 ? std::atoi(id.c_str() + 5) : 0; }
std::string Thousands(long n) {
  std::string d = std::to_string(n), out;
  for (size_t i = 0; i < d.size(); i++) {
    if (i > 0 && (d.size() - i) % 3 == 0) out += ',';
    out += d[i];
  }
  return out;
}
std::string ChoiceText(const CRow& r, int v) {
  const std::string id = ChoiceId(r, v);
  if (std::strcmp(r.setting, "cheats.exp") == 0) return Fmt(Tr("\xC3\x97%d"), ExpFactorOf(id));
  const int d = ShinyOf(id);
  return d == 0 ? std::string(Tr("Normal")) : d == 1 ? std::string(Tr("Always")) : Fmt(Tr("1 in %d"), d);
}

bool LevelCapRuleOn() { return xd::challenges::Active("level_cap"); }
bool RowMissing(const CRow& r) { return r.setting != nullptr && !HaveSetting(r.setting); }
bool RowGrey(const CRow& r) {
  return RowMissing(r) || (r.setting != nullptr && std::strcmp(r.setting, "cheats.level_cap_off") == 0 && !LevelCapRuleOn());
}

struct ItemInfo {
  uint16_t id;
  std::string name;
  uint32_t doc;
};
std::string GameText(uint32_t msg) {
  std::string out;
  const uint16_t* t = msg != 0 ? reinterpret_cast<const uint16_t*>(GSmsgGetGSchar(msg)) : nullptr;
  for (int n = 0; t != nullptr && n < 400; n++) {
    const uint16_t c = t[n];
    if (c == 0 || c == 0xFFFF) break;
    if (c >= 0xFF00) {
      if (c == 0xFFFE && !out.empty() && out.back() != ' ') out += ' ';
      continue;
    }
    if (c < 0x20) continue;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) out += static_cast<char>(0xC0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3F));
    else out += static_cast<char>(0xE0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}
constexpr int kPockets = 5;
constexpr uint8_t kPocketKind[kPockets] = {2, 1, 4, 3, 6};
const char* PocketName(int p) {
  switch (p) {
  case 0: return Tr("ITEMS");
  case 1: return Tr("BALLS");
  case 2: return Tr("TMs");
  case 3: return Tr("BERRIES");
  default: return Tr("COLOGNES");
  }
}
std::vector<ItemInfo> sPocket[kPockets];
uint16_t sCandy = 0;
std::string sCandyName;
bool sItemsRead = false;
void ReadItems() {
  if (sItemsRead || PortXD_LiveCommonTables() == nullptr) return;
  sItemsRead = true;
  for (auto& p : sPocket) p.clear();
  for (uint16_t id = 1; id < 0x400; id++) {
    void* p = itemDataBiosGetPtr(id);
    if (p == nullptr) continue;
    const uint32_t name = itemDataBiosGetName(p);
    if (name == 0) continue;
    const uint8_t kind = itemDataBiosGetKind(p);
    for (int k = 0; k < kPockets; k++) {
      if (kPocketKind[k] != kind) continue;
      ItemInfo it{id, GameText(name), itemDataBiosGetDoc(p)};
      if (!it.name.empty() && it.name != "?" && it.name != "-") sPocket[k].push_back(it);
    }
    if (sCandy == 0) {
      const uint32_t effect = itemDataBiosGetItemEffectParam(p);
      uint8_t* param = static_cast<uint8_t>(effect) != 0 ? itemParamGetPtr(static_cast<u8>(effect)) : nullptr;
      if (param != nullptr && itemParamGetLevelUpFlag(param) != 0) sCandy = id, sCandyName = GameText(name);
    }
  }
  std::fprintf(stderr, "[cheat menu] items: %zu / %zu / %zu / %zu / %zu in the pockets; the level-up item is %u (%s)\n", sPocket[0].size(),
               sPocket[1].size(), sPocket[2].size(), sPocket[3].size(), sPocket[4].size(), sCandy, sCandyName.c_str());
}
int InBag(uint16_t id) {
  void* p = itemDataBiosGetPtr(id);
  if (p == nullptr) return 0;
  uint16_t count = 0;
  const auto* slots = static_cast<const BagSlot*>(heroItemGetItemKindToItemAryPtr(nullptr, itemDataBiosGetKind(p), &count, nullptr, nullptr, nullptr));
  int n = 0;
  for (uint16_t i = 0; slots != nullptr && i < count; i++) {
    if (slots[i].itemDataId == id) n += slots[i].num;
  }
  return n;
}
long Money() { return static_cast<long>(static_cast<uint32_t>(heroGetStatus(nullptr, 0xC, 0))); }
std::map<uint16_t, int> sPending;

std::string CandyLabel() {
  ReadItems();
  return sCandyName.empty() ? std::string(Tr("RARE CANDY")) : sCandyName;
}
const char* RowLabel(const CRow& r) {
  static std::string candy;
  if (r.kind == Kind::Candy) return (candy = CandyLabel()).c_str();
  return r.label();
}

struct State {
  int group = 0;
  int cursor[kGroups] = {0, 0, 0, 0};
  int candy = 10;
  bool picker = false;
  int pocket = 1;
  int item[kPockets] = {0, 0, 0, 0, 0}, first[kPockets] = {0, 0, 0, 0, 0};
  bool howMany = false;
  int amount = 1;
  std::string note;
  float noteAge = 0.f;
  float descAge = 0.f;
  kit::Age age;
  bool closeByKey = false;
  ImVec2 lastMouse{-1.f, -1.f};
} S;
constexpr int kCandySteps[] = {1, 5, 10, 25, 50, 99};

std::vector<int> RowsOf(int g) {
  std::vector<int> v;
  for (int i = 0; i < kRowCount; i++) {
    if (kCheatRows[i].group == g) v.push_back(i);
  }
  return v;
}

std::string ValueTextOf(const CRow& r) {
  switch (r.kind) {
  case Kind::Toggle: return ValueOf(r) != 0 ? Tr("On") : Tr("Off");
  case Kind::Choice: return ChoiceText(r, ValueOf(r));
  case Kind::Candy: return Fmt(Tr("\xC3\x97%d"), S.candy);
  default: return "";
  }
}

std::string DescriptionOf(const CRow& r) {
  if (r.kind == Kind::Picker) return Tr("Pick any item and how many: they go into your BAG.");
  if (r.kind == Kind::Purify) return Tr("A opens the heart of every Shadow POKéMON in your party: purify them at the next chance.");
  if (RowMissing(r)) return Tr("Not in this version yet: the game has no safe switch for this.");
  if (r.kind == Kind::Candy) return Fmt(Tr("Left / right: how many. A puts them into your BAG (%s)."), CandyLabel().c_str());
  const std::string id = r.setting;
  if (id == "cheats.catch_always") return Tr("Every BALL you throw catches on the first try.");
  if (id == "cheats.catch_trainers") return Tr("Throw BALLS at other trainers' POK\xC3\xA9MON too. Shadow POK\xC3\xA9MON are snagged as usual.");
  if (id == "cheats.balls_infinite") return Tr("A BALL you throw stays in your BAG.");
  if (id == "cheats.exp") {
    const int f = ExpFactorOf(ChoiceId(r, ValueOf(r)));
    return f <= 1 ? std::string(Tr("Your POK\xC3\xA9MON get the usual EXP. Points.")) : Fmt(Tr("Your POK\xC3\xA9MON get %d times the EXP. Points."), f);
  }
  if (id == "cheats.level_cap_off") {
    return LevelCapRuleOn() ? Tr("The level cap challenge no longer holds your POK\xC3\xA9MON back.")
                            : Tr("This playthrough has no level cap challenge.");
  }
  if (id == "cheats.money_infinite") return Tr("Your money never runs out: buying takes none of it.");
  if (id == "cheats.coupons_infinite") return Tr("Your POK\xC3\xA9 COUPONS never run out.");
  if (id == "cheats.shiny") return Tr("How often a POK\xC3\xA9MON is shiny. Normal: 1 in 8,192.");
  if (id == "cheats.perfect_ivs") return Tr("POK\xC3\xA9MON you get from now on have perfect IVs.");
  if (id == "cheats.max_evs") return Tr("POK\xC3\xA9MON you get from now on have the most EVs the game allows.");
  if (id == "cheats.no_damage") return Tr("Your POK\xC3\xA9MON take no damage in battle.");
  if (id == "cheats.win_battles") return Tr("Your first hit makes the foe's POK\xC3\xA9MON faint. Shadow POK\xC3\xA9MON keep 1 HP, so you can still snag them.");
  if (id == "cheats.purify_instant") return Tr("A Shadow POK\xC3\xA9MON's heart opens at once: purify it at the next chance.");
  if (id == "cheats.no_spot_pokemon") return Tr("No POK\xC3\xA9MON come to the POK\xC3\xA9 SPOTS.");
  if (id == "cheats.walk_through_walls") return Tr("Walk through walls and everything else in your way.");
  return "";
}

struct MartPiece {
  s16 sprite;
  float x, y, w, h;
};
bool DrawMenuSprite(ImDrawList* dl, const Screen& sc, s16 spriteId, float x, float y, float w, float h, float dy = 0.f) {
  const auto* ms = static_cast<const MenuSpriteView*>(menuSpriteBiosGetPtr(static_cast<u32>(spriteId)));
  if (ms == nullptr || ms->type != 1) return false;
  const auto* val = static_cast<const MenuSpriteValView*>(menuSpriteBiosGetValPtr(const_cast<MenuSpriteView*>(ms)));
  if (val == nullptr) return false;
  Shot shot;
  SpriteWorkLayout& sp = shot.sp;
  sp.x = static_cast<s16>(x), sp.y = static_cast<s16>(y + dy), sp.w = static_cast<s16>(w), sp.h = static_cast<s16>(h);
  sp.tex2 = val->tex;
  sp.u = val->u, sp.v = val->v, sp.uw = val->w, sp.uh = val->h;
  sp.color[0] = sp.color[1] = sp.color[2] = 255;
  sp.color[3] = ms->alpha;
  sp.sx = sp.sy = 1.f;
  shot.pic = PictureById(val->tex);
  shot.valid = shot.seen = shot.pic.ok;
  shot.inset = true;
  if (!shot.valid) return false;
  DrawSprite(dl, sc, shot, 0.f);
  return true;
}
void DrawPieces(ImDrawList* dl, const Screen& sc, const MartPiece* p, size_t n, float dy) {
  for (size_t i = 0; i < n; i++) DrawMenuSprite(dl, sc, p[i].sprite, p[i].x, p[i].y, p[i].w, p[i].h, dy);
}
const MartPiece kMoney[] = {{0xE1, 405, 41, 130, 28}, {0xE0, 366, 37, 51, 38}, {0xDF, 417, 37, 107, 38}, {0xDE, 524, 37, 24, 38}};
void MoneyPlate(ImDrawList* dl, const Screen& sc, float dy, long money) {
  DrawPieces(dl, sc, kMoney, std::size(kMoney), dy);
  const UseFont font(1);
  const std::string t = Thousands(money);
  DrawText(dl, sc, 528.f - static_cast<float>(TextWidth(t.c_str())), 47.f + dy + std::floor((18.f - static_cast<float>(sFontW + 1)) / 2.f), t.c_str(), kWhite);
}
const MartPiece kHave[] = {{0xE1, 379, 85, 235, 60}, {0xE9, 366, 95, 14, 41}, {0xE9, 614, 95, 14, 41}, {0xE8, 390, 145, 214, 6}, {0xE8, 390, 79, 214, 6},
                           {0xE7, 604, 135, -24, -16}, {0xE7, 604, 79, -24, 16}, {0xE7, 366, 135, 24, -16}, {0xE7, 366, 79, 24, 16}};
const MartPiece kAmount[] = {{0xE1, 388, 289, 203, 33}, {0xD8, 367, 275, 45, 61}, {0xD7, 412, 275, 26, 61}, {0xD5, 438, 275, 19, 61},
                             {0xE8, 457, 283, 123, 6}, {0xE8, 457, 322, 123, 6}, {0xE9, 590, 299, 14, 13}, {0xE7, 580, 312, -24, -16},
                             {0xE7, 580, 283, -24, 16}};

void Header(ImDrawList* dl, const Screen& sc, const char* word) {
  const float end = kit::Prompt(dl, sc, 380.f, 44.f, {"L", "R"}, word);
  kit::Prompt(dl, sc, std::max(530.f, end + 14.f), 44.f, {"B"}, Tr("BACK"));
}
std::string Fit(const std::string& t, float room) { return static_cast<float>(TextWidth(t.c_str())) > room ? CutTo(t, room) : t; }
void Frame(ImDrawList* dl, const Screen& sc, int n, int active) {
  bag::Frame(dl, sc, -1, nullptr);
  const float x0 = 91.f + 20.f * static_cast<float>(5 - n);
  for (int i = 0; i < n; i++) {
    const float x = x0 + 40.f * static_cast<float>(i);
    kit::Spr(dl, sc, bag::kSheet, 33, 144, 31, 31, x, 80, 31, 31);
    if (i == active) kit::Spr(dl, sc, bag::kSheet, 1, 144, 31, 31, x, 80, 31, 31);
  }
}
float RowTop(int i) { return bag::kRowTop + bag::kPitch * static_cast<float>(i); }

void DrawCheatRow(ImDrawList* dl, const Screen& sc, float top, const CRow& r, bool sel) {
  const bool grey = RowGrey(r);
  const std::string v = ValueTextOf(r);
  if (r.kind == Kind::Picker || r.kind == Kind::Purify) {
    bag::DrawRow(dl, sc, top, RowLabel(r), "", "", sel, false, grey ? kGrey : nullptr);
    fit::Art(321.f, top + 3.f, 22.f, 22.f, "A button");
    if (!fit::sFit.noDraw) kit::Key(dl, sc, 331.f, top + 14.f, "A", 1.1f);
    return;
  }
  bag::DrawRow(dl, sc, top, RowLabel(r), v, "", sel, sel && !grey, grey ? kGrey : nullptr);
}

void DrawPage(ImDrawList* dl, const Screen& sc) {
  FillWindow(dl, IM_COL32(0, 0, 0, 128));
  Frame(dl, sc, kGroups, S.group);
  bag::Plate(dl, sc, GroupName(S.group));
  Header(dl, sc, Tr("PAGE"));
  const std::vector<int> rows = RowsOf(S.group);
  int& cur = S.cursor[S.group];
  cur = std::clamp(cur, 0, static_cast<int>(rows.size()) - 1);
  for (size_t i = 0; i < rows.size(); i++) DrawCheatRow(dl, sc, RowTop(static_cast<int>(i)), kCheatRows[rows[i]], static_cast<int>(i) == cur);
  if (S.group == 2) MoneyPlate(dl, sc, 38.f, Money());
  const std::string desc = !S.note.empty() ? S.note : DescriptionOf(kCheatRows[rows[static_cast<size_t>(cur)]]);
  bag::DrawDescription(dl, sc, desc, S.descAge);
}

void TotalText(ImDrawList* dl, const Screen& sc, const std::string& total, float y) {
  const float room = 136.f, w = static_cast<float>(TextWidth(total.c_str()));
  const float squeeze = w > room ? room / w : 1.f;
  DrawText(dl, sc, 584.f - w * squeeze, y, total.c_str(), kWhite, squeeze);
}
void DrawPicker(ImDrawList* dl, const Screen& sc) {
  FillWindow(dl, IM_COL32(0, 0, 0, 128));
  Frame(dl, sc, kPockets, S.pocket);
  bag::Plate(dl, sc, PocketName(S.pocket));
  Header(dl, sc, Tr("POCKET"));
  const std::vector<ItemInfo>& items = sPocket[S.pocket];
  const int n = static_cast<int>(items.size()), shown = std::min(n, static_cast<int>(bag::kRowsShown));
  int& at = S.item[S.pocket];
  int& first = S.first[S.pocket];
  at = std::clamp(at, 0, std::max(0, n - 1));
  first = std::clamp(first, std::max(0, at - shown + 1), at);
  first = std::clamp(first, 0, std::max(0, n - shown));
  for (int i = first; i < first + shown; i++) bag::DrawRow(dl, sc, RowTop(i - first), items[static_cast<size_t>(i)].name.c_str(), "", "", i == at, false);
  if (first > 0) kit::Spr(dl, sc, kArrows, 23, 1, 13, 8, 180.f, 128.f, 13, 8);
  if (first + shown < n) kit::Spr(dl, sc, kArrows, 23, 48, 13, 8, 180.f, 414.f, 13, 8);
  if (n == 0) {
    bag::DrawDescription(dl, sc, Tr("This pocket has nothing to give."), S.descAge);
    return;
  }
  const ItemInfo& it = items[static_cast<size_t>(at)];
  if (!S.howMany) {
    bag::DrawDescription(dl, sc, !S.note.empty() ? S.note : GameText(it.doc), S.descAge);
    return;
  }
  const int have = InBag(it.id) + sPending[it.id];
  DrawPieces(dl, sc, kHave, std::size(kHave), 0.f);
  {
    const UseFont font(1);
    fit::Region(383.f, 90.f, 611.f, 140.f, "have box");
    DrawText(dl, sc, 383.f, 93.f, Fit(it.name, 226.f).c_str(), kWhite);
    DrawText(dl, sc, 383.f, 119.f, Fmt(Tr("In your BAG \xC3\x97%d"), have).c_str(), kWhite);
  }
  DrawPieces(dl, sc, kAmount, std::size(kAmount), 0.f);
  {
    const UseFont font(1);
    const float ty = 297.f + std::floor((18.f - static_cast<float>(sFontW + 1)) / 2.f);
    const char digits[2] = {static_cast<char>('0' + S.amount / 10), static_cast<char>('0' + S.amount % 10)};
    for (int k = 0; k < 2; k++) {
      const std::string d(1, digits[k]);
      const float cx = (k == 0 ? 398.f : 424.f);
      DrawText(dl, sc, cx - static_cast<float>(TextWidth(d.c_str())) / 2.f, ty, d.c_str(), kWhite);
    }
    DrawMenuSprite(dl, sc, 0x63, 417.f, 278.f, 15.f, 55.f);
    const std::string total = Fmt(Tr("Total \xC3\x97%d"), std::min(999, have + S.amount));
    fit::Region(446.f, 290.f, 586.f, 320.f, "amount plate");
    TotalText(dl, sc, total, ty);
  }
  bag::DrawDescription(dl, sc, Fmt(Tr("%s? How many should go into your BAG?"), it.name.c_str()), S.descAge);
}

struct Mouse {
  bool on = false;
  float x = 0, y = 0;
  bool moved = false, click = false, right = false;
  float wheel = 0.f;
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
  m.wheel = io.MouseWheel;
  return m;
}
int RowAt(const Mouse& m, int count) {
  if (!m.on || m.x < 28.f || m.x > 344.f) return -1;
  const int i = static_cast<int>(std::floor((m.y - bag::kRowTop) / bag::kPitch));
  return i >= 0 && i < count && m.y >= bag::kRowTop ? i : -1;
}
int ControlAt(const Mouse& m, int dots) {
  if (!m.on) return 0;
  if (m.y >= 44.f && m.y <= 70.f && m.x >= 36.f && m.x <= 62.f) return -1;
  if (m.y >= 44.f && m.y <= 70.f && m.x >= 310.f && m.x <= 336.f) return 1;
  if (m.y >= 30.f && m.y <= 58.f && m.x >= 520.f && m.x <= 620.f) return 2;
  const float x0 = 91.f + 20.f * static_cast<float>(5 - dots);
  for (int i = 0; i < dots; i++) {
    const float x = x0 + 40.f * static_cast<float>(i);
    if (m.x >= x && m.x <= x + 31.f && m.y >= 80.f && m.y <= 111.f) return 10 + i;
  }
  return 0;
}

void Open(void*) {
  const int group = S.group;
  int cursor[kGroups];
  std::copy(std::begin(S.cursor), std::end(S.cursor), cursor);
  const int candy = S.candy, pocket = S.pocket;
  S = State();
  S.group = group, S.candy = candy, S.pocket = pocket;
  std::copy(std::begin(cursor), std::end(cursor), S.cursor);
  S.age.Reset();
  sChanged = false;
  sPending.clear();
  sItemsRead = false;
  ReadItems();
  PortSpeedups_CheatMenuPresses();
}
void Close(void*) {
  if (sChanged && xd::SaveSettings(es::Global()) == xd::SettingsSave::Saved) std::fprintf(stderr, "[cheat menu] settings saved\n");
  PortInput_LatchHeld();
}

void Say(const std::string& note) { S.note = note, S.noteAge = 0.f, S.descAge = 0.f; }

bool PickerFrame(const PortMenuInput* in, const Mouse& m, int& sound) {
  const std::vector<ItemInfo>& items = sPocket[S.pocket];
  const int n = static_cast<int>(items.size());
  int& at = S.item[S.pocket];
  if (S.howMany) {
    int a = S.amount;
    if (in->repeat & PAD_BUTTON_UP) a++;
    if (in->repeat & PAD_BUTTON_DOWN) a--;
    if (in->repeat & PAD_BUTTON_RIGHT) a += 10;
    if (in->repeat & PAD_BUTTON_LEFT) a -= 10;
    if (m.wheel > 0.f) a++;
    if (m.wheel < 0.f) a--;
    a = a < 1 ? 99 : a > 99 ? 1 : a;
    if (a != S.amount) S.amount = a, sound = kSoundMove;
    const bool clickBox = m.click && m.x >= 366.f && m.x <= 604.f && m.y >= 275.f && m.y <= 336.f;
    if ((in->pressed & PAD_BUTTON_A) || clickBox) {
      const ItemInfo& it = items[static_cast<size_t>(at)];
      if (PortXD_CheatDo(("give_item " + std::to_string(it.id) + " " + std::to_string(S.amount)).c_str()) != 0) {
        sPending[it.id] += S.amount;
        Say(Fmt(Tr("%s \xC3\x97%d will be in your BAG."), it.name.c_str(), S.amount));
        sound = kSoundChoose;
      } else {
        Say(Tr("Not possible now."));
        sound = kSoundBack;
      }
      S.howMany = false;
      return true;
    }
    if ((in->pressed & PAD_BUTTON_B) || m.right) S.howMany = false, sound = kSoundBack;
    return true;
  }
  const int before = S.pocket;
  if (in->pressed & PAD_TRIGGER_L) S.pocket = (S.pocket + kPockets - 1) % kPockets;
  if (in->pressed & PAD_TRIGGER_R) S.pocket = (S.pocket + 1) % kPockets;
  const int ctl = m.click ? ControlAt(m, kPockets) : 0;
  if (ctl == -1) S.pocket = (S.pocket + kPockets - 1) % kPockets;
  if (ctl == 1) S.pocket = (S.pocket + 1) % kPockets;
  if (ctl >= 10) S.pocket = ctl - 10;
  if (S.pocket != before) {
    S.descAge = 0.f, S.note.clear(), sound = kSoundMove;
    return true;
  }
  if ((in->pressed & PAD_BUTTON_B) || m.right || ctl == 2) {
    sound = kSoundBack;
    S.note.clear(), S.descAge = 0.f;
    return false;
  }
  if (n == 0) return true;
  int next = at;
  if (in->repeat & PAD_BUTTON_UP) next = (at + n - 1) % n;
  if (in->repeat & PAD_BUTTON_DOWN) next = (at + 1) % n;
  if (in->repeat & PAD_BUTTON_LEFT) next = std::max(0, at - static_cast<int>(bag::kRowsShown));
  if (in->repeat & PAD_BUTTON_RIGHT) next = std::min(n - 1, at + static_cast<int>(bag::kRowsShown));
  if (m.wheel > 0.f) next = std::max(0, at - 1);
  if (m.wheel < 0.f) next = std::min(n - 1, at + 1);
  const int shown = std::min(n, static_cast<int>(bag::kRowsShown));
  const int hover = RowAt(m, shown);
  if (hover >= 0 && m.moved) next = S.first[S.pocket] + hover;
  if (next != at) at = next, S.descAge = 0.f, S.note.clear(), sound = kSoundMove;
  if ((in->pressed & PAD_BUTTON_A) || (m.click && hover >= 0)) {
    if (hover >= 0) at = S.first[S.pocket] + hover;
    S.howMany = true, S.amount = 1, S.descAge = 0.f, S.note.clear();
    sound = kSoundChoose;
  }
  return true;
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
  const float dt = 1.f / 60.f;
  S.age.Tick();
  S.descAge += dt;
  if (!S.note.empty() && (S.noteAge += dt) > 3.f) S.note.clear(), S.descAge = 0.f;
  if (!PortXD_CheatsEnabled()) return false;
  if (PortSpeedups_CheatMenuPresses() > 0) {
    sound = kSoundBack;
    return false;
  }
  if (in->pressed & PAD_BUTTON_START) {
    sound = kSoundBack;
    return false;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (S.picker) {
    if (!PickerFrame(in, m, sound)) S.picker = false;
    if (S.picker) {
      DrawPicker(dl, sc);
      return true;
    }
  } else {
    const int before = S.group;
    if (in->pressed & PAD_TRIGGER_L) S.group = (S.group + kGroups - 1) % kGroups;
    if (in->pressed & PAD_TRIGGER_R) S.group = (S.group + 1) % kGroups;
    const int ctl = m.click ? ControlAt(m, kGroups) : 0;
    if (ctl == -1) S.group = (S.group + kGroups - 1) % kGroups;
    if (ctl == 1) S.group = (S.group + 1) % kGroups;
    if (ctl >= 10) S.group = ctl - 10;
    if ((in->pressed & PAD_BUTTON_B) || m.right || ctl == 2) {
      sound = kSoundBack;
      return false;
    }
    if (S.group != before) {
      S.descAge = 0.f, S.note.clear(), sound = kSoundMove;
    } else {
      const std::vector<int> rows = RowsOf(S.group);
      const int n = static_cast<int>(rows.size());
      int& cur = S.cursor[S.group];
      int next = cur;
      if (in->repeat & PAD_BUTTON_UP) next = (cur + n - 1) % n;
      if (in->repeat & PAD_BUTTON_DOWN) next = (cur + 1) % n;
      if (m.wheel > 0.f) next = (cur + n - 1) % n;
      if (m.wheel < 0.f) next = (cur + 1) % n;
      const int hover = RowAt(m, n);
      if (hover >= 0 && m.moved) next = hover;
      if (next != cur) cur = next, S.descAge = 0.f, S.note.clear(), sound = kSoundMove;
      const CRow& r = kCheatRows[rows[static_cast<size_t>(cur)]];
      int dir = 0;
      if (in->repeat & PAD_BUTTON_LEFT) dir = -1;
      if (in->repeat & PAD_BUTTON_RIGHT) dir = +1;
      const bool click = m.click && hover == cur;
      const bool a = (in->pressed & PAD_BUTTON_A) != 0 || click;
      if (RowGrey(r)) {
        if (dir != 0 || a) sound = kSoundBack;
      } else if (r.kind == Kind::Toggle || r.kind == Kind::Choice) {
        if (dir == 0 && a) dir = +1;
        if (dir != 0) {
          const int k = ChoiceCount(r);
          SetValue(r, (ValueOf(r) + dir + k) % k);
          S.descAge = 0.f, S.note.clear(), sound = kSoundMove;
        }
      } else if (r.kind == Kind::Picker) {
        if (a) S.picker = true, S.howMany = false, S.descAge = 0.f, S.note.clear(), sound = kSoundChoose;
      } else if (r.kind == Kind::Purify) {
        if (a) {
          const bool done = PortXD_CheatDo("purify") != 0;
          Say(done ? Tr("Every heart in your party will be open.") : Tr("Not possible now."));
          sound = done ? kSoundChoose : kSoundBack;
        }
      } else if (r.kind == Kind::Candy) {
        if (dir != 0 && !click) {
          int at = 0;
          for (int i = 0; i < static_cast<int>(std::size(kCandySteps)); i++) {
            if (kCandySteps[i] == S.candy) at = i;
          }
          at = std::clamp(at + dir, 0, static_cast<int>(std::size(kCandySteps)) - 1);
          if (kCandySteps[at] != S.candy) S.candy = kCandySteps[at], sound = kSoundMove;
        } else if (a) {
          if (PortXD_CheatDo(("candy " + std::to_string(S.candy)).c_str()) != 0) {
            if (sCandy != 0) sPending[sCandy] += S.candy;
            Say(Fmt(Tr("%s \xC3\x97%d will be in your BAG."), CandyLabel().c_str(), S.candy));
            sound = kSoundChoose;
          } else {
            Say(Tr("Not possible now."));
            sound = kSoundBack;
          }
        }
      }
    }
  }
  if (S.picker) DrawPicker(dl, sc);
  else DrawPage(dl, sc);
  return true;
}

const PortMenuPage kPage{"cheats", Open, Frame, Close, nullptr};

void Audit(const std::function<void(const std::string&, const std::function<void()>&)>& run, ImDrawList* dl, const Screen& sc) {
  for (int g = 0; g < kGroups; g++) {
    run(std::string("cheats plate ") + GroupName(g), [&] { bag::Plate(dl, sc, GroupName(g)); });
    const std::vector<int> rows = RowsOf(g);
    for (size_t i = 0; i < rows.size(); i++) {
      const CRow& r = kCheatRows[rows[i]];
      const int k = r.setting != nullptr ? ChoiceCount(r) : 1;
      const int keep = ValueOf(r);
      for (int v = 0; v < k; v++) {
        if (r.setting != nullptr) sLocal[r.setting] = v;
        const bool real = HaveSetting(r.setting);
        es::Value saved;
        if (real) {
          saved = es::Global().ValueOf(es::Global().Find(r.setting));
          es::Value val = saved;
          val.i = v;
          es::Global().Set(es::Global().Find(r.setting), val);
        }
        for (int sel = 0; sel < 2; sel++) {
          run(std::string("cheats row ") + RowLabel(r) + " = " + ValueTextOf(r) + " sel" + std::to_string(sel), [&] { DrawCheatRow(dl, sc, RowTop(static_cast<int>(i)), r, sel != 0); });
          const std::string d = DescriptionOf(r);
          int pages = 1;
          {
            const UseFont f(1);
            pages = std::max(1, (static_cast<int>(WrapPaged(d, 210.f, 196.f, 3).size()) + 2) / 3);
          }
          if (sel == 0) {
            for (int pg = 0; pg < pages; pg++) run(std::string("cheats desc ") + RowLabel(r) + " page " + std::to_string(pg), [&] { bag::DrawDescription(dl, sc, d, 0.f, pg); });
          }
        }
        if (real) es::Global().Set(es::Global().Find(r.setting), saved);
      }
      if (r.setting != nullptr && !HaveSetting(r.setting)) sLocal[r.setting] = keep;
    }
  }
  run("cheats prompts", [&] { Header(dl, sc, Tr("PAGE")); });
  run("cheats prompts pocket", [&] { Header(dl, sc, Tr("POCKET")); });
  run("cheats money", [&] { MoneyPlate(dl, sc, 38.f, 9999999); });
  for (int p = 0; p < kPockets; p++) run(std::string("cheats pocket plate ") + PocketName(p), [&] { bag::Plate(dl, sc, PocketName(p)); });
  const std::string longName = "SUPER LONG ITEM NAME";
  run("cheats have box", [&] {
    const UseFont font(1);
    fit::Region(383.f, 90.f, 611.f, 140.f, "have box");
    DrawText(dl, sc, 383.f, 93.f, Fit(longName, 226.f).c_str(), kWhite);
    DrawText(dl, sc, 383.f, 119.f, Fmt(Tr("In your BAG \xC3\x97%d"), 999).c_str(), kWhite);
  });
  run("cheats amount", [&] {
    const UseFont font(1);
    const std::string total = Fmt(Tr("Total \xC3\x97%d"), 999);
    fit::Region(446.f, 290.f, 586.f, 320.f, "amount plate");
    TotalText(dl, sc, total, 299.f);
  });
  for (const std::string& d : {Fmt(Tr("%s? How many should go into your BAG?"), longName.c_str()), Fmt(Tr("%s \xC3\x97%d will be in your BAG."), longName.c_str(), 99),
                               std::string(Tr("This pocket has nothing to give.")), std::string(Tr("Not possible now."))}) {
    run("cheats picker desc " + d, [&] { bag::DrawDescription(dl, sc, d, 0.f, 0); });
  }
}

}

namespace cw {

int sStep = 0;
int sCursor = 1;
int sPage = 0;
constexpr size_t kLinesPerPage = 3;
const char* kSetting = "cheats.enabled";

std::string KeyName() {
  const auto action = engine::controls::ActionFromId("cheat_menu");
  if (!action) return "F5";
  const engine::controls::PortMap map = engine::controls::ReadPort(es::Global(), 0);
  const auto& keys = map.actions.keys[static_cast<size_t>(*action)];
  if (keys.empty()) return Tr("the cheat menu key");
  return engine::controls::InputLabel(keys.front(), engine::controls::Device::Keyboard).text;
}

bool IsRow(const char* id) { return id != nullptr && std::strcmp(id, kSetting) == 0; }
bool Active() { return sStep != 0; }
bool Intercept(bool turningOn) {
  if (!turningOn || PortXD_CheatsWarned() != 0) return false;
  sStep = 1, sCursor = 1, sPage = 0;
  return true;
}
void SwitchOn() {
  es::Registry& reg = es::Global();
  const size_t i = reg.Find(kSetting);
  if (i == es::kNoSetting) return;
  es::Value v = reg.ValueOf(i);
  v.i = 1;
  reg.Set(i, v);
  std::fprintf(stderr, "[settings menu] %s = on (after the warning)\n", kSetting);
  PortXD_CheatsSetWarned(1);
}
std::string Text(int step) {
  switch (step) {
  case 1: return Tr("Cheats mark this save \"cheats used\" for good, even if you switch them off again later. A save with cheats can't join competitive online rooms.");
  case 2: return Tr("Switch cheats on for this save?");
  default: return Fmt(Tr("Cheats are on. Press %s, or hold L and R and press START, to open the cheat menu at any time."), KeyName().c_str());
  }
}
std::vector<std::string> Lines(int step) {
  const UseFont f(0);
  return Wrap(Text(step), 530.f);
}
int Pages(int step) { return std::max(1, static_cast<int>((Lines(step).size() + kLinesPerPage - 1) / kLinesPerPage)); }
bool Input(const PortMenuInput* in, bool& changed, int& sound) {
  const bool a = (in->pressed & PAD_BUTTON_A) != 0, b = (in->pressed & (PAD_BUTTON_B | PAD_BUTTON_START)) != 0;
  bool click = false, right = false;
  if (ImGui::GetCurrentContext() != nullptr) click = ImGui::GetIO().MouseClicked[0], right = ImGui::GetIO().MouseClicked[1];
  if ((a || click) && sStep != 2 && sPage + 1 < Pages(sStep)) {
    sPage++, sound = kSoundChoose;
    return changed;
  }
  switch (sStep) {
  case 1:
    if (a || click) sStep = 2, sCursor = 1, sPage = 0, sound = kSoundChoose;
    else if (b || right) sStep = 0, sound = kSoundBack;
    break;
  case 2:
    if (in->repeat & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) sCursor ^= 1, sound = kSoundMove;
    if (click && ImGui::GetCurrentContext() != nullptr) {
      const Screen sc = ScreenNow();
      const float my = (ImGui::GetIO().MousePos.y - sc.y) / sc.s, mx = (ImGui::GetIO().MousePos.x - sc.x) / sc.s;
      if (mx >= 450.f && mx <= 604.f && my >= 190.f && my <= 281.f) sCursor = my < 236.f ? 0 : 1;
      else click = false;
    }
    if (a || click) {
      if (sCursor == 0) {
        SwitchOn();
        changed = true;
        sStep = 3, sPage = 0, sound = kSoundChoose;
      } else {
        sStep = 0, sound = kSoundBack;
      }
    } else if (b || right) {
      sStep = 0, sound = kSoundBack;
    }
    break;
  case 3:
    if (a || b || click || right) sStep = 0, sound = kSoundChoose;
    break;
  default: break;
  }
  return changed;
}
void Draw(ImDrawList* dl, const Screen& sc) {
  if (sStep == 0) return;
  kit::MessageWindow(dl, sc);
  const std::vector<std::string> lines = Lines(sStep);
  const int pages = std::max(1, static_cast<int>((lines.size() + kLinesPerPage - 1) / kLinesPerPage));
  sPage = std::clamp(sPage, 0, pages - 1);
  fit::Region(52.f, 300.f, 590.f, 436.f, "message window");
  for (size_t k = 0; k < kLinesPerPage; k++) {
    const size_t i = static_cast<size_t>(sPage) * kLinesPerPage + k;
    if (i < lines.size()) kit::Text(dl, sc, 53.f, 305.f + 33.f * static_cast<float>(k), lines[i], kit::kText, 1.f, 0.f, 0);
  }
  if (sStep == 1 || sPage + 1 < pages) kit::Spr(dl, sc, cm::kArrows, 23, 48, 13, 8, 584.f, 418.f + 2.f * std::sin(kit::Time() * 6.28f), 13, 8);
  if (sStep == 2) kit::ChoiceBox(dl, sc, {Tr("YES"), Tr("NO")}, sCursor, 0);
}
void Audit(const std::function<void(const std::string&, const std::function<void()>&)>& run, ImDrawList* dl, const Screen& sc) {
  for (int s = 1; s <= 3; s++) {
    for (int pg = 0; pg < Pages(s); pg++) {
      run("cheats message " + std::to_string(s) + " page " + std::to_string(pg), [&] {
        sStep = s, sPage = pg;
        Draw(dl, sc);
        sStep = 0, sPage = 0;
      });
    }
  }
}

}
