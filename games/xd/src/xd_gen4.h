#ifndef XD_GEN4_H
#define XD_GEN4_H

#include "pack_tables.h"

#include <mods.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xd::gen4 {

std::filesystem::path PackFolder();
bool PackPresent(std::string* why = nullptr);
void SetGen57Wanted(bool wanted);

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles);

bool TranslateForXG(const engine::mods::DataCatalog& catalog, const engine::mods::EntryReader& read, std::vector<engine::mods::DataModFile>& files,
                    std::string& error);

void AfterLoad(const engine::mods::LoadResult& result);

bool Loaded();
uint32_t ExtraRowCount(const char* table);
uint32_t ModelRowsTaken();
uint32_t WaveRowsTaken();
bool SpotsLoaded();

void SetMoreRows(const std::vector<xd::pack::RowSpec>& rows);

void ApplyMainTables();

struct ExtraEvolution {
  uint16_t species;
  uint8_t kind;
  uint16_t param;
  uint16_t target;
};
void SetExtraEvolutions(const std::vector<ExtraEvolution>& list, int32_t linkingCord);
const std::vector<ExtraEvolution>& ExtraEvolutions();

void SetEeveeChoices(bool on, uint32_t leafSpecies, uint32_t leafItem, uint32_t iceSpecies, uint32_t iceItem);
bool EeveeOfferSpecies(uint32_t& leaf, uint32_t& ice);

}

extern "C" void PortXD_Gen4MainTables(void);

#endif
