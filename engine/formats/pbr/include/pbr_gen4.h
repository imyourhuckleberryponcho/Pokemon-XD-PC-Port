#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pbr_to_xd.h"

namespace pbr {

struct Gen4Source {
  std::string archive;
  std::string member;
  bool has_xd = false;
  std::shared_ptr<const std::vector<uint8_t>> sdr;
  std::shared_ptr<const std::vector<uint8_t>> xd_pkx;
};

struct Gen4Features {
  bool ok = false;
  std::vector<std::string> ef, bones, actions;
  std::array<double, 3> ext{};
  int nodes = 0;
};

struct Gen4Result {
  std::string name, state, standin;
  std::vector<uint8_t> data;
  std::vector<std::string> lines;
  double secs = 0;
};

Gen4Features gen4_features(const std::vector<uint8_t>& sdr_bytes);
double gen4_score(const Gen4Features& g, const Gen4Features& c);
std::string gen4_tokens(const std::string& name);

std::vector<Gen4Result> gen4_run(const std::vector<Gen4Source>& all, const std::set<std::string>& converted, int jobs,
                                 const std::function<void(int, int, const std::string&)>& progress);

}
