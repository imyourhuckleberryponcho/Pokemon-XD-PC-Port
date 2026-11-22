#include "rulesets.h"

#include "json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace engine::rulesets {

using mods::json::Value;

namespace {

std::vector<std::string> SplitOn(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string Join(const std::vector<std::string>& v, const char* sep = ",") {
  std::string out;
  for (size_t i = 0; i < v.size(); i++) out += (i ? sep : "") + v[i];
  return out;
}

std::string GensText(const std::vector<int>& g) {
  std::string out;
  for (size_t i = 0; i < g.size(); i++) out += (i ? "," : "") + std::to_string(g[i]);
  return out;
}

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char esc[8];
      std::snprintf(esc, sizeof esc, "\\u%04x", c);
      out += esc;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

bool ToInt(const std::string& s, int& out) {
  if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
  out = std::atoi(s.c_str());
  return true;
}

std::vector<int> SortedUnique(std::vector<int> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

}

const RuleSet* Registry::FindRuleSet(std::string_view id) const {
  for (const RuleSet& r : mRuleSets) if (r.id == id) return &r;
  return nullptr;
}
const LooksSource* Registry::FindSource(std::string_view id) const {
  for (const LooksSource& s : mSources) if (s.id == id) return &s;
  return nullptr;
}
const LooksPreset* Registry::FindPreset(std::string_view id) const {
  for (const LooksPreset& p : mPresets) if (p.id == id) return &p;
  return nullptr;
}
const Generation* Registry::FindGeneration(int number) const {
  for (const Generation& g : mGens) if (g.number == number) return &g;
  return nullptr;
}

bool Choice::HasGen(int g) const { return std::find(gens.begin(), gens.end(), g) != gens.end(); }
bool Choice::HasOption(const std::string& id) const { return std::find(pokemonOptions.begin(), pokemonOptions.end(), id) != pokemonOptions.end(); }
void Choice::SetOption(const std::string& id, bool on) {
  const auto it = std::find(pokemonOptions.begin(), pokemonOptions.end(), id);
  if (on && it == pokemonOptions.end()) {
    pokemonOptions.push_back(id);
    std::sort(pokemonOptions.begin(), pokemonOptions.end());
  } else if (!on && it != pokemonOptions.end()) {
    pokemonOptions.erase(it);
  }
}

std::string Choice::Mechanic(const Registry& registry, const std::string& id) const {
  const auto o = mechanics.find(id);
  if (o != mechanics.end()) return o->second;
  if (const RuleSet* rs = registry.FindRuleSet(ruleSet)) {
    for (const auto& m : rs->mechanics) if (m.first == id) return m.second;
  }
  return "off";
}

std::string Choice::Canonical() const {
  std::string t = "rs=" + ruleSet + ";gens=" + GensText(gens);
  if (!mechanics.empty()) {
    std::string m;
    for (const auto& kv : mechanics) m += (m.empty() ? "" : ",") + kv.first + ":" + kv.second;
    t += ";mech=" + m;
  }
  if (!pokemonOptions.empty()) t += ";opt=" + Join(pokemonOptions);
  if (looks.Says()) t += ";looks=" + Join(looks.sources);
  if (!looks.overrides.empty()) {
    std::string o;
    for (const auto& kv : looks.overrides) o += (o.empty() ? "" : ",") + std::to_string(kv.first) + ":" + kv.second;
    t += ";ov=" + o;
  }
  return t;
}

bool ParseCanonical(std::string_view text, Choice& out, std::string& error) {
  Choice c;
  for (const std::string& item : SplitOn(text, ';')) {
    const size_t eq = item.find('=');
    if (eq == std::string::npos) { error = "'" + item + "' is not key=value"; return false; }
    const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
    if (k == "rs") {
      c.ruleSet = v;
    } else if (k == "gens") {
      for (const std::string& g : SplitOn(v, ',')) {
        int n;
        if (!ToInt(g, n)) { error = "bad generation '" + g + "'"; return false; }
        c.gens.push_back(n);
      }
    } else if (k == "mech") {
      for (const std::string& m : SplitOn(v, ',')) {
        const size_t colon = m.find(':');
        if (colon == std::string::npos) { error = "bad mechanic '" + m + "'"; return false; }
        c.mechanics[m.substr(0, colon)] = m.substr(colon + 1);
      }
    } else if (k == "opt") {
      c.pokemonOptions = SplitOn(v, ',');
    } else if (k == "looks") {
      c.looks.sources = SplitOn(v, ',');
    } else if (k == "ov") {
      for (const std::string& m : SplitOn(v, ',')) {
        const size_t colon = m.find(':');
        int n;
        if (colon == std::string::npos || !ToInt(m.substr(0, colon), n)) { error = "bad override '" + m + "'"; return false; }
        c.looks.overrides[n] = m.substr(colon + 1);
      }
    } else {
      error = "unknown key '" + k + "'";
      return false;
    }
  }
  if (c.ruleSet.empty()) { error = "no rule set"; return false; }
  c.present = true;
  out = c;
  return true;
}

std::vector<std::string> Validate(const Choice& c, const Registry& registry) {
  std::vector<std::string> out;
  const RuleSet* rs = registry.FindRuleSet(c.ruleSet);
  if (rs == nullptr) {
    out.push_back("The rule set \"" + c.ruleSet + "\" is not known to this version.");
  } else {
    if (!rs->available) out.push_back("The rule set \"" + rs->name + "\" is not available yet" + (rs->unavailableWhy.empty() ? "." : ": " + rs->unavailableWhy));
    for (int g : rs->needsGens) {
      if (!c.HasGen(g)) {
        const Generation* gen = registry.FindGeneration(g);
        out.push_back("The rule set \"" + rs->name + "\" needs the Pokemon of " + (gen ? gen->name : "generation " + std::to_string(g)) + " in this version.");
      }
    }
  }
  if (c.gens.empty()) out.push_back("No Pokemon generation is chosen.");
  for (int g : c.gens) {
    const Generation* gen = registry.FindGeneration(g);
    if (gen == nullptr) out.push_back("The Pokemon of generation " + std::to_string(g) + " are not known to this version.");
    else if (!gen->available) out.push_back(gen->name + " are not available yet" + (gen->unavailableWhy.empty() ? "." : ": " + gen->unavailableWhy));
  }
  for (const std::string& s : c.looks.sources) {
    if (registry.FindSource(s) == nullptr) out.push_back("The model source \"" + s + "\" is not known to this version.");
  }
  for (const auto& kv : c.looks.overrides) {
    if (registry.FindSource(kv.second) == nullptr) out.push_back("Species " + std::to_string(kv.first) + " is set to the unknown model source \"" + kv.second + "\".");
    if (kv.first < 1) out.push_back("A per-species look names species " + std::to_string(kv.first) + ".");
  }
  for (const auto& kv : c.mechanics) {
    if (kv.second != "on" && kv.second != "off" && kv.second != "player") out.push_back("The switch \"" + kv.first + "\" is set to \"" + kv.second + "\" (on, off or player).");
  }
  return out;
}

Choice Normalize(Choice c, const Registry& registry) {
  c.present = true;
  if (c.ruleSet.empty() || registry.FindRuleSet(c.ruleSet) == nullptr) c.ruleSet = registry.DefaultRuleSet();
  c.gens = SortedUnique(c.gens);
  c.gens.erase(std::remove_if(c.gens.begin(), c.gens.end(), [&](int g) { return registry.FindGeneration(g) == nullptr; }), c.gens.end());
  if (const RuleSet* rs = registry.FindRuleSet(c.ruleSet)) {
    for (int g : rs->needsGens) c.gens.push_back(g);
  }
  c.gens = SortedUnique(c.gens);
  if (c.gens.empty()) c.gens = registry.DefaultGens();
  if (!c.gens.empty() && !registry.Generations().empty()) {
    int low = registry.Generations().front().number;
    for (const Generation& g : registry.Generations()) low = std::min(low, g.number);
    const int high = c.gens.back();
    c.gens.clear();
    for (int g = low; g <= high; g++) if (registry.FindGeneration(g) != nullptr) c.gens.push_back(g);
  }
  for (auto it = c.mechanics.begin(); it != c.mechanics.end();) {
    if (it->second != "on" && it->second != "off" && it->second != "player") it = c.mechanics.erase(it);
    else ++it;
  }
  if (const RuleSet* rs = registry.FindRuleSet(c.ruleSet)) {
    for (auto it = c.mechanics.begin(); it != c.mechanics.end();) {
      bool same = false;
      for (const auto& m : rs->mechanics) if (m.first == it->first && m.second == it->second) same = true;
      if (same) it = c.mechanics.erase(it);
      else ++it;
    }
  }
  std::sort(c.pokemonOptions.begin(), c.pokemonOptions.end());
  c.pokemonOptions.erase(std::unique(c.pokemonOptions.begin(), c.pokemonOptions.end()), c.pokemonOptions.end());
  std::vector<std::string> src;
  for (const std::string& s : c.looks.sources) {
    if (registry.FindSource(s) != nullptr && std::find(src.begin(), src.end(), s) == src.end()) src.push_back(s);
  }
  c.looks.sources = src;
  for (auto it = c.looks.overrides.begin(); it != c.looks.overrides.end();) {
    if (registry.FindSource(it->second) == nullptr || it->first < 1) it = c.looks.overrides.erase(it);
    else ++it;
  }
  if (!c.looks.preset.empty()) {
    const LooksPreset* p = registry.FindPreset(c.looks.preset);
    Looks pl;
    if (p != nullptr) pl.sources = p->sources;
    if (p == nullptr || Ranked(pl, registry) != Ranked(c.looks, registry)) c.looks.preset.clear();
  }
  return c;
}

Choice Default(const Registry& registry) {
  Choice c;
  c.present = true;
  c.ruleSet = registry.DefaultRuleSet();
  c.gens = registry.DefaultGens();
  if (const LooksPreset* p = registry.FindPreset(registry.DefaultPreset())) {
    c.looks.preset = p->id;
    c.looks.sources = p->sources;
  }
  return Normalize(c, registry);
}

std::string ToJsonKeys(const Choice& c, int indent) {
  const std::string in(static_cast<size_t>(indent), ' ');
  std::string o = in + "\"rule_set\": {\"id\": " + Quote(c.ruleSet);
  if (!c.mechanics.empty()) {
    o += ", \"details\": {";
    size_t i = 0;
    for (const auto& kv : c.mechanics) o += std::string(i++ ? ", " : "") + Quote(kv.first) + ": " + Quote(kv.second);
    o += "}";
  }
  o += "},\n" + in + "\"pokemon\": {\"gens\": [" + GensText(c.gens) + "]";
  if (!c.pokemonOptions.empty()) {
    o += ", \"options\": [";
    for (size_t i = 0; i < c.pokemonOptions.size(); i++) o += (i ? ", " : "") + Quote(c.pokemonOptions[i]);
    o += "]";
  }
  o += "}";
  if (c.looks.Says() || !c.looks.overrides.empty()) {
    o += ",\n" + in + "\"looks\": {";
    bool first = true;
    if (!c.looks.preset.empty()) { o += "\"style\": " + Quote(c.looks.preset); first = false; }
    if (c.looks.Says()) {
      o += std::string(first ? "" : ", ") + "\"sources\": [";
      for (size_t i = 0; i < c.looks.sources.size(); i++) o += (i ? ", " : "") + Quote(c.looks.sources[i]);
      o += "]";
      first = false;
    }
    if (!c.looks.overrides.empty()) {
      o += std::string(first ? "" : ", ") + "\"pins\": {";
      size_t i = 0;
      for (const auto& kv : c.looks.overrides) o += std::string(i++ ? ", " : "") + Quote(std::to_string(kv.first)) + ": " + Quote(kv.second);
      o += "}";
    }
    o += "}";
  }
  return o;
}

bool FromJson(const Value& root, Choice& out, std::string& error) {
  Choice c;
  const auto text = [](const Value& o, const char* k) {
    const auto it = o.fields.find(k);
    return it != o.fields.end() && it->second.kind == Value::Kind::String ? it->second.text : std::string();
  };
  const auto rs = root.fields.find("rule_set");
  if (rs == root.fields.end()) { out = Choice(); return true; }
  if (rs->second.kind != Value::Kind::Object || text(rs->second, "id").empty()) { error = "\"rule_set\" needs an \"id\""; return false; }
  c.ruleSet = text(rs->second, "id");
  const auto me = rs->second.fields.find("details");
  if (me != rs->second.fields.end() && me->second.kind == Value::Kind::Object) {
    for (const auto& kv : me->second.fields) if (kv.second.kind == Value::Kind::String) c.mechanics[kv.first] = kv.second.text;
  }
  const auto pk = root.fields.find("pokemon");
  if (pk != root.fields.end() && pk->second.kind == Value::Kind::Object) {
    const auto g = pk->second.fields.find("gens");
    if (g != pk->second.fields.end() && g->second.kind == Value::Kind::Array) {
      for (const Value& v : g->second.items) if (v.kind == Value::Kind::Number && v.number >= 1 && v.number < 100) c.gens.push_back(static_cast<int>(v.number));
    }
    const auto op = pk->second.fields.find("options");
    if (op != pk->second.fields.end() && op->second.kind == Value::Kind::Array) {
      for (const Value& v : op->second.items) if (v.kind == Value::Kind::String) c.pokemonOptions.push_back(v.text);
    }
  }
  const auto lk = root.fields.find("looks");
  if (lk != root.fields.end() && lk->second.kind == Value::Kind::Object) {
    c.looks.preset = text(lk->second, "style");
    const auto s = lk->second.fields.find("sources");
    if (s != lk->second.fields.end() && s->second.kind == Value::Kind::Array) {
      for (const Value& v : s->second.items) if (v.kind == Value::Kind::String) c.looks.sources.push_back(v.text);
    }
    const auto ov = lk->second.fields.find("pins");
    if (ov != lk->second.fields.end() && ov->second.kind == Value::Kind::Object) {
      for (const auto& kv : ov->second.fields) {
        int n;
        if (kv.second.kind == Value::Kind::String && ToInt(kv.first, n)) c.looks.overrides[n] = kv.second.text;
      }
    }
  }
  c.gens = SortedUnique(c.gens);
  c.present = true;
  out = c;
  return true;
}

std::vector<std::string> Ranked(const Looks& looks, const Registry& registry) {
  std::vector<std::string> out;
  if (!looks.Says()) return out;
  for (const std::string& s : looks.sources) {
    if (registry.FindSource(s) != nullptr && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
  }
  const auto fb = std::find(out.begin(), out.end(), registry.FallbackSource());
  if (fb != out.end()) out.erase(fb + 1, out.end());
  else out.push_back(registry.FallbackSource());
  return out;
}

std::vector<std::string> LayerOrder(const Looks& looks, const Registry& registry) {
  std::vector<std::string> r = Ranked(looks, registry);
  std::vector<std::string> out;
  for (auto it = r.rbegin(); it != r.rend(); ++it) {
    if (*it != registry.FallbackSource()) out.push_back(*it);
  }
  return out;
}

Pick ResolveSpecies(const Looks& looks, const Registry& registry, int species, const HasModel& has) {
  Pick p;
  const auto ov = looks.overrides.find(species);
  if (ov != looks.overrides.end() && registry.FindSource(ov->second) != nullptr &&
      (ov->second == registry.FallbackSource() || has(ov->second, species))) {
    p.source = ov->second;
    p.overridden = true;
    p.rank = -1;
    return p;
  }
  const std::vector<std::string> r = Ranked(looks, registry);
  for (size_t i = 0; i < r.size(); i++) {
    if (r[i] == registry.FallbackSource() || has(r[i], species)) {
      p.source = r[i];
      p.rank = static_cast<int>(i);
      return p;
    }
  }
  p.source = registry.FallbackSource();
  p.rank = static_cast<int>(r.size());
  return p;
}

std::string LevelRule::Text() const {
  switch (kind) {
    case Kind::SetAll: return "all:" + std::to_string(a);
    case Kind::Cap: return "cap:" + std::to_string(a);
    case Kind::Range: return "range:" + std::to_string(a) + "-" + std::to_string(b);
    default: return "asis";
  }
}

bool LevelRule::Parse(std::string_view text, LevelRule& out) {
  LevelRule r;
  if (text == "asis") { out = r; return true; }
  const size_t colon = text.find(':');
  if (colon == std::string_view::npos) return false;
  const std::string kind(text.substr(0, colon)), rest(text.substr(colon + 1));
  int a, b;
  if (kind == "all" || kind == "cap") {
    if (!ToInt(rest, a) || a < 1 || a > 100) return false;
    r.kind = kind == "all" ? Kind::SetAll : Kind::Cap;
    r.a = a;
  } else if (kind == "range") {
    const size_t dash = rest.find('-');
    if (dash == std::string::npos || !ToInt(rest.substr(0, dash), a) || !ToInt(rest.substr(dash + 1), b) || a < 1 || b > 100 || a > b) return false;
    r.kind = Kind::Range;
    r.a = a;
    r.b = b;
  } else {
    return false;
  }
  out = r;
  return true;
}

const std::vector<std::string>& KnownClauses() {
  static const std::vector<std::string> k = {kClauseSpecies, kClauseItem, kClauseSleep, kClauseOhko, kClauseEvasion};
  return k;
}

bool ClauseCheckedAtSubmit(const std::string& id) { return id == kClauseSpecies || id == kClauseItem || id == kClauseOhko || id == kClauseEvasion; }

namespace {
std::string IntList(const std::set<int>& s) {
  std::string o;
  for (int v : s) o += (o.empty() ? "" : ",") + std::to_string(v);
  return o;
}
bool ParseIntList(const std::string& v, std::set<int>& out) {
  out.clear();
  for (const std::string& x : SplitOn(v, ',')) {
    int n;
    if (!ToInt(x, n)) return false;
    out.insert(n);
  }
  return true;
}
}

std::string Format::ToText() const {
  std::string t = "id=" + id + ";rs=" + ruleSet + ";gens=" + GensText(SortedUnique(gens)) + ";level=" + level.Text();
  std::string cl;
  for (const std::string& c : clauses) cl += (cl.empty() ? "" : ",") + c;
  t += ";clauses=" + cl + ";bsp=" + IntList(bannedSpecies) + ";bit=" + IntList(bannedItems) + ";bmv=" + IntList(bannedMoves) + ";bab=" + IntList(bannedAbilities);
  std::string cat;
  for (const std::string& x : bannedCategories) cat += (cat.empty() ? "" : ",") + x;
  std::string det;
  for (const auto& kv : details) det += (det.empty() ? "" : ",") + kv.first + ":" + kv.second;
  t += ";cat=" + cat + ";style=" + style + ";det=" + det;
  t += ";team=" + std::to_string(teamMin) + "-" + std::to_string(teamMax) + ";preview=" + (teamPreview ? "1" : "0") + ";pick=" + std::to_string(pick) +
       ";tp=" + std::to_string(timerPreview) + ";tc=" + std::to_string(timerPerChoice) + ";tb=" + std::to_string(timerPerBattle);
  return t;
}

bool ParseFormat(std::string_view text, Format& out, std::string& error) {
  Format f;
  for (const std::string& item : SplitOn(text, ';')) {
    const size_t eq = item.find('=');
    if (eq == std::string::npos) { error = "'" + item + "' is not key=value"; return false; }
    const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
    int n;
    if (k == "id") f.id = v;
    else if (k == "rs") f.ruleSet = v;
    else if (k == "gens") { std::set<int> g; if (!ParseIntList(v, g)) { error = "bad gens"; return false; } f.gens.assign(g.begin(), g.end()); }
    else if (k == "level") { if (!LevelRule::Parse(v, f.level)) { error = "bad level rule '" + v + "'"; return false; } }
    else if (k == "clauses") {
      for (const std::string& c : SplitOn(v, ',')) {
        if (std::find(KnownClauses().begin(), KnownClauses().end(), c) == KnownClauses().end()) { error = "unknown clause '" + c + "'"; return false; }
        f.clauses.insert(c);
      }
    }
    else if (k == "bsp") { if (!ParseIntList(v, f.bannedSpecies)) { error = "bad species ban list"; return false; } }
    else if (k == "bit") { if (!ParseIntList(v, f.bannedItems)) { error = "bad item ban list"; return false; } }
    else if (k == "bmv") { if (!ParseIntList(v, f.bannedMoves)) { error = "bad move ban list"; return false; } }
    else if (k == "bab") { if (!ParseIntList(v, f.bannedAbilities)) { error = "bad ability ban list"; return false; } }
    else if (k == "team") {
      const size_t dash = v.find('-');
      if (dash == std::string::npos || !ToInt(v.substr(0, dash), f.teamMin) || !ToInt(v.substr(dash + 1), f.teamMax) || f.teamMin < 1 || f.teamMax > 6 || f.teamMin > f.teamMax) { error = "bad team size"; return false; }
    }
    else if (k == "det") { for (const std::string& x : SplitOn(v, ',')) { const size_t c2 = x.find(':'); if (c2 == std::string::npos) { error = "bad detail"; return false; } f.details[x.substr(0, c2)] = x.substr(c2 + 1); } }
    else if (k == "preview") f.teamPreview = v == "1";
    else if (k == "cat") { for (const std::string& x : SplitOn(v, ',')) { if (x != "legendary" && x != "mythical") { error = "unknown ban category '" + x + "'"; return false; } f.bannedCategories.insert(x); } }
    else if (k == "style") { if (v != "single" && v != "double") { error = "bad battle style"; return false; } f.style = v; }
    else if (k == "pick") { if (!ToInt(v, n) || n > 6) { error = "bad pick"; return false; } f.pick = n; }
    else if (k == "tp") { if (!ToInt(v, n) || n > 600) { error = "bad timer"; return false; } f.timerPreview = n; }
    else if (k == "tc") { if (!ToInt(v, n) || n > 600) { error = "bad timer"; return false; } f.timerPerChoice = n; }
    else if (k == "tb") { if (!ToInt(v, n) || n > 7200) { error = "bad timer"; return false; } f.timerPerBattle = n; }
    else { error = "unknown format key '" + k + "'"; return false; }
  }
  if (f.id.empty()) { error = "a format needs an id"; return false; }
  out = f;
  return true;
}

int LevelUnder(const LevelRule& rule, int ownLevel) { return rule.kind == LevelRule::Kind::SetAll ? rule.a : ownLevel; }

std::vector<std::string> CheckTeam(const Format& f, const std::vector<TeamMember>& team, const ClauseData& data, const std::vector<int>& roomGens,
                                   const std::function<int(int)>& speciesGen) {
  std::vector<std::string> out;
  const auto nm = [&](const char* kind, int id) {
    if (data.name) {
      const std::string s = data.name(kind, id);
      if (!s.empty()) return s;
    }
    return std::string(kind) + " " + std::to_string(id);
  };
  const int size = static_cast<int>(team.size());
  if (size < f.teamMin) out.push_back("The team has " + std::to_string(size) + " Pokemon; this format needs at least " + std::to_string(f.teamMin) + ".");
  if (size > f.teamMax) out.push_back("The team has " + std::to_string(size) + " Pokemon; this format allows at most " + std::to_string(f.teamMax) + ".");
  std::map<int, int> seenSpecies, seenItems;
  const bool speciesClause = f.clauses.count(kClauseSpecies) != 0, itemClause = f.clauses.count(kClauseItem) != 0;
  const std::vector<int>& gens = f.gens.empty() ? roomGens : f.gens;
  int slot = 0;
  for (const TeamMember& m : team) {
    slot++;
    const std::string who = "Pokemon " + std::to_string(slot) + " (" + nm("species", m.species) + ")";
    if (f.bannedSpecies.count(m.species)) out.push_back(who + " is banned in this format.");
    for (const std::string& cat : f.bannedCategories) {
      const auto it = data.categories.find(cat);
      if (it != data.categories.end() && it->second.count(m.species)) out.push_back(who + " is a " + cat + " Pokemon; those are banned in this format.");
    }
    if (!gens.empty() && speciesGen) {
      const int g = speciesGen(m.species);
      if (g != 0 && g > *std::max_element(gens.begin(), gens.end())) out.push_back(who + " is from generation " + std::to_string(g) + ", which this format does not allow.");
    }
    if (speciesClause) {
      if (seenSpecies[m.species]++ == 1) out.push_back("Species clause: more than one " + nm("species", m.species) + " in the team.");
    }
    if (m.item != 0) {
      if (f.bannedItems.count(m.item)) out.push_back(who + " holds " + nm("item", m.item) + ", which is banned in this format.");
      if (itemClause && seenItems[m.item]++ == 1) out.push_back("Item clause: more than one Pokemon holds " + nm("item", m.item) + ".");
    }
    if (m.ability != 0 && f.bannedAbilities.count(m.ability)) out.push_back(who + " has the ability " + nm("ability", m.ability) + ", which is banned in this format.");
    for (int mv : m.moves) {
      if (mv == 0) continue;
      if (f.bannedMoves.count(mv)) out.push_back(who + " knows " + nm("move", mv) + ", which is banned in this format.");
      if (f.clauses.count(kClauseOhko) && data.ohkoMoves.count(mv)) out.push_back("OHKO clause: " + who + " knows " + nm("move", mv) + ".");
      if (f.clauses.count(kClauseEvasion) && data.evasionMoves.count(mv)) out.push_back("Evasion clause: " + who + " knows " + nm("move", mv) + ".");
    }
    switch (f.level.kind) {
      case LevelRule::Kind::Cap:
        if (m.level > f.level.a) out.push_back(who + " is level " + std::to_string(m.level) + "; this format's cap is " + std::to_string(f.level.a) + ".");
        break;
      case LevelRule::Kind::Range:
        if (m.level < f.level.a || m.level > f.level.b)
          out.push_back(who + " is level " + std::to_string(m.level) + "; this format allows " + std::to_string(f.level.a) + " to " + std::to_string(f.level.b) + ".");
        break;
      default: break;
    }
  }
  return out;
}

std::vector<MemberProblem> TeamProblems(const Format& f, const std::vector<TeamMember>& team, const ClauseData& data, const std::vector<int>& roomGens,
                                        const std::function<int(int)>& speciesGen) {
  std::vector<MemberProblem> out;
  const auto nm = [&](const char* kind, int id) {
    if (data.name) {
      const std::string s = data.name(kind, id);
      if (!s.empty()) return s;
    }
    return std::string(kind) + " " + std::to_string(id);
  };
  const auto add = [&](int member, const char* kind, const std::string& arg = std::string()) { out.push_back({member, kind, arg}); };
  const int size = static_cast<int>(team.size());
  if (size < f.teamMin || size > f.teamMax) add(-1, "size", std::to_string(size));
  std::map<int, int> seenSpecies, seenItems;
  const bool speciesClause = f.clauses.count(kClauseSpecies) != 0, itemClause = f.clauses.count(kClauseItem) != 0;
  const std::vector<int>& gens = f.gens.empty() ? roomGens : f.gens;
  int index = -1;
  for (const TeamMember& m : team) {
    index++;
    if (f.bannedSpecies.count(m.species)) add(index, "banned_species");
    for (const std::string& cat : f.bannedCategories) {
      const auto it = data.categories.find(cat);
      if (it != data.categories.end() && it->second.count(m.species)) add(index, "category", cat);
    }
    if (!gens.empty() && speciesGen) {
      const int g = speciesGen(m.species);
      if (g != 0 && g > *std::max_element(gens.begin(), gens.end())) add(index, "gen", std::to_string(g));
    }
    if (speciesClause && seenSpecies[m.species]++ >= 1) add(index, "species");
    if (m.item != 0) {
      if (f.bannedItems.count(m.item)) add(index, "banned_item", nm("item", m.item));
      if (itemClause && seenItems[m.item]++ >= 1) add(index, "item", nm("item", m.item));
    }
    if (m.ability != 0 && f.bannedAbilities.count(m.ability)) add(index, "banned_ability", nm("ability", m.ability));
    for (int mv : m.moves) {
      if (mv == 0) continue;
      if (f.bannedMoves.count(mv)) add(index, "banned_move", nm("move", mv));
      if (f.clauses.count(kClauseOhko) && data.ohkoMoves.count(mv)) add(index, "ohko", nm("move", mv));
      if (f.clauses.count(kClauseEvasion) && data.evasionMoves.count(mv)) add(index, "evasion", nm("move", mv));
    }
    if ((f.level.kind == LevelRule::Kind::Cap && m.level > f.level.a) ||
        (f.level.kind == LevelRule::Kind::Range && (m.level < f.level.a || m.level > f.level.b)))
      add(index, "level", std::to_string(m.level));
  }
  return out;
}

}
