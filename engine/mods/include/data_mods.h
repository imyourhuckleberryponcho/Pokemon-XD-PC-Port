#ifndef ENGINE_DATA_MODS_H
#define ENGINE_DATA_MODS_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::mods {

struct Message;

struct DataField {
  std::string name;
  uint8_t size = 1;
  uint32_t offset = 0;
  uint32_t count = 1;
  uint32_t stride = 0;
  uint8_t bitShift = 0, bitWidth = 0;
  bool editable = true;
  std::string values;
  std::string note;

  uint32_t Stride() const { return stride != 0 ? stride : size; }
  uint32_t MaxValue() const;
};

inline constexpr const char* kProgramArchive = "<program>";

struct DataPlace {
  std::string archive, entry;
  uint32_t offset = 0;
};

struct DataTable {
  std::string name;
  std::vector<std::string> aliases;
  std::string note;
  uint32_t entrySize = 0, count = 0;
  std::vector<DataPlace> places;
  std::vector<DataField> fields;
  std::string entryNames;
  bool addable = false;
  uint32_t addFirstId = 0;
  uint32_t addMax = 0;
  std::string idList;
  std::string idTable;
  std::vector<std::string> textFields;
  std::string primaryText;
};

struct NameList {
  std::string name;
  std::vector<std::pair<std::string, uint32_t>> names;
  uint32_t limit = 0;
};

struct TextStore {
  std::vector<DataPlace> places;
  std::function<bool(const uint8_t* table, size_t room, uint32_t id, std::string& out)> read;
  std::function<bool(uint8_t* table, size_t room, const std::map<uint32_t, std::string>& texts, std::vector<uint32_t>& written,
                     std::string& error)>
      write;
};

struct DataCatalog {
  std::vector<DataTable> tables;
  std::vector<NameList> lists;
  TextStore text;
  uint32_t addedTextGroup = 0;
  std::string toolHint;

  const DataTable* FindTable(std::string_view name) const;
  const NameList* FindList(std::string_view name) const;
};

using EntryReader = std::function<bool(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out)>;
using CatalogBuilder = std::function<bool(const EntryReader& read, DataCatalog& out, std::string& error)>;

struct DataChange {
  std::string table;
  uint32_t entry = 0;
  std::string entryName;
  std::string field;
  uint32_t offset = 0;
  uint8_t size = 1, bitShift = 0, bitWidth = 0;
  uint32_t before = 0, after = 0;
  std::string mod;
  std::string where;
};

struct DataModFile {
  std::string mod, modName;
  std::string label;
  std::string text;
};

struct TextChange {
  uint32_t id = 0;
  std::string text;
  std::string mod, where;
};

struct AddedRow {
  std::string table;
  uint32_t id = 0;
  uint32_t row = 0;
  uint32_t copyOf = 0;
  std::string key;
  std::string mod, modName;
  std::string where;
  std::vector<uint8_t> bytes;
};
struct AddedText {
  uint32_t key = 0;
  std::string text;
  std::string mod, label;
};

struct DataApplyResult {
  std::map<std::pair<std::string, std::string>, std::vector<uint8_t>> entries;
  std::vector<DataChange> changes;
  std::vector<TextChange> texts;
  std::vector<AddedRow> added;
  std::vector<AddedText> addedTexts;
};

std::string AddedRowsJson(const std::vector<AddedRow>& rows);
std::map<std::string, uint64_t> AddedRowsDigests(const std::vector<AddedRow>& rows);

DataApplyResult ApplyDataMods(const DataCatalog& catalog, const std::vector<DataModFile>& files, const EntryReader& read,
                              std::vector<Message>& messages);

uint32_t ReadDataField(const uint8_t* entry, const DataField& f, uint32_t element);

std::string NameKey(std::string_view name);

}

#endif
