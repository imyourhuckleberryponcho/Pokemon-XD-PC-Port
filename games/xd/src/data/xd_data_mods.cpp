#include "xd_data_mods.h"

#include "dol_tables.h"

#include <data_table.h>
#include <gs_formats.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>

extern "C" size_t PortXD_DescribedTables(const TableDesc* const** out);

namespace xd::data {

namespace em = engine::mods;

namespace {

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

struct RelGlobal {
  uint32_t address;
  const char* name;
};
const RelGlobal kRelGlobals[] = {
#define RELGLOBAL(addr, name) {addr, #name},
#include "relglobal_vars.inc"
#undef RELGLOBAL
};
const char* relglobal_name(uint32_t address) {
  for (const auto& g : kRelGlobals) {
    if (g.address == address) {
      return g.name;
    }
  }
  return nullptr;
}

struct Alias {
  const char* table;
  const char* alias;
};
const Alias kAliases[] = {
    {"pokemon_data", "pokemon"},     {"waza_data", "moves"},    {"item_data_prime", "items"},
    {"zokusei_data", "types"},       {"pokemon_seikaku_data", "natures"},
    {"story_trainers", "trainers"},  {"story_pokemon", "trainer_pokemon"}, {"darkpokemon_shadow", "shadow_pokemon"},
};
struct Values {
  const char* table;
  const char* field;
  const char* list;
};
const Values kValues[] = {
    {"pokemon_data", "zokuseiDataId", "type"},    {"pokemon_data", "itemDataId", "item"},
    {"pokemon_data", "kowaza", "move"},           {"pokemon_data", "waza.dataId", "move"},
    {"pokemon_data", "sinka.pokemonDataId", "species"},
    {"waza_data", "zokuseiDataId", "type"},
    {"esabapokemondata_E1", "id", "species"},     {"esabapokemondata_E2", "id", "species"},
    {"esabapokemondata_E3", "id", "species"},     {"esabapokemondata_EX", "id", "species"},
    {"*_pokemon", "pokemonID", "species"},        {"*_pokemon", "holdItem", "item"},
    {"*_pokemon", "wazaID", "move"},              {"*_pokemon", "seikakuID", "nature"},
    {"*_shadow", "darkWaza", "move"},             {"*_trainers", "trainerItem", "item"},
    {"darkPokemonList", "pokemonID", "species"},  {"item_waza_machine_no", "wazaID", "move"},
};
bool matches(const char* pattern, const std::string& table) {
  if (pattern[0] != '*') {
    return table == pattern;
  }
  const size_t n = std::strlen(pattern + 1);
  return table.size() >= n && table.compare(table.size() - n, n, pattern + 1) == 0;
}

em::DataField from_desc(const FieldDesc& d) {
  em::DataField f;
  f.name = d.name;
  f.offset = d.gcOffset;
  f.count = d.count;
  f.stride = d.gcStride;
  switch (d.kind) {
  case FIELD_U8: f.size = 1; break;
  case FIELD_U16: f.size = 2; break;
  case FIELD_U32:
  case FIELD_U32_IN_PTR: f.size = 4; break;
  case FIELD_PTR:
  case FIELD_ADDR32:
    f.size = 4;
    f.editable = false;
    f.note = "an address: the port fills it in";
    break;
  }
  return f;
}

struct Bank {
  const uint8_t* data;
  size_t size;
};

struct CommonRel {
  std::vector<uint8_t> file;
  RelModule rel{};
  bool ok = false;
  ~CommonRel() {
    if (ok) Rel_Free(&rel);
  }
};

size_t section_offset(const CommonRel& c, uint8_t section) {
  return static_cast<size_t>(c.rel.sections[section].data - c.file.data());
}

void add_common_rel(CommonRel& c, em::DataCatalog& out, std::vector<Bank>& banks) {
  RelPrologStore all[512];
  const int n = Rel_ReadPrologStores(&c.rel, all, 512);
  if (n < 0) {
    return;
  }
  std::map<std::string, RelPrologStore> stores;
  for (int i = 0; i < n; i++) {
    const char* name = relglobal_name(all[i].dolAddress);
    if (name != nullptr && all[i].isRelAddress && all[i].section < c.rel.numSections && c.rel.sections[all[i].section].data != nullptr) {
      stores.emplace(name, all[i]);
    }
  }
  const auto count_of = [&](const std::string& table, uint32_t& count) {
    const auto it = stores.find(table + "_number");
    if (it == stores.end()) return false;
    const RelSection& s = c.rel.sections[it->second.section];
    if (it->second.offset + 4 > s.size) return false;
    count = be32(s.data + it->second.offset);
    return true;
  };
  const auto extent_of = [&](const RelPrologStore& at) {
    uint32_t next = c.rel.sections[at.section].size;
    for (const auto& [name, s] : stores) {
      if (s.section == at.section && s.offset > at.offset) next = std::min(next, s.offset);
    }
    return next - at.offset;
  };

  const TableDesc* const* descs = nullptr;
  const size_t numDescs = PortXD_DescribedTables(&descs);
  for (size_t k = 0; k < numDescs; k++) {
    const TableDesc* d = descs[k];
    const auto it = stores.find(d->name);
    if (it == stores.end()) continue;
    em::DataTable t;
    t.name = d->name;
    t.entrySize = d->gcSize;
    if (!count_of(t.name, t.count)) {
      t.count = extent_of(it->second) / d->gcSize;
    }
    if (static_cast<uint64_t>(it->second.offset) + static_cast<uint64_t>(t.count) * t.entrySize > c.rel.sections[it->second.section].size) {
      continue;
    }
    t.places.push_back({"common.fsys", "common_rel", static_cast<uint32_t>(section_offset(c, it->second.section) + it->second.offset)});
    bool anyEditable = false;
    for (uint32_t i = 0; i < d->numFields; i++) {
      t.fields.push_back(from_desc(d->fields[i]));
      anyEditable |= t.fields.back().editable;
    }
    if (!anyEditable) continue;
    if (t.name == "pokemon_data") {
      static const char* const kStats[] = {"maxHp", "phyAtk", "phyDef", "speAtk", "speDef", "nimbleness"};
      for (uint32_t set = 0; set < 2; set++) {
        for (uint32_t s = 0; s < 6; s++) {
          em::DataField f;
          f.name = "basisStatus[" + std::to_string(set) + "]." + kStats[s];
          f.size = 2;
          f.offset = 0x8E + set * 12 + s * 2;
          f.note = "the same number as basisStatus[" + std::to_string(set * 6 + s) + "]";
          t.fields.push_back(f);
        }
      }
      t.entryNames = "species";
    } else if (t.name == "waza_data") {
      t.entryNames = "move";
    } else if (t.name == "item_data_prime") {
      t.entryNames = "item_entry";
    } else if (t.name == "zokusei_data") {
      t.entryNames = "type";
    } else if (t.name == "pokemon_seikaku_data") {
      t.entryNames = "nature";
    }
    out.tables.push_back(std::move(t));
  }

  const auto res = stores.find("table_res");
  if (res != stores.end()) {
    const RelReloc* r = Rel_FindReloc(&c.rel, res->second.section, res->second.offset + 4);
    if (r != nullptr && r->type == REL_R_ADDR32 && r->module == c.rel.id && r->targetSection < c.rel.numSections &&
        c.rel.sections[r->targetSection].data != nullptr && r->addend + 0x10 <= c.rel.sections[r->targetSection].size) {
      banks.push_back({c.rel.sections[r->targetSection].data + r->addend, c.rel.sections[r->targetSection].size - r->addend});
    }
  }
}

std::string message(const std::vector<Bank>& banks, uint32_t key) {
  if (key == 0) return "";
  const uint32_t group = key >> 20, sub = key & 0xFFFFF;
  for (const Bank& b : banks) {
    if (be16(b.data) != group) continue;
    const uint32_t count = be16(b.data + 4);
    if (b.size != 0 && 0x10 + static_cast<size_t>(count) * 8 > b.size) continue;
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
      const uint32_t mid = (lo + hi) >> 1;
      const uint32_t k = be32(b.data + 0x10 + mid * 8);
      if (k == sub) {
        const uint32_t at = be32(b.data + 0x10 + mid * 8 + 4);
        std::string out;
        for (size_t i = 0; i < 256; i++) {
          if (b.size != 0 && at + i * 2 + 2 > b.size) break;
          const uint8_t* p = b.data + at + i * 2;
          const uint16_t ch = be16(p);
          if (ch == 0) break;
          if (ch == 0xFFFF) {
            break;
          }
          if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
          } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | ch >> 6));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | ch >> 12));
            out.push_back(static_cast<char>(0x80 | (ch >> 6 & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
          }
        }
        return out;
      }
      if (k < sub) lo = mid + 1;
      else hi = mid;
    }
  }
  return "";
}

