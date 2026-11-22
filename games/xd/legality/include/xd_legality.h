#ifndef XD_LEGALITY_H
#define XD_LEGALITY_H

#include <xd_save.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace xd::legality {

enum class Level { Safe, Legal };

struct LevelUpMove {
  uint8_t level = 0;
  uint16_t move = 0;
};

struct Evolution {
  uint8_t kind = 0;
  uint16_t param = 0;
  uint16_t to = 0;
};

struct SpeciesRules {
  bool exists = false;
  uint16_t national = 0;
  uint8_t growth = 0;
  uint8_t abilities[2] = {0, 0};
  uint16_t baseStats[6] = {};
  std::array<uint8_t, 58> machine{};
  std::array<uint8_t, 12> tutor{};
  std::vector<LevelUpMove> levelUp;
  std::vector<uint16_t> eggMoves;
  std::vector<Evolution> evolutions;
  uint16_t evolvesFrom = 0;
};

struct MoveRules {
  bool exists = false;
  bool shadow = false;
  uint8_t basePp = 0;
};

struct ItemRules {
  bool exists = false;
  uint16_t soubi = 0;
};

struct Rules {
  std::vector<SpeciesRules> species;
  std::vector<MoveRules> moves;
  std::vector<ItemRules> items;
  std::array<uint16_t, 58> machineMove{};
  std::array<uint16_t, 12> tutorMove{};
  std::vector<std::array<uint32_t, 101>> expCurves;
  uint32_t vanillaSpecies = 0, vanillaMoves = 0, vanillaItems = 0;
  uint32_t gen4Species = 107, gen4Moves = 113, gen4Items = 64;
  std::vector<std::pair<uint16_t, uint16_t>> giftMoves;
  bool eggsExist = false;
  uint32_t maxEvTotal = 510;
  std::string gameName = "Pokemon XD";
  std::function<std::string(uint16_t)> speciesName, moveName, itemName;

  bool Gen4On() const {
    return species.size() > vanillaSpecies || moves.size() > vanillaMoves || items.size() > vanillaItems;
  }
};

struct Context {
  const xd::save::ShadowEntry* shadow = nullptr;
  bool shadowStaysInItsSave = false;
  bool movesNotJudged = false;
};

struct Finding {
  Level level;
  std::string code;
  std::string text;
};

struct Result {
  std::vector<Finding> findings;
  bool Ok() const { return findings.empty(); }
  std::string Text(const char* separator = "; ") const;
};

Result Check(const xd::save::Pokemon& p, const Rules& rules, Level level, const Context& context = {});

bool ExpForLevel(const Rules& rules, uint8_t growth, int level, uint32_t& exp);

bool ExpectedStats(const Rules& rules, const xd::save::Pokemon& p, std::array<uint16_t, 6>& stats);

}

#endif
