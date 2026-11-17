#include "modupdate.h"

#include "json.h"
#include "update.h"

#include <mod_layout.h>
#include <modsafety.h>
#include <playthrough.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

namespace engine::modupdate {

namespace em = engine::mods;
namespace mc = engine::modcheck;
namespace eu = engine::update;
namespace fs = std::filesystem;

namespace {

constexpr uint64_t kMaxModDownload = 512ull << 20;

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string ReadText(const fs::path& file, size_t limit = 1 << 20) {
  std::error_code ec;
  const auto size = fs::file_size(file, ec);
  if (ec || size > limit) return {};
  std::ifstream in(file, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteText(const fs::path& file, const std::string& text) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path tmp = fs::path(file).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::binary);
    out << text;
    if (!out) return false;
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(file, ec);
    fs::rename(tmp, file, ec);
  }
  return !ec;
}

std::string JsonEscape(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c < 0x20) out += ' ';
    else out += static_cast<char>(c);
  }
  return out + "\"";
}

std::string Today() {
  const std::time_t t = std::time(nullptr);
  char buf[32];
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

bool MoveFolder(const fs::path& from, const fs::path& to, std::string& error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::rename(from, to, ec);
  if (!ec) return true;
  ec.clear();
  fs::copy(from, to, fs::copy_options::recursive, ec);
  if (ec) {
    error = "couldn't move " + mc::PathText(from) + ": " + ec.message();
    std::error_code ignore;
    fs::remove_all(to, ignore);
    return false;
  }
  fs::remove_all(from, ec);
  return true;
}

std::string VersionOfFolder(const fs::path& folder) {
  em::Manifest m;
  std::string err;
  if (em::ParseManifest(ReadText(folder / "mod.json"), m, err)) return m.version;
  return {};
}

}

bool AllowLocalForTests() {
  const char* v = std::getenv("XD_MODUPDATE_ALLOW_LOCAL");
  return v != nullptr && std::string(v) == "1";
}

std::string HostOf(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return {};
  const size_t start = scheme + 3;
  size_t end = url.find_first_of("/?#", start);
  std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
  const size_t at = host.rfind('@');
  if (at != std::string::npos) host = host.substr(at + 1);
  return host;
}

bool UrlAllowed(const std::string& url, std::string& why) {
  const std::string low = Lower(url);
  if (low.rfind("https://", 0) == 0) {
    if (HostOf(url).empty()) {
      why = "the address has no server name";
      return false;
    }
    if (url.find('@') != std::string::npos && url.find('@') < url.find('/', 8)) {
      why = "the address has a name and password in it";
      return false;
    }
    return true;
  }
  if (AllowLocalForTests() && (low.rfind("http://127.0.0.1", 0) == 0 || low.rfind("http://localhost", 0) == 0)) return true;
  why = low.rfind("http://", 0) == 0 ? "only https:// addresses are used for mod updates (this one is http://, which anyone on the way could change)"
                                      : "only https:// addresses are used for mod updates";
  return false;
}

Choices LoadChoices(const Folders& f) {
  Choices c;
  std::istringstream in(ReadText(f.Ini()));
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line == "asked=yes") c.asked = true;
    else if (line == "always=yes") c.always = true;
    else if (line.rfind("skip=", 0) == 0) {
      const size_t sp = line.find(' ', 5);
      if (sp != std::string::npos) c.skipped[line.substr(5, sp - 5)] = line.substr(sp + 1);
    }
  }
  return c;
}

bool SaveChoices(const Folders& f, const Choices& c) {
  std::string text = "# The launcher's mod update choices. Delete this file to be asked again.\r\n";
  if (c.asked) text += "asked=yes\r\n";
  if (c.always) text += "always=yes\r\n";
  for (const auto& [id, version] : c.skipped) text += "skip=" + id + " " + version + "\r\n";
  return WriteText(f.Ini(), text);
}

std::string KeyStore::Pinned(const std::string& id) const {
  const auto it = keys.find(Lower(id));
  return it == keys.end() ? std::string() : it->second;
}

