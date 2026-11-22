#include "online_team.h"
#include "xd_rulesets.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <cstdio>
#include <cstdlib>

namespace xd::online {

std::string EncodeTeam(const std::vector<xd::save::Pokemon>& team) {
  static const char* hex = "0123456789abcdef";
  std::string out = "T " + std::to_string(team.size()) + " ";
  for (const xd::save::Pokemon& p : team) {
    for (uint8_t b : p.raw) {
      out += hex[b >> 4];
      out += hex[b & 15];
    }
  }
  return out;
}

bool DecodeTeam(const std::string& line, std::vector<xd::save::Pokemon>& out, std::string* why) {
  auto fail = [&](const char* m) {
    if (why) *why = m;
    return false;
  };
  if (line.size() < 4 || line[0] != 'T' || line[1] != ' ') return fail("not a team message");
  const size_t sp = line.find(' ', 2);
  if (sp == std::string::npos) return fail("not a team message");
  const int count = std::atoi(line.substr(2, sp - 2).c_str());
  if (count < 0 || static_cast<size_t>(count) > kMaxTeam) return fail("a team has 0 to 6 Pokemon");
  const std::string hex = line.substr(sp + 1);
  if (hex.size() != static_cast<size_t>(count) * xd::save::kPokemonSize * 2) return fail("the team message has the wrong length");
  std::vector<xd::save::Pokemon> team;
  for (int i = 0; i < count; i++) {
    uint8_t raw[xd::save::kPokemonSize];
    for (size_t k = 0; k < xd::save::kPokemonSize; k++) {
      const char a = hex[(static_cast<size_t>(i) * xd::save::kPokemonSize + k) * 2], b = hex[(static_cast<size_t>(i) * xd::save::kPokemonSize + k) * 2 + 1];
      auto val = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
      if (val(a) < 0 || val(b) < 0) return fail("the team message is not hexadecimal");
      raw[k] = static_cast<uint8_t>(val(a) * 16 + val(b));
    }
    team.push_back(xd::save::ParsePokemon(raw, xd::save::ByteOrder::PC));
  }
  out = team;
  return true;
}

std::vector<std::string> CheckTeam(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room) {
  std::vector<std::string> out;
  size_t members = 0;
  for (const xd::save::Pokemon& p : team) {
    if (!p.IsEmpty()) members++;
  }
  if (members == 0) out.push_back("The team has no Pokemon.");
  if (team.size() > kMaxTeam) out.push_back("A team has at most 6 Pokemon.");
  int slot = 0;
  for (const xd::save::Pokemon& p : team) {
    slot++;
    if (p.IsEmpty()) continue;
    xd::legality::Context context;
    context.shadowStaysInItsSave = true;
    const xd::legality::Result r = xd::legality::Check(p, rules, xd::legality::Level::Safe, context);
    if (!r.Ok()) out.push_back("Pokemon " + std::to_string(slot) + ": " + r.Text());
    if (p.shadowId != 0 && !room.shadowAllowed) out.push_back("Pokemon " + std::to_string(slot) + " is a Shadow Pokemon; this room does not allow them.");
  }
  if (!room.format.empty()) {
    for (const std::string& line : CheckTeamFormat(team, rules, room)) out.push_back(line);
  }
  return out;
}

bool ApplyFormat(RoomRules& r, std::string* error) {
  const engine::rulesets::Format* f = xd::rulesets::FindFormat(r.format);
  if (f == nullptr) {
    if (error) *error = "no format \"" + r.format + "\"";
    return false;
  }
  const std::string rs = f->ruleSet.empty() ? RoomRuleSetId(r) : f->ruleSet;
  std::string gens = RoomGens(r);
  if (!f->gens.empty()) {
    gens.clear();
    for (int g : f->gens) gens += (gens.empty() ? "" : ",") + std::to_string(g);
  }
  std::string details;
  for (const auto& kv : f->details) {
    if (kv.first == "split") r.split = kv.second == "on";
    else details += (details.empty() ? "" : ",") + kv.first + ":" + kv.second;
  }
  SetRuleChoice(r, rs, gens, details);
  r.level = f->level.kind == engine::rulesets::LevelRule::Kind::Range ? "cap:" + std::to_string(f->level.b) : f->level.Text();
  const int allowed[] = {0, 30, 60, 90, 120};
  int best = 0;
  for (int a : allowed) if (std::abs(a - f->timerPerChoice) <= std::abs(best - f->timerPerChoice)) best = a;
  r.timerSeconds = best;
  r.style = f->style;
  r.formatHash = xd::rulesets::FormatDigest(*f);
  r.sleepClause = f->clauses.count(engine::rulesets::kClauseSleep) > 0;
  r.preview = f->teamPreview;
  r.pick = f->teamPreview ? std::min(std::max(f->pick, 0), 6) : 0;
  r.previewSeconds = f->teamPreview ? f->timerPreview : 0;
  r.bankSeconds = f->timerPerBattle;
  return RulesValid(r, error);
}

namespace {
struct FormatView {
  const engine::rulesets::Format* format = nullptr;
  std::vector<engine::rulesets::TeamMember> members;
  engine::rulesets::ClauseData data;
  std::vector<int> gens;
};
FormatView ViewOf(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room) {
  FormatView v;
  v.format = xd::rulesets::FindFormat(room.format);
  if (v.format == nullptr) return v;
  const auto national = [&](uint16_t species) -> int {
    if (species < rules.species.size() && rules.species[species].national != 0) return rules.species[species].national;
    return species;
  };
  for (const xd::save::Pokemon& p : team) {
    if (p.IsEmpty()) continue;
    engine::rulesets::TeamMember m;
    m.species = national(p.species);
    m.level = p.level;
    m.item = p.heldItem;
    for (size_t i = 0; i < 4; i++) m.moves[i] = p.moves[i].id;
    v.members.push_back(m);
  }
  v.data = xd::rulesets::ClauseDataFor(rules.Gen4On());
  v.data.name = [&rules](const char* kind, int id) -> std::string {
    if (std::strcmp(kind, "move") == 0 && rules.moveName) return rules.moveName(static_cast<uint16_t>(id));
    if (std::strcmp(kind, "item") == 0 && rules.itemName) return rules.itemName(static_cast<uint16_t>(id));
    return std::string();
  };
  std::string cur;
  for (char c : RoomGens(room) + ",") {
    if (c == ',') { if (!cur.empty()) v.gens.push_back(std::atoi(cur.c_str())); cur.clear(); }
    else cur += c;
  }
  return v;
}
}

std::vector<engine::rulesets::MemberProblem> CheckTeamMembers(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room) {
  const FormatView v = ViewOf(team, rules, room);
  if (v.format == nullptr) return {};
  engine::rulesets::Format format = *v.format;
  if (format.gens.empty()) format.gens = v.gens;
  return engine::rulesets::TeamProblems(format, v.members, v.data, v.gens, [](int n) { return xd::rulesets::GenerationOfNational(n); });
}

std::vector<std::string> CheckTeamFormat(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room) {
  std::vector<std::string> out;
  const engine::rulesets::Format* f = xd::rulesets::FindFormat(room.format);
  if (f == nullptr) {
    out.push_back("This game does not have the format \"" + room.format + "\".");
    return out;
  }
  const auto national = [&](uint16_t species) -> int {
    if (species < rules.species.size() && rules.species[species].national != 0) return rules.species[species].national;
    return species;
  };
  std::vector<engine::rulesets::TeamMember> members;
  for (const xd::save::Pokemon& p : team) {
    if (p.IsEmpty()) continue;
    engine::rulesets::TeamMember m;
    m.species = national(p.species);
    m.level = p.level;
    m.item = p.heldItem;
    for (size_t i = 0; i < 4; i++) m.moves[i] = p.moves[i].id;
    members.push_back(m);
  }
  engine::rulesets::ClauseData data = xd::rulesets::ClauseDataFor(rules.Gen4On());
  data.name = [&](const char* kind, int id) -> std::string {
    if (std::strcmp(kind, "move") == 0 && rules.moveName) return rules.moveName(static_cast<uint16_t>(id));
    if (std::strcmp(kind, "item") == 0 && rules.itemName) return rules.itemName(static_cast<uint16_t>(id));
    return std::string();
  };
  std::vector<int> gens;
  for (const std::string& g : [&] { std::vector<std::string> v; std::string cur; for (char c : RoomGens(room)) { if (c == ',') { v.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) v.push_back(cur); return v; }()) gens.push_back(std::atoi(g.c_str()));
  engine::rulesets::Format format = *f;
  if (format.gens.empty()) format.gens = gens;
  for (const std::string& line : engine::rulesets::CheckTeam(format, members, data, gens, [](int n) { return xd::rulesets::GenerationOfNational(n); })) out.push_back(line);
  return out;
}

}
