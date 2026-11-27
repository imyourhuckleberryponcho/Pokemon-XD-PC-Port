#ifndef ENGINE_ROOM_RECIPE_H
#define ENGINE_ROOM_RECIPE_H

#include "room_ccd.h"
#include "room_hsd.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::rooms {

constexpr int kRecipeVersion = 1;

struct RecipeHide { int model = 0, joint = 0; bool children = true; };
struct RecipeMove {
  int model = 0, joint = 0;
  float by[3] = {}, turn = 0;
  int record = -1;
  bool hasArea = false;
  float area[4] = {};
};
struct RecipePiece {
  std::string from;
  int model = 0, joint = 0;
  float at[3] = {}, turn = 0;
  std::string hash;
  std::string collide = "none";
  float height = 0;
  std::string gltf;
  float scale = 1;
};
struct RecipeWall { float x0 = 0, z0 = 0, x1 = 0, z1 = 0, y = -2.f, height = 24.f; bool flip = false; };
struct RecipeBox { float cx = 0, cz = 0, hx = 5, hz = 5, y = -2.f, height = 24.f; };
struct RecipeArea { float x0 = 0, z0 = 0, x1 = 0, z1 = 0; };
struct RecipeEntrance { float x = 0, y = 0, z = 0, angle = 0; std::string exitOf; };
struct RecipeCamera {
  int kind = 0;
  float pos[3] = {}, distance = 60, height = 40, rotY = 0, radius = 80, ratio = 1, angle = 0, powValue = 1, blendTime = 30, fov = 30;
};

struct RoomRecipe {
  int version = 0;
  std::string name, base, baseHash;
  std::vector<RecipeHide> hide;
  std::vector<RecipeMove> move;
  std::vector<RecipePiece> pieces;
  std::vector<RecipeWall> wallsAdd;
  std::vector<RecipeBox> boxes;
  std::vector<RecipeArea> wallsRemove;
  bool entrancesGiven = false, cameraGiven = false;
  std::vector<RecipeEntrance> entrances;
  std::vector<RecipeCamera> camera;
};

bool ParseRecipe(const std::string& json, RoomRecipe& out, std::vector<std::string>& problems);
std::string RecipeToJson(const RoomRecipe& r);

std::vector<uint8_t> EntranceBytes(const std::vector<RecipeEntrance>& e);
std::vector<uint8_t> CameraBytes(const std::vector<RecipeCamera>& c);

struct RoomInputs {
  std::vector<uint8_t> map, collision;
  std::function<bool(const std::string& archive, std::vector<uint8_t>& map)> sourceMap;
  std::function<std::string(const std::vector<uint8_t>&)> hashOf;
  std::function<bool(const std::string& path, std::vector<uint8_t>& bytes)> readPart;
  uint32_t maxArchiveBytes = 0;
  uint32_t baseArchiveBytes = 0;
};

struct RoomOutputs {
  std::vector<uint8_t> map, collision;
  std::vector<std::string> report;
  uint32_t mapBytes = 0, collisionBytes = 0, textureBytesAdded = 0;
  int pieces = 0;
};

std::string RoomSha1(const uint8_t* data, size_t size);

bool ApplyRecipe(const RoomRecipe& r, const RoomInputs& in, RoomOutputs& out, std::vector<std::string>& problems);

}

#endif
