#include "xd_flag_scan.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <tuple>
#include <type_traits>

namespace xd::data {

namespace {

constexpr int32_t kStoryFlag = 964;

uint32_t Be(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }

struct Val {
  bool isInt = false;
  int32_t v = 0;
};

struct Call {
  std::string func;
  uint32_t cls = 0, method = 0;
  bool hasA0 = false, hasA1 = false, a0Int = false, a1Int = false;
  int32_t a0 = 0, a1 = 0;
};

bool Scan(const std::vector<uint8_t>& d, std::vector<Call>& out) {
  if (d.size() < 0x20 || std::memcmp(d.data(), "TCOD", 4) != 0) return false;
  const uint32_t size = std::min<uint32_t>(Be(d.data() + 4), static_cast<uint32_t>(d.size()));
  size_t codeAt = 0, codeSize = 0, ftblAt = 0;
  bool haveCode = false, haveFtbl = false;
  for (uint32_t p = 0x10; p + 12 <= size;) {
    const uint32_t csz = Be(d.data() + p + 4), no = Be(d.data() + p + 8);
    if (no == 0 && std::memcmp(d.data() + p, "CODE", 4) == 0 && !haveCode) codeAt = p, codeSize = csz, haveCode = true;
    if (no == 0 && std::memcmp(d.data() + p, "FTBL", 4) == 0 && !haveFtbl) ftblAt = p, haveFtbl = true;
    if (csz == 0 || csz > 0x7FFFFFFF) break;
    p += csz;
  }
  if (!haveCode || codeSize < 0x20 || codeAt + codeSize > d.size()) return false;
  std::vector<std::pair<uint32_t, std::string>> funcs;
  if (haveFtbl && ftblAt + 0x20 <= d.size()) {
    const uint32_t n = Be(d.data() + ftblAt + 0x10);
    for (uint32_t i = 0; i < n && ftblAt + 0x28 + 8ull * i <= d.size(); i++) {
      const uint32_t ci = Be(d.data() + ftblAt + 0x20 + 8 * i), no = Be(d.data() + ftblAt + 0x24 + 8 * i);
      if (ftblAt + no >= d.size()) continue;
      funcs.emplace_back(ci, std::string(reinterpret_cast<const char*>(d.data() + ftblAt + no)));
    }
    std::sort(funcs.begin(), funcs.end());
  }
  const uint8_t* codes = d.data() + codeAt + 0x20;
  const size_t n = (codeSize - 0x20) / 4;
  std::vector<Val> stack;
  const auto pop = [&](size_t k = 1) {
    for (size_t j = 0; j < k && !stack.empty(); j++) stack.pop_back();
  };
  for (size_t i = 0; i < n;) {
    const uint8_t op = codes[4 * i], a = codes[4 * i + 1];
    const int16_t b = static_cast<int16_t>(codes[4 * i + 2] << 8 | codes[4 * i + 3]);
    if (op == 2) {
      if (a <= 2 || a == 0x35) {
        Val v;
        v.isInt = (a != 2) && i + 1 < n;
        if (i + 1 < n) v.v = static_cast<int32_t>(Be(codes + 4 * (i + 1)));
        stack.push_back(v);
        i += 2;
        continue;
      }
      stack.push_back({});
      i++;
      continue;
    }
    if (op == 3 || op == 0x11) {
      stack.push_back({});
    } else if (op == 4 || op == 5) {
      pop();
    } else if (op == 1) {
      if (a < 0x20) {
        if (!stack.empty()) stack.back() = {};
      } else {
        pop();
        if (!stack.empty()) stack.back() = {};
      }
    } else if (op == 6) {
      pop(a);
    } else if (op == 13) {
      for (int j = 0; j < a; j++) stack.push_back({});
    } else if (op == 14) {
      pop(a);
    } else if (op == 10 || op == 11) {
      pop();
    } else if (op == 9) {
      Call c;
      std::string fn = "?";
      for (const auto& f : funcs) {
        if (f.first <= i) fn = f.second;
      }
      c.func = fn;
      c.cls = a;
      c.method = static_cast<uint16_t>(b);
      const size_t have = std::min<size_t>(stack.size(), 8);
      if (have >= 1) c.hasA0 = true, c.a0Int = stack[stack.size() - 1].isInt, c.a0 = stack[stack.size() - 1].v;
      if (have >= 2) c.hasA1 = true, c.a1Int = stack[stack.size() - 2].isInt, c.a1 = stack[stack.size() - 2].v;
      out.push_back(std::move(c));
    }
    i++;
  }
  return true;
}

std::string Ident(const std::string& t) {
  std::string out;
  bool gap = false;
  for (char ch : t) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      if (gap && !out.empty()) out += '_';
      gap = false;
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    } else {
      gap = true;
    }
  }
  return out;
}

