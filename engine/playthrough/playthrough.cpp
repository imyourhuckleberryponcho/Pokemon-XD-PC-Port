#include "playthrough.h"

#include "json.h"
#include "i18n.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#define gmtime_s(tmOut, timeIn) gmtime_r((timeIn), (tmOut))
#endif

namespace engine::playthrough {

namespace fs = std::filesystem;
using mods::json::Value;

namespace {

using engine::i18n::Tr;
std::string ShadowAmountName(const std::string& text) {
  const size_t c = text.find(':');
  const std::string name = text.substr(0, c);
  if ((name == "own" || name == "pct") && c != std::string::npos) {
    const size_t c2 = text.find(':', c + 1);
    const std::string pct = text.substr(c + 1, c2 == std::string::npos ? std::string::npos : c2 - c - 1);
    if (!pct.empty() && pct.find_first_not_of("0123456789") == std::string::npos) return pct + "%";
  }
  return name;
}

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof buf, format, args);
  va_end(args);
  return buf;
}
const char* TrEngineName(Engine engine) { return engine == Engine::Enhanced ? Tr("Enhanced") : Tr("Faithful"); }

const char* const kDot = " \xC2\xB7 ";

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
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

std::string Text(const Value& obj, const char* key) {
  const auto it = obj.fields.find(key);
  return it != obj.fields.end() && it->second.kind == Value::Kind::String ? it->second.text : std::string();
}

uint32_t Number(const Value& obj, const char* key) {
  const auto it = obj.fields.find(key);
  return it != obj.fields.end() && it->second.kind == Value::Kind::Number && it->second.number >= 0 &&
                 it->second.number <= 4294967295.0
             ? static_cast<uint32_t>(it->second.number)
             : 0;
}

std::vector<std::string> Texts(const Value& obj, const char* key) {
  std::vector<std::string> out;
  const auto it = obj.fields.find(key);
  if (it != obj.fields.end() && it->second.kind == Value::Kind::Array) {
    for (const Value& v : it->second.items) {
      if (v.kind == Value::Kind::String) out.push_back(v.text);
    }
  }
  return out;
}

}

const char* EngineId(Engine engine) { return engine == Engine::Enhanced ? "enhanced" : "faithful"; }
const char* EngineName(Engine engine) { return engine == Engine::Enhanced ? "Enhanced" : "Faithful"; }
bool ParseEngine(std::string_view text, Engine& out) {
  if (text == "faithful") {
    out = Engine::Faithful;
    return true;
  }
  if (text == "enhanced") {
    out = Engine::Enhanced;
    return true;
  }
  return false;
}

std::vector<const ModUse*> Setup::WorldMods() const {
  std::vector<const ModUse*> out;
  for (const ModUse& m : mods) {
    if (m.applies == mods::Applies::NewPlaythrough) out.push_back(&m);
  }
  return out;
}

static std::vector<std::string> ShownChallenges(const Setup& setup) {
  bool later = false;
  for (const std::string& c : setup.challenges) later = later || c == "gen5_rules" || c == "gen6_rules" || c == "gen7_rules";
  std::vector<std::string> out;
  for (const std::string& c : setup.challenges) {
    if (later && c == "gen4_rules") continue;
    out.push_back(c);
  }
  return out;
}

