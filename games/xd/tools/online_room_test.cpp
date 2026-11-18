
#include "../src/online_room.h"

#include <cstdio>
#include <string>

using namespace xd::online;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                                                  \
  do {                                                                            \
    sChecks++;                                                                    \
    if (!(c)) {                                                                   \
      sFailed++;                                                                  \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                          \
    }                                                                             \
  } while (0)

static bool Has(const std::vector<std::string>& v, const char* part) {
  for (const std::string& s : v) {
    if (s.find(part) != std::string::npos) return true;
  }
  return false;
}

int main() {
  RoomRules r;
  std::string err;
  CHECK(ParseRules("", r, &err));
  CHECK(r.ruleSet == "classic" && r.items == "none" && r.timerSeconds == 0 && !r.shadowAllowed);
  CHECK(ParseRules("rules=gen4;items=kit;timer=60;shadow=on;shadow_obey=purified;level=all:50;team=free14", r, &err));
  CHECK(r.ruleSet == "gen4" && r.items == "kit" && r.timerSeconds == 60 && r.shadowAllowed && r.level == "all:50" && r.team == "free14");
  CHECK(!ParseRules("rules=gen9", r, &err));

  CHECK(ParseRules("rules=gen4pokemon;team=free14", r, &err) && r.ruleSet == "gen4pokemon");
  CHECK(RoomGen4Pokemon("gen4pokemon") && !RoomGen4Rules("gen4pokemon"));
  CHECK(RoomGen4Pokemon("gen4") && RoomGen4Rules("gen4") && RoomGen4Pokemon("gen4mt") && RoomGen4Rules("gen4mt"));
  CHECK(!RoomGen4Pokemon("classic") && !RoomGen4Rules("classic"));
  CHECK(RulesText(r).find("Gen 4 Pokemon with the classic XD rules") != std::string::npos);
  CHECK(!ParseRules("timer=45", r, &err));
  CHECK(!ParseRules("items=balls", r, &err));
  CHECK(!ParseRules("level=cap:0", r, &err));
  CHECK(!ParseRules("level=all:101", r, &err));
  CHECK(!ParseRules("colour=red", r, &err) && err.find("colour") != std::string::npos);
  CHECK(!ParseRules("rules=classic;team=free14", r, &err));
  CHECK(ParseRules("rules=gen4mt;items=bag;timer=120", r, &err));
  CHECK(!RulesText(r).empty());

  Fingerprint host;
  host.caps.ruleSet = "gen4";
  host.caps.gen4Pack = "00ff00ff00ff00ff";
  host.caps.mods = "alpha,beta";
  host.caps.tables = 0x1234;
  host.rules.ruleSet = "gen4";
  host.rules.items = "kit";
  host.rules.timerSeconds = 90;
  host.rules.shadowAllowed = true;
  host.rules.level = "cap:50";
  host.rules.team = "free12";
  std::vector<uint8_t> blob = Encode(host);
  CHECK(blob.size() <= 400);
  Fingerprint back;
  CHECK(Decode(blob, back));
  CHECK(back.caps.ruleSet == "gen4" && back.caps.gen4Pack == host.caps.gen4Pack && back.caps.mods == "alpha,beta" && back.caps.tables == 0x1234);
  CHECK(back.rules.items == "kit" && back.rules.timerSeconds == 90 && back.rules.shadowAllowed && back.rules.level == "cap:50" && back.rules.team == "free12");
  std::vector<uint8_t> bad = blob;
  bad[0] = 'Y';
  CHECK(!Decode(bad, back));
  CHECK(!Decode({}, back));


  CHECK(Judge(back.caps, back.rules, host.caps, true).Ok() && !Judge(back.caps, back.rules, host.caps, true).NeedsRestart());
  CHECK(Judge(host.caps, host.rules, host.caps, false).Ok());


  Capabilities plain;
  Verdict v = Judge(back.caps, back.rules, plain, true);
  CHECK(!v.Ok() && Has(v.refuse, "Gen 4 pack from your Pokemon Battle Revolution disc"));
  CHECK(Has(Judge(host.caps, host.rules, plain, false).refuse, "does not have the Gen 4 pack"));


  Capabilities noData = host.caps;
  noData.ruleSet = "classic";
  v = Judge(back.caps, back.rules, noData, true);
  CHECK(v.Ok() && v.NeedsRestart() && Has(v.restart, "restarts with the Gen 4"));


  Capabilities otherPack = host.caps;
  otherPack.gen4Pack = "aaaaaaaaaaaaaaaa";
  CHECK(Has(Judge(back.caps, back.rules, otherPack, true).refuse, "different version"));


  RoomRules classicRoom;
  Capabilities classicHost;
  classicHost.mods = host.caps.mods;
  CHECK(Judge(classicHost, classicRoom, host.caps, true).Ok() && !Judge(classicHost, classicRoom, host.caps, true).NeedsRestart());


  Capabilities mods = host.caps;
  mods.mods = "alpha";
  v = Judge(back.caps, back.rules, mods, true);
  CHECK(!v.Ok() && Has(v.refuse, "beta"));
  mods.mods = "alpha,beta,gamma";
  v = Judge(back.caps, back.rules, mods, true);
  CHECK(v.Ok() && v.NeedsRestart() && Has(v.restart, "gamma"));


  Capabilities c = host.caps;
  c.randomizer = "classic:5";
  v = Judge(back.caps, back.rules, c, true);
  CHECK(v.Ok() && v.NeedsRestart() && Has(v.restart, "randomizer"));

  {

    Capabilities rg = host.caps;
    rg.ruleSet = "gen4";
    rg.randomizer = "classic:5";
    RoomRules classic;
    classic.ruleSet = "classic";
    Capabilities room = host.caps;
    room.ruleSet = "classic";
    room.gen4Pack = "";
    Verdict before = Judge(room, classic, rg, true);
    CHECK(before.Ok() && before.NeedsRestart() && Has(before.restart, "randomizer"));
    Capabilities after = rg;
    after.ruleSet = classic.ruleSet != "classic" ? "gen4" : "classic";
    after.randomizer.clear();
    Verdict cured = Judge(room, classic, after, true);
    CHECK(cured.Ok() && !cured.NeedsRestart());

    after.mods = room.mods + ",zzz";
    CHECK(Judge(room, classic, after, true).NeedsRestart());
  }

  RoomRules withSplit = back.rules;
  withSplit.split = true;
  CHECK(Judge(back.caps, withSplit, host.caps, true).Ok() && !Judge(back.caps, withSplit, host.caps, true).NeedsRestart());
  CHECK(ParseRules("split=on", r, &err) && r.split);
  CHECK(Encode(host).size() <= 400);
  host.rules.split = true;
  CHECK(Decode(Encode(host), back) && back.rules.split);


  c = host.caps;
  c.disc = "GXXP01";
  CHECK(Has(Judge(back.caps, back.rules, c, true).refuse, "GXXP01"));
  c = host.caps;
  c.protocol = 2;
  v = Judge(back.caps, back.rules, c, true);
  CHECK(v.refuse.size() == 1 && Has(v.refuse, "another version"));


  c = host.caps;
  c.tables = 0x9999;
  CHECK(Has(Judge(back.caps, back.rules, c, true).refuse, "game data"));
  c.tables = 0;
  CHECK(Judge(back.caps, back.rules, c, true).Ok());


  Fingerprint big = host;
  big.caps.mods.clear();
  for (int i = 0; i < 60; i++) big.caps.mods += (i ? "," : "") + std::string("some-long-mod-name-") + std::to_string(i);
  std::vector<uint8_t> blob2 = Encode(big);
  CHECK(blob2.size() <= 400);
  CHECK(Decode(blob2, back));
  Capabilities same = big.caps;
  CHECK(Judge(back.caps, back.rules, same, true).refuse.empty());
  same.mods += ",extra";
  CHECK(!Judge(back.caps, back.rules, same, true).refuse.empty());

  {
    RoomRules d;
    CHECK(ParseRules("rules=gen4;team=draft;timer=60;draft=size:3,pool:24,bans:1,gen:4,evo:full", d, &err));
    CHECK(d.team == "draft" && d.draft == "size:3,pool:24,bans:1,gen:4,evo:full");
    Fingerprint f;
    f.rules = d;
    Fingerprint g;
    CHECK(Decode(Encode(f), g) && g.rules.team == "draft" && g.rules.draft == d.draft && g.rules.split == d.split);
    CHECK(ParseRules("team=offer;draft=size:6,offer:3", d, &err) && d.team == "offer");
    CHECK(!ParseRules("team=draft;draft=size 3", d, &err));
    CHECK(RulesText(d).find("random offers") != std::string::npos);
  }
  {
    Fingerprint xd, xg;
    xg.caps.edition = "xg";
    ParseRules("rules=classic", xd.rules, &err);
    xg.rules = xd.rules;
    Fingerprint back;
    CHECK(Decode(Encode(xg), back) && back.caps.edition == "xg");
    CHECK(Decode(Encode(xd), back) && back.caps.edition == "xd");
    CHECK(Has(Judge(xd.caps, xd.rules, xg.caps, true).refuse, "cannot battle or trade"));
    CHECK(Has(Judge(xg.caps, xg.rules, xd.caps, false).refuse, "cannot battle or trade"));
    CHECK(Judge(xg.caps, xg.rules, xg.caps, true).Ok());
  }
  std::printf("online_room_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
