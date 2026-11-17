#include "mods.h"

#include "json.h"

#include "i18n.h"

#include <charconv>
#include <cstdarg>
#include <cstdio>

namespace engine::mods {

namespace {

using json::Value;
using json::Reader;
using engine::i18n::Tr;

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

bool TextField(const Value& obj, const char* key, std::string& out, std::string& error) {
  const auto it = obj.fields.find(key);
  if (it == obj.fields.end() || it->second.kind == Value::Kind::Null) {
    return true;
  }
  if (it->second.kind != Value::Kind::String) {
    error = Fmt(Tr("\"%s\" must be a text in quotes"), key);
    return false;
  }
  out = it->second.text;
  return true;
}

bool ListField(const Value& obj, const char* key, std::vector<std::string>& out, std::string& error) {
  const auto it = obj.fields.find(key);
  if (it == obj.fields.end() || it->second.kind == Value::Kind::Null) {
    return true;
  }
  if (it->second.kind == Value::Kind::String) {
    out.push_back(it->second.text);
    return true;
  }
  if (it->second.kind == Value::Kind::Array) {
    for (const Value& v : it->second.items) {
      if (v.kind != Value::Kind::String) {
        break;
      }
      out.push_back(v.text);
    }
    if (out.size() == it->second.items.size()) {
      return true;
    }
  }
  error = Fmt(Tr("\"%s\" must be a text or a list of texts, like [\"a\", \"b\"]"), key);
  return false;
}

bool IntField(const Value& obj, const char* key, int& out, std::string& error) {
  const auto it = obj.fields.find(key);
  if (it == obj.fields.end() || it->second.kind == Value::Kind::Null) {
    return true;
  }
  const double n = it->second.number;
  if (it->second.kind != Value::Kind::Number || n != static_cast<double>(static_cast<int>(n)) || n < -1000000 ||
      n > 1000000) {
    error = Fmt(Tr("\"%s\" must be a whole number"), key);
    return false;
  }
  out = static_cast<int>(n);
  return true;
}

}

int CompareVersions(std::string_view a, std::string_view b) {
  while (!a.empty() || !b.empty()) {
    const size_t da = a.find('.'), db = b.find('.');
    const std::string_view pa = a.substr(0, da), pb = b.substr(0, db);
    a = da == std::string_view::npos ? std::string_view() : a.substr(da + 1);
    b = db == std::string_view::npos ? std::string_view() : b.substr(db + 1);
    unsigned long long na = 0, nb = 0;
    const auto ra = std::from_chars(pa.data(), pa.data() + pa.size(), na);
    const auto rb = std::from_chars(pb.data(), pb.data() + pb.size(), nb);
    const bool numA = pa.empty() || (ra.ec == std::errc() && ra.ptr == pa.data() + pa.size());
    const bool numB = pb.empty() || (rb.ec == std::errc() && rb.ptr == pb.data() + pb.size());
    na = pa.empty() ? 0 : na;
    nb = pb.empty() ? 0 : nb;
    if (numA && numB) {
      if (na != nb) {
        return na < nb ? -1 : 1;
      }
    } else if (pa != pb) {
      return pa < pb ? -1 : 1;
    }
  }
  return 0;
}

bool VersionNeed::SatisfiedBy(std::string_view have) const {
  if (op.empty()) {
    return true;
  }
  const int c = CompareVersions(have, version);
  return op == ">=" ? c >= 0 : op == ">" ? c > 0 : op == "<=" ? c <= 0 : op == "<" ? c < 0 : c == 0;
}

const char* AppliesId(Applies applies) {
  switch (applies) {
  case Applies::AnyTime: return "any_time";
  case Applies::Restart: return "restart";
  default: return "new_playthrough";
  }
}

const char* AppliesName(Applies applies) {
  switch (applies) {
  case Applies::AnyTime: return "Any time";
  case Applies::Restart: return "On restart";
  default: return "New playthrough only";
  }
}

bool ParseApplies(std::string_view text, Applies& out) {
  for (Applies a : {Applies::AnyTime, Applies::Restart, Applies::NewPlaythrough}) {
    if (text == AppliesId(a)) {
      out = a;
      return true;
    }
  }
  return false;
}

bool ValidModId(std::string_view id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
      return false;
    }
  }
  return true;
}

