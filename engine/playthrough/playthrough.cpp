#include "playthrough.h"

#include "json.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#define gmtime_s(tmOut, timeIn) gmtime_r((timeIn), (tmOut))
#endif

namespace engine::playthrough {

namespace fs = std::filesystem;
using mods::json::Value;

namespace {

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

std::string Label(const Setup& setup) {
  std::string label = EngineName(setup.engine);
  const std::vector<const ModUse*> world = setup.WorldMods();
  if (!setup.randomizer.empty()) {
    label += kDot + std::string("Randomizer ") +
             (!setup.seed.empty() ? setup.seed
                                  : setup.randomizer + (setup.randomizerVersion.empty() ? "" : " " + setup.randomizerVersion));
  }
  for (size_t i = 0; i < setup.challenges.size(); i++) {
    std::string name = setup.challenges[i];
    std::replace(name.begin(), name.end(), '_', ' ');
    label += (i == 0 ? kDot + std::string("Challenge: ") : std::string(", ")) + name;
  }
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
  if (!setup.setupChanged.empty()) {
    label += kDot + std::string("setup changed");
  }
  if (setup.Failed()) {
    label += kDot + std::string("run failed");
  }
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
  out += "  \"started\": " + Quote(s.started) + ",\n";
  out += "  \"port_version\": " + Quote(s.portVersion) + ",\n";
  if (!s.chosenBy.empty()) out += "  \"chosen_by\": " + Quote(s.chosenBy) + ",\n";
  if (!s.engineChanged.empty()) {
    out += "  \"engine_started\": " + Quote(EngineId(s.startedEngine)) + ",\n";
    out += "  \"engine_changed\": [";
    for (size_t i = 0; i < s.engineChanged.size(); i++) out += (i > 0 ? ", " : "") + Quote(s.engineChanged[i]);
    out += "],\n";
  }
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
  if (s.battlesWonKept) {
    out += "  \"battles_won\": [";
    for (size_t i = 0; i < s.battlesWon.size(); i++) out += (i > 0 ? ", " : "") + std::to_string(s.battlesWon[i]);
    out += "],\n";
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
  const auto r = root.fields.find("randomizer");
  if (r != root.fields.end() && r->second.kind == Value::Kind::Object) {
    out.randomizer = Text(r->second, "name");
    out.randomizerVersion = Text(r->second, "version");
    out.seed = Text(r->second, "seed");
  }
  out.challenges = Texts(root, "challenges");
  out.started = Text(root, "started");
  out.portVersion = Text(root, "port_version");
  out.chosenBy = Text(root, "chosen_by");
  out.engineChanged = Texts(root, "engine_changed");
  if (!ParseEngine(Text(root, "engine_started"), out.startedEngine)) {
    out.startedEngine = out.engineChanged.size() % 2 == 0 ? out.engine
                                                           : out.engine == Engine::Enhanced ? Engine::Faithful : Engine::Enhanced;
  }
  out.setupChanged = Texts(root, "setup_changed");
  out.challengeLog = Texts(root, "challenge_log");
  out.challengeState = Texts(root, "challenge_state");
  const auto won = root.fields.find("battles_won");
  if (won != root.fields.end() && won->second.kind == Value::Kind::Array) {
    out.battlesWonKept = true;
    for (const Value& v : won->second.items) {
      if (v.kind == Value::Kind::Number && v.number >= 1 && v.number <= 65535) out.battlesWon.push_back(static_cast<uint32_t>(v.number));
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
        check.worldProblems.push_back(was + " is not in the mods folder any more");
      } else if (mods::CompareVersions(there->manifest.version, m.version) != 0) {
        check.worldProblems.push_back(was + " is now version " +
                                      (there->manifest.version.empty() ? std::string("(none)") : there->manifest.version));
      }
    } else if (there == nullptr) {
      check.notes.push_back(was + " (" + mods::AppliesName(m.applies) + ") is not in the mods folder any more");
    }
  }
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
    if (IsEngineSetting(d) && d.implemented) names.push_back(d.name);
  }
  return names;
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
