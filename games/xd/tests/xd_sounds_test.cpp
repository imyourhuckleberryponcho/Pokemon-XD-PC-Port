#define _USE_MATH_DEFINES
#include "xd_sounds.h"
#include "xd_toc.h"

#include <audio_sfx.h>
#include <gs_formats.h>
#include <mods.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace em = engine::mods;
namespace ea = engine::audio;
namespace es = engine::audio::sfx;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) failures++;
}

namespace {

ea::Pcm Tone(int rate, double seconds, double freq) {
  ea::Pcm p;
  p.rate = rate;
  const size_t n = static_cast<size_t>(rate * seconds);
  p.ch.assign(1, std::vector<float>(n));
  for (size_t i = 0; i < n; i++) p.ch[0][i] = static_cast<float>(0.4 * std::sin(2 * M_PI * freq * static_cast<double>(i) / rate));
  return p;
}
void WriteWavFile(const fs::path& p, const ea::Pcm& pcm) {
  const std::vector<uint8_t> wav = ea::WriteWav(pcm, nullptr);
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
}
void Put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x); }
void Put16(std::vector<uint8_t>& v, size_t at, uint32_t x) { v[at] = uint8_t(x >> 8), v[at + 1] = uint8_t(x); }

}

int main() {
  const fs::path root = fs::temp_directory_path() / "xd_sounds_test";
  fs::remove_all(root);
  fs::create_directories(root / "mod" / "sounds");
  const size_t n = 3;
  std::vector<uint8_t> sdir(n * 32 + 4 + n * 40, 0), samp;
  for (size_t i = 0; i < n; i++) {
    ea::Pcm tone = Tone(22050, 0.2, 440.0 * (i + 1));
    std::vector<int16_t> pcm(tone.Frames());
    for (size_t k = 0; k < pcm.size(); k++) pcm[k] = static_cast<int16_t>(tone.ch[0][k] * 32000);
    ea::AdpcmChannel ch;
    std::vector<uint8_t> frames;
    ea::AdpcmEncode(pcm, -1, ch, frames);
    const size_t e = i * 32, x = n * 32 + 4 + i * 40;
    Put16(sdir, e, 10 + i), Put32(sdir, e + 4, static_cast<uint32_t>(samp.size())), Put32(sdir, e + 0x0C, 0x3C000000u | 22050), Put32(sdir, e + 0x10, static_cast<uint32_t>(pcm.size())), Put32(sdir, e + 0x1C, static_cast<uint32_t>(x));
    sdir[x + 1] = 8;
    for (int k = 0; k < 16; k++) sdir[x + 8 + 2 * k] = uint8_t(ch.coef[k] >> 8), sdir[x + 9 + 2 * k] = uint8_t(ch.coef[k]);
    samp.insert(samp.end(), frames.begin(), frames.end());
    while (samp.size() % 32) samp.push_back(0);
  }
  sdir[n * 32] = 0xFF, sdir[n * 32 + 1] = 0xFF;
  std::vector<uint8_t> proj(0x28 + 4 + 3 * 10, 0), pool(16, 0);
  Put32(proj, 0, 0xFFFFFFFFu), Put32(proj, 0x1C, 0x28), Put16(proj, 0x28, 3);
  const uint16_t macros[3] = {100, 101, 102};
  for (size_t i = 0; i < 3; i++) Put16(proj, 0x2C + i * 10, static_cast<uint32_t>(1 + i)), Put16(proj, 0x2E + i * 10, macros[i]), proj[0x30 + i * 10] = 1, proj[0x32 + i * 10] = 127;
  Put32(pool, 0, 16);
  const auto macro = [&](uint16_t id, std::vector<uint16_t> samples) {
    const size_t at = pool.size();
    pool.resize(at + 8 + 8 * samples.size() + 8, 0);
    Put32(pool, at, static_cast<uint32_t>(pool.size() - at)), Put16(pool, at + 4, id);
    for (size_t k = 0; k < samples.size(); k++) Put32(pool, at + 8 + 8 * k, (static_cast<uint32_t>(samples[k]) << 8) | 0x10);
  };
  macro(100, {10}), macro(101, {11, 12}), macro(102, {12});
  const size_t end = pool.size();
  pool.resize(end + 8, 0);
  Put32(pool, end, 0xFFFFFFFFu);
  {
    std::vector<ea::sfx::Effect> fx;
    std::string error;
    check(es::ParseEffects(proj, pool, fx, error) && fx.size() == 3, "bank: the made-up project has three effects");
    check(fx.size() == 3 && fx[0].samples == std::vector<uint16_t>{10} && fx[1].samples.size() == 2 && fx[2].samples == std::vector<uint16_t>{12}, "bank: effect 2 plays two samples, the others one");
  }
  const auto member = [](const char* name, const std::vector<uint8_t>& data, uint32_t id) { return xd::toc::Member{(id << 16) | (0u << 9), 0, name, data}; };
  std::vector<uint8_t> noEffects = proj;
  Put16(noEffects, 0x28, 0);
  const std::vector<uint8_t> archive = xd::toc::MakeFsys(7, {member("snd_se_proj", proj, 1), member("snd_se_pool", pool, 2), member("snd_se_sdir", sdir, 3),
                                                              member("snd_se_battle_proj", noEffects, 4), member("snd_se_battle_pool", pool, 5), member("snd_se_battle_sdir", sdir, 6),
                                                              member("snd_music_atmos_proj", noEffects, 7), member("snd_music_atmos_pool", pool, 8), member("snd_music_atmos_sdir", sdir, 9)});
  std::vector<uint8_t> tocHeader(0x20, 0);
  Put32(tocHeader, 0x10, 0x20);
  xd::toc::Toc toc0;
  xd::toc::Read(tocHeader, toc0);
  const std::vector<uint8_t> tocBytes = xd::toc::Add(toc0, {{100, "common"}});

  std::ofstream(root / "mod" / "sounds" / "effects.json") << R"({ "effects": [
    { "fx": 1, "file": "sounds/short.wav" },
    { "fx": 2, "file": "sounds/short.wav" },
    { "fx": 2, "file": "sounds/short.wav", "all": true },
    { "sample": 12, "bank": "menu", "file": "sounds/long.wav", "volume": 0.5 },
    { "fx": 9, "file": "sounds/short.wav" },
    { "fx": 3, "file": "sounds/missing.wav" } ] })";
  WriteWavFile(root / "mod" / "sounds" / "short.wav", Tone(44100, 0.05, 880));
  WriteWavFile(root / "mod" / "sounds" / "long.wav", Tone(22050, 1.0, 330));
  std::ofstream(root / "mod" / "sounds" / "songs.json") << R"({ "songs": [ { "name": "tune", "file": "sounds/long.wav", "loop": true } ] })";

  std::vector<em::AddedRow> rows;
  em::DataCatalog catalog;
  em::DataTable snd;
  snd.name = "snd_data", snd.entrySize = 12, snd.count = 1507, snd.places = {{"common.fsys", "snd", 0}};
  catalog.tables.push_back(snd);
  std::vector<uint8_t> sndTable(1507 * 12, 0);
  sndTable[1267 * 12 + 1] = 111;
  em::RowsJob job;
  job.rows = &rows;
  job.gameId = "GXXE01";
  job.modFolders["m"] = root / "mod";
  job.catalog = &catalog;
  job.discFiles = {"GSfsys.toc", "common.fsys", "sound/snd_se.samp", "sound/snd_se_battle.samp", "sound/snd_music_atmos.samp"};
  job.readFile = [&](const std::string& path, std::vector<uint8_t>& out) {
    if (path == "GSfsys.toc") return out = tocBytes, true;
    if (path == "common.fsys") return out = archive, true;
    if (path.rfind("sound/", 0) == 0) return out = samp, true;
    return false;
  };
  job.readEntry = [&](const std::string& a, const std::string& e, std::vector<uint8_t>& out) { return a == "common.fsys" && e == "snd" && (out = sndTable, true); };
  std::vector<std::string> said;
  job.say = [&](em::Message::Level, const std::string& t) { said.push_back(t); };
  xd::sound::BuildSounds(job);
  for (const std::string& s : said) std::printf("      (said: %s)\n", s.c_str());

  const auto out = [&](const std::string& path) -> const em::RowsJob::OutFile* {
    for (const auto& o : job.outFiles) {
      if (o.discPath == path) return &o;
    }
    return nullptr;
  };
  const auto* common = out("common.fsys");
  const auto* menuSamp = out("sound/snd_se.samp");
  check(common != nullptr && menuSamp != nullptr, "effects: the directory's archive and the menu sample file are replaced");
  check(out("sound/snd_se_battle.samp") == nullptr, "effects: a bank no effect names is left alone");
  if (common != nullptr && menuSamp != nullptr) {
    FsysArchive fsys{};
    check(Fsys_Open(common->data.data(), common->data.size(), &fsys), "effects: the new archive opens");
    const int i = Fsys_Find(&fsys, "snd_se_sdir");
    uint32_t size = 0;
    uint8_t* p = i >= 0 ? Fsys_Extract(&fsys, static_cast<uint32_t>(i), &size) : nullptr;
    std::vector<uint8_t> newSdir(p, p + size);
    std::free(p);
    ea::Pcm heard;
    ea::LoopPoints loop;
    std::string error;
    check(!newSdir.empty() && es::DecodeSample(newSdir, menuSamp->data, 10, heard, loop, error) && std::fabs(static_cast<double>(heard.Frames()) / heard.rate - 0.05) < 0.01, "effects: sample 10 (effect 1) now plays the 0.05 s sound");
    check(es::DecodeSample(newSdir, menuSamp->data, 12, heard, loop, error) && std::fabs(static_cast<double>(heard.Frames()) / heard.rate - 1.0) < 0.02, "effects: sample 12 (named by \"sample\") now plays the 1 s sound");
    check(es::DecodeSample(newSdir, menuSamp->data, 11, heard, loop, error) && std::fabs(static_cast<double>(heard.Frames()) / heard.rate - 0.05) < 0.01, "effects: \"all\": true replaced every sample of an effect that plays two (sample 11)");
    check(menuSamp->data.size() > samp.size(), "effects: the longer sound went to the end of the sample file");
  }
  bool toldTwo = false, toldNine = false, toldMissing = false;
  for (const std::string& s : said) {
    toldTwo |= s.find("effect 2") != std::string::npos && s.find("different samples") != std::string::npos;
    toldNine |= s.find("no effect 9") != std::string::npos;
    toldMissing |= s.find("missing.wav") != std::string::npos;
  }
  check(toldTwo && toldNine && toldMissing, "effects: an effect with two samples, an unknown effect and a missing file are each told, and the rest still works");

  const em::AddedRow* file = nullptr;
  const em::AddedRow* data = nullptr;
  for (const em::AddedRow& r : rows) {
    if (r.table == "snd_file") file = &r;
    if (r.table == "snd_data") data = &r;
  }
  check(file != nullptr && data != nullptr && file->row == 516 && data->row == 1507 && data->key == "song:tune", "songs: a song adds a snd_file row 516 and a snd_data row 1507");
  check(file != nullptr && data != nullptr && file->unbound && data->unbound, "songs: its rows are left out of the playthrough's list (a save loads without the mod)");
  check(data != nullptr && data->bytes.size() == 12 && data->bytes[1] == 111 && ((data->bytes[6] << 8) | data->bytes[7]) == 516, "songs: the snd_data row is a copy of the song row with the new file");
  check(xd::sound::SongNumber("tune") == 1507 && xd::sound::SongNumber("m:tune") == 1507 && xd::sound::SongNumber("nope") == -1, "songs: the number is found by name");
  const auto* newArchive = out("stm_modsong5001.fsys");
  FsysArchive song{};
  check(newArchive != nullptr && Fsys_Open(newArchive->data.data(), newArchive->data.size(), &song) && song.count == 2, "songs: a new archive with two members (header and sound)");
  check(out("GSfsys.toc") != nullptr, "songs: the archive list names the new archive");

  fs::remove_all(root);
  std::printf(failures == 0 ? "xd_sounds_test: all passed\n" : "xd_sounds_test: FAILED\n");
  return failures == 0 ? 0 : 1;
}
