namespace ot {

enum class Step { Pick, Menu, Typing, Connect, Trade, Result };

struct SlotInfo {
  std::string name;
  std::filesystem::path root;
  bool ok = false;
  std::string trainer, why;
  double seconds = 0;
};

constexpr const char* kAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
constexpr int kKeyCols = 8, kCodeLen = 6;

struct PageState {
  Step step = Step::Menu;
  std::vector<SlotInfo> slots;
  int slotCursor = 0;
  std::string slotName;
  xd::trade::Config cfg;
  int menuCursor = 0;
  std::string typed;
  int key = 0;
  std::string message;
  std::vector<std::string> notes;
  std::unique_ptr<xd::legality::DiscRules> rules;
  std::unique_ptr<xd::legality::DiscRules> originalRules;
  std::unique_ptr<xd::trade::Trader> trader;
  std::unique_ptr<xd::trade::OnlineTrade> net;
  xd::trade::CardSave card;
  int cursor = 0, top = 0;
  int yesNo = 1;
  float age = 0.f;
  bool traded = false;
  bool live = false;
};
PageState P;
bool sLive = false;

const uint8_t kWhite[4] = {0xF0, 0xF0, 0xF0, 0xFF};
const uint8_t kGrey[4] = {0x88, 0x88, 0x88, 0xFF};
const uint8_t kGold[4] = {0xFF, 0xE0, 0x70, 0xFF};
const uint8_t kGreen[4] = {0x90, 0xE8, 0x90, 0xFF};
const uint8_t kRed[4] = {0xFF, 0x90, 0x80, 0xFF};

std::string F1(const char* fmt, const std::string& a) {
  char b[512];
  std::snprintf(b, sizeof(b), fmt, a.c_str());
  return b;
}

std::string TrMsg(const std::string& m) {
  if (m == "There is no Pokemon there.") return Tr("There is no Pokemon there.");
  if (m == "A Shadow Pokemon can't be traded until it is purified.") return Tr("A Shadow Pokemon can't be traded until it is purified.");
  if (m == "Eggs can't be traded.") return Tr("Eggs can't be traded.");
  if (m == "Your friend's Pokemon could not be read.") return Tr("Your friend's Pokemon could not be read.");
  if (m == "No save is loaded.") return Tr("No save is loaded.");
  if (m == "The Pokemon you offered is no longer in your save.") return Tr("The Pokemon you offered is no longer in your save.");
  if (m == "The Pokemon you offered is no longer in your game.") return Tr("The Pokemon you offered is no longer in your game.");
  if (m == "The save is missing a section the trade needs.") return Tr("The save is missing a section the trade needs.");
  if (m == "After this trade you would have no Pokemon in your party that can battle.")
    return Tr("After this trade you would have no Pokemon in your party that can battle.");
  if (m == "The new save could not be built.") return Tr("The new save could not be built.");
  if (m == "The new save did not hold the Pokemon it should.") return Tr("The new save did not hold the Pokemon it should.");
  if (m == "Nothing is prepared.") return Tr("Nothing is prepared.");
  if (m == "The save could not be written.") return Tr("The save could not be written.");
  if (m == "The game's tables are not ready.") return Tr("The game's tables are not ready.");
  if (m == "The save could not be read in the GameCube order.") return Tr("The save could not be read in the GameCube order.");
  if (m == "There is no Pokemon XD save here yet. Play and save once first.") return Tr("There is no Pokemon XD save here yet. Play and save once first.");
  if (m == "The new save has the wrong size.") return Tr("The new save has the wrong size.");
  if (m == "The backup copy of your save could not be made, so nothing was changed.")
    return Tr("The backup copy of your save could not be made, so nothing was changed.");
  if (m == "The new save could not be written.") return Tr("The new save could not be written.");
  if (m == "The save could not be replaced.") return Tr("The save could not be replaced.");
  const std::string pre = "The new save did not pass its checks: ";
  if (m.compare(0, pre.size(), pre) == 0) return F1(Tr("The new save did not pass its checks: %s"), m.substr(pre.size()));
  const std::string pre2 = "The old save file can't be read again: ";
  if (m.compare(0, pre2.size(), pre2) == 0) return F1(Tr("The old save file can't be read again: %s"), m.substr(pre2.size()));
  return m;
}

std::string TrWhere(const std::string& w) {
  int a = 0, b = 0;
  char out[128];
  if (std::sscanf(w.c_str(), "party slot %d", &a) == 1) {
    std::snprintf(out, sizeof(out), Tr("party slot %d"), a);
    return out;
  }
  if (std::sscanf(w.c_str(), "PC box %d, place %d", &a, &b) == 2) {
    std::snprintf(out, sizeof(out), Tr("PC box %d, place %d"), a, b);
    return out;
  }
  return w;
}

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
  kit::Window(dl, sc, x0, y0, x1, y1, bright);
}

