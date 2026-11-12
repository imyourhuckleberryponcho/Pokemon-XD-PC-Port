#include "settings.h"

#include <set>

namespace engine::settings {

namespace {

std::string_view SectionOf(std::string_view id) {
  const size_t dot = id.find('.');
  return dot == std::string_view::npos ? std::string_view() : id.substr(0, dot);
}

std::string_view KeyOf(std::string_view id) {
  const size_t dot = id.find('.');
  return dot == std::string_view::npos ? id : id.substr(dot + 1);
}

bool StartsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

std::string FirstOfNumbered(std::string_view section) {
  if (section.size() < 2 || section.back() < '2' || section.back() > '9') {
    return {};
  }
  const std::string_view base = section.substr(0, section.size() - 1);
  if (base.back() >= '0' && base.back() <= '9') {
    return {};
  }
  return std::string(base) + "1";
}

std::string Cell(std::string s) {
  std::string out;
  for (char ch : s) {
    if (ch == '|') {
      out += "\\|";
    } else if (ch == '\n' || ch == '\r') {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

std::string Shown(const Registry& r, size_t index, const Value& v) {
  const Definition& d = r.Get(index);
  switch (d.type) {
  case Type::Bool: return v.i ? "on" : "off";
  case Type::Enum:
    if (v.i >= 0 && v.i < static_cast<int32_t>(d.choices.size())) {
      const Choice& c = d.choices[static_cast<size_t>(v.i)];
      return c.name + " (`" + c.id + "`)";
    }
    return "?";
  case Type::Text: return v.s.empty() ? "(empty)" : "`" + v.s + "`";
  case Type::Int:
    if (const std::string name = d.IntName(v.i); !name.empty()) {
      return name + " (`" + std::to_string(v.i) + "`)";
    }
    return r.TextOf(index, v);
  default: return r.TextOf(index, v);
  }
}

std::string Allowed(const Registry& r, size_t index) {
  const Definition& d = r.Get(index);
  switch (d.type) {
  case Type::Bool: return "on / off";
  case Type::Int: {
    if (d.intNames.empty()) {
      return std::to_string(d.minInt) + " to " + std::to_string(d.maxInt);
    }
    std::string s;
    for (const auto& [value, name] : d.intNames) {
      s += (s.empty() ? "" : ", ") + std::string("`") + std::to_string(value) + "` " + name;
    }
    return s;
  }
  case Type::Float: {
    Value lo, hi;
    lo.f = d.minFloat;
    hi.f = d.maxFloat;
    return r.TextOf(index, lo) + " to " + r.TextOf(index, hi);
  }
  case Type::Enum: {
    std::string s;
    for (size_t c = 0; c < d.choices.size(); c++) {
      s += (c ? ", " : "") + std::string("`") + d.choices[c].id + "`";
    }
    return s;
  }
  case Type::Text: return "text";
  }
  return {};
}

}

std::vector<std::string> CheckRules(const Registry& r) {
  std::vector<std::string> broken;
  for (size_t i = 0; i < r.Count(); i++) {
    const Definition& d = r.Get(i);
    const std::string who = d.id + ": ";
    if (d.name.empty()) {
      broken.push_back(who + "has no name");
    }
    if (d.description.empty() || d.description.back() != '.') {
      broken.push_back(who + "needs a description that is a full sentence (ending with '.')");
    }
    for (const auto& [value, name] : d.intNames) {
      if (d.type != Type::Int || value < d.minInt || value > d.maxInt || name.empty()) {
        broken.push_back(who + "a named value must be an Int in the setting's range, with a name");
        break;
      }
    }
    if (d.testing && !d.preference) {
      broken.push_back(who + "a testing setting must also be a preference");
    }
    if (d.preference) {
      const bool same = d.type == Type::Float  ? d.original.f == d.enhanced.f
                        : d.type == Type::Text ? d.original.s == d.enhanced.s
                                               : d.original.i == d.enhanced.i;
      if (!same) {
        broken.push_back(who + "a preference must have the same default in every preset");
      }
      continue;
    }
    const std::string_view key = KeyOf(d.id);
    switch (d.type) {
    case Type::Bool:
      if (StartsWith(key, "original_") || StartsWith(key, "keep_")) {
        if (d.original.i != 1) {
          broken.push_back(who + "\"" + std::string(key) + "\" keeps the GameCube way when on, so Original must be on");
        }
      } else if (d.original.i != 0) {
        broken.push_back(who + "an enhancement must be off in Original (or be named original_... / keep_... "
                               "when on is the GameCube way)");
      }
      break;
    case Type::Enum: {
      static const std::set<std::string> kOriginalNames{"original", "gamecube", "off", "native"};
      if (d.choices.empty() || kOriginalNames.count(d.choices[0].id) == 0) {
        broken.push_back(who + "the first choice must be the GameCube one, called original, gamecube, off or native");
      } else if (d.original.i != 0) {
        broken.push_back(who + "Original must be the first choice (" + d.choices[0].id + ")");
      }
      break;
    }
    case Type::Text: broken.push_back(who + "a text setting must be a preference"); break;
    case Type::Int:
    case Type::Float: break;
    }
  }
  return broken;
}

std::string SettingsMarkdown(const Registry& r, std::string_view title, std::string_view intro) {
  std::string out = "# " + std::string(title) + "\n\n" + std::string(intro);
  if (!intro.empty() && intro.back() != '\n') {
    out += "\n";
  }

  std::set<std::string> sections;
  for (size_t i = 0; i < r.Count(); i++) {
    sections.insert(std::string(SectionOf(r.Get(i).id)));
  }
  const auto summed_up = [&](const Definition& d) {
    const std::string first = FirstOfNumbered(SectionOf(d.id));
    return !first.empty() && sections.count(first) != 0;
  };

  const auto table = [&](std::string& s, auto&& wanted) {
    s += "| Setting | What it does | Original | Enhanced | Works yet |\n|---|---|---|---|---|\n";
    for (size_t i = 0; i < r.Count(); i++) {
      const Definition& d = r.Get(i);
      if (!wanted(d) || summed_up(d)) {
        continue;
      }
      std::string what = d.description;
      if (!d.originalNote.empty()) {
        what += " GameCube: " + d.originalNote;
      }
      what += " Values: " + Allowed(r, i) + ".";
      if (!d.games.empty()) {
        std::string games;
        for (const std::string& g : d.games) {
          games += (games.empty() ? "" : ", ") + g;
        }
        what += " Only in: " + games + ".";
      }
      const std::string original = Shown(r, i, d.original);
      const std::string enhanced = d.preference ? "same" : Shown(r, i, d.enhanced);
      s += "| **" + Cell(d.name) + "**<br>`" + d.id + "` | " + Cell(what) + " | " + Cell(original) + " | " +
           Cell(enhanced) + " | " + (d.implemented ? "yes" : "not yet") + " |\n";
    }
  };

  for (Category c : {Category::Video, Category::System, Category::Gameplay, Category::Controls, Category::Audio,
                     Category::Accessibility}) {
    bool any = false;
    std::set<std::string> numbered;
    for (size_t i = 0; i < r.Count(); i++) {
      const Definition& d = r.Get(i);
      if (d.category == c && !d.testing) {
        any = true;
        if (summed_up(d)) {
          const std::string_view sec = SectionOf(d.id);
          numbered.insert(std::string(sec.substr(0, sec.size() - 1)));
        }
      }
    }
    if (!any) {
      continue;
    }
    out += "\n## " + std::string(CategoryName(c)) + "\n\n";
    table(out, [&](const Definition& d) { return d.category == c && !d.testing; });
    for (const std::string& base : numbered) {
      out += "\n`" + base + "2` to `" + base + "4`: the same settings as `" + base +
             "1`, for controllers 2 to 4 (same defaults).\n";
    }
  }

  bool anyTesting = false;
  for (size_t i = 0; i < r.Count(); i++) {
    anyTesting |= r.Get(i).testing;
  }
  if (anyTesting) {
    out += "\n## For test runs only\n\nNever in settings.ini or a preset; set for one run (see below).\n\n";
    table(out, [](const Definition& d) { return d.testing; });
  }
  return out;
}

}
