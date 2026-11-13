#include "pbr_pack_species.h"

#include <algorithm>
#include <cstring>

namespace pbr {
namespace {

constexpr int kGen4First = 387, kGen4Last = 493, kBonsly = 438, kMunchlax = 446, kXdBonsly = 413, kXdMunchlax = 414;
constexpr int kFirstNewId = 415, kFirstNewPkx = 419, kFirstNewNameMsg = 1391, kFlagBase = 2324;

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
void put16(std::vector<uint8_t>& r, size_t o, unsigned v) { r[o] = (uint8_t)(v >> 8); r[o + 1] = (uint8_t)v; }
void put32(std::vector<uint8_t>& r, size_t o, uint32_t v) {
  r[o] = (uint8_t)(v >> 24); r[o + 1] = (uint8_t)(v >> 16); r[o + 2] = (uint8_t)(v >> 8); r[o + 3] = (uint8_t)v;
}

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

class Xd {
 public:
  Xd(XdTables& t, const std::vector<uint8_t>& common_fsys) {
    std::vector<uint8_t> rel;
    for (const FsysEntry& e : fsys_entries(common_fsys))
      if (e.name == "common_rel") {
        rel = fsys_extract(common_fsys, e);
        break;
      }
    static const uint8_t sig[12] = {0, 35, 0, 55, 0, 30, 0, 50, 0, 40, 0, 90};
    size_t at = std::string::npos;
    for (size_t i = 0; i + 12 <= rel.size(); i++)
      if (std::memcmp(&rel[i], sig, 12) == 0) {
        at = i;
        break;
      }
    if (at == std::string::npos || at < 0x8E + 25 * (size_t)kRecSize) throw Fail("pokemon_data not found in common_rel");
    base_ = at - 0x8E - 25 * (size_t)kRecSize;
    static const uint8_t bulb[12] = {0, 45, 0, 49, 0, 49, 0, 65, 0, 65, 0, 45};
    if (std::memcmp(&rel[base_ + kRecSize + 0x8E], bulb, 12) != 0) throw Fail("pokemon_data is not where expected in common_rel");
    rel_ = std::move(rel);
    for (auto& kv : t.names("move")) moves[kv.first] = kv.second;
    for (auto& kv : t.names("item")) items[kv.first] = kv.second;
    for (uint32_t i = 0; i < 58; i++) tm_moves.push_back((int)first_value(t, "tm_moves", i, "wazaID"));
    for (uint32_t i = 0; i < 12; i++) tutors.push_back((int)first_value(t, "wazaoshie_data", i, "id"));
    for (auto& kv : moves) move_by_name[norm(kv.second)] = kv.first;
    for (auto& kv : items) item_by_name[norm(kv.second)] = kv.first;
    for (int s = 1; s < 415; s++) {
      int n = be16(&rel_[base_ + (size_t)s * kRecSize + 0x12]);
      if (n) nat2xd[n] = s;
    }
  }
  std::vector<uint8_t> rec(int s) const {
    const uint8_t* p = &rel_[base_ + (size_t)s * kRecSize];
    return std::vector<uint8_t>(p, p + kRecSize);
  }
  struct Sinka { int kind, a, buff, target; };
  std::vector<Sinka> sinka(int s) const {
    std::vector<uint8_t> r = rec(s);
    std::vector<Sinka> out;
    for (int k = 0; k < 5; k++) out.push_back({r[0xA6 + 6 * k], r[0xA7 + 6 * k], be16(&r[0xA8 + 6 * k]), be16(&r[0xAA + 6 * k])});
    return out;
  }
  std::map<uint32_t, std::string> moves, items;
  std::vector<int> tm_moves, tutors;
  std::map<std::string, uint32_t> move_by_name, item_by_name;
  std::map<int, int> nat2xd;