const em::DataTable* table_named(const em::DataCatalog& c, const char* name) {
  for (const em::DataTable& t : c.tables) {
    if (t.name == name) return &t;
  }
  return nullptr;
}

void add_names(em::DataCatalog& out, const CommonRel& c, const std::vector<Bank>& banks, const char* table,
               uint32_t nameOffset, const char* list) {
  const em::DataTable* t = table_named(out, table);
  if (t == nullptr || nameOffset + 4 > t->entrySize) return;
  em::NameList names{list, {}};
  for (uint32_t i = 0; i < t->count; i++) {
    const std::string n = message(banks, be32(c.file.data() + t->places[0].offset + i * t->entrySize + nameOffset));
    if (!n.empty()) names.names.emplace_back(n, i);
  }
  out.lists.push_back(std::move(names));
}

struct DeckChunk {
  std::string id;
  uint32_t offset, size, count;
};
bool read_chunks(const std::vector<uint8_t>& d, std::vector<DeckChunk>& out) {
  if (d.size() < 0x10 || std::memcmp(d.data(), "DECK", 4) != 0) return false;
  const uint32_t end = std::min<uint32_t>(be32(d.data() + 4), static_cast<uint32_t>(d.size()));
  for (uint32_t p = 0x10; p + 0x10 <= end;) {
    const uint32_t size = be32(d.data() + p + 4);
    if (size < 0x10 || p + size > end) return false;
    out.push_back({std::string(reinterpret_cast<const char*>(d.data() + p), 4), p, size, be32(d.data() + p + 8)});
    p += size;
  }
  return true;
}

