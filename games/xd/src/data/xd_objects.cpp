#include "xd_objects.h"

#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace xd::objects {

namespace js = engine::mods::json;

void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24), p[1] = static_cast<uint8_t>(v >> 16), p[2] = static_cast<uint8_t>(v >> 8), p[3] = static_cast<uint8_t>(v);
}
void PutBe16(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v >> 8), p[1] = static_cast<uint8_t>(v); }
void PutBeF32(uint8_t* p, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  PutBe32(p, bits);
}

namespace {

bool GoodName(const std::string& n) {
  if (n.empty() || n.size() > 40 || !std::isalpha(static_cast<unsigned char>(n[0]))) return false;
  for (char c : n) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

struct Ctx {
  std::vector<std::string>* problems = nullptr;
  void Say(const js::Value& at, const std::string& what) const { problems->push_back("line " + std::to_string(at.line) + ": " + what); }
};

class Fields {
public:
  Fields(const js::Value& v, const Ctx& c, const std::string& what) : mV(v), mC(c), mWhat(what) {
    if (v.kind != js::Value::Kind::Object) {
      c.Say(v, what + " must be an object { ... }");
      mOk = false;
    }
  }
  bool Ok() const { return mOk; }
  const js::Value* Get(const std::string& key) {
    if (!mOk) return nullptr;
    mUsed.insert(key);
    const auto it = mV.fields.find(key);
    return it == mV.fields.end() ? nullptr : &it->second;
  }
  bool Number(const std::string& key, double& out, double lo, double hi) {
    const js::Value* v = Get(key);
    if (v == nullptr) return false;
    if (v->kind != js::Value::Kind::Number || !std::isfinite(v->number) || v->number < lo || v->number > hi) {
      mC.Say(*v, mWhat + " \"" + key + "\" must be a number from " + js::Number(lo) + " to " + js::Number(hi));
      return false;
    }
    out = v->number;
    return true;
  }
  bool Whole(const std::string& key, long& out, long lo, long hi) {
    const js::Value* v = Get(key);
    if (v == nullptr) return false;
    if (v->kind != js::Value::Kind::Number || v->number != std::floor(v->number) || v->number < lo || v->number > hi) {
      mC.Say(*v, mWhat + " \"" + key + "\" must be a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
      return false;
    }
    out = static_cast<long>(v->number);
    return true;
  }
  bool Text(const std::string& key, std::string& out) {
    const js::Value* v = Get(key);
    if (v == nullptr) return false;
    if (v->kind != js::Value::Kind::String) {
      mC.Say(*v, mWhat + " \"" + key + "\" must be text in quotes");
      return false;
    }
    out = v->text;
    return true;
  }
  bool Bool(const std::string& key, bool& out) {
    const js::Value* v = Get(key);
    if (v == nullptr) return false;
    if (v->kind != js::Value::Kind::Bool) {
      mC.Say(*v, mWhat + " \"" + key + "\" must be true or false");
      return false;
    }
    out = v->boolean;
    return true;
  }
  void Finish() {
    if (!mOk) return;
    for (const std::string& k : mV.order) {
      if (mUsed.count(k) == 0) mC.Say(mV.fields.at(k), mWhat + " has \"" + k + "\", which is not a field (a typo?); ignored");
    }
  }

private:
  const js::Value& mV;
  const Ctx& mC;
  std::string mWhat;
  bool mOk = true;
  std::set<std::string> mUsed;
};

bool OneOf(const std::string& s, std::initializer_list<const char*> list) {
  for (const char* l : list) {
    if (s == l) return true;
  }
  return false;
}

}

bool Parse(const std::string& text, Room& out, std::vector<std::string>& problems) {
  js::Value root;
  js::Reader reader(text);
  if (!reader.Document(root)) {
    problems.push_back(reader.Error());
    return false;
  }
  Ctx c;
  c.problems = &problems;
  Fields top(root, c, "the file");
  if (!top.Ok()) return false;
  std::set<std::string> names;
  const auto list = [&](const char* key, size_t most, auto each) {
    const js::Value* v = top.Get(key);
    if (v == nullptr) return;
    if (v->kind != js::Value::Kind::Array) {
      c.Say(*v, std::string("\"") + key + "\" must be a list [ ... ]");
      return;
    }
    if (v->items.size() > most) c.Say(*v, std::string("\"") + key + "\": at most " + std::to_string(most) + " entries; the rest are left out");
    for (size_t i = 0; i < v->items.size() && i < most; i++) each(v->items[i]);
  };
  const auto nameOf = [&](Fields& f, const js::Value& v, const std::string& kind, std::string& name) {
    if (!f.Text("name", name) || !GoodName(name)) {
      c.Say(v, kind + " needs a \"name\" (letters, digits, _; up to 40)");
      return false;
    }
    if (!names.insert(kind + ":" + name).second) {
      c.Say(v, kind + " name \"" + name + "\" is used twice");
      return false;
    }
    return true;
  };
  list("people", kMaxPeople, [&](const js::Value& v) {
    Person p;
    Fields f(v, c, "a person");
    if (!f.Ok()) return;
    p.line = v.line;
    bool good = nameOf(f, v, "person", p.name);
    const bool hasModel = f.Whole("model", p.model, 0, 0xFFFE);
    if (!hasModel && v.fields.find("model") == v.fields.end()) c.Say(v, "person \"" + p.name + "\" needs a \"model\" (a row of peopleInfoData)");
    good &= hasModel;
    f.Number("x", p.x, -100000, 100000), f.Number("y", p.y, -100000, 100000), f.Number("z", p.z, -100000, 100000);
    f.Number("angle", p.angle, -360, 360);
    f.Text("talk", p.talk), f.Text("move", p.move), f.Text("behavior", p.behavior);
    f.Bool("solid", p.solid), f.Bool("visible", p.visible), f.Bool("look_at_hero", p.lookAtHero), f.Bool("talk_through_walls", p.talkThroughWalls);
    f.Bool("blend", p.blend), f.Whole("talk_start", p.talkStart, 0, 3), f.Whole("talk_end", p.talkEnd, 0, 1), f.Whole("name_text", p.nameText, 0, 0xFFFF);
    if (!OneOf(p.behavior, {"stand", "wander", "look"})) {
      c.Say(v, "person \"" + p.name + "\": \"behavior\" must be stand, wander or look");
      good = false;
    }
    f.Finish();
    if (good) out.people.push_back(std::move(p));
  });
  top.Bool("replace_people", out.replacePeople);
  list("zones", kMaxZones, [&](const js::Value& v) {
    Zone z;
    Fields f(v, c, "a zone");
    if (!f.Ok()) return;
    z.line = v.line;
    bool good = nameOf(f, v, "zone", z.name);
    const js::Value* area = f.Get("area");
    if (area != nullptr) {
      Fields a(*area, c, "a zone's area");
      z.area = true;
      good &= a.Ok();
      if (a.Ok()) {
        good &= a.Number("x", z.x, -100000, 100000) && a.Number("y", z.y, -100000, 100000) && a.Number("z", z.z, -100000, 100000) &&
                a.Number("radius", z.radius, 0.1, 100000);
        a.Finish();
      }
    } else {
      f.Text("on", z.on);
      f.Text("run", z.run);
      if (!OneOf(z.on, {"walk", "touch", "check"})) {
        c.Say(v, "zone \"" + z.name + "\": \"on\" must be walk, touch or check");
        good = false;
      }
      const bool hasEvent = f.Whole("event", z.event, 0, 0xFFFF);
      if (!hasEvent && v.fields.find("event") == v.fields.end()) c.Say(v, "zone \"" + z.name + "\" needs an \"event\" (the map's event number) or an \"area\"");
      good &= hasEvent;
      if (const js::Value* a = f.Get("args")) {
        if (a->kind != js::Value::Kind::Array || a->items.size() > 4) {
          c.Say(*a, "zone \"args\" must be a list of up to 4 whole numbers");
          good = false;
        } else {
          for (size_t i = 0; i < a->items.size(); i++) {
            if (a->items[i].kind != js::Value::Kind::Number || a->items[i].number != std::floor(a->items[i].number)) {
              c.Say(a->items[i], "zone \"args\" must be whole numbers");
              good = false;
            } else {
              z.args[i] = static_cast<long>(a->items[i].number);
            }
          }
        }
      }
    }
    f.Finish();
    if (good) out.zones.push_back(std::move(z));
  });
  list("chests", kMaxChests, [&](const js::Value& v) {
    Chest ch;
    Fields f(v, c, "a chest");
    if (!f.Ok()) return;
    ch.line = v.line;
    bool good = nameOf(f, v, "chest", ch.name);
    f.Number("x", ch.x, -100000, 100000), f.Number("y", ch.y, -100000, 100000), f.Number("z", ch.z, -100000, 100000);
    f.Number("angle", ch.angle, -360, 360);
    if (const js::Value* it = f.Get("item")) {
      if (it->kind == js::Value::Kind::Number) ch.item = js::Number(it->number);
      else if (it->kind == js::Value::Kind::String) ch.item = it->text;
      else { c.Say(*it, "chest \"item\" must be an item's name or number"); good = false; }
    }
    f.Whole("count", ch.count, 1, 999);
    f.Text("flag", ch.flag), f.Text("look", ch.look), f.Text("kind", ch.kind);
    if (ch.item.empty() && ch.kind != "money" && good) { c.Say(v, "chest \"" + ch.name + "\" needs an \"item\""); good = false; }
    if (ch.flag.empty() && good) { c.Say(v, "chest \"" + ch.name + "\" needs a \"flag\" (a story flag of the mod: that is how the game remembers it was taken)"); good = false; }
    if (!OneOf(ch.look, {"chest", "pickup", "pickup2"})) { c.Say(v, "chest \"look\" must be chest, pickup or pickup2"); good = false; }
    if (!OneOf(ch.kind, {"item", "money", "item_quiet"})) { c.Say(v, "chest \"kind\" must be item, money or item_quiet"); good = false; }
    f.Finish();
    if (good) out.chests.push_back(std::move(ch));
  });
  list("doors", kMaxDoors, [&](const js::Value& v) {
    Door d;
    Fields f(v, c, "a door");
    if (!f.Ok()) return;
    d.line = v.line;
    bool good = nameOf(f, v, "door", d.name);
    const bool hasLike = f.Whole("like", d.like, 0, 100000);
    if (!hasLike && v.fields.find("like") == v.fields.end()) c.Say(v, "door \"" + d.name + "\" needs \"like\": a row of the disc's floor_door_list to start from");
    good &= hasLike;
    f.Whole("type", d.type, 0, 255), f.Whole("part", d.part, 0, 255), f.Whole("collision", d.collision, -1, 32767);
    f.Whole("open_anim", d.openAnim, -1, 127), f.Whole("close_anim", d.closeAnim, -1, 127), f.Whole("resource", d.resource, 0, 0x7FFFFFFF);
    f.Text("open_flag", d.openFlag), f.Text("close_flag", d.closeFlag);
    f.Finish();
    if (good) out.doors.push_back(std::move(d));
  });
  if (const js::Value* m = top.Get("music")) {
    out.music.given = true;
    if (m->kind == js::Value::Kind::Null || (m->kind == js::Value::Kind::String && m->text == "none")) {
      out.music.none = true;
    } else if (m->kind == js::Value::Kind::String) {
      out.music.like = m->text;
    } else {
      Fields f(*m, c, "the music");
      if (f.Ok()) {
        f.Text("like", out.music.like);
        const auto bgmIt = m->fields.find("bgm");
        const js::Value* bgmV = bgmIt == m->fields.end() ? nullptr : &bgmIt->second;
        if (bgmV != nullptr && bgmV->kind == js::Value::Kind::String) {
          out.music.bgmName = bgmV->text;
          f.Text("bgm", out.music.bgmName);
        } else {
          f.Whole("bgm", out.music.bgm, 0, 0xFFFF);
        }
        f.Whole("volume", out.music.volume, 0, 127), f.Whole("fade", out.music.fade, 0, 0xFFFF);
        f.Whole("env_fade", out.music.envFade, 0, 0xFFFF), f.Text("flag", out.music.flag);
        f.Finish();
        if (out.music.like.empty() && out.music.bgm < 0 && out.music.bgmName.empty()) {
          c.Say(*m, "\"music\" needs \"like\" (a room of the disc) or \"bgm\" (a sound number)");
          out.music.given = false;
        }
      } else {
        out.music.given = false;
      }
    }
  }
  top.Finish();
  return true;
}

void Build(const Room& room, const Lookups& ask, Built& out, std::vector<std::string>& problems) {
  const auto say = [&](size_t line, const std::string& what) { problems.push_back("line " + std::to_string(line) + ": " + what); };
  for (const Person& p : room.people) {
    uint8_t e[kPersonSize] = {0};
    uint32_t moveId = 0, talkId = 0;
    bool ok = true;
    for (int which = 0; which < 2; which++) {
      const std::string& fn = which == 0 ? p.move : p.talk;
      if (fn.empty()) continue;
      const int idx = ask.function ? ask.function(fn) : -1;
      if (idx < 0) {
        say(p.line, "person \"" + p.name + "\": the room's script has no function \"" + fn + "\"");
        ok = false;
      } else {
        (which == 0 ? moveId : talkId) = 0x01000000u | static_cast<uint32_t>(idx);
      }
    }
    if (!ok) continue;
    const int move = p.behavior == "wander" ? 2 : p.behavior == "look" ? 3 : 0;
    e[0] = static_cast<uint8_t>((p.visible ? 0x80 : 0) | 0x40 | (p.solid ? 0x20 : 0) | (p.lookAtHero ? 0x10 : 0) | (p.talkThroughWalls ? 0x08 : 0) | (p.blend ? 0x04 : 0));
    e[1] = static_cast<uint8_t>(move << 5 | static_cast<int>(p.talkStart) << 3 | static_cast<int>(p.talkEnd) << 1);
    PutBe16(e + 4, static_cast<uint16_t>(static_cast<int16_t>(std::lround(p.angle))));
    PutBe16(e + 6, static_cast<uint32_t>(p.model));
    PutBe16(e + 8, static_cast<uint32_t>(p.nameText));
    PutBe32(e + 0x10, moveId);
    PutBe32(e + 0x14, talkId);
    PutBeF32(e + 0x18, static_cast<float>(p.x)), PutBeF32(e + 0x1C, static_cast<float>(p.y)), PutBeF32(e + 0x20, static_cast<float>(p.z));
    out.people.insert(out.people.end(), e, e + kPersonSize);
    out.personNames.push_back(p.name);
  }
  for (const Zone& z : room.zones) {
    if (z.area) {
      out.areas.push_back({z.name, static_cast<float>(z.x), static_cast<float>(z.y), static_cast<float>(z.z), static_cast<float>(z.radius)});
      continue;
    }
    uint32_t script = 0;
    if (!z.run.empty()) {
      const int idx = ask.function ? ask.function(z.run) : -1;
      if (idx < 0) {
        say(z.line, "zone \"" + z.name + "\": the room's script has no function \"" + z.run + "\"");
        continue;
      }
      script = 0x01000000u | static_cast<uint32_t>(idx);
    }
    std::vector<uint8_t> r(0x1C, 0);
    r[0] = static_cast<uint8_t>(z.on == "walk" ? 1 : z.on == "touch" ? 2 : 3);
    PutBe16(r.data() + 2, ask.floorId);
    PutBe32(r.data() + 4, static_cast<uint32_t>(z.event));
    PutBe32(r.data() + 8, script);
    for (int i = 0; i < 4; i++) PutBe32(r.data() + 0xC + 4 * i, static_cast<uint32_t>(z.args[i]));
    out.events.push_back(std::move(r));
    out.eventNames.push_back(z.name);
  }
  for (const Chest& ch : room.chests) {
    uint32_t flag = 0, item = 0;
    if (!ask.flag || !ask.flag(ch.flag, flag) || flag == 0 || flag > 0xFFFF) {
      say(ch.line, "chest \"" + ch.name + "\": the mod has no story flag \"" + ch.flag + "\" (or its number does not fit 16 bits)");
      continue;
    }
    if (ch.kind == "money") {
      item = static_cast<uint32_t>(ch.count);
    } else if (!ch.item.empty() && ch.item.find_first_not_of("0123456789") == std::string::npos) {
      item = static_cast<uint32_t>(std::strtoul(ch.item.c_str(), nullptr, 10));
    } else if (!ask.item || !ask.item(ch.item, item)) {
      say(ch.line, "chest \"" + ch.name + "\": \"" + ch.item + "\" is not an item of the game (its name as the game writes it, or its number)");
      continue;
    }
    std::vector<uint8_t> r(0x1C, 0);
    const int kind = ch.look == "chest" ? 1 : ch.look == "pickup" ? 2 : 3;
    const int type = ch.kind == "item" ? 1 : ch.kind == "money" ? 2 : 4;
    r[0] = static_cast<uint8_t>(kind << 5 | type << 2);
    r[1] = static_cast<uint8_t>(ch.kind == "money" ? 1 : ch.count);
    PutBe16(r.data() + 2, static_cast<uint16_t>(static_cast<int16_t>(std::lround(ch.angle))));
    PutBe16(r.data() + 4, ask.floorId);
    PutBe16(r.data() + 6, flag);
    PutBe32(r.data() + 0xC, item);
    PutBeF32(r.data() + 0x10, static_cast<float>(ch.x)), PutBeF32(r.data() + 0x14, static_cast<float>(ch.y)), PutBeF32(r.data() + 0x18, static_cast<float>(ch.z));
    out.chests.push_back(std::move(r));
    out.chestNames.push_back(ch.name);
  }
  for (const Door& d : room.doors) {
    std::vector<uint8_t> r;
    if (!ask.doorRow || !ask.doorRow(d.like, r) || r.size() != 0x18) {
      say(d.line, "door \"" + d.name + "\": the disc has no door row " + std::to_string(d.like) + " (floor_door_list)");
      continue;
    }
    bool ok = true;
    const auto flagField = [&](const std::string& name, size_t at) {
      if (name.empty()) return;
      uint32_t id = 0;
      if (!ask.flag || !ask.flag(name, id) || id == 0 || id > 0xFFFF) {
        say(d.line, "door \"" + d.name + "\": the mod has no story flag \"" + name + "\"");
        ok = false;
      } else {
        PutBe16(r.data() + at, id);
      }
    };
    PutBe16(r.data() + 0x0C, ask.floorId);
    if (d.openAnim >= -1) r[0] = static_cast<uint8_t>(d.openAnim);
    if (d.closeAnim >= -1) r[1] = static_cast<uint8_t>(d.closeAnim);
    if (d.type >= 0) r[7] = static_cast<uint8_t>(d.type);
    if (d.part >= 0) r[8] = static_cast<uint8_t>(d.part);
    if (d.collision >= -1) PutBe16(r.data() + 0x0A, static_cast<uint16_t>(static_cast<int16_t>(d.collision)));
    if (d.resource >= 0) PutBe32(r.data() + 0x14, static_cast<uint32_t>(d.resource));
    flagField(d.openFlag, 0x0E), flagField(d.closeFlag, 0x10);
    if (!ok) continue;
    out.doors.push_back(std::move(r));
    out.doorNames.push_back(d.name);
  }
  if (room.music.given && !room.music.none) {
    if (!room.music.like.empty()) {
      std::vector<std::vector<uint8_t>> rows;
      if (!ask.musicOf || !ask.musicOf(room.music.like, rows) || rows.empty()) {
        problems.push_back("the music: the room \"" + room.music.like + "\" has no music rows to copy");
      }
      for (auto& r : rows) {
        if (r.size() != 0xC) continue;
        PutBe16(r.data() + 2, ask.floorId);
        out.music.push_back(std::move(r));
      }
    } else {
      std::vector<uint8_t> r(0xC, 0);
      uint32_t flag = 0;
      long bgm = room.music.bgm;
      if (!room.music.bgmName.empty()) {
        bgm = ask.song ? ask.song(room.music.bgmName) : -1;
        if (bgm < 0) problems.push_back("the music: the song \"" + room.music.bgmName + "\" is not one a mod adds (sounds/songs.json); the room keeps the disc room's music");
      }
      if (bgm < 0) {
      } else if (!room.music.flag.empty() && (!ask.flag || !ask.flag(room.music.flag, flag) || flag > 0xFFFF)) {
        problems.push_back("the music: the mod has no story flag \"" + room.music.flag + "\"");
      } else {
        r[0] = 1;
        r[1] = static_cast<uint8_t>(room.music.volume);
        PutBe16(r.data() + 2, ask.floorId);
        PutBe16(r.data() + 4, static_cast<uint32_t>(bgm));
        PutBe16(r.data() + 6, static_cast<uint32_t>(room.music.fade));
        PutBe16(r.data() + 8, static_cast<uint32_t>(room.music.envFade));
        PutBe16(r.data() + 10, flag);
        out.music.push_back(std::move(r));
      }
    }
  }
}

std::vector<uint8_t> MergePeople(const uint8_t* own, uint32_t n, const std::vector<uint8_t>& mod, uint32_t base, bool replace, std::string& problem) {
  std::vector<uint8_t> out;
  if (!replace && own != nullptr && n != 0) {
    if (n > base) {
      problem = "a room already has " + std::to_string(n) + " people; a mod's people start at " + std::to_string(base);
      return {};
    }
    out.assign(own, own + static_cast<size_t>(n) * kPersonSize);
  }
  std::vector<uint8_t> blank(kPersonSize, 0);
  if (mod.size() >= kPersonSize) {
    std::memcpy(blank.data(), mod.data(), kPersonSize);
    blank[0] = static_cast<uint8_t>(blank[0] & ~0xC0);
  }
  while (out.size() < static_cast<size_t>(base) * kPersonSize) out.insert(out.end(), blank.begin(), blank.end());
  out.insert(out.end(), mod.begin(), mod.end());
  return out;
}

}
