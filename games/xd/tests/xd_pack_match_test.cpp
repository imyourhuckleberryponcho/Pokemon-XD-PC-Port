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
  {
    const fs::path a = root / "assets";
    using S = pm::AssetState;
    check(pm::StateOfAsset("gen4_pack", a) == S::NotMade && pm::StateOfAsset("gen57_pack", a) == S::NotMade &&
              pm::StateOfAsset("br_models", a) == S::NotMade && pm::StateOfAsset("usum_models", a) == S::NotMade,
          "assets: nothing made: every asset is not made");
    check(pm::StateOfAsset("nonsense", a) == S::UnknownId, "assets: an unknown id");
    put(a / "pbr" / "mod.json", "{\"id\": \"battle-revolution-models\", \"version\": \"1.0\"}");
    check(pm::StateOfAsset("br_models", a) == S::NotMade, "assets: a models folder without a single model archive is not made");
    fs::create_directories(a / "pbr" / "fsys" / "pkx_absol.fsys");
    check(pm::StateOfAsset("br_models", a) == S::Ready, "assets: Battle Revolution models made");
    put(a / "usum" / "mod.json", "{\"id\": \"battle-revolution-models\"}");
    fs::create_directories(a / "usum" / "fsys" / "pkx_absol.fsys");
    check(pm::StateOfAsset("usum_models", a) == S::NotMade, "assets: another converter's folder is not the Ultra Sun / Ultra Moon models");
    put(a / "usum" / "mod.json", "{\"id\": \"ultra-sun-moon-models\"}");
    check(pm::StateOfAsset("usum_models", a) == S::Ready, "assets: Ultra Sun / Ultra Moon models made");
    gen4(a, "usum_gen4", 0, toc);
    check(pm::StateOfAsset("gen4_pack", a) == S::Ready, "assets: the Gen 4 pack of the dump counts");
    put(a / "usum_gen4" / "gen4.json", "{\"id\": \"x\", \"version\": 1}");
    check(pm::StateOfAsset("gen4_pack", a) == S::OtherVersion, "assets: a Gen 4 pack of another version is out of date");
    put(a / "usum_gen4" / "gen4.json", "{\"id\": \"x\", \"version\": 2}");
    gen57(a, 622, true, tocHash);
    check(pm::StateOfAsset("gen57_pack", a) == S::Ready, "assets: the Gen 5-7 pack made with this Gen 4 pack");
    put(a / "usum_gen4" / "files" / "GSfsys.toc", "another archive list");
    check(pm::StateOfAsset("gen57_pack", a) == S::DoesNotFit, "assets: the Gen 5-7 pack does not fit another Gen 4 pack");
    put(a / "usum_gen4" / "files" / "GSfsys.toc", toc);
    put(a / "usum_gen57" / "gen57.json", "{\"id\": \"x\", \"version\": 9}");
    check(pm::StateOfAsset("gen57_pack", a) == S::OtherVersion, "assets: a Gen 5-7 pack of another version is out of date");
    fs::remove_all(a / "usum_gen4");
    put(a / "usum_gen57" / "gen57.json", "{\"id\": \"x\", \"version\": 1}");
    check(pm::StateOfAsset("gen57_pack", a) == S::DoesNotFit, "assets: the Gen 5-7 pack without any Gen 4 pack does not fit");
  }
  fs::remove_all(root, ec);
  std::printf(sOk ? "pack match test: all passed\n" : "pack match test: FAILED\n");
  return sOk ? 0 : 1;
}
