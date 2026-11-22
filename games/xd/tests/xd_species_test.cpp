#include "xd_species.h"
#include "xd_toc.h"

#include <dsp_adpcm.h>
#include <gs_formats.h>
#include <mods.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace em = engine::mods;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}
uint32_t get32(const std::vector<uint8_t>& v, size_t at) { return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3]; }
uint32_t get16(const std::vector<uint8_t>& v, size_t at) { return uint32_t(v[at]) << 8 | v[at + 1]; }

std::vector<uint8_t> wav_of(uint32_t rate, uint32_t channels, double seconds, double hz) {
  const size_t n = size_t(rate * seconds);
  std::vector<uint8_t> w(44 + n * channels * 2, 0);
  const auto p32 = [&](size_t at, uint32_t x) { for (int k = 0; k < 4; k++) w[at + size_t(k)] = uint8_t(x >> (8 * k)); };
  const auto p16 = [&](size_t at, uint32_t x) { w[at] = uint8_t(x), w[at + 1] = uint8_t(x >> 8); };
  std::memcpy(&w[0], "RIFF", 4), p32(4, uint32_t(w.size() - 8)), std::memcpy(&w[8], "WAVEfmt ", 8);
  p32(16, 16), p16(20, 1), p16(22, channels), p32(24, rate), p32(28, rate * channels * 2), p16(32, uint16_t(channels * 2)), p16(34, 16);
  std::memcpy(&w[36], "data", 4), p32(40, uint32_t(n * channels * 2));
  for (size_t i = 0; i < n; i++) {
    const double t = double(i) / rate, env = std::exp(-2.0 * t);
    const int16_t v = int16_t(std::lround(12000.0 * env * std::sin(2 * 3.14159265358979 * (hz + 300 * t) * t)));
    for (uint32_t c = 0; c < channels; c++) p16(44 + (i * channels + c) * 2, uint16_t(v));
  }
  return w;
}

double snr_db(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  double s = 0, e = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); i++) s += double(a[i]) * a[i], e += (double(a[i]) - b[i]) * (double(a[i]) - b[i]);
  return 10 * std::log10((s + 1) / (e + 1));
}

void write(const fs::path& p, const std::vector<uint8_t>& d) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}

}