void Keyboard(ImDrawList* dl, const Screen& sc, int key, bool full, float top) {
  kit::Window(dl, sc, 90.f, top, 550.f, top + 214.f);
  for (int k = 0; k < 34; k++) {
    if (k == 32) continue;
    const bool sel = k == key;
    if (k == 33) {
      kit::Button(dl, sc, 240.f, top + 170.f, 160.f, Tr("JOIN"), sel, full);
      continue;
    }
    const float x = 112.f + 52.f * static_cast<float>(k % kKeyCols), y = top + 16.f + 38.f * static_cast<float>(k / kKeyCols);
    const std::string label = k < 31 ? std::string(1, kAlphabet[k]) : std::string(Tr("DEL"));
    if (sel) kit::RowSel(dl, sc, x - 2.f, x + 46.f, y - 3.f, 30.f, false);
    kit::Oval(dl, sc, x, y, 44.f, sel, sel ? 255 : 150);
    kit::TextC(dl, sc, x + 2.f, y + 1.f, 40.f, label, sel ? kit::kText : kit::kSoft, 0.85f, 1);
  }
}

void CodeLine(ImDrawList* dl, const Screen& sc, float y, const std::string& code, float scale = 1.6f) {
  std::string shown;
  for (size_t i = 0; i < code.size(); i++) {
    if (i == 3 && code.size() == 6) shown += " - ";
    shown += code[i];
    shown += ' ';
  }
  kit::TextC(dl, sc, 90.f, y, 460.f, shown, kit::kGold, scale, 1);
}

void Wrapped(ImDrawList* dl, const Screen& sc, float x, float y, float w, const std::string& text, const uint8_t* col,
             float scale, float pitch, int maxLines) {
  const std::vector<std::string> lines = Wrap(text, w / scale);
  for (int i = 0; i < static_cast<int>(lines.size()) && i < maxLines; i++)
    Say(dl, sc, x, y + pitch * static_cast<float>(i), lines[static_cast<size_t>(i)], col, scale);
}

void DrawCard(ImDrawList* dl, const Screen& sc, float x, float y, float w, const xd::trade::Card& c, const char* empty) {
  if (c.species.empty()) {
    Say(dl, sc, x, y + 8.f, empty, kGrey, 0.8f, w);
    return;
  }
  const float s = 0.68f, p = 15.f;
  std::string head = c.name;
  if (c.name != c.species) head += " (" + c.species + ")";
  Say(dl, sc, x, y, head + "  " + std::string(Tr("Lv")) + " " + std::to_string(c.level) + (c.shiny ? std::string("  ") + Tr("SHINY") : std::string()), kGold, 0.8f, w);
  float yy = y + 22.f;
  auto line = [&](const std::string& t, const uint8_t* col = kWhite) { Say(dl, sc, x, yy, t, col, s, w); yy += p; };
  {
    char b[256];
    std::snprintf(b, sizeof(b), Tr("Nature: %s     Ability: %s"), c.nature.empty() ? "-" : c.nature.c_str(), c.ability.empty() ? "-" : c.ability.c_str());
    line(b);
    std::snprintf(b, sizeof(b), Tr("Held item: %s"), c.item.empty() ? Tr("none") : c.item.c_str());
    line(b);
    std::snprintf(b, sizeof(b), Tr("HP %d/%d     OT: %s (%d)"), static_cast<int>(c.hp), static_cast<int>(c.maxHp), c.ot.c_str(),
                  static_cast<int>(c.otId & 0xFFFF));
    line(b);
  }
  for (int i = 0; i < 4; i++) line(std::to_string(i + 1) + "  " + (c.moves[i].empty() ? "-" : c.moves[i]), c.moves[i].empty() ? kGrey : kWhite);
}