std::string Label(const Setup& setup) {
  std::string label = EngineName(setup.engine);
  const std::vector<const ModUse*> world = setup.WorldMods();
  if (!setup.randomizer.empty()) {
    label += kDot + std::string("Randomizer ") +
             (!setup.seed.empty() ? setup.seed
                                  : setup.randomizer + (setup.randomizerVersion.empty() ? "" : " " + setup.randomizerVersion));
  }
  const std::vector<std::string> shown = ShownChallenges(setup);
  for (size_t i = 0; i < shown.size(); i++) {
    std::string name = shown[i];
    std::replace(name.begin(), name.end(), '_', ' ');
    label += (i == 0 ? kDot + std::string("Challenge: ") : std::string(", ")) + name;
  }
  if (!setup.moreShadows.empty()) label += kDot + std::string("More Shadow Pokemon: ") + ShadowAmountName(setup.moreShadows);
  std::vector<std::string> starters;
  std::vector<std::string> others;
  for (const std::string& o : setup.options) {
    const bool first = o.rfind("starter=", 0) == 0, second = o.rfind("starter2=", 0) == 0;
    if (first || second) {
      const size_t colon = o.find(':');
      if (colon != std::string::npos && colon + 1 < o.size()) {
        starters.push_back(o.substr(colon + 1));
        continue;
      }
    }
    others.push_back(o);
  }
  if (!starters.empty()) {
    label += kDot + std::string(starters.size() > 1 ? "Starters: " : "Starter: ");
    for (size_t i = 0; i < starters.size(); i++) label += (i > 0 ? " + " : "") + starters[i];
    others.erase(std::remove_if(others.begin(), others.end(), [](const std::string& o) { return o == "starter_choose=on" || o == "starter_two=on"; }), others.end());
  }
  for (size_t i = 0; i < others.size(); i++) {
    label += (i == 0 ? kDot + std::string("Options: ") : std::string(", ")) + others[i];
  }
  if (!setup.ruleSetChanged.empty() && !setup.ruleSetStarted.empty()) label += kDot + std::string("rules were ") + setup.ruleSetStarted;
  if (!world.empty()) {
    label += kDot + std::string("Mods: ");
    for (size_t i = 0; i < world.size(); i++) {
      label += (i > 0 ? ", " : "") + world[i]->id + (world[i]->version.empty() ? "" : " " + world[i]->version);
    }
  }
  if (setup.Vanilla()) {
    label += kDot + std::string("Vanilla");
  }
  if (!setup.engineChanged.empty()) {
    label += kDot + std::string("was ") + EngineName(setup.engine == Engine::Enhanced ? Engine::Faithful : Engine::Enhanced);
  }
  if (!setup.cheatsUsed.empty()) {
    label += kDot + std::string("cheats used");
  }
  if (!setup.setupChanged.empty()) {
    label += kDot + std::string("setup changed");
  }
  if (setup.Failed()) {
    label += kDot + std::string("run failed");
  }
  return label;
}

std::string DisplayLabel(const Setup& setup) {
  std::string label = TrEngineName(setup.engine);
  const std::vector<const ModUse*> world = setup.WorldMods();
  if (!setup.randomizer.empty()) {
    label += kDot + Fmt(Tr("Randomizer %s"),
                        (!setup.seed.empty() ? setup.seed
                                             : setup.randomizer + (setup.randomizerVersion.empty() ? "" : " " + setup.randomizerVersion))
                            .c_str());
  }
  const std::vector<std::string> shown = ShownChallenges(setup);
  if (!shown.empty()) {
    std::string names;
    for (size_t i = 0; i < shown.size(); i++) {
      std::string name = shown[i];
      std::replace(name.begin(), name.end(), '_', ' ');
      names += (i == 0 ? "" : ", ") + name;
    }
    label += kDot + Fmt(Tr("Challenge: %s"), names.c_str());
  }
  if (!world.empty()) {
    std::string names;
    for (size_t i = 0; i < world.size(); i++) {
      names += (i > 0 ? ", " : "") + world[i]->id + (world[i]->version.empty() ? "" : " " + world[i]->version);
    }
    label += kDot + Fmt(Tr("Mods: %s"), names.c_str());
  }
  if (!setup.moreShadows.empty()) label += kDot + Fmt(Tr("More Shadow Pokemon: %s"), ShadowAmountName(setup.moreShadows).c_str());
  if (!setup.ruleSetChanged.empty() && !setup.ruleSetStarted.empty()) label += kDot + std::string("rules were ") + setup.ruleSetStarted;
  if (setup.Vanilla()) label += kDot + std::string(Tr("Vanilla"));
  if (!setup.engineChanged.empty()) {
    label += kDot + Fmt(Tr("was %s"), TrEngineName(setup.engine == Engine::Enhanced ? Engine::Faithful : Engine::Enhanced));
  }
  if (!setup.cheatsUsed.empty()) label += kDot + std::string(Tr("cheats used"));
  if (!setup.setupChanged.empty()) label += kDot + std::string(Tr("setup changed"));
  if (setup.Failed()) label += kDot + std::string(Tr("run failed"));
  return label;
}

bool Setup::Failed() const {
  for (const std::string& e : challengeLog) {
    if (e.find(": failed") != std::string::npos || e.rfind("failed", 0) == 0) return true;
  }
  return false;
}

std::string UnknownLabel() { return std::string("Faithful") + kDot + "Vanilla (unknown history: no setup file)"; }

