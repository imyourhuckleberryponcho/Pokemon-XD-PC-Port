#include <gs_formats.h>
#include <mod_layout.h>
#include <mods.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
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

struct FakeDisc {
  std::map<std::string, std::vector<uint8_t>> files;
  DiscView View(const std::string& gameId) const {
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

std::string manifest(const std::string& id, const std::string& games, const std::string& extra = "") {
  return "{\n  \"id\": \"" + id + "\",\n  \"name\": \"Mod " + id + "\",\n  \"version\": \"1.0\",\n  \"game\": " + games +
         (extra.empty() ? "" : ",\n  " + extra) + "\n}\n";
}

const Replacement* find(const LoadResult& r, const std::string& discPath) {
  for (const Replacement& f : r.files)
    if (f.discPath == discPath) return &f;
  return nullptr;
}

std::string contents(const Replacement* r) { return r == nullptr ? "<none>" : read_file(r->file); }

bool has_message(const LoadResult& r, Message::Level level, const std::string& part) {
  for (const Message& m : r.messages)
    if (m.level == level && m.text.find(part) != std::string::npos) return true;
  return false;
}

std::vector<std::string> script_names(const LoadResult& r) {
  std::vector<std::string> out;
  for (const ScriptFile& s : r.scripts) out.push_back(s.modId + ":" + s.name + "=" + s.text);
  return out;
}

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  if (v.size() < at + 4) v.resize(at + 4);
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}
uint32_t get32(const std::vector<uint8_t>& v, size_t at) {
  return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3];
}
size_t align_to(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

std::vector<uint8_t> make_texture_like_fsys(const std::vector<std::pair<std::string, std::string>>& entries) {
  const size_t n = entries.size();
  std::vector<uint8_t> a(0x60, 0);
  std::memcpy(a.data(), "FSYS", 4);
  put32(a, 0x08, 0x5C0);
  put32(a, 0x0C, uint32_t(n));
  put32(a, 0x18, 0x40);
  const size_t table = 0x60, names = table + align_to(4 * n, 16);
  std::string pool;
  std::vector<size_t> nameAt;
  for (const auto& e : entries) {
    nameAt.push_back(names + pool.size());
    pool += e.first + std::string(1, '\0');
  }
  const size_t hdr = names + align_to(pool.size(), 16);
  const size_t dataStart = align_to(hdr + 0x70 * n, 32);
  put32(a, 0x40, uint32_t(table));
  put32(a, 0x44, uint32_t(names));
  put32(a, 0x48, uint32_t(dataStart));
  put32(a, 0x1C, uint32_t(dataStart));
  a.resize(dataStart, 0);
  std::memcpy(a.data() + names, pool.data(), pool.size());
  size_t pos = dataStart;
  for (size_t i = 0; i < n; i++) {
    std::vector<uint8_t> raw(entries[i].second.begin(), entries[i].second.end());
    uint32_t packed = 0;
    uint8_t* p = Lzss_Pack(raw.data(), uint32_t(raw.size()), &packed);
    pos = align_to(pos, 32);
    a.resize(pos + packed, 0);
    std::memcpy(a.data() + pos, p, packed);
    std::free(p);
    put32(a, table + 4 * i, uint32_t(hdr + 0x70 * i));
    const size_t h = hdr + 0x70 * i;
    put32(a, h, (0x0D00u + uint32_t(i)) << 16 | 0x1200);
    put32(a, h + 4, uint32_t(pos));
    put32(a, h + 8, uint32_t(raw.size()));
    put32(a, h + 0x0C, 0x80000000u);
    put32(a, h + 0x14, packed);
    put32(a, h + 0x20, 9);
    put32(a, h + 0x24, uint32_t(nameAt[i]));
    pos += packed;
  }
  a.resize(align_to(a.size(), 32) + 0x20, 0);
  std::memcpy(a.data() + a.size() - 4, "FSYS", 4);
  put32(a, 0x20, uint32_t(a.size()));
  return a;
}

std::string entry_text(const std::vector<uint8_t>& archive, uint32_t index) {
  FsysArchive f;
  if (!Fsys_Open(archive.data(), archive.size(), &f)) return "<not fsys>";
  uint32_t n = 0;
  uint8_t* p = Fsys_Extract(&f, index, &n);
  if (p == nullptr) return "<missing>";
  std::string s(reinterpret_cast<char*>(p), n);
  std::free(p);
  return s;
}

}

