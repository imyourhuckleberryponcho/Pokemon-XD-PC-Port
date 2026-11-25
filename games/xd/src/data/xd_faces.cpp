#include "xd_faces.h"

#include <gs_formats.h>
#include <mod_layout.h>
#include <save_thumbnail.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace xd::faces {

namespace em = engine::mods;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kW = 42, kH = 84, kStride = 48;
constexpr size_t kHeader = 0x80, kPalette = 0x1040, kFileSize = 0x1240;

std::string LowerText(std::string s) {
  for (char& c : s) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  return s;
}

bool EndsWith(const std::string& s, const char* suffix) {
  const std::string t = LowerText(s), u = suffix;
  return t.size() >= u.size() && t.compare(t.size() - u.size(), u.size(), u) == 0;
}

uint32_t Be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3]; }
void PutBe16(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v >> 8), p[1] = static_cast<uint8_t>(v); }
void PutBe32(uint8_t* p, uint32_t v) { PutBe16(p, v >> 16), PutBe16(p + 2, v & 0xFFFF); }

struct Rgba {
  uint8_t r, g, b, a;
};

uint16_t ToRgb5A3(Rgba c) {
  if (c.a < 16) return 0x0000;
  if (c.a >= 240) return static_cast<uint16_t>(0x8000 | (c.r >> 3) << 10 | (c.g >> 3) << 5 | (c.b >> 3));
  return static_cast<uint16_t>((c.a >> 5) << 12 | (c.r >> 4) << 8 | (c.g >> 4) << 4 | (c.b >> 4));
}

Rgba FromRgb5A3(uint16_t v) {
  if (v & 0x8000) {
    const auto e = [](uint32_t x) { return static_cast<uint8_t>(x * 255 / 31); };
    return {e((v >> 10) & 31), e((v >> 5) & 31), e(v & 31), 255};
  }
  return {static_cast<uint8_t>(((v >> 8) & 15) * 17), static_cast<uint8_t>(((v >> 4) & 15) * 17), static_cast<uint8_t>((v & 15) * 17),
          static_cast<uint8_t>(((v >> 12) & 7) * 255 / 7)};
}

std::vector<Rgba> Fit(const engine::memcard::thumbnail::Picture& pic) {
  std::vector<Rgba> out(static_cast<size_t>(kW) * kH, Rgba{0, 0, 0, 0});
  const bool two = pic.height * 10 >= pic.width * 18;
  for (uint32_t half = 0; half < 2; half++) {
    const uint32_t srcY0 = two ? half * (pic.height / 2) : 0, srcH = two ? pic.height / 2 : pic.height;
    for (uint32_t y = 0; y < kW; y++) {
      for (uint32_t x = 0; x < kW; x++) {
        const uint32_t x0 = x * pic.width / kW, x1 = std::max(x0 + 1, (x + 1) * pic.width / kW);
        const uint32_t y0 = srcY0 + y * srcH / kW, y1 = std::max(y0 + 1, srcY0 + (y + 1) * srcH / kW);
        uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
        for (uint32_t sy = y0; sy < y1 && sy < pic.height; sy++) {
          for (uint32_t sx = x0; sx < x1 && sx < pic.width; sx++) {
            const uint8_t* p = &pic.rgba[(static_cast<size_t>(sy) * pic.width + sx) * 4];
            r += p[0] * p[3], g += p[1] * p[3], b += p[2] * p[3], a += p[3], n++;
          }
        }
        Rgba c{0, 0, 0, 0};
        if (n != 0 && a != 0) c = {static_cast<uint8_t>(r / a), static_cast<uint8_t>(g / a), static_cast<uint8_t>(b / a), static_cast<uint8_t>(a / n)};
        out[(static_cast<size_t>(half) * kW + y) * kW + x] = c;
      }
    }
  }
  return out;
}

