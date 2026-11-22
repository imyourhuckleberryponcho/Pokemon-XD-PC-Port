#include "pokedex.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace engine::pokedex {

Seen Status(const PlayerState& p, uint16_t index) {
  if (p.caught.count(index)) return Seen::Caught;
  if (p.seen.count(index)) return Seen::Seen;
  return Seen::Unseen;
}

bool TrainerLineVisible(const PlayerState& p, const WhereLine& l, const Settings& s) {
  if (s.showAll || s.showAllTrainers || l.battle == 0) return true;
  return p.battleFought && p.battleFought(l.battle);
}

std::string NumberText(const Species& s) {
  if (s.national == 0) return "No---";
  return "No" + std::to_string(static_cast<int>(s.national));
}

std::string RowName(const Species& s, Seen st, const Settings& set) {
  return (st == Seen::Unseen && !set.showAll) ? "-----" : s.name;
}
std::string EvolutionTargetName(const Species& s, Seen st, const Settings& set) {
  return (st == Seen::Unseen && !set.showAll) ? "?-----" : s.name;
}

namespace {
std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}

std::vector<uint16_t> Arrange(const std::vector<Species>& all, const PlayerState& p, const Settings& set, const Query& q,
                              const std::vector<uint16_t>& met) {
  std::vector<const Species*> v;
  for (const Species& s : all) {
    if (s.formOf != 0) continue;
    const Seen st = Status(p, s.index);
    const bool open = set.showAll || st != Seen::Unseen;
    switch (q.show) {
      case Show::All: break;
      case Show::AllSeen: if (!open) continue; break;
      case Show::Caught: if (st != Seen::Caught) continue; break;
      case Show::NotCaught: if (st == Seen::Caught) continue; break;
      case Show::Shadow: if (!s.shadow) continue; break;
      case Show::FromMods: if (!s.mod) continue; break;
    }
    if (q.generation != 0 && s.gen != q.generation) continue;
    if (q.typeId >= 0 && (!open || (s.types[0] != q.typeId && s.types[1] != q.typeId))) continue;
    if (!q.nameContains.empty() && (!open || Lower(s.name).find(Lower(q.nameContains)) == std::string::npos)) continue;
    v.push_back(&s);
  }
  auto metPos = [&](const Species* s) -> size_t {
    const auto it = std::find(met.begin(), met.end(), s->index);
    return it == met.end() ? met.size() + s->index : static_cast<size_t>(it - met.begin());
  };
  auto numberKey = [](const Species* s) { return s->national == 0 ? 100000u + s->index : static_cast<unsigned>(s->national); };
  std::stable_sort(v.begin(), v.end(), [&](const Species* a, const Species* b) {
    const bool ao = set.showAll || Status(p, a->index) != Seen::Unseen, bo = set.showAll || Status(p, b->index) != Seen::Unseen;
    switch (q.order) {
      case Order::National: break;
      case Order::AToZ:
        if (ao != bo) return ao;
        if (ao && a->name != b->name) return Lower(a->name) < Lower(b->name);
        break;
      case Order::Type:
        if (ao != bo) return ao;
        if (ao && a->types[0] != b->types[0]) return a->types[0] < b->types[0];
        break;
      case Order::Height:
        if (ao != bo) return ao;
        if (ao && a->heightDm != b->heightDm) return a->heightDm > b->heightDm;
        break;
      case Order::Weight:
        if (ao != bo) return ao;
        if (ao && a->weightHg != b->weightHg) return a->weightHg > b->weightHg;
        break;
      case Order::OrderMet:
        if (ao != bo) return ao;
        if (ao && metPos(a) != metPos(b)) return metPos(a) < metPos(b);
        break;
      case Order::CaughtFirst: {
        const bool ac = Status(p, a->index) == Seen::Caught, bc = Status(p, b->index) == Seen::Caught;
        if (ac != bc) return ac;
        break;
      }
    }
    return numberKey(a) < numberKey(b);
  });
  std::vector<uint16_t> out;
  for (const Species* s : v) out.push_back(s->index);
  return out;
}

