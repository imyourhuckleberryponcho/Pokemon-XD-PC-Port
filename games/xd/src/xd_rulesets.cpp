#include "xd_rulesets.h"

#include "xd_battle_models.h"

#include <playthrough.h>
#include <settings.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xd::rulesets {

namespace er = engine::rulesets;

namespace {

er::Registry Build() {
  er::Registry r;
  {
    er::RuleSet xd;
    xd.id = "xd";
    xd.name = "XD original (Gen 3)";
    xd.info = "The GameCube game's own battle rules.";
    xd.generation = 3;
    xd.mechanics = {{kGen4Rules, "off"}, {kSplit, "player"}, {kModernTypes, "off"}, {kNumbers, "off"}, {kMegaEvolution, "off"}};
    r.AddRuleSet(xd);
    er::RuleSet g4;
    g4.id = "gen4";
    g4.name = "Gen 4 (Diamond / Pearl)";
    g4.info = "Abilities and moves work as in the Gen 4 games. Physical and special moves are split.";
    g4.generation = 4;
    g4.needsGens = {4};
    g4.mechanics = {{kGen4Rules, "on"}, {kSplit, "on"}, {kModernTypes, "off"}, {kNumbers, "on"}, {kMegaEvolution, "off"}};
    r.AddRuleSet(g4);
    for (int g = 5; g <= 7; g++) {
      er::RuleSet later;
      later.id = "gen" + std::to_string(g);
      later.name = std::string("Gen ") + std::to_string(g) + (g == 5 ? " (Black / White)" : g == 6 ? " (X / Y)" : " (Sun / Moon)");
      later.info = g == 5 ? "The Gen 4 rules with the Black / White numbers." : g == 6 ? "Gen 6: Fairy type, 1.5x critical hits, X / Y numbers."
                                                                                   : "Gen 7: milder paralysis and burn, Sun / Moon numbers.";
      later.generation = g;
      later.needsGens = {4};
      later.mechanics = {{kGen4Rules, "on"}, {kSplit, "on"}, {kModernTypes, g >= 6 ? "on" : "off"}, {kNumbers, "on"}, {kMegaEvolution, g >= 6 ? "on" : "off"}};
      r.AddRuleSet(later);
    }
  }
  r.AddGeneration({3, "Gen 1-3 (XD's own Pokemon)", true, ""});
  r.AddGeneration({4, "Gen 1-4 (adds the Gen 4 Pokemon)", true, ""});
  for (int g = 5; g <= 7; g++) r.AddGeneration({g, "Gen 1-" + std::to_string(g), true, ""});
  r.AddSource({"xd", "Original XD models"});
  r.AddSource({"br", "Battle Revolution models"});
  r.AddSource({"usum", "Ultra Sun / Ultra Moon models"});
  r.AddPreset({"original_xd", "Original XD look", "The GameCube look.", {"xd"}});
  r.AddPreset({"most_modern", "Most modern", "The newest models available for each Pokemon.", {"usum", "br", "xd"}});
  r.AddPreset({"br_first", "Battle Revolution first", "The Wii look, which sits closest to XD's own style.", {"br", "xd", "usum"}});
  r.SetFallbackSource("xd");
  r.SetDefaults("xd", {3}, "original_xd");
  return r;
}

std::string Lower(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

const char* const kOwned[] = {"models_br", "models_usum", "gen4_pokemon", "gen4_rules", "modern_types", "gen4_spots", "gen57_pokemon", "gen_numbers", "mega_evolution", "gen5_rules", "gen6_rules", "gen7_rules"};

bool Has(const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); }

}

const er::Registry& Registry() {
  static const er::Registry r = Build();
  return r;
}

bool IsOwnedId(const std::string& id) {
  for (const char* o : kOwned) if (id == o) return true;
  return false;
}

