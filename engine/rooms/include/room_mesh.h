#ifndef ENGINE_ROOM_MESH_H
#define ENGINE_ROOM_MESH_H

#include "room_ccd.h"
#include "room_hsd.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::rooms {

struct MeshImage {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
  bool repeat = false;
};

struct MeshMaterial {
  float color[4] = {1, 1, 1, 1};
  int image = -1;
  bool doubleSided = false;
};

struct MeshPrim {
  int material = 0;
  std::vector<float> pos;
  std::vector<float> uv;
  std::vector<float> color;
  std::vector<uint32_t> idx;
};

struct MeshModel {
  std::vector<MeshPrim> prims;
  std::vector<MeshMaterial> materials;
  std::vector<MeshImage> images;
};

bool GltfRead(const uint8_t* bytes, size_t size, MeshModel& out, std::string& problem);
bool PngDecode(const uint8_t* bytes, size_t size, int& width, int& height, std::vector<uint8_t>& rgba, std::string& problem);

std::vector<std::array<float, 9>> MeshWorldTriangles(const MeshModel& m, float scale, float turnDegrees, float x, float y, float z);

bool HsdAddMeshModel(HsdArchive& into, const MeshModel& m, float scale, float turnDegrees, float x, float y, float z, std::string& problem, uint32_t* newRoot = nullptr,
                     uint32_t* bytesAdded = nullptr);

struct MeshCollisionCount { size_t walls = 0, floor = 0, skipped = 0; };
MeshCollisionCount CcdAddMeshTriangles(CcdRecord& rec, const std::vector<std::array<float, 9>>& tris, float floorLimit = 0.5f);

}

#endif
