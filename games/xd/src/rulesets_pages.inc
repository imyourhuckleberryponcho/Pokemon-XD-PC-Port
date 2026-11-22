namespace rp {

namespace er = engine::rulesets;
namespace tx = xd::rulesets::text;

enum View { kMain = 0, kRuleSet = 1, kPokemon = 2, kLooks = 3, kPins = 4 };

constexpr bool kSplitFairyAsDetailRows = true;
constexpr bool kChangeRulesMidPlaythrough = false;

bool SourceReady(const std::string& id) {
  if (id == "xd") return true;
  if (id == "br") return PortXD_BattleModelsFound();
  if (const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(id)) {
    std::error_code ec;
    return std::filesystem::is_directory(xd::SettingsFolder() / L"extra_assets" / f->layerId / L"fsys", ec);
  }
  return false;
}
bool Gen4Ready() { return s.gen4Why.empty() && xd::xg::Off(xd::xg::Feature::Gen4Pokemon) == nullptr; }
bool Gen57Ready() { return Gen4Ready() && xd::gen57::PackPresent(); }
std::string Gen57Why() { return Tr("Needs the Gen 5-7 pack: add your Ultra Sun / Ultra Moon dump in the launcher."); }
std::string Gen4Why() {
  if (const char* xg = xd::xg::Off(xd::xg::Feature::Gen4Pokemon)) return Tr(xg);
  return Fmt(Tr("Needs the Gen 4 pack: add your Battle Revolution disc in the launcher (Extra assets page). (%s)"), s.gen4Why.c_str());
}

const char* const kStyles[] = {"original_xd", "most_modern", "br_first", "custom"};

std::string StyleOf(const er::Looks& l) {
  if (!l.Says()) return "original_xd";
  for (const char* st : kStyles) {
    const er::LooksPreset* p = xd::rulesets::Registry().FindPreset(st);
    if (p != nullptr && p->sources == l.sources) return st;
  }
  return "custom";
}
void SetStyle(const std::string& id) {
  er::Looks keep = s.choice.looks;
  if (id == "custom") {
    if (StyleOf(keep) != "custom") {
      std::vector<std::string> l = keep.Says() ? keep.sources : std::vector<std::string>{"xd"};
      for (const char* x : {"usum", "br", "xd"}) if (std::find(l.begin(), l.end(), x) == l.end()) l.push_back(x);
      keep.sources = l;
    }
    keep.preset.clear();
  } else if (const er::LooksPreset* p = xd::rulesets::Registry().FindPreset(id)) {
    keep.preset = id;
    keep.sources = p->sources;
  }
  s.choice.looks = keep;
}
void SetRuleSet(const std::string& id) {
  const er::RuleSet* r = xd::rulesets::Registry().FindRuleSet(id);
  if (r == nullptr || !r->available) return;
  if (id != "xd" && !Gen4Ready()) return;
  s.choice.ruleSet = id;
  s.choice.mechanics.clear();
  if (id != "xd" && !s.choice.HasGen(4)) s.choice.gens = {3, 4};
  s.choice = er::Normalize(s.choice, xd::rulesets::Registry());
}
void SetGens(int top) {
  if (top >= 4 && !Gen4Ready()) return;
  if (top >= 5 && !Gen57Ready()) return;
  if (top < 3 || top > 7) return;
  s.choice.gens.clear();
  for (int g = 3; g <= top; g++) s.choice.gens.push_back(g);
  if (top == 3) {
    s.choice.SetOption(xd::rulesets::kOptionSpots, false);
    if (s.choice.ruleSet != "xd") s.choice.ruleSet = "xd", s.choice.mechanics.clear();
  }
  s.choice = er::Normalize(s.choice, xd::rulesets::Registry());
}
int TopGen() { return s.choice.gens.empty() ? 3 : std::max(3, s.choice.gens.back()); }
int NextTop(bool left) {
  std::vector<int> ok = {3};
  if (Gen4Ready()) ok.push_back(4);
  if (Gen57Ready()) for (int g = 5; g <= 7; g++) ok.push_back(g);
  size_t at = std::find(ok.begin(), ok.end(), TopGen()) - ok.begin();
  if (at >= ok.size()) at = 0;
  return ok[(at + (left ? ok.size() - 1 : 1)) % ok.size()];
}

std::string RuleSetValue() {
  std::string v = tx::RuleSetShort(s.choice.ruleSet);
  if (s.choice.MechanicOn(xd::rulesets::Registry(), xd::rulesets::kModernTypes)) v += std::string(" + ") + Tr("Fairy type");
  return v;
}
std::string PokemonValue() {
  std::string v = tx::PokemonShort(TopGen());
  if (s.choice.HasGen(4) && s.choice.HasOption(xd::rulesets::kOptionSpots)) v += std::string(" + ") + Tr("Poke Spots");
  return v;
}
std::string LooksValue() { return StyleOf(s.choice.looks) == "custom" ? Tr("Custom...") : tx::StyleName(StyleOf(s.choice.looks)); }

std::string SummaryLine() {
  return std::string(tx::RuleSetShort(s.choice.ruleSet)) + " - " + tx::PokemonShort(TopGen()) + " - " + tx::StyleShort(StyleOf(s.choice.looks));
}
std::string FitNote() {
  if (s.choice.HasGen(4) && s.choice.ruleSet == "xd") return Tr("Gen 4 Pokemon under XD rules: some of their moves and abilities work differently (see the list).");
  return "";
}

struct Counts { std::map<std::string, int> n; int pins = 0; };
Counts CountSources() {
  Counts c;
  const int total = TopGen() == 3 ? 386 : TopGen() == 4 ? 493 : TopGen() == 5 ? 649 : TopGen() == 6 ? 721 : 807;
  std::vector<std::string> ranked = er::Ranked(s.choice.looks, xd::rulesets::Registry());
  if (ranked.empty()) ranked = {"xd"};
  int left = total;
  for (const std::string& src : ranked) {
    if (!SourceReady(src)) continue;
    int has = total;
    if (src != "xd") {
      std::error_code ec;
      has = 0;
      if (const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(src)) {
        for (const auto& e : std::filesystem::directory_iterator(xd::SettingsFolder() / L"extra_assets" / f->layerId / L"fsys", ec)) {
          if (e.is_directory(ec)) has++;
        }
      }
      has = std::min(has, left);
    } else {
      has = left;
    }
    c.n[src] = has;
    left -= has;
  }
  c.pins = static_cast<int>(s.choice.looks.overrides.size());
  return c;
}
std::string YouWillSee() {
  const Counts c = CountSources();
  std::string t;
  std::vector<std::string> ranked = er::Ranked(s.choice.looks, xd::rulesets::Registry());
  if (ranked.empty()) ranked = {"xd"};
  for (const std::string& src : ranked) {
    auto it = c.n.find(src);
    t += (t.empty() ? "" : " - ") + std::string(tx::SourceShort(src)) + " " + std::to_string(it == c.n.end() ? 0 : it->second);
  }
  if (c.pins > 0) t += "  " + Fmt(Tr("(%d pinned)"), c.pins);
  return t;
}

const uint8_t* const kWhite = kit::kText;

void Radio(ImDrawList* dl, const Screen& sc, float cx, float cy, bool on, bool enabled) {
  const ImVec2 c = sc.At(cx, cy);
  const ImU32 rim = enabled ? IM_COL32(0xF0, 0xF0, 0xF0, 0xFF) : IM_COL32(0x70, 0x70, 0x78, 0xFF);
  dl->AddCircle(c, 8.f * sc.s, rim, 24, 2.f * sc.s);
  if (on) dl->AddCircleFilled(c, 4.5f * sc.s, enabled ? IM_COL32(0xE0, 0x20, 0x28, 0xFF) : IM_COL32(0x80, 0x50, 0x50, 0xFF), 20);
}

void Description(ImDrawList* dl, const Screen& sc, float y0, float y1, const std::string& head, const std::string& body, const std::string& warn = "",
                 const std::string& soft = "") {
  kit::Window(dl, sc, 75.f, y0, 565.f, y1);
  kit::Text(dl, sc, 93.f, y0 + 10.f, head, kit::kText, 0.85f, 440.f, 1);
  float y = y0 + 31.f;
  y += 14.f * static_cast<float>(kit::Para(dl, sc, 93.f, y, 458.f, body, kit::kText, 0.66f, 14.f, 3));
  if (!warn.empty()) y += 14.f * static_cast<float>(kit::Para(dl, sc, 93.f, y, 458.f, warn, kit::kGold, 0.66f, 14.f, 2));
  if (!soft.empty()) kit::Para(dl, sc, 93.f, y, 458.f, soft, kit::kSoft, 0.62f, 13.f, 2);
}

int vFirst = 0, vCur = 0;

std::vector<int> DetailRows() {
  std::vector<int> v;
  if (!kSplitFairyAsDetailRows) return v;
  if (s.choice.ruleSet == "xd" && s.engine == engine::playthrough::Engine::Enhanced) v.push_back(5);
  if (s.choice.ruleSet == "xd" || s.choice.ruleSet == "gen4" || s.choice.ruleSet == "gen5") v.push_back(6);
  return v;
}
std::vector<std::string> RuleSetIds() { std::vector<std::string> v; for (const auto& r : xd::rulesets::Registry().RuleSets()) v.push_back(r.id); return v; }

std::string SplitValue(bool& changeable) {
  const std::string m = s.choice.Mechanic(xd::rulesets::Registry(), xd::rulesets::kSplit);
  if (m == "on" && s.choice.ruleSet != "xd") { changeable = false; return Tr("On (part of Gen 4)"); }
  if (s.engine == engine::playthrough::Engine::Faithful) { changeable = false; return Tr("Off (Faithful)"); }
  changeable = true;
  const std::string o = s.choice.mechanics.count(xd::rulesets::kSplit) ? s.choice.mechanics[xd::rulesets::kSplit] : "player";
  return o == "on" ? Tr("On") : o == "off" ? Tr("Off") : Tr("As in your settings");
}

void DrawRuleSet(ImDrawList* dl, const Screen& sc) {
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("RULE SET"), Tr("how battles work"));
  kit::Window(dl, sc, 45.f, 80.f, 595.f, 236.f);
  const std::vector<std::string> ids = RuleSetIds();
  for (size_t i = 0; i < ids.size(); i++) {
    const er::RuleSet* r = xd::rulesets::Registry().FindRuleSet(ids[i]);
    const bool ok = r->available && (ids[i] == "xd" || Gen4Ready());
    const float y = 92.f + 26.f * static_cast<float>(i);
    const bool cur = vCur == static_cast<int>(i);
    if (cur) kit::RowSel(dl, sc, 50.f, 590.f, y - 2.f, 24.f, false);
    Radio(dl, sc, 92.f, y + 11.f, s.choice.ruleSet == ids[i], ok);
    kit::Text(dl, sc, 110.f, y + 3.f, tx::RuleSetName(ids[i]), ok ? kit::kText : kit::kDim, 0.8f, 290.f, 1);
    std::string right = tx::RuleSetBlurb(ids[i]);
    if (ids[i] != "xd" && r->available && !Gen4Ready()) right = Tr("needs the Gen 4 pack");
    const float w = kit::Width(right, 0.66f);
    kit::Text(dl, sc, 566.f - std::min(w, 175.f), y + 5.f, right, kit::kDim, 0.62f, 175.f);
    if (cur) kit::Cursor(dl, sc, 62.f, y + 11.f, 0.8f);
  }
  float y = 252.f;
  const std::vector<int> details = DetailRows();
  if (!details.empty()) {
    kit::Text(dl, sc, 60.f, y, Tr("Details"), kit::kGold, 0.7f);
    dl->AddLine(sc.At(55.f, y + 17.f), sc.At(585.f, y + 17.f), IM_COL32(0x80, 0x60, 0x60, 0xA0));
    bool changeable = false;
    const std::string split = SplitValue(changeable);
    const bool fairy = s.choice.MechanicOn(xd::rulesets::Registry(), xd::rulesets::kModernTypes);
    for (size_t k = 0; k < details.size(); k++) {
      const float ry = 272.f + 26.f * static_cast<float>(k);
      const bool cur = vCur == 5 + static_cast<int>(k);
      if (cur) kit::RowSel(dl, sc, 50.f, 590.f, ry - 2.f, 24.f, true);
      const bool isSplit = details[k] == 5;
      kit::Text(dl, sc, 80.f, ry + 4.f, isSplit ? Tr("Physical/special split") : Tr("Fairy type"), kit::kText, 0.8f, 230.f, 1);
      kit::Value(dl, sc, 360.f, ry, 210.f, isSplit ? split : std::string(fairy ? Tr("On") : Tr("Off")), cur);
      if (cur) kit::Cursor(dl, sc, 62.f, ry + 11.f, 0.8f);
    }
  }
  const std::vector<std::string> v = RuleSetIds();
  std::string head, body, warn, soft;
  if (vCur < 5) {
    const std::string& id = v[static_cast<size_t>(vCur)];
    head = tx::RuleSetName(id);
    body = tx::RuleSetInfo(id);
    if (id != "xd" && !Gen4Ready()) warn = Gen4Why();
  } else if (vCur - 5 < static_cast<int>(details.size()) && details[static_cast<size_t>(vCur - 5)] == 5) {
    head = Tr("Physical/special split");
    body = Tr("Moves are physical or special by the move, not by its type (Gen 4 and later). Free with XD original; part of Gen 4.");
    bool ch = false; SplitValue(ch);
    if (!ch) soft = Tr("The split is fixed for this choice, so its row is grey.");
  } else {
    head = Tr("Fairy type");
    body = Tr("Adds the Fairy type and the newer type chart. Free with XD original and Gen 4; part of Gen 6 and later.");
  }
  Description(dl, sc, 330.f, 432.f, head, body, warn, soft);
  kit::HintBar(dl, sc, Tr("Up / down: choose   Left / right: change   A: pick   B: back"));
}

