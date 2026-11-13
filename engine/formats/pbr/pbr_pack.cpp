#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "pbr_pack.h"
#include "pbr_pack_species.h"

namespace pbr {
namespace fs = std::filesystem;
namespace {

const std::map<std::string, std::pair<int, std::string>> kItemEvolutions = {
    {"LEAFEON", {7, "98"}},
    {"GLACEON", {7, "Ice_Stone"}},
    {"TOGEKISS", {7, "Shiny_Stone"}},
    {"ROSERADE", {7, "Shiny_Stone"}},
    {"HONCHKROW", {7, "Dusk_Stone"}},
    {"MISMAGIUS", {7, "Dusk_Stone"}},
    {"GALLADE", {19, "Dawn_Stone"}},
    {"FROSLASS", {18, "Dawn_Stone"}},
    {"WEAVILE", {17, "Razor_Claw"}},
    {"GLISCOR", {17, "Razor_Fang"}},
    {"RHYPERIOR", {6, "Protector"}},
    {"ELECTIVIRE", {6, "Electirizer"}},
    {"MAGMORTAR", {6, "Magmarizer"}},
    {"PORYGON-Z", {6, "Dubious_Disc"}},
    {"DUSKNOIR", {6, "Reaper_Cloth"}},
};

constexpr int kPackVersion = 2;
constexpr int kPkxGroupFirst = 4000, kCryGroup = 1966;
constexpr uint32_t kPkxRes = 0x00011E00;
constexpr int kFirstNewSpecies = 415;
constexpr int kSndDataCount = 1507, kSndFileCount = 516, kFlagCount = 2324, kPkxCount = 419, kFaceCount = 838;

uint32_t be32(const std::vector<uint8_t>& b, size_t o) {
  if (o + 4 > b.size()) throw Fail("pack: file too short");
  return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3];
}
uint16_t be16(const std::vector<uint8_t>& b, size_t o) { return (uint16_t)((b[o] << 8) | b[o + 1]); }
void wr32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  b[o] = (uint8_t)(v >> 24); b[o + 1] = (uint8_t)(v >> 16); b[o + 2] = (uint8_t)(v >> 8); b[o + 3] = (uint8_t)v;
}
size_t align(size_t v, size_t n) { return (v + n - 1) / n * n; }

struct FsEnt {
  uint32_t word0, type;
  std::string name;
  std::vector<uint8_t> data;
  uint32_t size;
  bool packed;
};

std::vector<uint8_t> build_fsys(uint32_t archive_id, const std::vector<FsEnt>& entries) {
  const size_t n = entries.size();
  const size_t table = 0x60;
  const size_t names_at = align(table + 4 * n, 16);
  std::vector<uint8_t> names;
  std::vector<size_t> name_off;
  for (const FsEnt& e : entries) {
    name_off.push_back(names_at + names.size());
    names.insert(names.end(), e.name.begin(), e.name.end());
    names.push_back(0);
  }
  const size_t hdrs_at = align(names_at + names.size(), 16);
  const size_t data_at = align(hdrs_at + 0x70 * n, 32);
  size_t pos = data_at;
  std::vector<size_t> offs;
  for (const FsEnt& e : entries) {
    offs.push_back(pos);
    pos = align(pos + e.data.size(), 32);
  }
  const size_t total = pos + 0x20;
  std::vector<uint8_t> out(total, 0);
  std::memcpy(out.data(), "FSYS", 4);
  const uint32_t head[8] = {0x201, archive_id, (uint32_t)n, 0x80000000u, 3, 0x40, (uint32_t)data_at, (uint32_t)total};
  for (int k = 0; k < 8; k++) wr32(out, 4 + 4 * k, head[k]);
  wr32(out, 0x40, (uint32_t)table);
  wr32(out, 0x44, (uint32_t)names_at);
  wr32(out, 0x48, (uint32_t)data_at);
  std::copy(names.begin(), names.end(), out.begin() + names_at);
  for (size_t i = 0; i < n; i++) {
    const FsEnt& e = entries[i];
    const size_t h = hdrs_at + 0x70 * i;
    wr32(out, table + 4 * i, (uint32_t)h);
    const uint32_t w[10] = {e.word0, (uint32_t)offs[i], e.size, e.packed ? 0x80000000u : 0u, 0, (uint32_t)e.data.size(), 0, 0, e.type,
                            (uint32_t)name_off[i]};
    for (int k = 0; k < 10; k++) wr32(out, h + 4 * k, w[k]);
    std::copy(e.data.begin(), e.data.end(), out.begin() + offs[i]);
  }
  std::memcpy(&out[total - 4], "FSYS", 4);
  return out;
}

