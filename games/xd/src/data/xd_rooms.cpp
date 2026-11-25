#include "xd_data_mods.h"
#include "xd_faces.h"
#include "xd_species.h"
#include "xd_sounds.h"
#include "xd_toc.h"
#include "xd_objects.h"
#include "json.h"

#include <gs_formats.h>
#include <mod_layout.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <map>

namespace xd::data {

namespace em = engine::mods;
namespace fs = std::filesystem;

namespace {

uint32_t Be32(const uint8_t* p) { return xd::toc::Be32(p); }
void PutBe32(uint8_t* p, uint32_t v) { xd::toc::PutBe32(p, v); }
std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

using Toc = xd::toc::Toc;
inline bool ReadToc(const std::vector<uint8_t>& d, Toc& t) { return xd::toc::Read(d, t); }
inline std::string TocName(const std::vector<uint8_t>& d, const Toc& t, uint32_t id, uint32_t* row = nullptr) { return xd::toc::Name(d, t, id, row); }
inline uint32_t TocTopId(const Toc& t) { return xd::toc::TopId(t); }
inline std::vector<uint8_t> AddToc(const Toc& t, const std::vector<std::pair<uint32_t, std::string>>& rows) { return xd::toc::Add(t, rows); }

std::string Sanitize(const std::string& key) {
  std::string out;
  for (char c : key) out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
  return out;
}

bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool FindEntryIndex(const FsysArchive& fsys, const std::string& file, uint32_t& index) {
  if (file.size() >= 2 && file[0] == '#') {
    char* end = nullptr;
    const unsigned long n = std::strtoul(file.c_str() + 1, &end, 10);
    if (end != file.c_str() + 1 && (*end == '\0' || *end == '.') && n < fsys.count) {
      index = static_cast<uint32_t>(n);
      return true;
    }
    return false;
  }
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (Fsys_Entry(&fsys, i, &e) && e.fullName[0] != '\0' && Lower(e.fullName) == Lower(file)) {
      index = i;
      return true;
    }
  }
  return false;
}

}

