#include "xd_added_rows.h"
#include "xd_gen4_abilities.h"

#include "data/common_rel.h"
#include "data/xd_data_mods.h"
#include "dol_tables.h"
#include "xd_user_folder.h"

#include <sdk_port.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace {

std::vector<engine::mods::AddedRow> sRows;
std::vector<engine::mods::AddedText> sTexts;
std::map<std::string, uint64_t> sDigests;
bool sBankOpened = false;

}

namespace xd::added {

void Install(const engine::mods::LoadResult& result) {
  sRows = result.addedRows;
  sTexts = result.addedTexts;
  sDigests = engine::mods::AddedRowsDigests(sRows);
  std::sort(sTexts.begin(), sTexts.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  if (sRows.empty()) {
    return;
  }
  std::vector<XDAddedRow> rows;
  for (const auto& r : sRows) {
    rows.push_back({r.table.c_str(), r.id, r.row, r.copyOf, r.bytes.data(), r.bytes.size()});
  }
  PortXD_SetAddedRows(rows.data(), rows.size());
  for (const auto& r : sRows) {
    std::fprintf(stderr, "[mods] added: %s \"%s\" = %s #%u (from the mod %s, copy of #%u)\n", r.table.c_str(), r.key.c_str(),
                 r.table == "pokemon_data" ? "species" : r.table == "waza_data" ? "move" : "item", r.id, r.mod.c_str(), r.copyOf);
  }
  if (!xd::PlayerFilesReadOnly()) {
    std::ofstream out(xd::SettingsFolder() / L"added_ids.json", std::ios::binary | std::ios::trunc);
    out << engine::mods::AddedRowsJson(sRows);
  }
}

const std::vector<engine::mods::AddedRow>& Rows() { return sRows; }
const std::map<std::string, uint64_t>& Digests() { return sDigests; }

}

extern "C" void* GSmsgOpen(void* bank);
extern "C" void PortXD_Gen4MoveTexts(void (*add)(void* ctx, uint32_t id, const char* text), void* ctx);

extern "C" void* PortXD_MsgOpen(void* bank) {
  void* result = GSmsgOpen(bank);
  std::vector<std::pair<uint32_t, std::string>> extra;
  xd::gen4::AppendAbilityTexts(extra);
  PortXD_Gen4MoveTexts(
      [](void* ctx, uint32_t id, const char* text) {
        static_cast<std::vector<std::pair<uint32_t, std::string>>*>(ctx)->emplace_back((xd::data::kAddedTextGroup << 20) | id, text);
      },
      &extra);
  if ((!sTexts.empty() || !extra.empty()) && !sBankOpened) {
    sBankOpened = true;
    std::vector<std::pair<uint32_t, std::string>> texts;
    for (const auto& t : sTexts) texts.emplace_back(t.key, t.text);
    for (auto& t : extra) texts.push_back(std::move(t));
    xd::gen4::InstallAbilityRows();
    std::vector<uint8_t> bytes;
    std::string error;
    const uint8_t* like = PortXD_GetDolTable("common_tbl");
    if (!xd::data::BuildMessageBank(xd::data::kAddedTextGroup, texts, like, bytes, error)) {
      std::fprintf(stderr, "[mods] problem: the names of the added rows could not be made: %s\n", error.c_str());
    } else {
      auto* bank = static_cast<uint8_t*>(PortMem_LowAlloc(bytes.size() + 0x10, 16));
      std::memcpy(bank, bytes.data(), bytes.size());
      GSmsgOpen(bank);
      std::fprintf(stderr, "[mods] message bank %u opened: %zu texts of added rows\n", xd::data::kAddedTextGroup, texts.size());
    }
  }
  return result;
}