KeyStore LoadKeys(const Folders& f) {
  KeyStore store;
  const std::string text = ReadText(f.Keys());
  if (text.empty()) return store;
  em::json::Value root;
  em::json::Reader reader(text);
  if (!reader.Document(root) || root.kind != em::json::Value::Kind::Object) return store;
  const auto keys = root.fields.find("keys");
  if (keys == root.fields.end()) return store;
  for (const auto& [id, v] : keys->second.fields) {
    if (v.kind == em::json::Value::Kind::String && v.text.size() == 64) store.keys[Lower(id)] = v.text;
  }
  return store;
}

bool SaveKeys(const Folders& f, const KeyStore& store) {
  std::string text = "{\n  \"note\": \"Author keys the launcher pinned the first time it installed a mod's update (trust on first use). Delete a line to be asked about that author again.\",\n  \"keys\": {";
  bool first = true;
  for (const auto& [id, key] : store.keys) {
    text += std::string(first ? "\n" : ",\n") + "    " + JsonEscape(id) + ": " + JsonEscape(key);
    first = false;
  }
  text += "\n  }\n}\n";
  return WriteText(f.Keys(), text);
}

TrustVerdict EvaluateTrust(const mc::UpdateFile& file, const std::string& expectedId, const std::string& pinned,
                           const std::string& authorKey) {
  TrustVerdict v;
  if (!expectedId.empty() && Lower(file.id) != Lower(expectedId)) {
    v.text = "This update file is for another mod (\"" + file.id + "\"), not \"" + expectedId + "\". Refused.";
    return v;
  }
  if (!file.isSigned) {
    v.text = "The update file isn't signed, so the launcher can't tell that it comes from the mod's author. Refused. "
             "(Mods without signing still work when you install them by hand; they just can't update themselves.)";
    return v;
  }
  if (!file.signatureValid) {
    v.text = "The update file's signature is wrong: it was changed after the author signed it, or it was signed by someone else. Refused.";
    return v;
  }
  if (!pinned.empty() && pinned != file.publicKey) {
    v.text = "This update is signed with a DIFFERENT key than the one this mod was first installed with (the author's key was " +
             mc::KeyFingerprint(pinned) + ", this one is " + mc::KeyFingerprint(file.publicKey) +
             "). That can mean the mod changed hands, or that someone is pretending to be the author. Refused.";
    return v;
  }
  if (!authorKey.empty() && authorKey != file.publicKey) {
    v.text = "The mod says its author's key is " + mc::KeyFingerprint(authorKey) + ", but this update is signed with " +
             mc::KeyFingerprint(file.publicKey) + ". Refused.";
    return v;
  }
  if (pinned.empty()) {
    v.trust = Trust::FirstUse;
    v.text = "First update of this mod from the launcher: the author's key " + mc::KeyFingerprint(file.publicKey) +
             " will be remembered, and later updates must be signed with the same key.";
    return v;
  }
  v.trust = Trust::Trusted;
  v.text = "Signed with the author's key " + mc::KeyFingerprint(file.publicKey) + " (the same one as before).";
  return v;
}

std::string PermissionText(const std::string& code) {
  if (code == "scripts") return "Runs Lua scripts in the game's sandbox (they can read and change what the game offers them, nothing else)";
  if (code == "replaces-files") return "Replaces game files (whole files, or files inside the game's archives)";
  if (code == "changes-tables") return "Changes numbers in the game's tables (stats, moves, trainers, shops...)";
  if (code == "adds-rows") return "Adds new entries to the game's tables (new Pokemon, moves, items)";
  if (code == "adds-story") return "Adds or changes story rooms, scripts or objects";
  if (code == "update-link") return "Has an update link: the launcher may ask that address for newer versions";
  if (code == "textures") return "Replaces pictures (textures)";
  if (code == "text-changes") return "Rewrites game message text";
  return code;
}