int main(int argc, char** argv) {
  const fs::path root = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path() / "xd_mod_layout_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);

  check(ValidGameFolderName("GXX") && ValidGameFolderName("GXXE") && ValidGameFolderName("GXXE01") && ValidGameFolderName("GC6"),
        "folder names: 3, 4 and 6 letters/digits are fine");
  check(!ValidGameFolderName("GX") && !ValidGameFolderName("GXXE0") && !ValidGameFolderName("gxx") && !ValidGameFolderName("GX-E01"),
        "folder names: 2, 5 characters, lower case, signs are not");
  check(GameCodeMatches("GXX", "GXXE01") && GameCodeMatches("gxxe", "GXXE01") && GameCodeMatches("GXXE01", "GXXE01") && !GameCodeMatches("GXXJ", "GXXE01") &&
            !GameCodeMatches("GXXE01", "GXX") && !GameCodeMatches("GC6", "GXXE01"),
        "game codes: a start of the ID matches, other games and longer codes do not");
  {
    std::string folder, rest;
    check(SplitGameFolder("games/GXXE01/data/a.json", folder, rest) && folder == "GXXE01" && rest == "data/a.json", "split: games/<id>/rest");
    check(SplitGameFolder("Games/gxx/files", folder, rest) && folder == "GXX" && rest == "files", "split: letter case of the folder is ignored");
    check(!SplitGameFolder("data/games/a.json", folder, rest) && !SplitGameFolder("games", folder, rest), "split: only a path that starts with games/<name>");
    check(StripGameFolder("games/GXX/scripts/a.lua") == "scripts/a.lua" && StripGameFolder("scripts/a.lua") == "scripts/a.lua", "strip: the games/<id>/ part goes");
  }

  const fs::path mods = root / "mods";
  const fs::path lay = mods / "lay";
  write_file(lay / "mod.json", manifest("lay", "[\"GXXE01\", \"GXXJ01\", \"GC6E01\"]", "\"scripts\": true"));
  write_file(lay / "files" / "shared.bin", "shared");
  write_file(lay / "files" / "both.bin", "shared both");
  write_file(lay / "games" / "GXX" / "files" / "both.bin", "xd family");
  write_file(lay / "games" / "GXXE01" / "files" / "both.bin", "xd exact");
  write_file(lay / "games" / "GC6E01" / "files" / "colo.bin", "colosseum only");
  write_file(lay / "scripts" / "a.lua", "-- shared a\n");
  write_file(lay / "scripts" / "c.lua", "-- shared c\n");
  write_file(lay / "games" / "GXX" / "scripts" / "b.lua", "-- xd b\n");
  write_file(lay / "games" / "GXXE01" / "scripts" / "a.lua", "-- xd exact a\n");
  write_file(lay / "textures" / "pic.png", "x");
  write_file(lay / "games" / "GXXE01" / "textures" / "pic2.png", "y");

  {
    const std::vector<fs::path> roots = ContentRoots(lay, "GXXE01");
    check(roots.size() == 3 && roots[0] == lay && roots[1].filename() == "GXX" && roots[2].filename() == "GXXE01", "roots for XD USA: shared, GXX, GXXE01 (least specific first)");
    check(ContentRoots(lay, "GXXJ01").size() == 2 && ContentRoots(lay, "GC6E01").size() == 2 && ContentRoots(lay, "GXXP01").size() == 2, "roots: another region gets only the folders that start its ID");
    check(ContentRoots(lay, "").size() == 4, "roots without a game: the shared part and every games/ folder");
    const std::vector<LayoutFile> files = LayoutFiles(lay, "GXXE01", "files");
    std::map<std::string, std::string> byName;
    for (const LayoutFile& f : files) byName[f.rel] = read_file(f.path) + "@" + f.from;
    check(files.size() == 2 && byName["both.bin"] == "xd exact@GXXE01" && byName["shared.bin"] == "shared@", "merged files: the most specific folder wins, other names add up");
    const std::vector<LayoutFile> j = LayoutFiles(lay, "GXXJ01", "files");
    std::map<std::string, std::string> jn;
    for (const LayoutFile& f : j) jn[f.rel] = read_file(f.path);
    check(j.size() == 2 && jn["both.bin"] == "xd family", "merged files, Japan: the GXX folder's file wins over the shared one");
    const std::vector<LayoutFile> sc = LayoutFiles(lay, "GXXE01", "scripts", false);
    check(sc.size() == 3 && sc[0].rel == "c.lua" && sc[1].rel == "b.lua" && sc[2].rel == "a.lua", "merged scripts: shared ones first (a replaced by the game's own), then the game's");
  }

  FakeDisc disc;
  disc.files["both.bin"] = {'o'};
  const std::string iniXd = "[profile default]\nlay = on\n";
  {
    write_file(mods.parent_path() / "list.ini", iniXd);
    LoadOptions o{mods, root / "list.ini", disc.View("GXXE01")};
    const LoadResult r = Load(o);
    check(!r.HasWarnings(), "load XD USA: no warnings");
    check(contents(find(r, "both.bin")) == "xd exact", "load XD USA: the exact folder's file replaces the disc file");
    check(contents(find(r, "shared.bin")) == "shared" && find(r, "colo.bin") == nullptr, "load XD USA: shared file added, Colosseum's folder not read");
    check(script_names(r) == std::vector<std::string>{"lay:scripts/c.lua=-- shared c\n", "lay:scripts/b.lua=-- xd b\n", "lay:scripts/a.lua=-- xd exact a\n"},
          "load XD USA: scripts = shared c, family b, exact a (a replaced the shared a)");
  }
  {
    LoadOptions o{mods, root / "list.ini", disc.View("GXXJ01")};
    const LoadResult r = Load(o);
    check(contents(find(r, "both.bin")) == "xd family" && script_names(r).size() == 3, "load XD Japan: the GXX family folder applies, GXXE01's does not");
  }
  {
    LoadOptions o{mods, root / "list.ini", disc.View("GC6E01")};
    const LoadResult r = Load(o);
    check(contents(find(r, "both.bin")) == "shared both" && contents(find(r, "colo.bin")) == "colosseum only", "load Colosseum: the shared file, and its own folder's file");
    check(script_names(r).size() == 2, "load Colosseum: only the shared scripts");
  }
  {
    LoadOptions o{mods, root / "list.ini", disc.View("GXXP01")};
    const LoadResult r = Load(o);
    check(has_message(r, Message::Level::Problem, "Made for") && r.active.empty(), "a game the mod does not name: not used");
  }

  write_file(mods / "legacy" / "mod.json", manifest("legacy", "\"GXXE01\""));
  write_file(mods / "legacy" / "files" / "both.bin", "legacy");
  write_file(mods / "short" / "mod.json", manifest("short", "[\"GXX\"]"));
  write_file(mods / "short" / "files" / "short.bin", "short");
  write_file(root / "list2.ini", "[profile default]\nlegacy = on\nshort = on\n");
  {
    LoadOptions o{mods, root / "list2.ini", disc.View("GXXE01")};
    const LoadResult r = Load(o);
    check(contents(find(r, "both.bin")) == "legacy" && contents(find(r, "short.bin")) == "short" && !r.HasWarnings(), "old single-game mod and a \"GXX\" entry: loaded as before");
    LoadOptions p{mods, root / "list2.ini", disc.View("GXXJ01")};
    const LoadResult rj = Load(p);
    check(find(rj, "short.bin") != nullptr && find(rj, "both.bin") == nullptr && has_message(rj, Message::Level::Problem, "legacy"),
          "\"GXX\" works for Japan too, \"GXXE01\" only for the USA disc");
  }

  check(ValidGameFolderName("XD") && ValidGameFolderName("COLOSSEUM") && !ValidGameFolderName("XDXXX"), "folder names: xd and colosseum are names too");
  check(GameCodeMatches("xd", "GXXE01") && GameCodeMatches("XD", "GXXJ01") && GameCodeMatches("colosseum", "GC6E01") && !GameCodeMatches("xd", "GC6E01") &&
            !GameCodeMatches("colosseum", "GXXE01"),
        "game codes: xd = every XD disc, colosseum = every Colosseum disc");
  {
    std::string folder, rest;
    check(SplitGameFolder("games/xd/scripts/a.luau", folder, rest) && folder == "XD" && rest == "scripts/a.luau", "split: games/xd/...");
  }
  const fs::path nm = mods / "named";
  write_file(nm / "mod.json", manifest("named", "[\"xd\", \"colosseum\"]", "\"scripts\": true"));
  write_file(nm / "files" / "both.bin", "shared both");
  write_file(nm / "games" / "xd" / "files" / "xd.bin", "xd only");
  write_file(nm / "games" / "xd" / "files" / "both.bin", "xd both");
  write_file(nm / "games" / "colosseum" / "files" / "colo.bin", "colo only");
  write_file(nm / "scripts" / "shared.luau", "-- shared\n");
  write_file(nm / "games" / "xd" / "scripts" / "own.luau", "-- xd own\n");
  write_file(root / "list3.ini", "[profile default]\nnamed = on\n");
  {
    check(ContentRoots(nm, "GXXE01").size() == 2 && ContentRoots(nm, "GXXE01")[1].filename() == "xd" && ContentRoots(nm, "GC6E01").size() == 2, "roots: games/xd for XD discs, games/colosseum for Colosseum discs");
    LoadOptions o{mods, root / "list3.ini", disc.View("GXXE01")};
    const LoadResult r = Load(o);
    check(!r.HasWarnings() && contents(find(r, "xd.bin")) == "xd only" && find(r, "colo.bin") == nullptr && contents(find(r, "both.bin")) == "xd both",
          "load XD with games/xd: its folder's files, the shared ones, nothing of games/colosseum");
    check(script_names(r) == std::vector<std::string>{"named:scripts/shared.luau=-- shared\n", "named:scripts/own.luau=-- xd own\n"}, "load XD: .luau scripts, shared first then games/xd's");
    LoadOptions c{mods, root / "list3.ini", disc.View("GC6E01")};
    const LoadResult rc = Load(c);
    check(contents(find(rc, "colo.bin")) == "colo only" && find(rc, "xd.bin") == nullptr && contents(find(rc, "both.bin")) == "shared both" && script_names(rc).size() == 1,
          "load Colosseum with the same mod: its folder, the shared parts, nothing of games/xd");
    Manifest nmM;
    std::string nmErr;
    ParseManifest(read_file(nm / "mod.json"), nmM, nmErr);
    const std::vector<GameSupport> sup = SupportOf(nm, nmM);
    check(sup.size() == 2 && sup[0].level == GameSupport::Level::Full && sup[1].level == GameSupport::Level::Full && SupportText(sup, false) == "XD; Colosseum",
          "support: XD and Colosseum both have their folders: " + SupportText(sup, false));
  }

  {
    check(MakeTarget("GXXE01", "") == "GXXE01" && MakeTarget("GXXE01", "xd") == "GXXE01" && MakeTarget("GXXE01", "xg") == "GXXE01#xg", "target: XD stays the bare ID, XG gets the edition");
    check(GameCodeMatches("xg", "GXXE01#xg") && !GameCodeMatches("xg", "GXXE01") && !GameCodeMatches("xd", "GXXE01#xg") && !GameCodeMatches("GXXE01", "GXXE01#xg") && GameCodeMatches("GXX", "GXXP01"),
          "codes: xg only matches XG; xd, GXX and a bare ID only match the plain game");
    check(ValidGameFolderName("XG") && TargetName("GXXE01#xg") == "XG" && TargetName("GXXE01") == "XD" && GameNameOf("xg", true) == "XG", "names: XD and XG");
    const fs::path xo = mods / "xdonly", both = mods / "bothxg", bothSame = mods / "bothxd", xgo = mods / "xgonly";
    write_file(xo / "mod.json", manifest("xdonly", "[\"xd\"]"));
    write_file(xo / "games" / "xd" / "files" / "a.bin", "xdonly a");
    write_file(both / "mod.json", manifest("bothxg", "[\"xd\", \"xg\"]"));
    write_file(both / "games" / "xd" / "files" / "x.bin", "xd x");
    write_file(both / "games" / "xg" / "files" / "y.bin", "xg y");
    write_file(bothSame / "mod.json", manifest("bothxd", "[\"xd\", \"xg\"]"));
    write_file(bothSame / "games" / "xd" / "files" / "z.bin", "xd z");
    write_file(xgo / "mod.json", manifest("xgonly", "[\"xg\"]"));
    write_file(xgo / "games" / "xd" / "files" / "q.bin", "xd q");
    write_file(xgo / "games" / "xg" / "files" / "r.bin", "xg r");
    write_file(root / "list4.ini", "[profile default]\nxdonly = on\nbothxg = on\nbothxd = on\nxgonly = on\n");
    LoadOptions xd{mods, root / "list4.ini", disc.View("GXXE01")};
    const LoadResult rxd = Load(xd);
    check(contents(find(rxd, "a.bin")) == "xdonly a" && contents(find(rxd, "x.bin")) == "xd x" && find(rxd, "y.bin") == nullptr && contents(find(rxd, "z.bin")) == "xd z" &&
              find(rxd, "q.bin") == nullptr && find(rxd, "r.bin") == nullptr,
          "load XD: the XD-only mod and the two-edition mods read games/xd, the XG-only mod is refused");
    LoadOptions xg{mods, root / "list4.ini", disc.View("GXXE01#xg")};
    const LoadResult rxg = Load(xg);
    check(find(rxg, "a.bin") == nullptr, "load XG: an XD-only mod is not loaded");
    bool said = false;
    for (const Message& m : rxg.messages) said = said || (m.level == Message::Level::Problem && m.text.find("Made for XD, not XG") != std::string::npos && m.text.find("Mod xdonly") != std::string::npos);
    check(said, "load XG: the refusal says \"Made for XD, not XG\"");
    check(find(rxg, "y.bin") != nullptr && contents(find(rxg, "y.bin")) == "xg y" && find(rxg, "x.bin") == nullptr, "load XG: a mod with both folders reads games/xg, not games/xd");
    check(contents(find(rxg, "z.bin")) == "xd z", "load XG: [xd, xg] with no games/xg reads games/xd (the author says it is safe)");
    check(contents(find(rxg, "r.bin")) == "xg r" && find(rxg, "q.bin") == nullptr, "load XG: an xg-only mod reads games/xg and never games/xd");
    Manifest mx;
    std::string er;
    ParseManifest(read_file(xo / "mod.json"), mx, er);
    check(GameTag(mx) == "XD" && !SupportsGame(mx, "GXXE01#xg") && SupportsGame(mx, "GXXE01"), "tag XD; an XD mod does not support the XG target");
    ParseManifest(read_file(both / "mod.json"), mx, er);
    check(GameTag(mx) == "XD + XG" && SupportsGame(mx, "GXXE01#xg"), "tag XD + XG");
    ParseManifest(read_file(xgo / "mod.json"), mx, er);
    check(GameTag(mx) == "XG" && !SupportsGame(mx, "GXXE01"), "tag XG; an XG mod does not load on XD");
    const std::vector<GameSupport> sx = SupportOf(bothSame, mx = [&] { Manifest t; ParseManifest(read_file(bothSame / "mod.json"), t, er); return t; }());
    check(sx.size() == 2 && sx[1].level == GameSupport::Level::Full && sx[1].folders.size() == 1 && sx[1].folders[0] == "XD", "support: xg without its folder uses the xd folder");
    Manifest flat;
    ParseManifest(manifest("flatx", "[\"xd\"]"), flat, er);
    write_file(mods / "flatx" / "mod.json", manifest("flatx", "[\"xd\"]"));
    check(!SupportsGame(flat, "GXXE01#xg"), "a flat XD-only mod is not for XG");
  }

  {
    Manifest lm;
    std::string lerr;
    ParseManifest(read_file(lay / "mod.json"), lm, lerr);
    const std::vector<GameSupport> s = SupportOf(lay, lm);
    check(s.size() == 3 && s[0].level == GameSupport::Level::Full && s[1].level == GameSupport::Level::Full && s[2].level == GameSupport::Level::Full,
          "support: each game with a games/ folder works fully");
    write_file(mods / "half" / "mod.json", manifest("half", "[\"GXXE01\", \"GC6E01\"]"));
    write_file(mods / "half" / "textures" / "x.png", "x");
    write_file(mods / "half" / "games" / "GXXE01" / "data" / "a.json", "{}");
    Manifest hm;
    std::string err;
    check(ParseManifest(read_file(mods / "half" / "mod.json"), hm, err), "support: manifest parses");
    const std::vector<GameSupport> h = SupportOf(mods / "half", hm);
    check(h.size() == 2 && h[0].level == GameSupport::Level::Full && h[1].level == GameSupport::Level::SharedOnly, "support: a game with no folder gets the shared parts only");
    check(SupportText(h, false) == "XD; Colosseum: shared parts only", "support text: \"XD; Colosseum: shared parts only\" (got \"" + SupportText(h, false) + "\")");
    write_file(mods / "bare" / "mod.json", manifest("bare", "[\"GXXE01\", \"GC6E01\"]"));
    write_file(mods / "bare" / "games" / "GXXE01" / "data" / "a.json", "{}");
    Manifest bm;
    ParseManifest(read_file(mods / "bare" / "mod.json"), bm, err);
    const std::vector<GameSupport> b = SupportOf(mods / "bare", bm);
    check(b.size() == 2 && b[1].level == GameSupport::Level::Nothing, "support: no folder and no shared parts = nothing there");
    ParseManifest(read_file(mods / "legacy" / "mod.json"), bm, err);
    check(SupportOf(mods / "legacy", bm)[0].level == GameSupport::Level::Full && SupportText(SupportOf(mods / "legacy", bm), false) == "XD", "support: an old-layout mod works fully with the game it names");
  }

  {
    fs::path out;
    check(ResolveLayoutFile(lay, "GXXE01", "textures/pic.png", out) && out == lay / "textures" / "pic.png", "resolve: a shared file");
    check(ResolveLayoutFile(lay, "GXXE01", "textures/pic2.png", out) && out.parent_path().parent_path().filename() == "GXXE01", "resolve: the game's folder's file");
    check(!ResolveLayoutFile(lay, "GC6E01", "textures/pic2.png", out), "resolve: another game's folder is not looked in");
    check(!ResolveLayoutFile(lay, "GXXE01", "../mod.json", out) && !ResolveLayoutFile(lay, "GXXE01", "C:/x.png", out) && !ResolveLayoutFile(lay, "GXXE01", "/x.png", out),
          "resolve: a path that leaves the mod is refused");
  }

  {
    std::vector<Message> messages;
    const std::vector<ModFolder> found = FindMods(mods, messages);
    const ModFolder* l = nullptr;
    for (const ModFolder& f : found)
      if (f.manifest.id == "lay") l = &f;
    check(l != nullptr, "found the layered mod");
    if (l != nullptr) {
      const ModChanges xd = ChangesOf(*l, "GXXE01"), colo = ChangesOf(*l, "GC6E01");
      check(xd.files.size() == 2 && colo.files.size() == 3, "changes: the files the mod replaces for each game");
      ModList::Profile p;
      p.mods = {{"lay", true}};
      const Review review = ReviewList(p, found, "GC6E01");
      check(review.rows.size() == 1 && review.rows[0].Usable(), "review: usable for Colosseum too");
      const Review none = ReviewList(p, found, "GXXP01");
      check(none.rows.size() == 1 && !none.rows[0].Usable(), "review: not usable for a game it does not name");
    }
  }

  {
    const std::vector<uint8_t> src = make_texture_like_fsys({{"face001", "first picture bytes first picture bytes"}, {"face002", "second"}, {"face003", "third third third third"}});
    const std::string a = "new entry A, with some contents that repeat repeat repeat";
    const std::string b = "B";
    const FsysAddition add[2] = {{0x0E00, "modface1", reinterpret_cast<const uint8_t*>(a.data()), uint32_t(a.size()), 1},
                                 {0x0E01, "modface2", reinterpret_cast<const uint8_t*>(b.data()), uint32_t(b.size()), 0}};
    size_t size = 0;
    uint8_t* built = Fsys_RebuildAdded(src.data(), src.size(), add, 2, &size);
    check(built != nullptr, "fsys add: the archive is made");
    if (built != nullptr) {
      const std::vector<uint8_t> out(built, built + size);
      std::free(built);
      FsysArchive f;
      check(Fsys_Open(out.data(), out.size(), &f) && f.count == 5, "fsys add: five entries");
      check(get32(out, 0x20) == out.size() && std::memcmp(out.data() + out.size() - 4, "FSYS", 4) == 0, "fsys add: the size word and the closing mark");
      check(entry_text(out, 0) == "first picture bytes first picture bytes" && entry_text(out, 1) == "second" && entry_text(out, 2) == "third third third third",
            "fsys add: the old entries are unchanged");
      check(entry_text(out, 3) == a && entry_text(out, 4) == b, "fsys add: the new entries' contents");
      FsysEntry e;
      check(Fsys_Entry(&f, 3, &e) && e.id == 0x0E00 && std::strcmp(e.name, "modface1") == 0 && e.type == 9 && e.packed && e.size == a.size(), "fsys add: id, name, type and size of the new entry");
      check(Fsys_Entry(&f, 0, &e) && e.id == 0x0D00 && std::strcmp(e.name, "face001") == 0 && Fsys_Find(&f, "face003") == 2, "fsys add: old names and ids still found");
      check(get32(out, 0x1C) % 32 == 0 && get32(out, 0x1C) == get32(out, 0x48) && f.entryTable == 0x60, "fsys add: the data start moved and is still aligned");
      const std::string c = "third addition";
      const FsysAddition again[1] = {{0x0E02, "modface3", reinterpret_cast<const uint8_t*>(c.data()), uint32_t(c.size()), 2}};
      size_t size2 = 0;
      uint8_t* more = Fsys_RebuildAdded(out.data(), out.size(), again, 1, &size2);
      check(more != nullptr && entry_text(std::vector<uint8_t>(more, more + size2), 5) == c && entry_text(std::vector<uint8_t>(more, more + size2), 3) == a,
            "fsys add: it can be extended again");
      std::free(more);
      const std::string used = "x";
      const FsysAddition clash[1] = {{0x0D01, "dup", reinterpret_cast<const uint8_t*>(used.data()), 1, 0}};
      check(Fsys_RebuildAdded(out.data(), out.size(), clash, 1, &size2) == nullptr, "fsys add: an id that is used is refused");
    }
  }

  fs::remove_all(root, ec);
  std::printf(sOk ? "mod_layout_test: all passed\n" : "mod_layout_test: FAILED\n");
  return sOk ? 0 : 1;
}
