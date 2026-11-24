#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "n3ds_dump.h"
#include "n3ds_plan.h"
#include "pbr_pack.h"

namespace n3ds {

using pbr::Json;

std::u32string to_u32(const std::string& utf8);
std::string to_utf8(const std::u32string& s);
std::string upper_str(const std::string& s);
std::string py_strip(const std::string& s);
std::vector<std::string> py_split(const std::string& s);
std::string shown(const std::string& s);
std::string desc_text(const std::string& s);
std::string move_upper(const std::string& s);
std::string move_desc(const std::string& s);

struct Personal {
  int hp = 0, atk = 0, defn = 0, spe = 0, spa = 0, spd = 0, type1 = 0, type2 = 0, catch_rate = 0;
  int ev_hp = 0, ev_atk = 0, ev_def = 0, ev_spe = 0, ev_spa = 0, ev_spd = 0;
  int gender = 0, hatch = 0, friendship = 0, growth = 0, abilities[3] = {0, 0, 0};
  int form_stats_index = 0, form_count = 0, base_exp = 0, height_cm = 0, weight_hg = 0;
  int bst() const { return hp + atk + defn + spe + spa + spd; }
};
struct MoveRec {
  int id = 0, type = 0, quality = 0, category = 0, power = 0, accuracy = 0, pp = 0, priority = 0, hits = 0, inflict = 0, inflict_percent = 0;
  int turn_min = 0, turn_max = 0, crit = 0, flinch = 0, effect_seq = 0, recoil = 0, heal = 0, target = 0, flags = 0;
  int stats[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  std::vector<int> signature() const;
  bool contact() const { return (flags & 1) != 0; }
};
struct EvoRec { int method = 0, arg = 0, species = 0, form = 0, level = 0; };

class Usum {
 public:
  explicit Usum(const std::string& rom_path);
  const RomFs& rom() const { return rom_; }
  const std::string& prod() const { return rom_.product; }
  const std::vector<std::string>& lines(const std::string& lang, int entry);
  std::string species_name(int n, const std::string& lang = "en") { return lines(lang, 60).at((size_t)n); }
  std::string species_class(int n, const std::string& lang = "en") { return lines(lang, 121).at((size_t)n); }
  Personal personal(int record);
  Bytes personal_raw(int record) { return read_entry(det_.personal, (size_t)record); }
  const std::string& text_archive() const { return det_.text; }
  const std::string& levelup_archive() const { return det_.levelup; }
  std::vector<std::pair<int, int>> levelup(int record);
  std::vector<EvoRec> evolutions(int record);
  const std::vector<MoveRec>& moves();
  const MoveRec& move(int i) { return moves().at((size_t)i); }
  std::string move_name(int i, const std::string& lang = "en") { return lines(lang, 118).at((size_t)i); }
  std::string move_desc_text(int i, const std::string& lang = "en") { return lines(lang, 117).at((size_t)i); }
  std::string ability_name(int i, const std::string& lang = "en") { return lines(lang, 101).at((size_t)i); }
  std::string ability_desc(int i, const std::string& lang = "en") { return lines(lang, 102).at((size_t)i); }
  int ability_count() { return (int)lines("en", 101).size() - 1; }
  std::string item_name(int i, const std::string& lang = "en") { return lines(lang, 40).at((size_t)i); }
  int item_count() { return (int)lines("en", 40).size(); }
  Bytes read_entry(const std::string& garc_path, size_t i);
  const Garc& garc(const std::string& path);

 private:
  RomFs rom_;
  Detected det_;
  std::map<std::string, std::unique_ptr<Garc>> garcs_;
  std::map<std::pair<std::string, int>, std::vector<std::string>> text_;
  std::vector<MoveRec> moves_;
  bool moves_loaded_ = false;
};

struct XdTablesCache {
  Json moves;
  std::vector<int> tm_moves, tutors;
};
XdTablesCache load_xd_tables(const std::string& data_mods_exe, const std::string& xd_disc, const std::string& work);

int make_dex_entries(const std::string& rom, const std::string& out_dir, const std::function<void(const std::string&)>& log);

struct Gen4SourceOptions {
  std::string rom, data_dir, src;
  bool values_on = true;
  bool eol_crlf = false;
  std::vector<std::string> plan_lines;
};
void write_gen4_source(const Gen4SourceOptions& o, const std::function<void(const std::string&)>& log);
std::vector<std::u32string> decode_text_units(const Bytes& d);

struct DataOptions {
  std::string rom, xd_disc, data_dir, gen4_pack, out, work, data_mods_exe;
  std::string models_dir;
  std::string converter_version;
  std::set<int> only;
  std::set<std::string> only_kinds;
  bool eol_crlf = false;
  bool cries = true;
  std::function<void(const std::string&)> log;
};
struct ModelResults {
  std::map<int, Json> species;
  std::map<std::string, Json> forms;
  bool present = false;
};
void make_gen57_data(const DataOptions& o, const Plan& plan, const ModelResults& mr);
void make_gen57_forms_data(const DataOptions& o, const Plan& plan, const ModelResults& mr);

struct DumpCry { Bytes hdr, body; int wave = -1; };
std::map<int, DumpCry> dump_cries(Usum& u, const std::string& data_dir, const std::vector<int>& nats);

}
