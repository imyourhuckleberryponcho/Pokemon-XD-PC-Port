#include "../src/data/common_rel.h"

#include <gs_formats.h>
#include <port_disc.h>

#include <game/pxdvs/app/pokemon/pokemonDB.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int code_refs() {
  size_t size = 0;
  uint8_t* file = PortDisc_ReadFile("common.fsys", &size);
  FsysArchive fsys;
  int index = -1;
  if (file == nullptr || !Fsys_Open(file, size, &fsys) || (index = Fsys_Find(&fsys, "common_rel")) < 0) {
    std::fprintf(stderr, "FAILED: common_rel not found\n");
    return 1;
  }
  uint32_t relSize = 0;
  uint8_t* data = Fsys_Extract(&fsys, (uint32_t)index, &relSize);
  RelModule rel{};
  if (data == nullptr || !Rel_Parse(data, relSize, &rel)) {
    std::fprintf(stderr, "FAILED: common_rel doesn't parse\n");
    return 1;
  }
  for (uint32_t i = 0; i < rel.numRelocs; i++) {
    const RelReloc& r = rel.relocs[i];
    if (r.module == 0) {
      std::printf("%u %08X %08X %u\n", r.section, r.offset, r.addend, r.type);
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [first] [last] | --code-refs\n", argv[0]);
    return 2;
  }
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  if (argc >= 3 && std::strcmp(argv[2], "--code-refs") == 0) {
    return code_refs();
  }
  XDCommonTables tables;
  if (!PortXD_LoadCommonRel(&tables, error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  std::printf("common_rel fills %u table variables; pokemon_data has %u entries (%zu bytes each on PC, 292 on GameCube)\n",
              tables.tablesFound, tables.pokemon_data_number, sizeof(PokemonData));

  const unsigned first = argc >= 3 ? (unsigned)std::atoi(argv[2]) : 1;
  const unsigned last = argc >= 4 ? (unsigned)std::atoi(argv[3]) : first + 5;
  std::printf("%4s %-8s %5s %5s %5s %5s %5s %5s  %-7s %-9s %s\n", "#", "name id", "HP", "Atk", "Def", "SpA", "SpD",
              "Spe", "types", "abilities", "evolves");
  for (unsigned i = first; i <= last && i < tables.pokemon_data_number; i++) {
    const PokemonData& p = tables.pokemon_data[i];
    const BasisStatus& s = p.basisStatus[0];
    std::printf("%4u %-8u %5u %5u %5u %5u %5u %5u  %3u/%-3u %4u/%-4u", i, (unsigned)(uintptr_t)p.name, s.maxHp,
                s.phyAtk, s.phyDef, s.speAtk, s.speDef, s.nimbleness, p.zokuseiDataId[0], p.zokuseiDataId[1],
                p.tokuseiDataId[0], p.tokuseiDataId[1]);
    if (p.sinka[0].kind != 0) {
      std::printf(" -> #%u (method %u, value %u)", p.sinka[0].pokemonDataId, p.sinka[0].kind, p.sinka[0].buff);
    }
    std::printf("\n");
  }
  return 0;
}
