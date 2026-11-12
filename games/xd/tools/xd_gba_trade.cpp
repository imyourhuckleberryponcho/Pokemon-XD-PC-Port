#include <gc_memcard.h>
#include <gen3_save.h>
#include <xd_gba.h>
#include <xd_gba_disc.h>
#include <xd_save.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#endif

namespace fs = std::filesystem;
namespace mc = engine::memcard;
namespace g3 = engine::gen3;
using namespace xd::gba;

namespace {

xd::gba::DiscRules sRules;

std::string Named(uint32_t id, const std::string& name) { return name.empty() ? "#" + std::to_string(id) : name; }

std::string GbaText(const uint8_t* p, int len, int xdLanguage = 2) {
  return xd::save::ToUtf8(GbaToText(sRules, p, len, xdLanguage));
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes, std::string& error) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    error = "can't create " + path.string();
    return false;
  }
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!f) {
    error = "can't write " + path.string();
    return false;
  }
  return true;
}

g3::Game ParseGame(const std::string& s) {
  if (s == "rs" || s == "ruby" || s == "sapphire") {
    return g3::Game::RubySapphire;
  }
  if (s == "e" || s == "emerald") {
    return g3::Game::Emerald;
  }
  if (s == "frlg" || s == "firered" || s == "leafgreen") {
    return g3::Game::FireRedLeafGreen;
  }
  return g3::Game::Unknown;
}

struct XdInput {
  mc::CardFile card;
  bool isGci = false;
  xd::save::Save save;
  xd::save::ReadReport report;
};

bool LoadXd(const std::string& path, XdInput& in, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(path, bytes)) {
    error = "can't open " + path;
    return false;
  }
  if (mc::ReadGci(bytes, in.card, error)) {
    in.isGci = true;
  } else {
    std::vector<mc::CardFile> files;
    mc::CardImageInfo info;
    if (!mc::LoadCardFiles(bytes, files, info, error)) {
      return false;
    }
    bool found = false;
    for (const mc::CardFile& f : files) {
      if (f.entry.gameCode == "GXXE" && f.entry.fileName == "PokemonXD") {
        in.card = f;
        found = true;
      }
    }
    if (!found) {
      error = "no Pokemon XD (USA) save on this memory card";
      return false;
    }
  }
  if (in.card.entry.gameCode != "GXXE") {
    error = "not a Pokemon XD (USA) save (game code " + in.card.entry.gameCode + ")";
    return false;
  }
  if (in.card.data.size() < xd::save::kFileSize) {
    error = "the XD save file is too short";
    return false;
  }
  std::vector<uint8_t> data(in.card.data.begin(), in.card.data.begin() + xd::save::kFileSize);
  return xd::save::ReadSave(data, in.save, in.report, error);
}

bool LoadGba(const std::string& path, g3::Game game, g3::Save& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!mc::ReadWholeFile(path, bytes)) {
    error = "can't open " + path;
    return false;
  }
  std::vector<std::string> warnings;
  if (!g3::ReadSave(bytes, game, out, error, warnings)) {
    return false;
  }
  for (const std::string& w : warnings) {
    std::printf("  note: %s\n", w.c_str());
  }
  return true;
}

void PrintMonLine(const char* where, const XdMon& m, State st) {
  const auto& n = sRules.Names();
  if (st == State::Empty) {
    std::printf("  %-7s (empty)\n", where);
    return;
  }
  std::printf("  %-7s %-12s Lv%3u  \"%s\"  OT %s %05u%s  -> %s\n", where,
              Named(m.Species(), n.Species(m.Species())).c_str(), m.Level(), xd::save::ToUtf8(m.Text(0x4E)).c_str(),
              xd::save::ToUtf8(m.Text(0x38)).c_str(), m.OtId() & 0xFFFF, g3::IsShiny(m.Pid(), m.OtId()) ? "  shiny" : "",
              st == State::Tradeable ? "can be traded" : ("can't be traded: " + std::string(StateText(st))).c_str());
  if (m.Item() != 0) {
    std::printf("          holds %s\n", Named(m.Item(), n.Item(m.Item())).c_str());
  }
}

