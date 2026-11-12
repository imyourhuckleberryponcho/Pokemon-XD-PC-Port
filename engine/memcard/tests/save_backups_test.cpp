#include <save_backups.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace bk = engine::memcard::backups;

namespace {
int sFailures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
  if (!ok) sFailures++;
}

std::vector<uint8_t> Bytes(uint8_t fill, size_t size = 0x2040) { return std::vector<uint8_t>(size, fill); }

std::vector<uint8_t> ReadFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path& p, const std::vector<uint8_t>& b) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

size_t CountFolders(const fs::path& p) {
  size_t n = 0;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(p, ec)) n += e.is_directory(ec) ? 1 : 0;
  return n;
}
}

int main(int argc, char** argv) {
  const bool own = argc < 2;
  const fs::path work = own ? fs::current_path() / "save_backups_test.tmp" : fs::path(argv[1]);
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work);
  const fs::path backups = bk::FolderFor(work);
  const fs::path cardA = backups / "Card A";
  const std::string file = "01-GXXE-Test.gci";
  std::tm tm{};
  tm.tm_year = 2020 - 1900, tm.tm_mon = 10, tm.tm_mday = 8, tm.tm_hour = 12, tm.tm_isdst = -1;
  const std::time_t t0 = std::mktime(&tm);

  fs::create_directories(backups);
  const fs::path loose = backups / "01-GXXE-PokemonXD.20261101-120000.gci";
  WriteFile(loose, Bytes(0xEE));

  std::printf("only when changed\n");
  fs::path first = bk::Add(backups, "Card A", file, Bytes(1), t0, bk::kKeep, nullptr);
  Check(!first.empty() && first.filename() == "2020-11-08_12-00-00", "first backup: " + first.filename().string());
  Check(ReadFile(first / file) == Bytes(1), "its bytes");
  Check(bk::Add(backups, "Card A", file, Bytes(1), t0 + 1, bk::kKeep, nullptr).empty(), "same bytes again: no backup");
  const fs::path same = bk::Add(backups, "Card A", file, Bytes(2), t0, bk::kKeep, nullptr);
  Check(same.filename() == "2020-11-08_12-00-00_2", "changed, same second: " + same.filename().string());
  Check(bk::Add(backups, "Card A", file, Bytes(2), t0 + 2, bk::kKeep, nullptr).empty(), "compared with the newest");
  Check(!bk::Add(backups, "Card A", file, Bytes(1), t0 + 3, bk::kKeep, nullptr).empty(),
        "back to older bytes: a new backup (only the newest counts)");

  std::printf("rotation\n");
  for (int i = 0; i < 15; i++) {
    bk::Add(backups, "Card A", file, Bytes(static_cast<uint8_t>(10 + i)), t0 + 10 + i, bk::kKeep, nullptr);
  }
  Check(CountFolders(cardA) == bk::kKeep, "kept " + std::to_string(CountFolders(cardA)) + " (expected 10)");
  std::vector<bk::Backup> list = bk::List(backups);
  Check(list.size() == bk::kKeep, "listed " + std::to_string(list.size()));
  Check(!list.empty() && list.front().name == "2020-11-08_12-00-24", "newest first: " + (list.empty() ? "" : list.front().name));
  Check(!list.empty() && list.back().name == "2020-11-08_12-00-15", "oldest kept: " + (list.empty() ? "" : list.back().name));
  Check(!list.empty() && list.front().when == "2020-11-08 12:00:24" && list.front().card == "Card A" &&
            list.front().files.size() == 1 && list.front().files[0].size == 0x2040,
        "date, card, file and size");
  Check(fs::exists(loose), "xd_save_convert's loose copy untouched");
  for (int n = 0; n < 11; n++) bk::Add(backups, "Card B", file, Bytes(static_cast<uint8_t>(100 + n)), t0, 20, nullptr);
  list = bk::List(backups);
  Check(!list.empty() && list.front().name == "2020-11-08_12-00-24", "Card B's _2.._11 are older than Card A's newest");
  bool b11first = false;
  for (const auto& b : list) {
    if (b.card == "Card B") { b11first = b.name == "2020-11-08_12-00-00_11"; break; }
  }
  Check(b11first, "within one second, _11 is the newest");
  fs::remove_all(backups / "Card B");

  std::printf("restore\n");
  const fs::path card = work / "USA" / "Card A";
  fs::create_directories(card);
  WriteFile(card / file, Bytes(0x55));
  list = bk::List(backups);
  const bk::Backup oldest = list.back();
  const std::vector<uint8_t> want = ReadFile(oldest.folder / file);
  std::string error;
  Check(bk::Restore(oldest, file, card, backups, bk::kKeep, error), "restored the oldest " + oldest.name + " " + error);
  Check(ReadFile(card / file) == want, "the card holds the backup's bytes");
  Check(!fs::exists(card / (file + ".restoring")), "no temporary file left");
  list = bk::List(backups);
  Check(!list.empty() && ReadFile(list.front().folder / file) == Bytes(0x55), "the replaced save was backed up first");
  Check(list.size() == bk::kKeep, "still 10");
  const bk::Backup newest = list.front();
  Check(bk::Restore(newest, file, card, backups, bk::kKeep, error), "restore the 0x55 one back " + error);
  Check(ReadFile(card / file) == Bytes(0x55), "the card holds 0x55 again");
  Check(ReadFile(bk::List(backups).front().folder / file) == want, "and what it replaced is the newest backup");
  fs::remove(card / file);
  Check(bk::Restore(bk::List(backups).front(), file, card, backups, bk::kKeep, error), "restore into an empty card");
  Check(ReadFile(card / file) == want, "file there");
  bk::Backup broken = bk::List(backups).front();
  broken.folder = broken.folder.parent_path() / "missing";
  Check(!bk::Restore(broken, file, card, backups, bk::kKeep, error) && !error.empty(), "missing backup refused: " + error);
  Check(ReadFile(card / file) == want, "card unchanged");

  Check(bk::SizeText(0x58040) == "352 KB", "size text " + bk::SizeText(0x58040));
  Check(bk::CardTimeText(0) == "" && bk::CardTimeText(86400 + 3600) == "2000-01-02 01:00", "card time text");

  if (own) fs::remove_all(work, ec);
  std::printf(sFailures == 0 ? "save_backups_test: all passed\n" : "save_backups_test: %d FAILED\n", sFailures);
  return sFailures == 0 ? 0 : 1;
}