std::vector<uint8_t> extend_toc(const std::vector<uint8_t>& toc, const std::vector<std::pair<uint32_t, std::string>>& rows) {
  const uint32_t count = be32(toc, 8), go = be32(toc, 0x10);
  std::vector<uint8_t> strings(toc.begin() + 0x20, toc.begin() + go);
  std::vector<uint8_t> old_rows(toc.begin() + go, toc.begin() + go + 16 * (size_t)count);
  std::vector<uint8_t> trailer(toc.begin() + go + 16 * (size_t)count, toc.end());
  const size_t base = 0x20;
  std::vector<uint8_t> new_rows;
  for (const auto& r : rows) {
    const uint32_t off = (uint32_t)(base + strings.size());
    strings.insert(strings.end(), r.second.begin(), r.second.end());
    strings.push_back(0);
    uint32_t w[4] = {r.first, off, 0, 0};
    for (uint32_t v : w) {
      new_rows.push_back((uint8_t)(v >> 24)); new_rows.push_back((uint8_t)(v >> 16)); new_rows.push_back((uint8_t)(v >> 8));
      new_rows.push_back((uint8_t)v);
    }
  }
  strings.resize(align(strings.size(), 16), 0);
  std::vector<uint8_t> out(toc.begin(), toc.begin() + 0x20);
  const size_t new_go = base + strings.size();
  wr32(out, 8, count + (uint32_t)rows.size());
  wr32(out, 0x10, (uint32_t)new_go);
  out.insert(out.end(), strings.begin(), strings.end());
  out.insert(out.end(), old_rows.begin(), old_rows.end());
  out.insert(out.end(), new_rows.begin(), new_rows.end());
  out.insert(out.end(), trailer.begin(), trailer.end());
  return out;
}

std::string title(const std::string& s) {
  std::string o = s;
  bool prev_letter = false;
  for (char& c : o) {
    const bool up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z';
    if (up || lo) {
      c = prev_letter ? (char)(up ? c + 32 : c) : (char)(lo ? c - 32 : c);
      prev_letter = true;
    } else {
      prev_letter = false;
    }
  }
  return o;
}

int face_k(int dex) { return dex - 387 - (dex > 438 ? 1 : 0); }

Json ints(const std::vector<int>& v) {
  Json a = Json::array();
  for (int x : v) a.push(x);
  return a;
}

std::vector<std::pair<std::string, std::vector<int>>> species_fields(const std::vector<uint8_t>& rec) {
  auto u8 = [&](size_t o) { return (int)rec[o]; };
  auto u16 = [&](size_t o) { return (int)be16(rec, o); };
  std::vector<std::pair<std::string, std::vector<int>>> f;
  f.push_back({"growDataId", {u8(0)}});
  f.push_back({"get", {u8(1)}});
  f.push_back({"sexRatio", {u8(2)}});
  f.push_back({"giveExp", {u16(4)}});
  f.push_back({"initFriend", {u16(6)}});
  f.push_back({"height", {u16(8)}});
  f.push_back({"weight", {u16(10)}});
  f.push_back({"numPokemon", {u16(0x12)}});
  f.push_back({"numZukan", {u16(0x14)}});
  f.push_back({"zokuseiDataId", {u8(0x30), u8(0x31)}});
  f.push_back({"tokuseiDataId", {u8(0x32), u8(0x33)}});
  std::vector<int> v;
  for (int i = 0; i < 58; i++) v.push_back(rec[0x34 + i]);
  f.push_back({"wazaMcn", v});
  v.clear();
  for (int i = 0; i < 12; i++) v.push_back(rec[0x6E + i]);
  f.push_back({"teachWaza", v});
  f.push_back({"itemDataId", {u16(0x7A), u16(0x7C)}});
  v.clear();
  for (int i = 0; i < 8; i++) v.push_back(u16(0x7E + 2 * i));
  f.push_back({"kowaza", v});
  v.clear();
  for (int i = 0; i < 12; i++) v.push_back(u16(0x8E + 2 * i));
  f.push_back({"basisStatus", v});
  std::vector<int> a, b, c, d, e;
  for (int i = 0; i < 5; i++) {
    a.push_back(u8(0xA6 + 6 * i));
    b.push_back(u16(0xA8 + 6 * i));
    c.push_back(u16(0xAA + 6 * i));
  }
  f.push_back({"sinka.kind", a});
  f.push_back({"sinka.buff", b});
  f.push_back({"sinka.pokemonDataId", c});
  for (int i = 0; i < 20; i++) {
    d.push_back(u8(0xC4 + 4 * i));
    e.push_back(u16(0xC6 + 4 * i));
  }
  f.push_back({"waza.level", d});
  f.push_back({"waza.dataId", e});
  return f;
}
bool is_single(const std::string& n) {
  static const std::set<std::string> arrays = {"zokuseiDataId", "tokuseiDataId", "wazaMcn", "teachWaza", "itemDataId", "kowaza",
                                               "basisStatus", "sinka.kind", "sinka.buff", "sinka.pokemonDataId", "waza.level",
                                               "waza.dataId"};
  return !arrays.count(n);
}

