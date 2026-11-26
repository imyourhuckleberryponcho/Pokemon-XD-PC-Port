#include "online_lobby.h"

#include "online_draft.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace xd::online {
namespace {

const char* kNoPack = "This PC has no Gen 4 pack yet (make it from your Pokemon Battle Revolution disc in the launcher).";

int Index(const std::vector<Option>& o, const std::string& value) {
  for (size_t i = 0; i < o.size(); i++) {
    if (o[i].value == value) return static_cast<int>(i);
  }
  return 0;
}

Option Opt(const std::string& v, const std::string& l, bool ok = true, const std::string& why = "") {
  Option o;
  o.value = v;
  o.label = l;
  o.enabled = ok;
  o.why = why;
  return o;
}

Row MakeRow(const std::string& id, const std::string& title, std::vector<Option> o, const std::string& current) {
  Row r;
  r.id = id;
  r.title = title;
  r.options = std::move(o);
  r.current = Index(r.options, current);
  return r;
}

Row NumberRow(const std::string& id, const std::string& title, int n, const std::string& suffix = "") {
  Row r;
  r.id = id;
  r.title = title;
  r.number = true;
  r.options.push_back(Opt(std::to_string(n), std::to_string(n) + suffix));
  return r;
}

std::string LevelKind(const std::string& level) { return level == "asis" ? "asis" : level.substr(0, level.find(':')); }
int LevelN(const std::string& level) { return level == "asis" ? 50 : std::atoi(level.c_str() + level.find(':') + 1); }

draft::Settings DraftOf(const RoomRules& r) {
  draft::Settings s;
  draft::ParseSettings(r.team == "offer" ? "offer" : "snake", r.draft, s, nullptr);
  return s;
}

}

RoomRules DefaultRoom() {
  RoomRules r;
  r.timerSeconds = 90;
  return r;
}

