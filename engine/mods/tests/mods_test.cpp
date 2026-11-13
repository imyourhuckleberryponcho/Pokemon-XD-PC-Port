#include <gs_formats.h>
#include <mods.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  if (v.size() < at + 4) v.resize(at + 4);
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}
uint32_t get32(const std::vector<uint8_t>& v, size_t at) {
  return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3];
}
size_t align32(size_t v) { return (v + 31) & ~size_t(31); }

struct FakeEntry {
  std::string name, fullName, data;
  bool packed;
};
std::vector<uint8_t> make_fsys(const std::vector<FakeEntry>& entries) {
  std::vector<uint8_t> a(0x60, 0);
  std::memcpy(a.data(), "FSYS", 4);
  put32(a, 0x0C, uint32_t(entries.size()));
  put32(a, 0x18, 0x40);
  put32(a, 0x40, 0x60);
  const size_t headers = align32(0x60 + 4 * entries.size());
  size_t names = headers + 0x70 * entries.size();
  a.resize(names, 0);
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
    std::vector<uint8_t> stored = bytes(e.data);
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

std::string extract(const std::vector<uint8_t>& archive, const char* fullOrShortName, int index = -1) {
  FsysArchive f;
  if (!Fsys_Open(archive.data(), archive.size(), &f)) return "<not fsys>";
  if (index < 0) {
    for (uint32_t i = 0; i < f.count; i++) {
      FsysEntry e;
      if (Fsys_Entry(&f, i, &e) && (std::strcmp(e.fullName, fullOrShortName) == 0 || std::strcmp(e.name, fullOrShortName) == 0)) {
        index = int(i);
        break;
      }
    }
  }
  uint32_t n = 0;
  uint8_t* p = index >= 0 ? Fsys_Extract(&f, uint32_t(index), &n) : nullptr;
  if (p == nullptr) return "<missing>";
  std::string s(reinterpret_cast<char*>(p), n);
  std::free(p);
  return s;
}

bool lzss_round_trip(const std::vector<uint8_t>& data) {
  uint32_t packedSize = 0, unpackedSize = 0;
  uint8_t* packed = Lzss_Pack(data.data(), uint32_t(data.size()), &packedSize);
  if (packed == nullptr) return false;
  uint8_t* back = Lzss_Unpack(packed, packedSize, &unpackedSize);
  const bool ok = back != nullptr && unpackedSize == data.size() && std::memcmp(back, data.data(), data.size()) == 0 &&
                  get32(std::vector<uint8_t>(packed, packed + 16), 8) == packedSize;
  std::free(packed);
  std::free(back);
  return ok;
}

struct FakeDisc {
  std::map<std::string, std::vector<uint8_t>> files;
  DiscView View(const std::string& gameId = "TEST01") const {
    DiscView v;
    v.gameId = gameId;
    for (const auto& [path, data] : files) v.files.push_back(path);
    v.read = [this](const std::string& path, std::vector<uint8_t>& out) {
      auto it = files.find(path);
      if (it == files.end()) return false;
      out = it->second;
      return true;
    };
    return v;
  }
};

std::string manifest(const std::string& id, const std::string& extra = "", const std::string& game = "\"TEST01\"") {
  return "{\n  \"id\": \"" + id + "\",\n  \"name\": \"Mod " + id + "\",\n  \"version\": \"1.0\",\n  \"game\": " + game +
         (extra.empty() ? "" : ",\n  " + extra) + "\n}\n";
}

const Replacement* find(const LoadResult& r, const std::string& discPath) {
  for (const Replacement& f : r.files)
    if (f.discPath == discPath) return &f;
  return nullptr;
}

std::string contents(const Replacement* r) {
  if (r == nullptr) return "<none>";
  if (r->rebuilt) return std::string(r->data.begin(), r->data.end());
  return read_file(r->file);
}

bool has_message(const LoadResult& r, Message::Level level, const std::string& part) {
  for (const Message& m : r.messages)
    if (m.level == level && m.text.find(part) != std::string::npos) return true;
  return false;
}

void dump(const LoadResult& r) {
  for (const Message& m : r.messages) std::printf("      [%d] %s\n", int(m.level), m.text.c_str());
}

}

