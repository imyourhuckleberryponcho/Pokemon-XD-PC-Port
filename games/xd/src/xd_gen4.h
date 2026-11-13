#ifndef XD_GEN4_H
#define XD_GEN4_H

#include <mods.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::gen4 {

std::filesystem::path PackFolder();
bool PackPresent(std::string* why = nullptr);

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles);

void AfterLoad(const engine::mods::LoadResult& result);

bool Loaded();
bool SpotsLoaded();

void ApplyMainTables();

struct ExtraEvolution {
  uint16_t species;
  uint8_t kind;
  uint16_t param;
  uint16_t target;
};
void SetExtraEvolutions(const std::vector<ExtraEvolution>& list, int32_t linkingCord);

void SetEeveeChoices(bool on, uint32_t leafSpecies, uint32_t leafItem, uint32_t iceSpecies, uint32_t iceItem);

}

extern "C" void PortXD_Gen4MainTables(void);

#endif