int Show(const std::vector<std::string>& paths, g3::Game game) {
  for (const std::string& path : paths) {
    std::printf("%s\n", path.c_str());
    std::string error;
    g3::Save gba;
    std::vector<std::string> warnings;
    std::vector<uint8_t> bytes;
    if (!mc::ReadWholeFile(path, bytes)) {
      std::printf("  can't open it\n\n");
      continue;
    }
    if (bytes.size() >= g3::kFlashSize && bytes.size() <= g3::kFlashSize + 0x100) {
      if (!g3::ReadSave(bytes, game, gba, error, warnings)) {
        std::printf("  %s\n\n", error.c_str());
        continue;
      }
      for (const std::string& w : warnings) {
        std::printf("  note: %s\n", w.c_str());
      }
      std::printf("  GBA save: %s, slot %c, saved %u times\n", g3::GameName(gba.game), gba.slot == 0 ? 'A' : 'B',
                  gba.counter);
      const uint32_t ot = gba.TrainerOtId();
      std::printf("  Trainer %s (%s), ID %05u / secret ID %05u\n", GbaText(gba.TrainerName(), 7).c_str(),
                  gba.TrainerGender() == 0 ? "male" : "female", ot & 0xFFFF, ot >> 16);
      const PartyView p = GbaParty(gba, sRules, xd::save::ByteOrder::GameCube);
      std::printf("  Party (%d):\n", gba.PartyCount());
      for (int i = 0; i < 6; i++) {
        char where[16];
        std::snprintf(where, sizeof(where), "#%d", i + 1);
        if (i < gba.PartyCount()) {
          PrintMonLine(where, p.mons[i], p.states[i]);
        }
      }
      int boxed = 0;
      for (int b = 0; b < g3::kBoxCount; b++) {
        for (int s = 0; s < g3::kBoxSlots; s++) {
          boxed += gba.BoxMon(b, s).IsEmpty() ? 0 : 1;
        }
      }
      std::printf("  PC boxes: %d Pokemon (the GameCube trades from the party only: move one into the party in "
                  "the GBA game first)\n\n",
                  boxed);
      continue;
    }
    XdInput xd;
    if (!LoadXd(path, xd, error)) {
      std::printf("  %s\n\n", error.c_str());
      continue;
    }
    std::printf("  XD save: trainer %s, saved %u times (%s)\n", xd::save::ToUtf8(xd.save.trainerName).c_str(),
                xd.save.gameSaveCount, xd::save::ByteOrderName(xd.save.order));
    const PartyView p = XdParty(xd.save, sRules);
    std::printf("  Party:\n");
    for (int i = 0; i < 6; i++) {
      char where[16];
      std::snprintf(where, sizeof(where), "#%d", i + 1);
      PrintMonLine(where, p.mons[i], p.states[i]);
    }
    std::printf("\n");
  }
  return 0;
}

std::string Stamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm);
  return buf;
}

fs::path TradedName(const fs::path& in, const fs::path& outDir) {
  const fs::path dir = outDir.empty() ? in.parent_path() : outDir;
  return dir / (in.stem().string() + ".traded" + in.extension().string());
}

