#include "../src/data/common_rel.h"
#include "../src/data/xd_data_mods.h"
#include "../src/dol_tables.h"
#include "../src/xd_user_folder.h"

#include <data_table.h>
#include <gs_formats.h>
#include <mods.h>
#include <mods_disc.h>
#include <port_disc.h>
#include <port_user_folder.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <string>
#include <vector>

extern "C" size_t PortXD_DescribedTables(const TableDesc* const** out);

namespace fs = std::filesystem;
namespace em = engine::mods;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

bool read_disc_entry_uncached(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out);
bool read_disc_entry(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
  static std::map<std::pair<std::string, std::string>, std::pair<bool, std::vector<uint8_t>>> cache;
  auto it = cache.find({archive, entry});
  if (it == cache.end()) {
    std::vector<uint8_t> bytes;
    const bool ok = read_disc_entry_uncached(archive, entry, bytes);
    it = cache.emplace(std::make_pair(archive, entry), std::make_pair(ok, std::move(bytes))).first;
  }
  out = it->second.second;
  return it->second.first;
}
bool read_disc_entry_uncached(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
  size_t size = 0;
  if (archive == em::kProgramArchive) {
    const uint8_t* dol = PortDisc_Dol(&size);
    if (dol == nullptr || size == 0) return false;
    out.assign(dol, dol + size);
    return true;
  }
  uint8_t* file = PortDisc_ReadOriginalFile(archive.c_str(), &size);
  if (file == nullptr) return false;
  FsysArchive f{};
  bool ok = false;
  if (Fsys_Open(file, size, &f)) {
    int index = -1;
    const bool byNumber = entry.size() >= 2 && entry[0] == '#';
    if (byNumber) {
      const unsigned long n = std::strtoul(entry.c_str() + 1, nullptr, 10);
      if (n < f.count) index = static_cast<int>(n);
    }
    for (uint32_t i = 0; i < f.count && !byNumber; i++) {
      FsysEntry e;
      if (Fsys_Entry(&f, i, &e) && (std::strcmp(e.fullName, entry.c_str()) == 0 || (index < 0 && std::strcmp(e.name, entry.c_str()) == 0))) {
        index = static_cast<int>(i);
        if (std::strcmp(e.fullName, entry.c_str()) == 0) break;
      }
    }
    uint32_t n = 0;
    uint8_t* p = index >= 0 ? Fsys_Extract(&f, static_cast<uint32_t>(index), &n) : nullptr;
    if (p != nullptr) {
      out.assign(p, p + n);
      std::free(p);
      ok = true;
    }
  }
  PortDisc_Free(file);
  return ok;
}

std::vector<uint8_t> extract(const std::vector<uint8_t>& archive, const std::string& entry) {
  FsysArchive f{};
  if (!Fsys_Open(archive.data(), archive.size(), &f)) return {};
  for (uint32_t i = 0; i < f.count; i++) {
    FsysEntry e;
    if (Fsys_Entry(&f, i, &e) && (entry == e.fullName || entry == e.name)) {
      uint32_t n = 0;
      uint8_t* p = Fsys_Extract(&f, i, &n);
      std::vector<uint8_t> out(p, p + (p != nullptr ? n : 0));
      std::free(p);
      return out;
    }
  }
  return {};
}

std::string name_of(const em::DataCatalog& c, const std::string& list, uint32_t number) {
  if (list.empty()) return "";
  if (const em::NameList* l = c.FindList(list)) {
    for (const auto& [n, v] : l->names) {
      if (v == number) return n;
    }
  }
  return "";
}

const em::DataField* field_at(const em::DataTable& t, const em::DataChange& c) {
  for (const em::DataField& f : t.fields) {
    for (uint32_t e = 0; e < f.count; e++) {
      if (f.offset + e * f.Stride() == c.offset && f.size == c.size && f.bitShift == c.bitShift && f.bitWidth == c.bitWidth) {
        return &f;
      }
    }
  }
  return nullptr;
}

std::string value_text(const em::DataCatalog& c, const em::DataField* f, uint32_t v) {
  std::string s = std::to_string(v);
  if (f != nullptr) {
    const std::string n = name_of(c, f->values, v);
    if (!n.empty()) s += " (" + n + ")";
  }
  return s;
}

