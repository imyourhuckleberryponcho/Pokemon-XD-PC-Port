#include <audio_sfx.h>
#include <audio_stream.h>
#include <gs_formats.h>
#include <port_disc.h>
#include <xd_sound_banks.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ea = engine::audio;
namespace fs = std::filesystem;

namespace {

long long gBodyId = -1;
long long gHeaderId = -1;

struct Pair {
  uint32_t header = 0, body = 0;
  uint32_t id = 0;
};

void Usage() {
  std::fprintf(stderr,
               "Usage:\n"
               "  xd_audio_tool list <disc>\n"
               "  xd_audio_tool info <disc> <archive.fsys>\n"
               "  xd_audio_tool decode <disc> <archive.fsys> [--pair N | --header-id ID [--body-id ID]] -o <out.wav>\n"
               "  xd_audio_tool preview <header.bin> <body.bin> -o <out.wav>\n"
               "  xd_audio_tool encode <sound.wav|.ogg> <disc> <archive.fsys> --mod <mod folder> [--pair N | --header-id ID [--body-id ID]]\n"
               "                       [--loop [--loop-start S] [--loop-end E] | --no-loop] [--trim-intro] [--max-blocks N]\n"
               "  xd_audio_tool sfx-list <disc> [--bank menu|battle|atmos]\n"
               "  xd_audio_tool sfx-decode <disc> (--fx N | --sample N) [--bank B] -o <out.wav>\n"
               "  xd_audio_tool sfx-try <sound.wav|ogg> <disc> (--fx N | --sample N) [--bank B] [--volume X] [--loop | --no-loop] -o <heard.wav>\n"
               "  Sound files: WAV (16/24/32-bit, float) or OGG Vorbis. Loop points are sample numbers of YOUR file (a WAV's \"smpl\" loop and an OGG's\n"
               "  LOOPSTART / LOOPLENGTH tags are used when you give none). Nothing of the game is written, only your sound in the game's format.\n");
}

bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}
bool WriteFileBytes(const fs::path& p, const std::vector<uint8_t>& data) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

struct Archive {
  uint8_t* data = nullptr;
  size_t size = 0;
  FsysArchive fsys{};
  ~Archive() { PortDisc_Free(data); }
  bool Open(const std::string& path) {
    data = PortDisc_ReadOriginalFile(path.c_str(), &size);
    return data != nullptr && Fsys_Open(data, size, &fsys);
  }
  bool Entry(uint32_t i, std::vector<uint8_t>& out, uint32_t* type = nullptr) const {
    FsysEntry e{};
    if (!Fsys_Entry(&fsys, i, &e)) return false;
    if (type != nullptr) *type = e.type;
    uint32_t n = 0;
    uint8_t* p = Fsys_Extract(&fsys, i, &n);
    if (p == nullptr) return false;
    out.assign(p, p + n);
    std::free(p);
    return true;
  }
  std::vector<Pair> Pairs() const {
    std::vector<Pair> out;
    std::vector<FsysEntry> e(fsys.count);
    for (uint32_t i = 0; i < fsys.count; i++) Fsys_Entry(&fsys, i, &e[i]);
    for (uint32_t i = 0; i < fsys.count; i++) {
      if (e[i].type != 20) continue;
      bool found = false;
      if (i + 1 < fsys.count && e[i + 1].type == 21) {
        out.push_back({i, i + 1, e[i].id});
        continue;
      }
      for (uint32_t j = 0; j < fsys.count && !found; j++) {
        if (e[j].type == 21 && e[j].id + 1 == e[i].id) {
          out.push_back({i, j, e[i].id});
          found = true;
        }
      }
    }
    if (gHeaderId >= 0 && gBodyId >= 0) {
      for (Pair& p : out) {
        if (p.id != static_cast<uint32_t>(gHeaderId)) continue;
        for (uint32_t j = 0; j < fsys.count; j++) {
          if (e[j].type == 21 && e[j].id == static_cast<uint32_t>(gBodyId)) p.body = j;
        }
      }
    }
    return out;
  }
};

bool OpenDisc(const std::string& disc) {
  char error[512];
  if (!PortDisc_Open(disc.c_str(), error, sizeof error)) {
    std::fprintf(stderr, "Could not use the disc: %s\n", error);
    return false;
  }
  return true;
}