std::string ToJson(const Setup& s) {
  std::string out = "{\n  \"format\": 1,\n";
  out += "  \"engine\": " + Quote(EngineId(s.engine)) + ",\n";
  out += "  \"mods\": [";
  for (size_t i = 0; i < s.mods.size(); i++) {
    const ModUse& m = s.mods[i];
    out += std::string(i > 0 ? "," : "") + "\n    {\"id\": " + Quote(m.id) + ", \"name\": " + Quote(m.name) +
           ", \"version\": " + Quote(m.version) + ", \"applies\": " + Quote(mods::AppliesId(m.applies)) + "}";
  }
  out += s.mods.empty() ? "],\n" : "\n  ],\n";
  if (!s.addedRows.empty()) {
    out += "  \"added_rows\": [";
    for (size_t i = 0; i < s.addedRows.size(); i++) {
      const AddedRowRef& r = s.addedRows[i];
      out += std::string(i > 0 ? "," : "") + "\n    {\"mod\": " + Quote(r.mod) + ", \"table\": " + Quote(r.table) + ", \"name\": " +
             Quote(r.name) + ", \"id\": " + std::to_string(r.id) + (r.label.empty() ? "" : ", \"label\": " + Quote(r.label)) + "}";
    }
    out += "\n  ],\n";
  }
  if (s.randomizer.empty()) {
    out += "  \"randomizer\": null,\n";
  } else {
    out += "  \"randomizer\": {\"name\": " + Quote(s.randomizer) + ", \"version\": " + Quote(s.randomizerVersion) +
           ", \"seed\": " + Quote(s.seed) + "},\n";
  }
  out += "  \"challenges\": [";
  for (size_t i = 0; i < s.challenges.size(); i++) {
    out += (i > 0 ? ", " : "") + Quote(s.challenges[i]);
  }
  out += "],\n";
  if (!s.options.empty()) {
    out += "  \"options\": [";
    for (size_t i = 0; i < s.options.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.options[i]);
    out += "],\n";
  }
  if (s.rules.present) {
    out += rulesets::ToJsonKeys(s.rules, 2) + ",\n";
    if (!s.ruleSetChanged.empty()) {
      out += "  \"rule_set_started\": " + Quote(s.ruleSetStarted) + ",\n  \"rule_set_changed\": [";
      for (size_t i = 0; i < s.ruleSetChanged.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.ruleSetChanged[i]);
      out += "],\n";
    }
  }
  if (!s.split.empty()) out += "  \"physical_special_split\": " + Quote(s.split) + ",\n";
  if (!s.moreShadows.empty()) out += "  \"more_shadows\": " + Quote(s.moreShadows) + ",\n";
  out += "  \"started\": " + Quote(s.started) + ",\n";
  out += "  \"port_version\": " + Quote(s.portVersion) + ",\n";
  if (!s.chosenBy.empty()) out += "  \"chosen_by\": " + Quote(s.chosenBy) + ",\n";
  if (!s.engineChanged.empty()) {
    out += "  \"engine_started\": " + Quote(EngineId(s.startedEngine)) + ",\n";
    out += "  \"engine_changed\": [";
    for (size_t i = 0; i < s.engineChanged.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.engineChanged[i]);
    out += "],\n";
  }
  if (!s.cheatsUsed.empty()) {
    out += "  \"cheats_used\": [";
    for (size_t i = 0; i < s.cheatsUsed.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.cheatsUsed[i]);
    out += "],\n";
  }
  if (!s.cheatState.empty()) {
    out += "  \"cheats\": [";
    for (size_t i = 0; i < s.cheatState.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.cheatState[i]);
    out += "],\n";
  }
  if (s.cheatWarned) out += "  \"cheat_warned\": true,\n";
  if (!s.challengeLog.empty()) {
    out += "  \"challenge_log\": [";
    for (size_t i = 0; i < s.challengeLog.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.challengeLog[i]);
    out += "],\n";
  }
  if (!s.challengeState.empty()) {
    out += "  \"challenge_state\": [";
    for (size_t i = 0; i < s.challengeState.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.challengeState[i]);
    out += "],\n";
  }
  if (!s.modData.empty()) {
    out += "  \"mod_data\": {";
    for (size_t i = 0; i < s.modData.size(); i++) {
      const mods::ModValueSet& m = s.modData[i];
      out += std::string(i > 0 ? "," : "") + "\n    " + Quote(m.mod) + ": {\"version\": " + Quote(m.version) + ", \"values\": {";
      size_t n = 0;
      for (const auto& [name, v] : m.values) out += std::string(n++ > 0 ? ", " : "") + Quote(name) + ": " + std::to_string(v);
      out += "}}";
    }
    out += "\n  },\n";
  }
  if (!s.extRequired.empty()) {
    out += "  \"ext_required\": [";
    for (size_t i = 0; i < s.extRequired.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.extRequired[i]);
    out += "],\n";
  }
  if (s.battlesWonKept) {
    out += "  \"battles_won\": [";
    for (size_t i = 0; i < s.battlesWon.size(); i++) out += (i > 0 ? ", " : "") + std::to_string(s.battlesWon[i]);
    out += "],\n";
  }
  if (s.dexKept) {
    out += "  \"pokedex\": {\"caught\": " + Quote(s.dexCaught) + ", \"shiny\": " + Quote(s.dexShiny) + "},\n";
  }
  if (s.stats.kept) {
    out += "  \"stats\": {\"battles_won\": " + std::to_string(s.stats.battlesWon) + ", \"battles_lost\": " +
           std::to_string(s.stats.battlesLost) + ", \"battles_other\": " + std::to_string(s.stats.battlesOther) + "},\n";
  }
  if (s.speedrun.present) {
    const Speedrun& r = s.speedrun;
    out += "  \"speedrun\": {\"on\": " + std::string(r.on ? "true" : "false") + ", \"without_loads\": " +
           (r.withoutLoads ? "true" : "false") + ", \"start\": " + std::to_string(r.start) + ", \"end\": " +
           std::to_string(r.end) + ", \"category\": " + std::to_string(r.category) + ", \"rta_ms\": " + std::to_string(r.rtaMs) + ", \"load_ms\": " +
           std::to_string(r.loadMs);
    if (r.finished) {
      out += ", \"final_rta_ms\": " + std::to_string(r.finalRtaMs) + ", \"final_load_ms\": " + std::to_string(r.finalLoadMs);
    }
    out += ", \"breaks\": [";
    for (size_t i = 0; i < r.breaks.size(); i++) {
      out += (i > 0 ? ", " : "") + std::string("{\"at_ms\": ") + std::to_string(r.breaks[i].atMs) + ", \"length_ms\": " +
             std::to_string(r.breaks[i].lengthMs) + ", \"epoch\": " + std::to_string(r.breaks[i].epoch) + "}";
    }
    out += "], \"break_pending\": " + std::string(r.breakPending ? "true" : "false") + ", \"break_epoch\": " +
           std::to_string(r.breakEpoch) + ", \"break_at_ms\": " + std::to_string(r.breakAtMs) + ", \"invalid\": [";
    for (size_t i = 0; i < r.invalid.size(); i++) out += (i > 0 ? ", " : "") + Quote(r.invalid[i]);
    out += "], \"splits\": [";
    for (size_t i = 0; i < r.splits.size(); i++) {
      out += (i > 0 ? ", " : "") + std::string("{\"id\": ") + Quote(r.splits[i].id) + ", \"rta_ms\": " +
             std::to_string(r.splits[i].rtaMs) + ", \"load_ms\": " + std::to_string(r.splits[i].loadMs) + "}";
    }
    out += "]},\n";
  }
  out += "  \"setup_changed\": [";
  for (size_t i = 0; i < s.setupChanged.size(); i++) {
    out += (i > 0 ? ", " : "") + Quote(s.setupChanged[i]);
  }
  out += "],\n";
  out += "  \"save\": {\"trainer\": " + Quote(s.trainer) + ", \"trainer_id\": " + std::to_string(s.trainerId) +
         ", \"secret_id\": " + std::to_string(s.secretId) + "}\n}\n";
  return out;
}