void print_change(const em::DataCatalog& cat, const em::DataChange& c) {
  const em::DataTable* t = cat.FindTable(c.table);
  const em::DataField* f = t != nullptr ? field_at(*t, c) : nullptr;
  std::printf("  %s #%u%s %s: %s -> %s   [%s, %s]\n", c.table.c_str(), c.entry,
              c.entryName.empty() ? "" : (" (" + c.entryName + ")").c_str(), c.field.c_str(), value_text(cat, f, c.before).c_str(),
              value_text(cat, f, c.after).c_str(), c.mod.c_str(), c.where.c_str());
}

uint32_t kind_gc(FieldKind k) { return k == FIELD_U8 ? 1 : k == FIELD_U16 ? 2 : 4; }
uint32_t kind_pc(FieldKind k) {
  return k == FIELD_U8 ? 1 : k == FIELD_U16 ? 2 : (k == FIELD_PTR || k == FIELD_U32_IN_PTR) ? sizeof(void*) : 4;
}
uint32_t read_pc(const uint8_t* p, FieldKind k) {
  switch (k) {
  case FIELD_U8: return p[0];
  case FIELD_U16: { uint16_t v; std::memcpy(&v, p, 2); return v; }
  case FIELD_U32: { uint32_t v; std::memcpy(&v, p, 4); return v; }
  case FIELD_U32_IN_PTR: { uintptr_t v; std::memcpy(&v, p, sizeof(v)); return static_cast<uint32_t>(v); }
  default: return 0;
  }
}

const uint8_t* global(const XDCommonTables& t, const char* name) {
  for (uint32_t i = 0; i < t.numGlobals; i++) {
    if (t.globals[i].converted && std::strcmp(t.globals[i].name, name) == 0) return static_cast<const uint8_t*>(t.globals[i].value);
  }
  return nullptr;
}

bool convert(const std::vector<uint8_t>& rel, XDCommonTables& out) {
  char error[512];
  if (!PortXD_ConvertCommonRel(rel.data(), rel.size(), &out, error, sizeof(error))) {
    std::printf("FAIL converting common_rel: %s\n", error);
    sOk = false;
    return false;
  }
  return true;
}

void compare_pc(const em::DataCatalog& cat, const XDCommonTables& a, const XDCommonTables& b, const std::vector<em::DataChange>& changes,
                const char* what) {
  const TableDesc* const* descs = nullptr;
  const size_t n = PortXD_DescribedTables(&descs);
  uint64_t compared = 0, differing = 0, unexpected = 0, wrong = 0;
  std::map<std::pair<std::string, uint32_t>, std::vector<const em::DataChange*>> byEntry;
  for (const em::DataChange& c : changes) byEntry[{c.table, c.entry}].push_back(&c);
  for (size_t k = 0; k < n; k++) {
    const TableDesc* d = descs[k];
    const em::DataTable* t = cat.FindTable(d->name);
    const uint8_t* pa = global(a, d->name);
    const uint8_t* pb = global(b, d->name);
    if (t == nullptr || t->places.empty() || t->places[0].entry != "common_rel" || pa == nullptr || pb == nullptr) continue;
    for (uint32_t i = 0; i < t->count; i++) {
      const auto ch = byEntry.find({t->name, i});
      for (uint32_t fi = 0; fi < d->numFields; fi++) {
        const FieldDesc& f = d->fields[fi];
        if (f.kind == FIELD_PTR || f.kind == FIELD_ADDR32) continue;
        for (uint32_t e = 0; e < f.count; e++) {
          const uint32_t gc = f.gcOffset + e * (f.gcStride != 0 ? f.gcStride : kind_gc(f.kind));
          const size_t pc = static_cast<size_t>(i) * d->pcSize + f.pcOffset + e * (f.pcStride != 0 ? f.pcStride : kind_pc(f.kind));
          compared++;
          const uint32_t va = read_pc(pa + pc, f.kind), vb = read_pc(pb + pc, f.kind);
          uint32_t want = va;
          if (ch != byEntry.end()) {
            for (const em::DataChange* c : ch->second) {
              if (c->offset < gc || c->offset + c->size > gc + kind_gc(f.kind)) continue;
              const uint32_t shiftBytes = (gc + kind_gc(f.kind)) - (c->offset + c->size);
              const uint32_t width = c->bitWidth != 0 ? c->bitWidth : 8u * c->size;
              const uint32_t mask = (width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1) << (c->bitShift + 8 * shiftBytes);
              want = (want & ~mask) | ((c->after << (c->bitShift + 8 * shiftBytes)) & mask);
            }
          }
          if (va != vb) differing++;
          if (vb != want) {
            (want == va ? unexpected : wrong)++;
            if (unexpected + wrong <= 10) {
              std::printf("     %s #%u %s[%u]: disc %u, modded %u, expected %u\n", d->name, i, f.name, e, va, vb, want);
            }
          }
        }
      }
    }
  }
  check(unexpected == 0 && wrong == 0,
        std::string(what) + ": converted PC tables, " + std::to_string(compared) + " numbers compared, " + std::to_string(differing) +
            " differ, all of them set by the mods to the value the PC table holds");
}

