#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pbr {
namespace sdr {

struct Curve {
  int what = 0, axis = 0;
  std::vector<double> values, slopes;
  bool has_keys = false;
  struct Key { int interp, value, slope_in, slope_out; double time; };
  std::vector<Key> keys;
  int fps = 60;
  double at(double t) const;
};

template <class V>
struct OrderedMap {
  std::vector<std::pair<int, V>> items;
  V* find(int k) {
    for (auto& p : items) if (p.first == k) return &p.second;
    return nullptr;
  }
  const V* find(int k) const {
    for (auto& p : items) if (p.first == k) return &p.second;
    return nullptr;
  }
  V& set(int k, V v) {
    if (V* e = find(k)) { *e = std::move(v); return *e; }
    items.emplace_back(k, std::move(v));
    return items.back().second;
  }
  bool empty() const { return items.empty(); }
  size_t size() const { return items.size(); }
};

using Anim = OrderedMap<std::vector<Curve>>;

struct Texture {
  int width = 0, height = 0;
  uint32_t code = 0;
  int format = -1;
  uint32_t wrap_s = 0, wrap_t = 0;
  std::vector<uint8_t> data;
  uint32_t palette_format = 0, palette_at = 0;
  int levels = 0;
};

struct Layer {
  int texture = -1;
  bool uv = false;
  int uvset = -1;
  double offset[2] = {0, 0}, rotation = 0, scale[2] = {1, 1};
  uint8_t raw[0x18] = {0};
};

struct Material {
  std::string name;
  int texture = -1;
  std::vector<Layer> layers;
  uint8_t ambient[4] = {0}, diffuse[4] = {0};
  std::vector<uint8_t> raw;
  std::vector<std::pair<int, Anim>> layer_anims;
  const Anim* anim() const { return layer_anims.empty() ? nullptr : &layer_anims[0].second; }
};

struct Corner { int pos = -1, nrm = -1, uv0 = -1, uv1 = -1, uv2 = -1; };
struct Tri { Corner c[3]; };
struct Part { int material = -1; std::vector<Tri> tris; };
using Weights = std::vector<std::pair<int, double>>;

struct Mesh {
  struct Vert { double p[3], n[3]; };
  std::vector<Vert> verts;
  bool has_weights = false;
  std::vector<Weights> weights;
  std::vector<std::vector<std::pair<double, double>>> uvsets;
  std::vector<Part> parts;
};

struct Node {
  int parent = -1;
  uint32_t kind = 0;
  std::string name;
  int number = 0, flags = 0;
  double pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
  bool has_pivots = false;
  double pivots[12] = {0};
  uint32_t jflags = 0;
  double orient[3] = {0, 0, 0};
  bool has_inv_bind = false;
  double inv_bind[12] = {0};
  bool has_mesh = false;
  Mesh mesh;
  Anim anim;
};

struct Action { std::string name; double f[3]; };

class Sdr {
 public:
  explicit Sdr(std::vector<uint8_t> bytes);
  std::vector<uint8_t> b;
  std::vector<std::string> warnings;
  std::vector<Texture> textures;
  std::vector<Material> materials;
  std::string name;
  std::vector<Action> actions;
  std::vector<Node> nodes;
  std::vector<std::pair<int, int>> by_number;
 private:
  uint32_t u32(uint64_t o) const;
  int u16(uint64_t o) const;
  int u8(uint64_t o) const;
  double f32(uint64_t o) const;
  std::string cstr(uint64_t o) const;
  void vec(uint64_t o, double* out, const double* dflt, int n = 3) const;
  void read_textures();
  void read_materials();
  void walk(uint64_t o, int parent);
  void material_anims();
  Curve curve(uint64_t q);
  Mesh read_mesh(uint64_t m);
  std::vector<uint32_t> tex_keys_, mat_keys_;
  int tex_at(uint32_t off) const;
  int mat_at(uint32_t off) const;
};

}
}
