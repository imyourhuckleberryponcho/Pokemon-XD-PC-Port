#include "data_mods.h"

#include "json.h"
#include "mods.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <tuple>

namespace engine::mods {

namespace {

using json::Value;

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return out;
}

bool ParseNumberText(std::string_view s, uint32_t& out) {
  if (!s.empty() && s[0] == '#') {
    s.remove_prefix(1);
  }
  int base = 10;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s.remove_prefix(2);
    base = 16;
  }
  if (s.empty()) {
    return false;
  }
  const auto r = std::from_chars(s.data(), s.data() + s.size(), out, base);
  return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

std::string Quote(std::string_view s) { return "\"" + std::string(s) + "\""; }

class Applier {
public:
  Applier(const DataCatalog& catalog, const EntryReader& read, std::vector<Message>& messages)
      : mCatalog(catalog), mRead(read), mMessages(messages) {}

  void File(const DataModFile& file) {
    Value root;
    json::Reader reader(file.text);
    if (!reader.Document(root)) {
      Problem(file, 0, "is not valid JSON (" + reader.Error() + "); none of it is used");
      return;
    }
    if (root.kind != Value::Kind::Object) {
      Problem(file, root.line, "must be one { ... } block with tables in it; none of it is used");
      return;
    }
    for (const std::string& key : root.order) {
      const Value& v = root.fields.at(key);
      if (key.empty() || key[0] == '_') {
        continue;
      }
      if (key == "format") {
        if (v.kind != Value::Kind::Number || v.number > 1) {
          Problem(file, v.line, "is made for a newer version of the port (\"format\"); none of it is used");
          return;
        }
        continue;
      }
      const DataTable* table = mCatalog.FindTable(key);
      if (table == nullptr) {
        Problem(file, v.line, "names the table " + Quote(key) + ", which the game doesn't have" + Hint());
        continue;
      }
      if (!Usable(*table)) {
        Problem(file, v.line, "changes " + Quote(table->name) + ", which can't be read from this disc");
        continue;
      }
      if (v.kind != Value::Kind::Object) {
        Problem(file, v.line, Quote(key) + " must be a { ... } block of entries, like { \"#1\": { ... } }");
        continue;
      }
      for (const std::string& entryKey : v.order) {
        if (entryKey.empty() || entryKey[0] == '_') {
          continue;
        }
        const Value& ev = v.fields.at(entryKey);
        uint32_t entry = 0;
        if (!FindEntry(file, *table, entryKey, ev.line, entry)) {
          continue;
        }
        if (ev.kind != Value::Kind::Object) {
          Problem(file, ev.line, EntryText(*table, entry) + " must be a { ... } block of fields, like { \"field\": 5 }");
          continue;
        }
        for (const std::string& fieldKey : ev.order) {
          if (fieldKey.empty() || fieldKey[0] == '_') {
            continue;
          }
          Field(file, *table, entry, fieldKey, ev.fields.at(fieldKey));
        }
      }
    }
  }

  DataApplyResult Finish() {
    DataApplyResult out;
    for (auto& [key, w] : mWork) {
      if (w.now != w.before) {
        out.entries.emplace(key, std::move(w.now));
      }
    }
    for (auto& [key, change] : mChanges) {
      out.changes.push_back(std::move(change));
    }
    return out;
  }

private:
  const DataCatalog& mCatalog;
  const EntryReader& mRead;
  std::vector<Message>& mMessages;

  struct Work {
    bool ok = false;
    std::vector<uint8_t> before, now;
  };
  std::map<std::pair<std::string, std::string>, Work> mWork;
  std::map<std::string, bool> mUsable;
  struct Owner {
    std::string mod, where;
    uint8_t bits = 0;
  };
  std::map<std::tuple<std::string, uint32_t, uint32_t>, std::vector<Owner>> mOwners;
  std::map<std::tuple<std::string, uint32_t, uint32_t, uint8_t>, DataChange> mChanges;
  std::set<std::string> mSaid;
  std::map<std::string, std::map<uint32_t, std::string>> mReverse;

  std::string Hint() const { return mCatalog.toolHint.empty() ? "" : " (" + mCatalog.toolHint + ")"; }

  void Say(Message::Level level, const std::string& text) {
    if (mSaid.insert(text).second) {
      mMessages.push_back({level, text});
    }
  }
  static std::string Where(const DataModFile& file, size_t line) {
    return file.label + (line != 0 ? " line " + std::to_string(line) : "");
  }
  void Problem(const DataModFile& file, size_t line, const std::string& what) {
    Say(Message::Level::Problem, "The mod " + Quote(file.modName) + ", " + Where(file, line) + ": " + what + ".");
  }