void write_file(const fs::path& p, const void* data, size_t n) {
  std::ofstream f(p, std::ios::binary);
  if (!f) throw Fail("cannot write " + p.string());
  f.write(static_cast<const char*>(data), (std::streamsize)n);
  if (!f) throw Fail("cannot write " + p.string());
}
void write_text(const fs::path& p, const std::string& s) { write_file(p, s.data(), s.size()); }
void write_bytes(const fs::path& p, const std::vector<uint8_t>& v) { write_file(p, v.data(), v.size()); }

std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

}

void make_gen4_pack(const std::string& out_folder, const DiscSource& pbr, const DiscSource& xd, XdTables& xdt,
                    const std::vector<PackModel>& models, const std::vector<PairRow>& pairs,
                    const std::function<void(const std::string&)>& say) {
  (void)say;
  const PbrData pd(pbr);
  MovesOut mv = gen4_moves(pd, xdt);
  std::map<std::string, int> nat_of_name;
  for (const PairRow& r : pairs) {
    if (r.pbr_archive.size() > 4 && r.pbr_archive.compare(0, 4, "pkx_") == 0) nat_of_name[r.name] = std::atoi(r.pbr_archive.c_str() + 4);
  }
  std::map<int, PlanIn> plan;
  std::map<std::string, const PackModel*> by_model;
  for (const PackModel& m : models) {
    PlanIn p;
    p.model = m.name;
    if (!m.standin.empty()) {
      auto it = nat_of_name.find(m.standin);
      if (it != nat_of_name.end()) p.standin_nat = it->second;
    }
    plan[m.dex] = p;
    by_model[m.name] = &m;
  }
  std::string xd_toc_name, xd_pf_name, xd_common_name;
  for (const auto& f : xd.files()) {
    const std::string l = lower(f.first);
    if (l == "gsfsys.toc") xd_toc_name = f.first;
    if (l == "poke_face.fsys") xd_pf_name = f.first;
  }
  if (xd_toc_name.empty() || xd_pf_name.empty()) throw Fail("The XD disc has no GSfsys.toc / poke_face.fsys.", 2);
  const std::vector<uint8_t> xd_common = xd.read("common.fsys");
  SpeciesSet ss = gen4_species(pd, xdt, xd_common, plan, mv.built);

  fs::path pack = fs::absolute(fs::u8path(out_folder));
  fs::path tmp = pack;
  tmp += ".tmp";
  std::error_code ec;
  if (fs::is_directory(tmp, ec)) fs::remove_all(tmp, ec);
  fs::create_directories(tmp / "files");
  fs::create_directories(tmp / "data");

  const std::vector<CryOut> cry_all = gen4_cries(pbr);
  const std::vector<FaceOut> face_all = gen4_faces(pbr, xd);
  const std::vector<IconOut> icon_all = gen4_icons(pbr, xd);

  struct CryRef { int dex, voice_from, n, hdr_id, body_id; };
  struct PkxRef { int row, group; uint32_t res; std::string model; int dex; };
  std::vector<CryRef> cries;
  std::vector<PkxRef> pkx;
  Json add = Json::object(), edit = Json::object();
  int model_k = 0;
  for (SpeciesRec& s : ss.species) {
    const bool existing = s.xd_species_id < kFirstNewSpecies;
    std::vector<std::pair<std::string, Json>> fl;
    for (auto& f : species_fields(s.rec)) fl.emplace_back(f.first, is_single(f.first) ? Json(f.second[0]) : ints(f.second));
    auto setfl = [&](const std::string& k, Json v) {
      for (auto& kv : fl)
        if (kv.first == k) { kv.second = std::move(v); return; }
      fl.emplace_back(k, std::move(v));
    };
    setfl("mitaFlag", s.mita);
    setfl("tukamaetaFlag", s.tuka);
    if (s.has_model) {
      const int row = kPkxCount + model_k;
      pkx.push_back({row, kPkxGroupFirst + model_k, kPkxRes, s.model, s.dex});
      setfl("pkxDataId", row);
      model_k++;
    } else {
      setfl("pkxDataId", 418);
    }
    if (s.dex != 438) {
      Json sp = Json::array();
      sp.push(kFaceCount + 2 * face_k(s.dex));
      sp.push(kFaceCount + 2 * face_k(s.dex) + 1);
      setfl("cgData.spriteId", sp);
    }
    if (s.dex != 438) {
      const int j = (int)cries.size();
      cries.push_back({s.dex, be16(s.rec, 0x0C), j, 0, 0});
      Json pb = Json::array();
      pb.push((int64_t)((((uint32_t)kIconFirstId + 2 * face_k(s.dex)) << 16) | 0x3200u));
      pb.push((int64_t)((((uint32_t)kIconFirstId + 2 * face_k(s.dex) + 1) << 16) | 0x3200u));
      setfl("cgData.pokebodyId", pb);
      setfl("nakigoeId", kWaveOld + j);
      setfl("voice", kSndDataCount + j);
    } else {
      setfl("voice", 1269);
    }
    Json d = Json::object();
    if (existing) {
      s.key = "#" + std::to_string(s.xd_species_id);
      for (auto& kv : fl) d.set(kv.first, kv.second);
      edit.set(s.key, d);
    } else {
      const std::string name = title(s.name);
      d.set("copy", s.standin_species_id ? Json(s.standin_species_id) : Json());
      for (auto& kv : fl) d.set(kv.first, kv.second);
      Json t = Json::object();
      std::string shown = name;
      for (char& c : shown) c = (char)std::toupper((unsigned char)c);
      t.set("name", shown);
      auto cat = kCategory.find(s.dex);
      if (cat != kCategory.end()) {
        std::string up = cat->second;
        for (char& c : up) c = (char)std::toupper((unsigned char)c);
        t.set("typeName", up);
      }
      d.set("text", t);
      s.key = name;
      add.set(name, d);
    }
  }
  std::map<int, std::string> by_id;
  for (const SpeciesRec& s : ss.species) by_id[s.xd_species_id] = s.key;
  auto remap = [&](Json& d) {
    Json* e = d.find("sinka.pokemonDataId");
    for (Json& v : e->a) {
      const int id = (int)v.i;
      if (id >= kFirstNewSpecies - 2) {
        auto it = by_id.find(id);
        if (it != by_id.end()) v = Json(it->second);
      }
    }
  };
  for (auto& kv : add.o) remap(kv.second);
  for (auto& kv : edit.o) remap(kv.second);
  std::map<int, std::string> nm;
  for (const SpeciesRec& s : ss.species) nm[s.dex] = s.key;
  std::vector<std::string> evo_lines;
  if (Json* happiny = add.find("Happiny")) {
    for (const char* f : {"sinka.kind", "sinka.buff", "sinka.pokemonDataId"}) {
      Json z = Json::array();
      for (int i = 0; i < 5; i++) z.push((int64_t)0);
      happiny->set(f, z);
    }
    evo_lines.push_back("evo Happiny 17 Oval_Stone #113");
  }
  for (const ExistingPatch& p : ss.patches) {
    auto item = kItemEvolutions.find(p.to_name);
    if (item != kItemEvolutions.end()) {
      std::string to = nm.at(p.to_dex);
      for (char& c : to)
        if (c == ' ') c = '_';
      evo_lines.push_back("evo #" + std::to_string(p.xd_species_id) + " " + std::to_string(item->second.first) + " " + item->second.second + " " + to);
      continue;
    }
    if (p.slot < 0) continue;
    const std::string key = "#" + std::to_string(p.xd_species_id);
    Json* d = edit.find(key);
    if (!d) d = &edit.set(key, Json::object());
    int kind = p.kind, buff = p.buff;
    if (p.to_name == "MAGNEZONE" || p.to_name == "PROBOPASS") {
      kind = 7;
      buff = 96;
    }
    d->set("sinka.kind[" + std::to_string(p.slot) + "]", kind);
    d->set("sinka.buff[" + std::to_string(p.slot) + "]", buff);
    d->set("sinka.pokemonDataId[" + std::to_string(p.slot) + "]", nm.at(p.to_dex));
  }
  for (const ExistingMachines& m : ss.machines) {
    const std::string key = "#" + std::to_string(m.xd_species_id);
    Json* d = edit.find(key);
    if (!d) d = &edit.set(key, Json::object());
    for (int i : m.machines) d->set("wazaMcn[" + std::to_string(i) + "]", 1);
  }
  {
    Json doc = Json::object();
    doc.set("_note", "made by tools/pbr_convert/gen4_pack.py from the player's own discs: game data, never in the repository");
    Json a = Json::object();
    a.set("pokemon", add);
    doc.set("add", a);
    doc.set("pokemon", edit);
    write_text(tmp / "data" / "02_species.json", doc.dump(1));
  }

  std::vector<std::pair<uint32_t, std::string>> toc_rows;
  for (const PkxRef& p : pkx) {
    auto it = by_model.find(p.model);
    if (it == by_model.end()) throw Fail("The converted model " + p.model + " is missing.");
    const std::string name = "pkx_g4_" + p.model;
    write_bytes(tmp / "files" / (name + ".fsys"), build_fsys((uint32_t)p.group, {{p.res, 15, p.model, it->second->data,
                                                                                  (uint32_t)it->second->data.size(), false}}));
    toc_rows.emplace_back((uint32_t)p.group, name);
  }
  std::map<int, const CryOut*> cry_by_dex;
  for (const CryOut& c : cry_all) cry_by_dex[c.dex] = &c;
  std::vector<FsEnt> entries;
  for (CryRef& c : cries) {
    auto it = cry_by_dex.find(c.dex);
    if (it == cry_by_dex.end()) throw Fail("No cry for Pokemon " + std::to_string(c.dex));
    c.hdr_id = 0x1000 + 2 * c.n;
    c.body_id = 0x1001 + 2 * c.n;
    entries.push_back({((uint32_t)c.hdr_id << 16) | (20u << 9), 20, "(null)", it->second->hdr, (uint32_t)it->second->hdr.size(), false});
    entries.push_back({((uint32_t)c.body_id << 16) | (21u << 9), 21, "(null)", it->second->body, (uint32_t)it->second->body.size(), false});
  }
  {
    std::string cry_name;
    for (const auto& f : xd.files())
      if (lower(f.first) == "stm_se_nakigoe_archive.fsys") cry_name = f.first;
    if (cry_name.empty()) throw Fail("The XD disc has no stm_se_nakigoe_archive.fsys.", 2);
    const std::vector<uint8_t> cd = xd.read(cry_name);
    const uint32_t cinfo = be32(cd, 0x18), ctable = be32(cd, cinfo);
    std::vector<FsEnt> old;
    std::set<uint32_t> used;
    for (const FsysEntry& e : fsys_entries(cd)) {
      FsEnt f;
      f.word0 = be32(cd, be32(cd, ctable + 4 * (size_t)e.index));
      f.type = e.type;
      f.name = e.name;
      const size_t len = e.packed ? e.packed_size : e.size;
      if ((size_t)e.offset + len > cd.size()) throw Fail("stm_se_nakigoe_archive.fsys: a member runs past the end");
      f.data.assign(cd.begin() + e.offset, cd.begin() + e.offset + len);
      f.size = e.size;
      f.packed = e.packed;
      used.insert(f.word0 >> 16);
      old.push_back(std::move(f));
    }
    for (const CryRef& c : cries)
      if (used.count((uint32_t)c.hdr_id) || used.count((uint32_t)c.body_id)) throw Fail("cry member id clash");
    std::vector<FsEnt> out;
    for (const FsEnt& e : old)
      if (e.type == 20) out.push_back(e);
    for (const FsEnt& e : entries)
      if (e.type == 20) out.push_back(e);
    for (const FsEnt& e : old)
      if (e.type == 21) out.push_back(e);
    for (const FsEnt& e : entries)
      if (e.type == 21) out.push_back(e);
    for (const FsEnt& e : old)
      if (e.type != 20 && e.type != 21) out.push_back(e);
    write_bytes(tmp / "files" / cry_name, build_fsys(be32(cd, 8), out));
  }
  write_bytes(tmp / "files" / xd_toc_name, extend_toc(xd.read(xd_toc_name), toc_rows));
  std::vector<std::pair<int, uint32_t>> faces;
  {
    const std::vector<uint8_t> pf = xd.read(xd_pf_name);
    std::vector<FsEnt> ents;
    const uint32_t info = be32(pf, 0x18), table = be32(pf, info);
    for (const FsysEntry& e : fsys_entries(pf)) {
      const uint32_t h = be32(pf, table + 4 * (size_t)e.index);
      FsEnt f;
      f.word0 = be32(pf, h);
      f.type = e.type;
      f.name = e.name;
      const size_t len = e.packed ? e.packed_size : e.size;
      if ((size_t)e.offset + len > pf.size()) throw Fail("poke_face.fsys: a member runs past the end");
      f.data.assign(pf.begin() + e.offset, pf.begin() + e.offset + len);
      f.size = e.size;
      f.packed = e.packed;
      ents.push_back(std::move(f));
    }
    std::vector<int> dexes;
    for (const SpeciesRec& s : ss.species)
      if (s.dex != 438) dexes.push_back(s.dex);
    std::sort(dexes.begin(), dexes.end());
    std::map<int, const FaceOut*> face_by_dex;
    for (const FaceOut& f : face_all) face_by_dex[f.dex] = &f;
    for (int dex : dexes) {
      const int k = face_k(dex);
      auto it = face_by_dex.find(dex);
      if (it == face_by_dex.end()) continue;
      const uint32_t fid = 0x2000 + k;
      char nmb[32];
      std::snprintf(nmb, sizeof nmb, "g4face%03d", dex);
      ents.push_back({(fid << 16) | (9u << 9), 9, nmb, it->second->data, (uint32_t)it->second->data.size(), false});
      faces.emplace_back(dex, fid);
    }
    write_bytes(tmp / "files" / xd_pf_name, build_fsys(be32(pf, 8), ents));
  }
  {
    std::string dance_name;
    for (const auto& f : xd.files())
      if (lower(f.first) == "poke_dance.fsys") dance_name = f.first;
    if (dance_name.empty()) throw Fail("The XD disc has no poke_dance.fsys.", 2);
    const std::vector<uint8_t> pdn = xd.read(dance_name);
    std::vector<FsEnt> ents;
    std::set<uint32_t> used;
    const uint32_t info = be32(pdn, 0x18), table = be32(pdn, info);
    for (const FsysEntry& e : fsys_entries(pdn)) {
      const uint32_t h = be32(pdn, table + 4 * (size_t)e.index);
      FsEnt f;
      f.word0 = be32(pdn, h);
      f.type = e.type;
      f.name = e.name;
      const size_t len = e.packed ? e.packed_size : e.size;
      if ((size_t)e.offset + len > pdn.size()) throw Fail("poke_dance.fsys: a member runs past the end");
      f.data.assign(pdn.begin() + e.offset, pdn.begin() + e.offset + len);
      f.size = e.size;
      f.packed = e.packed;
      used.insert(f.word0 >> 16);
      ents.push_back(std::move(f));
    }
    std::vector<int> dexes;
    for (const SpeciesRec& s : ss.species)
      if (s.dex != 438) dexes.push_back(s.dex);
    std::sort(dexes.begin(), dexes.end());
    std::map<int, const IconOut*> icon_by_dex;
    for (const IconOut& i : icon_all) icon_by_dex[i.dex] = &i;
    for (int dex : dexes) {
      auto it = icon_by_dex.find(dex);
      if (it == icon_by_dex.end()) throw Fail("No PC box picture for Pokemon " + std::to_string(dex));
      for (int shiny = 0; shiny < 2; shiny++) {
        const uint32_t id = (uint32_t)(kIconFirstId + 2 * face_k(dex) + shiny);
        if (used.count(id)) throw Fail("poke_dance member id clash");
        char nmb[32];
        std::snprintf(nmb, sizeof nmb, "g4body%03d%s", dex, shiny ? "_c" : "");
        const std::vector<uint8_t>& d = shiny ? it->second->shiny : it->second->normal;
        ents.push_back({(id << 16) | 0x3200u, 25, nmb, d, (uint32_t)d.size(), false});
      }
    }
    write_bytes(tmp / "files" / dance_name, build_fsys(be32(pdn, 8), ents));
  }
  fs::create_directories(tmp / "optional");
  {
    std::map<std::string, int> ids;
    for (const SpeciesRec& sp : ss.species) ids[sp.key] = sp.xd_species_id;
    Json spots = Json::object();
    spots.set("_note", "Poke Spot odds of the Gen 4 set: the originals keep 65 % (33/23/9); the Gen 4 rows are appended (spots_tables.txt)");
    std::string rows = "format 1\n";
    for (const SpotTable& t : kSpots) {
      Json tab = Json::object();
      for (int i = 0; i < 3; i++) {
        Json one = Json::object();
        Json pr = Json::array();
        pr.push(t.probs[i]);
        pr.push(t.rates[i]);
        one.set("prob, rate", pr);
        tab.set("#" + std::to_string(i), one);
      }
      spots.set(t.table, tab);
      for (size_t k = 0; k < t.adds.size(); k++) {
        auto it = ids.find(t.adds[k].name);
        if (it == ids.end()) throw Fail(std::string("A Poke Spot species is missing: ") + t.adds[k].name);
        char buf[200];
        std::snprintf(buf, sizeof buf, "row %s %d 1 %d 0:1:10 1:1:%d 2:2:%d 4:4:%d 8:4:%d\n", t.table.c_str(), 3 + (int)k, t.adds[k].rate_of,
                      t.top, it->second, t.adds[k].prob, t.rates[t.adds[k].rate_of]);
        rows += buf;
      }
    }
    write_text(tmp / "optional" / "spots.json", spots.dump(1));
    write_text(tmp / "optional" / "spots_tables.txt", rows);
  }
  {
    std::string m = mv.json;
    write_text(tmp / "data" / "01_moves.json", m);
  }
  {
    const uint32_t seen_ref = ((uint32_t)ss.xd_rec1[0x24] << 24) | (ss.xd_rec1[0x25] << 16) | (ss.xd_rec1[0x26] << 8) | ss.xd_rec1[0x27];
    const uint32_t caught_ref = ((uint32_t)ss.xd_rec1[0x28] << 24) | (ss.xd_rec1[0x29] << 16) | (ss.xd_rec1[0x2A] << 8) | ss.xd_rec1[0x2B];
    std::string out = "format 1\npkxcount " + std::to_string(kPkxCount) + "\n";
    char buf[256];
    for (const CryRef& c : cries) {
      std::snprintf(buf, sizeof buf, "row snd_file %d 1 -1 0:4:%d 4:4:%u 8:4:%u\n", kSndFileCount + c.n, kCryGroup, ((unsigned)c.hdr_id << 16) | (20u << 9),
                    ((unsigned)c.body_id << 16) | (21u << 9));
      out += buf;
      std::snprintf(buf, sizeof buf, "row snd_data %d 1 %d 6:2:%d\n", kSndDataCount + c.n, c.voice_from, kSndFileCount + c.n);
      out += buf;
    }
    std::snprintf(buf, sizeof buf, "row flag_data %d 107 %u\n", kFlagCount, seen_ref);
    out += buf;
    std::snprintf(buf, sizeof buf, "row flag_data %d 107 %u\n", kFlagCount + 107, caught_ref);
    out += buf;
    for (const PkxRef& p : pkx) {
      std::snprintf(buf, sizeof buf, "pkx %d %d %u\n", p.row, p.group, (unsigned)p.res);
      out += buf;
    }
    for (const CryRef& c : cries) {
      auto it = cry_by_dex.find(c.dex);
      const std::vector<uint8_t> w = gen4_wave(*it->second);
      std::string hex;
      for (uint8_t b : w) {
        std::snprintf(buf, sizeof buf, "%02x", (unsigned)b);
        hex += buf;
      }
      out += "wave " + std::to_string(c.dex) + " " + hex + "\n";
    }
    std::vector<std::tuple<int, int, int>> face_rows;
    for (const auto& f : faces) {
      const int k = face_k(f.first);
      std::snprintf(buf, sizeof buf, "row poke_face %d 1 -1 0:1:64 4:4:%u\n", kFaceCount + 2 * k, (f.second << 16) | 0x1200u);
      out += buf;
      std::snprintf(buf, sizeof buf, "row poke_face %d 1 -1 0:1:192 4:4:%u\n", kFaceCount + 2 * k + 1, (f.second << 16) | 0x1200u);
      out += buf;
      face_rows.emplace_back(f.first, kFaceCount + 2 * k, kFaceCount + 2 * k + 1);
    }
    for (const auto& r : face_rows) {
      std::snprintf(buf, sizeof buf, "face %d %d %d\n", std::get<0>(r), std::get<1>(r), std::get<2>(r));
      out += buf;
    }
    for (const SpeciesRec& s : ss.species) {
      std::string key = s.key;
      for (char& c : key)
        if (c == ' ') c = '_';
      out += "species " + std::to_string(s.dex) + " " + std::to_string(s.xd_species_id) + " " + key + "\n";
    }
    {
      std::vector<std::pair<std::string, int>> new_species;
      for (const SpeciesRec& s : ss.species) {
        if (s.xd_species_id < kFirstNewSpecies) continue;
        std::string key = s.key;
        for (char& c : key)
          if (c == ' ') c = '_';
        new_species.emplace_back(key, s.dex);
      }
      const MachinesOut machines = gen4_machines(pd, xdt, new_species);
      write_text(tmp / "machines.txt", machines.text);
      const ItemsOut items = gen4_items(xdt, machines);
      write_text(tmp / "data" / "00_items.json", items.json);
      for (const std::string& g : items.goods) out += g + "\n";
    }
    write_text(tmp / "tables.txt", out);
  }
  {
    auto key_of = [&](int dex) {
      std::string k;
      for (const SpeciesRec& s : ss.species)
        if (s.dex == dex) k = s.key;
      for (char& c : k)
        if (c == ' ') c = '_';
      return k;
    };
    std::string out = "format 1\ncord Linking_Cord\neevee " + key_of(470) + " 98 " + key_of(471) + " Ice_Stone\n";
    for (const std::string& l : evo_lines) out += l + "\n";
    write_text(tmp / "evolutions.txt", out);
  }
  {
    std::string rz = "format 1\n";
    int nid = kFirstNewSpecies;
    static const std::set<int> legend = {480, 481, 482, 483, 484, 485, 486, 487, 488, 490, 491, 492, 493};
    auto join = [](const std::vector<int>& v, size_t n) {
      std::string s;
      for (size_t i = 0; i < n && i < v.size(); i++) s += (i ? " " : "") + std::to_string(v[i]);
      return s;
    };
    for (const SpeciesRec& s : ss.species) {
      if (s.xd_species_id < kFirstNewSpecies) continue;
      std::map<std::string, std::vector<int>> f;
      for (auto& kv : species_fields(s.rec)) f[kv.first] = kv.second;
      rz += "sp " + std::to_string(nid) + " " + std::to_string(s.dex) + " " + (legend.count(s.dex) ? "1" : "0") + " " +
            join(f["basisStatus"], 6) + " | " + join(f["zokuseiDataId"], 99) + " | " + join(f["tokuseiDataId"], 99) + " | " +
            join(f["sinka.pokemonDataId"], 99) + " | " + join(f["waza.level"], 99) + " | " + join(f["waza.dataId"], 99) + "\n";
      nid++;
    }
    write_text(tmp / "randomizer.txt", rz);
  }
  {
    Json g = Json::object();
    g.set("id", "battle-revolution-gen4");
    g.set("version", kPackVersion);
    g.set("species", ss.species.size());
    g.set("models", pkx.size());
    g.set("cries", cries.size());
    g.set("game", "GXXE01");
    g.set("generated_from", "RPBE01");
    write_text(tmp / "gen4.json", g.dump(1));
  }
  fs::path old = pack;
  old += ".old";
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
  if (fs::is_directory(pack, ec)) fs::rename(pack, old);
  fs::rename(tmp, pack);
  if (fs::is_directory(old, ec)) fs::remove_all(old, ec);
}

}