int main(int argc, char** argv) {
  const fs::path root = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path() / "xd_mods_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);

  {
    std::vector<uint8_t> random(20000), runs(70000, 'a'), text;
    uint32_t seed = 12345;
    for (auto& b : random) b = uint8_t((seed = seed * 1103515245 + 12345) >> 16);
    for (size_t i = 0; i < runs.size(); i += 7) runs[i] = uint8_t(i / 5000);
    for (int i = 0; i < 3000; i++) text.insert(text.end(), {'P', 'o', 'k', 'e', uint8_t('0' + i % 10), ' '});
    std::vector<uint8_t> far(10000);
    for (size_t i = 0; i < far.size(); i++) far[i] = uint8_t(i % 4095 < 20 ? i % 4095 : (i * 31) & 0xFF);
    check(lzss_round_trip({}), "LZSS: empty data packs and unpacks");
    check(lzss_round_trip({42}), "LZSS: one byte");
    check(lzss_round_trip(random), "LZSS: random bytes");
    check(lzss_round_trip(runs), "LZSS: long runs (overlapping back-references)");
    check(lzss_round_trip(text), "LZSS: repeated text");
    check(lzss_round_trip(far), "LZSS: matches at the far end of the window");
    uint32_t n = 0;
    uint8_t* p = Lzss_Pack(text.data(), uint32_t(text.size()), &n);
    check(p != nullptr && n < text.size() / 4, "LZSS: repeated text gets much smaller");
    std::free(p);
  }

  const std::vector<uint8_t> archive = make_fsys({{"menu", "menu.msg", "original menu text, original menu text", true},
                                                   {"menu", "menu.dat", "model data", true},
                                                   {"raw_entry", "", "stored as it is", false},
                                                   {"(null)", "", "no name", true}});
  {
    check(extract(archive, "menu.msg") == "original menu text, original menu text", "FSYS: the test archive reads back");
    const std::string longer(5000, 'x');
    FsysReplacement r[2] = {{0, reinterpret_cast<const uint8_t*>(longer.data()), uint32_t(longer.size())},
                            {2, reinterpret_cast<const uint8_t*>("new raw"), 7}};
    size_t size = 0;
    uint8_t* out = Fsys_Rebuild(archive.data(), archive.size(), r, 2, &size);
    check(out != nullptr, "FSYS: rebuild with two replaced entries");
    std::vector<uint8_t> rebuilt(out, out + size);
    std::free(out);
    check(extract(rebuilt, "menu.msg") == longer, "FSYS: replaced packed entry reads back (bigger than before)");
    check(extract(rebuilt, "raw_entry") == "new raw", "FSYS: replaced unpacked entry reads back");
    check(extract(rebuilt, "menu.dat") == "model data" && extract(rebuilt, "", 3) == "no name",
          "FSYS: the other entries are unchanged");
    FsysArchive f;
    FsysEntry e0, e2;
    Fsys_Open(rebuilt.data(), rebuilt.size(), &f);
    Fsys_Entry(&f, 0, &e0);
    Fsys_Entry(&f, 2, &e2);
    check(e0.packed && !e2.packed, "FSYS: entries keep being packed / not packed as they were");
    check(e0.offset % 32 == 0 && e2.offset % 32 == 0 && e0.offset >= archive.size() - 0x20,
          "FSYS: new data starts on 32-byte boundaries after the old data");
    check(get32(rebuilt, 0x20) == rebuilt.size() && std::memcmp(rebuilt.data() + rebuilt.size() - 4, "FSYS", 4) == 0 &&
              rebuilt.size() % 32 == 0,
          "FSYS: size field and closing \"FSYS\" block updated");
    check(std::memcmp(rebuilt.data() + 0x60, archive.data() + 0x60, 4 * 4) == 0, "FSYS: entry table unchanged");
    FsysReplacement bad = {9, reinterpret_cast<const uint8_t*>("x"), 1};
    check(Fsys_Rebuild(archive.data(), archive.size(), &bad, 1, &size) == nullptr, "FSYS: an entry number out of range is refused");
    FsysReplacement twice[2] = {{1, reinterpret_cast<const uint8_t*>("a"), 1}, {1, reinterpret_cast<const uint8_t*>("b"), 1}};
    check(Fsys_Rebuild(archive.data(), archive.size(), twice, 2, &size) == nullptr, "FSYS: the same entry twice is refused");
    const uint8_t junk[64] = {'J', 'U', 'N', 'K'};
    check(Fsys_Rebuild(junk, sizeof junk, nullptr, 0, &size) == nullptr, "FSYS: not an archive is refused");
  }

  {
    Manifest m;
    std::string error;
    check(ParseManifest("\xEF\xBB\xBF{ \"id\": \"better-text\", \"name\": \"Better text \\u00e9\\ud83d\\ude00\", "
                        "\"version\": \"1.2\", \"author\": \"A\", \"description\": \"line\\nbreak\", "
                        "\"game\": [\"GXXE01\", \"GXXJ01\"], \"requires\": \"base\", \"conflicts\": [\"other\"], "
                        "\"priority\": -3, \"format\": 1, \"homepage\": {\"unknown\": [1, 2.5e3, true, null]} }",
                        m, error),
          "mod.json: a full manifest reads (" + error + ")");
    check(m.id == "better-text" && m.name == "Better text \xC3\xA9\xF0\x9F\x98\x80" && m.version == "1.2" &&
              m.description == "line\nbreak" && m.games.size() == 2 && m.games[1] == "GXXJ01" && m.required.size() == 1 &&
              m.required[0] == "base" && m.conflicts[0] == "other" && m.priority == -3,
          "mod.json: every field has its value (unicode escapes, lists, single text as list)");
    check(!ParseManifest("{\"name\": \"x\", \"game\": \"G\"}", m, error) && error.find("id") != std::string::npos,
          "mod.json: no id is refused");
    check(!ParseManifest("{\"id\": \"x\"}", m, error) && error.find("game") != std::string::npos, "mod.json: no game is refused");
    check(!ParseManifest("{\"id\": \"has space\", \"game\": \"G\"}", m, error), "mod.json: an id with a space is refused");
    check(!ParseManifest("{\"id\": \"x\", \"game\": 5}", m, error), "mod.json: game must be text");
    check(!ParseManifest("{\"id\": \"x\", \"game\": \"G\", \"priority\": 1.5}", m, error), "mod.json: priority must be whole");
    check(!ParseManifest("{\"id\": \"x\", \"game\": \"G\", \"format\": 2}", m, error) && error.find("newer") != std::string::npos,
          "mod.json: a newer format is refused");
    const bool missingComma = !ParseManifest("{\n\"id\": \"x\",\n\"game\": \"G\"\n\"name\": \"n\"}", m, error);
    check(missingComma && error.find("line 4") != std::string::npos, "mod.json: a missing comma says where (" + error + ")");
    check(ParseManifest("{\"id\":\"x\",\"game\":\"G\"}", m, error) && m.name == "x", "mod.json: name defaults to the id");
  }

  {
    std::vector<std::string> warnings;
    ModList list = ParseModList("; comment\nprofile = run\n[profile default]\na = on\nb = off\n[profile run]\nb = yes\n"
                                "a = no\nnonsense\na = on\n[broken\n",
                                warnings);
    check(list.active == "run" && list.profiles.size() == 2, "mods.ini: profiles and the active one");
    check(list.Active().mods.size() == 2 && list.Active().mods[0] == std::make_pair(std::string("b"), true) &&
              list.Active().mods[1] == std::make_pair(std::string("a"), false),
          "mods.ini: order and on/off kept");
    check(warnings.size() == 3, "mods.ini: three bad lines reported (" + std::to_string(warnings.size()) + ")");
    std::vector<std::string> none;
    const ModList again = ParseModList(FormatModList(list), none);
    check(none.empty() && again.active == "run" && again.profiles.size() == 2 && again.profiles[0].mods == list.profiles[0].mods &&
              again.profiles[1].mods == list.profiles[1].mods,
          "mods.ini: written and read back unchanged");
  }

  FakeDisc disc;
  disc.files["common.fsys"] = archive;
  disc.files["Title.thp"] = bytes("original movie");
  disc.files["sound/se.samp"] = bytes("original sound");
  const fs::path modsDir = root / "user" / "mods";
  const fs::path listFile = root / "user" / "mods.ini";
  LoadOptions opt{modsDir, listFile, disc.View()};

  {
    const LoadResult r = Load(opt);
    check(r.files.empty() && r.active.empty() && !r.HasWarnings() && !fs::exists(listFile),
          "no mods folder: nothing replaced, no list written");
  }

  write_file(modsDir / "Movie Mod" / "mod.json", manifest("movie", "\"priority\": 5"));
  write_file(modsDir / "Movie Mod" / "files" / "TITLE.THP", "movie from mod A");
  write_file(modsDir / "Movie Mod" / "files" / "sound" / "se.samp", "sound from mod A");
  write_file(modsDir / "Movie Mod" / "readme.txt", "not a disc file");
  write_file(modsDir / "other" / "mod.json", manifest("other", "\"priority\": 1"));
  write_file(modsDir / "other" / "files" / "title.thp", "movie from mod B");
  write_file(modsDir / "other" / "files" / "added" / "new.bin", "a new file");
  write_file(modsDir / "other" / "files" / "start.dol", "not allowed");
  {
    LoadOptions ro = opt;
    ro.listReadOnly = true;
    const LoadResult r = Load(ro);
    check(!r.listWritten && r.listWriteSkipped && !fs::exists(listFile) && !fs::exists(fs::path(listFile).concat(".new")),
          "read-only list, new mods: no list file written");
    check(r.files.empty() && r.active.empty() && has_message(r, Message::Level::Info, "New mod found"),
          "read-only list: new mods mentioned and off");
  }
  {
    const LoadResult r = Load(opt);
    check(r.files.empty() && r.active.empty(), "new mods are off: nothing replaced");
    check(r.listWritten && fs::exists(listFile), "new mods: the list is written");
    std::vector<std::string> w;
    ModList list = ParseModList(read_file(listFile), w);
    check(list.Active().mods.size() == 2 && list.Active().mods[0].first == "other" && list.Active().mods[1].first == "movie" &&
              !list.Active().mods[0].second && !list.Active().mods[1].second,
          "new mods: listed off, lower priority first");
    check(has_message(r, Message::Level::Info, "New mod found") && !r.HasWarnings(), "new mods: mentioned, no warning");
    const LoadResult again = Load(opt);
    check(!again.listWritten, "second start: list unchanged, not written again");
    write_file(modsDir / "third" / "mod.json", manifest("third", "\"priority\": 9"));
    const std::string before = read_file(listFile);
    LoadOptions ro = opt;
    ro.listReadOnly = true;
    const LoadResult third = Load(ro);
    check(third.listWriteSkipped && !third.listWritten && read_file(listFile) == before,
          "read-only list, one more mod: the list file is unchanged");
    fs::remove_all(modsDir / "third");
  }

  write_file(listFile, "profile = default\n[profile default]\nother = on\nmovie = on\n");
  {
    const LoadResult r = Load(opt);
    dump(r);
    check(r.active.size() == 2 && r.ActiveText() == "other 1.0, movie 1.0", "both on, in list order");
    check(contents(find(r, "Title.thp")) == "movie from mod A", "path in another case maps to the disc's file; later mod wins");
    check(contents(find(r, "sound/se.samp")) == "sound from mod A", "file in a disc folder");
    const Replacement* added = find(r, "added/new.bin");
    check(added != nullptr && added->added && contents(added) == "a new file", "a file the disc lacks is added");
    check(find(r, "start.dol") == nullptr && has_message(r, Message::Level::Warning, "start.dol"),
          "start.dol is refused with a warning");
    check(find(r, "readme.txt") == nullptr, "files outside files/ and fsys/ are not disc files");
    check(has_message(r, Message::Level::Warning, "Conflict") && r.Report().find("Title.thp") != std::string::npos,
          "the conflict is reported, naming the file");
  }

  write_file(listFile, "[profile default]\nmovie = on\nother = on\n");
  {
    const LoadResult r = Load(opt);
    check(contents(find(r, "Title.thp")) == "movie from mod B", "list order decides who wins");
  }

  write_file(listFile, "[profile default]\nmovie = off\nother = on\n");
  {
    const LoadResult r = Load(opt);
    check(r.active.size() == 1 && find(r, "sound/se.samp") == nullptr, "a mod that is off changes nothing");
  }
  {
    LoadOptions wrong = opt;
    wrong.disc = disc.View("GXXP01");
    const LoadResult r = Load(wrong);
    check(r.active.empty() && r.files.empty() && has_message(r, Message::Level::Problem, "GXXP01"),
          "a mod for another game ID is refused, saying which disc this is");
  }

  write_file(modsDir / "needy" / "mod.json", manifest("needy", "\"requires\": [\"base\"]"));
  write_file(modsDir / "needy" / "files" / "common.fsys", "x");
  write_file(modsDir / "rival" / "mod.json", manifest("rival", "\"conflicts\": \"other\""));
  write_file(modsDir / "rival" / "files" / "Title.thp", "rival");
  write_file(listFile, "[profile default]\nother = on\nneedy = on\nrival = on\n");
  {
    const LoadResult r = Load(opt);
    check(r.active.size() == 1 && r.active[0].id == "other", "missing requirement and declared conflict: those mods are not used");
    check(has_message(r, Message::Level::Problem, "needs the mod \"base\"") &&
              has_message(r, Message::Level::Problem, "can't be used together"),
          "both are explained");
    check(contents(find(r, "Title.thp")) == "movie from mod B", "the mod further up the list is kept");
  }

  write_file(modsDir / "texts" / "mod.json", manifest("texts", "", "[\"OTHER1\", \"test01\"]"));
  write_file(modsDir / "texts" / "fsys" / "common.fsys" / "menu.msg", "new menu text");
  write_file(modsDir / "texts" / "fsys" / "COMMON.fsys" / "raw_entry", "new raw");
  write_file(modsDir / "texts2" / "mod.json", manifest("texts2"));
  write_file(modsDir / "texts2" / "fsys" / "common.fsys" / "#3.bin", "named by number");
  write_file(modsDir / "texts2" / "fsys" / "common.fsys" / "menu.msg", "second mod's menu");
  write_file(modsDir / "texts2" / "fsys" / "common.fsys" / "menu", "ambiguous");
  write_file(modsDir / "texts2" / "fsys" / "common.fsys" / "nothing", "no such entry");
  write_file(modsDir / "texts2" / "fsys" / "missing.fsys" / "x", "no such archive");
  write_file(listFile, "[profile default]\ntexts = on\ntexts2 = on\n");
  {
    const LoadResult r = Load(opt);
    dump(r);
    const Replacement* c = find(r, "common.fsys");
    check(c != nullptr && c->rebuilt, "entry changes: the archive is rebuilt");
    const std::vector<uint8_t> data = c != nullptr ? c->data : std::vector<uint8_t>();
    check(extract(data, "menu.msg") == "second mod's menu", "entry by full name; later mod wins");
    check(extract(data, "raw_entry") == "new raw", "entry by short name, archive folder in another case");
    check(extract(data, "", 3) == "named by number", "entry by number");
    check(extract(data, "menu.dat") == "model data", "untouched entry unchanged");
    check(has_message(r, Message::Level::Problem, "several entries") && has_message(r, Message::Level::Problem, "no entry called nothing") &&
              has_message(r, Message::Level::Problem, "missing.fsys") && has_message(r, Message::Level::Warning, "both replace menu.msg"),
          "ambiguous name, unknown entry, unknown archive and the overlap are reported");
    check(find(r, "missing.fsys") == nullptr, "no file made up for an archive the disc lacks");
  }

  write_file(modsDir / "whole" / "mod.json", manifest("whole"));
  {
    const std::vector<uint8_t> modArchive = make_fsys({{"menu", "menu.msg", "whole mod's menu", false}, {"raw_entry", "", "whole raw", true}});
    fs::create_directories(modsDir / "whole" / "files");
    std::ofstream(modsDir / "whole" / "files" / "common.fsys", std::ios::binary)
        .write(reinterpret_cast<const char*>(modArchive.data()), std::streamsize(modArchive.size()));
  }
  write_file(listFile, "[profile default]\ntexts = on\nwhole = on\n");
  {
    const LoadResult r = Load(opt);
    const Replacement* c = find(r, "common.fsys");
    check(c != nullptr && !c->rebuilt && has_message(r, Message::Level::Warning, "replaces all of common.fsys"),
          "whole file after entry changes: the whole file is used, reported");
  }
  write_file(listFile, "[profile default]\nwhole = on\ntexts = on\n");
  {
    const LoadResult r = Load(opt);
    const Replacement* c = find(r, "common.fsys");
    const std::vector<uint8_t> data = c != nullptr ? c->data : std::vector<uint8_t>();
    check(c != nullptr && c->rebuilt && extract(data, "menu.msg") == "new menu text" && extract(data, "raw_entry") == "new raw",
          "entry changes on top of an earlier mod's whole archive");
    check(!r.HasWarnings(), "no conflict reported for layering in that order");
  }

  write_file(listFile, "profile = vanilla\n[profile default]\nwhole = on\n[profile vanilla]\n");
  {
    const LoadResult r = Load(opt);
    check(r.active.empty() && r.files.empty(), "an empty profile in use: nothing replaced");
    std::vector<std::string> w;
    ModList list = ParseModList(read_file(listFile), w);
    check(list.active == "vanilla" && list.profiles.size() == 2 && list.profiles[1].mods.size() == 7 &&
              list.profiles[0].mods.size() == 1,
          "the profile in use gets the newly found mods (off); the other stays as it was");
  }

  write_file(modsDir / "broken" / "mod.json", "{ \"id\": \"broken\" ");
  write_file(modsDir / "copy" / "mod.json", manifest("whole"));
  write_file(modsDir / "loose" / "files" / "Title.thp", "no manifest");
  write_file(modsDir / "packed.zip", "zip");
  {
    const LoadResult r = Load(opt);
    check(has_message(r, Message::Level::Problem, "\"broken\" can't be used") &&
              has_message(r, Message::Level::Problem, "both hold a mod with the id") &&
              has_message(r, Message::Level::Warning, "has no mod.json") && has_message(r, Message::Level::Warning, "packed.zip"),
          "bad JSON, duplicate id, folder without mod.json and a zip are reported");
  }

  {
    Manifest m;
    std::string error;
    check(ParseManifest(manifest("a"), m, error) && m.applies == Applies::NewPlaythrough && !m.appliesGiven,
          "applies: missing = new_playthrough");
    check(ParseManifest(manifest("a", "\"applies\": \"any_time\""), m, error) && m.applies == Applies::AnyTime &&
              m.appliesGiven,
          "applies: any_time read");
    check(ParseManifest(manifest("a", "\"applies\": \"restart\""), m, error) && m.applies == Applies::Restart,
          "applies: restart read");
    check(!ParseManifest(manifest("a", "\"applies\": \"anytime\""), m, error) && error.find("applies") != std::string::npos,
          "applies: an unknown value is refused, with a message naming the field");

    const fs::path dir = root / "applies";
    const LooksOrSound looks = [](const std::string& path, int type) {
      if (type == -1) return path.size() > 5 && (path.substr(path.size() - 5) == ".samp" || path.substr(path.size() - 4) == ".thp");
      return type == 9;
    };
    std::vector<uint8_t> typed = archive;
    {
      FsysArchive f;
      Fsys_Open(typed.data(), typed.size(), &f);
      const uint32_t h0 = get32(typed, 0x60);
      put32(typed, h0 + 0x20, 9);
    }
    FakeDisc d2;
    d2.files["common.fsys"] = typed;
    const DiscView view = d2.View();
    const auto folder = [&](const std::string& id, const std::string& applies) {
      ModFolder f;
      f.folder = dir / id;
      f.folderName = id;
      std::string e;
      write_file(f.folder / "mod.json", manifest(id, "\"applies\": \"" + applies + "\""));
      ParseManifest(read_file(f.folder / "mod.json"), f.manifest, e);
      return f;
    };
    ModFolder sound = folder("sound", "any_time");
    write_file(sound.folder / "files" / "sound" / "se.samp", "x");
    write_file(sound.folder / "fsys" / "common.fsys" / "menu.msg", "x");
    AppliesVerdict v = CheckApplies(sound, looks, &view);
    check(v.effective == Applies::AnyTime && !v.Downgraded(), "CheckApplies: sound file + texture entry: any_time kept");
    v = CheckApplies(sound, looks, nullptr);
    check(v.effective == Applies::NewPlaythrough && v.reason.find("without the disc") != std::string::npos,
          "CheckApplies: without the disc an entry can't be checked: new_playthrough, saying why");
    ModFolder data = folder("data", "any_time");
    write_file(data.folder / "data" / "t.json", "{}");
    v = CheckApplies(data, looks, &view);
    check(v.Downgraded() && v.effective == Applies::NewPlaythrough && v.reason.find("tables") != std::string::npos,
          "CheckApplies: any_time with a data folder counts as new_playthrough");
    ModFolder entry = folder("entry", "any_time");
    write_file(entry.folder / "fsys" / "common.fsys" / "menu.dat", "x");
    v = CheckApplies(entry, looks, &view);
    check(v.Downgraded() && v.reason.find("menu.dat") != std::string::npos,
          "CheckApplies: any_time replacing a data entry counts as new_playthrough");
    ModFolder restart = folder("restart", "restart");
    write_file(restart.folder / "data" / "t.json", "{}");
    check(!CheckApplies(restart, looks, &view).Downgraded(), "CheckApplies: a restart claim is taken as it is");
    check(!CheckApplies(folder("none", "new_playthrough"), looks, &view).Downgraded(),
          "CheckApplies: new_playthrough stays");

    const fs::path mods3 = root / "user3" / "mods";
    const fs::path list3 = root / "user3" / "mods.ini";
    write_file(mods3 / "world" / "mod.json", manifest("world"));
    write_file(mods3 / "world" / "files" / "Title.thp", "world");
    write_file(mods3 / "world2" / "mod.json", manifest("world2"));
    write_file(mods3 / "world2" / "files" / "common.fsys", "world2");
    write_file(mods3 / "music" / "mod.json", manifest("music", "\"applies\": \"any_time\""));
    write_file(mods3 / "music" / "files" / "sound" / "se.samp", "music");
    write_file(list3, "[profile default]\nworld = on\nworld2 = off\nmusic = on\n");
    LoadOptions o3{mods3, list3, disc.View()};
    o3.looksOrSound = looks;
    LoadResult r = Load(o3);
    check(r.active.size() == 2 && r.activeApplies.size() == 2 && r.activeApplies[1].effective == Applies::AnyTime,
          "load: the list decides without a playthrough (world + music), each with its kind");
    o3.usePlaythroughMods = true;
    o3.playthroughMods = {"world2"};
    r = Load(o3);
    check(r.active.size() == 2 && r.active[0].id == "world2" && r.active[1].id == "music",
          "load: a playthrough's mods: its world mod (off in the list) on, the list's other world mod off, music by the list");
    o3.playthroughMods = {};
    r = Load(o3);
    check(r.active.size() == 1 && r.active[0].id == "music", "load: a Vanilla playthrough keeps only the any_time mod");
    o3.playthroughMods = {"gone"};
    r = Load(o3);
    check(has_message(r, Message::Level::Info, "\"gone\""), "load: a playthrough's mod that is gone is said (info)");

    {
      const fs::path layerDir = root / "extra_assets" / "pbr";
      write_file(layerDir / "mod.json", manifest("battle-revolution-models"));
      write_file(layerDir / "files" / "sound" / "se.samp", "layer sound");
      write_file(layerDir / "files" / "Title.thp", "layer movie");
      write_file(layerDir / "fsys" / "common.fsys" / "raw_entry", "layer raw");
      write_file(layerDir / "fsys" / "common.fsys" / "menu.msg", "layer menu");
      write_file(mods3 / "ent" / "mod.json", manifest("ent", "\"applies\": \"any_time\""));
      write_file(mods3 / "ent" / "fsys" / "common.fsys" / "menu.msg", "mod menu");
      write_file(list3, "[profile default]\nmusic = on\nent = on\n");
      LoadOptions lo{mods3, list3, disc.View()};
      lo.looksOrSound = looks;
      lo.extraLayers = {{"pbr", "Battle Revolution models", layerDir}};
      LoadResult r = Load(lo);
      const Replacement* common = find(r, "common.fsys");
      check(r.extraLayers.size() == 1 && r.extraLayers[0].found && r.extraLayers[0].files == 4,
            "extra layer: found, its 4 files counted");
      check(contents(find(r, "sound/se.samp")) == "music", "extra layer: a mod replacing the same file wins");
      check(contents(find(r, "Title.thp")) == "layer movie", "extra layer: above the disc (a file no mod touches)");
      check(common != nullptr && common->rebuilt && extract(common->data, "menu.msg") == "mod menu" &&
                extract(common->data, "raw_entry") == "layer raw" && extract(common->data, "menu.dat") == "model data",
            "extra layer: archive entries: mod over layer over disc");
      check(!r.HasWarnings() && !has_message(r, Message::Level::Warning, "Conflict"),
            "extra layer: a mod over a layer file is no conflict");
      check(r.active.size() == 2 && r.active[0].id == "music" && r.active[1].id == "ent" &&
                r.ActiveText().find("pbr") == std::string::npos && r.ActiveText().find("battle") == std::string::npos,
            "extra layer: never among the mods in use");
      check(read_file(list3).find("pbr") == std::string::npos && read_file(list3).find("battle") == std::string::npos,
            "extra layer: never in the mod list");
      lo.extraLayers.clear();
      r = Load(lo);
      check(r.extraLayers.empty() && find(r, "Title.thp") == nullptr,
            "extra layer: not passed: nothing of it");
      lo.extraLayers = {{"pbr", "Battle Revolution models", root / "extra_assets" / "nothing"}};
      r = Load(lo);
      check(r.extraLayers.size() == 1 && !r.extraLayers[0].found && r.extraLayers[0].files == 0 && !r.HasWarnings() &&
                find(r, "Title.thp") == nullptr,
            "extra layer: folder missing: nothing, no warning");
      write_file(mods3 / "world2" / "files" / "common.fsys", "world2");
      write_file(list3, "[profile default]\nworld2 = on\n");
      lo.extraLayers = {{"pbr", "Battle Revolution models", layerDir}};
      lo.usePlaythroughMods = false;
      r = Load(lo);
      check(contents(find(r, "common.fsys")) == "world2" && !has_message(r, Message::Level::Warning, "replaces all of"),
            "extra layer: a mod's whole archive replaces the layer's entries without a warning");
    }
  }

  {
    const fs::path modsR = root / "mods_review", listR = root / "user_review" / "mods.ini";
    write_file(modsR / "a" / "mod.json", manifest("a"));
    write_file(modsR / "a" / "files" / "Title.thp", "a");
    write_file(modsR / "a" / "fsys" / "common.fsys" / "menu.msg", "a");
    write_file(modsR / "b" / "mod.json", manifest("b", "\"after\": [\"c\"]"));
    write_file(modsR / "b" / "files" / "title.THP", "b");
    write_file(modsR / "c" / "mod.json", manifest("c"));
    write_file(modsR / "c" / "data" / "prices.json", "{}");
    write_file(modsR / "needs" / "mod.json", manifest("needs", "\"requires\": [\"missing-one\"]"));
    write_file(modsR / "other" / "mod.json", manifest("other", "", "\"GXXP01\""));
    write_file(modsR / "arch" / "mod.json", manifest("arch"));
    write_file(modsR / "arch" / "files" / "common.fsys", "all of it");
    write_file(modsR / "foe" / "mod.json", manifest("foe", "\"conflicts\": [\"c\"]"));
    write_file(modsR / "dup1" / "mod.json", manifest("same"));
    write_file(modsR / "dup2" / "mod.json", manifest("same"));
    write_file(modsR / "broken" / "mod.json", "{ not json");
    write_file(modsR / "nomanifest" / "readme.txt", "no mod.json: not a mod, not a row");
    write_file(modsR / "packed.zip", "zip");
    std::vector<Message> msgs;
    std::vector<BrokenFolder> broken;
    const std::vector<ModFolder> found = FindMods(modsR, msgs, broken);
    check(found.size() == 8, "review: the usable mods are found (8 of the folders)");
    bool brokenOk = broken.size() == 3;
    for (const BrokenFolder& b : broken) brokenOk &= !b.reason.empty();
    bool hasDup = false, hasBroken = false, hasZip = false;
    for (const BrokenFolder& b : broken) {
      hasDup |= b.folderName == "dup2" && b.reason.find("same id") != std::string::npos;
      hasBroken |= b.folderName == "broken";
      hasZip |= b.folderName == "packed.zip";
    }
    check(brokenOk && hasDup && hasBroken && hasZip, "review: unusable folders come back with a reason (broken mod.json, packed file, id twice), a folder without mod.json doesn't");
    check(FindMods(modsR, msgs).size() == found.size(), "review: the plain FindMods finds the same mods");

    const ModChanges changes = ChangesOf(*std::find_if(found.begin(), found.end(), [](const ModFolder& f) { return f.manifest.id == "a"; }));
    check(changes.files == std::vector<std::string>{"Title.thp"} && changes.entries == std::vector<std::string>{"common.fsys/menu.msg"} &&
              changes.dataFiles.empty() && DescribeChanges(changes) == "1 game file, 1 archive entry",
          "review: what a mod changes, by name");
    check(DescribeChanges(ModChanges{}) == "nothing", "review: a mod that changes nothing says so");

    ModList::Profile p;
    p.name = "default";
    p.mods = {{"a", true}, {"b", true}, {"c", true}, {"gone", true}, {"other", true}, {"needs", true}, {"c2", false}};
    const Review r = ReviewList(p, found, "TEST01");
    const auto row = [&](const char* id) -> const ReviewRow& {
      for (const ReviewRow& x : r.rows)
        if (x.id == id) return x;
      static ReviewRow none;
      return none;
    };
    const auto mentions = [](const std::vector<std::string>& v, const std::string& part) {
      return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return s.find(part) != std::string::npos; });
    };
    check(r.rows.size() == p.mods.size() && r.rows[0].id == "a" && r.rows[6].id == "c2", "review: one row per listed mod, in list order");
    check(row("a").Usable() && row("b").Usable() && row("c").Usable(), "review: a plain mod that is on is usable");
    check(mentions(row("a").warnings, "Clashes with \"Mod b\"") && mentions(row("b").warnings, "this one, further down the list, wins") &&
              mentions(row("b").warnings, "title.thp"),
          "review: two mods that replace the same file (other spelling too) warn each other, the later one wins");
    check(mentions(row("b").warnings, "after \"Mod c\""), "review: a mod that wants to come after another further down says so");
    check(!row("gone").Usable() && row("gone").mod == nullptr && mentions(row("gone").problems, "Not in the mods folder"),
          "review: a listed mod that isn't there is a row with the reason");
    check(!row("other").Usable() && mentions(row("other").problems, "GXXP01"), "review: a mod for another game version is not usable, with the reason");
    check(!row("needs").Usable() && mentions(row("needs").problems, "missing-one"), "review: a missing requirement is a reason");
    check(row("c2").mod == nullptr && !row("c2").on, "review: a listed mod that is off stays a row (off)");
    check(mentions(ReviewList(p, found, "").rows[4].problems, "GXXP01") == false, "review: no game ID given: the game isn't checked");

    ModList::Profile q;
    q.mods = {{"c", true}, {"foe", true}};
    const Review rq = ReviewList(q, found, "TEST01");
    check(rq.rows[0].Usable() && !rq.rows[1].Usable() && mentions(rq.rows[1].problems, "Can't be used together"),
          "review: mods that conflict: the one further down isn't used, and says why");
    q.mods = {{"a", true}, {"arch", true}};
    const Review rw = ReviewList(q, found, "TEST01");
    check(mentions(rw.rows[0].warnings, "common.fsys") && mentions(rw.rows[1].warnings, "common.fsys"),
          "review: a whole archive and one of its entries replaced by two mods clash");
    q.mods = {{"a", true}, {"arch", false}};
    check(ReviewList(q, found, "TEST01").rows[0].warnings.empty(), "review: a clash only counts while both are on");

    ModList list;
    list.active = "default";
    list.Active().mods = {{"a", true}, {"b", false}, {"c", true}};
    check(!MoveMod(list.Active(), 0, -1) && !MoveMod(list.Active(), 2, 1) && !MoveMod(list.Active(), 3, -1) && !MoveMod(list.Active(), 1, 2),
          "order: nothing moves past the ends or by more than one");
    check(MoveMod(list.Active(), 2, -1) && list.Active().mods[1].first == "c" && list.Active().mods[2].first == "b" && !list.Active().mods[2].second,
          "order: a mod moves up one place with its on/off");
    list.Active().mods[0].second = false;
    check(WriteModListFile(listR, list), "order: the list is written");
    ModList back;
    std::vector<std::string> warn;
    check(ReadModListFile(listR, back, warn) && back.Active().mods.size() == 3 && back.Active().mods[0] == std::make_pair(std::string("a"), false) &&
              back.Active().mods[1] == std::make_pair(std::string("c"), true) && back.Active().mods[2] == std::make_pair(std::string("b"), false),
          "order: mods.ini holds the new order and the on/off, read back");
    list.profiles.push_back({"second", {{"b", true}}});
    check(WriteModListFile(listR, list) && ReadModListFile(listR, back, warn) && back.profiles.size() == 2 && back.profiles[1].mods.size() == 1,
          "order: other profiles in the file survive");
  }

  {
    const fs::path sm = root / "scriptmods";
    write_file(sm / "lua1" / "mod.json", manifest("lua1", "\"scripts\": true, \"applies\": \"any_time\""));
    write_file(sm / "lua1" / "scripts" / "b.lua", "-- second");
    write_file(sm / "lua1" / "scripts" / "a.lua", "-- first");
    write_file(sm / "lua1" / "scripts" / "notes.txt", "not a script");
    write_file(sm / "quiet" / "mod.json", manifest("quiet"));
    write_file(sm / "quiet" / "scripts" / "x.lua", "-- not switched on in mod.json");
    write_file(sm / "future" / "mod.json", manifest("future", "\"scripts\": true, \"script_api\": 99"));
    write_file(sm / "bad" / "mod.json", manifest("bad", "\"scripts\": \"yes\""));
    write_file(sm / "empty" / "mod.json", manifest("empty", "\"scripts\": true"));
    write_file(sm / "list.ini", "[profile default]\nlua1 = on\nquiet = on\nempty = on\n");
    LoadOptions o4{sm, sm / "list.ini", disc.View()};
    const LoadResult r = Load(o4);
    check(r.scripts.size() == 2 && r.scripts[0].name == "scripts/a.lua" && r.scripts[1].name == "scripts/b.lua" &&
              r.scripts[0].modId == "lua1" && r.scripts[0].text == "-- first",
          "scripts: scripts/*.lua of a mod with \"scripts\": true, by name, with their text; .txt left out");
    check(has_message(r, Message::Level::Warning, "only .lua files") && has_message(r, Message::Level::Warning, "does not say \"scripts\": true") &&
              has_message(r, Message::Level::Warning, "has no scripts/*.lua"),
          "scripts: a stray file, a scripts folder without the flag and an empty script mod are said");
    check(has_message(r, Message::Level::Problem, "newer script interface") || has_message(r, Message::Level::Warning, "newer script interface"),
          "scripts: a newer script_api is refused");
    Manifest m;
    std::string error;
    check(!ParseManifest(manifest("bad", "\"scripts\": \"yes\""), m, error) && error.find("true or false") != std::string::npos,
          "scripts: \"scripts\" must be true or false");
    check(ParseManifest(manifest("ok", "\"scripts\": true"), m, error) && m.scripts && m.scriptApi == 1, "scripts: manifest fields");
    ModFolder folder;
    folder.manifest = r.active.empty() ? m : r.active[0];
    folder.manifest.applies = Applies::AnyTime;
    folder.manifest.scripts = true;
    folder.folder = sm / "lua1";
    const AppliesVerdict v = CheckApplies(folder, nullptr);
    check(v.Downgraded() && v.effective == Applies::NewPlaythrough && v.reason.find("scripts") != std::string::npos,
          "scripts: a mod that runs scripts is never any_time");
  }

  fs::remove_all(root, ec);
  std::printf(sOk ? "mods_test: all passed\n" : "mods_test: FAILED\n");
  return sOk ? 0 : 1;
}
