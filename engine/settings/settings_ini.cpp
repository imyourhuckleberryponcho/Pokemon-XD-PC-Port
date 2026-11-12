#include "settings.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace engine::settings {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

std::string_view SectionOf(std::string_view id) {
  const size_t dot = id.find('.');
  return dot == std::string_view::npos ? std::string_view() : id.substr(0, dot);
}

std::string_view KeyOf(std::string_view id) {
  const size_t dot = id.find('.');
  return dot == std::string_view::npos ? id : id.substr(dot + 1);
}

std::string PathText(const std::filesystem::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string FloatText(float f) {
  char buf[32];
  const auto r = std::to_chars(buf, buf + sizeof(buf), f);
  return std::string(buf, r.ptr);
}

std::string Describe(const Definition& d) {
  std::string s = d.name + (d.implemented ? "" : " (does nothing yet)") + ": " + d.description;
  for (char& ch : s) {
    if (ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }
  switch (d.type) {
  case Type::Bool: s += " (on/off)"; break;
  case Type::Int:
    if (d.intNames.empty()) {
      s += " (" + std::to_string(d.minInt) + " to " + std::to_string(d.maxInt) + ")";
    } else {
      s += " (";
      for (size_t n = 0; n < d.intNames.size(); n++) {
        s += (n ? ", " : "") + std::to_string(d.intNames[n].first) + " = " + d.intNames[n].second;
      }
      s += ")";
    }
    break;
  case Type::Float: s += " (" + FloatText(d.minFloat) + " to " + FloatText(d.maxFloat) + ")"; break;
  case Type::Enum:
    s += " (";
    for (size_t c = 0; c < d.choices.size(); c++) {
      s += (c ? ", " : "") + d.choices[c].id;
    }
    s += ")";
    break;
  case Type::Text: break;
  }
  return s;
}

struct Line {
  std::string id;
  std::string value;
  int number;
};

void Migrate(int from, std::vector<Line>& values) {
  for (int version = from; version < Registry::kFormatVersion; version++) {
    switch (version) {
    case 0: break;
    default: break;
    }
  }
  (void)values;
}

}

void Registry::ParseFile(std::string_view text) {
  if (text.substr(0, 3) == "\xEF\xBB\xBF") {
    text.remove_prefix(3);
  }

  std::vector<Line> values;
  std::string section;
  bool badSection = false;
  Preset preset = Preset::Original, base = Preset::Original;
  bool baseGiven = false;
  int format = 0;

  int number = 0;
  while (!text.empty()) {
    const size_t end = text.find('\n');
    const std::string_view line = Trim(text.substr(0, end));
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
    number++;
    const std::string where = "settings.ini line " + std::to_string(number) + ": ";

    if (line.empty() || line.front() == ';' || line.front() == '#') {
      continue;
    }
    if (line.front() == '[') {
      if (line.back() != ']' || Trim(line.substr(1, line.size() - 2)).empty()) {
        Warn(where + "\"" + std::string(line) + "\" is not a valid [section]; the lines under it are ignored");
        badSection = true;
      } else {
        section = Trim(line.substr(1, line.size() - 2));
        badSection = false;
      }
      continue;
    }
    const size_t eq = line.find('=');
    const std::string_view key = eq == std::string_view::npos ? std::string_view() : Trim(line.substr(0, eq));
    if (key.empty()) {
      Warn(where + "\"" + std::string(line) + "\" is not \"name = value\"; ignored");
      continue;
    }
    if (badSection) {
      continue;
    }
    const std::string_view value = Trim(line.substr(eq + 1));

    if (section.empty() && key == "format") {
      const auto r = std::from_chars(value.data(), value.data() + value.size(), format);
      if (value.empty() || r.ec != std::errc() || r.ptr != value.data() + value.size() || format < 1) {
        Warn(where + "format must be a number (" + std::to_string(kFormatVersion) + "); read as the current one");
        format = kFormatVersion;
      }
    } else if (section.empty() && key == "preset") {
      if (!ParsePreset(value, preset)) {
        Warn(where + "unknown preset \"" + std::string(value) + "\"; using original");
        preset = Preset::Original;
      }
    } else if (section.empty() && key == "based_on") {
      if (!ParsePreset(value, base) || base == Preset::Custom) {
        Warn(where + "based_on must be original or enhanced; using original");
        base = Preset::Original;
      }
      baseGiven = true;
    } else {
      std::string id = section.empty() ? std::string(key) : section + "." + std::string(key);
      for (const Line& l : values) {
        if (l.id == id) {
          Warn(where + "\"" + id + "\" appears twice; the last one counts");
        }
      }
      values.push_back(Line{std::move(id), std::string(value), number});
    }
  }

  report_.format = format;
  if (format < kFormatVersion) {
    Migrate(format, values);
    report_.migrated = true;
  } else if (format > kFormatVersion) {
    report_.newerFormat = true;
    Warn("settings.ini was written by a newer version of the port (format " + std::to_string(format) +
         ", this one knows " + std::to_string(kFormatVersion) + "); read as far as possible, and kept as "
         "settings.ini.format" + std::to_string(format) + ".bak before it is replaced");
  }

  if (preset != Preset::Custom) {
    if (baseGiven && base != preset) {
      Warn("settings.ini: based_on is only used with preset = custom; ignored");
    }
    base = preset;
  }
  preset_ = preset;
  base_ = base;
  unknown_.clear();
  for (size_t i = 0; i < entries_.size(); i++) {
    const Value def = DefaultOf(i, preset_);
    if (!SameValue(entries_[i].def, entries_[i].own, def)) {
      StoreOwn(i, def);
    }
  }
  for (const Line& l : values) {
    size_t index = Find(l.id);
    if (index == kNoSetting) {
      index = FindFormer(l.id);
      if (index != kNoSetting) {
        report_.renamed.emplace_back(l.id, entries_[index].def.id);
      }
    }
    if (index == kNoSetting) {
      unknown_[l.id] = l.value;
      continue;
    }
    const Definition& def = entries_[index].def;
    if (def.testing) {
      Warn("settings.ini line " + std::to_string(l.number) + ": " + def.id +
           " can only be set for one run (not in settings.ini); ignored");
      continue;
    }
    Value v;
    if (!ParseValue(def, l.value, v)) {
      Warn("settings.ini line " + std::to_string(l.number) + ": \"" + l.value + "\" is not a valid value for " +
           def.id + "; using the default (" + TextOf(index, entries_[index].own) + ")");
      continue;
    }
    Set(index, v);
  }
  Touched();
}

std::string Registry::WriteFile() const {
  std::string out;
  out += "; Game settings. Lines starting with ; are comments.\n";
  out += "; preset = original (exactly like the GameCube), enhanced (recommended improvements) or custom.\n";
  out += "; With original or enhanced the values below come from the preset and are shown commented out;\n";
  out += "; remove the ; in front of one to change it, which makes the preset custom.\n";
  out += "; format: the version of this file's layout (leave it as it is).\n\n";
  out += "format = " + std::to_string(kFormatVersion) + "\n";
  out += std::string("preset = ") + PresetId(preset_) + "\n";
  if (preset_ == Preset::Custom) {
    out += std::string("based_on = ") + PresetId(base_) + "\n";
  }
  for (const auto& [id, value] : unknown_) {
    if (SectionOf(id).empty()) {
      out += id + " = " + value + "\n";
    }
  }

  std::vector<std::string> sections;
  std::set<std::string> seen;
  for (const Entry& e : entries_) {
    const std::string s(SectionOf(e.def.id));
    if (!e.def.testing && seen.insert(s).second) {
      sections.push_back(s);
    }
  }
  for (const auto& [id, value] : unknown_) {
    const std::string s(SectionOf(id));
    if (!s.empty() && seen.insert(s).second) {
      sections.push_back(s);
    }
  }

  for (const std::string& section : sections) {
    out += "\n[" + section + "]\n";
    for (size_t i = 0; i < entries_.size(); i++) {
      const Definition& d = entries_[i].def;
      if (d.testing || SectionOf(d.id) != section) {
        continue;
      }
      const bool real = d.preference || preset_ == Preset::Custom;
      out += "; " + Describe(d) + "\n";
      out += std::string(real ? "" : "; ") + std::string(KeyOf(d.id)) + " = " + TextOf(i, entries_[i].own) + "\n";
    }
    for (const auto& [id, value] : unknown_) {
      if (SectionOf(id) == section) {
        out += std::string(KeyOf(id)) + " = " + value + "\n";
      }
    }
  }
  return out;
}

std::string Registry::FileText() const { return WriteFile(); }

std::vector<std::string> Registry::UnknownIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, value] : unknown_) {
    ids.push_back(id);
  }
  return ids;
}

bool Registry::Load(const std::filesystem::path& folder) { return LoadFile(folder / kFileName); }

bool Registry::LoadFile(const std::filesystem::path& file) {
  report_ = LoadReport{};
  report_.path = file;
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return false;
  }
  report_.found = true;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  loading_ = true;
  ParseFile(text);
  loading_ = false;
  report_.unknownIds = UnknownIds();
  return true;
}

bool Registry::Save(const std::filesystem::path& folder) const { return SaveFile(folder / kFileName); }

bool Registry::SaveFile(const std::filesystem::path& path) const {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  if (report_.newerFormat && report_.path == path && std::filesystem::exists(path, ec)) {
    std::filesystem::path backup = path;
    backup += ".format" + std::to_string(report_.format) + ".bak";
    if (!std::filesystem::exists(backup, ec)) {
      std::filesystem::copy_file(path, backup, ec);
      if (ec) {
        Warn("could not keep a copy of " + PathText(path) + " (" + ec.message() + "); not replaced");
        return false;
      }
    }
  }
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const std::string text = WriteFile();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
      Warn("could not write " + PathText(temp));
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    Warn("could not replace " + PathText(path) + ": " + ec.message());
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

}
