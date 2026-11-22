#include "xd_spare.h"
#include "xd_species.h"

#include "xd_toc.h"

#include <dsp_adpcm.h>
#include <gs_formats.h>
#include <mod_layout.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace xd::species {

namespace em = engine::mods;
namespace fs = std::filesystem;

namespace {

Taken sTaken;

constexpr uint32_t kDiscModels = 419, kDiscWaves = 389, kDiscSndFile = 516, kDiscSndData = 1507, kDiscWzx = 373;
constexpr uint32_t kSpare = XD_PC_SPARE_SPECIES;
constexpr uint32_t kMaxModelBytes = 614400;
constexpr uint32_t kCryRate = 32000, kBlock = 0x8000, kMaxBlocks = 2;
constexpr uint32_t kPkxMember = 0x00011E00;
constexpr uint32_t kCryGroupFallback = 1966;
constexpr uint32_t kFlagBitsFree = 9368;

uint32_t Be32(const uint8_t* p) { return xd::toc::Be32(p); }
void PutBe32(uint8_t* p, uint32_t v) { xd::toc::PutBe32(p, v); }
void PutBe16(uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v >> 8), p[1] = static_cast<uint8_t>(v); }
uint32_t Be16(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 8 | p[1]; }

std::string Lower(std::string s) {
  for (char& c : s) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  return s;
}
bool EndsWith(const std::string& s, const char* suffix) {
  const std::string t = Lower(s), u = suffix;
  return t.size() >= u.size() && t.compare(t.size() - u.size(), u.size(), u) == 0;
}
std::string Sanitize(const std::string& key) {
  std::string out;
  for (char c : key) out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
  return out.empty() ? "x" : out.substr(0, 24);
}
bool ReadFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}

void SetTaken(const Taken& taken) { sTaken = taken; }

bool MakeCry(const std::vector<uint8_t>& wav, Cry& out, std::string& error) {
  engine::dsp::Pcm pcm;
  if (!engine::dsp::ReadWav(wav, pcm, error)) return false;
  pcm = engine::dsp::Resample(pcm, kCryRate);
  const size_t cap = static_cast<size_t>(kMaxBlocks) * kBlock / 8 * 14;
  out.cut = pcm.channels[0].size() > cap;
  for (auto& c : pcm.channels) {
    if (c.size() > cap) c.resize(cap);
  }
  std::vector<int16_t> left = pcm.channels[0], right = pcm.channels.size() > 1 ? pcm.channels[1] : pcm.channels[0];
  const size_t n = left.size();
  out.seconds = static_cast<double>(n) / kCryRate;
  const engine::dsp::Encoded enc[2] = {engine::dsp::Encode(left), engine::dsp::Encode(right)};
  const uint32_t last = static_cast<uint32_t>(n) - 1, end = last / 14 * 16 + 2 + last % 14;
  out.header.assign(192, 0);
  std::memcpy(out.header.data(), "I_SF", 4);
  const uint32_t w[9] = {0x20040109, kCryRate, static_cast<uint32_t>(n), end + 1, 2, kBlock, 0, 2, end};
  for (int k = 0; k < 9; k++) PutBe32(&out.header[4 + 4 * static_cast<size_t>(k)], w[k]);
  PutBe32(&out.header[0x28], end);
  for (size_t c = 0; c < 2; c++) {
    const size_t o = 0x40 + 0x40 * c;
    for (size_t k = 0; k < 16; k++) PutBe16(&out.header[o + 2 * k], static_cast<uint16_t>(enc[c].coefficients[k]));
    const uint32_t ps = enc[c].frames.empty() ? 0 : enc[c].frames[0];
    const uint32_t v[7] = {0, ps, 0, 0, ps, 0, 0};
    for (size_t k = 0; k < 7; k++) PutBe16(&out.header[o + 32 + 2 * k], v[k]);
  }
  const size_t blocks = (enc[0].frames.size() + kBlock - 1) / kBlock;
  for (size_t j = 0; j < blocks; j++) {
    for (int c = 0; c < 2; c++) {
      const auto& d = enc[c].frames;
      const size_t a = std::min(j * kBlock, d.size()), b = std::min((j + 1) * kBlock, d.size());
      out.body.insert(out.body.end(), d.begin() + static_cast<std::ptrdiff_t>(a), d.begin() + static_cast<std::ptrdiff_t>(b));
      out.body.insert(out.body.end(), kBlock - (b - a), 0);
    }
  }
  const int64_t pieces = std::max<int64_t>(16, std::min<int64_t>(60, (static_cast<int64_t>(n) * 30 * 2 + kCryRate) / (2 * kCryRate)));
  std::vector<int> picks;
  int peak = 0;
  for (int64_t k = 0; k < pieces; k++) {
    const size_t a = static_cast<size_t>(static_cast<int64_t>(n) * k / pieces), b = static_cast<size_t>(static_cast<int64_t>(n) * (k + 1) / pieces);
    int best = 0;
    for (size_t i = a; i < b; i++) {
      if (std::abs(left[i]) > std::abs(best)) best = left[i];
    }
    picks.push_back(best);
    peak = std::max(peak, std::abs(best));
  }
  if (peak == 0) peak = 1;
  out.wave.push_back(static_cast<uint8_t>(pieces));
  for (int v : picks) {
    const int m = std::abs(v) * 127 / peak;
    out.wave.push_back(static_cast<uint8_t>((v < 0 ? -m : m) & 255));
  }
  return true;
}