std::vector<Row> SettingRows(const RoomRules& r, const PcInfo& pc) {
  std::vector<Row> rows;
  const bool g4 = r.ruleSet != "classic";
  const std::string noPack = pc.gen4Off.empty() ? std::string(kNoPack) : pc.gen4Off;
  if (!pc.formats.empty()) {
    std::vector<Option> fo = {Opt("", "Custom")};
    for (const PcInfo::FormatChoice& f : pc.formats) fo.push_back(Opt(f.id, f.name, f.enabled, f.why));
    rows.push_back(MakeRow("format", "Format", fo, r.format));
  }
  rows.push_back(MakeRow("rules", "Rules",
                         {Opt("classic", pc.edition == "xg" ? "Classic XG" : "Classic XD"), Opt("gen4pokemon", pc.edition == "xg" ? "Gen 4 Pokemon, XG rules" : "Gen 4 Pokemon, XD rules", pc.gen4PackPresent, noPack),
                          Opt("gen4", "Gen 4", pc.gen4PackPresent && pc.gen4RulesOff.empty(), pc.gen4RulesOff.empty() ? noPack : pc.gen4RulesOff),
                          Opt("gen4mt", "Gen 4 + modern types", pc.gen4PackPresent && pc.gen4RulesOff.empty(), pc.gen4RulesOff.empty() ? noPack : pc.gen4RulesOff),
                          Opt("gen5", "Gen 5 rules and numbers", pc.gen4PackPresent && pc.gen4RulesOff.empty(), pc.gen4RulesOff.empty() ? noPack : pc.gen4RulesOff),
                          Opt("gen6", "Gen 6 rules and numbers", pc.gen4PackPresent && pc.gen4RulesOff.empty(), pc.gen4RulesOff.empty() ? noPack : pc.gen4RulesOff),
                          Opt("gen7", "Gen 7 rules and numbers", pc.gen4PackPresent && pc.gen4RulesOff.empty(), pc.gen4RulesOff.empty() ? noPack : pc.gen4RulesOff)},
                         (r.rs == "gen5" || r.rs == "gen6" || r.rs == "gen7") ? r.rs : r.ruleSet));
  rows.push_back(MakeRow("split", "Physical / special split", {Opt("off", pc.edition == "xg" ? "Off (as in XG)" : "Off (as in XD)"), Opt("on", "On (as in Gen 4)")}, r.split ? "on" : "off"));
  rows.push_back(MakeRow("style", "Battle style", {Opt("single", "Single battle"), Opt("double", "Double battle")}, r.style));
  rows.push_back(MakeRow("team", "Teams",
                         {Opt("preset", "Preset teams"), Opt("save", "From a save (party or PC)"), Opt("free12", "Free pick, Gen 1-3"),
                          Opt("free14", "Free pick, Gen 1-4", g4, "Needs the Gen 4 rules."), Opt("draft", "Snake draft"), Opt("offer", "Random offers")},
                         r.team));
  if (r.team == "draft" || r.team == "offer") {
    const draft::Settings s = DraftOf(r);
    rows.push_back(NumberRow("d_size", "Pokemon each", s.teamSize));
    if (r.team == "draft") {
      rows.push_back(NumberRow("d_pool", "Pool size", s.poolSize));
      rows.push_back(NumberRow("d_bans", "Bans each", s.bans));
      rows.push_back(MakeRow("d_first", "First pick", {Opt("host", "The host"), Opt("coin", "Coin flip")}, s.first));
    } else {
      rows.push_back(NumberRow("d_offer", "Offers per pick", s.offerCount));
    }
    rows.push_back(MakeRow("d_gen", "Pool", {Opt("3", "Gen 1-3"), Opt("4", "Gen 1-4", g4, "Needs the Gen 4 rules.")}, s.gen4 ? "4" : "3"));
    rows.push_back(MakeRow("d_evo", "Evolution", {Opt("any", "Any stage"), Opt("full", "Fully evolved only"), Opt("final", "Last stage of a line")},
                           s.finalStage ? "final" : s.fullyEvolved ? "full" : "any"));
    rows.push_back(MakeRow("d_legend", "Legendary Pokemon", {Opt("yes", "Allowed"), Opt("no", "Not allowed")}, s.noLegend ? "no" : "yes"));
  }
  rows.push_back(MakeRow("level", "Levels",
                         {Opt("asis", "As they are"), Opt("all", "All the same level"), Opt("cap", "Capped at a level")}, LevelKind(r.level)));
  if (r.level != "asis") rows.push_back(NumberRow("levelN", "Level", LevelN(r.level)));
  if (!pc.formats.empty() && pc.formatLine != nullptr && !r.format.empty()) {
    for (const char* what : {"clauses", "bans", "preview", "timers"}) {
      const char* title = std::string(what) == "clauses" ? "Clauses" : std::string(what) == "bans" ? "Bans" : std::string(what) == "preview" ? "Team preview" : "Timers";
      rows.push_back(MakeRow(what, title, {Opt("line", pc.formatLine(r, what))}, "line"));
    }
  }
  rows.push_back(MakeRow("items", "Items",
                         {Opt("none", "None"), Opt("bag", "Own bag items (never used up in the save)", r.team == "save", "Only with teams from a save."),
                          Opt("kit", "Standard kit (Max Potion, Full Heal, Revive)")},
                         r.items));
  rows.push_back(MakeRow("timer", "Time per choice",
                         {Opt("0", "No limit"), Opt("30", "30 seconds"), Opt("60", "60 seconds"), Opt("90", "90 seconds"), Opt("120", "120 seconds")},
                         std::to_string(r.timerSeconds)));
  rows.push_back(MakeRow("shadow", "Shadow Pokemon", {Opt("off", "Not allowed"), Opt("on", "Allowed")}, r.shadowAllowed ? "on" : "off"));
  if (r.shadowAllowed) {
    rows.push_back(MakeRow("obey", "Shadow Pokemon obey", {Opt("story", "As in the story (they may refuse)"), Opt("purified", "Like purified ones")}, r.shadowObey));
  }
  return rows;
}

RoomRules Sanitized(RoomRules r, const PcInfo& pc) {
  if (r.ruleSet != "classic" && !pc.gen4PackPresent) r.ruleSet = "classic";
  if (r.ruleSet == "classic") {
    if (r.team == "free14") r.team = "free12";
    if (r.team == "draft" || r.team == "offer") {
      draft::Settings s = DraftOf(r);
      if (s.gen4) {
        s.gen4 = false;
        r.draft = draft::SettingsText(s);
      }
    }
  }
  if (r.items == "bag" && r.team != "save") r.items = "none";
  return r;
}

