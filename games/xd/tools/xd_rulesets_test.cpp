#include "../src/xd_rulesets.h"

#include <playthrough.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace er = engine::rulesets;
namespace ep = engine::playthrough;
namespace xr = xd::rulesets;
namespace fs = std::filesystem;

namespace {
void SetEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}
bool sOk = true;
void check(bool cond, const std::string& what) {
  if (!cond || std::getenv("XD_RULESETS_TEST_VERBOSE")) std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

bool Has(const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); }

struct Old {
  bool pack, rules, spots, modern;
  int model;
};
Old OldMeaning(const std::vector<std::string>& r) {
  Old o;
  o.pack = Has(r, "gen4_pokemon") || Has(r, "gen4_rules");
  o.rules = Has(r, "gen4_rules");
  o.spots = o.pack && Has(r, "gen4_spots");
  o.modern = Has(r, "modern_types");
  o.model = Has(r, "models_usum") ? 2 : Has(r, "models_br") ? 1 : -1;
  return o;
}
Old NewMeaning(const er::Choice& c) {
  const er::Registry& reg = xr::Registry();
  Old o;
  o.pack = c.HasGen(4);
  o.rules = c.MechanicOn(reg, xr::kGen4Rules);
  o.spots = o.pack && c.HasOption(xr::kOptionSpots);
  o.modern = c.MechanicOn(reg, xr::kModernTypes);
  const std::vector<std::string> ranked = er::Ranked(c.looks, reg);
  o.model = ranked.empty() ? -1 : ranked[0] == "usum" ? 2 : ranked[0] == "br" ? 1 : -1;
  return o;
}
bool Same(const Old& a, const Old& b) { return a.pack == b.pack && a.rules == b.rules && a.spots == b.spots && a.modern == b.modern && a.model == b.model; }

ep::Setup Made(const std::vector<std::string>& ids, const std::string& split = "") {
  ep::Setup s;
  s.challenges = ids;
  s.split = split;
  s.started = "2026-11-18 10:00";
  s.portVersion = "0.6.1";
  return s;
}
}

