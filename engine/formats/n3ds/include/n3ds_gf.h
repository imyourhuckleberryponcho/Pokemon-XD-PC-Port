#pragma once
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "n3ds_dump.h"

namespace n3ds {

struct GfBone {
  std::string name, parent;
  int flags = 0;
  std::array<double, 3> scale{}, rot{}, pos{};
};

struct GfCoord {
  std::string name;
  int unit = 0, mapping = 0;
  std::array<double, 2> scale{}, trans{};
  double rot = 0;
  uint32_t wrap[2] = {0, 0};
};

struct GfMaterial {
  std::string name, shader, vtx_shader, frag_shader;
  uint8_t colors[12][4] = {};
  std::vector<GfCoord> coords;
};

struct GfAttr {
  std::string name;
  int fmt = 0, n = 0;
  double scale = 1;
};

struct Arr {
  int rows = 0, cols = 0;
  std::vector<double> v;
  double& at(int r, int c) { return v[(size_t)r * cols + c]; }
  double at(int r, int c) const { return v[(size_t)r * cols + c]; }
  bool empty() const { return v.empty(); }
};

struct GfVerts {
  std::map<std::string, Arr> a;
  int n = 0;
  bool has(const char* k) const { return a.count(k) != 0; }
  const Arr& get(const char* k) const { return a.at(k); }
};

struct GfSub {
  std::string name;
  std::vector<int> bone_idx;
  Bytes raw;
  std::vector<int64_t> indices;
  std::vector<GfAttr> attrs;
  std::vector<std::pair<std::string, std::array<double, 4>>> fixed;
  int stride = 0, prim = 0;
  GfVerts verts;
};

struct GfMesh {
  std::string name;
  std::vector<GfSub> subs;
};

struct GfModel {
  std::vector<GfBone> bones;
  std::vector<GfMaterial> materials;
  std::vector<GfMesh> meshes;
  std::vector<std::string> tex_names;
  std::array<double, 4> bbmin{}, bbmax{};
};

GfModel parse_model(const Bytes& b);
std::string model_first_texture(const Bytes& package);
GfVerts decode_verts(const GfSub& sm);
std::vector<std::array<int64_t, 3>> triangles(const GfSub& sm);

struct GfTexture {
  std::string name;
  int w = 0, h = 0, fmt = 0, mip = 0;
  Bytes data;
};
GfTexture parse_texture(const Bytes& b);
struct Rgba {
  int w = 0, h = 0;
  Bytes px;
};
Rgba decode_texture(const GfTexture& t);

struct GfKey {
  double frame = 0, value = 0, slope = 0;
};
using GfChannel = std::vector<GfKey>;

struct GfMotionBone {
  std::string name;
  bool axis = false;
  GfChannel ch[9];
};
struct GfMatChan {
  std::string name;
  int unit = 0;
  GfChannel sx, sy, rot, tx, ty;
};
struct GfMotion {
  int frames = 0;
  bool loop = false;
  std::vector<GfMotionBone> bones;
  std::vector<std::pair<std::string, std::vector<bool>>> vis;
  std::vector<GfMatChan> mats;
  const GfMotionBone* bone(const std::string& n) const {
    for (auto& b : bones) if (b.name == n) return &b;
    return nullptr;
  }
};
bool parse_motion(const Bytes& b, GfMotion& out);
bool herp(const GfChannel& k, double frame, double* out);

}