void DrawPokemon(ImDrawList* dl, const Screen& sc) {
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("POKEMON"), Tr("which Pokemon are in Orre"));
  kit::Window(dl, sc, 45.f, 80.f, 595.f, 236.f);
  const int gens[5] = {3, 4, 5, 6, 7};
  for (int i = 0; i < 5; i++) {
    const int g = gens[i];
    const bool avail = g == 3 || (g == 4 ? Gen4Ready() : Gen57Ready());
    const float y = 92.f + 26.f * static_cast<float>(i);
    const bool cur = vCur == i;
    if (cur) kit::RowSel(dl, sc, 50.f, 590.f, y - 2.f, 24.f, false);
    Radio(dl, sc, 92.f, y + 11.f, TopGen() == g, avail);
    kit::Text(dl, sc, 110.f, y + 3.f, tx::PokemonName(g), avail ? kit::kText : kit::kDim, 0.8f, 290.f, 1);
    std::string right = tx::PokemonBlurb(g);
    if (g == 4) right += std::string(" - ") + (Gen4Ready() ? Tr("Gen 4 pack ready") : Tr("needs the Gen 4 pack"));
    if (g >= 5) right += std::string(" - ") + (Gen57Ready() ? Tr("Gen 5-7 pack ready") : Tr("needs the Gen 5-7 pack"));
    kit::Text(dl, sc, 566.f - std::min(kit::Width(right, 0.62f), 200.f), y + 5.f, right, g >= 4 && avail ? kit::kGood : kit::kDim, 0.62f, 200.f);
    if (cur) kit::Cursor(dl, sc, 62.f, y + 11.f, 0.8f);
  }
  kit::Text(dl, sc, 60.f, 252.f, Tr("Where they appear"), kit::kGold, 0.7f);
  dl->AddLine(sc.At(55.f, 269.f), sc.At(585.f, 269.f), IM_COL32(0x80, 0x60, 0x60, 0xA0));
  const bool cur = vCur == 5, on = s.choice.HasGen(4) && s.choice.HasOption(xd::rulesets::kOptionSpots);
  if (cur) kit::RowSel(dl, sc, 50.f, 590.f, 276.f, 24.f, true);
  kit::Text(dl, sc, 80.f, 282.f, Tr("Also at the Poke Spots"), s.choice.HasGen(4) ? kit::kText : kit::kDim, 0.8f, 250.f, 1);
  kit::Value(dl, sc, 360.f, 274.f, 210.f, on ? Tr("On") : Tr("Off"), cur);
  if (cur) kit::Cursor(dl, sc, 62.f, 287.f, 0.8f);
  const int g = vCur < 5 ? gens[vCur] : TopGen();
  std::string warn = vCur >= 5 || g == 4 ? FitNote() : "";
  if (g == 4 && !Gen4Ready()) warn = Gen4Why();
  if (g >= 5 && !Gen57Ready()) warn = Gen4Ready() ? Gen57Why() : Gen4Why();
  Description(dl, sc, 330.f, 432.f, tx::PokemonName(vCur < 5 ? g : TopGen()), tx::PokemonInfo(vCur < 5 ? g : TopGen()), warn,
              Tr("The list is one choice: each step includes the ones before it."));
  kit::HintBar(dl, sc, Tr("Up / down: choose   Left / right: change   A: pick   B: back"));
}

