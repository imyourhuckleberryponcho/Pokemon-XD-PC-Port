#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "pbr_pack.h"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream f(argv[1], std::ios::binary);
  const std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  size_t at = 0;
  auto u32 = [&] { const uint32_t v = in[at] | in[at + 1] << 8 | in[at + 2] << 16 | (uint32_t)in[at + 3] << 24; at += 4; return v; };
  int ok = 0, bad = 0, halved = 0;
  while (at + 8 < in.size()) {
    pbr::CryStream s;
    s.rate = u32();
    s.nsamp = u32();
    std::array<uint16_t, 16> c{};
    for (auto& x : c) { x = (uint16_t)(in[at] | in[at + 1] << 8); at += 2; }
    s.coefs.push_back(c);
    const uint32_t n = u32();
    s.data.emplace_back(in.begin() + at, in.begin() + at + n);
    at += n;
    const uint32_t hl = u32();
    const std::vector<uint8_t> h(in.begin() + at, in.begin() + at + hl);
    at += hl;
    const uint32_t bl = u32();
    const std::vector<uint8_t> b(in.begin() + at, in.begin() + at + bl);
    at += bl;
    std::vector<uint8_t> hdr, body;
    std::vector<std::string> notes;
    pbr::cry_build_xd(s, hdr, body, notes);
    bool lv = false;
    for (const auto& t : notes) lv |= t.rfind("level", 0) == 0;
    halved += lv;
    if (hdr == h && body == b) ok++; else { bad++; std::printf("DIFF case %d\n", ok + bad); }
  }
  std::printf("%d identical, %d different (%d of them with the level step)\n", ok, bad, halved);
  return bad != 0;
}
