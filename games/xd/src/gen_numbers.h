#ifndef XD_GEN_NUMBERS_H
#define XD_GEN_NUMBERS_H

#include <cstddef>
#include <cstdint>

namespace xd::gennum {

struct SpeciesDiff {
  uint16_t national;
  uint16_t gameId;
  const char* name;
  uint16_t stat[6];
  uint8_t type[2];
};
struct MoveDiff {
  uint16_t id;
  const char* name;
  uint8_t type;
  uint16_t power;
  uint8_t pp;
  uint8_t accuracy;
};
struct GenTable {
  int gen;
  const SpeciesDiff* species;
  size_t nSpecies;
  const MoveDiff* moves;
  size_t nMoves;
};
const GenTable* Table(int gen);

}
#endif