void check_disc_side(const em::DataCatalog& cat, const std::map<std::pair<std::string, std::string>, std::vector<uint8_t>>& entries,
                     const std::vector<em::DataChange>& changes, const char* what) {
  uint64_t checked = 0, bad = 0;
  for (const em::DataChange& c : changes) {
    const em::DataTable* t = cat.FindTable(c.table);
    const em::DataField* f = t != nullptr ? field_at(*t, c) : nullptr;
    if (f == nullptr) continue;
    for (const em::DataPlace& p : t->places) {
      std::vector<uint8_t> base;
      const auto it = entries.find({p.archive, p.entry});
      const std::vector<uint8_t>* bytes = it != entries.end() ? &it->second : nullptr;
      if (bytes == nullptr) {
        if (!read_disc_entry(p.archive, p.entry, base)) continue;
        bytes = &base;
      }
      em::DataField one = *f;
      one.offset = c.offset;
      one.count = 1;
      checked++;
      if (em::ReadDataField(bytes->data() + p.offset + static_cast<size_t>(c.entry) * t->entrySize, one, 0) != c.after) bad++;
    }
  }
  check(bad == 0, std::string(what) + ": " + std::to_string(checked) + " changed numbers read back from the changed entries, all as set");
}

std::unique_ptr<em::DataCatalog> disc_catalog() {
  auto cat = std::make_unique<em::DataCatalog>();
  std::string error;
  if (!xd::data::BuildDataCatalog(read_disc_entry, *cat, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.c_str());
    return nullptr;
  }
  return cat;
}

int list_tables(const em::DataCatalog& cat) {
  for (const em::DataTable& t : cat.tables) {
    std::string aliases;
    for (const std::string& a : t.aliases) aliases += (aliases.empty() ? " (also: " : ", ") + a;
    if (!aliases.empty()) aliases += ")";
    std::string where;
    for (const em::DataPlace& p : t.places) where += (where.empty() ? "" : ", ") + p.archive + "/" + p.entry;
    std::printf("%-36s %5u entries x %3u bytes  %s%s%s\n", t.name.c_str(), t.count, t.entrySize, where.c_str(), aliases.c_str(),
                t.entryNames.empty() ? "" : ("  named by " + t.entryNames).c_str());
  }
  std::printf("\nname lists:");
  for (const em::NameList& l : cat.lists) std::printf(" %s (%zu)", l.name.c_str(), l.names.size());
  std::printf("\n");
  return 0;
}

int list_fields(const em::DataCatalog& cat, const char* name) {
  const em::DataTable* t = cat.FindTable(name);
  if (t == nullptr) {
    std::fprintf(stderr, "no table %s (--tables lists them)\n", name);
    return 1;
  }
  std::printf("%s: %u entries of %u bytes\n", t->name.c_str(), t->count, t->entrySize);
  for (const em::DataField& f : t->fields) {
    std::string kind = f.bitWidth != 0 ? std::to_string(f.bitWidth) + " bit(s) of a byte" : std::to_string(f.size) + " byte(s)";
    std::printf("  %-32s @0x%02X %-18s %s%s%s%s\n", f.name.c_str(), f.offset, kind.c_str(),
                f.count > 1 ? (std::to_string(f.count) + " values  ").c_str() : "",
                f.values.empty() ? "" : ("names: " + f.values + "  ").c_str(), f.editable ? "" : "(not editable) ", f.note.c_str());
  }
  return 0;
}

