#include <gs_formats.h>
#include <modcheck.h>
#include <modpack.h>
#include <modsafety.h>

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mc = engine::modcheck;
namespace em = engine::mods;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

const fs::path kFixtures = MODCHECK_FIXTURES;

void write_file(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
void write_bytes(const fs::path& p, const std::vector<uint8_t>& b) { write_file(p, std::string(b.begin(), b.end())); }
std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
std::vector<uint8_t> read_all(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

mc::GameRules MadeUpRules() {
  mc::GameRules r;
  r.gameName = "Made-up game";
  r.scriptHooks = {"battle.damage", "battle.turn_end"};
  r.checkMessageText = [](const std::string& text, std::string& why) {
    if (text.find('{') != std::string::npos && text.find('}') == std::string::npos) {
      why = "a { has no closing }";
      return false;
    }
    return true;
  };
  r.checkEntryFile = [](const std::string& path, const std::vector<uint8_t>& data, mc::Report& report) {
    if (data.size() >= 4 && std::memcmp(data.data(), "BAD!", 4) == 0) report.Problem("entry-bad", path, 0, "made-up bad entry");
  };
  r.extraFolders = {{"rooms", "entry"}, {"objects", "json"}};
  return r;
}

mc::Report Check(const fs::path& folder, const mc::GameRules* rules, const fs::path& modsFolder = {}) {
  mc::Options o;
  o.folder = folder;
  o.rules = rules;
  o.modsFolder = modsFolder;
  return mc::CheckMod(o);
}

const mc::Finding* Find(const mc::Report& r, const std::string& code, const std::string& file = "") {
  for (const mc::Finding& f : r.findings) {
    if (f.code == code && (file.empty() || f.file == file)) return &f;
  }
  return nullptr;
}
std::string Codes(const mc::Report& r) {
  std::string s;
  for (const mc::Finding& f : r.findings) s += (s.empty() ? "" : " ") + f.code;
  return s;
}

std::vector<uint8_t> Png(uint32_t w, uint32_t h, uint32_t extraRaw = 0, bool badCrc = false) {
  std::vector<uint8_t> raw(static_cast<size_t>(h) * (1 + w * 3) + extraRaw, 0);
  uLongf n = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> packed(n);
  compress2(packed.data(), &n, raw.data(), static_cast<uLong>(raw.size()), 9);
  packed.resize(n);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  const auto be = [&](std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; i--) v.push_back(static_cast<uint8_t>(x >> (8 * i))); };
  const auto chunk = [&](const char* type, const std::vector<uint8_t>& body) {
    be(out, static_cast<uint32_t>(body.size()));
    std::vector<uint8_t> t(type, type + 4);
    t.insert(t.end(), body.begin(), body.end());
    out.insert(out.end(), t.begin(), t.end());
    uint32_t crc = mc::Crc32(t.data(), t.size());
    if (badCrc) crc ^= 1;
    be(out, crc);
  };
  std::vector<uint8_t> ihdr;
  be(ihdr, w);
  be(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk("IHDR", ihdr);
  chunk("IDAT", packed);
  chunk("IEND", {});
  return out;
}

struct ZEntry {
  std::string name;
  std::vector<uint8_t> data;
  bool deflate = true;
  uint32_t madeBy = 3 << 8 | 20;
  uint32_t mode = 0100644;
  int64_t declaredSize = -1;
};
std::vector<uint8_t> Zip(const std::vector<ZEntry>& entries) {
  std::vector<uint8_t> z;
  const auto p16 = [&](uint32_t x) { z.push_back(static_cast<uint8_t>(x)); z.push_back(static_cast<uint8_t>(x >> 8)); };
  const auto p32 = [&](uint32_t x) { p16(x & 0xFFFF); p16(x >> 16); };
  struct C { std::string name; uint32_t crc, packed, size, off, madeBy, mode; uint16_t method; };
  std::vector<C> cs;
  for (const ZEntry& e : entries) {
    std::vector<uint8_t> body = e.data;
    uint16_t method = 0;
    if (e.deflate && !e.data.empty()) {
      z_stream s{};
      deflateInit2(&s, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);
      body.resize(deflateBound(&s, static_cast<uLong>(e.data.size())));
      s.next_in = const_cast<uint8_t*>(e.data.data());
      s.avail_in = static_cast<uInt>(e.data.size());
      s.next_out = body.data();
      s.avail_out = static_cast<uInt>(body.size());
      deflate(&s, Z_FINISH);
      body.resize(s.total_out);
      deflateEnd(&s);
      method = 8;
    }
    C c{e.name, mc::Crc32(e.data.data(), e.data.size()), static_cast<uint32_t>(body.size()),
        static_cast<uint32_t>(e.declaredSize >= 0 ? e.declaredSize : static_cast<int64_t>(e.data.size())), static_cast<uint32_t>(z.size()), e.madeBy, e.mode, method};
    p32(0x04034B50); p16(20); p16(0); p16(method); p16(0); p16(0x21); p32(c.crc); p32(c.packed); p32(c.size);
    p16(static_cast<uint32_t>(e.name.size())); p16(0);
    z.insert(z.end(), e.name.begin(), e.name.end());
    z.insert(z.end(), body.begin(), body.end());
    cs.push_back(c);
  }
  const uint32_t cd = static_cast<uint32_t>(z.size());
  for (const C& c : cs) {
    p32(0x02014B50); p16(c.madeBy); p16(20); p16(0); p16(c.method); p16(0); p16(0x21); p32(c.crc); p32(c.packed); p32(c.size);
    p16(static_cast<uint32_t>(c.name.size())); p16(0); p16(0); p16(0); p16(0); p32(c.mode << 16); p32(c.off);
    z.insert(z.end(), c.name.begin(), c.name.end());
  }
  const uint32_t cdSize = static_cast<uint32_t>(z.size()) - cd;
  p32(0x06054B50); p16(0); p16(0); p16(static_cast<uint32_t>(cs.size())); p16(static_cast<uint32_t>(cs.size())); p32(cdSize); p32(cd); p16(0);
  return z;
}

mc::Report ScanOf(const std::vector<ZEntry>& e, const mc::SafetyLimits& limits = {}) {
  mc::Report r;
  mc::ScanZip(Zip(e), limits, r);
  return r;
}

const char* kMod = "{ \"id\": \"t\", \"name\": \"T\", \"version\": \"1.0\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\" }\n";

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  if (v.size() < at + 4) v.resize(at + 4);
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}
size_t align32(size_t v) { return (v + 31) & ~size_t(31); }
std::vector<uint8_t> make_fsys(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::vector<uint8_t> a(0x60, 0);
  std::memcpy(a.data(), "FSYS", 4);
  put32(a, 0x0C, uint32_t(entries.size()));
  put32(a, 0x18, 0x40);
  put32(a, 0x40, 0x60);
  const size_t headers = align32(0x60 + 4 * entries.size());
  a.resize(headers + 0x70 * entries.size(), 0);
  std::vector<uint32_t> nameAt;
  for (const auto& e : entries) {
    nameAt.push_back(uint32_t(a.size()));
    a.insert(a.end(), e.first.begin(), e.first.end());
    a.push_back(0);
  }
  a.resize(align32(a.size()), 0);
  put32(a, 0x1C, uint32_t(a.size()));
  for (size_t i = 0; i < entries.size(); i++) {
    const size_t at = align32(a.size());
    a.resize(at, 0);
    a.insert(a.end(), entries[i].second.begin(), entries[i].second.end());
    const size_t h = headers + 0x70 * i;
    put32(a, 0x60 + 4 * i, uint32_t(h));
    put32(a, h + 0x00, uint32_t(0x100 + i) << 16);
    put32(a, h + 0x04, uint32_t(at));
    put32(a, h + 0x08, uint32_t(entries[i].second.size()));
    put32(a, h + 0x0C, 0);
    put32(a, h + 0x14, uint32_t(entries[i].second.size()));
    put32(a, h + 0x1C, 0);
    put32(a, h + 0x24, nameAt[i]);
  }
  a.resize(align32(a.size()) + 0x20, 0);
  std::memcpy(a.data() + a.size() - 4, "FSYS", 4);
  put32(a, 0x20, uint32_t(a.size()));
  return a;
}

fs::path sTemp;
fs::path Fresh(const std::string& name) {
  const fs::path p = sTemp / name;
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}
void CopyFixture(const std::string& name, const fs::path& to) {
  fs::remove_all(to);
  fs::copy(kFixtures / name, to, fs::copy_options::recursive);
}

void TestPrimitives() {
  const std::string abc = "abc";
  check(mc::Sha256Hex(reinterpret_cast<const uint8_t*>(abc.data()), 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of \"abc\"");
  check(mc::Sha256Hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of nothing");
  const std::string two(1000, 'a');
  check(mc::Sha256Hex(reinterpret_cast<const uint8_t*>(two.data()), two.size()) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", "SHA-256 of 1000 x 'a' (two blocks)");
  check(mc::Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u, "CRC-32 of 123456789");
  check(mc::IsJunkPath(".DS_Store") && mc::IsJunkPath("data/Thumbs.db") && mc::IsJunkPath("data/x.json.bak") && mc::IsJunkPath("a/.git/config") &&
            mc::IsJunkPath("~$x.txt") && mc::IsJunkPath("__MACOSX/a") && !mc::IsJunkPath("data/x.json"), "junk names");
  check(!mc::ExecutableNameReason("run.EXE").empty() && !mc::ExecutableNameReason("a/b.dll ").empty() && !mc::ExecutableNameReason("x.Ps1").empty() &&
            !mc::ExecutableNameReason("lib.so.1").empty() && !mc::ExecutableNameReason("Foo.app/Contents/x.txt").empty() &&
            !mc::ExecutableNameReason("a.js").empty() && mc::ExecutableNameReason("data/x.json").empty() && mc::ExecutableNameReason("scripts/a.lua").empty(),
        "program names");
  uint8_t pe[0x80] = {0};
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3C] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  const uint8_t elf[8] = {0x7F, 'E', 'L', 'F'};
  const uint8_t macho[8] = {0xCF, 0xFA, 0xED, 0xFE};
  const uint8_t sh[8] = {'#', '!', '/', 'b', 'i', 'n'};
  const uint8_t mz[8] = {'M', 'Z', ' ', 'n', 'o', 't', 'e', 's'};
  check(!mc::ExecutableContentReason(pe, sizeof pe).empty() && !mc::ExecutableContentReason(elf, 8).empty() && !mc::ExecutableContentReason(macho, 8).empty() &&
            !mc::ExecutableContentReason(sh, 8).empty() && mc::ExecutableContentReason(mz, 8).empty(), "program contents: PE, ELF, Mach-O, shebang (and not a text that starts with MZ)");
  check(!mc::TraversalReason("../x").empty() && !mc::TraversalReason("a/../x").empty() && !mc::TraversalReason("/etc/x").empty() &&
            !mc::TraversalReason("C:/x").empty() && !mc::TraversalReason("\\\\server\\x").empty() && !mc::TraversalReason("//server/x").empty() &&
            mc::TraversalReason("a/b/c.txt").empty(), "path traversal");
  check(!mc::UnsafePathReason("CON.txt").empty() && !mc::UnsafePathReason("a/nul").empty() && !mc::UnsafePathReason("LPT3").empty() &&
            !mc::UnsafePathReason("x.").empty() && !mc::UnsafePathReason("a:b").empty() && !mc::UnsafePathReason("a?b").empty() &&
            mc::UnsafePathReason("console.txt").empty() && mc::UnsafePathReason("data/stats.json").empty(), "Windows-unsafe names");
  check(mc::Closest("aplies", {"applies", "after"}) == "applies" && mc::Closest("zzzzzz", {"applies"}).empty(), "did-you-mean");
}

void TestGoodFixtures(const mc::GameRules& rules) {
  const mc::Report data = Check(kFixtures / "good-data", &rules);
  check(data.manifestOk && data.Count(mc::Severity::Problem) == 0 && data.Count(mc::Severity::Warning) == 0, "good-data: nothing wrong (" + Codes(data) + ")");
  check(data.HasPermission("changes-tables") && data.HasPermission("text-changes") && data.HasPermission("adds-rows") && data.HasPermission("update-link") &&
            !data.HasPermission("scripts"), "good-data: permissions listed plainly");
  check(data.ExitCode(true) == 0 && data.UnsafeProblems() == 0, "good-data: exit code 0");
  const mc::Report scripts = Check(kFixtures / "good-scripts", &rules);
  check(scripts.Count(mc::Severity::Problem) == 0 && scripts.Count(mc::Severity::Warning) == 0, "good-scripts: nothing wrong (" + Codes(scripts) + ")");
  check(scripts.HasPermission("scripts"), "good-scripts: permission \"runs Lua scripts\"");
  check(Check(kFixtures / "good-data", nullptr).Count(mc::Severity::Problem) == 0, "good-data without a game's rules: still clean");
}

void TestBadManifest(const mc::GameRules& rules) {
  const mc::Report r = Check(kFixtures / "bad-manifest", &rules);
  const mc::Finding* id = Find(r, "manifest-id");
  check(id != nullptr && id->file == "mod.json" && id->line == 2, "bad-manifest: the bad id, with file and line 2");
  const mc::Finding* name = Find(r, "manifest-type", "mod.json");
  check(name != nullptr && r.Has("version-format") && r.Has("manifest-requires") && r.Has("updates-not-https") && r.Has("manifest-unknown-field"),
        "bad-manifest: lists several problems at once (" + Codes(r) + ")");
  const mc::Finding* typo = Find(r, "manifest-unknown-field");
  check(typo != nullptr && typo->text.find("applies") != std::string::npos, "bad-manifest: a mistyped field says what was probably meant");
  check(r.ExitCode(false) == 1 && !r.manifestOk, "bad-manifest: exit code 1");
  em::Manifest m;
  std::string error;
  std::ifstream in(kFixtures / "bad-manifest" / "mod.json");
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  check(!em::ParseManifest(text, m, error) && !r.manifestOk, "agreement: the engine's own parser refuses it too");
  for (const char* good : {"good-data", "good-scripts", "bad-data", "bad-lua"}) {
    std::ifstream gin(kFixtures / good / "mod.json");
    const std::string gt((std::istreambuf_iterator<char>(gin)), std::istreambuf_iterator<char>());
    check(em::ParseManifest(gt, m, error) == Check(kFixtures / good, &rules).manifestOk, std::string("agreement: the engine and the checker agree on ") + good);
  }
}

void TestAssets() {
  const fs::path d = Fresh("assets");
  const auto run = [&](const std::string& extra) {
    write_file(d / "mod.json", std::string("{ \"id\": \"u\", \"name\": \"U\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\"") + extra + " }");
    write_file(d / "data/a.json", "{ \"text\": { \"#1\": \"x\" } }");
    return Check(d, nullptr);
  };
  const mc::Report ok = run(", \"needs_assets\": [\"gen57_pack\"], \"recommends_assets\": [\"usum_models\"]");
  check(ok.manifestOk && ok.Has("asset-needed") && !ok.Has("manifest-assets") && ok.manifest.needsAssets.size() == 1,
        "assets: known ids pass, a needed one is noted");
  const mc::Report bad = run(", \"needs_assets\": [\"gen9_pack\"]");
  check(bad.Has("manifest-assets") && !bad.manifestOk, "assets: an unknown id is a problem (manifest-assets)");
  check(run(", \"recommends_assets\": [\"pbr\"]").Has("manifest-assets"), "assets: an unknown recommended id is a problem too");
  check(!run(", \"recommends_assets\": [\"br_models\"]").Has("manifest-unknown-field"), "assets: the two fields are known fields");
}

void TestUpdates() {
  const fs::path d = Fresh("updates");
  const auto run = [&](const std::string& extra) {
    write_file(d / "mod.json", std::string("{ \"id\": \"u\", \"name\": \"U\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\"") + extra + " }");
    write_file(d / "data/a.json", "{ \"text\": { \"#1\": \"x\" } }");
    return Check(d, nullptr);
  };
  check(run(", \"updates\": \"https://example.com/u.json\"").Has("updates-no-version"), "\"updates\" without \"version\" warns");
  check(run(", \"version\": \"1.0\", \"updates\": \"http://example.com/u.json\"").Has("updates-not-https"), "\"updates\" over http warns");
  check(run(", \"version\": \"1.0\", \"updates\": \"example.com/u.json\"").Has("updates-url"), "\"updates\" that is no web address warns");
  check(run(", \"version\": \"1.0\", \"updates\": 5").Has("manifest-type"), "\"updates\" that is no text is a problem");
  const mc::Report ok = run(", \"version\": \"1.0.2\", \"updates\": \"https://example.com/u.json\"");
  check(ok.Count(mc::Severity::Warning) == 0 && ok.Count(mc::Severity::Problem) == 0, "version + https \"updates\": clean");
  check(run(", \"version\": \"v1-beta\"").Has("version-format"), "a version that is not dotted numbers warns");
}

void TestBadData(const mc::GameRules& rules) {
  const mc::Report r = Check(kFixtures / "bad-data", &rules);
  const mc::Finding* broken = Find(r, "data-json", "data/broken.json");
  check(broken != nullptr && broken->line == 3, "bad-data: invalid JSON with its line (3)");
  const std::string s = "data/shape.json";
  const auto line = [&](const char* code, int l) {
    for (const mc::Finding& f : r.findings) if (f.code == code && f.file == s && f.line == l) return true;
    return false;
  };
  check(line("data-number", 3) && line("data-value", 3) && line("data-entry", 4) && line("data-table", 6), "bad-data: negative number, nested list/block/null, entry and table shapes, each at its line");
  check(line("data-text-key", 8) && line("data-text", 9) && line("data-text", 10), "bad-data: message keys and texts (including the game's own text rule) at their lines");
  check(line("data-add", 13), "bad-data: \"add\" rows that are no block");
  check(r.Has("applies-claim"), "bad-data: \"any_time\" claim with data files is called out");
  check(r.ExitCode(false) == 1, "bad-data: exit code 1");
}

void TestLooksOrSoundRule(const mc::GameRules& rules) {
  const fs::path mod = Fresh("sound-mod");
  write_file(mod / "mod.json", "{\"id\": \"sound-mod\", \"name\": \"Sound\", \"version\": \"1.0\", \"game\": \"MADE01\", \"applies\": \"any_time\"}");
  write_file(mod / "files" / "sound" / "x.samp", "own sound, not the game's");
  mc::GameRules withRule = rules;
  withRule.looksOrSound = [](const std::string& path, int type) { return type == -1 && path.size() > 5 && path.substr(path.size() - 5) == ".samp"; };
  mc::GameRules without = rules;
  without.looksOrSound = nullptr;
  check(!Check(mod, &withRule).Has("applies-claim"), "any_time with only sound files: believed when the game's rule says they are sound");
  check(Check(mod, &without).Has("applies-claim"), "any_time with sound files: called out when the game gives no rule");

  const fs::path sounds = Fresh("sounds-folder-mod");
  write_file(sounds / "mod.json", "{\"id\": \"sounds-mod\", \"name\": \"Sounds\", \"version\": \"1.0\", \"game\": \"MADE01\"}");
  write_file(sounds / "sounds" / "effects.json", "{\"effects\": []}");
  write_file(sounds / "sounds" / "a.wav", "RIFF....WAVEfmt ");
  write_file(sounds / "sounds" / "b.ogg", "OggS....");
  const mc::Report sr = Check(sounds, &rules);
  check(!sr.Has("unknown-kind") && !sr.Has("empty-mod") && !sr.HasPermission("adds-story"), "a mod of only sounds/ files: the files are a known kind, the mod is not empty, and it is not called a story mod");
}

void TestBadLua(const mc::GameRules& rules) {
  const mc::Report r = Check(kFixtures / "bad-lua", &rules);
  const mc::Finding* syn = Find(r, "lua-syntax", "scripts/syntax.lua");
  check(syn != nullptr && syn->line >= 2, "bad-lua: syntax error with its line");
  const mc::Finding* hook = Find(r, "lua-unknown-hook", "scripts/hook.lua");
  check(hook != nullptr && hook->line == 1 && hook->text.find("battle.damage") != std::string::npos, "bad-lua: unknown hook, with what was probably meant");
  check(Find(r, "lua-sandbox", "scripts/hook.lua") != nullptr, "bad-lua: os.time() is not in the sandbox");
  check(Find(r, "unknown-kind", "scripts/notes.txt") != nullptr, "bad-lua: a non-Lua file in scripts/ is an unknown kind");
}

void TestRequires(const mc::GameRules& rules) {
  const fs::path folder = kFixtures / "requires-case";
  const mc::Report r = Check(folder / "a", &rules, folder);
  check(r.Has("requires-version") && r.Has("conflicts-present"), "requires/conflicts against the mods folder (" + Codes(r) + ")");
  const mc::Report alone = Check(folder / "a", &rules);
  check(alone.Has("requires-unchecked") && !alone.Has("requires-version"), "without a mods folder: noted, not guessed");
}

void TestUnsafeFolder(const mc::GameRules& rules) {
  const fs::path d = Fresh("unsafe");
  write_file(d / "mod.json", kMod);
  write_file(d / "data/a.json", "{ \"text\": { \"#1\": \"x\" } }");
  write_file(d / "tools/run.exe", "harmless text");
  write_file(d / "tools/setup.bat", "echo hi");
  write_file(d / "scripts/helper.sh", "echo hi");
  write_file(d / "misc/readme.dat", "#!/bin/sh\necho hi\n");
  std::vector<uint8_t> pe(0x80, 0);
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3C] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  write_bytes(d / "misc/picture.png", pe);
  write_bytes(d / "misc/model.bin", {0x7F, 'E', 'L', 'F', 2, 1, 1, 0});
  write_bytes(d / "Cool.app/Contents/MacOS/cool", {1, 2, 3});
  write_bytes(d / "extra/more.zip", Zip({{"a.txt", bytes("hi")}}));
  write_file(d / "extra/disguised.dat", std::string("PK\x03\x04", 4) + "xxxxxxxx");
  write_file(d / ".DS_Store", "junk");
  write_file(d / "data/stats.json.bak", "junk");
  const mc::Report r = Check(d, &rules);
  check(Find(r, "program-file", "tools/run.exe") && Find(r, "program-file", "tools/setup.bat") && Find(r, "program-file", "scripts/helper.sh"), "program names refused: .exe .bat .sh");
  check(Find(r, "program-content", "misc/readme.dat") && Find(r, "program-content", "misc/picture.png") && Find(r, "program-content", "misc/model.bin"),
        "program contents refused whatever the name: shebang, PE as .png, ELF");
  check(Find(r, "program-file", "Cool.app"), "a Mac application bundle refused");
  check(Find(r, "nested-archive", "extra/more.zip") && Find(r, "nested-archive", "extra/disguised.dat"), "nested archives flagged by name and by content");
  check(Find(r, "junk", ".DS_Store") && Find(r, "junk", "data/stats.json.bak"), "junk noted");
  check(r.UnsafeProblems() >= 8 && r.ExitCode(false) == 1, "safety problems give exit code 1");
  const std::string text = r.Format();
  check(text.find("SAFETY") < text.find("CHECKS") && text.find("PROBLEM  tools/run.exe") != std::string::npos, "the report has a SAFETY section first, with file names");
  const mc::Report good = Check(kFixtures / "good-data", &rules);
  check(good.Format().find("cannot prove") != std::string::npos && good.Format().find("WHAT THIS MOD CAN DO") != std::string::npos, "a clean report is honest about its limits and lists permissions");
  std::error_code ec;
  const fs::path link = d / "linked";
  fs::create_symlink(d / "data", link, ec);
  if (!ec) {
    check(Find(Check(d, &rules), "symlink", "linked") != nullptr, "a symbolic link refused");
  } else {
    std::printf("skip (no permission to make links here): symbolic link refused\n");
  }
}

void TestFormats(const mc::GameRules& rules) {
  const fs::path d = Fresh("formats");
  write_bytes(d / "textures/good.png", Png(16, 16));
  write_bytes(d / "textures/damaged.png", Png(16, 16, 0, true));
  {
    std::vector<uint8_t> cut = Png(16, 16);
    cut.resize(cut.size() / 2);
    write_bytes(d / "textures/cut.png", cut);
  }
  write_bytes(d / "textures/bomb.png", Png(16, 16, 5000000));
  write_bytes(d / "textures/huge.png", Png(9000, 9000));
  write_file(d / "data/deep.json", std::string(100, '[') + std::string(100, ']'));
  write_file(d / "scripts/b.lua", std::string("\x1bLua\x54", 5) + "xxxx");
  write_file(d / "scripts/big.lua", std::string(300 * 1024, '-'));
  std::vector<uint8_t> dds(128, 0);
  std::memcpy(dds.data(), "DDS ", 4);
  dds[4] = 124; dds[12] = 64; dds[16] = 64; dds[28] = 1; dds[76] = 32; dds[80] = 4;
  std::memcpy(dds.data() + 84, "DXT1", 4);
  write_bytes(d / "textures/short.dds", dds);
  dds.resize(128 + 64 * 64 / 2);
  write_bytes(d / "textures/ok.dds", dds);
  write_file(d / "mod.json", "{ \"id\": \"t\", \"name\": \"T\", \"version\": \"1.0\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\", \"scripts\": true }");
  const mc::Report r = Check(d, &rules);
  check(Find(r, "png-damaged", "textures/damaged.png") && Find(r, "png-damaged", "textures/cut.png"), "PNG: a wrong checksum and a cut-off file are damage");
  check(Find(r, "png-bomb", "textures/bomb.png") && Find(r, "png-too-big", "textures/huge.png"), "PNG: unpacking more than the header says, and too-large dimensions, are safety problems");
  check(!Find(r, "png-damaged", "textures/good.png") && Find(r, "dds-damaged", "textures/short.dds") && !Find(r, "dds-damaged", "textures/ok.dds"), "a good PNG and DDS pass, a short DDS fails");
  check(Find(r, "json-too-deep", "data/deep.json"), "JSON nested too deep is refused before parsing");
  check(Find(r, "lua-bytecode", "scripts/b.lua") && Find(r, "lua-too-big", "scripts/big.lua"), "Lua: compiled bytecode refused, too-big script reported");
  mc::Report sizes;
  uint32_t w = 0, h = 0;
  check(mc::CheckPng("x.png", Png(40, 30), mc::SafetyLimits(), sizes, &w, &h) && w == 40 && h == 30, "PNG dimensions are read back");
}

void TestStoryFilesAndPlugin(const mc::GameRules& base) {
  const fs::path d = Fresh("entries");
  write_file(d / "mod.json", kMod);
  write_file(d / "fsys/common.fsys/thing", "BAD! entry");
  write_file(d / "fsys/common.fsys/other", "fine");
  write_file(d / "rooms/hall/script", "fine");
  write_file(d / "objects/hall.json", "{ \"objects\": [] }");
  write_file(d / "objects/broken.json", "{ \"objects\": [ }");
  mc::GameRules rules = base;
  bool ran = false;
  rules.extra.push_back({"made-up objects", [&](const mc::GameRules::Context& c, mc::Report& report) {
                           ran = true;
                           if (fs::exists(c.folder / "objects" / "hall.json")) report.Warn("objects-checked", "objects/hall.json", 0, "plugged in");
                         }});
  const mc::Report r = Check(d, &rules);
  check(Find(r, "entry-bad", "fsys/common.fsys/thing") != nullptr, "the game's file check runs on fsys entries");
  check(ran && Find(r, "objects-checked") != nullptr, "a game's extra check plugs in and sees the folder and manifest");
  check(Find(r, "json-invalid", "objects/broken.json") != nullptr && Find(r, "unknown-kind") == nullptr, "objects/*.json are known JSON files that must parse; rooms/ files are not unknown");
  check(r.HasPermission("adds-story") && r.HasPermission("replaces-files"), "story files and archive entries show as permissions");
}

void TestZipScan() {
  const auto has = [](const mc::Report& r, const char* code) { return r.Has(code); };
  check(!ScanOf({{"t/mod.json", bytes(kMod)}, {"t/data/a.json", bytes("{}")}}).Count(mc::Severity::Problem), "a normal zip passes the scan");
  check(has(ScanOf({{"../evil.txt", bytes("x")}}), "zip-bad-path"), "zip: \"..\" refused");
  check(has(ScanOf({{"/etc/evil", bytes("x")}}), "zip-bad-path"), "zip: absolute path refused");
  check(has(ScanOf({{"C:/evil.txt", bytes("x")}}), "zip-bad-path"), "zip: drive letter refused");
  check(has(ScanOf({{"//server/share/x", bytes("x")}}), "zip-bad-path") && has(ScanOf({{"\\\\server\\x", bytes("x")}}), "zip-bad-path"), "zip: network paths refused");
  check(has(ScanOf({{"a/..\\..\\x", bytes("x")}}), "zip-bad-path"), "zip: backslash tricks refused");
  check(has(ScanOf({{"t/CON.txt", bytes("x")}}), "zip-bad-name") && has(ScanOf({{"t/nul", bytes("x")}}), "zip-bad-name") && has(ScanOf({{"t/a:stream", bytes("x")}}), "zip-bad-name"), "zip: device names and streams refused");
  ZEntry link{"t/link", bytes("/etc/passwd")};
  link.mode = 0120777;
  check(has(ScanOf({link}), "zip-symlink"), "zip: a symbolic link refused");
  ZEntry dev{"t/dev", bytes("")};
  dev.mode = 0020644;
  check(has(ScanOf({dev}), "zip-special"), "zip: a device file refused");
  check(has(ScanOf({{"t/A.txt", bytes("1")}, {"t/a.TXT", bytes("2")}}), "zip-case-duplicate"), "zip: names that differ only by case refused");
  check(has(ScanOf({{"t/run.exe", bytes("x")}}), "program-file") && has(ScanOf({{"t/Foo.app/x", bytes("x")}}), "program-file"), "zip: programs refused");
  check(has(ScanOf({{"t/inner.zip", bytes("x")}}), "nested-archive") && has(ScanOf({{"t/inner.7z", bytes("x")}}), "nested-archive"), "zip: nested archives refused");
  mc::SafetyLimits few;
  few.maxFiles = 3;
  check(has(ScanOf({{"a", bytes("1")}, {"b", bytes("1")}, {"c", bytes("1")}, {"d", bytes("1")}}, few), "zip-too-many-files"), "zip: too many files refused");
  mc::SafetyLimits small;
  small.maxTotalBytes = 1000;
  check(has(ScanOf({{"a", std::vector<uint8_t>(2000, 'x')}}, small), "zip-too-big"), "zip: too much unpacked data refused");
  check(has(ScanOf({{"t/zeros.bin", std::vector<uint8_t>(6 << 20, 0)}}), "zip-bomb"), "zip: a huge compression ratio (zip bomb) refused");
  ZEntry lie{"t/lie.bin", std::vector<uint8_t>(5000, 'x')};
  lie.declaredSize = 10;
  mc::Report scan;
  const std::vector<uint8_t> liar = Zip({lie});
  check(mc::ScanZip(liar, mc::SafetyLimits(), scan), "zip: (a lying size passes the listing...)");
  std::string error;
  const fs::path out = Fresh("lie");
  check(!mc::ExtractZip(liar, out / "x", mc::SafetyLimits(), error) && !error.empty(), "zip: ...but unpacking never writes past the declared size: " + error);
  mc::Report notZip;
  check(!mc::ScanZip(bytes("this is not a zip at all, just text, long enough to look like something"), mc::SafetyLimits(), notZip), "zip: a non-zip is refused");
  const std::vector<uint8_t> good = Zip({{"t/mod.json", bytes(kMod)}, {"t/data/a.json", bytes("{ \"text\": { \"#1\": \"x\" } }")}});
  const fs::path into = Fresh("unzip") / "x";
  check(mc::ExtractZip(good, into, mc::SafetyLimits(), error) && fs::exists(into / "t/data/a.json"), "zip: a good zip unpacks");
  mc::Report whole;
  mc::Options o;
  const bool fine = mc::CheckAndExtractArchive(good, Fresh("dl") / "x", o, mc::SafetyLimits(), whole);
  check(fine && whole.manifestOk && whole.manifest.id == "t", "CheckAndExtractArchive: scan + unpack + checks on a good download (" + Codes(whole) + ")");
  mc::Report evil;
  check(!mc::CheckAndExtractArchive(Zip({{"t/mod.json", bytes(kMod)}, {"t/../../evil", bytes("x")}}), Fresh("dl2") / "x", o, mc::SafetyLimits(), evil) && evil.Has("zip-bad-path"),
        "CheckAndExtractArchive: a bad download is stopped before anything is unpacked");
  check(!fs::exists(sTemp / "dl2" / "x" / "t"), "...and nothing was written");
}

std::vector<std::string> Names(const std::vector<uint8_t>& zip) {
  std::vector<mc::ZipEntry> e;
  mc::Report r;
  mc::ScanZip(zip, mc::SafetyLimits(), r, &e);
  std::vector<std::string> out;
  for (const mc::ZipEntry& z : e) out.push_back(z.name);
  return out;
}

std::string Text(const fs::path& p) {
  const auto b = read_all(p);
  return std::string(b.begin(), b.end());
}

void TestPacker(const mc::GameRules& rules) {
  const fs::path folder = Fresh("pack") / "good-data";
  CopyFixture("good-data", folder);
  write_file(folder / ".DS_Store", "junk");
  write_file(folder / "Thumbs.db", "junk");
  write_file(folder / "data/stats.json.bak", "junk");
  mc::PackOptions o;
  o.check.folder = folder;
  o.check.rules = &rules;
  o.outDir = sTemp / "pack" / "out";
  o.urlBase = "https://example.com/releases/v1.2.0";
  const mc::PackResult r = mc::PackMod(o);
  check(r.ok && fs::exists(r.zip) && r.zip.filename() == "good-data-1.2.0.zip", "pack: a clean mod packs to <id>-<version>.zip (" + r.error + ")");
  const std::vector<uint8_t> zip = read_all(r.zip);
  const std::vector<std::string> names = Names(zip);
  check(std::find(names.begin(), names.end(), "good-data/mod.json") != names.end() && std::find(names.begin(), names.end(), "good-data/data/stats.json") != names.end() &&
            names.size() == 4, "pack: files sit in one top folder named after the id (mod.json, README, CHANGES, data)");
  check(r.leftOut.size() == 3 && std::none_of(names.begin(), names.end(), [](const std::string& n) { return n.find("DS_Store") != std::string::npos || n.find(".bak") != std::string::npos; }), "pack: editor and system junk is left out and listed");
  mc::Report scan;
  check(mc::ScanZip(zip, mc::SafetyLimits(), scan) && scan.findings.empty(), "pack: the zip passes the same safety scan the launcher will run");
  mc::Report dl;
  mc::Options plain;
  check(mc::CheckAndExtractArchive(zip, Fresh("packdl") / "x", plain, mc::SafetyLimits(), dl) && dl.Count(mc::Severity::Problem) == 0, "pack: the zip round-trips through unpack + check");
  mc::PackOptions again = o;
  again.outDir = sTemp / "pack" / "out2";
  check(mc::PackMod(again).sha256 == r.sha256, "pack: the same mod gives the same zip bytes (reproducible)");
  check(fs::exists(r.updateFile) && r.updateFile.filename() == "good-data.update.json", "pack: \"updates\" in mod.json gives an update file next to the zip");
  const std::string upd = Text(r.updateFile);
  check(upd.find("\"id\": \"good-data\"") != std::string::npos && upd.find("\"version\": \"1.2.0\"") != std::string::npos &&
            upd.find("\"sha256\": \"" + mc::Sha256Hex(zip.data(), zip.size()) + "\"") != std::string::npos &&
            upd.find("\"size\": " + std::to_string(zip.size())) != std::string::npos &&
            upd.find("\"url\": \"https://example.com/releases/v1.2.0/good-data-1.2.0.zip\"") != std::string::npos &&
            upd.find("made the numbers rounder") != std::string::npos && upd.find("\"games\": [\"GXXE01\"]") != std::string::npos, "pack: the update file holds id, version, url, sha256, size, notes (from CHANGES), games");
  check(upd.find("\"permissions\"") != std::string::npos && upd.find("update-link") != std::string::npos, "pack: ...and the permission codes (for the launcher to ask about)");
  mc::PackOptions notes = o;
  notes.outDir = sTemp / "pack" / "out3";
  notes.notes = "From --notes";
  notes.urlBase.clear();
  const mc::PackResult n = mc::PackMod(notes);
  const std::string upd2 = Text(n.updateFile);
  check(upd2.find("From --notes") != std::string::npos && upd2.find("\"url\": \"\"") != std::string::npos && !n.messages.empty(), "pack: --notes wins, an empty url is said plainly");
  const fs::path plainDir = Fresh("pack2") / "good-scripts";
  CopyFixture("good-scripts", plainDir);
  mc::PackOptions po;
  po.check.folder = plainDir;
  po.check.rules = &rules;
  po.outDir = sTemp / "pack2" / "out";
  const mc::PackResult pr = mc::PackMod(po);
  check(pr.ok && pr.updateFile.empty() && !fs::exists(sTemp / "pack2" / "out" / "good-scripts.update.json"), "pack: no \"updates\", no update file");
}

void TestPackerRefusals(const mc::GameRules& rules) {
  const fs::path folder = Fresh("refuse") / "m";
  CopyFixture("good-data", folder);
  const std::vector<uint8_t> fsys = make_fsys({{"hello", "world"}});
  write_bytes(folder / "files/common.fsys", fsys);
  write_bytes(folder / "files/renamed.dat", fsys);
  write_file(folder / "extra/disc.iso", "x");
  write_bytes(folder / "save/xd.gci", bytes("GXXE01 save data that is long enough to look like one............................."));
  write_file(folder / "crash.log", "log");
  write_file(folder / "play_20261116_1.txt", "recording");
  write_bytes(folder / "textures/tex1_64x64_0123456789abcdef_5.png", Png(64, 64));
  write_bytes(folder / "textures/tex1_64x64_aaaaaaaaaaaaaaaa_5.png", Png(128, 128));
  write_file(folder / "known.bin", "known game bytes");
  mc::PackOptions o;
  o.check.folder = folder;
  o.check.rules = &rules;
  o.outDir = sTemp / "refuse" / "out";
  o.refuseHashes.insert(mc::Sha256Hex(reinterpret_cast<const uint8_t*>("known game bytes"), 16));
  const mc::PackResult r = mc::PackMod(o);
  const auto refused = [&](const mc::PackResult& x, const std::string& path, const char* code) {
    return std::any_of(x.refused.begin(), x.refused.end(), [&](const mc::Refusal& q) { return q.path == path && q.code == code; });
  };
  check(!r.ok && r.zip.empty() && !fs::exists(sTemp / "refuse" / "out" / "good-data-1.2.0.zip"), "refuse: nothing is written when game data is found");
  check(refused(r, "files/common.fsys", "game-archive") && refused(r, "files/renamed.dat", "game-archive") && refused(r, "extra/disc.iso", "game-archive"), "refuse: disc-format archives (by extension and by header) and disc images");
  check(refused(r, "save/xd.gci", "save") && refused(r, "crash.log", "log") && refused(r, "play_20261116_1.txt", "log"), "refuse: saves, logs and recordings");
  check(refused(r, "textures/tex1_64x64_0123456789abcdef_5.png", "texture-dump") && !refused(r, "textures/tex1_64x64_aaaaaaaaaaaaaaaa_5.png", "texture-dump"), "refuse: a texture the size of the game's own, accept a bigger replacement");
  check(refused(r, "known.bin", "known-hash"), "refuse: a file whose SHA-256 is on the known game list");
  const std::string text = r.Format();
  check(text.find("REFUSED") != std::string::npos && text.find("files/common.fsys") != std::string::npos && text.find("Nothing was packed") != std::string::npos, "refuse: the output says plainly what and why");
  mc::PackOptions same = o;
  same.allowSameSizeTextures = true;
  check(!refused(mc::PackMod(same), "textures/tex1_64x64_0123456789abcdef_5.png", "texture-dump"), "refuse: --allow-same-size-textures lets an own same-size texture through");
  const fs::path bad = Fresh("refuse2") / "m";
  CopyFixture("good-data", bad);
  write_file(bad / "tool.exe", "x");
  mc::PackOptions bo;
  bo.check.folder = bad;
  bo.check.rules = &rules;
  bo.outDir = sTemp / "refuse2" / "out";
  const mc::PackResult b = mc::PackMod(bo);
  check(!b.ok && b.report.UnsafeProblems() > 0 && !fs::exists(sTemp / "refuse2" / "out"), "pack: a program in the mod stops the packer (the checker's safety problem)");
  const fs::path warn = Fresh("refuse3") / "m";
  CopyFixture("good-data", warn);
  write_file(warn / "stray.xyz", "x");
  mc::PackOptions wo;
  wo.check.folder = warn;
  wo.check.rules = &rules;
  wo.outDir = sTemp / "refuse3" / "out";
  check(mc::PackMod(wo).ok, "pack: a warning alone does not stop packing");
  wo.strict = true;
  wo.outDir = sTemp / "refuse3" / "out2";
  check(!mc::PackMod(wo).ok, "pack: --strict does");
  const fs::path broken = Fresh("refuse4") / "m";
  CopyFixture("bad-manifest", broken);
  mc::PackOptions xo;
  xo.check.folder = broken;
  xo.check.rules = &rules;
  xo.outDir = sTemp / "refuse4" / "out";
  check(!mc::PackMod(xo).ok, "pack: a mod with problems is not packed");
}

void TestPackerDisc(const mc::GameRules& rules) {
  const std::vector<uint8_t> fsys = make_fsys({{"alpha.bin", "ALPHA ORIGINAL"}, {"beta.bin", "BETA ORIGINAL"}});
  em::DiscView disc;
  disc.gameId = "TEST01";
  disc.files = {"Plain.bin", "pack.fsys"};
  disc.read = [&](const std::string& path, std::vector<uint8_t>& out) {
    if (path == "Plain.bin") {
      out = bytes("PLAIN ORIGINAL");
      return true;
    }
    if (path == "pack.fsys") {
      out = fsys;
      return true;
    }
    return false;
  };
  const fs::path folder = Fresh("disc") / "m";
  CopyFixture("good-data", folder);
  write_file(folder / "files/plain.bin", "PLAIN ORIGINAL");
  write_file(folder / "files/changed.bin", "mine");
  write_file(folder / "fsys/pack.fsys/alpha.bin", "ALPHA ORIGINAL");
  write_file(folder / "fsys/pack.fsys/beta.bin", "BETA CHANGED BY ME");
  mc::PackOptions o;
  o.check.folder = folder;
  o.check.rules = &rules;
  o.outDir = sTemp / "disc" / "out";
  o.disc = &disc;
  const mc::PackResult r = mc::PackMod(o);
  const auto same = [&](const std::string& path) {
    return std::any_of(r.refused.begin(), r.refused.end(), [&](const mc::Refusal& x) { return x.path == path && x.code == "same-as-disc"; });
  };
  check(same("files/plain.bin") && same("fsys/pack.fsys/alpha.bin"), "disc: unchanged copies of the disc's files and archive entries are refused");
  check(!same("files/changed.bin") && !same("fsys/pack.fsys/beta.bin") && r.refused.size() == 2, "disc: a file you really changed is fine");
}

void TestDeepPass(const mc::GameRules& rules) {
  em::DiscView disc;
  disc.gameId = "TEST01";
  disc.read = [](const std::string&, std::vector<uint8_t>&) { return false; };
  em::LoadOptions load;
  load.disc = disc;
  const fs::path mods = Fresh("deep");
  CopyFixture("good-data", mods / "good-data");
  mc::Options o;
  o.folder = mods / "good-data";
  o.rules = &rules;
  o.deep = &load;
  const mc::Report r = mc::CheckMod(o);
  check(r.deepDone, "deep: the loader pass ran");
  const mc::Finding* wrong = Find(r, "game-loader");
  check(wrong != nullptr && wrong->text.find("Made for") != std::string::npos, "deep: the loader's own message about the wrong disc comes through (" + Codes(r) + ")");
  mc::Options plain = o;
  plain.deep = nullptr;
  const mc::Report without = mc::CheckMod(plain);
  check(!without.deepDone && without.Format().find("Without a disc") != std::string::npos, "deep: without a disc the pass is skipped and the report says so");
}

void TestLayout(const mc::GameRules& base) {
  mc::GameRules rules = base;
  rules.gameIds = {"GXXE01", "GXXJ01"};
  rules.gameOnlyLua = {"story_function", "FLAG", "battle_tables"};
  const fs::path dir = sTemp / "layout";
  const std::string manifest = "{\"id\": \"two\", \"name\": \"Two games\", \"version\": \"1.0\", \"game\": [\"GXXE01\", \"GC6E01\"], \"scripts\": true}";
  write_file(dir / "mod.json", manifest);
  write_file(dir / "scripts" / "shared.lua", "on(\"battle.damage\", function(e) return e.damage end)\n");
  write_file(dir / "scripts" / "uses.lua", "story_function(\"x\", function() return 1 end)\n");
  write_file(dir / "scripts" / "tables.lua", "battle_tables{ moves = { [1] = { power = 90 } } }\n");
  write_bytes(dir / "textures" / "a.png", Png(8, 8));
  write_file(dir / "games" / "GXXE01" / "data" / "a.json", "{ \"pokemon\": { \"#1\": { \"basisStatus[0].maxHp\": 5 } } }");
  write_file(dir / "games" / "GXXE01" / "scripts" / "own.lua", "story_function(\"y\", function() return 1 end)\n");
  mc::Report r = Check(dir, &rules);
  check(r.manifestOk && r.support.size() == 2, "layout: the report has one support entry per game in mod.json");
  check(r.support.size() == 2 && r.support[0].level == em::GameSupport::Level::Full && r.support[1].level == em::GameSupport::Level::SharedOnly,
        "layout: XD works fully (its games/ folder), Colosseum with the shared parts only");
  check(r.Format().find("works with: XD; Colosseum: shared parts only") != std::string::npos, "layout: the report says \"works with: XD; Colosseum: shared parts only\"");
  check(Find(r, "manifest-game") == nullptr, "layout: a game of the port's table that is not this game's is not an unknown ID");
  check(Find(r, "unknown-kind") == nullptr, "layout: files under games/<id>/ are read as the same kinds as at the top");
  const mc::Finding* only = Find(r, "lua-game-only", "scripts/uses.lua");
  check(only != nullptr && only->severity == mc::Severity::Warning, "layout: shared Lua using a name only this game has is a warning when the mod names another game");
  check(Find(r, "lua-game-only", "scripts/tables.lua") != nullptr, "layout: a call written with a table (name{...}) is found too");
  check(Find(r, "lua-game-only", "scripts/shared.lua") == nullptr && Find(r, "lua-game-only", "games/GXXE01/scripts/own.lua") == nullptr,
        "layout: neutral Lua, and game-only Lua inside the game's own folder, are fine");
  bool tablesOnlyXd = false;
  for (const mc::Permission& p : r.permissions) tablesOnlyXd |= p.code == "changes-tables" && p.text.find("[only for GXXE01]") != std::string::npos;
  check(tablesOnlyXd, "layout: a permission that only a games/ folder uses says so");
  bool scriptsAll = false;
  for (const mc::Permission& p : r.permissions) scriptsAll |= p.code == "scripts" && p.text.find("[only for") == std::string::npos;
  check(scriptsAll, "layout: a permission the shared part uses has no \"only for\"");

  write_file(dir / "games" / "GXXJ01" / "data" / "b.json", "{}");
  write_file(dir / "games" / "ABC" / "data" / "b.json", "{}");
  write_file(dir / "games" / "AB" / "data" / "b.json", "{}");
  write_file(dir / "games" / "readme.txt", "notes");
  r = Check(dir, &rules);
  check(Find(r, "games-folder-unlisted", "games/GXXJ01") != nullptr && Find(r, "games-folder-unlisted", "games/ABC") != nullptr,
        "layout: a games/ folder no \"game\" entry matches is a warning");
  check(Find(r, "games-folder-name", "games/AB") != nullptr && Find(r, "games-folder-name", "games/readme.txt") != nullptr,
        "layout: a folder name that is not 3, 4 or 6 characters, and a file straight under games/, are warnings");
  fs::remove_all(dir / "games" / "AB");
  fs::remove_all(dir / "games" / "ABC");
  fs::remove_all(dir / "games" / "GXXJ01");
  fs::remove(dir / "games" / "readme.txt");

  write_file(dir / "mod.json", "{\"id\": \"two\", \"name\": \"Two\", \"version\": \"1\", \"game\": [\"GXX\", \"GC6\"], \"scripts\": true}");
  r = Check(dir, &rules);
  check(Find(r, "manifest-game") == nullptr && r.manifestOk, "layout: \"GXX\" and \"GC6\" are fine as game entries");
  write_file(dir / "mod.json", "{\"id\": \"two\", \"name\": \"Two\", \"version\": \"1\", \"game\": [\"GXXE0\"], \"scripts\": true}");
  r = Check(dir, &rules);
  check(Find(r, "manifest-game") != nullptr, "layout: a game entry of 5 characters is not an ID start");

  const fs::path old = sTemp / "layout_old";
  write_file(old / "mod.json", "{\"id\": \"old\", \"name\": \"Old\", \"version\": \"1\", \"game\": \"GXXE01\"}");
  write_file(old / "data" / "a.json", "{ \"pokemon\": { \"#1\": { \"basisStatus[0].maxHp\": 5 } } }");
  mc::Report ro = Check(old, &rules);
  check(ro.ExitCode(false) == 0 && ro.support.size() == 1 && ro.support[0].level == em::GameSupport::Level::Full &&
            ro.Format().find("works with: XD") != std::string::npos,
        "layout: an old-layout mod checks as before and works fully with its game");
  bool noOnly = true;
  for (const mc::Permission& p : ro.permissions) noOnly &= p.text.find("[only for") == std::string::npos;
  check(noOnly, "layout: an old-layout mod's permissions have no \"only for\"");
}

}

void TestLuau(const mc::GameRules& base) {
  mc::GameRules rules = base;
  rules.gameIds = {"GXXE01", "GXXJ01"};
  const fs::path dir = sTemp / "luau_new";
  write_file(dir / "mod.json", "{\"id\": \"typed\", \"name\": \"Typed\", \"version\": \"1.0\", \"game\": [\"xd\"], \"scripts\": true}");
  write_file(dir / "games" / "xd" / "scripts" / "typed.luau",
             "--!strict\ntype Hit = {damage: number}\nlocal n = 0\nfor i = 1, 4 do if i == 2 then continue end n += i end\n"
             "on(\"battle.damage\", function(e: Hit): number return math.floor(e.damage / 2) + n end)\n");
  mc::Report r = Check(dir, &rules);
  check(r.manifestOk && Find(r, "lua-syntax") == nullptr && Find(r, "manifest-game") == nullptr, "luau: typed Luau in games/xd/scripts compiles, \"xd\" is a fine game");
  check(Find(r, "unknown-kind") == nullptr && Find(r, "games-folder-unlisted") == nullptr && Find(r, "games-folder-name") == nullptr, "luau: games/xd is a known folder for \"game\": [\"xd\"]");
  check(r.Format().find("works with: XD") != std::string::npos, "luau: the report says it works with XD");
  bool scripts = false;
  for (const mc::Permission& p : r.permissions) scripts |= p.code == "scripts" && p.text.find("Luau") != std::string::npos;
  check(scripts, "luau: the permission text says Luau scripts");
  const fs::path flat = sTemp / "luau_flat";
  write_file(flat / "mod.json", "{\"id\": \"flat\", \"name\": \"Flat\", \"version\": \"1.0\", \"game\": \"GXXE01\", \"scripts\": true}");
  write_file(flat / "scripts" / "old.lua", "on(\"battle.damage\", function(e) return e.damage * 2 end)\n");
  r = Check(flat, &rules);
  const mc::Finding* oldName = Find(r, "lua-old-name", "scripts/old.lua");
  const mc::Finding* flatNote = Find(r, "flat-layout");
  check(oldName != nullptr && oldName->severity == mc::Severity::Note, "luau: an older .lua name is only a note");
  check(flatNote != nullptr && flatNote->severity == mc::Severity::Note && flatNote->text.find("games/xd") != std::string::npos, "luau: the flat layout gets a gentle note that names games/xd");
  check(Find(r, "lua-syntax") == nullptr && r.ExitCode(false) == 0, "luau: the old mod passes the check (exit 0)");
  const fs::path bad = sTemp / "luau_bad";
  write_file(bad / "mod.json", "{\"id\": \"bad54\", \"name\": \"Bad\", \"version\": \"1.0\", \"game\": \"GXXE01\", \"scripts\": true}");
  write_file(bad / "scripts" / "int.lua", "local a = 1\nlocal b = a & 2\non(\"battle.damage\", function(e) return b end)\n");
  r = Check(bad, &rules);
  const mc::Finding* syn = Find(r, "lua-syntax", "scripts/int.lua");
  check(syn != nullptr && syn->line == 2 && syn->text.find("luau.md") != std::string::npos, "luau: Lua 5.4's & is a syntax error at its line, with a pointer to luau.md");
}

void TestXg(const mc::GameRules& base) {
  mc::GameRules rules = base;
  rules.gameIds = {"GXXE01", "GXXJ01"};
  const fs::path xd = sTemp / "xg_xdonly";
  write_file(xd / "mod.json", "{\"id\": \"xdonly\", \"name\": \"XD only\", \"version\": \"1.0\", \"game\": [\"xd\"]}");
  write_file(xd / "games" / "xd" / "data" / "pokemon.json", "{}");
  mc::Report r = Check(xd, &rules);
  const mc::Finding* only = Find(r, "xd-only");
  check(only != nullptr && only->severity == mc::Severity::Note && only->text.find("never load") != std::string::npos, "xg: an XD-only mod gets the xd-only note");
  check(Find(r, "manifest-game") == nullptr, "xg: \"xd\" and \"xg\" are known game names");
  const fs::path both = sTemp / "xg_both";
  write_file(both / "mod.json", "{\"id\": \"both\", \"name\": \"Both\", \"version\": \"1.0\", \"game\": [\"xd\", \"xg\"]}");
  write_file(both / "games" / "xd" / "data" / "pokemon.json", "{}");
  r = Check(both, &rules);
  check(Find(r, "xd-only") == nullptr && Find(r, "xg-folder") != nullptr && Find(r, "xg-xd-data") != nullptr, "xg: [xd, xg] with XD's data and no games/xg: folder note and the data warning");
  check(r.Format().find("XG") != std::string::npos, "xg: the report says it works with XG");
  write_file(both / "games" / "xg" / "data" / "pokemon.json", "{}");
  r = Check(both, &rules);
  check(Find(r, "xg-folder") == nullptr && Find(r, "xg-xd-data") == nullptr && Find(r, "games-folder-unlisted") == nullptr, "xg: with games/xg there is nothing to warn about");
  write_file(xd / "games" / "xg" / "data" / "pokemon.json", "{}");
  r = Check(xd, &rules);
  check(Find(r, "games-folder-unlisted", "games/XG") != nullptr, "xg: games/xg without \"xg\" in \"game\" is warned");
}

int main() {
  sTemp = fs::temp_directory_path() / ("modcheck_test_" + std::to_string(std::rand() % 100000));
  fs::remove_all(sTemp);
  fs::create_directories(sTemp);
  const mc::GameRules rules = MadeUpRules();
  TestPrimitives();
  TestGoodFixtures(rules);
  TestBadManifest(rules);
  TestAssets();
  TestUpdates();
  TestBadData(rules);
  TestBadLua(rules);
  TestLooksOrSoundRule(rules);
  TestXg(rules);
  TestRequires(rules);
  TestUnsafeFolder(rules);
  TestFormats(rules);
  TestStoryFilesAndPlugin(rules);
  TestZipScan();
  TestPacker(rules);
  TestPackerRefusals(rules);
  TestPackerDisc(rules);
  TestLayout(rules);
  TestLuau(rules);
  TestDeepPass(rules);
  std::error_code ec;
  fs::remove_all(sTemp, ec);
  std::printf(sOk ? "modcheck_test: all passed\n" : "modcheck_test: FAILED\n");
  return sOk ? 0 : 1;
}