bool ParseManifest(std::string_view json, Manifest& out, std::string& error) {
  out = Manifest{};
  Value root;
  Reader reader(json);
  if (!reader.Document(root)) {
    error = Fmt(Tr("not valid JSON (%s)"), reader.Error().c_str());
    return false;
  }
  if (root.kind != Value::Kind::Object) {
    error = Tr("must be one { ... } block");
    return false;
  }
  int format = kManifestFormat;
  if (!TextField(root, "id", out.id, error) || !TextField(root, "name", out.name, error) ||
      !TextField(root, "version", out.version, error) || !TextField(root, "author", out.author, error) ||
      !TextField(root, "description", out.description, error) || !ListField(root, "game", out.games, error) ||
      !ListField(root, "requires", out.required, error) || !ListField(root, "conflicts", out.conflicts, error) ||
      !ListField(root, "after", out.after, error) || !ListField(root, "before", out.before, error) ||
      !ListField(root, "works_with", out.worksWith, error) || !ListField(root, "recommends", out.recommends, error) ||
      !ListField(root, "only_with", out.onlyWith, error) || !TextField(root, "updates", out.updates, error) ||
      !TextField(root, "author_key", out.authorKey, error) ||
      !IntField(root, "priority", out.priority, error) || !IntField(root, "format", format, error)) {
    return false;
  }
  {
    const auto it = root.fields.find("scripts");
    if (it != root.fields.end() && it->second.kind != Value::Kind::Null) {
      if (it->second.kind != Value::Kind::Bool) {
        error = Tr("\"scripts\" must be true or false");
        return false;
      }
      out.scripts = it->second.boolean;
    }
  }
  if (!IntField(root, "script_api", out.scriptApi, error)) {
    return false;
  }
  if (out.scriptApi > kScriptApi) {
    error = Fmt(Tr("made for a newer script interface than this port has (script_api %d)"), out.scriptApi);
    return false;
  }
  std::string applies;
  if (!TextField(root, "applies", applies, error)) {
    return false;
  }
  if (!applies.empty()) {
    if (!ParseApplies(applies, out.applies)) {
      error = Fmt(Tr("\"applies\" must be \"any_time\", \"restart\" or \"new_playthrough\" (it is \"%s\")"), applies.c_str());
      return false;
    }
    out.appliesGiven = true;
  }
  if (format > kManifestFormat) {
    error = Fmt(Tr("made for a newer version of the port (mod.json format %d)"), format);
    return false;
  }
  if (!ValidModId(out.id)) {
    error = out.id.empty() ? std::string(Tr("has no \"id\""))
                           : Fmt(Tr("\"id\" may only have letters, digits, '-', '_' and '.' (\"%s\")"), out.id.c_str());
    return false;
  }
  for (std::string& other : out.required) {
    VersionNeed need;
    const size_t op = other.find_first_of("<>=");
    std::string_view id = std::string_view(other).substr(0, op);
    while (!id.empty() && id.back() == ' ') {
      id.remove_suffix(1);
    }
    if (op != std::string::npos) {
      size_t v = op;
      while (v < other.size() && (other[v] == '<' || other[v] == '>' || other[v] == '=')) {
        v++;
      }
      need.op = other.substr(op, v - op);
      while (v < other.size() && other[v] == ' ') {
        v++;
      }
      need.version = other.substr(v);
      if ((need.op != ">=" && need.op != ">" && need.op != "<=" && need.op != "<" && need.op != "=" && need.op != "==") ||
          need.version.empty()) {
        error = Fmt(Tr("\"requires\" has \"%s\": write a version condition like \"other-mod >= 1.2\""), other.c_str());
        return false;
      }
    }
    if (!ValidModId(id)) {
      error = Fmt(Tr("\"requires\" has \"%s\", which isn't a mod id"), other.c_str());
      return false;
    }
    other = std::string(id);
    out.requiredVersions.push_back(std::move(need));
  }
  for (const std::string& other : out.after) {
    if (!ValidModId(other)) {
      error = Fmt(Tr("\"after\" has \"%s\", which isn't a mod id"), other.c_str());
      return false;
    }
  }
  {
    const auto links = root.fields.find("links");
    if (links != root.fields.end() && links->second.kind != Value::Kind::Null) {
      if (links->second.kind != Value::Kind::Object) {
        error = Tr("\"links\" must be a block like {\"other-mod\": \"https://...\"}");
        return false;
      }
      for (const auto& [id, value] : links->second.fields) {
        if (!ValidModId(id) || value.kind != Value::Kind::String) {
          error = Fmt(Tr("\"links\" has \"%s\": a mod id and an address in quotes are expected"), id.c_str());
          return false;
        }
        out.links[id] = value.text;
      }
    }
  }
  if (!out.authorKey.empty()) {
    bool hex = out.authorKey.size() == 64;
    for (char c : out.authorKey) hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    if (!hex) {
      error = Tr("\"author_key\" must be 64 lower case hex digits (the author's public key)");
      return false;
    }
  }
  for (const auto& [field, ids] : {std::pair<const char*, const std::vector<std::string>*>{"before", &out.before},
                                   {"works_with", &out.worksWith},
                                   {"recommends", &out.recommends},
                                   {"only_with", &out.onlyWith}}) {
    for (const std::string& other : *ids) {
      if (!ValidModId(other)) {
        error = Fmt(Tr("\"%s\" has \"%s\", which isn't a mod id"), field, other.c_str());
        return false;
      }
    }
  }
  for (const std::string& other : out.conflicts) {
    if (!ValidModId(other)) {
      error = Fmt(Tr("\"conflicts\" has \"%s\", which isn't a mod id"), other.c_str());
      return false;
    }
  }
  if (out.games.empty()) {
    error = Tr("has no \"game\" (the game ID it is made for, e.g. \"GXXE01\")");
    return false;
  }
  if (out.name.empty()) {
    out.name = out.id;
  }
  return true;
}

}
