#include "xd_faces.h"

#include <gs_formats.h>
#include <mod_layout.h>
#include <mods.h>
#include <save_thumbnail.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace em = engine::mods;
namespace th = engine::memcard::thumbnail;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  if (v.size() < at + 4) v.resize(at + 4);
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}
uint32_t get32(const std::vector<uint8_t>& v, size_t at) {
  return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3];
}
size_t align_to(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

std::vector<uint8_t> make_archive(const std::vector<std::string>& names, const std::vector<uint8_t>& file) {
  const size_t n = names.size();
  std::vector<uint8_t> a(0x60, 0);
  std::memcpy(a.data(), "FSYS", 4);
  put32(a, 0x08, 0x5C0);
  put32(a, 0x0C, uint32_t(n));
  put32(a, 0x18, 0x40);
  const size_t table = 0x60, namesAt = table + align_to(4 * n, 16);
  std::string pool;
  std::vector<size_t> nameAt;
  for (const std::string& s : names) {
    nameAt.push_back(namesAt + pool.size());
    pool += s + std::string(1, '\0');
  }
  const size_t hdr = namesAt + align_to(pool.size(), 16);
  const size_t dataStart = align_to(hdr + 0x70 * n, 32);
  put32(a, 0x40, uint32_t(table));
  put32(a, 0x44, uint32_t(namesAt));
  put32(a, 0x48, uint32_t(dataStart));
  put32(a, 0x1C, uint32_t(dataStart));
  a.resize(dataStart, 0);
  std::memcpy(a.data() + namesAt, pool.data(), pool.size());
  size_t pos = dataStart;
  for (size_t i = 0; i < n; i++) {
    uint32_t packed = 0;
    uint8_t* p = Lzss_Pack(file.data(), uint32_t(file.size()), &packed);
    pos = align_to(pos, 32);
    a.resize(pos + packed, 0);
    std::memcpy(a.data() + pos, p, packed);
    std::free(p);
    put32(a, table + 4 * i, uint32_t(hdr + 0x70 * i));
    const size_t h = hdr + 0x70 * i;
    put32(a, h, (0x0D07u + uint32_t(i)) << 16 | 0x1200);
    put32(a, h + 4, uint32_t(pos));
    put32(a, h + 8, uint32_t(file.size()));
    put32(a, h + 0x0C, 0x80000000u);
    put32(a, h + 0x14, packed);
    put32(a, h + 0x20, 9);
    put32(a, h + 0x24, uint32_t(nameAt[i]));
    pos += packed;
  }
  a.resize(align_to(a.size(), 32) + 0x20, 0);
  std::memcpy(a.data() + a.size() - 4, "FSYS", 4);
  put32(a, 0x20, uint32_t(a.size()));
  return a;
}

th::Picture gradient(uint32_t w, uint32_t h, bool clearCorner) {
  th::Picture p;
  p.width = w, p.height = h, p.rgba.resize(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* d = &p.rgba[(size_t(y) * w + x) * 4];
      const bool lower = y >= h / 2;
      d[0] = uint8_t(x * 255 / (w - 1)), d[1] = uint8_t(lower ? 200 : 20), d[2] = uint8_t(lower ? 20 : 200), d[3] = (clearCorner && x < w / 8 && y % (h / 2) < h / 16) ? 0 : 255;
    }
  }
  return p;
}

std::vector<uint8_t> png_of(const th::Picture& p) { return th::EncodePng(p); }

}

