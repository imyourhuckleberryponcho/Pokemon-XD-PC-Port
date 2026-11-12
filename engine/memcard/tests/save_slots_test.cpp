#include <save_slots.h>

#include <gc_memcard.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace sl = engine::memcard::slots;

namespace {
int sFailures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
  if (!ok) sFailures++;
}

std::vector<uint8_t> ReadFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path& p, const std::vector<uint8_t>& b) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

void WriteText(const fs::path& p, const std::string& s) { WriteFile(p, std::vector<uint8_t>(s.begin(), s.end())); }

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

std::vector<uint8_t> Gci(uint8_t fill, uint16_t blocks = 2) {
  std::vector<uint8_t> b(0x40 + blocks * 0x2000u, fill);
  std::fill(b.begin(), b.begin() + 0x40, uint8_t{0});
  const char* head = "GXXE01";
  std::copy(head, head + 6, b.begin());
  const char* name = "TestSave";
  std::copy(name, name + 8, b.begin() + 8);
  b[0x38] = static_cast<uint8_t>(blocks >> 8);
  b[0x39] = static_cast<uint8_t>(blocks);
  return b;
}

std::map<std::string, std::vector<uint8_t>> Tree(const fs::path& root, const std::string& skip = "") {
  std::map<std::string, std::vector<uint8_t>> out;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const std::string rel = Utf8(fs::relative(it->path(), root));
    if (!skip.empty() && rel.compare(0, skip.size(), skip) == 0) {
      if (it->is_directory(ec)) it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec)) out[rel] = ReadFile(it->path());
  }
  return out;
}

fs::path Plus(fs::path p, const char* ending) {
  p += ending;
  return p;
}

