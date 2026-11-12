#include "save_slots.h"

#include "gc_memcard.h"
#include "json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace engine::memcard::slots {
namespace {

using engine::mods::json::Reader;
using engine::mods::json::Value;

std::string Utf8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

fs::path PathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

fs::path SavesFolder(const fs::path& dataFolder) { return dataFolder / PathOf("saves"); }

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char esc[8];
      std::snprintf(esc, sizeof esc, "\\u%04x", c);
      out += esc;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + "\"";
}

unsigned NumberOf(const std::string& id) {
  if (id.size() < 2 || id.size() > 9 || id[0] != 's') return 0;
  unsigned n = 0;
  for (size_t i = 1; i < id.size(); i++) {
    if (id[i] < '0' || id[i] > '9') return 0;
    n = n * 10 + static_cast<unsigned>(id[i] - '0');
  }
  return id[1] == '0' ? 0 : n;
}

std::vector<std::string> FoldersFound(const fs::path& dataFolder) {
  std::vector<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(SavesFolder(dataFolder), ec)) {
    const std::string name = Utf8(e.path().filename());
    if (e.is_directory(ec) && NumberOf(name) != 0) out.push_back(name);
  }
  std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return NumberOf(a) < NumberOf(b); });
  return out;
}

std::string NewId(const fs::path& dataFolder, const Index& index) {
  unsigned top = 1;
  for (const Slot& s : index.slots) top = std::max(top, NumberOf(s.id));
  for (const std::string& f : FoldersFound(dataFolder)) top = std::max(top, NumberOf(f));
  return "s" + std::to_string(top + 1);
}

bool ReadText(const fs::path& file, std::string& out) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return !in.bad();
}

void Mend(Index& index) {
  const auto main = std::find_if(index.slots.begin(), index.slots.end(), [](const Slot& s) { return s.id == kMainId; });
  if (main == index.slots.end()) {
    index.slots.insert(index.slots.begin(), Slot{kMainId, kMainName});
  } else if (main != index.slots.begin()) {
    std::rotate(index.slots.begin(), main, main + 1);
  }
  if (index.Find(index.active) == nullptr) index.active = kMainId;
}

bool Parse(const std::string& text, Index& out, std::string& why) {
  Value root;
  Reader reader(text);
  if (!reader.Document(root)) {
    why = reader.Error();
    return false;
  }
  if (root.kind != Value::Kind::Object) {
    why = "not a list of saves";
    return false;
  }
  const auto slots = root.fields.find("slots");
  if (slots == root.fields.end() || slots->second.kind != Value::Kind::Array) {
    why = "no \"slots\" list";
    return false;
  }
  for (const Value& v : slots->second.items) {
    if (v.kind != Value::Kind::Object) continue;
    const auto id = v.fields.find("id");
    const auto name = v.fields.find("name");
    if (id == v.fields.end() || id->second.kind != Value::Kind::String) continue;
    const std::string& i = id->second.text;
    if ((i != kMainId && NumberOf(i) == 0) || out.Find(i) != nullptr) continue;
    std::string n = name != v.fields.end() && name->second.kind == Value::Kind::String ? CleanName(name->second.text) : "";
    if (n.empty()) n = i == kMainId ? kMainName : "Save (" + i + ")";
    out.slots.push_back(Slot{i, n});
  }
  const auto active = root.fields.find("active");
  if (active != root.fields.end() && active->second.kind == Value::Kind::String) out.active = active->second.text;
  return true;
}

bool CopyTree(const fs::path& from, const fs::path& to, std::string& error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::copy(from, to, fs::copy_options::recursive, ec);
  if (ec) {
    error = "Can't copy " + Utf8(from) + " (" + ec.message() + ").";
    return false;
  }
  return true;
}

}

