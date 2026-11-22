#include "labels.h"

#include <algorithm>
#include <set>

namespace engine::mods {

namespace {

bool SlugChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }
bool Alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

char Unaccent(uint32_t cp) {
  if (cp >= 0xC0 && cp <= 0xC5) return 'a';
  if (cp == 0xC7) return 'c';
  if (cp >= 0xC8 && cp <= 0xCB) return 'e';
  if (cp >= 0xCC && cp <= 0xCF) return 'i';
  if (cp == 0xD1) return 'n';
  if ((cp >= 0xD2 && cp <= 0xD6) || cp == 0xD8) return 'o';
  if (cp >= 0xD9 && cp <= 0xDC) return 'u';
  if (cp == 0xDD) return 'y';
  if (cp >= 0xE0 && cp <= 0xE5) return 'a';
  if (cp == 0xE7) return 'c';
  if (cp >= 0xE8 && cp <= 0xEB) return 'e';
  if (cp >= 0xEC && cp <= 0xEF) return 'i';
  if (cp == 0xF1) return 'n';
  if ((cp >= 0xF2 && cp <= 0xF6) || cp == 0xF8) return 'o';
  if (cp >= 0xF9 && cp <= 0xFC) return 'u';
  if (cp == 0xFD || cp == 0xFF) return 'y';
  return 0;
}

}

bool ValidKind(std::string_view kind) {
  if (kind.empty() || kind.size() > 16) return false;
  return std::all_of(kind.begin(), kind.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

bool ValidSlug(std::string_view slug) {
  if (slug.empty() || slug.size() > 48) return false;
  if (!Alnum(slug.front())) return false;
  return std::all_of(slug.begin(), slug.end(), SlugChar);
}

bool ValidNamespace(std::string_view ns) {
  if (ns.empty() || ns.size() > 40) return false;
  const size_t dot = ns.find('.');
  if (dot == std::string_view::npos) return ValidSlug(ns);
  if (ns.find('.', dot + 1) != std::string_view::npos) return false;
  return ValidSlug(ns.substr(0, dot)) && ValidSlug(ns.substr(dot + 1));
}

bool LooksLikeLabel(std::string_view text) {
  const size_t colon = text.find(':');
  const size_t slash = text.find('/');
  return colon != std::string_view::npos && slash != std::string_view::npos && colon > 0 && slash > colon;
}

bool ParseLabel(std::string_view text, Label& out, std::string& why) {
  out = Label{};
  if (text.size() > kMaxLabelLength) {
    why = "a label is at most 96 characters";
    return false;
  }
  const size_t colon = text.find(':');
  const size_t slash = text.find('/', colon == std::string_view::npos ? 0 : colon);
  if (colon == std::string_view::npos || slash == std::string_view::npos) {
    why = "a label looks like kind:namespace/name, for example species:base/turtwig";
    return false;
  }
  Label l;
  l.kind = std::string(text.substr(0, colon));
  l.ns = std::string(text.substr(colon + 1, slash - colon - 1));
  l.slug = std::string(text.substr(slash + 1));
  if (!ValidKind(l.kind)) {
    why = "the kind of a label is lower case letters (species, move, item)";
  } else if (!ValidNamespace(l.ns)) {
    why = "the namespace of a label is base, or <author>.<pack>: lower case letters, digits, - and _ (one dot between author and pack)";
  } else if (!ValidSlug(l.slug)) {
    why = "the name part of a label is lower case letters, digits, - and _ (it starts with a letter or digit)";
  } else {
    out = std::move(l);
    return true;
  }
  return false;
}

std::string Slugify(std::string_view name) {
  std::string out;
  const auto sep = [&] {
    if (!out.empty() && out.back() != '-') out += '-';
  };
  for (size_t i = 0; i < name.size();) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x80) {
      i++;
      if (c >= 'A' && c <= 'Z') {
        out += static_cast<char>(c - 'A' + 'a');
      } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        out += static_cast<char>(c);
      } else if (c == '\'' || c == '.' || c == '`') {
      } else {
        sep();
      }
      continue;
    }
    uint32_t cp = 0;
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    for (size_t k = 1; k < len && i + k < name.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(name[i + k]) & 0x3F);
    i += len;
    if (cp == 0x2640) {
      sep();
      out += 'f';
    } else if (cp == 0x2642) {
      sep();
      out += 'm';
    } else if (cp == 0x2019 || cp == 0x2018) {
    } else if (const char a = Unaccent(cp)) {
      out += a;
    } else {
      sep();
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.size() > 48) {
    out.resize(48);
    while (!out.empty() && out.back() == '-') out.pop_back();
  }
  return out;
}