bool Cycle(RoomRules& r, const std::string& id, int dir, const PcInfo& pc) {
  const RoomRules before = r;
  std::vector<Row> rows = SettingRows(r, pc);
  Row* row = nullptr;
  for (Row& x : rows) {
    if (x.id == id) row = &x;
  }
  if (!row) return false;
  const int n = static_cast<int>(row->options.size());
  std::string value;
  if (row->number) {
    const int cur = std::atoi(row->options[0].value.c_str());
    int lo = 1, hi = 100, step = 1;
    if (id == "d_size") hi = 6;
    else if (id == "d_pool") lo = 4, hi = 120, step = 2;
    else if (id == "d_bans") lo = 0, hi = 5;
    else if (id == "d_offer") lo = 2, hi = 5;
    value = std::to_string(std::max(lo, std::min(hi, cur + dir * step)));
  } else {
    int i = row->current;
    for (int k = 0; k < n; k++) {
      i = (i + dir + n) % n;
      if (row->options[static_cast<size_t>(i)].enabled) break;
    }
    if (!row->options[static_cast<size_t>(i)].enabled) return false;
    value = row->options[static_cast<size_t>(i)].value;
  }
  if (id == "format") {
    r.format = value;
    r.formatHash.clear();
    r.sleepClause = r.preview = false, r.pick = r.previewSeconds = r.bankSeconds = 0;
    std::string err;
    if (!value.empty() && pc.applyFormat != nullptr && !pc.applyFormat(r)) { r = before; return false; }
  } else if (id == "rules") {
    if (value == "gen5" || value == "gen6" || value == "gen7") SetRuleChoice(r, value, "3,4", "");
    else { r.ruleSet = value; r.rs.clear(); r.gens.clear(); r.details.clear(); }
  }
  else if (id == "split") r.split = value == "on";
  else if (id == "style") r.style = value;
  else if (id == "team") {
    r.team = value;
    if ((value == "free12" || value == "free14" || value == "draft" || value == "offer") && r.level == "asis") r.level = "all:50";
    if (value == "draft" || value == "offer") {
      draft::Settings s = DraftOf(r);
      draft::Settings d;
      d.mode = value == "offer" ? "offer" : "snake";
      d.gen4 = s.gen4 && r.ruleSet != "classic";
      r.draft = draft::SettingsText(d);
    } else {
      r.draft.clear();
    }
  } else if (id == "level") {
    r.level = value == "asis" ? "asis" : value + ":" + std::to_string(LevelN(r.level));
  } else if (id == "levelN") {
    r.level = LevelKind(r.level) + ":" + value;
  } else if (id == "items") r.items = value;
  else if (id == "timer") r.timerSeconds = std::atoi(value.c_str());
  else if (id == "shadow") r.shadowAllowed = value == "on";
  else if (id == "obey") r.shadowObey = value;
  else if (id.compare(0, 2, "d_") == 0) {
    draft::Settings s = DraftOf(r);
    if (id == "d_size") s.teamSize = std::atoi(value.c_str());
    else if (id == "d_pool") s.poolSize = std::atoi(value.c_str());
    else if (id == "d_bans") s.bans = std::atoi(value.c_str());
    else if (id == "d_offer") s.offerCount = std::atoi(value.c_str());
    else if (id == "d_first") s.first = value;
    else if (id == "d_gen") s.gen4 = value == "4";
    else if (id == "d_evo") s.fullyEvolved = value != "any", s.finalStage = value == "final";
    else if (id == "d_legend") s.noLegend = value == "no";
    r.draft = draft::SettingsText(s);
  }
  r = Sanitized(r, pc);
  if (r.team == "draft" || r.team == "offer") {
    std::string why;
    draft::Settings s;
    if (!draft::ParseSettings(r.team == "offer" ? "offer" : "snake", r.draft, s, &why)) {
      r = before;
      return false;
    }
  }
  std::string why;
  if (!RulesValid(r, &why)) {
    r = before;
    return false;
  }
  if (id != "format" && !r.format.empty() && RulesToText(r) != RulesToText(before)) r.format.clear(), r.formatHash.clear(), r.sleepClause = r.preview = false, r.pick = r.previewSeconds = r.bankSeconds = 0;
  return RulesToText(r) != RulesToText(before);
}