std::vector<std::string> SourceRows() {
  std::vector<std::string> v;
  const std::vector<std::string> ranked = er::Ranked(s.choice.looks, xd::rulesets::Registry());
  for (const std::string& r : ranked) v.push_back(r);
  for (const char* x : {"usum", "br", "xd"}) if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
  v.push_back("xy");
  return v;
}
int LooksRows() { return 4 + static_cast<int>(SourceRows().size()) + 1; }
int PinsRow() { return LooksRows() - 1; }

void DrawLooks(ImDrawList* dl, const Screen& sc) {
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("LOOKS"), Tr("pictures only: battles never change"));
  kit::Window(dl, sc, 30.f, 76.f, 610.f, 372.f);
  kit::Text(dl, sc, 50.f, 90.f, Tr("Style"), kit::kGold, 0.8f, 100.f, 1);
  const std::string cur = StyleOf(s.choice.looks);
  for (int i = 0; i < 4; i++) {
    const float y = 86.f + 23.f * static_cast<float>(i);
    const bool on = vCur == i;
    if (on) kit::RowSel(dl, sc, 140.f, 600.f, y - 1.f, 22.f, false);
    Radio(dl, sc, 172.f, y + 10.f, cur == kStyles[i], true);
    kit::Text(dl, sc, 188.f, y + 3.f, tx::StyleName(kStyles[i]), kit::kText, 0.72f, 160.f, 1);
    kit::Text(dl, sc, 355.f, y + 6.f, tx::StyleOrder(kStyles[i]), kit::kDim, 0.52f, 235.f);
    if (on) kit::Cursor(dl, sc, 152.f, y + 10.f, 0.6f);
  }
  kit::Text(dl, sc, 50.f, 187.f, Tr("You will see:"), kit::kText, 0.7f, 100.f);
  kit::Text(dl, sc, 150.f, 187.f, YouWillSee(), kit::kGold, 0.7f, 440.f);
  kit::Text(dl, sc, 50.f, 214.f, Tr("Sources"), kit::kGold, 0.8f, 100.f, 1);
  if (cur == "custom") kit::Text(dl, sc, 50.f, 232.f, Tr("(Custom)"), kit::kDim, 0.55f);
  const std::vector<std::string> src = SourceRows();
  const std::vector<std::string> ranked = er::Ranked(s.choice.looks, xd::rulesets::Registry());
  int rank = 0;
  for (size_t i = 0; i < src.size(); i++) {
    const float y = 208.f + 23.f * static_cast<float>(i);
    const bool on = vCur == 4 + static_cast<int>(i);
    const bool listed = std::find(ranked.begin(), ranked.end(), src[i]) != ranked.end();
    const bool ready = src[i] != "xy" && SourceReady(src[i]);
    if (on) kit::RowSel(dl, sc, 140.f, 600.f, y - 1.f, 22.f, cur == "custom");
    std::string state = src[i] == "xy" ? Tr("later") : ready ? (listed ? Tr("ready") : Tr("off")) : tx::SourceAdd(src[i]);
    const uint8_t* col = ready && listed ? kit::kText : kit::kDim;
    kit::Text(dl, sc, 172.f, y + 3.f, (listed && ready ? std::to_string(++rank) + ".  " : std::string("-.  ")) + tx::SourceName(src[i]), col, 0.76f, 300.f, 1);
    kit::Text(dl, sc, 585.f - std::min(110.f, kit::Width(state, 0.62f)), y + 5.f, state, ready && listed ? kit::kGood : kit::kDim, 0.66f, 110.f);
    if (on) kit::Cursor(dl, sc, 152.f, y + 10.f, 0.6f);
  }
  const bool pinRow = vCur == PinsRow();
  const float py = 208.f + 23.f * static_cast<float>(src.size()) + 8.f;
  dl->AddLine(sc.At(50.f, py - 4.f), sc.At(590.f, py - 4.f), IM_COL32(0x80, 0x60, 0x60, 0xA0));
  if (pinRow) kit::RowSel(dl, sc, 40.f, 600.f, py - 1.f, 22.f, true);
  kit::Text(dl, sc, 50.f, py + 3.f, Tr("Advanced: per Pokemon ..."), kit::kText, 0.76f, 300.f, 1);
  const std::string np = Fmt(Tr("%d pinned"), static_cast<int>(s.choice.looks.overrides.size()));
  kit::Text(dl, sc, 582.f - kit::Width(np, 0.7f), py + 4.f, np, kit::kGold, 0.7f);
  if (pinRow) kit::Cursor(dl, sc, 36.f, py + 10.f, 0.7f);
  std::string info = Tr("For each Pokemon the game uses the first source in the list that has it, so there are never gaps. Grey sources are skipped until you add them in the launcher.");
  if (vCur >= 4 && vCur < PinsRow()) {
    const std::string id = src[static_cast<size_t>(vCur - 4)];
    if (cur != "custom") info = Tr("Choose Custom... to change the order of the sources.");
    else if (id == "xy") info = Tr("X / Y models are not available yet.");
    else if (!SourceReady(id)) info = Fmt(Tr("%s is not on this PC: it is skipped until you add it in the launcher (Extra assets page)."), tx::SourceName(id));
  }
  kit::Window(dl, sc, 75.f, 382.f, 565.f, 436.f);
  kit::Para(dl, sc, 93.f, 392.f, 458.f, info, kit::kText, 0.66f, 14.f, 3);
  kit::HintBar(dl, sc, vCur >= 4 && vCur < PinsRow() ? Tr("X: move up   Y: move down   A: on / off   B: back") : Tr("Up / down: choose   A: pick   B: back"));
}