bool FromJson(std::string_view text, Setup& out, std::string& error) {
  out = Setup{};
  Value root;
  mods::json::Reader reader(text);
  if (!reader.Document(root)) {
    error = "not valid JSON (" + reader.Error() + ")";
    return false;
  }
  if (root.kind != Value::Kind::Object) {
    error = "must be one { ... } block";
    return false;
  }
  if (Number(root, "format") > 1) {
    error = "written by a newer version of the port";
    return false;
  }
  if (!ParseEngine(Text(root, "engine"), out.engine)) {
    error = "\"engine\" must be \"faithful\" or \"enhanced\"";
    return false;
  }
  const auto modsIt = root.fields.find("mods");
  if (modsIt != root.fields.end() && modsIt->second.kind == Value::Kind::Array) {
    for (const Value& v : modsIt->second.items) {
      if (v.kind != Value::Kind::Object) continue;
      ModUse m;
      m.id = Text(v, "id");
      m.name = Text(v, "name");
      m.version = Text(v, "version");
      if (!mods::ParseApplies(Text(v, "applies"), m.applies)) m.applies = mods::Applies::NewPlaythrough;
      if (!m.id.empty()) out.mods.push_back(std::move(m));
    }
  }
  const auto rowsIt = root.fields.find("added_rows");
  if (rowsIt != root.fields.end() && rowsIt->second.kind == Value::Kind::Array) {
    for (const Value& v : rowsIt->second.items) {
      if (v.kind != Value::Kind::Object) continue;
      AddedRowRef a;
      a.mod = Text(v, "mod");
      a.table = Text(v, "table");
      a.name = Text(v, "name");
      a.id = Number(v, "id");
      a.label = Text(v, "label");
      if (!a.mod.empty() && !a.table.empty()) out.addedRows.push_back(std::move(a));
    }
  }
  const auto r = root.fields.find("randomizer");
  if (r != root.fields.end() && r->second.kind == Value::Kind::Object) {
    out.randomizer = Text(r->second, "name");
    out.randomizerVersion = Text(r->second, "version");
    out.seed = Text(r->second, "seed");
  }
  out.challenges = Texts(root, "challenges");
  out.options = Texts(root, "options");
  if (!rulesets::FromJson(root, out.rules, error)) return false;
  out.ruleSetStarted = Text(root, "rule_set_started");
  out.ruleSetChanged = Texts(root, "rule_set_changed");
  out.split = Text(root, "physical_special_split");
  if (out.split != "on" && out.split != "off") out.split.clear();
  out.moreShadows = Text(root, "more_shadows");
  out.edition = Text(root, "edition");
  out.started = Text(root, "started");
  out.portVersion = Text(root, "port_version");
  out.chosenBy = Text(root, "chosen_by");
  out.engineChanged = Texts(root, "engine_changed");
  if (!ParseEngine(Text(root, "engine_started"), out.startedEngine)) {
    out.startedEngine = out.engineChanged.size() % 2 == 0 ? out.engine
                                                           : out.engine == Engine::Enhanced ? Engine::Faithful : Engine::Enhanced;
  }
  out.cheatsUsed = Texts(root, "cheats_used");
  out.cheatState = Texts(root, "cheats");
  {
    const auto w = root.fields.find("cheat_warned");
    out.cheatWarned = w != root.fields.end() && w->second.kind == Value::Kind::Bool && w->second.boolean;
  }
  out.setupChanged = Texts(root, "setup_changed");
  out.challengeLog = Texts(root, "challenge_log");
  out.challengeState = Texts(root, "challenge_state");
  const auto md = root.fields.find("mod_data");
  if (md != root.fields.end() && md->second.kind == Value::Kind::Object) {
    for (const auto& [id, v] : md->second.fields) {
      if (v.kind != Value::Kind::Object) continue;
      mods::ModValueSet set;
      set.mod = id;
      set.version = Text(v, "version");
      const auto vals = v.fields.find("values");
      if (vals != v.fields.end() && vals->second.kind == Value::Kind::Object) {
        for (const auto& [name, n] : vals->second.fields) {
          if (n.kind == Value::Kind::Number && n.number == static_cast<double>(static_cast<long long>(n.number))) set.values[name] = static_cast<int64_t>(n.number);
        }
      }
      out.modData.push_back(std::move(set));
    }
  }
  out.extRequired = Texts(root, "ext_required");
  const auto won = root.fields.find("battles_won");
  if (won != root.fields.end() && won->second.kind == Value::Kind::Array) {
    out.battlesWonKept = true;
    for (const Value& v : won->second.items) {
      if (v.kind == Value::Kind::Number && v.number >= 1 && v.number <= 65535) out.battlesWon.push_back(static_cast<uint32_t>(v.number));
    }
  }
  const auto dex = root.fields.find("pokedex");
  if (dex != root.fields.end() && dex->second.kind == Value::Kind::Object) {
    out.dexKept = true;
    out.dexCaught = Text(dex->second, "caught");
    out.dexShiny = Text(dex->second, "shiny");
  }
  const auto st = root.fields.find("stats");
  if (st != root.fields.end() && st->second.kind == Value::Kind::Object) {
    out.stats.kept = true;
    out.stats.battlesWon = Number(st->second, "battles_won");
    out.stats.battlesLost = Number(st->second, "battles_lost");
    out.stats.battlesOther = Number(st->second, "battles_other");
  }
  const auto sr = root.fields.find("speedrun");
  if (sr != root.fields.end() && sr->second.kind == Value::Kind::Object) {
    const auto num = [](const Value& o, const char* key, uint32_t& into, bool* has = nullptr) {
      const auto f = o.fields.find(key);
      if (f != o.fields.end() && f->second.kind == Value::Kind::Number && f->second.number >= 0 && f->second.number < 4294967295.0) {
        into = static_cast<uint32_t>(f->second.number);
        if (has != nullptr) *has = true;
      }
    };
    const auto flag = [](const Value& o, const char* key) {
      const auto f = o.fields.find(key);
      return f != o.fields.end() && f->second.kind == Value::Kind::Bool && f->second.boolean;
    };
    Speedrun& r = out.speedrun;
    r.present = true;
    r.on = flag(sr->second, "on");
    r.withoutLoads = flag(sr->second, "without_loads");
    uint32_t pt = 0;
    num(sr->second, "start", pt);
    r.start = pt > 2 ? 2 : static_cast<int>(pt);
    pt = 0;
    num(sr->second, "category", pt);
    r.category = pt > 3 ? 0 : static_cast<int>(pt);
    pt = 0;
    num(sr->second, "end", pt);
    r.end = pt > 3 ? 2 : static_cast<int>(pt);
    r.invalid = Texts(sr->second, "invalid");
    r.breakPending = flag(sr->second, "break_pending");
    num(sr->second, "break_epoch", r.breakEpoch);
    num(sr->second, "break_at_ms", r.breakAtMs);
    num(sr->second, "rta_ms", r.rtaMs);
    num(sr->second, "load_ms", r.loadMs);
    num(sr->second, "final_rta_ms", r.finalRtaMs, &r.finished);
    num(sr->second, "final_load_ms", r.finalLoadMs);
    const auto br = sr->second.fields.find("breaks");
    if (br != sr->second.fields.end() && br->second.kind == Value::Kind::Array) {
      for (const Value& v : br->second.items) {
        if (v.kind != Value::Kind::Object) continue;
        SpeedBreak b;
        num(v, "at_ms", b.atMs);
        num(v, "length_ms", b.lengthMs);
        num(v, "epoch", b.epoch);
        r.breaks.push_back(b);
      }
    }
    const auto sp = sr->second.fields.find("splits");
    if (sp != sr->second.fields.end() && sp->second.kind == Value::Kind::Array) {
      for (const Value& v : sp->second.items) {
        if (v.kind != Value::Kind::Object) continue;
        SpeedSplit s;
        s.id = Text(v, "id");
        num(v, "rta_ms", s.rtaMs);
        num(v, "load_ms", s.loadMs);
        if (!s.id.empty()) r.splits.push_back(std::move(s));
      }
    }
  }
  const auto save = root.fields.find("save");
  if (save != root.fields.end() && save->second.kind == Value::Kind::Object) {
    out.trainer = Text(save->second, "trainer");
    out.trainerId = Number(save->second, "trainer_id");
    out.secretId = Number(save->second, "secret_id");
  }
  return true;
}