std::vector<std::string> AddedPermissions(const std::vector<std::string>& before, const std::vector<std::string>& now) {
  std::vector<std::string> out;
  for (const std::string& c : now) {
    if (std::find(before.begin(), before.end(), c) == before.end() && std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
  }
  return out;
}

std::vector<std::string> PermissionCodes(const mc::Report& report) {
  std::vector<std::string> out;
  for (const mc::Permission& p : report.permissions) {
    if (std::find(out.begin(), out.end(), p.code) == out.end()) out.push_back(p.code);
  }
  return out;
}

Offer FetchOffer(const std::string& url, const std::string& expectedId, const std::string& installedVersion,
                 const fs::path& installedFolder, const std::string& authorKey, const KeyStore& keys, const mc::GameRules* rules,
                 const std::function<bool()>& stop) {
  Offer offer;
  offer.installedVersion = installedVersion;
  std::string why;
  if (!UrlAllowed(url, why)) {
    offer.error = why;
    return offer;
  }
  eu::FetchOptions options;
  options.maxBytes = mc::kMaxUpdateFileBytes;
  options.timeoutMs = 15000;
  options.userAgent = "XDPortModUpdater/1";
  options.httpsOnly = Lower(url).rfind("https://", 0) == 0;
  options.progress = [&](uint64_t, uint64_t) { return !stop || !stop(); };
  std::string text, error;
  if (!eu::FetchToString(url, options, text, error)) {
    offer.error = "Couldn't get the update file from " + HostOf(url) + ": " + error;
    return offer;
  }
  if (!mc::ParseUpdateFile(text, offer.file, error)) {
    offer.error = "The update file from " + HostOf(url) + " can't be used: " + error;
    return offer;
  }
  offer.trust = EvaluateTrust(offer.file, expectedId, keys.Pinned(expectedId.empty() ? offer.file.id : expectedId), authorKey);
  offer.newer = installedVersion.empty() || em::CompareVersions(offer.file.version, installedVersion) > 0;
  std::string urlWhy;
  if (!UrlAllowed(offer.file.url, urlWhy)) {
    offer.error = "The update file names a download address that isn't allowed: " + urlWhy + ".";
    return offer;
  }
  if (offer.file.size == 0 || offer.file.size > kMaxModDownload) {
    offer.error = "The update file's download size isn't acceptable (" + std::to_string(offer.file.size) + " bytes; the most the launcher takes is " +
                  std::to_string(kMaxModDownload >> 20) + " MB).";
    return offer;
  }
  if (!installedFolder.empty() && rules != nullptr) {
    mc::Options o;
    o.folder = installedFolder;
    o.rules = rules;
    offer.installedPermissions = PermissionCodes(mc::CheckMod(o));
  }
  offer.addedPermissions = AddedPermissions(offer.installedPermissions, offer.file.permissions);
  return offer;
}

#if !defined(_WIN32)
AvResult ScanDownload(const fs::path&, const std::string&) {
  AvResult r;
  r.text = "no system virus scan is available on this system; only the launcher's own checks were made";
  return r;
}
#endif

Staged Download(const Folders& f, const Offer& offer, const mc::GameRules* rules, const std::function<bool(uint64_t, uint64_t)>& progress) {
  Staged s;
  static std::atomic<int> counter{0};
  std::error_code ec;
  s.root = f.Temp() / (offer.file.id + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 100000000) + "-" +
                      std::to_string(counter++));
  fs::create_directories(s.root, ec);
  const fs::path zip = s.root / "download.zip";
  const fs::path unpacked = s.root / "mod";
  const auto fail = [&](const std::string& why) {
    s.ok = false;
    s.error = why;
    return s;
  };
  std::string why;
  if (!UrlAllowed(offer.file.url, why)) return fail(why);
  eu::FetchOptions options;
  options.maxBytes = std::min<uint64_t>(offer.file.size, kMaxModDownload);
  options.timeoutMs = 30000;
  options.userAgent = "XDPortModUpdater/1";
  options.httpsOnly = Lower(offer.file.url).rfind("https://", 0) == 0;
  options.progress = progress;
  std::string error;
  if (!eu::FetchToFile(offer.file.url, zip, options, error)) {
    return fail(error == "stopped" ? std::string("Cancelled.") : "The download failed: " + error);
  }
  const uint64_t size = fs::file_size(zip, ec);
  if (ec || size != offer.file.size) {
    return fail("The download is " + std::to_string(size) + " bytes but the update file says " + std::to_string(offer.file.size) + ". Refused.");
  }
  std::string sha;
  if (!eu::Sha256File(zip, sha, error) || sha != Lower(offer.file.sha256)) {
    return fail("The download's sha256 fingerprint doesn't match the one in the signed update file, so it is not the file the author published. Refused.");
  }
  const AvResult av = ScanDownload(zip, offer.file.url);
  s.antivirus = av.text;
  if (av.blocked) return fail("The system's virus scanner refused the download: " + av.text);
  std::vector<uint8_t> bytes;
  if (!mc::ReadFileBytes(zip, bytes, kMaxModDownload + 1)) return fail("The download couldn't be read back.");
  mc::Options check;
  check.rules = rules;
  check.modsFolder = f.modsFolder;
  const mc::SafetyLimits limits;
  mc::Report report;
  const bool passed = mc::CheckAndExtractArchive(bytes, unpacked, check, limits, report);
  s.report = report;
  s.folder = report.folder.empty() ? unpacked : report.folder;
  s.actualPermissions = PermissionCodes(report);
  if (!passed) {
    s.findings = report.Format();
    return fail("The safety scan or the checker found problems in the download, so it was not installed (details below).");
  }
  if (!report.manifestOk) return fail("The downloaded mod has no usable mod.json. Refused.");
  if (Lower(report.manifest.id) != Lower(offer.file.id)) {
    return fail("The downloaded mod's id (\"" + report.manifest.id + "\") isn't the one in the update file (\"" + offer.file.id + "\"). Refused.");
  }
  if (report.manifest.version != offer.file.version) {
    return fail("The downloaded mod says version " + report.manifest.version + " but the update file says " + offer.file.version + ". Refused.");
  }
  const std::vector<std::string> understated = AddedPermissions(offer.file.permissions, s.actualPermissions);
  if (!understated.empty()) {
    std::string names;
    for (const std::string& c : understated) names += (names.empty() ? "" : "; ") + PermissionText(c);
    return fail("The update file leaves out permissions the mod really needs (" + names + "). Refused.");
  }
  if (!report.manifest.authorKey.empty() && report.manifest.authorKey != offer.file.publicKey) {
    return fail("The mod names a different author key than the one that signed the update file. Refused.");
  }
  for (const mc::Finding& f : report.findings) {
    if (f.severity == mc::Severity::Note) continue;
    s.findings += std::string("warning ") + (f.file.empty() ? "" : f.file + (f.line > 0 ? " line " + std::to_string(f.line) : std::string()) + ": ") + f.text + "\n";
  }
  s.ok = true;
  return s;
}