int main() {
  std::vector<uint8_t> templ(0x1240, 0);
  put32(templ, 0, 0x002A0054);
  templ[4] = 8, templ[5] = 1;
  put32(templ, 0x28, 0x80);
  put32(templ, 0x48, 0x1040);
  put32(templ, 0x4C, 0x1040);

  {
    const th::Picture pic = gradient(84, 168, true);
    const std::vector<uint8_t> tex = xd::faces::TextureFromPicture(templ, pic);
    check(tex.size() == 0x1240 && get32(tex, 0) == 0x002A0054 && get32(tex, 0x48) == 0x1040, "texture: 0x1240 bytes with the template's header");
    th::Picture back;
    check(xd::faces::DecodeTexture(tex, back) && back.width == 42 && back.height == 84, "texture: decodes to 42 x 84");
    const auto at = [&](uint32_t x, uint32_t y) { return &back.rgba[(size_t(y) * 42 + x) * 4]; };
    check(at(30, 10)[2] > at(30, 10)[1] && at(30, 60)[1] > at(30, 60)[2], "texture: the normal face (top) and the shiny face (bottom) keep their colours");
    check(at(0, 0)[3] == 0 && at(30, 20)[3] == 255, "texture: transparency is kept");
    check(at(41, 10)[0] > at(1, 10)[0] + 100, "texture: the picture runs left to right");
    const th::Picture one = gradient(100, 100, false);
    const std::vector<uint8_t> t2 = xd::faces::TextureFromPicture(templ, one);
    th::Picture b2;
    xd::faces::DecodeTexture(t2, b2);
    const uint8_t* top = &b2.rgba[(size_t(10) * 42 + 20) * 4];
    const uint8_t* bottom = &b2.rgba[(size_t(52) * 42 + 20) * 4];
    check(std::abs(int(top[0]) - int(bottom[0])) < 12 && std::abs(int(top[1]) - int(bottom[1])) < 12, "texture: a square picture is used for both halves");
    th::Picture noisy;
    noisy.width = 84, noisy.height = 168, noisy.rgba.resize(84 * 168 * 4);
    uint32_t seed = 7;
    for (auto& b : noisy.rgba) b = uint8_t((seed = seed * 1103515245 + 12345) >> 16);
    for (size_t i = 3; i < noisy.rgba.size(); i += 4) noisy.rgba[i] = 255;
    check(xd::faces::TextureFromPicture(templ, noisy).size() == 0x1240, "texture: a picture with thousands of colours still makes a 256-colour texture");
    check(xd::faces::TextureFromPicture(std::vector<uint8_t>(100, 0), pic).empty(), "texture: a template that is too small gives nothing");
  }

  const fs::path root = fs::temp_directory_path() / "xd_faces_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "mod" / "textures" / "faces");
  {
    const std::vector<uint8_t> png = png_of(gradient(84, 168, false));
    std::ofstream(root / "mod" / "textures" / "faces" / "z.png", std::ios::binary).write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    std::ofstream(root / "mod" / "textures" / "faces" / "broken.png", std::ios::binary) << "not a png";
  }
  em::DataCatalog catalog;
  {
    em::DataTable species;
    species.name = "pokemon_data";
    species.entrySize = 0x124;
    species.count = 4;
    species.places = {{"common.fsys", "common_rel", 0}};
    em::DataField f;
    f.name = "cgData.spriteId";
    f.size = 2, f.offset = 0x116 - 0x100, f.count = 2, f.stride = 8;
    species.entrySize = 0x24;
    species.fields.push_back(f);
    catalog.tables.push_back(species);
    em::DataTable face;
    face.name = "poke_face";
    face.entrySize = 8;
    face.count = 10;
    face.places = {{"common.fsys", "faces", 0}};
    catalog.tables.push_back(face);
    em::NameList names{"species", {{"Alpha", 1}, {"Beta", 2}, {"Gamma", 3}}, 4};
    catalog.lists.push_back(names);
  }
  std::vector<uint8_t> speciesTable(4 * 0x24, 0);
  for (uint32_t n = 0; n < 4; n++) {
    speciesTable[n * 0x24 + 0x16] = 0, speciesTable[n * 0x24 + 0x17] = uint8_t(n + 2);
    speciesTable[n * 0x24 + 0x1E] = 0, speciesTable[n * 0x24 + 0x1F] = uint8_t(n + 6);
  }
  const std::vector<uint8_t> archive = make_archive({"face001", "face002", "face133"}, templ);

  const auto make_row = [&](const std::string& key, uint32_t id, const std::string& face) {
    em::AddedRow r;
    r.table = "pokemon_data";
    r.id = r.row = id;
    r.key = key;
    r.mod = "m";
    r.modName = "M";
    r.where = "data/x.json line 3";
    r.bytes.assign(0x24, 0);
    r.bytes[0x17] = 1, r.bytes[0x1F] = 5;
    r.assets["face"] = face;
    return r;
  };
  std::vector<em::AddedRow> rows = {make_row("Zed", 4, "textures/faces/z.png"), make_row("Yak", 5, "Gamma"), make_row("Xan", 6, "textures/faces/z.png"),
                                    make_row("Wob", 7, "textures/faces/missing.png"), make_row("Vim", 8, "textures/faces/broken.png"),
                                    make_row("Uno", 9, "Zed"), make_row("Tez", 10, "Nobody")};
  em::RowsJob job;
  job.rows = &rows;
  job.gameId = "GXXE01";
  job.modFolders["m"] = root / "mod";
  job.catalog = &catalog;
  job.discFiles = {"poke_face.fsys"};
  job.readFile = [&](const std::string& path, std::vector<uint8_t>& out) {
    if (path != "poke_face.fsys") return false;
    out = archive;
    return true;
  };
  job.readEntry = [&](const std::string& a, const std::string& e, std::vector<uint8_t>& out) {
    if (a == "common.fsys" && e == "common_rel") {
      out = speciesTable;
      return true;
    }
    return false;
  };
  std::vector<std::string> said;
  job.say = [&](em::Message::Level, const std::string& t) { said.push_back(t); };
  xd::faces::BuildFaces(job);

  const auto sprites = [&](const em::AddedRow& r) { return std::make_pair(uint32_t(r.bytes[0x16] << 8 | r.bytes[0x17]), uint32_t(r.bytes[0x1E] << 8 | r.bytes[0x1F])); };
  check(sprites(rows[0]) == std::make_pair(10u, 11u), "step: the picture species gets the two new poke_face rows (the table had 10)");
  check(sprites(rows[2]) == sprites(rows[0]), "step: the same picture for a second species is stored once");
  check(sprites(rows[1]) == std::make_pair(5u, 9u), "step: \"face\": \"Gamma\" takes that species' sprite ids from the disc's table");
  check(sprites(rows[3]) == std::make_pair(1u, 5u) && sprites(rows[4]) == std::make_pair(1u, 5u), "step: a missing or broken picture leaves the copy's face");
  check(sprites(rows[5]) == sprites(rows[0]), "step: a face can name a species the mod adds before");
  check(sprites(rows[6]) == std::make_pair(1u, 5u), "step: a name that is no species leaves the copy's face");
  check(said.size() == 3, "step: three problems told to the player (missing, broken, unknown name) (got " + std::to_string(said.size()) + ")");
  size_t faceRows = 0;
  uint32_t texNormal = 0, texShiny = 0;
  for (const em::AddedRow& r : rows) {
    if (r.table != "poke_face") continue;
    faceRows++;
    (r.bytes[0] & 0x80 ? texShiny : texNormal) = get32(r.bytes, 4);
    check(r.bytes.size() == 8 && (r.bytes[0] == 0x40 || r.bytes[0] == 0xC0), "step: a poke_face row is 8 bytes: 0x40 normal, 0xC0 shiny");
  }
  check(faceRows == 2 && texNormal == texShiny && (texNormal >> 16) == 0x0D07 + 3 && (texNormal & 0xFFFF) == 0x1200, "step: the rows name one new texture entry (the next resource id, the template's low bits)");
  check(job.outFiles.size() == 1 && job.outFiles[0].discPath == "poke_face.fsys", "step: the longer archive is handed back as a file");
  if (job.outFiles.size() == 1) {
    FsysArchive f;
    const std::vector<uint8_t>& out = job.outFiles[0].data;
    check(Fsys_Open(out.data(), out.size(), &f) && f.count == 4, "step: the archive has one more entry");
    uint32_t n = 0;
    uint8_t* p = Fsys_Extract(&f, 3, &n);
    th::Picture decoded;
    check(p != nullptr && n == 0x1240 && xd::faces::DecodeTexture(std::vector<uint8_t>(p, p + n), decoded) && decoded.rgba[(size_t(10) * 42 + 30) * 4 + 2] > 100, "step: the new entry is the picture");
    std::free(p);
    FsysEntry e;
    check(Fsys_Entry(&f, 3, &e) && (uint32_t(e.id) << 16 | 0x1200) == texNormal, "step: its resource id is the one in the rows");
  }
  {
    std::vector<em::AddedRow> none;
    em::RowsJob j2;
    j2.rows = &none;
    j2.gameId = "GXXE01";
    j2.modFolders["m"] = root / "mod";
    j2.catalog = &catalog;
    j2.discFiles = job.discFiles;
    j2.readFile = job.readFile;
    j2.readEntry = job.readEntry;
    std::vector<std::string> said2;
    j2.say = [&](em::Message::Level, const std::string& t) { said2.push_back(t); };
    const auto ask = [&](uint32_t entry, const std::string& text) {
      em::EditedAsset a;
      a.table = "pokemon_data", a.entry = entry, a.key = "face", a.text = text, a.mod = "m", a.modName = "M", a.where = "data/x.json line 9";
      return a;
    };
    j2.editedAssets = {ask(3, "textures/faces/z.png"), ask(1, "Gamma"), ask(2, "Nobody")};
    xd::faces::BuildPictures(j2);
    check(none.size() == 2 && none[0].table == "poke_face" && none[1].table == "poke_face", "disc rows: the picture makes two poke_face rows and no look-alike species row stays in the list");
    check(j2.editedRows.size() == 2, "disc rows: two rows come back with new bytes (the unknown name and a disc row nobody asked about stay as the disc has them) (got " + std::to_string(j2.editedRows.size()) + ")");
    uint32_t n1 = 0, s1 = 0, n3 = 0, s3 = 0;
    for (const em::RowsJob::EditedRow& e : j2.editedRows) {
      const uint32_t n = uint32_t(e.bytes[0x16] << 8 | e.bytes[0x17]), sh = uint32_t(e.bytes[0x1E] << 8 | e.bytes[0x1F]);
      check(e.table == "pokemon_data" && e.bytes.size() == 0x24, "disc rows: a whole row of the table's size");
      if (e.entry == 1) n1 = n, s1 = sh;
      if (e.entry == 3) n3 = n, s3 = sh;
    }
    check(n1 == 5 && s1 == 9, "disc rows: \"face\": \"Gamma\" on a disc row gives it that species' sprite ids");
    check(n3 == 10 && s3 == 11, "disc rows: a picture on a disc row gives it the two new poke_face rows");
    check(said2.size() == 1, "disc rows: the unknown name is told to the player once (got " + std::to_string(said2.size()) + ")");
    check(j2.outFiles.size() == 1 && j2.outFiles[0].discPath == "poke_face.fsys", "disc rows: the longer archive is handed back");
    std::vector<em::AddedRow> none2;
    em::RowsJob j3 = j2;
    j3.rows = &none2;
    j3.editedAssets.clear(), j3.editedRows.clear(), j3.outFiles.clear();
    xd::faces::BuildPictures(j3);
    check(none2.empty() && j3.editedRows.empty() && j3.outFiles.empty(), "disc rows: no key given = no change at all");
  }
  fs::remove_all(root, ec);
  std::printf(sOk ? "xd_faces_test: all passed\n" : "xd_faces_test: FAILED\n");
  return sOk ? 0 : 1;
}
