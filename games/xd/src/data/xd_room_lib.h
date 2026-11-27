#ifndef XD_ROOM_LIB_H
#define XD_ROOM_LIB_H

#include <room_recipe.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xd::rooms {

using ReadFile = std::function<bool(const std::string& file, std::vector<uint8_t>& out)>;
using ReadPart = std::function<bool(const std::string& path, std::vector<uint8_t>& out)>;

constexpr int kTypeMap = 1, kTypeCollision = 3, kTypeScript = 7;

struct Entry { uint32_t index = 0, type = 0, size = 0; std::string name; };
struct FloorArchive {
  std::string file;
  std::vector<Entry> entries;
  int map = -1, collision = -1;
  uint32_t unpackedBytes = 0;
};
bool ListArchive(const std::vector<uint8_t>& archive, FloorArchive& out);
bool ExtractEntry(const std::vector<uint8_t>& archive, uint32_t index, std::vector<uint8_t>& out);
std::string FindArchiveFile(const std::vector<std::string>& discFiles, const std::string& archiveName);

struct BuiltRoom {
  bool ok = false;
  std::vector<std::string> problems, report;
  engine::rooms::RoomRecipe recipe;
  FloorArchive base;
  engine::rooms::RoomOutputs out;
  std::vector<uint8_t> baseArchive;
  std::vector<uint8_t> archive;
};
constexpr uint32_t kLargestFloorArchiveBytes = 5059483;

void NoteRoomHash(const std::string& room, const std::string& sha1);
const std::vector<std::pair<std::string, std::string>>& RoomHashes();

bool BuildRoom(const std::string& recipeJson, const std::string& baseOverride, const ReadFile& read, const std::vector<std::string>& discFiles, BuiltRoom& out,
               const ReadPart& readPart = ReadPart());

}

#endif