void Quantize(const std::vector<Rgba>& pixels, std::vector<uint16_t>& palette, std::vector<uint8_t>& index) {
  std::map<uint16_t, uint32_t> counts;
  std::vector<uint16_t> values(pixels.size());
  for (size_t i = 0; i < pixels.size(); i++) counts[values[i] = ToRgb5A3(pixels[i])]++;
  palette.assign(1, 0x0000);
  std::map<uint16_t, uint8_t> slot{{0x0000, 0}};
  if (counts.size() <= 256) {
    for (const auto& [v, n] : counts) {
      if (v == 0x0000) continue;
      slot[v] = static_cast<uint8_t>(palette.size());
      palette.push_back(v);
    }
  } else {
    struct Box {
      std::vector<std::pair<uint16_t, uint32_t>> colours;
    };
    Box all;
    for (const auto& [v, n] : counts) {
      if (v != 0x0000) all.colours.emplace_back(v, n);
    }
    std::vector<Box> boxes{all};
    const auto channel = [](uint16_t v, int c) {
      const Rgba p = FromRgb5A3(v);
      return c == 0 ? p.r : c == 1 ? p.g : c == 2 ? p.b : p.a;
    };
    while (boxes.size() < 255) {
      size_t best = 0, bestSpread = 0;
      int bestC = 0;
      for (size_t k = 0; k < boxes.size(); k++) {
        if (boxes[k].colours.size() < 2) continue;
        for (int c = 0; c < 4; c++) {
          int lo = 255, hi = 0;
          for (const auto& [v, n] : boxes[k].colours) lo = std::min<int>(lo, channel(v, c)), hi = std::max<int>(hi, channel(v, c));
          const size_t spread = static_cast<size_t>(hi - lo) * 1000 + boxes[k].colours.size();
          if (spread > bestSpread) bestSpread = spread, best = k, bestC = c;
        }
      }
      if (bestSpread == 0) break;
      auto& cs = boxes[best].colours;
      std::sort(cs.begin(), cs.end(), [&](const auto& a, const auto& b) { return channel(a.first, bestC) < channel(b.first, bestC); });
      uint64_t total = 0, run = 0;
      for (const auto& c : cs) total += c.second;
      size_t cut = 1;
      for (; cut < cs.size() - 1 && run + cs[cut - 1].second < total / 2; cut++) run += cs[cut - 1].second;
      Box second;
      second.colours.assign(cs.begin() + static_cast<std::ptrdiff_t>(cut), cs.end());
      cs.resize(cut);
      boxes.push_back(std::move(second));
    }
    for (const Box& b : boxes) {
      uint64_t r = 0, g = 0, bl = 0, a = 0, n = 0;
      for (const auto& [v, count] : b.colours) {
        const Rgba p = FromRgb5A3(v);
        r += static_cast<uint64_t>(p.r) * count, g += static_cast<uint64_t>(p.g) * count, bl += static_cast<uint64_t>(p.b) * count, a += static_cast<uint64_t>(p.a) * count, n += count;
      }
      if (n == 0) continue;
      const uint16_t mid = ToRgb5A3({static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(bl / n), static_cast<uint8_t>(a / n)});
      const uint8_t at = static_cast<uint8_t>(palette.size());
      palette.push_back(mid == 0x0000 ? static_cast<uint16_t>(0x8000) : mid);
      for (const auto& [v, count] : b.colours) slot[v] = at;
    }
  }
  index.resize(values.size());
  for (size_t i = 0; i < values.size(); i++) index[i] = slot[values[i]];
  palette.resize(256, 0x0000);
}

std::vector<uint8_t> MakeTexture(const std::vector<uint8_t>& templateFile, const std::vector<Rgba>& pixels) {
  std::vector<uint16_t> palette;
  std::vector<uint8_t> index;
  Quantize(pixels, palette, index);
  std::vector<uint8_t> out(templateFile.begin(), templateFile.begin() + static_cast<std::ptrdiff_t>(kFileSize));
  size_t p = kHeader;
  for (uint32_t ty = 0; ty < kH; ty += 4) {
    for (uint32_t tx = 0; tx < kStride; tx += 8) {
      for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 8; x++) {
          const uint32_t px = tx + x, py = ty + y;
          out[p++] = px < kW && py < kH ? index[static_cast<size_t>(py) * kW + px] : 0;
        }
      }
    }
  }
  for (size_t i = 0; i < 256; i++) PutBe16(&out[kPalette + 2 * i], palette[i]);
  return out;
}

}