int main() {
  {
    std::string error;
    engine::dsp::Pcm pcm;
    check(engine::dsp::ReadWav(wav_of(22050, 1, 0.8, 440), pcm, error) && pcm.channels.size() == 1 && pcm.rate == 22050, "wav: a mono 16-bit file is read");
    const engine::dsp::Pcm r = engine::dsp::Resample(pcm, 32000);
    check(r.rate == 32000 && r.channels[0].size() == size_t(pcm.channels[0].size() * 32000ull / 22050), "wav: resampled to 32 kHz");
    const engine::dsp::Encoded e = engine::dsp::Encode(r.channels[0]);
    const std::vector<int16_t> back = engine::dsp::Decode(e);
    check(back.size() == r.channels[0].size() && e.frames.size() == (back.size() + 13) / 14 * 8, "adpcm: 8 bytes for every 14 samples");
    const double snr = snr_db(r.channels[0], back);
    std::printf("      (round trip signal-to-noise %.1f dB)\n", snr);
    check(snr > 25.0, "adpcm: the sound survives the round trip (more than 25 dB)");
    check(!engine::dsp::ReadWav(std::vector<uint8_t>{1, 2, 3}, pcm, error) && !error.empty(), "wav: junk is refused with a reason");
  }
  {
    xd::species::Cry cry;
    std::string error;
    check(xd::species::MakeCry(wav_of(22050, 1, 0.8, 440), cry, error), "cry: made from a mono WAV");
    check(cry.header.size() == 192 && std::memcmp(cry.header.data(), "I_SF", 4) == 0 && get32(cry.header, 0x08) == 32000 && get32(cry.header, 0x14) == 2 && get32(cry.header, 0x18) == 0x8000,
          "cry: the header says I_SF, 32 kHz, 2 channels, 0x8000-byte slices");
    const uint32_t samples = get32(cry.header, 0x0C);
    check(samples == 25600 && cry.body.size() == 2 * 0x8000, "cry: 0.8 s = 25600 samples, one slice per channel");
    check(!cry.wave.empty() && cry.wave.size() == size_t(cry.wave[0]) + 1 && cry.wave[0] >= 16 && cry.wave[0] <= 60, "cry: a PDA waveform of 16 to 60 values");
    check(!cry.cut, "cry: not cut");
    xd::species::Cry longer;
    check(xd::species::MakeCry(wav_of(32000, 2, 5.0, 300), longer, error) && longer.cut && get32(longer.header, 0x0C) == 114688 && longer.body.size() == 4 * 0x8000, "cry: 5 seconds are cut at 3.58 s (two slices per channel)");
    engine::dsp::Encoded e;
    for (int k = 0; k < 16; k++) e.coefficients[k] = int16_t(get16(cry.header, size_t(0x40 + 2 * k)));
    e.samples = samples;
    e.frames.assign(cry.body.begin(), cry.body.begin() + (samples + 13) / 14 * 8);
    engine::dsp::Pcm src;
    engine::dsp::ReadWav(wav_of(22050, 1, 0.8, 440), src, error);
    check(snr_db(engine::dsp::Resample(src, 32000).channels[0], engine::dsp::Decode(e)) > 25.0, "cry: the body's first channel decodes to the sound");
  }
  const fs::path root = fs::temp_directory_path() / "xd_species_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  std::vector<uint8_t> goodPkx(0x300, 0);
  goodPkx[3] = 0x10, goodPkx[0x1B] = 0x0C;
  std::vector<uint8_t> badPkx(0x300, 0);
  write(root / "mod" / "models" / "z.pkx", goodPkx);
  write(root / "mod" / "models" / "bad.pkx", badPkx);
  write(root / "mod" / "sounds" / "z.wav", wav_of(22050, 1, 0.5, 500));
  write(root / "mod" / "sounds" / "bad.wav", {'R', 'I', 'F', 'F', 0, 0, 0, 0});
  em::DataCatalog catalog;
  {
    em::DataTable species;
    species.name = "pokemon_data", species.entrySize = 0x30, species.count = 4, species.places = {{"common.fsys", "common_rel", 0}};
    catalog.tables.push_back(species);
    em::DataTable flags;
    flags.name = "flag_data", flags.entrySize = 6, flags.count = 20, flags.places = {{"common.fsys", "flags", 0}};
    catalog.tables.push_back(flags);
    em::DataTable snd;
    snd.name = "snd_data", snd.entrySize = 12, snd.count = 1507, snd.places = {{"common.fsys", "snd", 0}};
    catalog.tables.push_back(snd);
    em::DataTable moves;
    moves.name = "waza_data", moves.entrySize = 0x38, moves.count = 3, moves.places = {{"common.fsys", "moves", 0}};
    catalog.tables.push_back(moves);
    catalog.lists.push_back(em::NameList{"species", {{"Alpha", 1}, {"Beta", 2}}, 4});
    catalog.lists.push_back(em::NameList{"move", {{"Tackle", 1}, {"Surf", 2}}, 3});
  }
  std::vector<uint8_t> speciesTable(4 * 0x30, 0), sndTable(1507 * 12, 0), moveTable(3 * 0x38, 0);
  for (uint32_t n = 0; n < 4; n++) {
    uint8_t* r = &speciesTable[n * 0x30];
    r[0x0D] = uint8_t(100 + n), r[0x0F] = uint8_t(n), r[0x11] = uint8_t(200 + n), r[0x2F] = uint8_t(50 + n);
  }
  for (size_t i = 0; i < 1507; i++) sndTable[i * 12 + 1] = 0x55;
  moveTable[2 * 0x38 + 0x33] = 9;
  moveTable[1 * 0x38 + 0x33] = 4;
  xd::toc::Toc toc0;
  {
    std::vector<uint8_t> t(0x20, 0);
    xd::toc::PutBe32(&t[8], 0), xd::toc::PutBe32(&t[0x10], 0x20);
    xd::toc::Read(t, toc0);
  }
  const std::vector<uint8_t> tocBytes = xd::toc::Add(toc0, {{100, "common"}, {1966, "stm_se_nakigoe_archive"}});
  xd::toc::Member h{(7u << 16) | (20u << 9), 20, "h", std::vector<uint8_t>(192, 1)}, b{(8u << 16) | (21u << 9), 21, "b", std::vector<uint8_t>(0x8000, 2)};
  const std::vector<uint8_t> cryArchive = xd::toc::MakeFsys(1966, {h, b});

  const auto find_move = [](const std::vector<em::AddedRow>& all, uint32_t id) -> const em::AddedRow* {
    for (const em::AddedRow& r : all) {
      if (r.table == "waza_data" && r.id == id) return &r;
    }
    return nullptr;
  };
  const auto make = [&](const std::string& key, uint32_t id) {
    em::AddedRow r;
    r.table = "pokemon_data", r.id = r.row = id, r.key = key, r.mod = "m", r.modName = "M", r.where = "data/x.json line 3";
    r.bytes = std::vector<uint8_t>(speciesTable.begin() + 0x30, speciesTable.begin() + 0x60);
    return r;
  };
  em::AddedRow zed = make("Zed", 4), yak = make("Yak", 5), wob = make("Wob", 6), vim = make("Vim", 7), uno = make("Uno", 8), dup = make("Dup", 9), cp = make("Cop", 10);
  zed.assets = {{"model", "models/z.pkx"}, {"cry", "sounds/z.wav"}, {"dex", "own"}};
  yak.assets = {{"model", "Beta"}, {"cry", "Beta"}, {"dex", "copy"}};
  wob.assets = {{"model", "models/missing.pkx"}, {"dex", "own"}};
  vim.assets = {{"model", "models/bad.pkx"}, {"dex", "OWN"}};
  uno.assets = {{"cry", "sounds/bad.wav"}, {"dex", "own"}};
  dup.assets = {{"model", "models/z.pkx"}, {"cry", "sounds/z.wav"}, {"dex", "own"}};
  cp.assets = {{"dex", "maybe"}};
  em::AddedRow move;
  move.table = "waza_data", move.id = move.row = 3, move.key = "Zap", move.mod = "m", move.modName = "M", move.where = "data/x.json line 9";
  move.bytes.assign(0x38, 0);
  move.assets = {{"animation", "Surf"}};
  em::AddedRow mixed = move, mixed2 = move, mixed3 = move;
  mixed.id = mixed.row = 4, mixed.key = "Mix";
  mixed.assets = {{"animation_attack", "Surf"}, {"animation_damage", "Tackle"}, {"animation_status", "none"}};
  mixed2.id = mixed2.row = 5, mixed2.key = "Mix2";
  mixed2.assets = {{"animation", "Surf"}, {"animation_damage", "Tackle"}};
  mixed3.id = mixed3.row = 6, mixed3.key = "Mix3";
  mixed3.assets = {{"animation_attack", "NoSuchMove"}};
  std::vector<em::AddedRow> rows = {zed, yak, wob, vim, uno, dup, cp, move, mixed, mixed2, mixed3};
  em::RowsJob job;
  job.rows = &rows;
  job.gameId = "GXXE01";
  job.modFolders["m"] = root / "mod";
  job.catalog = &catalog;
  job.discFiles = {"GSfsys.toc", "stm_se_nakigoe_archive.fsys"};
  job.readFile = [&](const std::string& path, std::vector<uint8_t>& out) {
    if (path == "GSfsys.toc") return out = tocBytes, true;
    if (path == "stm_se_nakigoe_archive.fsys") return out = cryArchive, true;
    return false;
  };
  job.readEntry = [&](const std::string& a, const std::string& e, std::vector<uint8_t>& out) {
    if (a != "common.fsys") return false;
    if (e == "common_rel") return out = speciesTable, true;
    if (e == "snd") return out = sndTable, true;
    if (e == "moves") return out = moveTable, true;
    return false;
  };
  std::vector<std::string> said;
  job.say = [&](em::Message::Level, const std::string& t) { said.push_back(t); };
  xd::species::Taken taken;
  taken.models = 2, taken.sndFiles = 3, taken.sndData = 3, taken.waves = 2, taken.flags = 4;
  xd::species::SetTaken(taken);
  xd::species::BuildSpecies(job);
  xd::species::SetTaken({});
  {
    const auto wzx = [&](uint32_t row) -> const em::AddedRow* {
      for (const em::AddedRow& r : rows) {
        if (r.table == "waza_wzx_data" && r.row == row) return &r;
      }
      return nullptr;
    };
    const em::AddedRow* a = wzx(373);
    const em::AddedRow* b = wzx(374);
    check(a != nullptr && a->bytes.size() == 6 && get16(a->bytes, 0) == 9 && get16(a->bytes, 2) == 4 && get16(a->bytes, 4) == 0xFFFF, "animation: a move put together from pieces gets a new animation row (attack from Surf's row 9, damage from Tackle's row 4, no status piece)");
    check(find_move(rows, 4) != nullptr && get32(find_move(rows, 4)->bytes, 0x30) == 373, "animation: the move plays the new row");
    check(b != nullptr && get16(b->bytes, 0) == 9 && get16(b->bytes, 2) == 4 && get16(b->bytes, 4) == 9, "animation: \"animation\" names the move for the pieces not named");
    check(find_move(rows, 5) != nullptr && get32(find_move(rows, 5)->bytes, 0x30) == 374, "animation: the second move plays row 374");
    check(wzx(375) == nullptr && find_move(rows, 6) != nullptr && get32(find_move(rows, 6)->bytes, 0x30) == 0, "animation: a piece from a move that does not exist adds no row and leaves the move as it was");
  }

  const auto find = [&](const std::string& table, uint32_t id) -> const em::AddedRow* {
    for (const em::AddedRow& r : rows) {
      if (r.table == table && r.id == id) return &r;
    }
    return nullptr;
  };
  for (const std::string& s : said) std::printf("      (said: %s)\n", s.c_str());
  const em::AddedRow& z = rows[0];
  check(get32(z.bytes, 0x2C) == 421, "step: the model is number 421 (the disc's 419 + the pack's 2 + 0)");
  const em::AddedRow* model = find("pokemon_pkx_data", 421);
  check(model != nullptr && model->bytes.size() == 8 && get32(model->bytes, 0) == 5001 && get32(model->bytes, 4) == 0x11E00, "step: its row is {group 5001, member 0x11E00}");
  check(get16(z.bytes, 0x0C) == 1507 + 3 && get16(z.bytes, 0x10) == 1507 + 3 && get16(z.bytes, 0x0E) == 389 + 2, "step: voice and shout are the new snd_data row, the waveform row follows the pack's");
  const em::AddedRow* sf = find("snd_file", 516 + 3);
  const em::AddedRow* sd = find("snd_data", 1507 + 3);
  const em::AddedRow* wv = find("pokemon_nakigoe_data", 389 + 2);
  check(sf && sd && wv, "step: rows of snd_file, snd_data and the waveform table were added");
  if (sf && sd && wv) {
    check(sf->bytes.size() == 12 && get32(sf->bytes, 0) == 1966 && (get32(sf->bytes, 4) >> 16) == 9 && (get32(sf->bytes, 8) >> 16) == 10, "step: the snd_file row names the archive and the two new members (ids 9 and 10)");
    check((get32(sf->bytes, 4) & 0xFFFF) == (20u << 9) && (get32(sf->bytes, 8) & 0xFFFF) == (21u << 9), "step: ... with the type bits of header and body members");
    check(sd->bytes.size() == 12 && get16(sd->bytes, 6) == 516 + 3 && sd->bytes[1] == 0x55 && sd->copyOf == 101, "step: the snd_data row is a copy of the copied species' voice row with the new file");
    check(wv->bytes.size() == size_t(wv->bytes[0]) + 1, "step: the waveform row is {count, samples}");
  }
  check(get32(z.bytes, 0x24) == 20 + 4 && get32(z.bytes, 0x28) == 20 + 5, "step: its seen / caught marks are the two flag rows after the table's end and the pack's");
  const em::AddedRow* seen = find("flag_data", 24);
  check(seen != nullptr && seen->bytes == std::vector<uint8_t>({0xC1, 0, 0, 0, 0, 0}), "step: a flag row is level 3, 1 bit");
  check(get32(rows[1].bytes, 0x2C) == 52 && get16(rows[1].bytes, 0x0C) == 102 && get16(rows[1].bytes, 0x0E) == 2 && get16(rows[1].bytes, 0x10) == 202, "step: \"Beta\" gives the model, voice, waveform and shout of that species");
  check(get32(rows[1].bytes, 0x24) == 0 && get32(rows[1].bytes, 0x28) == 0, "step: \"dex\": \"copy\" adds no flag rows");
  check(get32(rows[2].bytes, 0x24) != 0, "step: own marks are asked for with \"dex\": \"own\" (any letter case)");
  check(get32(rows[5].bytes, 0x2C) == 421 && get16(rows[5].bytes, 0x0C) == 1507 + 3, "step: the same files again are stored once (same model, same cry)");
  check(get32(rows[2].bytes, 0x2C) == 51 && get32(rows[3].bytes, 0x2C) == 51 && get16(rows[4].bytes, 0x0C) == 101, "step: a missing file, a bad model and a bad WAV leave what the species copies");
  check(said.size() == 5, "step: five problems told (missing model, bad model, bad WAV, \"maybe\", an animation piece from no move) (got " + std::to_string(said.size()) + ")");
  check(get32(rows[7].bytes, 0x30) == 9, "step: a move's \"animation\" takes the animation row of that move");
  size_t pkxCount = 0, flagCount = 0;
  for (const em::AddedRow& r : rows) pkxCount += r.table == "pokemon_pkx_data", flagCount += r.table == "flag_data";
  check(pkxCount == 1, "step: one model row for two species with the same file");
  check(flagCount == 10, "step: two flag rows for every species that has own marks (Zed, Vim, Uno, Wob, Dup)");
  check(job.outFiles.size() == 3, "step: three files handed back (archive list, model archive, cry archive) (got " + std::to_string(job.outFiles.size()) + ")");
  for (const em::RowsJob::OutFile& o : job.outFiles) {
    if (o.discPath == "pkx_m5001.fsys") {
      FsysArchive f;
      check(Fsys_Open(o.data.data(), o.data.size(), &f) && f.count == 1 && get32(o.data, 8) == 5001, "step: the model archive holds one member and carries its group");
      uint32_t n = 0;
      uint8_t* p = Fsys_Extract(&f, 0, &n);
      check(p != nullptr && n == 0x300 && std::memcmp(p, goodPkx.data(), n) == 0, "step: ... the mod's file, unchanged");
      std::free(p);
    } else if (o.discPath == "stm_se_nakigoe_archive.fsys") {
      FsysArchive f;
      check(Fsys_Open(o.data.data(), o.data.size(), &f) && f.count == 4, "step: the cry archive has two more members");
    } else if (o.discPath == "GSfsys.toc") {
      xd::toc::Toc t;
      check(xd::toc::Read(o.data, t) && t.count == 3 && xd::toc::TopId(t) == 5001, "step: the archive list has one more group (5001)");
    } else {
      check(false, "step: an unexpected file " + o.discPath);
    }
  }
  fs::remove_all(root, ec);
  std::printf(sOk ? "xd_species_test: all passed\n" : "xd_species_test: FAILED\n");
  return sOk ? 0 : 1;
}
