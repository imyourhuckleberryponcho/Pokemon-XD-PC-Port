#include "modpack.h"

#include "mod_layout.h"

#include "json.h"
#include "modsafety.h"

#include <gs_formats.h>
#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>

namespace fs = std::filesystem;
namespace em = engine::mods;

namespace engine::modcheck {

namespace {

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

void Put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(static_cast<uint8_t>(x)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void Put32(std::vector<uint8_t>& v, uint32_t x) { Put16(v, x & 0xFFFF); Put16(v, x >> 16); }

std::string JsonEscape(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) out += Fmt("\\u%04x", c);
      else out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

std::string SafeFileStem(const std::string& s) {
  std::string out;
  for (char c : s) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') ? c : '_';
  return out;
}

bool ResolveEntry(const FsysArchive& fsys, const std::string& name, uint32_t& index) {
  if (name.size() >= 2 && name[0] == '#') {
    char* end = nullptr;
    const unsigned long n = std::strtoul(name.c_str() + 1, &end, 10);
    if (end != name.c_str() + 1 && (*end == '\0' || *end == '.') && n < fsys.count) {
      index = static_cast<uint32_t>(n);
      return true;
    }
  }
  const std::string want = LowerAscii(name);
  std::vector<uint32_t> shortMatches;
  for (uint32_t i = 0; i < fsys.count; i++) {
    FsysEntry e;
    if (!Fsys_Entry(&fsys, i, &e)) continue;
    if (e.fullName[0] != '\0' && LowerAscii(e.fullName) == want) {
      index = i;
      return true;
    }
    if (LowerAscii(e.name) == want) shortMatches.push_back(i);
  }
  if (shortMatches.size() == 1) {
    index = shortMatches[0];
    return true;
  }
  return false;
}

std::string SectionOf(const std::string& path) { return path.substr(0, path.find('/')); }

}

std::vector<Refusal> GameDataReasons(const std::string& path, const std::vector<uint8_t>& head) {
  std::vector<Refusal> out;
  const std::string lower = LowerAscii(path);
  const std::string name = lower.substr(lower.rfind('/') == std::string::npos ? 0 : lower.rfind('/') + 1);
  const size_t dot = name.rfind('.');
  const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
  const auto add = [&](const char* code, const std::string& reason) { out.push_back({path, code, reason}); };

  static const std::set<std::string> kDisc = {"iso", "gcm", "ciso", "wbfs", "rvz", "wia", "tgc", "nfs", "dol", "fsys", "rel"};
  static const std::set<std::string> kSave = {"gci", "sav", "gcs", "mcd", "mci", "gcp", "raw", "srm"};
  static const std::set<std::string> kLog = {"log", "dmp", "mdmp", "dff"};
  if (kDisc.count(ext) != 0) {
    add("game-archive", "a disc image or disc-format file (." + ext + "): game data, never shared. Share your own changes (data/, scripts/) or single changed entries under fsys/");
  }
  if (kSave.count(ext) != 0) add("save", "a save file or memory card image (." + ext + "): it belongs to one player");
  if (kLog.count(ext) != 0) add("log", "a log, crash dump or capture (." + ext + "): it can hold the player's names and paths");
  if (name == "mc_system_area" || name == "settings.ini" || name == "mods.ini" || name == "slots.json" || name == "data_location.json") {
    add("save", "one of the port's own player files (" + name + "): it belongs to one player");
  }
  if (name == "dsp_rom.bin" || name == "dsp_coef.bin") add("game-data", "a sound chip dump (" + name + "): it comes from a console");
  if (std::regex_match(name, std::regex(R"((play|test)_\d{8}.*\.txt)"))) add("log", "a recording of a play session: it belongs to one player");
  {
    size_t from = 0;
    while (true) {
      const size_t to = lower.find('/', from);
      if (to == std::string::npos) break;
      const std::string seg = lower.substr(from, to - from);
      static const std::set<std::string> kDirs = {"texture_dump", "card a", "card b", "save_backups", "saves", "recordings", "reports",
                                                  "logs", "pbr_models", "extra_assets", "dumped_textures"};
      if (kDirs.count(seg) != 0 || seg.rfind("report_", 0) == 0) {
        add("game-data", "inside a folder (\"" + seg + "\") that holds the port's dumps, saves or generated assets");
        break;
      }
      from = to + 1;
    }
  }
  if (head.size() >= 4) {
    if (std::memcmp(head.data(), "FSYS", 4) == 0) add("game-archive", "the contents are a game archive (FSYS): game data, never shared");
    if (std::memcmp(head.data(), "CISO", 4) == 0 || std::memcmp(head.data(), "WBFS", 4) == 0 || std::memcmp(head.data(), "RVZ\x01", 4) == 0 ||
        std::memcmp(head.data(), "WIA\x01", 4) == 0) {
      add("game-archive", "the contents are a compressed disc image");
    }
  }
  if (head.size() >= 0x20 && head[0x1C] == 0xC2 && head[0x1D] == 0x33 && head[0x1E] == 0x9F && head[0x1F] == 0x3D) {
    add("game-archive", "the contents are a GameCube disc image");
  }
  if (head.size() >= 6) {
    static const char* const kIds[] = {"GXXE01", "GXXJ01", "GXXP01", "GC6E01", "GC6J01", "GC6P01"};
    for (const char* id : kIds) {
      if (std::memcmp(head.data(), id, 6) == 0 && head.size() >= 0x40) add("save", "the contents are a memory card save of the game");
    }
  }
  return out;
}

std::vector<uint8_t> MakeZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  std::vector<uint8_t> zip;
  struct Central {
    std::string name;
    uint32_t crc, packed, size, offset;
    uint16_t method;
  };
  std::vector<Central> central;
  const uint32_t kTime = 0, kDate = (0 << 9) | (1 << 5) | 1;
  for (const auto& [name, data] : files) {
    Central c;
    c.name = name;
    c.size = static_cast<uint32_t>(data.size());
    c.crc = Crc32(data.data(), data.size());
    c.offset = static_cast<uint32_t>(zip.size());
    std::vector<uint8_t> packed;
    c.method = 0;
    if (!data.empty()) {
      z_stream z{};
      deflateInit2(&z, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);
      packed.resize(deflateBound(&z, static_cast<uLong>(data.size())));
      z.next_in = const_cast<uint8_t*>(data.data());
      z.avail_in = static_cast<uInt>(data.size());
      z.next_out = packed.data();
      z.avail_out = static_cast<uInt>(packed.size());
      deflate(&z, Z_FINISH);
      packed.resize(z.total_out);
      deflateEnd(&z);
      if (packed.size() < data.size()) c.method = 8;
    }
    const std::vector<uint8_t>& body = c.method == 8 ? packed : data;
    c.packed = static_cast<uint32_t>(body.size());
    Put32(zip, 0x04034B50);
    Put16(zip, 20);
    Put16(zip, 0x0800);
    Put16(zip, c.method);
    Put16(zip, kTime);
    Put16(zip, kDate);
    Put32(zip, c.crc);
    Put32(zip, c.packed);
    Put32(zip, c.size);
    Put16(zip, static_cast<uint32_t>(name.size()));
    Put16(zip, 0);
    zip.insert(zip.end(), name.begin(), name.end());
    zip.insert(zip.end(), body.begin(), body.end());
    central.push_back(std::move(c));
  }
  const uint32_t cdStart = static_cast<uint32_t>(zip.size());
  for (const Central& c : central) {
    Put32(zip, 0x02014B50);
    Put16(zip, 3 << 8 | 20);
    Put16(zip, 20);
    Put16(zip, 0x0800);
    Put16(zip, c.method);
    Put16(zip, kTime);
    Put16(zip, kDate);
    Put32(zip, c.crc);
    Put32(zip, c.packed);
    Put32(zip, c.size);
    Put16(zip, static_cast<uint32_t>(c.name.size()));
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 0);
    Put32(zip, 0100644u << 16);
    Put32(zip, c.offset);
    zip.insert(zip.end(), c.name.begin(), c.name.end());
  }
  const uint32_t cdSize = static_cast<uint32_t>(zip.size()) - cdStart;
  Put32(zip, 0x06054B50);
  Put16(zip, 0);
  Put16(zip, 0);
  Put16(zip, static_cast<uint32_t>(central.size()));
  Put16(zip, static_cast<uint32_t>(central.size()));
  Put32(zip, cdSize);
  Put32(zip, cdStart);
  Put16(zip, 0);
  return zip;
}

