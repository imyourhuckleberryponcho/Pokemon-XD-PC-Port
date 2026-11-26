#include "xd_gen4_xg.h"

#include "../../../engine/mods/json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace J = engine::mods::json;

namespace {

#include "gen4_xg_tables.inc"

std::string Key(const std::string& s) {
  std::string k;
  for (char c : s) {
    if (std::isalnum(static_cast<unsigned char>(c))) k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (size_t at; (at = k.find("armour")) != std::string::npos;) k.replace(at, 6, "armor");
  return k;
}

const xd::gen4::xg::Translator* sActive = nullptr;

void Escape(const std::string& s, std::string& out) {
  out += '"';
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
        out += buf;
      } else {
        out += c;
      }
    }
  }
  out += '"';
}

void Write(const J::Value& v, std::string& out) {
  switch (v.kind) {
  case J::Value::Kind::Null: out += "null"; break;
  case J::Value::Kind::Bool: out += v.boolean ? "true" : "false"; break;
  case J::Value::Kind::Number: out += J::Number(v.number); break;
  case J::Value::Kind::String: Escape(v.text, out); break;
  case J::Value::Kind::Array:
    out += '[';
    for (size_t i = 0; i < v.items.size(); i++) {
      if (i) out += ',';
      Write(v.items[i], out);
    }
    out += ']';
    break;
  case J::Value::Kind::Object: {
    out += '{';
    bool first = true;
    for (const std::string& k : v.order) {
      if (!first) out += ',';
      first = false;
      Escape(k, out);
      out += ':';
      Write(v.fields.at(k), out);
    }
    out += '}';
    break;
  }
  }
}

J::Value Num(double n) {
  J::Value v;
  v.kind = J::Value::Kind::Number;
  v.number = n;
  return v;
}
J::Value Str(const std::string& s) {
  J::Value v;
  v.kind = J::Value::Kind::String;
  v.text = s;
  return v;
}
J::Value Arr() {
  J::Value v;
  v.kind = J::Value::Kind::Array;
  return v;
}
J::Value Obj() {
  J::Value v;
  v.kind = J::Value::Kind::Object;
  return v;
}
void Set(J::Value& obj, const std::string& key, J::Value v) {
  if (obj.fields.find(key) == obj.fields.end()) obj.order.push_back(key);
  obj.fields[key] = std::move(v);
}
J::Value* Get(J::Value& obj, const std::string& key) {
  const auto it = obj.fields.find(key);
  return it == obj.fields.end() ? nullptr : &it->second;
}
void Erase(J::Value& obj, const std::string& key) {
  obj.fields.erase(key);
  obj.order.erase(std::remove(obj.order.begin(), obj.order.end(), key), obj.order.end());
}

}

