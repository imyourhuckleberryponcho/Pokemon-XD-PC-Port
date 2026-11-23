#pragma once
#include <chrono>
#include <cstdio>

#include "n3ds_pose.h"

namespace n3ds_test {
using namespace n3ds;

inline void dump_pose(const RomFs& r, int group, int slot, double stretch, const char* outfile) {
  Detected d = detect(r);
  Garc g = garc_at(r, d.models);
  size_t base = 1 + 9 * (size_t)group;
  GfModel gm = parse_model(pc_split(lz11_maybe(g.read(base)))[0]);
  auto subs = pc_split(lz11_maybe(g.read(base + 4)));
  GfMotion mo;
  if (!parse_motion(subs.at(slot), mo)) throw Fail("no motion");
  auto t0 = std::chrono::steady_clock::now();
  Skeleton sk(gm.bones);
  int ns = (int)(mo.frames / stretch) + 1;
  std::vector<double> frames;
  for (int k = 0; k < ns; k++) frames.push_back(k * stretch);
  std::vector<std::vector<V3>> S, R, T;
  sk.sample_at(mo, frames, S, R, T);
  auto t1 = std::chrono::steady_clock::now();
  std::vector<double> out;
  for (auto* A : {&S, &R, &T})
    for (auto& row : *A)
      for (auto& v : row) out.insert(out.end(), v.begin(), v.end());
  std::vector<SubRef> pts;
  for (auto& me : gm.meshes)
    for (auto& sm : me.subs) pts.push_back({&sm.bone_idx, &sm.verts});
  for (int k : {0, ns - 1}) {
    auto W = sk.world(S[k], R[k], T[k]);
    for (auto& sr : pts)
      for (auto& p : skin_points(sk, W, *sr.bone_idx, *sr.verts)) out.insert(out.end(), p.begin(), p.end());
  }
  auto t2 = std::chrono::steady_clock::now();
  auto adj = ground_offsets(sk, S, R, T, pts, 0.25, 0.0);
  out.insert(out.end(), adj.begin(), adj.end());
  auto t3 = std::chrono::steady_clock::now();
  FILE* f = fopen(outfile, "wb");
  fwrite(out.data(), 8, out.size(), f);
  fclose(f);
  auto sec = [](std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };
  fprintf(stderr, "sec skeleton+sample_at %.4f  skin(2 poses) %.4f  ground_offsets %.4f  samples %d bones %d\n", sec(t0, t1), sec(t1, t2), sec(t2, t3), ns, sk.nb);
}
}