bool ReadFile(const fs::path& file, Setup& out, std::string& error) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    error = "can't be read";
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return FromJson(text, out, error);
}

bool WriteFile(const fs::path& file, const Setup& setup) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path temp = file;
  temp += L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const std::string text = ToJson(setup);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      return false;
    }
  }
  fs::rename(temp, file, ec);
  if (ec) {
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

fs::path FileFor(const fs::path& folder, std::string_view key) {
  std::string name;
  for (char c : key) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    name += ok ? c : '_';
  }
  return folder / (name + ".json");
}

Check Compare(const Setup& setup, const std::vector<mods::ModFolder>& found) {
  Check check;
  for (const ModUse& m : setup.mods) {
    const mods::ModFolder* there = nullptr;
    for (const mods::ModFolder& f : found) {
      if (Lower(f.manifest.id) == Lower(m.id)) there = &f;
    }
    const std::string name = m.name.empty() ? m.id : m.name;
    const std::string was = name + (m.version.empty() ? "" : " " + m.version);
    if (m.applies == mods::Applies::NewPlaythrough) {
      if (there == nullptr) {
        check.worldProblems.push_back(Fmt(Tr("%s is not in the mods folder any more"), was.c_str()));
      } else if (mods::CompareVersions(there->manifest.version, m.version) != 0) {
        check.worldProblems.push_back(Fmt(Tr("%s is now version %s"), was.c_str(),
                                          (there->manifest.version.empty() ? std::string(Tr("(none)")) : there->manifest.version).c_str()));
      }
    } else if (there == nullptr) {
      check.notes.push_back(Fmt(Tr("%s (%s) is not in the mods folder any more"), was.c_str(), mods::AppliesName(m.applies)));
    }
  }
  for (const mods::ModValueSet& d : setup.modData) {
    const mods::ModFolder* there = nullptr;
    for (const mods::ModFolder& f : found) {
      if (Lower(f.manifest.id) == Lower(d.mod)) there = &f;
    }
    if (there == nullptr) {
      check.notes.push_back(Fmt(Tr("The numbers %s keeps with this save are not used: the mod is not on"), d.mod.c_str()));
    } else if (!d.version.empty() && mods::CompareVersions(there->manifest.version, d.version) != 0) {
      check.notes.push_back(Fmt(Tr("%s keeps numbers with this save made by version %s; it is now version %s"), d.mod.c_str(), d.version.c_str(),
                                (there->manifest.version.empty() ? std::string(Tr("(none)")) : there->manifest.version).c_str()));
    }
  }
  return check;
}

