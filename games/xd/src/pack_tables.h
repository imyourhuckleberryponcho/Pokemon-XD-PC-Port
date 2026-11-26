#ifndef XD_PACK_TABLES_H
#define XD_PACK_TABLES_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xd::pack {

struct PkxRow {
  uint32_t row = 0, group = 0, res = 0;
};
struct RowSpec {
  std::string table;
  uint32_t row = 0, count = 1;
  int32_t copyOf = -1;
  std::vector<uint32_t> sets;
};
struct SpeciesRef {
  uint32_t dex = 0, id = 0;
  std::string key;
};
struct GoodsRow {
  int32_t copyOf = -1;
  std::string item;
};

struct PackTables {
  std::vector<RowSpec> rows;
  std::vector<PkxRow> pkx;
  uint32_t pkxExpect = 0;
  std::vector<std::vector<uint8_t>> waves;
  std::vector<SpeciesRef> species;
  std::vector<GoodsRow> goods;
};

bool ReadText(const std::filesystem::path& p, std::string& out);
std::map<uint32_t, uint32_t>& TocGroupRemap();
bool ParseTables(const std::string& text, PackTables& out, std::string& error);

}

#endif
