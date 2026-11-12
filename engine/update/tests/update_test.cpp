#include <update.h>

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;
using namespace engine::update;

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

std::vector<uint8_t> hex(const char* h) {
  std::vector<uint8_t> out;
  HexDecode(h, out);
  return out;
}

struct ZipEntry {
  std::string name, data;
  bool deflate = true;
  uint32_t crcOverride = 0;
};
void put16(std::string& s, uint32_t v) {
  s += static_cast<char>(v & 0xFF);
  s += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& s, uint32_t v) {
  put16(s, v & 0xFFFF);
  put16(s, v >> 16);
}
std::string make_zip(const std::vector<ZipEntry>& entries) {
  std::string out, dir;
  for (const ZipEntry& e : entries) {
    std::string packed = e.data;
    if (e.deflate) {
      z_stream z{};
      deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
      packed.assign(deflateBound(&z, static_cast<uLong>(e.data.size())) + 16, '\0');
      z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(e.data.data()));
      z.avail_in = static_cast<uInt>(e.data.size());
      z.next_out = reinterpret_cast<Bytef*>(packed.data());
      z.avail_out = static_cast<uInt>(packed.size());
      deflate(&z, Z_FINISH);
      packed.resize(z.total_out);
      deflateEnd(&z);
    }
    const uint32_t crc = e.crcOverride != 0 ? e.crcOverride
                                            : static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(e.data.data()),
                                                                          static_cast<uInt>(e.data.size())));
    const uint32_t offset = static_cast<uint32_t>(out.size());
    const uint16_t method = e.deflate ? 8 : 0;
    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, 0);
    put16(out, method);
    put32(out, 0);
    put32(out, crc);
    put32(out, static_cast<uint32_t>(packed.size()));
    put32(out, static_cast<uint32_t>(e.data.size()));
    put16(out, static_cast<uint32_t>(e.name.size()));
    put16(out, 0);
    out += e.name;
    out += packed;
    put32(dir, 0x02014b50);
    put16(dir, 20);
    put16(dir, 20);
    put16(dir, 0);
    put16(dir, method);
    put32(dir, 0);
    put32(dir, crc);
    put32(dir, static_cast<uint32_t>(packed.size()));
    put32(dir, static_cast<uint32_t>(e.data.size()));
    put16(dir, static_cast<uint32_t>(e.name.size()));
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put32(dir, 0);
    put32(dir, offset);
    dir += e.name;
  }
  const uint32_t dirOffset = static_cast<uint32_t>(out.size());
  out += dir;
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<uint32_t>(entries.size()));
  put16(out, static_cast<uint32_t>(entries.size()));
  put32(out, static_cast<uint32_t>(dir.size()));
  put32(out, dirOffset);
  put16(out, 0);
  return out;
}

const uint8_t kSeed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                           17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

std::string signed_file(const std::string& manifest, const uint8_t* seed, std::string* publicHex = nullptr) {
  uint8_t sig[64], pub[32];
  SignEd25519(seed, std::string(kSignedPrefix) + manifest, sig, pub);
  if (publicHex != nullptr) {
    *publicHex = HexEncode(pub, 32);
  }
  return "{\n  \"signature\": \"" + HexEncode(sig, 64) + "\",\n  \"manifest\": " + manifest + "\n}\n";
}

std::string manifest_json(const std::string& version, const std::string& url, uint64_t size, const std::string& sha,
                          const std::string& product = "test-product", const std::string& extra = "") {
  return "{\n    \"format\": 1,\n    \"product\": \"" + product + "\",\n    \"version\": \"" + version +
         "\",\n    \"date\": \"2026-11-08\",\n    \"notes\": \"Line one\\nLine two \\u00e9 \\ud83d\\ude00\",\n"
         "    \"url\": \"" + url + "\",\n    \"size\": " + std::to_string(size) + ",\n    \"sha256\": \"" + sha + "\"" +
         extra + "\n  }";
}

std::string sha_of(const fs::path& p) {
  std::string h, e;
  Sha256File(p, h, e);
  return h;
}

std::string file_url(const fs::path& p) {
  std::string s = p.generic_string();
  return "file:///" + s;
}

}

