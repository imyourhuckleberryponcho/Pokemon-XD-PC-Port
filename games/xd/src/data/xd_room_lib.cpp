#include "xd_room_lib.h"

#include <gs_formats.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace xd::rooms {

namespace {
std::vector<std::pair<std::string, std::string>> sRoomHashes;
}
void NoteRoomHash(const std::string& room, const std::string& sha1) {
  for (auto& p : sRoomHashes) if (p.first == room) { p.second = sha1; return; }
  sRoomHashes.emplace_back(room, sha1);
}
const std::vector<std::pair<std::string, std::string>>& RoomHashes() { return sRoomHashes; }

namespace er = engine::rooms;

namespace {
std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}

bool ListArchive(const std::vector<uint8_t>& archive, FloorArchive& out) {
  FsysArchive f{};
  if (!Fsys_Open(archive.data(), archive.size(), &f)) return false;
  out.entries.clear();
  out.map = out.collision = -1;
  out.unpackedBytes = 0;
  for (uint32_t i = 0; i < f.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&f, i, &e)) return false;
    Entry en;
    en.index = i, en.type = e.type, en.size = e.size, en.name = e.fullName[0] != 0 ? e.fullName : (e.name != nullptr ? e.name : "");
    if (e.type == uint32_t(kTypeMap) && out.map < 0) out.map = int(out.entries.size());
    if (e.type == uint32_t(kTypeCollision) && out.collision < 0) out.collision = int(out.entries.size());
    out.unpackedBytes += e.size;
    out.entries.push_back(std::move(en));
  }
  return true;
}

bool ExtractEntry(const std::vector<uint8_t>& archive, uint32_t index, std::vector<uint8_t>& out) {
  FsysArchive f{};
  if (!Fsys_Open(archive.data(), archive.size(), &f) || index >= f.count) return false;
  uint32_t n = 0;
  uint8_t* p = Fsys_Extract(&f, index, &n);
  if (p == nullptr) return false;
  out.assign(p, p + n);
  std::free(p);
  return true;
}

std::string FindArchiveFile(const std::vector<std::string>& files, const std::string& name) {
  const std::string want = Lower(name) + ".fsys";
  for (const std::string& f : files) {
    const size_t slash = f.find_last_of("/\\");
    if (Lower(slash == std::string::npos ? f : f.substr(slash + 1)) == want) return f;
  }
  return "";
}

bool BuildRoom(const std::string& json, const std::string& baseOverride, const ReadFile& read, const std::vector<std::string>& discFiles, BuiltRoom& out, const ReadPart& readPart) {
  out = BuiltRoom{};
  if (!er::ParseRecipe(json, out.recipe, out.problems)) return false;
  const std::string baseName = baseOverride.empty() ? out.recipe.base : baseOverride;
  const auto archiveOf = [&](const std::string& name, std::vector<uint8_t>& bytes, FloorArchive& fa) {
    const std::string file = FindArchiveFile(discFiles, name);
    return !file.empty() && read(file, bytes) && ListArchive(bytes, fa) && (fa.file = file, true);
  };
  if (!archiveOf(baseName, out.baseArchive, out.base)) {
    out.problems.push_back("room \"" + out.recipe.name + "\": the base room \"" + baseName + "\" is not an archive of the disc");
    return false;
  }
  if (out.base.map < 0 || out.base.collision < 0) {
    out.problems.push_back("room \"" + out.recipe.name + "\": the base room " + baseName + " has no map or no collision entry (it is not a floor)");
    return false;
  }
  er::RoomInputs in;
  if (!ExtractEntry(out.baseArchive, out.base.entries[size_t(out.base.map)].index, in.map) ||
      !ExtractEntry(out.baseArchive, out.base.entries[size_t(out.base.collision)].index, in.collision)) {
    out.problems.push_back("room \"" + out.recipe.name + "\": the base room's map or collision could not be unpacked");
    return false;
  }
  in.sourceMap = [&](const std::string& name, std::vector<uint8_t>& map) {
    std::vector<uint8_t> bytes;
    FloorArchive fa;
    return archiveOf(name, bytes, fa) && fa.map >= 0 && ExtractEntry(bytes, fa.entries[size_t(fa.map)].index, map);
  };
  in.readPart = readPart;
  in.hashOf = [](const std::vector<uint8_t>& b) { return er::RoomSha1(b.data(), b.size()); };
  if (kLargestFloorArchiveBytes != 0) {
    in.maxArchiveBytes = kLargestFloorArchiveBytes;
    in.baseArchiveBytes = out.base.unpackedBytes - out.base.entries[size_t(out.base.map)].size - out.base.entries[size_t(out.base.collision)].size;
  }
  if (!er::ApplyRecipe(out.recipe, in, out.out, out.problems)) return false;
  out.report = out.out.report;
  const FsysReplacement list[2] = {{out.base.entries[size_t(out.base.map)].index, out.out.map.data(), uint32_t(out.out.map.size())},
                                   {out.base.entries[size_t(out.base.collision)].index, out.out.collision.data(), uint32_t(out.out.collision.size())}};
  size_t size = 0;
  uint8_t* built = Fsys_Rebuild(out.baseArchive.data(), out.baseArchive.size(), list, 2, &size);
  if (built == nullptr) {
    out.problems.push_back("room \"" + out.recipe.name + "\": the archive could not be made");
    return false;
  }
  out.archive.assign(built, built + size);
  std::free(built);
  out.ok = true;
  return true;
}

}
