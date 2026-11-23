#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "n3ds_assemble.h"

using namespace n3ds;

static std::vector<uint8_t> slurp(const char* p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error(std::string("cannot read ") + p);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
  try {
    if (argc < 7) { fprintf(stderr, "usage: n3ds_convert <dump> <group> <xd.pkx> <height_cm> <start> <out.pkx>\n"); return 2; }
    RomFs r = open_dump(argv[1]);
    Detected d = detect(r);
    Garc g = garc_at(r, d.models);
    Species sp = load_species(g, atoi(argv[2]));
    auto xd = slurp(argv[3]);
    ConvertInfo info;
    std::vector<uint8_t> out;
    std::vector<std::string> lines;
    int mode = atoi(argv[5]);
    if (mode == -1) out = convert_one(sp, xd, opts_xd_species(), &info, &lines);
    else if (mode == -2) out = convert_one(sp, xd, opts_gen4(atof(argv[4])), &info, &lines);
    else out = convert_ladder(sp, xd, atof(argv[4]), mode, &info);
    if (argc > 7) { std::ofstream lf(argv[7]); for (auto& l : lines) lf << l << '\n'; }
    std::ofstream o(argv[6], std::ios::binary);
    o.write((const char*)out.data(), (std::streamsize)out.size());
    printf("size %zu tris %d step %d scale %.6f\n", info.size, info.tris, info.step, info.scale);
    return 0;
  } catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