em::DataField field(const char* name, uint8_t size, uint32_t offset, uint32_t count = 1, uint8_t shift = 0, uint8_t width = 0,
                    const char* note = "") {
  em::DataField f;
  f.name = name;
  f.size = size;
  f.offset = offset;
  f.count = count;
  f.bitShift = shift;
  f.bitWidth = width;
  f.note = note;
  return f;
}

std::vector<em::DataField> trainer_fields() {
  std::vector<em::DataField> f = {
      field("msgOffset", 2, 0x00, 1, 0, 0, "where the trainer's ID text starts (DSTR)"),
      field("unk2", 1, 0x02, 2),
      field("darkPokemonFlags", 1, 0x04, 1, 0, 0, "bit i (1, 2, 4, 8, 16, 32) on: pokemonDataID[i] is a shadow_pokemon number"),
      field("trainerKind", 1, 0x05),
      field("trainerName", 2, 0x06, 1, 0, 0, "a message ID"),
      field("trainerItem", 2, 0x08, 4),
      field("trainerPKX", 2, 0x10),
      field("introWZX", 2, 0x12),
      field("trainerMsg", 2, 0x14, 4, 0, 0, "message IDs"),
      field("pokemonDataID", 2, 0x1C, 6, 0, 0, "the team: entry numbers of this deck's _pokemon table (or shadow_pokemon)"),
      field("aiID", 2, 0x28, 1, 0, 0, "entry number of this deck's _ai table"),
      field("fixedActionID", 2, 0x2A),
      field("comboID", 2, 0x2C, 4),
      field("comboActive", 1, 0x34, 4),
  };
  f[0].editable = false;
  return f;
}
std::vector<em::DataField> pokemon_fields() {
  return {
      field("pokemonID", 2, 0x00, 1, 0, 0, "species number"),
      field("level", 1, 0x02),
      field("friendLevel", 1, 0x03),
      field("holdItem", 2, 0x04),
      field("role", 1, 0x06),
      field("keyPlayer", 1, 0x07),
      field("statusRnd", 1, 0x08, 6, 0, 0, "individual values (HP, Atk, Def, SpA, SpD, Spe)"),
      field("statusEffort", 1, 0x0E, 6, 0, 0, "effort values (same order)"),
      field("wazaID", 2, 0x14, 4, 0, 0, "moves"),
      field("unk1C", 1, 0x1C),
      field("comboPartnerAll", 1, 0x1D),
      field("seikakuID", 1, 0x1E, 1, 3, 5, "nature"),
      field("sex", 1, 0x1E, 1, 1, 2),
      field("tokuseiFlag", 1, 0x1E, 1, 0, 1, "which of the species' two abilities"),
      field("randomSetting", 1, 0x1F, 1, 7, 1),
      field("unk1F", 1, 0x1F, 1, 0, 7),
  };
}
std::vector<em::DataField> shadow_fields() {
  return {
      field("worth", 1, 0x00),
      field("snatchRatio", 1, 0x01, 1, 0, 0, "catch rate"),
      field("playerLevel", 1, 0x02),
      field("scenarioPokemon", 1, 0x03, 1, 7, 1),
      field("unk3", 1, 0x03, 1, 0, 7),
      field("deckPokemonID", 4, 0x04, 1, 0, 0, "entry number of the Pokemon in the deck of the trainer using it (story_pokemon...)"),
      field("initDarkPoint", 2, 0x08, 1, 0, 0, "heart gauge at the start"),
      field("bonusExp", 2, 0x0A),
      field("darkWaza", 2, 0x0C, 4, 0, 0, "Shadow moves"),
      field("revers", 1, 0x14),
      field("underTakerFlag", 1, 0x15),
      field("unk16", 1, 0x16, 2),
  };
}
std::vector<em::DataField> ai_fields() {
  std::vector<em::DataField> f;
  static const char* const kByte0[] = {"selectRandomPokemon", "notSameWeekPoint", "order", "selectAceBoss",
                                       "respectKeyPlayer", "attrDetermination", "abilityDetermination", "calcWazaDamage"};
  static const char* const kByte1[] = {"selectRandomDefence", "selectRandomWaza", "respectWazaAssmntVal", "respectRole",
                                       "respectWazaHit", "respectWazaRiskJudge", "respectPokemonCondJuge", "unk1_0"};
  for (int i = 0; i < 8; i++) f.push_back(field(kByte0[i], 1, 0x00, 1, static_cast<uint8_t>(7 - i), 1));
  for (int i = 0; i < 8; i++) f.push_back(field(kByte1[i], 1, 0x01, 1, static_cast<uint8_t>(7 - i), 1));
  f.push_back(field("unk2", 1, 0x02, 2));
  f.push_back(field("statusDetermVal.max", 1, 0x04, 8, 4, 4));
  f.push_back(field("statusDetermVal.min", 1, 0x04, 8, 0, 4));
  static const char* const kRates[] = {"statusDetermValTotalMax", "statusDetermValTotalMin", "pokeChgRate", "itemUseRate",
                                       "hitRate", "annihilateDetermRate", "lowHPDetermRate", "lastPPDetermRate"};
  for (int i = 0; i < 8; i++) f.push_back(field(kRates[i], 1, static_cast<uint32_t>(0x0C + i)));
  f.push_back(field("attrTypeID", 1, 0x14, 2));
  f.push_back(field("attrTypeParam", 1, 0x16, 2));
  f.push_back(field("wazaKindID", 2, 0x18, 2));
  f.push_back(field("wazaKindParam", 1, 0x1C, 2));
  f.push_back(field("totalCorrection", 1, 0x1E));
  f.push_back(field("unk1F", 1, 0x1F));
  return f;
}

