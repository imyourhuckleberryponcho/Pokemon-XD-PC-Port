#pragma once
#include <array>
#include <string>
#include <vector>

#include "n3ds_gf.h"

namespace n3ds {

struct M4 {
  double m[4][4];
};
M4 m4_identity();
M4 m4_mul(const M4& a, const M4& b);
bool m4_inv(const M4& a, M4* out);

using V3 = std::array<double, 3>;
using Mat3 = std::array<std::array<double, 3>, 3>;
Mat3 rot_mat(double rx, double ry, double rz);
Mat3 axis_angle_mat(const V3& v);
V3 euler_from_mat(const Mat3& R);

struct Pose {
  std::vector<V3> s, r, t;
  std::vector<char> axis;
};

class Skeleton {
 public:
  explicit Skeleton(const std::vector<GfBone>& bones);
  int nb = 0;
  std::vector<std::string> names;
  std::vector<int> parent;
  std::vector<char> ssc;
  std::vector<V3> bind_s, bind_r, bind_t, raw_s, raw_r, raw_t;
  std::vector<M4> bind_world, inv_bind;

  bool needs_ssc(const std::vector<V3>& s) const;
  void ssc_to_plain(std::vector<V3>& s, std::vector<V3>& r, std::vector<V3>& t) const;
  std::vector<M4> world(const std::vector<V3>& s, const std::vector<V3>& r, const std::vector<V3>& t, const std::vector<char>* axis = nullptr) const;
  Pose sample_motion(const GfMotion& mo, double frame) const;
  void sample_at(const GfMotion& mo, const std::vector<double>& frames, std::vector<std::vector<V3>>& S, std::vector<std::vector<V3>>& R,
                 std::vector<std::vector<V3>>& T) const;
};

std::vector<V3> skin_points(const Skeleton& sk, const std::vector<M4>& W, const std::vector<int>& bone_idx_table, const GfVerts& verts);

struct SubRef {
  const std::vector<int>* bone_idx;
  const GfVerts* verts;
};
std::vector<double> ground_offsets(const Skeleton& sk, const std::vector<std::vector<V3>>& S, const std::vector<std::vector<V3>>& R,
                                   const std::vector<std::vector<V3>>& T, const std::vector<SubRef>& pts, double shift, double hover);
constexpr double HOVER_MIN = 0.35;

double np_sum(const double* a, size_t n);
std::vector<double> motion_energy(const Skeleton& sk, const GfMotion& mo);
std::vector<double> role_warp(const std::string& role, int nat, int xd_len, const std::vector<double>& e);

}
