#include "xd_challenges.h"
#include "xd_gen4_abilities.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern uint8_t pokemon_tokusei_data[];
extern uint32_t pokemon_tokusei_data_number;
}

namespace {

struct Ability {
  uint8_t id;
  const char* name;
  const char* text;
};

constexpr uint32_t kGroup = 0x7D0;
constexpr uint32_t kFirstText = 0x8000;
constexpr uint8_t kFirst = 77, kLast = 123;

constexpr Ability kAbilities[] = {
    {77, "Tangled Feet", "Raises evasion if confused."},
    {78, "Motor Drive", "Electric moves raise Speed."},
    {79, "Rivalry", "Stronger against the same gender."},
    {80, "Steadfast", "Flinching raises Speed."},
    {81, "Snow Cloak", "Raises evasion in a hailstorm."},
    {82, "Gluttony", "Eats berries earlier."},
    {83, "Anger Point", "Maxes Attack when hit critically."},
    {84, "Unburden", "Speed rises when its item is used."},
    {85, "Heatproof", "Halves damage from Fire moves."},
    {86, "Simple", "Doubles stat changes."},
    {87, "Dry Skin", "Water heals, Fire hurts more."},
    {88, "Download", "Raises Attack or Sp. Atk on entry."},
    {89, "Iron Fist", "Boosts punching moves."},
    {90, "Poison Heal", "Poison restores HP."},
    {91, "Adaptability", "Boosts same-type moves further."},
    {92, "Skill Link", "Multi-hit moves hit the most."},
    {93, "Hydration", "Heals status in rain."},
    {94, "Solar Power", "Boosts Sp. Atk in sunshine."},
    {95, "Quick Feet", "Boosts Speed with a status problem."},
    {96, "Normalize", "All its moves become Normal."},
    {97, "Sniper", "Critical hits do more damage."},
    {98, "Magic Guard", "Only attacks can hurt it."},
    {99, "No Guard", "Its moves and moves against it hit."},
    {100, "Stall", "Always moves last."},
    {101, "Technician", "Boosts weak moves."},
    {102, "Leaf Guard", "Prevents status in sunshine."},
    {103, "Klutz", "Cannot use its held item."},
    {104, "Mold Breaker", "Ignores abilities that block moves."},
    {105, "Super Luck", "Critical hits land more often."},
    {106, "Aftermath", "Damages the foe that knocks it out."},
    {107, "Anticipation", "Senses dangerous moves."},
    {108, "Forewarn", "Reveals a foe's strongest move."},
    {109, "Unaware", "Ignores the foe's stat changes."},
    {110, "Tinted Lens", "Powers up weak hits."},
    {111, "Filter", "Weakens super effective hits."},
    {112, "Slow Start", "Attack and Speed halved at first."},
    {113, "Scrappy", "Hits Ghost types with Normal moves."},
    {114, "Storm Drain", "Draws in Water moves."},
    {115, "Ice Body", "Heals in a hailstorm."},
    {116, "Solid Rock", "Weakens super effective hits."},
    {117, "Snow Warning", "Summons a hailstorm."},
    {118, "Honey Gather", "May gather Honey after battle."},
    {119, "Frisk", "Checks the foe's held item."},
    {120, "Reckless", "Boosts recoil moves."},
    {121, "Multitype", "Changes type with its plate."},
    {122, "Flower Gift", "Boosts allies in sunshine."},
    {123, "Bad Dreams", "Hurts sleeping foes."},
};

}

namespace xd::gen4 {

bool AbilityTextsWanted() { return xd::challenges::Active("gen4_rules"); }

constexpr const char* kMessages[] = {
    "{0F} was hurt by its Solar Power!",
    "{0F} was hurt by its Dry Skin!",
    "{0F} is tormented by Bad Dreams!",
    "{0F} was hurt by Aftermath!",
    "{0F} restored HP with its Dry Skin!",
    "{0F} restored HP with its Ice Body!",
    "{0F} restored HP with its Poison Heal!",
    "{0F}'s Hydration cured its status problem!",
    "{0F} shuddered with Anticipation!",
    "{0F} frisked the foe and found a held item!",
    "{0F} was warned by Forewarn!",
};

void AppendAbilityTexts(std::vector<std::pair<uint32_t, std::string>>& texts) {
  if (!AbilityTextsWanted()) return;
  for (const Ability& a : kAbilities) {
    std::string shown = a.name;
    for (char& c : shown) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    texts.emplace_back((kGroup << 20) | (kFirstText + a.id * 2), shown);
    texts.emplace_back((kGroup << 20) | (kFirstText + a.id * 2 + 1), a.text);
  }
  for (size_t i = 0; i < sizeof kMessages / sizeof kMessages[0]; i++) texts.emplace_back((kGroup << 20) | (0x9000 + i), kMessages[i]);
}

void InstallAbilityRows() {
  if (!AbilityTextsWanted()) return;
  static bool done = false;
  if (done) return;
  done = true;
  const uint32_t old = pokemon_tokusei_data_number;
  if (old < kFirst || old > kFirst + 1) {
    std::fprintf(stderr, "[gen4] ability table has %u rows, not the 78 expected: ability names not added\n", old);
    return;
  }
  auto* rows = reinterpret_cast<uint32_t*>(pokemon_tokusei_data);
  for (const Ability& a : kAbilities) {
    uint32_t* row = rows + 3 * a.id;
    row[0] = 0;
    row[1] = (kGroup << 20) | (kFirstText + a.id * 2);
    row[2] = (kGroup << 20) | (kFirstText + a.id * 2 + 1);
  }
  pokemon_tokusei_data_number = kLast + 1;
  std::fprintf(stderr, "[gen4] ability table grown: %u -> %u rows (names and descriptions of Diamond/Pearl abilities)\n", old,
               pokemon_tokusei_data_number);
}

}
