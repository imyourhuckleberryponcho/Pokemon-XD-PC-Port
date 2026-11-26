#include <data_mods.h>
#include <gs_formats.h>
#include <mods.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace engine::mods;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void write_file(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  if (v.size() < at + 4) v.resize(at + 4);
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}
size_t align32(size_t v) { return (v + 31) & ~size_t(31); }

struct FakeEntry {
  std::string name, fullName;
  std::vector<uint8_t> data;
  bool packed;
};
std::vector<uint8_t> make_fsys(const std::vector<FakeEntry>& entries) {
  std::vector<uint8_t> a(0x60, 0);
  std::memcpy(a.data(), "FSYS", 4);
  put32(a, 0x0C, uint32_t(entries.size()));
  put32(a, 0x18, 0x40);
  put32(a, 0x40, 0x60);
  const size_t headers = align32(0x60 + 4 * entries.size());
  a.resize(headers + 0x70 * entries.size(), 0);
  std::vector<std::pair<uint32_t, uint32_t>> nameAt;
  for (const FakeEntry& e : entries) {
    const uint32_t n = uint32_t(a.size());
    a.insert(a.end(), e.name.begin(), e.name.end());
    a.push_back(0);
    uint32_t f = 0;
    if (!e.fullName.empty()) {
      f = uint32_t(a.size());
      a.insert(a.end(), e.fullName.begin(), e.fullName.end());
      a.push_back(0);
    }
    nameAt.emplace_back(n, f);
  }
  a.resize(align32(a.size()), 0);
  put32(a, 0x1C, uint32_t(a.size()));
  for (size_t i = 0; i < entries.size(); i++) {
    const FakeEntry& e = entries[i];
    std::vector<uint8_t> stored = e.data;
    if (e.packed) {
      uint32_t n = 0;
      uint8_t* p = Lzss_Pack(stored.data(), uint32_t(stored.size()), &n);
      stored.assign(p, p + n);
      std::free(p);
    }
    const size_t at = align32(a.size());
    a.resize(at, 0);
    a.insert(a.end(), stored.begin(), stored.end());
    const size_t h = headers + 0x70 * i;
    put32(a, 0x60 + 4 * i, uint32_t(h));
    put32(a, h + 0x00, uint32_t(0x100 + i) << 16);
    put32(a, h + 0x04, uint32_t(at));
    put32(a, h + 0x08, uint32_t(e.data.size()));
    put32(a, h + 0x0C, e.packed ? 0x80000000u : 0);
    put32(a, h + 0x14, uint32_t(stored.size()));
    put32(a, h + 0x1C, nameAt[i].second);
    put32(a, h + 0x24, nameAt[i].first);
  }
  a.resize(align32(a.size()) + 0x20, 0);
  std::memcpy(a.data() + a.size() - 4, "FSYS", 4);
  put32(a, 0x20, uint32_t(a.size()));
  return a;
}

std::vector<uint8_t> extract(const std::vector<uint8_t>& archive, const char* fullName) {
  FsysArchive f;
  if (!Fsys_Open(archive.data(), archive.size(), &f)) return {};
  for (uint32_t i = 0; i < f.count; i++) {
    FsysEntry e;
    if (Fsys_Entry(&f, i, &e) && std::strcmp(e.fullName, fullName) == 0) {
      uint32_t n = 0;
      uint8_t* p = Fsys_Extract(&f, i, &n);
      std::vector<uint8_t> out(p, p + n);
      std::free(p);
      return out;
    }
  }
  return {};
}

constexpr uint32_t kHeader = 16, kEntry = 12, kCount = 4;
std::vector<uint8_t> make_table() {
  std::vector<uint8_t> t(kHeader + kEntry * kCount, 0);
  std::memcpy(t.data(), "TBL!", 4);
  t[7] = kCount;
  for (uint32_t i = 0; i < kCount; i++) {
    uint8_t* e = t.data() + kHeader + i * kEntry;
    e[1] = uint8_t(i);
    e[2] = uint8_t(10 + i);
    e[3] = uint8_t(0xA0 | i);
    e[7] = uint8_t(100 + i);
    e[8] = 1, e[9] = 2;
    e[10] = 0x12, e[11] = 0x34;
  }
  return t;
}

uint8_t* entry_of(std::vector<uint8_t>& t, uint32_t i) { return t.data() + kHeader + i * kEntry; }

DataCatalog make_catalog() {
  DataCatalog c;
  c.toolHint = "see the test";
  DataTable t;
  t.name = "things";
  t.aliases = {"stuff"};
  t.entrySize = kEntry;
  t.count = kCount;
  t.places = {{"data.fsys", "table.bin", kHeader}, {"copy.fsys", "table.bin", kHeader}};
  t.entryNames = "thing";
  DataField f;
  f = {}; f.name = "kind"; f.size = 2; f.offset = 0; f.values = "colour"; t.fields.push_back(f);
  f = {}; f.name = "level"; f.size = 1; f.offset = 2; t.fields.push_back(f);
  f = {}; f.name = "flags.a"; f.size = 1; f.offset = 3; f.bitShift = 5; f.bitWidth = 3; t.fields.push_back(f);
  f = {}; f.name = "flags.b"; f.size = 1; f.offset = 3; f.bitShift = 0; f.bitWidth = 5; t.fields.push_back(f);
  f = {}; f.name = "flags"; f.size = 1; f.offset = 3; t.fields.push_back(f);
  f = {}; f.name = "value"; f.size = 4; f.offset = 4; t.fields.push_back(f);
  f = {}; f.name = "pair"; f.size = 1; f.offset = 8; f.count = 2; f.values = "colour"; t.fields.push_back(f);
  f = {}; f.name = "link"; f.size = 2; f.offset = 10; f.editable = false; t.fields.push_back(f);
  c.tables.push_back(t);
  c.lists.push_back({"colour", {{"Red", 0}, {"Blue", 1}, {"Mr. Green", 2}, {"Dup", 3}, {"dup", 4}}});
  c.lists.push_back({"thing", {{"Alpha", 0}, {"Beta", 1}, {"Gamma", 2}, {"Delta", 3}}});
  return c;
}

struct FakeDisc {
  std::map<std::string, std::vector<uint8_t>> files;
  DiscView View() const {
    DiscView v;
    v.gameId = "TEST01";
    for (const auto& [path, data] : files) v.files.push_back(path);
    v.read = [this](const std::string& path, std::vector<uint8_t>& out) {
      const auto it = files.find(path);
      if (it == files.end()) return false;
      out = it->second;
      return true;
    };
    return v;
  }
};

