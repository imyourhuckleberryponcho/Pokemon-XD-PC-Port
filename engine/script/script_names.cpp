#include "script_names.h"

#include <cstdlib>
#include <memory>

namespace engine::script {

namespace {

const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

uint32_t NextChar(std::string_view s, size_t& i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = 1;
  uint32_t cp = c;
  if (c >= 0xF0) { n = 4; cp = c & 0x07; }
  else if (c >= 0xE0) { n = 3; cp = c & 0x0F; }
  else if (c >= 0xC0) { n = 2; cp = c & 0x1F; }
  if (n == 1 || i + n > s.size()) { i++; return c; }
  for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
  i += n;
  return cp;
}

bool IsIdentifier(const std::string& s) {
  if (s.empty() || !(s[0] >= 'A' && s[0] <= 'Z')) return false;
  for (char c : s) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

std::string Upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

}

std::string NameIdentifier(std::string_view text) {
  std::string out;
  bool gap = false;
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = NextChar(text, i);
    char c = 0;
    if (cp >= 'A' && cp <= 'Z') c = static_cast<char>(cp);
    else if (cp >= 'a' && cp <= 'z') c = static_cast<char>(cp - 'a' + 'A');
    else if (cp >= '0' && cp <= '9') c = static_cast<char>(cp);
    else if (cp >= 0xC0 && cp <= 0xFF) {
      const char l = kLatin1[cp - 0xC0];
      if (l >= 'A' && l <= 'Z') c = l;
      else if (l >= 'a' && l <= 'z') c = static_cast<char>(l - 'a' + 'A');
    } else if (cp == 0x2640 || cp == 0x2642) {
      if (!out.empty() && out.back() != '_') out += '_';
      out += cp == 0x2640 ? 'F' : 'M';
      gap = false;
      continue;
    }
    if (c != 0) {
      if (gap && !out.empty() && out.back() != '_') out += '_';
      out += c;
      gap = false;
    } else {
      gap = true;
    }
  }
  if (out.empty()) return out;
  if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
  return out;
}

std::string NameKey(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = NextChar(text, i);
    if (cp >= 'A' && cp <= 'Z') out += static_cast<char>(cp - 'A' + 'a');
    else if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) out += static_cast<char>(cp);
    else if (cp >= 0xC0 && cp <= 0xFF) {
      const char l = kLatin1[cp - 0xC0];
      if (l >= 'A' && l <= 'Z') out += static_cast<char>(l - 'A' + 'a');
      else if (l >= 'a' && l <= 'z') out += l;
    } else if (cp == 0x2640) out += 'f';
    else if (cp == 0x2642) out += 'm';
  }
  return out;
}

bool ParseNameFile(std::string_view text, std::vector<NameEntry>& out, std::vector<std::string>& problems) {
  bool ok = true;
  size_t lineNo = 0;
  std::map<std::string, bool> seen;
  for (size_t pos = 0; pos <= text.size();) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;
    lineNo++;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (line.empty() || line.front() == '#') continue;
    const std::string where = "line " + std::to_string(lineNo) + ": ";
    size_t i = 0;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
    const std::string number(line.substr(0, i));
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t') j++;
    const std::string name(line.substr(i, j - i));
    char* stop = nullptr;
    const long long value = std::strtoll(number.c_str(), &stop, 0);
    if (number.empty() || stop == nullptr || *stop != 0 || value < 0) {
      problems.push_back(where + "\"" + number + "\" is not a number");
      ok = false;
      continue;
    }
    if (!IsIdentifier(name)) {
      problems.push_back(where + "the name must be capitals, digits and _, starting with a letter (got \"" + name + "\")");
      ok = false;
      continue;
    }
    if (seen[name]) {
      problems.push_back(where + "the name " + name + " is used twice");
      ok = false;
      continue;
    }
    seen[name] = true;
    out.push_back({name, static_cast<int64_t>(value)});
  }
  return ok;
}

