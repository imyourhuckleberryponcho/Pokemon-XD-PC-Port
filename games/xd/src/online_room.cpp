#include "online_room.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace xd::online {

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
  if (ruleSet == "gen4") return "Gen 4 Pokemon and rules";
  if (ruleSet == "gen4mt") return "Gen 4 Pokemon and rules with the modern types";
  return edition == "xg" ? "the classic XG rules" : "the classic XD rules";
}

}

bool RulesValid(const RoomRules& r, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  if (!OneOf(r.ruleSet, {"classic", "gen4", "gen4mt"})) return fail("rules must be classic, gen4 or gen4mt");
  if (!OneOf(r.items, {"none", "bag", "kit"})) return fail("items must be none, bag or kit");
  if (!OneOf(std::to_string(r.timerSeconds), {"0", "30", "60", "90", "120"})) return fail("timer must be 0 (off), 30, 60, 90 or 120");
  if (!OneOf(r.shadowObey, {"story", "purified"})) return fail("shadow_obey must be story or purified");
  if (!LevelValid(r.level)) return fail("level must be asis, all:N or cap:N (N 1-100)");
  if (!OneOf(r.team, {"preset", "save", "free12", "free14", "draft", "offer"})) return fail("team must be preset, save, free12, free14, draft or offer");
  if (r.draft.size() > 90 || r.draft.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789:,") != std::string::npos) return fail("draft options: letters, digits, : and , only (up to 90 characters)");
  if (!OneOf(r.style, {"single", "double"})) return fail("style must be single or double");
  if (r.team == "free14" && r.ruleSet == "classic") return fail("a free pick of Gen 1-4 needs the Gen 4 rules");
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
    else if (k == "shadow_obey") r.shadowObey = v;
    else if (k == "level") r.level = v;
    else if (k == "team") r.team = v;
    else if (k == "draft") r.draft = v;
    else if (k == "style") r.style = v;
    else if (k == "split") r.split = v == "on" || v == "1";
    else {
      if (error) *error = "unknown room setting '" + k + "'";
      return false;
    }
  }
  if (!RulesValid(r, error)) return false;
  out = r;
  return true;
}

std::string RulesText(const RoomRules& r, const std::string& edition) {
  std::ostringstream o;
  o << GenName(r.ruleSet, edition);
  o << "; items: " << (r.items == "none" ? "none" : r.items == "bag" ? "your bag items (not used up in your save)" : "standard kit (Max Potion, Full Heal, Revive)");
  o << "; time per choice: ";
  if (r.timerSeconds == 0) o << "no limit"; else o << r.timerSeconds << " s";
  o << "; Shadow Pokemon: ";
  if (!r.shadowAllowed) o << "not allowed";
  else o << "allowed, " << (r.shadowObey == "story" ? "they may refuse orders" : "they obey like purified ones");
  o << "; levels: ";
  if (r.level == "asis") o << "as they are";
  else if (r.level.compare(0, 4, "all:") == 0) o << "all set to " << r.level.substr(4);
  else o << "capped at " << r.level.substr(4);
  o << "; " << (r.style == "double" ? "double" : "single") << " battle";
  o << "; physical/special split: " << (r.split ? "on" : "off");
  o << "; teams: " << (r.team == "preset" ? "preset teams" : r.team == "save" ? "from a save" : r.team == "free12" ? "free pick, Gen 1-3" : r.team == "draft" ? "snake draft (" + r.draft + ")" : r.team == "offer" ? "random offers (" + r.draft + ")" : "free pick, Gen 1-4");
  return o.str();
}