std::vector<uint8_t> TextureFromPicture(const std::vector<uint8_t>& templateFile, const engine::memcard::thumbnail::Picture& picture) {
  if (templateFile.size() < kFileSize || picture.Empty()) return {};
  return MakeTexture(templateFile, Fit(picture));
}

bool DecodeTexture(const std::vector<uint8_t>& file, engine::memcard::thumbnail::Picture& out) {
  if (file.size() < kFileSize) return false;
  out.width = kW, out.height = kH, out.rgba.assign(static_cast<size_t>(kW) * kH * 4, 0);
  size_t p = kHeader;
  for (uint32_t ty = 0; ty < kH; ty += 4) {
    for (uint32_t tx = 0; tx < kStride; tx += 8) {
      for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 8; x++) {
          const uint8_t i = file[p++];
          if (tx + x >= kW || ty + y >= kH) continue;
          const uint16_t v = static_cast<uint16_t>(file[kPalette + 2 * i] << 8 | file[kPalette + 2 * i + 1]);
          const Rgba c = FromRgb5A3(v);
          uint8_t* d = &out.rgba[(static_cast<size_t>(ty + y) * kW + tx + x) * 4];
          d[0] = c.r, d[1] = c.g, d[2] = c.b, d[3] = c.a;
        }
      }
    }
  }
  return true;
}

namespace { uint32_t sFaceRowsTaken = 0; }
void SetFaceRowsTaken(uint32_t count) { sFaceRowsTaken = count; }

