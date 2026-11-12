#include "../src/xd_user_folder.h"

#include <gc_memcard.h>
#include <xd_save.h>
#include <xd_save_convert.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#endif

using namespace xd::save;
namespace mc = engine::memcard;
namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool InsideDolphin(const fs::path& p) {
  const std::string s = Lower(fs::weakly_canonical(p).string());
  return s.find("dolphin emulator") != std::string::npos || s.find("\\dolphin\\") != std::string::npos ||
         s.find("/dolphin/") != std::string::npos;
}

bool WriteNewFile(const fs::path& path, const std::vector<uint8_t>& bytes, std::string& error) {
  if (fs::exists(path)) {
    error = path.string() + " already exists (this tool only writes new files)";
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "can't create " + path.string();
    return false;
  }
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (!out) {
    error = "writing " + path.string() + " failed";
    return false;
  }
  return true;
}

std::string Stamp() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
  return buf;
}

void Add(std::string& s, const char* what, uint64_t v) {
  s += what;
  s += '=';
  s += std::to_string(v);
  s += '\n';
}

void AddPokemon(std::string& s, const char* where, const Pokemon& p) {
  if (p.IsEmpty()) {
    return;
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "%s: sp %u item %u hp %u fr %u met %u/%u ball %u otg %u lv %u lus %u rus %u mk %u st %u fl %u exp %u "
                "ot %08X pid %08X rib %08X sh %u '%s' '%s' '%s'",
                where, p.species, p.heldItem, p.hp, p.friendship, p.metLocation, p.metLevel, p.ball, p.otGender,
                p.level, p.contestLuster, p.pokerus, p.marks, p.status, p.flags, p.exp, p.otId, p.pid, p.ribbons,
                p.shadowId, ToUtf8(p.otName).c_str(), ToUtf8(p.nickname).c_str(), ToUtf8(p.nicknameOrg).c_str());
  s += buf;
  for (const Move& m : p.moves) s += " m" + std::to_string(m.id) + "/" + std::to_string(m.pp) + "/" + std::to_string(m.ppUps);
  for (int i = 0; i < 6; i++) {
    s += " " + std::to_string(p.stats[i]) + "/" + std::to_string(p.evs[i]) + "/" + std::to_string(p.ivs[i]);
  }
  for (uint8_t c : p.contest) s += " c" + std::to_string(c);
  s += '\n';
}

void AddPocket(std::string& s, const char* name, const std::vector<ItemSlot>& items) {
  s += name;
  for (const ItemSlot& it : items) s += " " + std::to_string(it.item) + "x" + std::to_string(it.count);
  s += '\n';
}

std::string Contents(const Save& v) {
  std::string s;
  Add(s, "saveCount", static_cast<uint32_t>(v.saveCount));
  Add(s, "memcardId", v.memcardId);
  Add(s, "timesSaved", v.gameSaveCount);
  Add(s, "floor", v.floorId);
  char t[64];
  std::snprintf(t, sizeof(t), "playTime=%.6f\n", v.playTimeSeconds);
  s += t;
  Add(s, "noVibration", v.optionNoVibration);
  Add(s, "audio", v.optionAudio);
  Add(s, "sysHeadChecksum", v.sysHeadChecksum);
  s += "trainer=" + ToUtf8(v.trainerName) + "\n";
  Add(s, "trainerId", v.trainerOtId);
  Add(s, "gender", v.trainerGender);
  Add(s, "money", v.money);
  Add(s, "coupons", v.coupons);
  Add(s, "couponsTotal", v.couponsTotal);
  for (int i = 0; i < kPartySize; i++) AddPokemon(s, ("party" + std::to_string(i)).c_str(), v.party[i]);
  AddPocket(s, "items", v.items);
  AddPocket(s, "key", v.keyItems);
  AddPocket(s, "balls", v.balls);
  AddPocket(s, "tmhm", v.tmHm);
  AddPocket(s, "berries", v.berries);
  AddPocket(s, "colognes", v.colognes);
  AddPocket(s, "cds", v.battleCds);
  AddPocket(s, "pc", v.pcItems);
  for (int b = 0; b < kBoxCount; b++) {
    s += "box" + std::to_string(b) + "=" + ToUtf8(v.boxes[b].name) + "\n";
    for (int j = 0; j < kBoxSlots; j++) {
      AddPokemon(s, ("box" + std::to_string(b) + "." + std::to_string(j)).c_str(), v.boxes[b].slots[j]);
    }
  }
  AddPokemon(s, "daycare", v.dayCare);
  for (size_t c = 0; c < v.chambers.size(); c++) {
    for (size_t j = 0; j < v.chambers[c].size(); j++) {
      AddPokemon(s, ("chamber" + std::to_string(c) + "." + std::to_string(j)).c_str(), v.chambers[c][j]);
    }
  }
  for (int i = 0; i < kShadowEntries; i++) {
    const ShadowEntry& e = v.shadows[i];
    char buf[96];
    std::snprintf(buf, sizeof(buf), "shadow%d: st %u sp %u pid %08X gauge %d\n", i, e.status, e.species, e.pid,
                  e.heartGauge);
    s += buf;
  }
  if (const uint8_t* f = v.SectionData(23)) {
    uint32_t sum = 0, set = 0;
    for (size_t i = 0; i < 0x6B0; i += 4) {
      const uint32_t w = Read32(f + i, v.order);
      sum = sum * 31 + w;
      for (uint32_t b = w; b; b &= b - 1) set++;
    }
    Add(s, "flagsHash", sum);
    Add(s, "flagsSet", set);
  }
  return s;
}

