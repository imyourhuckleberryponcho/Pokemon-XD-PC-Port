#include <xd_legality_disc.h>
#include <xd_save_write.h>
#include <xd_trade_online.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cctype>
#include <thread>

using namespace xd::trade;

static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <disc> <card folder|save.gci> list|trade --give N [options]\n", argv[0]);
    return 2;
  }
  std::string cmd = argv[3], code, dieAt, resultFile;
  int give = -1, seconds = 90;
  bool noConfirm = false, forceOffer = false, fromCheats = false;
  xdnet::ConnectionConfig cfg;
  bool customNet = false;
  for (int i = 4; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--give") give = std::atoi(next().c_str());
    else if (a == "--code") code = next();
    else if (a == "--direct") { cfg.directPeers.push_back(next()); customNet = true; }
    else if (a == "--local-port") { cfg.localPort = static_cast<uint16_t>(std::atoi(next().c_str())); customNet = true; }
    else if (a == "--no-go-between") { cfg.useGoBetween = false; customNet = true; }
    else if (a == "--die-at") dieAt = next();
    else if (a == "--no-confirm") noConfirm = true;
    else if (a == "--cheats") fromCheats = true;
    else if (a == "--force-offer") forceOffer = true;
    else if (a == "--seconds") seconds = std::atoi(next().c_str());
    else if (a == "--result") resultFile = next();
  }
  std::string error;
  xd::legality::DiscRules disc;
  if (!xd::legality::LoadRulesFromDisc(argv[1], disc, error)) { std::fprintf(stderr, "disc: %s\n", error.c_str()); return 2; }
  CardSave card;
  if (!LoadCardSave(argv[2], card, error)) { std::fprintf(stderr, "save: %s\n", error.c_str()); return 2; }
  Config tc;
  tc.rules = &disc.rules;
  tc.names = disc.names.get();
  tc.fromCheats = fromCheats;
  Trader trader(tc);
  if (!trader.Load(card.data, error)) { std::fprintf(stderr, "save: %s\n", error.c_str()); return 2; }

  if (cmd == "forge") {
    if (argc < 7) { std::fprintf(stderr, "forge <party place 1-6> <species name> <level> [item name]\n"); return 2; }
    using xd::save::Read16; using xd::save::Write16; using xd::save::Write32;
    const int place = std::atoi(argv[4]) - 1;
    std::string want = argv[5];
    const int lv = std::atoi(argv[6]);
    const std::string itemName = (argc > 7 && std::strncmp(argv[7], "--", 2) != 0) ? argv[7] : "";
    auto lower = [](std::string x) { for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return x; };
    uint16_t sp = 0, item = 0;
    for (uint16_t i = 1; i < disc.rules.species.size(); i++)
      if (disc.rules.species[i].exists && lower(disc.names->Species(i)) == lower(want)) { sp = i; break; }
    for (uint16_t i = 1; !itemName.empty() && i < disc.rules.items.size(); i++)
      if (disc.rules.items[i].exists && lower(disc.names->Item(i)) == lower(itemName)) { item = i; break; }
    if (place < 0 || place > 5 || sp == 0 || lv < 1 || lv > 100 || (!itemName.empty() && item == 0)) { std::fprintf(stderr, "forge: unknown place, species, level or item\n"); return 2; }
    xd::save::Save sv = trader.save();
    xd::save::ReadReport rep;
    xd::save::Save tmp; xd::save::ReadSave(card.data, tmp, rep, error);
    const size_t hero = xd::save::SectionOffsetInSlot(tmp, 2);
    uint8_t* r = tmp.slotBytes.data() + hero + 0x30 + static_cast<size_t>(place) * xd::save::kPokemonSize;
    const auto O = xd::save::ByteOrder::GameCube;
    const auto& sr = disc.rules.species[sp];
    Write16(r + 0x00, sp, O);
    Write16(r + 0x02, item, O);
    r[0x11] = static_cast<uint8_t>(lv);
    if (r[0x0E] > lv) r[0x0E] = static_cast<uint8_t>(lv);
    r[0x1D] = static_cast<uint8_t>(r[0x1D] & ~0x40);
    uint32_t exp = 0;
    xd::legality::ExpForLevel(disc.rules, sr.growth, lv, exp);
    Write32(r + 0x20, exp, O);
    std::u16string nick; for (char c : disc.names->Species(sp)) nick.push_back(static_cast<char16_t>(std::toupper(static_cast<unsigned char>(c))));
    nick.resize(10);
    for (size_t i = 0; i < 11; i++) Write16(r + 0x4E + i * 2, i < nick.size() ? nick[i] : 0, O);
    std::vector<uint16_t> mv;
    for (const auto& m : sr.levelUp) if (m.level <= lv) { bool dup = false; for (auto x : mv) dup |= x == m.move; if (!dup) mv.push_back(m.move); }
    while (mv.size() > 4) mv.erase(mv.begin());
    for (size_t i = 0; i < 4; i++) {
      const uint16_t id = i < mv.size() ? mv[i] : 0;
      Write16(r + 0x80 + i * 4, id, O);
      r[0x80 + i * 4 + 2] = id ? disc.rules.moves[id].basePp : 0;
      r[0x80 + i * 4 + 3] = 0;
    }
    for (size_t i = 0; i < 6; i++) Write16(r + 0x9C + i * 2, 0, O);
    xd::save::Pokemon pk = xd::save::ParsePokemon(r, O);
    std::array<uint16_t, 6> st{};
    xd::legality::ExpectedStats(disc.rules, pk, st);
    for (int k = 4; k < argc; k++) if (std::strcmp(argv[k], "--badstats") == 0) st[1] = static_cast<uint16_t>(st[1] + 7);
    for (size_t i = 0; i < 6; i++) Write16(r + 0x90 + i * 2, st[i], O);
    Write16(r + 0x04, st[0], O);
    pk = xd::save::ParsePokemon(r, O);
    const auto res = xd::legality::Check(pk, disc.rules, xd::legality::Level::Legal);
    std::printf("forged %s lv%d: %s\n", disc.names->Species(sp).c_str(), lv, res.Ok() ? "passes the Legal check" : res.Text().c_str());
    std::string err2;
    Bytes nf = xd::save::WriteSaveFile(card.data, tmp, rep, false, err2);
    if (nf.empty() || !WriteCardSave(card, nf, err2)) { std::fprintf(stderr, "forge: %s\n", err2.c_str()); return 2; }
    return res.Ok() ? 0 : 1;
  }

  const std::vector<Entry> entries = trader.List();
  if (cmd == "list") {
    std::printf("Trainer %s\n", trader.trainerName().c_str());
    for (size_t i = 0; i < entries.size(); i++) {
      const Entry& e = entries[i];
      std::printf("%zu: %s %s lv%d %s%s%s | %s%s\n", i, e.box < 0 ? "party" : ("box" + std::to_string(e.box + 1)).c_str(),
                  e.card.species.c_str(), e.card.level, e.card.nature.c_str(), e.card.item.empty() ? "" : " holding ",
                  e.card.item.c_str(), e.ok ? "OK" : "REFUSED: ", e.ok ? "" : e.reason.c_str());
    }
    return 0;
  }
  if (cmd != "trade" || give < 0 || give >= static_cast<int>(entries.size())) { std::fprintf(stderr, "need --give N from `list`\n"); return 2; }

  OnlineTrade t(trader, [&](const Bytes& nf, std::string& err) { return WriteCardSave(card, nf, err); });
  t.SetLog([](const std::string& s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
  if (customNet) { cfg.session = xdnet::Connection::defaultConfig().session; t.SetNetConfig(cfg); }
  if (code.empty()) code = t.StartHost();
  else t.StartJoin(code);
  std::printf("[trade] room %s\n", t.code().c_str());
  std::fflush(stdout);

  auto die = [&](const char* stage) { std::printf("[trade] DYING at %s\n", stage); std::fflush(stdout); std::_Exit(3); };
  const auto t0 = std::chrono::steady_clock::now();
  bool offered = false, readied = false, answered = false;
  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(seconds)) {
    t.Update();
    if (t.phase() == Phase::Trading) {
      auto* s = t.session();
      if (!offered) { offered = forceOffer ? s->offer(entries[static_cast<size_t>(give)].wire) : t.Offer(give); if (offered && dieAt == "offer") { Sleep(300); t.Update(); die("offer"); } }
      else if (s->state() == xdnet::trade::State::Negotiating && s->canReady() && !s->iAmReady() && !readied) {
        readied = t.Ready(true);
        if (readied && dieAt == "ready") { Sleep(300); t.Update(); die("ready"); }
      } else if (s->state() == xdnet::trade::State::Confirming && !s->iConfirmed() && !noConfirm && !answered) {
        answered = t.Answer(true);
        if (answered && dieAt == "confirm") { Sleep(300); t.Update(); die("confirm"); }
      } else if (s->state() == xdnet::trade::State::Committing && dieAt == "ack") {
        t.Update();
        die("ack");
      } else if (offered && !s->theirsVerdict().ok && !s->theirs().record.empty()) {
        std::printf("[trade] cannot take the friend's Pokemon: %s\n", s->theirsVerdict().text.c_str());
      }
    }
    if (t.phase() == Phase::Over && (!t.session() || t.session()->state() != xdnet::trade::State::Committing)) break;
    Sleep(20);
  }
  Sleep(1500);
  t.Update();
  int rc = 1;
  std::string line;
  if (t.session()) {
    auto* s = t.session();
    const char* o = s->outcome() == xdnet::trade::Outcome::Traded ? "TRADED" : s->outcome() == xdnet::trade::Outcome::NothingChanged ? "NOTHING_CHANGED"
                  : s->outcome() == xdnet::trade::Outcome::Unknown ? "UNKNOWN" : s->outcome() == xdnet::trade::Outcome::CommitFailed ? "COMMIT_FAILED" : "NOT_OVER";
    line = std::string("RESULT ") + o + " | " + s->endText();
    if (s->outcome() == xdnet::trade::Outcome::Traded) {
      rc = 0;
      const Result& r = t.result();
      line += " | received " + r.received + (r.evolved ? " and it evolved: " + r.evolvedFrom + " -> " + r.evolvedInto : "") +
              (r.newEntry ? " | new Pokedex entry" : "") + " | in " + r.where + (r.receivedFromCheats ? " | FROM A SAVE WITH CHEATS (the screen asks first; a live game marks the save)" : "");
    } else if (!s->myOfferVerdict().ok && s->myOfferJudged()) {
      line += " | friend refused my Pokemon: " + s->myOfferVerdict().text;
    } else if (!s->theirsVerdict().ok && !s->theirs().record.empty()) {
      line += " | I refused the friend's Pokemon: " + s->theirsVerdict().text;
    }
  } else {
    line = "RESULT NO_SESSION | " + t.connectionText();
  }
  std::printf("%s\n", line.c_str());
  if (!resultFile.empty()) { std::ofstream f(resultFile); f << line << "\n"; }
  return rc;
}