static void BuildNewRooms(em::RowsJob& job) {
  std::vector<em::AddedRow>& rows = *job.rows;
  const auto say = [&](em::Message::Level level, const std::string& text) { if (job.say) job.say(level, text); };
  std::vector<size_t> floors;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].table == "floordata") floors.push_back(i);
  }
  if (floors.empty()) return;

  std::vector<uint8_t> tocBytes;
  std::string tocPath = "GSfsys.toc";
  for (const std::string& f : job.discFiles) {
    if (Lower(f) == "gsfsys.toc") tocPath = f;
  }
  Toc toc;
  if (!job.readFile(tocPath, tocBytes) || !ReadToc(tocBytes, toc)) {
    say(em::Message::Level::Problem, "New rooms: the game's archive list (GSfsys.toc) could not be read; the rooms are not made.");
    return;
  }
  const em::DataTable* doors = job.catalog != nullptr ? job.catalog->FindTable("floor_door_list") : nullptr;
  const em::DataTable* floorTable = job.catalog != nullptr ? job.catalog->FindTable("floordata") : nullptr;
  std::vector<uint8_t> doorBytes;
  if (doors != nullptr && !doors->places.empty()) {
    if (!job.readEntry(doors->places[0].archive, doors->places[0].entry, doorBytes)) doorBytes.clear();
  }
  std::vector<uint8_t> floorBytes;
  if (floorTable != nullptr && !floorTable->places.empty()) {
    if (!job.readEntry(floorTable->places[0].archive, floorTable->places[0].entry, floorBytes)) floorBytes.clear();
  }

  std::vector<std::pair<uint32_t, std::string>> newGroups;
  uint32_t nextGroup = std::max(TocTopId(toc), 5000u);
  uint32_t doorCount = doors != nullptr ? doors->count : 0;
  std::string mods;
  std::vector<em::AddedRow> extra;
  std::set<std::string> usedNames;
  for (const std::string& f : job.discFiles) usedNames.insert(Lower(f));

  for (size_t index : floors) {
    em::AddedRow& row = rows[index];
    const std::string what = "The room \"" + row.key + "\" of the mod \"" + row.modName + "\"";
    if (row.bytes.size() != 64 || row.copyOf >= (floorTable != nullptr ? floorTable->count : 0)) {
      say(em::Message::Level::Problem, what + " must copy a room of the game (its \"copy\" is another new room); the room stays a plain copy of the original.");
      continue;
    }
    const uint32_t sourceGroup = Be32(row.bytes.data() + 0x2C);
    const std::string sourceName = TocName(tocBytes, toc, sourceGroup);
    if (sourceName.empty()) {
      say(em::Message::Level::Problem, what + ": the room it copies has no archive in the archive list; the room stays a plain copy of the original.");
      continue;
    }
    std::vector<uint8_t> archive;
    if (!job.readFile(sourceName + ".fsys", archive)) {
      say(em::Message::Level::Problem, what + ": the archive " + sourceName + ".fsys could not be read; the room stays a plain copy of the original.");
      continue;
    }
    FsysArchive fsys;
    if (!Fsys_Open(archive.data(), archive.size(), &fsys)) {
      say(em::Message::Level::Problem, what + ": " + sourceName + ".fsys is not an archive the port can read; the room stays a plain copy of the original.");
      continue;
    }
    std::vector<std::vector<uint8_t>> contents;
    std::vector<FsysReplacement> list;
    const auto folder = job.modFolders.find(row.mod);
    std::vector<std::pair<std::string, fs::path>> files;
    if (folder != job.modFolders.end()) {
      for (em::LayoutFile& f : em::LayoutFiles(folder->second, job.gameId, "rooms/" + row.key, false)) files.emplace_back(f.rel, f.path);
      std::sort(files.begin(), files.end());
    }
    contents.reserve(files.size());
    std::set<uint32_t> seen;
    for (const auto& [name, path] : files) {
      uint32_t entry = 0;
      if (!FindEntryIndex(fsys, name, entry) || !seen.insert(entry).second) {
        say(em::Message::Level::Problem, what + ": rooms/" + row.key + "/" + name + " names no entry of the archive it copies (" + sourceName +
                                             ".fsys), or an entry twice; ignored.");
        continue;
      }
      std::vector<uint8_t> data;
      if (!ReadFileBytes(path, data) || data.empty()) {
        say(em::Message::Level::Problem, what + ": rooms/" + row.key + "/" + name + " could not be read; ignored.");
        continue;
      }
      FsysEntry old;
      Fsys_Entry(&fsys, entry, &old);
      std::string why;
      if (job.checkEntry && !job.checkEntry(static_cast<int>(old.type), row.mod, data, rows, why)) {
        say(em::Message::Level::Problem, what + ": rooms/" + row.key + "/" + name + " is not usable (" + why + "); the copied room's own file is kept.");
        continue;
      }
      contents.push_back(std::move(data));
      list.push_back({entry, contents.back().data(), static_cast<uint32_t>(contents.back().size())});
    }
    size_t size = 0;
    uint8_t* built = list.empty() ? nullptr : Fsys_Rebuild(archive.data(), archive.size(), list.data(), list.size(), &size);
    std::vector<uint8_t> out;
    if (built != nullptr) {
      out.assign(built, built + size);
      std::free(built);
    } else {
      if (!list.empty()) {
        say(em::Message::Level::Problem, what + ": the archive could not be made; the room stays a plain copy of the original.");
        continue;
      }
      out = archive;
    }
    const uint32_t group = ++nextGroup;
    if (out.size() >= 12) PutBe32(out.data() + 8, group);
    std::string name = "xdmod_" + Sanitize(row.key) + "_" + std::to_string(group);
    if (name.size() > 40) name.resize(40);
    while (usedNames.count(Lower(name + ".fsys")) != 0) name += "_";
    usedNames.insert(Lower(name + ".fsys"));
    newGroups.emplace_back(group, name);
    job.outFiles.push_back({name + ".fsys", std::move(out), row.mod});
    for (int lang = 0; lang < 5; lang++) PutBe32(row.bytes.data() + 0x2C + 4 * lang, group);
    const uint32_t newFloor = row.id;
    uint32_t madeDoors = 0;
    if (doors != nullptr && !doorBytes.empty() && floorTable != nullptr && !floorBytes.empty()) {
      const uint32_t sourceId = static_cast<uint32_t>(floorBytes[floorTable->places[0].offset + static_cast<size_t>(row.copyOf) * 64 + 2]) << 8 |
                                floorBytes[floorTable->places[0].offset + static_cast<size_t>(row.copyOf) * 64 + 3];
      for (uint32_t i = 0; i < doors->count; i++) {
        const size_t at = doors->places[0].offset + static_cast<size_t>(i) * doors->entrySize;
        if (at + doors->entrySize > doorBytes.size()) break;
        if ((static_cast<uint32_t>(doorBytes[at + 0x0C]) << 8 | doorBytes[at + 0x0D]) != sourceId) continue;
        em::AddedRow d;
        d.table = "floor_door_list";
        d.row = d.id = doorCount++;
        d.copyOf = i;
        d.key = row.key + ".door" + std::to_string(madeDoors++);
        d.mod = row.mod, d.modName = row.modName, d.where = row.where;
        d.bytes.assign(doorBytes.begin() + at, doorBytes.begin() + at + doors->entrySize);
        d.bytes[0x0C] = static_cast<uint8_t>(newFloor >> 8);
        d.bytes[0x0D] = static_cast<uint8_t>(newFloor);
        extra.push_back(std::move(d));
      }
    }
    char line[256];
    std::snprintf(line, sizeof line, "New room \"%s\" (mod %s): floor %u, archive %s.fsys (group %u), a copy of %s.fsys with %zu entr%s of the mod, %u door row(s)",
                  row.key.c_str(), row.mod.c_str(), row.id, name.c_str(), group, sourceName.c_str(), list.size(), list.size() == 1 ? "y" : "ies", madeDoors);
    say(em::Message::Level::Info, line);
    if (mods.find(row.mod) == std::string::npos) mods += (mods.empty() ? "" : ", ") + row.mod;
  }
  for (em::AddedRow& d : extra) rows.push_back(std::move(d));
  if (!newGroups.empty()) job.outFiles.push_back({tocPath, AddToc(toc, newGroups), mods});
}

