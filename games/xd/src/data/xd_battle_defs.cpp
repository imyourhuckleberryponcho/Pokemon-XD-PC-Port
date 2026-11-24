#include "xd_battle_defs.h"

#include "json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace xd::battle {

namespace js = engine::mods::json;

int StatNumber(const std::string& w) {
  if (w == "best") return 8;
  static const std::pair<const char*, int> k[] = {{"atk", 1}, {"attack", 1}, {"def", 2}, {"defense", 2}, {"spe", 3}, {"speed", 3}, {"spa", 4},
                                                  {"spatk", 4}, {"spd", 5}, {"spdef", 5}, {"acc", 6}, {"accuracy", 6}, {"eva", 7}, {"evasion", 7}};
  for (const auto& p : k) if (w == p.first) return p.second;
  return 0;
}
int StatusCode(const std::string& w) {
  static const std::pair<const char*, int> k[] = {{"sleep", 1}, {"poison", 2}, {"burn", 3}, {"freeze", 4}, {"paralysis", 5}, {"toxic", 6},
                                                  {"bad_poison", 6}, {"confusion", 7}, {"confuse", 7}, {"flinch", 8}, {"trap", 0x20}};
  for (const auto& p : k) if (w == p.first) return p.second;
  return 0;
}
int WeatherNumber(const std::string& w) {
  if (w == "sun") return 1;
  if (w == "rain") return 2;
  if (w == "sand" || w == "sandstorm") return 3;
  if (w == "hail") return 4;
  return 0;
}

