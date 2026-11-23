#pragma once
#include <chrono>

#include "n3ds_pose.h"
#include "species_dump.h"

namespace n3ds_test {
inline void dump_warp(const RomFs& r, int group, int slot, int xd_len) {
  Detected d = detect(r);
  Garc g = garc_at(r, d.models);
  size_t base = 1 + 9 * (size_t)group;
  GfModel gm = parse_model(pc_split(lz11_maybe(g.read(base)))[0]);
  GfMotion mo;
  if (!parse_motion(pc_split(lz11_maybe(g.read(base + 4))).at(slot), mo)) throw Fail("no motion");
  auto t0 = std::chrono::steady_clock::now();
  Skeleton sk(gm.bones);
  auto e = motion_energy(sk, mo);
  printf("energy %zu %08x\n", e.size(), crcd(e));
  printf("values");
  for (double x : e) printf(" %.17g", x);
  printf("\n");
  for (const char* role : {"idle", "phys", "special", "hit", "faint"}) {
    auto f = role_warp(role, std::max(mo.frames, 1), xd_len, e);
    printf("%s %zu %08x\n", role, f.size(), crcd(f));
  }
  fprintf(stderr, "sec %.4f\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}
}
