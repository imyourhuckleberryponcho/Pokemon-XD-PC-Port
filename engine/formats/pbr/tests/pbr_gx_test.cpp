#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pbr_gx_tex.h"

using Bytes = std::vector<uint8_t>;

static bool rd(FILE* f, void* p, size_t n) { return fread(p, 1, n, f) == n; }
static bool blob(FILE* f, Bytes& b) {
  uint32_t n;
  if (!rd(f, &n, 4)) return false;
  b.resize(n);
  return n == 0 || rd(f, b.data(), n);
}

static size_t first_diff(const Bytes& a, const Bytes& b) {
  size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; i++)
    if (a[i] != b[i]) return i;
  return n;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: pbr_gx_test <cases.bin>\n"); return 2; }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
  int cases = 0, bad = 0;
  const char* names[6] = {"decode", "cmpr", "rgb5a3", "ia8", "resize", "cmpr(resized)"};
  int fails[6] = {0}, totals[6] = {0};
  long long cmpr_blocks = 0, cmpr_diff_blocks = 0;
  for (;; cases++) {
    int32_t fmt, w, h;
    uint32_t len;
    if (!rd(f, &fmt, 4)) break;
    rd(f, &w, 4); rd(f, &h, 4); rd(f, &len, 4);
    Bytes data(len), dec, cm, r5, ia, rs, rcm;
    rd(f, data.data(), len);
    blob(f, dec); blob(f, cm); blob(f, r5); blob(f, ia);
    int32_t nw, nh;
    rd(f, &nw, 4); rd(f, &nh, 4);
    blob(f, rs); blob(f, rcm);
    pbr::Rgba img;
    bool ok[6] = {true, true, true, true, true, true};
    if (fmt < 0) { img = pbr::Rgba(w, h); img.px = data; }
    else img = pbr::gx_decode(fmt, w, h, data.data(), data.size());
    ok[0] = img.px == dec;
    ok[1] = pbr::gx_encode_cmpr(img) == cm;
    ok[2] = pbr::gx_encode_rgb5a3(img) == r5;
    ok[3] = pbr::gx_encode_ia8(img) == ia;
    pbr::Rgba rz = pbr::gx_resize(img, nw, nh);
    ok[4] = rz.px == rs;
    Bytes c2 = pbr::gx_encode_cmpr(rz);
    ok[5] = c2 == rcm;
    {
      Bytes c1 = pbr::gx_encode_cmpr(img);
      for (size_t i = 0; i + 8 <= cm.size() && i + 8 <= c1.size(); i += 8) {
        cmpr_blocks++;
        if (memcmp(&c1[i], &cm[i], 8)) cmpr_diff_blocks++;
      }
    }
    for (int k = 0; k < 6; k++) {
      totals[k]++;
      if (!ok[k]) {
        fails[k]++;
        bad++;
        if (fails[k] <= 3) {
          printf("case %d (fmt %d %dx%d): %s differs", cases, fmt, w, h, names[k]);
          if (k == 4) printf(" first byte %zu", first_diff(rz.px, rs));
          printf("\n");
        }
      }
    }
  }
  fclose(f);
  for (int k = 0; k < 6; k++) printf("%-14s %d / %d identical\n", names[k], totals[k] - fails[k], totals[k]);
  printf("CMPR blocks: %lld, differing: %lld\n", cmpr_blocks, cmpr_diff_blocks);
  printf(bad ? "FAILED\n" : "ALL IDENTICAL (%d cases)\n", cases);
  return bad ? 1 : 0;
}