namespace {

struct Ctx {
  const std::string& mod;
  const std::string& file;
  const Resolver& resolve;
  Defs& defs;
  void Problem(size_t line, const std::string& what) {
    defs.problems.push_back(mod + ": " + file + " line " + std::to_string(line) + ": " + what);
  }
};

int FieldIndex(Defs& d, const std::string& name) {
  for (size_t i = 0; i < d.fieldNames.size(); i++) if (d.fieldNames[i] == name) return static_cast<int>(i);
  d.fieldNames.push_back(name);
  return static_cast<int>(d.fieldNames.size() - 1);
}

bool Str(const js::Value& v, std::string& out) {
  if (v.kind != js::Value::Kind::String) return false;
  out = v.text;
  return true;
}

bool TypeValue(Ctx& c, const js::Value& v, int64_t& out) {
  if (v.kind == js::Value::Kind::Number) { out = static_cast<int64_t>(v.number); return out >= 0 && out <= 18; }
  return v.kind == js::Value::Kind::String && c.resolve("type", v.text, out);
}

bool ParseCond(Ctx& c, const js::Value& v, Condition& out) {
  if (v.kind == js::Value::Kind::Object) {
    const auto it = v.fields.find("move_in");
    if (it == v.fields.end() || it->second.kind != js::Value::Kind::Array) { c.Problem(v.line, "a condition is text, or { \"move_in\": [move names] }"); return false; }
    out.kind = Cond::MoveInList;
    for (const auto& m : it->second.items) {
      int64_t id = 0;
      if (m.kind != js::Value::Kind::String) { c.Problem(m.line, "move_in holds move names"); return false; }
      if (!c.resolve("move", m.text, id)) continue;
      out.list.push_back(static_cast<int>(id));
    }
    return true;
  }
  std::string s;
  if (!Str(v, s)) { c.Problem(v.line, "a condition must be text like \"full_hp\""); return false; }
  if (!s.empty() && s[0] == '!') { out.negate = true; s = s.substr(1); }
  std::string arg;
  const size_t colon = s.find(':');
  if (colon != std::string::npos) { arg = s.substr(colon + 1); s = s.substr(0, colon); }
  static const std::pair<const char*, Cond> plain[] = {
      {"full_hp", Cond::FullHp}, {"not_full_hp", Cond::NotFullHp}, {"hp_below_half", Cond::HpBelowHalf}, {"hp_above_half", Cond::HpAboveHalf},
      {"hp_below_third", Cond::HpBelowThird}, {"target_full_hp", Cond::TargetFullHp}, {"target_hp_below_half", Cond::TargetHpBelowHalf},
      {"status_move", Cond::StatusMove}, {"damaging_move", Cond::DamagingMove}, {"contact", Cond::Contact}, {"physical", Cond::Physical},
      {"special", Cond::Special}, {"has_status", Cond::HasStatus}, {"no_status", Cond::NoStatus}, {"target_has_status", Cond::TargetHasStatus},
      {"target_no_status", Cond::TargetNoStatus}, {"user_has_item", Cond::UserHasItem}, {"user_no_item", Cond::UserNoItem},
      {"target_has_item", Cond::TargetHasItem}, {"target_no_item", Cond::TargetNoItem}, {"first_turn", Cond::FirstTurn},
      {"move_has_secondary", Cond::MoveHasSecondary}, {"super_effective", Cond::SuperEffective},
      {"not_very_effective", Cond::NotVeryEffective}, {"sound", Cond::Sound}, {"punch", Cond::Punch}};
  for (const auto& p : plain) {
    if (s == p.first && colon == std::string::npos) { out.kind = p.second; return true; }
  }
  auto typeArg = [&](Cond kind) {
    int64_t t = 0;
    if (!c.resolve("type", arg, t)) { c.Problem(v.line, "unknown type \"" + arg + "\" in \"" + s + ":" + arg + "\""); return false; }
    out.kind = kind;
    out.arg = static_cast<int32_t>(t);
    return true;
  };
  if (s == "type" && colon != std::string::npos) return typeArg(Cond::MoveType);
  if (s == "user_type" && colon != std::string::npos) return typeArg(Cond::UserType);
  if (s == "target_type" && colon != std::string::npos) return typeArg(Cond::TargetType);
  if (s == "weather" && colon != std::string::npos) {
    out.kind = Cond::Weather;
    out.arg = arg == "none" ? 0 : arg == "any" ? 9 : WeatherNumber(arg);
    if (out.arg == 0 && arg != "none") { c.Problem(v.line, "unknown weather \"" + arg + "\" (sun, rain, sand, hail, none, any)"); return false; }
    return true;
  }
  if (s == "move" && colon != std::string::npos) {
    int64_t m = 0;
    if (!c.resolve("move", arg, m)) { c.Problem(v.line, "unknown move \"" + arg + "\""); return false; }
    out.kind = Cond::MoveIs;
    out.arg = static_cast<int32_t>(m);
    return true;
  }
  if ((s == "target_status" || s == "user_status") && colon != std::string::npos) {
    static const std::pair<const char*, int> kinds[] = {{"poison", 3}, {"paralysis", 5}, {"burn", 6}, {"freeze", 7}, {"sleep", 8}};
    out.arg = 0;
    for (const auto& k : kinds) if (arg == k.first) out.arg = k.second;
    if (out.arg == 0) { c.Problem(v.line, "unknown status \"" + arg + "\" (poison, paralysis, burn, freeze, sleep)"); return false; }
    out.kind = s == "target_status" ? Cond::TargetStatusKind : Cond::UserStatusKind;
    return true;
  }
  if (s == "terrain" && colon != std::string::npos) {
    out.kind = Cond::Terrain;
    out.arg = arg == "grassy" ? 1 : arg == "electric" ? 2 : arg == "psychic" ? 3 : arg == "misty" ? 4 : arg == "any" ? 9 : 0;
    if (out.arg == 0) { c.Problem(v.line, "unknown terrain \"" + arg + "\" (grassy, electric, psychic, misty, any)"); return false; }
    return true;
  }
  if (s == "move_in" && colon == std::string::npos) { c.Problem(v.line, "\"move_in\" is written as { \"move_in\": [names] } inside a rule"); return false; }
  if (s == "hp_crossed_half" && colon == std::string::npos) { out.kind = Cond::HpCrossedHalf; return true; }
  if (s == "slower_than_target" && colon == std::string::npos) { out.kind = Cond::SlowerThanTarget; return true; }
  if (s == "target_fainted" && colon == std::string::npos) { out.kind = Cond::TargetFainted; return true; }
  if (s == "target_first_turn" && colon == std::string::npos) { out.kind = Cond::TargetFirstTurn; return true; }
  if (s == "target_grounded" && colon == std::string::npos) { out.kind = Cond::TargetGrounded; return true; }
  if (s == "user_grounded" && colon == std::string::npos) { out.kind = Cond::UserGrounded; return true; }
  if (s == "field" && colon != std::string::npos) { out.kind = Cond::Field; out.arg = FieldIndex(c.defs, arg); return true; }
  c.Problem(v.line, "unknown condition \"" + s + (colon != std::string::npos ? ":" + arg : std::string()) + "\"");
  return false;
}

bool ParseWhen(Ctx& c, const js::Value* v, std::vector<Condition>& out) {
  if (v == nullptr) return true;
  bool ok = true;
  auto one = [&](const js::Value& x) { Condition cd; if (ParseCond(c, x, cd)) out.push_back(cd); else ok = false; };
  if (v->kind == js::Value::Kind::Array) for (const auto& x : v->items) one(x);
  else one(*v);
  return ok;
}

const js::Value* Get(const js::Value& o, const char* key) {
  auto it = o.fields.find(key);
  return it == o.fields.end() ? nullptr : &it->second;
}

bool Num(const js::Value* v, double& out) {
  if (v == nullptr || v->kind != js::Value::Kind::Number) return false;
  out = v->number;
  return true;
}

bool ParseBlock(Ctx& c, const js::Value& v, Block& b, bool moveContext) {
  if (v.kind != js::Value::Kind::Object) { c.Problem(v.line, "an effect must be an object like {\"stat\": \"atk\", \"stages\": 1}"); return false; }
  b.line = static_cast<int>(v.line);
  static const char* kinds[] = {"stat", "status", "heal", "hurt", "recoil", "drain", "switch_out", "force_switch", "weather", "remove_item", "message",
                                "set_type", "field", "set_ability", "terrain", "cure_status", "hurt_dealt", "moody", "side", "steal_item", "restore_berry", "transform"};
  int found = 0;
  std::string kindKey;
  for (const char* k : kinds) if (Get(v, k)) { found++; if (kindKey.empty() || kindKey == "message") kindKey = k; }
  if (found == 0) { c.Problem(v.line, "an effect needs one of: stat, status, heal, hurt, recoil, drain, switch_out, force_switch, weather, remove_item, message, set_type, field, set_ability, terrain, cure_status"); return false; }
  if (found > 1 && !(Get(v, "message") && found == 2)) { c.Problem(v.line, "an effect holds only one action (put the others in their own { }); \"message\" may go with it"); return false; }
  static const std::set<std::string> known = {"stat", "stages", "status", "heal", "hurt", "recoil", "drain", "switch_out", "force_switch", "weather", "remove_item",
                                              "message", "set_type", "field", "set_ability", "terrain", "cure_status", "hurt_dealt", "moody", "side", "steal_item", "restore_berry", "transform", "turns", "end_message", "target", "chance", "when"};
  for (const auto& k : v.order) if (!known.count(k)) { c.Problem(v.line, "unknown key \"" + k + "\" in an effect"); return false; }
  std::string t;
  if (const js::Value* tv = Get(v, "target")) {
    if (!Str(*tv, t) || (t != "user" && t != "target" && t != "foes")) { c.Problem(v.line, "\"target\" is \"user\", \"target\" or \"foes\""); return false; }
    b.onUser = t == "user";
    b.onFoes = t == "foes";
  } else if (moveContext && (kindKey == "status" || kindKey == "remove_item" || kindKey == "force_switch")) {
    b.onUser = false;
  }
  double n = 0;
  if (const js::Value* cv = Get(v, "chance")) {
    if (!Num(cv, n) || n < 1 || n > 100) { c.Problem(v.line, "\"chance\" is a number from 1 to 100"); return false; }
    b.chance = static_cast<int>(n);
  }
  if (!ParseWhen(c, Get(v, "when"), b.when)) return false;
  if (const js::Value* m = Get(v, "message")) {
    if (!Str(*m, b.message) || b.message.size() > 120) { c.Problem(v.line, "\"message\" is text of at most 120 letters"); return false; }
  }
  if (kindKey == "stat") {
    std::string s;
    if (!Str(*Get(v, "stat"), s) || StatNumber(s) == 0) { c.Problem(v.line, "unknown stat (atk, def, spe, spa, spd, acc, eva)"); return false; }
    if (!Num(Get(v, "stages"), n) || n == 0 || std::fabs(n) > 6 || n != std::floor(n)) { c.Problem(v.line, "\"stages\" is a whole number from -6 to 6 (not 0)"); return false; }
    b.kind = BlockKind::Stat; b.stat = StatNumber(s); b.stages = static_cast<int>(n);
  } else if (kindKey == "status") {
    std::string s;
    if (!Str(*Get(v, "status"), s) || StatusCode(s) == 0) { c.Problem(v.line, "unknown status (sleep, poison, toxic, burn, freeze, paralysis, confusion, flinch)"); return false; }
    b.kind = BlockKind::Status; b.code = StatusCode(s);
  } else if (kindKey == "heal" || kindKey == "hurt" || kindKey == "recoil" || kindKey == "drain") {
    if (!Num(Get(v, kindKey.c_str()), n) || n <= 0 || n > 100) { c.Problem(v.line, "\"" + kindKey + "\" is a percent from 1 to 100"); return false; }
    b.kind = kindKey == "heal" ? BlockKind::Heal : kindKey == "hurt" ? BlockKind::Hurt : kindKey == "recoil" ? BlockKind::Recoil : BlockKind::Drain;
    b.permille = static_cast<int>(std::lround(n * 10));
    if (!moveContext && (b.kind == BlockKind::Recoil || b.kind == BlockKind::Drain)) { c.Problem(v.line, "\"" + kindKey + "\" works for moves only"); return false; }
  } else if (kindKey == "switch_out" || kindKey == "force_switch") {
    if (!moveContext) { c.Problem(v.line, "\"" + kindKey + "\" works for moves only"); return false; }
    b.kind = kindKey == "switch_out" ? BlockKind::SwitchOut : BlockKind::ForceSwitch;
  } else if (kindKey == "weather") {
    std::string s;
    if (!Str(*Get(v, "weather"), s) || WeatherNumber(s) == 0) { c.Problem(v.line, "unknown weather (sun, rain, sand, hail)"); return false; }
    b.kind = BlockKind::Weather; b.weather = WeatherNumber(s);
  } else if (kindKey == "remove_item") {
    b.kind = BlockKind::RemoveItem;
  } else if (kindKey == "message") {
    b.kind = BlockKind::Message;
    if (b.message.empty()) { c.Problem(v.line, "a message block needs words"); return false; }
  } else if (kindKey == "set_type") {
    std::string s;
    if (!Str(*Get(v, "set_type"), s)) { c.Problem(v.line, "\"set_type\" is \"move\" or a type name"); return false; }
    b.kind = BlockKind::SetType;
    if (s == "move") b.type = -1;
    else { int64_t tt = 0; if (!c.resolve("type", s, tt)) { c.Problem(v.line, "unknown type \"" + s + "\""); return false; } b.type = static_cast<int>(tt); }
  } else if (kindKey == "set_ability") {
    std::string s;
    int64_t id = 0;
    const js::Value* av = Get(v, "set_ability");
    if (av->kind == js::Value::Kind::Number) id = static_cast<int64_t>(av->number);
    else if (!Str(*av, s) || !c.resolve("ability", s, id)) { c.Problem(v.line, "unknown ability in \"set_ability\""); return false; }
    b.kind = BlockKind::SetAbility; b.ability = static_cast<int>(id);
  } else if (kindKey == "terrain") {
    std::string s;
    if (!Str(*Get(v, "terrain"), s)) { c.Problem(v.line, "\"terrain\" is grassy, electric, psychic or misty"); return false; }
    b.kind = BlockKind::Terrain;
    b.field = s == "grassy" ? 1 : s == "electric" ? 2 : s == "psychic" ? 3 : s == "misty" ? 4 : 0;
    if (b.field == 0) { c.Problem(v.line, "unknown terrain (grassy, electric, psychic, misty)"); return false; }
  } else if (kindKey == "cure_status") {
    b.kind = BlockKind::CureStatus;
  } else if (kindKey == "side") {
    std::string s;
    if (!Str(*Get(v, "side"), s) || (s != "aurora_veil" && s != "sticky_web")) { c.Problem(v.line, "\"side\" is aurora_veil or sticky_web"); return false; }
    b.kind = BlockKind::Side; b.field = s == "aurora_veil" ? 1 : 2;
  } else if (kindKey == "steal_item") {
    b.kind = BlockKind::StealItem;
    if (b.message.empty()) b.message = "{name} stole an item!";
  } else if (kindKey == "moody") {
    b.kind = BlockKind::Moody;
  } else if (kindKey == "restore_berry") {
    b.kind = BlockKind::RestoreBerry;
  } else if (kindKey == "transform") {
    b.kind = BlockKind::Transform; b.onUser = false;
  } else if (kindKey == "hurt_dealt") {
    if (!Num(Get(v, "hurt_dealt"), n) || n <= 0 || n > 100) { c.Problem(v.line, "\"hurt_dealt\" is a percent from 1 to 100"); return false; }
    b.kind = BlockKind::HurtDealt; b.permille = static_cast<int>(std::lround(n * 10));
  } else if (kindKey == "field") {
    std::string s;
    if (!Str(*Get(v, "field"), s) || s.empty()) { c.Problem(v.line, "\"field\" is a name you choose"); return false; }
    if (!Num(Get(v, "turns"), n) || n < 1 || n > 10) { c.Problem(v.line, "a field needs \"turns\" from 1 to 10"); return false; }
    b.kind = BlockKind::Field; b.field = FieldIndex(c.defs, s); b.turns = static_cast<int>(n);
    if (const js::Value* e = Get(v, "end_message")) Str(*e, b.endMessage);
  }
  return true;
}

bool ParseBlocks(Ctx& c, const js::Value* v, std::vector<Block>& out, bool moveContext) {
  if (v == nullptr) return true;
  if (v->kind != js::Value::Kind::Array) { c.Problem(v->line, "this must be a list [ ... ] of effects"); return false; }
  if (v->items.size() > 16) { c.Problem(v->line, "at most 16 effects in one list"); return false; }
  bool ok = true;
  for (const auto& x : v->items) { Block b; if (ParseBlock(c, x, b, moveContext)) out.push_back(b); else ok = false; }
  return ok;
}

bool ParseRules(Ctx& c, const js::Value* v, std::vector<PowerRule>& out, const char* what) {
  if (v == nullptr) return true;
  if (v->kind != js::Value::Kind::Array) { c.Problem(v->line, std::string("\"") + what + "\" is a list [ ... ]"); return false; }
  bool ok = true;
  for (const auto& x : v->items) {
    if (x.kind != js::Value::Kind::Object) { c.Problem(x.line, "a rule is an object { \"when\": ..., \"mult\": 2 }"); ok = false; continue; }
    PowerRule r;
    double n = 0;
    if (!ParseWhen(c, Get(x, "when"), r.when)) { ok = false; continue; }
    bool any = false;
    if (Num(Get(x, "mult"), n)) {
      if (n <= 0 || n > 10) { c.Problem(x.line, "\"mult\" is from 0.1 to 10"); ok = false; continue; }
      r.percent = static_cast<int>(std::lround(n * 100)); any = true;
    }
    if (Num(Get(x, "accuracy"), n)) {
      if (n < 1 || n > 255) { c.Problem(x.line, "\"accuracy\" is from 1 to 100 (255: never misses)"); ok = false; continue; }
      r.accuracy = static_cast<int>(n); any = true;
    }
    if (Num(Get(x, "add"), n)) {
      if (std::fabs(n) > 7) { c.Problem(x.line, "\"add\" (priority) is from -7 to 7"); ok = false; continue; }
      r.add = static_cast<int>(n); any = true;
    }
    if (!any) { c.Problem(x.line, "a rule needs \"mult\", \"accuracy\" or \"add\""); ok = false; continue; }
    out.push_back(r);
  }
  return ok;
}

void ParseMove(Ctx& c, const std::string& name, const js::Value& v) {
  MoveDef m;
  m.name = name; m.mod = c.mod; m.line = static_cast<int>(v.line);
  if (v.kind != js::Value::Kind::Object) { c.Problem(v.line, "the move \"" + name + "\" must be an object"); return; }
  int64_t id = 0;
  if (!c.resolve("move", name, id)) { c.Problem(v.line, "no move called \"" + name + "\" (add it in a data file first)"); return; }
  m.id = id;
  static const std::set<std::string> known = {"effects", "power_rules", "accuracy_rules", "script", "_note", "crit", "ignore_boosts", "use_target_attack", "defend_stat", "first_turn_only", "power_formula", "never_miss", "protect", "charge", "needs_physical_hit", "heats_up", "type_from_user"};
  for (const auto& k : v.order) if (!known.count(k)) { c.Problem(v.line, "unknown key \"" + k + "\" in the move \"" + name + "\""); return; }
  bool ok = ParseBlocks(c, Get(v, "effects"), m.effects, true);
  ok = ParseRules(c, Get(v, "power_rules"), m.power, "power_rules") && ok;
  ok = ParseRules(c, Get(v, "accuracy_rules"), m.accuracy, "accuracy_rules") && ok;
  for (const auto& r : m.accuracy) if (r.accuracy < 0) { c.Problem(v.line, "accuracy_rules need \"accuracy\""); ok = false; }
  for (const auto& r : m.power) if (r.accuracy >= 0 || r.add != 0) { c.Problem(v.line, "power_rules take only \"mult\""); ok = false; }
  if (const js::Value* x = Get(v, "crit")) {
    if (x->kind == js::Value::Kind::String && x->text == "always") m.crit = 1;
    else { c.Problem(v.line, "\"crit\" is \"always\""); ok = false; }
  }
  auto flag = [&](const char* key, bool& into) { if (const js::Value* x = Get(v, key)) into = x->kind == js::Value::Kind::Bool && x->boolean; };
  flag("ignore_boosts", m.ignoreBoosts); flag("use_target_attack", m.useTargetAttack); flag("first_turn_only", m.firstTurnOnly); flag("never_miss", m.neverMiss); flag("needs_physical_hit", m.needsPhysicalHit); flag("heats_up", m.heatsUp); flag("type_from_user", m.typeFromUser);
  if (const js::Value* x = Get(v, "defend_stat")) { m.defendWithDef = x->kind == js::Value::Kind::String && x->text == "def"; if (!m.defendWithDef) { c.Problem(v.line, "\"defend_stat\" is \"def\""); ok = false; } }
  if (const js::Value* x = Get(v, "protect")) {
    m.protectKind = x->kind == js::Value::Kind::String ? (x->text == "kings_shield" ? 1 : x->text == "spiky_shield" ? 2 : x->text == "baneful_bunker" ? 3 : 0) : 0;
    if (m.protectKind == 0) { c.Problem(v.line, "\"protect\" is kings_shield, spiky_shield or baneful_bunker"); ok = false; }
  }
  if (const js::Value* x = Get(v, "power_formula")) {
    m.powerFormula = x->kind == js::Value::Kind::String ? (x->text == "weight" ? 1 : x->text == "speed" ? 2 : x->text == "boosts" ? 3 : 0) : 0;
    if (m.powerFormula == 0) { c.Problem(v.line, "\"power_formula\" is weight, speed or boosts"); ok = false; }
  }
  if (const js::Value* s = Get(v, "script")) m.script = s->kind == js::Value::Kind::Bool && s->boolean;
  if (const js::Value* x = Get(v, "charge")) {
    std::string donor; int64_t did = 0;
    if (x->kind != js::Value::Kind::Object || !Get(*x, "donor") || !Str(*Get(*x, "donor"), donor) || !c.resolve("move", donor, did)) { c.Problem(x->line, "\"charge\" is { \"donor\": \"Razor Wind\", \"message\": \"{name} is charging!\" } (the donor is one of the game's own two-turn moves)"); ok = false; }
    else { m.chargeDonor = did; if (const js::Value* mm = Get(*x, "message")) Str(*mm, m.chargeMessage); if (const js::Value* u = Get(*x, "untouchable")) m.chargeUntouchable = u->kind == js::Value::Kind::Bool && u->boolean; if (const js::Value* u = Get(*x, "carry")) m.chargeCarry = u->kind == js::Value::Kind::Bool && u->boolean; }
  }
  if (ok) c.defs.moves.push_back(std::move(m));
}

void ParseAbility(Ctx& c, const std::string& name, const js::Value& v) {
  AbilityDef a;
  a.name = name; a.mod = c.mod; a.line = static_cast<int>(v.line);
  if (v.kind != js::Value::Kind::Object) { c.Problem(v.line, "the ability \"" + name + "\" must be an object"); return; }
  static const std::set<std::string> known = {"id", "text", "switch_in", "end_of_turn", "ko", "contact", "switch_out", "before_move", "damage_dealt",
                                              "damage_taken", "stat_mods", "absorb", "stat_drop_block", "status_block", "priority", "reflect_status",
                                              "remove_secondary", "script", "acts_as", "_note", "unnerve", "disable_on_hit", "poison_any_type", "strong_wind", "infiltrator", "illusion", "berry_eaten", "half_hp", "type_by_item", "hit", "hit_dealt", "fainted", "stat_dropped", "any_faint", "convert", "aura", "breaks_auras",
                                              "immune_moves", "dancer", "accuracy_against", "accuracy_dealt", "crit_when", "contrary", "blocks_priority", "no_contact"};
  for (const auto& k : v.order) if (!known.count(k)) { c.Problem(v.line, "unknown key \"" + k + "\" in the ability \"" + name + "\""); return; }
  double n = 0;
  if (const js::Value* i = Get(v, "id")) {
    if (!Num(i, n) || (n != 0 && n < 124) || n > 255 || n != std::floor(n)) { c.Problem(v.line, "\"id\" is a whole number from 124 to 255 (0 = the next free number; 77 to 123 belong to the Gen 4 pack)"); return; }
    a.id = static_cast<int>(n);
  }
  if (const js::Value* t = Get(v, "text")) Str(*t, a.text);
  if (a.text.size() > 60) { c.Problem(v.line, "\"text\" is at most 60 letters"); return; }
  bool ok = true;
  ok = ParseBlocks(c, Get(v, "switch_in"), a.switchIn, false) && ok;
  ok = ParseBlocks(c, Get(v, "end_of_turn"), a.endOfTurn, false) && ok;
  ok = ParseBlocks(c, Get(v, "ko"), a.ko, false) && ok;
  ok = ParseBlocks(c, Get(v, "contact"), a.contact, false) && ok;
  ok = ParseBlocks(c, Get(v, "switch_out"), a.switchOut, false) && ok;
  ok = ParseBlocks(c, Get(v, "before_move"), a.beforeMove, false) && ok;
  ok = ParseBlocks(c, Get(v, "hit"), a.hit, false) && ok;
  ok = ParseBlocks(c, Get(v, "hit_dealt"), a.hitDealt, false) && ok;
  ok = ParseBlocks(c, Get(v, "fainted"), a.fainted, false) && ok;
  ok = ParseBlocks(c, Get(v, "stat_dropped"), a.statDropped, false) && ok;
  ok = ParseBlocks(c, Get(v, "any_faint"), a.anyFaint, false) && ok;
  ok = ParseBlocks(c, Get(v, "berry_eaten"), a.berryEaten, false) && ok;
  ok = ParseBlocks(c, Get(v, "half_hp"), a.halfHp, true) && ok;
  if (const js::Value* x = Get(v, "disable_on_hit")) { if (!Num(x, n) || n < 1 || n > 100) { c.Problem(x->line, "\"disable_on_hit\" is a percent from 1 to 100"); ok = false; } else a.disableOnHit = static_cast<int>(n); }
  if (const js::Value* ti = Get(v, "type_by_item")) {
    if (ti->kind != js::Value::Kind::Array) { c.Problem(ti->line, "\"type_by_item\" is a list of { \"item\": \"Fire Memory\", \"type\": \"Fire\" }"); ok = false; }
    else for (const auto& x : ti->items) {
      std::string nm; int64_t iid = 0, t = 0;
      if (x.kind != js::Value::Kind::Object || !Get(x, "item") || !Str(*Get(x, "item"), nm) || !Get(x, "type") || !TypeValue(c, *Get(x, "type"), t)) { c.Problem(x.line, "a type_by_item entry is { \"item\": \"Fire Memory\", \"type\": \"Fire\" }"); ok = false; continue; }
      if (!c.resolve("item", nm, iid)) continue;
      AbilityDef::TypeByItem e; e.item = static_cast<int>(iid); e.type = static_cast<int>(t); a.typeByItem.push_back(e);
    }
  }
  if (const js::Value* dv = Get(v, "dancer")) {
    if (dv->kind != js::Value::Kind::Array) { c.Problem(dv->line, "\"dancer\" is a list of move names: [\"Swords Dance\", \"Dragon Dance\"]"); ok = false; }
    else for (const auto& x : dv->items) {
      std::string nm; int64_t mid = 0;
      if (!Str(x, nm)) { c.Problem(x.line, "\"dancer\" is a list of move names"); ok = false; continue; }
      if (c.resolve("move", nm, mid)) a.dancer.push_back(static_cast<int>(mid));
    }
  }
  ok = ParseRules(c, Get(v, "accuracy_against"), a.accuracyAgainst, "accuracy_against") && ok;
  ok = ParseRules(c, Get(v, "accuracy_dealt"), a.accuracyDealt, "accuracy_dealt") && ok;
  ok = ParseWhen(c, Get(v, "crit_when"), a.critWhen) && ok;
  if (const js::Value* cv = Get(v, "convert")) {
    if (cv->kind != js::Value::Kind::Array) { c.Problem(cv->line, "\"convert\" is a list of { \"from\": \"Normal\", \"to\": \"Ice\", \"mult\": 1.2 }"); ok = false; }
    else for (const auto& x : cv->items) {
      Convert k; std::string s; int64_t t = 0;
      if (x.kind != js::Value::Kind::Object || !Get(x, "to") || !TypeValue(c, *Get(x, "to"), t)) { c.Problem(x.line, "a convert entry needs \"to\": a type"); ok = false; continue; }
      k.to = static_cast<int>(t);
      if (const js::Value* f = Get(x, "from")) { if (!TypeValue(c, *f, t)) { c.Problem(x.line, "unknown type in \"from\""); ok = false; continue; } k.from = static_cast<int>(t); }
      if (Num(Get(x, "mult"), n) && n > 0 && n <= 10) k.percent = static_cast<int>(std::lround(n * 100));
      if (!ParseWhen(c, Get(x, "when"), k.when)) { ok = false; continue; }
      a.convert.push_back(k);
    }
  }
  if (const js::Value* ax = Get(v, "aura")) {
    std::string s; int64_t t = 0;
    if (ax->kind != js::Value::Kind::Object || !Get(*ax, "type") || !TypeValue(c, *Get(*ax, "type"), t)) { c.Problem(ax->line, "\"aura\" is { \"type\": \"Dark\", \"mult\": 1.33 }"); ok = false; }
    else { a.auraType = static_cast<int>(t); if (Num(Get(*ax, "mult"), n) && n > 0 && n <= 10) a.auraPercent = static_cast<int>(std::lround(n * 100)); }
  }
  if (const js::Value* im = Get(v, "immune_moves")) {
    if (im->kind != js::Value::Kind::Array) { c.Problem(im->line, "\"immune_moves\" is a list of move names"); ok = false; }
    else for (const auto& x : im->items) { int64_t id = 0; if (x.kind != js::Value::Kind::String) { c.Problem(x.line, "a move name"); ok = false; continue; } if (!c.resolve("move", x.text, id)) continue; a.immuneMoves.push_back(static_cast<int>(id)); }
  }
  ok = ParseRules(c, Get(v, "damage_dealt"), a.damageDealt, "damage_dealt") && ok;
  ok = ParseRules(c, Get(v, "damage_taken"), a.damageTaken, "damage_taken") && ok;
  ok = ParseRules(c, Get(v, "priority"), a.priority, "priority") && ok;
  if (const js::Value* sm = Get(v, "stat_mods")) {
    if (sm->kind != js::Value::Kind::Array) { c.Problem(sm->line, "\"stat_mods\" is a list"); ok = false; }
    else for (const auto& x : sm->items) {
      StatMod s; std::string st;
      if (x.kind != js::Value::Kind::Object || !Get(x, "stat") || !Str(*Get(x, "stat"), st) || StatNumber(st) == 0 || StatNumber(st) > 5 || !Num(Get(x, "mult"), n) || n <= 0 || n > 10) {
        c.Problem(x.line, "a stat mod is { \"stat\": \"atk\", \"mult\": 2 } (atk, def, spe, spa, spd)"); ok = false; continue;
      }
      s.stat = StatNumber(st); s.percent = static_cast<int>(std::lround(n * 100));
      if (!ParseWhen(c, Get(x, "when"), s.when)) { ok = false; continue; }
      a.statMods.push_back(s);
    }
  }
  if (const js::Value* ab = Get(v, "absorb")) {
    if (ab->kind != js::Value::Kind::Array) { c.Problem(ab->line, "\"absorb\" is a list"); ok = false; }
    else for (const auto& x : ab->items) {
      Absorb o; std::string ty;
      int64_t t = 0;
      if (x.kind != js::Value::Kind::Object || !Get(x, "type") || !Str(*Get(x, "type"), ty) || !c.resolve("type", ty, t)) {
        c.Problem(x.line, "an absorb entry is { \"type\": \"grass\", \"stat\": \"atk\", \"stages\": 1 } or { \"type\": \"water\", \"heal\": 25 }"); ok = false; continue;
      }
      o.type = static_cast<int>(t);
      if (const js::Value* h = Get(x, "heal")) { if (!Num(h, n) || n <= 0 || n > 100) { c.Problem(x.line, "\"heal\" is a percent"); ok = false; continue; } o.healPermille = static_cast<int>(std::lround(n * 10)); }
      if (const js::Value* st = Get(x, "stat")) {
        std::string s;
        if (!Str(*st, s) || StatNumber(s) == 0 || !Num(Get(x, "stages"), n) || n < 1 || n > 6) { c.Problem(x.line, "\"stat\" + \"stages\" (1 to 6)"); ok = false; continue; }
        o.stat = StatNumber(s); o.stages = static_cast<int>(n);
      }
      if (const js::Value* m = Get(x, "message")) Str(*m, o.message);
      a.absorb.push_back(o);
    }
  }
  if (const js::Value* sd = Get(v, "stat_drop_block")) {
    if (sd->kind != js::Value::Kind::Array) { c.Problem(sd->line, "\"stat_drop_block\" is a list of stats, or [\"all\"]"); ok = false; }
    else for (const auto& x : sd->items) { std::string s; if (!Str(x, s) || (s != "all" && StatNumber(s) == 0)) { c.Problem(x.line, "unknown stat"); ok = false; continue; } a.statDropBlock.push_back(s == "all" ? 0 : StatNumber(s)); }
  }
  if (const js::Value* sb = Get(v, "status_block")) {
    if (sb->kind != js::Value::Kind::Array) { c.Problem(sb->line, "\"status_block\" is a list of statuses, or [\"all\"]"); ok = false; }
    else for (const auto& x : sb->items) { std::string s; if (!Str(x, s) || (s != "all" && StatusCode(s) == 0)) { c.Problem(x.line, "unknown status"); ok = false; continue; } a.statusBlock.push_back(s == "all" ? 0 : StatusCode(s)); }
  }
  if (const js::Value* as = Get(v, "acts_as")) {
    std::string nm;
    int64_t id = 0;
    if (!Str(*as, nm) || !c.resolve("ability", nm, id) || id < 1 || id > 76) { c.Problem(as->line, "\"acts_as\" is the name of one of the game's own abilities (numbers 1 to 76)"); ok = false; }
    else a.actsAs = static_cast<int>(id);
  }
  auto flag = [&](const char* k, bool& out) { if (const js::Value* f = Get(v, k)) out = f->kind == js::Value::Kind::Bool && f->boolean; };
  flag("reflect_status", a.reflectStatus);
  flag("contrary", a.contrary);
  flag("no_contact", a.noContact);
  flag("blocks_priority", a.blocksPriority);
  flag("breaks_auras", a.breaksAuras);
  flag("remove_secondary", a.removesSecondary);
  flag("script", a.script);
  flag("unnerve", a.unnerve); flag("poison_any_type", a.poisonAnyType); flag("strong_wind", a.strongWind); flag("infiltrator", a.infiltrator); flag("illusion", a.illusion);
  if (ok) c.defs.abilities.push_back(std::move(a));
}

void ParseHidden(Ctx& c, const js::Value& v) {
  for (const auto& key : v.order) {
    const js::Value& item = v.fields.at(key);
    int64_t id = 0;
    if (item.kind == js::Value::Kind::Number) id = static_cast<int64_t>(item.number);
    else if (item.kind != js::Value::Kind::String || !c.resolve("ability", item.text, id)) { c.Problem(item.line, "the hidden ability of \"" + key + "\" is an ability number or name"); continue; }
    if (id < 1 || id > 255) { c.Problem(item.line, "hidden ability number out of range"); continue; }
    if (!key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) { c.defs.hiddenNat[std::atoi(key.c_str())] = static_cast<int>(id); continue; }
    int64_t row = 0;
    if (!c.resolve("species", key, row)) { c.Problem(item.line, "no species called \"" + key + "\""); continue; }
    c.defs.hiddenRow[static_cast<int>(row)] = static_cast<int>(id);
  }
}

void ParseRuleSet(Ctx& c, const std::string& name, const js::Value& v) {
  RuleSet r;
  r.name = name; r.mod = c.mod;
  if (v.kind != js::Value::Kind::Object) { c.Problem(v.line, "the rule set \"" + name + "\" must be an object"); return; }
  static const std::set<std::string> known = {"level_cap", "level_all", "banned_items", "force_double", "inverse", "weather", "_note"};
  for (const auto& k : v.order) if (!known.count(k)) { c.Problem(v.line, "unknown rule \"" + k + "\" (level_cap, level_all, banned_items, force_double, inverse, weather)"); return; }
  double n = 0;
  if (const js::Value* x = Get(v, "level_cap")) { if (!Num(x, n) || n < 1 || n > 100) { c.Problem(x->line, "\"level_cap\" is 1 to 100"); return; } r.levelCap = static_cast<int>(n); }
  if (const js::Value* x = Get(v, "level_all")) { if (!Num(x, n) || n < 1 || n > 100) { c.Problem(x->line, "\"level_all\" is 1 to 100"); return; } r.levelAll = static_cast<int>(n); }
  if (const js::Value* x = Get(v, "force_double")) r.forceDouble = x->kind == js::Value::Kind::Bool && x->boolean;
  if (const js::Value* x = Get(v, "inverse")) r.inverse = x->kind == js::Value::Kind::Bool && x->boolean;
  if (const js::Value* x = Get(v, "weather")) { std::string s; if (!Str(*x, s) || WeatherNumber(s) == 0) { c.Problem(x->line, "unknown weather"); return; } r.weather = WeatherNumber(s); }
  if (const js::Value* x = Get(v, "banned_items")) {
    if (x->kind != js::Value::Kind::Array) { c.Problem(x->line, "\"banned_items\" is a list of item names"); return; }
    for (const auto& it : x->items) {
      std::string s; int64_t id = 0;
      if (!Str(it, s) || !c.resolve("item", s, id)) { c.Problem(it.line, "unknown item \"" + s + "\""); return; }
      r.bannedItems.push_back(static_cast<int>(id));
    }
  }
  c.defs.ruleSets.push_back(std::move(r));
}

}