bool Generic(const std::string& f) {
  static const char* const k[] = {"hero_main", "preprocess", "anywaysave_restart", "postprocess", "modify_floor", "sound_start", "play_init",
                                  "play_scene", "pushpop_preprocess", "battleevent", "black_out", "?"};
  for (const char* g : k) {
    if (f == g) return true;
  }
  return false;
}

struct SetUse { std::string scr, func, val; };
struct TestUse { std::string scr, func; };
struct Info { std::vector<int32_t> story, fights; std::vector<std::string> gifts; };

}

std::vector<ScannedFlag> ScanRoomScriptFlags(const engine::mods::EntryReader& read, std::vector<std::string> archives, const std::set<uint32_t>& named,
                                             const std::map<uint32_t, std::string>& steps) {
  std::sort(archives.begin(), archives.end(), [](const std::string& a, const std::string& b) { return a + ".fsys" < b + ".fsys"; });
  std::map<uint32_t, std::pair<std::vector<SetUse>, std::vector<TestUse>>> use;
  std::map<std::pair<std::string, std::string>, Info> info;
  for (const std::string& stem : archives) {
    for (int k = 0; k < 64; k++) {
      std::vector<uint8_t> bytes;
      if (!read(stem + ".fsys", "#" + std::to_string(k), bytes)) break;
      std::vector<Call> calls;
      if (!Scan(bytes, calls)) continue;
      const std::string scr = stem + "#" + std::to_string(k);
      for (const Call& c : calls) {
        Info& in = info[{scr, c.func}];
        if (c.cls == 0 && c.method == 0x83 && c.hasA0 && c.a0Int && c.a0 == kStoryFlag && c.hasA1 && c.a1Int) in.story.push_back(c.a1);
        if (c.cls == 42 && (c.method == 0x10 || c.method == 0x11) && c.hasA1 && c.a1Int) in.fights.push_back(c.a1);
        if (c.cls == 43 && c.method == 0x25 && c.hasA1) in.gifts.push_back(c.a1Int ? std::to_string(c.a1) : std::string("a computed value"));
      }
      std::vector<std::string> funcs;
      for (const Call& c : calls) {
        if (std::find(funcs.begin(), funcs.end(), c.func) == funcs.end()) funcs.push_back(c.func);
      }
      for (const std::string& fn : funcs) {
        for (const Call& c : calls) {
          if (c.func != fn || c.cls != 0 || !c.hasA0 || !c.a0Int) continue;
          const uint32_t fid = static_cast<uint32_t>(c.a0);
          if (c.method == 0x81 || c.method == 0x82 || c.method == 0x83) {
            std::string val = c.method == 0x81 ? "on" : c.method == 0x82 ? "off" : "set=" + (c.hasA1 ? (c.a1Int ? std::to_string(c.a1) : std::string("a computed value")) : std::string("?"));
            use[fid].first.push_back({scr, fn, val});
          } else if (c.method == 0x84 || c.method == 0x85) {
            use[fid].second.push_back({scr, fn});
          }
        }
      }
    }
  }
  const auto room = [](const std::string& s) { return s.substr(0, s.find('#')); };
  struct Row { int prio; uint32_t id; std::string name, meaning; };
  std::vector<Row> rows;
  for (const auto& [fid, uses] : use) {
    if (named.count(fid) != 0 || fid == 0) continue;
    const std::vector<SetUse>& sets = uses.first;
    const std::vector<TestUse>& tests = uses.second;
    std::vector<SetUse> on;
    for (const SetUse& s : sets) {
      if (s.val == "on") on.push_back(s);
    }
    if (on.empty()) on = sets;
    const SetUse* pick = nullptr;
    for (const SetUse& s : on) {
      if (!Generic(s.func)) { pick = &s; break; }
    }
    if (pick == nullptr && !on.empty()) pick = &on[0];
    std::string kind, base;
    std::vector<std::string> extra;
    bool storyLinked = false;
    if (pick != nullptr) {
      const Info& in = info[{pick->scr, pick->func}];
      bool multi = false;
      for (const SetUse& s : sets) {
        if (s.val.rfind("set=", 0) == 0 && s.val != "set=0" && s.val != "set=1") multi = true;
      }
      const bool switchedOn = pick->val == "on" || pick->val == "set=1";
      const auto list = [](const auto& v) {
        std::string s;
        for (size_t i = 0; i < v.size() && i < 3; i++) {
          if constexpr (std::is_same_v<std::decay_t<decltype(v[i])>, std::string>) s += (i ? "," : "") + v[i];
          else s += (i ? "," : "") + std::to_string(v[i]);
        }
        return s;
      };
      if (!in.fights.empty()) {
        extra.push_back("same function starts battle " + list(in.fights));
        if (switchedOn && !multi) kind = "BEAT_";
      }
      if (!in.gifts.empty()) {
        extra.push_back("same function gives gift kind " + list(in.gifts));
        if (switchedOn && !multi && kind.empty()) kind = "GOT_";
      }
      if (multi) {
        kind = "STATE_";
        extra.insert(extra.begin(), "a multi-value STATE (several values set), not a yes/no flag");
      }
      if (!in.story.empty()) {
        const auto s = steps.find(static_cast<uint32_t>(in.story.back()));
        extra.push_back("same function sets story step " + std::to_string(in.story.back()) + " (" + (s != steps.end() ? s->second : std::string("?")) + ")");
        storyLinked = true;
      }
      base = room(pick->scr) + (Generic(pick->func) ? "" : "_" + pick->func);
    } else {
      const TestUse* t = nullptr;
      for (const TestUse& x : tests) {
        if (!Generic(x.func)) { t = &x; break; }
      }
      if (t == nullptr && !tests.empty()) t = &tests[0];
      const std::string scr = t != nullptr ? t->scr : "?", fn = t != nullptr ? t->func : "?";
      base = room(scr) + (Generic(fn) ? "" : "_" + fn);
      kind = "CHECK_";
    }
    Row r;
    r.id = fid;
    r.name = kind + Ident(base) + "_" + std::to_string(fid);
    std::set<std::string> rooms;
    for (const SetUse& s : sets) rooms.insert(room(s.scr));
    for (const TestUse& s : tests) rooms.insert(room(s.scr));
    std::string m;
    if (!sets.empty()) {
      m = "set: ";
      for (size_t i = 0; i < sets.size() && i < 4; i++) m += (i ? "; " : "") + room(sets[i].scr) + " " + sets[i].func + " " + sets[i].val;
      if (sets.size() > 4) m += " ...";
    } else {
      m = "never set by a script with a literal id (set by code or computed id)";
    }
    std::string rl;
    size_t shown = 0;
    for (const std::string& x : rooms) {
      if (shown++ >= 5) break;
      rl += (shown > 1 ? ", " : "") + x;
    }
    m += " | tested in " + std::to_string(tests.size()) + " place(s) (" + rl + (rooms.size() > 5 ? " ..." : "") + ")";
    for (const std::string& e : extra) m += " | " + e;
    r.meaning = m;
    r.prio = storyLinked ? 0 : ((kind == "BEAT_" || kind == "GOT_") ? 1 : 2);
    rows.push_back(std::move(r));
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return std::tie(a.prio, a.id, a.name, a.meaning) < std::tie(b.prio, b.id, b.name, b.meaning); });
  std::vector<ScannedFlag> out;
  for (Row& r : rows) out.push_back({r.id, std::move(r.name), std::move(r.meaning)});
  return out;
}

}
