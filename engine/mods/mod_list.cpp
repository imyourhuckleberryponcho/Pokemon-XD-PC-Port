#include "mods.h"

namespace engine::mods {

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

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return out;
}

bool ParseOnOff(std::string_view text, bool& out) {
  const std::string v = Lower(text);
  if (v == "on" || v == "true" || v == "yes" || v == "1") {
    out = true;
    return true;
  }
  if (v == "off" || v == "false" || v == "no" || v == "0") {
    out = false;
    return true;
  }
  return false;
}

constexpr const char* kHeader =
    "; Mods: which are on, and in which order (see the mods folder next to this file).\n"
    "; A newly found mod is added here switched off; change \"off\" to \"on\" to use it.\n"
    "; When two mods replace the same file, the one further down wins.\n"
    "; \"profile\" picks which list below is used; add more [profile <name>] lists to switch quickly.\n";

}

ModList::Profile& ModList::Active() {
  for (Profile& p : profiles) {
    if (p.name == active) {
      return p;
    }
  }
  profiles.push_back({active, {}});
  return profiles.back();
}

ModList ParseModList(std::string_view text, std::vector<std::string>& warnings) {
  ModList list;
  list.active.clear();
  ModList::Profile* section = nullptr;
  size_t number = 0;
  if (text.substr(0, 3) == "\xEF\xBB\xBF") {
    text.remove_prefix(3);
  }
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    const std::string_view line = Trim(text.substr(0, nl));
    text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    number++;
    const std::string where = std::string(kListFile) + " line " + std::to_string(number) + ": ";
    if (line.empty() || line.front() == ';' || line.front() == '#') {
      continue;
    }
    if (line.front() == '[') {
      if (line.back() != ']') {
        warnings.push_back(where + "a section name needs a closing ']'");
        section = nullptr;
        continue;
      }
      std::string_view name = Trim(line.substr(1, line.size() - 2));
      if (Lower(name.substr(0, 8)) == "profile ") {
        name = Trim(name.substr(8));
      }
      if (name.empty()) {
        warnings.push_back(where + "a profile needs a name, like [profile default]");
        section = nullptr;
        continue;
      }
      section = nullptr;
      for (ModList::Profile& p : list.profiles) {
        if (p.name == name) {
          section = &p;
        }
      }
      if (section == nullptr) {
        list.profiles.push_back({std::string(name), {}});
        section = &list.profiles.back();
      }
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      warnings.push_back(where + "expected \"<mod id> = on\" or \"= off\"");
      continue;
    }
    const std::string_view key = Trim(line.substr(0, eq));
    const std::string_view value = Trim(line.substr(eq + 1));
    if (section == nullptr) {
      if (Lower(key) == "profile" && !value.empty()) {
        list.active = std::string(value);
      } else {
        warnings.push_back(where + "\"" + std::string(key) + "\" outside a [profile ...] list; ignored");
      }
      continue;
    }
    bool on = false;
    if (!ValidModId(key) || !ParseOnOff(value, on)) {
      warnings.push_back(where + "expected \"<mod id> = on\" or \"= off\"; ignored");
      continue;
    }
    bool seen = false;
    for (const auto& m : section->mods) {
      seen |= m.first == key;
    }
    if (seen) {
      warnings.push_back(where + "\"" + std::string(key) + "\" is listed twice; the first line counts");
      continue;
    }
    section->mods.emplace_back(std::string(key), on);
  }
  if (list.active.empty()) {
    list.active = list.profiles.empty() ? "default" : list.profiles.front().name;
  }
  return list;
}

std::string FormatModList(const ModList& list) {
  std::string out = kHeader;
  out += "\nprofile = " + list.active + "\n";
  for (const ModList::Profile& p : list.profiles) {
    out += "\n[profile " + p.name + "]\n";
    for (const auto& [id, on] : p.mods) {
      out += id + (on ? " = on\n" : " = off\n");
    }
  }
  return out;
}

}
