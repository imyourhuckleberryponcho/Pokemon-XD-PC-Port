#include "xd_added_rows.h"
#include "xd_gen4_abilities.h"
#include "xd_mod_battle.h"
#include "xd_gen57.h"
#include "xd_forms.h"
#include "xd_modern_types.h"
#include "xd_pc_trade.h"

#include "data/common_rel.h"
#include "data/xd_data_mods.h"
#include "dol_tables.h"
#include "xd_user_folder.h"

#include <sdk_port.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

extern "C" uint16_t pc_metronome_first_mod_move;
#ifdef XD_HAVE_METRONOME_NEVER_PICK
extern "C" uint16_t pc_metronome_never_pick[1024];
extern "C" uint16_t pc_metronome_never_pick_count;
#endif

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
    if (r.inFile) continue;
    rows.push_back({r.table.c_str(), r.id, r.row, r.copyOf, r.bytes.data(), r.bytes.size()});
  }
  PortXD_SetAddedRows(rows.data(), rows.size());
  uint16_t firstMove = 0;
  for (const auto& r : sRows) {
    if (r.table == "waza_data" && r.bytes.size() > 0x16 && r.bytes[0x16] != 0xA4 && r.mod != "gen4" && r.id <= 0xFFFF && (firstMove == 0 || r.id < firstMove)) {
      firstMove = static_cast<uint16_t>(r.id);
    }
  }
  std::vector<uint16_t> never;
  for (const auto& r : sRows) {
    if (r.table == "waza_data" && firstMove != 0 && r.id >= firstMove && r.id <= 0xFFFF && r.bytes.size() > 0x0C && r.bytes[0x0C] == 0 &&
        r.bytes[0x16] != 0xA4 && r.mod != "gen4" && never.size() < 1024) {
      never.push_back(static_cast<uint16_t>(r.id));
    }
  }
#ifdef XD_HAVE_METRONOME_NEVER_PICK
  for (size_t i = 0; i < never.size(); i++) pc_metronome_never_pick[i] = never[i];
  pc_metronome_never_pick_count = static_cast<uint16_t>(never.size());
#endif
  pc_metronome_first_mod_move = firstMove;
  if (firstMove != 0) std::fprintf(stderr, "[mods] Metronome: moves %u and up can be picked, %zu never (metronome: false)\n", firstMove, never.size());
  for (const auto& r : sRows) {
    std::fprintf(stderr, "[mods] added: %s \"%s\" = %s #%u (from the mod %s, copy of #%u)%s%s\n", r.table.c_str(), r.key.c_str(),
                 r.table == "pokemon_data" ? "species" : r.table == "waza_data" ? "move" : r.table == "poke_face" ? "face row" : r.table == "pokemon_pkx_data" ? "model row" : r.table == "snd_file" ? "cry file row" : r.table == "snd_data" ? "cry sound row" : r.table == "waza_wzx_data" ? "move animation row" : r.table == "pokemon_nakigoe_data" ? "cry waveform row" : r.table == "flag_data" ? "story flag" : r.table == "floordata" ? "room (floor)" : r.table == "floor_door_list" ? "door row" : r.table == "floor_tresure_list" ? "chest row" : r.table == "floor_event_list" ? "event zone row" : r.table == "floorsoundTbl" ? "music row" : r.table == "floor_chars" ? "room people" : r.table == "floor_zone" ? "area zone" : "item", r.id, r.mod.c_str(), r.copyOf, r.label.empty() ? "" : " label ", r.label.c_str());
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

extern "C" void* PortXD_LanguageBank(void* bank);
extern "C" const char* PortXD_GetLanguageCode(void);
extern "C" void* PortXD_MsgOpen(void* bank) {
  void* result = GSmsgOpen(PortXD_LanguageBank(bank));
  std::vector<std::pair<uint32_t, std::string>> extra;
  xd::gen4::AppendAbilityTexts(extra);
  xd::modbattle::AppendTexts(extra);
  xd::gen57::AppendAbilityTexts(extra);
  xd::forms::AppendTexts(extra);
  xd::modern::AppendTexts(extra);
  PortXD_Gen4MoveTexts(
      [](void* ctx, uint32_t id, const char* text) {
        static_cast<std::vector<std::pair<uint32_t, std::string>>*>(ctx)->emplace_back((xd::data::kAddedTextGroup << 20) | id, text);
      },
      &extra);
  if (!sBankOpened && PortXD_PcTradeAvailable()) extra.emplace_back((xd::data::kAddedTextGroup << 20) | 0xFFE00u, "Trade with a friend online");
  if ((!sTexts.empty() || !extra.empty()) && !sBankOpened) {
    sBankOpened = true;
    std::vector<std::pair<uint32_t, std::string>> texts;
    const char* lang = PortXD_GetLanguageCode();
    for (const auto& t : sTexts) {
      const auto l = lang != nullptr ? t.lang.find(lang) : t.lang.end();
      texts.emplace_back(t.key, l != t.lang.end() ? l->second : t.text);
    }
    for (auto& t : extra) texts.push_back(std::move(t));
    std::stable_sort(texts.begin(), texts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    xd::gen4::InstallAbilityRows();
    xd::modbattle::InstallAbilityRows();
    xd::gen57::InstallAbilityRows();
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