  Work& WorkFor(const DataPlace& p) {
    auto key = std::make_pair(p.archive, p.entry);
    auto it = mWork.find(key);
    if (it == mWork.end()) {
      Work w;
      w.ok = mRead && mRead(p.archive, p.entry, w.before);
      w.now = w.before;
      it = mWork.emplace(std::move(key), std::move(w)).first;
    }
    return it->second;
  }

  bool Usable(const DataTable& t) {
    const auto it = mUsable.find(t.name);
    if (it != mUsable.end()) {
      return it->second;
    }
    bool ok = !t.places.empty() && t.entrySize != 0;
    for (const DataPlace& p : t.places) {
      const Work& w = WorkFor(p);
      ok = ok && w.ok && static_cast<uint64_t>(p.offset) + static_cast<uint64_t>(t.count) * t.entrySize <= w.before.size();
    }
    for (const DataField& f : t.fields) {
      ok = ok && (f.size == 1 || f.size == 2 || f.size == 4) && f.count != 0 &&
           f.offset + static_cast<uint64_t>(f.count - 1) * f.Stride() + f.size <= t.entrySize;
    }
    mUsable[t.name] = ok;
    return ok;
  }

  const std::map<uint32_t, std::string>& Reverse(const std::string& listName) {
    auto it = mReverse.find(listName);
    if (it == mReverse.end()) {
      std::map<uint32_t, std::string> m;
      if (const NameList* list = mCatalog.FindList(listName)) {
        for (const auto& [name, number] : list->names) {
          m.emplace(number, name);
        }
      }
      it = mReverse.emplace(listName, std::move(m)).first;
    }
    return it->second;
  }

  std::string NameOf(const std::string& listName, uint32_t number) {
    if (listName.empty()) {
      return "";
    }
    const auto& m = Reverse(listName);
    const auto it = m.find(number);
    return it != m.end() ? it->second : "";
  }

  std::string EntryText(const DataTable& t, uint32_t entry) {
    const std::string name = NameOf(t.entryNames, entry);
    return t.name + " #" + std::to_string(entry) + (name.empty() ? "" : " (" + name + ")");
  }

  bool Lookup(const DataModFile& file, size_t line, const std::string& listName, const std::string& name, uint32_t& out,
              const std::string& what) {
    const NameList* list = mCatalog.FindList(listName);
    if (list == nullptr) {
      Problem(file, line, what + " can only be given as a number (no names for it on this disc)");
      return false;
    }
    const std::string key = NameKey(name);
    std::set<uint32_t> found;
    for (const auto& [n, number] : list->names) {
      if (!key.empty() && NameKey(n) == key) {
        found.insert(number);
      }
    }
    if (found.size() == 1) {
      out = *found.begin();
      return true;
    }
    if (found.empty()) {
      Problem(file, line, "no " + listName + " is called " + Quote(name) + " on this disc" + Hint());
    } else {
      std::string numbers;
      for (uint32_t n : found) {
        numbers += (numbers.empty() ? "#" : ", #") + std::to_string(n);
      }
      Problem(file, line, "several " + listName + " numbers are called " + Quote(name) + " (" + numbers +
                              "); write the number meant, like \"#" + std::to_string(*found.begin()) + "\"");
    }
    return false;
  }

  bool FindEntry(const DataModFile& file, const DataTable& t, const std::string& key, size_t line, uint32_t& entry) {
    bool ok = ParseNumberText(key, entry);
    if (!ok && !t.entryNames.empty()) {
      ok = Lookup(file, line, t.entryNames, key, entry, "an entry of " + t.name);
      if (!ok) {
        return false;
      }
    } else if (!ok) {
      Problem(file, line, "the entries of " + t.name + " are named by number, like \"#3\" (not " + Quote(key) + ")");
      return false;
    }
    if (entry >= t.count) {
      Problem(file, line, t.name + " has entries #0 to #" + std::to_string(t.count == 0 ? 0 : t.count - 1) + ", no #" +
                              std::to_string(entry));
      return false;
    }
    return true;
  }

  const DataField* FindField(const DataTable& t, std::string_view name) const {
    for (const DataField& f : t.fields) {
      if (f.name == name) {
        return &f;
      }
    }
    const std::string lower = Lower(name);
    for (const DataField& f : t.fields) {
      if (Lower(f.name) == lower) {
        return &f;
      }
    }
    return nullptr;
  }