std::string LoopText(const ea::StreamInfo& info) {
  if (!info.loop) return "plays once";
  char buf[160];
  std::snprintf(buf, sizeof buf, "loops from %.2f s to %.2f s", static_cast<double>(info.loopStartSample) / info.rate, static_cast<double>(info.loopEndSample) / info.rate);
  return buf;
}

std::string Describe(const Archive& a, const Pair& p, bool& ok) {
  std::vector<uint8_t> header;
  ea::StreamInfo info;
  ea::AdpcmChannel ch[2];
  std::string error;
  ok = a.Entry(p.header, header) && ea::ParseStreamHeader(header, info, ch, error);
  if (!ok) return "not a sound stream";
  char buf[256];
  std::snprintf(buf, sizeof buf, "entries #%u (header, id %u) and #%u (sound): %.2f s at %u Hz, %u channels, %u blocks, %s", p.header, p.id, p.body,
                static_cast<double>(info.samples) / info.rate, info.rate, info.channels, info.blocks, LoopText(info).c_str());
  return buf;
}

int List(const std::string& disc) {
  if (!OpenDisc(disc)) return 3;
  std::vector<std::string> files;
  PortDisc_ListFiles([](const char* path, uint32_t, void* user) {
    const size_t n = std::strlen(path);
    if (n > 5 && std::strcmp(path + n - 5, ".fsys") == 0) static_cast<std::vector<std::string>*>(user)->emplace_back(path);
  }, &files);
  std::sort(files.begin(), files.end());
  int archives = 0;
  for (const std::string& f : files) {
    if (f.find("stm_") == std::string::npos) continue;
    Archive a;
    if (!a.Open(f)) continue;
    const std::vector<Pair> pairs = a.Pairs();
    if (pairs.empty()) continue;
    archives++;
    if (pairs.size() > 1) {
      std::printf("%-42s %zu sound streams (use \"info\" for the list)\n", f.c_str(), pairs.size());
    } else {
      bool ok;
      std::printf("%-42s %s\n", f.c_str(), Describe(a, pairs[0], ok).c_str());
    }
  }
  std::printf("%d archives with sound streams.\n", archives);
  PortDisc_Close();
  return 0;
}

int Info(const std::string& disc, const std::string& archive) {
  if (!OpenDisc(disc)) return 3;
  Archive a;
  if (!a.Open(archive)) {
    std::fprintf(stderr, "%s is not an archive on this disc\n", archive.c_str());
    return 3;
  }
  const std::vector<Pair> pairs = a.Pairs();
  for (size_t i = 0; i < pairs.size(); i++) {
    bool ok;
    std::printf("pair %zu: %s\n", i, Describe(a, pairs[i], ok).c_str());
  }
  if (pairs.empty()) std::printf("No sound streams in this archive.\n");
  PortDisc_Close();
  return 0;
}

bool SaveWav(const ea::Pcm& pcm, const ea::LoopPoints& loop, const std::string& out) {
  return WriteFileBytes(out, ea::WriteWav(pcm, &loop));
}

size_t PairIndex(const std::vector<Pair>& pairs, size_t pair) {
  if (gHeaderId < 0) return pair;
  for (size_t i = 0; i < pairs.size(); i++) {
    if (pairs[i].id == static_cast<uint32_t>(gHeaderId)) return i;
  }
  return pairs.size();
}