int show_entry(const em::DataCatalog& cat, const char* name, const char* entryText) {
  const em::DataTable* t = cat.FindTable(name);
  if (t == nullptr) {
    std::fprintf(stderr, "no table %s (--tables lists them)\n", name);
    return 1;
  }
  uint32_t entry = UINT32_MAX;
  const char* digits = entryText[0] == '#' ? entryText + 1 : entryText;
  char* end = nullptr;
  const unsigned long n = std::strtoul(digits, &end, 0);
  if (end != digits && *end == '\0') {
    entry = static_cast<uint32_t>(n);
  } else if (const em::NameList* l = cat.FindList(t->entryNames)) {
    for (const auto& [nm, v] : l->names) {
      if (em::NameKey(nm) == em::NameKey(entryText)) {
        entry = v;
        break;
      }
    }
  }
  if (entry >= t->count) {
    std::fprintf(stderr, "%s has no entry %s\n", t->name.c_str(), entryText);
    return 1;
  }
  std::vector<uint8_t> bytes;
  if (!read_disc_entry(t->places[0].archive, t->places[0].entry, bytes)) return 1;
  const uint8_t* e = bytes.data() + t->places[0].offset + static_cast<size_t>(entry) * t->entrySize;
  const std::string en = name_of(cat, t->entryNames, entry);
  std::printf("%s #%u%s\n", t->name.c_str(), entry, en.empty() ? "" : (" (" + en + ")").c_str());
  for (const em::DataField& f : t->fields) {
    std::string values;
    for (uint32_t i = 0; i < f.count; i++) {
      values += (i == 0 ? "" : ", ") + value_text(cat, &f, em::ReadDataField(e, f, i));
    }
    std::printf("  %-32s %s%s%s\n", f.name.c_str(), f.count > 1 ? "[" : "", values.c_str(), f.count > 1 ? "]" : "");
  }
  return 0;
}

int show_names(const em::DataCatalog& cat, const char* name) {
  const em::NameList* l = cat.FindList(name);
  if (l == nullptr) {
    std::fprintf(stderr, "no name list %s (--tables lists them)\n", name);
    return 1;
  }
  for (const auto& [n, v] : l->names) std::printf("%5u  %s\n", v, n.c_str());
  return 0;
}

int show_text(const em::DataCatalog& cat, const char* fromText, const char* toText) {
  const uint32_t from = static_cast<uint32_t>(std::strtoul(fromText, nullptr, 0));
  const uint32_t to = toText != nullptr ? static_cast<uint32_t>(std::strtoul(toText, nullptr, 0)) : from;
  if (cat.text.places.empty() || !cat.text.read) {
    std::fprintf(stderr, "no message table that data files can change was found on this disc\n");
    return 1;
  }
  for (const em::DataPlace& p : cat.text.places) {
    std::vector<uint8_t> bytes;
    if (!read_disc_entry(p.archive, p.entry, bytes) || p.offset >= bytes.size()) continue;
    for (uint32_t id = from; id <= to && id >= from; id++) {
      std::string text;
      if (!cat.text.read(bytes.data() + p.offset, bytes.size() - p.offset, id, text)) continue;
      std::string shown;
      for (char c : text) {
        if (c == '"' || c == '\\') shown.push_back('\\');
        if (c == '\n') shown += "\\n";
        else shown.push_back(c);
      }
      std::printf("    \"#%u\": \"%s\"\n", id, shown.c_str());
    }
  }
  return 0;
}

int run_mods(const fs::path& modsFolder, const fs::path& listFile) {
  em::LoadOptions options;
  options.modsFolder = modsFolder;
  options.listFile = listFile;
  options.disc = em::OpenDiscView();
  options.dataCatalog = xd::data::BuildDataCatalog;
  options.listReadOnly = true;
  std::printf("disc %s, mods in %s, list %s\n", options.disc.gameId.c_str(), modsFolder.string().c_str(), listFile.string().c_str());
  const em::LoadResult result = em::Load(options);
  if (result.listWriteSkipped) {
    std::printf("[mods] the list file is left as it is (this tool only reads; the game or the launcher adds new mods to it)\n");
  }
  for (const em::Message& m : result.messages) {
    static const char* const kLabel[] = {"", "warning: ", "problem: "};
    std::printf("[mods] %s%s\n", kLabel[static_cast<int>(m.level)], m.text.c_str());
  }
  std::printf("mods on: %s\n", result.active.empty() ? "none" : result.ActiveText().c_str());
  if (result.dataChanges.empty() || result.catalog == nullptr) {
    std::printf("no data changes: the game reads its tables exactly as on the disc\n");
    return sOk ? 0 : 1;
  }
  std::printf("%zu number(s) set by data mods:\n", result.dataChanges.size());
  for (const em::DataChange& c : result.dataChanges) print_change(*result.catalog, c);

  std::map<std::pair<std::string, std::string>, std::vector<uint8_t>> entries;
  for (const em::Replacement& r : result.files) {
    if (!r.rebuilt) continue;
    for (const em::DataTable& t : result.catalog->tables) {
      for (const em::DataPlace& p : t.places) {
        if (p.archive == r.discPath && entries.find({p.archive, p.entry}) == entries.end()) {
          entries[{p.archive, p.entry}] = extract(r.data, p.entry);
        }
      }
    }
  }
  check_disc_side(*result.catalog, entries, result.dataChanges, "disc side");
  const auto rel = entries.find({"common.fsys", "common_rel"});
  if (rel != entries.end()) {
    std::vector<uint8_t> original;
    read_disc_entry("common.fsys", "common_rel", original);
    auto a = std::make_unique<XDCommonTables>(), b = std::make_unique<XDCommonTables>();
    if (convert(original, *a) && convert(rel->second, *b)) {
      compare_pc(*result.catalog, *a, *b, result.dataChanges, "PC side");
    }
  }
  std::printf(sOk ? "xd_data_mods: all passed\n" : "xd_data_mods: FAILED\n");
  return sOk ? 0 : 1;
}

