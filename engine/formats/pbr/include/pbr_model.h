#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pbr {

enum { OP_CON = 1, OP_LIN = 2, OP_SPL0 = 3, OP_SPL = 4, OP_SLP = 5, OP_KEY = 6 };
enum { J_ROTX = 1, J_ROTY, J_ROTZ, J_PATH, J_TRAX, J_TRAY, J_TRAZ, J_SCAX, J_SCAY, J_SCAZ };

using Mat = std::array<double, 12>;
using V3 = std::array<double, 3>;

Mat mat_mul(const Mat& a, const Mat& b);
Mat mat_inv(const Mat& m);
V3 mat_point(const Mat& m, const V3& p);
V3 mat_vec(const Mat& m, const V3& p);
Mat mat_rot(double rx, double ry, double rz);
Mat mat_srt(const V3& scale, const V3& rot, const V3& pos);
void mat_decompose(const Mat& m, V3* scale, V3* rot, V3* pos);

struct Joint {
  std::string name;
  int parent = -1;
  std::optional<uint32_t> flags;
  V3 rot{0, 0, 0}, scale{1, 1, 1}, pos{0, 0, 0};
  std::optional<std::vector<double>> inv_bind;
  bool hidden = false;
  int billboard = 0;
};

using Weights = std::vector<std::pair<int, double>>;

struct Vertex {
  V3 pos{0, 0, 0};
  std::optional<V3> nrm;
  std::optional<std::array<double, 2>> uv;
  std::optional<std::vector<int>> color;
  std::optional<Weights> weights;
  bool has_uvx = false;
  std::vector<std::optional<std::array<double, 2>>> uvx;
};

struct Mesh {
  std::string name;
  int joint = 0;
  std::optional<int> material = 0;
  bool rigid = false;
  std::vector<Vertex> verts;
  std::vector<std::array<int, 3>> tris;
  std::optional<int> pobj_flags;
};

struct Image {
  int width = 0, height = 0, format = 0, mipmap = 0;
  double min_lod = 0, max_lod = 0;
  std::vector<uint8_t> data;
};
struct Tlut {
  uint32_t format = 0, name = 0;
  int count = 0;
  std::vector<uint8_t> data;
};
using Bytes = std::optional<std::vector<uint8_t>>;
struct Texture {
  std::optional<int> image, tlut;
  uint32_t tex_id = 0, src = 4;
  V3 rot{0, 0, 0}, scale{1, 1, 1}, trans{0, 0, 0};
  uint32_t wrap_s = 1, wrap_t = 1;
  int repeat_s = 1, repeat_t = 1;
  uint32_t flags = 0x00030010;
  double blending = 1.0;
  uint32_t mag_filter = 1;
  Bytes lod, tev;
};
struct Material {
  std::string name;
  std::string draw_class = "opa";
  uint32_t rendermode = 0x2015;
  std::vector<double> ambient{0x80, 0x80, 0x80, 0xFF}, diffuse{0xFF, 0xFF, 0xFF, 0xFF}, specular{0xFF, 0xFF, 0xFF, 0xFF};
  double alpha = 1.0, shininess = 50.0;
  std::vector<Texture> textures;
  Bytes pe;
};

struct Key { int op; double value, slope; long long wait; };
struct Track {
  int kind = 0;
  double start = 0.0;
  std::vector<Key> keys;
  std::optional<std::pair<int, int>> src_format;
};
struct AnimObj {
  uint32_t flags = 0;
  double end = 0.0;
  std::vector<Track> tracks;
};
struct TexAnim {
  uint32_t tex_id = 0;
  std::optional<AnimObj> aobj;
  std::vector<int> images, tluts;
};
struct MatAnim {
  bool has_render_anim = false;
  std::optional<AnimObj> aobj;
  std::vector<TexAnim> texanims;
};
struct MatList {
  std::vector<std::optional<MatAnim>> items;
};
struct Animation {
  std::string name;
  bool has_material_tree = false;
  std::vector<std::pair<int, AnimObj>> joints;
  std::vector<std::pair<int, uint32_t>> joint_flags;
  std::vector<std::pair<int, std::optional<MatList>>> materials;
  std::optional<std::vector<std::array<double, 6>>> bounds;
  AnimObj& set_joint(int i, AnimObj a) {
    for (auto& p : joints) if (p.first == i) { p.second = std::move(a); return p.second; }
    joints.emplace_back(i, std::move(a));
    return joints.back().second;
  }
  void set_joint_flags(int i, uint32_t f) {
    for (auto& p : joint_flags) if (p.first == i) { p.second = f; return; }
    joint_flags.emplace_back(i, f);
  }
  void set_material(int i, std::optional<MatList> l) {
    for (auto& p : materials) if (p.first == i) { p.second = std::move(l); return; }
    materials.emplace_back(i, std::move(l));
  }
  double end() const {
    double e = 0.0;
    for (auto& p : joints) e = e > p.second.end ? e : p.second.end;
    return e;
  }
  const AnimObj* joint(int i) const {
    for (auto& p : joints) if (p.first == i) return &p.second;
    return nullptr;
  }
  uint32_t jflags(int i) const {
    for (auto& p : joint_flags) if (p.first == i) return p.second;
    return 0;
  }
  const MatList* material(int i, bool* present) const {
    for (auto& p : materials)
      if (p.first == i) { *present = p.second.has_value(); return p.second ? &*p.second : nullptr; }
    *present = false;
    return nullptr;
  }
};

struct Model {
  std::vector<Joint> joints;
  int skeleton_root = 0;
  std::vector<Mesh> meshes;
  std::vector<Material> materials;
  std::vector<Image> images;
  std::vector<Tlut> tluts;
  std::vector<Animation> animations;
  std::optional<std::vector<int>> bound_head;
  std::vector<std::string> image_classes;
  std::vector<Mat> world_static() const;
};

struct Seg { double start, dur; int op; double p0, p1, d0, d1; };
std::vector<Seg> track_segments(const Track& t);
double track_value_segs(const std::vector<Seg>& segs, double track_start, double frame);
double track_value(const Track& t, double frame);
std::vector<Mat> pose_at(const Model& model, const Animation* anim, double frame);

}
