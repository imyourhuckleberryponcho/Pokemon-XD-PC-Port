#include <gc_memcard.h>
#include <xd_save.h>
#include <xd_save_names.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace xd::save;
namespace mc = engine::memcard;

namespace {

GameNames sNames;

std::string Named(uint32_t id, const std::string& name) {
  return name.empty() ? std::to_string(id) : std::to_string(id) + " " + name;
}
std::string SpeciesName(uint16_t id) { return Named(id, sNames.Species(id)); }
std::string ItemName(uint16_t id) { return Named(id, sNames.Item(id)); }
std::string MoveName(uint16_t id) { return Named(id, sNames.Move(id)); }
std::string NatureName(uint8_t id) { return Named(id, sNames.Nature(id)); }

std::string ShadowText(const Save& s, const Pokemon& p) {
  const ShadowEntry* e = s.ShadowOf(p);
  if (e == nullptr) {
    return "";
  }
  char buf[96];
  if (e->Purified()) {
    std::snprintf(buf, sizeof(buf), "  [was Shadow #%u, purified]", p.shadowId);
  } else {
    std::snprintf(buf, sizeof(buf), "  [SHADOW #%u, heart gauge %d]", p.shadowId, e->heartGauge);
  }
  return buf;
}

void PrintPokemonLine(const Save& s, const char* where, const Pokemon& p) {
  std::printf("  %-8s %-18s Lv%3u  \"%s\"  OT %s %05u%s%s%s\n", where, SpeciesName(p.species).c_str(), p.level,
              ToUtf8(p.nickname).c_str(), ToUtf8(p.otName).c_str(), p.OtTrainerId(), p.IsShiny() ? "  shiny" : "",
              p.IsEgg() ? "  egg" : "", ShadowText(s, p).c_str());
}

void PrintPokemonFull(const Save& s, const char* where, const Pokemon& p) {
  PrintPokemonLine(s, where, p);
  std::printf("           PID %08X  nature %s  ability %d  item %s  exp %u  friendship %u\n", p.pid,
              NatureName(p.Nature()).c_str(), p.SecondAbility() ? 2 : 1, ItemName(p.heldItem).c_str(), p.exp,
              p.friendship);
  std::printf("           OT %s (%s) ID %05u / secret ID %05u   met Lv%u at %u, ball %s\n", ToUtf8(p.otName).c_str(),
              p.otGender == 0 ? "male" : "female", p.OtTrainerId(), p.OtSecretId(), p.metLevel, p.metLocation,
              ItemName(p.ball).c_str());
  std::printf("           HP %u/%u  stats %u/%u/%u/%u/%u (Atk/Def/SpA/SpD/Spe)\n", p.hp, p.stats[0], p.stats[1],
              p.stats[2], p.stats[3], p.stats[4], p.stats[5]);
  std::printf("           IVs %u/%u/%u/%u/%u/%u  EVs %u/%u/%u/%u/%u/%u (HP/Atk/Def/SpA/SpD/Spe)\n", p.ivs[0], p.ivs[1],
              p.ivs[2], p.ivs[3], p.ivs[4], p.ivs[5], p.evs[0], p.evs[1], p.evs[2], p.evs[3], p.evs[4], p.evs[5]);
  for (const Move& m : p.moves) {
    if (m.id != 0) {
      std::printf("           move %-22s PP %u%s\n", MoveName(m.id).c_str(), m.pp,
                  m.ppUps != 0 ? (" (" + std::to_string(m.ppUps) + " PP Ups)").c_str() : "");
    }
  }
  if (p.pokerus != 0 || p.status != 0 || p.ribbons != 0) {
    std::printf("           pokerus %02X  status %u  ribbons %08X\n", p.pokerus, p.status, p.ribbons);
  }
}

void PrintPocket(const char* name, const std::vector<ItemSlot>& pocket) {
  std::string line;
  int n = 0;
  for (const ItemSlot& it : pocket) {
    if (it.item == 0) {
      continue;
    }
    line += (n++ ? ", " : "") + ItemName(it.item) + " x" + std::to_string(it.count);
  }
  if (n > 0) {
    std::printf("  %-12s %s\n", name, line.c_str());
  }
}

std::string PlayTime(double seconds) {
  const long long s = static_cast<long long>(seconds);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
  return buf;
}

}