const Slot* Index::Find(const std::string& id) const {
  for (const Slot& s : slots) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

const Slot& Index::Active() const {
  static const Slot kMain{kMainId, kMainName};
  if (const Slot* s = Find(active)) return *s;
  if (const Slot* s = Find(kMainId)) return *s;
  return kMain;
}

fs::path IndexFile(const fs::path& dataFolder) { return SavesFolder(dataFolder) / PathOf("slots.json"); }

fs::path RootOf(const fs::path& dataFolder, const std::string& id) {
  return id == kMainId || NumberOf(id) == 0 ? dataFolder : SavesFolder(dataFolder) / PathOf(id);
}

Index Load(const fs::path& dataFolder, std::string* note) {
  Index index;
  const fs::path file = IndexFile(dataFolder);
  std::error_code ec;
  std::string text, why;
  if (!fs::exists(file, ec)) {
  } else if (!ReadText(file, text) || !Parse(text, index, why)) {
    index = Index();
    for (const std::string& id : FoldersFound(dataFolder)) {
      index.slots.push_back(Slot{id, "Save " + id.substr(1) + " (found again)"});
    }
    if (note != nullptr) {
      *note = "the list of saves can't be read (" + (why.empty() ? std::string("can't open it") : why) + "): " +
              Utf8(file) + "; every save folder found is listed";
    }
  }
  Mend(index);
  return index;
}

bool Save(const fs::path& dataFolder, const Index& index, std::string& error) {
  const fs::path file = IndexFile(dataFolder);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  if (fs::exists(file, ec)) {
    std::string text, why;
    Index was;
    if (!ReadText(file, text) || !Parse(text, was, why)) {
      fs::path kept = file;
      kept += PathOf(".damaged");
      fs::copy_file(file, kept, fs::copy_options::skip_existing, ec);
    }
  }
  std::string text = "{\n  \"version\": 1,\n  \"active\": " + Quote(index.active) + ",\n  \"slots\": [";
  for (size_t i = 0; i < index.slots.size(); i++) {
    text += std::string(i > 0 ? "," : "") + "\n    {\"id\": " + Quote(index.slots[i].id) + ", \"name\": " +
            Quote(index.slots[i].name) + "}";
  }
  text += "\n  ]\n}\n";
  fs::path temp = file;
  temp += PathOf(".tmp");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
      fs::remove(temp, ec);
      error = "Can't write the list of saves: " + Utf8(file);
      return false;
    }
  }
  fs::rename(temp, file, ec);
  if (ec) {
    error = "Can't write the list of saves (" + ec.message() + "): " + Utf8(file);
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

fs::path ActiveRoot(const fs::path& dataFolder, std::string* name) {
  const Index index = Load(dataFolder);
  const Slot& active = index.Active();
  if (name != nullptr) *name = active.name;
  return RootOf(dataFolder, active.id);
}

std::string CleanName(const std::string& name) {
  std::string out;
  for (unsigned char c : name) {
    if (c >= 0x20 && c != 0x7F) out += static_cast<char>(c);
  }
  while (!out.empty() && out.front() == ' ') out.erase(out.begin());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (out.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
    out.resize(cut);
    while (!out.empty() && out.back() == ' ') out.pop_back();
  }
  return out;
}

std::string NextName(const Index& index) {
  for (size_t n = index.slots.size() + 1;; n++) {
    const std::string name = "Save " + std::to_string(n);
    if (std::none_of(index.slots.begin(), index.slots.end(), [&](const Slot& s) { return s.name == name; })) {
      return name;
    }
  }
}

bool SetActive(const fs::path& dataFolder, const std::string& id, std::string& error) {
  Index index = Load(dataFolder);
  if (index.Find(id) == nullptr) {
    error = "There is no such save.";
    return false;
  }
  if (index.active == id) return true;
  index.active = id;
  return Save(dataFolder, index, error);
}

bool Create(const fs::path& dataFolder, const std::string& name, std::string& newId, std::string& error) {
  const std::string clean = CleanName(name);
  if (clean.empty()) {
    error = "The save needs a name.";
    return false;
  }
  Index index = Load(dataFolder);
  const std::string id = NewId(dataFolder, index);
  std::error_code ec;
  fs::create_directories(RootOf(dataFolder, id), ec);
  if (ec) {
    error = "Can't make the save's folder (" + ec.message() + "): " + Utf8(RootOf(dataFolder, id));
    return false;
  }
  index.slots.push_back(Slot{id, clean});
  if (!Save(dataFolder, index, error)) {
    fs::remove(RootOf(dataFolder, id), ec);
    return false;
  }
  newId = id;
  return true;
}

bool Rename(const fs::path& dataFolder, const std::string& id, const std::string& name, std::string& error) {
  const std::string clean = CleanName(name);
  if (clean.empty()) {
    error = "The save needs a name.";
    return false;
  }
  Index index = Load(dataFolder);
  for (Slot& s : index.slots) {
    if (s.id == id) {
      s.name = clean;
      return Save(dataFolder, index, error);
    }
  }
  error = "There is no such save.";
  return false;
}

std::vector<fs::path> RegionFolders(const fs::path& cardRoot) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(cardRoot, ec)) {
    const std::string name = Utf8(e.path().filename());
    if (name == "save_backups" || name == "playthroughs" || name == "saves" || !e.is_directory(ec)) continue;
    if (fs::is_directory(e.path() / PathOf("Card A"), ec) || fs::is_directory(e.path() / PathOf("Card B"), ec)) {
      out.push_back(e.path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool Duplicate(const fs::path& dataFolder, const std::string& id, std::string& newId, std::string& error) {
  Index index = Load(dataFolder);
  const Slot* from = index.Find(id);
  if (from == nullptr) {
    error = "There is no such save.";
    return false;
  }
  std::string name = CleanName(from->name + " (copy)");
  if (name.size() < 7 || name.compare(name.size() - 7, 7, " (copy)") != 0) {
    name = CleanName(from->name.substr(0, kMaxNameBytes - 10)) + " (copy)";
  }
  const std::string made = NewId(dataFolder, index);
  const fs::path source = RootOf(dataFolder, id), target = RootOf(dataFolder, made);
  std::error_code ec;
  fs::create_directories(target, ec);
  if (ec) {
    error = "Can't make the save's folder (" + ec.message() + "): " + Utf8(target);
    return false;
  }
  bool ok = true;
  for (const fs::path& region : RegionFolders(source)) {
    ok = ok && CopyTree(region, target / region.filename(), error);
  }
  if (ok && fs::is_directory(source / PathOf("playthroughs"), ec)) {
    ok = CopyTree(source / PathOf("playthroughs"), target / PathOf("playthroughs"), error);
  }
  if (ok) {
    index.slots.push_back(Slot{made, name});
    ok = Save(dataFolder, index, error);
  }
  if (!ok) {
    fs::remove_all(target, ec);
    return false;
  }
  newId = made;
  return true;
}

bool Delete(const fs::path& dataFolder, const std::string& id, const Bin& bin, std::string& error) {
  Index index = Load(dataFolder);
  if (index.Find(id) == nullptr) {
    error = "There is no such save.";
    return false;
  }
  std::error_code ec;
  if (id == kMainId) {
    std::vector<fs::path> gone = RegionFolders(dataFolder);
    for (const char* name : {"playthroughs", "save_backups"}) {
      if (fs::exists(dataFolder / PathOf(name), ec)) gone.push_back(dataFolder / PathOf(name));
    }
    for (const fs::path& folder : gone) {
      if (!bin || !bin(folder, error)) {
        if (error.empty()) error = "Can't delete " + Utf8(folder) + ".";
        return false;
      }
    }
    return true;
  }
  const fs::path root = RootOf(dataFolder, id);
  if (fs::exists(root, ec) && (!bin || !bin(root, error))) {
    if (error.empty()) error = "Can't delete " + Utf8(root) + ".";
    return false;
  }
  index.slots.erase(std::remove_if(index.slots.begin(), index.slots.end(), [&](const Slot& s) { return s.id == id; }),
                    index.slots.end());
  if (index.active == id) index.active = kMainId;
  return Save(dataFolder, index, error);
}

bool Import(const fs::path& dataFolder, const fs::path& file, const std::string& name, const fs::path& cardFolder,
            const std::string& cardFileName, std::string& newId, std::string& error) {
  std::vector<uint8_t> bytes;
  {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      error = "The file can't be read: " + Utf8(file);
      return false;
    }
    bytes.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  CardFile card;
  std::string why;
  if (!ReadGci(bytes, card, why)) {
    error = "This is not a memory card save file (.gci): " + why + ".";
    return false;
  }
  if (cardFolder.empty() || cardFolder.is_absolute() || cardFileName.empty()) {
    error = "Where the save goes on the memory card isn't known.";
    return false;
  }
  std::string id;
  if (!Create(dataFolder, name, id, error)) return false;
  const fs::path folder = RootOf(dataFolder, id) / cardFolder;
  std::error_code ec;
  fs::create_directories(folder, ec);
  bool ok = !ec;
  if (ok) {
    std::ofstream out(folder / PathOf(cardFileName), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    ok = static_cast<bool>(out);
  }
  if (!ok) {
    Index index = Load(dataFolder);
    index.slots.erase(std::remove_if(index.slots.begin(), index.slots.end(), [&](const Slot& s) { return s.id == id; }),
                      index.slots.end());
    std::string ignored;
    Save(dataFolder, index, ignored);
    fs::remove_all(RootOf(dataFolder, id), ec);
    error = "Can't write the save into its folder: " + Utf8(folder);
    return false;
  }
  newId = id;
  return true;
}

bool Export(const fs::path& cardFile, const fs::path& to, std::string& error) {
  std::error_code ec;
  if (cardFile.empty() || !fs::is_regular_file(cardFile, ec)) {
    error = "This save has no save file yet: the game makes it the first time it saves.";
    return false;
  }
  if (fs::equivalent(cardFile, to, ec)) {
    error = "That is the save's own file: choose another place.";
    return false;
  }
  fs::path temp = to;
  temp += PathOf(".tmp");
  fs::copy_file(cardFile, temp, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "Can't write " + Utf8(to) + " (" + ec.message() + ").";
    fs::remove(temp, ec);
    return false;
  }
  fs::rename(temp, to, ec);
  if (ec) {
    error = "Can't write " + Utf8(to) + " (" + ec.message() + ").";
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

}