BaseLabels::BaseLabels(std::string kind, const std::vector<std::pair<std::string, uint32_t>>& names) : mKind(std::move(kind)) {
  std::map<std::string, std::vector<uint32_t>> bySlug;
  for (const auto& [name, number] : names) {
    const std::string slug = Slugify(name);
    if (slug.empty()) continue;
    auto& v = bySlug[slug];
    if (std::find(v.begin(), v.end(), number) == v.end()) v.push_back(number);
  }
  for (auto& [slug, numbers] : bySlug) {
    if (numbers.size() == 1) {
      mBySlug[slug] = numbers[0];
      mByNumber[numbers[0]] = slug;
    } else {
      for (uint32_t n : numbers) {
        const std::string s = slug + "-" + std::to_string(n);
        mBySlug[s] = n;
        mByNumber[n] = s;
      }
    }
  }
}

bool BaseLabels::Find(const std::string& slug, uint32_t& number) const {
  const auto it = mBySlug.find(slug);
  if (it == mBySlug.end()) return false;
  number = it->second;
  return true;
}

bool BaseLabels::SlugOf(uint32_t number, std::string& slug) const {
  const auto it = mByNumber.find(number);
  if (it == mByNumber.end()) return false;
  slug = it->second;
  return true;
}

std::string KindOf(const std::string& label) {
  Label l;
  std::string why;
  return ParseLabel(label, l, why) ? l.kind : std::string();
}

std::map<uint32_t, uint32_t> RemapPlan::Map(const std::string& kind) const {
  std::map<uint32_t, uint32_t> out;
  for (const RemapStep& s : steps) {
    if (s.kind == kind && s.from != s.to) out[s.from] = s.to;
  }
  return out;
}

RemapPlan PlanRemap(const std::vector<LabelRow>& saved, const std::vector<LabelRow>& now) {
  RemapPlan plan;
  std::map<std::string, const LabelRow*> byLabel, byLegacy;
  for (const LabelRow& n : now) {
    if (!n.label.empty()) byLabel[n.label] = &n;
    if (!n.legacy.empty()) byLegacy[n.legacy] = &n;
  }
  std::set<std::string> said;
  const auto problem = [&](const std::string& text) {
    if (said.insert(text).second) plan.problems.push_back(text);
  };
  std::map<std::pair<std::string, uint32_t>, std::string> claimed;
  for (const LabelRow& s : saved) {
    const LabelRow* there = nullptr;
    std::string label = s.label;
    if (!label.empty()) {
      const auto it = byLabel.find(label);
      if (it != byLabel.end()) there = it->second;
    } else if (!s.legacy.empty()) {
      const auto it = byLegacy.find(s.legacy);
      if (it != byLegacy.end()) {
        there = it->second;
        label = there->label;
      }
    }
    if (there == nullptr) {
      bool same = false;
      for (const LabelRow& n : now) {
        if (s.label.empty() && n.label.empty() && !s.legacy.empty() && s.legacy == n.legacy && s.number == n.number) same = true;
      }
      if (!same) problem(s.describe.empty() ? s.label : s.describe);
      continue;
    }
    if (label.empty()) {
      if (there->number != s.number) problem(s.describe + " (it has another number now)");
      continue;
    }
    const std::string kind = KindOf(label);
    if (kind.empty()) {
      problem(s.describe.empty() ? label : s.describe);
      continue;
    }
    const auto key = std::make_pair(kind, s.number);
    const auto c = claimed.find(key);
    if (c != claimed.end() && c->second != label) {
      problem("two rows of the save (" + c->second + " and " + label + ") have the same number " + std::to_string(s.number));
      continue;
    }
    claimed[key] = label;
    if (there->number != s.number) plan.steps.push_back({kind, label, s.number, there->number});
  }
  return plan;
}

}