void Fail(const std::string& why) {
  std::fprintf(stderr, "[online trade] %s\n", why.c_str());
  P.step = Step::Result;
  P.message = why;
}

void MakeNet() {
  P.net = std::make_unique<xd::trade::OnlineTrade>(*P.trader, [](const xd::trade::Bytes& nf, std::string& e) {
    if (P.live) return true;
    return xd::trade::WriteCardSave(P.card, nf, e);
  });
  P.net->SetEdition(xd::xg::On());
  P.net->SetLog([](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); });
  if (const char* direct = std::getenv("XD_ONLINE_DIRECT")) {
    xdnet::ConnectionConfig nc = xdnet::Connection::defaultConfig();
    nc.directPeers.push_back(direct);
    if (const char* lp = std::getenv("XD_ONLINE_LOCAL_PORT")) nc.localPort = static_cast<uint16_t>(std::atoi(lp));
    if (std::getenv("XD_ONLINE_NO_GO_BETWEEN")) nc.useGoBetween = false;
    P.net->SetNetConfig(nc);
  }
}

void ChooseSlot(int i);

void Open(void*) {
  sOnlineKind = 2;
  const bool live = sLive;
  sLive = false;
  P = PageState{};
  P.live = live;
  std::string err;
  P.rules = std::make_unique<xd::legality::DiscRules>();
  bool openedDisc = false;
  {
    char derr[256] = {};
    if (!PortDisc_Open(PortXD_DiscPath(), derr, sizeof(derr))) return Fail(F1(Tr("The disc could not be read: %s"), derr));
    openedDisc = true;
  }
  const bool rulesOk = xd::legality::LoadRulesFromRunningGame(*P.rules, err);
  if (openedDisc) PortDisc_Close();
  if (!rulesOk) return Fail(F1(Tr("The game's Pokemon tables could not be read: %s"), err));
  if (P.live || PortXD_LiveCommonTables() != nullptr) P.rules->names->SetLiveText(&PortXD_LiveMessage);
  P.cfg.rules = &P.rules->rules;
  P.cfg.names = P.rules->names.get();
  P.cfg.original = !xd::challenges::Active("gen4_pokemon") && xd::pt::LoadedRandomizer().empty() && PortDisc_OverlayCount() == 0 &&
                   std::getenv("XD_TRADE_ENHANCED") == nullptr;
  if (P.cfg.original) {
    P.originalRules = std::make_unique<xd::legality::DiscRules>();
    char derr[256] = {};
    if (PortDisc_Open(PortXD_DiscPath(), derr, sizeof(derr))) {
      std::string e2;
      const bool ok = xd::legality::LoadRulesOriginalInRunningGame(*P.originalRules, e2);
      PortDisc_Close();
      if (ok) P.cfg.originalRules = &P.originalRules->rules;
      else std::fprintf(stderr, "[online trade] the original tables could not be read: %s\n", e2.c_str());
    }
  }
  if (P.live) {
    P.trader = PortXD_MakeLiveTrader(P.cfg);
    P.slotName = "game";
    MakeNet();
    P.step = Step::Menu;
    return;
  }
  namespace sl = engine::memcard::slots;
  std::vector<std::pair<std::string, std::filesystem::path>> roots;
  if (xd::SaveSlotsInUse()) {
    for (const sl::Slot& slot : sl::Load(xd::UserFolder()).slots) roots.push_back({slot.name, sl::RootOf(xd::UserFolder(), slot.id)});
  } else {
    roots.push_back({"Save", xd::CardRoot()});
  }
  for (const auto& r : roots) {
    SlotInfo si;
    si.name = r.first;
    si.root = r.second;
    xd::trade::CardSave cs;
    std::string e2;
    xd::trade::Trader tr(P.cfg);
    if (!xd::trade::LoadCardSave(r.second, cs, e2)) si.why = Tr("No Pokemon XD save in it yet.");
    else if (!tr.Load(cs.data, e2)) si.why = F1(Tr("Can't be read: %s"), TrMsg(e2));
    else si.ok = true, si.trainer = tr.trainerName(), si.seconds = tr.save().playTimeSeconds;
    P.slots.push_back(si);
  }
  int usable = 0, only = 0;
  for (size_t i = 0; i < P.slots.size(); i++) if (P.slots[i].ok) usable++, only = static_cast<int>(i);
  if (usable == 0) return Fail(P.slots.empty() ? std::string(Tr("There is no save.")) : P.slots[0].why + " " + Tr("Play and save once first."));
  if (P.slots.size() == 1 || usable == 1) return ChooseSlot(only);
  P.step = Step::Pick;
  P.slotCursor = only;
}

