#include "xd_sounds.h"

#include "xd_sound_banks.h"
#include "xd_toc.h"

#include <audio_sfx.h>
#include <audio_stream.h>
#include <gs_formats.h>
#include <json.h>
#include <mod_layout.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace xd::sound {

namespace em = engine::mods;
namespace ea = engine::audio;
namespace js = engine::mods::json;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kDiscSndFile = 516, kDiscSndData = 1507;
constexpr uint32_t kSongTemplateRow = 1267;
constexpr uint32_t kMaxSongs = 64;
constexpr uint32_t kSpareRows = 1024;

uint32_t sTakenFiles = 0, sTakenData = 0;
std::map<std::string, long> sSongs;

std::string Lower(std::string s) {
  for (char& c : s) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  return s;
}
bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
bool GoodName(const std::string& n) {
  if (n.empty() || n.size() > 48) return false;
  for (char c : n) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

struct Ctx {
  em::RowsJob& job;
  std::string mods;
};

void Say(Ctx& c, em::Message::Level level, const std::string& text) {
  if (c.job.say) c.job.say(level, text);
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
void WriteOut(Ctx& c, const std::string& path, std::vector<uint8_t> data, const std::string& mods) {
  for (em::RowsJob::OutFile& o : c.job.outFiles) {
    if (Lower(o.discPath) == Lower(path)) {
      o.data = std::move(data);
      if (!mods.empty() && o.mods.find(mods) == std::string::npos) o.mods += (o.mods.empty() ? "" : ", ") + mods;
      return;
    }
  }
  c.job.outFiles.push_back({path, std::move(data), mods});
}
std::string DiscPath(const em::RowsJob& job, const std::string& want) {
  for (const std::string& f : job.discFiles) {
    if (Lower(f) == Lower(want)) return f;
  }
  return want;
}

const js::Value* Field(const js::Value& v, const char* key) {
  const auto it = v.fields.find(key);
  return it == v.fields.end() ? nullptr : &it->second;
}

bool ModFile(Ctx& c, const std::string& modId, const std::string& rel, fs::path& out) {
  const auto folder = c.job.modFolders.find(modId);
  if (folder == c.job.modFolders.end()) return false;
  if (em::ResolveLayoutFile(folder->second, c.job.gameId, rel, out)) return true;
  return em::ResolveLayoutFile(folder->second, c.job.gameId, "sounds/" + rel, out);
}

struct Parsed {
  js::Value doc;
  bool ok = false;
};
bool ReadJson(Ctx& c, const std::string& modId, const char* rel, js::Value& doc, bool& present) {
  present = false;
  const auto folder = c.job.modFolders.find(modId);
  fs::path file;
  if (folder == c.job.modFolders.end() || !em::ResolveLayoutFile(folder->second, c.job.gameId, rel, file)) return true;
  present = true;
  std::vector<uint8_t> raw;
  if (!ReadFileBytes(file, raw)) {
    Say(c, em::Message::Level::Problem, "The mod \"" + modId + "\": " + rel + " could not be read.");
    return false;
  }
  js::Reader reader(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
  if (!reader.Document(doc) || doc.kind != js::Value::Kind::Object) {
    Say(c, em::Message::Level::Problem, "The mod \"" + modId + "\": " + rel + " is not valid JSON (" + reader.Error() + "); it is not used.");
    return false;
  }
  return true;
}

double NumberOr(const js::Value& v, const char* key, double fallback, bool& bad) {
  const js::Value* f = Field(v, key);
  if (f == nullptr) return fallback;
  if (f->kind != js::Value::Kind::Number) {
    bad = true;
    return fallback;
  }
  return f->number;
}

struct BankData {
  const Bank* bank = nullptr;
  std::vector<uint8_t> proj, pool, sdir, samp;
  std::vector<ea::sfx::Effect> effects;
  std::vector<ea::sfx::Sample> samples;
  size_t originalSize = 0;
  bool loaded = false, changed = false;
};

struct Archive {
  std::vector<uint8_t> bytes;
  FsysArchive fsys{};
  bool ok = false;
  bool Entry(const char* name, std::vector<uint8_t>& out, uint32_t& index) const {
    const int i = Fsys_Find(&fsys, name);
    if (i < 0) return false;
    index = static_cast<uint32_t>(i);
    uint32_t n = 0;
    uint8_t* p = Fsys_Extract(&fsys, index, &n);
    if (p == nullptr) return false;
    out.assign(p, p + n);
    std::free(p);
    return true;
  }
};

bool LoadBank(Ctx& c, const Archive& archive, BankData& b, std::string& why) {
  uint32_t ignore = 0;
  if (!archive.Entry(b.bank->proj, b.proj, ignore) || !archive.Entry(b.bank->pool, b.pool, ignore) || !archive.Entry(b.bank->sdir, b.sdir, ignore)) {
    why = std::string("the sound tables of the bank \"") + b.bank->name + "\" are not in " + kBanksArchive;
    return false;
  }
  if (!ReadCurrent(c, DiscPath(c.job, b.bank->samp), b.samp)) {
    why = std::string("the sample file ") + b.bank->samp + " is not on this disc";
    return false;
  }
  b.originalSize = b.samp.size();
  if (!ea::sfx::ParseSdir(b.sdir, b.samples, why)) return false;
  std::string ignoreText;
  ea::sfx::ParseEffects(b.proj, b.pool, b.effects, ignoreText);
  b.loaded = true;
  return true;
}

void BuildEffects(Ctx& c, const std::vector<std::string>& mods) {
  struct Want {
    std::string mod;
    const js::Value* spec;
  };
  std::vector<Want> wants;
  std::vector<js::Value> docs(mods.size());
  for (size_t m = 0; m < mods.size(); m++) {
    bool present = false;
    if (!ReadJson(c, mods[m], "sounds/effects.json", docs[m], present) || !present) continue;
    const js::Value* list = Field(docs[m], "effects");
    if (list == nullptr || list->kind != js::Value::Kind::Array) {
      Say(c, em::Message::Level::Problem, "The mod \"" + mods[m] + "\": sounds/effects.json needs \"effects\": [ ... ]; it is not used.");
      continue;
    }
    for (const js::Value& e : list->items) wants.push_back({mods[m], &e});
  }
  if (wants.empty()) return;

  std::string archivePath = DiscPath(c.job, kBanksArchive);
  Archive archive;
  if (!ReadCurrent(c, archivePath, archive.bytes) || !Fsys_Open(archive.bytes.data(), archive.bytes.size(), &archive.fsys)) {
    Say(c, em::Message::Level::Problem, std::string("Sound effects: the game's ") + kBanksArchive + " could not be read; the effects of the mods are not used.");
    return;
  }
  std::vector<BankData> banks(sizeof(kBanks) / sizeof(kBanks[0]));
  for (size_t i = 0; i < banks.size(); i++) banks[i].bank = &kBanks[i];
  std::set<std::string> done;
  std::map<std::string, std::string> sampleOwner;
  std::map<std::string, std::vector<uint8_t>> decoded;
  std::set<std::string> usedMods;
  int replaced = 0;
  for (const Want& w : wants) {
    const js::Value& e = *w.spec;
    const std::string where = "The mod \"" + w.mod + "\", sounds/effects.json line " + std::to_string(e.line) + ": ";
    const auto problem = [&](const std::string& text) { Say(c, em::Message::Level::Problem, where + text + " That effect keeps the game's sound."); };
    if (e.kind != js::Value::Kind::Object) {
      problem("each entry must be an object like {\"fx\": 1, \"file\": \"blip.wav\"}.");
      continue;
    }
    for (const auto& key : e.order) {
      static const char* const kKnown[] = {"fx", "sample", "bank", "file", "volume", "loop", "loop_start", "loop_end", "all", "note"};
      if (std::find(std::begin(kKnown), std::end(kKnown), key) == std::end(kKnown)) Say(c, em::Message::Level::Warning, where + "\"" + key + "\" is not a key of an effect; ignored.");
    }
    const js::Value* fileV = Field(e, "file");
    if (fileV == nullptr || fileV->kind != js::Value::Kind::String) {
      problem("\"file\" (a WAV or OGG of the mod) is missing.");
      continue;
    }
    fs::path file;
    if (!ModFile(c, w.mod, fileV->text, file)) {
      problem("there is no file \"" + fileV->text + "\" in the mod.");
      continue;
    }
    const js::Value* fxV = Field(e, "fx");
    const js::Value* smpV = Field(e, "sample");
    const js::Value* bankV = Field(e, "bank");
    if ((fxV == nullptr) == (smpV == nullptr)) {
      problem("give exactly one of \"fx\" (an effect number) or \"sample\".");
      continue;
    }
    const std::string bankName = bankV != nullptr && bankV->kind == js::Value::Kind::String ? bankV->text : "";
    for (BankData& b : banks) {
      if (!b.loaded && (bankName.empty() || bankName == b.bank->name)) {
        std::string why;
        if (!LoadBank(c, archive, b, why)) {
          Say(c, em::Message::Level::Problem, std::string("Sound effects: ") + why + ".");
          b.loaded = false;
        }
      }
    }
    BankData* bank = nullptr;
    std::vector<uint16_t> sampleIds;
    if (fxV != nullptr) {
      if (fxV->kind != js::Value::Kind::Number) {
        problem("\"fx\" must be a whole number.");
        continue;
      }
      const long fx = static_cast<long>(fxV->number);
      std::string why;
      for (BankData& b : banks) {
        if (!b.loaded) continue;
        for (const ea::sfx::Effect& ef : b.effects) {
          if (ef.fx != fx) continue;
          bank = &b;
          const js::Value* allV = Field(e, "all");
          const bool all = allV != nullptr && allV->kind == js::Value::Kind::Bool && allV->boolean;
          if (ef.samples.size() == 1 || (all && !ef.samples.empty())) sampleIds = ef.samples;
          else why = ef.samples.empty() ? "effect " + std::to_string(fx) + " does not start a sample itself; use \"sample\" (xd_audio_tool sfx-list)."
                                        : "effect " + std::to_string(fx) + " plays " + std::to_string(ef.samples.size()) + " different samples; name one with \"sample\", or add \"all\": true to replace all of them with this sound (xd_audio_tool sfx-list).";
        }
      }
      if (bank == nullptr) {
        problem("this disc has no effect " + std::to_string(fx) + " (xd_audio_tool sfx-list shows them).");
        continue;
      }
      if (!why.empty()) {
        problem(why);
        continue;
      }
    } else {
      if (smpV->kind != js::Value::Kind::Number) {
        problem("\"sample\" must be a whole number.");
        continue;
      }
      int hits = 0;
      for (BankData& b : banks) {
        if (!b.loaded || (!bankName.empty() && bankName != b.bank->name)) continue;
        for (const ea::sfx::Sample& s : b.samples) {
          if (s.id == static_cast<long>(smpV->number)) bank = &b, sampleIds.assign(1, s.id), hits++;
        }
      }
      if (hits != 1) {
        problem(hits == 0 ? "this disc has no sample " + std::to_string(static_cast<long>(smpV->number)) + (bankName.empty() ? "" : " in the bank \"" + bankName + "\"") + "."
                          : "sample " + std::to_string(static_cast<long>(smpV->number)) + " is in more than one bank; add \"bank\": \"menu\", \"battle\" or \"atmos\".");
        continue;
      }
    }
    std::vector<uint8_t> raw;
    ea::Pcm pcm;
    ea::LoopPoints fileLoop;
    std::string error;
    if (!ReadFileBytes(file, raw) || !ea::ReadSoundFile(raw, pcm, fileLoop, error)) {
      problem("\"" + fileV->text + "\" can't be used (" + (error.empty() ? std::string("unreadable") : error) + ").");
      continue;
    }
    ea::sfx::ReplaceOptions options;
    bool bad = false;
    options.volume = NumberOr(e, "volume", 1.0, bad);
    if (bad || options.volume <= 0 || options.volume > 8) {
      problem("\"volume\" must be a number above 0 (1 = as the file is).");
      continue;
    }
    if (const js::Value* l = Field(e, "loop")) {
      if (l->kind == js::Value::Kind::Bool) options.loop = l->boolean ? ea::sfx::ReplaceOptions::Loop::On : ea::sfx::ReplaceOptions::Loop::Off;
      else if (l->kind == js::Value::Kind::String && l->text == "keep") options.loop = ea::sfx::ReplaceOptions::Loop::Keep;
      else {
        problem("\"loop\" is true, false or \"keep\".");
        continue;
      }
    }
    options.loopStart = static_cast<uint64_t>(std::max(0.0, NumberOr(e, "loop_start", 0, bad)));
    options.loopEnd = static_cast<uint64_t>(std::max(0.0, NumberOr(e, "loop_end", 0, bad)));
    bool failed = false;
    for (const uint16_t sampleId : sampleIds) {
      const size_t grown = bank->samp.size() > bank->originalSize ? bank->samp.size() - bank->originalSize : 0;
      ea::sfx::ReplaceOptions each = options;
      each.maxGrow = grown < options.maxGrow ? options.maxGrow - grown : 0;
      ea::sfx::ReplaceResult result;
      const std::string key = std::string(bank->bank->name) + ":" + std::to_string(sampleId);
      if (!ea::sfx::ReplaceSample(bank->sdir, bank->samp, sampleId, pcm, fileLoop, each, result, error)) {
        problem(error + ".");
        failed = true;
        break;
      }
      bank->changed = true;
      replaced++;
      usedMods.insert(w.mod);
      const auto owner = sampleOwner.find(key);
      if (owner != sampleOwner.end() && owner->second != w.mod) Say(c, em::Message::Level::Warning, where + "the mod \"" + owner->second + "\" already replaced the same sound (" + key + "); this one wins.");
      sampleOwner[key] = w.mod;
      std::string notes;
      for (const std::string& n : result.notes) notes += (notes.empty() ? "" : "; ") + n;
      std::fprintf(stderr, "[mods] sound effect: %s sample %u <- %s (%s)\n", bank->bank->name, sampleId, fileV->text.c_str(), notes.c_str());
    }
    (void)failed;
  }
  std::vector<FsysReplacement> replacements;
  std::vector<std::vector<uint8_t>> keep;
  keep.reserve(banks.size());
  std::string modList;
  for (const std::string& m : usedMods) modList += (modList.empty() ? "" : ", ") + m;
  for (BankData& b : banks) {
    if (!b.changed) continue;
    uint32_t index = 0;
    std::vector<uint8_t> ignore;
    if (!archive.Entry(b.bank->sdir, ignore, index)) continue;
    keep.push_back(b.sdir);
    replacements.push_back({index, keep.back().data(), static_cast<uint32_t>(keep.back().size())});
  }
  if (replacements.empty()) return;
  size_t size = 0;
  uint8_t* built = Fsys_Rebuild(archive.bytes.data(), archive.bytes.size(), replacements.data(), replacements.size(), &size);
  if (built == nullptr) {
    Say(c, em::Message::Level::Problem, std::string("Sound effects: the game's ") + kBanksArchive + " could not be rebuilt; the effects of the mods are not used.");
    return;
  }
  WriteOut(c, archivePath, std::vector<uint8_t>(built, built + size), modList);
  std::free(built);
  for (BankData& b : banks) {
    if (b.changed) WriteOut(c, DiscPath(c.job, b.bank->samp), b.samp, modList);
  }
  if (replaced > 0) Say(c, em::Message::Level::Info, "Sound effects of mods: " + std::to_string(replaced) + " replaced.");
}

uint32_t NextRow(const std::vector<em::AddedRow>& rows, const char* table, uint32_t discRows, uint32_t taken) {
  uint32_t next = discRows + taken;
  for (const em::AddedRow& r : rows) {
    if (r.table == table) next = std::max(next, r.row + 1);
  }
  return next;
}

bool RowBytes(Ctx& c, const std::vector<em::AddedRow>& rows, const char* table, uint32_t number, std::vector<uint8_t>& out) {
  for (const em::AddedRow& r : rows) {
    if (r.table == table && r.row == number && !r.bytes.empty()) {
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

void BuildSongs(Ctx& c, const std::vector<std::string>& mods) {
  struct Song {
    std::string mod, name;
    fs::path file;
    const js::Value* spec;
    size_t line;
  };
  std::vector<Song> songs;
  std::vector<js::Value> docs(mods.size());
  for (size_t m = 0; m < mods.size(); m++) {
    bool present = false;
    if (!ReadJson(c, mods[m], "sounds/songs.json", docs[m], present) || !present) continue;
    const js::Value* list = Field(docs[m], "songs");
    if (list == nullptr || list->kind != js::Value::Kind::Array) {
      Say(c, em::Message::Level::Problem, "The mod \"" + mods[m] + "\": sounds/songs.json needs \"songs\": [ ... ]; it is not used.");
      continue;
    }
    for (const js::Value& s : list->items) {
      const std::string where = "The mod \"" + mods[m] + "\", sounds/songs.json line " + std::to_string(s.line) + ": ";
      if (s.kind != js::Value::Kind::Object) {
        Say(c, em::Message::Level::Problem, where + "each song must be an object like {\"name\": \"forest\", \"file\": \"forest.ogg\"}.");
        continue;
      }
      const js::Value* n = Field(s, "name");
      const js::Value* f = Field(s, "file");
      if (n == nullptr || n->kind != js::Value::Kind::String || !GoodName(n->text) || f == nullptr || f->kind != js::Value::Kind::String) {
        Say(c, em::Message::Level::Problem, where + "a song needs a \"name\" (letters, digits, - _ .) and a \"file\" (a WAV or OGG of the mod).");
        continue;
      }
      fs::path file;
      if (!ModFile(c, mods[m], f->text, file)) {
        Say(c, em::Message::Level::Problem, where + "there is no file \"" + f->text + "\" in the mod; the song \"" + n->text + "\" is not added.");
        continue;
      }
      songs.push_back({mods[m], n->text, file, &s, s.line});
    }
  }
  if (songs.empty()) return;
  if (songs.size() > kMaxSongs) songs.resize(kMaxSongs);

  const std::string tocPath = [&] {
    for (const std::string& f : c.job.discFiles) {
      if (Lower(f) == "gsfsys.toc") return f;
    }
    return std::string("GSfsys.toc");
  }();
  std::vector<uint8_t> tocBytes;
  xd::toc::Toc toc;
  if (!ReadCurrent(c, tocPath, tocBytes) || !xd::toc::Read(tocBytes, toc)) {
    Say(c, em::Message::Level::Problem, "New songs: the game's archive list (GSfsys.toc) could not be read; the songs of the mods are not added.");
    return;
  }
  std::vector<uint8_t> template_;
  if (!RowBytes(c, *c.job.rows, "snd_data", kSongTemplateRow, template_) || template_.size() != 12) {
    Say(c, em::Message::Level::Problem, "New songs: the game's sound table could not be found on this disc; the songs of the mods are not added.");
    return;
  }
  uint32_t nextGroup = std::max(xd::toc::TopId(toc), 5000u);
  uint32_t nextFile = NextRow(*c.job.rows, "snd_file", kDiscSndFile, sTakenFiles), nextData = NextRow(*c.job.rows, "snd_data", kDiscSndData, sTakenData);
  std::vector<std::pair<uint32_t, std::string>> newGroups;
  std::vector<em::AddedRow> added;
  std::set<std::string> names;
  for (const Song& s : songs) {
    const std::string where = "The mod \"" + s.mod + "\", sounds/songs.json line " + std::to_string(s.line) + ": ";
    const auto problem = [&](const std::string& text) { Say(c, em::Message::Level::Problem, where + "the song \"" + s.name + "\": " + text + " It is not added."); };
    if (!names.insert(s.mod + ":" + s.name).second) {
      problem("another song of this mod has the same name.");
      continue;
    }
    if (nextFile >= kDiscSndFile + kSpareRows || nextData >= kDiscSndData + kSpareRows) {
      problem("the game's sound tables are full.");
      continue;
    }
    std::vector<uint8_t> raw;
    ea::Pcm pcm;
    ea::LoopPoints fileLoop;
    std::string error;
    if (!ReadFileBytes(s.file, raw) || !ea::ReadSoundFile(raw, pcm, fileLoop, error)) {
      problem("the file can't be used (" + (error.empty() ? std::string("unreadable") : error) + ").");
      continue;
    }
    ea::BuildOptions options;
    bool bad = false;
    const js::Value* loopV = Field(*s.spec, "loop");
    options.loop = loopV != nullptr ? (loopV->kind == js::Value::Kind::Bool && loopV->boolean) : fileLoop.found;
    options.loopStart = static_cast<uint64_t>(std::max(0.0, NumberOr(*s.spec, "loop_start", fileLoop.found ? static_cast<double>(fileLoop.start) : 0, bad)));
    options.loopEnd = static_cast<uint64_t>(std::max(0.0, NumberOr(*s.spec, "loop_end", fileLoop.found ? static_cast<double>(fileLoop.end) : 0, bad)));
    const js::Value* trim = Field(*s.spec, "trim_intro");
    options.trimIntro = trim != nullptr && trim->kind == js::Value::Kind::Bool && trim->boolean;
    ea::BuildResult built;
    if (!ea::BuildStream(pcm, options, built, error)) {
      problem(error + ".");
      continue;
    }
    const uint32_t group = ++nextGroup;
    const std::string archiveName = "stm_modsong" + std::to_string(group);
    const uint32_t bodyId = 1, headerId = 2;
    xd::toc::Member body, header;
    body.word0 = (bodyId << 16) | (21u << 9), body.type = 21, body.name = "(null)", body.data = std::move(built.body);
    header.word0 = (headerId << 16) | (20u << 9), header.type = 20, header.name = "(null)", header.data = std::move(built.header);
    WriteOut(c, archiveName + ".fsys", xd::toc::MakeFsys(group, {header, body}), s.mod);
    newGroups.emplace_back(group, archiveName);
    em::AddedRow f, d;
    f.table = "snd_file", f.id = f.row = nextFile, f.copyOf = 0, f.key = "song file " + s.name;
    d.table = "snd_data", d.id = d.row = nextData, d.copyOf = kSongTemplateRow, d.key = "song:" + s.name;
    f.unbound = d.unbound = true;
    for (em::AddedRow* r : {&f, &d}) r->mod = s.mod, r->where = "sounds/songs.json line " + std::to_string(s.line);
    f.bytes.assign(12, 0);
    xd::toc::PutBe32(f.bytes.data(), group), xd::toc::PutBe32(f.bytes.data() + 4, 0), xd::toc::PutBe32(f.bytes.data() + 8, 0);
    d.bytes = template_;
    d.bytes[6] = static_cast<uint8_t>(nextFile >> 8), d.bytes[7] = static_cast<uint8_t>(nextFile);
    if (const js::Value* v = Field(*s.spec, "volume")) {
      if (v->kind == js::Value::Kind::Number && v->number >= 1 && v->number <= 127) d.bytes[1] = static_cast<uint8_t>(v->number);
    }
    sSongs[s.mod + ":" + s.name] = static_cast<long>(nextData);
    if (sSongs.count(s.name) == 0) sSongs[s.name] = static_cast<long>(nextData);
    else sSongs[s.name] = -2;
    std::fprintf(stderr, "[mods] song \"%s\" (%s): archive %s.fsys (group %u), snd_file row %u, snd_data row (the song's number) %u; %s\n", s.name.c_str(), s.mod.c_str(),
                 archiveName.c_str(), group, nextFile, nextData, built.notes.empty() ? "" : built.notes[0].c_str());
    for (const std::string& n : built.notes) Say(c, em::Message::Level::Info, "The song \"" + s.name + "\" of the mod \"" + s.mod + "\": " + n + ".");
    added.push_back(std::move(f));
    added.push_back(std::move(d));
    nextFile++, nextData++;
  }
  if (!newGroups.empty()) WriteOut(c, tocPath, xd::toc::Add(toc, newGroups), "");
  for (em::AddedRow& r : added) c.job.rows->push_back(std::move(r));
}

}

void SetTaken(uint32_t sndFiles, uint32_t sndData) { sTakenFiles = sndFiles, sTakenData = sndData; }

long SongNumber(const std::string& name) {
  const auto it = sSongs.find(name);
  return it == sSongs.end() || it->second < 0 ? -1 : it->second;
}

void BuildSounds(em::RowsJob& job) {
  sSongs.clear();
  Ctx c{job, ""};
  std::vector<std::string> mods;
  for (const auto& [id, folder] : job.modFolders) {
    fs::path unused;
    if (em::ResolveLayoutFile(folder, job.gameId, "sounds/effects.json", unused) || em::ResolveLayoutFile(folder, job.gameId, "sounds/songs.json", unused)) mods.push_back(id);
  }
  if (mods.empty() || job.rows == nullptr) return;
  BuildEffects(c, mods);
  BuildSongs(c, mods);
}

}