std::string json_of(const std::map<std::string, std::map<uint32_t, std::map<std::string, std::vector<uint32_t>>>>& set) {
  std::string s = "{\n";
  bool firstT = true;
  for (const auto& [table, entries] : set) {
    s += std::string(firstT ? "" : ",\n") + "\"" + table + "\": {";
    firstT = false;
    bool firstE = true;
    for (const auto& [entry, fields] : entries) {
      s += std::string(firstE ? "" : ",") + "\n \"#" + std::to_string(entry) + "\": {";
      firstE = false;
      bool firstF = true;
      for (const auto& [field, values] : fields) {
        s += std::string(firstF ? "" : ", ") + "\"" + field + "\": ";
        firstF = false;
        if (values.size() == 1) {
          s += std::to_string(values[0]);
        } else {
          s += "[";
          for (size_t i = 0; i < values.size(); i++) s += (i ? ", " : "") + std::to_string(values[i]);
          s += "]";
        }
      }
      s += "}";
    }
    s += "}";
  }
  return s + "\n}\n";
}

int selftest(const em::DataCatalog& cat) {
  std::vector<uint8_t> original;
  check(read_disc_entry("common.fsys", "common_rel", original), "common_rel read from the disc");

  std::map<std::string, std::map<uint32_t, std::map<std::string, std::vector<uint32_t>>>> same;
  size_t numbers = 0;
  std::set<std::tuple<std::string, uint32_t, uint32_t, uint8_t>> distinct;
  for (const em::DataTable& t : cat.tables) {
    std::vector<uint8_t> bytes;
    if (!read_disc_entry(t.places[0].archive, t.places[0].entry, bytes)) continue;
    for (uint32_t i = 0; i < t.count; i++) {
      const uint8_t* e = bytes.data() + t.places[0].offset + static_cast<size_t>(i) * t.entrySize;
      for (const em::DataField& f : t.fields) {
        if (!f.editable) continue;
        auto& values = same[t.name][i][f.name];
        for (uint32_t k = 0; k < f.count; k++) {
          values.push_back(em::ReadDataField(e, f, k));
          numbers += distinct.insert({t.name, i, f.offset + k * f.Stride(), f.bitShift}).second ? 1 : 0;
        }
      }
    }
  }
  {
    std::vector<em::Message> messages;
    const em::DataApplyResult r = em::ApplyDataMods(cat, {{"same", "Same values", "data/same.json", json_of(same)}}, read_disc_entry, messages);
    for (const em::Message& m : messages) std::printf("     %s\n", m.text.c_str());
    if (r.changes.size() != numbers) std::printf("     %zu changes listed, %zu numbers set\n", r.changes.size(), numbers);
    check(messages.empty() && r.entries.empty() && r.changes.size() == numbers,
          "every editable number of every table (" + std::to_string(numbers) + " in " + std::to_string(cat.tables.size()) +
              " tables) set to its own value: no message, no entry changed");
  }
  auto a = std::make_unique<XDCommonTables>(), b = std::make_unique<XDCommonTables>();
  if (convert(original, *a) && convert(original, *b)) {
    compare_pc(cat, *a, *b, {}, "no data mods (common_rel converted twice)");
  }
  std::map<std::string, std::map<uint32_t, std::map<std::string, std::vector<uint32_t>>>> one;
  for (const em::DataTable& t : cat.tables) {
    std::vector<uint8_t> bytes;
    if (t.count == 0 || !read_disc_entry(t.places[0].archive, t.places[0].entry, bytes)) continue;
    const uint32_t i = t.count - 1;
    const uint8_t* e = bytes.data() + t.places[0].offset + static_cast<size_t>(i) * t.entrySize;
    for (const em::DataField& f : t.fields) {
      if (!f.editable || (f.bitWidth != 0 && t.name != "story_pokemon")) continue;
      one[t.name][i][f.name + (f.count > 1 ? "[0]" : "")] = {em::ReadDataField(e, f, 0) ^ 1u};
      if (t.name != "story_pokemon" || f.name == "seikakuID") break;
    }
  }
  std::vector<em::Message> messages;
  const em::DataApplyResult r = em::ApplyDataMods(cat, {{"one", "One each", "data/one.json", json_of(one)}}, read_disc_entry, messages);
  for (const em::Message& m : messages) std::printf("     %s\n", m.text.c_str());
  check(messages.empty() && !r.entries.empty(), "one number per table changed: " + std::to_string(r.changes.size()) + " changes in " +
                                                    std::to_string(r.entries.size()) + " archive entries");
  uint64_t compared = 0, bad = 0;
  for (const em::DataTable& t : cat.tables) {
    for (const em::DataPlace& p : t.places) {
      const auto it = r.entries.find({p.archive, p.entry});
      if (it == r.entries.end()) continue;
      std::vector<uint8_t> before;
      read_disc_entry(p.archive, p.entry, before);
      for (uint32_t i = 0; i < t.count; i++) {
        const uint8_t* eb = before.data() + p.offset + static_cast<size_t>(i) * t.entrySize;
        const uint8_t* ea = it->second.data() + p.offset + static_cast<size_t>(i) * t.entrySize;
        for (const em::DataField& f : t.fields) {
          for (uint32_t k = 0; k < f.count; k++) {
            const uint32_t at = f.offset + k * f.Stride();
            bool touched = false, exact = false;
            uint32_t want = 0;
            for (const em::DataChange& c : r.changes) {
              if (c.table != t.name || c.entry != i || c.offset + c.size <= at || at + f.size <= c.offset) continue;
              touched = true;
              if (c.offset == at && c.size == f.size && c.bitShift == f.bitShift && c.bitWidth == f.bitWidth) {
                exact = true;
                want = c.after;
              }
            }
            compared++;
            const uint32_t vb = em::ReadDataField(eb, f, k), va = em::ReadDataField(ea, f, k);
            if (exact ? va != want : (!touched && va != vb)) {
              if (++bad <= 10) std::printf("     %s #%u %s[%u]: %u -> %u\n", t.name.c_str(), i, f.name.c_str(), k, vb, va);
            }
          }
        }
      }
    }
  }
  check(bad == 0, "disc side: " + std::to_string(compared) + " numbers of the changed tables compared; only the changed ones differ");
  check_disc_side(cat, r.entries, r.changes, "disc side");
  const auto rel = r.entries.find({"common.fsys", "common_rel"});
  if (rel != r.entries.end()) {
    auto c = std::make_unique<XDCommonTables>();
    if (convert(rel->second, *c)) compare_pc(cat, *a, *c, r.changes, "one number per table");
  }
  std::printf(sOk ? "xd_data_mods --selftest: all passed\n" : "xd_data_mods --selftest: FAILED\n");
  return sOk ? 0 : 1;
}

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "Usage: %s <disc> --tables | --fields <table> | --show <table> <entry> | --names <list> |\n"
                 "                 --mods [mods folder] [mods.ini] | --selftest\n",
                 argv[0]);
    return 2;
  }
  char error[512];
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Open(argv[1], error, sizeof(error)) ? PortDisc_Dol(&dolSize) : nullptr;
  if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  const std::string mode = argv[2];
  if (mode == "--mods") {
    const fs::path user = xd::SettingsFolder();
    return run_mods(argc >= 4 ? fs::path(argv[3]) : user / L"mods", argc >= 5 ? fs::path(argv[4]) : user / em::kListFile);
  }
  const auto cat = disc_catalog();
  if (cat == nullptr) return 1;
  if (mode == "--tables") return list_tables(*cat);
  if (mode == "--fields" && argc >= 4) return list_fields(*cat, argv[3]);
  if (mode == "--show" && argc >= 5) return show_entry(*cat, argv[3], argv[4]);
  if (mode == "--names" && argc >= 4) return show_names(*cat, argv[3]);
  if (mode == "--text" && argc >= 4) return show_text(*cat, argv[3], argc >= 5 ? argv[4] : nullptr);
  if (mode == "--selftest") return selftest(*cat);
  std::fprintf(stderr, "unknown or incomplete option %s\n", argv[2]);
  return 2;
}
