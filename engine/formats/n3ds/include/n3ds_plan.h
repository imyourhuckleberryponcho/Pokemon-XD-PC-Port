#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "n3ds_dump.h"
#include "pbr_batch.h"

namespace n3ds {

struct JV {
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JV> arr;
  struct Member;
  std::vector<Member> obj;
  const JV* get(const std::string& k) const;
  const JV& at(const std::string& k) const;
  int as_int() const { return (int)num; }
};
struct JV::Member {
  std::string first;
  JV second;
  Member(std::string k, JV v) : first(std::move(k)), second(std::move(v)) {}
};
JV json_parse(const std::string& text);
JV json_read_file(const std::string& path);

std::string dump_fingerprint(const std::string& path);
std::string sha1_hex(const uint8_t* d, size_t n);

std::unique_ptr<pbr::DiscSource> open_iso(const std::string& path);
std::vector<uint8_t> disc_dol(const pbr::DiscSource& d);

struct XdModel {
  std::string arch;
  int idx = 0;
  std::string name;
};
class XdSide {
 public:
  explicit XdSide(const pbr::DiscSource& disc);
  std::map<int, XdModel> species;
  std::shared_ptr<const Bytes> pkx_of(int nat) const;
 private:
  const pbr::DiscSource& disc_;
  mutable std::mutex mu_;
};

struct PersonalRec {
  int type1 = 0, type2 = 0, form_stats_index = 0, height_cm = 0;
};
PersonalRec read_personal(const Garc& personal, int record);
std::optional<std::string> model_texture_name(const Garc& models, size_t k);
std::map<int, std::pair<int, std::string>> scan_groups(const Garc& models, int threads = 8);

struct PlanItem {
  std::string pack;
  std::string name;
  std::string model;
  std::string arch;
  int idx = 0, nat = 0, group = -1;
  double height = 0;
  int ladder = 0;
  uint32_t gid = 0, res = 0;
  std::string standin_path;
  int standin_nat = 0;
  std::string skip;
};
struct PlanOptions {
  std::string rom, data_dir;
  std::set<std::string> packs{"usum", "gen4", "g57", "form"};
  std::set<int> only;
  std::function<void(const std::string&)> log;
};
struct Plan {
  std::vector<PlanItem> items;
  size_t species_in_xd = 0, groups_scanned = 0, direct = 0;
  std::map<int, int> lookup;
  std::vector<int> no_model_in_dump;
};
Plan make_plan(const RomFs& rom, const Detected& det, const Garc& models, const pbr::DiscSource& xd_disc, const PlanOptions& o);

constexpr int PKX_GROUP_FIRST_G57 = 4200, PKX_GROUP_FIRST_FORM = 4600;
constexpr uint32_t PKX_RES = 0x00011E00;

}
