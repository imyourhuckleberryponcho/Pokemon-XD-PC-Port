#include "modcheck.h"

#include "json.h"
#include "modsafety.h"
#include "script_host.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>

namespace fs = std::filesystem;
namespace em = engine::mods;
using engine::mods::json::Value;
using engine::mods::json::Reader;

namespace engine::modcheck {

namespace {

std::string Fmt(const char* format, ...) {
  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

int SplitReaderError(const std::string& error, std::string& what) {
  int line = 0;
  what = error;
  if (error.rfind("line ", 0) == 0) {
    const size_t colon = error.find(": ");
    if (colon != std::string::npos) {
      line = std::atoi(error.c_str() + 5);
      what = error.substr(colon + 2);
    }
  }
  return line;
}

bool LooksLikeVersion(const std::string& v) {
  size_t i = 0;
  while (true) {
    const size_t start = i;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9') i++;
    if (i == start) return false;
    if (i == v.size()) return true;
    if (v[i] != '.') return false;
    i++;
  }
}

bool IsHttps(const std::string& url) { return LowerAscii(url).rfind("https://", 0) == 0; }

struct FileInfo {
  std::string rel;
  fs::path path;
  uint64_t size = 0;
};

struct Tree {
  std::vector<FileInfo> files;
  std::vector<std::string> dirs;
};

void WalkTree(const fs::path& root, Tree& tree, Report& report, const SafetyLimits& limits) {
  std::error_code ec;
  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    const std::string rel = PathText(it->path().lexically_relative(root));
    const fs::file_status link = it->symlink_status(ec);
    if (fs::is_symlink(link)) {
      report.Unsafe(Severity::Problem, "symlink", rel, 0, "this is a link (shortcut) to somewhere else; mods may not contain links");
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(ec)) {
      tree.dirs.push_back(rel);
      continue;
    }
    if (!it->is_regular_file(ec)) {
      report.Unsafe(Severity::Problem, "special-file", rel, 0, "this is not an ordinary file (a device, pipe or similar)");
      continue;
    }
    FileInfo f;
    f.rel = rel;
    f.path = it->path();
    f.size = it->file_size(ec);
    tree.files.push_back(std::move(f));
    if (tree.files.size() > limits.maxFiles + 100000) break;
  }
  std::sort(tree.files.begin(), tree.files.end(), [](const FileInfo& a, const FileInfo& b) { return a.rel < b.rel; });
}

void CheckManifest(const Options& o, Report& report, const std::string& text) {
  static const std::vector<std::string> kKnown = {"id", "name", "version", "author", "description", "game", "requires", "conflicts",
                                                  "after", "before", "works_with", "recommends", "only_with", "needs_assets", "recommends_assets", "links", "author_key", "label_namespace", "priority", "applies", "scripts", "script_api", "format", "updates",
                                                  "homepage", "license", "tags"};
  const std::string file = "mod.json";
  const size_t before = report.Count(Severity::Problem);
  Value root;
  Reader reader(text);
  if (!reader.Document(root)) {
    std::string what;
    const int line = SplitReaderError(reader.Error(), what);
    report.Problem("manifest-json", file, line, "mod.json is not valid JSON: " + what);
    return;
  }
  if (root.kind != Value::Kind::Object) {
    report.Problem("manifest-shape", file, root.line, "mod.json must be one { ... } block");
    return;
  }
  const auto text_field = [&](const char* key, bool required) -> const Value* {
    const auto it = root.fields.find(key);
    if (it == root.fields.end() || it->second.kind == Value::Kind::Null) {
      if (required) report.Problem("manifest-missing", file, 0, Fmt("mod.json has no \"%s\"", key));
      return nullptr;
    }
    if (it->second.kind != Value::Kind::String) {
      report.Problem("manifest-type", file, it->second.line, Fmt("\"%s\" must be a text in quotes", key));
      return nullptr;
    }
    return &it->second;
  };
  const auto list_field = [&](const char* key, std::vector<std::pair<std::string, int>>& out) {
    const auto it = root.fields.find(key);
    if (it == root.fields.end() || it->second.kind == Value::Kind::Null) return;
    const Value& v = it->second;
    if (v.kind == Value::Kind::String) {
      out.emplace_back(v.text, static_cast<int>(v.line));
      return;
    }
    if (v.kind == Value::Kind::Array) {
      for (const Value& item : v.items) {
        if (item.kind != Value::Kind::String) {
          report.Problem("manifest-type", file, item.line, Fmt("\"%s\" must be a text or a list of texts, like [\"a\", \"b\"]", key));
          return;
        }
        out.emplace_back(item.text, static_cast<int>(item.line));
      }
      return;
    }
    report.Problem("manifest-type", file, v.line, Fmt("\"%s\" must be a text or a list of texts, like [\"a\", \"b\"]", key));
  };
  const auto number_field = [&](const char* key) {
    const auto it = root.fields.find(key);
    if (it == root.fields.end() || it->second.kind == Value::Kind::Null) return;
    const double n = it->second.number;
    if (it->second.kind != Value::Kind::Number || n != static_cast<double>(static_cast<int>(n)) || n < -1000000 || n > 1000000) {
      report.Problem("manifest-type", file, it->second.line, Fmt("\"%s\" must be a whole number", key));
    }
  };

  if (const Value* id = text_field("id", true)) {
    if (!em::ValidModId(id->text)) {
      report.Problem("manifest-id", file, id->line,
                     id->text.empty() ? std::string("\"id\" is empty")
                                      : Fmt("\"id\" may only have letters, digits, '-', '_' and '.' (it is \"%s\")", id->text.c_str()));
    }
  }
  text_field("name", false);
  const Value* version = text_field("version", false);
  text_field("author", false);
  text_field("description", false);
  text_field("homepage", false);
  text_field("license", false);

  std::vector<std::pair<std::string, int>> games, requires_, conflicts, after;
  list_field("game", games);
  if (root.fields.find("game") == root.fields.end() || root.fields.at("game").kind == Value::Kind::Null) {
    report.Problem("manifest-missing", file, 0, "mod.json has no \"game\" (the game ID it is made for, e.g. \"GXXE01\")");
  } else if (games.empty() && report.Count(Severity::Problem) == before) {
    report.Problem("manifest-missing", file, root.fields.at("game").line, "\"game\" is an empty list");
  }
  for (const auto& [g, line] : games) {
    std::string upper = g;
    for (char& c : upper) c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    if (!em::ValidGameFolderName(upper)) {
      report.Warn("manifest-game", file, line, Fmt("\"%s\" is not a game ID: write the whole ID (\"GXXE01\") or its first 3 or 4 letters (\"GXX\" = every region)", g.c_str()));
    }
  }
  if (o.rules != nullptr && !o.rules->gameIds.empty()) {
    for (const auto& [g, line] : games) {
      const bool startOfOurs = std::any_of(o.rules->gameIds.begin(), o.rules->gameIds.end(), [&](const std::string& k) { return em::GameCodeMatches(g, k); });
      if (startOfOurs || !em::GameNameOf(g, false).empty()) continue;
      if (std::find(o.rules->gameIds.begin(), o.rules->gameIds.end(), g) == o.rules->gameIds.end()) {
        std::string known;
        for (const std::string& k : o.rules->gameIds) known += (known.empty() ? "" : ", ") + k;
        report.Warn("manifest-game", file, line, Fmt("\"%s\" is not a disc ID this port knows for %s (%s)", g.c_str(), o.rules->gameName.c_str(), known.c_str()));
      }
    }
  }
  list_field("requires", requires_);
  list_field("conflicts", conflicts);
  list_field("after", after);
  std::vector<std::pair<std::string, int>> relations[4];
  const char* const relationKeys[4] = {"before", "works_with", "recommends", "only_with"};
  for (int k = 0; k < 4; k++) list_field(relationKeys[k], relations[k]);
  for (int k = 0; k < 4; k++) {
    for (const auto& [id, line] : relations[k]) {
      if (!em::ValidModId(id)) report.Problem("manifest-relation", file, line, Fmt("\"%s\" has \"%s\", which isn't a mod id", relationKeys[k], id.c_str()));
      else if ((root.fields.count("id") && root.fields.at("id").kind == Value::Kind::String && id == root.fields.at("id").text)) report.Problem("manifest-self", file, line, Fmt("\"%s\" names the mod itself", relationKeys[k]));
    }
  }
  {
    std::vector<std::pair<std::string, int>> assets[2];
    const char* const assetKeys[2] = {"needs_assets", "recommends_assets"};
    for (int k = 0; k < 2; k++) list_field(assetKeys[k], assets[k]);
    for (int k = 0; k < 2; k++) {
      for (const auto& [id, line] : assets[k]) {
        if (const em::AssetInfo* a = em::FindAsset(id)) {
          if (k == 0) report.Note("asset-needed", file, line, Fmt("the mod is only used while the player has made the %s on the launcher's Extra assets page (\"needs_assets\"); nothing of it must be inside the mod", a->name));
        } else {
          std::string list;
          for (const em::AssetInfo& a : em::KnownAssets()) list += (list.empty() ? "" : ", ") + std::string(a.id);
          report.Problem("manifest-assets", file, line, Fmt("\"%s\" has \"%s\", which isn't an extra asset this port knows (use one of: %s)", assetKeys[k], id.c_str(), list.c_str()));
        }
      }
    }
  }
  if (const auto it = root.fields.find("links"); it != root.fields.end() && it->second.kind != Value::Kind::Null) {
    if (it->second.kind != Value::Kind::Object) {
      report.Problem("manifest-type", file, it->second.line, "\"links\" must be a block like {\"other-mod\": \"https://.../other-mod.update.json\"}");
    } else {
      for (const auto& [id, v] : it->second.fields) {
        if (!em::ValidModId(id) || v.kind != Value::Kind::String) {
          report.Problem("manifest-links", file, v.line, Fmt("\"links\" has \"%s\": a mod id and an address in quotes are expected", id.c_str()));
        } else if (v.text.rfind("https://", 0) != 0) {
          report.Warn("manifest-links-not-https", file, v.line, Fmt("the link for \"%s\" is not an https:// address; the launcher will refuse it", id.c_str()));
        }
      }
    }
  }
  if (const Value* key = text_field("author_key", false)) {
    bool hex = key->text.size() == 64;
    for (char c : key->text) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    if (!hex) report.Problem("manifest-author-key", file, key->line, "\"author_key\" must be 64 lower case hex digits (your public signing key)");
  }
  if (const Value* ns = text_field("label_namespace", false)) {
    if (!em::ValidNamespace(ns->text) || ns->text == em::kBaseNamespace) {
      report.Problem("manifest-label-namespace", file, ns->line, "\"label_namespace\" must be <author>.<pack>: lower case letters, digits, - and _, one dot between the two (\"base\" is for the game's own rows)");
    }
  }
  number_field("priority");
  number_field("format");
  number_field("script_api");
  if (const auto it = root.fields.find("scripts"); it != root.fields.end() && it->second.kind != Value::Kind::Null &&
                                                 it->second.kind != Value::Kind::Bool) {
    report.Problem("manifest-type", file, it->second.line, "\"scripts\" must be true or false");
  }
  if (const Value* applies = text_field("applies", false)) {
    em::Applies a;
    if (!em::ParseApplies(applies->text, a)) {
      report.Problem("manifest-applies", file, applies->line,
                     Fmt("\"applies\" must be \"any_time\", \"restart\" or \"new_playthrough\" (it is \"%s\")", applies->text.c_str()));
    }
  }

  const std::string myId = root.fields.count("id") && root.fields.at("id").kind == Value::Kind::String ? root.fields.at("id").text : "";
  std::set<std::string> requiredIds, conflictIds;
  for (const auto& [raw, line] : requires_) {
    const size_t op = raw.find_first_of("<>=");
    std::string id = raw.substr(0, op);
    while (!id.empty() && id.back() == ' ') id.pop_back();
    if (op != std::string::npos) {
      size_t v = op;
      while (v < raw.size() && (raw[v] == '<' || raw[v] == '>' || raw[v] == '=')) v++;
      const std::string opText = raw.substr(op, v - op);
      while (v < raw.size() && raw[v] == ' ') v++;
      const std::string ver = raw.substr(v);
      if ((opText != ">=" && opText != ">" && opText != "<=" && opText != "<" && opText != "=" && opText != "==") || ver.empty()) {
        report.Problem("manifest-requires", file, line, Fmt("\"requires\" has \"%s\": write a version condition like \"other-mod >= 1.2\"", raw.c_str()));
      } else if (!LooksLikeVersion(ver)) {
        report.Warn("manifest-requires", file, line, Fmt("the version in \"%s\" is not dotted numbers like 1.2.0", raw.c_str()));
      }
    }
    if (!em::ValidModId(id)) {
      report.Problem("manifest-requires", file, line, Fmt("\"requires\" has \"%s\", which isn't a mod id", raw.c_str()));
      continue;
    }
    if (id == myId) report.Problem("manifest-self", file, line, "the mod requires itself");
    requiredIds.insert(id);
  }
  for (const auto& [id, line] : conflicts) {
    if (!em::ValidModId(id)) {
      report.Problem("manifest-conflicts", file, line, Fmt("\"conflicts\" has \"%s\", which isn't a mod id", id.c_str()));
      continue;
    }
    if (id == myId) report.Problem("manifest-self", file, line, "the mod conflicts with itself, so it could never be used");
    if (requiredIds.count(id)) report.Problem("manifest-contradiction", file, line, Fmt("\"%s\" is in both \"requires\" and \"conflicts\": the mod could never be used", id.c_str()));
    conflictIds.insert(id);
  }
  for (const auto& [id, line] : after) {
    if (!em::ValidModId(id)) report.Problem("manifest-after", file, line, Fmt("\"after\" has \"%s\", which isn't a mod id", id.c_str()));
    else if (id == myId) report.Problem("manifest-self", file, line, "\"after\" names the mod itself");
  }

  if (version != nullptr) {
    if (!LooksLikeVersion(version->text)) {
      report.Warn("version-format", file, version->line,
                  Fmt("the version \"%s\" is not dotted numbers like 1.2.0; the port compares versions number by number, so other forms compare as plain text", version->text.c_str()));
    }
  }
  if (const Value* updates = text_field("updates", false)) {
    if (version == nullptr) {
      report.Warn("updates-no-version", file, updates->line, "\"updates\" is set but there is no \"version\": the launcher can't tell whether an update is newer");
    }
    const std::string& url = updates->text;
    if (url.find_first_of(" \t\r\n") != std::string::npos || url.find("://") == std::string::npos) {
      report.Warn("updates-url", file, updates->line, Fmt("\"updates\" (\"%s\") does not look like a web address (https://...)", url.c_str()));
    } else if (!IsHttps(url)) {
      report.Warn("updates-not-https", file, updates->line, Fmt("\"updates\" (\"%s\") is not an https:// address; updates must come over https", url.c_str()));
    }
  } else if (version == nullptr) {
    report.Note("no-version", file, 0, "mod.json has no \"version\"; add one so players can tell which one they have");
  }

  for (const std::string& key : root.order) {
    if (key.empty() || key[0] == '_') continue;
    if (std::find(kKnown.begin(), kKnown.end(), key) == kKnown.end()) {
      const std::string near = Closest(key, kKnown);
      report.Warn("manifest-unknown-field", file, root.fields.at(key).line,
                  Fmt("\"%s\" is not a field the port knows; it is ignored%s", key.c_str(),
                      near.empty() ? "" : (" (did you mean \"" + near + "\"?)").c_str()));
    }
  }
  if (root.fields.count("name") == 0) report.Note("no-name", file, 0, "mod.json has no \"name\"; the id is shown instead");
  if (root.fields.count("author") == 0) report.Note("no-author", file, 0, "mod.json has no \"author\"");
  if (root.fields.count("description") == 0) report.Note("no-description", file, 0, "mod.json has no \"description\"");

  em::Manifest manifest;
  std::string error;
  if (em::ParseManifest(text, manifest, error)) {
    report.manifest = manifest;
    report.manifestOk = true;
  } else if (report.Count(Severity::Problem) == before) {
    report.Problem("manifest-engine", file, 0, "the game would refuse mod.json: " + error);
  }
}

bool NumberKey(const std::string& key) {
  std::string k = key;
  if (!k.empty() && k[0] == '#') k = k.substr(1);
  if (k.empty()) return false;
  if (k.size() > 2 && k[0] == '0' && (k[1] == 'x' || k[1] == 'X')) {
    return std::all_of(k.begin() + 2, k.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
  }
  return std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

void CheckTextValue(const GameRules* rules, Report& report, const std::string& file, const Value& v, const std::string& what) {
  if (v.kind != Value::Kind::String) {
    report.Problem("data-text", file, static_cast<int>(v.line), what + " needs its new text in quotes");
    return;
  }
  if (v.text.size() > 4000) {
    report.Problem("data-text-long", file, static_cast<int>(v.line), Fmt("%s is %zu characters long; no game message is that long", what.c_str(), v.text.size()));
    return;
  }
  std::string why;
  if (rules != nullptr && rules->checkMessageText && !rules->checkMessageText(v.text, why)) {
    report.Problem("data-text", file, static_cast<int>(v.line), what + ": " + why);
  }
}

void CheckFieldValue(Report& report, const std::string& file, const std::string& where, const Value& v, bool inList = false) {
  switch (v.kind) {
  case Value::Kind::Number:
    if (v.number < 0 || v.number != static_cast<double>(static_cast<uint64_t>(v.number)) || v.number > 4294967295.0) {
      report.Problem("data-number", file, static_cast<int>(v.line),
                     where + " is " + em::json::Number(v.number) + ": table numbers are whole numbers from 0 to 4294967295 (a field's own limit is checked with a disc)");
    }
    break;
  case Value::Kind::String:
  case Value::Kind::Bool:
    break;
  case Value::Kind::Null:
    if (!inList) report.Problem("data-value", file, static_cast<int>(v.line), where + " is null; only a list may have null in it (to leave a value as it is)");
    break;
  case Value::Kind::Array:
    if (inList) {
      report.Problem("data-value", file, static_cast<int>(v.line), where + " has a list inside a list");
    } else {
      for (const Value& i : v.items) CheckFieldValue(report, file, where, i, true);
    }
    break;
  case Value::Kind::Object:
    report.Problem("data-value", file, static_cast<int>(v.line), where + " is a { ... } block; a field takes a number, a name in quotes or a list");
    break;
  }
}

struct DataSummary {
  bool changesTables = false, adds = false, text = false, addsStory = false;
  std::set<std::string> addTables;
};

void CheckEntryBlock(Report& report, const std::string& file, const std::string& table, const Value& entries) {
  for (const std::string& entryKey : entries.order) {
    if (entryKey.empty() || entryKey[0] == '_') continue;
    const Value& ev = entries.fields.at(entryKey);
    if (ev.kind != Value::Kind::Object) {
      report.Problem("data-entry", file, static_cast<int>(ev.line), "\"" + entryKey + "\" in \"" + table + "\" must be a { ... } block of fields, like { \"field\": 5 }");
      continue;
    }
    for (const std::string& fk : ev.order) {
      if (fk.empty() || fk[0] == '_') continue;
      CheckFieldValue(report, file, "\"" + fk + "\" of \"" + entryKey + "\"", ev.fields.at(fk));
    }
  }
}

void CheckDataFile(const GameRules* rules, Report& report, const std::string& file, const std::string& text, DataSummary& sum) {
  Value root;
  Reader reader(text);
  if (!reader.Document(root)) {
    std::string what;
    const int line = SplitReaderError(reader.Error(), what);
    report.Problem("data-json", file, line, "this data file is not valid JSON: " + what + " (the game uses none of it)");
    return;
  }
  if (root.kind != Value::Kind::Object) {
    report.Problem("data-shape", file, static_cast<int>(root.line), "a data file must be one { ... } block with tables in it");
    return;
  }
  for (const std::string& key : root.order) {
    const Value& v = root.fields.at(key);
    if (key.empty() || key[0] == '_') continue;
    if (key == "format") {
      if (v.kind != Value::Kind::Number || v.number > 1) report.Problem("data-format", file, static_cast<int>(v.line), "\"format\" says the file is made for a newer version of the port");
      continue;
    }
    if (key == "text") {
      sum.text = true;
      if (v.kind != Value::Kind::Object) {
        report.Problem("data-text", file, static_cast<int>(v.line), "\"text\" must be a { ... } block of messages, like { \"#1234\": \"new words\" }");
        continue;
      }
      for (const std::string& k : v.order) {
        if (k.empty() || k[0] == '_') continue;
        const Value& tv = v.fields.at(k);
        if (!NumberKey(k)) {
          report.Problem("data-text-key", file, static_cast<int>(tv.line), "messages are named by number, like \"#1234\" (not \"" + k + "\")");
          continue;
        }
        CheckTextValue(rules, report, file, tv, "message " + k);
      }
      continue;
    }
    if (key == "add") {
      sum.adds = true;
      if (v.kind != Value::Kind::Object) {
        report.Problem("data-add", file, static_cast<int>(v.line), "\"add\" must be a { ... } block of tables, like { \"pokemon\": { \"Name\": { ... } } }");
        continue;
      }
      for (const std::string& t : v.order) {
        if (t.empty() || t[0] == '_') continue;
        const Value& tv = v.fields.at(t);
        sum.addTables.insert(t);
        if (t == "floors" || t == "floordata" || t == "flags" || t == "flag_data") sum.addsStory = true;
        if (tv.kind != Value::Kind::Object) {
          report.Problem("data-add", file, static_cast<int>(tv.line), "\"" + t + "\" must be a { ... } block of new rows, like { \"Name\": { \"copy\": \"Eevee\" } }");
          continue;
        }
        for (const std::string& rk : tv.order) {
          if (rk.empty() || rk[0] == '_') continue;
          const Value& rv = tv.fields.at(rk);
          if (rv.kind != Value::Kind::Object) {
            report.Problem("data-add", file, static_cast<int>(rv.line), "the new row \"" + rk + "\" must be a { ... } block, like { \"copy\": \"Eevee\", ... }");
            continue;
          }
          for (const std::string& fk : rv.order) {
            if (fk.empty() || fk[0] == '_') continue;
            const Value& fv = rv.fields.at(fk);
            if (fk == "copy") {
              if (fv.kind != Value::Kind::String && fv.kind != Value::Kind::Number) {
                report.Problem("data-add", file, static_cast<int>(fv.line), "\"copy\" must be the name or number of the row to start from");
              }
            } else if (fk == "text") {
              if (fv.kind != Value::Kind::Object) {
                report.Problem("data-add", file, static_cast<int>(fv.line), "\"text\" of the new row must be a { ... } block, like { \"name\": \"words\" }");
              } else {
                for (const std::string& tk : fv.order) {
                  if (tk.empty() || tk[0] == '_') continue;
                  CheckTextValue(rules, report, file, fv.fields.at(tk), "the text \"" + tk + "\" of \"" + rk + "\"");
                }
              }
            } else {
              CheckFieldValue(report, file, "\"" + fk + "\" of the new row \"" + rk + "\"", fv);
            }
          }
        }
      }
      continue;
    }
    sum.changesTables = true;
    if (v.kind != Value::Kind::Object) {
      report.Problem("data-table", file, static_cast<int>(v.line), "\"" + key + "\" must be a { ... } block of entries, like { \"#1\": { ... } }");
      continue;
    }
    CheckEntryBlock(report, file, key, v);
  }
}

struct LuaToken {
  std::string text;
  bool isString = false;
  int line = 0;
};

std::vector<LuaToken> LuaTokens(const std::string& s) {
  std::vector<LuaToken> out;
  int line = 1;
  size_t i = 0;
  const auto longBracket = [&](size_t at, size_t& level) {
    if (at >= s.size() || s[at] != '[') return false;
    size_t k = at + 1;
    level = 0;
    while (k < s.size() && s[k] == '=') { level++; k++; }
    return k < s.size() && s[k] == '[';
  };
  const auto skipLong = [&](size_t at, size_t level) {
    const std::string close = "]" + std::string(level, '=') + "]";
    size_t e = s.find(close, at);
    const size_t stop = e == std::string::npos ? s.size() : e + close.size();
    for (size_t k = at; k < stop; k++) if (s[k] == '\n') line++;
    return stop;
  };
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\n') { line++; i++; continue; }
    if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }
    if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
      size_t level = 0;
      if (longBracket(i + 2, level)) { i = skipLong(i + 2, level); continue; }
      while (i < s.size() && s[i] != '\n') i++;
      continue;
    }
    if (c == '"' || c == '\'' || c == '`') {
      LuaToken t;
      t.isString = true;
      t.line = line;
      size_t k = i + 1;
      while (k < s.size() && s[k] != c && s[k] != '\n') {
        if (s[k] == '\\' && k + 1 < s.size()) { t.text += s[k + 1]; k += 2; continue; }
        t.text += s[k++];
      }
      i = k + 1;
      out.push_back(std::move(t));
      continue;
    }
    size_t level = 0;
    if (c == '[' && longBracket(i, level)) {
      LuaToken t;
      t.isString = true;
      t.line = line;
      i = skipLong(i, level);
      out.push_back(std::move(t));
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t k = i;
      while (k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_')) k++;
      out.push_back({s.substr(i, k - i), false, line});
      i = k;
      continue;
    }
    out.push_back({std::string(1, c), false, line});
    i++;
  }
  return out;
}

void CheckLuaFile(const GameRules* rules, Report& report, const std::string& file, const std::string& text, bool& compiles,
                  bool warnGameOnly = false) {
  compiles = true;
  if (text.size() >= 4 && text[0] == '\x1b' && text.compare(1, 3, "Lua") == 0) {
    report.Unsafe(Severity::Problem, "lua-bytecode", file, 0, "this is compiled Lua, not source text; the port only reads text scripts");
    compiles = false;
    return;
  }
  std::string error;
  if (!engine::script::CheckSyntax({file, text}, error)) {
    compiles = false;
    int line = 0;
    std::string what = error;
    const std::string prefix = file + ":";
    if (error.rfind(prefix, 0) == 0) {
      line = std::atoi(error.c_str() + prefix.size());
      const size_t colon = error.find(": ", prefix.size());
      if (colon != std::string::npos) what = error.substr(colon + 2);
    }
    std::string hint;
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".lua") == 0) {
      hint = " (scripts are Luau now: Lua 5.4-only syntax such as goto, <const> and the & | ~ << >> operators is not understood; see docs/modding/luau.md)";
    }
    report.Problem("lua-syntax", file, line, "this script does not compile: " + what + hint);
    return;
  }
  static const std::set<std::string> kMissing = {"io", "os", "debug", "package", "coroutine", "require", "dofile", "loadfile", "load",
                                                 "loadstring", "pcall", "xpcall", "collectgarbage", "module", "utf8", "getfenv", "setfenv", "newproxy", "gcinfo", "buffer", "vector"};
  const std::vector<LuaToken> tokens = LuaTokens(text);
  std::set<std::string> reported;
  for (size_t i = 0; i < tokens.size(); i++) {
    const LuaToken& t = tokens[i];
    if (t.isString) continue;
    const bool member = i > 0 && !tokens[i - 1].isString && (tokens[i - 1].text == "." || tokens[i - 1].text == ":");
    if (!member && kMissing.count(t.text) != 0 && i + 1 < tokens.size() && (tokens[i + 1].text == "." || tokens[i + 1].text == "(" || tokens[i + 1].text == "[") &&
        reported.insert(t.text).second) {
      report.Warn("lua-sandbox", file, t.line, "\"" + t.text + "\" is not available to mod scripts (the sandbox leaves it out); this script will fail when it reaches it");
    }
    if (warnGameOnly && !member && rules != nullptr && i + 1 < tokens.size() &&
        std::find(rules->gameOnlyLua.begin(), rules->gameOnlyLua.end(), t.text) != rules->gameOnlyLua.end() &&
        (tokens[i + 1].text == "(" || tokens[i + 1].text == "." || tokens[i + 1].text == "[" || tokens[i + 1].text == "{") && reported.insert("game-only " + t.text).second) {
      report.Warn("lua-game-only", file, t.line,
                  "\"" + t.text + "\" only exists in " + rules->gameName + "; this script is in the shared part of a mod that also names another game, which will fail here. "
                  "Move it to games/<id>/scripts of " + rules->gameName + ", or leave it out");
    }
    if (!member && t.text == "on" && i + 2 < tokens.size() && tokens[i + 1].text == "(" && tokens[i + 2].isString && rules != nullptr &&
        !rules->scriptHooks.empty()) {
      const std::string& hook = tokens[i + 2].text;
      if (std::find(rules->scriptHooks.begin(), rules->scriptHooks.end(), hook) == rules->scriptHooks.end()) {
        const std::string near = Closest(hook, rules->scriptHooks);
        std::string all;
        for (const std::string& h : rules->scriptHooks) all += (all.empty() ? "" : ", ") + h;
        report.Problem("lua-unknown-hook", file, t.line,
                       "on(\"" + hook + "\") names a hook the game doesn't have" + (near.empty() ? "" : " (did you mean \"" + near + "\"?)") +
                           "; the hooks are " + all);
      }
    }
  }
}

}