int FlagsSet(const Save& v) {
  int n = 0;
  if (const uint8_t* f = v.SectionData(23)) {
    for (size_t i = 0; i < 0x6B0; i += 4) {
      for (uint32_t b = Read32(f + i, v.order); b; b &= b - 1) n++;
    }
  }
  return n;
}

void PrintSummary(const char* label, const Save& v) {
  const int secs = static_cast<int>(v.playTimeSeconds);
  int party = 0, boxed = 0;
  for (const Pokemon& p : v.party) party += !p.IsEmpty();
  for (const Box& b : v.boxes)
    for (const Pokemon& p : b.slots) boxed += !p.IsEmpty();
  std::printf("  %-10s %s, slot %d (save counter %d): %s, P%u, play time %d:%02d:%02d, %d in party, %d in boxes, "
              "%d story flags set\n",
              label, ByteOrderName(v.order), v.slot, v.saveCount, ToUtf8(v.trainerName).c_str(), v.money, secs / 3600,
              secs / 60 % 60, secs % 60, party, boxed, FlagsSet(v));
}

bool ReadBoth(const std::vector<uint8_t>& data, Save& out, std::string& error) {
  ReadReport report;
  return ReadSave(data, out, report, error);
}

fs::path PortCardFolder() { return xd::CardRoot() / "USA" / "Card A"; }

}