namespace xd::gen4::xg {

void SetActive(const Translator* t) { sActive = t; }
const Translator* Active() { return sActive; }

Translator::Translator(Tables t) : mT(std::move(t)) {
  for (size_t i = 0; i < mT.moves.size(); i++) {
    const std::string k = Key(mT.moves[i]);
    if (k.empty() || mT.movePp[i] == 0) continue;
    const auto it = mMoveKey.find(k);
    if (it == mMoveKey.end() || mT.movePp[i] > mT.movePp[it->second]) mMoveKey[k] = static_cast<int>(i);
  }
  for (size_t i = 1; i < mT.abilities.size(); i++) {
    const std::string k = Key(mT.abilities[i]);
    if (!k.empty() && k != "" && mAbilityKey.find(k) == mAbilityKey.end()) mAbilityKey[k] = static_cast<int>(i);
  }
  for (size_t i = 0; i < mT.species.size(); i++) {
    const std::string k = Key(mT.species[i]);
    if (!k.empty() && mSpeciesKey.find(k) == mSpeciesKey.end()) mSpeciesKey[k] = static_cast<int>(i);
  }
  for (size_t i = 0; i < mT.items.size(); i++) {
    const std::string k = Key(mT.items[i]);
    if (!k.empty() && mItemKey.find(k) == mItemKey.end()) mItemKey[k] = static_cast<int>(i);
  }
}

int Translator::FindMove(const std::string& name) const {
  const auto it = mMoveKey.find(Key(name));
  return it == mMoveKey.end() ? -1 : it->second;
}

std::string Translator::XdMoveName(uint32_t number) const {
  if (number == 0) return "";
  if (number < 375) return kXdMoveNames[number];
  const size_t i = number - 375;
  return i < mPackMoves.size() ? mPackMoves[i] : std::string();
}

std::string Translator::RefKey(const MoveRef& r) { return r.id >= 0 ? "#" + std::to_string(r.id) : Key(r.name); }

MoveRef Translator::ResolveMove(uint32_t xdNumber, const std::set<std::string>& used) const {
  MoveRef none;
  const std::string name = XdMoveName(xdNumber);
  if (name.empty()) return none;
  auto byId = [&](int id) {
    MoveRef r;
    r.id = id;
    return r;
  };
  if (const int id = FindMove(name); id >= 0) return byId(id);
  const std::string key = Key(name);
  if (const auto k = mPackKind.find(key); k != mPackKind.end()) {
    if (k->second == 'R') {
      MoveRef r;
      r.name = name;
      return r;
    }
    if (k->second == 'S') {
      const int id = FindMove(mPackBase.at(key));
      return id >= 0 ? byId(id) : none;
    }
  }
  for (const OldMoveRule& o : kOldMoveRules) {
    if (Key(o.move) != key) continue;
    MoveRef first;
    for (const char* alt : o.stand) {
      if (alt[0] == 0) continue;
      const int id = FindMove(alt);
      if (id < 0) continue;
      MoveRef r = byId(id);
      if (first.id < 0 && first.name.empty()) first = r;
      if (used.count(RefKey(r)) == 0) return r;
    }
    return first;
  }
  return none;
}

std::vector<std::pair<uint32_t, MoveRef>> Translator::Learnset(const std::vector<std::pair<uint32_t, uint32_t>>& in) const {
  std::vector<std::pair<uint32_t, MoveRef>> out;
  std::set<std::string> used;
  for (const auto& e : in) {
    const std::string name = XdMoveName(e.second);
    if (!name.empty() && FindMove(name) >= 0) used.insert("#" + std::to_string(FindMove(name)));
  }
  for (const auto& e : in) {
    if (e.second == 0) continue;
    const std::string name = XdMoveName(e.second);
    const bool own = !name.empty() && FindMove(name) >= 0;
    MoveRef r = ResolveMove(e.second, own ? std::set<std::string>() : used);
    if (r.id < 0 && r.name.empty()) continue;
    const std::string k = RefKey(r);
    if (own) {
      bool dup = false;
      for (const auto& o : out) dup = dup || RefKey(o.second) == k;
      if (dup) continue;
    } else {
      if (used.count(k)) continue;
      used.insert(k);
    }
    out.emplace_back(e.first, r);
  }
  return out;
}

std::string Translator::XdAbilityName(uint32_t id) const {
  if (id == 0) return "";
  if (id >= 77) return id < mT.packAbilities.size() ? mT.packAbilities[id] : std::string();
  return kXdAbilityNames[id];
}

int Translator::AbilityFor(const std::string& xdName) const {
  if (xdName.empty()) return 0;
  const std::string k = Key(xdName);
  if (const auto it = mAbilityKey.find(k); it != mAbilityKey.end()) return it->second;
  for (const AbilityRule& r : kAbilityRules) {
    if (Key(r.ability) != k) continue;
    if (r.stand[0] == 0) return 0;
    const auto it = mAbilityKey.find(Key(r.stand));
    return it == mAbilityKey.end() ? 0 : it->second;
  }
  return 0;
}

uint32_t Translator::MapAbility(uint32_t xdAbility) const {
  const std::string name = XdAbilityName(xdAbility);
  if (xdAbility >= 124 && mHasGen57) {
    const auto it = mAbilityKey.find(Key(name));
    return it != mAbilityKey.end() ? static_cast<uint32_t>(it->second) : xdAbility;
  }
  return static_cast<uint32_t>(AbilityFor(name));
}

void Translator::MapTypes(const std::string& species, uint32_t types[2]) const {
  const std::string k = Key(species);
  if (k == "togekiss") types[0] = 9;
  if (k == "mimejr") types[1] = 9;
}

void Translator::MapTypes57(uint32_t dex, uint32_t types[2]) const {
  const auto it = mInfo.types.find(dex);
  if (it == mInfo.types.end()) return;
  if (it->second.first == 17) types[0] = 9;
  if (it->second.second == 17) types[1] = 9;
}

uint32_t Translator::AddedMoveNumber(const std::string& name) const {
  for (size_t i = 0; i < mAddedOrder.size(); i++) {
    if (Key(mAddedOrder[i]) == Key(name)) return mT.firstNewMove + static_cast<uint32_t>(i);
  }
  return 0;
}

void Translator::MapLearnset(uint32_t level[20], uint32_t move[20]) const {
  std::vector<std::pair<uint32_t, uint32_t>> in;
  for (int i = 0; i < 20; i++) {
    if (move[i] != 0) in.emplace_back(level[i], move[i]);
  }
  const auto out = Learnset(in);
  int n = 0;
  for (const auto& e : out) {
    if (n >= 20) break;
    level[n] = e.first;
    move[n] = e.second.id >= 0 ? static_cast<uint32_t>(e.second.id) : AddedMoveNumber(e.second.name);
    n++;
  }
  for (; n < 20; n++) level[n] = move[n] = 0;
}

namespace {

struct MachineText {
  std::vector<std::string> extraMoves;
  std::map<std::string, std::string> bitsBySpecies;
};

}

bool Translator::Run(std::vector<engine::mods::DataModFile>& files, const std::string& machinesText, std::string& error) {
  mReport.clear();
  mAddedOrder.clear();
  mPackMoves.clear();
  mPackKind.clear();
  mPackBase.clear();
  mCounts = Counts();
  mHasGen57 = false;
  return RunPack(false, files, machinesText, error);
}

bool Translator::RunGen57(std::vector<engine::mods::DataModFile>& files, const Gen57Info& info, std::string& error) {
  mInfo = info;
  mInfo.portCodeMoves.clear();
  mInfo.fairyMoves.clear();
  for (const std::string& n : info.portCodeMoves) mInfo.portCodeMoves.insert(Key(n));
  for (const std::string& n : info.fairyMoves) mInfo.fairyMoves.insert(Key(n));
  mHasGen57 = true;
  if (mT.packAbilities.size() < 256) mT.packAbilities.resize(256);
  if (!RunPack(true, files, std::string(), error)) return false;
  for (uint32_t id : mInfo.abilityIds) {
    const std::string name = XdAbilityName(id);
    if (!name.empty() && mAbilityKey.find(Key(name)) != mAbilityKey.end()) {
      mCounts.abilitiesByName++;
    } else if (mInfo.portCodeAbilities.count(id) != 0) {
      mCounts.abilitiesPortCode++;
      mReport.push_back("ability " + name + ": not on XG, keeps its own number (the port's battle code)");
    } else {
      mCounts.abilitiesNameOnly++;
      mReport.push_back("ability " + name + ": not on XG, a name with no effect");
    }
  }
  return true;
}

bool Translator::RunPack(bool gen57, std::vector<engine::mods::DataModFile>& files, const std::string& machinesText, std::string& error) {
  using engine::mods::DataModFile;
  const std::string prefix = gen57 ? "gen57/" : "gen4/";
  DataModFile *items = nullptr, *moves = nullptr, *species = nullptr, *forms = nullptr;
  std::vector<DataModFile*> patches;
  for (DataModFile& f : files) {
    const std::string& l = f.label;
    if (l.compare(0, prefix.size(), prefix) != 0) continue;
    const auto ends = [&](const char* s) { return l.size() >= std::string(s).size() && l.compare(l.size() - std::string(s).size(), std::string::npos, s) == 0; };
    if (ends("00_items.json")) items = &f;
    else if (ends("01_moves.json")) moves = &f;
    else if (ends("02_species.json")) species = &f;
    else if (gen57 && ends("03_forms.json")) forms = &f;
    else if (gen57 && (ends("04_ability_patch.json") || ends("05_rks_memories.json") || ends("06_evolution_items.json"))) patches.push_back(&f);
    else if (gen57 && ends("07_stone_coupons.json")) f.text = "{\"_note\": \"XD only: XG's own prize desk sells the classic stones with its own prices\"}";
  }
  if (!moves || !species) {
    error = "the pack has no moves or species file";
    return false;
  }
  J::Value mroot;
  {
    J::Reader rd(moves->text);
    if (!rd.Document(mroot) || mroot.kind != J::Value::Kind::Object) {
      error = "the pack's moves file can't be read: " + rd.Error();
      return false;
    }
  }
  J::Value* add = Get(mroot, "add");
  J::Value* rows = add ? Get(*add, "moves") : nullptr;
  if (!rows) {
    error = "the pack's moves file has no moves";
    return false;
  }
  const std::vector<std::string> packOrder = rows->order;
  std::vector<std::string> mine;
  for (const std::string& name : packOrder) {
    if (name.empty() || name[0] == '_') continue;
    mPackMoves.push_back(name);
    mine.push_back(name);
  }
  J::Value newRows = Obj();
  for (const std::string& name : mine) {
    const std::string key = Key(name);
    if (FindMove(name) >= 0) {
      mPackKind[key] = 'X';
      if (gen57) mCounts.movesByName++;
      continue;
    }
    Gen4MoveRule rule57 = {"", 'R', "Tackle", ""};
    std::string donorNote;
    const Gen4MoveRule* rule = nullptr;
    if (gen57) {
      const J::Value& src0 = rows->fields.at(name);
      const J::Value* cp = nullptr;
      if (const auto itc = src0.fields.find("copy"); itc != src0.fields.end()) cp = &itc->second;
      const J::Value* power = nullptr;
      if (const auto itp = src0.fields.find("iryoku"); itp != src0.fields.end()) power = &itp->second;
      const bool damaging = power != nullptr && power->kind == J::Value::Kind::Number && power->number > 0;
      const std::string donor = cp && cp->kind == J::Value::Kind::String ? cp->text : std::string();
      static std::string base57;
      base57 = damaging ? "Tackle" : "Splash";
      if (!donor.empty() && FindMove(donor) >= 0 && Key(donor) != "pound") base57 = donor;
      rule57.base = base57.c_str();
      rule57.keep = "";
      rule = &rule57;
    } else {
      for (const Gen4MoveRule& r : kGen4MoveRules) {
        if (Key(r.move) == key) rule = &r;
      }
      if (!rule) {
        mReport.push_back("pack move \"" + name + "\" has no rule for XG: it becomes a plain hit (Tackle)");
        static const Gen4MoveRule fallback = {"", 'R', "Tackle", ""};
        rule = &fallback;
      }
    }
    const int base = FindMove(rule->base);
    if (base < 0) {
      error = std::string("XG has no move \"") + rule->base + "\" (stand-in for " + name + ")";
      return false;
    }
    if (rule->kind == 'S') {
      mPackKind[key] = 'S';
      mPackBase[key] = rule->base;
      mReport.push_back(name + ": no row on XG, uses " + rule->base);
      continue;
    }
    mPackKind[key] = 'R';
    mAddedOrder.push_back(name);
    if (gen57) {
      const bool code = mInfo.portCodeMoves.count(key) != 0;
      if (code) mCounts.movesPortCode++; else mCounts.movesStandIn++;
      mReport.push_back(name + ": new row on XG, effect of " + rule->base + (code ? " + the port's battle code" : ", no effect beyond that"));
    } else {
      mReport.push_back(name + ": new row on XG, effect of " + rule->base);
    }
    J::Value src = rows->fields.at(name);
    J::Value row = Obj();
    Set(row, "copy", Num(base));
    const std::string keep = rule->keep;
    for (const std::string& f : src.order) {
      if (f == "copy" || f == "wazawzxdataId" || f == "seqId" || f == "unk16") continue;
      if (f == "animation") continue;
      if (f == "avg" && keep.find("avg") != std::string::npos) continue;
      Set(row, f, src.fields.at(f));
    }
    if (gen57 && mInfo.fairyMoves.count(key) != 0) Set(row, "zokuseiDataId", Num(9));
    Set(newRows, name, std::move(row));
  }
  Set(*add, "moves", std::move(newRows));
  moves->text.clear();
  Write(mroot, moves->text);

  std::vector<std::string> extraMoveKeys;
  std::map<std::string, std::string> extraBits;
  {
    std::istringstream in(machinesText);
    std::string line;
    while (std::getline(in, line)) {
      std::istringstream ls(line);
      std::string kind;
      ls >> kind;
      if (kind == "machine") {
        std::string idx, item, mv;
        ls >> idx >> item >> mv;
        std::string name;
        if (!mv.empty() && std::isdigit(static_cast<unsigned char>(mv[0]))) {
          const uint32_t n = static_cast<uint32_t>(std::atoi(mv.c_str()));
          name = XdMoveName(n);
        } else {
          name = mv;
          for (char& c : name) c = c == '_' ? ' ' : c;
        }
        extraMoveKeys.push_back(Key(name));
      } else if (kind == "bits") {
        std::string sp, bits;
        ls >> sp >> bits;
        extraBits[Key(sp)] = bits;
      }
    }
  }
  std::vector<std::string> xdTm, xdTutor, xgTm, xgTutor;
  for (uint16_t m : kXdTmMove) xdTm.push_back(Key(kXdMoveNames[m]));
  for (uint16_t m : kXdTutorMove) xdTutor.push_back(Key(kXdMoveNames[m]));
  for (size_t j = 0; j < mT.machineMoves.size(); j++) {
    xgTm.push_back(j < 50 && mT.machineMoves[j] < mT.moves.size() ? Key(mT.moves[mT.machineMoves[j]]) : std::string());
  }
  for (uint16_t m : mT.tutorMoves) xgTutor.push_back(m < mT.moves.size() ? Key(mT.moves[m]) : std::string());

  J::Value sroot;
  {
    J::Reader rd(species->text);
    if (!rd.Document(sroot) || sroot.kind != J::Value::Kind::Object) {
      error = "the pack's species file can't be read: " + rd.Error();
      return false;
    }
  }
  J::Value* sadd = Get(sroot, "add");
  J::Value* srows = sadd ? Get(*sadd, "pokemon") : nullptr;
  if (!srows) {
    error = "the pack's species file has no species";
    return false;
  }
  const auto numbers = [](J::Value* v) {
    std::vector<uint32_t> out;
    if (v && v->kind == J::Value::Kind::Array) {
      for (const J::Value& i : v->items) out.push_back(i.kind == J::Value::Kind::Number ? static_cast<uint32_t>(i.number) : 0);
    }
    return out;
  };
  const auto refValue = [&](const MoveRef& r) { return r.id >= 0 ? Num(r.id) : r.name.empty() ? Num(0) : Str(r.name); };
  size_t withoutMoves = 0;
  const auto doRow = [&](const std::string& sname, J::Value& s, bool discRow = false) {
    if (s.kind != J::Value::Kind::Object) return;
    if (gen57) {
      if (J::Value* c = Get(s, "copy")) {
        if (c->kind == J::Value::Kind::String) {
          const auto sk = mSpeciesKey.find(Key(c->text));
          if (sk != mSpeciesKey.end()) c->text = "#" + std::to_string(sk->second);
        }
      }
    }
    if (true) {
      Erase(s, "nakigoeId");
      Erase(s, "voice");
    }
    const auto lv = numbers(Get(s, "waza.level")), mv = numbers(Get(s, "waza.dataId"));
    std::vector<std::pair<uint32_t, uint32_t>> in;
    std::set<std::string> ownXd;
    for (size_t i = 0; i < mv.size() && i < lv.size(); i++) {
      if (mv[i]) in.emplace_back(lv[i], mv[i]), ownXd.insert(Key(XdMoveName(mv[i])));
    }
    const auto set = Learnset(in);
    J::Value lvl = Arr(), dat = Arr();
    for (size_t i = 0; i < 20; i++) {
      lvl.items.push_back(Num(i < set.size() ? set[i].first : 0));
      dat.items.push_back(i < set.size() ? refValue(set[i].second) : Num(0));
    }
    Set(s, "waza.level", std::move(lvl));
    Set(s, "waza.dataId", std::move(dat));
    if (set.empty()) withoutMoves++;
    if (J::Value* k = Get(s, "kowaza")) {
      std::vector<std::pair<uint32_t, uint32_t>> e;
      for (uint32_t m : numbers(k)) {
        if (m) e.emplace_back(0, m), ownXd.insert(Key(XdMoveName(m)));
      }
      const auto es = Learnset(e);
      J::Value arr = Arr();
      for (size_t i = 0; i < 8; i++) arr.items.push_back(i < es.size() ? refValue(es[i].second) : Num(0));
      Set(s, "kowaza", std::move(arr));
    }
    std::set<std::string> learns;
    for (const auto& e : set) learns.insert(e.second.id >= 0 ? Key(mT.moves[e.second.id]) : Key(e.second.name));
    if (J::Value* a = Get(s, "tokuseiDataId")) {
      const auto ab = numbers(a);
      J::Value arr = Arr();
      for (uint32_t x : ab) arr.items.push_back(Num(MapAbility(x)));
      Set(s, "tokuseiDataId", std::move(arr));
    }
    if (J::Value* t = Get(s, "zokuseiDataId")) {
      auto ty = numbers(t);
      if (ty.size() == 2) {
        uint32_t tt[2] = {ty[0], ty[1]};
        if (gen57) {
          const J::Value* dexv = Get(s, "numPokemon");
          const auto ft = mInfo.formTypes.find(sname);
          if (ft != mInfo.formTypes.end()) {
            if (ft->second.first == 17) tt[0] = 9;
            if (ft->second.second == 17) tt[1] = 9;
          } else if (dexv && dexv->kind == J::Value::Kind::Number) {
            MapTypes57(static_cast<uint32_t>(dexv->number), tt);
          }
        } else {
          MapTypes(sname, tt);
        }
        J::Value arr = Arr();
        arr.items.push_back(Num(tt[0]));
        arr.items.push_back(Num(tt[1]));
        Set(s, "zokuseiDataId", std::move(arr));
      }
    }
    const auto xdBits = numbers(Get(s, "wazaMcn"));
    const auto bitsIt = extraBits.find(Key(sname));
    const auto can = [&](const std::string& moveKey, const std::vector<std::string>& xdSlots, const std::vector<uint32_t>& slotBits) {
      if (moveKey.empty()) return false;
      for (size_t i = 0; i < xdSlots.size() && i < slotBits.size(); i++) {
        if (xdSlots[i] == moveKey && slotBits[i]) return true;
      }
      return false;
    };
    if (Get(s, "wazaMcn")) {
      J::Value arr = Arr();
      for (size_t j = 0; j < 58; j++) {
        bool bit = false;
        const std::string mk = j < xgTm.size() ? xgTm[j] : std::string();
        if (!mk.empty()) {
          bit = can(mk, xdTm, xdBits);
          if (!bit && bitsIt != extraBits.end()) {
            for (size_t e = 0; e < extraMoveKeys.size() && e < bitsIt->second.size(); e++) {
              if (extraMoveKeys[e] == mk && bitsIt->second[e] == '1') bit = true;
            }
          }
          if (!bit) bit = learns.count(mk) != 0 || ownXd.count(mk) != 0;
        }
        arr.items.push_back(Num(bit ? 1 : 0));
      }
      Set(s, "wazaMcn", std::move(arr));
    }
    if (J::Value* tw = Get(s, "teachWaza")) {
      const auto xdTeach = numbers(tw);
      J::Value arr = Arr();
      for (size_t k = 0; k < 12; k++) {
        const std::string mk = k < xgTutor.size() ? xgTutor[k] : std::string();
        bool bit = !mk.empty() && (can(mk, xdTutor, xdTeach) || learns.count(mk) != 0 || ownXd.count(mk) != 0);
        arr.items.push_back(Num(bit ? 1 : 0));
      }
      Set(s, "teachWaza", std::move(arr));
    }
  };
  for (const std::string& sname : srows->order) doRow(sname, srows->fields.at(sname));
  if (J::Value* old = Get(sroot, "pokemon")) {
    for (const std::string& key : old->order) {
      J::Value& s = old->fields.at(key);
      if (s.kind != J::Value::Kind::Object) continue;
      if (Get(s, "waza.dataId")) {
        doRow(key, s, true);
        continue;
      }
      std::vector<std::pair<std::string, J::Value>> moved;
      for (const std::string& f : std::vector<std::string>(s.order)) {
        if (f.compare(0, 8, "wazaMcn[") != 0) continue;
        const size_t n = static_cast<size_t>(std::atoi(f.c_str() + 8));
        J::Value v = s.fields.at(f);
        Erase(s, f);
        if (n >= xdTm.size()) continue;
        for (size_t j = 0; j < 50 && j < xgTm.size(); j++) {
          if (!xgTm[j].empty() && xgTm[j] == xdTm[n]) moved.emplace_back("wazaMcn[" + std::to_string(j) + "]", v);
        }
      }
      for (auto& m : moved) Set(s, m.first, std::move(m.second));
    }
  }
  species->text.clear();
  Write(sroot, species->text);
  if (forms) {
    J::Value froot;
    J::Reader rd(forms->text);
    if (!rd.Document(froot) || froot.kind != J::Value::Kind::Object) {
      error = "the pack's forms file can't be read: " + rd.Error();
      return false;
    }
    J::Value* fadd = Get(froot, "add");
    if (J::Value* frows = fadd ? Get(*fadd, "pokemon") : nullptr) {
      for (const std::string& sname : frows->order) doRow(sname, frows->fields.at(sname));
    }
    if (J::Value* irows = fadd ? Get(*fadd, "items") : nullptr) {
      for (const std::string& name : irows->order) {
        J::Value& row = irows->fields.at(name);
        if (row.kind != J::Value::Kind::Object) continue;
        if (J::Value* c = Get(row, "copy")) {
          if (c->kind == J::Value::Kind::String) {
            const auto ik = mItemKey.find(Key(c->text));
            if (ik == mItemKey.end()) {
              mReport.push_back("item " + name + ": XG has no \"" + c->text + "\", copies Everstone");
              c->text = "Everstone";
            } else {
              c->text = "#" + std::to_string(ik->second);
            }
          }
        }
      }
    }
    forms->text.clear();
    Write(froot, forms->text);
  }
  for (DataModFile* patch : patches) {
    J::Value proot;
    J::Reader rd(patch->text);
    if (!rd.Document(proot) || proot.kind != J::Value::Kind::Object) {
      error = "the pack's ability patch file can't be read: " + rd.Error();
      return false;
    }
    J::Value* padd = Get(proot, "add");
    if (J::Value* irows = padd ? Get(*padd, "items") : nullptr) {
      for (const std::string& name : irows->order) {
        J::Value& row = irows->fields.at(name);
        J::Value* c = row.kind == J::Value::Kind::Object ? Get(row, "copy") : nullptr;
        if (!c || c->kind != J::Value::Kind::String) continue;
        const auto ik = mItemKey.find(Key(c->text));
        c->text = ik == mItemKey.end() ? "Everstone" : "#" + std::to_string(ik->second);
      }
    }
    patch->text.clear();
    Write(proot, patch->text);
  }
  if (withoutMoves != 0) mReport.push_back(std::to_string(withoutMoves) + " species ended with no learn-set (check the tables)");

  if (items) {
    J::Value iroot;
    J::Reader rd(items->text);
    if (!rd.Document(iroot) || iroot.kind != J::Value::Kind::Object) {
      error = "the pack's items file can't be read: " + rd.Error();
      return false;
    }
    Erase(iroot, "shopData");
    J::Value* iadd = Get(iroot, "add");
    J::Value* irows = iadd ? Get(*iadd, "items") : nullptr;
    if (irows) {
      J::Value out = Obj();
      for (const std::string& name : irows->order) {
        J::Value row = irows->fields.at(name);
        if (name.size() == 4 && name[0] == 'T' && name[1] == 'M' && std::isdigit(static_cast<unsigned char>(name[2]))) continue;
        if (row.kind == J::Value::Kind::Object) {
          if (J::Value* c = Get(row, "copy")) {
            if (c->kind == J::Value::Kind::String && mItemKey.find(Key(c->text)) == mItemKey.end()) {
              mReport.push_back("item " + name + ": XG has no \"" + c->text + "\", copies Everstone");
              c->text = "Everstone";
            }
          }
        }
        Set(out, name, std::move(row));
      }
      Set(*iadd, "items", std::move(out));
    }
    items->text.clear();
    Write(iroot, items->text);
  }
  return true;
}

}