void ChooseSlot(int i) {
  std::string err;
  P.slotName = P.slots[static_cast<size_t>(i)].name;
  if (!xd::trade::LoadCardSave(P.slots[static_cast<size_t>(i)].root, P.card, err)) return Fail(err);
  P.trader = std::make_unique<xd::trade::Trader>(P.cfg);
  if (!P.trader->Load(P.card.data, err)) return Fail(F1(Tr("This save can't be traded from: %s"), TrMsg(err)));
  P.step = Step::Menu;
  P.menuCursor = 0;
  MakeNet();
}

void Close(void*) {
  sOnlineKind = 0;
  if (P.net) P.net->Leave();
  P.net.reset();
  P.trader.reset();
}

void Start(bool host) {
  if (const char* fixed = std::getenv("XD_ONLINE_CODE")) {
    P.net->StartJoin(fixed);
  } else if (host) {
    P.net->StartHost();
  } else {
    if (!P.net->StartJoin(P.typed)) return;
  }
  P.step = Step::Connect;
}

const char* IpWarning() { return Tr("Warning: playing online exposes your internet address (IP) to the other player."); }

bool StepPick(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const int n = static_cast<int>(P.slots.size());
  if (in->repeat & PAD_BUTTON_UP) P.slotCursor = (P.slotCursor + n - 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_DOWN) P.slotCursor = (P.slotCursor + 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->pressed & PAD_BUTTON_B) return PlayMenuSound(kTitleMenu, kSoundBack), false;
  if ((in->pressed & PAD_BUTTON_A)) {
    if (P.slots[static_cast<size_t>(P.slotCursor)].ok) { PlayMenuSound(kTitleMenu, kSoundChoose); ChooseSlot(P.slotCursor); return true; }
    PlayMenuSound(kTitleMenu, kSoundBack);
  }
  kit::Header(dl, sc, Tr("ONLINE TRADE"));
  kit::TitledWindow(dl, sc, 60.f, 80.f, 580.f, 420.f, Tr("Which save do you want to trade from?"));
  const int top = std::max(0, std::min(P.slotCursor - 4, n - 8));
  for (int r = 0; r < 8 && top + r < n; r++) {
    const int i = top + r;
    const SlotInfo& s = P.slots[static_cast<size_t>(i)];
    const float y = 122.f + 36.f * static_cast<float>(r);
    const bool sel = i == P.slotCursor;
    if (sel) kit::RowSel(dl, sc, 68.f, 572.f, y, 32.f);
    char play[32] = "";
    if (s.ok) std::snprintf(play, sizeof(play), "%d:%02d", static_cast<int>(s.seconds) / 3600, static_cast<int>(s.seconds) / 60 % 60);
    kit::Text(dl, sc, 96.f, y + 6.f, s.name, !s.ok ? kit::kDim : sel ? kit::kText : kit::kSoft, 0.85f, 180.f, 1);
    kit::Text(dl, sc, 290.f, y + 7.f, s.ok ? s.trainer : s.why, s.ok ? kit::kGold : kit::kDim, 0.8f, 200.f);
    kit::Text(dl, sc, 505.f, y + 7.f, play, kit::kSoft, 0.8f);
  }
  kit::HintBar(dl, sc, Tr("A: choose   B: back"));
  return true;
}

