#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "pbr_pack.h"

namespace pbr {
namespace {

const char* const kTypeNames[18] = {"Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel",
                                    "???", "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark"};
const char* const kClassNames[3] = {"physical", "special", "status"};
const char* const kFlagFields[6] = {"dageki", "mamoru", "majikku", "yokodori", "oumugaesi", "oujanosirusi"};
constexpr int kFirst = 355, kLast = 467;

std::string norm(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0xC3 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA9) {
      out += 'E';
      i++;
    } else if (c >= 'a' && c <= 'z') {
      out += (char)(c - 32);
    } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out += (char)c;
    }
  }
  return out;
}

struct XdMove {
  bool have = false;
  std::string name;
  std::map<std::string, int64_t> v;
  int64_t get(const char* k) const {
    auto it = v.find(k);
    if (it == v.end()) throw Fail(std::string("waza_data has no field ") + k);
    return it->second;
  }
};
struct PbrMove {
  std::string name;
  int eff, cls, power, type, acc, pp, chance, pri, flags;
};

struct Data {
  std::map<int, XdMove> xd;
  std::map<std::string, int> by_name;
  std::map<int, PbrMove> pbr;
  std::map<int, std::vector<int>> groups;

  Data(const PbrData& p, XdTables& x) {
    for (int i = 0; i < 375; i++) {
      XdMove m;
      std::string nm;
      std::vector<XdField> f;
      if (x.row("waza_data", (uint32_t)i, nm, f)) {
        m.have = true;
        m.name = nm;
        for (const XdField& fl : f)
          if (!fl.values.empty()) m.v[fl.name] = fl.values[0];
      }
      xd[i] = std::move(m);
    }
    for (auto& kv : xd)
      if (!kv.second.name.empty()) by_name[norm(kv.second.name)] = kv.first;
    for (int m = 1; m <= kLast; m++) {
      const uint8_t* b = p.move_row(m);
      PbrMove q;
      q.name = p.move_name(m);
      q.eff = (b[0] << 8) | b[1];
      q.cls = b[14];
      q.power = b[15];
      q.type = b[16];
      q.acc = b[17];
      q.pp = b[18];
      q.chance = b[19];
      q.pri = b[20] > 127 ? b[20] - 256 : b[20];
      q.flags = b[21];
      pbr[m] = q;
    }
    for (int m = 1; m < 355; m++) groups[pbr[m].eff].push_back(m);
  }
  int xid(const std::string& name) const {
    auto it = by_name.find(norm(name));
    if (it == by_name.end()) throw Fail("no XD move named " + name);
    return it->second;
  }
};

int best_donor(const Data& d, int m) {
  const PbrMove& p = d.pbr.at(m);
  const std::vector<int>& cands = d.groups.at(p.eff);
  std::vector<std::pair<int64_t, int>> counts;
  for (int c : cands) {
    const int64_t s = d.xd.at(c).get("seqId");
    bool found = false;
    for (auto& e : counts)
      if (e.first == s) {
        e.second++;
        found = true;
      }
    if (!found) counts.emplace_back(s, 1);
  }
  int64_t modal = counts[0].first;
  int best = counts[0].second;
  for (auto& e : counts)
    if (e.second > best) {
      best = e.second;
      modal = e.first;
    }
  int pick = -1;
  double pick_score = 0;
  for (int c : cands) {
    const PbrMove& q = d.pbr.at(c);
    const double score = (double)((d.xd.at(c).get("seqId") == modal) * 8 + (q.chance == p.chance) * 4 +
                                  ((q.flags & 1) == (p.flags & 1)) * 2 + (q.cls == p.cls)) -
                         c / 10000.0;
    if (pick < 0 || score > pick_score) {
      pick = c;
      pick_score = score;
    }
  }
  return pick;
}

struct Plan {
  int id;
  std::string name;
  int type, cls, power, acc, pp, pri, chance, eff;
  char kind;
  int donor;
  std::string donor_name, note;
};

void classify(const Data& d, int m, char& kind, int& donor, std::string& note) {
  const PbrMove& p = d.pbr.at(m);
  if (d.groups.count(p.eff)) {
    donor = best_donor(d, m);
    kind = 'a';
    note = "same effect as " + d.pbr.at(donor).name;
    return;
  }
  auto it = kMovePlan.find(p.name);
  if (it == kMovePlan.end()) throw Fail("no plan for Gen 4 move " + p.name);
  const char k = it->second.kind;
  const std::string dn = it->second.donor, nt = it->second.note;
  if (k == 'b' && !kImplemented.count(p.name)) {
    if (p.cls == 2) {
      kind = 'd';
      donor = -1;
      note = "effect not built yet";
    } else {
      kind = 'c';
      donor = d.xid("Pound");
      note = nt + " (not built yet: plain damage)";
    }
    return;
  }
  if (k == 'd') {
    kind = 'd';
    donor = -1;
    note = "not offered";
    return;
  }
  kind = k;
  donor = d.xid(dn);
  note = nt;
}

int anim_row(const Data& d, const std::string& name, int donor) {
  std::map<std::string, std::string> sub;
  for (const auto& kv : kMoveSubstitute) sub[norm(kv.first)] = kv.second;
  std::string srcs[2];
  auto a = kAnimDonor.find(name);
  if (a != kAnimDonor.end()) srcs[0] = a->second;
  auto s = sub.find(norm(name));
  if (s != sub.end()) srcs[1] = s->second;
  for (const std::string& src : srcs)
    if (!src.empty()) return d.xid(src);
  return donor;
}