EntryReader disc_reader(const FakeDisc& disc) {
  return [&disc](const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
    const auto it = disc.files.find(archive);
    if (it == disc.files.end()) return false;
    out = extract(it->second, entry.c_str());
    return !out.empty();
  };
}

bool has_message(const std::vector<Message>& m, Message::Level level, const std::string& part) {
  for (const Message& x : m) {
    if (x.level == level && x.text.find(part) != std::string::npos) return true;
  }
  return false;
}
bool has_message(const LoadResult& r, Message::Level level, const std::string& part) {
  return has_message(r.messages, level, part);
}

std::string manifest(const std::string& id, const std::string& extra = "") {
  return "{ \"id\": \"" + id + "\", \"name\": \"" + id + " mod\", \"version\": \"1.0\", \"game\": \"TEST01\"" +
         (extra.empty() ? "" : ", " + extra) + " }";
}

}

int main(int argc, char** argv) {
  const fs::path root = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path() / "xd_data_mods_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);

  FakeDisc disc;
  const std::vector<uint8_t> table = make_table();
  const std::vector<uint8_t> other = {'o', 't', 'h', 'e', 'r', ' ', 'e', 'n', 't', 'r', 'y'};
  disc.files["data.fsys"] = make_fsys({{"other", "other.bin", other, true}, {"table", "table.bin", table, true}});
  disc.files["copy.fsys"] = make_fsys({{"table", "table.bin", table, false}});
  const DataCatalog catalog = make_catalog();
  const EntryReader read = disc_reader(disc);

  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(catalog, {}, read, m);
    check(r.entries.empty() && r.changes.empty() && m.empty(), "no data files: nothing changed, nothing said");
  }
  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        catalog,
        {{"a", "A", "data/a.json",
          "{ \"_note\": \"comments are keys starting with _\",\n"
          "  \"things\": {\n"
          "    \"Alpha\": { \"level\": 50, \"kind\": \"blue\", \"pair\": [\"mr green\", null], \"flags.a\": 2,\n"
          "               \"value\": \"0x10000\", \"_why\": \"ignored\" },\n"
          "    \"#3\": { \"flags.b\": 31, \"pair[1]\": 7 }\n"
          "  }\n}"}},
        read, m);
    check(m.empty(), "a correct file: no messages");
    check(r.entries.size() == 2, "both copies of the table changed (data.fsys and copy.fsys)");
    std::vector<uint8_t> want = table;
    uint8_t* e0 = entry_of(want, 0);
    e0[2] = 50, e0[1] = 1, e0[8] = 2, e0[3] = uint8_t((2 << 5) | 0), e0[4] = 0, e0[5] = 1, e0[6] = 0, e0[7] = 0;
    uint8_t* e3 = entry_of(want, 3);
    e3[3] = uint8_t(0xA0 | 31), e3[9] = 7;
    const auto it = r.entries.find({"data.fsys", "table.bin"});
    check(it != r.entries.end() && it->second == want, "numbers, names (any case, signs ignored), lists with null, bits, \"0x\" text");
    const auto it2 = r.entries.find({"copy.fsys", "table.bin"});
    check(it2 != r.entries.end() && it2->second == want, "the second copy holds the same changes");
    bool sawLevel = false;
    for (const DataChange& c : r.changes) {
      if (c.field == "level" && c.entry == 0) {
        sawLevel = c.before == 10 && c.after == 50 && c.entryName == "Alpha" && c.mod == "a" && c.where == "data/a.json line 3";
      }
    }
    check(sawLevel && r.changes.size() == 7, "each change listed with before, after, entry name, mod and line");
  }
  {
    DataCatalog withKey = catalog;
    withKey.tables[0].editAssetKeys = {"pic"};
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        withKey,
        {{"a", "A", "data/a.json", "{ \"things\": { \"Alpha\": { \"pic\": \"textures/a.png\", \"level\": 51 }, \"Beta\": { \"pic\": \"\" }, \"#2\": { \"Pic\": \"..\\\\x.png\" } } }"},
         {"b", "B", "data/b.json", "{ \"things\": { \"Alpha\": { \"pic\": \"textures/b.png\" } } }"}},
        read, m);
    check(r.editedAssets.size() == 1 && r.editedAssets[0].entry == 0 && r.editedAssets[0].key == "pic" && r.editedAssets[0].text == "textures/b.png" &&
              r.editedAssets[0].mod == "b" && r.editedAssets[0].table == "things",
          "a picture key on a disc row is kept (the last file's text for the row)");
    check(has_message(m, Message::Level::Problem, "must be a text in quotes") && has_message(m, Message::Level::Problem, "must be a path inside the mod"),
          "an empty picture text and a path with \"..\" are told");
    const auto it = r.entries.find({"data.fsys", "table.bin"});
    check(it != r.entries.end() && it->second[kHeader + 2] == 51, "the ordinary field of the same row is still set");
    std::vector<Message> m2;
    const DataApplyResult r2 = ApplyDataMods(catalog, {{"a", "A", "data/a.json", "{ \"things\": { \"Alpha\": { \"pic\": \"textures/a.png\" } } }"}}, read, m2);
    check(r2.editedAssets.empty() && has_message(m2, Message::Level::Problem, "has no field \"pic\""), "a table that does not take the key: \"has no field\" as before");
  }
  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        catalog,
        {{"a", "A", "data/a.json", "{ \"stuff\": { \"Beta\": { \"level\": 20, \"flags.a\": 1 } } }"},
         {"b", "B", "data/b.json", "{ \"things\": { \"#1\": { \"level\": 30, \"flags\": 255 } } }"},
         {"b", "B", "data/c.json", "{ \"things\": { \"#1\": { \"level\": 31 } } }"}},
        read, m);
    const auto it = r.entries.find({"data.fsys", "table.bin"});
    check(it != r.entries.end() && it->second[kHeader + kEntry + 2] == 31 && it->second[kHeader + kEntry + 3] == 255,
          "later mods win (level 31, flags 255)");
    check(has_message(m, Message::Level::Warning, "Conflict: the mods \"A\" and \"B\" both change level of things #1 (Beta)"),
          "conflict reported for level");
    check(has_message(m, Message::Level::Warning, "both change flags of things #1"), "conflict reported where bits overlap (flags.a vs flags)");
    check(has_message(m, Message::Level::Warning, "sets level of things #1 (Beta) twice (data/b.json line 1 and data/c.json line 1)"),
          "one mod setting a number twice is reported");
  }
  {
    std::vector<Message> m;
    DataModFile g{"gen4", "Gen 4", "gen4/a.json", "{ \"things\": { \"#1\": { \"level\": 20 } } }"};
    DataModFile rz{"randomizer", "Randomizer", "data/randomizer.json", "{ \"things\": { \"#1\": { \"level\": 30 } } }"};
    g.builtIn = rz.builtIn = true;
    const DataApplyResult r = ApplyDataMods(catalog, {g, rz}, read, m);
    const auto it = r.entries.find({"data.fsys", "table.bin"});
    check(it != r.entries.end() && it->second[kHeader + kEntry + 2] == 30, "built-in layers: the later one wins");
    check(m.empty(), "built-in layers: no conflict message among them");
    std::vector<Message> m2;
    ApplyDataMods(catalog, {{"u", "User mod", "data/u.json", "{ \"things\": { \"#1\": { \"level\": 5 } } }"}, g, rz}, read, m2);
    check(has_message(m2, Message::Level::Warning, "Conflict: the mods \"User mod\" and \"Gen 4\" both change level of things #1"),
          "a user mod against a built-in layer is still reported");
  }
  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        catalog,
        {{"x", "X", "data/x.json",
          "{\n"
          "\"tables\": {},\n"
          "\"things\": {\n"
          "  \"Epsilon\": { \"level\": 1 },\n"
          "  \"#9\": { \"level\": 1 },\n"
          "  \"Alpha\": { \"levle\": 1, \"level\": 300, \"flags.a\": 8, \"link\": 0, \"kind\": \"dup\",\n"
          "             \"pair\": [1, 2, 3], \"value\": -1, \"kind\": \"Purple\", \"pair[2]\": 1 },\n"
          "  \"Gamma\": { \"level\": 99, \"value\": 1.5 }\n"
          "}}"},
         {"y", "Y", "data/broken.json", "{ \"things\": { \"Alpha\": { \"level\": 5 } } "}},
        read, m);
    check(has_message(m, Message::Level::Problem, "line 2: names the table \"tables\", which the game doesn't have (see the test)"),
          "unknown table (with the tool hint)");
    check(has_message(m, Message::Level::Problem, "line 4: no thing is called \"Epsilon\""), "unknown entry name");
    check(has_message(m, Message::Level::Problem, "things has entries #0 to #3, no #9"), "entry number out of range");
    check(has_message(m, Message::Level::Problem, "things has no field \"levle\""), "unknown field");
    check(has_message(m, Message::Level::Problem, "level must be a whole number from 0 to 255 (not 300)"), "value too big");
    check(has_message(m, Message::Level::Problem, "flags.a must be a whole number from 0 to 7 (not 8)"), "value too big for its bits");
    check(has_message(m, Message::Level::Problem, "link in things can't be changed by mods"), "an address can't be changed");
    check(has_message(m, Message::Level::Problem, "pair has 2 values, the list has 3"), "list too long");
    check(has_message(m, Message::Level::Problem, "there is no pair[2]"), "element out of range");
    check(has_message(m, Message::Level::Problem, "value must be a whole number from 0 to 4294967295 (not -1)"), "negative value");
    check(has_message(m, Message::Level::Problem, "value must be a whole number from 0 to 4294967295 (not 1.5)"), "fraction");
    check(has_message(m, Message::Level::Problem, "no colour is called \"Purple\""), "unknown value name");
    check(has_message(m, Message::Level::Problem, "data/broken.json: is not valid JSON"), "broken JSON: the file is skipped");
    const auto it = r.entries.find({"data.fsys", "table.bin"});
    check(it != r.entries.end() && it->second[kHeader + 2 * kEntry + 2] == 99 && it->second[kHeader + 2] == 10,
          "the usable changes still apply (Gamma's level), the bad ones don't (Alpha unchanged)");
  }
  {
    std::vector<Message> m;
    ApplyDataMods(catalog, {{"x", "X", "data/x.json", "{ \"things\": { \"Alpha\": { \"kind\": \"DUP\" } } }"}}, read, m);
    check(has_message(m, Message::Level::Problem, "several colour numbers are called \"DUP\" (#3, #4)"), "ambiguous name: asks for the number");
  }
  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        catalog, {{"s", "S", "data/s.json", "{ \"things\": { \"Alpha\": { \"level\": 10, \"kind\": \"Red\", \"flags.a\": 5 } } }"}},
        read, m);
    check(r.entries.empty() && r.changes.size() == 3 && m.empty(), "numbers set to what the disc has: no entry replaced");
  }

  {
    DataCatalog cat = catalog;
    cat.tables[0].addable = true;
    cat.tables[0].addMax = 3;
    cat.tables[0].addFirstId = 6;
    cat.tables[0].idList = "thing";
    cat.tables[0].textFields = {"value"};
    cat.addedTextGroup = 5;
    for (NameList& l : cat.lists) {
      if (l.name == "colour") l.limit = 5;
      if (l.name == "thing") l.limit = 4;
    }
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        cat,
        {{"a", "A", "data/a.json",
          "{ \"add\": { \"things\": {\n"
          "    \"Delta Two\": { \"copy\": \"Beta\", \"level\": 77, \"kind\": \"Blue\", \"text\": { \"value\": \"Delta the Second\" } },\n"
          "    \"Eps\": { \"copy\": \"Delta Two\", \"level\": 78, \"pair\": [\"Blue\", \"Red\"] },\n"
          "    \"Alpha\": { \"copy\": 1 },\n"
          "    \"Late\": { \"copy\": \"Eps\", \"level\": 79 },\n"
          "    \"Over\": { \"copy\": 0 }\n"
          "} } }"},
         {"b", "B", "data/b.json",
          "{ \"things\": { \"Eps\": { \"flags.b\": 7, \"kind\": 9 }, \"Alpha\": { \"kind\": \"Delta Two\" } } }"}},
        read, m);
    check(r.entries.empty(), "added rows change no archive entry");
    check(r.added.size() == 3 && r.added[0].key == "Delta Two" && r.added[0].id == 6 && r.added[0].row == 6 && r.added[1].id == 7 && r.added[2].id == 8,
          "rows get numbers after the table's end, never below addFirstId, in load order (6, 7, 8)");
    check(r.added.size() == 3 && r.added[0].copyOf == 1 && r.added[1].copyOf == 1, "a copy of an added row remembers the disc row it began as");
    check(r.added.size() == 3 && r.added[0].bytes[2] == 77 && r.added[1].bytes[2] == 78 && r.added[2].bytes[2] == 79,
          "each row has its own fields set (levels 77, 78, 79)");
    check(r.added.size() == 3 && r.added[1].bytes[1] == 1 && r.added[1].bytes[8] == 1 && r.added[1].bytes[9] == 0,
          "a row copying an added row takes its bytes (kind 1) and then its own fields (pair Blue, Red)");
    check(r.addedTexts.size() == 1 && r.addedTexts[0].key == (5u << 20 | 1) && r.addedTexts[0].text == "Delta the Second" &&
              r.added.size() == 3 && r.added[0].bytes[4] == 0 && r.added[0].bytes[5] == 0x50 && r.added[0].bytes[6] == 0 && r.added[0].bytes[7] == 1,
          "a row's text gets a message number in the added group, written into the row's text field");
    check(has_message(m, Message::Level::Problem, "the new row \"Alpha\" has the name of thing #0"), "a new row can't have a name the game already has");
    check(has_message(m, Message::Level::Problem, "mods can add at most 3 rows to things; \"Over\" is left out"), "too many rows refused");
    check(has_message(m, Message::Level::Problem, "kind is colour #9, but the game's colour numbers end at #4"),
          "a number past the table's end that no mod added is refused (the game dropped such a Pokemon silently)");
    check(!has_message(m, Message::Level::Problem, "colour #3") && !has_message(m, Message::Level::Problem, "colour #1"), "numbers inside the table are fine");
    check(has_message(m, Message::Level::Problem, "no colour is called \"Delta Two\""), "an added name only joins the list of its own table");
    check(r.added.size() == 3 && (r.added[1].bytes[3] & 0x1F) == 7, "a data file can edit a row an earlier file added, by its name");
  }
  {
    DataCatalog cat = catalog;
    cat.tables[0].addable = true;
    cat.tables[0].addMax = 16;
    cat.tables[0].addFirstId = 6;
    cat.tables[0].idList = "thing";
    for (NameList& l : cat.lists) {
      if (l.name == "thing") l.limit = 4;
    }
    std::vector<Message> m;
    DataModFile a{"a", "A", "data/a.json",
                  "{ \"add\": { \"things\": {\n"
                  "    \"Delta Two\": { \"copy\": \"thing:base/beta\", \"level\": 77 },\n"
                  "    \"Eps\": { \"copy\": \"thing:ada.pack/delta-two\", \"label\": \"epsilon\" },\n"
                  "    \"Zeta\": { \"copy\": \"thing:ada.pack/epsilon\", \"label\": \"thing:ada.pack/zed\", \"kind\": \"Blue\" },\n"
                  "    \"Bad\": { \"copy\": 1, \"label\": \"Bad Label!\" },\n"
                  "    \"Wrong\": { \"copy\": 1, \"label\": \"thing:other.pack/w\" },\n"
                  "    \"Nope\": { \"copy\": \"thing:base/nothing\" },\n"
                  "    \"Epsilon Too\": { \"copy\": 1, \"label\": \"epsilon\" }\n"
                  "} } }"};
    a.labelNamespace = "ada.pack";
    DataModFile b{"bmod", "B", "data/b.json", "{ \"add\": { \"things\": { \"Plain One\": { \"copy\": 2 } } } }"};
    DataModFile g{"gen4", "G", "data/g.json", "{ \"add\": { \"things\": { \"Ice Stone\": { \"copy\": 2 } } } }", true};
    const DataApplyResult r = ApplyDataMods(cat, {a, b, g}, read, m);
    const auto labelOf = [&](const char* key) {
      for (const AddedRow& row : r.added) {
        if (row.key == key) return row.label;
      }
      return std::string("(none)");
    };
    check(labelOf("Delta Two") == "thing:ada.pack/delta-two", "a new row's label: the mod's namespace + a slug of its name");
    check(labelOf("Eps") == "thing:ada.pack/epsilon", "\"label\" gives the name part");
    check(labelOf("Zeta") == "thing:ada.pack/zed", "\"label\" may be the whole label when its namespace is the mod's");
    check(labelOf("Plain One") == "thing:local.bmod/plain-one", "no namespace in mod.json: local.<mod id>");
    check(labelOf("Ice Stone") == "thing:base/ice-stone", "a built-in layer's rows (the Gen 4 pack) are base/ labels");
    check(labelOf("Bad").empty() && labelOf("Wrong").empty(), "a bad \"label\" is refused; the row keeps its number, with no label");
    check(has_message(m, Message::Level::Problem, "\"label\" of the new row \"Bad\"") && has_message(m, Message::Level::Problem, "must start with thing:ada.pack/"),
          "the label mistakes are said");
    check(labelOf("Epsilon Too").empty() && has_message(m, Message::Level::Problem, "has the label thing:ada.pack/epsilon"), "two rows with one label: the second has none, it is said");
    check(has_message(m, Message::Level::Problem, "no row has the label \"thing:base/nothing\" on this disc"), "an unknown base label is refused");
    check(r.added.size() == 9 && r.added[0].copyOf == 1 && r.added[1].copyOf == 1 && r.added[2].copyOf == 1,
          "copy by label (base/ and a mod's own) finds the rows");
    check(AddedRowsJson(r.added).find("\"label\": \"thing:ada.pack/delta-two\"") != std::string::npos, "the mod -> numbers map has the labels");
  }
  {
    DataCatalog cat = catalog;
    DataTable& t = cat.tables[0];
    t.addable = true;
    t.addMax = 4;
    t.addFirstId = 6;
    t.idList = "thing";
    t.textFields = {"value", "link"};
    t.primaryText = "value";
    cat.addedTextGroup = 7;
    DataField f;
    f.name = "mark";
    f.size = 1;
    f.offset = 2;
    f.values = "markname";
    t.fields.push_back(f);
    cat.lists.push_back({"markname", {{"physical", 0x81}, {"special", 0x82}}, 0});
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        cat,
        {{"a", "A", "data/a.json",
          "{ \"add\": { \"things\": {\n"
          "    \"Spec\": { \"copy\": \"Beta\", \"mark\": \"special\" },\n"
          "    \"Phys\": { \"copy\": \"Beta\", \"mark\": \"physical\" },\n"
          "    \"Plain\": { \"copy\": \"Beta\" },\n"
          "    \"Bad\": { \"copy\": \"Beta\", \"mark\": \"both\" }\n"
          "} } }"}},
        read, m);
    check(r.added.size() == 4 && r.added[0].bytes[2] == 0x82 && r.added[1].bytes[2] == 0x81, "a value name on a second field over the same byte: special = 0x82, physical = 0x81");
    check(r.added.size() == 4 && r.added[2].bytes[2] != 0x81 && r.added[2].bytes[2] != 0x82 && r.added[2].copyOf == 1, "a row with no mark keeps its source's byte");
    check(has_message(m, Message::Level::Problem, "no markname is called \"both\""), "an unknown category name is refused");
  }
  {
    DataCatalog cat = catalog;
    cat.tables[0].addable = true;
    cat.tables[0].addMax = 5;
    cat.tables[0].addStart.assign(cat.tables[0].entrySize, 0);
    cat.tables[0].addStart[2] = 0xC1;
    cat.tables[0].addKeys = {{"bits", 2, 0x3F, 0, 1, 16}};
    cat.tables[0].addFirstId = 10;
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        cat,
        {{"a", "A", "data/a.json", "{ \"add\": { \"things\": { \"started\": {}, \"count\": { \"bits\": 8 }, \"huge\": { \"bits\": 99 }, \"odd\": { \"colour\": 1 } } } }"},
         {"b", "B", "data/b.json", "{ \"add\": { \"things\": { \"started\": { \"bits\": 2 } } } }"}},
        read, m);
    check(r.added.size() == 5 && r.added[0].key == "started" && r.added[0].id == 10 && r.added[1].id == 11 && r.added[4].mod == "b" && r.added[4].id == 14,
          "rows with start bytes: no \"copy\" needed, numbers from addFirstId in load order, a name may repeat in another mod");
    check(r.added.size() == 5 && r.added[0].bytes[2] == 0xC1 && r.added[1].bytes[2] == 0xC8 && r.added[4].bytes[2] == 0xC2,
          "the key's number is put into the start bytes (bits 1 -> 0xC1, 8 -> 0xC8, 2 -> 0xC2)");
    check(has_message(m, Message::Level::Problem, "\"bits\" must be a number from 1 to 16") &&
              has_message(m, Message::Level::Problem, "has no setting \"colour\"; its settings are: bits"),
          "a value out of range, or a setting the table does not have: refused with the reason");
    check(!has_message(m, Message::Level::Problem, "needs \"copy\""), "no complaint about a missing copy");
  }
  {
    DataCatalog cat = catalog;
    DataTable& t = cat.tables[0];
    t.addable = true;
    t.addMax = 5;
    t.addKeyOffset = 0;
    t.addKeySize = 1;
    std::vector<uint8_t> bytes;
    read(t.places[0].archive, t.places[0].entry, bytes);
    uint32_t top = 0;
    for (uint32_t i = 0; i < t.count; i++) top = std::max<uint32_t>(top, bytes[t.places[0].offset + i * t.entrySize]);
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        cat, {{"a", "A", "data/a.json", "{ \"add\": { \"things\": { \"One\": { \"copy\": 0 }, \"Two\": { \"copy\": 1 } } } }"}}, read, m);
    check(r.added.size() == 2 && r.added[0].id == top + 1 && r.added[1].id == top + 2 && r.added[0].row == t.count && r.added[1].row == t.count + 1,
          "rows with a key: the number is the next free key (above every row's), the row the next place");
    check(r.added.size() == 2 && r.added[0].bytes[0] == top + 1 && r.added[1].bytes[0] == top + 2 && r.added[0].copyOf == 0 && r.added[1].copyOf == 1,
          "the key is written into the new row (over the copy's own), the rest is the copy's");
  }
  {
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        catalog, {{"a", "A", "data/a.json", "{ \"add\": { \"things\": { \"X\": { \"copy\": 0 } }, \"nothing\": {} } }"}}, read, m);
    check(r.added.empty() && has_message(m, Message::Level::Problem, "which can't get new rows") &&
              has_message(m, Message::Level::Problem, "adds rows to the table \"nothing\", which the game doesn't have"),
          "adding to a table that can't grow, or doesn't exist: refused");
    const auto digests = AddedRowsDigests({AddedRow{"things", 6, 6, 1, "X", "m", "M", "", {}}, AddedRow{"things", 7, 7, 1, "Y", "m", "M", "", {}}});
    const auto other = AddedRowsDigests({AddedRow{"things", 7, 7, 1, "X", "m", "M", "", {}}, AddedRow{"things", 6, 6, 1, "Y", "m", "M", "", {}}});
    check(digests.size() == 1 && other.size() == 1 && digests.begin()->second != other.begin()->second,
          "the mod's digest changes when its rows' numbers change (another load order)");
    check(AddedRowsJson({AddedRow{"things", 6, 6, 1, "X \"q\"", "m", "M", "", {}}}).find("\"name\": \"X \\\"q\\\"\"") != std::string::npos,
          "the mod -> numbers map is JSON with the names escaped");
    AddedRow song{"things", 8, 8, 1, "song", "songmod", "S", "", {}};
    song.unbound = true;
    const auto onlySong = AddedRowsDigests({song});
    const auto mixed = AddedRowsDigests({AddedRow{"things", 6, 6, 1, "X", "m", "M", "", {}}, song});
    check(onlySong.empty() && mixed.size() == 1 && mixed.begin()->second == AddedRowsDigests({AddedRow{"things", 6, 6, 1, "X", "m", "M", "", {}}}).begin()->second,
          "rows marked unbound (a new song) are not in the playthrough's digest: a mod with only such rows has none");
  }

  const fs::path mods = root / "mods", list = root / "mods.ini";
  int built = 0;
  LoadOptions opt{mods, list, disc.View()};
  opt.dataCatalog = [&](const EntryReader& r, DataCatalog& out, std::string& error) {
    built++;
    std::vector<uint8_t> t;
    if (!r("data.fsys", "table.bin", t) || t.size() < 4 || std::memcmp(t.data(), "TBL!", 4) != 0) {
      error = "no table";
      return false;
    }
    out = make_catalog();
    return true;
  };
  write_file(mods / "files-only" / "mod.json", manifest("files-only"));
  write_file(mods / "files-only" / "files" / "readme.txt", "new file");
  write_file(list, "[profile default]\nfiles-only = on\n");
  {
    const LoadResult r = Load(opt);
    check(built == 0 && r.dataChanges.empty() && r.catalog == nullptr, "no data mod on: the tables aren't even looked for");
  }
  {
    LoadOptions g = opt;
    bool sawTable = false;
    g.generateData = [&](const DataCatalog& c, const EntryReader& r, std::vector<DataModFile>& out, std::string&) {
      std::vector<uint8_t> t;
      sawTable = c.FindTable("things") != nullptr && r("data.fsys", "table.bin", t);
      out.push_back({"randomizer", "Randomizer", "data/randomizer.json", "{ \"things\": { \"Alpha\": { \"level\": 55 } } }"});
      return true;
    };
    const LoadResult r = Load(g);
    check(built == 1 && sawTable && r.dataChanges.size() == 1 && r.dataChanges[0].after == 55 &&
              r.dataChanges[0].mod == "randomizer",
          "generated data: the tables built for it, its change applied though no data mod is on");
    g.generateData = [&](const DataCatalog&, const EntryReader&, std::vector<DataModFile>& out, std::string& error) {
      out.push_back({"randomizer", "Randomizer", "data/randomizer.json", "{}"});
      error = "made up";
      return false;
    };
    const LoadResult bad = Load(g);
    check(bad.dataChanges.empty() && has_message(bad, Message::Level::Problem, "could not be made: made up"),
          "generated data that fails: nothing of it used, a problem said");
    built = 0;
  }
  {
    LoadOptions g = opt;
    const std::vector<uint8_t> program = {'P', 'R', 'O', 'G', 0, 1, 0, 2, 0, 3, 0, 4};
    g.disc.readProgram = [&](std::vector<uint8_t>& out) {
      out = program;
      return true;
    };
    g.dataCatalog = [&](const EntryReader& r, DataCatalog& out, std::string&) {
      std::vector<uint8_t> p;
      out = DataCatalog{};
      DataTable t;
      t.name = "program_list";
      t.entrySize = 2;
      t.count = 4;
      t.places.push_back({kProgramArchive, "main.dol", 4});
      DataField f;
      f.name = "value";
      f.size = 2;
      t.fields.push_back(f);
      if (r(kProgramArchive, "main.dol", p) && p == program) out.tables.push_back(t);
      return true;
    };
    g.generateData = [&](const DataCatalog&, const EntryReader&, std::vector<DataModFile>& out, std::string&) {
      out.push_back({"randomizer", "Randomizer", "data/randomizer.json",
                     "{ \"program_list\": { \"#2\": { \"value\": 513 }, \"#3\": { \"value\": 70000 } } }"});
      return true;
    };
    const LoadResult r = Load(g);
    std::vector<uint8_t> want = program;
    want[8] = 2;
    want[9] = 1;
    bool inOverlay = false;
    for (const Replacement& f : r.files) inOverlay |= f.discPath.find("main.dol") != std::string::npos || f.discPath.find('<') != std::string::npos;
    check(r.program == want && r.dataChanges.size() == 1 && r.dataChanges[0].before == 3 && !inOverlay,
          "a table in the program file: changed bytes in LoadResult::program, not in the overlay");
    check(has_message(r, Message::Level::Problem, "value must be a whole number from 0 to 65535 (not 70000)"),
          "the program's tables are checked like any other");
    LoadOptions none = g;
    none.disc.readProgram = nullptr;
    const LoadResult n = Load(none);
    check(n.program.empty() && n.dataChanges.empty(), "no program reader: its tables can't be found or changed");
    built = 0;
  }
  std::vector<uint8_t> modTable = table;
  entry_of(modTable, 1)[2] = 77;
  write_file(mods / "table-file" / "mod.json", manifest("table-file"));
  write_file(mods / "table-file" / "fsys" / "data.fsys" / "table.bin", std::string(modTable.begin(), modTable.end()));
  write_file(mods / "stats" / "mod.json", manifest("stats", "\"requires\": \"table-file >= 1.0\", \"after\": [\"table-file\"]"));
  write_file(mods / "stats" / "data" / "things.json", "{ \"things\": { \"Alpha\": { \"level\": 42 } } }");
  write_file(mods / "stats" / "data" / "notes.txt", "not data");
  write_file(list, "[profile default]\ntable-file = on\nstats = on\n");
  {
    const LoadResult r = Load(opt);
    check(built == 1 && r.catalog != nullptr && r.dataChanges.size() == 1, "a data mod on: tables found once, one change");
    check(has_message(r, Message::Level::Warning, "only .json files in \"data\" are read"), "other files in data/ are pointed out");
    const Replacement* data = nullptr;
    const Replacement* copy = nullptr;
    for (const Replacement& x : r.files) {
      data = x.discPath == "data.fsys" ? &x : data;
      copy = x.discPath == "copy.fsys" ? &x : copy;
    }
    std::vector<uint8_t> want = modTable;
    entry_of(want, 0)[2] = 42;
    check(data != nullptr && data->rebuilt && data->generated.size() == 1 && data->entries.empty() &&
              extract(data->data, "table.bin") == want && extract(data->data, "other.bin") == other,
          "data.fsys: the data change on top of the file mod's table.bin; other entries untouched");
    std::vector<uint8_t> wantCopy = table;
    entry_of(wantCopy, 0)[2] = 42;
    check(copy != nullptr && copy->rebuilt && extract(copy->data, "table.bin") == wantCopy,
          "copy.fsys: the disc's table with the data change");
    check(r.dataChanges[0].before == 10 && r.dataChanges[0].after == 42, "before = the value under the data mod");
    check(!r.HasWarnings() || !has_message(r, Message::Level::Problem, ""), "no problems");
  }
  {
    const fs::path mods3 = root / "rooms" / "mods", list3 = root / "rooms" / "mods.ini";
    write_file(mods3 / "rooms-mod" / "mod.json", manifest("rooms-mod"));
    write_file(mods3 / "rooms-mod" / "data" / "things.json", "{ \"add\": { \"things\": { \"Added\": { \"copy\": 0 } } } }");
    write_file(list3, "[profile default]\nrooms-mod = on\n");
    LoadOptions g{mods3, list3, disc.View()};
    g.dataCatalog = [&](const EntryReader&, DataCatalog& out, std::string&) {
      out = make_catalog();
      out.tables[0].addable = true;
      out.tables[0].addMax = 4;
      return true;
    };
    int called = 0;
    bool sawFolder = false, readOk = false;
    g.finishRows = [&](RowsJob& job) {
      called++;
      sawFolder = job.modFolders.count("rooms-mod") == 1 && job.catalog != nullptr && job.rows->size() == 1;
      std::vector<uint8_t> bytes;
      readOk = job.readFile("data.fsys", bytes) && !bytes.empty();
      job.rows->push_back(AddedRow{"things", 99, 99, 0, "extra", "rooms-mod", "Rooms", "", std::vector<uint8_t>(4, 1)});
      job.outFiles.push_back({"newroom.fsys", {1, 2, 3, 4}, "rooms-mod"});
      job.outFiles.push_back({"data.fsys", {9, 9}, "rooms-mod"});
    };
    const LoadResult r = Load(g);
    const Replacement* added = nullptr;
    const Replacement* replaced = nullptr;
    for (const Replacement& x : r.files) {
      added = x.discPath == "newroom.fsys" ? &x : added;
      replaced = x.discPath == "data.fsys" ? &x : replaced;
    }
    check(called == 1 && sawFolder && readOk, "finishRows: called once with the rows, the mods' folders and a reader of the files as the mods leave them");
    check(r.addedRows.size() == 2 && r.addedRows[1].key == "extra", "finishRows: a row it adds is in LoadResult::addedRows");
    check(added != nullptr && added->added && added->rebuilt && added->data == std::vector<uint8_t>({1, 2, 3, 4}),
          "finishRows: a new whole file goes into the overlay as a file the disc doesn't have");
    check(replaced != nullptr && !replaced->added && replaced->rebuilt && replaced->data == std::vector<uint8_t>({9, 9}),
          "finishRows: a file of the disc is replaced whole (the archive list)");
    LoadOptions none = g;
    none.finishRows = nullptr;
    called = 0;
    const LoadResult n = Load(none);
    check(called == 0 && n.addedRows.size() == 1, "no finishRows: the rows stay as the data mods made them");
  }
  {
    const fs::path mods5 = root / "edited" / "mods", list5 = root / "edited" / "mods.ini";
    write_file(mods5 / "pic-mod" / "mod.json", manifest("pic-mod"));
    write_file(mods5 / "pic-mod" / "data" / "things.json", "{ \"things\": { \"Alpha\": { \"pic\": \"textures/a.png\", \"level\": 51 } } }");
    write_file(list5, "[profile default]\npic-mod = on\n");
    LoadOptions g{mods5, list5, disc.View()};
    g.dataCatalog = [&](const EntryReader&, DataCatalog& out, std::string&) {
      out = make_catalog();
      out.tables[0].editAssetKeys = {"pic"};
      return true;
    };
    int called = 0;
    bool sawAsset = false;
    g.finishRows = [&](RowsJob& job) {
      called++;
      sawAsset = job.editedAssets.size() == 1 && job.editedAssets[0].entry == 0 && job.editedAssets[0].key == "pic" && job.editedAssets[0].text == "textures/a.png";
      std::vector<uint8_t> bytes;
      if (!job.readEntry("data.fsys", "table.bin", bytes)) return;
      RowsJob::EditedRow e;
      e.table = "things";
      e.entry = 0;
      e.bytes.assign(bytes.begin() + kHeader, bytes.begin() + kHeader + kEntry);
      e.bytes[11] = 0x77;
      e.mods = "pic-mod";
      job.editedRows.push_back(std::move(e));
    };
    const LoadResult r = Load(g);
    const Replacement* data = nullptr;
    const Replacement* copy = nullptr;
    for (const Replacement& x : r.files) {
      data = x.discPath == "data.fsys" ? &x : data;
      copy = x.discPath == "copy.fsys" ? &x : copy;
    }
    check(called == 1 && sawAsset && r.addedRows.empty(), "edited assets: finishRows is called (no added row) with the picture key of the disc row");
    std::vector<uint8_t> want = table;
    entry_of(want, 0)[2] = 51;
    entry_of(want, 0)[11] = 0x77;
    check(data != nullptr && extract(data->data, "table.bin") == want && copy != nullptr && extract(copy->data, "table.bin") == want,
          "edited assets: the new row bytes are in both copies of the table, on top of the mod's own data change");
  }
  {
    const fs::path mods4 = root / "objects" / "mods", list4 = root / "objects" / "mods.ini";
    write_file(mods4 / "objects-mod" / "mod.json", manifest("objects-mod"));
    write_file(mods4 / "objects-mod" / "objects" / "room.json", "{}");
    write_file(list4, "[profile default]\nobjects-mod = on\n");
    LoadOptions g{mods4, list4, disc.View()};
    g.dataCatalog = [&](const EntryReader&, DataCatalog& out, std::string&) {
      out = make_catalog();
      return true;
    };
    int called = 0;
    bool sawCatalog = false;
    g.finishRows = [&](RowsJob& job) {
      called++;
      sawCatalog = job.catalog != nullptr && job.modFolders.count("objects-mod") == 1 && job.rows->empty();
    };
    Load(g);
    check(called == 1 && sawCatalog, "objects folder: no data file, yet the tables are read and finishRows is called with the catalog");
    write_file(mods4 / "plain" / "mod.json", manifest("plain"));
    write_file(list4, "[profile default]\nplain = on\n");
    called = 0;
    Load(g);
    check(called == 0, "no objects folder and no data file: finishRows is not called");
  }
  write_file(list, "[profile default]\nstats = on\ntable-file = on\n");
  {
    const LoadResult r = Load(opt);
    check(has_message(r, Message::Level::Warning, "\"stats mod\" should come after \"table-file mod\""), "\"after\": wrong order reported");
  }
  write_file(mods / "stats" / "mod.json", manifest("stats", "\"requires\": \"table-file >= 1.10\""));
  write_file(list, "[profile default]\ntable-file = on\nstats = on\n");
  {
    const LoadResult r = Load(opt);
    check(has_message(r, Message::Level::Problem, "needs the mod \"table-file mod\" in version >= 1.10, but the one in the mods folder is version 1.0"),
          "version condition not met: the mod isn't used");
    check(r.active.size() == 1 && r.dataChanges.empty(), "... and its data changes aren't made");
  }
  write_file(mods / "stats" / "mod.json", manifest("stats"));
  {
    LoadOptions noCatalog{mods, list, disc.View()};
    const LoadResult r = Load(noCatalog);
    check(has_message(r, Message::Level::Problem, "not supported for this game"), "data mods without a catalog: refused with a message");
  }

  {
    Manifest m;
    std::string error;
    check(ParseManifest("{\"id\":\"x\",\"game\":\"G\",\"requires\":[\"a\",\"b>=2.1\",\"c < 3\"],\"after\":\"a\"}", m, error) &&
              m.required == std::vector<std::string>{"a", "b", "c"} && m.requiredVersions.size() == 3 &&
              m.requiredVersions[0].op.empty() && m.requiredVersions[1].op == ">=" && m.requiredVersions[1].version == "2.1" &&
              m.requiredVersions[2].op == "<" && m.after == std::vector<std::string>{"a"},
          "mod.json: requires with version conditions, after");
    check(!ParseManifest("{\"id\":\"x\",\"game\":\"G\",\"requires\":\"b >= \"}", m, error), "mod.json: a condition without a version is refused");
    check(CompareVersions("1.10", "1.9") > 0 && CompareVersions("2.0", "2.0.0") == 0 && CompareVersions("1.0", "1.0.1") < 0,
          "versions compare number by number");
    VersionNeed need{">=", "1.2"};
    check(need.SatisfiedBy("1.2") && need.SatisfiedBy("1.12") && !need.SatisfiedBy("1.1.9"), "version conditions");
  }

  {
    const auto be = [](std::vector<uint8_t>& v, size_t at, uint32_t x) { put32(v, at, x); };
    std::vector<uint8_t> deck(0x10, 0);
    std::memcpy(deck.data(), "DECK", 4);
    const auto chunk = [&](const char* id, uint32_t rows, uint32_t rowSize, uint8_t fill) {
      const size_t at = deck.size();
      deck.resize(at + 0x10 + rows * rowSize, fill);
      std::memcpy(deck.data() + at, id, 4);
      be(deck, at + 4, 0x10 + rows * rowSize);
      be(deck, at + 8, rows);
      for (uint32_t r = 0; r < rows; r++) deck[at + 0x10 + r * rowSize] = uint8_t(r);
    };
    chunk("TRNR", 3, 8, 0x11);
    chunk("PKMN", 2, 4, 0x22);
    chunk("STRS", 0, 1, 0);
    deck.insert(deck.end(), {'h', 'i', 0, 0});
    be(deck, 4, uint32_t(deck.size()));
    const size_t trainerAt = 0x10 + 0x10, pokemonAt = 0x10 + 0x10 + 3 * 8 + 0x10;
    FakeDisc deckDisc;
    deckDisc.files["deck.fsys"] = make_fsys({{"deck", "deck.bin", deck, true}});
    DataCatalog cat;
    DataTable trainers;
    trainers.name = "trainers", trainers.entrySize = 8, trainers.count = 3, trainers.places = {{"deck.fsys", "deck.bin", uint32_t(trainerAt)}};
    trainers.addable = true, trainers.addInChunk = true, trainers.addMax = 4;
    DataField f;
    f.name = "team", f.size = 1, f.offset = 1, f.values = "pk";
    trainers.fields.push_back(f);
    f = {};
    f.name = "level", f.size = 1, f.offset = 2;
    trainers.fields.push_back(f);
    DataTable pokemon;
    pokemon.name = "pokemon_rows", pokemon.entrySize = 4, pokemon.count = 2, pokemon.places = {{"deck.fsys", "deck.bin", uint32_t(pokemonAt)}};
    pokemon.addable = true, pokemon.addInChunk = true, pokemon.addMax = 4, pokemon.entryNames = "pk";
    f = {};
    f.name = "species", f.size = 1, f.offset = 1;
    pokemon.fields.push_back(f);
    cat.tables = {trainers, pokemon};
    cat.lists.push_back(NameList{"pk", {}, 0});
    std::vector<Message> m;
    const DataApplyResult r = ApplyDataMods(
        cat,
        {{"a", "A", "data/a.json",
          "{ \"add\": { \"pokemon_rows\": { \"mine\": { \"copy\": 1, \"species\": 7 }, \"mine2\": { \"copy\": \"mine\", \"species\": 8 } },\n"
          "           \"trainers\": { \"rival\": { \"copy\": 2, \"team\": \"mine2\", \"level\": 9 } } } }"}},
        disc_reader(deckDisc), m);
    check(m.empty(), "chunk rows: a correct file says nothing");
    const auto it = r.entries.find({"deck.fsys", "deck.bin"});
    check(it != r.entries.end(), "chunk rows: the deck file is rebuilt");
    if (it != r.entries.end()) {
      const std::vector<uint8_t>& d = it->second;
      const auto get = [&](size_t at) { return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3]; };
      check(d.size() == deck.size() + 8 + 8, "chunk rows: the file grew by one trainer row (8) and two Pokemon rows (8)");
      check(get(4) == d.size(), "chunk rows: the file's own size field follows");
      check(get(0x10 + 8) == 4 && get(0x10 + 4) == 0x10 + 4 * 8, "chunk rows: the trainer chunk's count and size grew");
      const size_t pk = 0x10 + 0x10 + 4 * 8;
      check(std::memcmp(&d[pk], "PKMN", 4) == 0 && get(pk + 8) == 4 && get(pk + 4) == 0x10 + 4 * 4, "chunk rows: the next chunk moved and grew");
      check(d[0x20 + 3 * 8 + 1] == 3 && d[0x20 + 3 * 8 + 2] == 9, "chunk rows: the new trainer row has its team (the row number of mine2: 3) and level");
      check(d[pk + 0x10 + 2 * 4] == 1 && d[pk + 0x10 + 2 * 4 + 1] == 7 && d[pk + 0x10 + 3 * 4 + 1] == 8, "chunk rows: the new Pokemon rows are in, in order, with their fields");
      check(std::memcmp(&d[d.size() - 4], "hi\0\0", 4) == 0, "chunk rows: the chunk after them (and the text) moved with the rest");
    }
    check(r.added.size() == 3 && r.added[0].inFile && r.added[1].inFile && r.added[2].inFile, "chunk rows: the rows are listed, marked as already in the file");
  }

  fs::remove_all(root, ec);
  std::printf(sOk ? "data_mods_test: all passed\n" : "data_mods_test: FAILED\n");
  return sOk ? 0 : 1;
}