void BuildFaces(em::RowsJob& job) {
  if (job.rows == nullptr) return;
  std::vector<em::AddedRow>& rows = *job.rows;
  const auto say = [&](em::Message::Level level, const std::string& text) {
    if (job.say) job.say(level, text);
  };
  std::vector<size_t> wanting;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].table == "pokemon_data" && rows[i].assets.count("face") != 0) wanting.push_back(i);
  }
  if (wanting.empty()) return;
  const em::DataTable* species = job.catalog != nullptr ? job.catalog->FindTable("pokemon_data") : nullptr;
  const em::DataTable* faceTable = job.catalog != nullptr ? job.catalog->FindTable("poke_face") : nullptr;
  const em::DataField* sprite = nullptr;
  if (species != nullptr) {
    for (const em::DataField& f : species->fields) {
      if (f.name == "cgData.spriteId") sprite = &f;
    }
  }
  if (species == nullptr || faceTable == nullptr || sprite == nullptr || sprite->count < 2 || sprite->size != 2) {
    say(em::Message::Level::Problem, "Faces of added species: the game's face table could not be found on this disc; the species keep the face of the one they copy.");
    return;
  }
  const uint32_t spriteAt[2] = {sprite->offset, sprite->offset + sprite->Stride()};
  const auto spritesOf = [&](const em::AddedRow& r, uint32_t& normal, uint32_t& shiny) {
    if (r.bytes.size() < spriteAt[1] + 2) return false;
    normal = static_cast<uint32_t>(r.bytes[spriteAt[0]]) << 8 | r.bytes[spriteAt[0] + 1];
    shiny = static_cast<uint32_t>(r.bytes[spriteAt[1]]) << 8 | r.bytes[spriteAt[1] + 1];
    return true;
  };

  std::string archiveName = "poke_face.fsys";
  for (const std::string& f : job.discFiles) {
    if (LowerText(f) == "poke_face.fsys") archiveName = f;
  }
  std::vector<uint8_t> archive;
  FsysArchive fsys{};
  std::vector<uint8_t> templateFile;
  uint32_t templateIndex = 0, templateWord0 = 0, maxId = 0;
  const auto openArchive = [&] {
    if (archive.empty()) {
      if (!job.readFile || !job.readFile(archiveName, archive) || !Fsys_Open(archive.data(), archive.size(), &fsys)) {
        archive.clear();
        return false;
      }
      const int named = Fsys_Find(&fsys, "face133");
      templateIndex = named >= 0 ? static_cast<uint32_t>(named) : 0;
      uint32_t size = 0;
      uint8_t* p = Fsys_Extract(&fsys, templateIndex, &size);
      if (p != nullptr) {
        templateFile.assign(p, p + size);
        std::free(p);
      }
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e;
        if (Fsys_Entry(&fsys, i, &e)) maxId = std::max<uint32_t>(maxId, e.id);
      }
      const uint32_t at = Be32(archive.data() + fsys.entryTable + 4 * templateIndex);
      templateWord0 = at + 4 <= archive.size() ? Be32(archive.data() + at) : 0;
    }
    return !archive.empty() && templateFile.size() >= kFileSize;
  };

  uint32_t nextFaceRow = faceTable->count + sFaceRowsTaken;
  for (const em::AddedRow& r : rows) {
    if (r.table == "poke_face") nextFaceRow = std::max(nextFaceRow, r.row + 1);
  }
  struct Made {
    uint32_t normal, shiny;
  };
  std::map<std::string, Made> madeByPicture;
  std::vector<FsysAddition> additions;
  std::vector<std::vector<uint8_t>> contents;
  std::vector<std::string> names;
  std::string modsText;
  std::vector<em::AddedRow> newRows;
  const em::NameList* speciesNames = job.catalog->FindList("species");

  std::map<size_t, std::vector<uint8_t>> original;
  for (size_t index : wanting) original[index] = rows[index].bytes;
  for (size_t index : wanting) {
    em::AddedRow& row = rows[index];
    const std::string spec = row.assets.at("face");
    const std::string what = "The mod \"" + row.mod + "\", the face of \"" + row.key + "\" (" + row.where + ")";
    if (EndsWith(spec, ".png")) {
      const auto folder = job.modFolders.find(row.mod);
      fs::path file;
      if (folder == job.modFolders.end() || !em::ResolveLayoutFile(folder->second, job.gameId, spec, file)) {
        say(em::Message::Level::Problem, what + ": there is no picture " + spec + " in the mod; the species keeps the face of the one it copies.");
        continue;
      }
      const std::string key = file.generic_string();
      auto made = madeByPicture.find(key);
      if (made == madeByPicture.end()) {
        std::vector<uint8_t> raw;
        {
          std::ifstream in(file, std::ios::binary);
          raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        engine::memcard::thumbnail::Picture pic;
        std::string error;
        if (raw.empty() || !engine::memcard::thumbnail::DecodePng(raw, pic, error, true)) {
          say(em::Message::Level::Problem, what + ": " + spec + " is not a PNG the port reads (" + (error.empty() ? std::string("empty file") : error) +
                                               "); use an 8-bit RGB or RGBA PNG that is not interlaced. The species keeps the face of the one it copies.");
          continue;
        }
        if (pic.width < 8 || pic.height < 8 || pic.width > 4096 || pic.height > 4096) {
          say(em::Message::Level::Problem, what + ": " + spec + " must be between 8 and 4096 pixels each way; the species keeps the face of the one it copies.");
          continue;
        }
        if (!openArchive()) {
          say(em::Message::Level::Problem, what + ": the game's face archive (poke_face.fsys) could not be read or has an unknown layout; the species keeps the face of the one it copies.");
          return;
        }
        const uint32_t id = maxId + 1 + static_cast<uint32_t>(additions.size());
        if (id > 0xFFFF) {
          say(em::Message::Level::Problem, what + ": no resource number left in poke_face.fsys.");
          return;
        }
        contents.push_back(MakeTexture(templateFile, Fit(pic)));
        names.push_back("modface" + std::to_string(additions.size() + 1));
        additions.push_back({static_cast<uint16_t>(id), nullptr, nullptr, 0, templateIndex});
        const uint32_t texId = id << 16 | (templateWord0 & 0xFFFF);
        Made m{nextFaceRow, nextFaceRow + 1};
        for (int shiny = 0; shiny < 2; shiny++) {
          em::AddedRow f;
          f.table = "poke_face";
          f.id = f.row = nextFaceRow++;
          f.copyOf = shiny ? 519 : 133;
          f.key = "face of " + row.key + (shiny ? " (shiny)" : "");
          f.mod = row.mod, f.modName = row.modName, f.where = row.where;
          f.bytes = {static_cast<uint8_t>(shiny ? 0xC0 : 0x40), 0, 0, 0};
          f.bytes.resize(8);
          PutBe32(f.bytes.data() + 4, texId);
          newRows.push_back(std::move(f));
        }
        modsText += (modsText.empty() ? "" : ", ") + row.mod;
        made = madeByPicture.emplace(key, m).first;
        std::fprintf(stderr, "[mods] face: %s -> poke_face rows %u and %u, texture entry id %u (modface%zu)\n", spec.c_str(), m.normal, m.shiny, id, additions.size());
      }
      PutBe16(row.bytes.data() + spriteAt[0], made->second.normal);
      PutBe16(row.bytes.data() + spriteAt[1], made->second.shiny);
      continue;
    }
    uint32_t number = 0;
    bool found = false;
    {
      std::string text = spec;
      if (!text.empty() && text[0] == '#') text.erase(0, 1);
      char* end = nullptr;
      const unsigned long n = std::strtoul(text.c_str(), &end, 10);
      if (!text.empty() && *end == '\0') {
        number = static_cast<uint32_t>(n), found = true;
      } else if (speciesNames != nullptr) {
        const std::string want = em::NameKey(spec);
        for (const auto& [name, num] : speciesNames->names) {
          if (em::NameKey(name) == want) number = num, found = true;
        }
        for (const em::AddedRow& other : rows) {
          if (other.table == "pokemon_data" && em::NameKey(other.key) == want && &other != &row) number = other.id, found = true;
        }
      }
    }
    uint32_t normal = 0, shiny = 0;
    bool ok = false;
    if (found && number < species->count && !species->places.empty()) {
      std::vector<uint8_t> table;
      if (job.readEntry && job.readEntry(species->places[0].archive, species->places[0].entry, table)) {
        const size_t at = species->places[0].offset + static_cast<size_t>(number) * species->entrySize;
        if (at + species->entrySize <= table.size()) {
          em::AddedRow fake;
          fake.bytes.assign(table.begin() + static_cast<std::ptrdiff_t>(at), table.begin() + static_cast<std::ptrdiff_t>(at + species->entrySize));
          ok = spritesOf(fake, normal, shiny);
        }
      }
    } else if (found) {
      for (const em::AddedRow& other : rows) {
        if (other.table == "pokemon_data" && other.id == number) ok = spritesOf(other, normal, shiny);
      }
    }
    if (!ok) {
      say(em::Message::Level::Problem, what + ": \"" + spec + "\" is not a species the game has (or one the mod adds before this one) and not a .png picture of the mod; the species keeps the face of the one it copies.");
      continue;
    }
    PutBe16(row.bytes.data() + spriteAt[0], normal);
    PutBe16(row.bytes.data() + spriteAt[1], shiny);
  }

  if (additions.empty()) return;
  for (size_t i = 0; i < additions.size(); i++) {
    additions[i].name = names[i].c_str();
    additions[i].data = contents[i].data();
    additions[i].size = static_cast<uint32_t>(contents[i].size());
  }
  size_t size = 0;
  uint8_t* built = Fsys_RebuildAdded(archive.data(), archive.size(), additions.data(), additions.size(), &size);
  if (built == nullptr) {
    say(em::Message::Level::Problem, "Faces of added species: poke_face.fsys could not be made longer (an unknown layout?); the species keep the face of the one they copy.");
    for (size_t index : wanting) {
      for (int k = 0; k < 2; k++) {
        rows[index].bytes[spriteAt[k]] = original[index][spriteAt[k]];
        rows[index].bytes[spriteAt[k] + 1] = original[index][spriteAt[k] + 1];
      }
    }
    return;
  }
  em::RowsJob::OutFile out;
  out.discPath = archiveName;
  out.data.assign(built, built + size);
  out.mods = modsText;
  std::free(built);
  job.outFiles.push_back(std::move(out));
  for (em::AddedRow& f : newRows) rows.push_back(std::move(f));
}