std::string UpdateJson(const std::string& id, const std::string& version, const std::string& url, const std::string& sha256, uint64_t size,
                       const std::string& notes, const std::vector<std::string>& games, const std::vector<std::string>& permissions) {
  std::string out = "{\n  \"format\": 1,\n";
  out += "  \"id\": " + JsonEscape(id) + ",\n";
  out += "  \"version\": " + JsonEscape(version) + ",\n";
  out += "  \"url\": " + JsonEscape(url) + ",\n";
  out += "  \"sha256\": " + JsonEscape(sha256) + ",\n";
  out += "  \"size\": " + std::to_string(size) + ",\n";
  out += "  \"notes\": " + JsonEscape(notes) + ",\n";
  out += "  \"games\": [";
  for (size_t i = 0; i < games.size(); i++) out += (i ? ", " : "") + JsonEscape(games[i]);
  out += "],\n  \"permissions\": [";
  for (size_t i = 0; i < permissions.size(); i++) out += (i ? ", " : "") + JsonEscape(permissions[i]);
  out += "]\n}\n";
  return out;
}

std::string PackResult::Format() const {
  std::string out = report.Format();
  if (!refused.empty()) {
    out += "\nREFUSED (game data or a player's own files: these can never go in a shared mod)\n";
    for (const Refusal& r : refused) out += "  " + r.path + ": " + r.reason + " [" + r.code + "]\n";
  }
  if (!leftOut.empty()) {
    out += "\nLEFT OUT (editor and system leftovers)\n";
    for (const std::string& p : leftOut) out += "  " + p + "\n";
  }
  for (const std::string& m : messages) out += "\n" + m;
  if (ok) {
    out += Fmt("\n\nPacked %zu file(s) into %s\n  size %llu bytes, sha256 %s\n", included.size(), PathText(zip).c_str(), static_cast<unsigned long long>(size), sha256.c_str());
    if (!updateFile.empty()) out += "  update file: " + PathText(updateFile) + " (upload it next to the zip; the mod's \"updates\" link must point at it)\n";
  } else {
    out += "\n\nNothing was packed: " + error + "\n";
  }
  return out;
}