namespace {

struct ObjectsRoom {
  uint32_t floorId = 0, floorRow = 0, group = 0, sourceGroup = 0;
  bool isNew = false;
  std::string archive;
};

uint32_t CountRows(const std::vector<em::AddedRow>& rows, const std::string& table) {
  uint32_t n = 0;
  for (const em::AddedRow& r : rows) n += r.table == table;
  return n;
}

void BuildObjects(em::RowsJob& job) {
  std::vector<em::AddedRow>& rows = *job.rows;
  const auto say = [&](em::Message::Level level, const std::string& text) { if (job.say) job.say(level, text); };
  std::string tocPath = "GSfsys.toc";
  for (const std::string& f : job.discFiles) {
    if (Lower(f) == "gsfsys.toc") tocPath = f;
  }
  const auto tableBytes = [&](const char* name, const em::DataTable*& t, std::vector<uint8_t>& bytes) {
    t = job.catalog != nullptr ? job.catalog->FindTable(name) : nullptr;
    bytes.clear();
    if (t != nullptr && !t->places.empty() && !job.readEntry(t->places[0].archive, t->places[0].entry, bytes)) bytes.clear();
  };
  const em::DataTable *floorTable = nullptr, *doorTable = nullptr, *soundTable = nullptr, *chestTable = nullptr, *eventTable = nullptr;
  std::vector<uint8_t> floorBytes, doorBytes, soundBytes, ignore;
  tableBytes("floordata", floorTable, floorBytes);
  tableBytes("floor_door_list", doorTable, doorBytes);
  tableBytes("floorsoundTbl", soundTable, soundBytes);
  tableBytes("floor_tresure_list", chestTable, ignore);
  tableBytes("floor_event_list", eventTable, ignore);
  if (floorTable == nullptr || !floorTable->addable) {
    for (const auto& [modId, folder] : job.modFolders) {
      if (em::HasLayoutFolder(folder, job.gameId, "objects")) say(em::Message::Level::Info, "The mod \"" + modId + "\": objects of rooms (people, zones, chests, music, doors) are not used in this edition of the game.");
    }
    return;
  }
  const em::NameList* floorNames = job.catalog != nullptr ? job.catalog->FindList("floor") : nullptr;
  const em::NameList* itemNames = job.catalog != nullptr ? job.catalog->FindList("item") : nullptr;
  std::set<uint32_t> peopleRooms;

  for (const auto& [modId, folder] : job.modFolders) {
    std::vector<fs::path> files;
    for (const em::LayoutFile& f : em::LayoutFiles(folder, job.gameId, "objects", false)) {
      if (Lower(fs::path(f.rel).extension().string()) == ".json") files.push_back(f.path);
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const fs::path& file : files) {
      const std::string room = file.stem().string();
      const std::string what = "objects/" + file.filename().string() + " of the mod \"" + modId + "\"";
      std::vector<uint8_t> raw;
      if (!ReadFileBytes(file, raw)) { say(em::Message::Level::Problem, what + " could not be read; ignored."); continue; }
      xd::objects::Room spec;
      std::vector<std::string> problems;
      const auto report = [&] {
        for (const std::string& p : problems) say(em::Message::Level::Problem, what + ": " + p);
        problems.clear();
      };
      if (!xd::objects::Parse(std::string(raw.begin(), raw.end()), spec, problems)) { report(); continue; }
      report();

      ObjectsRoom r;
      std::string modName = modId;
      const em::AddedRow* newRow = nullptr;
      for (const em::AddedRow& row : rows) {
        if (row.table == "floordata" && row.mod == modId && row.key == room) newRow = &row;
      }
      if (newRow != nullptr) {
        r.isNew = true;
        r.floorId = newRow->id;
        r.floorRow = newRow->row;
        r.group = Be32(newRow->bytes.data() + 0x2C);
        if (floorTable != nullptr && !floorBytes.empty() && newRow->copyOf < floorTable->count) {
          r.sourceGroup = Be32(floorBytes.data() + floorTable->places[0].offset + static_cast<size_t>(newRow->copyOf) * 64 + 0x2C);
        }
        modName = newRow->modName;
      } else if (floorNames != nullptr && floorTable != nullptr && !floorBytes.empty()) {
        bool found = false;
        for (const auto& [name, number] : floorNames->names) {
          if (Lower(name) != Lower(room)) continue;
          const size_t at = floorTable->places[0].offset + static_cast<size_t>(number) * 64;
          if (at + 64 > floorBytes.size()) continue;
          r.floorRow = number;
          r.floorId = static_cast<uint32_t>(floorBytes[at + 2]) << 8 | floorBytes[at + 3];
          r.group = Be32(floorBytes.data() + at + 0x2C);
          found = true;
          break;
        }
        if (!found) {
          say(em::Message::Level::Problem, what + ": no room of that name (a room the mod adds with newroom, or a disc room's archive name, xd_data_mods <disc> --names floor); ignored.");
          continue;
        }
      } else {
        say(em::Message::Level::Problem, what + ": the game's room table could not be read; ignored.");
        continue;
      }
      std::vector<uint8_t> tocBytes;
      Toc toc;
      for (const em::RowsJob::OutFile& o : job.outFiles) {
        if (o.discPath == tocPath) tocBytes = o.data;
      }
      if (tocBytes.empty()) job.readFile(tocPath, tocBytes);
      if (!ReadToc(tocBytes, toc)) { say(em::Message::Level::Problem, what + ": the archive list could not be read; ignored."); continue; }
      const std::string archiveName = TocName(tocBytes, toc, r.group);
      if (archiveName.empty()) { say(em::Message::Level::Problem, what + ": the room has no archive in the archive list; ignored."); continue; }
      r.archive = archiveName + ".fsys";
      for (const std::string& f : job.discFiles) {
        if (Lower(f) == Lower(r.archive)) r.archive = f;
      }
      em::RowsJob::OutFile* made = nullptr;
      for (em::RowsJob::OutFile& o : job.outFiles) {
        if (Lower(o.discPath) == Lower(r.archive)) made = &o;
      }
      std::vector<uint8_t> archive;
      if (made != nullptr) {
        archive = made->data;
      } else if (!job.readFile(r.archive, archive)) {
        say(em::Message::Level::Problem, what + ": the archive " + r.archive + " could not be read; ignored.");
        continue;
      }
      FsysArchive fsys;
      if (!Fsys_Open(archive.data(), archive.size(), &fsys)) {
        say(em::Message::Level::Problem, what + ": " + r.archive + " is not an archive the port can read; ignored.");
        continue;
      }
      uint32_t scriptEntry = 0xFFFFFFFFu;
      for (uint32_t i = 0; i < fsys.count && scriptEntry == 0xFFFFFFFFu; i++) {
        FsysEntry e;
        if (Fsys_Entry(&fsys, i, &e) && e.type == 7) scriptEntry = i;
      }
      std::vector<uint8_t> script;
      if (scriptEntry != 0xFFFFFFFFu) {
        uint32_t size = 0;
        if (uint8_t* p = Fsys_Extract(&fsys, scriptEntry, &size)) {
          script.assign(p, p + size);
          std::free(p);
        }
        if (!Tiga_Validate(script.data(), script.size(), nullptr, 0)) script.clear();
      }

      xd::objects::Lookups ask;
      ask.floorId = r.floorId;
      ask.function = [&](const std::string& fn) {
        return script.empty() ? -1 : static_cast<int>(Tiga_FindFunction(script.data(), script.size(), fn.c_str()));
      };
      ask.flag = [&](const std::string& name, uint32_t& id) {
        for (const em::AddedRow& row : rows) {
          if (row.table == "flag_data" && row.mod == modId && row.key == name) { id = row.id; return true; }
        }
        return false;
      };
      ask.item = [&](const std::string& name, uint32_t& id) {
        char* end = nullptr;
        const unsigned long n = std::strtoul(name.c_str(), &end, 10);
        if (end != name.c_str() && *end == '\0') { id = static_cast<uint32_t>(n); return true; }
        if (itemNames == nullptr) return false;
        const std::string key = em::NameKey(name);
        int hits = 0;
        for (const auto& [nm, number] : itemNames->names) {
          if (em::NameKey(nm) == key) { id = number; hits++; }
        }
        return hits == 1;
      };
      ask.doorRow = [&](long index, std::vector<uint8_t>& out) {
        if (doorTable == nullptr || index < 0 || static_cast<uint32_t>(index) >= doorTable->count) return false;
        const size_t at = doorTable->places[0].offset + static_cast<size_t>(index) * doorTable->entrySize;
        if (at + doorTable->entrySize > doorBytes.size()) return false;
        out.assign(doorBytes.begin() + at, doorBytes.begin() + at + doorTable->entrySize);
        return true;
      };
      ask.song = [](const std::string& name) { return xd::sound::SongNumber(name); };
      ask.musicOf = [&](const std::string& like, std::vector<std::vector<uint8_t>>& out) {
        if (floorNames == nullptr || floorTable == nullptr || soundTable == nullptr || floorBytes.empty() || soundBytes.empty()) return false;
        for (const auto& [name, number] : floorNames->names) {
          if (Lower(name) != Lower(like)) continue;
          const size_t at = floorTable->places[0].offset + static_cast<size_t>(number) * 64;
          if (at + 64 > floorBytes.size()) return false;
          const uint32_t id = static_cast<uint32_t>(floorBytes[at + 2]) << 8 | floorBytes[at + 3];
          for (uint32_t i = 0; i < soundTable->count; i++) {
            const size_t s = soundTable->places[0].offset + static_cast<size_t>(i) * soundTable->entrySize;
            if (s + 12 > soundBytes.size()) break;
            if ((static_cast<uint32_t>(soundBytes[s + 2]) << 8 | soundBytes[s + 3]) == id) out.emplace_back(soundBytes.begin() + s, soundBytes.begin() + s + 12);
          }
          return true;
        }
        return false;
      };
      xd::objects::Built built;
      xd::objects::Build(spec, ask, built, problems);
      report();

      {
        std::set<uint32_t> here, shared;
        for (uint32_t i = 0; i < fsys.count; i++) {
          FsysEntry e;
          if (Fsys_Entry(&fsys, i, &e)) here.insert(e.id);
        }
        bool sharedKnown = false;
        std::string sharedName = TocName(tocBytes, toc, 0x9A);
        if (!sharedName.empty()) {
          sharedName += ".fsys";
          for (const std::string& f : job.discFiles) {
            if (Lower(f) == Lower(sharedName)) sharedName = f;
          }
          std::vector<uint8_t> sb;
          FsysArchive sf;
          if (job.readFile(sharedName, sb) && Fsys_Open(sb.data(), sb.size(), &sf)) {
            for (uint32_t i = 0; i < sf.count; i++) {
              FsysEntry e;
              if (Fsys_Entry(&sf, i, &e)) shared.insert(e.id);
            }
            sharedKnown = true;
          }
        }
        const auto loads = [&](uint32_t resource) { return here.count(resource >> 16) != 0 || shared.count(resource >> 16) != 0; };
        if (sharedKnown) {
          std::vector<uint8_t> peopleBytes;
          const em::DataTable* peopleTable = nullptr;
          tableBytes("peopleInfoData", peopleTable, peopleBytes);
          if (peopleTable != nullptr) {
            for (const xd::objects::Person& pr : spec.people) {
              const size_t at = peopleTable->places[0].offset + static_cast<size_t>(pr.model) * peopleTable->entrySize;
              if (pr.model < 0 || static_cast<uint32_t>(pr.model) >= peopleTable->count || at + 8 > peopleBytes.size()) continue;
              if (!loads(Be32(peopleBytes.data() + at + 4))) {
                say(em::Message::Level::Warning, what + ": the model of person \"" + pr.name + "\" (peopleInfoData row " + std::to_string(pr.model) +
                    ") is not in the shared people archive or this room's archive: the person will be missing (the log says person setup failed).");
              }
            }
          }
          if (!spec.chests.empty()) {
            bool used[3] = {false, false, false};
            for (const xd::objects::Chest& ch : spec.chests) used[ch.look == "pickup" ? 1 : ch.look == "pickup2" ? 2 : 0] = true;
            const uint32_t models[3] = {0x2F80400u, 0x2F90400u, 0x2FA0400u};
            for (int mi = 0; mi < 3; mi++) {
              if (!used[mi]) continue;
              const uint32_t res = models[mi];
              char hex[16];
              std::snprintf(hex, sizeof hex, "0x%X", res);
              if (!loads(res)) say(em::Message::Level::Warning, what + ": the chest model " + hex + " is not in the shared people archive or this room's archive: chests will be missing.");
            }
          }
        }
        const size_t mine = spec.people.size() + spec.chests.size();
        if (mine >= 40) {
          say(em::Message::Level::Warning, what + ": " + std::to_string(spec.people.size()) + " people + " + std::to_string(spec.chests.size()) +
              " chests: the game has ONE pool of 48 people (the hero, up to 3 party members, the room's own people that load on entry, every chest); the room's own + yours + chests must stay at 44 or fewer or some are silently missing.");
        }
      }
      if (!built.people.empty() && script.empty()) {
        say(em::Message::Level::Problem, what + ": the room has no readable script, so its people can not be listed in it (they would never talk); the people are left out.");
        built.people.clear(), built.personNames.clear();
      }
      if ((!built.people.empty() || spec.replacePeople) && !peopleRooms.insert(r.floorId).second) {
        say(em::Message::Level::Problem, what + ": another mod already adds people to this room; these people are left out.");
        built.people.clear(), built.personNames.clear();
      }

      if (!script.empty()) {
        std::vector<uint32_t> from, to;
        for (size_t i = 0; i < built.personNames.size(); i++) {
          from.push_back(Tiga_PersonPlaceholder(built.personNames[i].c_str()));
          to.push_back(xd::objects::kModPeopleBase + static_cast<uint32_t>(i));
        }
        size_t remapped = from.empty() ? 0 : Tiga_RemapPeople(script.data(), script.size(), 1, from.data(), to.data(), from.size());
        std::vector<uint32_t> gfrom = {TIGA_THIS_ROOM}, gto = {r.group};
        if (r.isNew && r.sourceGroup != 0 && r.sourceGroup != r.group) {
          gfrom.push_back(r.sourceGroup), gto.push_back(r.group);
        }
        const size_t groups = Tiga_RemapPeople(script.data(), script.size(), 0, gfrom.data(), gto.data(), gfrom.size());
        if (!built.personNames.empty() || groups != 0) {
          say(em::Message::Level::Info, "Objects of the room \"" + room + "\": the script's people table: " + std::to_string(remapped) + " person number(s) and " + std::to_string(groups) + " group(s) set (group " + std::to_string(r.group) + ")");
        }
        FsysReplacement rep{scriptEntry, script.data(), static_cast<uint32_t>(script.size())};
        size_t size = 0;
        uint8_t* b = Fsys_Rebuild(archive.data(), archive.size(), &rep, 1, &size);
        if (b != nullptr) {
          std::vector<uint8_t> out(b, b + size);
          std::free(b);
          if (r.isNew && out.size() >= 12) PutBe32(out.data() + 8, r.group);
          if (made != nullptr) made->data = std::move(out);
          else job.outFiles.push_back({r.archive, std::move(out), modId});
        } else {
          say(em::Message::Level::Problem, what + ": the room's archive could not be rebuilt with the people table; the people are left out.");
          built.people.clear(), built.personNames.clear();
        }
      }

      const auto addRow = [&](const std::string& table, uint32_t id, uint32_t row, uint32_t copyOf, const std::string& key, std::vector<uint8_t> bytes) {
        em::AddedRow a;
        a.table = table, a.id = id, a.row = row, a.copyOf = copyOf, a.key = key, a.mod = modId, a.modName = modName;
        a.where = "objects/" + file.filename().string();
        a.bytes = std::move(bytes);
        rows.push_back(std::move(a));
      };
      const auto grow = [&](const char* table, const em::DataTable* t, const std::vector<std::vector<uint8_t>>& list, const std::vector<std::string>& names) {
        for (size_t i = 0; i < list.size(); i++) {
          const uint32_t row = (t != nullptr ? t->count : 0) + CountRows(rows, table);
          addRow(table, row, row, 0, room + "." + (i < names.size() ? names[i] : std::to_string(i)), list[i]);
        }
      };
      if (!built.people.empty() || spec.replacePeople) {
        addRow("floor_chars", xd::objects::kModPeopleBase, r.floorRow, spec.replacePeople ? 1 : 0, room + ".people", built.people);
      }
      grow("floor_tresure_list", chestTable, built.chests, built.chestNames);
      grow("floor_event_list", eventTable, built.events, built.eventNames);
      grow("floor_door_list", doorTable, built.doors, built.doorNames);
      std::vector<std::string> musicNames;
      for (size_t i = 0; i < built.music.size(); i++) musicNames.push_back("music" + std::to_string(i));
      grow("floorsoundTbl", soundTable, built.music, musicNames);
      for (const xd::objects::AreaZone& z : built.areas) {
        std::vector<uint8_t> b(16);
        xd::objects::PutBeF32(b.data(), z.x), xd::objects::PutBeF32(b.data() + 4, z.y);
        xd::objects::PutBeF32(b.data() + 8, z.z), xd::objects::PutBeF32(b.data() + 12, z.radius);
        addRow("floor_zone", r.floorId, CountRows(rows, "floor_zone"), 0, room + "." + z.name, std::move(b));
      }
      char line[320];
      std::snprintf(line, sizeof line,
                    "Objects of the room \"%s\" (floor %u, mod %s): %zu people, %zu chests, %zu event zones, %zu area zones, %zu door rows, %zu music rows",
                    room.c_str(), r.floorId, modId.c_str(), built.personNames.size(), built.chests.size(), built.events.size(),
                    built.areas.size(), built.doors.size(), built.music.size());
      say(em::Message::Level::Info, line);
    }
  }
}

}