namespace {

constexpr uint32_t kAntTex = 0xA0, kCell = 64, kPicSide = 54;
constexpr uint32_t kFrameTops[3] = {10, 8, 6};

void FitIcon(const engine::memcard::thumbnail::Picture& pic, uint32_t srcY0, uint32_t srcH, std::vector<Rgba>& out, uint32_t& outW, uint32_t& outH) {
  const double scale = std::min(1.0, std::min(static_cast<double>(kPicSide) / pic.width, static_cast<double>(kPicSide) / srcH));
  outW = std::max<uint32_t>(1, static_cast<uint32_t>(pic.width * scale + 0.5));
  outH = std::max<uint32_t>(1, static_cast<uint32_t>(srcH * scale + 0.5));
  out.assign(static_cast<size_t>(outW) * outH, Rgba{0, 0, 0, 0});
  for (uint32_t y = 0; y < outH; y++) {
    for (uint32_t x = 0; x < outW; x++) {
      const uint32_t x0 = x * pic.width / outW, x1 = std::max(x0 + 1, (x + 1) * pic.width / outW);
      const uint32_t y0 = srcY0 + y * srcH / outH, y1 = std::max(y0 + 1, srcY0 + (y + 1) * srcH / outH);
      uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (uint32_t sy = y0; sy < y1 && sy < pic.height; sy++) {
        for (uint32_t sx = x0; sx < x1 && sx < pic.width; sx++) {
          const uint8_t* p = &pic.rgba[(static_cast<size_t>(sy) * pic.width + sx) * 4];
          r += p[0] * p[3], g += p[1] * p[3], b += p[2] * p[3], a += p[3], n++;
        }
      }
      if (n != 0 && a != 0) out[static_cast<size_t>(y) * outW + x] = {static_cast<uint8_t>(r / a), static_cast<uint8_t>(g / a), static_cast<uint8_t>(b / a), static_cast<uint8_t>(a / n)};
    }
  }
}

std::vector<uint8_t> MakeAnt(const std::vector<uint8_t>& templateFile, const std::vector<Rgba>& pic, uint32_t w, uint32_t h) {
  std::vector<Rgba> sheet(static_cast<size_t>(kCell) * 3 * kCell, Rgba{0, 0, 0, 0});
  const uint32_t x0 = (kCell - w) / 2;
  for (uint32_t f = 0; f < 3; f++) {
    const uint32_t y0 = f * kCell + kFrameTops[f] + (kPicSide - h);
    for (uint32_t y = 0; y < h; y++)
      for (uint32_t x = 0; x < w; x++) sheet[static_cast<size_t>(y0 + y) * kCell + x0 + x] = pic[static_cast<size_t>(y) * w + x];
  }
  std::vector<uint16_t> palette;
  std::vector<uint8_t> index;
  Quantize(sheet, palette, index);
  std::vector<uint8_t> out = templateFile;
  const size_t dataAt = kAntTex + Be32(&out[kAntTex + 0x28]), palAt = kAntTex + Be32(&out[kAntTex + 0x48]);
  size_t p = dataAt;
  for (uint32_t ty = 0; ty < 3 * kCell; ty += 4)
    for (uint32_t tx = 0; tx < kCell; tx += 8)
      for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 8; x++) out[p++] = index[static_cast<size_t>(ty + y) * kCell + tx + x];
  for (size_t i = 0; i < 256; i++) PutBe16(&out[palAt + 2 * i], palette[i]);
  return out;
}

}