void Report::Add(Severity severity, std::string code, std::string file, int line, std::string text, Category category) {
  Finding f;
  f.severity = severity;
  f.code = std::move(code);
  f.file = std::move(file);
  f.line = line;
  f.text = std::move(text);
  f.category = category;
  findings.push_back(std::move(f));
}

size_t Report::Count(Severity severity) const {
  return static_cast<size_t>(std::count_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.severity == severity; }));
}

size_t Report::Count(Severity severity, Category category) const {
  return static_cast<size_t>(
      std::count_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.severity == severity && f.category == category; }));
}

bool Report::Has(const std::string& code) const {
  return std::any_of(findings.begin(), findings.end(), [&](const Finding& f) { return f.code == code; });
}

bool Report::HasPermission(const std::string& code) const {
  return std::any_of(permissions.begin(), permissions.end(), [&](const Permission& p) { return p.code == code; });
}

int Report::ExitCode(bool strict) const {
  if (Count(Severity::Problem) > 0) return 1;
  return strict && Count(Severity::Warning) > 0 ? 2 : 0;
}

std::string Report::Format() const {
  std::string out;
  const auto where = [](const Finding& f) {
    std::string w = f.file.empty() ? std::string("(the mod)") : f.file;
    if (f.line > 0) w += " line " + std::to_string(f.line);
    return w;
  };
  const auto label = [](Severity s) { return s == Severity::Problem ? "PROBLEM" : s == Severity::Warning ? "warning" : "note   "; };
  const auto section = [&](Category cat) {
    std::vector<const Finding*> list;
    for (const Finding& f : findings) if (f.category == cat) list.push_back(&f);
    std::stable_sort(list.begin(), list.end(), [](const Finding* a, const Finding* b) {
      if (a->severity != b->severity) return a->severity < b->severity;
      if (a->file != b->file) return a->file < b->file;
      return a->line < b->line;
    });
    return list;
  };
  out += "Mod check: " + PathText(folder) + "\n";
  if (manifestOk) {
    const std::string tag = engine::mods::GameTag(manifest);
    out += "  " + manifest.name + " (" + manifest.id + (manifest.version.empty() ? "" : " " + manifest.version) + ")" + (tag.empty() ? "" : "  [" + tag + "]") + "\n";
  }
  if (!support.empty()) {
    out += "  works with: " + engine::mods::SupportText(support, false) + "\n";
    for (const engine::mods::GameSupport& g : support) {
      if (!g.folders.empty()) {
        std::string list;
        for (const std::string& f : g.folders) list += (list.empty() ? "" : ", ") + std::string("games/") + f;
        out += "    " + g.code + ": own parts in " + list + "\n";
      }
    }
  }
  out += "\nSAFETY\n";
  const auto safety = section(Category::Safety);
  if (safety.empty()) {
    out += "  Nothing dangerous found. This lowers the risk; it cannot prove a mod is harmless. Only install mods from people you trust.\n";
  }
  for (const Finding* f : safety) out += std::string("  ") + label(f->severity) + "  " + where(*f) + ": " + f->text + " [" + f->code + "]\n";
  if (!permissions.empty()) {
    out += "\nWHAT THIS MOD CAN DO\n";
    for (const Permission& p : permissions) out += "  - " + p.text + "\n";
  }
  out += "\nCHECKS\n";
  const auto mod = section(Category::Mod);
  if (mod.empty()) out += "  Nothing wrong found.\n";
  for (const Finding* f : mod) out += std::string("  ") + label(f->severity) + "  " + where(*f) + ": " + f->text + " [" + f->code + "]\n";
  if (!deepDone) out += "  (Without a disc, table and field names and number ranges are not checked.)\n";
  out += Fmt("\n%zu problem(s), %zu warning(s), %zu note(s)", Count(Severity::Problem), Count(Severity::Warning), Count(Severity::Note));
  out += Count(Severity::Problem) > 0 ? ": NOT OK\n" : ": OK\n";
  return out;
}

