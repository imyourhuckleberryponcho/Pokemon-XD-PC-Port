namespace ol {

using xd::online::Peer;
using xd::online::RoomRules;

enum class Step { Menu, Settings, Details, Typing, Connect, HostView, JoinView, SavePick, Choose, Team, Ready, Preview, FormatList, TeamCheck, Restart, Message };

struct Slot {
  std::string name;
  std::filesystem::path root;
  bool ok = false;
  std::string trainer, why;
};

struct State {
  Step step = Step::Menu;
  bool host = false;
  int menuCursor = 0;
  RoomRules rules;
  xd::online::PcInfo pc;
  int row = 0, top = 0;
  std::string typed;
  int key = 0;
  std::unique_ptr<xdnet::Connection> conn;
  std::string code;
  xd::online::Capabilities mine, theirs;
  xd::online::Verdict verdict;
  bool judged = false;
  Peer peer;
  bool iAccepted = false, iReady = false;
  uint32_t myNonce = 0;
  std::string message;
  std::unique_ptr<xd::legality::DiscRules> rulesData;
  std::vector<Slot> slots;
  int slotCursor = 0;
  xd::trade::CardSave card;
  std::unique_ptr<xd::trade::Trader> trader;
  std::vector<xd::trade::Entry> entries;
  std::vector<std::string> entryWhy;
  std::vector<int> picked;
  std::vector<uint16_t> species;
  std::vector<uint16_t> chosenSpecies;
  int cursor = 0, listTop = 0;
  float age = 0.f;
  bool started = false;
  const PortMenuPage* sub = nullptr;
  bool pvDone = false, pvStarted = false, pvSent = false, pvLocked = false, pvRevealSent = false;
  std::vector<int> pvMine;
  std::vector<int> pvChosen;
  std::vector<int> pvOrder;
  std::string pvNonceHex, pvRecord;
  std::vector<uint16_t> pvPresetOrder;
  std::vector<uint16_t> pvFriendPicked;
  std::chrono::steady_clock::time_point pvDeadline;
  int fmtCursor = 0;
  std::vector<engine::rulesets::MemberProblem> tcProblems;
  bool cardSent = false, haveFriendCard = false;
  std::string friendCardLine;
};
State L;
bool gClose = false;

using ot::kGold; using ot::kGrey; using ot::kGreen; using ot::kRed; using ot::kWhite;

std::filesystem::path LastFile() { return xd::SettingsFolder() / L"online_room_last.txt"; }
RoomRules LoadLast(const xd::online::PcInfo& pc) {
  RoomRules r = xd::online::DefaultRoom();
  std::string text;
  if (const char* env = std::getenv("XD_ONLINE_RULES")) {
    text = env;
  } else if (!std::getenv("XD_ONLINE_LOBBY_NO_SAVE")) {
    std::ifstream in(LastFile(), std::ios::binary);
    std::getline(in, text);
  }
  RoomRules parsed;
  if (!text.empty() && xd::online::ParseRules(text, parsed, nullptr)) r = parsed;
  return xd::online::Sanitized(r, pc);
}
void SaveLast() {
  if (std::getenv("XD_ONLINE_LOBBY_NO_SAVE") || xd::PlayerFilesReadOnly()) return;
  std::ofstream out(LastFile(), std::ios::binary | std::ios::trunc);
  out << xd::online::RulesToText(L.rules) << "\n";
}

void Note(const std::string& s) { std::fprintf(stderr, "[online lobby] %s\n", s.c_str()); }

void Fail(const std::string& why) {
  Note(why);
  L.conn.reset();
  L.step = Step::Message;
  L.message = why;
}

void StartHosting(const std::string& fixedCode);
void StartJoining();
void UnsetResume() {
#ifdef _WIN32
  _putenv_s("XD_ONLINE_ROOM_RESUME", "");
#else
  unsetenv("XD_ONLINE_ROOM_RESUME");
#endif
}
bool ResumeDue(const WinWorkLayout* w) {
  const char* r = std::getenv("XD_ONLINE_ROOM_RESUME");
  return r != nullptr && r[0] != '\0' && w != nullptr && IsTitleMenu(w) && w->state == 2 && w->pad_a4 == 0 && !PortMenu_Active() && !PortMenu_Pending();
}

void Open(void*) {
  sOnlineKind = 1;
  L = State();
  L.pc = xd::online::LobbyPcInfo();
  L.mine = xd::online::LobbyCapabilities();
  L.rules = LoadLast(L.pc);
  if (xd::online::LobbyLink() != nullptr) xd::online::LobbyLeave();
  const char* resume = std::getenv("XD_ONLINE_ROOM_RESUME");
  if (resume != nullptr && resume[0] != '\0') {
    const std::string r = resume;
    UnsetResume();
    const size_t a = r.find('|'), b = r.find('|', a + 1);
    const std::string what = r.substr(0, a), code = r.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    Note("back from the restart: " + what + " room " + code);
    if (what == "host") {
      RoomRules rr;
      if (b != std::string::npos && xd::online::ParseRules(r.substr(b + 1), rr, nullptr)) L.rules = xd::online::Sanitized(rr, L.pc);
      L.step = Step::Settings;
      StartHosting(code);
    } else {
      L.typed = code;
      StartJoining();
    }
  }
}

void Close(void*) {
  sOnlineKind = 0;
  if (!L.started) L.conn.reset();
}

xdnet::ConnectionConfig MakeConfig(const xd::online::Fingerprint& fp) {
  xdnet::ConnectionConfig cfg = xdnet::Connection::defaultConfig();
  cfg.session.fingerprint = xd::online::Encode(fp);
  if (const char* d = std::getenv("XD_ONLINE_DIRECT")) {
    std::string cur;
    for (const char* c = d; ; c++) {
      if (*c == ';' || *c == '\0') { if (!cur.empty()) cfg.directPeers.push_back(cur); cur.clear(); } else cur += *c;
      if (*c == '\0') break;
    }
  }
  if (const char* lp = std::getenv("XD_ONLINE_LOCAL_PORT")) cfg.localPort = static_cast<uint16_t>(std::atoi(lp));
  if (std::getenv("XD_ONLINE_NO_GO_BETWEEN")) cfg.useGoBetween = false;
  return cfg;
}

void StartHosting(const std::string& fixedCode = "") {
  SaveLast();
  xd::online::Fingerprint fp;
  fp.caps = L.mine;
  fp.rules = L.rules;
  L.conn = std::make_unique<xdnet::Connection>();
  L.code = !fixedCode.empty() ? fixedCode : std::getenv("XD_ONLINE_CODE") ? xdnet::normalizeRoomCode(std::getenv("XD_ONLINE_CODE")) : xdnet::newRoomCode();
  L.host = true;
  if (!L.conn->start(L.code, MakeConfig(fp))) return Fail(L.conn->status().text);
  L.step = Step::Connect;
  Note("hosting room " + xdnet::formatRoomCode(L.code) + ": " + xd::online::RulesText(L.rules));
}

void StartJoining() {
  xd::online::Fingerprint fp;
  fp.caps = L.mine;
  L.conn = std::make_unique<xdnet::Connection>();
  std::string typed = L.typed;
  if (const char* fixed = std::getenv("XD_ONLINE_CODE")) typed = fixed;
  L.code = xdnet::normalizeRoomCode(typed);
  L.host = false;
  if (L.code.empty()) return Fail(Tr("That is not a room code. Check it and try again."));
  if (!L.conn->start(L.code, MakeConfig(fp))) return Fail(L.conn->status().text);
  L.step = Step::Connect;
  Note("joining room " + xdnet::formatRoomCode(L.code));
}

void Send(const std::string& line) {
  if (L.conn) L.conn->send(line, true);
}

void Reset() {
  L.conn.reset();
  L.peer = Peer();
  L.judged = L.iAccepted = L.iReady = false;
  L.picked.clear();
  L.chosenSpecies.clear();
  L.step = Step::Menu;
}

void Follow() {
  if (!L.conn) return;
  const xdnet::ConnectionStatus st = L.conn->status();
  if (L.step == Step::Connect && st.phase == xdnet::Phase::Connected && !L.judged) {
    L.judged = true;
    xd::online::Fingerprint peer;
    if (!xd::online::Decode(st.peerFingerprint, peer)) {
      return Fail(Tr("Your friend's game does not speak the same online battle version. Update both games."));
    }
    L.theirs = peer.caps;
    if (L.host) {
      L.verdict = xd::online::Judge(L.mine, L.rules, peer.caps, false);
      if (!L.verdict.Ok()) {
        L.message = Fmt(Tr("Your friend can't play in this room: %s"), L.verdict.refuse.front().c_str());
        Note(L.message);
        L.step = Step::Message;
        return;
      }
      L.step = Step::HostView;
    } else {
      L.rules = peer.rules;
      L.theirs = peer.caps;
      L.verdict = xd::online::Judge(peer.caps, peer.rules, L.mine, true);
      L.step = Step::JoinView;
    }
    Note(std::string("connected; room: ") + xd::online::RulesText(L.rules));
  }
  if (L.judged || L.step == Step::Connect) {
    std::vector<uint8_t> m;
    while (L.conn && L.conn->receive(m)) {
      const std::string line(m.begin(), m.end());
      if (!xd::online::Apply(L.peer, line) && line.compare(0, 2, "K ") == 0) { L.friendCardLine = line; L.haveFriendCard = true; }
    }
  }
  if (L.host && L.judged && L.step == Step::HostView && (L.peer.left || L.peer.declined || st.phase == xdnet::Phase::Closed || st.phase == xdnet::Phase::Failed)) {
    Note("the friend left before accepting: the room " + xdnet::formatRoomCode(L.code) + " opens again");
    const std::string code = L.code;
    L.conn.reset();
    L.peer = Peer();
    L.judged = false;
    StartHosting(code);
  } else if (L.judged && (st.phase == xdnet::Phase::Closed || st.phase == xdnet::Phase::Failed) && L.step != Step::Message) {
    Fail(Tr("The connection to your friend ended."));
  } else if (L.judged && L.peer.left && L.step != Step::Message) {
    Fail(Tr("Your friend left the room."));
  }
}

void Title(ImDrawList* dl, const Screen& sc, const char* t) { kit::Header(dl, sc, t, L.code.empty() ? "" : Fmt(Tr("Room %s"), xdnet::formatRoomCode(L.code).c_str())); }
void Hint(ImDrawList* dl, const Screen& sc, const char* t) { kit::HintBar(dl, sc, t); }
void Buttons(ImDrawList* dl, const Screen& sc, float y, const std::vector<std::string>& labels, int cursor, float w = 240.f) {
  for (size_t i = 0; i < labels.size(); i++) kit::Button(dl, sc, 320.f - w / 2.f, y + 44.f * static_cast<float>(i), w, labels[i], static_cast<int>(i) == cursor);
}

bool Back(const PortMenuInput* in) {
  if (in->pressed & PAD_BUTTON_B) {
    PlayMenuSound(kTitleMenu, kSoundBack);
    return true;
  }
  return false;
}
void Move(const PortMenuInput* in, int& cursor, int n) {
  if (in->repeat & PAD_BUTTON_UP) cursor = (cursor + n - 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_DOWN) cursor = (cursor + 1) % n, PlayMenuSound(kTitleMenu, kSoundMove);
}

bool StepMenu(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const char* items[3] = {"Make a room", "Join a room", "Back"};
  Move(in, L.menuCursor, 3);
  if (Back(in)) return false;
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (L.menuCursor == 0) L.step = Step::Settings, L.row = L.top = 0;
    else if (L.menuCursor == 1) L.step = Step::Typing, L.typed.clear(), L.key = 0;
    else return false;
  }
  Title(dl, sc, Tr("BATTLE A FRIEND"));
  kit::Window(dl, sc, 100.f, 82.f, 540.f, 162.f);
  kit::Text(dl, sc, 124.f, 98.f, Tr("Make a room and tell your friend its code,"), kit::kText, 0.8f, 392.f);
  kit::Text(dl, sc, 124.f, 124.f, Tr("or join the room your friend made."), kit::kText, 0.8f, 392.f);
  Buttons(dl, sc, 184.f, {Tr(items[0]), Tr(items[1]), Tr(items[2])}, L.menuCursor);
  kit::Window(dl, sc, 60.f, 340.f, 580.f, 436.f);
  kit::Para(dl, sc, 86.f, 351.f, 468.f, ot::IpWarning(), kit::kBad, 0.76f, 19.f, 2);
  kit::Para(dl, sc, 86.f, 393.f, 468.f, Tr("Your friend needs the same game and this screen too."), kit::kDim, 0.66f, 16.f, 2);
  Hint(dl, sc, Tr("A: choose   B: back"));
  return true;
}