bool StepMenu(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const char* items[3] = {Tr("Make a room"), Tr("Join a room"), Tr("Back")};
  if (in->repeat & PAD_BUTTON_UP) P.menuCursor = (P.menuCursor + 2) % 3, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_DOWN) P.menuCursor = (P.menuCursor + 1) % 3, PlayMenuSound(kTitleMenu, kSoundMove);
  const bool canPick = P.slots.size() > 1;
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    if (!canPick) return false;
    P.net.reset(), P.trader.reset(), P.step = Step::Pick;
    return true;
  }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (P.menuCursor == 0) Start(true);
    else if (P.menuCursor == 1) P.step = Step::Typing, P.typed.clear(), P.key = 0;
    else return false;
  }
  kit::Header(dl, sc, Tr("ONLINE TRADE"));
  kit::Window(dl, sc, 100.f, 82.f, 540.f, 162.f);
  kit::Text(dl, sc, 124.f, 98.f, Tr("Trade Pokemon with a friend over the internet."), kit::kText, 0.8f, 392.f);
  {
    char from[256];
    if (P.live) std::snprintf(from, sizeof(from), "%s", Tr("Trading from: your game"));
    else if (P.slots.size() > 1) std::snprintf(from, sizeof(from), Tr("Trading from: %s (%s)"), P.trader->trainerName().c_str(), P.slotName.c_str());
    else std::snprintf(from, sizeof(from), Tr("Trading from: %s's save"), P.trader->trainerName().c_str());
    kit::Text(dl, sc, 124.f, 126.f, from, kit::kGold, 0.75f, 392.f);
  }
  for (int i = 0; i < 3; i++) kit::Button(dl, sc, 210.f, 184.f + 46.f * static_cast<float>(i), 220.f, items[i], i == P.menuCursor);
  kit::Window(dl, sc, 60.f, 340.f, 580.f, 436.f);
  kit::Para(dl, sc, 86.f, 351.f, 468.f, IpWarning(), kit::kBad, 0.76f, 19.f, 2);
  kit::Para(dl, sc, 86.f, 393.f, 468.f, Tr("Your friend needs the same game and this screen too."), kit::kDim, 0.66f, 16.f, 2);
  kit::HintBar(dl, sc, Tr("A: choose   B: back"));
  return true;
}

void TypeSymbol(char c) {
  if (static_cast<int>(P.typed.size()) < kCodeLen) P.typed += c;
}

bool StepTyping(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (ImGui::GetCurrentContext() != nullptr) {
    for (ImWchar ch : ImGui::GetIO().InputQueueCharacters) {
      const char u = static_cast<char>(std::toupper(static_cast<int>(ch)));
      if (ch < 128 && std::strchr(kAlphabet, u) != nullptr) TypeSymbol(u);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !P.typed.empty()) P.typed.pop_back();
  }
  const int nkeys = 31;
  int col = P.key % kKeyCols, row = P.key / kKeyCols;
  if (P.key < 32) {
    if (in->repeat & PAD_BUTTON_LEFT) P.key = std::max(0, P.key - 1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_RIGHT) P.key = std::min(31, P.key + 1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_UP) P.key = row > 0 ? P.key - kKeyCols : P.key, PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_DOWN) P.key = (row < 3 && P.key + kKeyCols <= 32) ? P.key + kKeyCols : 33, PlayMenuSound(kTitleMenu, kSoundMove);
  } else {
    if (in->repeat & PAD_BUTTON_UP) P.key = 24, PlayMenuSound(kTitleMenu, kSoundMove);
  }
  (void)col;
  if (in->pressed & PAD_BUTTON_B) return PlayMenuSound(kTitleMenu, kSoundBack), P.step = Step::Menu, true;
  if (in->pressed & PAD_BUTTON_X) { if (!P.typed.empty()) P.typed.pop_back(); }
  const bool full = static_cast<int>(P.typed.size()) == kCodeLen;
  if (((in->pressed & PAD_BUTTON_START) || ((in->pressed & PAD_BUTTON_A) && P.key == 33)) && full) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    Start(false);
    return true;
  }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (P.key < nkeys) TypeSymbol(kAlphabet[P.key]);
    else if (P.key == 31 && !P.typed.empty()) P.typed.pop_back();
    else if (P.key == 32 && full) Start(false);
  }
  kit::Header(dl, sc, Tr("ONLINE TRADE"));
  kit::TitledWindow(dl, sc, 90.f, 76.f, 550.f, 150.f, Tr("Type your friend's room code"));
  std::string shown;
  for (int i = 0; i < kCodeLen; i++) shown += i < static_cast<int>(P.typed.size()) ? P.typed[static_cast<size_t>(i)] : '_';
  CodeLine(dl, sc, 114.f, shown, 1.3f);
  Keyboard(dl, sc, P.key, full, 158.f);
  kit::Window(dl, sc, 90.f, 380.f, 550.f, 436.f);
  kit::Para(dl, sc, 114.f, 392.f, 412.f, IpWarning(), kit::kBad, 0.7f, 17.f, 2);
  kit::HintBar(dl, sc, Tr("D-pad: choose  A: type  X: delete  START: join  B: back"));
  (void)nkeys;
  return true;
}