void ParseFile(const std::string& mod, const std::string& file, const std::string& text, const Resolver& resolve, Defs& defs) {
  Ctx c{mod, file, resolve, defs};
  js::Value root;
  js::Reader reader(text);
  if (!reader.Document(root)) { defs.problems.push_back(mod + ": " + file + ": " + reader.Error()); return; }
  if (root.kind != js::Value::Kind::Object) { defs.problems.push_back(mod + ": " + file + ": the file must hold one { } object"); return; }
  for (const auto& key : root.order) {
    const js::Value& v = root.fields.at(key);
    if (key == "_note") continue;
    if (key != "moves" && key != "abilities" && key != "rule_sets" && key != "hidden_abilities") { c.Problem(v.line, "unknown part \"" + key + "\" (moves, abilities, rule_sets, hidden_abilities)"); continue; }
    if (v.kind != js::Value::Kind::Object) { c.Problem(v.line, "\"" + key + "\" must be an object"); continue; }
    if (key == "hidden_abilities") { ParseHidden(c, v); continue; }
    for (const auto& name : v.order) {
      const js::Value& item = v.fields.at(name);
      if (key == "moves") ParseMove(c, name, item);
      else if (key == "abilities") ParseAbility(c, name, item);
      else ParseRuleSet(c, name, item);
    }
  }
}

