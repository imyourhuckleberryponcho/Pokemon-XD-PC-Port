#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "pbr_sdr.h"

namespace pbr {

constexpr double FPS = 30.0;

using M4 = std::array<double, 16>;

M4 mm4(const double* a, const double* b);
inline M4 mm4(const M4& a, const M4& b) { return mm4(a.data(), b.data()); }

double det3(const double* m);

struct Pose {
  int frames = 0;
  int n = 0;
  std::vector<double> scale;
  std::vector<char> animated;
  std::vector<double> rot_only;
  std::vector<double> local;
  std::vector<char> compensated;
  std::vector<double> dcomp;
  std::vector<char> visible;
  std::vector<char> own_visible;
  const double* loc(int f, int i) const { return &local[((size_t)f * n + i) * 16]; }
};

struct Decomp {
  int frames = 0, ne = 0;
  std::vector<double> s, r, t;
};

struct PointSet {
  int node = 0, nv = 0;
  std::vector<double> pts;
};

class Eval {
 public:
  Eval(const sdr::Sdr& sdr, const std::string& compensate = "joint8_under_joint");
  bool comp_t = false;
  const sdr::Sdr& src;
  int n = 0;
  std::vector<int> parent;
  std::vector<char> is_joint, comp;
  std::vector<std::array<double, 9>> orient;
  std::vector<std::array<double, 9>> piv;
  std::vector<std::array<double, 3>> st_pos, st_rot, st_scale;
  std::vector<M4> inv_bind;
  struct EvMesh {
    int node;
    std::vector<std::array<double, 3>> pos;
    bool has_terms = false;
    std::vector<std::pair<int, std::vector<std::pair<int, double>>>> terms;
  };
  std::vector<EvMesh> meshes;

  int frames_of(int act) const;
  Pose sample(int act, int frames = -1, const std::function<double(double)>& tmap = nullptr) const;
  Pose sample_static() const;
  std::vector<double> world(const Pose& p) const;
  std::vector<double> world_of(const std::vector<double>& local, int frames) const;
  std::vector<PointSet> points(const std::vector<double>& world, int frames, const char* visible) const;

 private:
  Pose finish(Pose p, std::vector<double> pos, std::vector<double> rot, std::vector<double> sca, std::vector<char> vis) const;
};

std::vector<double> boxes(const std::vector<PointSet>& sets, int frames);

std::vector<double> continuous_euler(const std::vector<double>& rm, int frames, int k);

Decomp decompose(const Pose& pose, const Eval& ev, const std::set<int>& pairs);
std::vector<double> compose_local(const Decomp& d, int n, const std::set<int>& pairs);
std::vector<double> shear_error(const Pose& pose, const Eval& ev);

std::array<double, 9> rot3(double rx, double ry, double rz);

}