namespace xd::gen4::xg {

namespace {
struct TableView {
  const engine::mods::DataTable* t = nullptr;
  const engine::mods::DataField* f = nullptr;
  std::vector<uint8_t> bytes;
  uint32_t Get(uint32_t entry) const {
    const size_t at = t->places[0].offset + static_cast<size_t>(entry) * t->entrySize;
    if (at + t->entrySize > bytes.size()) return 0;
    return engine::mods::ReadDataField(bytes.data() + at, *f, 0);
  }
};
bool Open(const engine::mods::DataCatalog& c, const engine::mods::EntryReader& read, const char* table, const char* field, TableView& v) {
  v.t = c.FindTable(table);
  if (!v.t || v.t->places.empty()) return false;
  for (const engine::mods::DataField& f : v.t->fields) {
    if (f.name == field) v.f = &f;
  }
  return v.f && read(v.t->places[0].archive, v.t->places[0].entry, v.bytes);
}
}

bool BuildTables(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, Tables& out, std::string& error) {
  TableView moves, machines, tutors;
  if (!Open(catalog, read, "waza_data", "pp", moves)) {
    error = "the move table could not be read";
    return false;
  }
  const engine::mods::NameList* ml = catalog.FindList("move");
  const engine::mods::NameList* al = catalog.FindList("ability");
  const engine::mods::NameList* il = catalog.FindList("item");
  if (!ml || !al || !il) {
    error = "the disc's name lists (moves, abilities, items) are missing";
    return false;
  }
  out.moves.assign(moves.t->count, "");
  out.movePp.assign(moves.t->count, 0);
  for (const auto& n : ml->names) {
    if (n.second < out.moves.size()) out.moves[n.second] = n.first;
  }
  for (uint32_t i = 0; i < moves.t->count; i++) out.movePp[i] = static_cast<int>(moves.Get(i));
  out.abilities.assign(256, "");
  for (const auto& n : al->names) {
    if (n.second < 256) out.abilities[n.second] = n.first;
  }
  if (const engine::mods::NameList* sl = catalog.FindList("species")) {
    out.species.assign(1024, "");
    for (const auto& n : sl->names) {
      if (n.second < 1024) out.species[n.second] = n.first;
    }
  }
  out.items.assign(1024, "");
  for (const auto& n : il->names) {
    if (n.second < 1024) out.items[n.second] = n.first;
  }
  if (Open(catalog, read, "item_waza_machine_no", "wazaID", machines)) {
    for (uint32_t i = 0; i < machines.t->count; i++) out.machineMoves.push_back(static_cast<uint16_t>(machines.Get(i)));
  }
  if (Open(catalog, read, "wazaoshie_data", "id", tutors)) {
    for (uint32_t i = 0; i < tutors.t->count; i++) out.tutorMoves.push_back(static_cast<uint16_t>(tutors.Get(i)));
  }
  out.firstNewMove = std::max(moves.t->count, moves.t->addFirstId);
  return true;
}

}