void Finish(Defs& defs) {
  std::vector<MoveDef> moves;
  for (auto& m : defs.moves) {
    auto it = std::find_if(moves.begin(), moves.end(), [&](const MoveDef& o) { return o.id == m.id; });
    if (it != moves.end()) {
      defs.problems.push_back(m.mod + ": the move \"" + m.name + "\" is defined by \"" + it->mod + "\" too; the later one is used");
      *it = m;
    } else {
      moves.push_back(m);
    }
  }
  defs.moves = std::move(moves);
  std::set<int> used;
  for (const auto& a : defs.abilities) if (a.id != 0) used.insert(a.id);
  std::vector<AbilityDef> kept;
  std::set<int> seen;
  int next = 124;
  for (auto& a : defs.abilities) {
    if (a.id == 0) {
      while (used.count(next)) next++;
      if (next > 255) { defs.problems.push_back(a.mod + ": no free ability number for \"" + a.name + "\""); continue; }
      a.id = next;
      used.insert(next);
    } else if (!seen.insert(a.id).second) {
      defs.problems.push_back(a.mod + ": the ability number " + std::to_string(a.id) + " of \"" + a.name + "\" is already used by another ability; left out");
      continue;
    }
    kept.push_back(a);
  }
  defs.abilities = std::move(kept);
}

