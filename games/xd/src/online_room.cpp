#include "online_room.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace xd::online {

int ActionNumbers(const std::string& text) {
  std::istringstream in(text);
  std::string word;
  if (!(in >> word) || word.size() > 3 || word.find_first_not_of("0123456789") != std::string::npos) return 0;
  const size_t count = static_cast<size_t>(std::atoi(word.c_str()));
  size_t numbers = 0;
  while (in >> word) numbers++;
  if (numbers == count * 9) return 9;
  if (numbers == count * 8) return 8;
  return 0;
}

uint64_t Fnv64(const void* data, size_t size, uint64_t seed) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  uint64_t h = seed;
  for (size_t i = 0; i < size; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

std::string Hex(uint64_t v) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

namespace {

std::vector<std::string> Split(const std::string& s, char sep) {
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
  if (!cur.empty() || !s.empty()) out.push_back(cur);
  return out;
}

bool OneOf(const std::string& v, std::initializer_list<const char*> list) {
  for (const char* s : list) {
    if (v == s) return true;
  }
  return false;
}

bool LevelValid(const std::string& v) {
  if (v == "asis") return true;
  const size_t colon = v.find(':');
  if (colon == std::string::npos) return false;
  const std::string kind = v.substr(0, colon);
  if (kind != "all" && kind != "cap") return false;
  const std::string n = v.substr(colon + 1);
  if (n.empty() || n.size() > 3 || n.find_first_not_of("0123456789") != std::string::npos) return false;
  const int level = std::atoi(n.c_str());
  return level >= 1 && level <= 100;
}

std::string GenName(const std::string& ruleSet, const std::string& edition) {
  if (ruleSet == "gen4pokemon") return edition == "xg" ? "Gen 4 Pokemon with the classic XG rules" : "Gen 4 Pokemon with the classic XD rules";
  if (ruleSet == "gen4") return "Gen 4 Pokemon and rules";
  if (ruleSet == "gen4mt") return "Gen 4 Pokemon and rules with the modern types";
  return edition == "xg" ? "the classic XG rules" : "the classic XD rules";
}

}

std::string RoomRuleSetId(const RoomRules& r) { return !r.rs.empty() ? r.rs : RoomGen4Rules(r.ruleSet) ? "gen4" : "xd"; }
std::string RoomGens(const RoomRules& r) { return !r.gens.empty() ? r.gens : RoomGen4Pokemon(r.ruleSet) ? "3,4" : "3"; }
std::string RoomDetails(const RoomRules& r) { return !r.details.empty() || !r.rs.empty() ? r.details : r.ruleSet == "gen4mt" ? "modern_types:on" : ""; }
std::string RoomNumbers(const RoomRules& r) {
  const std::string rs = RoomRuleSetId(r);
  if (rs == "gen5") return "5";
  if (rs == "gen6") return "6f";
  if (rs == "gen7") return "7f";
  return "3";
}
int RoomMaxGen(const RoomRules& r) {
  int best = 0;
  for (const std::string& g : Split(RoomGens(r), ',')) best = std::max(best, std::atoi(g.c_str()));
  return best;
}
void SetRuleChoice(RoomRules& r, const std::string& rs, const std::string& gens, const std::string& details) {
  const bool pack = gens != "3";
  if (rs == "xd" && gens == "3" && details.empty()) r.ruleSet = "classic";
  else if (rs == "xd" && gens == "3,4" && details.empty()) r.ruleSet = "gen4pokemon";
  else if (rs == "gen4" && gens == "3,4" && details.empty()) r.ruleSet = "gen4";
  else if (rs == "gen4" && gens == "3,4" && details == "modern_types:on") r.ruleSet = "gen4mt";
  else {
    r.ruleSet = rs == "gen4" || rs == "gen5" || rs == "gen6" || rs == "gen7" ? "gen4" : pack ? "gen4pokemon" : "classic";
    r.rs = rs;
    r.gens = gens;
    r.details = details;
    return;
  }
  r.rs.clear();
  r.gens.clear();
  r.details.clear();
}
bool NeedsNewProtocol(const RoomRules& r) { return !r.rs.empty() || !r.gens.empty() || !r.details.empty() || !r.format.empty() || NeedsExtras(r); }
bool NeedsExtras(const RoomRules& r) { return r.sleepClause || r.preview || r.bankSeconds > 0; }
std::string ExtrasKeyText(const RoomRules& r) {
  std::ostringstream o;
  if (!r.rs.empty()) o << ";rs=" << r.rs;
  if (!r.gens.empty()) o << ";gens=" << r.gens;
  if (!r.details.empty()) o << ";details=" << r.details;
  if (!r.format.empty()) o << ";format=" << r.format << ";fmthash=" << r.formatHash;
  if (r.sleepClause) o << ";sleep_clause=on";
  if (r.preview) o << ";preview=on;pick=" << r.pick << ";preview_timer=" << r.previewSeconds;
  if (r.bankSeconds > 0) o << ";bank=" << r.bankSeconds;
  return o.str();
}

bool RulesValid(const RoomRules& r, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  if (!OneOf(r.ruleSet, {"classic", "gen4pokemon", "gen4", "gen4mt"})) return fail("rules must be classic, gen4pokemon, gen4 or gen4mt");
  if (!OneOf(r.items, {"none", "bag", "kit"})) return fail("items must be none, bag or kit");
  if (!OneOf(std::to_string(r.timerSeconds), {"0", "30", "60", "90", "120"})) return fail("timer must be 0 (off), 30, 60, 90 or 120");
  if (!OneOf(r.shadowObey, {"story", "purified"})) return fail("shadow_obey must be story or purified");
  if (!LevelValid(r.level)) return fail("level must be asis, all:N or cap:N (N 1-100)");
  if (!OneOf(r.team, {"preset", "save", "free12", "free14", "draft", "offer"})) return fail("team must be preset, save, free12, free14, draft or offer");
  if (r.draft.size() > 90 || r.draft.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789:,") != std::string::npos) return fail("draft options: letters, digits, : and , only (up to 90 characters)");
  if (!OneOf(r.style, {"single", "double"})) return fail("style must be single or double");
  if (r.team == "free14" && r.ruleSet == "classic") return fail("a free pick of Gen 1-4 needs the Gen 4 rules");
  const auto word = [](const std::string& v, size_t max) {
    return v.size() <= max && v.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_:,") == std::string::npos;
  };
  if (!word(r.rs, 16)) return fail("rule set: letters, digits and _ only");
  if (!word(r.gens, 16)) return fail("generations: digits and , only");
  if (!word(r.details, 60)) return fail("rule set details: letters, digits, _ : and , only");
  if (!word(r.format, 32) || !word(r.formatHash, 16)) return fail("format: letters, digits and _ only");
  if (r.pick < 0 || r.pick > 6) return fail("pick must be 0-6");
  if (r.previewSeconds < 0 || r.previewSeconds > 600) return fail("preview_timer must be 0-600 seconds");
  if (r.bankSeconds < 0 || r.bankSeconds > 3600) return fail("bank must be 0-3600 seconds");
  if (!r.preview && (r.pick != 0 || r.previewSeconds != 0)) return fail("pick and preview_timer need preview=on");
  return true;
}

bool ParseRules(const std::string& text, RoomRules& out, std::string* error) {
  RoomRules r;
  for (const std::string& item : Split(text, ';')) {
    if (item.empty()) continue;
    const size_t eq = item.find('=');
    if (eq == std::string::npos) {
      if (error) *error = "'" + item + "' is not key=value";
      return false;
    }
    const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
    if (k == "rules") r.ruleSet = v;
    else if (k == "items") r.items = v;
    else if (k == "timer") r.timerSeconds = std::atoi(v.c_str());
    else if (k == "shadow") r.shadowAllowed = v == "on" || v == "1";
    else if (k == "shadow_edited") r.editedShadows = v == "on" || v == "1";
    else if (k == "shadow_obey") r.shadowObey = v;
    else if (k == "level") r.level = v;
    else if (k == "team") r.team = v;
    else if (k == "draft") r.draft = v;
    else if (k == "style") r.style = v;
    else if (k == "split") r.split = v == "on" || v == "1";
    else if (k == "rs") r.rs = v;
    else if (k == "gens") r.gens = v;
    else if (k == "details") r.details = v;
    else if (k == "format") r.format = v;
    else if (k == "fmthash") r.formatHash = v;
    else if (k == "sleep_clause") r.sleepClause = v == "on" || v == "1";
    else if (k == "preview") r.preview = v == "on" || v == "1";
    else if (k == "pick") r.pick = std::atoi(v.c_str());
    else if (k == "preview_timer") r.previewSeconds = std::atoi(v.c_str());
    else if (k == "bank") r.bankSeconds = std::atoi(v.c_str());
    else {
      if (error) *error = "unknown room setting '" + k + "'";
      return false;
    }
  }
  if (!RulesValid(r, error)) return false;
  out = r;
  return true;
}

std::string RulesKeyText(const RoomRules& r) {
  std::ostringstream o;
  o << "rules=" << r.ruleSet << ";items=" << r.items << ";timer=" << r.timerSeconds << ";shadow=" << (r.shadowAllowed ? "on" : "off") << ";shadow_obey=" << r.shadowObey
    << ";level=" << r.level << ";team=" << r.team << ";style=" << r.style << ";split=" << (r.split ? "on" : "off");
  if (r.editedShadows) o << ";shadow_edited=on";
  if (!r.draft.empty()) o << ";draft=" << r.draft;
  o << ExtrasKeyText(r);
  return o.str();
}

std::string RulesText(const RoomRules& r, const std::string& edition) {
  std::ostringstream o;
  o << GenName(r.ruleSet, edition);
  o << "; items: " << (r.items == "none" ? "none" : r.items == "bag" ? "your bag items (not used up in your save)" : "standard kit (Max Potion, Full Heal, Revive)");
  o << "; time per choice: ";
  if (r.timerSeconds == 0) o << "no limit"; else o << r.timerSeconds << " s";
  o << "; Shadow Pokemon: ";
  if (!r.shadowAllowed) o << "not allowed";
  else o << "allowed, " << (r.shadowObey == "story" ? "they may refuse orders" : "they obey like purified ones") << (r.editedShadows ? ", also the ones made with the save editor" : "");
  o << "; levels: ";
  if (r.level == "asis") o << "as they are";
  else if (r.level.compare(0, 4, "all:") == 0) o << "all set to " << r.level.substr(4);
  else o << "capped at " << r.level.substr(4);
  o << "; " << (r.style == "double" ? "double" : "single") << " battle";
  o << "; physical/special split: " << (r.split ? "on" : "off");
  if (!r.rs.empty() && r.rs != "xd" && r.rs != "gen4") o << "; rule set: " << r.rs;
  if (!r.details.empty()) o << "; details: " << r.details;
  if (!r.gens.empty() && r.gens != "3" && r.gens != "3,4") o << "; Pokemon generations: " << r.gens;
  if (!r.format.empty()) o << "; format: " << r.format;
  if (r.sleepClause) o << "; Sleep clause: on";
  if (r.preview) o << "; team preview: bring up to 6, pick " << (r.pick > 0 ? r.pick : 6);
  if (r.bankSeconds > 0) o << "; battle clock: " << r.bankSeconds << " s each";
  o << "; teams: " << (r.team == "preset" ? "preset teams" : r.team == "save" ? "from a save" : r.team == "free12" ? "free pick, Gen 1-3" : r.team == "draft" ? "snake draft (" + r.draft + ")" : r.team == "offer" ? "random offers (" + r.draft + ")" : "free pick, Gen 1-4");
  return o.str();
}

std::vector<uint8_t> Encode(const Fingerprint& f) {
  auto build = [&](size_t maxModNames) {
    std::ostringstream o;
    o << "XDB1\n";
    o << "proto=" << (NeedsExtras(f.rules) ? 3 : NeedsNewProtocol(f.rules) ? 2 : 1) << "\n";
    o << "cap=" << f.caps.protocol << "\n";
    if (!f.caps.version.empty()) o << "ver=" << f.caps.version << "\n";
    o << "disc=" << f.caps.disc << "\n";
    o << "edition=" << f.caps.edition << "\n";
    o << "loaded=" << f.caps.ruleSet << "\n";
    o << "pack=" << f.caps.gen4Pack << "\n";
    if (f.caps.numbers != "3") o << "numbers=" << f.caps.numbers << "\n";
    o << "modsum=" << Hex(Fnv64(f.caps.mods.data(), f.caps.mods.size())) << "\n";
    o << "mods=" << f.caps.mods.substr(0, maxModNames) << "\n";
    o << "rando=" << f.caps.randomizer.substr(0, 90) << "\n";
    o << "tables=" << Hex(f.caps.tables) << "\n";
    if (!f.caps.formatsDigest.empty()) o << "fmts=" << f.caps.formatsDigest << "\n";
    if (f.caps.cheats) o << "cheats=1\n";
    o << "r.rules=" << f.rules.ruleSet << "\n";
    o << "r.items=" << f.rules.items << "\n";
    o << "r.timer=" << f.rules.timerSeconds << "\n";
    o << "r.shadow=" << (f.rules.shadowAllowed ? 1 : 0) << "\n";
    o << "r.obey=" << f.rules.shadowObey << "\n";
    if (f.rules.editedShadows) o << "r.edsh=1\n";
    o << "r.level=" << f.rules.level << "\n";
    o << "r.team=" << f.rules.team << "\n";
    o << "r.split=" << (f.rules.split ? 1 : 0) << "\n";
    o << "r.draft=" << f.rules.draft << "\n";
    o << "r.style=" << f.rules.style << "\n";
    if (NeedsNewProtocol(f.rules)) {
      o << "r.rs=" << f.rules.rs << "\n";
      o << "r.gens=" << f.rules.gens << "\n";
      o << "r.det=" << f.rules.details << "\n";
      o << "r.fmt=" << f.rules.format << ":" << f.rules.formatHash << "\n";
      if (NeedsExtras(f.rules)) {
        o << "r.ex=sleep:" << (f.rules.sleepClause ? 1 : 0) << ",pv:" << (f.rules.preview ? 1 : 0) << ",pick:" << f.rules.pick << ",pvt:" << f.rules.previewSeconds
          << ",bank:" << f.rules.bankSeconds << "\n";
      }
    }
    return o.str();
  };
  std::string text = build(120);
  for (size_t names = 100; text.size() > 400 && names > 0; names -= 20) text = build(names);
  if (text.size() > 400) text.resize(400);
  return std::vector<uint8_t>(text.begin(), text.end());
}

bool Decode(const std::vector<uint8_t>& blob, Fingerprint& out) {
  const std::string text(blob.begin(), blob.end());
  if (text.compare(0, 5, "XDB1\n") != 0) return false;
  std::map<std::string, std::string> kv;
  for (const std::string& line : Split(text.substr(5), '\n')) {
    const size_t eq = line.find('=');
    if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
  }
  auto get = [&](const char* k) { return kv.count(k) ? kv[k] : std::string(); };
  if (!kv.count("proto") || !kv.count("disc")) return false;
  Fingerprint f;
  f.caps.protocol = kv.count("cap") ? std::atoi(get("cap").c_str()) : std::atoi(get("proto").c_str());
  f.caps.disc = get("disc");
  f.caps.version = get("ver").substr(0, 16);
  f.caps.edition = kv.count("edition") ? get("edition") : std::string("xd");
  f.caps.ruleSet = get("loaded");
  f.caps.gen4Pack = get("pack");
  f.caps.numbers = kv.count("numbers") ? get("numbers") : std::string("3");
  f.caps.mods = get("mods");
  f.caps.randomizer = get("rando");
  f.caps.tables = std::strtoull(get("tables").c_str(), nullptr, 16);
  f.caps.formatsDigest = get("fmts");
  f.caps.cheats = get("cheats") == "1";
  f.rules.ruleSet = get("r.rules");
  f.rules.items = get("r.items");
  f.rules.timerSeconds = std::atoi(get("r.timer").c_str());
  f.rules.shadowAllowed = get("r.shadow") == "1";
  f.rules.shadowObey = get("r.obey");
  f.rules.editedShadows = get("r.edsh") == "1";
  f.rules.level = get("r.level");
  f.rules.team = get("r.team");
  f.rules.draft = get("r.draft");
  if (kv.count("r.style")) f.rules.style = get("r.style");
  f.rules.split = get("r.split") == "1";
  f.rules.rs = get("r.rs");
  f.rules.gens = get("r.gens");
  f.rules.details = get("r.det");
  if (const std::string fmt = get("r.fmt"); !fmt.empty()) {
    const size_t colon = fmt.find(':');
    f.rules.format = fmt.substr(0, colon);
    f.rules.formatHash = colon == std::string::npos ? std::string() : fmt.substr(colon + 1);
  }
  if (const std::string ex = get("r.ex"); !ex.empty()) {
    for (const std::string& part : Split(ex, ',')) {
      const size_t colon = part.find(':');
      if (colon == std::string::npos) continue;
      const std::string key = part.substr(0, colon);
      const int value = std::atoi(part.c_str() + colon + 1);
      if (key == "sleep") f.rules.sleepClause = value != 0;
      else if (key == "pv") f.rules.preview = value != 0;
      else if (key == "pick") f.rules.pick = value;
      else if (key == "pvt") f.rules.previewSeconds = value;
      else if (key == "bank") f.rules.bankSeconds = value;
    }
  }
  const std::string sum = get("modsum");
  if (Hex(Fnv64(f.caps.mods.data(), f.caps.mods.size())) != sum) f.caps.mods += "\x01" + sum;
  std::string why;
  if (!RulesValid(f.rules, &why)) return false;
  out = f;
  return true;
}

namespace {

std::string ListText(const std::set<std::string>& s) {
  std::string out;
  for (const std::string& x : s) out += (out.empty() ? "" : ", ") + x;
  return out;
}

std::set<std::string> ModSet(const std::string& mods) {
  std::set<std::string> s;
  for (const std::string& m : Split(mods.substr(0, mods.find('\x01')), ',')) {
    if (!m.empty()) s.insert(m);
  }
  return s;
}

}

std::string VersionLabel(const Capabilities& c) {
  if (!c.version.empty()) return c.version;
  if (c.protocol >= 6) return "0.8.0 or newer";
  if (c.protocol == 5) return "0.7.7";
  if (c.protocol == 4) return "0.7.5 or 0.7.6";
  return "0.7.4 or older";
}

Verdict Judge(const Capabilities& room, const RoomRules& rules, const Capabilities& mine, bool joiner) {
  Verdict v;
  const std::string they = joiner ? "The room" : "Your friend's game";
  const std::string you = joiner ? "your game" : "theirs";
  if (mine.cheats) {
    if (!rules.format.empty()) {
      v.refuse.push_back(joiner ? "This is a competitive room, and your playthrough used cheats, so it can't join. Casual rooms and trades are still open to it."
                                : "Your friend's playthrough used cheats, so it can't be in a competitive room. A casual room or a trade works.");
    } else {
      v.notes.push_back(joiner ? "Your playthrough used cheats: this room is marked." : "Your friend's playthrough used cheats: this room is marked.");
    }
  }
  const bool newKind = NeedsNewProtocol(rules);
  const int needed = NeedsExtras(rules) ? 3 : newKind ? 2 : 1;
  const bool known = room.protocol >= 1 && room.protocol <= kBattleProtocol && mine.protocol >= 1 && mine.protocol <= kBattleProtocol &&
                     (room.protocol >= 4) == (mine.protocol >= 4) && (room.protocol < 4 || room.protocol == mine.protocol);
  const bool compatible = known && mine.protocol >= needed && room.protocol >= needed;
  if (!compatible && room.protocol != mine.protocol && (room.protocol >= 4 || mine.protocol >= 4)) {
    const std::string theirV = VersionLabel(joiner ? room : mine), myV = VersionLabel(joiner ? mine : room);
    v.refuse.push_back(std::string(joiner ? "This room's game is another version (" : "Your friend's game is another version (") + theirV + "). You have " + myV +
                       ". Both players need the same game version.");
    return v;
  }
  if (!compatible && room.protocol != mine.protocol) {
    if (needed == kExtrasProtocol && mine.protocol < kExtrasProtocol) {
      v.refuse.push_back(joiner ? "This room uses format rules (Sleep clause, team preview or a battle clock) that your game version does not have. Update the game to join it."
                                : "Your friend's game version does not know this room's format rules (Sleep clause, team preview or a battle clock). Ask them to update.");
      return v;
    }
    v.refuse.push_back(joiner ? "The room uses another version of the online battles (room " + std::to_string(room.protocol) + ", yours " +
                                    std::to_string(mine.protocol) + "). Update the game."
                              : "Your friend's game has another version of the online battles (theirs " + std::to_string(mine.protocol) +
                                    ", yours " + std::to_string(room.protocol) + "). Ask them to update.");
    return v;
  }
  if (room.edition != mine.edition) {
    auto name = [](const std::string& e) { return e == "xg" ? "Pokemon XG" : "Pokemon XD"; };
    v.refuse.push_back(joiner ? std::string("This room is for ") + name(room.edition) + " players; your game is " + name(mine.edition) +
                                    ". XD and XG players cannot battle or trade with each other."
                              : std::string("Your friend plays ") + name(mine.edition) + "; your game is " + name(room.edition) +
                                    ". XD and XG players cannot battle or trade with each other.");
    return v;
  }
  if (room.disc != mine.disc) {
    v.refuse.push_back(they + " uses the disc " + room.disc + "; " + you + " has " + mine.disc + ".");
  }
  {
    const std::string rsId = RoomRuleSetId(rules);
    const std::set<std::string> known = ModSet(mine.knownRuleSets);
    if (!known.count(rsId)) {
      v.refuse.push_back(joiner ? "The room uses the rule set \"" + rsId + "\", which this version of the game does not have. Update the game."
                                : "Your friend's game does not have the rule set \"" + rsId + "\". Ask them to update.");
    }
    if (RoomMaxGen(rules) > mine.knownMaxGen) {
      v.refuse.push_back(joiner ? "The room uses Pokemon up to generation " + std::to_string(RoomMaxGen(rules)) + "; this version of the game has them up to generation " +
                                      std::to_string(mine.knownMaxGen) + ". Update the game."
                                : "Your friend's game does not have the Pokemon of generation " + std::to_string(RoomMaxGen(rules)) + ". Ask them to update.");
    }
    if (!rules.format.empty() && !joiner && mine.knownFormats.empty()) {
      if (!room.formatsDigest.empty() && room.formatsDigest != mine.formatsDigest)
        v.refuse.push_back("Your friend's game has another version of the format \"" + rules.format + "\". Both players should use the same game version.");
    } else if (!rules.format.empty()) {
      const std::string want = rules.format + ":" + rules.formatHash;
      const std::set<std::string> formats = ModSet(mine.knownFormats);
      if (!formats.count(want)) {
        const bool sameId = [&] { for (const std::string& f : formats) if (f.compare(0, rules.format.size() + 1, rules.format + ":") == 0) return true; return false; }();
        v.refuse.push_back(sameId ? std::string(joiner ? "The room's format \"" : "Your friend's game has another version of the format \"") + rules.format +
                                        (joiner ? "\" is another version from yours. Both players should use the same game version." : "\". Both players should use the same game version.")
                                  : std::string(joiner ? "The room's format \"" : "Your friend's game does not have the format \"") + rules.format +
                                        (joiner ? "\" is not in this version of the game. Update the game." : "\". Ask them to update."));
      }
    }
  }
  if (rules.ruleSet != "classic") {
    if (mine.gen4Pack.empty()) {
      v.refuse.push_back(joiner ? "This room plays with Gen 4 Pokemon. You need the Gen 4 pack from your Pokemon Battle Revolution disc (the launcher makes it)."
                                : "Your friend does not have the Gen 4 pack from the Pokemon Battle Revolution disc, which this room needs.");
    } else if (room.gen4Pack != mine.gen4Pack && room.gen4Pack.substr(0, room.gen4Pack.find("+g57")) == mine.gen4Pack.substr(0, mine.gen4Pack.find("+g57"))) {
      const auto tail = [](const std::string& s) { const size_t p = s.find("+g57"); return p == std::string::npos ? std::string("none") : s.substr(p + 4, 8); };
      v.refuse.push_back(joiner ? "Your Gen 5-7 pack is a different version from the room's (yours " + tail(mine.gen4Pack) + ", the room's " + tail(room.gen4Pack) +
                                      "). Make the pack again in the launcher."
                                : "Your friend's Gen 5-7 pack is a different version from yours. Ask them to make it again in the launcher.");
    } else if (room.gen4Pack != mine.gen4Pack) {
      v.refuse.push_back(joiner ? "Your Gen 4 pack is a different version from the room's (yours " + mine.gen4Pack.substr(0, 8) + ", the room's " +
                                      room.gen4Pack.substr(0, 8) + "). Make the pack again in the launcher."
                                : "Your friend's Gen 4 pack is a different version from yours. Ask them to make it again in the launcher.");
    } else if (mine.ruleSet != "gen4") {
      v.restart.push_back(std::string(joiner ? "The game restarts with the Gen 4 Pokemon for this battle" : "Your friend's game restarts with the Gen 4 Pokemon") +
                          " (it comes back to your own game afterwards).");
    }
  }
  {
    const std::string want = RoomNumbers(rules);
    if (want[0] != mine.numbers[0] || (want.size() > 1 && mine.numbers.size() < 2)) {
      v.restart.push_back(std::string(joiner ? "The game restarts with the numbers of this room's rule set" : "Your friend's game restarts with the numbers of this room's rule set") +
                          " (the Pokemon's stats and the moves' power, accuracy and PP of Gen " + want.substr(0, 1) + "; it comes back to your own game afterwards).");
    }
  }
  const bool roomCut = room.mods.find('\x01') != std::string::npos;
  if (roomCut) {
    if (room.mods.substr(room.mods.find('\x01') + 1) != Hex(Fnv64(mine.mods.data(), mine.mods.size()))) {
      v.refuse.push_back("The mods of the room (a long list) are not the mods of " + you + ". Use the same mods.");
    }
  } else {
    const std::set<std::string> a = ModSet(room.mods), b = ModSet(mine.mods);
    std::set<std::string> missing, extra;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(missing, missing.end()));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::inserter(extra, extra.end()));
    std::set<std::string> blocked;
    for (const std::string& id : missing) {
      if (joiner && mine.modsBlocked.count(id)) blocked.insert(id);
    }
    for (const std::string& id : blocked) missing.erase(id);
    for (const std::string& id : blocked) {
      const auto it = mine.modAssets.find(id);
      v.refuse.push_back("The room uses the mod " + id + ", which is on your PC but needs extra assets of your own that are not ready" +
                         (it != mine.modAssets.end() ? " (" + it->second + ")" : std::string()) +
                         ". Make them on the launcher's Extra assets page, then join again.");
    }
    if (!missing.empty()) {
      std::string needs;
      if (!joiner) {
        for (const std::string& id : missing) {
          const auto it = room.modAssets.find(id);
          if (it != room.modAssets.end()) needs += (needs.empty() ? " " : " ") + id + " needs your own " + it->second + " (the launcher's Extra assets page makes it).";
        }
      }
      v.refuse.push_back(they + " uses mods that " + you + " does not have: " + ListText(missing) + "." + needs);
    } else if (!extra.empty()) v.restart.push_back(std::string(joiner ? "The game restarts without the mods the room does not use: " : "Your friend's game restarts without: ") + ListText(extra) + ".");
  }
  if (!mine.randomizer.empty()) {
    v.restart.push_back(std::string(joiner ? "The game restarts without your randomizer" : "Your friend's game restarts without their randomizer") +
                        " (online battles use the room's data, never a save's randomized data).");
  }
  if (room.tables != 0 && mine.tables != 0 && room.tables != mine.tables && v.refuse.empty() && v.restart.empty()) {
    v.refuse.push_back("The game data of the two games differs in a way the list above does not explain. Both players should use the same game version.");
  }
  return v;
}

std::string PackBattleDigest(const std::filesystem::path& folder) {
  namespace fs = std::filesystem;
  std::string all;
  bool any = false;
  const auto add = [&](const fs::path& file, const std::string& name) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    std::ostringstream o;
    o << in.rdbuf();
    const std::string bytes = o.str();
    all += name + '\n' + std::to_string(bytes.size()) + '\n' + bytes + '\n';
    any = true;
  };
  std::error_code ec;
  for (const char* sub : {"data", "battle"}) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(folder / sub, ec)) {
      if (e.is_regular_file(ec) && e.path().extension() == ".json") names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    for (const std::string& n : names) add(folder / sub / n, std::string(sub) + "/" + n);
  }
  for (const char* n : {"tables.txt", "machines.txt"}) add(folder / n, n);
  return any ? Hex(Fnv64(all.data(), all.size())) : std::string("unknown");
}

}
