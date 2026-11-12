#ifndef XD_DATA_MODS_H
#define XD_DATA_MODS_H

#include <data_mods.h>

#include <string>

namespace xd::data {

bool BuildDataCatalog(const engine::mods::EntryReader& read, engine::mods::DataCatalog& out, std::string& error);

void AddDeckTables(const engine::mods::EntryReader& read, engine::mods::DataCatalog& out);

bool DolAddressToOffset(const uint8_t* dol, size_t size, uint32_t address, uint32_t bytes, uint32_t& offset);
bool DolOffsetToAddress(const uint8_t* dol, size_t size, uint32_t offset, uint32_t& address);

}

#endif