namespace {

struct Ctx {
  em::RowsJob& job;
  std::vector<em::AddedRow>& rows;
  std::vector<em::AddedRow> added;
  std::string mods;
};

void Say(Ctx& c, em::Message::Level level, const std::string& text) {
  if (c.job.say) c.job.say(level, text);
}

bool RowBytes(Ctx& c, const char* table, uint32_t number, std::vector<uint8_t>& out) {
  for (const em::AddedRow& r : c.rows) {
    if (r.table == table && r.id == number) {
      out = r.bytes;
      return true;
    }
  }
  const em::DataTable* t = c.job.catalog != nullptr ? c.job.catalog->FindTable(table) : nullptr;
  if (t == nullptr || t->places.empty() || number >= t->count || !c.job.readEntry) return false;
  std::vector<uint8_t> all;
  if (!c.job.readEntry(t->places[0].archive, t->places[0].entry, all)) return false;
  const size_t at = t->places[0].offset + static_cast<size_t>(number) * t->entrySize;
  if (at + t->entrySize > all.size()) return false;
  out.assign(all.begin() + static_cast<std::ptrdiff_t>(at), all.begin() + static_cast<std::ptrdiff_t>(at + t->entrySize));
  return true;
}

bool ResolveNumber(Ctx& c, const char* list, const char* table, const std::string& spec, const em::AddedRow* self, uint32_t& number) {
  std::string text = spec;
  if (!text.empty() && text[0] == '#') text.erase(0, 1);
  char* end = nullptr;
  const unsigned long n = std::strtoul(text.c_str(), &end, 10);
  if (!text.empty() && *end == '\0') {
    number = static_cast<uint32_t>(n);
    return true;
  }
  const std::string want = em::NameKey(spec);
  for (const em::AddedRow& r : c.rows) {
    if (&r != self && r.table == table && em::NameKey(r.key) == want) {
      number = r.id;
      return true;
    }
  }
  const em::NameList* names = c.job.catalog != nullptr ? c.job.catalog->FindList(list) : nullptr;
  if (names != nullptr) {
    for (const auto& [name, num] : names->names) {
      if (em::NameKey(name) == want) {
        number = num;
        return true;
      }
    }
  }
  return false;
}

std::string TocPath(const em::RowsJob& job) {
  for (const std::string& f : job.discFiles) {
    if (Lower(f) == "gsfsys.toc") return f;
  }
  return "GSfsys.toc";
}

bool ReadCurrent(Ctx& c, const std::string& path, std::vector<uint8_t>& out) {
  for (const em::RowsJob::OutFile& o : c.job.outFiles) {
    if (Lower(o.discPath) == Lower(path)) {
      out = o.data;
      return true;
    }
  }
  return c.job.readFile && c.job.readFile(path, out);
}
void WriteOut(Ctx& c, const std::string& path, std::vector<uint8_t> data) {
  for (em::RowsJob::OutFile& o : c.job.outFiles) {
    if (Lower(o.discPath) == Lower(path)) {
      o.data = std::move(data);
      if (o.mods.find(c.mods) == std::string::npos) o.mods += (o.mods.empty() ? "" : ", ") + c.mods;
      return;
    }
  }
  c.job.outFiles.push_back({path, std::move(data), c.mods});
}

void Note(Ctx& c, const em::AddedRow& r) {
  if (c.mods.find(r.mod) == std::string::npos) c.mods += (c.mods.empty() ? "" : ", ") + r.mod;
}

bool CheckPkx(std::vector<uint8_t>& data, std::string& why) {
  if (data.size() >= 16 && std::memcmp(data.data(), "LZSS", 4) == 0) {
    uint32_t size = 0;
    uint8_t* p = Lzss_Unpack(data.data(), data.size(), &size);
    if (p == nullptr) {
      why = "it is packed (LZSS) but the packing is damaged";
      return false;
    }
    data.assign(p, p + size);
    std::free(p);
  }
  if (data.size() < 0x84 + 0xD0) {
    why = "it is far too small to be a model";
    return false;
  }
  if (data.size() > kMaxModelBytes) {
    why = "it is " + std::to_string(data.size()) + " bytes; the game's model memory holds at most " + std::to_string(kMaxModelBytes);
    return false;
  }
  if (Be32(data.data()) == 0 || Be16(&data[0x1A]) != 0x0C) {
    why = "it is not a Pokemon model file (.pkx as the game stores them; the header says no model)";
    return false;
  }
  return true;
}

}