bool IsJunkPath(const std::string& path) {
  static const std::set<std::string> kNames = {".ds_store", "thumbs.db", "desktop.ini", "ehthumbs.db", "__macosx", ".git", ".svn",
                                               ".hg", ".idea", ".vscode", ".vs", "__pycache__", ".gitignore", ".gitattributes",
                                               ".gitkeep", ".directory", "icon\r", ".fseventsd", ".trashes", ".spotlight-v100",
                                               ".appledouble", ".localized"};
  size_t from = 0;
  while (from <= path.size()) {
    const size_t to = path.find('/', from);
    const std::string seg = LowerAscii(path.substr(from, to == std::string::npos ? std::string::npos : to - from));
    if (kNames.count(seg) != 0) return true;
    if (seg.rfind("._", 0) == 0 || seg.rfind("~$", 0) == 0 || seg.rfind(".#", 0) == 0 ||
        (seg.size() > 2 && seg.front() == '#' && seg.back() == '#')) {
      return true;
    }
    if (to == std::string::npos) {
      for (const char* ext : {"~", ".bak", ".swp", ".swo", ".tmp", ".temp", ".orig", ".rej", ".pyc", ".pyo", ".old", ".crdownload", ".part"}) {
        const size_t n = std::strlen(ext);
        if (seg.size() > n && seg.compare(seg.size() - n, n, ext) == 0) return true;
      }
      break;
    }
    from = to + 1;
  }
  return false;
}

