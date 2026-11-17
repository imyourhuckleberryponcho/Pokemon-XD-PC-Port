#include "xd_objects.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace xd::objects;

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  failures += !ok;
}
static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static uint32_t be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
static float bef(const uint8_t* p) {
  const uint32_t v = be32(p);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
static bool has(const std::vector<std::string>& v, const char* part) {
  for (const std::string& s : v) {
    if (s.find(part) != std::string::npos) return true;
  }
  return false;
}

static Lookups lookups() {
  Lookups l;
  l.floorId = 4002;
  l.function = [](const std::string& n) { return n == "guard_talk" ? 3 : n == "guard_idle" ? 4 : n == "gate_event" ? 7 : -1; };
  l.flag = [](const std::string& n, uint32_t& id) {
    if (n == "chest_one") { id = 2400; return true; }
    if (n == "gate_open") { id = 2401; return true; }
    return false;
  };
  l.item = [](const std::string& n, uint32_t& id) {
    if (n == "SUPER POTION") { id = 22; return true; }
    return false;
  };
  l.doorRow = [](long i, std::vector<uint8_t>& out) {
    if (i != 3) return false;
    out.assign(0x18, 0);
    out[7] = 1, out[0x0C] = 0, out[0x0D] = 9;
    return true;
  };
  l.musicOf = [](const std::string& room, std::vector<std::vector<uint8_t>>& rows) {
    if (room != "D1_labo_1F") return false;
    std::vector<uint8_t> r(0xC, 0);
    r[0] = 1, r[1] = 127, r[2] = 0, r[3] = 7, r[4] = 4, r[5] = 0x2B;
    rows.push_back(r);
    return true;
  };
  return l;
}

int main() {
  const char* good = R"({
    "people": [ { "name": "guard", "model": 10, "x": 100, "y": 0, "z": -40.5, "angle": 90, "talk": "guard_talk", "move": "guard_idle",
                  "behavior": "wander", "talk_start": 1, "look_at_hero": true } ],
    "zones": [ { "name": "gate", "event": 7, "on": "touch", "run": "gate_event", "args": [1, 2] },
               { "name": "hall", "area": { "x": 10, "y": 0, "z": 20, "radius": 30 } } ],
    "chests": [ { "name": "c1", "x": 6, "y": 0, "z": 20, "angle": 180, "item": "super potion", "count": 3, "flag": "chest_one" },
                { "name": "c2", "x": 0, "y": 0, "z": 0, "item": 22, "flag": "chest_one", "look": "pickup", "kind": "item_quiet" } ],
    "doors": [ { "name": "d1", "like": 3, "open_flag": "gate_open", "type": 2 } ],
    "music": { "like": "D1_labo_1F" }
  })";
  Room room;
  std::vector<std::string> problems;
  check(Parse(good, room, problems) && problems.empty(), "a good file parses without a problem");
  check(room.people.size() == 1 && room.zones.size() == 2 && room.chests.size() == 2 && room.doors.size() == 1 && room.music.given,
        "one person, two zones, two chests, a door, the music");
  Lookups ask = lookups();
  room.chests[0].item = "SUPER POTION";
  Built b;
  Build(room, ask, b, problems);
  check(problems.empty(), "it builds without a problem");
  check(b.people.size() == kPersonSize && b.personNames[0] == "guard", "one 0x24-byte person");
  const uint8_t* e = b.people.data();
  check(e[0] == 0xF4, "flags: visible, loaded on entry, solid, looking at the hero and blending motions (0x80 | 0x40 | 0x20 | 0x10 | 0x04, flag2 is the default)");
  check(e[1] == (2 << 5 | 1 << 3 | 1 << 1), "move type 2 (wander), talk start 1, talk end 1 (the default)");
  check(int16_t(be16(e + 4)) == 90 && be16(e + 6) == 10, "angle 90 degrees, model row 10");
  check(be32(e + 0x10) == 0x01000004 && be32(e + 0x14) == 0x01000003, "move and talk script numbers: 0x0100 0000 | function index");
  check(bef(e + 0x18) == 100.0f && bef(e + 0x1C) == 0.0f && bef(e + 0x20) == -40.5f, "position");
  check(b.events.size() == 1 && b.events[0][0] == 2 && be16(b.events[0].data() + 2) == 4002 && be32(b.events[0].data() + 4) == 7 &&
            be32(b.events[0].data() + 8) == 0x01000007 && be32(b.events[0].data() + 0xC) == 1 && be32(b.events[0].data() + 0x10) == 2,
        "the event zone: type 2 (touch), floor, the map's event number, script, arguments");
  check(b.areas.size() == 1 && b.areas[0].name == "hall" && b.areas[0].radius == 30.0f, "the area zone is kept for the port to answer");
  const std::vector<uint8_t>& c = b.chests[0];
  check(c.size() == 0x1C && c[0] == (1 << 5 | 1 << 2) && c[1] == 3 && int16_t(be16(&c[2])) == 180 && be16(&c[4]) == 4002 && be16(&c[6]) == 2400 &&
            be32(&c[0xC]) == 22 && bef(&c[0x10]) == 6.0f && bef(&c[0x18]) == 20.0f,
        "the chest: kind 1 / type 1, count, angle, floor, the flag that remembers it, item, position");
  check(b.chests.size() == 2 && b.chests[1][0] == (2 << 5 | 4 << 2), "a pickup that vanishes (kind 2) giving the item quietly (type 4)");
  check(b.doors.size() == 1 && be16(b.doors[0].data() + 0x0C) == 4002 && b.doors[0][7] == 2 && be16(b.doors[0].data() + 0x0E) == 2401,
        "the door: a copy of the disc's row with the new floor, type and open flag");
  check(b.music.size() == 1 && b.music[0][4] == 4 && b.music[0][5] == 0x2B, "music copied from a disc room: its sound kept");
  check(be16(b.music[0].data() + 2) == 4002, "its floor number is the new room's");

  {
    Room r;
    std::vector<std::string> p;
    Parse(R"({"people": [ {"name": "a b", "model": 1}, {"name": "ok", "model": 2, "colour": 3}, {"name": "nomodel"} ],
              "chests": [ {"name": "c", "item": "X"} ], "zones": [ {"name": "z", "on": "stomp", "event": 1} ], "oops": 1 })", r, p);
    check(has(p, "needs a \"name\"") && has(p, "\"colour\"") && has(p, "needs a \"model\"") && has(p, "needs a \"flag\"") &&
              has(p, "walk, touch or check") && has(p, "\"oops\""),
          "bad names, a typo'd field, a missing model or flag, a bad zone kind and a stray field are each told");
    for (const std::string& m : p) std::printf("      %s\n", m.c_str());
    check(r.people.size() == 1 && r.people[0].name == "ok" && r.chests.empty() && r.zones.empty(), "the good person is kept, the bad parts left out");
    Room r2;
    std::vector<std::string> p2;
    check(!Parse("{ \"people\": [ ", r2, p2) && !p2.empty() && p2[0].find("line") != std::string::npos, "a broken file is refused with a line");
  }
  {
    Room r;
    std::vector<std::string> p;
    Parse(R"({"people": [ {"name": "g", "model": 1, "talk": "missing_fn"} ],
              "chests": [ {"name": "c", "item": "NOT AN ITEM", "flag": "chest_one"}, {"name": "d", "item": "SUPER POTION", "flag": "no_such_flag"} ],
              "doors": [ {"name": "d", "like": 99} ], "music": "D1_nowhere" })", r, p);
    Built bb;
    Build(r, lookups(), bb, p);
    check(has(p, "no function \"missing_fn\"") && has(p, "not an item") && has(p, "no story flag \"no_such_flag\"") && has(p, "door row 99") &&
              has(p, "no music rows"),
          "an unknown function, item, flag, door row and music room are each told");
    check(bb.people.empty() && bb.chests.empty() && bb.doors.empty() && bb.music.empty(), "and left out");
  }
  {
    Room r;
    std::vector<std::string> p;
    Parse(R"({"chests": [ {"name": "m", "kind": "money", "count": 500, "flag": "chest_one"} ], "music": {"bgm": 1067, "volume": 90, "fade": 500}})", r, p);
    Built bb;
    Build(r, lookups(), bb, p);
    check(p.empty() && bb.chests.size() == 1 && be32(&bb.chests[0][0xC]) == 500 && bb.chests[0][1] == 1 && (bb.chests[0][0] & 0x1C) == 2 << 2,
          "money: type 2, the amount in the item field");
    check(bb.music.size() == 1 && bb.music[0][1] == 90 && be16(bb.music[0].data() + 4) == 1067 && be16(bb.music[0].data() + 6) == 500, "music by sound number");
  }
  {
    std::vector<uint8_t> own(2 * kPersonSize, 0x11), mod(kPersonSize, 0);
    mod[0] = 0xF0, mod[6] = 0, mod[7] = 10;
    std::string why;
    const std::vector<uint8_t> list = MergePeople(own.data(), 2, mod, kModPeopleBase, false, why);
    check(list.size() == (kModPeopleBase + 1) * kPersonSize && list[0] == 0x11 && list[kPersonSize] == 0x11, "people list: the room's own two people keep their places");
    check(list[2 * kPersonSize] == 0x30 && list[2 * kPersonSize + 7] == 10, "people list: the places between are empty entries (not visible, not loaded on entry) that look like the mod's first person");
    check(list[kModPeopleBase * kPersonSize] == 0xF0, "people list: the mod's person is number 128");
    const std::vector<uint8_t> only = MergePeople(own.data(), 2, mod, kModPeopleBase, true, why);
    check(only.size() == list.size() && only[0] == 0x30, "people list: replace_people leaves the room's own people out (their places are empty)");
    std::vector<uint8_t> many(129 * kPersonSize, 0);
    check(MergePeople(many.data(), 129, mod, kModPeopleBase, false, why).empty() && why.find("already has 129") != std::string::npos,
          "people list: a room with more people than the mod's first number is refused");
  }
  std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILED");
  return failures == 0 ? 0 : 1;
}