std::vector<uint8_t> Encode(const Fingerprint& f) {
  auto build = [&](size_t maxModNames) {
    std::ostringstream o;
    o << "XDB1\n";
    o << "proto=" << f.caps.protocol << "\n";
    o << "disc=" << f.caps.disc << "\n";
    o << "edition=" << f.caps.edition << "\n";
    o << "loaded=" << f.caps.ruleSet << "\n";
    o << "pack=" << f.caps.gen4Pack << "\n";
    o << "modsum=" << Hex(Fnv64(f.caps.mods.data(), f.caps.mods.size())) << "\n";
    o << "mods=" << f.caps.mods.substr(0, maxModNames) << "\n";
    o << "rando=" << f.caps.randomizer.substr(0, 90) << "\n";
    o << "tables=" << Hex(f.caps.tables) << "\n";
    o << "r.rules=" << f.rules.ruleSet << "\n";
    o << "r.items=" << f.rules.items << "\n";
    o << "r.timer=" << f.rules.timerSeconds << "\n";
    o << "r.shadow=" << (f.rules.shadowAllowed ? 1 : 0) << "\n";
    o << "r.obey=" << f.rules.shadowObey << "\n";
    o << "r.level=" << f.rules.level << "\n";
    o << "r.team=" << f.rules.team << "\n";
    o << "r.split=" << (f.rules.split ? 1 : 0) << "\n";
    o << "r.draft=" << f.rules.draft << "\n";
    o << "r.style=" << f.rules.style << "\n";
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
  f.caps.protocol = std::atoi(get("proto").c_str());
  f.caps.disc = get("disc");
  f.caps.edition = kv.count("edition") ? get("edition") : std::string("xd");
  f.caps.ruleSet = get("loaded");
  f.caps.gen4Pack = get("pack");
  f.caps.mods = get("mods");
  f.caps.randomizer = get("rando");
  f.caps.tables = std::strtoull(get("tables").c_str(), nullptr, 16);
  f.rules.ruleSet = get("r.rules");
  f.rules.items = get("r.items");
  f.rules.timerSeconds = std::atoi(get("r.timer").c_str());
  f.rules.shadowAllowed = get("r.shadow") == "1";
  f.rules.shadowObey = get("r.obey");
  f.rules.level = get("r.level");
  f.rules.team = get("r.team");
  f.rules.draft = get("r.draft");
  if (kv.count("r.style")) f.rules.style = get("r.style");
  f.rules.split = get("r.split") == "1";
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

Verdict Judge(const Capabilities& room, const RoomRules& rules, const Capabilities& mine, bool joiner) {
  Verdict v;
  const std::string they = joiner ? "The room" : "Your friend's game";
  const std::string you = joiner ? "your game" : "theirs";
  if (room.protocol != mine.protocol) {
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
  if (rules.ruleSet != "classic") {
    if (mine.gen4Pack.empty()) {
      v.refuse.push_back(joiner ? "This room plays with Gen 4 Pokemon. You need the Gen 4 pack from your Pokemon Battle Revolution disc (the launcher makes it)."
                                : "Your friend does not have the Gen 4 pack from the Pokemon Battle Revolution disc, which this room needs.");
    } else if (room.gen4Pack != mine.gen4Pack) {
      v.refuse.push_back(joiner ? "Your Gen 4 pack is a different version from the room's (yours " + mine.gen4Pack.substr(0, 8) + ", the room's " +
                                      room.gen4Pack.substr(0, 8) + "). Make the pack again in the launcher."
                                : "Your friend's Gen 4 pack is a different version from yours. Ask them to make it again in the launcher.");
    } else if (mine.ruleSet != "gen4") {
      v.restart.push_back(std::string(joiner ? "The game restarts with the Gen 4 Pokemon for this battle" : "Your friend's game restarts with the Gen 4 Pokemon") +
                          " (it comes back to your own game afterwards).");
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
    if (!missing.empty()) v.refuse.push_back(they + " uses mods that " + you + " does not have: " + ListText(missing) + ".");
    else if (!extra.empty()) v.restart.push_back(std::string(joiner ? "The game restarts without the mods the room does not use: " : "Your friend's game restarts without: ") + ListText(extra) + ".");
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

}
