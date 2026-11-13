#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pbr_pack.h"

namespace pbr {

constexpr int kRecSize = 292;

struct PlanIn {
  std::string model;
  int standin_nat = 0;
};

struct SpeciesRec {
  int dex = 0, xd_species_id = 0;
  std::string name;
  bool has_model = false;
  std::string model;
  int standin_species_id = 0;
  std::vector<uint8_t> rec;
  int mita = 0, tuka = 0;
  std::string key;
};
struct ExistingPatch { int xd_species_id, dex, slot, kind, buff, to_dex; std::string to_name; };

struct ExistingMachines { int xd_species_id = 0; std::vector<int> machines; };

struct SpeciesSet {
  std::vector<SpeciesRec> species;
  std::vector<ExistingPatch> patches;
  std::vector<ExistingMachines> machines;
  std::vector<uint8_t> xd_rec1;
};

SpeciesSet gen4_species(const PbrData& pbr, XdTables& xd, const std::vector<uint8_t>& xd_common_fsys,
                        const std::map<int, PlanIn>& plan, const std::set<std::string>& built);

}
