#include <xd_extract.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
  using namespace xd::extract;
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <disc> <source save> list | take <N> <target save> [--species-map a=b] [--move-map a=b] [--drop-moves] [--item-map a=b | --drop-item] [--party] [--out file | --in-place]\n", argv[0]);
    return 2;
  }
  const std::string cmd = argv[3];
  std::string error;
  xd::legality::DiscRules disc;
  if (!xd::legality::LoadRulesFromDisc(argv[1], disc, error)) {
    std::fprintf(stderr, "disc: %s\n", error.c_str());
    return 2;
  }
  const Names N{&disc.rules, disc.names.get()};
  Source src;
  if (!LoadSource(argv[2], src, error)) {
    std::fprintf(stderr, "source save: %s\n", error.c_str());
    return 2;
  }
  const std::vector<Place>& places = src.places;

  if (cmd == "list") {
    std::printf("Trainer %s: %zu Pokemon\n", xd::save::ToUtf8(src.save.trainerName).c_str(), places.size());
    for (size_t i = 0; i < places.size(); i++) {
      const Place& pl = places[i];
      const auto& p = pl.pokemon;
      std::printf("%zu: %s %s lv%d", i, pl.box < 0 ? "party" : ("box" + std::to_string(pl.box + 1)).c_str(), N.Species(p.species).c_str(), p.level);
      for (const auto& m : p.moves) {
        if (m.id != 0) std::printf(" | %s", N.Move(m.id).c_str());
      }
      if (p.heldItem != 0) std::printf(" | holds %s", N.Item(p.heldItem).c_str());
      const auto bad = NotPlain(N, p);
      if (bad.empty()) {
        std::printf("  => fits a plain save as it is\n");
      } else {
        std::printf("  => NOT plain:");
        for (const std::string& b : bad) std::printf(" [%s]", b.c_str());
        std::printf("\n");
      }
    }
    return 0;
  }
  if (cmd != "take" || argc < 6) {
    std::fprintf(stderr, "need: take <N from list> <target save>\n");
    return 2;
  }
  const size_t index = static_cast<size_t>(std::atoi(argv[4]));
  if (index >= places.size()) {
    std::fprintf(stderr, "there is no Pokemon %zu in the source save (list shows 0..%zu)\n", index, places.empty() ? 0 : places.size() - 1);
    return 2;
  }
  Maps maps;
  bool toParty = false, inPlace = false;
  std::string outFile;
  for (int i = 6; i < argc; i++) {
    const std::string a = argv[i];
    const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--species-map") maps.species = ParseMap(next());
    else if (a == "--move-map") maps.moves = ParseMap(next());
    else if (a == "--item-map") maps.items = ParseMap(next());
    else if (a == "--drop-moves") maps.dropMoves = true;
    else if (a == "--drop-item") maps.dropItem = true;
    else if (a == "--party") toParty = true;
    else if (a == "--out") outFile = next();
    else if (a == "--in-place") inPlace = true;
    else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 2;
    }
  }
  xd::trade::CardSave tgt;
  if (!xd::trade::LoadCardSave(argv[5], tgt, error)) {
    std::fprintf(stderr, "target save: %s\n", error.c_str());
    return 2;
  }
  xd::save::Save target;
  xd::save::ReadReport trep;
  if (!xd::save::ReadSave(tgt.data, target, trep, error)) {
    std::fprintf(stderr, "target save: %s\n", error.c_str());
    return 2;
  }

  const Place& pl = places[index];
  const TakeResult r = Take(disc, src, index, tgt, target, trep, maps, toParty);
  if (!r.done) {
    if (r.fatal && r.refuse.size() == 1 && r.refuse[0].rfind("target save: ", 0) == 0) {
      std::fprintf(stderr, "%s\n", r.refuse[0].c_str());
      return 2;
    }
    if (r.fatal || (r.refuse.size() == 1 && r.toWhere.empty() && (r.refuse[0].rfind("the plain game would not", 0) == 0 || r.refuse[0].rfind("the target", 0) == 0))) {
      std::printf("REFUSED: %s\n", r.refuse.empty() ? "" : r.refuse[0].c_str());
      return 1;
    }
    std::printf("REFUSED (%s %d, %s):\n", pl.box < 0 ? "party" : ("box" + std::to_string(pl.box + 1)).c_str(), pl.slot + 1, N.Species(pl.pokemon.species).c_str());
    for (const std::string& x : r.refuse) std::printf("  - %s\n", x.c_str());
    return 1;
  }
  const xd::save::Pokemon& p = pl.pokemon;
  std::printf("%s: %s lv%d from the source's %s %d goes to the target's %s.\n", outFile.empty() && !inPlace ? "DRY RUN (nothing is written)" : "TAKEN OUT", r.species.c_str(), p.level,
              pl.box < 0 ? "party" : ("box" + std::to_string(pl.box + 1)).c_str(), pl.slot + 1, r.toWhere.c_str());
  for (const std::string& n : r.notes) std::printf("  - %s\n", n.c_str());
  if (!r.metNote.empty()) std::printf("  - note: %s\n", r.metNote.c_str());
  if (outFile.empty() && !inPlace) return 0;
  if (!r.memoNote.empty()) std::printf("  - %s\n", r.memoNote.c_str());
  const std::vector<uint8_t>& file = r.file;
  if (inPlace) {
    std::string err;
    if (!xd::trade::WriteCardSave(tgt, file, err)) {
      std::printf("REFUSED: %s\n", err.c_str());
      return 1;
    }
    std::printf("Written into the target save (a backup of the old one is in save_backups next to it).\n");
  } else {
    std::vector<uint8_t> gci = tgt.gci;
    const size_t header = gci.size() - tgt.data.size();
    if (gci.size() < tgt.data.size()) {
      std::printf("REFUSED: the target file is smaller than its data.\n");
      return 1;
    }
    std::memcpy(gci.data() + header, file.data(), file.size());
    std::ofstream o(outFile, std::ios::binary);
    o.write(reinterpret_cast<const char*>(gci.data()), static_cast<std::streamsize>(gci.size()));
    if (!o) {
      std::printf("REFUSED: could not write %s\n", outFile.c_str());
      return 1;
    }
    std::printf("Written to %s (the target save itself is untouched).\n", outFile.c_str());
  }
  return 0;
}