namespace {
std::string RowKind(const std::string& table) {
  return table == "pokemon_data" ? Tr("Pokemon") : table == "waza_data" ? Tr("move") : table == "item_data_prime" ? Tr("item") : table == "flag_data" ? Tr("story flag") : Tr("entry");
}
bool SaveHoldsNumbersOf(const std::string& table) {
  return table == "pokemon_data" || table == "waza_data" || table == "item_data_prime" || table == "flag_data";
}
std::string DescribeUnmet(const AddedRowRef& want, const std::vector<AddedRowRef>& now) {
  const AddedRowRef* moved = nullptr;
  bool modThere = false;
  for (const AddedRowRef& n : now) {
    if (Lower(n.mod) == Lower(want.mod)) modThere = true;
    if (Lower(n.mod) == Lower(want.mod) && n.table == want.table && n.name == want.name) moved = &n;
  }
  if (moved != nullptr) {
    return Fmt(Tr("the mod \"%s\" now gives the %s \"%s\" the number %d, the save has it as %d (another mod before it, or another order of the mods)"),
               want.mod.c_str(), RowKind(want.table).c_str(), want.name.c_str(), static_cast<int>(moved->id), static_cast<int>(want.id));
  }
  if (modThere) {
    return Fmt(Tr("the mod \"%s\" no longer adds the %s \"%s\" (it was changed)"), want.mod.c_str(), RowKind(want.table).c_str(), want.name.c_str());
  }
  return Fmt(Tr("the mod \"%s\" is not on: this save has its %s \"%s\" (number %d)"), want.mod.c_str(), RowKind(want.table).c_str(),
             want.name.c_str(), static_cast<int>(want.id));
}
mods::LabelRow ToLabelRow(const AddedRowRef& r, const std::vector<AddedRowRef>& now) {
  mods::LabelRow row;
  row.label = r.label;
  row.number = r.id;
  row.legacy = r.mod + "|" + r.table + "|" + r.name;
  row.describe = DescribeUnmet(r, now);
  return row;
}
}

