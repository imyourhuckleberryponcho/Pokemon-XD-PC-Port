#include "../src/data/common_rel.h"
#include "../src/data/xd_data_mods.h"
#include "../src/dol_tables.h"

#include <data_mods.h>
#include <gs_formats.h>
#include <mods.h>
#include <port_disc.h>

#include <pbr_pack.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace em = engine::mods;

namespace {

bool read_disc_entry(const std::string& archive, const std::string& entry, std::vector<uint8_t>& out) {
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

class Tables : public pbr::XdTables {
 public:
  explicit Tables(const pbr::DiscSource& xd) : xd_(xd) {
    xd_.activate();
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    char error[512];
    if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, error, sizeof(error)))
      throw pbr::Fail(std::string("The XD disc's tables cannot be read: ") + error, 2);
    std::string err;
    if (!xd::data::BuildDataCatalog(read_disc_entry, cat_, err)) throw pbr::Fail("The XD disc's tables cannot be read: " + err, 2);
  }

  std::vector<std::pair<uint32_t, std::string>> names(const std::string& list) override {
    std::vector<std::pair<uint32_t, std::string>> out;
    const em::NameList* l = cat_.FindList(list);
    if (l == nullptr) throw pbr::Fail("XD has no name list " + list);
    for (const auto& [n, v] : l->names) {
      size_t a = 0, b = n.size();
      while (a < b && std::isspace((unsigned char)n[a])) a++;
      while (b > a && std::isspace((unsigned char)n[b - 1])) b--;
      if (a < b) out.emplace_back(v, n.substr(a, b - a));
    }
    return out;
  }

  bool row(const std::string& table, uint32_t entry, std::string& entry_name, std::vector<pbr::XdField>& fields) override {
    const em::DataTable* t = cat_.FindTable(table);
    if (t == nullptr || entry >= t->count || t->places.empty()) return false;
    const std::vector<uint8_t>& bytes = entry_bytes(t->places[0].archive, t->places[0].entry);
    if (bytes.empty()) return false;
    const size_t at = t->places[0].offset + static_cast<size_t>(entry) * t->entrySize;
    if (at + t->entrySize > bytes.size()) return false;
    const uint8_t* e = bytes.data() + at;
    entry_name.clear();
    if (const em::NameList* l = cat_.FindList(t->entryNames)) {
      for (const auto& [n, v] : l->names)
        if (v == entry) {
          entry_name = n;
          break;
        }
    }
    fields.clear();
    for (const em::DataField& f : t->fields) {
      pbr::XdField x;
      x.name = f.name;
      for (uint32_t i = 0; i < f.count; i++) x.values.push_back(em::ReadDataField(e, f, i));
      fields.push_back(std::move(x));
    }
    return true;
  }

 private:
  const std::vector<uint8_t>& entry_bytes(const std::string& archive, const std::string& entry) {
    auto it = cache_.find({archive, entry});
    if (it == cache_.end()) {
      std::vector<uint8_t> b;
      xd_.activate();
      if (!read_disc_entry(archive, entry, b)) b.clear();
      it = cache_.emplace(std::make_pair(archive, entry), std::move(b)).first;
    }
    return it->second;
  }
  const pbr::DiscSource& xd_;
  em::DataCatalog cat_;
  std::map<std::pair<std::string, std::string>, std::vector<uint8_t>> cache_;
};

}

std::unique_ptr<pbr::XdTables> pbr_make_xd_tables(const pbr::DiscSource& xd_disc) { return std::make_unique<Tables>(xd_disc); }