bool StepConnect(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    P.net->Leave();
    MakeNet();
    P.step = Step::Menu;
    return true;
  }
  const xdnet::ConnectionStatus st = P.net->connection();
  kit::Header(dl, sc, Tr("ONLINE TRADE"));
  const bool over = P.net->phase() == xd::trade::Phase::Over;
  kit::Window(dl, sc, 90.f, 80.f, 550.f, 336.f, over);
  if (!P.net->code().empty()) {
    kit::TextC(dl, sc, 90.f, 98.f, 460.f, Tr("Room code (tell your friend):"), kit::kText, 0.8f);
    CodeLine(dl, sc, 126.f, P.net->code(), 1.9f);
  }
  if (!over) kit::Ring(dl, sc, 150.f, 250.f, 26.f);
  kit::Para(dl, sc, over ? 116.f : 196.f, 214.f, over ? 410.f : 330.f, P.net->connectionText(), over ? kit::kBad : kit::kText, 0.85f, 24.f, 4);
  kit::Window(dl, sc, 90.f, 346.f, 550.f, 432.f);
  kit::Para(dl, sc, 114.f, 362.f, 412.f, IpWarning(), kit::kBad, 0.78f, 21.f, 3);
  kit::HintBar(dl, sc, over ? Tr("Press B to go back.") : Tr("B: cancel"));
  return true;
}

std::string EntryLabel(const xd::trade::Entry& e) {
  std::string where = e.box < 0 ? "P" + std::to_string(e.slot + 1) : "B" + std::to_string(e.box + 1) + "-" + std::to_string(e.slot + 1);
  return where + "  " + e.card.name + "  Lv" + std::to_string(e.card.level);
}

