
#include "../src/online_room.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

  CHECK(ParseRules("shadow=on", r, &err) && r.shadowAllowed && !r.editedShadows);
  CHECK(ParseRules("shadow=on;shadow_edited=on", r, &err) && r.editedShadows && RulesKeyText(r).find("shadow_edited=on") != std::string::npos);
  RoomRules edited = r;
  CHECK(ParseRules(RulesKeyText(edited), r, &err) && r.editedShadows);
  r.editedShadows = false;
  CHECK(RulesKeyText(r).find("shadow_edited") == std::string::npos && RulesText(edited, "xd").find("save editor") != std::string::npos && RulesText(r, "xd").find("save editor") == std::string::npos);
  {
    Fingerprint a, b;
    a.rules = edited;
    CHECK(Decode(Encode(a), b) && b.rules.editedShadows);
    a.rules.editedShadows = false;
    const std::vector<uint8_t> enc = Encode(a);
    CHECK(Decode(enc, b) && !b.rules.editedShadows && std::string(enc.begin(), enc.end()).find("r.edsh") == std::string::npos);
  }

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

  {
    Capabilities mine = host.caps;
    mine.mods = "alpha";
    mine.modsBlocked = {"beta"};
    mine.modAssets = {{"beta", "Gen 5-7 pack"}};
    v = Judge(back.caps, back.rules, mine, true);
    CHECK(!v.Ok() && Has(v.refuse, "needs extra assets") && Has(v.refuse, "Gen 5-7 pack") && Has(v.refuse, "Extra assets page") && !Has(v.refuse, "does not have"));
    Capabilities guest = host.caps;
    guest.mods = "alpha";
    Capabilities hostCaps = back.caps;
    hostCaps.modAssets = {{"beta", "Gen 5-7 pack"}};
    v = Judge(hostCaps, back.rules, guest, false);
    CHECK(!v.Ok() && Has(v.refuse, "beta needs your own Gen 5-7 pack"));
    v = Judge(back.caps, back.rules, guest, false);
    CHECK(!v.Ok() && Has(v.refuse, "does not have: beta") && !Has(v.refuse, "needs your own"));
  }


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
  c.protocol = 7;
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
  {
    RoomRules legacy;
    CHECK(ParseRules("rules=gen4", legacy, &err) && !NeedsNewProtocol(legacy));
    Fingerprint f;
    f.rules = legacy;
    std::vector<uint8_t> blob = Encode(f);
    const std::string text(blob.begin(), blob.end());
    CHECK(text.find("proto=1\n") != std::string::npos && text.find("r.rs=") == std::string::npos);

    RoomRules a;
    SetRuleChoice(a, "gen4", "3,4", "");
    CHECK(a.ruleSet == "gen4" && a.rs.empty() && a.gens.empty() && !NeedsNewProtocol(a));
    SetRuleChoice(a, "gen4", "3,4", "modern_types:on");
    CHECK(a.ruleSet == "gen4mt" && a.rs.empty());
    SetRuleChoice(a, "xd", "3", "modern_types:on");
    CHECK(a.ruleSet == "classic" && a.rs == "xd" && a.details == "modern_types:on" && NeedsNewProtocol(a));
    CHECK(RoomRuleSetId(a) == "xd" && RoomGens(a) == "3" && RoomDetails(a) == "modern_types:on");
    CHECK(RoomRuleSetId(legacy) == "gen4" && RoomGens(legacy) == "3,4" && RoomDetails(legacy).empty());
    RoomRules mt;
    ParseRules("rules=gen4mt", mt, &err);
    CHECK(RoomDetails(mt) == "modern_types:on");

    Fingerprint nf;
    nf.rules = a;
    nf.rules.format = "gen4_singles";
    nf.rules.formatHash = "ab12cd34";
    Fingerprint back2;
    CHECK(Decode(Encode(nf), back2) && back2.rules.rs == "xd" && back2.rules.gens == "3" && back2.rules.details == "modern_types:on" &&
          back2.rules.format == "gen4_singles" && back2.rules.formatHash == "ab12cd34");
    CHECK(Encode(nf).size() <= 400);
    CHECK(ParseRules("rules=classic;rs=xd;gens=3;details=modern_types:on;format=gen4_singles;fmthash=ab12cd34", r, &err) && r.format == "gen4_singles");
    CHECK(!ParseRules("rs=Gen 5", r, &err));

    Capabilities mine;
    mine.protocol = 3;
    Capabilities roomCaps;
    roomCaps.protocol = 1;
    CHECK(Judge(roomCaps, legacy, mine, true).refuse.empty() || !Has(Judge(roomCaps, legacy, mine, true).refuse, "another version"));
    roomCaps.protocol = 2;
    mine.knownFormats = "gen4_singles:ab12cd34";
    CHECK(!Has(Judge(roomCaps, nf.rules, mine, true).refuse, "rule set"));
    roomCaps.protocol = 1;
    CHECK(Has(Judge(roomCaps, nf.rules, mine, true).refuse, "another version"));
    roomCaps.protocol = 2;
    RoomRules later = nf.rules;
    later.rs = "gen9";
    CHECK(Has(Judge(roomCaps, later, mine, true).refuse, "rule set \"gen9\""));

    {
      RoomRules g6 = nf.rules;
      SetRuleChoice(g6, "gen6", "3,4", "");
      g6.format.clear();
      g6.formatHash.clear();
      CHECK(g6.rs == "gen6" && g6.ruleSet == "gen4" && NeedsNewProtocol(g6));
      CHECK(RoomNumbers(g6) == "6f" && RoomNumbers(nf.rules) == "3");
      CHECK(!Has(Judge(roomCaps, g6, mine, true).refuse, "rule set"));
      Capabilities oldGame = mine;
      oldGame.knownRuleSets = "xd,gen4";
      CHECK(Has(Judge(roomCaps, g6, oldGame, true).refuse, "rule set \"gen6\""));
      Capabilities has3 = mine, has6 = mine;
      has3.numbers = "3";
      has6.numbers = "6f";
      CHECK(Has(Judge(roomCaps, g6, has3, true).restart, "numbers of this room"));
      CHECK(!Has(Judge(roomCaps, g6, has6, true).restart, "numbers of this room"));
      CHECK(Has(Judge(roomCaps, g6, has6, false).restart, "numbers of this room") == false);
      has6.numbers = "6";
      CHECK(Has(Judge(roomCaps, g6, has6, true).restart, "numbers of this room"));
      Fingerprint f;
      f.caps = has6;
      f.caps.numbers = "7f";
      f.rules = g6;
      Fingerprint back;
      CHECK(Decode(Encode(f), back) && back.caps.numbers == "7f" && back.rules.rs == "gen6");
    }
    later = nf.rules;
    later.gens = "3,4,5";
    CHECK(Has(Judge(roomCaps, later, mine, true).refuse, "generation 5"));
    later = nf.rules;
    later.formatHash = "ffffffff";
    CHECK(Has(Judge(roomCaps, later, mine, true).refuse, "another version"));
    later.format = "nonsense";
    CHECK(Has(Judge(roomCaps, later, mine, true).refuse, "not in this version"));
    CHECK(Has(Judge(roomCaps, later, mine, false).refuse, "does not have the format"));
  }
  {
    RoomRules x;
    CHECK(ParseRules("rules=classic;rs=xd;gens=3;format=gen4_singles;fmthash=ab12cd34;sleep_clause=on;preview=on;pick=3;preview_timer=90;bank=420", x, &err));
    CHECK(x.sleepClause && x.preview && x.pick == 3 && x.previewSeconds == 90 && x.bankSeconds == 420 && NeedsExtras(x) && NeedsNewProtocol(x));
    Fingerprint f;
    f.rules = x;
    const std::vector<uint8_t> blob = Encode(f);
    const std::string text(blob.begin(), blob.end());
    CHECK(text.find("proto=3\n") != std::string::npos && text.find("r.ex=sleep:1,pv:1,pick:3,pvt:90,bank:420") != std::string::npos);
    CHECK(blob.size() <= 400);
    Fingerprint back2;
    CHECK(Decode(blob, back2) && back2.rules.sleepClause && back2.rules.preview && back2.rules.pick == 3 && back2.rules.previewSeconds == 90 && back2.rules.bankSeconds == 420);
    CHECK(ExtrasKeyText(back2.rules).find("sleep_clause=on") != std::string::npos && ExtrasKeyText(back2.rules).find("bank=420") != std::string::npos);
    RoomRules again;
    CHECK(ParseRules("rules=classic" + ExtrasKeyText(x), again, &err) && again.sleepClause && again.pick == 3 && again.format == "gen4_singles");
    Capabilities mine;
    mine.protocol = 3;
    mine.knownFormats = "gen4_singles:ab12cd34";
    Capabilities roomCaps;
    roomCaps.protocol = 3;
    CHECK(Judge(roomCaps, x, mine, true).refuse.empty());
    mine.protocol = 1;
    Verdict old1 = Judge(roomCaps, x, mine, true);
    CHECK(!old1.Ok() && Has(old1.refuse, "format rules") && Has(old1.refuse, "Update the game"));
    mine.protocol = 2;
    CHECK(Has(Judge(roomCaps, x, mine, true).refuse, "format rules"));
    CHECK(Has(Judge(roomCaps, x, mine, false).refuse, "does not know this room's format rules"));
    RoomRules plainFormat = x;
    plainFormat.sleepClause = plainFormat.preview = false, plainFormat.pick = plainFormat.previewSeconds = plainFormat.bankSeconds = 0;
    CHECK(!NeedsExtras(plainFormat) && NeedsNewProtocol(plainFormat));
    Fingerprint pf;
    pf.rules = plainFormat;
    const std::vector<uint8_t> pb = Encode(pf);
    CHECK(std::string(pb.begin(), pb.end()).find("proto=2\n") != std::string::npos && std::string(pb.begin(), pb.end()).find("r.ex=") == std::string::npos);
    CHECK(!ParseRules("pick=3", again, &err));
    CHECK(!ParseRules("preview=on;pick=9", again, &err));
    CHECK(RulesText(x).find("Sleep clause") != std::string::npos && RulesText(x).find("team preview") != std::string::npos);
  }
  {
    CHECK(ActionNumbers("1 0 19 5 1 0 0 0 0 0") == 9);
    CHECK(ActionNumbers("1 0 19 5 1 0 0 0 0") == 8);
    CHECK(ActionNumbers("2 0 19 5 1 0 0 0 0 1 19 6 2 0 0 0 0") == 8);
    CHECK(ActionNumbers("2 0 19 5 1 0 0 0 0 0 1 19 6 2 0 0 0 0 1") == 9);
    CHECK(ActionNumbers("2 0 19 5 1 0 0 0 0 0 1 19 6 2 0 0 0 0") == 0);
    CHECK(ActionNumbers("") == 0 && ActionNumbers("x 1 2") == 0);
    CHECK(ActionNumbers("1 0 8 0 0 0 0 0 0 0") == 9);
  }


  {
    Fingerprint casual;
    Fingerprint formatRoom;
    formatRoom.rules.format = "gen4_singles";
    formatRoom.rules.formatHash = "ab12cd34";
    formatRoom.rules.rs = "gen4";
    formatRoom.rules.gens = "3,4";
    {
      namespace fs = std::filesystem;
      const fs::path root = fs::temp_directory_path() / "xd_pack_digest_test";
      std::error_code ec;
      fs::remove_all(root, ec);
      fs::create_directories(root / "data", ec);
      fs::create_directories(root / "battle", ec);
      const auto write = [&](const char* rel, const std::string& text) { std::ofstream(root / rel, std::ios::binary) << text; };
      CHECK(PackBattleDigest(root) == "unknown");
      write("data/02_species.json", "{\"add\":{\"pokemon\":{\"A\":{\"zokuseiDataId\":[1,1]}}}}");
      write("data/01_moves.json", "{}");
      write("battle/moves.json", "{\"moves\":{}}");
      write("tables.txt", "format 1\n");
      write("gen57.json", "{\"version\": 1, \"converter\": \"0.2.0\", \"dump\": \"x-1\", \"make\": 5}");
      const std::string first = PackBattleDigest(root);
      CHECK(first != "unknown" && first.size() == 16);
      write("gen57.json", "{\"version\": 1, \"converter\": \"0.2.2\", \"dump\": \"y-2\", \"make\": 7, \"recipe\": 2}");
      write("gen4.json", "{\"recipe\": 2}");
      CHECK(PackBattleDigest(root) == first);
      write("data/02_species.json", "{\"add\":{\"pokemon\":{\"A\":{\"zokuseiDataId\":[1,2]}}}}");
      CHECK(PackBattleDigest(root) != first);
      write("data/02_species.json", "{\"add\":{\"pokemon\":{\"A\":{\"zokuseiDataId\":[1,1]}}}}");
      CHECK(PackBattleDigest(root) == first);
      write("battle/moves.json", "{\"moves\":{\"B\":{}}}");
      CHECK(PackBattleDigest(root) != first);
      fs::remove_all(root, ec);
    }
    {
    CHECK(kBattleProtocol == 6);
    Fingerprint cur;
    cur.caps.protocol = kBattleProtocol;
    const std::vector<uint8_t> blob = Encode(cur);
    CHECK(std::string(blob.begin(), blob.end()).find("cap=6\n") != std::string::npos);
    Fingerprint back4;
    CHECK(Decode(blob, back4) && back4.caps.protocol == 6);
    Capabilities now, old3;
    now.protocol = 6;
    old3.protocol = 3;
    RoomRules classic;
    for (const char* rs : {"classic", "gen4pokemon", "gen4", "gen4mt"}) {
      classic.ruleSet = rs;
      const Verdict joinOld = Judge(old3, classic, now, true), hostOld = Judge(now, classic, old3, false), joinNew = Judge(now, classic, old3, true), hostNew = Judge(old3, classic, now, false);
      CHECK(Has(joinOld.refuse, "same game version") && Has(hostOld.refuse, "same game version") && Has(joinNew.refuse, "same game version") && Has(hostNew.refuse, "same game version"));
      CHECK(joinOld.refuse.size() == 1 && hostOld.refuse.size() == 1);
    }
    classic.ruleSet = "classic";
    Capabilities now2 = now;
    CHECK(Judge(now, classic, now2, true).refuse.empty() && Judge(now, classic, now2, false).refuse.empty());
    Capabilities rev4 = now;
    rev4.protocol = 4;
    CHECK(Has(Judge(rev4, classic, now, true).refuse, "same game version") && Has(Judge(now, classic, rev4, false).refuse, "same game version"));
    Capabilities rev5 = now;
    rev5.protocol = 5;
    CHECK(Has(Judge(rev5, classic, now, true).refuse, "same game version") && Has(Judge(now, classic, rev5, false).refuse, "same game version"));
    {
      Capabilities me = now, they = now;
      me.version = "0.8.0";
      they.protocol = 5;
      CHECK(Has(Judge(they, classic, me, true).refuse, "This room's game is another version (0.7.7). You have 0.8.0. Both players need the same game version."));
      CHECK(Has(Judge(me, classic, they, false).refuse, "Your friend's game is another version (0.7.7). You have 0.8.0. Both players need the same game version."));
      they.protocol = 4;
      CHECK(Has(Judge(they, classic, me, true).refuse, "(0.7.5 or 0.7.6). You have 0.8.0"));
      Fingerprint f;
      f.caps = me;
      Fingerprint g;
      CHECK(Decode(Encode(f), g) && g.caps.version == "0.8.0");
      f.caps.version.clear();
      const std::vector<uint8_t> noVer = Encode(f);
      CHECK(std::string(noVer.begin(), noVer.end()).find("ver=") == std::string::npos && Decode(noVer, g) && g.caps.version.empty());
    }
    Capabilities old1 = old3, old2 = old3;
    old1.protocol = 1, old2.protocol = 2;
    CHECK(Has(Judge(old1, classic, now, true).refuse, "same game version") && Has(Judge(old2, classic, now, true).refuse, "same game version"));
  }
  Capabilities cheater = casual.caps;
    cheater.cheats = true;
    const std::vector<uint8_t> plainBytes = Encode(casual);
    const std::string plainBlob(plainBytes.begin(), plainBytes.end());
    CHECK(plainBlob.find("cheats") == std::string::npos);
    Fingerprint withCheats = casual;
    withCheats.caps.cheats = true;
    const std::vector<uint8_t> blob = Encode(withCheats);
    Fingerprint back;
    CHECK(Decode(blob, back) && back.caps.cheats);
    Fingerprint backPlain;
    CHECK(Decode(Encode(casual), backPlain) && !backPlain.caps.cheats);
    const Verdict casualVerdict = Judge(casual.caps, casual.rules, cheater, true);
    CHECK(casualVerdict.Ok() && Has(casualVerdict.notes, "marked"));
    const Verdict competitive = Judge(formatRoom.caps, formatRoom.rules, cheater, true);
    CHECK(!competitive.Ok() && Has(competitive.refuse, "used cheats"));
    CHECK(Has(Judge(formatRoom.caps, formatRoom.rules, cheater, false).refuse, "used cheats"));
    CHECK(Judge(formatRoom.caps, formatRoom.rules, formatRoom.caps, true).notes.empty());
  }
  std::printf("online_room_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed ? 1 : 0;
}