PackResult PackMod(const PackOptions& o) {
  PackResult r;
  r.report = CheckMod(o.check);
  const fs::path folder = o.check.folder;
  const SafetyLimits limits;
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    r.error = "there is no folder " + PathText(folder);
    return r;
  }

  uint8_t signSeed[32] = {};
  std::string signPublic;
  if (!o.signKey.empty()) {
    std::string keyError;
    if (!ReadKeyFile(o.signKey, signSeed, signPublic, keyError)) {
      r.error = keyError;
      return r;
    }
    const fs::path keyAbs = fs::weakly_canonical(o.signKey, ec), modAbs = fs::weakly_canonical(folder, ec);
    const std::string k = PathText(keyAbs), m2 = PathText(modAbs) + "/";
    if (k.size() > m2.size() && k.compare(0, m2.size(), m2) == 0) {
      r.error = "the signing key file is inside the mod's folder: the private key must never travel with the mod. Move it somewhere else.";
      return r;
    }
    if (r.report.manifestOk && !r.report.manifest.authorKey.empty() && r.report.manifest.authorKey != signPublic) {
      r.error = "mod.json's \"author_key\" is not this key's public half (" + signPublic + "): launchers would refuse the update. Use the right key, or fix author_key.";
      return r;
    }
  }

  struct Item {
    std::string rel;
    fs::path path;
    uint64_t size;
  };
  std::vector<Item> items;
  for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::file_status st = it->symlink_status(ec);
    if (fs::is_symlink(st)) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    const std::string rel = PathText(it->path().lexically_relative(folder));
    if (IsJunkPath(rel)) {
      r.leftOut.push_back(rel);
      continue;
    }
    items.push_back({rel, it->path(), it->file_size(ec)});
  }
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    const bool am = a.rel == "mod.json", bm = b.rel == "mod.json";
    return am != bm ? am : a.rel < b.rel;
  });
  std::sort(r.leftOut.begin(), r.leftOut.end());

  std::map<std::string, std::unique_ptr<std::vector<uint8_t>>> archives;
  std::vector<std::pair<std::string, std::vector<uint8_t>>> zipFiles;
  const std::string prefix = r.report.manifestOk ? r.report.manifest.id + "/" : std::string();
  uint64_t total = 0;
  std::regex dumpName(R"(^(tex1|arb1)_(\d+)x(\d+)_.*\.(png|dds)$)", std::regex::icase);
  std::map<std::string, std::string> discLower;
  if (o.disc != nullptr) {
    for (const std::string& f : o.disc->files) discLower[LowerAscii(f)] = f;
  }
  for (const Item& it : items) {
    std::vector<uint8_t> bytes;
    const bool readable = it.size <= limits.maxFileBytes && ReadFileBytes(it.path, bytes, limits.maxFileBytes);
    std::vector<uint8_t> head(bytes.begin(), bytes.begin() + std::min<size_t>(bytes.size(), 4096));
    std::vector<Refusal> why = GameDataReasons(it.rel, head);
    if (readable && !bytes.empty()) {
      if (!o.refuseHashes.empty() && o.refuseHashes.count(Sha256Hex(bytes.data(), bytes.size())) != 0) {
        why.push_back({it.rel, "known-hash", "its contents are a known file of the game"});
      }
      const std::string inner = engine::mods::StripGameFolder(it.rel);
      const std::string section = LowerAscii(SectionOf(inner));
      if (o.disc != nullptr && o.disc->read && section == "files") {
        const auto found = discLower.find(LowerAscii(inner.substr(6)));
        std::vector<uint8_t> original;
        if (found != discLower.end() && o.disc->read(found->second, original) && original == bytes) {
          why.push_back({it.rel, "same-as-disc", "this is an unchanged copy of the disc's " + found->second + ": share only what you changed"});
        }
      } else if (o.disc != nullptr && o.disc->read && section == "fsys") {
        const size_t slash = inner.find('/', 5);
        if (slash != std::string::npos) {
          const std::string archive = inner.substr(5, slash - 5), entry = inner.substr(slash + 1);
          const auto found = discLower.find(LowerAscii(archive));
          if (found != discLower.end()) {
            auto& slot = archives[found->second];
            if (!slot) {
              slot = std::make_unique<std::vector<uint8_t>>();
              if (!o.disc->read(found->second, *slot)) slot->clear();
            }
            FsysArchive fsys{};
            uint32_t index = 0;
            if (!slot->empty() && Fsys_Open(slot->data(), slot->size(), &fsys) && ResolveEntry(fsys, entry, index)) {
              uint32_t n = 0;
              uint8_t* p = Fsys_Extract(&fsys, index, &n);
              const bool same = p != nullptr && std::vector<uint8_t>(p, p + n) == bytes;
              std::free(p);
              if (same) why.push_back({it.rel, "same-as-disc", "this is an unchanged copy of an entry of the disc's " + found->second + ": share only what you changed"});
            }
          }
        }
      }
      const std::string base = it.rel.substr(it.rel.rfind('/') == std::string::npos ? 0 : it.rel.rfind('/') + 1);
      std::smatch m;
      if (!o.allowSameSizeTextures && std::regex_match(base, m, dumpName)) {
        uint32_t w = 0, h = 0;
        if (LowerAscii(m[4].str()) == "png") {
          Report scratch;
          CheckPng(it.rel, bytes, limits, scratch, &w, &h);
        } else if (bytes.size() >= 20 && std::memcmp(bytes.data(), "DDS ", 4) == 0) {
          h = static_cast<uint32_t>(bytes[12] | bytes[13] << 8 | bytes[14] << 16);
          w = static_cast<uint32_t>(bytes[16] | bytes[17] << 8 | bytes[18] << 16);
        }
        if (w != 0 && w == static_cast<uint32_t>(std::atoi(m[2].str().c_str())) && h == static_cast<uint32_t>(std::atoi(m[3].str().c_str()))) {
          why.push_back({it.rel, "texture-dump", Fmt("a %u x %u picture named like a texture the port dumped from the game, and the same size as the game's own: it looks like a straight dump (a replacement is bigger). If it is your own redrawing at the same size, pack with --allow-same-size-textures", w, h)});
        }
      }
    }
    if (!why.empty()) {
      r.refused.insert(r.refused.end(), why.begin(), why.end());
      continue;
    }
    if (!readable) {
      r.refused.push_back({it.rel, "unreadable", "this file can't be read (or is too big)"});
      continue;
    }
    total += bytes.size();
    r.included.push_back(it.rel);
    zipFiles.emplace_back(prefix + it.rel, std::move(bytes));
  }

  if (!r.report.manifestOk) {
    r.error = "mod.json is not usable (see the problems above)";
    return r;
  }
  if (r.report.Count(Severity::Problem) > 0) {
    r.error = Fmt("the checker found %zu problem(s)%s", r.report.Count(Severity::Problem),
                  r.report.UnsafeProblems() > 0 ? ", some of them about safety (a mod with those is never packed)" : "");
    return r;
  }
  if (!r.refused.empty()) {
    r.error = Fmt("%zu file(s) are game data or a player's own files", r.refused.size());
    return r;
  }
  if (o.strict && r.report.Count(Severity::Warning) > 0) {
    r.error = "there are warnings and --strict was given";
    return r;
  }
  if (total > limits.maxTotalBytes) {
    r.error = "the mod is bigger than the port accepts";
    return r;
  }

  const em::Manifest& m = r.report.manifest;
  const std::string stem = SafeFileStem(m.id) + (m.version.empty() ? std::string() : "-" + SafeFileStem(m.version));
  fs::path outDir = o.outDir.empty() ? folder.parent_path() : o.outDir;
  if (outDir.empty()) outDir = ".";
  fs::create_directories(outDir, ec);
  r.zip = outDir / (stem + ".zip");
  const std::vector<uint8_t> zip = MakeZip(zipFiles);
  {
    Report scan;
    if (!ScanZip(zip, limits, scan)) {
      r.error = "the zip made here fails its own safety scan: " + (scan.findings.empty() ? std::string("?") : scan.findings.front().text);
      r.zip.clear();
      return r;
    }
  }
  {
    const fs::path tmp = r.zip.string() + ".part";
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
    out.close();
    if (!out) {
      r.error = "could not write " + PathText(r.zip);
      fs::remove(tmp, ec);
      return r;
    }
    fs::rename(tmp, r.zip, ec);
    if (ec) {
      fs::remove(r.zip, ec);
      fs::rename(tmp, r.zip, ec);
    }
  }
  r.size = zip.size();
  r.sha256 = Sha256Hex(zip.data(), zip.size());

  std::string updates;
  {
    std::vector<uint8_t> manifestBytes;
    em::json::Value root;
    if (ReadFileBytes(folder / "mod.json", manifestBytes, 1 << 20)) {
      em::json::Reader reader(std::string_view(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()));
      if (reader.Document(root) && root.kind == em::json::Value::Kind::Object && root.fields.count("updates") &&
          root.fields.at("updates").kind == em::json::Value::Kind::String) {
        updates = root.fields.at("updates").text;
      }
    }
  }
  if (!updates.empty()) {
    std::string notes = o.notes;
    if (notes.empty()) {
      for (const char* name : {"CHANGES.md", "CHANGES.txt", "CHANGES", "CHANGELOG.md", "CHANGELOG.txt", "CHANGELOG"}) {
        std::vector<uint8_t> b;
        if (ReadFileBytes(folder / name, b, 1 << 20) && !b.empty()) {
          notes.assign(b.begin(), b.begin() + std::min<size_t>(b.size(), 4000));
          break;
        }
      }
    }
    std::string url;
    if (!o.urlBase.empty()) {
      url = o.urlBase;
      if (url.back() != '/') url += '/';
      url += PathText(r.zip.filename());
      if (LowerAscii(url).rfind("https://", 0) != 0) r.messages.push_back("Warning: the update file's \"url\" is not https://, so the launcher will refuse it.");
    } else {
      r.messages.push_back("The update file's \"url\" is empty: put the address the zip will have after you upload it there (or pack again with --url-base).");
    }
    std::vector<std::string> codes;
    for (const Permission& p : r.report.permissions) {
      if (std::find(codes.begin(), codes.end(), p.code) == codes.end()) codes.push_back(p.code);
    }
    r.updateFile = outDir / (SafeFileStem(m.id) + ".update.json");
    std::ofstream out(r.updateFile, std::ios::binary);
    if (!o.signKey.empty()) {
      UpdateFile fields;
      fields.id = m.id;
      fields.version = m.version;
      fields.url = url;
      fields.sha256 = r.sha256;
      fields.size = r.size;
      fields.notes = notes;
      fields.games = m.games;
      fields.permissions = codes;
      std::string pub;
      out << SignedUpdateText(fields, signSeed, pub);
      r.messages.push_back("The update file is signed with the key " + pub + " (fingerprint " + KeyFingerprint(pub) +
                           "). Launchers pin this key the first time they install the mod: always sign with the same key.");
    } else {
      out << UpdateJson(m.id, m.version, url, r.sha256, r.size, notes, m.games, codes);
      r.messages.push_back("The update file is NOT signed (no --key): the launcher will not install updates from it. Players can still install the zip by hand.");
    }
  }
  if (updates.empty() && !o.signKey.empty()) r.messages.push_back("A signing key was given but mod.json has no \"updates\" link, so no update file was written.");
  r.ok = true;
  return r;
}

}
