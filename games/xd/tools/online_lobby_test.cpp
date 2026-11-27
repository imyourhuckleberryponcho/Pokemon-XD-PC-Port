
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
  CHECK(rules && rules->options.size() == 7 && rules->options[1].enabled == false && rules->options[2].enabled == false && !rules->options[1].why.empty());
  CHECK(Find(rows, "levelN") == nullptr && Find(rows, "obey") == nullptr && Find(rows, "d_size") == nullptr);

  CHECK(!Cycle(r, "rules", 1, noPack) && r.ruleSet == "classic");
  CHECK(Cycle(r, "rules", 1, pack) && r.ruleSet == "gen4pokemon");
  CHECK(Cycle(r, "rules", 1, pack) && r.ruleSet == "gen4");
  CHECK(Cycle(r, "rules", -1, pack) && r.ruleSet == "gen4pokemon");
  CHECK(Cycle(r, "rules", -1, pack) && r.ruleSet == "classic");
  r.ruleSet = "gen4pokemon";
  CHECK(Sanitized(r, noPack).ruleSet == "classic" && Sanitized(r, pack).ruleSet == "gen4pokemon");

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
  CHECK(Find(SettingRows(r, pack), "edited") == nullptr);
  CHECK(Cycle(r, "shadow", 1, pack) && r.shadowAllowed && Find(SettingRows(r, pack), "obey"));
  {
    CHECK(Find(SettingRows(r, pack), "edited") == nullptr);
    CHECK(!Cycle(r, "edited", 1, pack) && !r.editedShadows);
  }

  r = DefaultRoom();
  CHECK(Cycle(r, "style", 1, pack) && r.style == "double");
  Fingerprint f;
  f.rules = r;
  Fingerprint g;
  CHECK(Decode(Encode(f), g) && g.rules.style == "double");
  CHECK(ParseRules(RulesToText(r), back, &why) && back.style == "double" && back.timerSeconds == 90);

  const std::vector<std::string> lines = RoomLines(r);
  CHECK(lines.size() >= 7 && lines[0].find("Classic XD") != std::string::npos);
  CHECK(RoomLines(r, "xg")[0].find("Classic XG") != std::string::npos);
  PcInfo xgPc;
  xgPc.edition = "xg";
  CHECK(SettingRows(r, xgPc)[0].options[0].label == "Classic XG");

  xgPc.gen4PackPresent = true;
  xgPc.gen4RulesOff = "XG has its own modern rules.";
  const Row xgRules = SettingRows(r, xgPc)[0];
  CHECK(xgRules.options[1].value == "gen4pokemon" && xgRules.options[1].enabled && xgRules.options[1].label.find("XG rules") != std::string::npos);
  CHECK(xgRules.options[2].value == "gen4" && !xgRules.options[2].enabled && xgRules.options[2].why == xgPc.gen4RulesOff);
  CHECK(xgRules.options[3].value == "gen4mt" && !xgRules.options[3].enabled);

  PcInfo fmtPc = pack;
  fmtPc.formats = {{"gen4_singles", "Gen 4 Singles", true, ""}, {"xd_classic", "XD Classic", true, ""}};
  fmtPc.applyFormat = [](RoomRules& room) { room.ruleSet = "gen4"; room.level = "all:50"; room.formatHash = "abcd1234"; return true; };
  fmtPc.formatLine = [](const RoomRules&, const char* what) { return std::string(what) + " line"; };
  RoomRules fr = DefaultRoom();
  CHECK(SettingRows(fr, fmtPc)[0].id == "format" && SettingRows(fr, fmtPc)[0].options.size() == 3 && SettingRows(fr, fmtPc)[0].options[0].label == "Custom");
  CHECK(Find(SettingRows(fr, fmtPc), "clauses") == nullptr);
  CHECK(Cycle(fr, "format", 1, fmtPc) && fr.format == "gen4_singles" && fr.ruleSet == "gen4" && fr.formatHash == "abcd1234");
  CHECK(Find(SettingRows(fr, fmtPc), "clauses") != nullptr && Find(SettingRows(fr, fmtPc), "bans") != nullptr && Find(SettingRows(fr, fmtPc), "preview") != nullptr && Find(SettingRows(fr, fmtPc), "timers") != nullptr);
  fr.sleepClause = true, fr.preview = true, fr.pick = 3, fr.previewSeconds = 90, fr.bankSeconds = 420;
  CHECK(Cycle(fr, "timer", 1, fmtPc) && fr.format.empty() && fr.formatHash.empty());
  CHECK(!fr.sleepClause && !fr.preview && fr.pick == 0 && fr.previewSeconds == 0 && fr.bankSeconds == 0);
  CHECK(!Cycle(fr, "clauses", 1, fmtPc));
  PcInfo fmtNo = noPack;
  fmtNo.formats = {{"gen4_singles", "Gen 4 Singles", false, "Needs the Gen 4 pack."}};
  RoomRules fr2 = DefaultRoom();
  CHECK(!Cycle(fr2, "format", 1, fmtNo) && fr2.format.empty());

  Peer p;
  CHECK(Apply(p, Msg("ACCEPT")) && p.accepted);
  CHECK(Apply(p, Msg("TEAM", "3")) && p.teamCount == 3);
  CHECK(Apply(p, Msg("READY")) && p.ready);
  CHECK(Apply(p, Msg("UNREADY")) && !p.ready);
  CHECK(Apply(p, Msg("READY", "1a2b3c4d")) && p.ready && p.nonce == 0x1a2b3c4du);
  CHECK(!Apply(p, "T 1 abcd") && !Apply(p, "Z 123"));
  CHECK(Apply(p, Msg("LEAVE")) && p.left);
  {
    Peer q;
    CHECK(Apply(q, Msg("PV", "12,45,300,9")) && q.pvTeam && q.pvIds == std::vector<int>({12, 45, 300, 9}));
    CHECK(Apply(q, Msg("PVC", "abcdef0123")) && q.pvCommit && q.pvCommitHex == "abcdef0123");
    CHECK(Apply(q, Msg("PVR", "00ff 2,0,1")) && q.pvReveal && q.pvNonce == "00ff" && q.pvList == "2,0,1");
    CHECK(JoinInts({2, 0, 1}) == "2,0,1" && SplitInts("2,0,1") == std::vector<int>({2, 0, 1}) && SplitInts("").empty() && SplitInts("5,,7") == std::vector<int>({5, 7}));
    CHECK(PickValid({2, 0, 1}, 6, 3) && !PickValid({2, 0}, 6, 3) && !PickValid({2, 2, 1}, 6, 3) && !PickValid({2, 0, 6}, 6, 3) && !PickValid({-1, 0, 1}, 6, 3) && !PickValid({}, 6, 0));

    CHECK(TimeoutPick({}, 6, 3) == std::vector<int>({0, 1, 2}));
    CHECK(TimeoutPick({4}, 6, 3) == std::vector<int>({4, 0, 1}));
    CHECK(TimeoutPick({4, 1, 5, 2}, 6, 3) == std::vector<int>({4, 1, 5}));
    CHECK(TimeoutPick({7, 4, 4}, 6, 3) == std::vector<int>({4, 0, 1}));
    CHECK(TimeoutPick({}, 2, 3) == std::vector<int>({0, 1}));
  }

  CHECK(OutcomeOf(2, true, false) == Outcome::Won && OutcomeOf(2, false, false) == Outcome::Lost);
  CHECK(OutcomeOf(5, true, false) == Outcome::FriendQuit && OutcomeOf(5, false, false) == Outcome::YouQuit);
  CHECK(OutcomeOf(1, true, false) == Outcome::ConnectionLost && OutcomeOf(1, true, true) == Outcome::Desync);
  std::printf("%d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
