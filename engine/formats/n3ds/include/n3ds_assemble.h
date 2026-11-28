#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "n3ds_dump.h"
#include "n3ds_gf.h"
#include "pbr_hsd_write.h"

namespace n3ds {

class Unsupported : public std::runtime_error {
 public:
  explicit Unsupported(const std::string& s) : std::runtime_error("not ported to C++ yet: " + s) {}
};

struct Species {
  GfModel gm;
  std::map<std::string, Rgba> texs;
  std::map<std::string, Rgba> shiny_texs;
  std::vector<std::pair<std::string, GfMotion>> mots;
  const GfMotion* mot(const std::string& k) const { for (auto& p : mots) if (p.first == k) return &p.second; return nullptr; }
  int group = 0;
};
Species load_species(const Garc& models, int group, int motion_group = -1);
GfModel load_model_only(const Garc& models, int group);

struct Opts {
  bool ground = true, fire = true, eyes = true, recolour = true, hand = true;
  int decimate = 2600;
  double light = 1.0;
  int size_rule = 2;
  double height_cm = 0;
  bool size_flat = false;
  bool hover_model = true;
  bool hover_idle = true;
  bool fx_skip_fire = false;
  double anim_tol = 1.0, chan_tol = 1.0;
  int quant = 0, max_side = 256;
  bool iris_bake = true, eye_rgb5 = true;
  bool fx = false, bake_scale = false, match = false, eye_blend = false, lure = false;
  double steel = 0, cheeks = 0, sat_boost = 0, ghost_alpha = 0, scale = 0;
  bool shiny = false;
};

using Logger = std::function<void(const std::string&)>;

struct ConvertInfo {
  size_t size = 0;
  int tris = 0;
  double scale = 0;
  pbr::WriteReport report;
  int step = 0;
  bool iris2 = false;
  struct Shiny {
    bool done = false;
    int swap[3] = {0, 1, 2}, mod[3] = {127, 127, 127};
    int standin_swap[3] = {0, 1, 2}, standin_mod[3] = {127, 127, 127};
    double err_fit = 0, err_none = 0, err_standin = 0, cover = 0;
  } shiny;
};

std::vector<uint8_t> convert(const Species& sp, const std::vector<uint8_t>& xd_bytes, const Opts& opts, const Logger& log, ConvertInfo* info,
                             double step = 0.1);

struct LadderStep {
  int side, dec;
  double atol, ctol;
  int quant;
};
const std::vector<LadderStep>& ladder();
constexpr size_t FILE_CAP_BYTES = 614400;
std::vector<uint8_t> convert_ladder(const Species& sp, const std::vector<uint8_t>& xd_bytes, double height_cm, int start, ConvertInfo* info);

Opts opts_xd_species();
Opts opts_gen4(double height_cm);
std::vector<uint8_t> convert_one(const Species& sp, const std::vector<uint8_t>& xd_bytes, const Opts& o, ConvertInfo* info, std::vector<std::string>* log = nullptr);

struct PicRef {};
}