er::Choice FromLegacy(const std::vector<std::string>& challenges, const std::string& split) {
  er::Choice c;
  const int later = Has(challenges, "gen7_rules") ? 7 : Has(challenges, "gen6_rules") ? 6 : Has(challenges, "gen5_rules") ? 5 : 0;
  const bool rules = Has(challenges, "gen4_rules") || later != 0;
  const bool gen57 = Has(challenges, "gen57_pokemon");
  const bool pokemon = rules || gen57 || Has(challenges, "gen4_pokemon");
  c.ruleSet = later != 0 ? "gen" + std::to_string(later) : rules ? "gen4" : "xd";
  c.gens = gen57 ? std::vector<int>{3, 4, 5, 6, 7} : pokemon ? std::vector<int>{3, 4} : std::vector<int>{3};
  if (later == 0 && rules && !Has(challenges, "gen_numbers")) c.mechanics[kNumbers] = "off";
  if (rules) c.mechanics[kSplit] = "player";
  (void)split;
  if (Has(challenges, "modern_types")) c.mechanics[kModernTypes] = "on";
  if (pokemon && Has(challenges, "gen4_spots")) c.pokemonOptions.push_back(kOptionSpots);
  if (Has(challenges, "models_usum")) {
    c.looks.sources = {"usum", "xd"};
  } else if (Has(challenges, "models_br")) {
    c.looks.preset = "br_first";
    c.looks.sources = {"br", "xd"};
  }
  c.present = false;
  er::Choice n = er::Normalize(c, Registry());
  n.present = false;
  return n;
}

std::vector<std::string> LegacyIds(const er::Choice& c) {
  std::vector<std::string> ids;
  const er::Registry& reg = Registry();
  const std::vector<std::string> ranked = er::Ranked(c.looks, reg);
  if (!ranked.empty() && ranked[0] == "usum") ids.push_back("models_usum");
  else if (!ranked.empty() && ranked[0] == "br") ids.push_back("models_br");
  if (c.HasGen(4)) ids.push_back("gen4_pokemon");
  if (c.HasGen(5)) ids.push_back("gen57_pokemon");
  if (c.MechanicOn(reg, kGen4Rules)) ids.push_back("gen4_rules");
  if (c.ruleSet != "xd" && c.MechanicOn(reg, kNumbers)) ids.push_back("gen_numbers");
  if (c.HasGen(5) && c.MechanicOn(reg, kMegaEvolution)) ids.push_back("mega_evolution");
  if (c.ruleSet == "gen5" || c.ruleSet == "gen6" || c.ruleSet == "gen7") ids.push_back(c.ruleSet + "_rules");
  if (c.MechanicOn(reg, kModernTypes)) ids.push_back("modern_types");
  if (c.HasGen(4) && c.HasOption(kOptionSpots)) ids.push_back("gen4_spots");
  return ids;
}

er::Choice ChoiceOf(const engine::playthrough::Setup& s) {
  if (s.rules.present) return s.rules;
  return FromLegacy(s.challenges, s.split);
}

namespace {
void SetOwned(engine::playthrough::Setup& s, const std::vector<std::string>& owned) {
  std::vector<std::string> out;
  for (const std::string& id : s.challenges) if (!IsOwnedId(id)) out.push_back(id);
  std::vector<std::string> all = owned;
  all.insert(all.end(), out.begin(), out.end());
  s.challenges = all;
}
}

bool Resolve(engine::playthrough::Setup& s) {
  if (!s.rules.present) return false;
  s.rules = er::Normalize(s.rules, Registry());
  const std::vector<std::string> before = s.challenges;
  SetOwned(s, LegacyIds(s.rules));
  std::vector<std::string> a = before, b = s.challenges;
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  if (a == b) {
    s.challenges = before;
    return false;
  }
  return true;
}

void Stamp(engine::playthrough::Setup& s) {
  if (s.rules.present) return;
  s.rules = FromLegacy(s.challenges, s.split);
  s.rules.present = true;
}

void Apply(engine::playthrough::Setup& s, const er::Choice& choice) {
  s.rules = er::Normalize(choice, Registry());
  SetOwned(s, LegacyIds(s.rules));
  const std::string split = s.rules.Mechanic(Registry(), kSplit);
  if (split == "on" || split == "off") s.split = split;
}