 private:
  static uint32_t first_value(XdTables& t, const char* table, uint32_t i, const char* field) {
    std::string nm;
    std::vector<XdField> f;
    if (!t.row(table, i, nm, f)) throw Fail(std::string("XD has no ") + table + " entry " + std::to_string(i));
    for (const XdField& x : f)
      if (x.name == field && !x.values.empty()) return x.values[0];
    throw Fail(std::string("XD's ") + table + " has no field " + field);
  }
  std::vector<uint8_t> rel_;
  size_t base_ = 0;
};

struct MoveRow { int xd; bool same_id; };

class Conv {
 public:
  Conv(const PbrData& p, const Xd& x, const std::map<int, PlanIn>& plan, const std::set<std::string>& built)
      : p_(p), x_(x), plan_(plan) {
    int nxt = kFirstNewId;
    for (int d = kGen4First; d <= kGen4Last; d++) {
      if (d == kBonsly) dex_to_id[d] = kXdBonsly;
      else if (d == kMunchlax) dex_to_id[d] = kXdMunchlax;
      else dex_to_id[d] = nxt++;
    }
    for (int d = kGen4First; d <= kGen4Last; d++)
      if (d != kBonsly && d != kMunchlax) new_ids.push_back(d);
    int k = 0;
    for (int d = kGen4First; d <= kGen4Last; d++) {
      if (d == kBonsly) continue;
      pkx_index[d] = kFirstNewPkx + k++;
    }
    id_of = x_.nat2xd;
    for (auto& kv : dex_to_id) id_of[kv.first] = kv.second;
    for (auto& kv : id_of) dex_of_id[kv.second] = kv.first;
    std::map<std::string, std::string> sub;
    for (auto& kv : kMoveSubstitute) sub[norm(kv.first)] = kv.second;
    for (int m = 1; m < 468; m++) {
      const std::string name = p_.move_name(m);
      MoveRow row{0, false};
      if (m <= 354) {
        row.xd = x_.moves.count(m) ? m : 0;
        row.same_id = true;
        if (norm(x_.moves.count(m) ? x_.moves.at(m) : std::string()) != norm(name)) row.same_id = false;
      } else if (built.count(name)) {
        row.xd = m + (375 - 355);
      } else {
        auto it = sub.find(norm(name));
        if (it == sub.end()) throw Fail("no substitute rule for Gen 4 move " + std::to_string(m) + " " + name);
        if (it->second.empty()) {
          row.xd = 0;
        } else {
          auto b = x_.move_by_name.find(norm(it->second));
          if (b == x_.move_by_name.end()) throw Fail("no XD move named " + it->second);
          row.xd = (int)b->second;
        }
      }
      move_rows[m] = row;
    }
    for (int i = 1; i < 600; i++) {
      if (1990 + i >= (int)p_.text.size()) break;
      auto it = x_.item_by_name.find(norm(p_.item_name(i)));
      item_xd[i] = it == x_.item_by_name.end() ? 0 : (int)it->second;
    }
    std::map<int, int> tm_index;
    for (size_t k2 = 0; k2 < p_.tmlist.size(); k2++) tm_index[p_.tmlist[k2]] = (int)k2;
    for (size_t i = 0; i < x_.tm_moves.size(); i++) {
      auto it = tm_index.find(x_.tm_moves[i]);
      machine.push_back({x_.tm_moves[i], it == tm_index.end() ? -1 : it->second});
    }
  }

  int ability(int a) const {
    if (a == 0 || a <= 76) return a;
    return kAbilitySubstitute.count(a) != 0 ? a : 0;
  }

  struct Evo { int kind, buff, target; };
  bool evo_entry(int src_dex, int m, int par, int tgt, Evo& out) const {
    auto it = id_of.find(tgt);
    if (it == id_of.end()) return false;
    const int tid = it->second;
    bool baby = false;
    const std::string sn = norm(p_.species_name(src_dex));
    for (const std::string& b : kBabies)
      if (norm(b) == sn) baby = true;
    const int fb = baby ? kFallbackLevelBaby : kFallbackLevel;
    if (m == 1 || m == 2 || m == 3) { out = {1, 0, tid}; return true; }
    if (m == 4) { out = {4, par, tid}; return true; }
    if (m == 5) { out = {5, 0, tid}; return true; }
    if (m == 6 || m == 7) {
      auto ii = item_xd.find(par);
      const int xi = ii == item_xd.end() ? 0 : ii->second;
      if (xi) { out = {m, xi, tid}; return true; }
      out = {4, fb, tid};
      return true;
    }
    if (m == 8 || m == 9 || m == 10 || m == 11 || m == 12 || m == 15) { out = {m, par, tid}; return true; }
    if (m == 13) { out = {4, par, tid}; return true; }
    if (m == 14) { out = {14, par, tid}; return true; }
    if (m == 22) { out = {12, par, tid}; return true; }
    if (m == 23) { out = src_dex == 412 ? Evo{11, par, tid} : Evo{4, par, tid}; return true; }
    out = {4, fb, tid};
    return true;
  }