int Decode(const std::string& disc, const std::string& archive, size_t pair, const std::string& out) {
  if (!OpenDisc(disc)) return 3;
  Archive a;
  if (!a.Open(archive)) {
    std::fprintf(stderr, "%s is not an archive on this disc\n", archive.c_str());
    return 3;
  }
  const std::vector<Pair> pairs = a.Pairs();
  pair = PairIndex(pairs, pair);
  if (pair >= pairs.size()) {
    std::fprintf(stderr, "This archive has %zu sound streams; --pair %zu does not exist\n", pairs.size(), pair);
    return 3;
  }
  std::vector<uint8_t> header, body;
  ea::Pcm pcm;
  ea::LoopPoints loop;
  ea::StreamInfo info;
  std::string error;
  if (!a.Entry(pairs[pair].header, header) || !a.Entry(pairs[pair].body, body) || !ea::DecodeStream(header, body, pcm, loop, info, error)) {
    std::fprintf(stderr, "Could not read the stream: %s\n", error.c_str());
    return 3;
  }
  if (!SaveWav(pcm, loop, out)) {
    std::fprintf(stderr, "Could not write %s\n", out.c_str());
    return 3;
  }
  std::printf("Wrote %s: %.2f seconds, %d Hz, %s. (This is the game's own sound: keep the file to yourself.)\n", out.c_str(), static_cast<double>(pcm.Frames()) / pcm.rate, pcm.rate,
              LoopText(info).c_str());
  PortDisc_Close();
  return 0;
}