void DropStaged(const Staged& staged) {
  std::error_code ec;
  if (!staged.root.empty()) fs::remove_all(staged.root, ec);
}

namespace {

std::map<std::string, std::string> HashesUnder(const fs::path& folder, const char* sub) {
  std::map<std::string, std::string> out;
  for (const fs::path& root : em::ContentRoots(folder, std::string_view())) {
    std::error_code ec;
    const fs::path base = root / sub;
    if (!fs::is_directory(base, ec)) continue;
    const std::string prefix = root == folder ? std::string() : mc::PathText(root.filename()) + "/";
    for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string hex, err;
      eu::Sha256File(it->path(), hex, err);
      out[prefix + mc::PathText(fs::relative(it->path(), base, ec))] = hex;
    }
  }
  return out;
}

std::vector<std::string> AddedRowSequence(const fs::path& folder) {
  std::vector<std::string> out;
  std::error_code ec;
  std::vector<fs::path> files;
  for (const fs::path& root : em::ContentRoots(folder, std::string_view())) {
    const fs::path data = root / "data";
    if (!fs::is_directory(data, ec)) continue;
    std::vector<fs::path> here;
    for (auto it = fs::directory_iterator(data, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".json") here.push_back(it->path());
    }
    std::sort(here.begin(), here.end());
    files.insert(files.end(), here.begin(), here.end());
  }
  if (files.empty()) return out;
  for (const fs::path& file : files) {
    em::json::Value root;
    const std::string text = ReadText(file, 8u << 20);
    em::json::Reader reader(text);
    if (!reader.Document(root) || root.kind != em::json::Value::Kind::Object) continue;
    const auto add = root.fields.find("add");
    if (add == root.fields.end() || add->second.kind != em::json::Value::Kind::Object) continue;
    for (const std::string& table : add->second.order) {
      const em::json::Value& rows = add->second.fields.at(table);
      for (const std::string& name : rows.order) out.push_back(table + ":" + name);
    }
  }
  return out;
}

}