void add_decks(const em::EntryReader& read, em::DataCatalog& out) {
  static const char* const kDecks[] = {"Story", "Colosseum", "Hundred", "Imasugu", "Virtual", "Bingo", "Sample", "DarkPokemon"};
  static const char* const kArchives[] = {"deck_archive.fsys", "common.fsys"};
  struct Kind {
    const char* chunk;
    const char* suffix;
    uint32_t size;
    std::vector<em::DataField> (*fields)();
  };
  static const Kind kKinds[] = {{"DTNR", "_trainers", 0x38, trainer_fields},
                                {"DPKM", "_pokemon", 0x20, pokemon_fields},
                                {"DDPK", "_shadow", 0x18, shadow_fields},
                                {"DTAI", "_ai", 0x20, ai_fields}};
  for (const char* deck : kDecks) {
    std::string lower = deck;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::string entry = std::string("DeckData_") + deck + ".bin";
    std::vector<DeckChunk> first;
    std::vector<uint8_t> firstData;
    std::vector<std::string> places;
    for (const char* archive : kArchives) {
      std::vector<uint8_t> d;
      std::vector<DeckChunk> chunks;
      if (!read(archive, entry, d) || !read_chunks(d, chunks)) continue;
      if (places.empty()) {
        first = chunks;
        firstData = d;
      } else if (d.size() != firstData.size() || chunks.size() != first.size()) {
        continue;
      }
      places.push_back(archive);
    }
    if (places.empty()) continue;
    for (const Kind& k : kKinds) {
      for (const DeckChunk& c : first) {
        if (c.id != k.chunk || 0x10 + static_cast<uint64_t>(c.count) * k.size > c.size) continue;
        em::DataTable t;
        t.name = lower + k.suffix;
        t.entrySize = k.size;
        t.count = c.count;
        t.fields = k.fields();
        t.note = entry + " (" + k.chunk + ")";
        for (const std::string& a : places) t.places.push_back({a, entry, c.offset + 0x10});
        if (std::strcmp(k.chunk, "DTNR") == 0) {
          for (const DeckChunk& s : first) {
            if (s.id != "DSTR") continue;
            em::NameList names{lower + "_trainer", {}};
            for (uint32_t i = 0; i < t.count; i++) {
              const uint32_t at = s.offset + 0x10 + be16(firstData.data() + t.places[0].offset + i * t.entrySize);
              std::string text;
              for (uint32_t p = at; p < s.offset + s.size && firstData[p] != 0 && text.size() < 64; p++) {
                text.push_back(static_cast<char>(firstData[p]));
              }
              if (!text.empty()) names.names.emplace_back(text, i);
            }
            t.entryNames = names.name;
            out.lists.push_back(std::move(names));
          }
        }
        out.tables.push_back(std::move(t));
      }
    }
  }
}

