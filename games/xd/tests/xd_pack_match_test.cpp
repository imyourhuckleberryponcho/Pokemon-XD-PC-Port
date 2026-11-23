#include "pack_match.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace pm = xd::packmatch;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
void gen4(const fs::path& assets, const char* name, int cries, const std::string& toc) {
  std::string tables = "format 1\n";
  for (int i = 0; i < cries; i++) tables += "row snd_file " + std::to_string(516 + i) + " 1 -1 0:4:1966\nwave " + std::to_string(i) + " 00\n";
  put(assets / name / "gen4.json", "{\"id\": \"x\", \"version\": 2}");
  put(assets / name / "tables.txt", tables);
  put(assets / name / "files" / "GSfsys.toc", toc);
}
void gen57(const fs::path& assets, int firstRow, bool own, const std::string& tocHash) {
  std::string json = "{\n \"id\": \"ultra-sun-moon-gen57\",\n \"version\": 1";
  if (own) json += ",\n \"cry_archive\": \"own\",\n \"cry_rows_reserved\": 106,\n \"gen4_toc\": \"" + tocHash + "\"";
  json += "\n}";
  put(assets / "usum_gen57" / "gen57.json", json);
  put(assets / "usum_gen57" / "tables.txt", "format 1\nrow snd_file " + std::to_string(firstRow) + " 1 -1 0:4:1\n");
}
}

int main() {
  const fs::path root = fs::temp_directory_path() / "xd_pack_match_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const std::string toc = "archive list";
  const std::string tocHash = pm::Fnv64Hex(toc.data(), toc.size());
  check(pm::Fnv64Hex("", 0) == "cbf29ce484222325", "FNV-1a of nothing is the offset basis");
  check(pm::Fnv64Hex("a", 1) == "af63dc4c8601ec8c", "FNV-1a of \"a\"");

  {
    const fs::path a = root / "1";
    gen4(a, "pbr_gen4", 106, toc), gen4(a, "usum_gen4", 0, toc), gen57(a, 516, false, "");
    check(pm::Gen4Folder(a, false) == a / "pbr_gen4", "no Gen 5-7 choice: Battle Revolution's pack");
    check(pm::Gen4Folder(a, true) == a / "usum_gen4", "old pack made on the Ultra Sun pack: that pack is used with it");
    check(pm::CheckGen57(a) == pm::Problem::LegacyOtherGen4, "...and it still works, with a hint to make it again");
    check(pm::Works(pm::CheckGen57(a)), "works");
  }
  {
    const fs::path a = root / "2";
    gen4(a, "pbr_gen4", 106, toc), gen4(a, "usum_gen4", 0, toc), gen57(a, 622, false, "");
    check(pm::Gen4Folder(a, true) == a / "pbr_gen4", "old pack made on the Battle Revolution pack: that pack");
    check(pm::CheckGen57(a) == pm::Problem::None, "fits as it is");
  }
  {
    const fs::path a = root / "3";
    gen4(a, "pbr_gen4", 106, toc), gen57(a, 516, false, "");
    check(pm::CheckGen57(a) == pm::Problem::LegacyGen4Gone, "old pack made on the Ultra Sun pack, only Battle Revolution's installed: refused with a reason");
    const fs::path b = root / "4";
    gen4(b, "usum_gen4", 0, toc), gen57(b, 622, false, "");
    check(pm::CheckGen57(b) == pm::Problem::LegacyGen4Gone, "old pack made on the Battle Revolution pack, only the Ultra Sun pack installed: refused");
  }
  {
    const fs::path a = root / "5";
    gen4(a, "usum_gen4", 0, toc), gen57(a, 516, false, "");
    check(pm::Gen4Folder(a, true) == a / "usum_gen4" && pm::CheckGen57(a) == pm::Problem::None, "Ultra Sun only, old pack: unchanged");
  }
  {
    for (int combo = 0; combo < 3; combo++) {
      const fs::path a = root / ("n" + std::to_string(combo));
      if (combo != 1) gen4(a, "pbr_gen4", 106, toc);
      if (combo != 0) gen4(a, "usum_gen4", 0, toc);
      gen57(a, 622, true, tocHash);
      check(pm::CheckGen57(a) == pm::Problem::None, "new pack, combination " + std::to_string(combo));
    }
    const fs::path a = root / "n3";
    gen4(a, "pbr_gen4", 106, "another archive list"), gen57(a, 622, true, tocHash);
    check(pm::CheckGen57(a) == pm::Problem::TocDiffers, "new pack, Gen 4 pack with another archive list: refused");
    const fs::path b = root / "n4";
    gen4(b, "pbr_gen4", 107, toc), gen57(b, 622, true, tocHash);
    check(pm::CheckGen57(b) == pm::Problem::TooManyCries, "new pack, Gen 4 pack with more cries than the room: refused");
  }
  fs::remove_all(root, ec);
  std::printf(sOk ? "pack match test: all passed\n" : "pack match test: FAILED\n");
  return sOk ? 0 : 1;
}