namespace {

constexpr uint32_t kRoomTextFirstKey = 900000;
constexpr uint32_t kRoomTextLastKey = 0xFFFFF;

bool LikeBankBytes(em::RowsJob& job, uint8_t out[8]) {
  static const char* const kArchives[] = {"D1_labo_1F.fsys", "D1_garage_1F.fsys", "B1_1.fsys"};
  for (const char* name : kArchives) {
    for (const std::string& f : job.discFiles) {
      if (Lower(f) != Lower(name)) continue;
      std::vector<uint8_t> a;
      if (!job.readFile(f, a)) continue;
      FsysArchive fsys;
      if (!Fsys_Open(a.data(), a.size(), &fsys)) continue;
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e;
        if (!Fsys_Entry(&fsys, i, &e) || e.type != 5) continue;
        uint32_t size = 0;
        if (uint8_t* p = Fsys_Extract(&fsys, i, &size)) {
          const bool ok = size >= 8;
          if (ok) std::memcpy(out, p, 8);
          std::free(p);
          if (ok) return true;
        }
      }
    }
  }
  return false;
}

void BuildRoomTexts(em::RowsJob& job) {
  std::vector<em::AddedRow>& rows = *job.rows;
  const auto say = [&](em::Message::Level level, const std::string& text) { if (job.say) job.say(level, text); };
  std::string tocPath = "GSfsys.toc";
  for (const std::string& f : job.discFiles) {
    if (Lower(f) == "gsfsys.toc") tocPath = f;
  }
  const em::DataTable* floorTable = job.catalog != nullptr ? job.catalog->FindTable("floordata") : nullptr;
  const em::NameList* floorNames = job.catalog != nullptr ? job.catalog->FindList("floor") : nullptr;
  std::vector<uint8_t> floorBytes;
  if (floorTable != nullptr && !floorTable->places.empty() && !job.readEntry(floorTable->places[0].archive, floorTable->places[0].entry, floorBytes)) floorBytes.clear();
  for (const auto& [modId, folder] : job.modFolders) {
    std::error_code ec;
    const fs::path dir = folder / "texts";
    if (!fs::is_directory(dir, ec)) continue;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
      const std::string room = file.stem().string();
      const std::string what = "texts/" + file.filename().string() + " of the mod \"" + modId + "\"";
      if (floorTable == nullptr || !floorTable->addable) {
        say(em::Message::Level::Info, what + ": text for rooms is not used in this edition of the game.");
        continue;
      }
      std::vector<uint8_t> raw;
      if (!ReadFileBytes(file, raw)) { say(em::Message::Level::Problem, what + " could not be read; ignored."); continue; }
      em::json::Value doc;
      em::json::Reader reader(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
      if (!reader.Document(doc) || doc.kind != em::json::Value::Kind::Object) {
        say(em::Message::Level::Problem, what + " is not a JSON { ... } block (" + reader.Error() + "); ignored.");
        continue;
      }
      const auto tx = doc.fields.find("texts");
      if (tx == doc.fields.end() || tx->second.kind != em::json::Value::Kind::Object || tx->second.order.empty()) {
        say(em::Message::Level::Problem, what + " needs \"texts\": { \"900000\": \"words\", ... }; ignored.");
        continue;
      }
      std::vector<std::pair<uint32_t, std::string>> texts;
      bool bad = false;
      for (const std::string& k : tx->second.order) {
        const em::json::Value& v = tx->second.fields.at(k);
        char* end = nullptr;
        const unsigned long key = std::strtoul(k.c_str(), &end, 10);
        std::string why;
        if (end == k.c_str() || *end != '\0' || key < kRoomTextFirstKey || key > kRoomTextLastKey) {
          say(em::Message::Level::Problem, what + ": the text number \"" + k + "\" must be a whole number from 900000 up (the game's own banks use smaller ones).");
          bad = true;
        } else if (v.kind != em::json::Value::Kind::String) {
          say(em::Message::Level::Problem, what + ": text " + k + " needs its words in quotes.");
          bad = true;
        } else if (!CheckMessageText(v.text, why)) {
          say(em::Message::Level::Problem, what + ": text " + k + ": " + why + ".");
          bad = true;
        } else {
          texts.emplace_back(static_cast<uint32_t>(key), v.text);
        }
      }
      if (bad) continue;
      std::sort(texts.begin(), texts.end());

      std::vector<uint8_t> tocBytes;
      Toc toc;
      for (const em::RowsJob::OutFile& o : job.outFiles) {
        if (o.discPath == tocPath) tocBytes = o.data;
      }
      if (tocBytes.empty()) job.readFile(tocPath, tocBytes);
      if (!ReadToc(tocBytes, toc)) { say(em::Message::Level::Problem, what + ": the archive list could not be read; ignored."); continue; }
      uint32_t group = 0;
      bool found = false;
      for (const em::AddedRow& row : rows) {
        if (row.table == "floordata" && row.mod == modId && row.key == room && row.bytes.size() >= 0x30) {
          group = Be32(row.bytes.data() + 0x2C);
          found = true;
        }
      }
      if (!found && floorNames != nullptr && !floorBytes.empty()) {
        for (const auto& [name, number] : floorNames->names) {
          if (Lower(name) != Lower(room)) continue;
          const size_t at = floorTable->places[0].offset + static_cast<size_t>(number) * 64;
          if (at + 64 > floorBytes.size()) continue;
          group = Be32(floorBytes.data() + at + 0x2C);
          found = true;
          break;
        }
      }
      const std::string archiveName = found ? TocName(tocBytes, toc, group) : std::string();
      if (archiveName.empty()) {
        say(em::Message::Level::Problem, what + ": no room of that name (a room the mod adds with newroom, or a disc room's archive name, xd_data_mods <disc> --names floor); ignored.");
        continue;
      }
      std::string archivePath = archiveName + ".fsys";
      for (const std::string& f : job.discFiles) {
        if (Lower(f) == Lower(archivePath)) archivePath = f;
      }
      em::RowsJob::OutFile* made = nullptr;
      for (em::RowsJob::OutFile& o : job.outFiles) {
        if (Lower(o.discPath) == Lower(archivePath)) made = &o;
      }
      std::vector<uint8_t> archive;
      if (made != nullptr) archive = made->data;
      else if (!job.readFile(archivePath, archive)) { say(em::Message::Level::Problem, what + ": the archive " + archivePath + " could not be read; ignored."); continue; }
      FsysArchive fsys;
      if (!Fsys_Open(archive.data(), archive.size(), &fsys)) { say(em::Message::Level::Problem, what + ": " + archivePath + " is not an archive the port can read; ignored."); continue; }
      std::set<uint32_t> ids;
      bool hasBank = false;
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e;
        if (!Fsys_Entry(&fsys, i, &e)) continue;
        ids.insert(e.id);
        hasBank = hasBank || e.type == 5;
      }
      uint32_t id = 0x7E00;
      while (ids.count(id) != 0) id++;
      uint8_t like[8];
      std::vector<uint8_t> bank;
      std::string error;
      if (!LikeBankBytes(job, like) || !BuildMessageBank(0, texts, like, bank, error)) {
        say(em::Message::Level::Problem, what + ": the text bank could not be made (" + (error.empty() ? std::string("no room bank of the disc to take the language from") : error) + "); ignored.");
        continue;
      }
      size_t size = 0;
      uint8_t* built = Fsys_AddEntry(archive.data(), archive.size(), static_cast<uint16_t>(id), 5, "modtext", bank.data(), static_cast<uint32_t>(bank.size()), true, &size);
      if (built == nullptr) { say(em::Message::Level::Problem, what + ": the room's archive could not be given a text bank; ignored."); continue; }
      std::vector<uint8_t> out(built, built + size);
      std::free(built);
      if (made != nullptr) made->data = std::move(out);
      else job.outFiles.push_back({archivePath, std::move(out), modId});
      char line[256];
      std::snprintf(line, sizeof line, "Text of the room \"%s\" (mod %s): %zu message(s) in a new text bank in %s (%s)", room.c_str(), modId.c_str(), texts.size(),
                    archivePath.c_str(), hasBank ? "next to the room's own bank" : "the room had no bank");
      say(em::Message::Level::Info, line);
    }
  }
}

}

void BuildRooms(em::RowsJob& job) {
  BuildNewRooms(job);
  xd::faces::BuildFaces(job);
  xd::faces::BuildBoxIcons(job);
  xd::species::BuildSpecies(job);
  xd::sound::BuildSounds(job);
  BuildObjects(job);
  BuildRoomTexts(job);
}

}
