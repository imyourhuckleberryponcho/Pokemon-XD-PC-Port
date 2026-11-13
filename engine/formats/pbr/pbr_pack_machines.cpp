#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pbr_pack.h"

namespace pbr {
namespace {

constexpr int kFirstMachine = 58;
constexpr int kFirstExtraBit = 50;
constexpr int kLastExtraBit = 91;
constexpr int kGen4MoveShift = 20;

struct CouponDef { const char* move; int coupon; };
const CouponDef kCoupon[] = {
    {"Stone Edge", 4500}, {"Focus Blast", 4500}, {"Energy Ball", 4000}, {"Dragon Pulse", 4000}, {"Dark Pulse", 4000},
    {"Flash Cannon", 4000}, {"Swords Dance", 4000}, {"Trick Room", 4000}, {"X-Scissor", 3500}, {"Poison Jab", 3500},
    {"Shadow Claw", 3500}, {"Drain Punch", 3500}, {"Rock Slide", 3500}, {"Giga Impact", 3500}, {"Explosion", 3500},
    {"U-turn", 3500}, {"Roost", 3500}, {"Stealth Rock", 3500}, {"Gyro Ball", 3000}, {"Avalanche", 3000},
    {"Grass Knot", 3000}, {"Charge Beam", 3000}, {"Payback", 3000}, {"Brine", 3000}, {"Silver Wind", 3000},
    {"Rock Polish", 3000}, {"Substitute", 3000}, {"Thunder Wave", 2500}, {"Will-O-Wisp", 2500}, {"Sleep Talk", 2500},
    {"Dream Eater", 2500}, {"Swagger", 2500}, {"Psych Up", 2000}, {"Recycle", 2000}, {"Natural Gift", 2000},
    {"False Swipe", 2000}, {"Pluck", 2000}, {"Endure", 1500}, {"Embargo", 1500}, {"Captivate", 1500}, {"Fling", 1500},
};

struct Machine { int no, tm, xd_move; std::string name; int bit; };

uint32_t first_value(XdTables& xd, const char* table, uint32_t entry, const char* field) {
  std::string name;
  std::vector<XdField> fields;
  if (!xd.row(table, entry, name, fields)) throw Fail(std::string("XD's ") + table + " has no entry " + std::to_string(entry));
  for (const XdField& f : fields)
    if (f.name == field && !f.values.empty()) return f.values[0];
  throw Fail(std::string("XD's ") + table + " has no field " + field);
}

std::string underscored(std::string s) {
  for (char& c : s)
    if (c == ' ') c = '_';
  return s;
}

}

MachinesOut gen4_machines(const PbrData& pbr, XdTables& xd, const std::vector<std::pair<std::string, int>>& new_species) {
  std::set<int> have;
  for (uint32_t i = 0; i < 58; i++) have.insert((int)first_value(xd, "tm_moves", i, "wazaID"));
  std::vector<Machine> mach;
  for (int k = kFirstExtraBit; k <= kLastExtraBit; k++) {
    const int p = pbr.tmlist.at((size_t)k);
    const int x = p <= 354 ? p : p + kGen4MoveShift;
    if (have.count(x)) continue;
    mach.push_back({kFirstMachine + (int)mach.size(), k + 1, x, pbr.move_name(p), k});
  }
  MachinesOut out;
  for (const Machine& m : mach) {
    int price = -1;
    for (const CouponDef& c : kCoupon)
      if (m.name == c.move) price = c.coupon;
    if (price < 0) throw Fail("no coupon value for the machine " + m.name);
    char item[16];
    std::snprintf(item, sizeof item, "TM%02d", m.tm);
    Json text = Json::object();
    text.set("name", item);
    Json row = Json::object();
    row.set("copy", "TM01");
    row.set("price", price >= 3000 ? 3000 : 2000);
    row.set("coupon", price);
    row.set("buff", m.no);
    text.set("doc", gen4_item_doc(m.name));
    row.set("text", std::move(text));
    out.items.emplace_back(item, std::move(row));
    out.coupon.push_back(item);
  }
  out.text = "format 1\n";
  for (const Machine& m : mach) {
    out.text += "machine " + std::to_string(m.no) + " " + std::to_string(m.tm) + " " +
                (m.xd_move <= 354 ? std::to_string(m.xd_move) : underscored(m.name)) + "\n";
  }
  auto bits = [&](int dex) {
    const std::vector<int> tm = pbr.personal(dex).tm;
    std::string b;
    for (const Machine& m : mach) b += std::find(tm.begin(), tm.end(), m.bit) != tm.end() ? '1' : '0';
    return b;
  };
  for (const auto& s : new_species) {
    const std::string b = bits(s.second);
    if (b.find('1') != std::string::npos) out.text += "bits " + underscored(s.first) + " " + b + "\n";
  }
  std::map<int, int> nat2xd;
  for (int s = 1; s < 415; s++) {
    const int n = (int)first_value(xd, "pokemon_data", (uint32_t)s, "numPokemon");
    if (n) nat2xd[n] = s;
  }
  std::vector<std::pair<int, int>> existing;
  for (const auto& kv : nat2xd) existing.emplace_back(kv.second, kv.first);
  std::sort(existing.begin(), existing.end());
  for (const auto& e : existing) {
    const std::string b = bits(e.second);
    if (b.find('1') != std::string::npos) out.text += "bits #" + std::to_string(e.first) + " " + b + "\n";
  }
  return out;
}

}