Change Analyse(const fs::path& oldFolder, const fs::path& newFolder) {
  Change c;
  const struct {
    const char* sub;
    const char* text;
  } kinds[] = {{"data", "numbers or rows in the game's tables"}, {"rooms", "story rooms"}, {"objects", "story objects"},
               {"scripts", "scripts"}, {"files", "whole game files"}, {"fsys", "files inside the game's archives"}};
  for (const auto& k : kinds) {
    if (HashesUnder(oldFolder, k.sub) != HashesUnder(newFolder, k.sub)) {
      c.worldChanged = true;
      c.reasons.push_back(std::string("It changes ") + k.text + ".");
    }
  }
  const std::vector<std::string> before = AddedRowSequence(oldFolder), now = AddedRowSequence(newFolder);
  if (before.size() > now.size() || !std::equal(before.begin(), before.end(), now.begin())) {
    c.addedRowsBreak = true;
    c.worldChanged = true;
    c.reasons.push_back("Rows the old version added (new Pokemon, moves, items) are removed, renamed or reordered: a save that holds them gets other "
                        "things in their place, so it can't be continued with this version.");
  }
  return c;
}

std::vector<Playthrough> PlaythroughsUsing(const std::vector<std::pair<std::string, fs::path>>& cardRoots, const std::string& id) {
  std::vector<Playthrough> out;
  for (const auto& [saveName, root] : cardRoots) {
    std::error_code ec;
    const fs::path base = root / "playthroughs";
    if (!fs::is_directory(base, ec)) continue;
    for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != ".json") continue;
      engine::playthrough::Setup setup;
      std::string err;
      if (!engine::playthrough::ReadFile(it->path(), setup, err)) continue;
      for (const auto& use : setup.mods) {
        if (Lower(use.id) == Lower(id) && use.applies == em::Applies::NewPlaythrough) {
          Playthrough p;
          p.save = saveName;
          p.label = setup.trainer.empty() ? it->path().stem().string() : setup.trainer;
          p.version = use.version;
          out.push_back(std::move(p));
        }
      }
    }
  }
  return out;
}