struct Species { int national; std::string name; };
std::vector<Species> sSpecies;
int pCur = 0, pFirst = 0, pSource = 0;
std::string sPinNote;

void BuildSpecies() {
  if (!sSpecies.empty()) return;
  std::map<int, std::string> by;
  if (pokemon_data != nullptr && pokemon_data_number != nullptr) {
    for (uint32_t i = 1; i < *pokemon_data_number; i++) {
      const int nat = pokemon_data[i].numPokemon;
      if (nat < 1 || nat > 493 || by.count(nat)) continue;
      if (nat > 386 && !s.choice.HasGen(4)) continue;
      by[nat] = xd::starter::SpeciesName(i);
    }
  }
  for (const auto& p : by) sSpecies.push_back({p.first, p.second});
}
std::vector<std::string> PinSources() {
  std::vector<std::string> v;
  for (const char* x : {"xd", "br", "usum", "xy"}) v.push_back(x);
  return v;
}

struct CardPicture { ImTextureID id{}; float w = 0.f, h = 0.f; bool tried = false; };
std::map<std::string, CardPicture> sCardPictures;
const CardPicture& CardPictureFor(const std::string& source, int national) {
  const std::string key = source + "/" + std::to_string(national);
  CardPicture& p = sCardPictures[key];
  if (p.tried) return p;
  p.tried = true;
  const std::filesystem::path file = std::filesystem::path(xd::UserFolder()) / "icons" / "looks" / source / (std::to_string(national) + ".png");
  std::error_code ec;
  const bool there = std::filesystem::exists(file, ec);
  if (std::getenv("XD_LOOKS_LOG")) std::fprintf(stderr, "[looks] card picture %s: %s\n", file.string().c_str(), there ? "found" : "not made yet");
  if (!there) return p;
  SDL_Surface* loaded = SDL_LoadPNG(file.string().c_str());
  SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
  if (rgba && rgba->pitch == rgba->w * 4) {
    p.id = aurora_imgui_add_texture(static_cast<uint32_t>(rgba->w), static_cast<uint32_t>(rgba->h), rgba->pixels);
    p.w = static_cast<float>(rgba->w);
    p.h = static_cast<float>(rgba->h);
  }
  if (std::getenv("XD_LOOKS_LOG")) std::fprintf(stderr, "[looks] card picture loaded: %s (%dx%d)\n", p.id ? "yes" : "NO texture", rgba ? rgba->w : 0, rgba ? rgba->h : 0);
  if (rgba) SDL_DestroySurface(rgba);
  if (loaded) SDL_DestroySurface(loaded);
  return p;
}

