#include "../src/data/xd_room_lib.h"

#include <port_disc.h>
#include <room_preview.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void CollectFile(const char* path, uint32_t, void* user) { static_cast<std::vector<std::string>*>(user)->push_back(path); }

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

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: xd_room_build <disc> <room.json> [--base <archive>] [--out <folder>] [--json <file>]\n");
    return 2;
  }
  std::string base, out, jsonFile;
  for (int i = 3; i < argc; i++) {
    if (!std::strcmp(argv[i], "--base") && i + 1 < argc) base = argv[++i];
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) jsonFile = argv[++i];
  }
  std::ifstream in(argv[2], std::ios::binary);
  if (!in) {
    std::printf("the recipe %s could not be read\n", argv[2]);
    return 2;
  }
  std::stringstream text;
  text << in.rdbuf();
  char error[256] = "";
  if (!PortDisc_Open(argv[1], error, sizeof error)) {
    std::printf("the disc could not be opened: %s\n", error);
    return 2;
  }
  std::vector<std::string> files;
  PortDisc_ListFiles(CollectFile, &files);
  const xd::rooms::ReadFile read = [](const std::string& file, std::vector<uint8_t>& bytes) {
    size_t size = 0;
    uint8_t* p = PortDisc_ReadOriginalFile(file.c_str(), &size);
    if (p == nullptr) return false;
    bytes.assign(p, p + size);
    PortDisc_Free(p);
    return true;
  };
  xd::rooms::BuiltRoom room;
  const std::filesystem::path recipeDir = std::filesystem::path(argv[2]).parent_path();
  const xd::rooms::ReadPart readPart = [&](const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(recipeDir / path, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
  };
  const bool ok = xd::rooms::BuildRoom(text.str(), base, read, files, room, readPart);
  for (const std::string& p : room.problems) std::printf("PROBLEM  %s\n", p.c_str());
  for (const std::string& r : room.report) std::printf("done     %s\n", r.c_str());
  if (ok) {
    std::printf("room \"%s\" is usable: map %u bytes, collision %u bytes, archive %zu bytes on disc form (base %s: %u bytes unpacked)\n", room.recipe.name.c_str(), room.out.mapBytes,
                room.out.collisionBytes, room.archive.size(), room.base.file.c_str(), room.base.unpackedBytes);
    if (!out.empty()) {
      std::filesystem::create_directories(out);
      std::ofstream(std::filesystem::path(out) / "map.bin", std::ios::binary).write(reinterpret_cast<const char*>(room.out.map.data()), std::streamsize(room.out.map.size()));
      std::ofstream(std::filesystem::path(out) / "collision.bin", std::ios::binary)
          .write(reinterpret_cast<const char*>(room.out.collision.data()), std::streamsize(room.out.collision.size()));
      engine::rooms::Ccd ccd;
      std::string why;
      if (engine::rooms::CcdRead(room.out.collision.data(), room.out.collision.size(), ccd, why)) {
        const std::vector<uint8_t> png = engine::rooms::CcdTopViewPng(ccd, 640);
        std::ofstream(std::filesystem::path(out) / "topview.png", std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
      }
      std::printf("written to %s\n", out.c_str());
    }
  }
  if (!jsonFile.empty()) {
    std::string j = std::string("{ \"ok\": ") + (ok ? "true" : "false") + ", \"problems\": [";
    for (size_t i = 0; i < room.problems.size(); i++) j += std::string(i ? ", " : "") + "\"" + Esc(room.problems[i]) + "\"";
    j += "], \"report\": [";
    for (size_t i = 0; i < room.report.size(); i++) j += std::string(i ? ", " : "") + "\"" + Esc(room.report[i]) + "\"";
    j += "], \"map_bytes\": " + std::to_string(room.out.mapBytes) + ", \"collision_bytes\": " + std::to_string(room.out.collisionBytes) +
         ", \"base_unpacked_bytes\": " + std::to_string(room.base.unpackedBytes) + " }\n";
    std::ofstream(jsonFile, std::ios::binary) << j;
  }
  PortDisc_Close();
  return ok ? 0 : 1;
}