  SpeciesRec species(int dex) const {
    const PbrPersonal pr = p_.personal(dex);
    const int sid = dex_to_id.at(dex);
    const bool existing = sid == kXdBonsly || sid == kXdMunchlax;
    auto pl = plan_.find(dex);
    const bool stand = pl != plan_.end();
    int standin_id = 0;
    if (stand && pl->second.standin_nat) {
      auto n = x_.nat2xd.find(pl->second.standin_nat);
      if (n != x_.nat2xd.end()) standin_id = n->second;
    }
    std::vector<uint8_t> sref = standin_id ? x_.rec(standin_id) : std::vector<uint8_t>(kRecSize, 0);
    std::vector<uint8_t> r(kRecSize, 0);
    std::vector<uint8_t> old;
    if (existing) old = x_.rec(sid);
    r[0] = pr.growth < 6 ? pr.growth : 0;
    r[1] = pr.catch_rate;
    r[2] = pr.gender;
    put16(r, 0x04, pr.exp);
    put16(r, 0x06, pr.friendship);
    put16(r, 0x08, pr.height);
    put16(r, 0x0A, pr.weight);
    int idx = -1;
    for (size_t k = 0; k < new_ids.size(); k++)
      if (new_ids[k] == dex) idx = (int)k;
    const int order = dex - kGen4First;
    if (existing) {
      put16(r, 0x0C, be16(&old[0x0C]));
      put16(r, 0x10, be16(&old[0x10]));
      put32(r, 0x18, be32(&old[0x18]));
    } else {
      put16(r, 0x0C, be16(&sref[0x0C]));
      put16(r, 0x10, be16(&sref[0x10]));
      put32(r, 0x18, kFirstNewNameMsg + idx);
    }
    put16(r, 0x12, dex);
    put16(r, 0x0E, dex);
    put16(r, 0x14, 387 + order);
    put32(r, 0x1C, stand ? be32(&sref[0x1C]) : 0);
    put32(r, 0x20, 0);
    const int mita = kFlagBase + order, tuka = kFlagBase + (kGen4Last - kGen4First + 1) + order;
    put32(r, 0x24, mita);
    put32(r, 0x28, tuka);
    if (sid == kXdBonsly) put32(r, 0x2C, be32(&old[0x2C]));
    else put32(r, 0x2C, pkx_index.at(dex));
    r[0x30] = pr.t1;
    r[0x31] = pr.t2;
    int a1 = ability(pr.ab[0]), a2 = ability(pr.ab[1]);
    if (a2 == a1) a2 = 0;
    r[0x32] = a1;
    r[0x33] = a2;
    std::set<int> learned;
    for (auto& e : p_.learnset(dex)) learned.insert(e.second);
    std::set<int> tmbits(pr.tm.begin(), pr.tm.end());
    for (size_t i = 0; i < machine.size(); i++) {
      bool ok;
      if (machine[i].pbr_bit >= 0) ok = tmbits.count(machine[i].pbr_bit) != 0;
      else ok = learned.count(machine[i].xd_move) != 0;
      r[0x34 + i] = ok ? 1 : 0;
    }
    std::map<int, int> tm_of;
    for (size_t k2 = 0; k2 < p_.tmlist.size(); k2++) tm_of[p_.tmlist[k2]] = (int)k2;
    for (size_t i = 0; i < x_.tutors.size(); i++) {
      auto f = tm_of.find(x_.tutors[i]);
      const bool by_machine = f != tm_of.end() && tmbits.count(f->second) != 0;
      r[0x6E + i] = (learned.count(x_.tutors[i]) || by_machine) ? 1 : 0;
    }
    for (int k = 0; k < 2; k++) {
      const int it = pr.items[k];
      int xi = 0;
      if (it) {
        auto f = item_xd.find(it);
        xi = f == item_xd.end() ? 0 : f->second;
      }
      put16(r, 0x7A + 2 * k, xi);
    }
    const int stats[6] = {pr.hp, pr.atk, pr.de, pr.spa, pr.spd, pr.spe};
    for (int k = 0; k < 6; k++) put16(r, 0x8E + 2 * k, stats[k]);
    const int ev[6] = {pr.ev_hp, pr.ev_atk, pr.ev_def, pr.ev_spa, pr.ev_spd, pr.ev_spe};
    for (int k = 0; k < 6; k++) put16(r, 0x9A + 2 * k, ev[k]);
    std::vector<Evo> evo;
    for (const PbrEvo& e : p_.evolutions(dex)) {
      if (e.species < kGen4First && !id_of.count(e.species)) continue;
      Evo x;
      if (evo_entry(dex, e.method, e.param, e.species, x)) evo.push_back(x);
    }
    for (size_t k = 0; k < evo.size() && k < 5; k++) {
      r[0xA6 + 6 * k] = evo[k].kind;
      r[0xA7 + 6 * k] = 0;
      put16(r, 0xA8 + 6 * k, evo[k].buff);
      put16(r, 0xAA + 6 * k, evo[k].target);
    }
    std::vector<std::pair<int, int>> ls;
    std::set<int> seen;
    for (auto& e : p_.learnset(dex)) {
      const MoveRow& row = move_rows.at(e.second);
      if (row.xd == 0) continue;
      if (seen.count(row.xd)) continue;
      seen.insert(row.xd);
      ls.emplace_back(e.first, row.xd);
    }
    std::stable_sort(ls.begin(), ls.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
    for (size_t k = 0; k < ls.size() && k < 20; k++) {
      r[0xC4 + 4 * k] = ls[k].first;
      put16(r, 0xC6 + 4 * k, ls[k].second);
    }
    for (int i = 0; i < 2; i++) {
      const size_t o = 0x114 + 8 * i;
      const std::vector<uint8_t>& src = existing ? old : sref;
      std::copy(src.begin() + o, src.begin() + o + 8, r.begin() + o);
    }
    SpeciesRec s;
    s.dex = dex;
    s.xd_species_id = sid;
    s.name = p_.species_name(dex);
    s.has_model = stand;
    if (stand) s.model = pl->second.model;
    s.standin_species_id = standin_id;
    s.rec = std::move(r);
    s.mita = mita;
    s.tuka = tuka;
    return s;
  }

  struct Machine { int xd_move, pbr_bit; };
  std::map<int, int> dex_to_id, pkx_index, id_of, dex_of_id, item_xd;
  std::vector<int> new_ids;
  std::map<int, MoveRow> move_rows;
  std::vector<Machine> machine;

 private:
  const PbrData& p_;
  const Xd& x_;
  const std::map<int, PlanIn>& plan_;
};

}

SpeciesSet gen4_species(const PbrData& pbr, XdTables& xdt, const std::vector<uint8_t>& xd_common_fsys,
                        const std::map<int, PlanIn>& plan, const std::set<std::string>& built) {
  Xd x(xdt, xd_common_fsys);
  Conv c(pbr, x, plan, built);
  SpeciesSet out;
  for (int dex = kGen4First; dex <= kGen4Last; dex++) out.species.push_back(c.species(dex));
  std::sort(out.species.begin(), out.species.end(),
            [](const SpeciesRec& a, const SpeciesRec& b) { return a.xd_species_id < b.xd_species_id; });
  out.xd_rec1 = x.rec(1);
  for (int dex = 1; dex < kGen4First; dex++) {
    auto ns = x.nat2xd.find(dex);
    if (ns == x.nat2xd.end()) continue;
    const int sid = ns->second;
    for (const PbrEvo& e : pbr.evolutions(dex)) {
      if (e.species < kGen4First || e.species > kGen4Last) continue;
      Conv::Evo ev;
      const bool ok = c.evo_entry(dex, e.method, e.param, e.species, ev);
      const auto cur = x.sinka(sid);
      int slot = -1;
      for (int k = 0; k < 5; k++)
        if (cur[k].kind == 0 && cur[k].target == 0) {
          slot = k;
          break;
        }
      if (!ok || slot < 0) {
        out.patches.push_back({sid, dex, -1, 0, 0, e.species, pbr.species_name(e.species)});
        continue;
      }
      out.patches.push_back({sid, dex, slot, ev.kind, ev.buff, e.species, pbr.species_name(e.species)});
    }
  }
  for (int sid = 1; sid < 412; sid++) {
    const std::vector<uint8_t> r = x.rec(sid);
    const int dex = be16(&r[0x12]);
    if (!dex || dex >= kGen4First) continue;
    const PbrPersonal pr = pbr.personal(dex);
    const std::set<int> tbits(pr.tm.begin(), pr.tm.end());
    ExistingMachines m;
    m.xd_species_id = sid;
    for (size_t i = 0; i < c.machine.size(); i++)
      if (c.machine[i].pbr_bit >= 0 && tbits.count(c.machine[i].pbr_bit) && !r[0x34 + i]) m.machines.push_back((int)i);
    if (!m.machines.empty()) out.machines.push_back(m);
  }
  return out;
}

}