  void Field(const DataModFile& file, const DataTable& t, uint32_t entry, const std::string& key, const Value& v) {
    const DataField* f = FindField(t, key);
    int element = -1;
    if (f == nullptr && key.size() > 3 && key.back() == ']') {
      const size_t open = key.rfind('[');
      uint32_t i = 0;
      if (open != std::string::npos && open > 0 && ParseNumberText(std::string_view(key).substr(open + 1, key.size() - open - 2), i)) {
        f = FindField(t, std::string_view(key).substr(0, open));
        if (f != nullptr && i >= f->count) {
          Problem(file, v.line, f->name + " has " + std::to_string(f->count) + " values, [0] to [" +
                                    std::to_string(f->count - 1) + "]; there is no " + key);
          return;
        }
        element = static_cast<int>(i);
      }
    }
    if (f == nullptr) {
      Problem(file, v.line, t.name + " has no field " + Quote(key) + Hint());
      return;
    }
    if (!f->editable) {
      Problem(file, v.line, f->name + " in " + t.name + " can't be changed by mods" +
                                (f->note.empty() ? std::string(" (the game program fills it in or relies on it)") : " (" + f->note + ")"));
      return;
    }
    if (element >= 0) {
      Set(file, t, entry, *f, static_cast<uint32_t>(element), v);
      return;
    }
    if (f->count == 1) {
      if (v.kind == Value::Kind::Array) {
        Problem(file, v.line, f->name + " is one value, not a list");
        return;
      }
      Set(file, t, entry, *f, 0, v);
      return;
    }
    if (v.kind != Value::Kind::Array) {
      Problem(file, v.line, f->name + " has " + std::to_string(f->count) + " values: write a list like [1, 2] (null for " +
                                "one to leave as it is), or one of them as " + f->name + "[0]");
      return;
    }
    if (v.items.size() > f->count) {
      Problem(file, v.line, f->name + " has " + std::to_string(f->count) + " values, the list has " +
                                std::to_string(v.items.size()));
      return;
    }
    for (size_t i = 0; i < v.items.size(); i++) {
      if (v.items[i].kind != Value::Kind::Null) {
        Set(file, t, entry, *f, static_cast<uint32_t>(i), v.items[i]);
      }
    }
  }

  bool ToNumber(const DataModFile& file, const DataField& f, const std::string& label, const Value& v, uint32_t& out) {
    const uint32_t max = f.MaxValue();
    if (v.kind == Value::Kind::Bool) {
      out = v.boolean ? 1 : 0;
      return true;
    }
    if (v.kind == Value::Kind::Number) {
      if (v.number < 0 || v.number != static_cast<double>(static_cast<uint64_t>(v.number)) || v.number > max) {
        Problem(file, v.line, label + " must be a whole number from 0 to " + std::to_string(max) + " (not " +
                                  json::Number(v.number) + ")");
        return false;
      }
      out = static_cast<uint32_t>(v.number);
      return true;
    }
    if (v.kind == Value::Kind::String) {
      uint32_t n = 0;
      if (ParseNumberText(v.text, n) && (v.text[0] == '#' || v.text.rfind("0x", 0) == 0 || v.text.rfind("0X", 0) == 0)) {
        out = n;
      } else if (f.values.empty()) {
        Problem(file, v.line, label + " is a number; " + Quote(v.text) + " isn't one");
        return false;
      } else if (!Lookup(file, v.line, f.values, v.text, out, label)) {
        return false;
      }
      if (out > max) {
        Problem(file, v.line, label + " must be from 0 to " + std::to_string(max) + " (" + Quote(v.text) + " is " +
                                  std::to_string(out) + ")");
        return false;
      }
      return true;
    }
    Problem(file, v.line, label + " needs a number" + (f.values.empty() ? std::string() : " or a " + f.values + " name"));
    return false;
  }