InstallResult Install(const Folders& f, const Staged& staged, const std::string& id, const fs::path& existingFolder, const std::string& pinKey) {
  InstallResult r;
  std::error_code ec;
  const bool replacing = !existingFolder.empty() && fs::is_directory(existingFolder, ec);
  const fs::path dest = existingFolder.empty() ? f.modsFolder / id : existingFolder;
  if (!replacing && fs::exists(dest, ec)) {
    r.error = "There is already a folder " + mc::PathText(dest) + " in the mods folder.";
    return r;
  }
  const fs::path holding = f.Previous() / (id + ".new");
  fs::remove_all(holding, ec);
  std::string error;
  if (replacing) {
    fs::create_directories(holding, ec);
    if (!MoveFolder(dest, holding / "mod", error)) {
      r.error = error;
      return r;
    }
  }
  if (!MoveFolder(staged.folder, dest, error)) {
    if (replacing) {
      std::string ignore;
      MoveFolder(holding / "mod", dest, ignore);
      fs::remove_all(holding, ec);
    }
    r.error = error;
    return r;
  }
  if (replacing) {
    const std::string version = VersionOfFolder(holding / "mod");
    WriteText(holding / "info.json", "{\"version\": " + JsonEscape(version) + ", \"date\": " + JsonEscape(Today()) + "}\n");
    fs::remove_all(f.Previous() / id, ec);
    fs::rename(holding, f.Previous() / id, ec);
    r.replaced = !ec;
  }
  if (!pinKey.empty()) {
    KeyStore keys = LoadKeys(f);
    keys.keys[Lower(id)] = pinKey;
    SaveKeys(f, keys);
  }
  DropStaged(staged);
  r.ok = true;
  r.modFolder = dest;
  return r;
}

PreviousInfo Previous(const Folders& f, const std::string& id) {
  PreviousInfo p;
  std::error_code ec;
  if (!fs::is_directory(f.Previous() / id / "mod", ec)) return p;
  p.exists = true;
  em::json::Value root;
  const std::string text = ReadText(f.Previous() / id / "info.json");
  em::json::Reader reader(text);
  if (reader.Document(root) && root.kind == em::json::Value::Kind::Object) {
    if (root.fields.count("version")) p.version = root.fields.at("version").text;
    if (root.fields.count("date")) p.date = root.fields.at("date").text;
  }
  if (p.version.empty()) p.version = VersionOfFolder(f.Previous() / id / "mod");
  return p;
}

bool RollBack(const Folders& f, const std::string& id, const fs::path& existingFolder, std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(f.Previous() / id / "mod", ec) || existingFolder.empty() || !fs::is_directory(existingFolder, ec)) {
    error = "There is no earlier version of this mod to go back to.";
    return false;
  }
  const fs::path swap = f.Previous() / (id + ".swap");
  fs::remove_all(swap, ec);
  fs::create_directories(swap, ec);
  if (!MoveFolder(existingFolder, swap / "mod", error)) return false;
  if (!MoveFolder(f.Previous() / id / "mod", existingFolder, error)) {
    std::string ignore;
    MoveFolder(swap / "mod", existingFolder, ignore);
    fs::remove_all(swap, ec);
    return false;
  }
  WriteText(swap / "info.json", "{\"version\": " + JsonEscape(VersionOfFolder(swap / "mod")) + ", \"date\": " + JsonEscape(Today()) + "}\n");
  fs::remove_all(f.Previous() / id, ec);
  fs::rename(swap, f.Previous() / id, ec);
  return true;
}

std::string ExportList(const em::ModList::Profile& profile, const std::vector<em::ModFolder>& found, const std::string& listName,
                       const std::string& gameId) {
  std::string out = "{\n  \"format\": 1,\n  \"kind\": \"xd-port-mod-list\",\n  \"name\": " + JsonEscape(listName) + ",\n  \"game\": " +
                    JsonEscape(gameId) + ",\n  \"note\": \"Only names and versions. Importing this shows what is missing; nothing runs until you say yes.\",\n  \"mods\": [";
  bool first = true;
  for (const auto& [id, on] : profile.mods) {
    if (!on) continue;
    const em::ModFolder* mod = nullptr;
    for (const em::ModFolder& m : found) {
      if (Lower(m.manifest.id) == Lower(id)) mod = &m;
    }
    if (mod == nullptr || mod->texturePack) continue;
    out += std::string(first ? "\n" : ",\n") + "    {\"id\": " + JsonEscape(mod->manifest.id) + ", \"name\": " + JsonEscape(mod->manifest.name) +
           ", \"version\": " + JsonEscape(mod->manifest.version);
    if (!mod->manifest.updates.empty()) out += ", \"updates\": " + JsonEscape(mod->manifest.updates);
    out += "}";
    first = false;
  }
  out += "\n  ]\n}\n";
  return out;
}