int main(int argc, char** argv) {
  const std::vector<const char*> six = {"gen4_pokemon", "gen4_rules", "gen4_spots", "modern_types", "models_br", "models_usum"};

  int combos = 0, bad = 0;
  for (unsigned mask = 0; mask < (1u << six.size()); mask++) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < six.size(); i++) if (mask & (1u << i)) ids.push_back(six[i]);
    ids.push_back("level_cap");
    const Old before = OldMeaning(ids);
    ep::Setup old = Made(ids, mask & 2 ? "off" : "");
    ep::Setup fromOld;
    std::string err;
    if (!ep::FromJson(ep::ToJson(old), fromOld, err)) { check(false, "old file reads: " + err); continue; }
    check(!fromOld.rules.present, "an old file has no keys");
    const bool changed = xr::Resolve(fromOld);
    if (changed || fromOld.challenges != ids) { bad++; check(false, "an old file's challenge list is left exactly as it is, mask " + std::to_string(mask)); }
    if (!Same(before, NewMeaning(xr::ChoiceOf(fromOld)))) { bad++; check(false, "derived choice means what the old ids meant, mask " + std::to_string(mask)); }
    xr::Stamp(fromOld);
    ep::Setup again;
    const std::string text = ep::ToJson(fromOld);
    if (!ep::FromJson(text, again, err) || !again.rules.present) { bad++; check(false, "stamped file reads back with keys, mask " + std::to_string(mask)); continue; }
    xr::Resolve(again);
    if (!Same(before, NewMeaning(again.rules)) || !Same(before, OldMeaning(again.challenges)) || !Has(again.challenges, "level_cap")) {
      bad++;
      check(false, "stamped file means the same through its keys and through its old ids, mask " + std::to_string(mask) + "\n" + text);
    }
    if (again.split != old.split) { bad++; check(false, "the split of the playthrough is untouched, mask " + std::to_string(mask)); }
    combos++;
  }
  check(bad == 0, "all " + std::to_string(combos) + " combinations of the six old ids mean the same through the new choices");

  {
    er::Choice c = er::Default(xr::Registry());
    c.ruleSet = "gen4";
    c.gens = {3, 4};
    c.mechanics[xr::kModernTypes] = "on";
    c.SetOption(xr::kOptionSpots, true);
    c.looks.preset = "most_modern";
    c.looks.sources = {"usum", "br", "xd"};
    ep::Setup s = Made({"nuzlocke_faint"});
    xr::Apply(s, c);
    check(s.rules.present && s.split == "on", "Apply: the keys are set; Gen 4 fixes the split on");
    check(Has(s.challenges, "gen4_pokemon") && Has(s.challenges, "gen4_rules") && Has(s.challenges, "modern_types") && Has(s.challenges, "gen4_spots") &&
              Has(s.challenges, "models_usum") && Has(s.challenges, "nuzlocke_faint") && !Has(s.challenges, "models_br"),
          "Apply: the old ids next to the keys; the other challenge stays");
    ep::Setup back;
    std::string err;
    check(ep::FromJson(ep::ToJson(s), back, err) && back.rules == s.rules, "the keys round trip through the setup file");
    back.challenges = {"nuzlocke_faint"};
    xr::Resolve(back);
    check(Has(back.challenges, "gen4_rules") && Has(back.challenges, "models_usum") && Has(back.challenges, "nuzlocke_faint"), "Resolve: the keys win over the challenge list");
    ep::Setup xdRules = Made({});
    c.ruleSet = "xd";
    c.mechanics.clear();
    c.SetOption(xr::kOptionSpots, false);
    c.looks = {};
    xr::Apply(xdRules, c);
    check(!Has(xdRules.challenges, "gen4_rules") && Has(xdRules.challenges, "gen4_pokemon") && xdRules.split.empty(),
          "XD rules with the Gen 4 Pokemon: the pack only (Gen 4 Pokemon under Gen 3 rules)");
    c.gens = {3};
    ep::Setup plain = Made({});
    xr::Apply(plain, c);
    check(plain.challenges.empty() && plain.Vanilla(), "XD rules, Gen 1-3, no looks: the plain game, Vanilla");
  }

  {
    er::Looks none, usum, br, modern, first;
    usum.sources = {"usum", "xd"};
    br.sources = {"br", "xd"};
    modern.sources = {"usum", "br", "xd"};
    first.sources = {"br", "xd", "usum"};
    check(xr::LayerOrderOf(none, false).empty(), "looks: nothing said, setting off: XD's own");
    check(xr::LayerOrderOf(none, true) == std::vector<std::string>{"br"}, "looks: nothing said, the old setting on: Battle Revolution");
    check(xr::LayerOrderOf(usum, true) == std::vector<std::string>{"usum"}, "looks: models_usum: Ultra Sun / Ultra Moon only");
    check(xr::LayerOrderOf(br, false) == std::vector<std::string>{"br"}, "looks: models_br: Battle Revolution only");
    check((xr::LayerOrderOf(modern, false) == std::vector<std::string>{"br", "usum"}), "looks: most modern: Battle Revolution below Ultra Sun / Ultra Moon");
    check(xr::LayerOrderOf(first, false) == std::vector<std::string>{"br"}, "looks: Battle Revolution first: nothing is ranked below XD");
    check(xr::RankedText(modern, false) == "usum > br > xd" && xr::RankedText(none, false) == "xd", "looks: the line for the log");
    const er::HasModel has = [](const std::string& s, int sp) { return s == "usum" ? sp <= 100 : s == "br" ? sp <= 200 || sp > 386 : false; };
    check(er::ResolveSpecies(modern, xr::Registry(), 25, has).source == "usum", "looks: species 25 gets Ultra Sun / Ultra Moon");
    check(er::ResolveSpecies(modern, xr::Registry(), 150, has).source == "br", "looks: species 150 (not in USUM) gets Battle Revolution");
    check(er::ResolveSpecies(modern, xr::Registry(), 300, has).source == "xd", "looks: species 300 (neither) gets XD's own");
    check(er::ResolveSpecies(modern, xr::Registry(), 400, has).source == "br", "looks: a Gen 4 species gets Battle Revolution's (its pack)");
    er::Looks pinned = modern;
    pinned.overrides[25] = "br";
    check(er::ResolveSpecies(pinned, xr::Registry(), 25, has).source == "br" && er::ResolveSpecies(pinned, xr::Registry(), 26, has).source == "usum", "looks: a pin for one species only");
  }

  {
    SetEnv("XD_RULESET", "gen4");
    SetEnv("XD_POKEMON", "4");
    const std::vector<std::string> ids = xr::TestLegacyIds();
    check(Has(ids, "gen4_rules") && Has(ids, "gen4_pokemon"), "XD_RULESET=gen4 XD_POKEMON=4 mean the old Gen 4 rules + Pokemon");
    SetEnv("XD_RULESET", "");
    SetEnv("XD_POKEMON", "");
    SetEnv("XD_LOOKS", "most_modern");
    const xr::TestChoice t = xr::Test();
    check(t.hasLooks && !t.hasRuleSet && t.choice.looks.sources == (std::vector<std::string>{"usum", "br", "xd"}) && xr::TestLegacyIds().empty(), "XD_LOOKS=most_modern: looks only");
    SetEnv("XD_LOOKS", "");
  }

  {
    check(xr::Formats().size() == 6, "six formats");
    for (const er::Format& f : xr::Formats()) {
      er::Format p;
      std::string err;
      check(er::ParseFormat(f.ToText(), p, err) && p == f && !xr::FormatDigest(f).empty(), "format " + f.id + " round trips");
      check(f.ruleSet.empty() || xr::Registry().FindRuleSet(f.ruleSet) != nullptr, "format " + f.id + " names a known rule set");
    }
    const er::Format* g4 = xr::FindFormat("gen4_singles");
    check(g4 != nullptr && g4->level.kind == er::LevelRule::Kind::SetAll && g4->level.a == 50 && g4->clauses.count("species") && g4->teamPreview && g4->pick == 3, "Gen 4 Singles: level 50, clauses, preview 6 pick 3");
    const er::ClauseData data = xr::ClauseDataFor(true);
    er::TeamMember a, b;
    a.species = 25; a.level = 50; a.item = 3;
    b = a;
    b.moves[0] = 90;
    const auto issues = er::CheckTeam(*xr::FindFormat("level50_singles"), {a, b}, data);
    check(issues.size() == 3, "Level 50 Singles refuses a team with a repeated species, a repeated item and Fissure (" + std::to_string(issues.size()) + ")");
    er::TeamMember legend = a;
    legend.species = 150;
    er::Format nl = *xr::FindFormat("anything_goes");
    nl.bannedCategories = {"legendary"};
    check(!er::CheckTeam(nl, {legend}, data).empty() && er::CheckTeam(nl, {a}, data).empty(), "a ban category (legendary) from the game's own list");
    check(xr::GenerationOfNational(25) == 1 && xr::GenerationOfNational(387) == 4 && xr::GenerationOfNational(494) == 0, "generation of a national number");
  }

  if (argc >= 2) {
    int files = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(argv[1])) {
      if (e.path().extension() != ".json") continue;
      std::ifstream in(e.path(), std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      ep::Setup s;
      std::string err;
      if (!ep::FromJson(ss.str(), s, err)) continue;
      files++;
      const std::vector<std::string> before = s.challenges;
      const Old meaning = OldMeaning(before);
      const bool hadKeys = s.rules.present;
      xr::Resolve(s);
      check(hadKeys || s.challenges == before, e.path().filename().string() + ": challenge list left exactly as it is");
      check(Same(meaning, NewMeaning(xr::ChoiceOf(s))), e.path().filename().string() + ": same meaning through the choices");
      xr::Stamp(s);
      ep::Setup back;
      check(ep::FromJson(ep::ToJson(s), back, err) && back.rules.present && Same(meaning, NewMeaning(back.rules)), e.path().filename().string() + ": stamped and read back");
    }
    std::printf("setup files from %s: %d checked\n", argv[1], files);
  }

  {
    const auto has = [](const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    for (int g = 5; g <= 7; g++) {
      const std::string id = "gen" + std::to_string(g);
      er::Choice c;
      c.ruleSet = id;
      c = er::Normalize(c, xr::Registry());
      const std::vector<std::string> ids = xr::LegacyIds(c);
      check(xr::Registry().FindRuleSet(id) != nullptr && xr::Registry().FindRuleSet(id)->available, id + ": available");
      check(has(ids, "gen4_pokemon") && has(ids, "gen4_rules") && has(ids, "gen_numbers") && has(ids, (id + "_rules").c_str()), id + ": the Gen 4 rules, the numbers and its own id are written");
      check(has(ids, "modern_types") == (g >= 6), id + ": the Fairy type from Gen 6 on");
      const er::Choice back = xr::FromLegacy(ids, "");
      check(back.ruleSet == id && back.MechanicOn(xr::Registry(), xr::kNumbers), id + ": the old ids mean the same rule set again");
    }
    const er::Choice old = xr::FromLegacy({"gen4_pokemon", "gen4_rules"}, "");
    check(old.ruleSet == "gen4" && !old.MechanicOn(xr::Registry(), xr::kNumbers) && !has(xr::LegacyIds(old), "gen_numbers"), "an old Gen 4 rules file keeps its numbers: none of the Gen 4 numbers");
    er::Choice fresh;
    fresh.ruleSet = "gen4";
    fresh = er::Normalize(fresh, xr::Registry());
    check(has(xr::LegacyIds(fresh), "gen_numbers"), "a Gen 4 rule set chosen now has the Gen 4 numbers");
    check(!has(xr::LegacyIds(er::Default(xr::Registry())), "gen_numbers"), "the original rule set never has them");
    er::Choice pk;
    pk.gens = {3, 4, 5, 6, 7};
    check(has(xr::LegacyIds(er::Normalize(pk, xr::Registry())), "gen57_pokemon"), "Pokemon of Gen 5-7: the pack's old id");
    check(xr::FromLegacy({"gen4_pokemon", "gen57_pokemon"}, "").HasGen(7), "the old id of the Gen 5-7 Pokemon reads as Gen 1-7");
  }

  {
    const auto has = [](const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    for (int g = 5; g <= 7; g++) {
      er::Choice c;
      c.ruleSet = "gen" + std::to_string(g);
      c.gens = {3, 4, 5, 6, 7};
      check(has(xr::LegacyIds(er::Normalize(c, xr::Registry())), "mega_evolution") == (g >= 6), "gen" + std::to_string(g) + ": Mega Evolution with the Gen 5-7 Pokemon only from Gen 6");
      c.gens = {3, 4};
      check(!has(xr::LegacyIds(er::Normalize(c, xr::Registry())), "mega_evolution"), "gen" + std::to_string(g) + ": no Mega Evolution without the Gen 5-7 Pokemon");
    }
  }

  std::printf(sOk ? "ALL PASSED\n" : "SOME FAILED\n");
  return sOk ? 0 : 1;
}