int DoTrade(const std::string& xdPath, int xdSlot, const std::string& gbaPath, int gbaSlot, g3::Game game,
            const std::string& outDir) {
  std::string error;
  XdInput xd;
  if (!LoadXd(xdPath, xd, error)) {
    std::printf("XD save %s: %s\n", xdPath.c_str(), error.c_str());
    return 1;
  }
  if (!xd.isGci) {
    std::printf("The XD save is inside a whole memory-card image (.raw). Writing into card images isn't supported "
                "yet: export the save as a .gci (Dolphin: Tools > Memory Card Manager) and use that.\n");
    return 1;
  }
  g3::Save gba;
  if (!LoadGba(gbaPath, game, gba, error)) {
    std::printf("GBA save %s: %s\n", gbaPath.c_str(), error.c_str());
    return 1;
  }
  const std::vector<uint8_t> xdData(xd.card.data.begin(), xd.card.data.begin() + xd::save::kFileSize);
  TradeRequest req;
  req.xdSlot = xdSlot - 1;
  req.gbaSlot = gbaSlot - 1;
  const PartyView xp = XdParty(xd.save, sRules);
  const PartyView gp = GbaParty(gba, sRules, xd.save.order);
  TradeResult r = Trade(xdData, xd.save, xd.report, gba, req, sRules);
  if (!r.ok) {
    std::printf("No trade: %s.\nNothing was written.\n", r.error.c_str());
    return 1;
  }

  {
    xd::save::Save check;
    xd::save::ReadReport rep;
    std::string err;
    if (!xd::save::ReadSave(r.xdFile, check, rep, err) || check.slot == xd.save.slot ||
        check.saveCount <= xd.save.saveCount) {
      std::printf("Internal check failed on the new XD save (%s). Nothing was written.\n", err.c_str());
      return 1;
    }
    const PartyView np = XdParty(check, sRules);
    if (std::memcmp(np.mons[req.xdSlot].raw.data() + 0x00, gp.mons[req.gbaSlot].raw.data(), 6) != 0 ||
        np.mons[req.xdSlot].Pid() != gp.mons[req.gbaSlot].Pid()) {
      std::printf("Internal check failed: the new XD save doesn't hold the received Pokemon. Nothing was written.\n");
      return 1;
    }
    g3::Save gcheck;
    std::vector<std::string> warnings;
    if (!g3::ReadSave(r.gbaFile, gba.game, gcheck, err, warnings) || gcheck.counter != gba.counter + 1) {
      std::printf("Internal check failed on the new GBA save (%s). Nothing was written.\n", err.c_str());
      return 1;
    }
    const g3::PlainMon got = gcheck.PartyMon(req.gbaSlot);
    if (!got.checksumOk || got.Pid() != xp.mons[req.xdSlot].Pid() || got.Species() != xp.mons[req.xdSlot].Species()) {
      std::printf("Internal check failed: the new GBA save doesn't hold the sent Pokemon. Nothing was written.\n");
      return 1;
    }
  }

  const fs::path xdIn(xdPath), gbaIn(gbaPath);
  const fs::path backupDir = (outDir.empty() ? xdIn.parent_path() : fs::path(outDir)) / ("gba_trade_backup_" + Stamp());
  std::error_code ec;
  fs::create_directories(backupDir, ec);
  if (ec || !fs::copy_file(xdIn, backupDir / xdIn.filename(), ec) || !fs::copy_file(gbaIn, backupDir / gbaIn.filename(), ec)) {
    std::printf("Couldn't make the backup copies in %s (%s). Nothing was written.\n", backupDir.string().c_str(),
                ec.message().c_str());
    return 1;
  }
  mc::CardFile outCard = xd.card;
  std::memcpy(outCard.data.data(), r.xdFile.data(), xd::save::kFileSize);
  const fs::path xdOut = TradedName(xdIn, outDir), gbaOut = TradedName(gbaIn, outDir);
  if (!WriteFile(xdOut, mc::MakeGci(outCard), error) || !WriteFile(gbaOut, r.gbaFile, error)) {
    std::printf("%s\n", error.c_str());
    return 1;
  }
  const auto& n = sRules.Names();
  std::printf("Traded: %s (XD party #%d) <-> %s (GBA party #%d)\n",
              Named(xp.mons[req.xdSlot].Species(), n.Species(xp.mons[req.xdSlot].Species())).c_str(), xdSlot,
              Named(gp.mons[req.gbaSlot].Species(), n.Species(gp.mons[req.gbaSlot].Species())).c_str(), gbaSlot);
  for (const std::string& note : r.notes) {
    std::printf("  %s\n", note.c_str());
  }
  std::printf("New XD save:  %s\nNew GBA save: %s\nBackups of the originals: %s\n", xdOut.string().c_str(),
              gbaOut.string().c_str(), backupDir.string().c_str());
  std::printf("The original files were not changed. To use the new saves, put them in place of the originals "
              "(same file names) while the game/emulator is closed.\n");
  return 0;
}