struct ProgramTable {
  const char* name;
  const char* alias;
  uint32_t address, size, countAddress, entrySize;
  const char* note;
  std::vector<em::DataField> (*fields)();
};
std::vector<em::DataField> dark_list_fields() {
  return {field("pokemonID", 2, 0, 1, 0, 0,
                "species of the Shadow Pokemon with this number (darkpokemon_shadow entry); copied into the save's "
                "Shadow records at New Game (deck.cpp deckInitDarkPokemon): the P*DA's Shadow list and monitor")};
}
std::vector<em::DataField> tm_fields() {
  return {field("unk0", 4, 0), field("wazaID", 4, 4, 1, 0, 0, "the move this TM teaches (entry 0 = TM01)")};
}
const ProgramTable kProgramTables[] = {
    {"darkPokemonList", "shadow_species", 0x804044E8, 0x100, 0x804E83D8, 2,
     "main.dol: species of each Shadow Pokemon number (darkPokemonList.c)", dark_list_fields},
    {"item_waza_machine_no", "tm_moves", 0x804053A0, 0x1D0, 0x804E8430, 8,
     "main.dol: the move of each TM (itemWazaMachineNo.c, itemDataBiosGetWazaIDByWazaMachineNo)", tm_fields},
};

void add_program_tables(const em::EntryReader& read, em::DataCatalog& out) {
  std::vector<uint8_t> dol;
  if (!read(em::kProgramArchive, "main.dol", dol)) return;
  for (const ProgramTable& p : kProgramTables) {
    uint32_t at = 0, countAt = 0;
    if (!DolAddressToOffset(dol.data(), dol.size(), p.address, p.size, at) ||
        !DolAddressToOffset(dol.data(), dol.size(), p.countAddress, 4, countAt)) {
      continue;
    }
    const uint32_t count = be32(dol.data() + countAt);
    if (count == 0 || static_cast<uint64_t>(count) * p.entrySize > p.size) continue;
    em::DataTable t;
    t.name = p.name;
    t.aliases.push_back(p.alias);
    t.note = p.note;
    t.entrySize = p.entrySize;
    t.count = count;
    t.places.push_back({em::kProgramArchive, "main.dol", at});
    t.fields = p.fields();
    out.tables.push_back(std::move(t));
  }
}

}

