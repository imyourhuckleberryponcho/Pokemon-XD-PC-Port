#include <rulesets.h>

#include "json.h"

#include <cstdio>
#include <string>

namespace er = engine::rulesets;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

er::Registry MakeRegistry() {
  er::Registry r;
  er::RuleSet a;
  a.id = "a"; a.name = "Rules A"; a.generation = 3; a.mechanics = {{"fairy", "off"}, {"split", "player"}};
  r.AddRuleSet(a);
  er::RuleSet b;
  b.id = "b"; b.name = "Rules B"; b.generation = 4; b.needsGens = {4}; b.mechanics = {{"fairy", "off"}, {"split", "on"}};
  r.AddRuleSet(b);
  er::RuleSet c;
  c.id = "c"; c.name = "Rules C"; c.generation = 5; c.available = false; c.unavailableWhy = "later";
  r.AddRuleSet(c);
  r.AddGeneration({3, "Gen 1-3", true, ""});
  r.AddGeneration({4, "Gen 1-4", true, ""});
  r.AddGeneration({5, "Gen 1-5", false, "later"});
  r.AddSource({"base", "Base"});
  r.AddSource({"mid", "Middle"});
  r.AddSource({"top", "Top"});
  r.AddPreset({"plain", "Plain", "", {"base"}});
  r.AddPreset({"modern", "Modern", "", {"top", "mid", "base"}});
  r.SetFallbackSource("base");
  r.SetDefaults("a", {3}, "plain");
  return r;
}
}