int main(int argc, char** argv) {
  const char* savePath = nullptr;
  const char* discPath = nullptr;
  bool full = false, sections = false, shadows = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--full") == 0) {
      full = true;
    } else if (std::strcmp(argv[i], "--sections") == 0) {
      sections = true;
    } else if (std::strcmp(argv[i], "--shadows") == 0) {
      shadows = true;
    } else if (savePath == nullptr) {
      savePath = argv[i];
    } else if (discPath == nullptr) {
      discPath = argv[i];
    }
  }
  if (savePath == nullptr) {
    std::fprintf(stderr, "Usage: %s <save.gci | card.raw> [disc image] [--full] [--sections] [--shadows]\n", argv[0]);
    return 2;
  }

  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(savePath, bytes)) {
    std::fprintf(stderr, "FAILED: can't read %s\n", savePath);
    return 1;
  }
  std::vector<mc::CardFile> files;
  mc::CardImageInfo cardInfo;
  std::string error;
  if (!mc::LoadCardFiles(bytes, files, cardInfo, error)) {
    std::fprintf(stderr, "FAILED: %s\n", error.c_str());
    return 1;
  }
  for (const std::string& w : cardInfo.warnings) {
    std::printf("card: %s\n", w.c_str());
  }
  const mc::CardFile* file = nullptr;
  for (const mc::CardFile& f : files) {
    if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) {
      file = &f;
      break;
    }
  }
  if (file == nullptr) {
    std::fprintf(stderr, "FAILED: no Pokemon XD save in %s (%zu card files)\n", savePath, files.size());
    for (const mc::CardFile& f : files) {
      std::fprintf(stderr, "  %s-%s %s (%u blocks)\n", f.entry.gameCode.c_str(), f.entry.makerCode.c_str(),
                   f.entry.fileName.c_str(), f.entry.blockCount);
    }
    return 1;
  }
  std::printf("card file: %s-%s \"%s\", %u blocks\n", file->entry.gameCode.c_str(), file->entry.makerCode.c_str(),
              file->entry.fileName.c_str(), file->entry.blockCount);
  if (file->entry.gameCode != "GXXE") {
    std::printf("note: this is the %s version; the reader is written for the USA one (GXXE)\n",
                file->entry.gameCode.c_str());
  }

  if (discPath != nullptr) {
    std::string nameError;
    if (!sNames.Load(discPath, nameError)) {
      std::printf("names: not available (%s); printing numbers only\n", nameError.c_str());
    }
  }

  Save save;
  ReadReport report;
  const bool ok = ReadSave(file->data, save, report, error);
  for (int s = 0; s < kSlotCount; s++) {
    const SlotReport& r = report.slots[s];
    if (!r.written) {
      std::printf("slot %d: never written\n", s);
    } else if (r.saveCount <= 0) {
      std::printf("slot %d: empty (save counter 0)\n", s);
    } else {
      std::printf("slot %d: save counter %d, checksums %s%s%s\n", s, r.saveCount, r.checksumOk ? "ok" : "WRONG",
                  r.checksumOk ? (r.headerSumOk ? ", header check ok" : ", header check WRONG") : "",
                  r.checksumOk ? (std::string(", ") + ByteOrderName(r.order)).c_str() : "");
    }
  }
  std::printf("banner checksum: %s\n", report.bannerSumOk ? "ok" : "wrong");
  for (const std::string& w : report.warnings) {
    std::printf("warning: %s\n", w.c_str());
  }
  if (!ok) {
    std::printf("no save to read: %s\n", error.c_str());
    return 1;
  }
  std::printf("reading slot %d (%s)\n\n", save.slot, ByteOrderName(save.order));

  if (sections) {
    std::printf("Sections:\n");
    for (const Section& s : save.sections) {
      std::printf("  %2d %-28s offset 0x%05X size 0x%04X%s\n", s.id, s.name, s.offset, s.size,
                  s.size != s.expectedSize ? "  (unexpected size)" : "");
    }
    std::printf("\n");
  }

  std::printf("Trainer: %s (%s)  ID %05u  secret ID %05u\n", ToUtf8(save.trainerName).c_str(),
              save.trainerGender == 0 ? "male" : "female", save.TrainerId(), save.SecretId());
  std::printf("Money: P%u   Poke Coupons: %u (%u in all)\n", save.money, save.coupons, save.couponsTotal);
  std::printf("Play time: %s   saved %u times   continues on floor %u   card serial %016llX\n",
              PlayTime(save.playTimeSeconds).c_str(), save.gameSaveCount, save.floorId,
              static_cast<unsigned long long>(save.memcardId));

  std::printf("\nParty:\n");
  for (int i = 0; i < kPartySize; i++) {
    if (!save.party[i].IsEmpty()) {
      PrintPokemonFull(save, ("#" + std::to_string(i + 1)).c_str(), save.party[i]);
    }
  }

  int boxed = 0;
  for (int b = 0; b < kBoxCount; b++) {
    int n = 0;
    for (const Pokemon& p : save.boxes[b].slots) {
      n += p.IsEmpty() ? 0 : 1;
    }
    std::printf("\nBox %d \"%s\": %d Pokemon\n", b + 1, ToUtf8(save.boxes[b].name).c_str(), n);
    for (int j = 0; j < kBoxSlots; j++) {
      const Pokemon& p = save.boxes[b].slots[j];
      if (p.IsEmpty()) {
        continue;
      }
      boxed++;
      const std::string where = "slot " + std::to_string(j + 1);
      if (full) {
        PrintPokemonFull(save, where.c_str(), p);
      } else {
        PrintPokemonLine(save, where.c_str(), p);
      }
    }
  }

  std::printf("\nDay care:%s\n", save.dayCare.IsEmpty() ? " empty" : "");
  if (!save.dayCare.IsEmpty()) {
    PrintPokemonLine(save, "", save.dayCare);
  }
  std::printf("\nPurification Chamber:\n");
  int chamberCount = 0;
  for (size_t c = 0; c < save.chambers.size(); c++) {
    for (size_t j = 0; j < save.chambers[c].size(); j++) {
      const Pokemon& p = save.chambers[c][j];
      if (!p.IsEmpty()) {
        chamberCount++;
        PrintPokemonLine(save, ("ch" + std::to_string(c + 1) + "." + std::to_string(j + 1)).c_str(), p);
      }
    }
  }
  if (chamberCount == 0) {
    std::printf("  empty\n");
  }

  int seen = 0, snagged = 0, purified = 0;
  for (int i = 1; i < kShadowEntries; i++) {
    const ShadowEntry& e = save.shadows[i];
    if (e.species == 0) {
      continue;
    }
    seen++;
    snagged += e.Snagged() ? 1 : 0;
    purified += e.Purified() ? 1 : 0;
  }
  std::printf("\nShadow Pokemon list: %d entries hold a Pokemon, %d snagged, %d purified\n", seen, snagged, purified);
  if (shadows) {
    static const char* const kStatus[] = {"not met", "status 1", "status 2", "snagged", "purified", "status 5",
                                          "status 6", "status 7"};
    for (int i = 1; i < kShadowEntries; i++) {
      const ShadowEntry& e = save.shadows[i];
      if (e.species != 0) {
        std::printf("  #%-3d %-18s %-9s heart gauge %d\n", i, SpeciesName(e.species).c_str(), kStatus[e.status & 7],
                    e.heartGauge);
      }
    }
  }

  std::printf("\nBag:\n");
  PrintPocket("items", save.items);
  PrintPocket("key items", save.keyItems);
  PrintPocket("balls", save.balls);
  PrintPocket("TMs/HMs", save.tmHm);
  PrintPocket("berries", save.berries);
  PrintPocket("colognes", save.colognes);
  PrintPocket("battle CDs", save.battleCds);
  PrintPocket("PC items", save.pcItems);

  std::printf("\n%d Pokemon in the party, %d in boxes\n",
              [&] {
                int n = 0;
                for (const Pokemon& p : save.party) {
                  n += p.IsEmpty() ? 0 : 1;
                }
                return n;
              }(),
              boxed);
  return 0;
}