constexpr int kVisibleRows = 9;
bool StepSettings(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  std::vector<xd::online::Row> rows = xd::online::SettingRows(L.rules, L.pc);
  const int n = static_cast<int>(rows.size()) + 1;
  L.row = std::min(L.row, n - 1);
  Move(in, L.row, n);
  if (Back(in)) { L.step = Step::Menu; return true; }
  const bool onButton = L.row == n - 1;
  if (!onButton) {
    int d = 0;
    if (in->repeat & PAD_BUTTON_LEFT) d = -1;
    if (in->repeat & PAD_BUTTON_RIGHT) d = 1;
    const std::string& rid = rows[static_cast<size_t>(L.row)].id;
    if ((in->pressed & PAD_BUTTON_A) && (rid == "clauses" || rid == "bans")) {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      L.step = Step::Details;
      return true;
    }
    if ((in->pressed & PAD_BUTTON_A) && rid == "format") {
      PlayMenuSound(kTitleMenu, kSoundChoose);
      L.fmtCursor = 0;
      if (L.rules.format.empty()) L.fmtCursor = static_cast<int>(L.pc.formats.size());
      for (size_t i = 0; i < L.pc.formats.size(); i++) if (L.pc.formats[i].id == L.rules.format) L.fmtCursor = static_cast<int>(i);
      L.step = Step::FormatList;
      return true;
    }
    if ((in->pressed & PAD_BUTTON_A) && !rows[static_cast<size_t>(L.row)].number && rid != "preview" && rid != "timers") d = 1;
    if (d != 0) {
      if (xd::online::Cycle(L.rules, rows[static_cast<size_t>(L.row)].id, d, L.pc)) PlayMenuSound(kTitleMenu, kSoundMove);
      else PlayMenuSound(kTitleMenu, kSoundBack);
    }
  } else if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    xd::online::Verdict own = xd::online::Judge(L.mine, L.rules, L.mine, true);
    if (!own.Ok()) {
      L.verdict = own;
      L.step = Step::Message;
      L.message = own.refuse.front();
    } else if (own.NeedsRestart()) {
      L.verdict = own;
      L.host = true;
      L.step = Step::Restart;
    } else {
      StartHosting();
    }
    return true;
  }
  if (L.row < L.top) L.top = L.row;
  if (L.row >= L.top + kVisibleRows) L.top = L.row - kVisibleRows + 1;
  Title(dl, sc, Tr("ROOM SETTINGS"));
  kit::Window(dl, sc, 40.f, 72.f, 600.f, 388.f);
  for (int r = 0; r < kVisibleRows; r++) {
    const int i = L.top + r;
    if (i >= n) break;
    const float y = 82.f + 33.f * static_cast<float>(r);
    const bool sel = i == L.row;
    if (i == n - 1) {
      kit::Button(dl, sc, 210.f, y, 220.f, Tr("Make the room"), sel);
      continue;
    }
    if (sel) kit::RowSel(dl, sc, 48.f, 592.f, y, 30.f);
    const xd::online::Row& row = rows[static_cast<size_t>(i)];
    kit::Text(dl, sc, 76.f, y + 6.f, Tr(row.title), sel ? kit::kText : kit::kSoft, 0.78f, 236.f, 1);
    kit::Value(dl, sc, 322.f, y + 3.f, 262.f, Tr(row.Current().label), sel, sel ? kit::kText : kit::kGold);
    if (row.id == "format" || row.id == "clauses" || row.id == "bans") kit::Text(dl, sc, 572.f, y + 8.f, "...", kit::kDim, 0.7f);
  }
  if (L.top > 0) kit::Arrow(dl, sc, 320.f, 78.f, true);
  if (L.top + kVisibleRows < n) kit::Arrow(dl, sc, 320.f, 384.f, false);
  if (!onButton && rows[static_cast<size_t>(L.row)].id == "format" && !L.rules.format.empty()) {
    kit::Window(dl, sc, 40.f, 394.f, 600.f, 440.f);
    kit::Para(dl, sc, 64.f, 403.f, 512.f, xd::rulesets::text::FormatInfo(L.rules.format), kit::kText, 0.62f, 14.f, 3);
  }
  if (!onButton) {
    const xd::online::Row& row = rows[static_cast<size_t>(L.row)];
    for (const xd::online::Option& o : row.options) {
      if (!o.enabled) {
        kit::Window(dl, sc, 40.f, 394.f, 600.f, 440.f);
        kit::Para(dl, sc, 64.f, 403.f, 512.f, Tr(o.label) + ": " + Tr(o.why), kit::kBad, 0.66f, 15.f, 2);
        break;
      }
    }
  }
  Hint(dl, sc, Tr("Up / down: choose   Left / right: change   A: open \"...\"   B: back"));
  return true;
}

bool StepDetails(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (Back(in)) { L.step = Step::Settings; return true; }
  namespace tx = xd::rulesets::text;
  const engine::rulesets::Format* f = xd::rulesets::FindFormat(L.rules.format);
  Title(dl, sc, Tr("CLAUSES AND BANS"));
  kit::Window(dl, sc, 20.f, 76.f, 620.f, 270.f);
  kit::Text(dl, sc, 45.f, 82.f, Tr("Clauses"), kit::kGold, 0.8f, 200.f, 1);
  const char* ids[] = {"species", "item", "sleep", "ohko", "evasion"};
  for (int i = 0; i < 5; i++) {
    const float y = 104.f + 32.f * static_cast<float>(i);
    const bool on = f != nullptr && f->clauses.count(ids[i]) > 0;
    kit::Text(dl, sc, 55.f, y, tx::ClauseName(ids[i]), kit::kText, 0.75f, 120.f, 1);
    kit::Text(dl, sc, 135.f, y + 4.f, tx::ClauseInfo(ids[i]), kit::kSoft, 0.46f, 295.f);
    const bool atTeam = engine::rulesets::ClauseCheckedAtSubmit(ids[i]);
    kit::Text(dl, sc, 435.f, y + 4.f, atTeam ? Tr("team step") : Tr("battle rule"), kit::kDim, 0.5f, 70.f);
    kit::Value(dl, sc, 520.f, y - 3.f, 80.f, on ? Tr("On") : Tr("Off"), false, on ? kit::kGold : kit::kDim);
  }
  kit::Window(dl, sc, 20.f, 280.f, 620.f, 392.f);
  kit::Text(dl, sc, 45.f, 285.f, Tr("Bans"), kit::kGold, 0.8f, 200.f, 1);
  const bool leg = f != nullptr && f->bannedCategories.count("legendary") > 0, myth = f != nullptr && f->bannedCategories.count("mythical") > 0;
  kit::Text(dl, sc, 55.f, 308.f, tx::CategoryName("legendary"), kit::kText, 0.72f, 220.f, 1);
  kit::Value(dl, sc, 490.f, 304.f, 110.f, leg ? Tr("Not allowed") : Tr("Allowed"), false, leg ? kit::kGold : kit::kDim);
  kit::Text(dl, sc, 55.f, 336.f, tx::CategoryName("mythical"), kit::kText, 0.72f, 220.f, 1);
  kit::Value(dl, sc, 490.f, 332.f, 110.f, myth ? Tr("Not allowed") : Tr("Allowed"), false, myth ? kit::kGold : kit::kDim);
  const size_t n = f != nullptr ? f->bannedSpecies.size() + f->bannedItems.size() + f->bannedMoves.size() + f->bannedAbilities.size() : 0;
  kit::Text(dl, sc, 55.f, 362.f, n > 0 ? Fmt(Tr("Ban list: %d entries (species, items, moves, abilities)"), static_cast<int>(n)) : std::string(Tr("Ban list: none")), kit::kSoft, 0.62f, 540.f);
  kit::Window(dl, sc, 20.f, 398.f, 620.f, 436.f);
  kit::Para(dl, sc, 40.f, 406.f, 560.f, Tr("Only our own categories and a list. No outside tier lists; a format file can carry its own ban list. The clauses are the format's own: choose another format to change them."), kit::kText, 0.6f, 14.f, 2);
  Hint(dl, sc, Tr("B: back"));
  return true;
}

bool StepTyping(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const int nkeys = 31;
  if (ImGui::GetCurrentContext() != nullptr) {
    for (ImWchar ch : ImGui::GetIO().InputQueueCharacters) {
      const char u = static_cast<char>(std::toupper(static_cast<int>(ch)));
      if (ch < 128 && std::strchr(ot::kAlphabet, u) != nullptr && static_cast<int>(L.typed.size()) < ot::kCodeLen) L.typed += u;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !L.typed.empty()) L.typed.pop_back();
  }
  const int row = L.key / ot::kKeyCols;
  if (L.key < 32) {
    if (in->repeat & PAD_BUTTON_LEFT) L.key = std::max(0, L.key - 1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_RIGHT) L.key = std::min(31, L.key + 1), PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_UP) L.key = row > 0 ? L.key - ot::kKeyCols : L.key, PlayMenuSound(kTitleMenu, kSoundMove);
    if (in->repeat & PAD_BUTTON_DOWN) L.key = (row < 3 && L.key + ot::kKeyCols <= 32) ? L.key + ot::kKeyCols : 33, PlayMenuSound(kTitleMenu, kSoundMove);
  } else if (in->repeat & PAD_BUTTON_UP) {
    L.key = 24, PlayMenuSound(kTitleMenu, kSoundMove);
  }
  if (Back(in)) { L.step = Step::Menu; return true; }
  if ((in->pressed & PAD_BUTTON_X) && !L.typed.empty()) L.typed.pop_back();
  const bool full = static_cast<int>(L.typed.size()) == ot::kCodeLen;
  if (((in->pressed & PAD_BUTTON_START) || ((in->pressed & PAD_BUTTON_A) && L.key == 33)) && full) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    StartJoining();
    return true;
  }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (L.key < nkeys) { if (static_cast<int>(L.typed.size()) < ot::kCodeLen) L.typed += ot::kAlphabet[L.key]; }
    else if (L.key == 31 && !L.typed.empty()) L.typed.pop_back();
  }
  Title(dl, sc, Tr("BATTLE A FRIEND"));
  kit::TitledWindow(dl, sc, 90.f, 76.f, 550.f, 150.f, Tr("Type your friend's room code"));
  std::string shown;
  for (int i = 0; i < ot::kCodeLen; i++) shown += i < static_cast<int>(L.typed.size()) ? L.typed[static_cast<size_t>(i)] : '_';
  ot::CodeLine(dl, sc, 114.f, shown, 1.3f);
  ot::Keyboard(dl, sc, L.key, full, 158.f);
  kit::Window(dl, sc, 90.f, 380.f, 550.f, 436.f);
  kit::Para(dl, sc, 114.f, 392.f, 412.f, ot::IpWarning(), kit::kBad, 0.7f, 17.f, 2);
  Hint(dl, sc, Tr("D-pad: choose  A: type  X: delete  START: join  B: back"));
  (void)nkeys;
  return true;
}