er::Choice LauncherDefaults(bool gen4Ready) {
  const er::Registry& reg = Registry();
  er::Choice c = er::Default(reg);
  const engine::settings::Registry& st = engine::settings::Global();
  auto text = [&](const char* id) -> std::string {
    const size_t i = st.Find(id);
    return i == engine::settings::kNoSetting ? std::string() : st.ValueText(i);
  };
  const std::string rs = text("playthrough.new_rule_set"), pk = text("playthrough.new_pokemon");
  std::string looks = text("playthrough.new_looks");
  const engine::settings::BoolSetting br = st.Bool("video.battle_revolution_models");
  if ((looks.empty() || looks == "original_xd") && br.Valid() && br.Get()) looks = "br_first";
  if (gen4Ready && pk == "4") c.gens = {3, 4};
  if (gen4Ready && rs == "gen4") { c.ruleSet = "gen4"; c.gens = {3, 4}; }
  if (const er::LooksPreset* p = reg.FindPreset(looks.empty() ? "original_xd" : looks)) { c.looks.preset = p->id; c.looks.sources = p->sources; }
  c.present = true;
  return er::Normalize(c, reg);
}

namespace {
const char* Env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' ? v : nullptr;
}
std::vector<std::string> SplitOn(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
}

TestChoice Test() {
  TestChoice t;
  const char* o = std::getenv("XD_REQUIRE_ORIGINAL");
  if (o != nullptr && std::strcmp(o, "1") == 0) return t;
  const er::Registry& reg = Registry();
  t.choice = er::Choice();
  if (const char* v = Env("XD_RULESET")) {
    if (reg.FindRuleSet(v) != nullptr) { t.choice.ruleSet = v; t.hasRuleSet = true; }
    else std::fprintf(stderr, "[rulesets] XD_RULESET: no rule set \"%s\"; left out\n", v);
  }
  if (const char* v = Env("XD_POKEMON")) {
    const int g = std::atoi(v);
    if (reg.FindGeneration(g) != nullptr) { t.choice.gens = {g}; t.hasPokemon = true; }
    else std::fprintf(stderr, "[rulesets] XD_POKEMON: no generation \"%s\"; left out\n", v);
  }
  if (const char* v = Env("XD_POKEMON_OPTIONS")) {
    for (const std::string& x : SplitOn(v, ',')) t.choice.SetOption(x, true);
    t.hasPokemon = true;
  }
  if (const char* v = Env("XD_RULE_DETAILS")) {
    for (const std::string& x : SplitOn(v, ',')) {
      const size_t c = x.find(':');
      if (c != std::string::npos) { t.choice.mechanics[x.substr(0, c)] = x.substr(c + 1); t.hasRuleSet = true; }
    }
  }
  if (const char* v = Env("XD_LOOKS")) {
    if (const er::LooksPreset* p = reg.FindPreset(v)) {
      t.choice.looks.preset = p->id;
      t.choice.looks.sources = p->sources;
      t.hasLooks = true;
    } else {
      for (const std::string& x : SplitOn(v, ',')) {
        if (reg.FindSource(Lower(x)) != nullptr) t.choice.looks.sources.push_back(Lower(x));
        else std::fprintf(stderr, "[rulesets] XD_LOOKS: no model source \"%s\"; left out\n", x.c_str());
      }
      t.hasLooks = !t.choice.looks.sources.empty();
    }
  }
  if (const char* v = Env("XD_LOOKS_PINS")) {
    for (const std::string& x : SplitOn(v, ',')) {
      const size_t c = x.find(':');
      if (c != std::string::npos && reg.FindSource(x.substr(c + 1)) != nullptr) t.choice.looks.overrides[std::atoi(x.c_str())] = x.substr(c + 1);
    }
    t.hasLooks = true;
  }
  t.any = t.hasRuleSet || t.hasPokemon || t.hasLooks;
  return t;
}

std::vector<std::string> TestLegacyIds() {
  const TestChoice t = Test();
  if (!t.hasRuleSet && !t.hasPokemon) return {};
  er::Choice c = er::Normalize(t.choice, Registry());
  c.looks = {};
  return LegacyIds(c);
}

const SourceFolder* SourceLayer(const std::string& id) {
  static const SourceFolder br{"br", xd::battle_models::kLayerId, "Battle Revolution models"};
  static const SourceFolder usum{"usum", xd::battle_models::kUsumLayerId, "Ultra Sun / Ultra Moon models"};
  if (id == "br") return &br;
  if (id == "usum") return &usum;
  return nullptr;
}

namespace {
er::Looks Effective(const er::Looks& looks, bool settingOn) {
  if (looks.Says()) return looks;
  er::Looks l;
  if (settingOn) l.sources = {"br", "xd"};
  return l;
}
}