Counts Count(const std::vector<Species>& all, const PlayerState& p) {
  Counts c;
  for (const Species& s : all) {
    if (s.formOf != 0) continue;
    c.total++;
    const Seen st = Status(p, s.index);
    if (st != Seen::Unseen) c.seen++;
    if (st == Seen::Caught) c.caught++;
  }
  return c;
}

std::vector<LearnedMove> MovesOf(const Species& s, MoveSource kind) {
  std::vector<LearnedMove> out;
  for (const LearnedMove& m : s.moves)
    if (m.source == kind) out.push_back(m);
  if (kind == MoveSource::Level) {
    std::stable_sort(out.begin(), out.end(), [](const LearnedMove& a, const LearnedMove& b) { return a.level < b.level; });
  } else {
    std::stable_sort(out.begin(), out.end(), [](const LearnedMove& a, const LearnedMove& b) { return a.number < b.number; });
  }
  return out;
}

std::vector<std::string> Wrap(const std::string& text, float maxWidth, const std::function<float(const std::string&)>& width) {
  std::vector<std::string> lines;
  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) {
      lines.push_back(cur);
      cur.clear();
    }
  };
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '\n') {
      flush();
      i++;
      continue;
    }
    size_t j = i;
    while (j < text.size() && text[j] != ' ' && text[j] != '\n') j++;
    std::string word = text.substr(i, j - i);
    i = j;
    while (i < text.size() && text[i] == ' ') i++;
    if (word.empty()) continue;
    const std::string joined = cur.empty() ? word : cur + " " + word;
    if (width(joined) <= maxWidth) {
      cur = joined;
      continue;
    }
    flush();
    while (!word.empty() && width(word) > maxWidth) {
      size_t n = 0, last = 0;
      while (n < word.size()) {
        size_t step = 1;
        const unsigned char c = static_cast<unsigned char>(word[n]);
        if (c >= 0xF0) step = 4;
        else if (c >= 0xE0) step = 3;
        else if (c >= 0xC0) step = 2;
        if (width(word.substr(0, n + step)) > maxWidth) break;
        n += step;
        last = n;
      }
      if (last == 0) last = std::min<size_t>(word.size(), 1);
      lines.push_back(word.substr(0, last));
      word.erase(0, last);
    }
    cur = word;
  }
  flush();
  return lines;
}

std::string ListToText(const std::set<uint16_t>& s) {
  std::string out;
  for (uint16_t v : s) {
    if (!out.empty()) out += ',';
    out += std::to_string(v);
  }
  return out;
}
std::set<uint16_t> ListFromText(const std::string& text) {
  std::set<uint16_t> out;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(',', i);
    if (j == std::string::npos) j = text.size();
    const long v = std::strtol(text.substr(i, j - i).c_str(), nullptr, 10);
    if (v > 0 && v < 65536) out.insert(static_cast<uint16_t>(v));
    i = j + 1;
  }
  return out;
}

std::string HeightText(uint16_t dm, bool imperial) {
  char b[32];
  if (!imperial) {
    std::snprintf(b, sizeof b, "%u.%u m", dm / 10u, dm % 10u);
    return b;
  }
  const unsigned inches = static_cast<unsigned>(dm * 3.937008 + 0.5);
  std::snprintf(b, sizeof b, "%u'%02u''", inches / 12, inches % 12);
  return b;
}
std::string WeightText(uint16_t hg, bool imperial) {
  char b[32];
  if (!imperial) {
    std::snprintf(b, sizeof b, "%u.%u kg", hg / 10u, hg % 10u);
    return b;
  }
  const unsigned tenths = static_cast<unsigned>(hg * 2.2046226 + 0.5);
  std::snprintf(b, sizeof b, "%u.%u lbs.", tenths / 10, tenths % 10);
  return b;
}
std::string GenderText(uint8_t ratio) {
  if (ratio == 255) return "Genderless";
  const double f = ratio / 256.0 * 100.0;
  char b[48];
  std::snprintf(b, sizeof b, "%.1f%% M  %.1f%% F", 100.0 - f, f);
  return b;
}

}