std::vector<std::string> RoomLinesTr(const RoomRules& r);

bool StepConnect(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (Back(in)) { Reset(); return true; }
  const xdnet::ConnectionStatus st = L.conn->status();
  Title(dl, sc, L.host ? Tr("YOUR ROOM") : Tr("JOINING A ROOM"));
  const bool failed = st.phase == xdnet::Phase::Failed;
  kit::Window(dl, sc, 90.f, 80.f, 550.f, 336.f, failed);
  if (L.host) {
    kit::TextC(dl, sc, 90.f, 96.f, 460.f, Tr("Room code (tell your friend):"), kit::kText, 0.8f);
    kit::TextC(dl, sc, 90.f, 122.f, 460.f, xdnet::formatRoomCode(L.code), kit::kGold, 1.9f, 1);
    const std::vector<std::string> lines = RoomLinesTr(L.rules);
    kit::TextC(dl, sc, 100.f, 176.f, 440.f, lines[0] + "; " + lines[1], kit::kSoft, 0.6f);
  } else {
    kit::TextC(dl, sc, 90.f, 100.f, 460.f, Fmt(Tr("Room code: %s"), xdnet::formatRoomCode(L.code).c_str()), kit::kGold, 1.f, 1);
  }
  if (!failed) kit::Ring(dl, sc, 150.f, 258.f, 26.f);
  kit::Para(dl, sc, failed ? 116.f : 196.f, 214.f, failed ? 410.f : 330.f, st.text + (st.detail.empty() ? "" : " " + st.detail), failed ? kit::kBad : kit::kText, 0.85f, 24.f, 4);
  kit::Window(dl, sc, 90.f, 346.f, 550.f, 432.f);
  kit::Para(dl, sc, 114.f, 362.f, 412.f, ot::IpWarning(), kit::kBad, 0.78f, 21.f, 3);
  Hint(dl, sc, Tr("B: cancel"));
  return true;
}

std::vector<std::string> RoomLinesTr(const RoomRules& r) {
  std::vector<std::string> l;
  l.push_back(Fmt(Tr("Rules: %s"), r.ruleSet == "gen4pokemon" ? Tr("Gen 4 Pokemon with the classic XD rules") : r.ruleSet == "gen4" ? Tr("Gen 4 Pokemon and moves") : r.ruleSet == "gen4mt" ? Tr("Gen 4 Pokemon and moves with the modern types") : L.pc.edition == "xg" ? Tr("Classic XG") : Tr("Classic XD")));
  l.push_back(Fmt(Tr("Battle: %s, physical/special split %s"), r.style == "double" ? Tr("double") : Tr("single"), r.split ? Tr("on") : Tr("off")));
  l.push_back(Fmt(Tr("Teams: %s"), r.team == "preset"   ? Tr("preset teams")
                                   : r.team == "save"   ? Tr("you bring a team from one of your saves")
                                   : r.team == "free12" ? Tr("free pick from Gen 1-3 Pokemon")
                                   : r.team == "free14" ? Tr("free pick from Gen 1-4 Pokemon")
                                   : r.team == "draft"  ? Tr("snake draft")
                                                        : Tr("random offers")));
  if (r.team == "draft" || r.team == "offer") {
    xd::online::draft::Settings ds;
    xd::online::draft::ParseSettings(r.team == "offer" ? "offer" : "snake", r.draft, ds, nullptr);
    l.push_back(Fmt(Tr("Draft: %s"), xd::online::draft::SettingsDescribe(ds).c_str()));
  }
  l.push_back(Fmt(Tr("Levels: %s"), (r.level == "asis" ? std::string(Tr("as they are"))
                                    : r.level.compare(0, 4, "all:") == 0 ? Fmt(Tr("all set to level %s"), r.level.substr(4).c_str())
                                                                         : Fmt(Tr("capped at level %s"), r.level.substr(4).c_str())).c_str()));
  l.push_back(Fmt(Tr("Items: %s"), r.items == "none" ? Tr("none") : r.items == "bag" ? Tr("your own bag items (not used up in your save)") : Tr("standard kit (Max Potion, Full Heal, Revive)")));
  l.push_back(Fmt(Tr("Time per choice: %s"), (r.timerSeconds == 0 ? std::string(Tr("no limit")) : Fmt(Tr("%d seconds"), r.timerSeconds)).c_str()));
  l.push_back(Fmt(Tr("Shadow Pokemon: %s"), !r.shadowAllowed ? Tr("not allowed") : r.shadowObey == "story" ? Tr("allowed, they may refuse orders as in the story") : Tr("allowed, they obey like purified ones")));
  return l;
}

std::string TrVerdictLine(const std::string& s) {
  if (s == "This room plays with Gen 4 Pokemon. You need the Gen 4 pack from your Pokemon Battle Revolution disc (the launcher makes it).")
    return Tr("This room plays with Gen 4 Pokemon. You need the Gen 4 pack from your Pokemon Battle Revolution disc (the launcher makes it).");
  if (s == "Your friend does not have the Gen 4 pack from the Pokemon Battle Revolution disc, which this room needs.")
    return Tr("Your friend does not have the Gen 4 pack from the Pokemon Battle Revolution disc, which this room needs.");
  if (s == "Your friend's Gen 4 pack is a different version from yours. Ask them to make it again in the launcher.")
    return Tr("Your friend's Gen 4 pack is a different version from yours. Ask them to make it again in the launcher.");
  if (s == "The game data of the two games differs in a way the list above does not explain. Both players should use the same game version.")
    return Tr("The game data of the two games differs in a way the list above does not explain. Both players should use the same game version.");
  return s;
}

std::string TrOutcomeText(xd::online::Outcome o) {
  switch (o) {
  case xd::online::Outcome::Won: return Tr("You won the battle!");
  case xd::online::Outcome::Lost: return Tr("You lost the battle.");
  case xd::online::Outcome::YouQuit: return Tr("You gave in. Your friend wins.");
  case xd::online::Outcome::FriendQuit: return Tr("Your friend gave in. You win!");
  case xd::online::Outcome::ConnectionLost: return Tr("The connection to your friend was lost. The battle ended without a winner.");
  case xd::online::Outcome::Desync: return Tr("The two games stopped agreeing about the battle, so it was ended. Nobody won.");
  }
  return "";
}

float DrawRoom(ImDrawList* dl, const Screen& sc, float y) {
  const std::vector<std::string> lines = RoomLinesTr(L.rules);
  const float bottom = y + 44.f + 18.f * static_cast<float>(lines.size());
  kit::TitledWindow(dl, sc, 40.f, y, 600.f, bottom, Tr("ROOM SETTINGS"));
  for (size_t i = 0; i < lines.size(); i++) kit::Text(dl, sc, 64.f, y + 39.f + 18.f * static_cast<float>(i), lines[i], kit::kText, 0.66f, 512.f);
  return bottom;
}

std::string FormatTitle(const RoomRules& r) { return r.format.empty() ? std::string(Tr("Custom")) : xd::rulesets::text::FormatName(r.format, r.format); }
std::string LevelsText(const RoomRules& r) {
  return r.level == "asis" ? std::string(Tr("as they are")) : r.level.compare(0, 4, "all:") == 0 ? Fmt(Tr("all set to %s for the battle"), r.level.substr(4).c_str()) : Fmt(Tr("capped at level %s"), r.level.substr(4).c_str());
}
float DrawRoomCard(ImDrawList* dl, const Screen& sc, float y0, float y1) {
  namespace tx = xd::rulesets::text;
  kit::Window(dl, sc, 40.f, y0, 600.f, y1, true);
  kit::Text(dl, sc, 62.f, y0 + 10.f, Fmt(Tr("The room: %s"), FormatTitle(L.rules).c_str()), kit::kGold, 0.86f, 520.f, 1);
  std::vector<std::pair<std::string, std::string>> rows;
  rows.push_back({Tr("Rule set"), tx::RuleSetShort(xd::online::RoomRuleSetId(L.rules))});
  rows.push_back({Tr("Pokemon"), tx::PokemonShort(xd::online::RoomMaxGen(L.rules))});
  rows.push_back({Tr("Levels"), LevelsText(L.rules)});
  const bool fmt = L.pc.formatLine != nullptr && !L.rules.format.empty();
  if (fmt) {
    rows.push_back({Tr("Clauses"), L.pc.formatLine(L.rules, "clauses")});
    rows.push_back({Tr("Bans"), L.pc.formatLine(L.rules, "bans")});
    rows.push_back({Tr("Team preview"), L.rules.preview ? Fmt(Tr("%s - timers: %s"), L.pc.formatLine(L.rules, "preview").c_str(), L.pc.formatLine(L.rules, "timers").c_str()) : std::string(Tr("Off"))});
  }
  float y = y0 + 42.f;
  for (const auto& kv : rows) {
    kit::Text(dl, sc, 62.f, y, kv.first, kit::kSoft, 0.7f, 140.f);
    y += 17.f * static_cast<float>(kit::Para(dl, sc, 212.f, y, 360.f, kv.second, kit::kText, 0.7f, 17.f, 2));
  }
  return y;
}

bool DrawJoinRefusal(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  Title(dl, sc, Tr("JOIN A ROOM"));
  const float bottom = DrawRoomCard(dl, sc, 76.f, 352.f);
  float y = bottom + 4.f;
  kit::Text(dl, sc, 62.f, y, Tr("You can't join this room yet:"), kit::kBad, 0.82f, 520.f, 1);
  y += 24.f;
  for (const std::string& r : L.verdict.refuse) y += 16.f * static_cast<float>(kit::Para(dl, sc, 62.f, y, 510.f, std::string("- ") + TrVerdictLine(r), kit::kBad, 0.68f, 16.f, 3));
  kit::Text(dl, sc, 62.f, 330.f, Tr("Looks are never a room rule: each player sees their own."), kit::kDim, 0.62f, 520.f);
  kit::Button(dl, sc, 200.f, 380.f, 240.f, Tr("Back"), true);
  Hint(dl, sc, Tr("A: choose   B: back"));
  (void)in;
  return true;
}

bool StepHostView(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (Back(in)) { Send(xd::online::Msg("LEAVE")); Reset(); return true; }
  if (L.peer.accepted) {
    L.step = Step::Team;
    L.cursor = L.listTop = 0;
    return true;
  }
  Title(dl, sc, Tr("YOUR FRIEND HAS JOINED"));
  const float y = DrawRoom(dl, sc, 76.f) + 10.f;
  kit::Window(dl, sc, 40.f, y, 600.f, y + 64.f, true);
  kit::Ring(dl, sc, 80.f, y + 32.f, 16.f);
  kit::Para(dl, sc, 110.f, y + 14.f, 470.f, Tr("Your friend is reading the settings. Waiting for them to accept..."), kit::kText, 0.78f, 20.f, 2);
  Hint(dl, sc, Tr("B: close the room"));
  return true;
}

