#include "room_c_api.h"

#include "room_preview.h"
#include "room_recipe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace er = engine::rooms;

namespace {
std::string Esc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') o += '\\', o += char(c);
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += char(c);
  }
  return o;
}
std::string F(float v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", double(v)); return b; }
char* Dup(const std::string& s) { char* p = static_cast<char*>(std::malloc(s.size() + 1)); if (p) std::memcpy(p, s.c_str(), s.size() + 1); return p; }
uint8_t* DupBytes(const std::vector<uint8_t>& v) { uint8_t* p = static_cast<uint8_t*>(std::malloc(v.size() ? v.size() : 1)); if (p && !v.empty()) std::memcpy(p, v.data(), v.size()); return p; }
std::string List(const std::vector<std::string>& v) {
  std::string s = "[";
  for (size_t i = 0; i < v.size(); i++) s += std::string(i ? ", " : "") + "\"" + Esc(v[i]) + "\"";
  return s + "]";
}
}

extern "C" {

int xdr_api_version(void) { return XDR_API_VERSION; }
void xdr_free(void* p) { std::free(p); }

char* xdr_recipe_check(const char* recipe_json) {
  er::RoomRecipe r;
  std::vector<std::string> problems;
  const bool ok = er::ParseRecipe(recipe_json ? recipe_json : "", r, problems);
  return Dup(std::string("{\"ok\": ") + (ok ? "true" : "false") + ", \"problems\": " + List(problems) + "}");
}

char* xdr_map_parts(const uint8_t* map, size_t size, char** error) {
  std::vector<std::string> problems;
  er::HsdArchive a;
  er::SceneInfo scene;
  if (!er::HsdParse(map, size, a, problems) || !er::HsdListScene(a, scene, problems)) {
    if (error) {
      std::string why;
      for (auto& p : problems) why += p + "; ";
      *error = Dup(why.empty() ? "not a room map" : why);
    }
    return nullptr;
  }
  std::string s = "{\"lights\": " + std::to_string(scene.lights) + ", \"camera\": " + (scene.camera ? "true" : "false") + ", \"models\": [";
  for (size_t m = 0; m < scene.models.size(); m++) {
    s += std::string(m ? ", " : "") + "{\"model\": " + std::to_string(m) + ", \"joints\": [";
    const auto& mi = scene.models[m];
    for (size_t j = 0; j < mi.joints.size(); j++) {
      const er::JointInfo& ji = mi.joints[j];
      s += std::string(j ? ", " : "") + "{\"joint\": " + std::to_string(ji.number) + ", \"parent\": " + std::to_string(ji.parent) + ", \"at\": [" + F(ji.trans[0]) + ", " + F(ji.trans[1]) + ", " + F(ji.trans[2]) +
           "], \"meshes\": " + std::to_string(ji.dobjs) + ", \"display_list_bytes\": " + std::to_string(ji.dlBytes);
      float lo[3], hi[3];
      if (er::HsdSubtreeBounds(a, ji.offset, lo, hi))
        s += ", \"size\": [" + F(hi[0] - lo[0]) + ", " + F(hi[1] - lo[1]) + ", " + F(hi[2] - lo[2]) + "], \"min\": [" + F(lo[0]) + ", " + F(lo[1]) + ", " + F(lo[2]) + "]";
      s += "}";
    }
    s += "]}";
  }
  return Dup(s + "]}");
}

uint8_t* xdr_collision_png(const uint8_t* collision, size_t collision_size, int pixels, size_t* png_size) {
  er::Ccd c;
  std::string why;
  if (!er::CcdRead(collision, collision_size, c, why)) return nullptr;
  const std::vector<uint8_t> png = er::CcdTopViewPng(c, pixels > 0 ? pixels : 512);
  if (png_size) *png_size = png.size();
  return DupBytes(png);
}

int xdr_build(const char* recipe_json, const uint8_t* base_map, size_t base_map_size, const uint8_t* base_collision, size_t base_collision_size,
              xdr_source_map_fn source, void* user, uint32_t max_archive_bytes, uint32_t base_archive_bytes, xdr_result* out) {
  if (!out) return 0;
  std::memset(out, 0, sizeof *out);
  er::RoomRecipe r;
  std::vector<std::string> problems;
  if (!er::ParseRecipe(recipe_json ? recipe_json : "", r, problems)) {
    out->report_json = Dup("{\"ok\": false, \"problems\": " + List(problems) + "}");
    return 0;
  }
  er::RoomInputs in;
  in.map.assign(base_map, base_map + base_map_size);
  in.collision.assign(base_collision, base_collision + base_collision_size);
  in.maxArchiveBytes = max_archive_bytes;
  in.baseArchiveBytes = base_archive_bytes;
  if (source) {
    in.sourceMap = [source, user](const std::string& name, std::vector<uint8_t>& map) {
      uint8_t* d = nullptr;
      size_t n = 0;
      if (!source(name.c_str(), &d, &n, user) || !d) return false;
      map.assign(d, d + n);
      std::free(d);
      return true;
    };
  }
  in.hashOf = [](const std::vector<uint8_t>& b) { return er::RoomSha1(b.data(), b.size()); };
  er::RoomOutputs o;
  const bool ok = er::ApplyRecipe(r, in, o, problems);
  std::string rep = std::string("{\"ok\": ") + (ok ? "true" : "false") + ", \"problems\": " + List(problems) + ", \"report\": " + List(o.report) + ", \"map_bytes\": " + std::to_string(o.mapBytes) +
                    ", \"collision_bytes\": " + std::to_string(o.collisionBytes) + ", \"texture_bytes_added\": " + std::to_string(o.textureBytesAdded) + ", \"pieces\": " + std::to_string(o.pieces) + "}";
  out->report_json = Dup(rep);
  if (!ok) return 0;
  out->map = DupBytes(o.map), out->map_size = o.map.size();
  out->collision = DupBytes(o.collision), out->collision_size = o.collision.size();
  return 1;
}

void xdr_result_free(xdr_result* r) {
  if (!r) return;
  std::free(r->map), std::free(r->collision), std::free(r->report_json);
  std::memset(r, 0, sizeof *r);
}

}