std::vector<fs::path> sBinned;
bool BinOk(const fs::path& folder, std::string&) {
  sBinned.push_back(folder);
  std::error_code ec;
  fs::remove_all(folder, ec);
  return !ec;
}
bool BinFails(const fs::path&, std::string& error) {
  error = "the bin is full";
  return false;
}
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const bool own = argc < 2;
  const fs::path work = own ? fs::current_path() / "save_slots_test.tmp" : fs::path(argv[1]);
  std::error_code ec;
  fs::remove_all(work, ec);
  const fs::path data = work / fs::path(u8"Pokémon XD データ");
  fs::create_directories(data);
  const std::string file = "01-GXXE-TestSave.gci";
  std::string error, id;

  WriteFile(data / "USA" / "Card A" / file, Gci(0x11));
  WriteFile(data / "USA" / "Card A" / "MC_SYSTEM_AREA", std::vector<uint8_t>(0x2000, 0x22));
  WriteText(data / "playthroughs" / "Card A" / "pt_1.json", "{\"engine\": \"faithful\"}");
  WriteFile(data / "save_backups" / "Card A" / "2026-11-01_10-00-00" / file, Gci(0x10));
  WriteText(data / "settings.ini", "format = 1\n");
  WriteText(data / "recordings" / "play.cards" / "start-card" / "USA" / "Card A" / "x.gci", "not a region folder");
  const auto before = Tree(data, "saves");

  std::printf("no index\n");
  std::string note;
  sl::Index index = sl::Load(data, &note);
  Check(index.slots.size() == 1 && index.slots[0].id == "main" && index.slots[0].name == "Save 1" && index.active == "main",
        "only the first save, active");
  Check(note.empty(), "nothing odd about that");
  std::string name;
  Check(sl::ActiveRoot(data, &name) == data && name == "Save 1", "its card root is the data folder itself");
  Check(!fs::exists(sl::IndexFile(data)) && !fs::exists(data / "saves"), "looking wrote nothing");
  Check(sl::SetActive(data, "main", error) && !fs::exists(data / "saves"), "choosing the save that is active writes nothing");
  Check(sl::RootOf(data, "s2") == data / "saves" / "s2" && sl::RootOf(data, "../x") == data, "roots (odd ids = the data folder)");
  const std::vector<fs::path> regions = sl::RegionFolders(data);
  Check(regions.size() == 1 && regions[0] == data / "USA", "the card's region folder: USA only");

  std::printf("create\n");
  Check(sl::NextName(index) == "Save 2", "next name: " + sl::NextName(index));
  Check(sl::Create(data, "  Nuzlocke run  ", id, error) && id == "s2", "new save: " + id + " " + error);
  Check(fs::is_directory(data / "saves" / "s2"), "its folder is there");
  index = sl::Load(data);
  Check(index.slots.size() == 2 && index.slots[1].id == "s2" && index.slots[1].name == "Nuzlocke run" && index.active == "main",
        "listed with its name trimmed; the first save still active");
  Check(!fs::exists(Plus(sl::IndexFile(data), ".tmp")), "no temporary file left");
  Check(!sl::Create(data, "   ", id, error) && !error.empty(), "a name of spaces refused: " + error);
  Check(!sl::Create(data, "\x01\x02", id, error), "a name of control characters refused");
  const std::string japanese = "\xE3\x82\xBB\xE3\x83\xBC\xE3\x83\x96 \xC3\xA9 \"quoted\" back\\slash";
  Check(sl::Create(data, japanese, id, error) && id == "s3", "a name that isn't ASCII, with quotes: " + id);
  index = sl::Load(data);
  Check(index.Find("s3") != nullptr && index.Find("s3")->name == japanese, "read back the same");
  Check(sl::CleanName(std::string(100, 'a')).size() == sl::kMaxNameBytes, "long names cut");
  const std::string cut = sl::CleanName(std::string(59, 'a') + "\xC3\xA9\xC3\xA9");
  Check(cut == std::string(59, 'a'), "cut at a character's end, not inside one (" + std::to_string(cut.size()) + " bytes)");

  std::printf("active\n");
  Check(sl::SetActive(data, "s2", error), "the second save made active");
  Check(sl::ActiveRoot(data, &name) == data / "saves" / "s2" && name == "Nuzlocke run", "active root: saves\\s2, " + name);
  Check(!sl::SetActive(data, "s9", error) && sl::Load(data).active == "s2", "an unknown save refused: " + error);

  std::printf("rename\n");
  Check(sl::Rename(data, "s2", "Second", error) && sl::Load(data).Find("s2")->name == "Second", "renamed");
  Check(sl::Rename(data, "main", "My first", error) && sl::Load(data).slots[0].name == "My first", "the first save renamed");
  Check(!sl::Rename(data, "s2", "", error) && sl::Load(data).Find("s2")->name == "Second", "an empty name refused");
  Check(!sl::Rename(data, "s9", "x", error), "an unknown save refused");
  Check(sl::Load(data).active == "s2", "still the active one");

  std::printf("duplicate\n");
  Check(sl::Duplicate(data, "main", id, error) && id == "s4", "the first save copied: " + id + " " + error);
  index = sl::Load(data);
  Check(index.Find("s4") != nullptr && index.Find("s4")->name == "My first (copy)", "named \"My first (copy)\"");
  const fs::path s4 = data / "saves" / "s4";
  Check(ReadFile(s4 / "USA" / "Card A" / file) == Gci(0x11) && fs::exists(s4 / "USA" / "Card A" / "MC_SYSTEM_AREA"),
        "the card's files copied");
  Check(fs::exists(s4 / "playthroughs" / "Card A" / "pt_1.json"), "the setup file copied");
  Check(!fs::exists(s4 / "save_backups"), "the backups not copied");
  Check(!fs::exists(s4 / "settings.ini") && !fs::exists(s4 / "recordings") && !fs::exists(s4 / "saves"),
        "nothing else of the data folder copied");
  Check(index.active == "s2", "the copy is not made active");
  WriteFile(s4 / "USA" / "Card A" / file, Gci(0x44));
  Check(sl::Duplicate(data, "s4", id, error) && id == "s5" && ReadFile(data / "saves" / "s5" / "USA" / "Card A" / file) == Gci(0x44),
        "a copy of a copy: " + id);
  Check(sl::Load(data).Find("s5")->name == "My first (copy) (copy)", "its name");
  Check(!sl::Duplicate(data, "s9", id, error), "an unknown save refused");

  std::printf("import / export\n");
  const fs::path gci = work / "from_dolphin.gci";
  WriteFile(gci, Gci(0x77, 3));
  Check(sl::Import(data, gci, "Imported", fs::path("USA") / "Card A", file, id, error) && id == "s6", "imported: " + id + " " + error);
  Check(ReadFile(data / "saves" / "s6" / "USA" / "Card A" / file) == Gci(0x77, 3), "its bytes are on the new save's card");
  Check(sl::Load(data).Find("s6")->name == "Imported" && sl::Load(data).active == "s2", "listed, not made active");
  const fs::path junk = work / "junk.gci";
  WriteText(junk, "this is not a card file");
  const size_t count = sl::Load(data).slots.size();
  Check(!sl::Import(data, junk, "Junk", fs::path("USA") / "Card A", file, id, error), "a file that is no .gci refused: " + error);
  std::vector<uint8_t> shortFile = Gci(0x55, 3);
  shortFile.resize(shortFile.size() - 100);
  WriteFile(junk, shortFile);
  Check(!sl::Import(data, junk, "Cut", fs::path("USA") / "Card A", file, id, error), "a cut-off .gci refused: " + error);
  Check(!sl::Import(data, work / "missing.gci", "None", fs::path("USA") / "Card A", file, id, error), "a missing file refused");
  Check(sl::Load(data).slots.size() == count && !fs::exists(data / "saves" / "s7"), "refused imports made no save");
  const fs::path out = work / "exported.gci";
  Check(sl::Export(data / "saves" / "s6" / "USA" / "Card A" / file, out, error) && ReadFile(out) == Gci(0x77, 3),
        "exported byte for byte " + error);
  Check(sl::Export(data / "USA" / "Card A" / file, out, error) && ReadFile(out) == Gci(0x11), "exporting over a file replaces it");
  Check(!fs::exists(Plus(out, ".tmp")), "no temporary file left");
  Check(!sl::Export(data / "saves" / "s2" / "USA" / "Card A" / file, work / "none.gci", error) && !fs::exists(work / "none.gci"),
        "a save without a save file: " + error);
  Check(!sl::Export(data / "USA" / "Card A" / file, data / "USA" / "Card A" / file, error), "onto itself refused: " + error);

  std::printf("the first save's own files\n");
  Check(Tree(data, "saves") == before, "everything outside saves\\ is byte for byte as before");

  std::printf("delete\n");
  Check(!sl::Delete(data, "s4", BinFails, error) && error == "the bin is full" && sl::Load(data).Find("s4") != nullptr &&
            fs::exists(s4 / "USA" / "Card A" / file),
        "the bin fails: the save stays, listed and on disk");
  Check(sl::Delete(data, "s4", BinOk, error) && sBinned.size() == 1 && sBinned[0] == s4, "deleted: its folder went to the bin");
  Check(sl::Load(data).Find("s4") == nullptr && sl::Load(data).active == "s2", "gone from the list; the active one unchanged");
  Check(sl::Delete(data, "s2", BinOk, error) && sl::Load(data).active == "main", "the active save deleted: the first one is active");
  Check(sl::ActiveRoot(data) == data, "active root: the data folder");
  Check(!sl::Delete(data, "s2", BinOk, error), "deleting it again refused");
  Check(sl::Create(data, "After", id, error) && id == "s7", "ids are not used again while newer ones exist: " + id);
  fs::create_directories(data / "saves" / "s9");
  Check(sl::Create(data, "After 2", id, error) && id == "s10", "nor a folder that is still there: " + id);
  fs::remove(data / "saves" / "s9");
  sBinned.clear();
  Check(sl::Delete(data, "main", BinOk, error), "the first save deleted " + error);
  Check(sBinned.size() == 3, std::to_string(sBinned.size()) + " folders went to the bin (USA, playthroughs, save_backups)");
  Check(!fs::exists(data / "USA") && !fs::exists(data / "playthroughs") && !fs::exists(data / "save_backups"), "they are gone");
  Check(fs::exists(data / "settings.ini") && fs::exists(data / "recordings" / "play.cards" / "start-card" / "USA" / "Card A" / "x.gci") &&
            fs::exists(data / "saves" / "s3"),
        "settings, recordings and the other saves untouched");
  index = sl::Load(data);
  Check(index.Find("main") != nullptr && index.slots[0].name == "My first" && index.active == "main",
        "the first save is still listed (empty), and active");
  Check(sl::Delete(data, "main", BinOk, error), "deleting the empty first save is fine");

  std::printf("odd index files\n");
  WriteText(sl::IndexFile(data), "{\"version\": 1, \"active\": \"s77\", \"slots\": [{\"id\": \"s3\", \"name\": \"Three\"}]}");
  index = sl::Load(data, &note);
  Check(index.slots.size() == 2 && index.slots[0].id == "main" && index.active == "main",
        "an active save that isn't listed = the first save; the first save always listed");
  WriteText(sl::IndexFile(data), "{\"version\": 1, \"active\": \"s3\", \"slots\": [{\"id\": \"..\\\\..\\\\x\", \"name\": \"Bad\"}, "
                                 "{\"id\": \"s3\"}, {\"id\": \"s3\", \"name\": \"Twice\"}, 5, {\"id\": \"main\", \"name\": \"M\"}]}");
  index = sl::Load(data, &note);
  Check(index.slots.size() == 2 && index.slots[0].id == "main" && index.slots[0].name == "M" && index.slots[1].id == "s3" &&
            index.slots[1].name == "Save (s3)" && index.active == "s3",
        "ids that aren't ours dropped, a missing name filled in, a repeated id once, the first save first");
  note.clear();
  WriteText(sl::IndexFile(data), "{\"version\": 1, \"active\": \"s3\", \"slots\": [{\"id\"");
  index = sl::Load(data, &note);
  Check(!note.empty(), "a damaged index is said: " + note);
  Check(index.active == "main" && index.Find("s3") != nullptr && index.Find("s6") != nullptr && index.Find("s7") != nullptr &&
            index.Find("s10") != nullptr && index.slots.size() == 6,
        "every save folder found is listed (" + std::to_string(index.slots.size()) + " saves), the first one active");
  Check(index.Find("s3")->name == "Save 3 (found again)", "named \"" + index.Find("s3")->name + "\"");
  Check(sl::Rename(data, "s3", "Three again", error), "a change writes a good index again");
  note.clear();
  index = sl::Load(data, &note);
  Check(note.empty() && index.Find("s3")->name == "Three again" && index.slots.size() == 6, "read back without complaint");
  Check(fs::exists(Plus(sl::IndexFile(data), ".damaged")), "the damaged file was kept as slots.json.damaged");
  WriteText(sl::IndexFile(data), "[1, 2]");
  index = sl::Load(data, &note);
  Check(!note.empty() && index.slots.size() == 6, "an index that is no list of saves: the same");

  if (own) fs::remove_all(work, ec);
  std::printf(sFailures == 0 ? "save_slots_test: all passed\n" : "save_slots_test: %d FAILED\n", sFailures);
  return sFailures == 0 ? 0 : 1;
}