bool StepJoinView(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const bool ok = L.verdict.Ok();
  const bool restart = ok && L.verdict.NeedsRestart();
  std::vector<std::string> items;
  if (ok && !restart) items = {"Accept", "Leave"};
  else if (restart) items = {"Restart into the room", "Leave"};
  else items = {"Leave"};
  Move(in, L.cursor, static_cast<int>(items.size()));
  if (Back(in)) { Send(xd::online::Msg("DECLINE")); Reset(); return true; }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    const std::string& it = items[static_cast<size_t>(L.cursor)];
    if (it == "Accept") {
      L.iAccepted = true;
      Send(xd::online::Msg("ACCEPT"));
      L.step = Step::Team;
      L.cursor = L.listTop = 0;
      return true;
    }
    if (it == "Restart into the room") { L.step = Step::Restart; return true; }
    Send(xd::online::Msg("DECLINE"));
    Reset();
    return true;
  }
  if (!ok) return DrawJoinRefusal(dl, sc, in);
  Title(dl, sc, Tr("THE ROOM'S SETTINGS"));
  float y = DrawRoom(dl, sc, 72.f) + 6.f;
  if (!L.verdict.refuse.empty() || !L.verdict.restart.empty()) {
    int lines = 0;
    for (const std::string& r : L.verdict.refuse) lines += std::min(3, kit::LinesOf(TrVerdictLine(r), 540.f, 0.68f));
    for (const std::string& r : L.verdict.restart) lines += std::min(3, kit::LinesOf(TrVerdictLine(r), 540.f, 0.68f));
    kit::Window(dl, sc, 40.f, y, 600.f, std::min(y + 16.f + 16.f * static_cast<float>(lines), 352.f));
    y += 9.f;
    for (const std::string& r : L.verdict.refuse) y += 16.f * static_cast<float>(kit::Para(dl, sc, 64.f, y, 512.f, TrVerdictLine(r), kit::kBad, 0.68f, 16.f, 3));
    for (const std::string& r : L.verdict.restart) y += 16.f * static_cast<float>(kit::Para(dl, sc, 64.f, y, 512.f, TrVerdictLine(r), kit::kGold, 0.68f, 16.f, 3));
  }
  std::vector<std::string> labels;
  for (const std::string& it : items) labels.push_back(Tr(it));
  Buttons(dl, sc, 444.f - 44.f * static_cast<float>(labels.size()) - 4.f, labels, L.cursor, 280.f);
  Hint(dl, sc, Tr("A: choose   B: back"));
  return true;
}

bool RestartCures() {
  xd::online::Capabilities after = L.mine;
  after.ruleSet = xd::online::RoomGen4Pokemon(L.rules.ruleSet) ? "gen4" : "classic";
  after.randomizer.clear();
  const xd::online::Capabilities& room = L.host ? after : L.theirs;
  const xd::online::Verdict v = xd::online::Judge(room, L.rules, after, true);
  return v.Ok() && !v.NeedsRestart();
}

bool StepRestart(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const bool cures = RestartCures();
  Move(in, L.cursor, cures ? 2 : 1);
  if (Back(in)) { L.step = L.host ? Step::Settings : Step::JoinView; return true; }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (cures && L.cursor == 0) {
      std::string resume;
      if (L.host) {
        L.code = L.code.empty() ? (std::getenv("XD_ONLINE_CODE") ? xdnet::normalizeRoomCode(std::getenv("XD_ONLINE_CODE")) : xdnet::newRoomCode()) : L.code;
        resume = "host|" + L.code + "|" + xd::online::RulesToText(L.rules);
      } else {
        resume = "join|" + L.code;
        Send(xd::online::Msg("LEAVE"));
      }
      if (xd::pt::RestartForRoom(xd::online::RoomGen4Pokemon(L.rules.ruleSet), xd::online::RoomGen4Rules(L.rules.ruleSet), xd::online::RoomRuleSetId(L.rules))) {
#ifdef _WIN32
        _putenv_s("XD_ONLINE_ROOM_RESUME", resume.c_str());
#else
        setenv("XD_ONLINE_ROOM_RESUME", resume.c_str(), 1);
#endif
        Note("restarting into the room: " + resume);
        L.conn.reset();
        gClose = true;
        return false;
      }
      L.message = Tr("The game could not be started again. Close it and start it again yourself.");
      L.step = Step::Message;
    } else {
      L.step = L.host ? Step::Settings : Step::JoinView;
    }
    return true;
  }
  Title(dl, sc, Tr("A RESTART IS NEEDED"));
  kit::Window(dl, sc, 40.f, 78.f, 600.f, 336.f, true);
  float y = 94.f;
  for (const std::string& r : L.verdict.restart) {
    y += 21.f * static_cast<float>(kit::Para(dl, sc, 66.f, y, 508.f, TrVerdictLine(r), kit::kGold, 0.8f, 21.f, 3)) + 6.f;
  }
  kit::Para(dl, sc, 66.f, y + 8.f, 508.f, cures ? Tr("The game starts again with the room's data and comes back to this room by itself (you wait through the opening again). Your saves stay as they are.")
                                                : Tr("A restart alone can't fix this: use the MODS page (EXTRAS) to switch the mods as the room needs, then come back."),
            cures ? kit::kText : kit::kBad, 0.72f, 18.f, 4);
  const char* items[2] = {"Restart into the room", "Back"};
  std::vector<std::string> labels;
  for (int i = 0; i < (cures ? 2 : 1); i++) labels.push_back(Tr(items[cures ? i : 1]));
  Buttons(dl, sc, 350.f, labels, L.cursor, 280.f);
  Hint(dl, sc, Tr("A: choose   B: back"));
  return true;
}

bool StepMessage(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if ((in->pressed & (PAD_BUTTON_A | PAD_BUTTON_B))) { PlayMenuSound(kTitleMenu, kSoundBack); Reset(); return true; }
  Title(dl, sc, Tr("BATTLE A FRIEND"));
  kit::Window(dl, sc, 60.f, 100.f, 580.f, 300.f, true);
  kit::Para(dl, sc, 86.f, 122.f, 468.f, L.message, kit::kText, 0.85f, 26.f, 6);
  Hint(dl, sc, Tr("A or B: back"));
  return true;
}

bool EnsureRules() {
  if (L.rulesData) return true;
  std::string err;
  auto rules = std::make_unique<xd::legality::DiscRules>();
  char derr[256] = {};
  if (!PortDisc_Open(PortXD_DiscPath(), derr, sizeof(derr))) { L.message = Fmt(Tr("The disc could not be read: %s"), derr); return false; }
  const bool ok = xd::legality::LoadRulesFromRunningGame(*rules, err);
  PortDisc_Close();
  if (!ok) { L.message = Fmt(Tr("The game's Pokemon tables could not be read: %s"), err.c_str()); return false; }
  L.rulesData = std::move(rules);
  return true;
}

std::string SpeciesName(uint16_t id) {
  if (!L.rulesData || !L.rulesData->names) return "#" + std::to_string(id);
  const std::string n = L.rulesData->names->Species(id);
  return n.empty() ? "#" + std::to_string(id) : n;
}

void OpenSlot(int i);
void BeginTeam() {
  L.picked.clear();
  L.chosenSpecies.clear();
  L.cursor = L.listTop = 0;
  const std::string& t = L.rules.team;
  if (t == "preset" || t == "draft" || t == "offer") {
    L.step = Step::Team;
    return;
  }
  if (!EnsureRules()) { L.step = Step::Message; return; }
  if (t == "save") {
    namespace sl = engine::memcard::slots;
    std::vector<std::pair<std::string, std::filesystem::path>> roots;
    if (const char* dir = std::getenv("XD_ONLINE_LOBBY_SAVE_DIR")) {
      roots.push_back({"Save", std::filesystem::path(dir)});
    } else if (xd::SaveSlotsInUse()) {
      for (const sl::Slot& slot : sl::Load(xd::UserFolder()).slots) roots.push_back({slot.name, sl::RootOf(xd::UserFolder(), slot.id)});
    } else {
      roots.push_back({"Save", xd::CardRoot()});
    }
    L.slots.clear();
    xd::trade::Config cfg;
    cfg.rules = &L.rulesData->rules;
    cfg.names = L.rulesData->names.get();
    int usable = 0, only = 0;
    for (size_t i = 0; i < roots.size(); i++) {
      Slot si;
      si.name = roots[i].first;
      si.root = roots[i].second;
      xd::trade::CardSave cs;
      std::string e2;
      xd::trade::Trader tr(cfg);
      if (!xd::trade::LoadCardSave(si.root, cs, e2)) si.why = Tr("No Pokemon XD save in it yet.");
      else if (!tr.Load(cs.data, e2)) si.why = Fmt(Tr("Can't be read: %s"), e2.c_str());
      else si.ok = true, si.trainer = tr.trainerName();
      Note("save " + si.name + " (" + si.root.string() + "): " + (si.ok ? "ok, trainer " + si.trainer : si.why));
      if (si.ok) usable++, only = static_cast<int>(i);
      L.slots.push_back(si);
    }
    if (usable == 0) { L.message = Tr("There is no save to bring a team from. Play and save once first, or ask your friend for another team source."); L.step = Step::Message; return; }
    L.slotCursor = only;
    if (usable == 1) OpenSlot(only);
    else L.step = Step::SavePick;
    return;
  }
  L.species.clear();
  const xd::legality::Rules& r = L.rulesData->rules;
  const size_t limit = t == "free12" ? std::min<size_t>(r.vanillaSpecies, r.species.size()) : r.species.size();
  for (size_t i = 1; i < limit; i++) {
    if (r.species[i].exists && !L.rulesData->names->Species(static_cast<uint16_t>(i)).empty()) L.species.push_back(static_cast<uint16_t>(i));
  }
  L.step = Step::Choose;
}

void OpenSlot(int i) {
  xd::trade::Config c2;
  c2.rules = &L.rulesData->rules;
  c2.names = L.rulesData->names.get();
  std::string e3;
  if (!xd::trade::LoadCardSave(L.slots[static_cast<size_t>(i)].root, L.card, e3)) { L.message = e3; L.step = Step::Message; return; }
  L.trader = std::make_unique<xd::trade::Trader>(c2);
  if (!L.trader->Load(L.card.data, e3)) { L.message = Fmt(Tr("This save can't be read: %s"), e3.c_str()); L.step = Step::Message; return; }
  L.entries = L.trader->List();
  L.entryWhy.assign(L.entries.size(), "");
  for (size_t k = 0; k < L.entries.size(); k++) {
    const std::vector<std::string> why = xd::online::CheckTeam({L.entries[k].pokemon}, L.rulesData->rules, L.rules);
    L.entryWhy[k] = why.empty() ? "" : why.front();
  }
  L.step = Step::Choose;
  L.cursor = L.listTop = 0;
}

bool StepSavePick(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const int n = static_cast<int>(L.slots.size());
  Move(in, L.slotCursor, n);
  if (Back(in)) { L.step = Step::Team; L.cursor = 0; return true; }
  if (in->pressed & PAD_BUTTON_A) {
    if (L.slots[static_cast<size_t>(L.slotCursor)].ok) { PlayMenuSound(kTitleMenu, kSoundChoose); OpenSlot(L.slotCursor); return true; }
    PlayMenuSound(kTitleMenu, kSoundBack);
  }
  Title(dl, sc, Tr("WHICH SAVE IS YOUR TEAM FROM?"));
  kit::Window(dl, sc, 60.f, 80.f, 580.f, 400.f);
  for (int i = 0; i < n && i < 8; i++) {
    const Slot& s = L.slots[static_cast<size_t>(i)];
    const float y = 92.f + 36.f * static_cast<float>(i);
    const bool sel = i == L.slotCursor;
    if (sel) kit::RowSel(dl, sc, 68.f, 572.f, y, 32.f);
    kit::Text(dl, sc, 96.f, y + 6.f, s.name, !s.ok ? kit::kDim : sel ? kit::kText : kit::kSoft, 0.85f, 180.f, 1);
    kit::Text(dl, sc, 290.f, y + 7.f, s.ok ? s.trainer : s.why, s.ok ? kit::kGold : kit::kDim, 0.8f, 270.f);
  }
  Hint(dl, sc, Tr("A: choose   B: back"));
  return true;
}

