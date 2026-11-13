#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "pbr_pack.h"

namespace pbr {
namespace {

constexpr uint32_t kShopGoodsCount = 184;
constexpr int kCouponShops[] = {9, 10, 11};

#define POKEMON "Pok\303\251mon"
#define HELD "A held item for{00}evolution. It does{00}nothing in battle."
struct ItemDef { const char *name, *copy; int price, coupon; const char* doc; };
const ItemDef kItems[] = {
    {"Ice Stone", "Thunder Stone", 2100, 6000, "A frosty stone that{00}evolves certain{00}" POKEMON " when used."},
    {"Dusk Stone", "Thunder Stone", 2100, 6000, "A dark stone that{00}evolves certain{00}" POKEMON " when used."},
    {"Dawn Stone", "Thunder Stone", 2100, 6000, "A bright stone that{00}evolves certain{00}" POKEMON " when used."},
    {"Shiny Stone", "Thunder Stone", 2100, 6000, "A shiny stone that{00}evolves certain{00}" POKEMON " when used."},
    {"Razor Claw", "Scope Lens", 100, 8000, "Raises the holder's{00}critical-hit stage.{00}Helps evolution."},
    {"Razor Fang", "King's Rock", 100, 8000, "Held: may make the{00}target flinch.{00}Helps evolution."},
    {"Protector", "Red Scarf", 100, 10000, HELD},
    {"Electirizer", "Red Scarf", 100, 10000, HELD},
    {"Magmarizer", "Red Scarf", 100, 10000, HELD},
    {"Reaper Cloth", "Red Scarf", 100, 10000, HELD},
    {"Dubious Disc", "Red Scarf", 100, 10000, HELD},
    {"Oval Stone", "Red Scarf", 100, 6000, HELD},
    {"Linking Cord", "Thunder Stone", 3000, 8000, "Acts as a trade for{00}evolution. Use on a{00}" POKEMON "."},
};
const char* const kCouponExtra[] = {"Razor Claw", "Razor Fang", "Protector", "Electirizer", "Magmarizer", "Reaper Cloth", "Dubious Disc",
                                    "Oval Stone", "Ice Stone", "Dusk Stone", "Dawn Stone", "Shiny Stone", "Linking Cord"};

uint32_t table_value(XdTables& xd, const char* table, uint32_t entry, const char* field) {
  std::string name;
  std::vector<XdField> fields;
  if (!xd.row(table, entry, name, fields)) throw Fail(std::string("XD's ") + table + " has no entry " + std::to_string(entry));
  for (const XdField& f : fields)
    if (f.name == field && !f.values.empty()) return f.values[0];
  throw Fail(std::string("XD's ") + table + " has no field " + field);
}

}

ItemsOut gen4_items(XdTables& xd, const MachinesOut& machines) {
  Json items = Json::object();
  for (const ItemDef& d : kItems) {
    Json text = Json::object();
    std::string upper = d.name;
    for (char& c : upper) c = (char)std::toupper((unsigned char)c);
    text.set("name", upper);
    text.set("doc", d.doc);
    Json one = Json::object();
    one.set("copy", d.copy);
    one.set("price", d.price);
    one.set("coupon", d.coupon);
    one.set("text", std::move(text));
    items.set(d.name, std::move(one));
  }
  for (const auto& m : machines.items) items.set(m.first, m.second);
  Json shop_edit = Json::object();
  ItemsOut out;
  uint32_t cursor = kShopGoodsCount;
  auto key = [](std::string n) {
    for (char& c : n)
      if (c == ' ') c = '_';
    return n;
  };
  auto new_list = [&](const int* shops, size_t n_shops, const char* const* extra, size_t n_extra) {
    std::set<uint32_t> firsts;
    for (size_t i = 0; i < n_shops; i++) firsts.insert(table_value(xd, "shopData", (uint32_t)shops[i], "goodsIdx"));
    if (firsts.size() != 1) throw Fail("the shops of one new list do not share a list");
    const uint32_t first = *firsts.begin();
    std::vector<uint32_t> orig;
    while (table_value(xd, "shopGoods", first + (uint32_t)orig.size(), "value") != 0) orig.push_back(first + (uint32_t)orig.size());
    const uint32_t start = cursor;
    for (uint32_t row : orig) out.goods.push_back("goodscopy " + std::to_string(row));
    for (size_t i = 0; i < n_extra; i++) out.goods.push_back("goods " + key(extra[i]));
    out.goods.push_back("goods 0");
    cursor += (uint32_t)(orig.size() + n_extra + 1);
    for (size_t i = 0; i < n_shops; i++) {
      Json e = Json::object();
      e.set("goodsIdx", start);
      shop_edit.set("#" + std::to_string(shops[i]), std::move(e));
    }
  };
  std::vector<const char*> coupon(std::begin(kCouponExtra), std::end(kCouponExtra));
  for (const std::string& n : machines.coupon) coupon.push_back(n.c_str());
  new_list(kCouponShops, sizeof kCouponShops / sizeof kCouponShops[0], coupon.data(), coupon.size());
  Json add = Json::object();
  add.set("items", std::move(items));
  Json doc = Json::object();
  doc.set("_note", "Gen 4 items: copies of rows of the disc with our own names, prices and texts; the shops list them (tables.txt goods lines)");
  doc.set("add", std::move(add));
  doc.set("shopData", std::move(shop_edit));
  out.json = doc.dump(1);
  return out;
}

}