bool SelectFormat(RoomRules& r, const std::string& id, const PcInfo& pc) {
  const RoomRules before = r;
  r.format = id;
  r.formatHash.clear();
  r.sleepClause = r.preview = false, r.pick = r.previewSeconds = r.bankSeconds = 0;
  if (!id.empty()) {
    bool known = false, enabled = true;
    for (const PcInfo::FormatChoice& c : pc.formats) if (c.id == id) known = true, enabled = c.enabled;
    if (!known || !enabled || (pc.applyFormat != nullptr && !pc.applyFormat(r))) { r = before; return false; }
  }
  std::string why;
  if (!RulesValid(r, &why)) { r = before; return false; }
  return true;
}

std::vector<std::string> RoomLines(const RoomRules& r, const std::string& edition) {
  std::vector<std::string> l;
  l.push_back(std::string("Rules: ") + (r.ruleSet == "gen4pokemon" ? (edition == "xg" ? "Gen 4 Pokemon with the classic XG rules" : "Gen 4 Pokemon with the classic XD rules") : r.ruleSet == "gen4" ? "Gen 4 Pokemon and moves" : r.ruleSet == "gen4mt" ? "Gen 4 Pokemon and moves with the modern types" : (edition == "xg" ? "Classic XG" : "Classic XD")));
  if (!r.format.empty()) l.push_back("Format: " + r.format);
  if (!r.rs.empty() || !r.gens.empty() || !r.details.empty()) {
    l.push_back("Rule set: " + RoomRuleSetId(r) + ", Pokemon generations " + RoomGens(r) + (RoomDetails(r).empty() ? "" : ", details " + RoomDetails(r)));
  }
  l.push_back(std::string("Battle: ") + (r.style == "double" ? "double" : "single") + ", physical/special split " + (r.split ? "on" : "off"));
  std::string team = r.team == "preset"   ? "preset teams"
                     : r.team == "save"   ? "you bring a team from one of your saves"
                     : r.team == "free12" ? "free pick from Gen 1-3 Pokemon"
                     : r.team == "free14" ? "free pick from Gen 1-4 Pokemon"
                     : r.team == "draft"  ? "snake draft"
                                          : "random offers";
  l.push_back("Teams: " + team);
  if (r.team == "draft" || r.team == "offer") l.push_back("Draft: " + draft::SettingsDescribe(DraftOf(r)));
  std::string lv = r.level == "asis" ? "as they are" : r.level.compare(0, 4, "all:") == 0 ? "all set to level " + r.level.substr(4) : "capped at level " + r.level.substr(4);
  l.push_back("Levels: " + lv);
  l.push_back(std::string("Items: ") + (r.items == "none" ? "none" : r.items == "bag" ? "your own bag items (not used up in your save)" : "standard kit (Max Potion, Full Heal, Revive)"));
  l.push_back("Time per choice: " + (r.timerSeconds == 0 ? std::string("no limit") : std::to_string(r.timerSeconds) + " seconds"));
  if (r.sleepClause) l.push_back("Sleep clause: a second foe cannot be put to sleep while one of that team is asleep.");
  if (r.preview) l.push_back("Team preview: you see your friend's team, then each of you picks " + std::to_string(r.pick > 0 ? r.pick : 6) + " Pokemon in the order you lead" +
                             (r.previewSeconds > 0 ? " (" + std::to_string(r.previewSeconds) + " seconds)." : "."));
  if (r.bankSeconds > 0) l.push_back("Battle clock: each player has " + std::to_string(r.bankSeconds / 60) + " minutes in all for their choices.");
  l.push_back(std::string("Shadow Pokemon: ") + (!r.shadowAllowed ? "not allowed" : r.shadowObey == "story" ? "allowed, they may refuse orders as in the story" : "allowed, they obey like purified ones"));
  return l;
}

std::string RulesToText(const RoomRules& r) {
  std::ostringstream o;
  o << "rules=" << r.ruleSet << ";items=" << r.items << ";timer=" << r.timerSeconds << ";shadow=" << (r.shadowAllowed ? "on" : "off")
    << ";shadow_obey=" << r.shadowObey << ";level=" << r.level << ";team=" << r.team << ";split=" << (r.split ? "on" : "off") << ";style=" << r.style;
  if (!r.draft.empty()) o << ";draft=" << r.draft;
  if (!r.rs.empty()) o << ";rs=" << r.rs;
  if (!r.gens.empty()) o << ";gens=" << r.gens;
  if (!r.details.empty()) o << ";details=" << r.details;
  if (!r.format.empty()) o << ";format=" << r.format << ";fmthash=" << r.formatHash;
  if (r.sleepClause) o << ";sleep_clause=on";
  if (r.preview) o << ";preview=on;pick=" << r.pick << ";preview_timer=" << r.previewSeconds;
  if (r.bankSeconds > 0) o << ";bank=" << r.bankSeconds;
  return o.str();
}

