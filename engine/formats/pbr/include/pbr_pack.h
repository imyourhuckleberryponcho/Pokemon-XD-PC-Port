#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "pbr_batch.h"

namespace pbr {

class Json {
 public:
  enum Kind { Null, Bool, Int, Str, Arr, Obj } kind = Null;
  bool b = false;
  int64_t i = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;
  Json() = default;
  Json(bool v) : kind(Bool), b(v) {}
  Json(int v) : kind(Int), i(v) {}
  Json(int64_t v) : kind(Int), i(v) {}
  Json(uint32_t v) : kind(Int), i(v) {}
  Json(size_t v) : kind(Int), i((int64_t)v) {}
  Json(const char* v) : kind(Str), s(v) {}
  Json(const std::string& v) : kind(Str), s(v) {}
  static Json array() { Json j; j.kind = Arr; return j; }
  static Json object() { Json j; j.kind = Obj; return j; }
  Json& push(Json v) { a.push_back(std::move(v)); return a.back(); }
  Json& set(const std::string& key, Json v);
  Json* find(const std::string& key);
  const Json* find(const std::string& key) const;
  std::string dump(int indent = 1) const;
};

struct PbrPersonal {
  int hp = 0, atk = 0, de = 0, spe = 0, spa = 0, spd = 0, t1 = 0, t2 = 0, catch_rate = 0, exp = 0;
  int ev_hp = 0, ev_atk = 0, ev_def = 0, ev_spe = 0, ev_spa = 0, ev_spd = 0;
  int gender = 0, hatch = 0, friendship = 0, growth = 0, egg[2] = {0, 0}, ab[2] = {0, 0}, items[2] = {0, 0}, height = 0, weight = 0;
  std::vector<int> tm;
};
struct PbrEvo { int method, param, species; };
class PbrData {
 public:
  explicit PbrData(const DiscSource& pbr);
  std::vector<uint8_t> common8, common9, common29, common30, common32;
  std::vector<std::string> text;
  std::vector<int> tmlist;
  std::string species_name(int dex) const { return text.at(9 + dex); }
  std::string move_name(int m) const { return text.at(505 + m); }
  std::string item_name(int i) const { return text.at(1990 + i); }
  std::string ability_name(int a) const { return a ? text.at(2989 + a) : std::string(); }
  PbrPersonal personal(int dex) const;
  std::vector<std::pair<int, int>> learnset(int dex) const;
  std::vector<PbrEvo> evolutions(int dex) const;
  const uint8_t* move_row(int m) const { return common30.data() + 0x40 + (size_t)m * 24; }
};

struct XdField {
  std::string name;
  std::vector<uint32_t> values;
};
class XdTables {
 public:
  virtual ~XdTables() = default;
  virtual std::vector<std::pair<uint32_t, std::string>> names(const std::string& list) = 0;
  virtual bool row(const std::string& table, uint32_t entry, std::string& entry_name, std::vector<XdField>& fields) = 0;
};

struct AbilitySub { const char* name; int xd; };
extern const std::vector<std::pair<std::string, std::string>> kMoveSubstitute;
extern const std::map<int, AbilitySub> kAbilitySubstitute;
extern const std::set<std::string> kBabies;
constexpr int kFallbackLevelBaby = 20, kFallbackLevel = 40;
struct SpotAdd { const char* name; int prob, rate_of; };
struct SpotTable { std::string table; int probs[3]; std::vector<SpotAdd> adds; int top; int rates[3]; };
extern const std::vector<SpotTable> kSpots;
extern const std::map<int, std::string> kCategory;

struct MovePlanEntry { char kind; const char* donor; const char* note; };
extern const std::map<std::string, MovePlanEntry> kMovePlan;
extern const std::set<std::string> kPunch, kImplemented, kIgnoresProtect;
extern const std::map<std::string, int> kFixedPower, kEffectSlots, kChance;
extern const std::map<std::string, std::string> kAnimDonor;

struct MovesOut {
  std::string json;
  std::set<std::string> built;
};
MovesOut gen4_moves(const PbrData& pbr, XdTables& xd);
std::string gen4_moves_classification(const PbrData& pbr, XdTables& xd);

std::string gen4_move_name_text(const std::string& name);
std::string gen4_move_doc(const std::string& name);
std::string gen4_item_doc(const std::string& name);

struct MachinesOut {
  std::vector<std::pair<std::string, Json>> items;
  std::vector<std::string> coupon;
  std::string text;
};
MachinesOut gen4_machines(const PbrData& pbr, XdTables& xd, const std::vector<std::pair<std::string, int>>& new_species);

struct ItemsOut {
  std::string json;
  std::vector<std::string> goods;
};
ItemsOut gen4_items(XdTables& xd, const MachinesOut& machines);

struct CryOut { int dex; std::vector<uint8_t> hdr, body; };
std::vector<CryOut> gen4_cries(const DiscSource& pbr);

struct FaceOut { int dex; std::vector<uint8_t> data; };
std::vector<FaceOut> gen4_faces(const DiscSource& pbr, const DiscSource& xd);

struct IconOut { int dex; std::vector<uint8_t> normal, shiny; };
std::vector<IconOut> gen4_icons(const DiscSource& pbr, const DiscSource& xd);
constexpr int kIconFirstId = 0x2200;
constexpr int kWaveOld = 389;

std::vector<uint8_t> gen4_wave(const CryOut& cry);

struct PackModel { int dex = 0; std::string name, standin; std::vector<uint8_t> data; };
void make_gen4_pack(const std::string& out_folder, const DiscSource& pbr, const DiscSource& xd, XdTables& xdt,
                    const std::vector<PackModel>& models, const std::vector<PairRow>& pairs,
                    const std::function<void(const std::string&)>& say);

}