void DrawPins(ImDrawList* dl, const Screen& sc) {
  BuildSpecies();
  kit::Backdrop(dl, sc);
  const int np = static_cast<int>(s.choice.looks.overrides.size());
  kit::Header(dl, sc, Tr("LOOKS: PER POKEMON"), Fmt(Tr("%d pinned"), np));
  kit::Window(dl, sc, 15.f, 75.f, 180.f, 420.f);
  constexpr int kShown = 14;
  const int n = static_cast<int>(sSpecies.size());
  pFirst = std::clamp(pFirst, std::max(0, pCur - kShown + 1), pCur);
  for (int k = 0; k < kShown && pFirst + k < n; k++) {
    const Species& sp = sSpecies[static_cast<size_t>(pFirst + k)];
    const float y = 82.f + 24.f * static_cast<float>(k);
    const bool on = pFirst + k == pCur;
    if (on) { kit::RowSel(dl, sc, 21.f, 174.f, y, 22.f, true); kit::Cursor(dl, sc, 15.f, y + 11.f, 0.7f); }
    char num[16]; std::snprintf(num, sizeof num, "#%03d", sp.national);
    kit::Text(dl, sc, 31.f, y + 5.f, num, kit::kSoft, 0.55f);
    std::string up = sp.name;
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    kit::Text(dl, sc, 64.f, y + 4.f, up, on ? kit::kText : kit::kSoft, 0.66f, 70.f, 1);
    auto pin = s.choice.looks.overrides.find(sp.national);
    if (pin != s.choice.looks.overrides.end()) kit::Text(dl, sc, 150.f, y + 5.f, pin->second == "xd" ? "XD" : pin->second == "br" ? "BR" : "UM", kit::kGold, 0.5f);
  }
  const Species* cur = n > 0 ? &sSpecies[static_cast<size_t>(std::clamp(pCur, 0, n - 1))] : nullptr;
  kit::Window(dl, sc, 190.f, 75.f, 625.f, 335.f);
  if (cur != nullptr) {
    std::string up = cur->name;
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    kit::Text(dl, sc, 205.f, 82.f, up, kit::kText, 0.9f, 150.f, 1);
    kit::Text(dl, sc, 360.f, 86.f, Tr("left / right: pick the source"), kit::kSoft, 0.6f, 250.f);
    const std::vector<std::string> srcs = PinSources();
    auto pin = s.choice.looks.overrides.find(cur->national);
    for (size_t i = 0; i < srcs.size(); i++) {
      const float x = 205.f + 102.f * static_cast<float>(i);
      const bool ready = srcs[i] != "xy" && SourceReady(srcs[i]);
      const bool pinned = pin != s.choice.looks.overrides.end() && pin->second == srcs[i];
      const bool sel = pSource == static_cast<int>(i);
      dl->AddRectFilled(sc.At(x, 106.f), sc.At(x + 94.f, 220.f), IM_COL32(0x10, 0x10, 0x14, 0xFF), 6.f * sc.s);
      dl->AddRect(sc.At(x, 106.f), sc.At(x + 94.f, 220.f), pinned ? IM_COL32(0xE0, 0x20, 0x28, 0xFF) : sel ? IM_COL32(0xFF, 0xD8, 0x68, kit::Pulse()) : IM_COL32(0x60, 0x60, 0x68, 0xFF), 6.f * sc.s, 0, 2.f * sc.s);
      const CardPicture& pic = ready ? CardPictureFor(srcs[i], cur->national) : sCardPictures["none"];
      if (pic.id) {
        const float k = std::min(86.f / pic.w, 72.f / pic.h);
        const float pw = pic.w * k, ph = pic.h * k;
        dl->AddImage(pic.id, sc.At(x + 47.f - pw / 2.f, 109.f + (72.f - ph) / 2.f), sc.At(x + 47.f + pw / 2.f, 109.f + (72.f + ph) / 2.f));
      } else {
        kit::TextC(dl, sc, x, 140.f, 94.f, ready ? "" : "?", kit::kDim, 2.f);
      }
      kit::Text(dl, sc, x + 6.f, 182.f, tx::SourceShort(srcs[i]), ready ? kit::kText : kit::kDim, 0.6f, 84.f, 1);
      kit::Text(dl, sc, x + 6.f, 199.f, pinned ? Tr("pinned") : srcs[i] == "xy" ? Tr("later") : ready ? Tr("ready") : tx::SourceAdd(srcs[i]), pinned ? kit::kGold : kit::kDim, 0.55f, 84.f);
    }
    const std::string style = std::string(tx::StyleName(StyleOf(s.choice.looks)));
    std::string t = pin != s.choice.looks.overrides.end()
                        ? Fmt(Tr("Pinned: %s always uses %s."), cur->name.c_str(), tx::SourceName(pin->second))
                        : Fmt(Tr("%s follows your style (%s)."), cur->name.c_str(), style.c_str());
    kit::Para(dl, sc, 205.f, 232.f, 405.f, t, kit::kText, 0.68f, 15.f, 2);
    kit::Para(dl, sc, 205.f, 266.f, 405.f, sPinNote.empty() ? Tr("The pictures on the cards are drawn from your own discs and dumps; where there is none yet, make them with tools/looks_pictures.py.") : sPinNote, kit::kSoft, 0.58f, 13.f, 3);
  }
  kit::Window(dl, sc, 190.f, 345.f, 625.f, 420.f);
  kit::Para(dl, sc, 205.f, 354.f, 405.f, Tr("A pin only changes how that one Pokemon looks, never how it battles. Y on a pinned Pokemon removes the pin."), kit::kSoft, 0.62f, 14.f, 4);
  kit::HintBar(dl, sc, Tr("Up / down: Pokemon   Left / right: source   A: pin   Y: unpin   L: page up   R: page down   B: back"));
}

