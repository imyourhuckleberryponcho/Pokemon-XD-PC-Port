#pragma once
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "pbr_gx_tex.h"
#include "pbr_hsd_read.h"
#include "pbr_hsd_write.h"
#include "pbr_model.h"
#include "pbr_sdr.h"

namespace pbr {

using Logger = std::function<void(const std::string&)>;

constexpr double POSE_TOL_PCT = 1.0;
constexpr double SHEAR_TOL = 0.003;
constexpr int MAX_SIDE = 256;
constexpr long long FILE_CAP = 600 * 1024;
constexpr int MAX_POBJ = 80;

class KeepXD : public std::runtime_error {
 public:
  explicit KeepXD(const std::string& s) : std::runtime_error(s) {}
};

struct Gen4Info {
  bool comp_t = false;
  std::map<int, std::string> pick_names;
  std::map<int, std::pair<double, double>> pick_times;
  std::map<int, std::pair<std::string, std::array<double, 3>>> roles;
};

struct StandinStats {
  bool stop_roles = false;
  std::map<int, std::string> pick_names;
  std::map<int, std::pair<double, double>> pick_times;
  std::map<int, std::pair<std::string, std::array<double, 3>>> roles;
};
class StopConversion : public std::runtime_error {
 public:
  StopConversion() : std::runtime_error("stop") {}
};

struct GraftRegion {
  std::vector<std::string> pts;
  std::vector<int> xb, bones;
};

struct ConvertInfo {
  std::vector<int> need;
  double pct = 0;
};

struct ConvertOut {
  Model m;
  Pkx pkx;
};

Weights quantise_weights(const std::vector<std::pair<int, double>>& w, double step, int most = 4);
std::vector<std::pair<int, double>> thin(const std::vector<double>& samples, double tol);
std::vector<Key> keys_of(const std::vector<double>& samples, double tol);
std::vector<Key> vis_keys(const std::vector<char>& v);
std::vector<Key> step_keys(const std::vector<double>& v);
std::string texture_alpha(int fmt, int w, int h, const std::vector<uint8_t>& data);
double py_round(double x, int digits);
std::vector<int> pyset_order(const std::vector<int>& inserted);
std::vector<int> used_images(const Model& m);
long long fit_textures(Model& m, std::vector<std::string>& classes, const Logger& log, long long budget = -1);

std::optional<ConvertOut> convert(const sdr::Sdr& sdr, const Pkx& xd_pkx, const Model& xd_model, double step, const Logger& log,
                                  const std::string& compensate, double tol_scale, const Model* xd_full,
                                  const std::vector<GraftRegion>* graft, const Gen4Info* gen4,
                                  std::optional<std::set<int>> pairs, ConvertInfo* info, StandinStats* stats = nullptr);

void graft_xd_pieces(Model& m, const Model& xd, const std::vector<GraftRegion>& regions, int idle_slot,
                     std::vector<std::string>& alpha, const Logger& log, double step);

struct ConvertFileResult {
  std::vector<uint8_t> data;
  WriteReport report;
};

ConvertFileResult convert_file(const std::vector<uint8_t>& sdr_bytes, const std::vector<uint8_t>& xd_bytes, const std::string& name,
                               const Logger& log, double step = 0.1, const std::string& compensate = "joint8_under_joint",
                               long long cap = FILE_CAP, const Gen4Info* gen4 = nullptr);

}