std::vector<std::string> LayerOrderOf(const er::Looks& looks, bool settingOn) { return er::LayerOrder(Effective(looks, settingOn), Registry()); }

std::string RankedText(const er::Looks& looks, bool settingOn) {
  std::vector<std::string> r = er::Ranked(Effective(looks, settingOn), Registry());
  if (r.empty()) r.push_back("xd");
  std::string t;
  for (const std::string& s : r) t += (t.empty() ? "" : " > ") + s;
  return t;
}

int GenerationOfNational(int n) {
  if (n < 1) return 0;
  if (n <= 151) return 1;
  if (n <= 251) return 2;
  if (n <= 386) return 3;
  if (n <= 493) return 4;
  return 0;
}
bool IsMythical(int n) { return n == 151 || n == 251 || n == 385 || n == 386 || (n >= 489 && n <= 493); }
bool IsLegendary(int n) { return (n >= 144 && n <= 146) || n == 150 || (n >= 243 && n <= 245) || n == 249 || n == 250 || (n >= 377 && n <= 384) || (n >= 480 && n <= 488); }

namespace {
er::Format Make(const char* id, const char* name, const char* info, const char* rs, std::vector<int> gens, const char* style) {
  er::Format f;
  f.id = id;
  f.name = name;
  f.info = info;
  f.ruleSet = rs;
  f.gens = std::move(gens);
  f.style = style;
  return f;
}
void Competitive(er::Format& f, int pick) {
  f.level = {er::LevelRule::Kind::SetAll, 50, 0};
  f.clauses = {er::kClauseSpecies, er::kClauseItem, er::kClauseSleep, er::kClauseOhko, er::kClauseEvasion};
  f.teamPreview = true;
  f.pick = pick;
  f.timerPerChoice = 60;
  f.timerPerBattle = 420;
  f.timerPreview = 90;
}
std::vector<er::Format> BuildFormats() {
  std::vector<er::Format> v;
  v.push_back(Make("xd_classic", "XD Classic", "XD's own rules, Gen 1-3, levels as they are, no extra rules.", "xd", {3}, "single"));
  v.push_back(Make("xd_classic_doubles", "XD Classic Doubles", "As XD Classic, two Pokemon a side (like the Colosseum).", "xd", {3}, "double"));
  er::Format l50 = Make("level50_singles", "Level 50 Singles", "XD's rules with the physical/special split, every Pokemon at level 50, one of each species and item.", "xd", {3}, "single");
  l50.details = {{kSplit, "on"}};
  Competitive(l50, 3);
  v.push_back(l50);
  er::Format g4 = Make("gen4_singles", "Gen 4 Singles", "The Gen 4 rules, level 50, one of each species and item, sleep / OHKO / evasion clauses.", "gen4", {3, 4}, "single");
  Competitive(g4, 3);
  v.push_back(g4);
  er::Format g4d = Make("gen4_doubles", "Gen 4 Doubles", "The Gen 4 rules, two Pokemon a side, level 50, one of each species and item.", "gen4", {3, 4}, "double");
  Competitive(g4d, 4);
  g4d.clauses = {er::kClauseSpecies, er::kClauseItem};
  v.push_back(g4d);
  v.push_back(Make("anything_goes", "Anything goes", "Whatever rule set the host has; levels as they are, no clauses.", "", {}, "single"));
  return v;
}
}

const std::vector<er::Format>& Formats() {
  static const std::vector<er::Format> f = BuildFormats();
  return f;
}
const er::Format* FindFormat(const std::string& id) {
  for (const er::Format& f : Formats()) if (f.id == id) return &f;
  return nullptr;
}

std::string FormatDigest(const er::Format& f) {
  const std::string t = f.ToText() + "|" + f.ruleSet;
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : t) { h ^= c; h *= 1099511628211ull; }
  char b[16];
  std::snprintf(b, sizeof b, "%08x", static_cast<unsigned>(h ^ (h >> 32)));
  return b;
}

er::ClauseData ClauseDataFor(bool) {
  er::ClauseData d;
  d.ohkoMoves = {12, 32, 90, 329};
  d.evasionMoves = {104, 107};
  for (int n = 1; n <= 493; n++) {
    if (IsLegendary(n)) d.categories["legendary"].insert(n);
    if (IsMythical(n)) d.categories["mythical"].insert(n);
  }
  return d;
}

}