void Names::Add(const std::string& categoryName, const std::vector<NameEntry>& entries) {
  Category& c = mCategories[categoryName];
  size_t skipped = 0, renamed = 0;
  for (const NameEntry& e : entries) {
    std::string id = NameIdentifier(e.name);
    if (id.empty()) { skipped++; continue; }
    if (c.used.count(id) != 0) {
      for (int n = 2;; n++) {
        const std::string candidate = id + "_" + std::to_string(n);
        if (c.used.count(candidate) == 0) { id = candidate; break; }
      }
      renamed++;
    }
    c.used[id] = true;
    c.entries.push_back({id, e.value});
    c.byKey.emplace(NameKey(id), e.value);
    c.byKey.emplace(NameKey(e.name), e.value);
    c.byValue.emplace(e.value, id);
  }
  if (skipped != 0) mNotes.push_back(categoryName + ": " + std::to_string(skipped) + " name(s) had no letters or digits and have no table name");
  if (renamed != 0) mNotes.push_back(categoryName + ": " + std::to_string(renamed) + " name(s) were used more than once; later ones end in _2, _3...");
}

bool Names::Has(const std::string& category) const { return mCategories.count(category) != 0; }

size_t Names::Count(const std::string& category) const {
  const auto it = mCategories.find(category);
  return it == mCategories.end() ? 0 : it->second.entries.size();
}

std::vector<std::string> Names::Categories() const {
  std::vector<std::string> out;
  for (const auto& kv : mCategories) out.push_back(kv.first);
  return out;
}

bool Names::Find(const std::string& category, std::string_view text, int64_t& value) const {
  const auto it = mCategories.find(category);
  if (it == mCategories.end()) return false;
  const auto k = it->second.byKey.find(NameKey(text));
  if (k == it->second.byKey.end()) return false;
  value = k->second;
  return true;
}

std::string Names::NameOf(const std::string& category, int64_t value) const {
  const auto it = mCategories.find(category);
  if (it == mCategories.end()) return {};
  const auto v = it->second.byValue.find(value);
  return v == it->second.byValue.end() ? std::string() : v->second;
}

const std::vector<NameEntry>* Names::Entries(const std::string& category) const {
  const auto it = mCategories.find(category);
  return it == mCategories.end() ? nullptr : &it->second.entries;
}

std::string Names::Summary() const {
  std::string out;
  for (const auto& kv : mCategories) out += (out.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second.entries.size());
  return out.empty() ? "none" : out;
}

void Names::Install(Host& host) const {
  for (const auto& kv : mCategories) {
    Value table = Value::Table();
    for (const NameEntry& e : kv.second.entries) table.Set(e.name, e.value);
    host.SetConstants(Upper(kv.first), table);
  }
  auto copy = std::make_shared<Names>(*this);
  const auto known = [copy](const std::string& category, std::string& error) {
    if (copy->Has(category)) return true;
    std::string list;
    for (const std::string& c : copy->Categories()) list += (list.empty() ? "" : ", ") + c;
    error = "no names for \"" + category + "\" (there are: " + list + ")";
    return false;
  };
  host.SetApi("name_of", [copy, known](const std::string&, const std::vector<Value>& args, std::string& error) -> Value {
    if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::Int) {
      error = "name_of(\"category\", number)";
      return Value();
    }
    if (!known(args[0].text, error)) return Value();
    const std::string n = copy->NameOf(args[0].text, args[1].integer);
    return n.empty() ? Value() : Value::Text(n);
  });
  host.SetApi("find", [copy, known](const std::string&, const std::vector<Value>& args, std::string& error) -> Value {
    if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::String) {
      error = "find(\"category\", \"name\")";
      return Value();
    }
    if (!known(args[0].text, error)) return Value();
    int64_t v = 0;
    return copy->Find(args[0].text, args[1].text, v) ? Value::Int(v) : Value();
  });
}

}
