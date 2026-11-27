#include "../src/data/xd_room_lib.h"

#include <gs_formats.h>
#include <port_disc.h>
#include <room_ccd.h>
#include <room_hsd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace er = engine::rooms;

namespace {

void CollectFile(const char* path, uint32_t, void* user) { static_cast<std::vector<std::string>*>(user)->push_back(path); }

bool ReadDisc(const std::string& file, std::vector<uint8_t>& out) {
  size_t size = 0;
  uint8_t* p = PortDisc_ReadOriginalFile(file.c_str(), &size);
  if (p == nullptr) return false;
  out.assign(p, p + size);
  PortDisc_Free(p);
  return true;
}

std::string Esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      o += ' ';
    } else {
      o += c;
    }
  }
  return o;
}
std::string Name(const std::string& file) {
  const size_t slash = file.find_last_of("/\\");
  std::string n = slash == std::string::npos ? file : file.substr(slash + 1);
  const size_t dot = n.rfind('.');
  return dot == std::string::npos ? n : n.substr(0, dot);
}
std::string F(float v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.3f", double(v));
  return b;
}

struct Floor {
  std::string file, name;
  xd::rooms::FloorArchive fa;
  std::vector<uint8_t> archive, map, collision;
  er::SceneInfo scene;
  er::HsdArchive arch;
  std::vector<std::string> mapProblems, colProblems;
  size_t records = 0, hitTris = 0, walkTris = 0;
  bool mapOk = false, colOk = false;
};

size_t JointCount(const Floor& f) {
  size_t n = 0;
  for (const auto& m : f.scene.models) n += m.joints.size();
  return n;
}

bool Load(const std::string& file, Floor& f) {
  f.file = file, f.name = Name(file);
  if (!ReadDisc(file, f.archive) || !xd::rooms::ListArchive(f.archive, f.fa)) return false;
  if (f.fa.map < 0 && f.fa.collision < 0) return false;
  if (f.fa.map >= 0 && xd::rooms::ExtractEntry(f.archive, f.fa.entries[size_t(f.fa.map)].index, f.map)) {
    f.mapOk = er::HsdValidateMap(f.map.data(), f.map.size(), f.mapProblems, &f.scene);
    std::vector<std::string> ignore;
    if (er::HsdParse(f.map.data(), f.map.size(), f.arch, ignore) && !f.mapOk) er::HsdListScene(f.arch, f.scene, ignore);
  }
  if (f.fa.collision >= 0 && xd::rooms::ExtractEntry(f.archive, f.fa.entries[size_t(f.fa.collision)].index, f.collision)) {
    f.colOk = er::CcdValidate(f.collision.data(), f.collision.size(), f.colProblems);
    er::Ccd c;
    std::string why;
    if (f.colOk && er::CcdRead(f.collision.data(), f.collision.size(), c, why)) {
      f.records = c.records.size();
      for (const auto& r : c.records) f.hitTris += r.block[er::kCcdHit].tris.size(), f.walkTris += r.block[er::kCcdWalk].tris.size();
    }
  }
  return true;
}

