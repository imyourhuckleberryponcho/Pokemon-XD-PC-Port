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

  fs::remove_all(root, ec);
  std::printf(sOk ? "data_mods_test: all passed\n" : "data_mods_test: FAILED\n");
  return sOk ? 0 : 1;
}
