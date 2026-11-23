#ifndef XD_GEN57_H
#define XD_GEN57_H

#include "xd_gen4_xg.h"

#include <mods.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace xd::gen57 {

std::filesystem::path PackFolder();
bool PackPresent(std::string* why = nullptr);

void Prepare(bool wanted, bool spots, engine::mods::LoadOptions& options, std::vector<engine::mods::DataModFile>& dataFiles);
bool Loaded();
bool SpotsLoaded();
uint32_t ExtraRowCount(const char* table);
uint32_t ModelRowsTaken();
uint32_t WaveRowsTaken();
std::vector<std::string> FairyMoveNames();
void AfterLoad(const engine::mods::LoadResult& result);
void ApplyMainTables();

void AppendAbilityTexts(std::vector<std::pair<uint32_t, std::string>>& texts);
void InstallAbilityRows();
bool XgInfo(xd::gen4::xg::Gen57Info& out);
std::vector<std::string> AbilityNamesForXG();
const char* AbilityName(uint32_t id);

}

#endif
