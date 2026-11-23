#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "n3ds_dump.h"
#include "species_dump.h"
#include "pose_dump.h"
#include "decimate_dump.h"
#include "warp_dump.h"

using namespace n3ds;

static uint64_t fnv(const Bytes& b) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
  return h;
}

int main(int argc, char** argv) {
  try {
    if (argc < 3) { fprintf(stderr, "usage: n3ds_tool list|detect|garc|lz11 <dump> ...\n"); return 2; }
    std::string cmd = argv[1];
    RomFs r = open_dump(argv[2]);
    if (cmd == "list") {
      std::string pre = argc > 3 ? argv[3] : "";
      printf("%s %zu files\n", r.product.c_str(), r.files.size());
      for (auto& kv : r.files)
        if (kv.first.compare(0, pre.size(), pre) == 0) printf("%10" PRIu64 " %s\n", kv.second.second, kv.first.c_str());
    } else if (cmd == "detect") {
      Detected d = detect(r);
      printf("models %s\npersonal %s\nlevelup %s\ntext %s\n", d.models.c_str(), d.personal.c_str(), d.levelup.c_str(), d.text.c_str());
    } else if (cmd == "pose") {
      n3ds_test::dump_pose(r, atoi(argv[3]), atoi(argv[4]), atof(argv[5]), argv[6]);
    } else if (cmd == "decimate") {
      n3ds_test::dump_decimate(r, atoi(argv[3]), argc > 4 ? atof(argv[4]) : 0.5);
    } else if (cmd == "warp") {
      n3ds_test::dump_warp(r, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    } else if (cmd == "species") {
      n3ds_test::dump_species(r, atoi(argv[3]));
    } else if (cmd == "garc" || cmd == "lz11") {
      Garc g = garc_at(r, argv[3]);
      size_t n = g.count();
      if (cmd == "lz11" && argc > 4) n = std::min<size_t>(n, (size_t)atoi(argv[4]));
      printf("entries %zu\n", g.count());
      for (size_t i = 0; i < n; i++) {
        printf("%zu", i);
        for (size_t s = 0; s < 32; s++) {
          if (g.entries[i][s].second < 0) continue;
          Bytes raw = g.read(i, s);
          Bytes d = lz11_maybe(raw);
          printf(" %zu:%zu:%016" PRIx64, s, d.size(), fnv(d));
          if (cmd == "lz11" && raw.size() && raw[0] == 0x11) {
            Bytes p = lz11(raw.data(), raw.size(), 64);
            printf(":p%016" PRIx64, fnv(p));
          }
        }
        printf("\n");
      }
    } else {
      fprintf(stderr, "unknown command\n");
      return 2;
    }
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