std::string Plain(const std::string& t) {
  for (char c : t) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return "";
  return t;
}
int MaxTeam() { return 6; }

bool CheckPickedTeam();
bool CheckPickedTeamEarly() { return CheckPickedTeam(); }
bool StepChoose(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  const bool fromSave = L.rules.team == "save";
  const int n = fromSave ? static_cast<int>(L.entries.size()) : static_cast<int>(L.species.size());
  const int total = n + 1;
  Move(in, L.cursor, total);
  if (in->repeat & PAD_BUTTON_LEFT) L.cursor = std::max(0, L.cursor - 8), PlayMenuSound(kTitleMenu, kSoundMove);
  if (in->repeat & PAD_BUTTON_RIGHT) L.cursor = std::min(total - 1, L.cursor + 8), PlayMenuSound(kTitleMenu, kSoundMove);
  if (Back(in)) {
    L.picked.clear();
    L.chosenSpecies.clear();
    if (fromSave && L.slots.size() > 1) L.step = Step::SavePick;
    else L.step = Step::Team, L.cursor = 0;
    return true;
  }
  const int count = fromSave ? static_cast<int>(L.picked.size()) : static_cast<int>(L.chosenSpecies.size());
  if (in->pressed & PAD_BUTTON_A) {
    if (L.cursor == n) {
      if (count >= 1 && fromSave && !CheckPickedTeamEarly()) {
        PlayMenuSound(kTitleMenu, kSoundBack);
        L.step = Step::TeamCheck;
        return true;
      }
      if (count >= 1) {
        PlayMenuSound(kTitleMenu, kSoundChoose);
        Send(xd::online::Msg("TEAM", std::to_string(count)));
        L.step = Step::Ready;
        L.cursor = 0;
        return true;
      }
      PlayMenuSound(kTitleMenu, kSoundBack);
    } else if (fromSave) {
      auto it = std::find(L.picked.begin(), L.picked.end(), L.cursor);
      if (it != L.picked.end()) { L.picked.erase(it); PlayMenuSound(kTitleMenu, kSoundBack); }
      else if (!L.entryWhy[static_cast<size_t>(L.cursor)].empty() || static_cast<int>(L.picked.size()) >= MaxTeam()) PlayMenuSound(kTitleMenu, kSoundBack);
      else { L.picked.push_back(L.cursor); PlayMenuSound(kTitleMenu, kSoundChoose); }
    } else {
      const uint16_t id = L.species[static_cast<size_t>(L.cursor)];
      auto it = std::find(L.chosenSpecies.begin(), L.chosenSpecies.end(), id);
      if (it != L.chosenSpecies.end()) { L.chosenSpecies.erase(it); PlayMenuSound(kTitleMenu, kSoundBack); }
      else if (static_cast<int>(L.chosenSpecies.size()) >= MaxTeam()) PlayMenuSound(kTitleMenu, kSoundBack);
      else { L.chosenSpecies.push_back(id); PlayMenuSound(kTitleMenu, kSoundChoose); }
    }
  }
  if (L.cursor < L.listTop) L.listTop = L.cursor;
  if (L.cursor >= L.listTop + 10) L.listTop = L.cursor - 9;
  Title(dl, sc, fromSave ? Tr("CHOOSE YOUR TEAM FROM YOUR SAVE") : Tr("PICK YOUR TEAM"));
  kit::Window(dl, sc, 40.f, 72.f, 430.f, 436.f);
  for (int r = 0; r < 10; r++) {
    const int i = L.listTop + r;
    if (i > n) break;
    const float y = 82.f + 35.f * static_cast<float>(r);
    const bool sel = i == L.cursor;
    if (i == n) { kit::Button(dl, sc, 95.f, y, 280.f, count >= 1 ? Tr("Done") : Tr("Done (pick at least one)"), sel, count >= 1); continue; }
    if (sel) kit::RowSel(dl, sc, 46.f, 424.f, y, 31.f);
    if (fromSave) {
      const xd::trade::Entry& e = L.entries[static_cast<size_t>(i)];
      const bool on = std::find(L.picked.begin(), L.picked.end(), i) != L.picked.end();
      const bool bad = !L.entryWhy[static_cast<size_t>(i)].empty();
      std::string where = e.box < 0 ? std::string(Tr("Party")) : Fmt(Tr("Box %d"), e.box + 1);
      const int order = on ? static_cast<int>(std::find(L.picked.begin(), L.picked.end(), i) - L.picked.begin()) + 1 : 0;
      if (on) kit::Chip(dl, sc, 384.f, y + 6.f, std::to_string(order), true);
      kit::Text(dl, sc, 76.f, y + 6.f, e.card.name + (e.card.name != e.card.species && !Plain(e.card.species).empty() ? " (" + e.card.species + ")" : ""), bad ? kit::kDim : on ? kit::kGood : sel ? kit::kText : kit::kSoft, 0.75f, 178.f, 1);
      kit::Text(dl, sc, 262.f, y + 7.f, Fmt(Tr("Lv %d"), static_cast<int>(e.card.level)), bad ? kit::kDim : kit::kGold, 0.72f, 58.f);
      kit::Text(dl, sc, 324.f, y + 8.f, where, kit::kSoft, 0.66f, 70.f);
    } else {
      const uint16_t id = L.species[static_cast<size_t>(i)];
      const auto at = std::find(L.chosenSpecies.begin(), L.chosenSpecies.end(), id);
      const bool on = at != L.chosenSpecies.end();
      if (on) kit::Chip(dl, sc, 384.f, y + 6.f, std::to_string(at - L.chosenSpecies.begin() + 1), true);
      kit::Text(dl, sc, 76.f, y + 6.f, SpeciesName(id), on ? kit::kGood : sel ? kit::kText : kit::kSoft, 0.78f, 310.f, 1);
    }
  }
  if (L.listTop > 0) kit::Arrow(dl, sc, 240.f, 78.f, true);
  if (L.listTop + 10 <= n) kit::Arrow(dl, sc, 240.f, 432.f, false);
  kit::TitledWindow(dl, sc, 438.f, 72.f, 604.f, 250.f, Fmt(Tr("Your team (%d/%d)"), count, MaxTeam()));
  for (int k = 0; k < count; k++) {
    std::string nm = fromSave ? L.entries[static_cast<size_t>(L.picked[static_cast<size_t>(k)])].card.name : SpeciesName(L.chosenSpecies[static_cast<size_t>(k)]);
    kit::Text(dl, sc, 462.f, 112.f + 21.f * static_cast<float>(k), std::to_string(k + 1) + "  " + nm, kit::kText, 0.7f, 118.f);
  }
  kit::Window(dl, sc, 438.f, 258.f, 604.f, 436.f);
  if (L.cursor < n && fromSave) {
    const xd::trade::Entry& e = L.entries[static_cast<size_t>(L.cursor)];
    const std::string& why = L.entryWhy[static_cast<size_t>(L.cursor)];
    if (!why.empty()) kit::Para(dl, sc, 461.f, 270.f, 120.f, why, kit::kBad, 0.62f, 16.f, 9);
    else {
      kit::Text(dl, sc, 460.f, 270.f, Fmt(Tr("Nature: %s"), Plain(e.card.nature).c_str()), kit::kGold, 0.6f, 120.f);
      for (int m = 0; m < 4; m++) kit::Text(dl, sc, 460.f, 290.f + 18.f * static_cast<float>(m), Plain(e.card.moves[m]).empty() ? "-" : e.card.moves[m], kit::kText, 0.62f, 120.f);
      kit::Text(dl, sc, 460.f, 366.f, Fmt(Tr("Item: %s"), (Plain(e.card.item).empty() ? std::string(Tr("none")) : e.card.item).c_str()), kit::kSoft, 0.6f, 120.f);
      if (L.rules.level != "asis") kit::Para(dl, sc, 461.f, 388.f, 120.f, Fmt(Tr("The room sets the level (%s)."), L.rules.level.c_str()), kit::kSoft, 0.58f, 15.f, 3);
    }
  } else if (L.cursor < n) {
    kit::Para(dl, sc, 461.f, 270.f, 120.f, Tr("The room sets its level and moves; the game gives it the moves a wild one of that level knows."), kit::kSoft, 0.6f, 16.f, 9);
  }
  Hint(dl, sc, Tr("A: pick / un-pick   Left / right: page   B: back"));
  return true;
}

bool StepTeam(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (L.rules.team == "save" || L.rules.team == "free12" || L.rules.team == "free14") {
    BeginTeam();
    return true;
  }
  if (Back(in)) { Send(xd::online::Msg("LEAVE")); Reset(); return true; }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    Send(xd::online::Msg("TEAM", "0"));
    L.step = Step::Ready;
    L.cursor = 0;
    return true;
  }
  Title(dl, sc, Tr("TEAMS"));
  kit::Window(dl, sc, 60.f, 96.f, 580.f, 300.f, true);
  std::string what = L.rules.team == "preset" ? Tr("The game gives you both a preset team.") :
                     L.rules.team == "draft" ? Tr("You and your friend take turns picking Pokemon from a shared pool. The draft starts when the battle starts.") :
                                               Tr("You are offered random Pokemon to pick from. The offers come when the battle starts.");
  kit::Para(dl, sc, 86.f, 120.f, 468.f, what, kit::kText, 0.85f, 26.f, 6);
  Hint(dl, sc, Tr("A: continue   B: leave the room"));
  return true;
}

bool PreviewWanted() {
  const std::string& t = L.rules.team;
  return L.rules.preview && (t == "save" || t == "free12" || t == "free14" || t == "preset");
}
std::vector<int> MyPreviewTeam() {
  std::vector<int> ids;
  const std::string& t = L.rules.team;
  if (t == "save") { for (int i : L.picked) ids.push_back(L.entries[static_cast<size_t>(i)].pokemon.species); }
  else if (t == "preset") { for (int id : (L.host ? std::vector<int>{3, 6, 9} : std::vector<int>{135, 134, 136})) ids.push_back(id); }
  else { for (uint16_t id : L.chosenSpecies) ids.push_back(id); }
  return ids;
}
std::string PreviewName(int id) {
  std::string n = xd::starter::SpeciesName(static_cast<uint32_t>(id));
  if (n.empty()) return "#" + std::to_string(id);
  for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return n;
}
int PreviewWant(int teamSize) { return std::max(1, std::min(L.rules.pick > 0 ? L.rules.pick : 6, teamSize)); }
void PreviewLock(const std::vector<int>& order) {
  L.pvOrder = order;
  L.pvLocked = true;
  unsigned char nonce[xd::online::kNonceBytes];
  xdnet::randomBytes(nonce, sizeof nonce);
  L.pvNonceHex = xd::online::ToHex(nonce, sizeof nonce);
  L.pvRecord = xd::online::JoinInts(order);
  Send(xd::online::Msg("PVC", xd::online::Commit(L.pvNonceHex, L.pvRecord)));
  Note("team preview: my pick " + L.pvRecord + " is locked in (committed)");
}
void PreviewApply(const std::vector<int>& friendOrder) {
  const std::string& t = L.rules.team;
  if (t == "save") { std::vector<int> np; for (int pos : L.pvOrder) np.push_back(L.picked[static_cast<size_t>(pos)]); L.picked = np; }
  else if (t == "preset") { L.pvPresetOrder.clear(); for (int pos : L.pvOrder) L.pvPresetOrder.push_back(static_cast<uint16_t>(L.pvMine[static_cast<size_t>(pos)])); }
  else { std::vector<uint16_t> ns; for (int pos : L.pvOrder) ns.push_back(L.chosenSpecies[static_cast<size_t>(pos)]); L.chosenSpecies = ns; }
  L.pvFriendPicked.clear();
  for (int pos : friendOrder) L.pvFriendPicked.push_back(static_cast<uint16_t>(L.peer.pvIds[static_cast<size_t>(pos)]));
  L.peer.teamCount = static_cast<int>(friendOrder.size());
  L.pvDone = true;
  Note("team preview: done, mine " + xd::online::JoinInts(L.pvOrder) + " of " + std::to_string(L.pvMine.size()) + ", the friend's " + xd::online::JoinInts(friendOrder) + " of " + std::to_string(L.peer.pvIds.size()));
}