std::string FloorJson(const Floor& f, bool joints) {
  std::string s = "{ \"archive\": \"" + Esc(f.name) + "\", \"file\": \"" + Esc(f.file) + "\", \"unpacked_bytes\": " + std::to_string(f.fa.unpackedBytes) + ",\n";
  s += "  \"map_entry\": " + std::to_string(f.fa.map >= 0 ? int(f.fa.entries[size_t(f.fa.map)].index) : -1) + ", \"map_bytes\": " + std::to_string(f.map.size()) + ", \"map_sha1\": \"" +
       (f.map.empty() ? std::string() : er::RoomSha1(f.map.data(), f.map.size())) + "\",\n";
  s += "  \"collision_entry\": " + std::to_string(f.fa.collision >= 0 ? int(f.fa.entries[size_t(f.fa.collision)].index) : -1) + ", \"collision_bytes\": " + std::to_string(f.collision.size()) +
       ", \"collision_records\": " + std::to_string(f.records) + ", \"wall_triangles\": " + std::to_string(f.hitTris) + ", \"ground_triangles\": " + std::to_string(f.walkTris) + ",\n";
  s += "  \"lights\": " + std::to_string(f.scene.lights) + ", \"camera\": " + (f.scene.camera ? "true" : "false") + ", \"fog\": " + (f.scene.fog ? "true" : "false") + ",\n  \"models\": [";
  for (size_t m = 0; m < f.scene.models.size(); m++) {
    const er::ModelInfo& mi = f.scene.models[m];
    s += std::string(m ? "," : "") + "\n    { \"model\": " + std::to_string(m) + ", \"joints\": " + std::to_string(mi.joints.size()) + ", \"meshes\": " + std::to_string(mi.dobjs) +
         ", \"textures\": " + std::to_string(mi.textures) + ", \"display_list_bytes\": " + std::to_string(mi.dlBytes) + ", \"animated\": " + (mi.animated ? "true" : "false");
    if (joints) {
      s += ", \"joint_list\": [";
      for (size_t j = 0; j < mi.joints.size(); j++) {
        const er::JointInfo& ji = mi.joints[j];
        s += std::string(j ? "," : "") + "\n      { \"joint\": " + std::to_string(ji.number) + ", \"parent\": " + std::to_string(ji.parent) + ", \"depth\": " + std::to_string(ji.depth) +
             ", \"at\": [" + F(ji.trans[0]) + ", " + F(ji.trans[1]) + ", " + F(ji.trans[2]) + "], \"meshes\": " + std::to_string(ji.dobjs) + ", \"display_list_bytes\": " + std::to_string(ji.dlBytes) +
             ", \"textures\": " + std::to_string(ji.textures);
        float lo[3], hi[3];
        if (er::HsdSubtreeBounds(f.arch, ji.offset, lo, hi)) {
          s += ", \"size\": [" + F(hi[0] - lo[0]) + ", " + F(hi[1] - lo[1]) + ", " + F(hi[2] - lo[2]) + "], \"min\": [" + F(lo[0]) + ", " + F(lo[1]) + ", " + F(lo[2]) + "]";
        }
        s += " }";
      }
      s += "\n    ]";
    }
    s += " }";
  }
  s += "\n  ] }";
  return s;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: xd_room_parts <disc> check [name] | list [--json file] | parts <archive> [--json file]\n");
    return 2;
  }
  char error[256] = "";
  if (!PortDisc_Open(argv[1], error, sizeof error)) {
    std::printf("the disc could not be opened: %s\n", error);
    return 2;
  }
  std::vector<std::string> files;
  PortDisc_ListFiles(CollectFile, &files);
  std::vector<std::string> fsys;
  for (const std::string& f : files) {
    if (f.size() > 5 && (f.compare(f.size() - 5, 5, ".fsys") == 0 || f.compare(f.size() - 5, 5, ".FSYS") == 0)) fsys.push_back(f);
  }
  const std::string cmd = argv[2];
  std::string jsonFile;
  std::string only;
  for (int i = 3; i < argc; i++) {
    if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
    else only = argv[i];
  }
  int rc = 0;
  if (cmd == "check" || cmd == "list") {
    std::string json = "{ \"format\": \"xd-room-catalogue\", \"version\": 1, \"floors\": [";
    size_t floors = 0, bad = 0;
    uint32_t largest = 0;
    std::string largestName;
    for (const std::string& file : fsys) {
      if (!only.empty() && Name(file) != only) continue;
      Floor f;
      if (!Load(file, f)) continue;
      floors++;
      if (f.fa.collision >= 0 && f.fa.map >= 0 && f.fa.unpackedBytes > largest) largest = f.fa.unpackedBytes, largestName = f.name;
      const bool ok = (f.fa.map < 0 || f.mapOk) && (f.fa.collision < 0 || f.colOk);
      if (cmd == "check") {
        if (!ok) bad++;
        std::printf("%-4s %-34s map %7zu bytes (%zu models, %zu joints, %d lights)  collision %6zu bytes (%zu records)\n", ok ? "ok" : "FAIL", f.name.c_str(), f.map.size(),
                    f.scene.models.size(), JointCount(f), f.scene.lights, f.collision.size(), f.records);
        for (const std::string& p : f.mapProblems) std::printf("       %s\n", p.c_str());
        for (const std::string& p : f.colProblems) std::printf("       %s\n", p.c_str());
      } else {
        json += std::string(floors > 1 ? "," : "") + "\n" + FloorJson(f, false);
      }
    }
    json += "\n], \"largest_floor_archive\": { \"archive\": \"" + largestName + "\", \"unpacked_bytes\": " + std::to_string(largest) + " } }\n";
    if (cmd == "check") {
      std::printf("%zu floor archive(s) checked, %zu refused; largest floor archive %s = %u bytes unpacked\n", floors, bad, largestName.c_str(), largest);
      rc = bad == 0 && floors != 0 ? 0 : 1;
    } else {
      if (jsonFile.empty()) std::fputs(json.c_str(), stdout);
      else std::ofstream(jsonFile, std::ios::binary) << json;
      std::printf("%zu floor archive(s); largest %s = %u bytes\n", floors, largestName.c_str(), largest);
    }
  } else if (cmd == "parts") {
    const std::string file = xd::rooms::FindArchiveFile(files, only);
    Floor f;
    if (file.empty() || !Load(file, f)) {
      std::printf("no floor archive called \"%s\" on the disc\n", only.c_str());
      rc = 1;
    } else {
      const std::string json = FloorJson(f, true) + "\n";
      if (jsonFile.empty()) std::fputs(json.c_str(), stdout);
      else std::ofstream(jsonFile, std::ios::binary) << json;
    }
  } else {
    std::printf("unknown command %s\n", cmd.c_str());
    rc = 2;
  }
  PortDisc_Close();
  return rc;
}