bool ParseList(const std::string& text, SharedList& out, std::string& error) {
  out = SharedList{};
  if (text.size() > (1u << 20)) {
    error = "the list file is too large";
    return false;
  }
  em::json::Value root;
  em::json::Reader reader(text);
  if (!reader.Document(root) || root.kind != em::json::Value::Kind::Object) {
    error = "this is not a mod list file (not valid JSON)";
    return false;
  }
  if (!root.fields.count("kind") || root.fields.at("kind").text != "xd-port-mod-list") {
    error = "this is not a mod list file made by the launcher";
    return false;
  }
  if (root.fields.count("format") && root.fields.at("format").number > 1) {
    error = "this list was made by a newer launcher";
    return false;
  }
  if (root.fields.count("name")) out.name = root.fields.at("name").text;
  if (root.fields.count("game")) out.game = root.fields.at("game").text;
  const auto mods = root.fields.find("mods");
  if (mods == root.fields.end() || mods->second.kind != em::json::Value::Kind::Array) {
    error = "the list has no \"mods\"";
    return false;
  }
  if (mods->second.items.size() > 500) {
    error = "the list has too many mods";
    return false;
  }
  for (const em::json::Value& v : mods->second.items) {
    if (v.kind != em::json::Value::Kind::Object || !v.fields.count("id") || !em::ValidModId(v.fields.at("id").text)) {
      error = "a mod in the list has no usable id";
      return false;
    }
    SharedMod m;
    m.id = v.fields.at("id").text;
    if (v.fields.count("name")) m.name = v.fields.at("name").text.substr(0, 100);
    if (v.fields.count("version")) m.version = v.fields.at("version").text.substr(0, 40);
    if (v.fields.count("updates")) m.updates = v.fields.at("updates").text.substr(0, 400);
    out.mods.push_back(std::move(m));
  }
  return true;
}

ImportPlan PlanImport(const SharedList& list, const std::vector<em::ModFolder>& found, const std::string& gameId) {
  ImportPlan plan;
  if (!gameId.empty() && !list.game.empty() && Lower(list.game) != Lower(gameId)) {
    plan.gameNote = "This list was made for another game or game version (" + list.game + "); this disc is " + gameId + ".";
  }
  for (const SharedMod& m : list.mods) {
    ImportRow row;
    row.mod = m;
    const em::ModFolder* have = nullptr;
    for (const em::ModFolder& f : found) {
      if (Lower(f.manifest.id) == Lower(m.id)) have = &f;
    }
    std::string why;
    if (have == nullptr) {
      row.state = ImportRow::State::Missing;
      if (!m.updates.empty() && UrlAllowed(m.updates, why)) row.link = m.updates;
    } else {
      row.haveVersion = have->manifest.version;
      row.state = (m.version.empty() || m.version == have->manifest.version) ? ImportRow::State::Have : ImportRow::State::OtherVersion;
      const std::string& own = have->manifest.updates;
      if (row.state == ImportRow::State::OtherVersion && !own.empty() && UrlAllowed(own, why)) row.link = own;
    }
    plan.rows.push_back(std::move(row));
  }
  return plan;
}

std::vector<std::string> ApplyImport(em::ModList::Profile& profile, const ImportPlan& plan) {
  std::vector<std::string> missing;
  std::vector<std::pair<std::string, bool>> next;
  std::set<std::string> used;
  for (const ImportRow& row : plan.rows) {
    if (row.state == ImportRow::State::Missing) {
      missing.push_back(row.mod.id);
      continue;
    }
    next.emplace_back(row.mod.id, row.mod.on);
    used.insert(Lower(row.mod.id));
  }
  for (const auto& entry : profile.mods) {
    if (used.count(Lower(entry.first)) == 0) next.push_back(entry);
  }
  profile.mods = std::move(next);
  return missing;
}

}
