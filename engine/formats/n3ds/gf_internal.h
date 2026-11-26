#pragma once
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "n3ds_assemble.h"
#include "pbr_gx_tex.h"
#include "pbr_model.h"

namespace n3ds {
namespace gfi {
struct MadeImages {
  std::vector<pbr::Image> images;
  std::vector<std::pair<int, std::string>> mat_img;
  std::vector<std::string> classes;
};
const GfCoord* colour_coord(const GfMaterial& mt);
bool plate_is_tra(const GfMaterial& mt, const std::string& nm, bool have_nm, const std::map<std::string, pbr::Rgba>& texs);
std::vector<std::pair<std::string, std::vector<std::string>>> iris_plan(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const Opts& opts);
bool is_effect_layer(const GfMaterial& mt);

struct MatchPlan {
  std::vector<std::array<double, 3>> cen;
  std::vector<double> dl, gain, dh;
  double sigma = 12.0;
  pbr::Rgba apply(const pbr::Rgba& in, bool skin_only) const;
};
MadeImages make_images(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const Opts& opts, int nat, const MatchPlan* plan = nullptr);
std::unique_ptr<MatchPlan> colour_match_plan(const GfModel& gm, const std::map<std::string, pbr::Rgba>& texs, const std::vector<uint8_t>& xd_bytes,
                                             const Opts& opts, int nat, const Logger& log);
bool match_piece_ok(const GfMaterial& mt, const std::vector<int>& fixes);
bool hand_match_enabled(int nat);
bool hand_plates_follow(int nat, const std::string& name);
double hand_glow_strength(int nat);
void rgb_to_lab(const double* rgb, double* lab);
void lab_to_rgb(const double* lab, double* rgb);
double py_sum(const std::vector<double>& v);
pbr::Rgba fire_alpha(const pbr::Rgba& in);
pbr::Rgba lure_fill(const pbr::Rgba& in);
pbr::Rgba steel_tint(const pbr::Rgba& in, double k);
pbr::Rgba cheek_boost(const pbr::Rgba& in, double k);
pbr::Rgba sat_boost(const pbr::Rgba& in, double k);
pbr::Rgba bake_emission(const pbr::Rgba& rgba, const pbr::Rgba& inc, double strength, const pbr::Rgba* raw, const pbr::Rgba* mask = nullptr);
const char* emission_mask(const GfMaterial& mt, const std::map<std::string, pbr::Rgba>& texs);
const char* emission_map(const GfMaterial& mt, const std::map<std::string, pbr::Rgba>& texs);
pbr::Rgba register_to_colour(const pbr::Rgba& src, const GfCoord& ck, const GfCoord& c0, int w, int h);
const GfCoord* coord_of(const GfMaterial& mt, const std::string& name);
bool is_lure_glow(const GfMaterial& mt);
bool is_fire_material(const GfMaterial& mt);
int species_of(const GfModel& gm);
constexpr int KEEP_FIRE_MESH = 146;
pbr::Rgba fire_tint(const pbr::Rgba& in);
pbr::Rgba light_tweak(const pbr::Rgba& in, double k);
std::vector<int> hand_rules_for(int nat, const std::string& name);
std::vector<int> recolour_fixes_for(int nat, const std::string& name);
}
}
