#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "pbr_hsd_write.h"
#include "pbr_model_io.h"

using namespace pbr;

static std::vector<uint8_t> slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: pbr_hsd_write_test <folder> <name>...\n"); return 2; }
  int same = 0, bad = 0;
  for (int a = 2; a < argc; a++) {
    std::string base = std::string(argv[1]) + "/" + argv[a];
    try {
      Model m = model_load(base + ".model.txt");
      WriteReport rep;
      std::vector<uint8_t> got = hsd_write_archive(m, &rep);
      std::vector<uint8_t> want = slurp(base + ".arc.bin");
      if (got == want) {
        same++;
        printf("%s: identical (%zu bytes)\n", argv[a], got.size());
      } else {
        bad++;
        size_t i = 0;
        while (i < got.size() && i < want.size() && got[i] == want[i]) i++;
        printf("%s: DIFFERENT (C++ %zu bytes, Python %zu bytes, first difference at %zu)\n", argv[a], got.size(), want.size(), i);
      }
    } catch (const std::exception& e) {
      bad++;
      printf("%s: error: %s\n", argv[a], e.what());
    }
  }
  printf("%d identical, %d differ\n", same, bad);
  return bad ? 1 : 0;
}