void Usage() {
  std::printf("usage:\n"
              "  xd_gba_trade show  <disc> <save.sav | save.gci> [more] [--game rs|e|frlg]\n"
              "  xd_gba_trade trade <disc> <xd.gci> <xd slot 1-6> <gba.sav> <gba slot 1-6> [--game rs|e|frlg] "
              "[--out <folder>]\n");
}

}

int main(int argc, char** argv) {
  std::vector<std::string> args;
  g3::Game game = g3::Game::Unknown;
  std::string outDir;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--game" && i + 1 < argc) {
      game = ParseGame(argv[++i]);
      if (game == g3::Game::Unknown) {
        std::printf("--game: use rs, e or frlg\n");
        return 2;
      }
    } else if (a == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else {
      args.push_back(a);
    }
  }
  if (args.size() < 3) {
    Usage();
    return 2;
  }
  std::string error;
  if (!sRules.Load(args[1], error)) {
    std::printf("disc %s: %s\n", args[1].c_str(), error.c_str());
    return 1;
  }
  if (args[0] == "show") {
    return Show(std::vector<std::string>(args.begin() + 2, args.end()), game);
  }
  if (args[0] == "trade" && args.size() == 6) {
    const int xs = std::atoi(args[3].c_str()), gs = std::atoi(args[5].c_str());
    if (xs < 1 || xs > 6 || gs < 1 || gs > 6) {
      std::printf("party slots are 1 to 6\n");
      return 2;
    }
    return DoTrade(args[2], xs, args[4], gs, game, outDir);
  }
  if (args[0] == "statcheck") {
    XdInput xd;
    if (!LoadXd(args[2], xd, error)) {
      std::printf("%s\n", error.c_str());
      return 1;
    }
    const PartyView p = XdParty(xd.save, sRules);
    for (int i = 0; i < 6; i++) {
      XdMon t = p.mons[i];
      ResetStats(t, sRules);
      std::printf("#%d species %u level %u->%u exp %u:", i + 1, p.mons[i].Species(), p.mons[i].Level(), t.Level(),
                  p.mons[i].U32(0x20));
      for (int k = 0; k < 6; k++) {
        std::printf(" %u/%u", p.mons[i].U16(0x90 + k * 2), t.U16(0x90 + k * 2));
      }
      std::printf("  EVs");
      for (int k = 0; k < 6; k++) {
        std::printf(" %u", p.mons[i].U16(0x9C + k * 2));
      }
      std::printf("  IVs");
      for (int k = 0; k < 6; k++) {
        std::printf(" %u", p.mons[i].U8(0xA8 + k));
      }
      const uint16_t sid = p.mons[i].ShadowId();
      if (sid != 0 && sid < xd::save::kShadowEntries) {
        const auto& raw = xd.save.shadows[sid].raw;
        std::printf("\n   Shadow entry %u bytes:", sid);
        for (int k = 0; k < 0x48; k++) {
          std::printf("%s%02X", k % 16 == 0 ? "\n    " : " ", raw[k]);
        }
        XdMon u = p.mons[i];
        for (int k = 0; k < 6; k++) {
          u.SetU8(0xA8 + k, raw[0x0B + k]);
        }
        ResetStats(u, sRules);
        std::printf("\n   with the entry's IVs (0x0B..):");
        for (int k = 0; k < 6; k++) {
          std::printf(" %u", u.U16(0x90 + k * 2));
        }
      }
      std::printf("\n");
    }
    return 0;
  }
  if (args[0] == "dexcheck") {
    for (uint16_t s = 1; s < 415; s++) {
      if (sRules.SpeciesValid(s)) {
        std::printf("%3u %-12s numPokemon %3u numZukan %3u\n", s, sRules.Names().Species(s).c_str(),
                    sRules.SpeciesNumberField(s, 0), sRules.SpeciesNumberField(s, 1));
      }
    }
    return 0;
  }
  Usage();
  return 2;
}
