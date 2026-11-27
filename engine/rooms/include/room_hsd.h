#ifndef ENGINE_ROOM_HSD_H
#define ENGINE_ROOM_HSD_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::rooms {

struct HsdArchive {
  std::vector<uint8_t> data;
  std::vector<uint32_t> slots;
  std::vector<std::pair<std::string, uint32_t>> publics;
  std::vector<std::pair<std::string, uint32_t>> externs;

  uint32_t U32(uint32_t off) const;
  float F32(uint32_t off) const;
  void SetU32(uint32_t off, uint32_t v);
  void SetF32(uint32_t off, float v);
  bool InData(uint32_t off, size_t bytes) const { return size_t(off) + bytes <= data.size(); }
  bool IsSlot(uint32_t off) const;
  void AddSlot(uint32_t off);
  void RemoveSlot(uint32_t off);
  int FindPublic(const std::string& name) const;
};

bool HsdParse(const uint8_t* bytes, size_t size, HsdArchive& out, std::vector<std::string>& problems);
std::vector<uint8_t> HsdWrite(const HsdArchive& a);

struct JointInfo {
  int number = 0;
  int parent = -1;
  int depth = 0;
  uint32_t offset = 0;
  uint32_t flags = 0;
  float rot[3] = {}, scale[3] = {}, trans[3] = {};
  int dobjs = 0;
  uint32_t dlBytes = 0;
  int textures = 0;
  bool box = false;
  float boxMin[3] = {}, boxMax[3] = {};
};

struct ModelInfo {
  uint32_t set = 0, root = 0;
  std::vector<JointInfo> joints;
  int dobjs = 0, pobjs = 0, textures = 0;
  uint32_t dlBytes = 0, textureBytes = 0;
  bool animated = false;
};

struct SceneInfo {
  std::vector<ModelInfo> models;
  int lights = 0;
  bool camera = false, fog = false;
  uint32_t textureBytes = 0;
};

bool HsdValidateMap(const uint8_t* bytes, size_t size, std::vector<std::string>& problems, SceneInfo* info = nullptr);
bool HsdListScene(const HsdArchive& a, SceneInfo& out, std::vector<std::string>& problems);

void HsdSetTranslation(HsdArchive& a, uint32_t joint, float x, float y, float z);
void HsdAddTurnY(HsdArchive& a, uint32_t joint, float degrees);
void HsdSetScale(HsdArchive& a, uint32_t joint, float sx, float sy, float sz);
int HsdHideJoint(HsdArchive& a, uint32_t joint, bool children);

struct HsdPieceCache { std::map<std::pair<const void*, uint32_t>, uint32_t> shared; };
bool HsdAddPiece(HsdArchive& into, const HsdArchive& from, uint32_t root, float x, float y, float z, float turnDegrees, std::string& problem,
                 uint32_t* newRoot = nullptr, uint32_t* bytesAdded = nullptr, HsdPieceCache* cache = nullptr);

bool HsdAddModelSet(HsdArchive& into, uint32_t root, std::string& problem);

bool HsdSubtreeBounds(const HsdArchive& a, uint32_t root, float outMin[3], float outMax[3]);

}

#endif
