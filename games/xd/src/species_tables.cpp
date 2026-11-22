#include "xd_spare.h"
#include "xd_added_rows.h"

#include <sdk_port.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
struct SpeciesPkxEntry {
  uint32_t a, b;
};
extern SpeciesPkxEntry pokemon_pkx_data[];
extern uint32_t pokemon_pkx_data_number;
extern void* pokemon_nakigoe_data[];
extern uint32_t pokemon_nakigoe_data_number;
struct MoveWzxEntry {
  uint16_t res[3];
};
extern MoveWzxEntry waza_wzx_data[];
extern uint32_t waza_wzx_data_number;
}

namespace {
uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
constexpr uint32_t kDiscModels = 419, kDiscWaves = 389, kDiscWzx = 373, kSpare = XD_PC_SPARE_SPECIES;
}

extern "C" void PortXD_ModSpeciesMainTables(void) {
  uint32_t models = 0, waves = 0, wzx = 0;
  for (const auto& r : xd::added::Rows()) {
    if (r.table == "waza_wzx_data") {
      if (r.row < kDiscWzx || r.row >= kDiscWzx + kSpare || r.bytes.size() != 6) {
        std::fprintf(stderr, "[mods] the animation row %u doesn't fit the game's animation table: left out\n", r.row);
        continue;
      }
      MoveWzxEntry e = {{0, 0, 0}};
      for (int part = 0; part < 3; part++) {
        const uint32_t src = static_cast<uint32_t>(r.bytes[2 * part]) << 8 | r.bytes[2 * part + 1];
        if (src != 0xFFFF && (src < r.row || src < kDiscWzx)) e.res[part] = waza_wzx_data[src].res[part];
      }
      waza_wzx_data[r.row] = e;
      wzx = std::max(wzx, r.row + 1);
      std::fprintf(stderr, "[mods] animation row %u: pieces %u / %u / %u\n", r.row, e.res[0], e.res[1], e.res[2]);
    } else if (r.table == "pokemon_pkx_data") {
      if (r.row < kDiscModels || r.row >= kDiscModels + kSpare || r.bytes.size() != 8) {
        std::fprintf(stderr, "[mods] the model row %u doesn't fit the game's model table: left out\n", r.row);
        continue;
      }
      pokemon_pkx_data[r.row].a = Be32(r.bytes.data());
      pokemon_pkx_data[r.row].b = Be32(r.bytes.data() + 4);
      models = std::max(models, r.row + 1);
    } else if (r.table == "pokemon_nakigoe_data") {
      if (r.row < kDiscWaves || r.row >= kDiscWaves + kSpare || r.bytes.empty() || r.bytes.size() != static_cast<size_t>(r.bytes[0]) + 1) {
        std::fprintf(stderr, "[mods] the cry waveform row %u doesn't fit the game's table: left out\n", r.row);
        continue;
      }
      auto* p = static_cast<uint8_t*>(PortMem_LowAlloc(r.bytes.size(), 16));
      std::memcpy(p, r.bytes.data(), r.bytes.size());
      pokemon_nakigoe_data[r.row] = p;
      waves = std::max(waves, r.row + 1);
    }
  }
  if (models > pokemon_pkx_data_number) {
    std::fprintf(stderr, "[mods] pokemon_pkx_data grown: %u -> %u rows (species mods)\n", pokemon_pkx_data_number, models);
    pokemon_pkx_data_number = models;
  }
  if (wzx > waza_wzx_data_number) {
    std::fprintf(stderr, "[mods] waza_wzx_data grown: %u -> %u rows (move animations of mods)\n", waza_wzx_data_number, wzx);
    waza_wzx_data_number = wzx;
  }
  if (waves > pokemon_nakigoe_data_number) {
    std::fprintf(stderr, "[mods] pokemon_nakigoe_data grown: %u -> %u rows (species mods)\n", pokemon_nakigoe_data_number, waves);
    pokemon_nakigoe_data_number = waves;
  }
}