int main() {
  const er::Registry reg = MakeRegistry();

  er::Choice d = er::Default(reg);
  check(d.ruleSet == "a" && d.gens == std::vector<int>{3} && d.looks.preset == "plain" && er::Validate(d, reg).empty(), "default choice is valid");
  er::Choice c;
  c.ruleSet = "b";
  c.gens = {3};
  c = er::Normalize(c, reg);
  check(c.gens == (std::vector<int>{3, 4}), "a rule set's needed generation is added (Gen 1-4 is cumulative)");
  er::Choice g5;
  g5.ruleSet = "a";
  g5.gens = {5};
  g5 = er::Normalize(g5, reg);
  check(g5.gens == (std::vector<int>{3, 4, 5}) && !er::Validate(g5, reg).empty(), "Gen 5 fills the gaps and is refused (not available)");
  check(!er::Validate(er::Normalize([] { er::Choice x; x.ruleSet = "c"; x.gens = {3}; return x; }(), reg), reg).empty(), "a rule set that is not available is refused");
  er::Choice m;
  m.ruleSet = "a";
  m.gens = {3};
  m.mechanics["fairy"] = "on";
  m.mechanics["split"] = "player";
  m = er::Normalize(m, reg);
  check(m.mechanics.size() == 1 && m.MechanicOn(reg, "fairy") && m.Mechanic(reg, "split") == "player", "details: an override equal to the bundle is dropped");
  check(er::Choice(c).Mechanic(reg, "split") == "on", "a rule set's bundle gives the detail's value");

  er::Choice l = d;
  l.looks.preset = "modern";
  l.looks.sources = {"top", "mid", "base"};
  l.looks.overrides[25] = "mid";
  l.SetOption("spots", true);
  l.mechanics["fairy"] = "on";
  l = er::Normalize(l, reg);
  er::Choice back;
  std::string err;
  check(er::ParseCanonical(l.Canonical(), back, err) && back == l, "Canonical text round trip: " + l.Canonical());

  const std::string json = "{\n" + er::ToJsonKeys(l, 2) + "\n}\n";
  engine::mods::json::Value root;
  engine::mods::json::Reader reader(json);
  check(reader.Document(root), "the keys are valid JSON");
  er::Choice fromJson;
  check(er::FromJson(root, fromJson, err) && fromJson.present, "FromJson reads the three keys " + err);
  fromJson = er::Normalize(fromJson, reg);
  check(fromJson == l && fromJson.looks.preset == "modern", "JSON round trip keeps style, order, pins, details and options");
  engine::mods::json::Value empty;
  empty.kind = engine::mods::json::Value::Kind::Object;
  er::Choice none;
  check(er::FromJson(empty, none, err) && !none.present, "a file without the keys: nothing chosen");

  er::Looks styles;
  styles.sources = {"top", "mid", "base"};
  check(er::Ranked(styles, reg) == (std::vector<std::string>{"top", "mid", "base"}), "ranked list");
  check(er::LayerOrder(styles, reg) == (std::vector<std::string>{"mid", "top"}), "layer order: lowest first, the fallback left out");
  er::Looks says;
  check(er::Ranked(says, reg).empty() && !says.Says(), "no list = nothing said (the player's setting decides)");
  er::Looks odd;
  odd.sources = {"mid", "nonsense", "mid", "top"};
  check(er::Ranked(odd, reg) == (std::vector<std::string>{"mid", "top", "base"}), "unknown and repeated sources dropped, the fallback ends the list");
  er::Looks early;
  early.sources = {"base", "top"};
  check(er::Ranked(early, reg) == (std::vector<std::string>{"base"}), "nothing below the fallback counts");
  const er::HasModel has = [](const std::string& s, int sp) {
    if (s == "top") return sp % 2 == 0;
    if (s == "mid") return sp % 3 == 0;
    return true;
  };
  check(er::ResolveSpecies(styles, reg, 4, has).source == "top", "species 4: top (rank 0)");
  check(er::ResolveSpecies(styles, reg, 9, has).source == "mid" && er::ResolveSpecies(styles, reg, 9, has).rank == 1, "species 9: mid (top lacks it)");
  check(er::ResolveSpecies(styles, reg, 7, has).source == "base", "species 7: the fallback (no gaps)");
  styles.overrides[4] = "base";
  styles.overrides[7] = "top";
  check(er::ResolveSpecies(styles, reg, 4, has).source == "base" && er::ResolveSpecies(styles, reg, 4, has).overridden, "a pin picks one source for one species");
  check(er::ResolveSpecies(styles, reg, 7, has).source == "base" && !er::ResolveSpecies(styles, reg, 7, has).overridden, "a pin to a source that lacks the species falls back to the list");

  er::Format f;
  f.id = "t";
  f.ruleSet = "a";
  f.gens = {3};
  f.level = {er::LevelRule::Kind::Cap, 50, 0};
  f.clauses = {er::kClauseSpecies, er::kClauseItem, er::kClauseOhko, er::kClauseEvasion};
  f.bannedSpecies = {150};
  f.bannedItems = {99};
  f.bannedCategories = {"legendary"};
  f.teamPreview = true;
  f.pick = 3;
  f.timerPerChoice = 60;
  f.details["split"] = "on";
  er::Format pf;
  check(er::ParseFormat(f.ToText(), pf, err) && pf == f, "Format text round trip: " + f.ToText());
  check(!er::ParseFormat("id=x;level=weird", pf, err), "a bad level rule is refused");
  er::LevelRule lr;
  check(er::LevelRule::Parse("range:5-50", lr) && lr.kind == er::LevelRule::Kind::Range && lr.a == 5 && lr.b == 50 && lr.Text() == "range:5-50", "level rule range");
  check(!er::LevelRule::Parse("all:0", lr) && !er::LevelRule::Parse("cap:101", lr), "level rule bounds");
  er::ClauseData data;
  data.ohkoMoves = {90};
  data.evasionMoves = {104};
  data.categories["legendary"] = {144, 150};
  er::TeamMember a;
  a.species = 25; a.level = 50; a.item = 1;
  er::TeamMember b = a;
  b.species = 6; b.item = 2; b.moves[0] = 90;
  er::TeamMember c3 = a;
  c3.species = 25; c3.item = 1; c3.level = 60; c3.moves[1] = 104;
  er::TeamMember leg = a;
  leg.species = 144; leg.item = 5;
  er::TeamMember banned = a;
  banned.species = 150; banned.item = 99;
  check(er::CheckTeam(f, {a, b}, data).size() == 1, "team: only the OHKO move is wrong");
  const auto issues = er::CheckTeam(f, {a, b, c3, leg, banned}, data);
  auto has_text = [&](const char* needle) {
    for (const std::string& s : issues) if (s.find(needle) != std::string::npos) return true;
    return false;
  };
  check(has_text("Species clause") && has_text("Item clause") && has_text("OHKO clause") && has_text("Evasion clause") && has_text("cap is 50") &&
            has_text("is banned in this format") && has_text("legendary") && has_text("at most 6") == false && has_text("holds"),
        "team: species / item / OHKO / evasion clauses, level cap, bans and categories all spoken");
  check(!er::CheckTeam(f, {}, data).empty(), "team: too small a team");
  {
    const auto pm = er::TeamProblems(f, {a, b, c3, leg, banned}, data);
    auto has_kind = [&](int member, const char* kind) { for (const auto& p : pm) if (p.member == member && p.kind == kind) return true; return false; };
    check(has_kind(2, "species") && has_kind(2, "item") && !has_kind(0, "item") && !has_kind(0, "species"), "team per member: the second Pikachu / second holder of the item is marked, not the first");
    check(has_kind(1, "ohko") && has_kind(2, "evasion") && has_kind(2, "level") && has_kind(3, "category") && has_kind(4, "banned_species") && has_kind(4, "banned_item"), "team per member: moves, level, category and bans each marked on their Pokemon");
    check(er::TeamProblems(f, {a}, data).empty() == false || er::TeamProblems(f, {a, b}, data).size() == 1, "team per member: only the OHKO move is wrong in a good team");
    check(er::TeamProblems(f, {}, data).size() == 1 && er::TeamProblems(f, {}, data)[0].member == -1 && er::TeamProblems(f, {}, data)[0].kind == "size", "team per member: a team of the wrong size is member -1");
  }
  er::Format gens = f;
  gens.bannedCategories.clear();
  gens.bannedSpecies.clear();
  gens.bannedItems.clear();
  const auto gi = er::CheckTeam(gens, {a}, data, {}, [](int sp) { return sp > 386 ? 4 : 3; });
  check(gi.empty(), "team: species of an allowed generation pass");
  {
    er::Format cum = gens;
    cum.gens = {3};
    er::TeamMember oldie = a;
    oldie.species = 9;
    const auto genOf = [](int sp) { return sp <= 151 ? 1 : sp <= 251 ? 2 : sp <= 386 ? 3 : 4; };
    check(er::CheckTeam(cum, {oldie}, data, {}, genOf).empty() && er::TeamProblems(cum, {oldie}, data, {}, genOf).empty(), "team: Gen 1 species are inside Gen 1-3");
    cum.gens = {3, 4};
    oldie.species = 250;
    check(er::CheckTeam(cum, {oldie}, data, {}, genOf).empty(), "team: Gen 2 species are inside Gen 1-4");
    oldie.species = 480;
    cum.gens = {3};
    check(!er::CheckTeam(cum, {oldie}, data, {}, genOf).empty() && !er::TeamProblems(cum, {oldie}, data, {}, genOf).empty(), "team: a Gen 4 species is outside Gen 1-3");
  }
  er::TeamMember g4m = a;
  g4m.species = 400;
  check(!er::CheckTeam(gens, {g4m}, data, {}, [](int sp) { return sp > 386 ? 4 : 3; }).empty(), "team: a generation the format does not allow is refused");
  check(er::LevelUnder({er::LevelRule::Kind::SetAll, 50, 0}, 12) == 50 && er::LevelUnder({}, 12) == 12, "level rule: set-all changes the level in battle only");
  check(er::ClauseCheckedAtSubmit(er::kClauseSpecies) && !er::ClauseCheckedAtSubmit(er::kClauseSleep), "which clauses are checked at team submit");

  std::printf(sOk ? "ALL PASSED\n" : "SOME FAILED\n");
  return sOk ? 0 : 1;
}