int FrameView(const PortMenuInput* in) {
  const bool up = (in->repeat & PAD_BUTTON_UP) != 0, down = (in->repeat & PAD_BUTTON_DOWN) != 0;
  const bool left = (in->repeat & PAD_BUTTON_LEFT) != 0, right = (in->repeat & PAD_BUTTON_RIGHT) != 0;
  const bool a = (in->pressed & PAD_BUTTON_A) != 0, b = (in->pressed & PAD_BUTTON_B) != 0;
  int ev = 0;
  if (b) {
    if (s.view == kPins) { s.view = kLooks; return kSoundBack; }
    vCur = 0; s.view = kMain; return kSoundBack;
  }
  if (s.view == kRuleSet) {
    const int n = 5 + static_cast<int>(DetailRows().size());
    if (vCur >= n) vCur = n - 1;
    if (up) vCur = (vCur + n - 1) % n, ev = kSoundMove;
    if (down) vCur = (vCur + 1) % n, ev = kSoundMove;
    const std::vector<std::string> ids = RuleSetIds();
    if (vCur < 5 && (a || left || right)) {
      if (a) { SetRuleSet(ids[static_cast<size_t>(vCur)]); ev = kSoundChoose; }
    } else if (vCur >= 5 && DetailRows()[static_cast<size_t>(vCur - 5)] == 5 && (a || left || right)) {
      bool ch = false; SplitValue(ch);
      if (ch) {
        std::string o = s.choice.mechanics.count(xd::rulesets::kSplit) ? s.choice.mechanics[xd::rulesets::kSplit] : "player";
        o = o == "player" ? (right || a ? "on" : "off") : o == "on" ? (left ? "player" : "off") : (left ? "on" : "player");
        if (o == "player") s.choice.mechanics.erase(xd::rulesets::kSplit); else s.choice.mechanics[xd::rulesets::kSplit] = o;
        ev = kSoundMove;
      }
    } else if (vCur >= 5 && DetailRows()[static_cast<size_t>(vCur - 5)] == 6 && (a || left || right)) {
      const bool on = s.choice.MechanicOn(xd::rulesets::Registry(), xd::rulesets::kModernTypes);
      s.choice.mechanics[xd::rulesets::kModernTypes] = left ? "on" : right ? "off" : (on ? "off" : "on");
      ev = kSoundMove;
    }
  } else if (s.view == kPokemon) {
    if (up) vCur = (vCur + 5) % 6, ev = kSoundMove;
    if (down) vCur = (vCur + 1) % 6, ev = kSoundMove;
    if (vCur < 5 && a) { SetGens(vCur + 3); ev = kSoundChoose; }
    if (vCur == 5 && (a || left || right) && s.choice.HasGen(4)) {
      s.choice.SetOption(xd::rulesets::kOptionSpots, !s.choice.HasOption(xd::rulesets::kOptionSpots));
      ev = kSoundMove;
    }
    if (vCur < 5 && (left || right)) { SetGens(NextTop(left)); ev = kSoundMove; }
  } else if (s.view == kLooks) {
    const int n = LooksRows();
    if (up) vCur = (vCur + n - 1) % n, ev = kSoundMove;
    if (down) vCur = (vCur + 1) % n, ev = kSoundMove;
    if (vCur < 4 && a) { SetStyle(kStyles[vCur]); ev = kSoundChoose; }
    else if (vCur >= 4 && vCur < PinsRow()) {
      const std::vector<std::string> src = SourceRows();
      const std::string id = src[static_cast<size_t>(vCur - 4)];
      if (StyleOf(s.choice.looks) == "custom" && id != "xy") {
        std::vector<std::string> list = s.choice.looks.sources;
        auto it = std::find(list.begin(), list.end(), id);
        if (a) {
          if (it != list.end() && id != "xd") list.erase(it);
          else if (it == list.end() && SourceReady(id)) list.insert(list.begin(), id);
          s.choice.looks.sources = list; ev = kSoundChoose;
        } else if (in->pressed & PAD_BUTTON_X) {
          if (it != list.end() && it != list.begin()) { std::iter_swap(it, it - 1); vCur--; ev = kSoundMove; }
          s.choice.looks.sources = list;
        } else if (in->pressed & PAD_BUTTON_Y) {
          if (it != list.end() && it + 1 != list.end()) { std::iter_swap(it, it + 1); vCur++; ev = kSoundMove; }
          s.choice.looks.sources = list;
        }
      }
    } else if (vCur == PinsRow() && a) {
      s.view = kPins; ev = kSoundChoose;
    }
  } else if (s.view == kPins) {
    BuildSpecies();
    const int n = static_cast<int>(sSpecies.size());
    if (n > 0) {
      if (up) pCur = (pCur + n - 1) % n, ev = kSoundMove;
      if (down) pCur = (pCur + 1) % n, ev = kSoundMove;
      if (in->pressed & PAD_TRIGGER_L) pCur = std::max(0, pCur - 14), ev = kSoundMove;
      if (in->pressed & PAD_TRIGGER_R) pCur = std::min(n - 1, pCur + 14), ev = kSoundMove;
      if (in->pressed & PAD_BUTTON_X) {
        const char c0 = sSpecies[static_cast<size_t>(pCur)].name.empty() ? 'A' : sSpecies[static_cast<size_t>(pCur)].name[0];
        for (int k = 1; k < n; k++) { const auto& nm = sSpecies[static_cast<size_t>((pCur + k) % n)].name; if (!nm.empty() && nm[0] != c0) { pCur = (pCur + k) % n; break; } }
        ev = kSoundMove;
      }
      if (left) pSource = (pSource + 3) % 4, ev = kSoundMove;
      if (right) pSource = (pSource + 1) % 4, ev = kSoundMove;
      const int nat = sSpecies[static_cast<size_t>(pCur)].national;
      const std::vector<std::string> srcs = PinSources();
      if (a) {
        const std::string id = srcs[static_cast<size_t>(pSource)];
        if (id == "xy" || !SourceReady(id)) sPinNote = Fmt(Tr("%s is not on this PC: add it in the launcher first."), tx::SourceName(id));
        else { s.choice.looks.overrides[nat] = id; sPinNote.clear(); if (!s.choice.looks.Says()) s.choice.looks.sources = {"xd"}; }
        ev = kSoundChoose;
      }
      if (in->pressed & PAD_BUTTON_Y) { s.choice.looks.overrides.erase(nat); sPinNote.clear(); ev = kSoundMove; }
    }
  }
  return ev;
}

