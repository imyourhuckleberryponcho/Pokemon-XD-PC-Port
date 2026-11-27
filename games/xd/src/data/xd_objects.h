#ifndef XD_OBJECTS_H
#define XD_OBJECTS_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xd::objects {

inline constexpr uint32_t kModPeopleBase = 128;
inline constexpr size_t kPersonSize = 0x24;
inline constexpr size_t kMaxPeople = 48, kMaxChests = 48, kMaxZones = 64, kMaxDoors = 16;

struct Person {
  std::string name;
  long model = -1;
  double x = 0, y = 0, z = 0, angle = 0;
  std::string talk, move;
  std::string behavior = "stand";
  bool solid = true, visible = true, lookAtHero = false, talkThroughWalls = false;
  bool blend = true;
  long talkStart = 2, talkEnd = 1;
  long motion = 3;
  long nameText = 0;
  size_t line = 0;
};

struct Zone {
  std::string name;
  std::string on = "walk";
  long event = -1;
  bool area = false;
  double x = 0, y = 0, z = 0, radius = 0;
  std::string run;
  long args[4] = {0, 0, 0, 0};
  size_t line = 0;
};

struct Chest {
  std::string name;
  double x = 0, y = 0, z = 0, angle = 0;
  std::string item;
  long count = 1;
  std::string flag;
  std::string look = "chest";
  std::string kind = "item";
  size_t line = 0;
};

struct Door {
  std::string name;
  long like = -1;
  long type = -1, part = -1, collision = -1, openAnim = -2, closeAnim = -2;
  long resource = -1;
  std::string openFlag, closeFlag;
  size_t line = 0;
};

struct Music {
  bool given = false;
  bool none = false;
  std::string like;
  long bgm = -1, volume = 127, fade = 0, envFade = 0;
  std::string bgmName;
  std::string flag;
};

struct Room {
  std::vector<Person> people;
  bool replacePeople = false;
  std::vector<Zone> zones;
  std::vector<Chest> chests;
  std::vector<Door> doors;
  Music music;
};

bool Parse(const std::string& text, Room& out, std::vector<std::string>& problems);

struct Lookups {
  uint32_t floorId = 0;
  std::function<int(const std::string&)> function;
  std::function<bool(const std::string&, uint32_t&)> flag;
  std::function<bool(const std::string&, uint32_t&)> item;
  std::function<bool(long, std::vector<uint8_t>&)> doorRow;
  std::function<long(const std::string&)> song;
  std::function<bool(const std::string&, std::vector<std::vector<uint8_t>>&)> musicOf;
};

struct AreaZone {
  std::string name;
  float x = 0, y = 0, z = 0, radius = 0;
};

struct Built {
  std::vector<uint8_t> people;
  std::vector<std::string> personNames;
  std::vector<std::vector<uint8_t>> chests;
  std::vector<std::string> chestNames;
  std::vector<std::vector<uint8_t>> events;
  std::vector<std::string> eventNames;
  std::vector<std::vector<uint8_t>> doors;
  std::vector<std::string> doorNames;
  std::vector<std::vector<uint8_t>> music;
  std::vector<AreaZone> areas;
};

void Build(const Room& room, const Lookups& ask, Built& out, std::vector<std::string>& problems);

std::vector<uint8_t> MergePeople(const uint8_t* own, uint32_t n, const std::vector<uint8_t>& mod, uint32_t base, bool replace, std::string& problem);

void PutBe32(uint8_t* p, uint32_t v);
void PutBe16(uint8_t* p, uint32_t v);
void PutBeF32(uint8_t* p, float v);

}

#endif
