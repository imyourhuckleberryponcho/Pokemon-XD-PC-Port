#ifndef XD_DATA_MODS_H
#define XD_DATA_MODS_H

#include <data_mods.h>
#include <mods.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::data {

bool BuildDataCatalog(const engine::mods::EntryReader& read, engine::mods::DataCatalog& out, std::string& error);

bool CheckModEntry(int fsysType, const std::string& mod, std::vector<uint8_t>& data, const std::vector<engine::mods::AddedRow>& rows,
                   std::string& why);
void BuildRooms(engine::mods::RowsJob& job);
bool CheckMessageText(const std::string& text, std::string& why);
void SetFlagRowsTaken(uint32_t count);

void AddDeckTables(const engine::mods::EntryReader& read, engine::mods::DataCatalog& out);

inline constexpr uint32_t kAddedTextGroup = 0x7D0;

bool BuildMessageBank(uint32_t group, const std::vector<std::pair<uint32_t, std::string>>& texts, const uint8_t* like,
                      std::vector<uint8_t>& out, std::string& error);

bool DolAddressToOffset(const uint8_t* dol, size_t size, uint32_t address, uint32_t bytes, uint32_t& offset);
bool DolOffsetToAddress(const uint8_t* dol, size_t size, uint32_t offset, uint32_t& address);

}

#endif