void BuildSpecies(em::RowsJob& job) {
  if (job.rows == nullptr) return;
  Ctx c{job, *job.rows, {}, {}};
  std::vector<size_t> species, moves;
  for (size_t i = 0; i < c.rows.size(); i++) {
    if (c.rows[i].table == "pokemon_data") species.push_back(i);
    if (c.rows[i].table == "waza_data" && (c.rows[i].assets.count("animation") != 0 || c.rows[i].assets.count("animation_attack") != 0 || c.rows[i].assets.count("animation_damage") != 0 ||
                                           c.rows[i].assets.count("animation_status") != 0))
      moves.push_back(i);
  }
  if (species.empty() && moves.empty()) return;
  const auto problem = [&](const em::AddedRow& r, const char* what, const std::string& text) {
    Say(c, em::Message::Level::Problem, std::string("The mod \"") + r.mod + "\", the " + what + " of \"" + r.key + "\" (" + r.where + "): " + text);
  };

  const std::string tocPath = TocPath(job);
  xd::toc::Toc toc;
  std::vector<uint8_t> tocBytes;
  bool haveToc = ReadCurrent(c, tocPath, tocBytes) && xd::toc::Read(tocBytes, toc);
  uint32_t nextGroup = haveToc ? std::max(xd::toc::TopId(toc), 5000u) : 5000;
  std::vector<std::pair<uint32_t, std::string>> newGroups;
  std::set<std::string> usedNames;
  for (const std::string& f : job.discFiles) usedNames.insert(Lower(f));

  std::map<std::string, uint32_t> modelByFile;
  uint32_t nextModel = kDiscModels + sTaken.models;
  for (size_t index : species) {
    em::AddedRow& row = c.rows[index];
    const auto it = row.assets.find("model");
    if (it == row.assets.end()) continue;
    const std::string spec = it->second;
    if (row.bytes.size() < 0x30) continue;
    uint32_t number = 0;
    if (EndsWith(spec, ".pkx")) {
      if (!haveToc) {
        problem(row, "model", "the game's archive list (GSfsys.toc) could not be read; the species keeps the model of the one it copies.");
        continue;
      }
      const auto folder = job.modFolders.find(row.mod);
      fs::path file;
      if (folder == job.modFolders.end() || !em::ResolveLayoutFile(folder->second, job.gameId, spec, file)) {
        problem(row, "model", "there is no file " + spec + " in the mod; the species keeps the model of the one it copies.");
        continue;
      }
      const std::string key = file.generic_string();
      const auto made = modelByFile.find(key);
      if (made != modelByFile.end()) {
        number = made->second;
      } else {
        std::vector<uint8_t> data;
        std::string why;
        if (!ReadFile(file, data) || !CheckPkx(data, why)) {
          problem(row, "model", spec + (why.empty() ? " could not be read" : ": " + why) + "; the species keeps the model of the one it copies.");
          continue;
        }
        if (nextModel >= kDiscModels + kSpare) {
          problem(row, "model", "the game's model table is full (" + std::to_string(kSpare) + " new models).");
          continue;
        }
        const uint32_t group = ++nextGroup;
        const std::string name = "pkx_m" + std::to_string(group);
        Note(c, row);
        xd::toc::Member m;
        m.word0 = kPkxMember, m.type = 15, m.name = Sanitize(row.key), m.data = std::move(data);
        WriteOut(c, name + ".fsys", xd::toc::MakeFsys(group, {m}));
        newGroups.emplace_back(group, name);
        number = nextModel++;
        em::AddedRow r;
        r.table = "pokemon_pkx_data", r.id = r.row = number, r.copyOf = 0, r.key = "model of " + row.key;
        r.mod = row.mod, r.modName = row.modName, r.where = row.where;
        r.bytes.assign(8, 0);
        PutBe32(r.bytes.data(), group), PutBe32(r.bytes.data() + 4, kPkxMember);
        c.added.push_back(std::move(r));
        modelByFile[key] = number;
        std::fprintf(stderr, "[mods] model: %s -> archive %s.fsys (group %u), model number %u\n", spec.c_str(), name.c_str(), group, number);
      }
    } else {
      uint32_t other = 0;
      std::vector<uint8_t> bytes;
      if (!ResolveNumber(c, "species", "pokemon_data", spec, &row, other) || !RowBytes(c, "pokemon_data", other, bytes) || bytes.size() < 0x30) {
        problem(row, "model", "\"" + spec + "\" is not a species the game has (or one the mod adds before this one) and not a .pkx file of the mod; the species keeps the model of the one it copies.");
        continue;
      }
      number = Be32(&bytes[0x2C]);
    }
    PutBe32(&row.bytes[0x2C], number);
    Note(c, row);
  }

  struct MadeCry {
    uint32_t sndData = 0, wave = 0;
  };
  std::map<std::string, MadeCry> cryByFile;
  struct Pending {
    size_t species;
    std::string file;
    uint32_t copyOfVoice;
  };
  std::vector<Cry> cryList;
  std::vector<Pending> pending;
  std::vector<std::string> cryFiles;
  for (size_t index : species) {
    em::AddedRow& row = c.rows[index];
    const auto it = row.assets.find("cry");
    if (it == row.assets.end() || row.bytes.size() < 0x14) continue;
    const std::string spec = it->second;
    if (EndsWith(spec, ".wav")) {
      const auto folder = job.modFolders.find(row.mod);
      fs::path file;
      if (folder == job.modFolders.end() || !em::ResolveLayoutFile(folder->second, job.gameId, spec, file)) {
        problem(row, "cry", "there is no file " + spec + " in the mod; the species keeps the cry of the one it copies.");
        continue;
      }
      const std::string key = file.generic_string();
      if (cryByFile.count(key) == 0) {
        std::vector<uint8_t> raw;
        Cry cry;
        std::string error;
        if (!ReadFile(file, raw) || !MakeCry(raw, cry, error)) {
          problem(row, "cry", spec + " can't be used (" + (error.empty() ? std::string("unreadable") : error) + "); the species keeps the cry of the one it copies.");
          continue;
        }
        if (cry.cut) Say(c, em::Message::Level::Info, "The cry " + spec + " of the mod \"" + row.mod + "\" is longer than the game's cries can be; it is cut at " + std::to_string(cry.seconds).substr(0, 4) + " seconds.");
        if (sTaken.waves + static_cast<uint32_t>(cryList.size()) >= kSpare) {
          problem(row, "cry", "the game's cry tables are full.");
          continue;
        }
        const uint32_t k = static_cast<uint32_t>(cryList.size());
        MadeCry m;
        m.sndData = kDiscSndData + sTaken.sndData + k;
        m.wave = kDiscWaves + sTaken.waves + k;
        cryByFile[key] = m;
        cryFiles.push_back(key);
        cryList.push_back(std::move(cry));
        std::fprintf(stderr, "[mods] cry: %s -> %.2f s, snd_data row %u, waveform row %u\n", spec.c_str(), cryList.back().seconds, m.sndData, m.wave);
      }
      pending.push_back({index, key, Be16(&row.bytes[0x0C])});
    } else {
      uint32_t other = 0;
      std::vector<uint8_t> bytes;
      if (!ResolveNumber(c, "species", "pokemon_data", spec, &row, other) || !RowBytes(c, "pokemon_data", other, bytes) || bytes.size() < 0x14) {
        problem(row, "cry", "\"" + spec + "\" is not a species the game has (or one the mod adds before this one) and not a .wav file of the mod; the species keeps the cry of the one it copies.");
        continue;
      }
      std::memcpy(&row.bytes[0x0C], &bytes[0x0C], 2);
      std::memcpy(&row.bytes[0x0E], &bytes[0x0E], 2);
      std::memcpy(&row.bytes[0x10], &bytes[0x10], 2);
      Note(c, row);
    }
  }
  if (!cryList.empty()) {
    std::vector<uint8_t> probe;
    if (!RowBytes(c, "snd_data", 1268, probe) || probe.size() != 12) {
      Say(c, em::Message::Level::Problem, "Cries of added species: the game's sound table could not be found on this disc; the species keep the cry of the one they copy.");
      pending.clear();
      cryList.clear();
    }
  }
  if (!cryList.empty()) {
    std::string archiveName = "stm_se_nakigoe_archive.fsys";
    for (const std::string& f : job.discFiles) {
      if (Lower(f) == "stm_se_nakigoe_archive.fsys") archiveName = f;
    }
    std::vector<uint8_t> archive;
    FsysArchive fsys{};
    bool ok = ReadCurrent(c, archiveName, archive) && Fsys_Open(archive.data(), archive.size(), &fsys);
    uint32_t headTemplate = 0, bodyTemplate = 0, maxId = 0, group = kCryGroupFallback;
    bool haveHead = false, haveBody = false;
    if (ok) {
      group = Be32(archive.data() + 8);
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e;
        if (!Fsys_Entry(&fsys, i, &e)) continue;
        maxId = std::max<uint32_t>(maxId, e.id);
        if (e.type == 20 && !haveHead) headTemplate = i, haveHead = true;
        if (e.type == 21 && !haveBody) bodyTemplate = i, haveBody = true;
      }
      ok = haveHead && haveBody && maxId + 2 * cryList.size() < 0xFFFF;
    }
    std::vector<FsysAddition> additions;
    std::vector<std::string> names;
    if (ok) {
      names.reserve(cryList.size() * 2);
      for (size_t k = 0; k < cryList.size(); k++) {
        names.push_back("modcry" + std::to_string(k + 1) + "h");
        names.push_back("modcry" + std::to_string(k + 1) + "b");
      }
      for (size_t k = 0; k < cryList.size(); k++) {
        additions.push_back({static_cast<uint16_t>(maxId + 1 + 2 * k), names[2 * k].c_str(), cryList[k].header.data(), static_cast<uint32_t>(cryList[k].header.size()), headTemplate});
        additions.push_back({static_cast<uint16_t>(maxId + 2 + 2 * k), names[2 * k + 1].c_str(), cryList[k].body.data(), static_cast<uint32_t>(cryList[k].body.size()), bodyTemplate});
      }
    }
    size_t builtSize = 0;
    uint8_t* built = ok ? Fsys_RebuildAdded(archive.data(), archive.size(), additions.data(), additions.size(), &builtSize) : nullptr;
    FsysArchive after{};
    if (built == nullptr || !Fsys_Open(built, builtSize, &after) || after.count != fsys.count + additions.size()) {
      std::free(built);
      Say(c, em::Message::Level::Problem, "Cries of added species: the game's cry archive could not be made longer (an unknown layout?); the species keep the cry of the one they copy.");
      pending.clear();
      cryList.clear();
    } else {
      std::vector<uint32_t> words(additions.size());
      for (size_t k = 0; k < additions.size(); k++) {
        const uint32_t at = Be32(built + after.entryTable + 4 * (fsys.count + k));
        words[k] = Be32(built + at);
      }
      std::free(built);
      built = Fsys_RebuildAdded(archive.data(), archive.size(), additions.data(), additions.size(), &builtSize);
      WriteOut(c, archiveName, std::vector<uint8_t>(built, built + builtSize));
      std::free(built);
      for (size_t k = 0; k < cryList.size(); k++) {
        const uint32_t fileRow = kDiscSndFile + sTaken.sndFiles + static_cast<uint32_t>(k), dataRow = kDiscSndData + sTaken.sndData + static_cast<uint32_t>(k),
                       waveRow = kDiscWaves + sTaken.waves + static_cast<uint32_t>(k);
        uint32_t copyOf = 1268;
        for (const Pending& p : pending) {
          if (cryByFile[p.file].sndData == dataRow && p.copyOfVoice < kDiscSndData) copyOf = p.copyOfVoice;
        }
        const em::AddedRow& first = c.rows[pending.empty() ? species[0] : pending[0].species];
        em::AddedRow f, d, w;
        f.table = "snd_file", f.id = f.row = fileRow, f.copyOf = 0, f.key = "cry file " + std::to_string(k + 1);
        d.table = "snd_data", d.id = d.row = dataRow, d.copyOf = copyOf, d.key = "cry " + std::to_string(k + 1);
        w.table = "pokemon_nakigoe_data", w.id = w.row = waveRow, w.copyOf = 0, w.key = "cry waveform " + std::to_string(k + 1);
        for (em::AddedRow* r : {&f, &d, &w}) r->mod = first.mod, r->modName = first.modName, r->where = first.where;
        f.bytes.assign(12, 0);
        PutBe32(f.bytes.data(), group), PutBe32(f.bytes.data() + 4, words[2 * k]), PutBe32(f.bytes.data() + 8, words[2 * k + 1]);
        std::vector<uint8_t> base;
        if (!RowBytes(c, "snd_data", copyOf, base) || base.size() != 12) RowBytes(c, "snd_data", 1268, base);
        d.bytes = base;
        PutBe16(&d.bytes[6], fileRow);
        w.bytes = cryList[k].wave;
        c.added.push_back(std::move(f));
        c.added.push_back(std::move(d));
        c.added.push_back(std::move(w));
      }
      for (const Pending& p : pending) {
        em::AddedRow& row = c.rows[p.species];
        const MadeCry& m = cryByFile[p.file];
        PutBe16(&row.bytes[0x0C], m.sndData);
        PutBe16(&row.bytes[0x0E], m.wave);
        PutBe16(&row.bytes[0x10], m.sndData);
        Note(c, row);
      }
    }
  }

  const em::DataTable* flagTable = job.catalog != nullptr ? job.catalog->FindTable("flag_data") : nullptr;
  if (flagTable != nullptr) {
    uint32_t nextFlag = flagTable->count + sTaken.flags;
    uint32_t used = 0;
    for (const em::AddedRow& r : c.rows) {
      if (r.table == "flag_data") nextFlag = std::max(nextFlag, r.row + 1), used += (r.bytes.empty() ? 1 : (r.bytes[0] & 0x3F));
    }
    used += sTaken.flags;
    for (size_t index : species) {
      em::AddedRow& row = c.rows[index];
      const auto it = row.assets.find("dex");
      const std::string mode = it == row.assets.end() ? "" : Lower(it->second);
      if (row.mod == "gen4" || mode.empty() || mode == "copy" || row.bytes.size() < 0x2C) continue;
      if (mode != "own") {
        problem(row, "dex", "\"" + (it == row.assets.end() ? std::string() : it->second) + "\" is not \"own\" or \"copy\"; the species shares the marks of the one it copies.");
        continue;
      }
      if (used + 2 > kFlagBitsFree) {
        problem(row, "dex", "the save has no room left for more Pokedex marks; the species shares the marks of the one it copies.");
        continue;
      }
      for (int caught = 0; caught < 2; caught++) {
        em::AddedRow f;
        f.table = "flag_data";
        f.id = f.row = nextFlag++;
        f.copyOf = 0;
        f.key = std::string(caught ? "caught mark of " : "seen mark of ") + row.key;
        f.mod = row.mod, f.modName = row.modName, f.where = row.where;
        f.bytes = {0xC1, 0, 0, 0, 0, 0};
        PutBe32(&row.bytes[caught ? 0x28 : 0x24], f.id);
        c.added.push_back(std::move(f));
      }
      used += 2;
      Note(c, row);
    }
  }

  uint32_t nextWzx = kDiscWzx + sTaken.wzx;
  for (const em::AddedRow& r : c.rows) {
    if (r.table == "waza_wzx_data") nextWzx = std::max(nextWzx, r.row + 1);
  }
  for (size_t index : moves) {
    em::AddedRow& row = c.rows[index];
    const bool composed = row.assets.count("animation_attack") != 0 || row.assets.count("animation_damage") != 0 || row.assets.count("animation_status") != 0;
    uint32_t other = 0;
    std::vector<uint8_t> bytes;
    if (!composed) {
      if (row.bytes.size() < 0x34 || !ResolveNumber(c, "move", "waza_data", row.assets.at("animation"), &row, other) || !RowBytes(c, "waza_data", other, bytes) || bytes.size() < 0x34) {
        problem(row, "animation", "\"" + row.assets.at("animation") + "\" is not a move the game has (or one the mod adds before this one); the move keeps the animation of the one it copies.");
        continue;
      }
      std::memcpy(&row.bytes[0x30], &bytes[0x30], 4);
      Note(c, row);
      continue;
    }
    static const char* const kParts[3] = {"animation_attack", "animation_damage", "animation_status"};
    uint32_t source[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    bool good = row.bytes.size() >= 0x34;
    std::string base = row.assets.count("animation") != 0 ? row.assets.at("animation") : "";
    for (int part = 0; part < 3 && good; part++) {
      std::string name = row.assets.count(kParts[part]) != 0 ? row.assets.at(kParts[part]) : base;
      if (name.empty() || Lower(name) == "none") continue;
      if (!ResolveNumber(c, "move", "waza_data", name, &row, other) || !RowBytes(c, "waza_data", other, bytes) || bytes.size() < 0x34) {
        problem(row, kParts[part], "\"" + name + "\" is not a move the game has (or one the mod adds before this one); the move keeps the animation of the one it copies.");
        good = false;
        break;
      }
      const uint32_t wzx = Be32(&bytes[0x30]);
      if (wzx >= kDiscWzx + sTaken.wzx + kSpare) {
        problem(row, kParts[part], "\"" + name + "\" has an animation the game cannot take a piece from.");
        good = false;
        break;
      }
      source[part] = wzx;
    }
    if (!good) continue;
    if (nextWzx >= kDiscWzx + kSpare) {
      problem(row, "animation", "the game's animation table is full (" + std::to_string(kSpare) + " new animations); the move keeps the animation of the one it copies.");
      continue;
    }
    em::AddedRow w;
    w.table = "waza_wzx_data", w.id = w.row = nextWzx, w.copyOf = 0, w.key = "animation of " + row.key;
    w.mod = row.mod, w.modName = row.modName, w.where = row.where;
    w.bytes.assign(6, 0);
    for (int part = 0; part < 3; part++) PutBe16(&w.bytes[2 * part], source[part]);
    c.added.push_back(std::move(w));
    PutBe32(&row.bytes[0x30], nextWzx);
    Note(c, row);
    std::fprintf(stderr, "[mods] animation of %s: new animation row %u = attack from row %d, damage from row %d, status from row %d\n", row.key.c_str(), nextWzx,
                 source[0] == 0xFFFF ? -1 : static_cast<int>(source[0]), source[1] == 0xFFFF ? -1 : static_cast<int>(source[1]), source[2] == 0xFFFF ? -1 : static_cast<int>(source[2]));
    nextWzx++;
  }

  if (!newGroups.empty()) WriteOut(c, tocPath, xd::toc::Add(toc, newGroups));
  for (em::AddedRow& r : c.added) c.rows.push_back(std::move(r));
}

}