void DrawView(ImDrawList* dl) {
  const Screen sc = ScreenNow();
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  switch (s.view) {
  case kRuleSet: DrawRuleSet(dl, sc); break;
  case kPokemon: DrawPokemon(dl, sc); break;
  case kLooks: DrawLooks(dl, sc); break;
  case kPins: DrawPins(dl, sc); break;
  default: break;
  }
}

}

struct Refusal { std::string head, red, soft, who; } sRefusal;

bool RefusalFrame(void*, const PortMenuInput* in) {
  if (in->pressed & (PAD_BUTTON_A | PAD_BUTTON_B)) { PlayMenuSound(kTitleMenu, kSoundChoose); return false; }
  if (sRefusal.head.empty()) sRefusal = {Tr("This playthrough has Gen 4 Pokemon."), Tr("Add your Battle Revolution disc or your Ultra Sun / Ultra Moon dump in the launcher (Extra assets) to continue it."), Tr("Your save is safe and unchanged. It needs the Gen 4 pack because the Pokemon in it come from there."), ""};
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Screen sc = ScreenNow();
  if (PreviewOn()) {
    kit::Stage(dl, sc);
    kit::MessageWindow(dl, sc, 27.f, 316.f, 613.f, 440.f);
    std::vector<std::string> lines;
    {
      const UseFont f(0);
      lines = Wrap(sRefusal.head + " " + sRefusal.red + " " + sRefusal.soft, 540.f);
    }
    const int pages = std::max(1, (static_cast<int>(lines.size()) + 2) / 3);
    static float age = 0.f;
    age += 1.f / 60.f;
    const int page = pages > 1 ? static_cast<int>(age / 3.6f) % pages : 0;
    for (int k = 0; k < 3; k++) {
      const size_t li = static_cast<size_t>(page * 3 + k);
      if (li < lines.size()) kit::Text(dl, sc, 52.f, 331.f + 33.f * static_cast<float>(k), lines[li], kit::kText, 1.f, 0.f, 0);
    }
    if (pages > 1) DrawTriangle(dl, sc, 586.f, 424.f + 2.f * std::sin(kit::Time() * 6.28f), 12.f, 1, IM_COL32(0xF0, 0xF0, 0xF0, 0xE0));
    return true;
  }
  FillWindow(dl, IM_COL32(0, 0, 0, 255));
  kit::Backdrop(dl, sc);
  kit::Header(dl, sc, Tr("CONTINUE"), sRefusal.who);
  kit::Window(dl, sc, 100.f, 100.f, 540.f, 290.f, true);
  kit::Para(dl, sc, 122.f, 118.f, 400.f, sRefusal.head, kit::kText, 0.85f, 20.f, 2);
  kit::Para(dl, sc, 122.f, 152.f, 400.f, sRefusal.red, kit::kBad, 0.8f, 21.f, 3);
  kit::Para(dl, sc, 122.f, 214.f, 400.f, sRefusal.soft, kit::kDim, 0.68f, 16.f, 4);
  kit::Button(dl, sc, 245.f, 322.f, 150.f, Tr("OK"), true);
  kit::Cursor(dl, sc, 232.f, 337.f, 0.8f);
  kit::HintBar(dl, sc, Tr("A: OK"));
  return true;
}
const PortMenuPage kRefusalPage{"continue refusal", nullptr, RefusalFrame, nullptr, nullptr};