namespace {
struct Hasher {
  uint64_t h = 1469598103934665603ull;
  void Add(uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ull; } }
  void Add(const std::string& s) { Add(s.size()); for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ull; } }
  void Add(const std::vector<Condition>& w) { Add(w.size()); for (const auto& c : w) { Add(static_cast<uint64_t>(c.kind)); Add(static_cast<uint64_t>(static_cast<int64_t>(c.arg))); Add(c.negate); for (int l : c.list) Add(static_cast<uint64_t>(l)); } }
  void Add(const Block& b) {
    Add(static_cast<uint64_t>(b.kind)); Add(b.onUser); Add(b.onFoes); Add(static_cast<uint64_t>(b.stat)); Add(static_cast<uint64_t>(static_cast<int64_t>(b.stages)));
    Add(static_cast<uint64_t>(b.chance)); Add(static_cast<uint64_t>(b.code)); Add(static_cast<uint64_t>(b.permille)); Add(static_cast<uint64_t>(b.weather));
    Add(static_cast<uint64_t>(static_cast<int64_t>(b.type))); Add(static_cast<uint64_t>(b.turns)); Add(static_cast<uint64_t>(static_cast<int64_t>(b.field)));
    Add(b.message); Add(b.endMessage); Add(b.when); if (b.ability != 0) Add(static_cast<uint64_t>(b.ability));
  }
  void Add(const std::vector<Block>& v) { Add(v.size()); for (const auto& b : v) Add(b); }
  void Add(const std::vector<PowerRule>& v) {
    Add(v.size());
    for (const auto& r : v) { Add(r.when); Add(static_cast<uint64_t>(r.percent)); Add(static_cast<uint64_t>(static_cast<int64_t>(r.accuracy))); Add(static_cast<uint64_t>(static_cast<int64_t>(r.add))); }
  }
};
}