int Preview(const std::string& hdr, const std::string& body, const std::string& out) {
  std::vector<uint8_t> h, b;
  if (!ReadFileBytes(hdr, h) || !ReadFileBytes(body, b)) {
    std::fprintf(stderr, "Could not read the two files\n");
    return 3;
  }
  ea::Pcm pcm;
  ea::LoopPoints loop;
  ea::StreamInfo info;
  std::string error;
  if (!ea::DecodeStream(h, b, pcm, loop, info, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 3;
  }
  if (!SaveWav(pcm, loop, out)) {
    std::fprintf(stderr, "Could not write %s\n", out.c_str());
    return 3;
  }
  std::printf("Wrote %s: %.2f seconds, %d Hz, %s.\n", out.c_str(), static_cast<double>(pcm.Frames()) / pcm.rate, pcm.rate, LoopText(info).c_str());
  return 0;
}

struct EncodeArgs {
  std::string sound, disc, archive, modFolder;
  size_t pair = 0;
  int loop = -1;
  long long loopStart = -1, loopEnd = -1;
  bool trim = false;
  long long maxBlocks = -1;
};

int Encode(EncodeArgs e) {
  std::vector<uint8_t> file;
  if (!ReadFileBytes(e.sound, file)) {
    std::fprintf(stderr, "Could not read %s\n", e.sound.c_str());
    return 3;
  }
  ea::Pcm pcm;
  ea::LoopPoints loop;
  std::string error;
  if (!ea::ReadSoundFile(file, pcm, loop, error)) {
    std::fprintf(stderr, "%s: %s\n", e.sound.c_str(), error.c_str());
    return 3;
  }
  if (!OpenDisc(e.disc)) return 3;
  Archive a;
  if (!a.Open(e.archive)) {
    std::fprintf(stderr, "%s is not an archive on this disc\n", e.archive.c_str());
    return 3;
  }
  const std::vector<Pair> pairs = a.Pairs();
  e.pair = PairIndex(pairs, e.pair);
  if (e.pair >= pairs.size()) {
    std::fprintf(stderr, "This archive has %zu sound streams; --pair %zu does not exist\n", pairs.size(), e.pair);
    return 3;
  }
  const Pair pair = pairs[e.pair];
  std::vector<uint8_t> original;
  ea::StreamInfo originalInfo;
  ea::AdpcmChannel ignore[2];
  if (!a.Entry(pair.header, original) || !ea::ParseStreamHeader(original, originalInfo, ignore, error)) {
    std::fprintf(stderr, "The original stream could not be read: %s\n", error.c_str());
    return 3;
  }
  ea::BuildOptions options;
  options.version = originalInfo.version;
  options.loop = e.loop >= 0 ? e.loop == 1 : (loop.found || originalInfo.loop);
  if (e.loop < 0 && !loop.found && originalInfo.loop) {
    std::printf("Note: the original loops and your file gives no loop points, so the whole sound loops. (Use --no-loop to play once, or --loop-start / --loop-end.)\n");
  }
  options.loopStart = e.loopStart >= 0 ? static_cast<uint64_t>(e.loopStart) : loop.found ? loop.start : 0;
  options.loopEnd = e.loopEnd >= 0 ? static_cast<uint64_t>(e.loopEnd) : loop.found ? loop.end : 0;
  options.trimIntro = e.trim;
  options.maxBlocks = e.maxBlocks >= 0 ? static_cast<size_t>(e.maxBlocks) : (e.archive.find("nakigoe") != std::string::npos ? 2 : 0);
  ea::BuildResult result;
  if (!ea::BuildStream(pcm, options, result, error)) {
    std::fprintf(stderr, "Could not make the stream: %s\n", error.c_str());
    return 1;
  }
  const fs::path folder = fs::path(e.modFolder) / "fsys" / e.archive;
  if (!WriteFileBytes(folder / ("#" + std::to_string(pair.header) + ".bin"), result.header) ||
      !WriteFileBytes(folder / ("#" + std::to_string(pair.body) + ".bin"), result.body)) {
    std::fprintf(stderr, "Could not write into %s\n", folder.string().c_str());
    return 3;
  }
  std::printf("Wrote %s\\#%u.bin and #%u.bin (replacing the original's entries #%u and #%u of %s).\n", folder.string().c_str(), pair.header, pair.body, pair.header, pair.body,
              e.archive.c_str());
  for (const std::string& n : result.notes) std::printf("  - %s\n", n.c_str());
  std::printf("  - the result %s.\n", LoopText(result.info).c_str());
  std::printf("Make sure the mod's mod.json exists (see docs/modding/getting-started.md), then check it with xd_mod_check.\n");
  PortDisc_Close();
  return 0;
}

struct SfxBank {
  const xd::sound::Bank* bank = nullptr;
  std::vector<uint8_t> proj, pool, sdir, samp;
  std::vector<ea::sfx::Effect> effects;
  std::vector<ea::sfx::Sample> samples;
};

bool LoadBank(const xd::sound::Bank& b, SfxBank& out, std::string& error) {
  out.bank = &b;
  Archive a;
  if (!a.Open(xd::sound::kBanksArchive)) {
    error = std::string(xd::sound::kBanksArchive) + " is not on this disc";
    return false;
  }
  const auto entry = [&](const char* name, std::vector<uint8_t>& into) {
    const int i = Fsys_Find(&a.fsys, name);
    return i >= 0 && a.Entry(static_cast<uint32_t>(i), into);
  };
  if (!entry(b.proj, out.proj) || !entry(b.pool, out.pool) || !entry(b.sdir, out.sdir)) {
    error = std::string("the sound tables of the bank \"") + b.name + "\" are not in " + xd::sound::kBanksArchive;
    return false;
  }
  size_t size = 0;
  uint8_t* data = PortDisc_ReadOriginalFile(b.samp, &size);
  if (data == nullptr) {
    error = std::string("the sample file ") + b.samp + " is not on this disc";
    return false;
  }
  out.samp.assign(data, data + size);
  PortDisc_Free(data);
  if (!ea::sfx::ParseSdir(out.sdir, out.samples, error)) return false;
  std::string ignore;
  ea::sfx::ParseEffects(out.proj, out.pool, out.effects, ignore);
  return true;
}

bool PickSample(std::vector<SfxBank>& banks, long long fx, long long sample, const std::string& bankName, size_t& bankIndex, uint16_t& sampleId, std::string& error) {
  if (fx >= 0) {
    for (size_t i = 0; i < banks.size(); i++) {
      for (const auto& e : banks[i].effects) {
        if (e.fx != fx) continue;
        if (e.samples.empty()) {
          error = "effect " + std::to_string(fx) + " does not start a sample itself (its macro calls other macros); use --sample";
          return false;
        }
        if (e.samples.size() > 1) {
          error = "effect " + std::to_string(fx) + " plays " + std::to_string(e.samples.size()) + " different samples; name one with --sample (sfx-list shows them)";
          return false;
        }
        bankIndex = i, sampleId = e.samples[0];
        return true;
      }
    }
    error = "no effect " + std::to_string(fx) + " on this disc (sfx-list shows them)";
    return false;
  }
  if (sample >= 0) {
    int hits = 0;
    for (size_t i = 0; i < banks.size(); i++) {
      if (!bankName.empty() && bankName != banks[i].bank->name) continue;
      for (const auto& s : banks[i].samples) {
        if (s.id == sample) bankIndex = i, sampleId = s.id, hits++;
      }
    }
    if (hits == 1) return true;
    error = hits == 0 ? "no sample " + std::to_string(sample) + " (sfx-list shows them)" : "sample " + std::to_string(sample) + " is in more than one bank; add --bank menu|battle|atmos";
    return false;
  }
  error = "say which effect: --fx N or --sample N";
  return false;
}

bool LoadBanks(const std::string& disc, std::vector<SfxBank>& banks, const std::string& only = "") {
  if (!OpenDisc(disc)) return false;
  for (const xd::sound::Bank& b : xd::sound::kBanks) {
    if (!only.empty() && only != b.name) continue;
    SfxBank sb;
    std::string error;
    if (!LoadBank(b, sb, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return false;
    }
    banks.push_back(std::move(sb));
  }
  return !banks.empty();
}

int SfxList(const std::string& disc, const std::string& only) {
  std::vector<SfxBank> banks;
  if (!LoadBanks(disc, banks, only)) return 3;
  for (const SfxBank& b : banks) {
    std::printf("Bank \"%s\" (%s): %zu samples, %zu effects\n", b.bank->name, b.bank->what, b.samples.size(), b.effects.size());
    for (const auto& e : b.effects) {
      std::printf("  effect %-4u macro %-5u volume %-3u voices %-3u  samples:", e.fx, e.macro, e.volume, e.maxVoices);
      if (e.samples.empty()) std::printf(" (none directly)");
      for (uint16_t id : e.samples) std::printf(" %u", id);
      std::printf("\n");
    }
    std::printf("  sample  seconds  rate   loop  bytes\n");
    for (const auto& s : b.samples) {
      std::printf("  %-7u %-8.2f %-6u %-5s %u%s\n", s.id, static_cast<double>(s.length) / std::max<uint32_t>(s.Rate(), 1), s.Rate(), s.loopLength > 0 ? "yes" : "no", s.Bytes(),
                  s.type != 0 ? "  (not DSP-ADPCM: cannot be replaced)" : "");
    }
  }
  PortDisc_Close();
  return 0;
}

int SfxDecode(const std::string& disc, long long fx, long long sample, const std::string& bankName, const std::string& out) {
  std::vector<SfxBank> banks;
  if (!LoadBanks(disc, banks)) return 3;
  size_t bi = 0;
  uint16_t id = 0;
  std::string error;
  if (!PickSample(banks, fx, sample, bankName, bi, id, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 3;
  }
  ea::Pcm pcm;
  ea::LoopPoints loop;
  if (!ea::sfx::DecodeSample(banks[bi].sdir, banks[bi].samp, id, pcm, loop, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 3;
  }
  if (!SaveWav(pcm, loop, out)) return 3;
  std::printf("Wrote %s: sample %u of the bank \"%s\", %.2f seconds, %d Hz%s. (The game's own sound: keep the file to yourself.)\n", out.c_str(), id, banks[bi].bank->name,
              static_cast<double>(pcm.Frames()) / pcm.rate, pcm.rate, loop.found ? ", loops" : "");
  PortDisc_Close();
  return 0;
}

struct SfxArgs {
  long long fx = -1, sample = -1;
  std::string bank;
  double volume = 1.0;
  int loop = -1;
};

int SfxTry(const std::string& soundFile, const std::string& disc, const SfxArgs& a, const std::string& out) {
  std::vector<uint8_t> file;
  ea::Pcm pcm;
  ea::LoopPoints loop;
  std::string error;
  if (!ReadFileBytes(soundFile, file) || !ea::ReadSoundFile(file, pcm, loop, error)) {
    std::fprintf(stderr, "%s: %s\n", soundFile.c_str(), error.empty() ? "could not be read" : error.c_str());
    return 3;
  }
  std::vector<SfxBank> banks;
  if (!LoadBanks(disc, banks)) return 3;
  size_t bi = 0;
  uint16_t id = 0;
  if (!PickSample(banks, a.fx, a.sample, a.bank, bi, id, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 3;
  }
  ea::sfx::ReplaceOptions options;
  options.volume = a.volume;
  options.loop = a.loop == 1 ? ea::sfx::ReplaceOptions::Loop::On : a.loop == 0 ? ea::sfx::ReplaceOptions::Loop::Off : ea::sfx::ReplaceOptions::Loop::Keep;
  ea::sfx::ReplaceResult result;
  if (!ea::sfx::ReplaceSample(banks[bi].sdir, banks[bi].samp, id, pcm, loop, options, result, error)) {
    std::fprintf(stderr, "Could not make the effect: %s\n", error.c_str());
    return 1;
  }
  ea::Pcm heard;
  ea::LoopPoints heardLoop;
  if (!ea::sfx::DecodeSample(banks[bi].sdir, banks[bi].samp, id, heard, heardLoop, error) || !SaveWav(heard, heardLoop, out)) {
    std::fprintf(stderr, "Could not write %s\n", out.c_str());
    return 3;
  }
  std::printf("Wrote %s: what the game will play for sample %u of the bank \"%s\".\n", out.c_str(), id, banks[bi].bank->name);
  for (const std::string& n : result.notes) std::printf("  - %s\n", n.c_str());
  PortDisc_Close();
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc < 2) {
    Usage();
    return 3;
  }
  const std::string cmd = argv[1];
  std::vector<std::string> pos;
  EncodeArgs e;
  std::string out;
  SfxArgs sfx;
  bool pairGiven = false;
  for (int i = 2; i < argc; i++) {
    const std::string a = argv[i];
    const auto num = [&](long long& into) {
      if (i + 1 < argc) into = std::strtoll(argv[++i], nullptr, 0);
    };
    if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "--mod" && i + 1 < argc) e.modFolder = argv[++i];
    else if (a == "--pair" && i + 1 < argc) e.pair = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 0)), pairGiven = true;
    else if (a == "--header-id" && i + 1 < argc) gHeaderId = std::strtoll(argv[++i], nullptr, 0);
    else if (a == "--body-id" && i + 1 < argc) gBodyId = std::strtoll(argv[++i], nullptr, 0);
    else if (a == "--fx" && i + 1 < argc) sfx.fx = std::strtoll(argv[++i], nullptr, 0);
    else if (a == "--sample" && i + 1 < argc) sfx.sample = std::strtoll(argv[++i], nullptr, 0);
    else if (a == "--bank" && i + 1 < argc) sfx.bank = argv[++i];
    else if (a == "--volume" && i + 1 < argc) sfx.volume = std::strtod(argv[++i], nullptr);
    else if (a == "--loop") e.loop = 1, sfx.loop = 1;
    else if (a == "--no-loop") e.loop = 0, sfx.loop = 0;
    else if (a == "--loop-start") num(e.loopStart), e.loop = e.loop < 0 ? 1 : e.loop;
    else if (a == "--loop-end") num(e.loopEnd), e.loop = e.loop < 0 ? 1 : e.loop;
    else if (a == "--trim-intro") e.trim = true;
    else if (a == "--max-blocks") num(e.maxBlocks);
    else if (a == "--help" || a == "-h") {
      Usage();
      return 0;
    } else if (!a.empty() && a[0] == '-' && a.size() > 1) {
      Usage();
      return 3;
    } else {
      pos.push_back(a);
    }
  }
  (void)pairGiven;
  if (cmd == "list" && pos.size() == 1) return List(pos[0]);
  if (cmd == "info" && pos.size() == 2) return Info(pos[0], pos[1]);
  if (cmd == "decode" && pos.size() == 2 && !out.empty()) return Decode(pos[0], pos[1], e.pair, out);
  if (cmd == "sfx-list" && pos.size() == 1) return SfxList(pos[0], sfx.bank);
  if (cmd == "sfx-decode" && pos.size() == 1 && !out.empty()) return SfxDecode(pos[0], sfx.fx, sfx.sample, sfx.bank, out);
  if (cmd == "sfx-try" && pos.size() == 2 && !out.empty()) return SfxTry(pos[0], pos[1], sfx, out);
  if (cmd == "preview" && pos.size() == 2 && !out.empty()) return Preview(pos[0], pos[1], out);
  if (cmd == "encode" && pos.size() == 3 && !e.modFolder.empty()) {
    e.sound = pos[0], e.disc = pos[1], e.archive = pos[2];
    return Encode(e);
  }
  Usage();
  return 3;
}