std::vector<std::string> BoundProblems(const Setup& setup, const std::vector<AddedRowRef>& now) {
  std::vector<std::string> problems;
  std::set<std::string> said;
  for (const AddedRowRef& want : setup.addedRows) {
    if (!SaveHoldsNumbersOf(want.table)) continue;
    if (std::find(now.begin(), now.end(), want) != now.end()) continue;
    const std::string text = DescribeUnmet(want, now);
    if (said.insert(text).second) problems.push_back(text);
  }
  return problems;
}

BoundCheck CheckBound(const Setup& setup, const std::vector<AddedRowRef>& now) {
  std::vector<mods::LabelRow> saved, current;
  for (const AddedRowRef& r : setup.addedRows) {
    if (SaveHoldsNumbersOf(r.table)) saved.push_back(ToLabelRow(r, now));
  }
  for (const AddedRowRef& r : now) {
    if (!SaveHoldsNumbersOf(r.table)) continue;
    mods::LabelRow row;
    row.label = r.label;
    row.number = r.id;
    row.legacy = r.mod + "|" + r.table + "|" + r.name;
    current.push_back(std::move(row));
  }
  BoundCheck check;
  check.plan = mods::PlanRemap(saved, current);
  return check;
}

std::vector<std::string> WorldModIds(const Setup& setup) {
  std::vector<std::string> out;
  for (const ModUse* m : setup.WorldMods()) out.push_back(m->id);
  return out;
}

