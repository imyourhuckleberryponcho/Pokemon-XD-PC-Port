
#include "../src/online_lobby.h"

#include <cstdio>
#include <string>

using namespace xd::online;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                          \
  do {                                                    \
    sChecks++;                                            \
    if (!(c)) {                                           \
      sFailed++;                                          \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);  \
    }                                                     \
  } while (0)

static const Row* Find(const std::vector<Row>& rows, const char* id) {
  for (const Row& r : rows) if (r.id == id) return &r;
  return nullptr;
}

int main() {
  PcInfo noPack, pack;
  pack.gen4PackPresent = true;
  RoomRules r = DefaultRoom();
  CHECK(r.timerSeconds == 90 && r.ruleSet == "classic" && r.team == "preset");
  std::vector<Row> rows = SettingRows(r, noPack);
  const Row* rules = Find(rows, "rules");
  CHECK(rules && rules->options.size() == 3 && rules->options[1].enabled == false && !rules->options[1].why.empty());
  CHECK(Find(rows, "levelN") == nullptr && Find(rows, "obey") == nullptr && Find(rows, "d_size") == nullptr);

  CHECK(!Cycle(r, "rules", 1, noPack) && r.ruleSet == "classic");
  CHECK(Cycle(r, "rules", 1, pack) && r.ruleSet == "gen4");
  CHECK(Cycle(r, "rules", -1, pack) && r.ruleSet == "classic");

  r = DefaultRoom();
  for (int i = 0; i < 2; i++) Cycle(r, "team", 1, pack);
  CHECK(r.team == "free12" && r.level == "all:50");
  CHECK(Find(SettingRows(r, pack), "levelN") != nullptr);
  CHECK(Cycle(r, "levelN", 1, pack) && r.level == "all:51");

  CHECK(Cycle(r, "team", 1, pack) && r.team == "free14" ? false : true);
  CHECK(r.team == "draft");

  r = DefaultRoom();
  CHECK(Cycle(r, "items", 1, pack) && r.items == "kit");
  r.items = "bag";
  r = Sanitized(r, pack);
  CHECK(r.items == "none");

  r = DefaultRoom();
  for (int i = 0; i < 3; i++) Cycle(r, "team", 1, pack);
  CHECK(r.team == "draft");
  std::vector<Row> d = SettingRows(r, pack);
  CHECK(Find(d, "d_size") && Find(d, "d_pool") && Find(d, "d_bans") && Find(d, "d_offer") == nullptr);
  CHECK(Cycle(r, "d_size", -1, pack));
  std::string why;
  RoomRules back;
  CHECK(ParseRules(RulesToText(r), back, &why));
  CHECK(RulesToText(back) == RulesToText(r));
  CHECK(Cycle(r, "team", 1, pack) && r.team == "offer" && Find(SettingRows(r, pack), "d_offer"));

  r = DefaultRoom();
  CHECK(Cycle(r, "shadow", 1, pack) && r.shadowAllowed && Find(SettingRows(r, pack), "obey"));

  r = DefaultRoom();
  CHECK(Cycle(r, "style", 1, pack) && r.style == "double");
  Fingerprint f;
  f.rules = r;
  Fingerprint g;
  CHECK(Decode(Encode(f), g) && g.rules.style == "double");
  CHECK(ParseRules(RulesToText(r), back, &why) && back.style == "double" && back.timerSeconds == 90);

  const std::vector<std::string> lines = RoomLines(r);
  CHECK(lines.size() >= 7 && lines[0].find("Classic XD") != std::string::npos);

  Peer p;
  CHECK(Apply(p, Msg("ACCEPT")) && p.accepted);
  CHECK(Apply(p, Msg("TEAM", "3")) && p.teamCount == 3);
  CHECK(Apply(p, Msg("READY")) && p.ready);
  CHECK(Apply(p, Msg("UNREADY")) && !p.ready);
  CHECK(Apply(p, Msg("READY", "1a2b3c4d")) && p.ready && p.nonce == 0x1a2b3c4du);
  CHECK(!Apply(p, "T 1 abcd") && !Apply(p, "Z 123"));
  CHECK(Apply(p, Msg("LEAVE")) && p.left);

  CHECK(OutcomeOf(2, true, false) == Outcome::Won && OutcomeOf(2, false, false) == Outcome::Lost);
  CHECK(OutcomeOf(5, true, false) == Outcome::FriendQuit && OutcomeOf(5, false, false) == Outcome::YouQuit);
  CHECK(OutcomeOf(1, true, false) == Outcome::ConnectionLost && OutcomeOf(1, true, true) == Outcome::Desync);
  std::printf("%d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