std::string Msg(const std::string& verb, const std::string& arg) { return "L1 " + verb + (arg.empty() ? "" : " " + arg); }

bool Apply(Peer& p, const std::string& line) {
  if (line.compare(0, 3, "L1 ") != 0) return false;
  const std::string rest = line.substr(3);
  const size_t sp = rest.find(' ');
  const std::string verb = rest.substr(0, sp);
  const std::string arg = sp == std::string::npos ? "" : rest.substr(sp + 1);
  if (verb == "ACCEPT") p.accepted = true, p.declined = false;
  else if (verb == "DECLINE") p.declined = true, p.accepted = false;
  else if (verb == "TEAM") p.teamCount = std::atoi(arg.c_str());
  else if (verb == "READY") p.ready = true, p.nonce = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 16));
  else if (verb == "UNREADY") p.ready = false;
  else if (verb == "REMATCH") p.rematch = true, p.nonce = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 16));
  else if (verb == "LEAVE") p.left = true;
  else if (verb == "PV") p.pvTeam = true, p.pvIds = SplitInts(arg);
  else if (verb == "PVC") p.pvCommit = true, p.pvCommitHex = arg;
  else if (verb == "PVR") {
    const size_t sp2 = arg.find(' ');
    p.pvReveal = true;
    p.pvNonce = arg.substr(0, sp2);
    p.pvList = sp2 == std::string::npos ? std::string() : arg.substr(sp2 + 1);
  }
  return true;
}

std::string JoinInts(const std::vector<int>& v) {
  std::string out;
  for (int n : v) out += (out.empty() ? "" : ",") + std::to_string(n);
  return out;
}
std::vector<int> SplitInts(const std::string& text) {
  std::vector<int> out;
  std::string cur;
  for (char c : text + ",") {
    if (c == ',') { if (!cur.empty()) out.push_back(std::atoi(cur.c_str())); cur.clear(); }
    else if (c >= '0' && c <= '9') cur += c;
  }
  return out;
}
bool PickValid(const std::vector<int>& pick, int teamSize, int want) {
  if (static_cast<int>(pick.size()) != want || want < 1) return false;
  for (size_t i = 0; i < pick.size(); i++) {
    if (pick[i] < 0 || pick[i] >= teamSize) return false;
    for (size_t j = 0; j < i; j++) if (pick[j] == pick[i]) return false;
  }
  return true;
}
std::vector<int> TimeoutPick(const std::vector<int>& chosen, int teamSize, int want) {
  std::vector<int> out;
  for (int c : chosen) if (static_cast<int>(out.size()) < want && c >= 0 && c < teamSize && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  for (int i = 0; i < teamSize && static_cast<int>(out.size()) < want; i++) if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
  return out;
}

Outcome OutcomeOf(unsigned result, bool host, bool desync) {
  if (desync) return Outcome::Desync;
  switch (result) {
  case 2: return host ? Outcome::Won : Outcome::Lost;
  case 3: return host ? Outcome::Lost : Outcome::Won;
  case 4: return host ? Outcome::YouQuit : Outcome::FriendQuit;
  case 5: return host ? Outcome::FriendQuit : Outcome::YouQuit;
  case 6: case 7: return Outcome::Draw;
  default: return Outcome::ConnectionLost;
  }
}

std::string OutcomeText(Outcome o) {
  switch (o) {
  case Outcome::Won: return "You won the battle!";
  case Outcome::Lost: return "You lost the battle.";
  case Outcome::YouQuit: return "You gave in. Your friend wins.";
  case Outcome::FriendQuit: return "Your friend gave in. You win!";
  case Outcome::ConnectionLost: return "The connection to your friend was lost. The battle ended without a winner.";
  case Outcome::Desync: return "The two games stopped agreeing about the battle, so it was ended. Nobody won.";
  case Outcome::Draw: return "The battle ended in a draw.";
  }
  return "";
}

}
