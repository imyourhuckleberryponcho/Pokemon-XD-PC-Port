#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pbr_model.h"

namespace pbr {

constexpr uint32_t JOBJ_SKELETON = 1, JOBJ_SKELETON_ROOT = 2, JOBJ_HIDDEN = 0x10, JOBJ_PTCL = 0x20, JOBJ_SPLINE = 0x4000;

struct Pkx {
  std::vector<uint8_t> header;
  std::vector<std::vector<uint8_t>> sections;
  std::vector<uint8_t> particles, archive;
  static Pkx parse(const std::vector<uint8_t>& b);
  std::vector<uint8_t> build() const;
  int head_joint() const;
  void set_head_joint(int j);
  std::vector<int> section_joints(size_t s) const;
  void set_section_joints(size_t s, const std::vector<int>& joints);
  std::vector<std::pair<int, int>> section_motions(size_t s, int* count = nullptr) const;
};

long long hsd_image_bytes(int w, int h, int fmt, int mipmap = 0, double max_lod = 0.0);

std::vector<Key> hsd_read_fobj_keys(const uint8_t* data, size_t n, int frac_value, int frac_slope);

struct ReadResult {
  Pkx pkx;
  Model model;
  std::vector<std::string> warnings;
};

ReadResult hsd_read_pkx(const std::vector<uint8_t>& b, bool meshes = true);

}