uint64_t Digest(const Defs& d) {
  Hasher h;
  std::vector<const MoveDef*> mv;
  for (const auto& m : d.moves) mv.push_back(&m);
  std::sort(mv.begin(), mv.end(), [](auto* a, auto* b) { return a->id < b->id; });
  h.Add(mv.size());
  for (auto* m : mv) { h.Add(static_cast<uint64_t>(m->id)); h.Add(m->effects); h.Add(m->power); h.Add(m->accuracy); h.Add(m->script);
    if (m->chargeDonor != 0) { h.Add(0x0714u); h.Add(static_cast<uint64_t>(m->chargeDonor)); h.Add(m->chargeMessage); if (m->chargeUntouchable) h.Add(0x0715u); if (m->chargeCarry) h.Add(0x0717u); }
    if (m->crit || m->ignoreBoosts || m->useTargetAttack || m->defendWithDef || m->firstTurnOnly || m->powerFormula || m->neverMiss || m->protectKind || m->needsPhysicalHit || m->heatsUp || m->typeFromUser)
      h.Add(static_cast<uint64_t>(m->crit * 1 + m->ignoreBoosts * 2 + m->useTargetAttack * 4 + m->defendWithDef * 8 + m->firstTurnOnly * 16 + m->powerFormula * 32 + m->neverMiss * 256 + m->protectKind * 1024 + m->needsPhysicalHit * 2048 + m->heatsUp * 4096 + m->typeFromUser * 8192)); }
  std::vector<const AbilityDef*> ab;
  for (const auto& a : d.abilities) ab.push_back(&a);
  std::sort(ab.begin(), ab.end(), [](auto* a, auto* b) { return a->id < b->id; });
  h.Add(ab.size());
  for (auto* a : ab) {
    h.Add(static_cast<uint64_t>(a->id)); h.Add(a->name); h.Add(a->text);
    h.Add(a->switchIn); h.Add(a->endOfTurn); h.Add(a->ko); h.Add(a->contact); h.Add(a->switchOut); h.Add(a->beforeMove);
    h.Add(a->damageDealt); h.Add(a->damageTaken); h.Add(a->priority);
    for (const auto& s : a->statMods) { h.Add(static_cast<uint64_t>(s.stat)); h.Add(static_cast<uint64_t>(s.percent)); h.Add(s.when); }
    for (const auto& o : a->absorb) { h.Add(static_cast<uint64_t>(o.type)); h.Add(static_cast<uint64_t>(o.healPermille)); h.Add(static_cast<uint64_t>(o.stat)); h.Add(static_cast<uint64_t>(o.stages)); h.Add(o.message); }
    for (int s : a->statDropBlock) h.Add(static_cast<uint64_t>(s));
    for (int s : a->statusBlock) h.Add(static_cast<uint64_t>(s));
    h.Add(a->hit); h.Add(a->hitDealt); h.Add(a->fainted); h.Add(a->statDropped); if (!a->anyFaint.empty()) h.Add(a->anyFaint); h.Add(a->accuracyAgainst); h.Add(a->accuracyDealt); h.Add(a->critWhen);
    for (const auto& k : a->convert) { h.Add(static_cast<uint64_t>(static_cast<int64_t>(k.from))); h.Add(static_cast<uint64_t>(k.to)); h.Add(static_cast<uint64_t>(k.percent)); h.Add(k.when); }
    h.Add(static_cast<uint64_t>(static_cast<int64_t>(a->auraType))); h.Add(static_cast<uint64_t>(a->auraPercent)); h.Add(a->breaksAuras); h.Add(a->contrary); h.Add(a->blocksPriority);
    for (int m : a->immuneMoves) h.Add(static_cast<uint64_t>(m));
    h.Add(static_cast<uint64_t>(a->actsAs)); h.Add(a->reflectStatus); h.Add(a->removesSecondary); h.Add(a->script); if (a->noContact) h.Add(0x4C52u);
  }
  for (const auto& r : d.ruleSets) {
    h.Add(r.name); h.Add(static_cast<uint64_t>(r.levelCap)); h.Add(static_cast<uint64_t>(r.levelAll)); h.Add(r.forceDouble); h.Add(r.inverse); h.Add(static_cast<uint64_t>(r.weather));
    for (int i : r.bannedItems) h.Add(static_cast<uint64_t>(i));
  }
  for (const auto& f : d.fieldNames) h.Add(f);
  for (const auto& a : d.abilities) {
    if (!(a.unnerve || a.disableOnHit || a.poisonAnyType || a.strongWind || a.infiltrator || a.illusion || !a.berryEaten.empty() || !a.halfHp.empty() || !a.typeByItem.empty() || !a.dancer.empty())) continue;
    h.Add(0x0711u); h.Add(static_cast<uint64_t>(a.id)); h.Add(a.unnerve); h.Add(static_cast<uint64_t>(a.disableOnHit)); h.Add(a.poisonAnyType); h.Add(a.strongWind); h.Add(a.infiltrator); if (a.illusion) h.Add(0x0716u); for (int dm : a.dancer) h.Add(static_cast<uint64_t>(dm));
    h.Add(a.berryEaten); h.Add(a.halfHp); for (const auto& t : a.typeByItem) { h.Add(static_cast<uint64_t>(t.item)); h.Add(static_cast<uint64_t>(t.type)); }
  }
  for (const auto& kv : d.hiddenNat) { h.Add(0x0712u); h.Add(static_cast<uint64_t>(kv.first)); h.Add(static_cast<uint64_t>(kv.second)); }
  for (const auto& kv : d.hiddenRow) { h.Add(0x0713u); h.Add(static_cast<uint64_t>(kv.first)); h.Add(static_cast<uint64_t>(kv.second)); }
  return h.h;
}

}