bool StepPreview(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (!EnsureRules()) { L.step = Step::Message; return true; }
  if (!L.pvStarted) {
    L.pvStarted = true;
    L.pvMine = MyPreviewTeam();
    L.pvChosen.clear();
    L.cursor = 0;
    const char* testMs = std::getenv("XD_ONLINE_TEST_PREVIEW_MS");
    L.pvDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(testMs ? std::atoi(testMs) : (L.rules.previewSeconds > 0 ? L.rules.previewSeconds * 1000 : 0));
    Send(xd::online::Msg("PV", xd::online::JoinInts(L.pvMine)));
    L.pvSent = true;
  }
  const int mineN = static_cast<int>(L.pvMine.size());
  const int want = PreviewWant(mineN);
  const bool haveFriend = L.peer.pvTeam && !L.peer.pvIds.empty();
  const int friendN = static_cast<int>(L.peer.pvIds.size());
  const int friendWant = PreviewWant(friendN);
  const bool timed = L.rules.previewSeconds > 0 || std::getenv("XD_ONLINE_TEST_PREVIEW_MS") != nullptr;
  const long leftMs = timed ? static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(L.pvDeadline - std::chrono::steady_clock::now()).count()) : (1L << 30);
  if (Back(in)) { Send(xd::online::Msg("LEAVE")); Reset(); return true; }
  if (haveFriend && !L.pvLocked) {
    if (const char* t = std::getenv("XD_ONLINE_TEST_PREVIEW")) {
      if (std::strcmp(t, "timeout") != 0) {
        std::vector<int> order = xd::online::SplitInts(t);
        if (!xd::online::PickValid(order, mineN, want)) order = xd::online::TimeoutPick(order, mineN, want);
        PreviewLock(order);
      }
    }
  }
  if (haveFriend && !L.pvLocked) {
    if (timed && leftMs <= 0) { Note("team preview: time is up, the timeout rule picks"); PreviewLock(xd::online::TimeoutPick(L.pvChosen, mineN, want)); }
    else {
      Move(in, L.cursor, mineN + 1);
      if (in->pressed & PAD_BUTTON_A) {
        if (L.cursor == mineN) {
          if (static_cast<int>(L.pvChosen.size()) == want) { PlayMenuSound(kTitleMenu, kSoundChoose); PreviewLock(L.pvChosen); }
          else PlayMenuSound(kTitleMenu, kSoundBack);
        } else {
          auto it = std::find(L.pvChosen.begin(), L.pvChosen.end(), L.cursor);
          if (it != L.pvChosen.end()) { L.pvChosen.erase(it); PlayMenuSound(kTitleMenu, kSoundBack); }
          else if (static_cast<int>(L.pvChosen.size()) >= want) PlayMenuSound(kTitleMenu, kSoundBack);
          else { L.pvChosen.push_back(L.cursor); PlayMenuSound(kTitleMenu, kSoundChoose); }
        }
      }
    }
  }
  if (L.pvLocked && L.peer.pvCommit && !L.pvRevealSent) { Send(xd::online::Msg("PVR", L.pvNonceHex + " " + L.pvRecord)); L.pvRevealSent = true; }
  if (L.pvRevealSent && L.peer.pvReveal) {
    const std::vector<int> theirs = xd::online::SplitInts(L.peer.pvList);
    if (!xd::online::Verify(L.peer.pvCommitHex, L.peer.pvNonce, L.peer.pvList) || !xd::online::PickValid(theirs, friendN, friendWant)) {
      L.message = Tr("Your friend's team preview pick does not match what they locked in. The battle was not started.");
      Send(xd::online::Msg("LEAVE"));
      L.step = Step::Message;
      return true;
    }
    PreviewApply(theirs);
    L.step = Step::Ready;
    return true;
  }
  Title(dl, sc, Tr("TEAM PREVIEW"));
  kit::TitledWindow(dl, sc, 40.f, 72.f, 330.f, 380.f, Tr("Your team"));
  for (int k = 0; k < mineN; k++) {
    const float y = 108.f + 36.f * static_cast<float>(k);
    const bool sel = !L.pvLocked && L.cursor == k;
    if (sel) kit::RowSel(dl, sc, 46.f, 324.f, y, 31.f);
    const auto at = std::find(L.pvChosen.begin(), L.pvChosen.end(), k);
    const bool on = L.pvLocked ? std::find(L.pvOrder.begin(), L.pvOrder.end(), k) != L.pvOrder.end() : at != L.pvChosen.end();
    if (on) {
      const int order = L.pvLocked ? static_cast<int>(std::find(L.pvOrder.begin(), L.pvOrder.end(), k) - L.pvOrder.begin()) + 1 : static_cast<int>(at - L.pvChosen.begin()) + 1;
      kit::Chip(dl, sc, 282.f, y + 6.f, std::to_string(order), true);
    }
    kit::Text(dl, sc, 62.f, y + 6.f, PreviewName(L.pvMine[static_cast<size_t>(k)]), on ? kit::kGood : sel ? kit::kText : kit::kSoft, 0.78f, 210.f, 1);
  }
  kit::TitledWindow(dl, sc, 340.f, 72.f, 610.f, 380.f, Tr("Your friend's team"));
  if (!haveFriend) kit::Text(dl, sc, 356.f, 120.f, Tr("Waiting for your friend..."), kit::kSoft, 0.72f, 240.f);
  for (int k = 0; k < friendN; k++) kit::Text(dl, sc, 356.f, 108.f + 36.f * static_cast<float>(k) + 6.f, PreviewName(L.peer.pvIds[static_cast<size_t>(k)]), kit::kText, 0.78f, 240.f, 1);
  const std::string timeText = timed && leftMs < (1L << 29) ? Fmt(Tr("Time left: %d s"), static_cast<int>(std::max(0L, (leftMs + 999) / 1000))) : std::string();
  kit::Text(dl, sc, 46.f, 386.f, Fmt(Tr("Pick %d of your %d, the one you lead with first."), want, mineN) + (timeText.empty() ? "" : "   " + timeText), kit::kGold, 0.7f, 560.f);
  if (L.pvLocked) kit::Text(dl, sc, 46.f, 408.f, L.peer.pvCommit ? Tr("Revealing the picks...") : Tr("Locked in. Waiting for your friend..."), kit::kGood, 0.72f, 560.f);
  else if (haveFriend) kit::Button(dl, sc, 214.f, 404.f, 212.f, static_cast<int>(L.pvChosen.size()) == want ? Tr("Lock in") : Fmt(Tr("Pick %d more"), want - static_cast<int>(L.pvChosen.size())), L.cursor == mineN, static_cast<int>(L.pvChosen.size()) == want);
  Hint(dl, sc, Tr("A: pick / un-pick   B: leave the room"));
  return true;
}

std::string FormatSummary(const engine::rulesets::Format& f) {
  namespace tx = xd::rulesets::text;
  std::string s = f.ruleSet == "xd" ? std::string(Tr("XD rules")) : f.ruleSet == "gen4" ? std::string(Tr("Gen 4 rules")) : f.ruleSet.empty() ? std::string(Tr("your own rules")) : std::string(tx::RuleSetShort(f.ruleSet));
  int top = 3;
  for (int g : f.gens) top = std::max(top, g);
  s += std::string(" - ") + tx::PokemonShort(top);
  s += std::string(" - ") + (f.style == "double" ? Tr("double") : Tr("single"));
  s += " - " + (f.level.kind == engine::rulesets::LevelRule::Kind::SetAll ? Fmt(Tr("level %d"), f.level.a) : std::string(Tr("levels as they are")));
  if (!f.clauses.empty()) s += " - " + Fmt(Tr("%d clauses"), static_cast<int>(f.clauses.size()));
  if (f.teamPreview) s += " - " + Fmt(Tr("pick %d of %d"), f.pick > 0 ? f.pick : f.teamMax, f.teamMax);
  return s;
}

bool StepFormatList(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  namespace tx = xd::rulesets::text;
  const int n = static_cast<int>(L.pc.formats.size()) + 1;
  Move(in, L.fmtCursor, n);
  if (Back(in)) { L.step = Step::Settings; return true; }
  if (in->pressed & PAD_BUTTON_A) {
    const std::string id = L.fmtCursor < n - 1 ? L.pc.formats[static_cast<size_t>(L.fmtCursor)].id : std::string();
    if (xd::online::SelectFormat(L.rules, id, L.pc)) { PlayMenuSound(kTitleMenu, kSoundChoose); L.step = Step::Settings; return true; }
    PlayMenuSound(kTitleMenu, kSoundBack);
  }
  Title(dl, sc, Tr("MAKE A ROOM"));
  kit::Window(dl, sc, 30.f, 70.f, 610.f, 372.f, true);
  kit::Text(dl, sc, 52.f, 84.f, Tr("Choose a format"), kit::kGold, 0.9f, 400.f, 1);
  for (int i = 0; i < n; i++) {
    const float y = 114.f + 29.f * static_cast<float>(i) + (i == n - 1 ? 8.f : 0.f);
    const bool sel = i == L.fmtCursor;
    const bool custom = i == n - 1;
    const bool enabled = custom || L.pc.formats[static_cast<size_t>(i)].enabled;
    if (sel) kit::RowSel(dl, sc, 38.f, 602.f, y, 27.f);
    const std::string name = custom ? std::string(Tr("Custom...")) : L.pc.formats[static_cast<size_t>(i)].name;
    kit::Text(dl, sc, 56.f, y + 4.f, name, enabled ? (sel ? kit::kText : kit::kSoft) : kit::kDim, 0.78f, 190.f, 1);
    std::string right = custom ? std::string(Tr("set every row yourself")) : enabled ? FormatSummary(*xd::rulesets::FindFormat(L.pc.formats[static_cast<size_t>(i)].id)) : std::string(Tr("needs the Gen 4 pack"));
    kit::Text(dl, sc, 232.f, y + 6.f, right, enabled ? kit::kSoft : kit::kDim, 0.5f, 362.f);
  }
  kit::Window(dl, sc, 30.f, 384.f, 610.f, 432.f);
  const std::string info = L.fmtCursor < n - 1 ? std::string(tx::FormatInfo(L.pc.formats[static_cast<size_t>(L.fmtCursor)].id)) : std::string(Tr("Custom: the rows of the room are yours, no format."));
  kit::Para(dl, sc, 46.f, 391.f, 548.f, info, kit::kText, 0.6f, 15.f, 3);
  Hint(dl, sc, Tr("Up / down: choose   A: pick   B: back"));
  return true;
}

