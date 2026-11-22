#ifndef XD_SPECIES_H
#define XD_SPECIES_H

#include <mods.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::species {

struct Taken {
  uint32_t models = 0;
  uint32_t waves = 0;
  uint32_t sndFiles = 0;
  uint32_t sndData = 0;
  uint32_t flags = 0;
  uint32_t wzx = 0;
};
void SetTaken(const Taken& taken);

struct Cry {
  std::vector<uint8_t> header, body, wave;
  bool cut = false;
  double seconds = 0;
};
bool MakeCry(const std::vector<uint8_t>& wav, Cry& out, std::string& error);

void BuildSpecies(engine::mods::RowsJob& job);

}

#endif