bool IsEngineSetting(const settings::Definition& def) {
  if (def.preference || def.testing) return false;
  if (def.category == settings::Category::Gameplay) return true;
  return def.id == "system.disc_timing" || def.id == "system.card_timing";
}

namespace {
std::vector<size_t> sOurOverrides;
}

std::string ApplyEngine(settings::Registry& reg, Engine engine) {
  std::string changed;
  const auto note = [&](size_t i) { changed += (changed.empty() ? "" : ";") + reg.Get(i).id + "=" + reg.ValueText(i); };
  if (engine == Engine::Enhanced) {
    for (size_t i : sOurOverrides) {
      const std::string before = reg.ValueText(i);
      reg.ClearOverride(i);
      if (reg.ValueText(i) != before) note(i);
    }
    sOurOverrides.clear();
    return changed;
  }
  for (size_t i = 0; i < reg.Count(); i++) {
    const settings::Definition& d = reg.Get(i);
    if (!IsEngineSetting(d) || reg.IsOverridden(i)) continue;
    const std::string original = reg.TextOf(i, reg.DefaultOf(i, settings::Preset::Original));
    if (reg.ValueText(i) == original) continue;
    if (reg.Override(d.id, original)) {
      sOurOverrides.push_back(i);
      note(i);
    }
  }
  return changed;
}

Engine DefaultEngine(const settings::Registry& reg) {
  for (size_t i = 0; i < reg.Count(); i++) {
    const settings::Definition& d = reg.Get(i);
    if (IsEngineSetting(d) && reg.TextOf(i, reg.SavedValueOf(i)) != reg.TextOf(i, reg.DefaultOf(i, settings::Preset::Original))) {
      return Engine::Enhanced;
    }
  }
  return Engine::Faithful;
}

bool EngineQuestionDue(const Setup& setup, Engine playersEngine) { return setup.engine != playersEngine; }

void SwitchEngine(Setup& setup, Engine to, const std::string& when) {
  if (setup.engine == to) return;
  if (setup.engineChanged.empty()) setup.startedEngine = setup.engine;
  setup.engineChanged.push_back(when + ": switched from " + EngineName(setup.engine) + " to " + EngineName(to) + " at Continue");
  setup.engine = to;
}

std::vector<std::string> EngineSettingNames(const settings::Registry& reg) {
  std::vector<std::string> names;
  for (size_t i = 0; i < reg.Count(); i++) {
    const settings::Definition& d = reg.Get(i);
    if (IsEngineSetting(d) && d.implemented) names.push_back(Tr(d.name));
  }
  return names;
}

std::string TablesStamp(const Setup& s) {
  std::string text;
  for (const ModUse& m : s.mods) {
    if (m.applies == mods::Applies::NewPlaythrough) text += "mod:" + m.id + "@" + m.version + ";";
  }
  for (const AddedRowRef& r : s.addedRows) text += "row:" + r.mod + "/" + r.table + "/" + r.name + "/" + r.label + "=" + std::to_string(r.id) + ";";
  text += "rando:" + s.randomizer + "/" + s.randomizerVersion + "/" + s.seed + ";";
  text += "shadows:" + s.moreShadows + ";edition:" + s.edition + ";";
  for (const std::string& c : s.challenges) text += "ch:" + c + ";";
  for (const std::string& o : s.options) text += "opt:" + o + ";";
  if (s.rules.present) {
    text += "rules:" + s.rules.ruleSet + ";gens:";
    for (int g : s.rules.gens) text += std::to_string(g) + ",";
    for (const auto& [k, v] : s.rules.mechanics) text += k + "=" + v + ",";
    for (const std::string& o : s.rules.pokemonOptions) text += "po:" + o + ",";
    text += ";";
  }
  uint64_t h = 0xCBF29CE484222325ull;
  for (unsigned char c : text) h = (h ^ c) * 0x100000001B3ull;
  char out[24];
  std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
  return out;
}

std::string Now() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char text[32];
  std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", &local);
  return text;
}

}