std::string ProblemText(const engine::rulesets::MemberProblem& p) {
  namespace tx = xd::rulesets::text;
  const auto name = [&](const char* fallback) { const std::string a = Plain(p.arg); return a.empty() ? std::string(fallback) : a; };
  if (p.kind == "item") return Tr("no two Pokemon hold the same item");
  if (p.kind == "species") return Tr("no two Pokemon of one species");
  if (p.kind == "category") return Fmt(Tr("%s: not allowed here"), tx::CategoryName(p.arg));
  if (p.kind == "evasion") return Fmt(Tr("knows %s (evasion clause)"), name(Tr("a move")).c_str());
  if (p.kind == "ohko") return Fmt(Tr("knows %s (OHKO clause)"), name(Tr("a move")).c_str());
  if (p.kind == "banned_species") return Tr("banned in this format");
  if (p.kind == "gen") return Fmt(Tr("generation %s Pokemon are not allowed here"), p.arg.c_str());
  if (p.kind == "banned_item") return Fmt(Tr("holds %s, which is banned"), name(Tr("this item")).c_str());
  if (p.kind == "banned_move") return Fmt(Tr("knows %s, which is banned"), name(Tr("a move")).c_str());
  if (p.kind == "banned_ability") return Fmt(Tr("has the ability %s, which is banned"), name(Tr("an ability")).c_str());
  if (p.kind == "level") return Fmt(Tr("level %s is above the room's limit"), p.arg.c_str());
  return Tr("not allowed here");
}
bool CheckPickedTeam() {
  L.tcProblems.clear();
  if (L.rules.format.empty() || L.rules.team != "save" || !L.rulesData) return true;
  std::vector<xd::save::Pokemon> team;
  for (int i : L.picked) team.push_back(L.entries[static_cast<size_t>(i)].pokemon);
  if (std::getenv("XD_ONLINE_TEST_TEAM_FIXTURE") && team.size() >= 3) {
    team[0].heldItem = team[1].heldItem = 17;
    team[2].moves[0].id = 107;
  }
  L.tcProblems = xd::online::CheckTeamMembers(team, L.rulesData->rules, L.rules);
  return L.tcProblems.empty();
}

bool StepTeamCheck(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (Back(in)) { Send(xd::online::Msg("LEAVE")); Reset(); return true; }
  if (in->pressed & PAD_BUTTON_A) { PlayMenuSound(kTitleMenu, kSoundChoose); L.picked.clear(); BeginTeam(); return true; }
  kit::Header(dl, sc, Tr("YOUR TEAM"), FormatTitle(L.rules));
  const int count = static_cast<int>(L.picked.size());
  kit::TitledWindow(dl, sc, 30.f, 66.f, 610.f, 66.f + 54.f + 56.f * static_cast<float>(std::min(count, 6)) - 8.f, Tr("Checked against the room's rules"));
  std::set<int> bad;
  for (const auto& p : L.tcProblems) if (p.member >= 0) bad.insert(p.member);
  for (int k = 0; k < count && k < 6; k++) {
    const xd::trade::Entry& e = L.entries[static_cast<size_t>(L.picked[static_cast<size_t>(k)])];
    const float y = 106.f + 56.f * static_cast<float>(k);
    const bool isBad = bad.count(k) > 0;
    if (isBad) kit::RowSel(dl, sc, 38.f, 602.f, y, 50.f, false);
    kit::Text(dl, sc, 60.f, y + 14.f, e.card.name, kit::kText, 0.78f, 190.f, 1);
    kit::Text(dl, sc, 262.f, y + 17.f, Plain(e.card.item).empty() ? std::string("-") : e.card.item, kit::kSoft, 0.62f, 110.f);
    if (!isBad) { kit::Text(dl, sc, 392.f, y + 14.f, Tr("OK"), kit::kGood, 0.78f, 40.f, 1); continue; }
    for (const auto& p : L.tcProblems) {
      if (p.member != k) continue;
      kit::Para(dl, sc, 392.f, y + 6.f, 200.f, ProblemText(p), kit::kBad, 0.6f, 15.f, 3);
      break;
    }
  }
  const float by = 66.f + 54.f + 56.f * static_cast<float>(std::min(count, 6)) + 4.f;
  kit::Window(dl, sc, 30.f, by, 610.f, by + 74.f, true);
  int broken = static_cast<int>(bad.size());
  kit::Text(dl, sc, 46.f, by + 8.f, broken > 0 ? Fmt(Tr("This team can't enter: %d Pokemon break the room's rules."), broken) : Tr("This team can't enter: it does not fit the room's rules."), kit::kBad, 0.78f, 560.f, 1);
  kit::Para(dl, sc, 46.f, by + 32.f, 560.f, Tr("Change the team in your save, or pick another save. Your Pokemon are never changed by the room."), kit::kSoft, 0.6f, 15.f, 2);
  Hint(dl, sc, Tr("A: pick another save   B: leave the room"));
  return true;
}

void StartDrive(bool rematch);

void Go() {
  xd::online::LobbyHandOver h;
  h.host = L.host;
  h.conn = L.conn.release();
  h.rules = L.rules;
  h.code = L.code;
  if (L.rules.team == "save") {
    h.saveFile = L.card.file.string();
    for (int i : L.picked) {
      xd::online::Pick p;
      p.box = L.entries[static_cast<size_t>(i)].box;
      p.slot = L.entries[static_cast<size_t>(i)].slot;
      h.picks.push_back(p);
    }
    h.mineCount = static_cast<int>(L.picked.size());
    h.friendCount = std::max(1, L.peer.teamCount);
  } else if (L.rules.team == "free12" || L.rules.team == "free14") {
    h.freeSpecies = L.chosenSpecies;
    h.mineCount = static_cast<int>(L.chosenSpecies.size());
    h.friendCount = std::max(1, L.peer.teamCount);
  } else if (L.rules.team == "preset") {
    h.freeSpecies = L.host ? std::vector<uint16_t>{3, 6, 9} : std::vector<uint16_t>{135, 134, 136};
    h.mineCount = h.friendCount = 3;
  } else {
    xd::online::draft::Settings ds;
    xd::online::draft::ParseSettings(L.rules.team == "offer" ? "offer" : "snake", L.rules.draft, ds, nullptr);
    h.mineCount = h.friendCount = ds.teamSize;
  }
  h.friendPreview = L.pvFriendPicked;
  if (L.pvDone && L.rules.team == "preset") { h.freeSpecies = L.pvPresetOrder; h.mineCount = static_cast<int>(L.pvPresetOrder.size()); }
  if (L.pvDone) h.friendCount = static_cast<int>(L.pvFriendPicked.size());
  xd::online::SetVsNonce(L.host ? L.myNonce : L.peer.nonce);
  Note("both ready: the battle starts (" + xd::online::RulesText(h.rules) + ")");
  xd::online::SetLobbyRoom(h);
  L.started = true;
  StartDrive(false);
  gClose = true;
}

bool StepReady(ImDrawList* dl, const Screen& sc, const PortMenuInput* in) {
  if (!L.iReady) Move(in, L.cursor, 2);
  if (Back(in)) {
    if (L.iReady) { L.iReady = false; Send(xd::online::Msg("UNREADY")); }
    else { Send(xd::online::Msg("LEAVE")); Reset(); }
    return true;
  }
  if ((in->pressed & PAD_BUTTON_A) && !L.iReady) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (L.cursor == 1) {
      L.sub = PortXD_TrainerCardPage();
      if (L.sub->open) L.sub->open(L.sub->ctx);
      return true;
    }
    L.iReady = true;
    if (L.host) L.myNonce = xd::online::NewNonce();
    { char nb[16]; std::snprintf(nb, sizeof nb, "%x", static_cast<unsigned>(L.host ? L.myNonce : 0u)); Send(xd::online::Msg("READY", L.host ? std::string(nb) : std::string())); }
  }
  if (L.iReady && L.peer.ready && PreviewWanted() && !L.pvDone) { L.step = Step::Preview; return true; }
  if (L.iReady && L.peer.ready) {
    if (!L.cardSent) { L.cardSent = true; Send(xd::card::Encode(xd::card::MyCard())); }
    if (L.haveFriendCard) {
      xd::card::Card theirs, mine = xd::card::MyCard();
      std::string why;
      if (!xd::card::Decode(L.friendCardLine, theirs, &why)) theirs = xd::card::Card();
      xd::card::SetRoom(L.host, mine, theirs);
      Go();
      return false;
    }
  }
  Title(dl, sc, Tr("READY TO BATTLE?"));
  float y = DrawRoom(dl, sc, 72.f) + 6.f;
  const int mine = L.rules.team == "save" ? static_cast<int>(L.picked.size()) : L.rules.team.compare(0, 4, "free") == 0 ? static_cast<int>(L.chosenSpecies.size()) : 0;
  std::string line = mine > 0 ? Fmt(Tr("Your team: %d Pokemon."), mine) : Fmt(Tr("Your team: %s."), L.rules.team.c_str());
  kit::Window(dl, sc, 40.f, y, 600.f, y + 70.f);
  kit::Text(dl, sc, 64.f, y + 9.f, line, kit::kText, 0.72f, 512.f);
  std::string theirs = L.peer.teamCount < 0 ? std::string(Tr("Your friend is choosing a team...")) : L.peer.teamCount > 0 ? Fmt(Tr("Your friend's team is chosen (%d Pokemon)."), L.peer.teamCount) : std::string(Tr("Your friend's team is chosen."));
  kit::Text(dl, sc, 64.f, y + 28.f, theirs, L.peer.teamCount < 0 ? kit::kSoft : kit::kGood, 0.72f, 512.f);
  kit::Text(dl, sc, 64.f, y + 47.f, L.peer.ready ? Tr("Your friend is ready.") : Tr("Your friend is not ready yet."), L.peer.ready ? kit::kGood : kit::kSoft, 0.72f, 512.f);
  const float by = std::max(358.f, y + 76.f);
  if (L.iReady) {
    kit::Ring(dl, sc, 194.f, by + 16.f, 14.f);
    kit::Button(dl, sc, 216.f, by, 240.f, Tr("Waiting for your friend..."), false, false);
  } else {
    Buttons(dl, sc, by, {Tr("Ready"), Tr("Trainer card")}, L.cursor, 220.f);
  }
  Hint(dl, sc, L.iReady ? Tr("B: not ready") : Tr("A: choose   B: leave the room"));
  return true;
}