Json make_row(const Data& d, int m, int donor) {
  const PbrMove& p = d.pbr.at(m);
  const XdMove& x = d.xd.at(donor);
  const std::string& name = p.name;
  int64_t power = p.power;
  auto fp = kFixedPower.find(name);
  if (fp != kFixedPower.end()) {
    power = fp->second;
  } else if (x.get("iryoku") <= 1 && power <= 1) {
    power = x.get("iryoku");
  } else if (p.cls != 2 && power <= 1) {
    power = 60;
  }
  Json f = Json::object();
  f.set("pp", p.pp);
  f.set("zokuseiDataId", p.type);
  f.set("iryoku", power);
  f.set("avg", p.acc ? p.acc : 100);
  f.set("pri", p.pri & 0xFF);
  if (x.get("addFightKoukaAvg")) f.set("addFightKoukaAvg", p.chance ? (int64_t)p.chance : x.get("addFightKoukaAvg"));
  if (kChance.count(name) && f.find("addFightKoukaAvg") != nullptr) f.set("addFightKoukaAvg", (int64_t)kChance.at(name));
  for (int bit = 0; bit < 6; bit++) f.set(kFlagFields[bit], (p.flags >> bit) & 1);
  if (kIgnoresProtect.count(name)) f.set("mamoru", 0);
  f.set("wazawzxdataId", d.xd.at(anim_row(d, name, donor)).get("wazawzxdataId"));
  f.set("pad37", p.cls == 0 ? 1 : p.cls == 1 ? 2 : 0);
  Json u = Json::array();
  u.push(0xA4);
  u.push(kPunch.count(name) ? 1 : 0);
  f.set("unk16", u);
  if (kEffectSlots.count(name) && kImplemented.count(name)) f.set("seqId", kEffectSlots.at(name));
  return f;
}

std::vector<Plan> make_plan(const Data& d) {
  std::vector<Plan> rows;
  for (int m = kFirst; m <= kLast; m++) {
    const PbrMove& p = d.pbr.at(m);
    Plan r;
    r.id = m;
    r.name = p.name;
    r.type = p.type;
    r.cls = p.cls;
    r.power = p.power;
    r.acc = p.acc;
    r.pp = p.pp;
    r.pri = p.pri;
    r.chance = p.chance;
    r.eff = p.eff;
    classify(d, m, r.kind, r.donor, r.note);
    r.donor_name = r.donor >= 0 ? d.xd.at(r.donor).name : "";
    rows.push_back(std::move(r));
  }
  return rows;
}

}

std::string gen4_moves_classification(const PbrData& pbr, XdTables& xd) {
  Data d(pbr, xd);
  std::string out = "pbr_id\tname\ttype\tclass\tpower\tacc\tpp\tpri\tchance\teffect\tkind\tdonor\tnote\n";
  for (const Plan& r : make_plan(d)) {
    char buf[64];
    std::string t = r.type < 18 ? kTypeNames[r.type] : std::to_string(r.type);
    std::string c = r.cls < 3 ? kClassNames[r.cls] : "?";
    std::snprintf(buf, sizeof buf, "%d\t", r.id);
    out += buf + r.name + "\t" + t + "\t" + c;
    std::snprintf(buf, sizeof buf, "\t%d\t%d\t%d\t%d\t%d\t%d\t", r.power, r.acc, r.pp, r.pri, r.chance, r.eff);
    out += buf;
    out += std::string(1, r.kind) + "\t" + r.donor_name + "\t" + r.note + "\n";
  }
  return out;
}

MovesOut gen4_moves(const PbrData& pbr, XdTables& xd) {
  Data d(pbr, xd);
  const std::vector<Plan> every = make_plan(d);
  MovesOut out;
  Json table = Json::object();
  for (const Plan& r : every) {
    if (r.kind != 'd') out.built.insert(r.name);
    Json row = Json::object();
    if (r.kind == 'd') {
      const PbrMove& p = d.pbr.at(r.id);
      row.set("copy", d.xd.at(d.xid("Splash")).name);
      row.set("pp", p.pp);
      row.set("zokuseiDataId", p.type);
      Json u = Json::array();
      u.push(0xA4);
      u.push(0);
      row.set("unk16", u);
      Json t = Json::object();
      t.set("name", gen4_move_name_text(r.name));
      t.set("doc", "Not built yet: does nothing.");
      row.set("text", t);
    } else {
      row.set("copy", d.xd.at(r.donor).name);
      Json f = make_row(d, r.id, r.donor);
      for (auto& kv : f.o) row.set(kv.first, kv.second);
      Json t = Json::object();
      t.set("name", gen4_move_name_text(r.name));
      t.set("doc", gen4_move_doc(r.name));
      row.set("text", t);
    }
    table.set(r.name, row);
  }
  Json add = Json::object();
  add.set("moves", table);
  Json doc = Json::object();
  doc.set("_note", "generated by tools/pbr_convert/gen4_moves.py from the PBR disc: game data, keep out of the repository");
  doc.set("add", add);
  out.json = doc.dump(1);
  return out;
}

}
