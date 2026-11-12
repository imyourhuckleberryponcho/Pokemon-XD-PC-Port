#include "../src/macho_symbols.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: macho_symbols_test file|image <mach-o file> <name>...\n");
    return 2;
  }
  const std::string mode = argv[1];
  std::ifstream in(argv[2], std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  xd_macho::Table table;
  std::vector<uint8_t> image;
  if (mode == "file") {
    if (!xd_macho::table_in_file(file.data(), file.size(), table)) {
      std::fprintf(stderr, "no symbol table found in the file\n");
      return 2;
    }
    for (size_t cut : {size_t(0), size_t(16), size_t(40), file.size() / 2, file.size() - 1}) {
      xd_macho::Table t2;
      if (cut < file.size() && xd_macho::table_in_file(file.data(), cut, t2)) {
        const uint8_t* end = file.data() + cut;
        if (reinterpret_cast<const uint8_t*>(t2.symbols + t2.count) > end ||
            reinterpret_cast<const uint8_t*>(t2.strings) + t2.stringsSize > end) {
          std::fprintf(stderr, "FAILED: a file cut to %zu bytes gave a table past its end\n", cut);
          return 1;
        }
      }
    }
  } else {
    uint64_t low = ~0ull, high = 0;
    std::vector<xd_macho::Segment64> segments;
    if (!xd_macho::for_each_command(reinterpret_cast<const xd_macho::Header64*>(file.data()), file.size(),
                                    [&](const xd_macho::LoadCommand& c, const uint8_t* p) {
                                      if (c.cmd == xd_macho::kSegment64) {
                                        xd_macho::Segment64 s;
                                        std::memcpy(&s, p, sizeof s);
                                        if (s.filesize != 0) {
                                          segments.push_back(s);
                                          if (s.vmaddr < low) low = s.vmaddr;
                                          if (s.vmaddr + s.vmsize > high) high = s.vmaddr + s.vmsize;
                                        }
                                      }
                                      return true;
                                    }) || segments.empty() || high - low > (64u << 20)) {
      std::fprintf(stderr, "not a small Mach-O program\n");
      return 2;
    }
    image.assign(static_cast<size_t>(high - low), 0);
    for (const auto& s : segments) {
      if (s.fileoff + s.filesize > file.size()) return 2;
      std::memcpy(image.data() + (s.vmaddr - low), file.data() + s.fileoff, static_cast<size_t>(s.filesize));
    }
    const intptr_t slide = static_cast<intptr_t>(reinterpret_cast<uintptr_t>(image.data())) - static_cast<intptr_t>(low);
    if (!xd_macho::table_in_memory(image.data(), slide, table)) {
      std::fprintf(stderr, "no symbol table found in the laid-out program\n");
      return 2;
    }
    std::printf("# laid out at %p (the file says 0x%llx: slide 0x%llx)\n", static_cast<void*>(image.data()),
                static_cast<unsigned long long>(low), static_cast<unsigned long long>(slide));
  }
  for (int i = 3; i < argc; ++i) {
    uint64_t value = 0;
    const int matches = xd_macho::find_variable(table, argv[i], value);
    std::printf("%s %d %llx\n", argv[i], matches, static_cast<unsigned long long>(value));
  }
  return 0;
}