constexpr int kTradeRows = 9;
bool StepTrade(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  using namespace xdnet::trade;
  TradeSession* s = P.net->session();
  const auto& entries = P.net->entries();
  const int n = static_cast<int>(entries.size());
  const bool confirming = s->state() == State::Confirming;
  if (confirming) {
    if (in->pressed & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) P.yesNo ^= 1, PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->pressed & PAD_BUTTON_B) { P.net->Answer(false); PlayMenuSound(kTitleMenu, kSoundBack); P.yesNo = 1; }
    else if ((in->pressed & PAD_BUTTON_A) && !s->iConfirmed()) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      P.net->Answer(P.yesNo == 0);
      P.yesNo = 1;
    }
  } else if (s->state() == State::Negotiating) {
    if (n > 0) {
      if (in->repeat & PAD_BUTTON_UP) P.cursor = (P.cursor + n - 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
      if (in->repeat & PAD_BUTTON_DOWN) P.cursor = (P.cursor + 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
      if (P.cursor < P.top) P.top = P.cursor;
      if (P.cursor >= P.top + kTradeRows) P.top = P.cursor - kTradeRows + 1;
    }
    if ((in->pressed & PAD_BUTTON_A) && n > 0) {
      if (P.net->offeredIndex() == P.cursor) P.net->Withdraw();
      else if (!P.net->Offer(P.cursor)) PlayMenuSound(kTitleMenu, kSoundBack);
      else PlayMenuSound(kTitleMenu, kSoundChoose);
    }
    if (in->pressed & PAD_BUTTON_Y) P.net->Withdraw();
    if (in->pressed & PAD_BUTTON_X) {
      if (s->iAmReady()) P.net->Ready(false);
      else if (!P.net->Ready(true)) PlayMenuSound(kTitleMenu, kSoundBack);
      else PlayMenuSound(kTitleMenu, kSoundChoose);
    }
    if (in->pressed & PAD_BUTTON_B) {
      PlayMenuSound(kTitleMenu, kSoundBack);
      P.net->Leave();
      return true;
    }
  }
  kit::Header(dl, sc, Tr("ONLINE TRADE"), F1(Tr("Room %s"), P.net->code()));
  kit::TitledWindow(dl, sc, 12.f, 74.f, 300.f, 342.f, Tr("Your Pokemon"));
  for (int r = 0; r < kTradeRows && P.top + r < n; r++) {
    const int i = P.top + r;
    const auto& e = entries[static_cast<size_t>(i)];
    const float y = 112.f + 25.f * static_cast<float>(r);
    const bool sel = i == P.cursor;
    if (sel) kit::RowSel(dl, sc, 18.f, 294.f, y - 2.f, 24.f);
    const bool offered = P.net->offeredIndex() == i;
    kit::Text(dl, sc, 44.f, y + 1.f, EntryLabel(e), !e.ok ? kit::kDim : offered ? kit::kGold : sel ? kit::kText : kit::kSoft,
              0.75f, 226.f);
    if (offered) dl->AddCircleFilled(sc.At(280.f, y + 10.f), 4.f * sc.s, IM_COL32(0xFF, 0xD8, 0x68, 255), 12);
  }
  if (n > kTradeRows) kit::Text(dl, sc, 240.f, 84.f, std::to_string(P.cursor + 1) + "/" + std::to_string(n), kit::kSoft, 0.7f);
  if (P.top > 0) kit::Arrow(dl, sc, 156.f, 106.f, true);
  if (P.top + kTradeRows < n) kit::Arrow(dl, sc, 156.f, 336.f, false);
  kit::TitledWindow(dl, sc, 312.f, 74.f, 628.f, 250.f, Tr("Your friend offers"), !P.net->friendCard().species.empty());
  DrawCard(dl, sc, 338.f, 112.f, 258.f, P.net->friendCard(), Tr("Nothing yet."));
  kit::Window(dl, sc, 312.f, 256.f, 628.f, 342.f);
  if (n > 0 && P.cursor < n) {
    const auto& e = entries[static_cast<size_t>(P.cursor)];
    const bool offered = P.net->offeredIndex() == P.cursor;
    {
      char b[256];
      const std::string shiny = e.card.shiny ? std::string(" ") + Tr("SHINY") : std::string();
      std::snprintf(b, sizeof(b), offered ? Tr("Your offer: %s Lv%d%s") : Tr("Selected: %s Lv%d%s"), e.card.name.c_str(),
                    static_cast<int>(e.card.level), shiny.c_str());
      kit::Text(dl, sc, 338.f, 266.f, b, offered ? kit::kGold : kit::kText, 0.7f, 258.f, 1);
    }
    kit::Text(dl, sc, 338.f, 284.f, e.card.nature + "  " + e.card.ability + "  " + (e.card.item.empty() ? std::string(Tr("no item")) : e.card.item), kit::kText, 0.62f, 258.f);
    std::string mv;
    for (int i = 0; i < 4; i++) if (!e.card.moves[i].empty()) mv += (mv.empty() ? "" : ", ") + e.card.moves[i];
    kit::Text(dl, sc, 338.f, 300.f, mv, kit::kText, 0.62f, 258.f);
    {
      char b[256];
      std::snprintf(b, sizeof(b), Tr("OT %s (%d)"), e.card.ot.c_str(), static_cast<int>(e.card.otId & 0xFFFF));
      kit::Text(dl, sc, 338.f, 316.f, b, kit::kSoft, 0.62f, 258.f);
    }
  }
  kit::Window(dl, sc, 12.f, 348.f, 628.f, 438.f);
  std::string reason;
  if (n > 0 && P.cursor < n && !entries[static_cast<size_t>(P.cursor)].ok) reason = F1(Tr("Can't be traded: %s"), TrMsg(entries[static_cast<size_t>(P.cursor)].reason));
  else if (!s->theirsVerdict().ok && !s->theirs().record.empty()) reason = F1(Tr("You can't take your friend's Pokemon: %s"), TrMsg(s->theirsVerdict().text));
  else if (s->myOfferJudged() && !s->myOfferVerdict().ok) reason = F1(Tr("Your friend's game can't take your Pokemon: %s"), TrMsg(s->myOfferVerdict().text));
  if (!reason.empty()) kit::Para(dl, sc, 36.f, 360.f, 568.f, reason, kit::kBad, 0.72f, 18.f, 2);
  kit::Para(dl, sc, 36.f, 400.f, 568.f, s->status(), kit::kGood, 0.78f, 18.f, 2);
  {
    char b[512];
    std::snprintf(b, sizeof(b), Tr("A: offer / take back   X: %s   Y: take back   B: leave   (you are %s, friend %s)"),
                  s->iAmReady() ? Tr("not ready") : Tr("Ready"), s->iAmReady() ? Tr("READY") : Tr("not ready"),
                  s->friendIsReady() ? Tr("READY") : Tr("not ready"));
    kit::HintBar(dl, sc, b);
  }
  if (confirming) {
    kit::Dim(dl, 150);
    kit::Window(dl, sc, 70.f, 150.f, 570.f, 320.f, true);
    const std::string mine = P.net->offeredIndex() >= 0 ? entries[static_cast<size_t>(P.net->offeredIndex())].card.name : "?";
    const std::string theirs = P.net->friendCard().name;
    const std::string theirName = P.net->friendCard().name == P.net->friendCard().species
                                      ? theirs : theirs + " (" + P.net->friendCard().species + ")";
    {
      char b[512];
      std::snprintf(b, sizeof(b), Tr("Trade %s for %s?"), mine.c_str(), theirName.c_str());
      kit::Para(dl, sc, 100.f, 172.f, 440.f, b, kit::kText, 1.f, 28.f, 3);
    }
    if (s->iConfirmed()) {
      kit::Ring(dl, sc, 116.f, 280.f, 14.f);
      kit::Text(dl, sc, 140.f, 270.f, Tr("Waiting for your friend to answer..."), kit::kGood, 0.85f, 400.f);
    } else {
      kit::Button(dl, sc, 160.f, 262.f, 140.f, Tr("YES"), P.yesNo == 0);
      kit::Button(dl, sc, 340.f, 262.f, 140.f, Tr("NO"), P.yesNo == 1);
    }
  }
  return true;
}

bool StepResult(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (in->pressed & (PAD_BUTTON_A | PAD_BUTTON_B)) return PlayMenuSound(kTitleMenu, kSoundChoose), false;
  kit::Header(dl, sc, P.traded ? Tr("TRADE DONE") : Tr("ONLINE TRADE"));
  kit::Window(dl, sc, 90.f, 96.f, 550.f, 380.f, true);
  float y = 118.f + 28.f * static_cast<float>(kit::Para(dl, sc, 116.f, 118.f, 408.f, TrMsg(P.message), P.traded ? kit::kGood : kit::kText, 0.9f, 28.f, 4));
  y += 10.f;
  for (const std::string& n : P.notes) y += 24.f * static_cast<float>(kit::Para(dl, sc, 116.f, y, 408.f, n, kit::kGold, 0.8f, 22.f, 2)) + 4.f;
  kit::HintBar(dl, sc, Tr("Press A to go back."));
  return true;
}

void Follow() {
  if (!P.net) return;
  P.net->Update();
  using xd::trade::Phase;
  if (P.step == Step::Connect && P.net->phase() == Phase::Trading) P.step = Step::Trade, P.cursor = P.top = 0;
  if (P.step == Step::Trade && P.net->phase() == Phase::Over) {
    auto* s = P.net->session();
    P.step = Step::Result;
    P.message = s ? s->endText() : P.net->connectionText();
    P.traded = s && s->outcome() == xdnet::trade::Outcome::Traded;
    if (P.traded) {
      const xd::trade::Result& r = P.net->result();
      char b[512];
      std::snprintf(b, sizeof(b), Tr("You received %s (%s)."), r.received.c_str(), TrWhere(r.where).c_str());
      P.notes.push_back(b);
      if (P.live) {
        P.notes.push_back(r.evolved ? Tr("Your game makes the swap now, shows the evolution, and saves.") : Tr("Your game makes the swap now and saves."));
      } else {
        if (r.evolved) {
          std::snprintf(b, sizeof(b), Tr("It evolved: %s became %s!"), r.evolvedFrom.c_str(), r.evolvedInto.c_str());
          P.notes.push_back(b);
        }
        if (r.newEntry) P.notes.push_back(Tr("Your Pokedex has a new entry."));
      }
    }
  }
}

bool Frame(void*, const PortMenuInput* in) {
  const Screen sc = ScreenNow();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  P.age += 1.f / 60.f;
  kit::Backdrop(dl, sc);
  Follow();
  switch (P.step) {
  case Step::Pick: return StepPick(dl, sc, in);
  case Step::Menu: return StepMenu(dl, sc, in);
  case Step::Typing: return StepTyping(dl, sc, in);
  case Step::Connect: return StepConnect(dl, sc, in);
  case Step::Trade: return StepTrade(dl, sc, in);
  case Step::Result: return StepResult(dl, sc, in);
  }
  return false;
}

const PortMenuPage kPage{"online trade", Open, Frame, Close, nullptr};

}