  void Set(const DataModFile& file, const DataTable& t, uint32_t entry, const DataField& f, uint32_t element, const Value& v) {
    const std::string label = f.count > 1 ? f.name + "[" + std::to_string(element) + "]" : f.name;
    uint32_t value = 0;
    if (!ToNumber(file, f, label, v, value)) {
      return;
    }
    const uint32_t at = f.offset + element * f.Stride();
    const uint32_t full = f.size == 4 ? 0xFFFFFFFFu : (1u << (8 * f.size)) - 1;
    const uint32_t mask = f.bitWidth != 0 ? f.MaxValue() << f.bitShift : full;
    const std::string where = Where(file, v.line);

    for (uint32_t b = 0; b < f.size; b++) {
      const uint8_t bits = static_cast<uint8_t>(mask >> (8 * (f.size - 1 - b)));
      if (bits == 0) {
        continue;
      }
      auto& owners = mOwners[{t.name, entry, at + b}];
      for (auto it = owners.begin(); it != owners.end();) {
        if ((it->bits & bits) == 0) {
          ++it;
          continue;
        }
        if (it->mod != file.mod) {
          Say(Message::Level::Warning, "Conflict: the mods " + Quote(ModName(it->mod)) + " and " + Quote(file.modName) +
                                           " both change " + label + " of " + EntryText(t, entry) + ". " +
                                           Quote(file.modName) + " (further down the list) is used.");
        } else if (it->where != where) {
          Say(Message::Level::Warning, "The mod " + Quote(file.modName) + " sets " + label + " of " + EntryText(t, entry) +
                                           " twice (" + it->where + " and " + where + "); the second is used.");
        }
        it->bits &= static_cast<uint8_t>(~bits);
        it = it->bits == 0 ? owners.erase(it) : it + 1;
      }
      owners.push_back({file.mod, where, bits});
    }
    mModNames[file.mod] = file.modName;

    uint32_t before = 0;
    bool first = true;
    for (const DataPlace& p : t.places) {
      Work& w = WorkFor(p);
      uint8_t* e = w.now.data() + p.offset + static_cast<size_t>(entry) * t.entrySize;
      if (first) {
        before = ReadDataField(w.before.data() + p.offset + static_cast<size_t>(entry) * t.entrySize, f, element);
        first = false;
      }
      uint32_t raw = 0;
      for (uint32_t b = 0; b < f.size; b++) {
        raw = raw << 8 | e[at + b];
      }
      raw = (raw & ~mask) | ((f.bitWidth != 0 ? value << f.bitShift : value) & mask);
      for (uint32_t b = 0; b < f.size; b++) {
        e[at + b] = static_cast<uint8_t>(raw >> (8 * (f.size - 1 - b)));
      }
    }
    DataChange& c = mChanges[{t.name, entry, at, f.bitShift}];
    if (c.mod.empty()) {
      c.table = t.name;
      c.entry = entry;
      c.entryName = NameOf(t.entryNames, entry);
      c.offset = at;
      c.size = f.size;
      c.bitShift = f.bitShift;
      c.bitWidth = f.bitWidth;
      c.before = before;
    }
    c.field = label;
    c.after = value;
    c.mod = file.mod;
    c.where = where;
  }

  std::map<std::string, std::string> mModNames;
  std::string ModName(const std::string& id) const {
    const auto it = mModNames.find(id);
    return it != mModNames.end() ? it->second : id;
  }
};

}

uint32_t DataField::MaxValue() const {
  if (bitWidth != 0) {
    return bitWidth >= 32 ? 0xFFFFFFFFu : (1u << bitWidth) - 1;
  }
  return size >= 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1;
}

uint32_t ReadDataField(const uint8_t* entry, const DataField& f, uint32_t element) {
  const uint8_t* p = entry + f.offset + element * f.Stride();
  uint32_t raw = 0;
  for (uint32_t b = 0; b < f.size; b++) {
    raw = raw << 8 | p[b];
  }
  return f.bitWidth != 0 ? (raw >> f.bitShift) & f.MaxValue() : raw;
}

std::string NameKey(std::string_view name) {
  std::string out;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || u >= 0x80) {
      out.push_back(c);
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    }
  }
  return out;
}

const DataTable* DataCatalog::FindTable(std::string_view name) const {
  const std::string lower = Lower(name);
  for (const DataTable& t : tables) {
    if (Lower(t.name) == lower) {
      return &t;
    }
  }
  for (const DataTable& t : tables) {
    for (const std::string& a : t.aliases) {
      if (Lower(a) == lower) {
        return &t;
      }
    }
  }
  return nullptr;
}

const NameList* DataCatalog::FindList(std::string_view name) const {
  for (const NameList& l : lists) {
    if (l.name == name) {
      return &l;
    }
  }
  return nullptr;
}

DataApplyResult ApplyDataMods(const DataCatalog& catalog, const std::vector<DataModFile>& files, const EntryReader& read,
                              std::vector<Message>& messages) {
  Applier a(catalog, read, messages);
  for (const DataModFile& f : files) {
    a.File(f);
  }
  return a.Finish();
}

}