int main(int argc, char** argv) {
  wchar_t self[MAX_PATH * 4];
  const fs::path own = fs::path(std::wstring(self, GetModuleFileNameW(nullptr, self, MAX_PATH * 4))).parent_path();
  const fs::path work = argc > 1 ? fs::path(argv[1]) : own / L"update_test_tmp";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work);

  struct Vector {
    const char *seed, *pub, *msg, *sig;
  } vectors[] = {
      {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
       "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
       "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
      {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
       "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
       "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
      {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
       "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
       "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
  };
  for (const Vector& v : vectors) {
    const auto seed = hex(v.seed), pub = hex(v.pub), msgBytes = hex(v.msg), sig = hex(v.sig);
    const std::string msg(msgBytes.begin(), msgBytes.end());
    uint8_t made[64], madePub[32];
    SignEd25519(seed.data(), msg, made, madePub);
    check(std::memcmp(madePub, pub.data(), 32) == 0, std::string("RFC 8032 public key ") + v.pub);
    check(std::memcmp(made, sig.data(), 64) == 0, "RFC 8032 signature");
    check(VerifyEd25519(pub.data(), sig.data(), msg), "RFC 8032 signature verifies");
    std::vector<uint8_t> bad = sig;
    bad[10] ^= 1;
    check(!VerifyEd25519(pub.data(), bad.data(), msg), "a changed signature fails");
    check(!VerifyEd25519(pub.data(), sig.data(), msg + "x"), "a changed message fails");
  }

  std::vector<uint8_t> h;
  check(HexDecode("00ff10Ab", h) && h.size() == 4 && h[1] == 0xFF && h[3] == 0xAB, "hex decode");
  check(!HexDecode("0g", h) && !HexDecode("abc", h), "bad hex refused");
  std::vector<uint32_t> parts;
  check(ParseVersion("1.2.3", parts) && parts.size() == 3 && parts[2] == 3, "version 1.2.3");
  check(ParseVersion("v0.10", parts) && parts[1] == 10, "version v0.10");
  check(!ParseVersion("1..2", parts) && !ParseVersion("1.2-beta", parts) && !ParseVersion("", parts) &&
            !ParseVersion("1.2.3.4.5", parts) && !ParseVersion("..", parts) && !ParseVersion("1.2/", parts),
        "bad versions refused");
  check(CompareVersions("1.2", "1.2.0") == 0 && CompareVersions("1.10", "1.9") == 1 && CompareVersions("0.1.0", "0.1.1") == -1,
        "version order (numbers, not letters)");
  check(IsNewer("0.2.0", "0.1.9") && !IsNewer("0.1.0", "0.1.0") && !IsNewer("0.0.9", "0.1") && !IsNewer("x", "0.1"),
        "IsNewer");
  check(IsGameFolderName("game") && IsGameFolderName("game-1.2.0") && !IsGameFolderName("game-") &&
            !IsGameFolderName("game-v1") && !IsGameFolderName("game-1.2/..") && !IsGameFolderName("..") &&
            !IsGameFolderName("Game-1.0") && !IsGameFolderName("game-1.0.partial"),
        "game folder names");

  std::string pubHex;
  const std::string sha(64, 'a');
  const std::string m1 = manifest_json("0.2.0", "https://example.invalid/p.zip", 1234, sha);
  const std::string good = signed_file(m1, kSeed, &pubHex);
  Manifest m;
  std::string error;
  check(ParseSignedManifest(good, pubHex, "test-product", m, error), "good manifest: " + error);
  check(m.version == "0.2.0" && m.size == 1234 && m.sha256 == sha && m.url == "https://example.invalid/p.zip" &&
            m.date == "2026-11-08" && m.notes == "Line one\nLine two \xC3\xA9 \xF0\x9F\x98\x80",
        "manifest fields (escapes, UTF-8)");
  {
    std::string changed = good;
    changed.replace(changed.find("0.2.0"), 5, "9.2.0");
    check(!ParseSignedManifest(changed, pubHex, "test-product", m, error) && error.find("signature") != std::string::npos,
          "a changed version is refused (signature)");
    changed = good;
    changed.replace(changed.find("\"size\": 1234"), 12, "\"size\":  1234");
    check(!ParseSignedManifest(changed, pubHex, "test-product", m, error), "even a changed space is refused");
    changed = good;
    changed.replace(changed.find("aaaa"), 4, "bbbb");
    check(!ParseSignedManifest(changed, pubHex, "test-product", m, error), "a changed hash is refused");
    changed = good;
    changed.insert(changed.find("\"manifest\""), "\"manifest\": {}, ");
    check(!ParseSignedManifest(changed, pubHex, "test-product", m, error), "a second \"manifest\" is refused");
  }
  {
    uint8_t other[32];
    std::memcpy(other, kSeed, 32);
    other[0] ^= 0x55;
    std::string otherPub;
    const std::string byOther = signed_file(m1, other, &otherPub);
    check(!ParseSignedManifest(byOther, pubHex, "test-product", m, error), "signed with another key: refused");
    check(ParseSignedManifest(byOther, otherPub, "test-product", m, error), "(with that key's public half: accepted)");
  }
  check(!ParseSignedManifest(good, pubHex, "other-product", m, error) && error.find("another program") != std::string::npos,
        "another product: refused");
  check(!ParseSignedManifest(good, "", "test-product", m, error), "no public key: refused");
  check(!ParseSignedManifest(good, std::string(64, '0'), "test-product", m, error), "an all-zero key: refused");
  {
    std::string m2 = m1;
    m2.replace(m2.find("\"format\": 1"), 11, "\"format\": 2");
    check(!ParseSignedManifest(signed_file(m2, kSeed), pubHex, "test-product", m, error) &&
              error.find("newer format") != std::string::npos,
          "format 2: refused");
  }
  check(!ParseSignedManifest(signed_file(manifest_json("0.2.0", "ftp://x/p.zip", 1, sha), kSeed), pubHex, "test-product",
                             m, error),
        "an ftp:// package address: refused");
  check(!ParseSignedManifest(signed_file(manifest_json("0.2.0", "https://x/p.zip", 0, sha), kSeed), pubHex,
                             "test-product", m, error),
        "size 0: refused");
  check(!ParseSignedManifest(signed_file(manifest_json("0.2.0", "https://x/p.zip", 1, "ABC"), kSeed), pubHex,
                             "test-product", m, error),
        "a bad SHA-256: refused");
  check(!ParseSignedManifest(signed_file(manifest_json("0.2-rc1", "https://x/p.zip", 1, sha), kSeed), pubHex,
                             "test-product", m, error),
        "an unreadable version: refused");
  check(!ParseSignedManifest("{\"signature\": \"00\"}", pubHex, "test-product", m, error), "no manifest: refused");
  check(!ParseSignedManifest("not json", pubHex, "test-product", m, error), "not JSON: refused");
  check(!ParseSignedManifest(good.substr(0, good.size() - 3), pubHex, "test-product", m, error), "cut short: refused");
  check(!ParseSignedManifest("\xEF\xBB\xBF" + good, pubHex, "test-product", m, error), "a byte order mark: refused");

  {
    const fs::path src = work / L"feed dir" / L"update.json";
    write_file(src, good);
    std::string got;
    FetchOptions o;
    check(FetchToString(file_url(src), o, got, error) && got == good, "file:// fetch (a space in the path): " + error);
    o.maxBytes = 10;
    check(!FetchToString(file_url(src), o, got, error), "a file over the limit is refused");
    const fs::path to = work / L"dl" / L"copy.bin";
    FetchOptions big;
    big.maxBytes = 1 << 20;
    check(FetchToFile(file_url(src), to, big, error) && read_file(to) == good, "file:// download to a file");
    big.maxBytes = 10;
    check(!FetchToFile(file_url(src), to, big, error) && !fs::exists(to) && !fs::exists(fs::path(to).concat(L".download")),
          "a refused download leaves no file");
    check(!FetchToString("file:///" + (work / L"missing.json").generic_string(), o, got, error), "a missing file");
    check(!FetchToString("gopher://x", o, got, error), "an unknown scheme");
  }

  {
    std::string big;
    for (int i = 0; i < 20000; i++) {
      big += "line " + std::to_string(i) + "\n";
    }
    const fs::path zip = work / L"a.zip";
    write_file(zip, make_zip({{"top.txt", "hello", false}, {"sub/", "", false}, {"sub/deep/big.txt", big, true},
                              {"empty.txt", "", true}}));
    const fs::path out = work / L"unzipped";
    check(ExtractZip(zip, out, error), "zip unpacks: " + error);
    check(read_file(out / L"top.txt") == "hello" && read_file(out / L"sub" / L"deep" / L"big.txt") == big &&
              fs::exists(out / L"empty.txt") && fs::file_size(out / L"empty.txt") == 0,
          "zip contents (stored, deflated, empty)");
    for (const char* bad : {"../evil.txt", "/abs.txt", "C:/x.txt", "a/../../b.txt", "a\\..\\..\\b.txt", "a//b.txt",
                            "con:x", "dot./x"}) {
      write_file(zip, make_zip({{"ok.txt", "x", true}, {bad, "evil", true}}));
      const fs::path o2 = work / L"unzipped_bad";
      fs::remove_all(o2, ec);
      check(!ExtractZip(zip, o2, error) && !fs::exists(work / L"evil.txt") && !fs::exists(work / L"b.txt"),
            std::string("zip name refused: ") + bad);
    }
    write_file(zip, make_zip({{"x.txt", big, true, 0x12345678}}));
    check(!ExtractZip(zip, work / L"unzipped_crc", error) && error.find("damaged") != std::string::npos,
          "a wrong CRC is refused");
    std::string cut = make_zip({{"x.txt", big, true}});
    write_file(zip, cut.substr(0, cut.size() / 2));
    check(!ExtractZip(zip, work / L"unzipped_cut", error), "half a zip is refused");
    write_file(zip, "PK not really");
    check(!ExtractZip(zip, work / L"unzipped_junk", error), "junk is refused");
  }

  {
    const fs::path root = work / L"Player folder";
    const std::vector<std::string> required = {"launcher.exe", "game.exe"};
    write_file(root / L"Starter.exe", "starter");
    write_file(root / L"game" / L"launcher.exe", "launcher 0.1.0");
    write_file(root / L"game" / L"game.exe", "game 0.1.0");
    write_file(root / L"game" / L"version.txt", "0.1.0\r\n");
    Layout l = FindLayout(root / L"game", "Starter.exe");
    check(l.valid && l.running == "game" && l.current == "game" && l.previous.empty(), "layout of a first install");
    check(!FindLayout(work / L"unzipped", "Starter.exe").valid, "a build folder is no player's folder");
    check(FolderVersion(root / L"game") == "0.1.0", "version.txt read");

    const auto package = [&](const std::string& version, const std::string& launcherText) {
      const fs::path zip = work / L"staging" / fs::path("p-" + version + ".zip");
      write_file(zip, make_zip({{"launcher.exe", launcherText, true}, {"game.exe", "game " + version, true},
                                {"version.txt", version + "\r\n", false}, {"fonts/a.ttf", "font", true}}));
      return zip;
    };
    InstallRequest r;
    r.layout = l;
    r.requiredFiles = required;
    r.package = package("0.2.0", "launcher 0.2.0");
    r.manifest.version = "0.2.0";
    r.manifest.size = fs::file_size(r.package);
    r.manifest.sha256 = std::string(64, '0');
    std::string installed;
    check(!Install(r, installed, error) && error.find("SHA-256") != std::string::npos, "a wrong hash is refused");
    check(!fs::exists(root / L"game-0.2.0") && !fs::exists(root / L"game-0.2.0.partial") &&
              !fs::exists(root / kPointerFile),
          "... and nothing is left behind");
    r.manifest.sha256 = sha_of(r.package);
    r.manifest.size += 1;
    check(!Install(r, installed, error) && error.find("size") != std::string::npos, "a wrong size is refused");
    r.manifest.size -= 1;
    r.manifest.version = "0.3.0";
    check(!Install(r, installed, error) && !fs::exists(root / L"game-0.3.0") && !fs::exists(root / L"game-0.3.0.partial"),
          "a package of another version is refused: " + error);
    r.manifest.version = "0.2.0";
    write_file(root / L"game-0.2.0.partial" / L"junk.txt", "junk");
    check(Install(r, installed, error) && installed == "game-0.2.0", "install 0.2.0: " + error);
    check(read_file(root / L"game-0.2.0" / L"launcher.exe") == "launcher 0.2.0" &&
              read_file(root / L"game-0.2.0" / L"fonts" / L"a.ttf") == "font" &&
              !fs::exists(root / L"game-0.2.0" / L"junk.txt") && !fs::exists(root / L"game-0.2.0.partial"),
          "the new folder is complete");
    check(read_file(root / L"game" / L"launcher.exe") == "launcher 0.1.0", "the running folder is untouched");
    std::string cur, prev;
    check(ReadPointer(root, cur, prev) && cur == "game-0.2.0" && prev == "game", "pointer: current 0.2.0, previous game");
    check(!fs::exists(fs::path(root / kPointerFile).concat(L".new")), "no half-written pointer file");

    l = FindLayout(root / L"game-0.2.0", "Starter.exe");
    check(l.valid && l.running == "game-0.2.0" && l.current == "game-0.2.0" && l.previous == "game", "layout after the update");
    r.layout = l;
    r.package = package("0.3.0", "launcher 0.3.0");
    r.manifest.version = "0.3.0";
    r.manifest.size = fs::file_size(r.package);
    r.manifest.sha256 = sha_of(r.package);
    check(Install(r, installed, error), "install 0.3.0: " + error);
    check(ReadPointer(root, cur, prev) && cur == "game-0.3.0" && prev == "game-0.2.0", "pointer: 0.3.0, previous 0.2.0");
    check(fs::exists(root / L"game"), "the first folder is kept");
    l = FindLayout(root / L"game-0.3.0", "Starter.exe");
    r.layout = l;
    r.package = package("0.4.0", "launcher 0.4.0");
    r.manifest.version = "0.4.0";
    r.manifest.size = fs::file_size(r.package);
    r.manifest.sha256 = sha_of(r.package);
    write_file(root / L"game-0.1.5" / L"portable" / L"save.txt", "player data");
    check(Install(r, installed, error), "install 0.4.0: " + error);
    check(!fs::exists(root / L"game-0.2.0") && fs::exists(root / L"game-0.3.0") && fs::exists(root / L"game") &&
              fs::exists(root / L"game-0.1.5" / L"portable" / L"save.txt"),
          "older update folders go; game\\, the previous one and folders with player data stay");

    l = FindLayout(root / L"game-0.4.0", "Starter.exe");
    check(GoBack(l, required, error) && ReadPointer(root, cur, prev) && cur == "game-0.3.0" && prev == "game-0.4.0",
          "go back: 0.3.0 next time: " + error);
    l = FindLayout(root / L"game-0.4.0", "Starter.exe");
    check(GoBack(l, required, error) && ReadPointer(root, cur, prev) && cur == "game-0.4.0", "and forward again");
    fs::remove_all(root / L"game-0.3.0");
    l = FindLayout(root / L"game-0.4.0", "Starter.exe");
    check(!GoBack(l, required, error), "no going back to a folder that is gone");
    write_file(root / L"game-0.4.0" / L"data_location.json", "{\"version\": 1, \"mode\": \"portable\"}");
    r.layout = FindLayout(root / L"game-0.4.0", "Starter.exe");
    r.package = package("0.5.0", "launcher 0.5.0");
    r.manifest.version = "0.5.0";
    r.manifest.size = fs::file_size(r.package);
    r.manifest.sha256 = sha_of(r.package);
    check(!Install(r, installed, error) && !fs::exists(root / L"game-0.5.0") && error.find("portable") != std::string::npos,
          "portable data inside the game folder: refused");
    write_file(root / kPointerFile, "current=..\\..\\Windows\nprevious=game\n");
    check(!ReadPointer(root, cur, prev) && cur.empty() && prev == "game", "a pointer to a path is ignored");
  }

  fs::remove_all(work, ec);
  std::printf(sOk ? "update_test: all passed\n" : "update_test: FAILED\n");
  return sOk ? 0 : 1;
}