std::string UnsafeNameReason(const std::string& name) { return UnsafePathReason(name); }

std::string Closest(const std::string& word, const std::vector<std::string>& choices) {
  const auto distance = [](const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
      cur[0] = i;
      for (size_t j = 1; j <= b.size(); j++) {
        cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
      }
      std::swap(prev, cur);
    }
    return prev[b.size()];
  };
  std::string best;
  size_t bestD = 3;
  const std::string lower = LowerAscii(word);
  for (const std::string& c : choices) {
    const size_t d = distance(lower, LowerAscii(c));
    if (d < bestD) {
      bestD = d;
      best = c;
    }
  }
  return best;
}

Report CheckMod(const Options& o) {
  Report report;
  report.folder = o.folder;
  const SafetyLimits limits;
  std::error_code ec;
  if (!fs::is_directory(o.folder, ec)) {
    report.Problem("no-folder", "", 0, "there is no folder " + PathText(o.folder));
    return report;
  }
  Tree tree;
  WalkTree(o.folder, tree, report, limits);

  const fs::path manifestPath = o.folder / "mod.json";
  std::string manifestText;
  {
    std::vector<uint8_t> bytes;
    if (!fs::is_regular_file(manifestPath, ec) || !ReadFileBytes(manifestPath, bytes, 1 << 20)) {
      std::string hint;
      std::vector<fs::path> inner;
      for (const auto& e : fs::directory_iterator(o.folder, ec)) {
        if (e.is_directory(ec) && fs::is_regular_file(e.path() / "mod.json", ec)) inner.push_back(e.path());
      }
      if (inner.size() == 1) hint = " (there is one in the folder \"" + PathText(inner[0].filename()) + "\": the mod is one folder further in)";
      report.Problem("no-manifest", "mod.json", 0, "mod.json is missing: every mod needs a mod.json directly in its folder" + hint);
    } else {
      manifestText.assign(bytes.begin(), bytes.end());
      CheckManifest(o, report, manifestText);
    }
  }

  DataSummary data;
  size_t luaCount = 0, luaCompiled = 0;
  std::vector<GameRules::ExtraFolder> extras;
  if (o.rules != nullptr) extras = o.rules->extraFolders;
  uint64_t totalBytes = 0;
  size_t fileCount = 0, replaced = 0, entries = 0, textures = 0, storyFiles = 0, dataFiles = 0, soundFiles = 0;
  std::map<std::string, std::string> lowerSeen;
  std::map<std::string, std::set<std::string>> scopes;
  const bool otherGames = report.manifestOk && o.rules != nullptr && !o.rules->gameIds.empty() &&
                          std::any_of(report.manifest.games.begin(), report.manifest.games.end(), [&](const std::string& g) {
                            return !std::any_of(o.rules->gameIds.begin(), o.rules->gameIds.end(), [&](const std::string& k) { return em::GameCodeMatches(g, k); });
                          });
  bool hasScriptsFolder = false;
  for (const std::string& d : tree.dirs) {
    const std::string why = ExecutableNameReason(d + "/x");
    if (why.rfind("part of a Mac", 0) == 0) report.Unsafe(Severity::Problem, "program-file", d, 0, "this folder is " + why + ": mods may not contain programs");
    if (LowerAscii(em::StripGameFolder(d)) == "scripts") hasScriptsFolder = true;
  }
  if (tree.files.size() > limits.maxFiles) {
    report.Unsafe(Severity::Problem, "too-many-files", "", 0, Fmt("the mod has %zu files; the most the port accepts is %zu", tree.files.size(), limits.maxFiles));
  }
  for (const FileInfo& f : tree.files) {
    if (IsJunkPath(f.rel)) {
      report.Note("junk", f.rel, 0, "an editor or system leftover; it does no harm but doesn't belong in a mod (the packer leaves it out)");
      continue;
    }
    fileCount++;
    totalBytes += f.size;
    const std::string lowerRel = LowerAscii(f.rel);
    const auto dup = lowerSeen.emplace(lowerRel, f.rel);
    if (!dup.second) {
      report.Problem("case-duplicate", f.rel, 0, "the same name as \"" + dup.first->second + "\" except for letter case: Windows and Mac can't keep both");
    }
    std::string why = UnsafePathReason(f.rel);
    if (!why.empty()) report.Unsafe(Severity::Problem, "bad-name", f.rel, 0, "the name has " + why);
    if (f.rel.size() > limits.maxPathBytes) report.Unsafe(Severity::Problem, "long-path", f.rel, 0, Fmt("the path is %zu bytes long; the most is %zu", f.rel.size(), limits.maxPathBytes));
    why = ExecutableNameReason(f.rel);
    if (!why.empty()) {
      report.Unsafe(Severity::Problem, "program-file", f.rel, 0, "this is " + why + ": mods may not contain programs or scripts the PC could run");
      continue;
    }
    if (f.size > limits.maxFileBytes) {
      report.Unsafe(Severity::Problem, "file-too-big", f.rel, 0, Fmt("this file is %llu MB; the most is %llu MB", static_cast<unsigned long long>(f.size >> 20), static_cast<unsigned long long>(limits.maxFileBytes >> 20)));
      continue;
    }
    std::vector<uint8_t> bytes;
    const Kind kind = KindOf(f.rel, &extras);
    const bool wantsContent = kind != Kind::DiscFile || f.size <= (64u << 20);
    if (wantsContent && !ReadFileBytes(f.path, bytes, limits.maxFileBytes)) {
      report.Problem("unreadable", f.rel, 0, "this file can't be read");
      continue;
    }
    if (!bytes.empty()) {
      why = ExecutableContentReason(bytes.data(), std::min<size_t>(bytes.size(), 4096));
      if (!why.empty()) {
        report.Unsafe(Severity::Problem, "program-content", f.rel, 0, "whatever its name, the contents are " + why + ": mods may not contain programs");
        continue;
      }
      why = ArchiveReason(f.rel, bytes.data(), std::min<size_t>(bytes.size(), 16));
    } else {
      why = ArchiveReason(f.rel, nullptr, 0);
    }
    if (!why.empty()) {
      report.Unsafe(Severity::Problem, "nested-archive", f.rel, 0, "this is " + why + "; the port can't look inside it, so it is not accepted in a mod. Put the files in the mod folder itself");
      continue;
    }
    const std::string inner = em::StripGameFolder(f.rel);
    const std::string top = inner.substr(0, inner.find('/'));
    std::string scope, restIgnored;
    em::SplitGameFolder(f.rel, scope, restIgnored);
    const auto used = [&](const char* what) { scopes[what].insert(scope); };
    switch (kind) {
    case Kind::ModJson:
      CheckJsonLimits(f.rel, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), limits, report);
      break;
    case Kind::Json: {
      const std::string text(bytes.begin(), bytes.end());
      if (!CheckJsonLimits(f.rel, text, limits, report)) break;
      if (LowerAscii(top) == "data") {
        dataFiles++;
        used("data");
        CheckDataFile(o.rules, report, f.rel, text, data);
      } else {
        Value root;
        Reader reader(text);
        if (!reader.Document(root)) {
          std::string what;
          const int line = SplitReaderError(reader.Error(), what);
          report.Problem("json-invalid", f.rel, line, "this is not valid JSON: " + what);
        }
        if (LowerAscii(top) == "sounds") { soundFiles++; used("sounds"); }
        else if (LowerAscii(top) != "mod.json" && f.rel.find('/') != std::string::npos) { storyFiles++; used("story"); }
      }
      break;
    }
    case Kind::Lua: {
      luaCount++;
      if (bytes.size() > em::kMaxScriptFileBytes) {
        report.Problem("lua-too-big", f.rel, 0, Fmt("this script is %zu KB; the game reads scripts up to %zu KB and switches the mod's scripts off for a bigger one", bytes.size() >> 10, em::kMaxScriptFileBytes >> 10));
        break;
      }
      bool compiles = true;
      used("scripts");
      if (f.rel.size() > 4 && LowerAscii(f.rel).compare(f.rel.size() - 4, 4, ".lua") == 0) {
        report.Note("lua-old-name", f.rel, 0, "the older .lua name: it still runs, as Luau (not Lua 5.4). Rename it to .luau");
      }
      CheckLuaFile(o.rules, report, f.rel, std::string(bytes.begin(), bytes.end()), compiles, scope.empty() && otherGames);
      luaCompiled += compiles ? 1 : 0;
      break;
    }
    case Kind::StoryText:
      if (bytes.size() > (4u << 20)) report.Problem("text-too-big", f.rel, 0, "this text file is over 4 MB");
      break;
    case Kind::Text:
      if (bytes.size() > (4u << 20)) report.Warn("text-too-big", f.rel, 0, "this text file is over 4 MB");
      break;
    case Kind::Png:
      CheckPng(f.rel, bytes, limits, report);
      if (LowerAscii(top) == "textures") { textures++; used("textures"); }
      break;
    case Kind::Sound:
      soundFiles++;
      used("sounds");
      break;
    case Kind::Dds:
      CheckDds(f.rel, bytes, limits, report);
      textures++;
      used("textures");
      break;
    case Kind::DiscFile:
      replaced++;
      used("files");
      break;
    case Kind::DiscEntry:
      if (top == "fsys") { entries++; used("entries"); } else { storyFiles++; used("story"); }
      if (o.rules != nullptr && o.rules->checkEntryFile && !bytes.empty()) o.rules->checkEntryFile(f.rel, bytes, report);
      break;
    case Kind::Unknown:
      report.Warn("unknown-kind", f.rel, 0, "the port does not read this kind of file here; it is ignored");
      break;
    }
  }
  if (totalBytes > limits.maxTotalBytes) {
    report.Unsafe(Severity::Problem, "mod-too-big", "", 0, Fmt("the mod is %llu MB; the most the port accepts is %llu MB", static_cast<unsigned long long>(totalBytes >> 20), static_cast<unsigned long long>(limits.maxTotalBytes >> 20)));
  }
  if (luaCount > em::kMaxScriptFiles) {
    report.Problem("lua-too-many", "scripts", 0, Fmt("the mod has %zu script files; the game reads at most %zu and uses none of them otherwise", luaCount, em::kMaxScriptFiles));
  }
  if (o.rules != nullptr && !o.rules->scriptHooks.empty() && luaCount > 0) {}

  if (report.manifestOk) {
    const em::Manifest& m = report.manifest;
    if (hasScriptsFolder && !m.scripts) report.Warn("scripts-not-enabled", "mod.json", 0, "the mod has a \"scripts\" folder but mod.json does not say \"scripts\": true; the scripts are not used");
    if (m.scripts && luaCount == 0) report.Warn("scripts-none", "mod.json", 0, "mod.json says \"scripts\": true but there are no scripts/*.luau");
    if (fileCount == 0 || (replaced + entries + dataFiles + luaCount + textures + storyFiles + soundFiles == 0)) {
      report.Warn("empty-mod", "", 0, "the mod has no files in \"files\", \"fsys\", \"data\", \"scripts\", \"sounds\" or \"textures\": it changes nothing");
    }
    report.support = em::SupportOf(o.folder, m);
    if (em::GameFolderNames(o.folder).empty() && em::HasSharedContent(o.folder)) {
      report.Note("flat-layout", "", 0, "this mod uses the older flat layout (data/, scripts/, ... at the top). It still loads, for the games mod.json names; a flat mod that names only \"xd\" is XD-only "
                  "(it is not loaded on XG). New layout: shared files at the top, a game's own files in games/xd/ (and games/xg/, games/colosseum/); see docs/modding/two-games.md");
    }
    {
      const auto names = [&](const char* alias) {
        return std::any_of(m.games.begin(), m.games.end(), [&](const std::string& g) { return em::GameCodeMatches(g, alias); });
      };
      const bool forXD = names("GXXE01"), forXG = names("GXXE01#xg");
      const std::vector<std::string> folders = em::GameFolderNames(o.folder);
      const bool hasXgFolder = std::find(folders.begin(), folders.end(), "XG") != folders.end();
      if (forXD && !forXG) {
        report.Note("xd-only", "mod.json", 0, "this mod is made for XD only (\"game\" does not name \"xg\"): the launcher and the game show it as \"made for XD, not XG\" and never load it on XG. "
                    "XG renumbers moves and changes data; add \"xg\" to \"game\" only if the mod is safe there, or give XG its own games/xg/ folder");
      }
      if (forXG && !hasXgFolder) {
        std::string first;
        for (const em::LayoutFile& f : em::LayoutFiles(o.folder, "GXXE01#xg", "data", false)) { first = f.rel; break; }
        const bool both = forXD;
        report.Note("xg-folder", "mod.json", 0, both ? "mod.json names \"xd\" and \"xg\" and there is no games/xg/ folder: XG reads the same files as XD (games/xd/ and the shared parts). You are saying that is safe on XG"
                                                       : "mod.json names \"xg\" only and there is no games/xg/ folder: XG reads the shared parts only");
        if (!first.empty() && both) {
          report.Warn("xg-xd-data", "data/" + first, 0, "the mod names \"xg\" but its data files (here data/" + first + ") are written for XD: XG numbers moves, items and Pokemon differently. "
                      "Check them on XG, or put XG's own version in games/xg/data/");
        }
      }
    }
    for (const std::string& name : em::GameFolderNames(o.folder)) {
      if (!std::any_of(m.games.begin(), m.games.end(), [&](const std::string& g) { return em::GameCodeMatches(name, g) || em::GameCodeMatches(g, name); })) {
        report.Warn("games-folder-unlisted", "games/" + name, 0, "no game in mod.json \"game\" matches this folder, so no game reads it: add the game's ID to \"game\"");
      }
    }
    {
      std::error_code gec;
      for (fs::directory_iterator it(o.folder / em::kGamesFolder, gec), end; !gec && it != end; it.increment(gec)) {
        std::error_code e2;
        const std::string name = PathText(it->path().filename());
        std::string upper = name;
        for (char& c : upper) c = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        if (it->is_directory(e2) && !em::ValidGameFolderName(upper)) {
          report.Warn("games-folder-name", "games/" + name, 0, "a folder under games/ is named xd or colosseum (or by the start of a game ID: GXX, GXXE, GXXE01); this one is not read");
        } else if (!it->is_directory(e2)) {
          report.Warn("games-folder-name", "games/" + name, 0, "only folders belong in games/ (games/xd/...); this file is not read");
        }
      }
    }
    for (const em::GameSupport& s : report.support) {
      if (s.level == em::GameSupport::Level::Nothing) {
        report.Warn("game-no-parts", "mod.json", 0, "the mod names " + s.code + " but has no games/ folder for it and no shared parts: it changes nothing there");
      }
    }
    em::ModFolder mf;
    mf.manifest = m;
    mf.folder = o.folder;
    mf.folderName = PathText(o.folder.filename());
    const em::AppliesVerdict verdict = em::CheckApplies(mf, o.rules != nullptr ? o.rules->looksOrSound : em::LooksOrSound(), o.deep != nullptr ? &o.deep->disc : nullptr);
    if (verdict.Downgraded()) {
      report.Warn("applies-claim", "mod.json", 0, std::string("mod.json says \"") + em::AppliesId(verdict.claimed) + "\" but the port will treat it as \"" +
                                                   em::AppliesId(verdict.effective) + "\": " + verdict.reason);
    }
    if (!o.modsFolder.empty()) {
      std::vector<em::Message> messages;
      const std::vector<em::ModFolder> others = em::FindMods(o.modsFolder, messages);
      for (size_t i = 0; i < m.required.size(); i++) {
        const auto it = std::find_if(others.begin(), others.end(), [&](const em::ModFolder& x) { return x.manifest.id == m.required[i] && x.folder != o.folder; });
        if (it == others.end()) {
          report.Warn("requires-missing", "mod.json", 0, "needs the mod \"" + m.required[i] + "\", which is not in " + PathText(o.modsFolder));
        } else if (!m.requiredVersions[i].SatisfiedBy(it->manifest.version)) {
          report.Warn("requires-version", "mod.json", 0, "needs \"" + m.required[i] + "\" " + m.requiredVersions[i].op + " " + m.requiredVersions[i].version + ", but the one found is version " +
                                                          (it->manifest.version.empty() ? std::string("(none)") : it->manifest.version));
        }
      }
      for (const std::string& c : m.conflicts) {
        const auto it = std::find_if(others.begin(), others.end(), [&](const em::ModFolder& x) { return x.manifest.id == c && x.folder != o.folder; });
        if (it != others.end()) report.Note("conflicts-present", "mod.json", 0, "says it can't be used with \"" + c + "\", which is in " + PathText(o.modsFolder));
      }
    } else if (!m.required.empty()) {
      std::string list;
      for (const std::string& r : m.required) list += (list.empty() ? "" : ", ") + r;
      report.Note("requires-unchecked", "mod.json", 0, "needs " + list + " (not checked: no mods folder was given)");
    }
    const auto only = [&](std::initializer_list<const char*> kinds) {
      std::set<std::string> all;
      for (const char* k : kinds) {
        const auto it = scopes.find(k);
        if (it != scopes.end()) all.insert(it->second.begin(), it->second.end());
      }
      if (all.empty() || all.count("") != 0) return std::string();
      std::string names;
      for (const std::string& n : all) names += (names.empty() ? "" : ", ") + n;
      return " [only for " + names + "]";
    };
    if (m.scripts && luaCount > 0) report.permissions.push_back({"scripts", "Runs Luau scripts in the game's sandbox (" + std::to_string(luaCount) + " file(s)); scripts can read and change what the game offers them, nothing else" + only({"scripts"})});
    if (replaced > 0) report.permissions.push_back({"replaces-files", "Replaces whole game files (" + std::to_string(replaced) + ")" + only({"files"})});
    if (entries > 0) report.permissions.push_back({"replaces-files", "Replaces files inside the game's archives (" + std::to_string(entries) + ")" + only({"entries"})});
    if (data.changesTables) report.permissions.push_back({"changes-tables", "Changes numbers in the game's tables (stats, moves, trainers, shops...)" + only({"data"})});
    if (data.text) report.permissions.push_back({"text-changes", "Rewrites game message text" + only({"data"})});
    if (data.adds) {
      std::string tables;
      for (const std::string& t : data.addTables) tables += (tables.empty() ? "" : ", ") + t;
      report.permissions.push_back({"adds-rows", "Adds new entries to the game's tables (" + tables + ")" + only({"data"})});
    }
    if (data.addsStory || storyFiles > 0) report.permissions.push_back({"adds-story", "Adds or changes story rooms, scripts or objects (" + std::to_string(storyFiles) + " file(s))" + only({"story", "data"})});
    if (textures > 0) report.permissions.push_back({"textures", "Replaces pictures (" + std::to_string(textures) + " texture file(s))" + only({"textures"})});
    std::string updates;
    {
      Value root;
      Reader r(manifestText);
      if (r.Document(root) && root.kind == Value::Kind::Object && root.fields.count("updates") && root.fields.at("updates").kind == Value::Kind::String) updates = root.fields.at("updates").text;
    }
    if (!updates.empty()) report.permissions.push_back({"update-link", "Has an update link (" + updates + "): the launcher may ask that address whether a newer version exists"});
  }
  (void)luaCompiled;

  if (o.rules != nullptr && report.manifestOk) {
    const GameRules::Context context{o.folder, report.manifest, o.deep != nullptr};
    for (const GameRules::Extra& e : o.rules->extra) {
      if (e.run) e.run(context, report);
    }
  }

  if (o.deep != nullptr && report.manifestOk) {
    em::LoadOptions load = *o.deep;
    load.modsFolder = o.folder.parent_path();
    load.listReadOnly = true;
    load.usePlaythroughMods = false;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path list = fs::temp_directory_path(ec) / ("modcheck_" + std::to_string(stamp) + ".ini");
    std::string ini = "profile = default\n[profile default]\n";
    {
      std::vector<em::Message> ignore;
      const std::vector<em::ModFolder> around = em::FindMods(load.modsFolder, ignore);
      for (const std::string& r : report.manifest.required) {
        const auto it = std::find_if(around.begin(), around.end(), [&](const em::ModFolder& x) { return x.manifest.id == r; });
        if (it != around.end()) ini += r + " = on\n";
      }
      ini += report.manifest.id + " = on\n";
      load.listFile = list;
      {
        std::ofstream out(list);
        out << ini;
      }
      const em::LoadResult result = em::Load(load);
      fs::remove(list, ec);
      report.deepDone = true;
      std::vector<std::string> otherNames;
      for (const em::ModFolder& x : around) {
        if (x.manifest.id != report.manifest.id) {
          otherNames.push_back(x.manifest.id);
          otherNames.push_back(x.manifest.name);
          otherNames.push_back(x.folderName);
        }
      }
      const std::regex where(R"(((?:data|scripts|rooms|objects|fsys|files|textures)/[A-Za-z0-9_./#$ -]*[A-Za-z0-9_#])(?: line (\d+))?)");
      for (const em::Message& msg : result.messages) {
        if (msg.text.rfind("Mods on:", 0) == 0 || msg.text.rfind("New mod found", 0) == 0) continue;
        const bool mine = msg.text.find(report.manifest.id) != std::string::npos || msg.text.find(report.manifest.name) != std::string::npos ||
                          msg.text.find(PathText(o.folder.filename())) != std::string::npos;
        bool about_other = false;
        for (const std::string& n : otherNames) {
          if (!n.empty() && msg.text.find(n) != std::string::npos) about_other = true;
        }
        if (!mine && about_other) continue;
        std::string file;
        int line = 0;
        std::smatch m;
        if (std::regex_search(msg.text, m, where)) {
          file = m[1].str();
          if (m[2].matched) line = std::atoi(m[2].str().c_str());
        }
        report.Add(msg.level == em::Message::Level::Problem ? Severity::Problem : msg.level == em::Message::Level::Warning ? Severity::Warning : Severity::Note,
                   "game-loader", file, line, "the game's loader says: " + msg.text);
      }
      if (!result.addedRows.empty() && !report.HasPermission("adds-rows")) report.permissions.push_back({"adds-rows", "Adds new entries to the game's tables"});
    }
  }
  return report;
}

}