void TestPace() {
  static int ms = -1;
  if (ms < 0) ms = std::getenv("XD_ONLINE_LOBBY_FRAME_MS") ? std::atoi(std::getenv("XD_ONLINE_LOBBY_FRAME_MS")) : 0;
  if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool Frame(void*, const PortMenuInput* in) {
  TestPace();
  const Screen sc = ScreenNow();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  L.age += 1.f / 60.f;
  kit::Backdrop(dl, sc);
  if (L.sub != nullptr) {
    Follow();
    if (!L.sub->frame(L.sub->ctx, in)) {
      if (L.sub->close) L.sub->close(L.sub->ctx);
      L.sub = nullptr;
      PortInput_LatchHeld();
    }
    return true;
  }
  Follow();
  switch (L.step) {
  case Step::Menu: return StepMenu(dl, sc, in);
  case Step::Settings: return StepSettings(dl, sc, in);
  case Step::Details: return StepDetails(dl, sc, in);
  case Step::Typing: return StepTyping(dl, sc, in);
  case Step::Connect: return StepConnect(dl, sc, in);
  case Step::HostView: return StepHostView(dl, sc, in);
  case Step::JoinView: return StepJoinView(dl, sc, in);
  case Step::SavePick: return StepSavePick(dl, sc, in);
  case Step::Choose: return StepChoose(dl, sc, in);
  case Step::Team: return StepTeam(dl, sc, in);
  case Step::Ready: return StepReady(dl, sc, in);
  case Step::Preview: return StepPreview(dl, sc, in);
  case Step::FormatList: return StepFormatList(dl, sc, in);
  case Step::TeamCheck: return StepTeamCheck(dl, sc, in);
  case Step::Restart: return StepRestart(dl, sc, in);
  case Step::Message: return StepMessage(dl, sc, in);
  }
  return false;
}

const PortMenuPage kPage{"online battle", Open, Frame, Close, nullptr};

struct ResultState {
  int cursor = 0;
  bool iRematch = false;
  bool host = false;
  uint32_t myNonce = 0;
  Peer peer;
  std::string outcome, rules;
  std::string ended;
  int shots = 0;
};
ResultState R;

void OpenResult(void*) {
  R = ResultState();
  const xd::online::BattleSummary b = xd::online::LobbySummary();
  const xd::online::Outcome oc = xd::online::OutcomeOf(b.result, b.host, b.desync);
  R.host = b.host;
  R.outcome = TrOutcomeText(oc);
  R.rules = b.rulesText;
  Note("result screen: " + xd::online::OutcomeText(oc));
  if (b.dead && b.result != 1 && !b.desync) R.outcome = TrOutcomeText(xd::online::Outcome::ConnectionLost);
}

bool ResultFrame(void*, const PortMenuInput* in) {
  TestPace();
  const Screen sc = ScreenNow();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  kit::Backdrop(dl, sc);
  xd::online::BattleTransport* link = xd::online::LobbyLink();
  if (link != nullptr && R.ended.empty()) {
    uint8_t buf[512];
    for (int guard = 0; guard < 64; guard++) {
      const int n = link->Receive(buf, sizeof buf - 1);
      if (n < 0) { R.ended = Tr("The connection to your friend ended."); break; }
      if (n == 0) break;
      xd::online::Apply(R.peer, std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n)));
    }
    if (R.peer.left) R.ended = Tr("Your friend left the room.");
  } else if (link == nullptr) {
    R.ended = Tr("The room is closed.");
  }
  const bool canRematch = R.ended.empty() && !xd::online::LobbySummary().dead;
  const int n = canRematch ? 2 : 1;
  Move(in, R.cursor, n);
  if (R.cursor >= n) R.cursor = n - 1;
  const bool leave = canRematch ? R.cursor == 1 : true;
  if ((in->pressed & PAD_BUTTON_B)) { PlayMenuSound(kTitleMenu, kSoundBack); xd::online::LobbyLeave(); return false; }
  if (in->pressed & PAD_BUTTON_A) {
    PlayMenuSound(kTitleMenu, kSoundChoose);
    if (leave) { xd::online::LobbyLeave(); return false; }
    if (!R.iRematch) {
      R.iRematch = true;
      if (R.host) R.myNonce = xd::online::NewNonce();
      char nb[16];
      std::snprintf(nb, sizeof nb, "%x", static_cast<unsigned>(R.host ? R.myNonce : 0u));
      const std::string m = xd::online::Msg("REMATCH", R.host ? std::string(nb) : std::string());
      link->Send(reinterpret_cast<const uint8_t*>(m.data()), m.size());
    }
  }
  if (canRematch && R.iRematch && R.peer.rematch) {
    xd::online::SetVsNonce(R.host ? R.myNonce : R.peer.nonce);
    xd::online::LobbyRematchReset();
    StartDrive(true);
    return false;
  }
  kit::Header(dl, sc, Tr("BATTLE OVER"));
  kit::Window(dl, sc, 60.f, 96.f, 580.f, 262.f, true);
  kit::Para(dl, sc, 86.f, 120.f, 468.f, R.outcome, kit::kText, 0.95f, 28.f, 3, 1);
  if (!R.ended.empty()) kit::Para(dl, sc, 86.f, 206.f, 468.f, R.ended, kit::kBad, 0.8f, 22.f, 2);
  else if (R.iRematch) {
    kit::Ring(dl, sc, 100.f, 220.f, 12.f);
    kit::Text(dl, sc, 122.f, 210.f, R.peer.rematch ? Tr("Starting the rematch...") : Tr("Waiting for your friend to answer..."), kit::kSoft, 0.8f, 430.f);
  } else if (R.peer.rematch) kit::Text(dl, sc, 86.f, 210.f, Tr("Your friend would like a rematch."), kit::kGood, 0.8f, 468.f);
  const char* items[2] = {"Rematch", "Leave"};
  std::vector<std::string> labels;
  for (int i = 0; i < n; i++) labels.push_back(Tr(canRematch ? items[i] : items[1]));
  Buttons(dl, sc, 290.f, labels, R.cursor, 240.f);
  kit::Text(dl, sc, 64.f, 402.f, Tr("Rematch: the same room and settings again."), kit::kSoft, 0.7f, 512.f);
  kit::HintBar(dl, sc, Tr("A: choose   B: back"));
  return true;
}
void CloseResult(void*) {}
const PortMenuPage kResultPage{"online result", OpenResult, ResultFrame, CloseResult, nullptr};

struct Driver {
  bool on = false;
  bool rematch = false;
  int stage = 0;
  long polls = 0, stageStart = 0, holdUntil = 0, nextPress = 0;
  uint16_t hold = 0;
  bool leftFight = false;
  int scheduleIdx = 0;
};
Driver D;
constexpr s16 kTitleVs = 2;

void StartDrive(bool rematch) {
  D = Driver();
  D.on = true;
  PortInput_SetPadFilter(Filter);
  D.rematch = rematch;
  D.stage = rematch ? 10 : 0;
  Note(rematch ? "driver: back into a battle for the rematch" : "driver: into VS MODE");
  static RoomRules sRules;
  if (!rematch) sRules = L.rules;
  std::vector<std::string> chips;
  chips.push_back(sRules.ruleSet == "gen4pokemon" ? Tr("Gen 4 Pokemon") : sRules.ruleSet == "gen4" ? Tr("Gen 4 rules") : sRules.ruleSet == "gen4mt" ? Tr("Gen 4 with modern types") : L.pc.edition == "xg" ? Tr("Classic XG") : Tr("Classic XD"));
  chips.push_back(sRules.style == "double" ? Tr("Double battle") : Tr("Single battle"));
  if (sRules.split) chips.push_back(Tr("Split on"));
  ls::CoverBattle(chips, rematch);
}

bool DriveDirect(PADStatus* status, const WinWorkLayout* w) {
  (void)status;
  if (!D.on) return false;
  D.polls++;
  if (D.polls > 60000) { D.on = false; Note("driver: gave up"); return false; }
  if (D.polls % 600 == 0 && !(IsTitleMenu(w) && w->state == 2)) Note("driver: waiting for the title menu (active window " + std::to_string(w ? w->menuID : -1) + ", state " + std::to_string(w ? w->state : -1) + ")");
  if (!(IsTitleMenu(w) && w->state == 2 && w->pad_a4 == 0)) return false;
  D.on = false;
  sTitleSelected = sTitleMods = sTitleOnline = false;
  Note(D.rematch ? "starting the rematch battle" : "starting the battle");
  if (!xd::online::LobbyStartBattleThread()) Note("the battle could not be started");
  return false;
}

bool DriveTimed(PADStatus* status, const WinWorkLayout* w);
bool Drive(PADStatus* status, const WinWorkLayout* w) {
  static int direct = -1;
  if (direct < 0) direct = std::getenv("XD_ONLINE_DIRECT_START") != nullptr ? 1 : 0;
  return direct ? DriveDirect(status, w) : DriveTimed(status, w);
}

bool DriveTimed(PADStatus* status, const WinWorkLayout* w) {
  if (!D.on) return false;
  D.polls++;
  if (D.polls > 60000) { D.on = false; Note("driver: gave up"); return false; }
  uint16_t press = 0;
  if (D.polls < D.holdUntil) press = D.hold;
  auto push = [&](uint16_t b, int polls) { D.hold = b; D.holdUntil = D.polls + polls; press = b; };
  const bool fight = xd::online::InFight();
  if (D.stage == 0 && D.polls % 600 == 0 && !(IsTitleMenu(w) && w->state == 2)) Note("driver: waiting for the title menu (active window " + std::to_string(w ? w->menuID : -1) + ", state " + std::to_string(w ? w->state : -1) + ")");
  if (D.stage == 0 && w != nullptr && w->menuID == 0x118 && w->state == 2 && w->pad_a4 == 0) {
    const_cast<WinWorkLayout*>(w)->cursor = 1;
    D.stage = 1; D.stageStart = D.polls; D.scheduleIdx = 0; Note("driver: already in the VS MODE submenu");
  }
  if (D.stage == 0) {
    if (IsTitleMenu(w) && w->state == 2 && w->pad_a4 == 0) {
      sTitleSelected = sTitleMods = sTitleOnline = false;
      int vs = -1;
      for (int i = 0; i < 5; i++) if (menuTitleGetSelect(i) == kTitleVs) vs = i;
      static bool logged = false;
      if (!logged) {
        logged = true;
        Note("title selects: " + std::to_string(menuTitleGetSelect(0)) + " " + std::to_string(menuTitleGetSelect(1)) + " " + std::to_string(menuTitleGetSelect(2)) + " " +
             std::to_string(menuTitleGetSelect(3)) + " " + std::to_string(menuTitleGetSelect(4)) + ", cursor " + std::to_string(w->cursor));
      }
      if (D.polls >= D.nextPress && D.polls >= D.holdUntil) {
        if (vs < 0 || w->cursor == vs) { push(PAD_BUTTON_A, 6); D.stage = 1; D.stageStart = D.polls; D.scheduleIdx = 0; Note("driver: A on VS MODE"); }
        else { push(vs > w->cursor ? PAD_BUTTON_RIGHT : PAD_BUTTON_LEFT, 6); D.nextPress = D.polls + 24; }
      }
    }
  } else if (D.stage == 1) {
    static const struct { long at; uint16_t b; } kSched[] = {{60, PAD_BUTTON_RIGHT}, {80, PAD_BUTTON_RIGHT}, {110, PAD_BUTTON_A}};
    xd::online::LobbyApplyVsStyle();
    const long t = D.polls - D.stageStart;
    if (D.scheduleIdx < 3 && t >= kSched[D.scheduleIdx].at) { push(kSched[D.scheduleIdx].b, 6); D.scheduleIdx++; }
    else if (D.scheduleIdx >= 3 && t >= 230 + 120 * static_cast<long>(D.scheduleIdx - 3) && D.polls >= D.holdUntil) { push(PAD_BUTTON_A, 6); D.scheduleIdx++; }
    if (fight) { D.on = false; Note("driver: the battle has started"); }
  } else if (D.stage == 10) {
    if (!fight) D.leftFight = true;
    if (D.leftFight && D.polls >= D.nextPress && D.polls >= D.holdUntil) { push(PAD_BUTTON_A, 6); D.nextPress = D.polls + 100; }
    if (D.leftFight && fight) { D.on = false; Note("driver: the rematch battle has started"); }
  }
  if (press != 0) {
    for (int i = 0; i < PAD_MAX_CONTROLLERS; i++) Neutral(status[i]);
    if (status[0].err == PAD_ERR_NONE) status[0].button |= press;
  }
  return false;
}

}