void AddDeckTables(const em::EntryReader& read, em::DataCatalog& out) { add_decks(read, out); }

bool BuildDataCatalog(const em::EntryReader& read, em::DataCatalog& out, std::string& error) {
  out = em::DataCatalog{};
  out.toolHint = "xd_data_mods <disc> --tables / --fields <table> / --names <list> show what there is";
  CommonRel c;
  if (!read("common.fsys", "common_rel", c.file)) {
    error = "common.fsys has no common_rel";
    return false;
  }
  if (!Rel_Parse(c.file.data(), c.file.size(), &c.rel)) {
    error = "common_rel is not a REL module";
    return false;
  }
  c.ok = true;
  std::vector<Bank> banks;
  if (const uint8_t* t = PortXD_GetDolTable("common_tbl")) {
    banks.push_back({t, 0});
  }
  add_common_rel(c, out, banks);
  if (out.tables.empty()) {
    error = "common_rel's tables weren't found";
    return false;
  }
  add_decks(read, out);
  add_program_tables(read, out);

  add_names(out, c, banks, "pokemon_data", 0x18, "species");
  add_names(out, c, banks, "waza_data", 0x20, "move");
  add_names(out, c, banks, "zokusei_data", 0x08, "type");
  add_names(out, c, banks, "pokemon_seikaku_data", 0x14, "nature");
  const em::DataTable* index = table_named(out, "item_data_index");
  const em::DataTable* items = table_named(out, "item_data_prime");
  if (index != nullptr && items != nullptr) {
    em::NameList byNumber{"item", {}}, byEntry{"item_entry", {}};
    std::set<uint32_t> entries;
    for (uint32_t id = 0; id < index->count; id++) {
      const uint32_t slot = be16(c.file.data() + index->places[0].offset + id * 2);
      if (slot >= items->count) continue;
      const std::string n = message(banks, be32(c.file.data() + items->places[0].offset + slot * items->entrySize + 0x10));
      if (n.empty()) continue;
      byNumber.names.emplace_back(n, id);
      if (entries.insert(slot).second) byEntry.names.emplace_back(n, slot);
    }
    out.lists.push_back(std::move(byNumber));
    out.lists.push_back(std::move(byEntry));
  }

  for (em::DataTable& t : out.tables) {
    for (const Alias& a : kAliases) {
      if (t.name == a.table) t.aliases.push_back(a.alias);
    }
    for (em::DataField& f : t.fields) {
      for (const Values& v : kValues) {
        if (matches(v.table, t.name) && f.name == v.field) f.values = v.list;
      }
    }
  }
  return true;
}

bool DolAddressToOffset(const uint8_t* dol, size_t size, uint32_t address, uint32_t bytes, uint32_t& offset) {
  if (size < 0x100) return false;
  for (int i = 0; i < 18; i++) {
    const uint32_t off = be32(dol + 4 * i), addr = be32(dol + 0x48 + 4 * i), len = be32(dol + 0x90 + 4 * i);
    if (len != 0 && address >= addr && static_cast<uint64_t>(address) + bytes <= static_cast<uint64_t>(addr) + len &&
        static_cast<uint64_t>(off) + (address - addr) + bytes <= size) {
      offset = off + (address - addr);
      return true;
    }
  }
  return false;
}

bool DolOffsetToAddress(const uint8_t* dol, size_t size, uint32_t offset, uint32_t& address) {
  if (size < 0x100) return false;
  for (int i = 0; i < 18; i++) {
    const uint32_t off = be32(dol + 4 * i), addr = be32(dol + 0x48 + 4 * i), len = be32(dol + 0x90 + 4 * i);
    if (len != 0 && offset >= off && offset - off < len) {
      address = addr + (offset - off);
      return true;
    }
  }
  return false;
}

}