void BuildBoxIcons(em::RowsJob& job) {
  if (job.rows == nullptr) return;
  std::vector<em::AddedRow>& rows = *job.rows;
  const auto say = [&](em::Message::Level level, const std::string& text) {
    if (job.say) job.say(level, text);
  };
  std::vector<size_t> wanting;
  for (size_t i = 0; i < rows.size(); i++) {
    if (rows[i].table == "pokemon_data" && rows[i].assets.count("icon") != 0) wanting.push_back(i);
  }
  if (wanting.empty()) return;
  const em::DataTable* species = job.catalog != nullptr ? job.catalog->FindTable("pokemon_data") : nullptr;
  const em::DataField* body = nullptr;
  if (species != nullptr) {
    for (const em::DataField& f : species->fields) {
      if (f.name == "cgData.pokebodyId") body = &f;
    }
  }
  if (body == nullptr || body->count < 2 || body->size != 4) {
    say(em::Message::Level::Problem, "Icons of added species: the game's box picture field could not be found on this disc; the species keep the icon of the one they copy.");
    return;
  }
  const uint32_t bodyAt[2] = {body->offset, body->offset + body->Stride()};

  std::string archiveName = "poke_dance.fsys";
  for (const std::string& f : job.discFiles) {
    if (LowerText(f) == "poke_dance.fsys") archiveName = f;
  }
  std::vector<uint8_t> archive;
  FsysArchive fsys{};
  std::vector<uint8_t> templateFile;
  uint32_t templateIndex = 0, templateWord0 = 0, maxId = 0;
  const auto openArchive = [&] {
    if (archive.empty()) {
      if (!job.readFile || !job.readFile(archiveName, archive) || !Fsys_Open(archive.data(), archive.size(), &fsys)) {
        archive.clear();
        return false;
      }
      const int named = Fsys_Find(&fsys, "unknown_a");
      if (named < 0) return false;
      templateIndex = static_cast<uint32_t>(named);
      uint32_t size = 0;
      uint8_t* p = Fsys_Extract(&fsys, templateIndex, &size);
      if (p != nullptr) {
        templateFile.assign(p, p + size);
        std::free(p);
      }
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e;
        if (Fsys_Entry(&fsys, i, &e)) maxId = std::max<uint32_t>(maxId, e.id);
      }
      const uint32_t at = Be32(archive.data() + fsys.entryTable + 4 * templateIndex);
      templateWord0 = at + 4 <= archive.size() ? Be32(archive.data() + at) : 0;
    }
    return !archive.empty() && templateFile.size() > kAntTex + 0x50 + 512 && Be32(&templateFile[kAntTex]) == (kCell << 16 | 3 * kCell);
  };

  struct Made {
    uint32_t normal, shiny;
  };
  std::map<std::string, Made> madeByPicture;
  std::vector<FsysAddition> additions;
  std::vector<std::vector<uint8_t>> contents;
  std::vector<std::string> names;
  std::string modsText;
  std::map<size_t, std::vector<uint8_t>> original;
  for (size_t index : wanting) original[index] = rows[index].bytes;
  for (size_t index : wanting) {
    em::AddedRow& row = rows[index];
    const std::string spec = row.assets.at("icon");
    const std::string what = "The mod \"" + row.mod + "\", the icon of \"" + row.key + "\" (" + row.where + ")";
    const auto folder = job.modFolders.find(row.mod);
    fs::path file;
    if (!EndsWith(spec, ".png") || folder == job.modFolders.end() || !em::ResolveLayoutFile(folder->second, job.gameId, spec, file)) {
      say(em::Message::Level::Problem, what + ": there is no picture " + spec + " in the mod; the species keeps the icon of the one it copies.");
      continue;
    }
    const std::string key = file.generic_string();
    auto made = madeByPicture.find(key);
    if (made == madeByPicture.end()) {
      std::vector<uint8_t> raw;
      {
        std::ifstream in(file, std::ios::binary);
        raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      engine::memcard::thumbnail::Picture pic;
      std::string error;
      if (raw.empty() || !engine::memcard::thumbnail::DecodePng(raw, pic, error, true)) {
        say(em::Message::Level::Problem, what + ": " + spec + " is not a PNG the port reads (" + (error.empty() ? std::string("empty file") : error) +
                                             "); use an 8-bit RGB or RGBA PNG that is not interlaced. The species keeps the icon of the one it copies.");
        continue;
      }
      if (pic.width < 8 || pic.height < 8 || pic.width > 4096 || pic.height > 4096) {
        say(em::Message::Level::Problem, what + ": " + spec + " must be between 8 and 4096 pixels each way; the species keeps the icon of the one it copies.");
        continue;
      }
      if (!openArchive()) {
        say(em::Message::Level::Problem, what + ": the game's box picture archive (poke_dance.fsys) could not be read or has an unknown layout; the species keep the icon of the one they copy.");
        return;
      }
      const bool two = pic.height * 10 >= pic.width * 18;
      Made m{};
      for (int shiny = 0; shiny < 2; shiny++) {
        std::vector<Rgba> px;
        uint32_t w = 0, h = 0;
        const uint32_t half = two ? pic.height / 2 : pic.height;
        FitIcon(pic, two ? static_cast<uint32_t>(shiny) * half : 0, half, px, w, h);
        const uint32_t id = maxId + 1 + static_cast<uint32_t>(additions.size());
        if (id > 0xFFFF) {
          say(em::Message::Level::Problem, what + ": no resource number left in poke_dance.fsys.");
          return;
        }
        contents.push_back(MakeAnt(templateFile, px, w, h));
        names.push_back("modbody" + std::to_string(additions.size() / 2 + 1) + (shiny ? "_c" : ""));
        additions.push_back({static_cast<uint16_t>(id), nullptr, nullptr, 0, templateIndex});
        (shiny ? m.shiny : m.normal) = id << 16 | (templateWord0 & 0xFFFF);
      }
      modsText += (modsText.empty() ? "" : ", ") + row.mod;
      made = madeByPicture.emplace(key, m).first;
      std::fprintf(stderr, "[mods] icon: %s -> poke_dance entries %u and %u\n", spec.c_str(), made->second.normal >> 16, made->second.shiny >> 16);
    }
    if (row.bytes.size() >= bodyAt[1] + 4) {
      PutBe32(row.bytes.data() + bodyAt[0], made->second.normal);
      PutBe32(row.bytes.data() + bodyAt[1], made->second.shiny);
    }
  }
  if (additions.empty()) return;
  for (size_t i = 0; i < additions.size(); i++) {
    additions[i].name = names[i].c_str();
    additions[i].data = contents[i].data();
    additions[i].size = static_cast<uint32_t>(contents[i].size());
  }
  size_t size = 0;
  uint8_t* built = Fsys_RebuildAdded(archive.data(), archive.size(), additions.data(), additions.size(), &size);
  if (built == nullptr) {
    say(em::Message::Level::Problem, "Icons of added species: poke_dance.fsys could not be made longer (an unknown layout?); the species keep the icon of the one they copy.");
    for (size_t index : wanting) {
      for (int k = 0; k < 2; k++)
        for (int b = 0; b < 4; b++) rows[index].bytes[bodyAt[k] + b] = original[index][bodyAt[k] + b];
    }
    return;
  }
  em::RowsJob::OutFile out;
  out.discPath = archiveName;
  out.data.assign(built, built + size);
  out.mods = modsText;
  std::free(built);
  job.outFiles.push_back(std::move(out));
}

}