int main(int argc, char** argv) {
  std::string input, output, to;
  bool install = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--to" && i + 1 < argc) {
      to = Lower(argv[++i]);
    } else if (a == "-o" && i + 1 < argc) {
      output = argv[++i];
    } else if (a == "--install") {
      install = true;
    } else if (input.empty() && a[0] != '-') {
      input = a;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
      return 2;
    }
  }
  if (input.empty() || (to != "pc" && to != "gamecube")) {
    std::fprintf(stderr, "Usage: %s <save.gci | card.raw> --to pc|gamecube [-o <out.gci>] [--install]\n", argv[0]);
    return 2;
  }
  const ByteOrder target = to == "pc" ? ByteOrder::PC : ByteOrder::GameCube;

  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(input, bytes)) {
    std::fprintf(stderr, "can't read %s\n", input.c_str());
    return 1;
  }
  std::vector<mc::CardFile> files;
  mc::CardImageInfo info;
  std::string error;
  if (!mc::LoadCardFiles(bytes, files, info, error)) {
    std::fprintf(stderr, "%s: %s\n", input.c_str(), error.c_str());
    return 1;
  }
  const mc::CardFile* found = nullptr;
  for (const mc::CardFile& f : files) {
    if (f.entry.IsUsed() && f.entry.gameCode == "GXXE" && f.entry.fileName == "PokemonXD") found = &f;
  }
  if (found == nullptr || found->data.size() < kFileSize) {
    std::fprintf(stderr, "%s holds no Pokemon XD (USA) save\n", input.c_str());
    return 1;
  }
  std::vector<uint8_t> original(found->data.begin(), found->data.begin() + kFileSize);

  std::printf("%s: Pokemon XD save\n", input.c_str());
  Save before;
  if (!ReadBoth(original, before, error)) {
    std::fprintf(stderr, "can't read the save: %s\n", error.c_str());
    return 1;
  }
  PrintSummary("before", before);

  std::vector<uint8_t> converted = original;
  ConvertReport report;
  if (!ConvertFile(converted, target, report, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  for (int s = 0; s < kSlotCount; s++) std::printf("  slot %d: %s\n", s, SlotResultName(report.slots[s]));
  for (const std::string& w : report.warnings) std::printf("  warning: %s\n", w.c_str());

  bool ok = true;
  Save after;
  if (!ReadBoth(converted, after, error)) {
    std::printf("  CHECK FAILED: the converted save doesn't read: %s\n", error.c_str());
    ok = false;
  } else {
    PrintSummary("after", after);
    if (after.order != target) {
      std::printf("  CHECK FAILED: the save that would be used is still in %s\n", ByteOrderName(after.order));
      ok = false;
    }
    const std::string a = Contents(before), b = Contents(after);
    if (a != b) {
      ok = false;
      size_t i = 0;
      while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
      const size_t lineStart = a.rfind('\n', i) == std::string::npos ? 0 : a.rfind('\n', i) + 1;
      std::printf("  CHECK FAILED: contents differ, first at:\n    before: %s\n    after:  %s\n",
                  a.substr(lineStart, a.find('\n', i) - lineStart).c_str(),
                  b.substr(lineStart, b.find('\n', i) - lineStart).c_str());
    } else {
      std::printf("  check: same contents (trainer, money, play time, party, boxes, bag, day care, chamber, "
                  "Shadow list, flags)\n");
    }
  }
  std::vector<uint8_t> back = converted;
  ConvertReport backReport;
  ConvertFile(back, Other(target), backReport, error);
  bool allConverted = true;
  for (SlotResult r : report.slots) allConverted &= r != SlotResult::AlreadyTarget;
  if (allConverted) {
    if (back != original) {
      std::printf("  CHECK FAILED: converting back doesn't give the original bytes\n");
      ok = false;
    } else {
      std::printf("  check: converting back gives the original file byte for byte\n");
    }
  }
  if (!ok) {
    std::fprintf(stderr, "not written: the checks failed\n");
    return 1;
  }

  mc::CardFile outFile = *found;
  std::copy(converted.begin(), converted.end(), outFile.data.begin());
  const std::vector<uint8_t> gci = mc::MakeGci(outFile);

  fs::path outPath = output.empty() ? fs::path(input).replace_extension(std::string(".") + to + ".gci") : fs::path(output);
  if (output.empty() && InsideDolphin(outPath)) {
    outPath = fs::current_path() / fs::path(input).filename().replace_extension(std::string(".") + to + ".gci");
  }
  if (InsideDolphin(outPath)) {
    std::fprintf(stderr, "won't write into Dolphin's folders: %s\n", outPath.string().c_str());
    return 1;
  }
  if (fs::exists(outPath) && fs::equivalent(outPath, input)) {
    std::fprintf(stderr, "won't overwrite the input\n");
    return 1;
  }
  if (!WriteNewFile(outPath, gci, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::printf("written: %s\n", outPath.string().c_str());

  if (install) {
    const fs::path card = PortCardFolder();
    fs::create_directories(card);
    const fs::path dest = card / "01-GXXE-PokemonXD.gci";
    if (fs::exists(dest)) {
      const fs::path backups = card.parent_path().parent_path() / "save_backups";
      fs::create_directories(backups);
      const fs::path backup = backups / ("01-GXXE-PokemonXD." + Stamp() + ".gci");
      std::error_code ec;
      fs::copy_file(dest, backup, fs::copy_options::none, ec);
      if (ec) {
        std::fprintf(stderr, "can't back up %s: %s (not installed)\n", dest.string().c_str(), ec.message().c_str());
        return 1;
      }
      std::printf("backed up the port's save to %s\n", backup.string().c_str());
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(gci.data()), static_cast<std::streamsize>(gci.size()));
    out.close();
    if (!out) {
      std::fprintf(stderr, "writing %s failed\n", dest.string().c_str());
      return 1;
    }
    std::printf("installed: %s\n", dest.string().c_str());
  }
  return 0;
}
